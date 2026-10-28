// Torrador de café - firmware v0.2
// ESP32 + 2x MAX31855 (BT/ET) + SSR (resistência) + módulo de ângulo de fase (fan)
//
// REGRA DE OURO: a resistência só liga com a fan ligada (>= FAN_MIN_PCT_FOR_HEAT),
// com zero-cross presente e depois do tempo de aceleração. Essa checagem é feita
// tanto na lógica de controle quanto em setHeater(), o único ponto que escreve no pino do SSR.

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "web_page.h"

// ---------------- Termopares ----------------
struct Thermo {
  const char *name;
  int cs;
  float c = NAN;        // última leitura boa
  uint8_t fault = 0;    // bits: 1=aberto, 2=curto GND, 4=curto VCC, 0x80=sem comunicação
  uint8_t badCount = 0;
  bool valid() const { return badCount < TC_FAULT_COUNT && !isnan(c); }
};
Thermo bt{"BT", PIN_TC_CS_BT};
Thermo et{"ET", PIN_TC_CS_ET};

// ---------------- Fan (ângulo de fase) ----------------
static esp_timer_handle_t fireTimer, gateOffTimer;
static volatile int64_t zcLastUs = 0;
static volatile uint32_t zcCount = 0;
static volatile uint32_t fanDelayUs = 0;   // 0 = fan desligada
static float zcHz = 0;

// ---------------- Estado / comandos ----------------
enum Mode { MODE_MANUAL, MODE_AUTO, MODE_PROFILE };
struct {
  uint8_t fanReq = 0;      // fan pedida pelo usuário (%) - no modo perfil, vem do perfil
  bool heatOn = false;     // resistência armada
  Mode mode = MODE_MANUAL;
  uint8_t powerReq = 0;    // potência manual (%)
  float setpoint = 200;    // alvo do BT no modo auto (°C)
} cmd;

uint8_t fanOut = 0;          // fan efetivamente aplicada
float heatOutPct = 0;        // potência efetivamente aplicada
float spNow = NAN;           // alvo de BT em uso (auto/perfil)
bool heaterPin = false;
bool faultLatched = false;
String faultMsg;
const char *blockMsg = nullptr;
uint32_t fanOnSince = 0;     // quando a fan passou do mínimo
uint32_t heaterLastOnMs = 0;
bool heaterEverOn = false;
uint32_t lastClientMs = 0;
float pidInteg = 0, pidLastInput = NAN;

// ---------------- Perfil ativo ----------------
struct ProfPt { uint16_t t; float bt; uint8_t fan; };
struct Profile {
  String id, name;
  ProfPt pts[PROFILE_MAX_PTS];
  uint8_t n = 0;
} prof;

// ---------------- Registro da torra ----------------
#define NO_VAL INT16_MIN
struct LogPt { uint16_t t; int16_t bt, et, sp; uint8_t heat, fan; };   // temperaturas em décimos de °C
static LogPt roastLog[ROAST_LOG_MAX];
uint16_t roastN = 0;
uint32_t roastId = 0;          // muda a cada torra nova (a página usa para recomeçar o gráfico)
bool roasting = false;
bool profileDone = false;
uint32_t roastStartMs = 0;
uint32_t roastLastLogMs = 0;
String roastProfile;

WebServer server(80);

// =================================================================
// Fan: ISR de zero-cross agenda o disparo do TRIAC com esp_timer
// =================================================================
static void onGateOff(void *) { digitalWrite(PIN_FAN_GATE, LOW); }

static void onFire(void *) {
  digitalWrite(PIN_FAN_GATE, HIGH);
  esp_timer_start_once(gateOffTimer, GATE_PULSE_US);
}

static void IRAM_ATTR onZeroCross() {
  int64_t now = esp_timer_get_time();
  if (now - zcLastUs < ZC_DEBOUNCE_US) return;
  zcLastUs = now;
  zcCount = zcCount + 1;
  uint32_t d = fanDelayUs;
  esp_timer_stop(fireTimer);
  if (d) esp_timer_start_once(fireTimer, d);
}

static bool zcOk() {
  return esp_timer_get_time() - zcLastUs < (int64_t)ZC_TIMEOUT_MS * 1000;
}

static void setFan(uint8_t pct) {
  fanOut = pct;
  if (pct == 0) {
    fanDelayUs = 0;
  } else {
    fanDelayUs = FAN_MAX_DELAY_US - (uint32_t)(pct - 1) * (FAN_MAX_DELAY_US - FAN_MIN_DELAY_US) / 99;
  }
  if (pct >= FAN_MIN_PCT_FOR_HEAT) {
    if (!fanOnSince) fanOnSince = millis() | 1;   // |1 evita o valor 0 (= "desligada")
  } else {
    fanOnSince = 0;
  }
}

// =================================================================
// Resistência: único ponto de escrita no SSR, com a trava final
// =================================================================
static bool fanSafeForHeat() {
  return fanOut >= FAN_MIN_PCT_FOR_HEAT && fanDelayUs != 0 && zcOk() && !faultLatched;
}

static void setHeater(bool on) {
  if (on && !fanSafeForHeat()) on = false;   // trava redundante: NUNCA aquece sem fan
  digitalWrite(PIN_HEATER, on ? HIGH : LOW);
  heaterPin = on;
  if (on) {
    heaterLastOnMs = millis();
    heaterEverOn = true;
  }
}

// =================================================================
// MAX31855
// =================================================================
static void readThermo(Thermo &t) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(t.cs, LOW);
  delayMicroseconds(1);
  uint32_t v = SPI.transfer32(0);
  digitalWrite(t.cs, HIGH);
  SPI.endTransaction();

  if (v == 0 || v == 0xFFFFFFFF) {
    t.fault = 0x80;
  } else if (v & 0x10000) {
    t.fault = v & 0x07;
  } else {
    t.fault = 0;
    t.c = ((int16_t)(v >> 16) >> 2) * 0.25f;   // 14 bits com sinal, 0,25 °C/LSB
    t.badCount = 0;
    return;
  }
  // Falha: tolera algumas leituras ruins (ruído do chaveamento) antes de invalidar
  if (t.badCount < 255) t.badCount++;
}

static const char *thermoFaultStr(const Thermo &t) {
  if (t.valid()) return "";
  if (t.fault & 0x80) return "sem comunicação";
  if (t.fault & 0x01) return "termopar aberto";
  if (t.fault & 0x02) return "curto com GND";
  if (t.fault & 0x04) return "curto com VCC";
  return "sem leitura";
}

// =================================================================
// Perfis (LittleFS, /p/<id>.csv)
// Formato: "#name=<nome>" e depois uma linha "segundos,bt,fan" por ponto
// =================================================================
static String profPath(const String &id) { return "/p/" + id + ".csv"; }

static bool validId(const String &id) {
  if (!id.length() || id.length() > 24) return false;
  for (char c : id)
    if (!(isdigit((uint8_t)c) || (c >= 'a' && c <= 'z') || c == '-')) return false;
  return true;
}

static String cleanName(const String &n) {
  String o;
  for (char c : n)
    if ((uint8_t)c >= 32 && c != '"' && c != '\\') o += c;
  o.trim();
  if (o.length() > 40) o = o.substring(0, 40);
  return o;
}

// Letra sem acento para um caractere UTF-8 "Ã + x" (Latin-1), ou 0 se não for letra acentuada
static char unaccent(uint8_t x) {
  static const char map[] = "aaaaaaaceeeeiiiidnooooo/ouuuuyby";   // 0x80-0x9F e 0xA0-0xBF dão a mesma letra
  if (x < 0x80 || x > 0xBF) return 0;
  char c = map[x & 0x1F];
  return c == '/' ? 0 : c;
}

static String slugify(const String &n) {
  String s;
  for (unsigned i = 0; i < n.length(); i++) {
    char c = n[i];
    if ((uint8_t)c == 0xC3 && i + 1 < n.length()) {
      char u = unaccent(n[++i]);
      if (u) c = u;
    }
    if (isalnum((uint8_t)c)) s += (char)tolower(c);
    else if (s.length() && s[s.length() - 1] != '-') s += '-';
  }
  if (s.length() > 24) s = s.substring(0, 24);
  while (s.endsWith("-")) s.remove(s.length() - 1);
  return s.length() ? s : String("perfil");
}

static bool parseProfile(const String &txt, Profile &p, String &err) {
  p.n = 0;
  int start = 0;
  while (start < (int)txt.length()) {
    int end = txt.indexOf('\n', start);
    if (end < 0) end = txt.length();
    String line = txt.substring(start, end);
    line.trim();
    start = end + 1;
    if (!line.length()) continue;
    if (line.startsWith("#name=")) { p.name = cleanName(line.substring(6)); continue; }
    if (line[0] == '#') continue;

    int t, fan;
    float temp;
    if (sscanf(line.c_str(), "%d,%f,%d", &t, &temp, &fan) != 3) { err = "Linha inválida: " + line; return false; }
    if (p.n >= PROFILE_MAX_PTS) { err = "Máximo de " + String(PROFILE_MAX_PTS) + " pontos"; return false; }
    if (t < 0 || t > ROAST_MAX_S) { err = "Tempo fora da faixa (0 a " + String(ROAST_MAX_S / 60) + " min)"; return false; }
    if (p.n && t <= p.pts[p.n - 1].t) { err = "Os tempos precisam ser crescentes"; return false; }
    if (temp < 0 || temp > BT_MAX_C - 10) { err = "BT precisa estar entre 0 e " + String(BT_MAX_C - 10, 0) + " °C"; return false; }
    if (fan < FAN_MIN_PCT_FOR_HEAT || fan > 100) { err = "Fan precisa estar entre " + String(FAN_MIN_PCT_FOR_HEAT) + " e 100 %"; return false; }
    p.pts[p.n++] = {(uint16_t)t, temp, (uint8_t)fan};
  }
  if (p.n < 2) { err = "O perfil precisa de pelo menos 2 pontos"; return false; }
  return true;
}

static String profileToText(const Profile &p) {
  String s = "#name=" + p.name + "\n";
  for (uint8_t i = 0; i < p.n; i++)
    s += String(p.pts[i].t) + "," + String(p.pts[i].bt, 1) + "," + String(p.pts[i].fan) + "\n";
  return s;
}

static bool loadProfileFile(const String &id, Profile &p) {
  File f = LittleFS.open(profPath(id), "r");
  if (!f) return false;
  String txt = f.readString();
  String err;
  p.id = id;
  p.name = id;
  return parseProfile(txt, p, err);
}

// BT alvo e fan do perfil no instante t (interpolação linear entre os pontos)
static float profileAt(float t, uint8_t *fan) {
  const ProfPt *a = prof.pts, *b = prof.pts;
  for (uint8_t i = 1; i < prof.n && prof.pts[i - 1].t < t; i++) { a = &prof.pts[i - 1]; b = &prof.pts[i]; }
  if (t <= a->t || a == b) { *fan = a->fan; return a->bt; }
  float k = (t - a->t) / (float)(b->t - a->t);
  if (k > 1) k = 1;
  *fan = a->fan + (int)roundf(k * (b->fan - a->fan));
  return a->bt + k * (b->bt - a->bt);
}

// =================================================================
// Torra (registro em RAM, última torra salva em /last.csv)
// =================================================================
static int16_t toLog(float v) { return isnan(v) ? NO_VAL : (int16_t)lroundf(v * 10); }

static void appendLogVal(String &s, int16_t v) {
  if (v != NO_VAL) s += String(v / 10.0f, 1);
}

static void appendLogRow(String &s, const LogPt &p) {
  s += p.t; s += ',';
  appendLogVal(s, p.bt); s += ',';
  appendLogVal(s, p.et); s += ',';
  appendLogVal(s, p.sp); s += ',';
  s += p.heat; s += ',';
  s += p.fan; s += '\n';
}

static void logSample() {
  if (roastN >= ROAST_LOG_MAX) return;
  roastLog[roastN++] = {
    (uint16_t)((millis() - roastStartMs) / 1000),
    toLog(bt.valid() ? bt.c : NAN),
    toLog(USE_ET && et.valid() ? et.c : NAN),
    toLog(spNow),
    (uint8_t)lroundf(heatOutPct),
    fanOut,
  };
}

static void saveLastRoast() {
  File f = LittleFS.open("/last.csv", "w");
  if (!f) { Serial.println("[ERRO] não consegui salvar /last.csv"); return; }
  f.printf("#profile=%s\n#duration=%u\nt,bt,et,sp,heat,fan\n", roastProfile.c_str(), roastLog[roastN - 1].t);
  String chunk;
  for (uint16_t i = 0; i < roastN; i++) {
    appendLogRow(chunk, roastLog[i]);
    if (chunk.length() > 2048) {
      f.print(chunk);
      chunk = "";
      esp_task_wdt_reset();   // gravar uma torra longa pode levar alguns segundos
    }
  }
  f.print(chunk);
  f.close();
  Serial.printf("[TORRA] %u pontos salvos em /last.csv\n", roastN);
}

static void startRoast() {
  roasting = true;
  profileDone = false;
  roastStartMs = millis();
  roastLastLogMs = roastStartMs;
  roastN = 0;
  roastId++;
  roastProfile = cmd.mode == MODE_PROFILE ? prof.name : String();
  pidInteg = 0;
  pidLastInput = NAN;
  if (cmd.mode == MODE_PROFILE) {
    cmd.fanReq = prof.pts[0].fan;
    cmd.heatOn = true;   // as travas continuam valendo: se algo estiver errado, desarma no próximo ciclo
  }
  logSample();
  Serial.printf("[TORRA] início #%u %s\n", (unsigned)roastId, roastProfile.c_str());
}

static void stopRoast(const char *why) {
  if (!roasting) return;
  roasting = false;
  cmd.heatOn = false;
  heatOutPct = 0;
  setHeater(false);   // desliga antes de gravar na flash
  logSample();
  Serial.printf("[TORRA] fim: %s (%u pontos)\n", why, roastN);
  if (roastN >= ROAST_MIN_SAVE_PTS) saveLastRoast();
}

// =================================================================
// Lógica de segurança e controle
// =================================================================
static void checkOverTemp() {
  if (faultLatched) return;
  char buf[80];
  if (bt.valid() && bt.c > BT_MAX_C) {
    snprintf(buf, sizeof buf, "Sobretemperatura BT: %.1f °C (máx %.0f)", bt.c, BT_MAX_C);
  } else if (et.valid() && et.c > ET_MAX_C) {
    snprintf(buf, sizeof buf, "Sobretemperatura ET: %.1f °C (máx %.0f)", et.c, ET_MAX_C);
  } else {
    return;
  }
  faultLatched = true;
  faultMsg = buf;
  cmd.heatOn = false;
  Serial.printf("[FALHA] %s\n", buf);
}

static bool coolingRequired() {
  if (heaterEverOn && millis() - heaterLastOnMs < COOLDOWN_MIN_MS) return true;
  if (bt.valid() && bt.c > COOLDOWN_TEMP_C) return true;
  if (et.valid() && et.c > COOLDOWN_TEMP_C) return true;
  return false;
}

// Retorna o motivo que impede o aquecimento (nullptr = liberado).
// *disarm = true quando o motivo deve desarmar a resistência (usuário precisa religar).
static const char *heatBlockReason(bool *disarm) {
  *disarm = true;
  if (faultLatched) return "Falha travada - veja o aviso";
  // Usa o pedido do usuário (não o mínimo forçado pelo resfriamento), senão a resistência
  // poderia ligar "pendurada" na fan de resfriamento que some assim que ela liga.
  if (cmd.fanReq < FAN_MIN_PCT_FOR_HEAT || fanOut < FAN_MIN_PCT_FOR_HEAT)
    return "Fan abaixo do mínimo para aquecer";
  if (!zcOk()) return "Sem zero-cross: módulo da fan sem rede/sinal";
  if (REQUIRE_SENSOR_FOR_HEAT && !bt.valid() && !et.valid()) return "Nenhum termopar válido";
  if (cmd.mode != MODE_MANUAL && !bt.valid()) return "Controle por temperatura precisa do BT";
  if (cmd.mode == MODE_PROFILE) {
    if (!prof.n) return "Nenhum perfil carregado";
    if (!roasting) return "Inicie a torra para seguir o perfil";
    if (profileDone) return "Perfil terminou";
  }
  if (millis() - lastClientMs > CLIENT_TIMEOUT_MS) return "Página sem comunicação";
  *disarm = false;
  if (millis() - fanOnSince < FAN_SPINUP_MS) return "Aguardando a fan acelerar";
  return nullptr;
}

static float pidStep(float dtS) {
  float in = bt.c;
  float err = spNow - in;
  pidInteg += PID_KI * err * dtS;
  pidInteg = constrain(pidInteg, 0.0f, 100.0f);
  float d = isnan(pidLastInput) ? 0 : (in - pidLastInput) / dtS;
  pidLastInput = in;
  return constrain(PID_KP * err + pidInteg - PID_KD * d, 0.0f, 100.0f);
}

static uint32_t roastSeconds() { return roasting ? (millis() - roastStartMs) / 1000 : 0; }

static void controlLoop() {
  uint32_t now = millis();

  // Leitura dos termopares a cada 250 ms (conversão do MAX31855 ~100 ms)
  static uint32_t lastRead = 0;
  if (now - lastRead >= 250) {
    lastRead = now;
    readThermo(bt);
    if (USE_ET) readThermo(et);
    checkOverTemp();
  }

  // Frequência do zero-cross (deve dar ~120 Hz em 60 Hz)
  static uint32_t lastHz = 0, lastZc = 0;
  if (now - lastHz >= 1000) {
    uint32_t c = zcCount;
    zcHz = (c - lastZc) * 1000.0f / (now - lastHz);
    lastZc = c;
    lastHz = now;
  }

  if (roasting && roastSeconds() >= ROAST_MAX_S) stopRoast("tempo máximo atingido");

  // Alvo de temperatura (e fan, no modo perfil)
  spNow = NAN;
  if (cmd.mode == MODE_AUTO) {
    spNow = cmd.setpoint;
  } else if (cmd.mode == MODE_PROFILE && roasting && prof.n && !profileDone) {
    float t = (now - roastStartMs) / 1000.0f;
    if (t > prof.pts[prof.n - 1].t) {
      profileDone = true;
      cmd.heatOn = false;
      Serial.println("[TORRA] perfil terminou, resistência desarmada");
    } else {
      spNow = profileAt(t, &cmd.fanReq);
    }
  }

  // Fan: o pedido, mas com mínimo forçado em falha / resfriamento
  uint8_t fan = cmd.fanReq;
  if (faultLatched) fan = 100;
  else if (coolingRequired() && fan < COOLDOWN_FAN_PCT) fan = COOLDOWN_FAN_PCT;
  if (fan != fanOut) setFan(fan);

  // Travas
  bool disarm;
  blockMsg = heatBlockReason(&disarm);
  if (blockMsg && disarm && cmd.heatOn) {
    cmd.heatOn = false;
    Serial.printf("[TRAVA] resistência desarmada: %s\n", blockMsg);
  }

  // Potência
  static uint32_t lastPid = 0;
  if (!cmd.heatOn || blockMsg) {
    heatOutPct = 0;
    pidInteg = 0;
    pidLastInput = NAN;
    lastPid = now;
  } else if (cmd.mode == MODE_MANUAL) {
    heatOutPct = cmd.powerReq;
  } else if (now - lastPid >= HEATER_WINDOW_MS) {
    heatOutPct = pidStep((now - lastPid) / 1000.0f);
    lastPid = now;
  }

  // PWM lento do SSR
  uint32_t pos = now % HEATER_WINDOW_MS;
  setHeater(heatOutPct > 0 && pos < heatOutPct * HEATER_WINDOW_MS / 100.0f);

  if (roasting && now - roastLastLogMs >= 1000) {
    roastLastLogMs += 1000;
    logSample();
  }
}

// =================================================================
// Web
// =================================================================
static String jsonStr(const String &s) { return "\"" + s + "\""; }   // nomes já vêm sem " e \ (cleanName)

static String jsonNum(float v, int dec) { return isnan(v) ? String("null") : String(v, dec); }

static void appendTemp(String &s, const char *key, const Thermo &t) {
  s += '"'; s += key; s += "\":";
  s += jsonNum(t.valid() ? t.c : NAN, 2);
  s += ",\""; s += key; s += "_err\":\""; s += thermoFaultStr(t); s += "\",";
}

static const char *modeName(Mode m) {
  return m == MODE_AUTO ? "auto" : m == MODE_PROFILE ? "profile" : "manual";
}

static void handleState() {
  lastClientMs = millis();
  String s = "{";
  appendTemp(s, "bt", bt);
  appendTemp(s, "et", et);
  s += "\"et_on\":" + String(USE_ET ? "true" : "false");
  s += ",\"fan_req\":" + String(cmd.fanReq);
  s += ",\"fan\":" + String(fanOut);
  s += ",\"zc_hz\":" + String(zcHz, 1);
  s += ",\"heat_on\":" + String(cmd.heatOn ? "true" : "false");
  s += ",\"mode\":\"" + String(modeName(cmd.mode)) + "\"";
  s += ",\"power_req\":" + String(cmd.powerReq);
  s += ",\"sp\":" + String(cmd.setpoint, 1);
  s += ",\"sp_now\":" + jsonNum(spNow, 1);
  s += ",\"heat\":" + String(heatOutPct, 1);
  s += ",\"ssr\":" + String(heaterPin ? "true" : "false");
  s += ",\"cooling\":" + String(coolingRequired() ? "true" : "false");
  s += ",\"block\":\"" + String(blockMsg ? blockMsg : "") + "\"";
  s += ",\"fault\":\"" + (faultLatched ? faultMsg : String()) + "\"";
  s += ",\"fan_min\":" + String(FAN_MIN_PCT_FOR_HEAT);
  s += ",\"bt_max\":" + String(BT_MAX_C - 10, 0);
  s += ",\"roasting\":" + String(roasting ? "true" : "false");
  s += ",\"roast_t\":" + String(roastSeconds());
  s += ",\"roast_n\":" + String(roastN);
  s += ",\"roast_id\":" + String(roastId);
  s += ",\"prof_id\":" + jsonStr(prof.n ? prof.id : String());
  s += ",\"prof_name\":" + jsonStr(prof.n ? prof.name : String());
  s += ",\"prof_done\":" + String(profileDone ? "true" : "false");
  s += ",\"uptime\":" + String(millis() / 1000);
  s += "}";
  server.send(200, "application/json", s);
}

static void handleCmd() {
  lastClientMs = millis();
  bool profileRunning = cmd.mode == MODE_PROFILE && roasting;
  if (server.hasArg("fan") && !profileRunning) cmd.fanReq = constrain(server.arg("fan").toInt(), 0, 100);
  if (server.hasArg("power")) cmd.powerReq = constrain(server.arg("power").toInt(), 0, 100);
  if (server.hasArg("sp")) cmd.setpoint = constrain(server.arg("sp").toFloat(), 0.0f, BT_MAX_C - 10);
  if (server.hasArg("mode")) {
    String a = server.arg("mode");
    Mode m = a == "auto" ? MODE_AUTO : a == "profile" ? MODE_PROFILE : MODE_MANUAL;
    if (m != cmd.mode) {
      if (m == MODE_PROFILE && roasting) {
        server.send(409, "text/plain", "Finalize a torra atual antes de mudar para o modo perfil");
        return;
      }
      cmd.mode = m;
      pidInteg = 0;
      pidLastInput = NAN;
    }
  }
  if (server.hasArg("heat")) {
    bool want = server.arg("heat") == "1";
    if (want) {
      bool disarm;
      const char *why = heatBlockReason(&disarm);
      if (why && disarm) {   // recusa armar se alguma trava "dura" estiver ativa
        server.send(409, "text/plain", why);
        return;
      }
    }
    cmd.heatOn = want;
  }
  handleState();
}

static void handleStop() {
  cmd.heatOn = false;
  cmd.powerReq = 0;
  setHeater(false);
  Serial.println("[PARADA] resistência desligada pelo usuário");
  handleState();
}

static void handleReset() {
  bool hot = (bt.valid() && bt.c > BT_MAX_C) || (et.valid() && et.c > ET_MAX_C);
  if (hot) {
    server.send(409, "text/plain", "Ainda acima do limite, aguarde esfriar");
    return;
  }
  faultLatched = false;
  faultMsg = "";
  handleState();
}

static void handleRoastStart() {
  lastClientMs = millis();
  if (roasting) { server.send(409, "text/plain", "Já existe uma torra em andamento"); return; }
  if (cmd.mode == MODE_PROFILE && !prof.n) { server.send(409, "text/plain", "Escolha um perfil antes de iniciar"); return; }
  startRoast();
  handleState();
}

static void handleRoastStop() {
  stopRoast("finalizada pelo usuário");
  handleState();
}

// Registro da torra atual em CSV (from = primeiro índice, para a página buscar só o que falta)
static void handleRoastLog() {
  uint32_t from = server.hasArg("from") ? server.arg("from").toInt() : 0;
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  String chunk = "#id=" + String(roastId) + "\n#profile=" + roastProfile + "\n#from=" + String(from) + "\n";
  for (uint32_t i = from; i < roastN; i++) {
    appendLogRow(chunk, roastLog[i]);
    if (chunk.length() > 2048) {
      server.sendContent(chunk);
      chunk = "";
    }
  }
  server.sendContent(chunk);
  server.sendContent("");
}

static void handleLastRoast() {
  File f = LittleFS.open("/last.csv", "r");
  if (!f) { server.send(404, "text/plain", "Nenhuma torra salva ainda"); return; }
  if (server.hasArg("dl")) server.sendHeader("Content-Disposition", "attachment; filename=ultima-torra.csv");
  server.streamFile(f, "text/csv");
}

static void handleProfiles() {
  String s = "{\"active\":" + jsonStr(prof.n ? prof.id : String()) + ",\"items\":[";
  File dir = LittleFS.open("/p");
  bool first = true;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String fn = f.name();
    if (!fn.endsWith(".csv")) continue;
    String id = fn.substring(0, fn.length() - 4);
    String line = f.readStringUntil('\n');
    String name = line.startsWith("#name=") ? cleanName(line.substring(6)) : id;
    if (!first) s += ',';
    first = false;
    s += "{\"id\":" + jsonStr(id) + ",\"name\":" + jsonStr(name) + "}";
  }
  s += "]}";
  server.send(200, "application/json", s);
}

static void handleProfileGet() {
  String id = server.arg("id");
  File f;
  if (validId(id)) f = LittleFS.open(profPath(id), "r");
  if (!f) { server.send(404, "text/plain", "Perfil não encontrado"); return; }
  server.streamFile(f, "text/plain");
}

static uint32_t countProfiles() {
  uint32_t n = 0;
  File dir = LittleFS.open("/p");
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) n++;
  return n;
}

static void handleProfileSave() {
  Profile p;
  String err;
  if (!parseProfile(server.arg("data"), p, err)) { server.send(400, "text/plain", err); return; }
  p.name = cleanName(server.arg("name"));
  if (!p.name.length()) { server.send(400, "text/plain", "Dê um nome ao perfil"); return; }
  p.id = slugify(p.name);
  bool exists = LittleFS.exists(profPath(p.id));
  if (!exists && countProfiles() >= PROFILE_MAX_FILES) {
    server.send(409, "text/plain", "Limite de " + String(PROFILE_MAX_FILES) + " perfis atingido, exclua algum");
    return;
  }
  bool isActive = prof.n && prof.id == p.id;
  if (isActive && roasting && cmd.mode == MODE_PROFILE) {
    server.send(409, "text/plain", "Este perfil está sendo usado na torra atual");
    return;
  }
  File f = LittleFS.open(profPath(p.id), "w");
  if (!f) { server.send(500, "text/plain", "Falha ao gravar na flash"); return; }
  f.print(profileToText(p));
  f.close();
  if (isActive) prof = p;
  server.send(200, "application/json", "{\"id\":" + jsonStr(p.id) + "}");
}

static void handleProfileDelete() {
  String id = server.arg("id");
  if (!validId(id) || !LittleFS.exists(profPath(id))) { server.send(404, "text/plain", "Perfil não encontrado"); return; }
  if (prof.n && prof.id == id) {
    if (roasting && cmd.mode == MODE_PROFILE) {
      server.send(409, "text/plain", "Este perfil está sendo usado na torra atual");
      return;
    }
    prof.n = 0;
    LittleFS.remove("/active.txt");
  }
  LittleFS.remove(profPath(id));
  handleProfiles();
}

static void handleProfileUse() {
  String id = server.arg("id");
  if (roasting && cmd.mode == MODE_PROFILE) {
    server.send(409, "text/plain", "Finalize a torra antes de trocar de perfil");
    return;
  }
  Profile p;
  if (!validId(id) || !loadProfileFile(id, p)) { server.send(404, "text/plain", "Perfil não encontrado ou inválido"); return; }
  prof = p;
  File f = LittleFS.open("/active.txt", "w");
  if (f) { f.print(id); f.close(); }
  handleState();
}

static void setupWifi() {
  WiFi.setHostname(HOSTNAME);
  if (strlen(WIFI_SSID)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("Conectando em %s", WIFI_SSID);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
      delay(250);
      Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("IP: http://%s  ou  http://%s.local\n", WiFi.localIP().toString().c_str(), HOSTNAME);
      MDNS.begin(HOSTNAME);
      return;
    }
    Serial.println("Falhou, subindo modo AP");
  }
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("AP \"%s\" (senha %s) -> http://%s\n", AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
}

static void setupStorage() {
  if (!LittleFS.begin(true)) {   // true = formata na primeira vez
    Serial.println("[ERRO] LittleFS indisponível: perfis e última torra não serão salvos");
    return;
  }
  if (!LittleFS.exists("/p")) LittleFS.mkdir("/p");
  File f = LittleFS.open("/active.txt", "r");
  if (f) {
    String id = f.readString();
    id.trim();
    Profile p;
    if (validId(id) && loadProfileFile(id, p)) {
      prof = p;
      Serial.printf("Perfil ativo: %s\n", prof.name.c_str());
    }
  }
}

// =================================================================
void setup() {
  // Saídas de potência em LOW antes de qualquer outra coisa
  pinMode(PIN_HEATER, OUTPUT);
  digitalWrite(PIN_HEATER, LOW);
  pinMode(PIN_FAN_GATE, OUTPUT);
  digitalWrite(PIN_FAN_GATE, LOW);

  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Torrador v0.2 ===");

  pinMode(PIN_TC_CS_BT, OUTPUT);
  digitalWrite(PIN_TC_CS_BT, HIGH);
  pinMode(PIN_TC_CS_ET, OUTPUT);
  digitalWrite(PIN_TC_CS_ET, HIGH);
  SPI.begin(PIN_TC_SCK, PIN_TC_MISO, -1, -1);

  esp_timer_create_args_t fireArgs = {};
  fireArgs.callback = onFire;
  fireArgs.name = "fan_fire";
  esp_timer_create(&fireArgs, &fireTimer);
  esp_timer_create_args_t offArgs = {};
  offArgs.callback = onGateOff;
  offArgs.name = "fan_gate_off";
  esp_timer_create(&offArgs, &gateOffTimer);

  pinMode(PIN_ZC, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ZC), onZeroCross, RISING);

  setupStorage();
  setupWifi();

  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/cmd", HTTP_POST, handleCmd);
  server.on("/api/stop", HTTP_POST, handleStop);
  server.on("/api/reset", HTTP_POST, handleReset);
  server.on("/api/roast/start", HTTP_POST, handleRoastStart);
  server.on("/api/roast/stop", HTTP_POST, handleRoastStop);
  server.on("/api/roast", HTTP_GET, handleRoastLog);
  server.on("/api/last", HTTP_GET, handleLastRoast);
  server.on("/api/profiles", HTTP_GET, handleProfiles);
  server.on("/api/profile", HTTP_GET, handleProfileGet);
  server.on("/api/profile/save", HTTP_POST, handleProfileSave);
  server.on("/api/profile/delete", HTTP_POST, handleProfileDelete);
  server.on("/api/profile/use", HTTP_POST, handleProfileUse);
  server.onNotFound([] { server.send(404, "text/plain", "404"); });
  server.begin();

  // Watchdog: se o loop travar, o chip reinicia e as saídas voltam a LOW
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t wdt = {};
  wdt.timeout_ms = WDT_TIMEOUT_S * 1000;
  wdt.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif
  esp_task_wdt_add(NULL);
}

void loop() {
  esp_task_wdt_reset();
  controlLoop();
  server.handleClient();

  static uint32_t lastLog = 0;
  if (millis() - lastLog >= 1000) {
    lastLog = millis();
    if (USE_ET) Serial.printf("BT %6.1f  ET %6.1f  ", bt.c, et.c);
    else Serial.printf("BT %6.1f  ", bt.c);
    Serial.printf("fan %3u%%  ZC %5.1f Hz  heat %5.1f%%  %s%s\n",
                  fanOut, zcHz, heatOutPct, roasting ? "[torra] " : "", blockMsg ? blockMsg : "");
  }
  delay(1);
}

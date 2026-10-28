#pragma once

// ================= Wi-Fi =================
// Os valores reais vêm do arquivo .env (lido por scripts/load_env.py na compilação).
// Os abaixo são só os padrões usados quando o .env não existe ou não define a variável.
// WIFI_SSID vazio -> o ESP32 cria a própria rede (modo AP) e a página fica em http://192.168.4.1
// Preenchido -> tenta conectar na rede; se falhar em 15 s, cai para o modo AP.
#ifndef WIFI_SSID
#define WIFI_SSID     ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS     ""
#endif
#ifndef AP_SSID
#define AP_SSID       "Torrador"
#endif
#ifndef AP_PASS
#define AP_PASS       "torra1234"      // mínimo 8 caracteres
#endif
#ifndef HOSTNAME
#define HOSTNAME      "torrador"       // http://torrador.local (modo estação)
#endif

// ================= Pinos =================
#if CONFIG_IDF_TARGET_ESP32S3
// ESP32-S3 DevKitC-1. Evitados: 0, 3, 45, 46 (strapping), 19/20 (USB), 26-37 (flash/PSRAM)
#define PIN_TC_SCK    12   // SCK dos dois MAX31855 (barramento compartilhado)
#define PIN_TC_MISO   13   // SO  dos dois MAX31855
#define PIN_TC_CS_BT  10   // CS do termopar BT (grão)
#define PIN_TC_CS_ET  9    // CS do termopar ET (ar)
#define PIN_ZC        4    // saída zero-cross do módulo de ângulo de fase
#define PIN_FAN_GATE  5    // entrada de disparo do TRIAC (fan)
#define PIN_HEATER    6    // entrada do SSR da resistência (use pull-down de 10k no hardware!)
#else
// ESP32 clássico (DevKit WROOM-32). Evitados: 6-11 (flash), 0, 2, 5, 12, 15 (strapping),
// 34-39 (só entrada, sem pull-up interno)
#define PIN_TC_SCK    18   // SCK dos dois MAX31855 (barramento compartilhado)
#define PIN_TC_MISO   19   // SO  dos dois MAX31855
#define PIN_TC_CS_BT  21   // CS do termopar BT (grão)
#define PIN_TC_CS_ET  22   // CS do termopar ET (ar)
#define PIN_ZC        27   // saída zero-cross do módulo de ângulo de fase
#define PIN_FAN_GATE  25   // entrada de disparo do TRIAC (fan)
#define PIN_HEATER    26   // entrada do SSR da resistência (use pull-down de 10k no hardware!)
#endif

// ================= Rede elétrica / fan (ângulo de fase) =================
#define HALF_CYCLE_US      8333   // 60 Hz -> semiciclo de 8,33 ms
#define ZC_DEBOUNCE_US     4000   // ignora pulsos de zero-cross mais próximos que isso
#define ZC_TIMEOUT_MS      100    // sem zero-cross por esse tempo -> fan considerada parada
#define FAN_MIN_DELAY_US   300    // atraso de disparo em 100 %
#define FAN_MAX_DELAY_US   7800   // atraso de disparo em 1 %
#define GATE_PULSE_US      200    // largura do pulso no gate (carga indutiva pede pulso longo)

// ================= Resistência (SSR, controle proporcional no tempo) =================
#define HEATER_WINDOW_MS   1000   // janela do PWM lento do SSR

// PID (modo automático, controla pelo BT). Unidades: % de potência por °C.
#define PID_KP   5.0f
#define PID_KI   0.05f
#define PID_KD   0.0f

// ================= Torra / perfis =================
#define ROAST_LOG_MAX          3600    // pontos do registro (1 por segundo -> 60 min)
#define ROAST_MAX_S            3600    // torra é encerrada automaticamente após esse tempo
#define ROAST_MIN_SAVE_PTS     30      // torras com menos pontos que isso não sobrescrevem a última salva
#define PROFILE_MAX_PTS        24      // pontos por perfil
#define PROFILE_MAX_FILES      30      // perfis salvos na flash

// ================= Travas de segurança =================
#define FAN_MIN_PCT_FOR_HEAT   40      // fan abaixo disso -> resistência NUNCA liga
#define FAN_SPINUP_MS          2000    // fan precisa estar ligada há esse tempo antes de aquecer
#define BT_MAX_C               250.0f  // sobretemperatura no grão -> desliga e trava
#define ET_MAX_C               290.0f  // sobretemperatura no ar   -> desliga e trava
#define USE_ET                 0       // 0 = só o termopar do grão (BT); 1 = BT + ET
#define REQUIRE_SENSOR_FOR_HEAT 1      // 1 = exige pelo menos um termopar válido para aquecer
#define TC_FAULT_COUNT         3       // leituras ruins seguidas antes de declarar o termopar com falha
#define CLIENT_TIMEOUT_MS      10000   // página sem comunicação -> resistência desarma
#define COOLDOWN_TEMP_C        60.0f   // acima disso a fan não desliga (resfriamento)
#define COOLDOWN_FAN_PCT       60      // fan mínima durante o resfriamento
#define COOLDOWN_MIN_MS        30000   // após desligar a resistência, fan fica ligada no mínimo esse tempo
#define WDT_TIMEOUT_S          3       // loop travado -> reinicia (saídas voltam a LOW)

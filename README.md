# Torrador de café: firmware do ESP32

Firmware para controlar uma **pipoqueira modificada usada como torrador de café**. Um ESP32 controla a
fan (por ângulo de fase) e a resistência (por SSR), lê a temperatura do grão por termopar e serve uma
página web pelo Wi-Fi para operar o torrador pelo celular ou pelo computador.


> Leia a seção [Segurança](#segurança) antes de ligar na tomada.

---

## Sumário

- [Hardware](#hardware)
- [Pinagem](#pinagem)
- [Ligações](#ligações)
- [Segurança](#segurança)
- [Como compilar e gravar](#como-compilar-e-gravar)
- [Usando a página](#usando-a-página)
- [Perfis de torra](#perfis-de-torra)
- [Configurações (`config.h`)](#configurações-configh)
- [API HTTP](#api-http)
- [Problemas comuns](#problemas-comuns)
- [Estrutura do projeto](#estrutura-do-projeto)

---

## Hardware

| Item | Função |
|---|---|
| ESP32 DevKit (WROOM-32) | Controle e servidor web. **É a placa atual do projeto.** O ESP32-S3 também é suportado (veja a pinagem). |
| MAX31855 (×1 por enquanto, até 2) | Leitura do termopar tipo K. **BT** = grão (instalado), **ET** = ar (opcional, futuro). |
| Termopar tipo K | Sonda de temperatura. |
| SSR (relé de estado sólido) | Liga e desliga a **resistência**. Use um SSR com chaveamento em zero-cross (o comum para cargas resistivas). |
| Módulo de ângulo de fase (dimmer AC com TRIAC + saída zero-cross) | Controla a velocidade da **fan** e informa ao ESP32 a passagem por zero da rede. |
| Disjuntor + fusível | Proteção da entrada da rede. |

---

## Pinagem

O firmware escolhe a pinagem sozinho conforme a placa para a qual foi compilado (`src/config.h`).

### ESP32 DevKit (WROOM-32): placa atual

| Sinal | GPIO | Liga em | Direção |
|---|---|---|---|
| SCK dos termopares | **18** | `SCK` (ou `CLK`) do MAX31855: os dois módulos no mesmo pino | saída |
| Dados dos termopares | **19** | `SO` (ou `DO`/`MISO`) do MAX31855: os dois módulos no mesmo pino | entrada |
| CS do termopar **BT** (grão) | **21** | `CS` do MAX31855 do grão | saída |
| CS do termopar **ET** (ar) | **22** | `CS` do MAX31855 do ar (só quando `USE_ET = 1`) | saída |
| Zero-cross | **27** | Saída `Z-C` do módulo de ângulo de fase | entrada (pull-up interno) |
| Disparo da fan | **25** | Entrada `PWM`/`GATE` do módulo de ângulo de fase | saída |
| Resistência | **26** | Entrada de controle (`+`) do SSR | saída, **com pull-down de 10 kΩ** |
| 3,3 V | `3V3` | `VCC`/`VIN` dos MAX31855 e alimentação lógica do módulo dimmer (se ele aceitar 3,3 V) | — |
| GND | `GND` | GND de todos os módulos (MAX31855, dimmer, `−` do SSR) | — |

**Pinos que não devem ser usados no ESP32 clássico:**

| GPIO | Motivo |
|---|---|
| 6 a 11 | Ligados à memória flash interna: usar trava a placa. |
| 0, 2, 5, 12, 15 | Pinos de *strapping*: definem o modo de boot e podem impedir a placa de ligar ou gravar. |
| 34 a 39 | Só entrada e sem pull-up interno. |
| 1, 3 | Serial USB (gravação e monitor). |

### ESP32-S3 DevKitC-1 (alternativa)

Para compilar para o S3, selecione o ambiente `esp32-s3-devkitc-1` no PlatformIO.

| Sinal | GPIO |
|---|---|
| SCK dos termopares | 12 |
| Dados dos termopares (SO) | 13 |
| CS BT | 10 |
| CS ET | 9 |
| Zero-cross | 4 |
| Disparo da fan | 5 |
| Resistência (SSR) | 6 |

---

## Ligações

```
                 ESP32 DevKit
              ┌──────────────────┐
  MAX31855 BT │ 3V3 ─────────────┼──► VCC  (BT e ET)
  ┌─────────┐ │ GND ─────────────┼──► GND  (todos os módulos)
  │ SCK ────┼─┤ GPIO18           │
  │ SO  ────┼─┤ GPIO19           │
  │ CS  ────┼─┤ GPIO21           │
  └─────────┘ │                  │
  MAX31855 ET │                  │
  (futuro)    │                  │
  SCK/SO nos  │                  │
  mesmos pinos│                  │
  CS ─────────┤ GPIO22           │
              │                  │        Módulo de ângulo de fase (fan)
              │ GPIO27 ◄─────────┼──────  Z-C
              │ GPIO25 ──────────┼──────► PWM / GATE
              │                  │
              │                  │        SSR (resistência)
              │ GPIO26 ──────────┼──┬───► + (controle)
              │                  │  │
              │                  │ [10kΩ] pull-down
              │ GND ─────────────┼──┴───► − (controle)
              └──────────────────┘
```

- **Pull-down de 10 kΩ no pino do SSR (GPIO 26 → GND): obrigatório.** Durante o boot e o reset o pino
  fica flutuando, e sem o resistor o SSR pode ligar a resistência sem a fan.
- **MAX31855 alimentado em 3,3 V.** Muitos módulos aceitam 3–5 V, mas o ESP32 só tolera 3,3 V nos pinos.
- **Termopar tipo K tem polaridade.** Se a temperatura cair quando esquenta, inverta os fios.
- Os dois MAX31855 compartilham `SCK` e `SO`. Só o `CS` é separado.
- Verifique se o seu módulo de ângulo de fase aceita lógica de 3,3 V. Se for só 5 V, use um conversor de nível.
- A parte de **127 V** (SSR, módulo dimmer, pipoqueira) deve ficar isolada fisicamente da parte de baixa tensão.

---

## Segurança

**A resistência NUNCA liga sem a fan.** Sem fluxo de ar, a resistência da pipoqueira queima em segundos.
O firmware só deixa a resistência ligar se **todas** as condições abaixo forem verdadeiras:

| Condição | Padrão |
|---|---|
| Fan pedida e aplicada ≥ mínimo | 40 % |
| Sinal de zero-cross chegando | ~120 Hz |
| Fan ligada há pelo menos | 2 s |
| Pelo menos um termopar válido | — |
| Sem falha travada (sobretemperatura) | — |
| Página se comunicou recentemente | 10 s |
| (Modos automático/perfil) BT válido | — |
| (Modo perfil) torra em andamento e perfil não terminado | — |

Se alguma trava disparar, a resistência é **desarmada**: ela não volta sozinha, e você precisa religá-la
pela página. A verificação da fan é feita duas vezes: na lógica de controle e em `setHeater()`, que é o
**único** ponto do código que escreve no pino do SSR.

Outras proteções:

- **Sobretemperatura:** BT > 250 °C (ou ET > 290 °C) corta a resistência, põe a fan em 100 % e trava.
  Só sai com "Limpar falha", depois que a temperatura cair.
- **Resfriamento:** depois de aquecer, a fan não desce de 60 % por 30 s, nem enquanto a temperatura estiver
  acima de 60 °C.
- **Watchdog:** se o firmware travar por 3 s, o ESP32 reinicia e as saídas voltam a desligado.

O software **não substitui** proteções físicas. Mantenha o **fusível térmico original** da pipoqueira e
considere um intertravamento elétrico, por exemplo alimentar o SSR da resistência através do circuito da
fan. O zero-cross prova que o módulo da fan tem rede, mas não que o motor está girando.

Nos primeiros testes, troque a resistência por uma **lâmpada incandescente** para ver o SSR funcionando sem risco.

---

## Como compilar e gravar

### 1. Instalar

1. [VS Code](https://code.visualstudio.com/) com a extensão **PlatformIO IDE**.
2. Clone o repositório e abra a pasta no VS Code (**File → Open Folder**).

### 2. Configurar o Wi-Fi (`.env`)

As senhas ficam num arquivo `.env`, que **não vai para o git**. Crie o seu a partir do exemplo:

```bash
cp .env.example .env
```

e edite:

```ini
WIFI_SSID=NomeDaSuaRede
WIFI_PASS=SenhaDaSuaRede
```

| Variável | Para que serve | Padrão |
|---|---|---|
| `WIFI_SSID` / `WIFI_PASS` | Rede onde o torrador se conecta. **Vazio = o ESP32 cria a própria rede.** | vazio |
| `AP_SSID` / `AP_PASS` | Rede criada pelo ESP32 (senha com no mínimo 8 caracteres) | `Torrador` / `torra1234` |
| `HOSTNAME` | Endereço `http://<HOSTNAME>.local` | `torrador` |

Sem `.env` o projeto compila mesmo assim, em modo AP. O arquivo é lido por `scripts/load_env.py` na hora
da compilação, então depois de mudá-lo é só gravar de novo.

### 3. Gravar

1. Conecte o ESP32 no USB.
2. Na barra inferior do VS Code, confira se o ambiente é **`env:esp32dev`** (ou `env:esp32-s3-devkitc-1` se a placa for S3).
3. Clique em **→ Upload**.
4. Clique em **🔌 Serial Monitor** (115200) para ver o IP e o log.

Pela linha de comando:

```bash
pio run -e esp32dev -t upload
pio device monitor
```

> A **primeira** compilação baixa o toolchain do ESP32 e pode levar vários minutos.

### 4. Abrir a página

O monitor serial mostra o endereço logo no boot:

```
=== Torrador v0.2 ===
Conectando em MinhaRede........
IP: http://192.168.15.4  ou  http://torrador.local
```

- **Com Wi-Fi configurado:** abra o IP mostrado (ou `http://torrador.local`) num aparelho **na mesma rede**.
- **Modo AP:** conecte na rede `Torrador` (senha `torra1234`) e abra `http://192.168.4.1`.

Digite sempre com **`http://`**: o ESP32 não tem https.

---

## Usando a página

| Quadro | O que faz |
|---|---|
| **BT / ET** | Temperaturas atuais. Mostra o erro do termopar quando houver (aberto, curto, sem comunicação). |
| **Torra** | Cronômetro e botão **Iniciar / Finalizar torra**. Funciona em qualquer modo. |
| **Gráfico** | Aba **Torra**: torra atual (BT, alvo, potência da resistência) com a última torra e o perfil por cima. Aba **Ao vivo**: últimos 10 min. |
| **Fan** | Velocidade da fan e frequência do zero-cross (deve marcar cerca de 120 Hz). |
| **Resistência** | Liga/desliga e modo de controle. O aviso amarelo 🔒 diz qual trava está impedindo o aquecimento. |
| **Perfis** | Criar, editar, salvar, excluir e ativar perfis. |
| **Última torra salva** | Resumo e download em CSV. |
| **PARAR AQUECIMENTO** | Desliga a resistência na hora. A fan continua ligada para resfriar. |

### Modos da resistência

| Modo | Controle |
|---|---|
| **Manual** | Você define a potência (0–100 %). |
| **Automático** | O PID mantém o BT num alvo fixo. |
| **Perfil** | Ao iniciar a torra, o PID segue a curva de BT do perfil e a fan segue a fan do perfil. Quando o perfil acaba, a resistência desarma e a fan continua para resfriar. |

### Registro da torra

- Durante a torra é gravado 1 ponto por segundo: tempo, BT, ET, alvo, potência da resistência e fan (até 60 min).
- Ao **finalizar**, a torra é salva na flash como **última torra** e continua lá depois de desligar.
  Torras com menos de 30 s não substituem a salva.
- O CSV baixado abre no Excel ou no LibreOffice:

```
#profile=Etiópia Clara
#duration=720
t,bt,et,sp,heat,fan
0,24.5,,100.0,0,80
1,25.0,,100.2,35,80
```

> **Mantenha a página aberta e a tela do celular ligada durante a torra.** Se a página ficar 10 s sem se
> comunicar, a resistência desarma por segurança (`CLIENT_TIMEOUT_MS`).

---

## Perfis de torra

Um perfil é uma lista de pontos **tempo → BT alvo + fan**. Entre os pontos, o alvo e a fan variam em linha reta.

1. Em **Perfis**, clique em **Novo** (vem um modelo pronto), em **Abrir** (para editar um salvo) ou em
   **Copiar da última torra** (cria um perfil a partir da última torra, com um ponto por minuto).
2. Ajuste nome e pontos. O tempo é em `min:seg` (ex.: `4:30`). O gráfico mostra a prévia.
3. **Salvar**, ou **Salvar e usar na torra**: já ativa o perfil e muda para o modo Perfil.
4. Clique em **Iniciar torra**.

Regras (validadas pelo ESP32 ao salvar):

- 2 a 24 pontos, com tempos crescentes, até 60 min;
- BT alvo entre 0 e 240 °C;
- fan **≥ 40 %** (o mínimo para aquecer), para que o perfil nunca desarme a resistência por falta de fan;
- até 30 perfis salvos.

Os perfis ficam na flash do ESP32 (`/p/<id>.csv`), um ponto por linha (`segundos,bt,fan`):

```
#name=Etiópia Clara
0,100.0,80
240,150.0,70
600,200.0,60
```

---

## Configurações (`config.h`)

Todos os ajustes ficam em `src/config.h`. Os mais usados:

| Constante | Padrão | Descrição |
|---|---|---|
| `USE_ET` | `0` | `1` habilita o segundo termopar (ET, ar). |
| `FAN_MIN_PCT_FOR_HEAT` | `40` | Fan mínima para permitir aquecer. |
| `FAN_SPINUP_MS` | `2000` | Tempo de aceleração da fan antes de aquecer. |
| `BT_MAX_C` / `ET_MAX_C` | `250` / `290` | Limites de sobretemperatura. |
| `REQUIRE_SENSOR_FOR_HEAT` | `1` | `0` permite aquecer sem termopar (**só para teste, com lâmpada**). |
| `CLIENT_TIMEOUT_MS` | `10000` | Tempo sem comunicação com a página até desarmar. |
| `COOLDOWN_TEMP_C` / `COOLDOWN_FAN_PCT` / `COOLDOWN_MIN_MS` | `60` / `60` / `30000` | Resfriamento obrigatório. |
| `PID_KP` / `PID_KI` / `PID_KD` | `5` / `0.05` / `0` | Ganhos do PID (**ainda não ajustados**). |
| `HEATER_WINDOW_MS` | `1000` | Janela do PWM lento do SSR. |
| `ZC_DEBOUNCE_US` | `4000` | Filtro do zero-cross. Ajuste se o ZC marcar muito diferente de 120 Hz. |
| `GATE_PULSE_US` | `200` | Largura do pulso no TRIAC. Aumente se a fan oscilar. |
| `FAN_MIN_DELAY_US` / `FAN_MAX_DELAY_US` | `300` / `7800` | Atraso de disparo em 100 % e em 1 %. |

---

## API HTTP

A página usa esta API, que também serve para scripts ou outros clientes. Os `POST` recebem parâmetros de formulário.

| Método | Rota | Parâmetros | Descrição |
|---|---|---|---|
| GET | `/api/state` | — | Estado completo (JSON). |
| POST | `/api/cmd` | `fan`, `heat` (0/1), `mode` (`manual`/`auto`/`profile`), `power`, `sp` | Comandos. Todos opcionais. |
| POST | `/api/stop` | — | Desliga a resistência. |
| POST | `/api/reset` | — | Limpa a falha de sobretemperatura. |
| POST | `/api/roast/start` | — | Inicia a torra. |
| POST | `/api/roast/stop` | — | Finaliza e salva a torra. |
| GET | `/api/roast` | `from` | Registro da torra atual (CSV) a partir do ponto `from`. |
| GET | `/api/last` | `dl` | Última torra salva (CSV). `dl=1` baixa como arquivo. |
| GET | `/api/profiles` | — | Lista de perfis (JSON). |
| GET | `/api/profile` | `id` | Conteúdo do perfil. |
| POST | `/api/profile/save` | `name`, `data` | Salva o perfil (`data` = linhas `segundos,bt,fan`). |
| POST | `/api/profile/delete` | `id` | Exclui o perfil. |
| POST | `/api/profile/use` | `id` | Ativa o perfil para o modo Perfil. |

Exemplo:

```bash
curl http://torrador.local/api/state
curl -X POST http://torrador.local/api/cmd -d fan=60
```

---

## Problemas comuns

| Sintoma | Causa / solução |
|---|---|
| `This chip is ESP32, not ESP32-S3` ao gravar | Ambiente errado: selecione `env:esp32dev` na barra inferior do VS Code. |
| Gravação parada em `Connecting....___` | Segure o botão **BOOT** da placa até começar a gravar. |
| Página carregando para sempre | Use o endereço mostrado no monitor serial, com `http://`, e com o aparelho na mesma rede. O `192.168.4.1` só vale no modo AP. |
| Termopar "sem comunicação" | Confira as ligações de SCK/SO/CS, o VCC em 3,3 V e o GND. |
| Termopar "aberto" | O termopar está desconectado do MAX31855. |
| ZC em 0 Hz / "Sem zero-cross" | O módulo da fan está sem rede ou o pino Z-C não está no GPIO 27. Sem isso a resistência não liga (é proposital). |
| Fan oscilando | Ajuste `GATE_PULSE_US` (aumentar) e `ZC_DEBOUNCE_US`. |
| Resistência desarma sozinha | Leia o aviso 🔒 na página ou o log serial (`[TRAVA] ...`). |
| `.env não encontrado` na compilação | Crie o `.env` a partir do `.env.example`. Sem ele, o ESP32 sobe em modo AP. |

---

## Estrutura do projeto

```
├── platformio.ini       # placas, partição da flash e script do .env
├── .env.example         # modelo de configuração do Wi-Fi (copie para .env)
├── scripts/
│   └── load_env.py      # lê o .env e passa para o firmware como #define
└── src/
    ├── config.h         # pinagem, limites de segurança, PID, ajustes
    ├── main.cpp         # controle, travas, termopares, perfis, torra, servidor web
    └── web_page.h       # página web (HTML/CSS/JS embutido)
```

Detalhes de implementação:

- **Fan:** a interrupção do zero-cross agenda, via `esp_timer`, o disparo do TRIAC com atraso proporcional à velocidade.
- **Resistência:** PWM lento (janela de 1 s) no SSR.
- **Armazenamento:** perfis e última torra ficam no LittleFS (partição `huge_app`: 3 MB de programa e ~900 KB de arquivos, sem OTA).

# Lê o arquivo .env da raiz do projeto e passa as variáveis como #define para o firmware.
# Assim as senhas de Wi-Fi ficam fora do código (e fora do git).
Import("env")
import os

KEYS = ("WIFI_SSID", "WIFI_PASS", "AP_SSID", "AP_PASS", "HOSTNAME")
path = os.path.join(env.subst("$PROJECT_DIR"), ".env")

if not os.path.isfile(path):
    print("[env] .env não encontrado: usando os padrões de src/config.h (modo AP). Veja .env.example")
else:
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = (s.strip() for s in line.split("=", 1))
            if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
                value = value[1:-1]
            if key in KEYS:
                env.Append(CPPDEFINES=[(key, env.StringifyMacro(value))])
            else:
                print("[env] variável desconhecida ignorada: %s" % key)

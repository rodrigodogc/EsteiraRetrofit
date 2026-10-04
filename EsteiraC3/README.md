# 🏃 Esteira Retrofit Firmware

![Platform](https://img.shields.io/badge/platform-ESP32-blue?logo=espressif)
![Framework](https://img.shields.io/badge/framework-Arduino-00979D?logo=arduino)
![Build](https://img.shields.io/badge/build-PlatformIO-orange?logo=platformio)
![BLE](https://img.shields.io/badge/BLE-Nordic%20UART%20Service-blueviolet)
![WiFi](https://img.shields.io/badge/WiFi-ESPAsyncWebServer-green)
![License](https://img.shields.io/badge/license-MIT-lightgrey)

Firmware completo para retrofit de esteiras ergométricas com **ESP32 WROOM-32**. Controle por PWM de hardware, sensor Hall de velocidade, inclinação por relés, malha fechada PID, **Bluetooth BLE**, painel web em tempo real e sistema de falhas.

---

## ✨ Funcionalidades

| Categoria | Recurso |
|-----------|---------|
| 🔧 Motor | PWM de hardware LEDC 20 kHz, 10 bits, duty máximo configurável |
| 📈 Velocidade | Sensor Hall via interrupção, RPM e km/h com debounce |
| 🔄 Controle | Malha aberta **ou** malha fechada PID (flag em `Config.h`) |
| 📉 Rampa | Aceleração/desaceleração suave configurável (% PWM/s) |
| 📐 Inclinação | 2 relés (UP/DOWN), homing automático, estimativa por tempo |
| 📱 BLE | Nordic UART Service (NUS) — compatível com iOS e Android via Expo |
| 🌐 WiFi | ESPAsyncWebServer + WebSocket, mDNS `esteira.local`, AP fallback |
| 🗄️ NVS | Credenciais WiFi persistidas em flash (Preferences) |
| ⚠️ Falhas | F01 heartbeat loss, F02 sensor Hall — parada de emergência imediata |
| 🎨 Painel | Interface web responsiva (gauge SVG, controles de velocidade e inclinação) |

---

## 🗂️ Estrutura do Projeto

```
EsteiraRetrofit/
├── include/
│   ├── Config.h            ← Todas as constantes e flags de compilação
│   ├── Motor.h             ← Classe Motor + enum FaultCode
│   ├── SpeedSensor.h       ← Sensor Hall (ISR + cálculo RPM/km/h)
│   ├── Incline.h           ← Máquina de estados de inclinação
│   ├── BLEManager.h        ← Bluetooth BLE (Nordic UART Service)
│   ├── WebSocketHandler.h  ← Handler WebSocket (WiFi)
│   ├── WebServer.h         ← Servidor HTTP + rotas de API
│   ├── WiFiManager.h       ← STA/AP, mDNS, DNS captivo
│   ├── NVSStorage.h        ← Persistência de credenciais
│   └── WSCommands.h        ← Constantes de protocolo (compartilhado WS e BLE)
├── src/
│   ├── main.cpp
│   ├── Motor.cpp
│   ├── SpeedSensor.cpp
│   ├── Incline.cpp
│   ├── BLEManager.cpp
│   ├── WebSocketHandler.cpp
│   ├── WebServer.cpp
│   ├── WiFiManager.cpp
│   └── NVSStorage.cpp
├── data/                   ← Filesystem LittleFS (upload separado)
│   ├── index.html
│   ├── style.css
│   └── app.js
└── platformio.ini
```

---

## ⚙️ Configuração — `include/Config.h`

### Flags principais

```cpp
#define OPEN_LOOP_MODE    true   // true = malha aberta | false = PID
#define WIFI_ENABLED      1      // 0 = desliga WiFi (libera ~40 KB heap para BLE)
```

### Motor

| Define | Padrão | Descrição |
|--------|--------|-----------|
| `MOTOR_PWM_PIN` | 25 | GPIO do driver MOSFET |
| `MAX_PWM_PERCENT` | 30.0 | Duty cycle máximo (%) |
| `MAX_SPEED_KMH` | 14.0 | Velocidade máxima |
| `MIN_SPEED_KMH` | 1.0 | Velocidade mínima ao ligar |
| `METERS_PER_PULSE` | 0.04712 | m/pulso Hall (calibrar na instalação) |
| `RAMP_RATE_UP` | 2.5 | % PWM/s na aceleração |
| `RAMP_RATE_DOWN` | 5.0 | % PWM/s na desaceleração normal |
| `RAMP_RATE_EMERGENCY` | 40.0 | % PWM/s na parada de emergência |

### Sensor Hall

| Define | Padrão | Descrição |
|--------|--------|-----------|
| `HALL_SENSOR_PIN` | 34 | GPIO input-only (⚠️ sem pull-up interno) |
| `HALL_DEBOUNCE_MIN_US` | 10000 | Debounce mínimo em µs |
| `HALL_TIMEOUT_MS` | 3000 | Timeout sem pulso → velocidade = 0 |

> ⚠️ **GPIO 34**: é input-only e não possui pull-up interno. Use **resistor externo de 10 kΩ para 3.3 V**.

### PID (malha fechada)

```cpp
#define PID_KP  3.0f
#define PID_KI  1.2f
#define PID_KD  0.15f
#define PID_INTERVAL_MS  100
```

### BLE

```cpp
#define BLE_DEVICE_NAME        "Esteira"
#define BLE_SERVICE_UUID       "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  // NUS
#define BLE_CHAR_RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // App → ESP
#define BLE_CHAR_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // ESP → App
#define BLE_STATUS_INTERVAL_MS 200
```

---

## 📟 Protocolo de Comunicação

**Tanto BLE quanto WebSocket usam o mesmo protocolo JSON.** O app Expo poderá usar o mesmo código de parsing para as duas interfaces.

### Comandos (App → ESP)

```json
{ "cmd": "heartbeat" }
{ "cmd": "start" }
{ "cmd": "stop" }
{ "cmd": "setSpeed",   "value": 6.5 }
{ "cmd": "setIncline", "value": 3.0 }
{ "cmd": "homing" }
{ "cmd": "clearFault" }
{ "cmd": "getStatus" }
```

### Status (ESP → App) — periódico a 200 ms

```json
{
  "type":          "status",
  "running":       true,
  "speed":         6.5,
  "rpm":           48,
  "targetSpeed":   6.5,
  "incline":       2.0,
  "targetIncline": 3.0,
  "pwmPct":        13.9,
  "heartbeatOk":   true,
  "openLoop":      true,
  "homingDone":    true,
  "fault":         0
}
```

### Heartbeat

O app deve enviar `{"cmd":"heartbeat"}` **a cada 1 segundo**. Se o ESP não receber heartbeat de **nenhuma interface** (WS ou BLE) por mais de `WS_HEARTBEAT_TIMEOUT_MS` (3 s), o motor para com **F01**.

---

## ⚠️ Códigos de Falha

| Código | `fault` | Causa | Ação |
|--------|---------|-------|------|
| **F01** | 1 | Perda de heartbeat (WS e BLE simultâneos) | Parada de emergência |
| **F02** | 2 | Sensor Hall ausente (malha fechada) | Parada de emergência |

Para limpar uma falha: `{"cmd":"clearFault"}` (pelo painel ou BLE).

---

## 📡 Bluetooth BLE — Integração com Expo

### UUID do serviço

O firmware usa o **Nordic UART Service (NUS)**, reconhecido pela maioria das ferramentas BLE (nRF Connect, LightBlue, etc.).

```
Service UUID : 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
RX (Write)   : 6E400002-B5A3-F393-E0A9-E50E24DCCA9E  ← App escreve comandos aqui
TX (Notify)  : 6E400003-B5A3-F393-E0A9-E50E24DCCA9E  ← App subscreve aqui para status
```

### MTU

O ESP32 aceita negociação de MTU até **512 bytes**. O status JSON tem ~190 bytes. No app Expo, solicite MTU alto ao conectar:

```ts
// react-native-ble-plx
await device.requestMTU(512);
```

### Fluxo de conexão

```
App conecta ao device "Esteira"
  → subscreve TX characteristic
  → recebe {"type":"connected","version":"1.0","openLoop":true}
  → começa a enviar heartbeat a cada 1 s
  → recebe status a cada 200 ms via Notify
```

---

## 🌐 WiFi e Painel Web

### Modos de operação

| Modo | SSID | IP | URL |
|------|------|-----|-----|
| **STA** | Rede do usuário | DHCP | `http://esteira.local` ou IP |
| **AP** | `Esteira-Config` | 192.168.4.1 | `http://192.168.4.1` |

### Configurar WiFi

1. Conecte ao AP `Esteira-Config` (senha: `12345678`)
2. Acesse `http://192.168.4.1`
3. Vá em **Config → Configurar WiFi**
4. Informe SSID e senha → **Conectar**

> Em modo AP, o DNS captivo redireciona qualquer domínio (incluindo `esteira.local`) para o IP do AP.

### Desativar WiFi

Se preferir usar somente BLE (economiza ~40 KB de heap):

```cpp
// include/Config.h
#define WIFI_ENABLED 0
```

---

## 🔌 Pinagem

| Função | GPIO | Observação |
|--------|------|-----------|
| Motor PWM | 25 | LEDC canal 0 |
| Sensor Hall | 34 | Input-only — resistor 10 kΩ externo para 3.3 V |
| Relé inclinação UP | 26 | Ativo HIGH |
| Relé inclinação DOWN | 27 | Ativo HIGH |

---

## 🛠️ Build e Flash

### Pré-requisitos

- [PlatformIO](https://platformio.org/) (VS Code extension ou CLI)
- ESP32 conectado via USB

### Comandos

```bash
# Compilar e gravar firmware
pio run --target upload

# Gravar filesystem (painel web — data/)
pio run --target uploadfs

# Monitor serial
pio device monitor
```

> 💡 Sempre grave o filesystem **ao menos uma vez**. Sem os arquivos em `data/`, o painel web não abrirá.

### Dependências (instaladas automaticamente pelo PlatformIO)

| Biblioteca | Versão | Uso |
|-----------|--------|-----|
| `esp32async/AsyncTCP` | ^3.4.10 | Base do servidor async |
| `esp32async/ESPAsyncWebServer` | ^3.9.4 | HTTP + WebSocket |
| `bblanchon/ArduinoJson` | ^7.4.2 | Serialização JSON |
| `br3ttb/PID` | ^1.2.1 | Controle PID (malha fechada) |
| `h2zero/NimBLE-Arduino` | ^2.3.6 | Bluetooth BLE (NimBLE stack) |

---

## 🏗️ Arquitetura do Firmware

```
loop()
  ├── SpeedSensor::update()     — calcula RPM/km/h por tempo entre pulsos Hall
  ├── Motor::update()           — verifica heartbeat, ramp PWM, PID ou malha aberta
  ├── Incline::update()         — máquina de estados (IDLE/HOMING/MOVING_UP/DOWN)
  ├── BLEManager::update()      — envia status via BLE Notify a 200 ms
  ├── WiFiManager::update()     — gerencia reconexão STA, DNS captivo em AP
  └── WebSocketHandler::update()— envia status WS a 200 ms, cleanup de clientes
```

### Heartbeat centralizado

O timer de heartbeat vive em `Motor`. Qualquer interface (BLE ou WebSocket) que receber `{"cmd":"heartbeat"}` chama `motor.resetHeartbeat()`. Se o timer expirar com o motor rodando, `Motor::update()` dispara `faultStop(HEARTBEAT_TIMEOUT)` diretamente — sem depender do handler de rede.

```
BLEManager     ──┐
                 ├─→ motor.resetHeartbeat() ──→ Motor::update() verifica timeout
WebSocketHandler─┘
```

### Rampa de velocidade

```
_currentPwmPct avança para _targetPwmPct a RAMP_RATE_UP %/s
_currentPwmPct recua  para _targetPwmPct a RAMP_RATE_DOWN %/s (ou EMERGENCY)
→ nunca há saltos bruscos de PWM
```

---

## 📐 Calibração

### METERS_PER_PULSE

```
METERS_PER_PULSE = (π × diâmetro_polia_motora) / relação_de_transmissão
Exemplo: polia Ø 120 mm, relação 8:1  →  (π × 0.12) / 8 = 0.04712 m/pulso
```

### MAX_PWM_PERCENT

Ajuste para que o motor atinja `MAX_SPEED_KMH` sem sobreaquecimento. Comece com 30 % e aumente gradualmente monitorando a temperatura do MOSFET e do motor.

### Inclinação — INCLINE_DEG_PER_SEC / INCLINE_HOMING_MS

Meça o tempo real (em ms) para o atuador percorrer o curso completo (0° → máximo) e configure `INCLINE_HOMING_MS`. Divida o ângulo máximo pelo tempo em segundos para obter `INCLINE_DEG_PER_SEC`.

---

## 🧪 Teste rápido com nRF Connect

1. Abra **nRF Connect** (Android/iOS)
2. Escaneie e conecte ao device **"Esteira"**
3. Encontre o serviço `6E400001...`
4. Subscreva a characteristic TX `6E400003...` → você verá o status JSON a 200 ms
5. Escreva na RX `6E400002...` o JSON `{"cmd":"heartbeat"}` repetidamente
6. Escreva `{"cmd":"start"}` para ligar o motor

---

*Firmware desenvolvido para ESP32 WROOM-32 com PlatformIO/Arduino framework.*

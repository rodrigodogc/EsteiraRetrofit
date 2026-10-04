#pragma once

// ─── Controle de malha ────────────────────────────────────────────────────────
// true  = malha aberta (target speed → PWM linear, sem sensor)
// false = malha fechada (PID fecha loop com sensor Hall)
#define OPEN_LOOP_MODE         true

// ─── PWM do Motor ─────────────────────────────────────────────────────────────
#define MOTOR_PWM_PIN          25
#define MOTOR_PWM_CHANNEL      0
#define MOTOR_PWM_FREQ_HZ      20000     // 20 kHz (audível-livre para MOSFETs)
#define MOTOR_PWM_RESOLUTION   10        // 10 bits → 0-1023
#define MOTOR_PWM_MAX_COUNTS   ((1 << MOTOR_PWM_RESOLUTION) - 1)  // 1023

// Limite de duty cycle em percentual
#define MAX_PWM_PERCENT        40.0f
#define MAX_PWM_COUNTS         ((uint32_t)(MOTOR_PWM_MAX_COUNTS * MAX_PWM_PERCENT / 100.0f))

// ─── Velocidade ───────────────────────────────────────────────────────────────
#define MAX_SPEED_KMH          12.0f     // km/h — velocidade máxima permitida
#define MIN_SPEED_KMH          1.0f      // km/h — velocidade mínima ao ligar

// Metros percorridos pela lona por pulso do encoder.
// Medido diretamente: lona avança 5 cm por volta do motor → 0,05 m ÷ 36 furos
#define ENCODER_PULSES_PER_REV 36        // furos no disco óptico
#define METERS_PER_PULSE       0.001389f // 0,05 m/rev ÷ 36 pulsos/rev

// ─── Rampa de Aceleração / Desaceleração ──────────────────────────────────────
// Unidade: % de duty-cycle por segundo
#define RAMP_RATE_UP           2.5f      // aceleração (subida)
#define RAMP_RATE_DOWN         5.0f      // desaceleração normal
#define RAMP_RATE_EMERGENCY    40.0f     // desaceleração de emergência

// ─── PID ──────────────────────────────────────────────────────────────────────
#define PID_KP                 1.5f
#define PID_KI                 0.15f
#define PID_KD                 0.0f
#define PID_INTERVAL_MS        100       // ciclo do PID em ms

// ─── Encoder óptico (disco perfurado 36 furos, borda de subida) ──────────────
// Sensor de distância/reflexão focado nos furos; saída digital.
// GPIO 34 é input-only no ESP32 — sem pull-up interno; use resistor externo 10kΩ→3V3.
//
// Limites de velocidade com os valores acima:
//   Máx suportada  : 1e6µs/ENCODER_DEBOUNCE_MIN_US / 36 * 60 ≈ 8333 RPM do motor
//   A 14 km/h      : ~2970 pulsos/s → período ~337 µs → debounce deve ser < 337 µs
#define ENCODER_PIN              16
#define ENCODER_DEBOUNCE_MIN_US  300     // µs — mínimo físico a 12 km/h ≈ 417 µs; margem anti-ruído
#define ENCODER_MAX_PERIOD_US    50000   // µs — acima disto reseta baseline (50 ms = ~0,1 km/h)
#define ENCODER_TIMEOUT_MS       1000    // ms sem pulso → velocidade = 0 (a 1 km/h: ~5 ms/pulso)
#define ENCODER_SPEED_ALPHA      0.35f   // coeficiente IIR: menor = mais suave mas mais lento

// ─── Inclinação ───────────────────────────────────────────────────────────────
#define INCLINE_UP_PIN         26        // relé UP (ativo HIGH)
#define INCLINE_DOWN_PIN       27        // relé DOWN (ativo HIGH)
#define INCLINE_MAX_DEG        12.0f     // ângulo máximo em graus
#define INCLINE_MIN_DEG        0.0f      // ângulo mínimo (nível)
#define INCLINE_DEG_PER_SEC    1.2f      // graus por segundo de movimento
#define INCLINE_HOMING_MS      35000     // tempo máximo de homing (ms) — curso completo

// ─── WebSocket / Heartbeat ────────────────────────────────────────────────────
// Motor para IMEDIATAMENTE se não receber heartbeat dentro do timeout
#define WS_HEARTBEAT_TIMEOUT_MS  3000    // ms sem heartbeat → parar motor
#define WS_STATUS_INTERVAL_MS    200     // frequência de envio de status ao cliente

// ─── WiFi / Rede ──────────────────────────────────────────────────────────────
#define WIFI_AP_SSID           "Esteira-Config"
#define WIFI_AP_PASSWORD       "12345678"
#define WIFI_AP_IP             "192.168.4.1"
#define MDNS_HOSTNAME          "esteira"    // → http://esteira.local
#define WIFI_CONNECT_TIMEOUT_MS  15000
#define WIFI_RECONNECT_INTERVAL_MS 30000

// ─── NVS (Preferences) ───────────────────────────────────────────────────────
#define NVS_NAMESPACE          "esteira"
#define NVS_KEY_SSID           "ssid"
#define NVS_KEY_PASS           "pass"
#define NVS_KEY_PID_KP         "pid_kp"
#define NVS_KEY_PID_KI         "pid_ki"
#define NVS_KEY_PID_KD         "pid_kd"

// ─── WiFi (1 = ligado, 0 = desliga WiFi/WebServer para liberar heap ao BLE) ──
#define WIFI_ENABLED           1

// ─── Bluetooth BLE — Nordic UART Service (NUS) ───────────────────────────────
// UUIDs padrão NUS — suportados nativamente por react-native-ble-plx (Expo)
// iOS + Android; iOS NÃO suporta Classic Bluetooth (SPP) sem MFi.
#define BLE_DEVICE_NAME        "Esteira"
#define BLE_SERVICE_UUID       "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHAR_RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // App → ESP (Write)
#define BLE_CHAR_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // ESP → App (Notify)
#define BLE_STATUS_INTERVAL_MS 200   // ms entre broadcasts de status via Notify

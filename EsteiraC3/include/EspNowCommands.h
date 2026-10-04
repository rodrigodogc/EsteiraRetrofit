#pragma once

// ─── Protocolo ESP-NOW — placa de potência ↔ painel superior ─────────────────
// Comandos (painel → placa) reaproveitam as strings de WSCommands.h
// (WS_CMD_START/STOP/SET_SPEED/SET_INCLINE/HEARTBEAT/CLEAR_FAULT/HOMING/
// GET_STATUS/EMERGENCY_STOP) — payload pequeno, cabe folgado no limite de
// ESP_NOW_MAX_DATA_LEN (250 bytes, teto rígido do ESP32-C3/IDF4.4).
//
// A telemetria (placa → painel) usa chaves curtas próprias, distintas do
// payload verboso do WS_TYPE_STATUS — o payload completo do WS não cabe
// com folga em 250 bytes. Testado com valores realistas: ~137 bytes.

// ─── Telemetria: placa → painel (broadcast/unicast pro peer ativo) ──────────
// {
//   "t":  "s"     (tipo — sempre "s" de status, reservado pra futuros tipos)
//   "r":  bool     (running)
//   "sp": float    (km/h — velocidade real estimada via FCEM)
//   "ts": float    (km/h — velocidade alvo)
//   "in": float    (graus — inclinação atual)
//   "ti": float    (graus — inclinação alvo)
//   "pw": float    (% duty cycle atual)
//   "hb": bool     (heartbeat ok)
//   "ol": bool     (malha aberta?)
//   "hd": bool     (homing concluído?)
//   "f":  uint8    (fault code — mesmo mapa de WSCommands.h)
//   "vb": float    (V — tensão do barramento)
//   "vm": float    (V — tensão do motor)
//   "ia": float    (A — corrente da armadura)
// }
#define ESPNOW_KEY_TYPE           "t"
#define ESPNOW_TYPE_STATUS        "s"
#define ESPNOW_KEY_RUNNING        "r"
#define ESPNOW_KEY_SPEED          "sp"
#define ESPNOW_KEY_TARGET_SPEED   "ts"
#define ESPNOW_KEY_INCLINE        "in"
#define ESPNOW_KEY_TARGET_INCLINE "ti"
#define ESPNOW_KEY_PWM            "pw"
#define ESPNOW_KEY_HEARTBEAT_OK   "hb"
#define ESPNOW_KEY_OPEN_LOOP      "ol"
#define ESPNOW_KEY_HOMING_DONE    "hd"
#define ESPNOW_KEY_FAULT          "f"
#define ESPNOW_KEY_VBUS           "vb"
#define ESPNOW_KEY_VMOT           "vm"
#define ESPNOW_KEY_CURRENT        "ia"

// ─── Beacon: placa → broadcast (FF:FF:FF:FF:FF:FF), ~1x/s ────────────────────
// Anuncia a presença/canal da placa antes de qualquer pareamento — permite um
// futuro firmware de painel descobrir a placa sem precisar do SSID da rede
// doméstica (ESP-NOW não faz channel-hopping automático; dois dispositivos em
// canais diferentes simplesmente não se enxergam).
// { "t": "b", "ch": uint8 (canal WiFi atual) }
#define ESPNOW_TYPE_BEACON        "b"
#define ESPNOW_KEY_CHANNEL        "ch"

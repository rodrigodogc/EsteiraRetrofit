#pragma once

// ─── Protocolo compartilhado com a placa de potência (EsteiraC3) ────────────
// CÓPIA MANUALMENTE SINCRONIZADA de WSCommands.h + EspNowCommands.h do
// projeto EsteiraC3 (d:\Devs\EsteiraRetrofit\EsteiraC3). São dois projetos
// PlatformIO independentes, sem build compartilhado — se o protocolo mudar
// de um lado, precisa mudar deste também. Não inclui os comandos de tuning
// de PID (setPidSpd/setPidCur/setFcemMax/setScreen/setMode) — fora de escopo
// do painel, são ferramenta de bancada via página web.

// ─── Comandos: painel → placa (idênticos a WSCommands.h) ────────────────────
#define CMD_HEARTBEAT       "heartbeat"
#define CMD_START            "start"
#define CMD_STOP              "stop"
#define CMD_EMERGENCY_STOP   "emergencyStop"
#define CMD_SET_SPEED        "setSpeed"      // {"cmd":"setSpeed","value":5.0} km/h
#define CMD_SET_INCLINE      "setIncline"    // {"cmd":"setIncline","value":3.0} graus
#define CMD_GET_STATUS       "getStatus"
#define CMD_HOMING           "homing"
#define CMD_CLEAR_FAULT      "clearFault"

// ─── Telemetria: placa → painel (chaves compactas, idênticas a EspNowCommands.h) ─
#define KEY_TYPE              "t"
#define TYPE_STATUS           "s"
#define TYPE_BEACON           "b"
#define KEY_RUNNING           "r"
#define KEY_SPEED             "sp"
#define KEY_TARGET_SPEED      "ts"
#define KEY_INCLINE           "in"
#define KEY_TARGET_INCLINE    "ti"
#define KEY_PWM               "pw"
#define KEY_HEARTBEAT_OK      "hb"
#define KEY_OPEN_LOOP         "ol"
#define KEY_HOMING_DONE       "hd"
#define KEY_FAULT             "f"
#define KEY_VBUS              "vb"
#define KEY_VMOT              "vm"
#define KEY_CURRENT           "ia"
#define KEY_CHANNEL           "ch"

// ─── Fault codes — mesmo mapa de Motor.h::FaultCode (EsteiraC3) ─────────────
#define FAULT_NONE                  0
#define FAULT_HEARTBEAT_TIMEOUT     1
#define FAULT_ENCODER_LOSS          2   // reservado, não usado
#define FAULT_OVERCURRENT           3
#define FAULT_OVERCURRENT_SUSTAINED 4
#define FAULT_OVERSPEED             5
#define FAULT_OVERSPEED_TARGET      6

// Mensagens curtas pra rolar no display (scrollText) — sem acento (fonte de
// 7 segmentos não tem, ver TreadmillFont.h).
inline const char* faultMessage(uint8_t code) {
    switch (code) {
        case FAULT_HEARTBEAT_TIMEOUT:     return "FALHA COMUNICACAO";
        case FAULT_OVERCURRENT:           return "SOBRECORRENTE";
        case FAULT_OVERCURRENT_SUSTAINED: return "SOBRECARGA SUSTENTADA";
        case FAULT_OVERSPEED:             return "SOBREVELOCIDADE";
        case FAULT_OVERSPEED_TARGET:      return "FUGA DE VELOCIDADE";
        default:                          return "FALHA DESCONHECIDA";
    }
}

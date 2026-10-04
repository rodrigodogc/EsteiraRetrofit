#pragma once

// ─── Comandos: Cliente → ESP32 ────────────────────────────────────────────────
// Payload JSON: {"cmd": "<CMD>", "value": <optional float>}

#define WS_CMD_HEARTBEAT       "heartbeat"   // keepalive obrigatório
#define WS_CMD_START           "start"       // ligar motor
#define WS_CMD_STOP            "stop"        // desligar motor
#define WS_CMD_SET_SPEED       "setSpeed"    // {"cmd":"setSpeed","value":5.0} km/h
#define WS_CMD_SET_INCLINE     "setIncline"  // {"cmd":"setIncline","value":3.0} graus
#define WS_CMD_GET_STATUS      "getStatus"   // solicita status imediato
#define WS_CMD_HOMING          "homing"      // inicia sequência de homing de inclinação
#define WS_CMD_CLEAR_FAULT     "clearFault"  // limpa estado de falha
#define WS_CMD_SET_PID         "setPid"      // {"cmd":"setPid","kp":1.5,"ki":0.15,"kd":0.0}
#define WS_CMD_GET_PID         "getPid"      // solicita config PID atual

// ─── Respostas: ESP32 → Cliente ───────────────────────────────────────────────
// Payload JSON: {"type": "<TYPE>", ...campos}

#define WS_TYPE_STATUS         "status"      // broadcast periódico de status
#define WS_TYPE_CONNECTED      "connected"   // enviado ao conectar
#define WS_TYPE_ERROR          "error"       // {"type":"error","message":"..."}
#define WS_TYPE_INFO           "info"        // {"type":"info","message":"..."}
#define WS_TYPE_HEARTBEAT_ACK  "heartbeatAck"
#define WS_TYPE_PID_CONFIG     "pidConfig"   // {"type":"pidConfig","kp":...,"ki":...,"kd":...}

// ─── Campos do payload de status ─────────────────────────────────────────────
// {
//   "type":          "status",
//   "running":       true/false,
//   "speed":         float (km/h medido),
//   "targetSpeed":   float (km/h alvo),
//   "incline":       float (graus estimados),
//   "targetIncline": float (graus alvo),
//   "pwmPct":        float (% duty cycle atual),
//   "rpm":           float (RPM do motor),
//   "heartbeatOk":   true/false,
//   "openLoop":      true/false,
//   "homingDone":    true/false,
//   "fault":         uint8 (0=ok, 1=heartbeat, 2=hall_sensor_loss),
//   "faultMsg":      string (mensagem legível)
// }

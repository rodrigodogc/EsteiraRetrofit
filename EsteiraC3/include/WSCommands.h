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
#define WS_CMD_EMERGENCY_STOP  "emergencyStop"  // parada imediata (rampa RAMP_RATE_EMERGENCY), NÃO seta fault — distinto de "stop" (rampa normal)
#define WS_CMD_SET_PID_SPD     "setPidSpd"   // {"cmd":"setPidSpd","kp":0.5,"ki":0.3,"kd":0.0} malha externa (FCEM→I_ref)
#define WS_CMD_SET_PID_CUR     "setPidCur"   // {"cmd":"setPidCur","kp":1.8,"ki":10.0,"kd":0.0} malha interna (I_a→duty%)
#define WS_CMD_SET_FCEM_MAX    "setFcemMax"  // {"cmd":"setFcemMax","value":90.0} FCEM (V) esperada em MAX_SPEED_KMH
#define WS_CMD_GET_PID         "getPid"      // solicita config PID atual (das duas malhas + FCEM_MAX_V)
#define WS_CMD_SET_SCREEN      "setScreen"   // {"cmd":"setScreen","value":"maintenance"|"dashboard"} — controla telemetria elétrica extra
// TEMPORÁRIO (testes) — troca malha aberta/fechada em runtime; motor deve estar parado.
#define WS_CMD_SET_MODE        "setMode"     // {"cmd":"setMode","value":true} — true=aberta, false=fechada

// ─── Respostas: ESP32 → Cliente ───────────────────────────────────────────────
// Payload JSON: {"type": "<TYPE>", ...campos}

#define WS_TYPE_STATUS         "status"      // broadcast periódico de status
#define WS_TYPE_CONNECTED      "connected"   // enviado ao conectar
#define WS_TYPE_ERROR          "error"       // {"type":"error","message":"..."}
#define WS_TYPE_INFO           "info"        // {"type":"info","message":"..."}
#define WS_TYPE_HEARTBEAT_ACK  "heartbeatAck"
#define WS_TYPE_PID_CONFIG     "pidConfig"   // {"type":"pidConfig","spdKp":...,"spdKi":...,"spdKd":...,"curKp":...,"curKi":...,"curKd":...,"fcemMaxV":...}

// ─── Campos do payload de status ─────────────────────────────────────────────
// {
//   "type":          "status",
//   "running":       true/false,
//   "speed":         float (km/h — ESTIMADA VIA FCEM/sensorless, não vem mais do encoder),
//   "targetSpeed":   float (km/h alvo),
//   "incline":       float (graus estimados),
//   "targetIncline": float (graus alvo),
//   "pwmPct":        float (% duty cycle atual),
//   "rpm":           float (RPM do encoder — telemetria visual, não alimenta o PID),
//   "heartbeatOk":   true/false,
//   "openLoop":      true/false,
//   "homingDone":    true/false,
//   "fault":         uint8 (0=ok, 1=heartbeat, 2=reservado/não usado, 3=overcurrent,
//                    4=overcurrent_sustained, 5=overspeed, 6=overspeed_target),
//   "faultMsg":      string (mensagem legível),
//
//   // V_mot/I_a/FCEM — sempre presentes (fase de testes da cascata sensorless;
//   // alimentam o rodapé de debug do dashboard principal):
//   "vMot":          float (V, tensão real do motor),
//   "iA":            float (A, corrente real da armadura),
//   "fcem":          float (V, FCEM estimada),
//
//   // Campos elétricos completos — SÓ presentes quando há cliente na tela de
//   // Manutenção (ver WS_CMD_SET_SCREEN), para economizar banda/processamento:
//   "vBusRaw":       float (V, bruto pós-EMA, pré-escala — barramento),
//   "vMotRaw":       float (V, bruto pós-EMA, pré-escala — motor),
//   "vCurRaw":       float (V, bruto pós-EMA, pré-escala — shunt),
//   "vBus":          float (V, tensão real do barramento)
// }

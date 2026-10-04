#include "WebSocketHandler.h"

WebSocketHandler::WebSocketHandler(AsyncWebServer& server, Motor& motor,
                                   Incline& incline, SpeedSensor& sensor,
                                   EspNowHandler& espNow)
    : _ws("/ws"), _motor(motor), _incline(incline), _sensor(sensor), _espNow(espNow) {}

void WebSocketHandler::begin() {
    _ws.onEvent([this](AsyncWebSocket* srv, AsyncWebSocketClient* client,
                       AwsEventType type, void* arg, uint8_t* data, size_t len) {
        onEvent(srv, client, type, arg, data, len);
    });

    Serial.println("[WS] WebSocket handler ready at /ws");
}

void WebSocketHandler::update() {
    _ws.cleanupClients();

    unsigned long now = millis();

    // ─── Status periódico ─────────────────────────────────────────────────────
    if (now - _lastStatusMs >= WS_STATUS_INTERVAL_MS) {
        _lastStatusMs = now;
        sendStatus();
    }
}

bool WebSocketHandler::isClientAlive() const {
    return _connectedClients > 0 && _motor.isHeartbeatOk();
}

// ─── Privados ─────────────────────────────────────────────────────────────────

void WebSocketHandler::onEvent(AsyncWebSocket* srv, AsyncWebSocketClient* client,
                               AwsEventType type, void* arg,
                               uint8_t* data, size_t len) {
    switch (type) {
    case WS_EVT_CONNECT: {
        _connectedClients++;
        _motor.resetHeartbeat();   // novo cliente = período de carência
        Serial.printf("[WS] Client #%u connected\n", client->id());

        JsonDocument doc;
        doc["type"]     = WS_TYPE_CONNECTED;
        doc["version"]  = "1.0";
        doc["openLoop"] = _motor.isOpenLoop();
        sendTo(client, doc);
        sendPidConfig(client);   // envia ganhos atuais ao novo cliente
        break;
    }
    case WS_EVT_DISCONNECT:
        if (_connectedClients > 0) _connectedClients--;
        if (_connectedClients == 0) _maintenanceActive = false;   // evita telemetria extra travada ligada
        Serial.printf("[WS] Client #%u disconnected\n", client->id());
        break;

    case WS_EVT_DATA:
        handleMessage(client, data, len);
        break;

    default:
        break;
    }
}

void WebSocketHandler::handleMessage(AsyncWebSocketClient* client,
                                     const uint8_t* data, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        Serial.printf("[WS] JSON error: %s\n", err.c_str());
        return;
    }

    const char* cmd = doc["cmd"] | "";

    if (strcmp(cmd, WS_CMD_HEARTBEAT) == 0) {
        _motor.resetHeartbeat();
        JsonDocument ack;
        ack["type"] = WS_TYPE_HEARTBEAT_ACK;
        sendTo(client, ack);
        return;
    }

    if (strcmp(cmd, WS_CMD_START) == 0) {
        if (panelHasPriority()) { rejectForPanelPriority(client); return; }
        _motor.start();
        return;
    }

    if (strcmp(cmd, WS_CMD_STOP) == 0) {
        if (panelHasPriority()) { rejectForPanelPriority(client); return; }
        _motor.stop();
        return;
    }

    if (strcmp(cmd, WS_CMD_EMERGENCY_STOP) == 0) {
        // Segurança sempre disponível por qualquer canal, mesmo com o painel conectado.
        _motor.emergencyStop();
        return;
    }

    if (strcmp(cmd, WS_CMD_SET_SPEED) == 0) {
        if (panelHasPriority()) { rejectForPanelPriority(client); return; }
        float value = doc["value"] | 0.0f;
        _motor.setTargetSpeed(value);
        return;
    }

    if (strcmp(cmd, WS_CMD_SET_INCLINE) == 0) {
        if (panelHasPriority()) { rejectForPanelPriority(client); return; }
        float value = doc["value"] | 0.0f;
        _incline.setTargetDegrees(value);
        return;
    }

    if (strcmp(cmd, WS_CMD_HOMING) == 0) {
        if (panelHasPriority()) { rejectForPanelPriority(client); return; }
        _incline.startHoming();
        return;
    }

    if (strcmp(cmd, WS_CMD_CLEAR_FAULT) == 0) {
        _motor.clearFault();
        sendStatus();
        return;
    }

    if (strcmp(cmd, WS_CMD_GET_STATUS) == 0) {
        sendStatus();
        return;
    }

    if (strcmp(cmd, WS_CMD_GET_PID) == 0) {
        sendPidConfig(client);
        return;
    }

    if (strcmp(cmd, WS_CMD_SET_PID_SPD) == 0) {
        float kp = constrain((float)(doc["kp"] | (double)_motor.getSpdKp()), 0.0f, 20.0f);
        float ki = constrain((float)(doc["ki"] | (double)_motor.getSpdKi()), 0.0f, 10.0f);
        float kd = constrain((float)(doc["kd"] | (double)_motor.getSpdKd()), 0.0f,  2.0f);
        _motor.setSpdPidGains(kp, ki, kd);
        _motor.saveSpdPidToNvs();
        sendPidConfig();   // broadcast — confirma para todos os clientes
        return;
    }

    if (strcmp(cmd, WS_CMD_SET_PID_CUR) == 0) {
        float kp = constrain((float)(doc["kp"] | (double)_motor.getCurKp()), 0.0f, 20.0f);
        float ki = constrain((float)(doc["ki"] | (double)_motor.getCurKi()), 0.0f, 50.0f);
        float kd = constrain((float)(doc["kd"] | (double)_motor.getCurKd()), 0.0f,  5.0f);
        _motor.setCurPidGains(kp, ki, kd);
        _motor.saveCurPidToNvs();
        sendPidConfig();
        return;
    }

    if (strcmp(cmd, WS_CMD_SET_FCEM_MAX) == 0) {
        float v = constrain((float)(doc["value"] | (double)_motor.getFcemMaxV()), 0.0f, 150.0f);
        _motor.setFcemMaxV(v);
        _motor.saveFcemMaxToNvs();
        sendPidConfig();
        return;
    }

    if (strcmp(cmd, WS_CMD_SET_SCREEN) == 0) {
        const char* screen = doc["value"] | "dashboard";
        _maintenanceActive = (strcmp(screen, "maintenance") == 0);
        return;
    }

    if (strcmp(cmd, WS_CMD_SET_MODE) == 0) {
        bool openLoop = doc["value"] | true;
        if (_motor.setOpenLoop(openLoop)) {
            sendStatus();   // broadcast — atualiza badge/toggle em todos os clientes
        } else {
            JsonDocument err;
            err["type"]    = WS_TYPE_ERROR;
            err["message"] = "Pare o motor antes de trocar o modo de malha";
            sendTo(client, err);
        }
        return;
    }

    Serial.printf("[WS] Unknown command: %s\n", cmd);
}

void WebSocketHandler::rejectForPanelPriority(AsyncWebSocketClient* client) {
    JsonDocument err;
    err["type"]    = WS_TYPE_ERROR;
    err["message"] = "Painel conectado - controle de movimento via web bloqueado";
    sendTo(client, err);
}

void WebSocketHandler::sendStatus() {
    if (_connectedClients == 0) return;

    JsonDocument doc;
    bool running = _motor.isRunning();

    doc["type"]          = WS_TYPE_STATUS;
    doc["running"]       = running;
    // Velocidade principal do dashboard — estimada via FCEM (sensorless), NÃO via encoder.
    doc["speed"]         = running ? roundf(_motor.getSpeedFromFcemKmh() * 100.0f) / 100.0f : 0.0f;
    doc["rpm"]           = running ? roundf(_sensor.getRPM()) : 0.0f;
    doc["targetSpeed"]   = roundf(_motor.getTargetSpeed() * 100.0f) / 100.0f;
    doc["incline"]       = roundf(_incline.getCurrentDegrees() * 10.0f) / 10.0f;
    doc["targetIncline"] = roundf(_incline.getTargetDegrees() * 10.0f) / 10.0f;
    doc["pwmPct"]        = roundf(_motor.getCurrentPwmPct() * 10.0f) / 10.0f;
    doc["heartbeatOk"]   = _motor.isHeartbeatOk();
    doc["openLoop"]      = _motor.isOpenLoop();
    doc["homingDone"]    = _incline.isHomingDone();
    doc["fault"]         = (uint8_t)_motor.getFault();
    doc["dbgPulses"]     = _sensor.getPulseCount();     // DEBUG
    doc["dbgPeriodUs"]   = _sensor.getLastPeriodUs();  // DEBUG

    // V_mot/I_a/FCEM — sempre enviados (fase de testes da cascata sensorless):
    // alimentam o rodapé de debug do dashboard principal, além dos cards da
    // aba de Manutenção.
    doc["vMot"] = roundf(_motor.getVMot()    * 100.0f) / 100.0f;
    doc["iA"]   = roundf(_motor.getCurrentA() * 100.0f) / 100.0f;
    doc["fcem"] = roundf(_motor.getFcem()    * 100.0f) / 100.0f;

    // Telemetria elétrica completa (V_bus + leituras brutas) — só quando há
    // cliente na tela de Manutenção (economiza banda/processamento)
    if (_maintenanceActive) {
        doc["vBusRaw"] = roundf(_motor.getVBusRaw() * 1000.0f) / 1000.0f;
        doc["vMotRaw"] = roundf(_motor.getVMotRaw() * 1000.0f) / 1000.0f;
        doc["vCurRaw"] = roundf(_motor.getVCurRaw() * 1000.0f) / 1000.0f;
        doc["vBus"]    = roundf(_motor.getVBus()    * 100.0f)  / 100.0f;
    }

    broadcastJson(doc);
}

void WebSocketHandler::sendPidConfig(AsyncWebSocketClient* client) {
    JsonDocument doc;
    doc["type"]     = WS_TYPE_PID_CONFIG;
    doc["spdKp"]    = _motor.getSpdKp();
    doc["spdKi"]    = _motor.getSpdKi();
    doc["spdKd"]    = _motor.getSpdKd();
    doc["curKp"]    = _motor.getCurKp();
    doc["curKi"]    = _motor.getCurKi();
    doc["curKd"]    = _motor.getCurKd();
    doc["fcemMaxV"] = _motor.getFcemMaxV();
    if (client) {
        sendTo(client, doc);
    } else {
        broadcastJson(doc);
    }
}

void WebSocketHandler::sendTo(AsyncWebSocketClient* client, const JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    client->text(out);
}

void WebSocketHandler::broadcastJson(const JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    _ws.textAll(out);
}

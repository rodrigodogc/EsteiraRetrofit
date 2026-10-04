#include "WebSocketHandler.h"

WebSocketHandler::WebSocketHandler(AsyncWebServer& server, Motor& motor,
                                   Incline& incline, SpeedSensor& sensor)
    : _ws("/ws"), _motor(motor), _incline(incline), _sensor(sensor) {}

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
        doc["openLoop"] = OPEN_LOOP_MODE;
        sendTo(client, doc);
        sendPidConfig(client);   // envia ganhos atuais ao novo cliente
        break;
    }
    case WS_EVT_DISCONNECT:
        if (_connectedClients > 0) _connectedClients--;
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
        _motor.start();
        return;
    }

    if (strcmp(cmd, WS_CMD_STOP) == 0) {
        _motor.stop();
        return;
    }

    if (strcmp(cmd, WS_CMD_SET_SPEED) == 0) {
        float value = doc["value"] | 0.0f;
        _motor.setTargetSpeed(value);
        return;
    }

    if (strcmp(cmd, WS_CMD_SET_INCLINE) == 0) {
        float value = doc["value"] | 0.0f;
        _incline.setTargetDegrees(value);
        return;
    }

    if (strcmp(cmd, WS_CMD_HOMING) == 0) {
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

    if (strcmp(cmd, WS_CMD_SET_PID) == 0) {
        float kp = constrain((float)(doc["kp"] | (double)_motor.getPidKp()), 0.0f, 20.0f);
        float ki = constrain((float)(doc["ki"] | (double)_motor.getPidKi()), 0.0f,  5.0f);
        float kd = constrain((float)(doc["kd"] | (double)_motor.getPidKd()), 0.0f,  2.0f);
        _motor.setPidGains(kp, ki, kd);
        _motor.savePidToNvs();
        sendPidConfig();   // broadcast — confirma para todos os clientes
        return;
    }

    Serial.printf("[WS] Unknown command: %s\n", cmd);
}

void WebSocketHandler::sendStatus() {
    if (_connectedClients == 0) return;

    JsonDocument doc;
    bool running = _motor.isRunning();

    doc["type"]          = WS_TYPE_STATUS;
    doc["running"]       = running;
    doc["speed"]         = running ? roundf(_sensor.getSpeedKmh() * 100.0f) / 100.0f : 0.0f;
    doc["rpm"]           = running ? roundf(_sensor.getRPM()) : 0.0f;
    doc["targetSpeed"]   = roundf(_motor.getTargetSpeed() * 100.0f) / 100.0f;
    doc["incline"]       = roundf(_incline.getCurrentDegrees() * 10.0f) / 10.0f;
    doc["targetIncline"] = roundf(_incline.getTargetDegrees() * 10.0f) / 10.0f;
    doc["pwmPct"]        = roundf(_motor.getCurrentPwmPct() * 10.0f) / 10.0f;
    doc["heartbeatOk"]   = _motor.isHeartbeatOk();
    doc["openLoop"]      = (bool)OPEN_LOOP_MODE;
    doc["homingDone"]    = _incline.isHomingDone();
    doc["fault"]         = (uint8_t)_motor.getFault();
    doc["dbgPulses"]     = _sensor.getPulseCount();     // DEBUG
    doc["dbgPeriodUs"]   = _sensor.getLastPeriodUs();  // DEBUG

    broadcastJson(doc);
}

void WebSocketHandler::sendPidConfig(AsyncWebSocketClient* client) {
    JsonDocument doc;
    doc["type"] = WS_TYPE_PID_CONFIG;
    doc["kp"]   = _motor.getPidKp();
    doc["ki"]   = _motor.getPidKi();
    doc["kd"]   = _motor.getPidKd();
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

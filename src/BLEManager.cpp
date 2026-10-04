#include "BLEManager.h"

BLEManager::BLEManager(Motor& motor, Incline& incline, SpeedSensor& sensor)
    : _motor(motor), _incline(incline), _sensor(sensor) {}

void BLEManager::begin() {
    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);   // +9 dBm máximo
    NimBLEDevice::setMTU(512);                 // aceita negociação de MTU até 512 bytes

    _server = NimBLEDevice::createServer();
    _server->setCallbacks(this);

    NimBLEService* svc = _server->createService(BLE_SERVICE_UUID);

    // TX — ESP envia status via Notify
    _txChar = svc->createCharacteristic(
        BLE_CHAR_TX_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );

    // RX — App envia comandos JSON (WRITE_NR = sem resposta, menor latência)
    _rxChar = svc->createCharacteristic(
        BLE_CHAR_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    _rxChar->setCallbacks(this);

    _server->start();   // NimBLE v2: inicia todos os serviços registrados

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SERVICE_UUID);
    adv->start();

    Serial.printf("[BLE] Advertising as \"%s\" (NUS)\n", BLE_DEVICE_NAME);
}

void BLEManager::update() {
    if (_connectedCount == 0) return;

    unsigned long now = millis();
    if (now - _lastStatusMs >= BLE_STATUS_INTERVAL_MS) {
        _lastStatusMs = now;
        sendStatus();
    }
}

// ─── Callbacks de servidor ────────────────────────────────────────────────────

void BLEManager::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
    _connectedCount++;
    _motor.resetHeartbeat();   // período de carência para o app enviar o primeiro HB
    Serial.printf("[BLE] Client connected (addr: %s, total: %u)\n",
                  connInfo.getAddress().toString().c_str(), _connectedCount);

    // Envia boas-vindas com versão e modo de operação
    JsonDocument doc;
    doc["type"]     = WS_TYPE_CONNECTED;
    doc["version"]  = "1.0";
    doc["openLoop"] = (bool)OPEN_LOOP_MODE;
    notifyJson(doc);
}

void BLEManager::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
    if (_connectedCount > 0) _connectedCount--;
    Serial.printf("[BLE] Client disconnected (reason: %d, remaining: %u)\n",
                  reason, _connectedCount);
    // Reinicia advertising para aceitar nova conexão
    NimBLEDevice::startAdvertising();
}

// ─── Callback de característica RX ───────────────────────────────────────────

void BLEManager::onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) {
    std::string val = pChar->getValue();
    if (!val.empty()) handleCommand(val);
}

// ─── Processamento de comandos (mesmo protocolo do WebSocket) ─────────────────

void BLEManager::handleCommand(const std::string& payload) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload.c_str(), payload.size());
    if (err) {
        Serial.printf("[BLE] JSON error: %s\n", err.c_str());
        return;
    }

    const char* cmd = doc["cmd"] | "";

    if (strcmp(cmd, WS_CMD_HEARTBEAT) == 0) {
        _motor.resetHeartbeat();
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
        _motor.setTargetSpeed(doc["value"] | 0.0f);
        return;
    }
    if (strcmp(cmd, WS_CMD_SET_INCLINE) == 0) {
        _incline.setTargetDegrees(doc["value"] | 0.0f);
        return;
    }
    if (strcmp(cmd, WS_CMD_HOMING) == 0) {
        _incline.startHoming();
        return;
    }
    if (strcmp(cmd, WS_CMD_CLEAR_FAULT) == 0) {
        _motor.clearFault();
        sendStatus();   // feedback imediato após limpar falha
        return;
    }
    if (strcmp(cmd, WS_CMD_GET_STATUS) == 0) {
        sendStatus();
        return;
    }

    Serial.printf("[BLE] Unknown command: %s\n", cmd);
}

// ─── Envio de status periódico ───────────────────────────────────────────────

void BLEManager::sendStatus() {
    if (_connectedCount == 0 || !_txChar) return;

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

    notifyJson(doc);
}

void BLEManager::notifyJson(const JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    _txChar->notify(reinterpret_cast<const uint8_t*>(out.c_str()), out.length());
}

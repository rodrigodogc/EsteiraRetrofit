#include "EspNowHandler.h"

// Ponteiro global para os callbacks estáticos do ESP-NOW acessarem a
// instância (mesmo padrão já usado em SpeedSensor.cpp para a ISR do encoder)
static EspNowHandler* _handlerInstance = nullptr;

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Assinatura ANTIGA (pré-IDF5) — confirmado que este toolchain resolve para
// arduino-esp32 2.0.17/IDF 4.4.7, apesar do versionamento "3.x" do PlatformIO.
// Numa futura migração de platform para IDF5+, essa assinatura muda para
// (const esp_now_recv_info_t* info, const uint8_t* data, int len).
static void onEspNowRecv(const uint8_t* macAddr, const uint8_t* data, int len) {
    if (_handlerInstance) _handlerInstance->onRecv(macAddr, data, len);
}

static void onEspNowSent(const uint8_t* macAddr, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.println("[ESPNOW] Falha no envio");
    }
}

EspNowHandler::EspNowHandler(Motor& motor, Incline& incline, SpeedSensor& sensor)
    : _motor(motor), _incline(incline), _sensor(sensor) {}

void EspNowHandler::begin() {
    _handlerInstance = this;

    // ESP-NOW precisa do rádio WiFi ativo. Se WIFI_ENABLED=1, o WiFiManager já
    // deixou o rádio em STA ou AP_STA — não mexe nesse caso. Se WIFI_ENABLED=0
    // (ou qualquer cenário em que ninguém ligou o rádio ainda), liga STA aqui.
    if (WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_STA);
    }

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] Falha ao inicializar esp_now_init()");
        return;
    }

    esp_now_register_recv_cb(onEspNowRecv);
    esp_now_register_send_cb(onEspNowSent);

    // Peer de broadcast — esp_now_send() pra FF:FF:FF:FF:FF:FF exige que o
    // endereço esteja na peer list também no lado que envia, mesmo sendo broadcast.
    esp_now_peer_info_t bcastPeer = {};
    memcpy(bcastPeer.peer_addr, BROADCAST_MAC, 6);
    bcastPeer.channel = 0;   // 0 = usa o canal atual da STA/AP, resolvido dinamicamente
    bcastPeer.encrypt = false;
    bcastPeer.ifidx   = WIFI_IF_STA;
    esp_now_add_peer(&bcastPeer);

    Serial.printf("[ESPNOW] Pronto — MAC local: %s\n", WiFi.macAddress().c_str());
}

bool EspNowHandler::isPeerFresh() const {
    return _hasPeer && (millis() - _lastPeerMs) < WS_HEARTBEAT_TIMEOUT_MS;
}

void EspNowHandler::onRecv(const uint8_t* macAddr, const uint8_t* data, int len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        Serial.printf("[ESPNOW] JSON invalido: %s\n", err.c_str());
        return;
    }

    // Pareamento dinâmico — primeiro comando válido de um MAC vira o peer
    // ativo; MAC diferente do peer atual re-pareia (higiene: remove o antigo).
    if (!_hasPeer || memcmp(macAddr, _peerMac, 6) != 0) {
        registerPeer(macAddr);
    }
    _lastPeerMs = millis();

    handleCommand(doc);
}

void EspNowHandler::registerPeer(const uint8_t* macAddr) {
    if (_hasPeer) {
        esp_now_del_peer(_peerMac);
    }
    memcpy(_peerMac, macAddr, 6);
    _hasPeer = true;

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, macAddr, 6);
    peer.channel = 0;
    peer.encrypt = false;
    peer.ifidx   = WIFI_IF_STA;
    esp_now_add_peer(&peer);

    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
    Serial.printf("[ESPNOW] Peer pareado: %s\n", macStr);
}

void EspNowHandler::handleCommand(JsonDocument& doc) {
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
    if (strcmp(cmd, WS_CMD_EMERGENCY_STOP) == 0) {
        _motor.emergencyStop();
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
        return;
    }
    if (strcmp(cmd, WS_CMD_GET_STATUS) == 0) {
        sendStatus();
        return;
    }

    Serial.printf("[ESPNOW] Comando desconhecido: %s\n", cmd);
}

void EspNowHandler::update() {
    unsigned long now = millis();

    if (_hasPeer && (now - _lastStatusMs >= WS_STATUS_INTERVAL_MS)) {
        _lastStatusMs = now;
        sendStatus();
    }

    if (now - _lastBeaconMs >= ESPNOW_BEACON_INTERVAL_MS) {
        _lastBeaconMs = now;
        sendBeacon();
    }
}

void EspNowHandler::sendStatus() {
    bool running = _motor.isRunning();

    JsonDocument doc;
    doc[ESPNOW_KEY_TYPE]           = ESPNOW_TYPE_STATUS;
    doc[ESPNOW_KEY_RUNNING]        = running;
    doc[ESPNOW_KEY_SPEED]          = running ? roundf(_motor.getSpeedFromFcemKmh() * 100.0f) / 100.0f : 0.0f;
    doc[ESPNOW_KEY_TARGET_SPEED]   = roundf(_motor.getTargetSpeed() * 100.0f) / 100.0f;
    doc[ESPNOW_KEY_INCLINE]        = roundf(_incline.getCurrentDegrees() * 10.0f) / 10.0f;
    doc[ESPNOW_KEY_TARGET_INCLINE] = roundf(_incline.getTargetDegrees() * 10.0f) / 10.0f;
    doc[ESPNOW_KEY_PWM]            = roundf(_motor.getCurrentPwmPct() * 10.0f) / 10.0f;
    doc[ESPNOW_KEY_HEARTBEAT_OK]   = _motor.isHeartbeatOk();
    doc[ESPNOW_KEY_OPEN_LOOP]      = _motor.isOpenLoop();
    doc[ESPNOW_KEY_HOMING_DONE]    = _incline.isHomingDone();
    doc[ESPNOW_KEY_FAULT]          = (uint8_t)_motor.getFault();
    doc[ESPNOW_KEY_VBUS]           = roundf(_motor.getVBus() * 100.0f) / 100.0f;
    doc[ESPNOW_KEY_VMOT]           = roundf(_motor.getVMot() * 100.0f) / 100.0f;
    doc[ESPNOW_KEY_CURRENT]        = roundf(_motor.getCurrentA() * 100.0f) / 100.0f;

    size_t size = measureJson(doc);
    if (size > ESP_NOW_MAX_DATA_LEN) {
        Serial.printf("[ESPNOW] Status excede payload maximo (%u > %d) - nao enviado\n",
                      (unsigned)size, ESP_NOW_MAX_DATA_LEN);
        return;
    }

    sendJson(_peerMac, doc);
}

void EspNowHandler::sendBeacon() {
    JsonDocument doc;
    doc[ESPNOW_KEY_TYPE]    = ESPNOW_TYPE_BEACON;
    doc[ESPNOW_KEY_CHANNEL] = (uint8_t)WiFi.channel();
    sendJson(BROADCAST_MAC, doc);
}

void EspNowHandler::sendJson(const uint8_t* macAddr, const JsonDocument& doc) {
    char buf[ESP_NOW_MAX_DATA_LEN];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    esp_now_send(macAddr, (const uint8_t*)buf, len);
}

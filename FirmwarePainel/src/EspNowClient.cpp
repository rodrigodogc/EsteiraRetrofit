#include "EspNowClient.h"

// Ponteiro global pros callbacks estáticos do ESP-NOW acessarem a instância
// (mesmo padrão usado no lado da placa de potência, EsteiraC3/EspNowHandler.cpp).
static EspNowClient* _clientInstance = nullptr;

// Assinatura ANTIGA (pré-IDF5) — mesma família de toolchain do EsteiraC3
// (arduino-esp32 2.0.x/IDF 4.4, apesar da numeração "3.x" que o PlatformIO
// mostra). Confirme a versão resolvida do framework antes de trocar isso.
static void onEspNowRecv(const uint8_t* macAddr, const uint8_t* data, int len) {
    if (_clientInstance) _clientInstance->onRecv(macAddr, data, len);
}

static void onEspNowSent(const uint8_t* macAddr, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.println("[ESPNOW] Falha no envio");
    }
}

void EspNowClient::begin() {
    _clientInstance = this;

    // Se algo já conectou a STA antes (ex: webserver de debug), não mexe —
    // só garante que o rádio está em algum modo ativo.
    if (WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();   // garante que a STA não tenta associar a nenhuma rede
    }

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] Falha ao inicializar esp_now_init()");
        return;
    }

    esp_now_register_recv_cb(onEspNowRecv);
    esp_now_register_send_cb(onEspNowSent);

    Serial.printf("[ESPNOW] MAC local: %s\n", WiFi.macAddress().c_str());
    _startDiscovery();
}

void EspNowClient::_startDiscovery() {
    _state       = EspNowLinkState::DISCOVERING;
    _scanChannel = ESPNOW_CHANNEL_MIN;
    _setChannel(_scanChannel);
    Serial.println("[ESPNOW] Procurando a placa de potencia...");
}

void EspNowClient::_setChannel(uint8_t ch) {
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    _channelSetMs = millis();
}

void EspNowClient::_pairWith(const uint8_t* macAddr, uint8_t channel) {
    if (_hasPeer) {
        esp_now_del_peer(_peerMac);
    }
    memcpy(_peerMac, macAddr, 6);
    _hasPeer = true;
    _state   = EspNowLinkState::PAIRED;

    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, macAddr, 6);
    peer.channel = channel;
    peer.encrypt = false;
    peer.ifidx   = WIFI_IF_STA;
    esp_now_add_peer(&peer);

    // Carência — evita que o timeout de telemetria dispare antes do primeiro
    // status realmente chegar (o pareamento só ocorreu por causa do beacon,
    // que não carrega telemetria nenhuma).
    _status.lastUpdateMs = millis();
    _lastHeartbeatMs      = 0;   // força heartbeat já no próximo update()

    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
    Serial.printf("[ESPNOW] Pareado com %s no canal %u\n", macStr, channel);
}

void EspNowClient::onRecv(const uint8_t* macAddr, const uint8_t* data, int len) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len) != DeserializationError::Ok) return;

    const char* type = doc[KEY_TYPE] | "";

    if (_state == EspNowLinkState::DISCOVERING) {
        if (strcmp(type, TYPE_BEACON) == 0) {
            uint8_t channel = doc[KEY_CHANNEL] | _scanChannel;
            _pairWith(macAddr, channel);
        }
        return;
    }

    // PAIRED — só aceita pacotes do peer conhecido (ignora outros
    // dispositivos ESP-NOW que possam estar por perto no mesmo canal).
    if (memcmp(macAddr, _peerMac, 6) != 0) return;

    if (strcmp(type, TYPE_STATUS) == 0) {
        _parseStatus(doc);
    }
}

void EspNowClient::_parseStatus(JsonDocument& doc) {
    _status.running       = doc[KEY_RUNNING]        | false;
    _status.speed         = doc[KEY_SPEED]          | 0.0f;
    _status.targetSpeed   = doc[KEY_TARGET_SPEED]   | 0.0f;
    _status.incline       = doc[KEY_INCLINE]        | 0.0f;
    _status.targetIncline = doc[KEY_TARGET_INCLINE] | 0.0f;
    _status.pwmPct        = doc[KEY_PWM]            | 0.0f;
    _status.heartbeatOk   = doc[KEY_HEARTBEAT_OK]   | false;
    _status.openLoop      = doc[KEY_OPEN_LOOP]      | false;
    _status.homingDone    = doc[KEY_HOMING_DONE]    | false;
    _status.fault         = doc[KEY_FAULT]          | (uint8_t)0;
    _status.vBus          = doc[KEY_VBUS]           | 0.0f;
    _status.vMot          = doc[KEY_VMOT]           | 0.0f;
    _status.current       = doc[KEY_CURRENT]        | 0.0f;
    _status.lastUpdateMs  = millis();
}

bool EspNowClient::isTelemetryFresh() const {
    return (millis() - _status.lastUpdateMs) < ESPNOW_PEER_TIMEOUT_MS;
}

void EspNowClient::update() {
    unsigned long now = millis();

    if (_state == EspNowLinkState::DISCOVERING) {
        // Se a STA estiver associada a uma rede WiFi real (ex: webserver de
        // debug), o canal já está travado pelo roteador — forçar
        // esp_wifi_set_channel() aqui derrubaria essa conexão. Só ouve
        // passivamente no canal atual nesse caso, sem varrer.
        if (WiFi.status() == WL_CONNECTED) return;

        if (now - _channelSetMs >= ESPNOW_CHANNEL_DWELL_MS) {
            _scanChannel++;
            if (_scanChannel > ESPNOW_CHANNEL_MAX) _scanChannel = ESPNOW_CHANNEL_MIN;
            _setChannel(_scanChannel);
        }
        return;
    }

    // PAIRED
    if (!isTelemetryFresh()) {
        Serial.println("[ESPNOW] Telemetria expirou - voltando a procurar");
        _startDiscovery();
        return;
    }

    if (now - _lastHeartbeatMs >= ESPNOW_HEARTBEAT_INTERVAL_MS) {
        _lastHeartbeatMs = now;
        _sendCommand(CMD_HEARTBEAT);
    }
}

void EspNowClient::sendStart()         { _sendCommand(CMD_START); }
void EspNowClient::sendStop()          { _sendCommand(CMD_STOP); }
void EspNowClient::sendEmergencyStop() { _sendCommand(CMD_EMERGENCY_STOP); }
void EspNowClient::sendClearFault()    { _sendCommand(CMD_CLEAR_FAULT); }
void EspNowClient::sendHoming()        { _sendCommand(CMD_HOMING); }

void EspNowClient::sendSetSpeed(float kmh)   { _sendCommandValue(CMD_SET_SPEED, kmh); }
void EspNowClient::sendSetIncline(float deg) { _sendCommandValue(CMD_SET_INCLINE, deg); }

void EspNowClient::_sendCommand(const char* cmd) {
    if (!_hasPeer) return;
    JsonDocument doc;
    doc["cmd"] = cmd;
    _sendJson(doc);
}

void EspNowClient::_sendCommandValue(const char* cmd, float value) {
    if (!_hasPeer) return;
    JsonDocument doc;
    doc["cmd"]   = cmd;
    doc["value"] = value;
    _sendJson(doc);
}

void EspNowClient::_sendJson(JsonDocument& doc) {
    char buf[128];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    esp_now_send(_peerMac, (const uint8_t*)buf, len);
}

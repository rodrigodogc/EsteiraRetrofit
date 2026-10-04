#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "PowerBoardProtocol.h"

enum class EspNowLinkState : uint8_t {
    DISCOVERING,   // varrendo canais 1-13 procurando o beacon da placa
    PAIRED,
};

// Último status recebido da placa de potência (telemetria compacta).
struct PowerBoardStatus {
    bool  running       = false;
    float speed         = 0.0f;   // km/h, real (via FCEM)
    float targetSpeed   = 0.0f;
    float incline       = 0.0f;
    float targetIncline = 0.0f;
    float pwmPct        = 0.0f;
    bool  heartbeatOk   = false;
    bool  openLoop      = false;
    bool  homingDone    = false;
    uint8_t fault       = 0;
    float vBus          = 0.0f;
    float vMot          = 0.0f;
    float current       = 0.0f;
    unsigned long lastUpdateMs = 0;
};

// Cliente ESP-NOW do painel — descoberta por varredura de canal (consome o
// beacon broadcast que a placa de potência já transmite), pareamento
// dinâmico, envio de comandos e heartbeat, recepção/parse de telemetria.
class EspNowClient {
public:
    void begin();
    void update();   // chamar a cada loop — varredura/heartbeat/timeout

    bool isPaired()         const { return _state == EspNowLinkState::PAIRED; }
    bool isTelemetryFresh() const;

    const PowerBoardStatus& status() const { return _status; }

    void sendStart();
    void sendStop();
    void sendEmergencyStop();
    void sendSetSpeed(float kmh);
    void sendSetIncline(float deg);
    void sendClearFault();
    void sendHoming();
    // heartbeat é automático dentro de update() enquanto pareado

    // Chamado pelo callback estático de recepção — deve ser público
    void onRecv(const uint8_t* macAddr, const uint8_t* data, int len);

private:
    EspNowLinkState _state = EspNowLinkState::DISCOVERING;

    uint8_t _peerMac[6] = {0, 0, 0, 0, 0, 0};
    bool    _hasPeer    = false;

    uint8_t       _scanChannel = ESPNOW_CHANNEL_MIN;
    unsigned long _channelSetMs = 0;

    unsigned long _lastHeartbeatMs = 0;

    PowerBoardStatus _status;

    void _startDiscovery();
    void _setChannel(uint8_t ch);
    void _pairWith(const uint8_t* macAddr, uint8_t channel);
    void _sendCommand(const char* cmd);
    void _sendCommandValue(const char* cmd, float value);
    void _sendJson(JsonDocument& doc);
    void _parseStatus(JsonDocument& doc);
};

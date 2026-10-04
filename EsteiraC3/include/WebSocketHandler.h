#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "WSCommands.h"
#include "Motor.h"
#include "Incline.h"
#include "SpeedSensor.h"
#include "EspNowHandler.h"

class WebSocketHandler {
public:
    WebSocketHandler(AsyncWebServer& server, Motor& motor,
                     Incline& incline, SpeedSensor& sensor,
                     EspNowHandler& espNow);

    void begin();
    void update();   // chamar no loop — envia status e verifica heartbeat

    // Retorna true se há ao menos um cliente conectado e com heartbeat válido
    bool isClientAlive() const;

    AsyncWebSocket& ws() { return _ws; }

private:
    AsyncWebSocket  _ws;
    Motor&          _motor;
    Incline&        _incline;
    SpeedSensor&    _sensor;
    EspNowHandler&  _espNow;

    unsigned long   _lastStatusMs     = 0;
    uint32_t        _connectedClients = 0;
    bool            _maintenanceActive = false;   // true = ao menos um cliente pediu telemetria elétrica extra

    void onEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                 AwsEventType type, void* arg, uint8_t* data, size_t len);

    void handleMessage(AsyncWebSocketClient* client,
                       const uint8_t* data, size_t len);

    // O painel físico, quando conectado, é a autoridade de controle de
    // movimento (start/stop/velocidade/inclinação) — evita que a página web e
    // o painel disputem o mesmo alvo ao mesmo tempo (desencontro observado ao
    // usar os dois juntos). Web continua podendo monitorar/ajustar tuning
    // (PID, FCEM_MAX_V, modo de malha) e mandar emergencyStop a qualquer momento.
    bool panelHasPriority() const { return _espNow.isPeerFresh(); }
    void rejectForPanelPriority(AsyncWebSocketClient* client);

    void sendStatus();
    void sendPidConfig(AsyncWebSocketClient* client = nullptr);
    void sendTo(AsyncWebSocketClient* client, const JsonDocument& doc);
    void broadcastJson(const JsonDocument& doc);
};

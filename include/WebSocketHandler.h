#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "WSCommands.h"
#include "Motor.h"
#include "Incline.h"
#include "SpeedSensor.h"

class WebSocketHandler {
public:
    WebSocketHandler(AsyncWebServer& server, Motor& motor,
                     Incline& incline, SpeedSensor& sensor);

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

    unsigned long   _lastStatusMs     = 0;
    uint32_t        _connectedClients = 0;

    void onEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                 AwsEventType type, void* arg, uint8_t* data, size_t len);

    void handleMessage(AsyncWebSocketClient* client,
                       const uint8_t* data, size_t len);

    void sendStatus();
    void sendPidConfig(AsyncWebSocketClient* client = nullptr);
    void sendTo(AsyncWebSocketClient* client, const JsonDocument& doc);
    void broadcastJson(const JsonDocument& doc);
};

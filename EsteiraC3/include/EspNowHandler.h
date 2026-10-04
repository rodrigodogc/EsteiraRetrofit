#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "WSCommands.h"
#include "EspNowCommands.h"
#include "Motor.h"
#include "Incline.h"
#include "SpeedSensor.h"

// Canal de comando/telemetria com o painel superior (outra ESP32, fora deste
// projeto) via ESP-NOW. Mesmo protocolo de comandos do WebSocket (WSCommands.h).
// Autoridade sobre o motor continua sendo sempre esta placa, mas comandos de
// movimento (start/stop/setSpeed/setIncline/homing) vindos da web são
// recusados enquanto o painel estiver conectado (ver
// WebSocketHandler::panelHasPriority()) — evita que os dois disputem o mesmo
// alvo ao mesmo tempo. emergencyStop e comandos de tuning/monitoramento
// continuam liberados pelos dois canais sempre.
//
// Pareamento é dinâmico: não há MAC fixo em Config.h. O primeiro comando JSON
// válido recebido de qualquer MAC vira o peer ativo (ver registerPeer()).
class EspNowHandler {
public:
    EspNowHandler(Motor& motor, Incline& incline, SpeedSensor& sensor);

    void begin();
    void update();   // chamar no loop — envia telemetria periódica + beacon

    bool isPeerActive() const { return _hasPeer; }   // já pareou alguma vez
    bool isPeerFresh() const;                        // pareado E recebeu algo há pouco (pro LED de status)

    // Chamado pelo callback estático de recepção — deve ser público
    void onRecv(const uint8_t* macAddr, const uint8_t* data, int len);

private:
    Motor&       _motor;
    Incline&     _incline;
    SpeedSensor& _sensor;

    bool          _hasPeer      = false;
    uint8_t       _peerMac[6]   = {0, 0, 0, 0, 0, 0};
    unsigned long _lastPeerMs   = 0;   // último recebimento válido do peer ativo
    unsigned long _lastStatusMs = 0;
    unsigned long _lastBeaconMs = 0;

    void handleCommand(JsonDocument& doc);
    void registerPeer(const uint8_t* macAddr);
    void sendStatus();
    void sendBeacon();
    void sendJson(const uint8_t* macAddr, const JsonDocument& doc);
};

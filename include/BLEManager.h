#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "Motor.h"
#include "Incline.h"
#include "SpeedSensor.h"
#include "WSCommands.h"

// Implementa BLE Nordic UART Service (NUS) — mesmo protocolo JSON do WebSocket.
// TX characteristic (Notify): ESP → App   (status periódico + eventos)
// RX characteristic (Write):  App → ESP   (comandos JSON)
// Heartbeat: App envia {"cmd":"heartbeat"} a cada 1 s; ESP chama motor.resetHeartbeat().
class BLEManager : public NimBLEServerCallbacks,
                   public NimBLECharacteristicCallbacks {
public:
    BLEManager(Motor& motor, Incline& incline, SpeedSensor& sensor);

    void begin();
    void update();   // chamar no loop — envia status por Notify se há cliente conectado

    bool isConnected() const { return _connectedCount > 0; }

private:
    Motor&                _motor;
    Incline&              _incline;
    SpeedSensor&          _sensor;

    NimBLEServer*         _server        = nullptr;
    NimBLECharacteristic* _txChar        = nullptr;   // Notify ESP → App
    NimBLECharacteristic* _rxChar        = nullptr;   // Write  App → ESP

    uint32_t              _connectedCount = 0;
    unsigned long         _lastStatusMs   = 0;

    // NimBLEServerCallbacks
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override;

    // NimBLECharacteristicCallbacks (usado no _rxChar)
    void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override;

    void handleCommand(const std::string& payload);
    void sendStatus();
    void notifyJson(const JsonDocument& doc);
};

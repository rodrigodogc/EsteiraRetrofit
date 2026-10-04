#include <Arduino.h>
#include "Config.h"
#include "SpeedSensor.h"
#include "Motor.h"
#include "Incline.h"
#include "BLEManager.h"

#if WIFI_ENABLED
#include "NVSStorage.h"
#include "WiFiManager.h"
#include "WebSocketHandler.h"
#include "WebServer.h"
#endif

// ─── Instâncias globais ───────────────────────────────────────────────────────
SpeedSensor  speedSensor;
Motor        motor(speedSensor);
Incline      incline;
BLEManager   bleManager(motor, incline, speedSensor);

#if WIFI_ENABLED
NVSStorage          nvs;
WiFiManager         wifiManager(nvs);
AsyncWebServer      asyncServer(80);
WebSocketHandler*   wsHandler = nullptr;
WebServerManager*   webServer = nullptr;
#endif

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Esteira Retrofit Firmware v1.0 ===");
    Serial.printf("Mode: open-loop=%s  WiFi=%s  BLE=YES\n",
                  OPEN_LOOP_MODE ? "YES" : "NO",
                  WIFI_ENABLED   ? "YES" : "NO");

    speedSensor.begin();
    motor.begin();
    incline.begin();
    bleManager.begin();

#if WIFI_ENABLED
    wifiManager.begin();

    // Aguarda conexão inicial (ou timeout → AP mode)
    unsigned long waitStart = millis();
    while (wifiManager.getMode() == WiFiMode::CONNECTING &&
           (millis() - waitStart) < (WIFI_CONNECT_TIMEOUT_MS + 1000)) {
        wifiManager.update();
        delay(100);
    }

    wsHandler = new WebSocketHandler(asyncServer, motor, incline, speedSensor);
    wsHandler->begin();

    webServer = new WebServerManager(nvs, wifiManager, *wsHandler);
    webServer->begin();

    Serial.printf("[Main] IP: %s\n", wifiManager.getIPAddress().c_str());
#endif

    Serial.println("[Main] Setup complete");
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
    speedSensor.update();
    motor.update();
    incline.update();
    bleManager.update();

#if WIFI_ENABLED
    wifiManager.update();
    if (wsHandler) wsHandler->update();
#endif
}

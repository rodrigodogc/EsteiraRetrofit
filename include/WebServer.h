#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "NVSStorage.h"
#include "WiFiManager.h"
#include "WebSocketHandler.h"

class WebServerManager {
public:
    WebServerManager(NVSStorage& nvs, WiFiManager& wifi,
                     WebSocketHandler& wsHandler);

    void begin();

    AsyncWebServer& server() { return _server; }

private:
    AsyncWebServer    _server{80};
    NVSStorage&       _nvs;
    WiFiManager&      _wifi;
    WebSocketHandler& _wsHandler;

    void setupStaticFiles();
    void setupAPIRoutes();
};

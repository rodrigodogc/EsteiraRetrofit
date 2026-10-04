#include "WebServer.h"
#include <ArduinoJson.h>

WebServerManager::WebServerManager(NVSStorage& nvs, WiFiManager& wifi,
                                   WebSocketHandler& wsHandler)
    : _nvs(nvs), _wifi(wifi), _wsHandler(wsHandler) {}

void WebServerManager::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("[HTTP] LittleFS mount failed");
        return;
    }
    Serial.println("[HTTP] LittleFS mounted");

    // Permite acesso cross-origin (dev server, apps externos)
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin",  "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    _server.addHandler(&_wsHandler.ws());
    setupStaticFiles();
    setupAPIRoutes();
    _server.begin();
    Serial.println("[HTTP] HTTP server started on port 80");
}

void WebServerManager::setupStaticFiles() {
    // Serve arquivos da pasta /data via LittleFS
    _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // 404
    _server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "Not found");
    });
}

void WebServerManager::setupAPIRoutes() {
    // GET /api/status — informações de rede
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["ip"]        = _wifi.getIPAddress();
        doc["mode"]      = _wifi.isAPMode() ? "AP" : (_wifi.isConnected() ? "STA" : "OFFLINE");
        doc["hostname"]  = String(MDNS_HOSTNAME) + ".local";

        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // POST /api/wifi — salva credenciais e reconecta
    // Body: {"ssid": "...", "password": "..."}
    _server.on("/api/wifi", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            String ssid = doc["ssid"] | "";
            String pass = doc["password"] | "";

            if (ssid.length() == 0) {
                req->send(400, "application/json", "{\"error\":\"SSID required\"}");
                return;
            }

            _wifi.connectTo(ssid, pass);
            req->send(200, "application/json", "{\"status\":\"connecting\"}");
        });

    // DELETE /api/wifi — apaga credenciais salvas
    _server.on("/api/wifi", HTTP_DELETE, [this](AsyncWebServerRequest* req) {
        _nvs.clearWiFiCredentials();
        req->send(200, "application/json", "{\"status\":\"cleared\"}");
    });
}

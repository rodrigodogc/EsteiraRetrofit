#include "WiFiManager.h"

WiFiManager::WiFiManager(NVSStorage& nvs) : _nvs(nvs) {}

void WiFiManager::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);   // gerenciamos manualmente

    auto creds = _nvs.loadWiFiCredentials();
    if (creds.valid) {
        _savedSSID = creds.ssid;
        _savedPass = creds.password;
        Serial.printf("[WiFi] Connecting to: %s\n", _savedSSID.c_str());
        tryConnect(_savedSSID, _savedPass);
    } else {
        Serial.println("[WiFi] No credentials saved — starting AP");
        startAP();
    }
}

void WiFiManager::update() {
    unsigned long now = millis();

    if (_mode == WiFiMode::CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            _mode = WiFiMode::STA_CONNECTED;
            Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
            setupMDNS();
        } else if (now - _connectStartMs > WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println("[WiFi] Timeout — starting AP");
            WiFi.disconnect();
            startAP();
        }
        return;
    }

    if (_mode == WiFiMode::AP_MODE) {
        _dnsServer.processNextRequest();   // serve DNS captivo para clientes AP
        return;
    }

    if (_mode == WiFiMode::STA_CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Lost connection");
            _mode = WiFiMode::DISCONNECTED;
            _lastReconnectMs = now;
        }
        return;
    }

    // DISCONNECTED: tenta reconectar periodicamente (não no AP mode)
    if (_mode == WiFiMode::DISCONNECTED && _savedSSID.length() > 0) {
        if (now - _lastReconnectMs > WIFI_RECONNECT_INTERVAL_MS) {
            Serial.println("[WiFi] Attempting reconnect...");
            tryConnect(_savedSSID, _savedPass);
        }
    }
}

bool WiFiManager::connectTo(const String& ssid, const String& password) {
    _savedSSID = ssid;
    _savedPass = password;
    _nvs.saveWiFiCredentials(ssid, password);

    if (_mode == WiFiMode::AP_MODE) {
        _dnsServer.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
    }

    tryConnect(ssid, password);
    return true;
}

void WiFiManager::startAP() {
    // AP_STA (não AP puro) — mantém a interface STA disponível pro ESP-NOW
    // continuar funcionando mesmo durante o portal cativo de configuração.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    delay(100);   // AP precisa estabilizar antes de softAPIP() ser válido
    // DNS captivo: qualquer domínio (inclusive esteira.local) → IP do AP
    _dnsServer.start(53, "*", WiFi.softAPIP());
    _mode = WiFiMode::AP_MODE;
    Serial.printf("[WiFi] AP started: %s  IP: %s (DNS captivo ativo)\n",
                  WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
}

String WiFiManager::getIPAddress() const {
    if (_mode == WiFiMode::STA_CONNECTED) return WiFi.localIP().toString();
    if (_mode == WiFiMode::AP_MODE)       return WiFi.softAPIP().toString();
    return "0.0.0.0";
}

void WiFiManager::tryConnect(const String& ssid, const String& password) {
    // Sem o argumento (wifioff=false) — desconecta a associação atual sem
    // desligar o driver WiFi inteiro. Com wifioff=true, esp_wifi_stop() é
    // chamado e derruba o estado interno do ESP-NOW (peer list, callbacks) a
    // cada tentativa de reconexão, inclusive a cada retry periódico enquanto
    // desconectado — quebraria a comunicação com o painel superior.
    WiFi.disconnect();
    delay(100);
    WiFi.begin(ssid.c_str(), password.c_str());
    _mode            = WiFiMode::CONNECTING;
    _connectStartMs  = millis();
}

void WiFiManager::setupMDNS() {
    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[mDNS] http://%s.local\n", MDNS_HOSTNAME);
    } else {
        Serial.println("[mDNS] MDNS.begin() falhou");
    }
}

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "NVSStorage.h"
#include "Config.h"

enum class WiFiMode { DISCONNECTED, CONNECTING, STA_CONNECTED, AP_MODE };

class WiFiManager {
public:
    WiFiManager(NVSStorage& nvs);

    void begin();
    void update();    // chamar no loop — gerencia reconexão

    WiFiMode getMode()      const { return _mode; }
    String   getIPAddress() const;
    bool     isConnected()  const { return _mode == WiFiMode::STA_CONNECTED; }
    bool     isAPMode()     const { return _mode == WiFiMode::AP_MODE; }

    bool connectTo(const String& ssid, const String& password);
    void startAP();

private:
    NVSStorage&   _nvs;
    DNSServer     _dnsServer;
    WiFiMode      _mode              = WiFiMode::DISCONNECTED;
    unsigned long _connectStartMs    = 0;
    unsigned long _lastReconnectMs   = 0;
    String        _savedSSID;
    String        _savedPass;

    void tryConnect(const String& ssid, const String& password);
    void setupMDNS();
};

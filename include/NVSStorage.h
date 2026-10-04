#pragma once
#include <Arduino.h>
#include <Preferences.h>

struct WiFiCredentials {
    String ssid;
    String password;
    bool   valid = false;
};

class NVSStorage {
public:
    NVSStorage() = default;

    bool            saveWiFiCredentials(const String& ssid, const String& password);
    WiFiCredentials loadWiFiCredentials();
    void            clearWiFiCredentials();

private:
    Preferences _prefs;
};

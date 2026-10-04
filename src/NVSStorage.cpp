#include "NVSStorage.h"
#include "Config.h"

bool NVSStorage::saveWiFiCredentials(const String& ssid, const String& password) {
    _prefs.begin(NVS_NAMESPACE, false);
    bool ok = _prefs.putString(NVS_KEY_SSID, ssid) &&
              _prefs.putString(NVS_KEY_PASS, password);
    _prefs.end();
    return ok;
}

WiFiCredentials NVSStorage::loadWiFiCredentials() {
    _prefs.begin(NVS_NAMESPACE, true);   // read-only
    WiFiCredentials creds;
    creds.ssid     = _prefs.getString(NVS_KEY_SSID, "");
    creds.password = _prefs.getString(NVS_KEY_PASS, "");
    creds.valid    = creds.ssid.length() > 0;
    _prefs.end();
    return creds;
}

void NVSStorage::clearWiFiCredentials() {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.remove(NVS_KEY_SSID);
    _prefs.remove(NVS_KEY_PASS);
    _prefs.end();
}

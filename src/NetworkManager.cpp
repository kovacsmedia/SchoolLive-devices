#include "NetworkManager.h"
#include "PersistStore.h"

extern PersistStore store;

NetworkManager::NetworkManager() {}

void NetworkManager::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    String hostname = "schoollive-" + WiFi.macAddress();
    hostname.replace(":", "");
    hostname.toLowerCase();
    WiFi.setHostname(hostname.c_str());
    loadFromNVS();
}

void NetworkManager::loadFromNVS() {
    knownNetworks.clear();
    if (!store.hasWifi()) return;

    WiFiCreds creds;
    creds.ssid = store.getWifiSsid();
    creds.pass = store.getWifiPass();
    creds.user = store.getWifiUser(); // Enterprise user (lehet üres)
    knownNetworks.push_back(creds);
}

bool NetworkManager::syncTimeBlocking() {
    loadFromNVS();
    if (knownNetworks.empty()) return false;

    WiFiCreds& c = knownNetworks[0];
    if (c.user.length() > 0) connectEnterprise(c.ssid, c.user, c.pass);
    else connectPersonal(c.ssid, c.pass);

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 40) {
        delay(500);
        retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        configTime(3600, 3600, "pool.ntp.org", "time.google.com");
        struct tm t;
        for (int i = 0; i < 10; i++) {
            if (getLocalTime(&t)) {
                _timeSynced = true;
                _lastTimeSync = millis();
                return true;
            }
            delay(200);
        }
    }
    return false;
}

void NetworkManager::connectEnterprise(String ssid, String user, String pass) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)user.c_str(), user.length());
    esp_wifi_sta_wpa2_ent_set_username((uint8_t*)user.c_str(), user.length());
    esp_wifi_sta_wpa2_ent_set_password((uint8_t*)pass.c_str(), pass.length());
    esp_wifi_sta_wpa2_ent_enable();
    WiFi.begin(ssid.c_str());
}

void NetworkManager::connectPersonal(String ssid, String pass) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
}

void NetworkManager::loop() {
    handleWiFi();
    if (isConnected()) handleNTP();
}

void NetworkManager::handleWiFi() {
    if (knownNetworks.empty()) return;
    if (WiFi.status() != WL_CONNECTED) {
        if (millis() - _lastWifiCheck > 30000) {
            WiFiCreds& c = knownNetworks[0];
            if (c.user.length() > 0) connectEnterprise(c.ssid, c.user, c.pass);
            else connectPersonal(c.ssid, c.pass);
            _lastWifiCheck = millis();
        }
    }
}

void NetworkManager::handleNTP() {
    if (!_timeSynced || (millis() - _lastTimeSync > 3600000)) {
        configTime(3600, 3600, "pool.ntp.org", "time.google.com");
        struct tm t;
        if (getLocalTime(&t)) {
            _timeSynced = true;
            _lastTimeSync = millis();
        }
    }
}

void NetworkManager::updateFirmware(const char* firmwareUrl) {
    if (WiFi.status() != WL_CONNECTED) return;
    WiFiClientSecure client;
    client.setInsecure();
    httpUpdate.update(client, firmwareUrl);
}

bool NetworkManager::isConnected() { return WiFi.status() == WL_CONNECTED; }
bool NetworkManager::isTimeSynced() { return _timeSynced; }
String NetworkManager::getIP() { return WiFi.localIP().toString(); }
int32_t NetworkManager::getRSSI() { return WiFi.RSSI(); }
String NetworkManager::getCurrentSSID() { return WiFi.SSID(); }
String NetworkManager::getStoredSSID() { return WiFi.SSID(); }
String NetworkManager::getStoredUser() { return ""; }
String NetworkManager::getStoredDeviceID() { return WiFi.macAddress(); }

String NetworkManager::fetchFile(const char* url) {
    if (WiFi.status() != WL_CONNECTED) return "";
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    http.setTimeout(5000);
    if (http.begin(client, url)) {
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
            String s = http.getString();
            http.end();
            return s;
        }
        http.end();
    }
    return "";
}

struct tm NetworkManager::getTimeInfo() {
    struct tm t = {0};
    getLocalTime(&t);
    return t;
}

bool NetworkManager::saveCredentials(String ssid, String pass, String user, String devid, String& debugMsg) {
    // Már nem wifi.txt-be ír, NVS-be menti
    store.setWifi(ssid, pass);
    if (user.length() > 0) store.setWifiUser(user);
    loadFromNVS();
    debugMsg = "Saved to NVS";
    return true;
}

void NetworkManager::loadWifiTxt() {
    // Legacy – már nem használjuk, NVS-ből töltünk
    loadFromNVS();
}
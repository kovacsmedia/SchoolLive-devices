// ─────────────────────────────────────────────────────────────────────────────
// NetworkManager.cpp – S3 verzió
// Újítások:
//   • SNTP callback – _timeSynced pontosan beállítva szinkronkor
//   • getCurrentTimeMs() – gettimeofday() µs pontosság
//   • WiFi reconnect: exponenciális backoff eltávolítva (10s fix)
// ─────────────────────────────────────────────────────────────────────────────
#include "NetworkManager.h"
#include "PersistStore.h"

extern PersistStore store;
NetworkManager* NetworkManager::_instance = nullptr;

void NetworkManager::sntpCallback(struct timeval* tv) {
    if (_instance) {
        _instance->_timeSynced   = true;
        _instance->_lastTimeSync = millis();
        Serial.printf("[NTP] Szinkronizálva: %lld.%06ld\n",
                      (long long)tv->tv_sec, tv->tv_usec);
    }
}

NetworkManager::NetworkManager() { _instance = this; }

void NetworkManager::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    String hostname = "schoollive-" + WiFi.macAddress();
    hostname.replace(":", "");
    hostname.toLowerCase();
    WiFi.setHostname(hostname.c_str());

    // SNTP callback regisztrálása
    sntp_set_time_sync_notification_cb(sntpCallback);
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);

    loadFromNVS();
}

void NetworkManager::loadFromNVS() {
    knownNetworks.clear();
    if (!store.hasWifi()) return;
    WiFiCreds c;
    c.ssid = store.getWifiSsid();
    c.pass = store.getWifiPass();
    c.user = store.getWifiUser();
    knownNetworks.push_back(c);
}

bool NetworkManager::syncTimeBlocking() {
    loadFromNVS();
    if (knownNetworks.empty()) return false;

    WiFiCreds& c = knownNetworks[0];
    if (c.user.length() > 0) connectEnterprise(c.ssid, c.user, c.pass);
    else                      connectPersonal(c.ssid, c.pass);

    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(500);

    if (WiFi.status() == WL_CONNECTED) {
        // SNTP inicializálás
        configTime(3600, 3600, "pool.ntp.org", "time.google.com",
                   "time.cloudflare.com");
        struct tm t;
        for (int i = 0; i < 20; i++) {
            if (getLocalTime(&t)) {
                _timeSynced   = true;
                _lastTimeSync = millis();
                Serial.printf("[NTP] Szinkron OK (blocking): %04d-%02d-%02d %02d:%02d:%02d\n",
                              t.tm_year+1900, t.tm_mon+1, t.tm_mday,
                              t.tm_hour, t.tm_min, t.tm_sec);
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
        unsigned long now = millis();
        if (now - _lastWifiCheck > 10000) {
            Serial.println("[WIFI] Lecsatlakozva, újracsatlakozás...");
            WiFiCreds& c = knownNetworks[0];
            if (c.user.length() > 0) connectEnterprise(c.ssid, c.user, c.pass);
            else                      connectPersonal(c.ssid, c.pass);
            _lastWifiCheck = now;
        }
    } else {
        _lastWifiCheck = 0;
    }
}

void NetworkManager::handleNTP() {
    // Az SNTP callback automatikusan frissíti _timeSynced-et
    // Óránkénti re-szinkron ha szükséges
    if (_timeSynced && (millis() - _lastTimeSync > 3600000UL)) {
        configTime(3600, 3600, "pool.ntp.org", "time.google.com");
    }
}

// ── getCurrentTimeMs ─────────────────────────────────────────────────────────
// gettimeofday() microsecond pontossággal → ms visszatérés
int64_t NetworkManager::getCurrentTimeMs() const {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int64_t)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

void NetworkManager::updateFirmware(const char* firmwareUrl) {
    if (WiFi.status() != WL_CONNECTED) return;
    WiFiClientSecure client;
    client.setInsecure();
    httpUpdate.update(client, firmwareUrl);
}

bool NetworkManager::isConnected() { return WiFi.status() == WL_CONNECTED; }

String NetworkManager::fetchFile(const char* url) {
    if (!isConnected()) return "";
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

bool NetworkManager::saveCredentials(String ssid, String pass, String user,
                                      String devid, String& debugMsg) {
    store.setWifi(ssid, pass);
    if (user.length() > 0) store.setWifiUser(user);
    loadFromNVS();
    debugMsg = "Saved to NVS";
    return true;
}

void NetworkManager::loadWifiTxt() { loadFromNVS(); }
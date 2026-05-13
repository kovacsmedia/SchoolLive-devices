#include "SLNetworkManager.h"
#include "PersistStore.h"

extern PersistStore store;

SLNetworkManager::SLNetworkManager() {}

void SLNetworkManager::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    String hostname = "schoollive-" + WiFi.macAddress();
    hostname.replace(":", "");
    hostname.toLowerCase();
    WiFi.setHostname(hostname.c_str());
    loadFromNVS();
}

void SLNetworkManager::loadFromNVS() {
    knownNetworks.clear();
    if (!store.hasWifi()) return;

    WiFiCreds creds;
    creds.ssid     = store.getWifiSsid();
    creds.pass     = store.getWifiPass();
    creds.user     = store.getWifiUser();
    creds.security = store.getWifiSecurity();
    knownNetworks.push_back(creds);

    Serial.printf("[WIFI] Loaded creds: ssid='%s' user='%s' security='%s'\n",
                  creds.ssid.c_str(), creds.user.c_str(), creds.security.c_str());
}

bool SLNetworkManager::syncTimeBlocking() {
    loadFromNVS();
    if (knownNetworks.empty()) return false;

    WiFiCreds& c = knownNetworks[0];
    if (c.security == "WPA2_ENTERPRISE") connectEnterprise(c.ssid, c.user, c.pass);
    else                                 connectPersonal(c.ssid, c.pass);

    // Az enterprise PEAP-MSCHAPv2 handshake hosszabb is lehet (TLS + EAP),
    // ezért 30 sec total timeout (60 × 500 ms) a 20 helyett.
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 60) {
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

void SLNetworkManager::connectEnterprise(String ssid, String user, String pass) {
    /*
     * WPA2 Enterprise (eduroam) — PEAP / MSCHAPv2.
     *
     * Konfiguráció a provisioning UI alapján:
     *   - Hitelesítés:        védett EAP (PEAP)
     *   - Névtelen identity:  NINCS (clear)
     *   - CA tanúsítvány:     NEM szükséges (server cert validation kikapcsolva)
     *   - PEAP verzió:        automatikus (default)
     *   - Belső auth:         MSCHAPv2 (PEAP default phase2)
     *   - Felhasználónév:     `user` (email formátum)
     *   - Jelszó:             `pass`
     *
     * A modern API (`esp_eap_client.h`) az ESP-IDF v5.x-től, a régi
     * `esp_wpa2.h` deprecated.
     */
    Serial.printf("[WIFI] Connecting Enterprise: ssid=%s user=%s\n",
                  ssid.c_str(), user.c_str());

    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);

    // 1) Identity-t NEM állítunk (provisioning UI: "nincs névtelen személyazonosság"),
    //    minden inner és outer azonosítás az `username` mezőből megy.
    esp_eap_client_clear_identity();

    // 2) Username + password.
    esp_eap_client_set_username((const uint8_t*)user.c_str(), user.length());
    esp_eap_client_set_password((const uint8_t*)pass.c_str(), pass.length());

    // 3) CA cert nincs - egyrészt clearelünk, másrészt kikapcsoljuk a
    //    server cert validation időbeli ellenőrzését (egyébként a CA cert
    //    hiánya miatt a handshake amúgy is fail-ne).
    esp_eap_client_clear_ca_cert();
    esp_eap_client_clear_certificate_and_key();
    esp_eap_client_set_disable_time_check(true);

    // 4) PEAP phase2 = MSCHAPv2 (default, explicit nem kell, de a tisztaság
    //    kedvéért beállítjuk - a PEAP esetén ezt a stack ismeri).
    // Megjegyzés: esp_eap_client_set_ttls_phase2_method() csak EAP-TTLS-re,
    // PEAP-re nincs külön phase2 method beállítás (PEAP belső MSCHAPv2 default).

    // 5) Enterprise mód bekapcsolása. Az új API: `esp_wifi_sta_enterprise_enable()`.
    esp_err_t err = esp_wifi_sta_enterprise_enable();
    if (err != ESP_OK) {
        Serial.printf("[WIFI] esp_wifi_sta_enterprise_enable failed: %d\n", (int)err);
    }

    // 6) WiFi.begin SSID-vel — password mezőt NEM adunk (a credentials az EAP-ban van).
    WiFi.begin(ssid.c_str());
}

void SLNetworkManager::connectPersonal(String ssid, String pass) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
}

void SLNetworkManager::loop() {
    handleWiFi();
    if (isConnected()) handleNTP();
}

void SLNetworkManager::handleWiFi() {
    if (knownNetworks.empty()) return;

    if (WiFi.status() != WL_CONNECTED) {
        const unsigned long now = millis();
        if (now - _lastWifiCheck > 10000) {
            Serial.println("[WIFI] Disconnected, reconnecting...");
            WiFiCreds& c = knownNetworks[0];
            if (c.security == "WPA2_ENTERPRISE") connectEnterprise(c.ssid, c.user, c.pass);
            else                                 connectPersonal(c.ssid, c.pass);
            _lastWifiCheck = now;
        }
    } else {
        _lastWifiCheck = 0; // reset hogy lecsatlakozás után azonnal próbáljon
    }
}

void SLNetworkManager::handleNTP() {
    if (!_timeSynced || (millis() - _lastTimeSync > 3600000)) {
        configTime(3600, 3600, "pool.ntp.org", "time.google.com");
        struct tm t;
        if (getLocalTime(&t)) {
            _timeSynced = true;
            _lastTimeSync = millis();
        }
    }
}

void SLNetworkManager::updateFirmware(const char* firmwareUrl) {
    if (WiFi.status() != WL_CONNECTED) return;
    WiFiClientSecure client;
    client.setInsecure();
    httpUpdate.update(client, firmwareUrl);
}

bool SLNetworkManager::isConnected() { return WiFi.status() == WL_CONNECTED; }
bool SLNetworkManager::isTimeSynced() { return _timeSynced; }
String SLNetworkManager::getIP() { return WiFi.localIP().toString(); }
int32_t SLNetworkManager::getRSSI() { return WiFi.RSSI(); }
String SLNetworkManager::getCurrentSSID() { return WiFi.SSID(); }
String SLNetworkManager::getStoredSSID() { return WiFi.SSID(); }
String SLNetworkManager::getStoredUser() { return ""; }
String SLNetworkManager::getStoredDeviceID() { return WiFi.macAddress(); }

String SLNetworkManager::fetchFile(const char* url) {
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

struct tm SLNetworkManager::getTimeInfo() {
    struct tm t = {0};
    getLocalTime(&t);
    return t;
}

bool SLNetworkManager::saveCredentials(String ssid, String pass, String user, String devid, String& debugMsg) {
    // Már nem wifi.txt-be ír, NVS-be menti
    store.setWifi(ssid, pass);
    if (user.length() > 0) store.setWifiUser(user);
    loadFromNVS();
    debugMsg = "Saved to NVS";
    return true;
}

void SLNetworkManager::loadWifiTxt() {
    // Legacy – már nem használjuk, NVS-ből töltünk
    loadFromNVS();
}
#include "NetworkManager.h"

NetworkManager::NetworkManager() {}

void NetworkManager::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); 
    if (String(DEVICE_ID).length() > 0) WiFi.setHostname(DEVICE_ID);
    loadWifiTxt();
}

void NetworkManager::loadWifiTxt() {
    knownNetworks.clear();
    
    if (!LittleFS.exists("/wifi.txt")) return;

    File file = LittleFS.open("/wifi.txt", "r");
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        std::vector<String> parts;
        int lastQuote = -1;
        
        for (int i = 0; i < line.length(); i++) {
            if (line[i] == '"') {
                if (lastQuote == -1) lastQuote = i; 
                else {
                    parts.push_back(line.substring(lastQuote + 1, i));
                    lastQuote = -1;
                }
            }
        }

        if (parts.size() >= 2) {
            WiFiCreds creds;
            creds.ssid = parts[0];
            if (parts.size() == 3) {
                creds.user = parts[1];
                creds.pass = parts[2];
            } else {
                creds.user = "";       
                creds.pass = parts[1];
            }
            knownNetworks.push_back(creds);
        }
    }
    file.close();
}

bool NetworkManager::saveCredentials(String ssid, String pass, String user, String devid, String &debugMsg) {
    File file = LittleFS.open("/wifi.txt", "w");
    if (!file) {
        debugMsg = "FS Err";
        return false;
    }
    
    file.print("\""); file.print(ssid); file.print("\",");
    if (user.length() > 0) {
        file.print("\""); file.print(user); file.print("\",");
    }
    file.print("\""); file.print(pass); file.println("\"");
    
    file.close();
    loadWifiTxt();
    
    debugMsg = "Saved";
    return true;
}

void NetworkManager::connectBestWiFi() {
    if (knownNetworks.empty()) return;

    int n = WiFi.scanNetworks();
    if (n == 0) return;

    for (int i = 0; i < n; ++i) {
        if (WiFi.SSID(i) == "Eduroam") {
            for (const auto& creds : knownNetworks) {
                if (creds.ssid == "Eduroam") {
                    connectEnterprise(creds.ssid, creds.user, creds.pass);
                    return;
                }
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        String scanSSID = WiFi.SSID(i);
        for (const auto& creds : knownNetworks) {
            if (scanSSID == creds.ssid) {
                if (creds.user.length() > 0) connectEnterprise(creds.ssid, creds.user, creds.pass);
                else connectPersonal(creds.ssid, creds.pass);
                return;
            }
        }
    }
    
    // Fallback: Ha nincs találat, próbáljuk az elsőt
    if (!knownNetworks.empty()) {
        WiFiCreds fb = knownNetworks[0];
        if (fb.user.length() > 0) connectEnterprise(fb.ssid, fb.user, fb.pass);
        else connectPersonal(fb.ssid, fb.pass);
    }
}

void NetworkManager::connectEnterprise(String ssid, String user, String pass) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)user.c_str(), user.length());
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)user.c_str(), user.length());
    esp_wifi_sta_wpa2_ent_set_password((uint8_t *)pass.c_str(), pass.length());
    esp_wifi_sta_wpa2_ent_enable();
    WiFi.begin(ssid.c_str());
}

void NetworkManager::connectPersonal(String ssid, String pass) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
}

bool NetworkManager::syncTimeBlocking() {
    if (knownNetworks.empty()) loadWifiTxt();
    if (knownNetworks.empty()) return false;

    connectBestWiFi();

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 40) { // 20mp
        delay(500);
        retries++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        configTime(3600, 3600, "pool.ntp.org", "time.google.com");
        struct tm t;
        for(int i=0; i<10; i++) {
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

void NetworkManager::loop() {
    handleWiFi();
    if (isConnected()) handleNTP();
}

void NetworkManager::handleWiFi() {
    if (knownNetworks.empty()) return;

    if (WiFi.status() != WL_CONNECTED) {
        if (millis() - _lastWifiCheck > 30000) {
            connectBestWiFi();
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
            http.end(); return s;
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

String NetworkManager::getStoredSSID() { return WiFi.SSID(); }
String NetworkManager::getStoredUser() { return ""; } 
String NetworkManager::getStoredDeviceID() { return DEVICE_ID; }
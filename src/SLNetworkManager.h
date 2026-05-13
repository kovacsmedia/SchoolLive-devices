#ifndef SLNETWORKMANAGER_H
#define SLNETWORKMANAGER_H

// SchoolLive hálózatkezelő. Az osztály eredeti neve NetworkManager volt,
// de Arduino-ESP32 v3.x-től a Network/NetworkManager.h egy globális
// NetworkManager osztályt definiál (WiFi.h-on át mindenhol importálódik),
// így átneveztük SLNetworkManager-re, hogy ne ütközzön.

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include <LittleFS.h>
#include <vector>
// Modern WPA2 Enterprise (EAP-PEAP/MSCHAPv2) API. ESP-IDF v5.x-en az `esp_wpa2.h`
// deprecated; a hivatalos felület most az `esp_eap_client.h` + `esp_wifi.h`
// `esp_wifi_sta_enterprise_enable()`.
#include "esp_eap_client.h"
#include "Config.h"

struct WiFiCreds {
    String ssid;
    String user;
    String pass;
    String security;   // "WPA2_PERSONAL" vagy "WPA2_ENTERPRISE"
};

class SLNetworkManager {
public:
    SLNetworkManager();
    void begin();
    void loop();
    bool syncTimeBlocking();

    bool isConnected();
    bool isTimeSynced();
    String getIP();
    int32_t getRSSI();

    String fetchFile(const char* url);
    void updateFirmware(const char* firmwareUrl);
    struct tm getTimeInfo();

    String getCurrentSSID();
    String getStoredSSID();
    String getStoredUser();
    String getStoredDeviceID();

    bool saveCredentials(String ssid, String pass, String user, String devid, String& debugMsg);

private:
    bool _timeSynced = false;
    unsigned long _lastTimeSync = 0;
    unsigned long _lastWifiCheck = 0;

    std::vector<WiFiCreds> knownNetworks;

    void loadFromNVS();
    void loadWifiTxt(); // legacy, NVS-re delegál
    void handleWiFi();
    void handleNTP();
    void connectEnterprise(String ssid, String user, String pass);
    void connectPersonal(String ssid, String pass);
};

#endif
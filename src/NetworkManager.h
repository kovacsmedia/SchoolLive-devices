#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include <LittleFS.h>
#include <vector>
#include "esp_wpa2.h"
#include "Config.h"

// WiFi adatok tárolására szolgáló struktúra
struct WiFiCreds {
    String ssid;
    String user; // Ha üres, akkor Personal, ha van, akkor Enterprise
    String pass;
};

class NetworkManager {
public:
    NetworkManager();
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
    
    // --- EZEK HIÁNYOZTAK, MOST PÓTOLJUK ---
    String getCurrentSSID(); 
    String getStoredSSID();
    String getStoredUser();
    String getStoredDeviceID();
    // --------------------------------------

    // Mentés (Bluetoothhoz) - Ez most már a wifi.txt-be ír
    bool saveCredentials(String ssid, String pass, String user, String devid, String &debugMsg);

private:
    bool _timeSynced = false;
    unsigned long _lastTimeSync = 0;
    unsigned long _lastWifiCheck = 0;
    
    std::vector<WiFiCreds> knownNetworks;

    void handleWiFi();
    void handleNTP();
    
    void loadWifiTxt(); 
    void connectBestWiFi(); 
    void connectEnterprise(String ssid, String user, String pass);
    void connectPersonal(String ssid, String pass);
};

#endif
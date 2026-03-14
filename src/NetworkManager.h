#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include <LittleFS.h>
#include <vector>
#include <sys/time.h>
#include "esp_wpa2.h"
#include "esp_sntp.h"
#include "Config.h"

struct WiFiCreds { String ssid, user, pass; };

class NetworkManager {
public:
    NetworkManager();
    void begin();
    void loop();
    bool syncTimeBlocking();

    bool    isConnected();
    bool    isTimeSynced()   { return _timeSynced; }
    String  getIP()          { return WiFi.localIP().toString(); }
    int32_t getRSSI()        { return WiFi.RSSI(); }

    // Milliszekundum pontosságú szerveridő (NTP alapján)
    int64_t getCurrentTimeMs() const;

    String fetchFile(const char* url);
    void   updateFirmware(const char* firmwareUrl);
    struct tm getTimeInfo();

    String getCurrentSSID()   { return WiFi.SSID(); }
    String getStoredSSID()    { return WiFi.SSID(); }
    String getStoredUser()    { return ""; }
    String getStoredDeviceID(){ return WiFi.macAddress(); }

    bool saveCredentials(String ssid, String pass, String user,
                         String devid, String& debugMsg);

private:
    bool          _timeSynced    = false;
    unsigned long _lastTimeSync  = 0;
    unsigned long _lastWifiCheck = 0;
    std::vector<WiFiCreds> knownNetworks;

    void loadFromNVS();
    void loadWifiTxt();
    void handleWiFi();
    void handleNTP();
    void connectEnterprise(String ssid, String user, String pass);
    void connectPersonal(String ssid, String pass);

    static void sntpCallback(struct timeval* tv);
    static NetworkManager* _instance;
};
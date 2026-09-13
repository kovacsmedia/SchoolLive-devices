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

    /** Nem blokkoló csatlakozás-indítás. A tényleges csatlakozásra NEM vár –
     *  a `loop()` (TaskNetwork) úgyis újrapróbálja, amíg sikerül. */
    void startConnect();

    /** Egy SZOFTVERES újraindítás (szervizgomb, OTA, crash, `ESP.restart()`)
     *  megőrzi az RTC órát, csak a RAM-beli `_timeSynced` flag vész el – és a
     *  libc időzóna-beállítása. Ez a metódus visszaállítja a TZ-t, és ha az
     *  óra hihető értéket mutat, azonnal "szinkronizáltnak" tekinti az időt.
     *  Így az eszköz WiFi NÉLKÜL is tud csengetni közvetlenül újraindulás
     *  után. Tápkimaradás után az RTC nullázódik – ott marad az NTP.
     *  @return true, ha az óra használható */
    bool adoptRtcTimeIfValid();

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
    // Az RTC-ből átvett idő pontos lehet, de driftelhet – ha WiFi lesz,
    // egyszer akkor is kérjünk friss NTP-t, ne csak az 1 órás ciklusban.
    bool _needsNtpRefresh = false;
    unsigned long _lastWifiCheck = 0;

    /*
     * Az újracsatlakozás FOKOZATOS. Ld. handleWiFi() – a teljes WiFi-stack
     * lebontása (`WiFi.disconnect(true)`) 10 másodpercenként megölte a
     * futó TCP-kapcsolódásokat és az esp_netif példányt is újraépítette.
     */
    uint8_t       _wifiSoftRetries = 0;

    std::vector<WiFiCreds> knownNetworks;

    void loadFromNVS();
    void loadWifiTxt(); // legacy, NVS-re delegál
    void handleWiFi();
    void handleNTP();
    void connectEnterprise(String ssid, String user, String pass);
    void connectPersonal(String ssid, String pass);
};

#endif
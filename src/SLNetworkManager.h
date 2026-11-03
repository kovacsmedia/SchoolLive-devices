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

    /*
     * ── A KAPCSOLAT MINŐSÉGE ───────────────────────────────────────────
     *
     * Megfigyelt hiba (2026-11-03): az ESP32 rádiója menet közben
     * „beragad" – a jelszint -50 dBm-ről -80 alá zuhan, miközben a WiFi
     * FORMÁLISAN kapcsolódva marad. A stream szakadozik, a bemondás
     * érthetetlen. Soft reset után a jelszint visszaáll, tehát nem a
     * környezet romlott el, hanem az eszköz rádiója: ha az AP vagy a zaj
     * lenne a baj, az újraindítás nem segítene.
     *
     * Ezért a csatlakozás után rögzítünk egy ALAPSZINTET, és ahhoz
     * viszonyítunk. Abszolút küszöb önmagában nem jó: egy -70-es alapszintű
     * helyen a -75 normális, egy -45-ösön viszont már baj.
     */

    /** Minden ciklusban hívandó – ebből él a figyelő. */
    void updateLinkQuality();

    /** Tartósan, az alapszinthez képest bezuhant-e a jel. */
    bool isLinkDegraded() const { return _linkDegraded; }

    /** A csatlakozáskor mért alapszint (0 = még nincs). */
    int32_t rssiBaseline() const { return _rssiBaseline; }

    /** Simított aktuális jelszint (0 = nincs adat). */
    int32_t rssiSmoothed() const { return _rssiSmoothed; }

    /** Az alapszint újrafelvétele – újracsatlakozás után. */
    void resetLinkBaseline();

    String fetchFile(const char* url);
    void updateFirmware(const char* firmwareUrl);
    struct tm getTimeInfo();

    String getCurrentSSID();
    String getStoredSSID();
    String getStoredUser();
    String getStoredDeviceID();

    bool saveCredentials(String ssid, String pass, String user, String devid, String& debugMsg);

private:
    /*
     * ── A minőségfigyelő küszöbei ──────────────────────────────────────
     *
     * MINDKETTŐNEK teljesülnie kell: a relatív zuhanás mondja meg, hogy
     * ROMLOTT-E, az abszolút alsó határ pedig azt, hogy a romlás egyáltalán
     * számít-e. Egy -30-as alapszintről -58-ra esés 28 dB, de -58 még
     * bőven lejátszható – ott nincs mit tenni.
     */
    static const int32_t LINK_DROP_DB     = 25;    // ennyivel az alapszint alatt
    static const int32_t LINK_ABS_FLOOR   = -78;   // és eközben ennél rosszabb
    static const uint32_t LINK_HOLD_MS    = 60000; // ennyi ideig tartósan

    int32_t  _rssiBaseline   = 0;
    int32_t  _rssiSmoothed   = 0;
    uint32_t _badSinceMs     = 0;
    uint32_t _baselineAtMs   = 0;
    bool     _linkDegraded   = false;
    uint32_t _lastQualityMs  = 0;

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
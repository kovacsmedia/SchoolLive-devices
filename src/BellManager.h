#ifndef BELLMANAGER_H
#define BELLMANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "AudioManager.h"
#include "SLNetworkManager.h"
#include "BackendClient.h"

// Csengetési módok
#define BELL_MODE_OFF   0
#define BELL_MODE_ON    1
#define BELL_MODE_TODAY 2

// Gyári default csengetőhangok. Ezek a firmware LittleFS képében (data/)
// szállítódnak, és a szerver `sounds` listája is MINDIG tartalmazza őket –
// a szinkron-takarítás SOSEM törölheti őket (ld. BellManager.cpp).
#define BELL_DEFAULT_SIGNAL "/jelzocsengo.mp3"
#define BELL_DEFAULT_MAIN   "/kibecsengo.mp3"

// Csengetés típusa
enum class BellType : uint8_t {
    MAIN   = 0,
    SIGNAL = 1
};

// Egy csengetési bejegyzés
struct BellEntry {
    uint8_t  hour;
    uint8_t  minute;
    BellType type;
    char     soundFile[32];  // pl. "kibecsengo.mp3"
};

#define MAX_BELL_ENTRIES 40

// NVS kulcsok – napi cache
#define NVS_BELL_NS         "bellsched"
#define NVS_BELL_DATE       "date"
#define NVS_BELL_COUNT      "count"
#define NVS_BELL_DATA       "data"
#define NVS_BELL_TODAY_VER  "todayVer"

// NVS kulcsok – default schedule
#define NVS_BELL_DEF_NS     "belldef"
#define NVS_BELL_DEF_COUNT  "count"
#define NVS_BELL_DEF_DATA   "data"
#define NVS_BELL_DEF_VER    "ver"

// NVS kulcsok – "ma már elcsengetve" állapot.
//
// KRITIKUS, hogy ez TÚLÉLJE az újraindulást: a csengetésnek van egy 120 mp-es
// pótlási ablaka (BELL_CATCHUP_MAX_S), és ha az eszköz ezen belül újraindul, a
// RAM-beli bitmező elvész, a csengetés pedig ÚJRA megszólal. Egy újraindulási
// ciklusban ez percekig tartó, ismétlődő csengetést okoz.
#define NVS_BELL_DONE_BITS  "hbits"
#define NVS_BELL_DONE_DATE  "hdate"

// LittleFS fájlok – teljes tanévnyi naptár (sablonok + naptár-kivételek).
//
// EZ KORÁBBAN NVS-BEN VOLT, ÉS AZ SÚLYOS HIBA VOLT (2026-09-12):
// az NVS partíció a partitions.csv szerint MINDÖSSZE 0x5000 = 20 480 bájt,
// a mentés felső határa viszont 24 kB volt – vagyis EGYETLEN érték nagyobb
// lehetett, mint a teljes partíció. A tanév naptára így megtöltötte az NVS-t,
// és onnantól MINDEN további írás elbukott: a provisioning `setWifi()` /
// `setDeviceKey()` hívásai csendben nem mentek végbe, az eszköz pedig
// újraindulás után újra provisioning módba esett – örökre.
//
// Az NVS-t megosztjuk a WiFi driverrel is (`wifi:config NVS flash: enabled`),
// tehát ott KIZÁRÓLAG rövid, konfigurációs értékeknek van helye. A tanévnyi
// JSON a LittleFS-re való, ahol 8 MB áll rendelkezésre.
#define FY_CACHE_PATH       "/bellfy.json"
#define FY_VERSION_PATH     "/bellfy.ver"

// A régi NVS névtér – csak azért maradt meg a neve, hogy induláskor
// TÖRÖLNI tudjuk, és visszanyerjük a helyet a már megtelt eszközökön.
#define NVS_BELL_FY_NS      "bellfy"
#define FY_LEGACY_DATA_KEY  "data"

// A teljes tanévnyi JSON felső korlátja. LittleFS-en ez már kényelmes.
#define MAX_FY_JSON_BYTES   (64 * 1024)

class BellManager {
public:
    BellManager(AudioManager& audioMgr, SLNetworkManager& netMgr, BackendClient& backend);

    void    begin();
    void    loop();

    void    setBellMode(uint8_t mode);
    uint8_t getBellMode();

    int    getSecondsToNextEvent();
    String getNextEventTimeStr();

    bool   isScheduleLoaded()   const { return _entryCount > 0; }
    String getScheduleSource()  const { return _scheduleSource; }
    bool   isSyncedFromServer() const { return _syncedFromServer; }

    // WS SCHEDULE_SYNC üzenet érkezett: frissítjük a helyi cache-t a push-olt adatokból
    void onScheduleSync(const JsonDocument& msg);

    // Csak a csengetés-figyelés (ütemezés-szinkron NÉLKÜL). A main loop ezt
    // hívja MINDEN körben; a teljes `loop()`-ot (ami HTTP-sync-et is csinál)
    // csak akkor, ha az eszköz ténylegesen offline.
    void checkBells();

    // "Elérhető-e a backend" – a main loop állítja minden körben.
    // MINDKETTŐ kell hozzá: a WS (a backend folyamat hajtja a mixert) ÉS az
    // élő snapclient-kapcsolat (azon jön a hang). Ld. checkSchedule().
    void setBackendReachable(bool ok) { _backendReachable = ok; }

    // A DeviceAgent hívja, amikor BELL PREPARE érkezik: bizonyíték arra, hogy
    // az online csengetés-út működik, tehát helyben NEM szabad lejátszani
    // (különben duplán szólna).
    void noteOnlineBell() { _lastOnlineBellMs = millis(); }

private:
    AudioManager&   audio;
    SLNetworkManager& network;
    BackendClient&  backend;

    uint8_t _mode       = BELL_MODE_ON;

    BellEntry _entries[MAX_BELL_ENTRIES];
    uint8_t   _entryCount    = 0;
    String    _scheduleSource;
    String    _loadedDate;

    // Verziókövetés
    String _todayVersionKnown;    // amit az eszköz már betöltött
    String _defaultVersionKnown;  // default amit az eszköz már betöltött
    String _fullYearVersionKnown; // teljes tanévnyi naptár amit már elmentett

    // Szinkronizáció állapot
    unsigned long _lastVersionCheckMs = 0;
    // A cache-bootstrap újrapróbálásának ritkítása (a checkBells 100 ms-enként fut).
    unsigned long _lastCacheLoadMs    = 0;
    bool          _syncedToday        = false;
    bool          _syncedFromServer   = false;

    // ── Offline csengetés állapota (ld. checkSchedule) ─────────────────────
    // A backend elérhetősége (ws && snap), a main loop frissíti.
    bool          _backendReachable   = false;
    // Az utolsó BELL PREPARE ideje (millis()); 0 = még nem volt ilyen.
    unsigned long _lastOnlineBellMs   = 0;
    // Melyik naptári napra érvényes a lenti két tömb (tm_yday).
    int           _bellStateDay       = -1;
    // Napi állapot NAPON BELÜLI PERC szerint indexelve (0..1439), bitenként:
    // 1440 bit = 180 bájt. SZÁNDÉKOSAN nem az `_entries` tömb indexe a kulcs –
    // a csengetési rend menet közben is frissülhet (WS SCHEDULE_SYNC), és
    // olyankor az indexek elcsúsznának, ami kihagyott vagy duplán lejátszott
    // csengetést okozna. A napon belüli perc viszont stabil azonosító.
    uint8_t       _bellHandledBits[180] = { 0 };  // ma már elintézve (online v. offline)
    uint8_t       _bellArmedBits[180]   = { 0 };  // T-60-nál nem volt elérhető a backend

    static const long BELL_LEAD_CHECK_S       = 60;    // ennyivel előbb kezdünk figyelni
    static const long BELL_GRACE_S            = 4;     // ennyit várunk PREPARE-re
    static const unsigned long BELL_ONLINE_EVIDENCE_MS = 15000; // friss PREPARE = él az online út
    static const long BELL_CATCHUP_MAX_S      = 120;   // ennel regebbi csengetest mar NEM potolunk

    const unsigned long VERSION_CHECK_MS = 60000UL;  // 1 perc

    // Hardcoded fallback (ha NVS default sem elérhető)
    static const BellEntry HARDCODED_DEFAULT[];
    static const uint8_t   HARDCODED_DEFAULT_COUNT;

    void maybeSyncSchedule();

    // Gyors verzió lekérdezés
    bool fetchVersion(String& outTodayVer, String& outDefaultVer,
                      bool& outIsHoliday);

    // Teljes szinkron (mai + default schedule)
    bool fetchFullSync();

    // NVS – napi cache
    bool loadTodayFromNVS(const String& dateStr, const String& version);
    void saveTodayToNVS(const String& dateStr, const String& version);

    // NVS – default schedule
    bool loadDefaultFromNVS(const String& version);
    void saveDefaultToNVS(const String& version,
                          const BellEntry* entries, uint8_t count);

    // NVS – teljes tanévnyi naptár
    void saveFullYearToNVS(const String& version, const JsonDocument& src);
    // Adott napra (YYYY-MM-DD) feloldja a csengetési rendet a tárolt
    // naptárból: explicit naptár-kivétel (ünnepnap / egyedi sablon) > hétvégi
    // csendes nap (ha nincs kivétel) > default sablon. Sikeres feloldáskor
    // az _entries/_entryCount-ot frissíti (holiday/hétvége esetén 0 elemre).
    bool resolveFullYearForDate(const String& dateStr, bool& outIsHoliday);

    void loadHardcodedDefault();
    void checkSchedule();

    // "A CSENGETÉS SOSEM MARADHAT EL": olyan LittleFS-útvonalat ad vissza,
    // ami TÉNYLEGESEN LÉTEZIK. Sorrend: a kért fájl → a típushoz tartozó
    // gyári default → a másik gyári default → bármelyik .mp3 a tárhelyen.
    // Üres String csak akkor, ha egyetlen hangfájl sincs az eszközön.
    String resolveLocalSound(const char* soundFile, BellType type);

    // A tárolt (NVS) csengetési rend betöltése HÁLÓZAT NÉLKÜL. A teljes
    // offline lánc: napi cache → tanévnyi naptár → default sablon → beégetett
    // alapérték. Akkor kell, ha az eszköz úgy indul el, hogy a backend nem
    // (vagy csak félig) érhető el – ilyenkor a `loop()` szinkron-ága nem fut,
    // és a rend enélkül üres maradna, azaz EGYETLEN csengetés sem szólalna meg.
    bool loadScheduleFromCache(const String& today);

    // "Ma már elcsengetve" bitmező mentése/betöltése NVS-be. A mentés csak
    // akkor fut, amikor TÉNYLEGESEN változik az állapot (naponta néhányszor),
    // tehát a flash élettartama szempontjából elhanyagolható.
    void saveBellDoneState();
    void loadBellDoneState();
    String getTodayDateStr();
};

#endif
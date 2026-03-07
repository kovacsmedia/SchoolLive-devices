#ifndef BELLMANAGER_H
#define BELLMANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "AudioManager.h"
#include "NetworkManager.h"
#include "BackendClient.h"

// Csengetési módok
#define BELL_MODE_OFF   0
#define BELL_MODE_ON    1
#define BELL_MODE_TODAY 2

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

class BellManager {
public:
    BellManager(AudioManager& audioMgr, NetworkManager& netMgr, BackendClient& backend);

    void    begin();
    void    loop();

    void    setBellMode(uint8_t mode);
    uint8_t getBellMode();

    int    getSecondsToNextEvent();
    String getNextEventTimeStr();

    bool   isScheduleLoaded()   const { return _entryCount > 0; }
    String getScheduleSource()  const { return _scheduleSource; }
    bool   isSyncedFromServer() const { return _syncedFromServer; }

private:
    AudioManager&   audio;
    NetworkManager& network;
    BackendClient&  backend;

    uint8_t _mode       = BELL_MODE_ON;
    int     _lastDay    = -1;
    int     _lastMinute = -1;

    BellEntry _entries[MAX_BELL_ENTRIES];
    uint8_t   _entryCount    = 0;
    String    _scheduleSource;
    String    _loadedDate;

    // Verziókövetés
    String _todayVersionKnown;    // amit az eszköz már betöltött
    String _defaultVersionKnown;  // default amit az eszköz már betöltött

    // Szinkronizáció állapot
    unsigned long _lastVersionCheckMs = 0;
    bool          _syncedToday        = false;
    bool          _syncedFromServer   = false;

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

    void loadHardcodedDefault();
    void checkSchedule();
    String getTodayDateStr();
};

#endif
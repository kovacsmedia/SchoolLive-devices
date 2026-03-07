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
    MAIN   = 0,  // kibecsengo.mp3
    SIGNAL = 1   // jelzocsengo.mp3
};

// Egy csengetési bejegyzés
struct BellEntry {
    uint8_t  hour;
    uint8_t  minute;
    BellType type;
};

// Maximum bejegyzések száma egy napra
#define MAX_BELL_ENTRIES 40

// NVS kulcs a mentett schedule-hoz
#define NVS_BELL_NS       "bellsched"
#define NVS_BELL_DATE     "date"       // "YYYY-MM-DD"
#define NVS_BELL_COUNT    "count"
#define NVS_BELL_DATA     "data"       // bináris tömb

class BellManager {
public:
    BellManager(AudioManager& audioMgr, NetworkManager& netMgr, BackendClient& backend);

    void begin();
    void loop();

    void    setBellMode(uint8_t mode);
    uint8_t getBellMode();

    int    getSecondsToNextEvent();
    String getNextEventTimeStr();

    // Szinkronizáció státusza
    bool   isScheduleLoaded() const { return _entryCount > 0; }
    String getScheduleSource() const { return _scheduleSource; }
    bool   isSyncedFromServer() const { return _syncedFromServer; }
private:
    AudioManager&  audio;
    NetworkManager& network;
    BackendClient&  backend;

    uint8_t _mode     = BELL_MODE_ON;
    int     _lastDay  = -1;
    int     _lastMinute = -1;

    // Aktív napi schedule
    BellEntry _entries[MAX_BELL_ENTRIES];
    uint8_t   _entryCount = 0;
    String    _scheduleSource;   // "server", "nvs", "default"
    String    _loadedDate;       // "YYYY-MM-DD"

    // Szinkronizáció időzítése
    unsigned long _lastSyncAttemptMs = 0;
    bool          _syncedToday       = false;
    bool          _syncedFromServer  = false;
    const unsigned long SYNC_RETRY_MS = 60000UL;  // 1 perc újrapróbálás ha sikertelen

    // --- Default hardcoded csengetési rend (normál) ---
    static const BellEntry DEFAULT_SCHEDULE[];
    static const uint8_t   DEFAULT_SCHEDULE_COUNT;

    void checkSchedule();
    void maybeSyncSchedule();
    bool fetchScheduleFromServer();
    bool loadScheduleFromNVS(const String& dateStr);
    void saveScheduleToNVS(const String& dateStr);
    void loadDefaultSchedule();

    String getTodayDateStr();   // "YYYY-MM-DD"
    bool   isWeekend();
};

#endif
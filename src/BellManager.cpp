#include "BellManager.h"
#include <Preferences.h>
#include <string.h>
#include "Config.h"
#include "PersistStore.h"
extern PersistStore store;

// ---------------------------------------------------------------------------
// Default hardcoded csengetési rend – normál iskolai nap
// Ha a szerver nem elérhető és NVS-ben sincs aznapi adat,
// hétköznapokon ez a rend lép életbe.
// ---------------------------------------------------------------------------
const BellEntry BellManager::DEFAULT_SCHEDULE[] = {
    // Jelzők
    { 7, 30, BellType::SIGNAL },
    { 7, 55, BellType::SIGNAL },
    { 8, 53, BellType::SIGNAL },
    { 9, 53, BellType::SIGNAL },
    {10, 48, BellType::SIGNAL },
    {11, 53, BellType::SIGNAL },
    {12, 48, BellType::SIGNAL },
    {14, 10, BellType::SIGNAL },
    {15, 13, BellType::SIGNAL },
    // Főcsengetések
    { 8,  0, BellType::MAIN },
    { 8, 45, BellType::MAIN },
    { 8, 55, BellType::MAIN },
    { 9, 40, BellType::MAIN },
    { 9, 55, BellType::MAIN },
    {10, 40, BellType::MAIN },
    {10, 50, BellType::MAIN },
    {11, 35, BellType::MAIN },
    {11, 55, BellType::MAIN },
    {12, 40, BellType::MAIN },
    {12, 50, BellType::MAIN },
    {13, 35, BellType::MAIN },
    {14, 15, BellType::MAIN },
    {15,  0, BellType::MAIN },
    {15, 15, BellType::MAIN },
    {16,  0, BellType::MAIN },
};
const uint8_t BellManager::DEFAULT_SCHEDULE_COUNT =
    sizeof(BellManager::DEFAULT_SCHEDULE) / sizeof(BellManager::DEFAULT_SCHEDULE[0]);

// ---------------------------------------------------------------------------
// Konstruktor
// ---------------------------------------------------------------------------
BellManager::BellManager(AudioManager& audioMgr,
                         NetworkManager& netMgr,
                         BackendClient& be)
    : audio(audioMgr), network(netMgr), backend(be) {}

// ---------------------------------------------------------------------------
// begin
// ---------------------------------------------------------------------------
void BellManager::begin() {
    _entryCount          = 0;
    _syncedToday         = false;
    _lastSyncAttemptMs   = 0;
    _scheduleSource      = "";
    _loadedDate          = "";
}

// ---------------------------------------------------------------------------
// loop – minden híváskor fut az Arduino loop()-ból
// ---------------------------------------------------------------------------
void BellManager::loop() {
    if (!network.isTimeSynced()) return;
    if (_mode == BELL_MODE_OFF) return;

    maybeSyncSchedule();

    if (_entryCount > 0) {
        checkSchedule();
    }
}

// ---------------------------------------------------------------------------
// maybeSyncSchedule – naponta egyszer szinkronizál a szerverrel
// ---------------------------------------------------------------------------
void BellManager::maybeSyncSchedule() {
    String today = getTodayDateStr();

    // Ha már van mai betöltött schedule → nincs teendő
    if (_syncedToday && _loadedDate == today) return;

    // Ha ma már megpróbáltuk és sikertelen volt, csak SYNC_RETRY_MS után próbáljuk újra
    if (_lastSyncAttemptMs != 0 &&
        (millis() - _lastSyncAttemptMs) < SYNC_RETRY_MS) return;

    // Ha dátum változott (éjfél után) → reset
    if (_loadedDate != today) {
        _syncedToday = false;
        _entryCount  = 0;
    }

    _lastSyncAttemptMs = millis();

    // 1. Próba: szerver
    if (network.isConnected() && fetchScheduleFromServer()) {
        _loadedDate     = today;
        _syncedToday    = true;
        _scheduleSource = "server";
        saveScheduleToNVS(today);
        Serial.printf("[BELL] Schedule loaded from server (%d entries)\n", _entryCount);
        return;
    }

    // 2. Próba: NVS cache
    if (loadScheduleFromNVS(today)) {
        _loadedDate     = today;
        _syncedToday    = true;
        _scheduleSource = "nvs";
        Serial.printf("[BELL] Schedule loaded from NVS (%d entries)\n", _entryCount);
        return;
    }

    // 3. Fallback: hardcoded default (csak hétköznapokon)
    if (!isWeekend()) {
        loadDefaultSchedule();
        _loadedDate     = today;
        _syncedToday    = true;
        _scheduleSource = "default";
        Serial.printf("[BELL] Using default schedule (%d entries)\n", _entryCount);
    } else {
        // Hétvégén nincs csengetés
        _entryCount     = 0;
        _loadedDate     = today;
        _syncedToday    = true;
        _scheduleSource = "weekend";
        Serial.println("[BELL] Weekend – no schedule");
    }
}

// ---------------------------------------------------------------------------
// fetchScheduleFromServer – GET /bells/sync
// ---------------------------------------------------------------------------
bool BellManager::fetchScheduleFromServer() {
    if (!backend.isReady()) return false;

    // A BackendClient::postJson-t GET-re nem tudjuk közvetlenül használni,
    // ezért egy üres POST payload-dal hívjuk a /bells/sync-et (GET-ként
    // is működik, de a BackendClient csak POST-ot ismer).
    // → Külön getJson metódus helyett: a /bells/sync GET-et WiFiClientSecure-rel hívjuk.

    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(8000);

    // A BackendClient baseUrl-jét nem tudjuk közvetlenül olvasni,
    // ezért a Config.h-ból vesszük
    String url = String(BACKEND_BASE_URL) + "/bells/sync";

    // x-device-key fejléc kell – a BackendClient-ből nem olvasható,
    // ezért a PersistStore-ból olvassuk
    String deviceKey = store.getDeviceKey();
    if (deviceKey.length() == 0) return false;

    if (!http.begin(client, url)) return false;
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-device-key", deviceKey);

    int code = http.GET();
    Serial.printf("[BELL] GET /bells/sync → %d\n", code);

    if (code != 200) {
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    // Parse
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return false;

    // Várt struktúra:
    // { "isHoliday": false, "bells": [ { "hour": 8, "minute": 0, "type": "MAIN" }, ... ] }
    if (doc["isHoliday"].as<bool>()) {
        // Ünnep/szünnap → üres schedule
        _entryCount = 0;
        return true;
    }

    JsonArray bells = doc["bells"].as<JsonArray>();
    if (bells.isNull()) return false;

    _entryCount = 0;
    for (JsonObject b : bells) {
        if (_entryCount >= MAX_BELL_ENTRIES) break;
        BellEntry& e = _entries[_entryCount];
        e.hour   = b["hour"]   | 0;
        e.minute = b["minute"] | 0;
        String t = b["type"]   | "MAIN";
        e.type   = (t == "SIGNAL") ? BellType::SIGNAL : BellType::MAIN;
        _entryCount++;
    }

    return true;
}

// ---------------------------------------------------------------------------
// NVS mentés/betöltés
// ---------------------------------------------------------------------------
void BellManager::saveScheduleToNVS(const String& dateStr) {
    Preferences prefs;
    if (!prefs.begin(NVS_BELL_NS, false)) return;

    prefs.putString(NVS_BELL_DATE, dateStr);
    prefs.putUChar(NVS_BELL_COUNT, _entryCount);

    // BellEntry tömböt binárisként mentjük
    prefs.putBytes(NVS_BELL_DATA, _entries,
                   _entryCount * sizeof(BellEntry));
    prefs.end();
    Serial.printf("[BELL] Saved %d entries to NVS for %s\n",
                  _entryCount, dateStr.c_str());
}

bool BellManager::loadScheduleFromNVS(const String& dateStr) {
    Preferences prefs;
    if (!prefs.begin(NVS_BELL_NS, true)) return false;

    String savedDate = prefs.getString(NVS_BELL_DATE, "");
    if (savedDate != dateStr) {
        prefs.end();
        return false;
    }

    uint8_t count = prefs.getUChar(NVS_BELL_COUNT, 0);
    if (count == 0 || count > MAX_BELL_ENTRIES) {
        prefs.end();
        // 0 bejegyzés = ünnepnap volt cache-elve → érvényes
        _entryCount = 0;
        return (count == 0);
    }

    size_t bytes = prefs.getBytes(NVS_BELL_DATA, _entries,
                                  count * sizeof(BellEntry));
    prefs.end();

    if (bytes != count * sizeof(BellEntry)) return false;
    _entryCount = count;
    return true;
}

// ---------------------------------------------------------------------------
// loadDefaultSchedule
// ---------------------------------------------------------------------------
void BellManager::loadDefaultSchedule() {
    _entryCount = min((uint8_t)MAX_BELL_ENTRIES, DEFAULT_SCHEDULE_COUNT);
    memcpy(_entries, DEFAULT_SCHEDULE, _entryCount * sizeof(BellEntry));
}

// ---------------------------------------------------------------------------
// checkSchedule – percenként ellenőriz
// ---------------------------------------------------------------------------
void BellManager::checkSchedule() {
    struct tm t = network.getTimeInfo();

    // Hétvégén BELL_MODE_ON esetén nem cseng
    if (_mode == BELL_MODE_ON && isWeekend()) return;

    int curMin = t.tm_hour * 60 + t.tm_min;
    if (_lastDay == t.tm_yday && _lastMinute == curMin) return;

    for (uint8_t i = 0; i < _entryCount; i++) {
        if (_entries[i].hour   == (uint8_t)t.tm_hour &&
            _entries[i].minute == (uint8_t)t.tm_min) {

            if (_entries[i].type == BellType::SIGNAL) {
                Serial.printf("[BELL] SIGNAL @ %02d:%02d (src: %s)\n",
                              t.tm_hour, t.tm_min, _scheduleSource.c_str());
                audio.playFile("/jelzocsengo.mp3");
            } else {
                Serial.printf("[BELL] MAIN @ %02d:%02d (src: %s)\n",
                              t.tm_hour, t.tm_min, _scheduleSource.c_str());
                audio.playFile("/kibecsengo.mp3");
            }

            _lastDay    = t.tm_yday;
            _lastMinute = curMin;

            // BELL_MODE_TODAY: utolsó csengetés után visszavált ON-ra
            if (_mode == BELL_MODE_TODAY) {
                bool isLast = true;
                for (uint8_t j = 0; j < _entryCount; j++) {
                    int eMin = _entries[j].hour * 60 + _entries[j].minute;
                    if (eMin > curMin) { isLast = false; break; }
                }
                if (isLast) _mode = BELL_MODE_ON;
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// setBellMode / getBellMode
// ---------------------------------------------------------------------------
void BellManager::setBellMode(uint8_t mode) {
    if (mode > 2) mode = 0;
    _mode = mode;
}

uint8_t BellManager::getBellMode() { return _mode; }

// ---------------------------------------------------------------------------
// getSecondsToNextEvent
// ---------------------------------------------------------------------------
int BellManager::getSecondsToNextEvent() {
    if (!network.isTimeSynced()) return -1;
    if (_mode == BELL_MODE_OFF)  return -1;
    if (_entryCount == 0)        return -1;

    struct tm t = network.getTimeInfo();
    if (_mode == BELL_MODE_ON && isWeekend()) return -1;

    int curSec  = t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
    int minDiff = 999999;

    for (uint8_t i = 0; i < _entryCount; i++) {
        int eSec = _entries[i].hour * 3600 + _entries[i].minute * 60;
        int diff = eSec - curSec;
        if (diff > 0 && diff < minDiff) minDiff = diff;
    }

    return (minDiff == 999999) ? -1 : minDiff;
}

// ---------------------------------------------------------------------------
// getNextEventTimeStr
// ---------------------------------------------------------------------------
String BellManager::getNextEventTimeStr() {
    if (!network.isTimeSynced()) return "--:--";
    if (_mode == BELL_MODE_OFF)  return "OFF";
    if (_entryCount == 0)        return "--:--";

    struct tm t = network.getTimeInfo();
    if (_mode == BELL_MODE_ON && isWeekend()) return "--:--";

    int curMin    = t.tm_hour * 60 + t.tm_min;
    int nextMin   = 9999;

    for (uint8_t i = 0; i < _entryCount; i++) {
        int eMin = _entries[i].hour * 60 + _entries[i].minute;
        if (eMin > curMin && eMin < nextMin) nextMin = eMin;
    }

    if (nextMin == 9999) return "--:--";

    char buf[6];
    sprintf(buf, "%02d:%02d", nextMin / 60, nextMin % 60);
    return String(buf);
}

// ---------------------------------------------------------------------------
// Segédfüggvények
// ---------------------------------------------------------------------------
String BellManager::getTodayDateStr() {
    struct tm t = network.getTimeInfo();
    char buf[11];
    sprintf(buf, "%04d-%02d-%02d",
            t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    return String(buf);
}

bool BellManager::isWeekend() {
    struct tm t = network.getTimeInfo();
    return (t.tm_wday == 0 || t.tm_wday == 6);
}
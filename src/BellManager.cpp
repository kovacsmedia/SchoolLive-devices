#include "BellManager.h"
#include "Config.h"
#include <Preferences.h>
#include <string.h>

extern PersistStore store;

const BellEntry BellManager::DEFAULT_SCHEDULE[] = {
    { 7, 30, BellType::SIGNAL },
    { 7, 55, BellType::SIGNAL },
    { 8, 53, BellType::SIGNAL },
    { 9, 53, BellType::SIGNAL },
    {10, 48, BellType::SIGNAL },
    {11, 53, BellType::SIGNAL },
    {12, 48, BellType::SIGNAL },
    {14, 10, BellType::SIGNAL },
    {15, 13, BellType::SIGNAL },
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

BellManager::BellManager(AudioManager& audioMgr, NetworkManager& netMgr, BackendClient& be)
    : audio(audioMgr), network(netMgr), backend(be) {}

void BellManager::begin() {
    _entryCount        = 0;
    _syncedToday       = false;
    _syncedFromServer  = false;
    _lastSyncAttemptMs = 0;
    _scheduleSource    = "";
    _loadedDate        = "";
}

void BellManager::loop() {
    if (!network.isTimeSynced()) return;
    if (_mode == BELL_MODE_OFF)  return;
    maybeSyncSchedule();
    if (_entryCount > 0) checkSchedule();
}

void BellManager::maybeSyncSchedule() {
    String today = getTodayDateStr();
    if (_syncedFromServer && _loadedDate == today) return;
    if (_lastSyncAttemptMs != 0 && (millis() - _lastSyncAttemptMs) < SYNC_RETRY_MS) return;
    if (_loadedDate != today) {
        _syncedToday = false; _syncedFromServer = false; _entryCount = 0;
    }
    _lastSyncAttemptMs = millis();

    if (network.isConnected() && fetchScheduleFromServer()) {
        _loadedDate = today; _syncedToday = true; _syncedFromServer = true;
        _scheduleSource = "server";
        saveScheduleToNVS(today);
        Serial.printf("[BELL] Schedule loaded from server (%d entries)\n", _entryCount);
        return;
    }
    if (loadScheduleFromNVS(today)) {
        _loadedDate = today; _syncedToday = true; _syncedFromServer = true;
        _scheduleSource = "nvs";
        Serial.printf("[BELL] Schedule loaded from NVS (%d entries)\n", _entryCount);
        return;
    }
    if (!isWeekend()) {
        loadDefaultSchedule();
        _loadedDate = today; _syncedToday = true;
        _scheduleSource = "default";
        Serial.printf("[BELL] Using default schedule (%d entries)\n", _entryCount);
    } else {
        _entryCount = 0; _loadedDate = today; _syncedToday = true; _syncedFromServer = true;
        _scheduleSource = "weekend";
        Serial.println("[BELL] Weekend - no schedule");
    }
}

bool BellManager::fetchScheduleFromServer() {
    if (!backend.isReady()) return false;
    JsonDocument resp;
    int httpCode = 0;
    if (!backend.getJson("/bells/sync", resp, httpCode)) {
        Serial.printf("[BELL] GET /bells/sync -> %d\n", httpCode);
        return false;
    }
    Serial.printf("[BELL] GET /bells/sync -> %d\n", httpCode);
    if (resp["isHoliday"].as<bool>()) { _entryCount = 0; return true; }
    JsonArray bells = resp["bells"].as<JsonArray>();
    if (bells.isNull()) return false;
    _entryCount = 0;
    for (JsonObject b : bells) {
        if (_entryCount >= MAX_BELL_ENTRIES) break;
        BellEntry& e = _entries[_entryCount];
        e.hour = b["hour"] | 0; e.minute = b["minute"] | 0;
        String t = b["type"] | "MAIN";
        e.type = (t == "SIGNAL") ? BellType::SIGNAL : BellType::MAIN;
        _entryCount++;
    }
    return true;
}

void BellManager::saveScheduleToNVS(const String& dateStr) {
    Preferences prefs;
    if (!prefs.begin(NVS_BELL_NS, false)) return;
    prefs.putString(NVS_BELL_DATE, dateStr);
    prefs.putUChar(NVS_BELL_COUNT, _entryCount);
    prefs.putBytes(NVS_BELL_DATA, _entries, _entryCount * sizeof(BellEntry));
    prefs.end();
    Serial.printf("[BELL] Saved %d entries to NVS for %s\n", _entryCount, dateStr.c_str());
}

bool BellManager::loadScheduleFromNVS(const String& dateStr) {
    Preferences prefs;
    if (!prefs.begin(NVS_BELL_NS, true)) return false;
    String savedDate = prefs.getString(NVS_BELL_DATE, "");
    if (savedDate != dateStr) { prefs.end(); return false; }
    uint8_t count = prefs.getUChar(NVS_BELL_COUNT, 0);
    if (count == 0) { prefs.end(); _entryCount = 0; return true; }
    if (count > MAX_BELL_ENTRIES) { prefs.end(); return false; }
    size_t bytes = prefs.getBytes(NVS_BELL_DATA, _entries, count * sizeof(BellEntry));
    prefs.end();
    if (bytes != count * sizeof(BellEntry)) return false;
    _entryCount = count;
    return true;
}

void BellManager::loadDefaultSchedule() {
    _entryCount = min((uint8_t)MAX_BELL_ENTRIES, DEFAULT_SCHEDULE_COUNT);
    memcpy(_entries, DEFAULT_SCHEDULE, _entryCount * sizeof(BellEntry));
}

void BellManager::checkSchedule() {
    struct tm t = network.getTimeInfo();
    if (_mode == BELL_MODE_ON && isWeekend()) return;
    int curMin = t.tm_hour * 60 + t.tm_min;
    if (_lastDay == t.tm_yday && _lastMinute == curMin) return;
    for (uint8_t i = 0; i < _entryCount; i++) {
        if (_entries[i].hour == (uint8_t)t.tm_hour && _entries[i].minute == (uint8_t)t.tm_min) {
            if (_entries[i].type == BellType::SIGNAL) {
                Serial.printf("[BELL] SIGNAL @ %02d:%02d (src: %s)\n", t.tm_hour, t.tm_min, _scheduleSource.c_str());
                audio.playFile("/jelzocsengo.mp3");
            } else {
                Serial.printf("[BELL] MAIN @ %02d:%02d (src: %s)\n", t.tm_hour, t.tm_min, _scheduleSource.c_str());
                audio.playFile("/kibecsengo.mp3");
            }
            _lastDay = t.tm_yday; _lastMinute = curMin;
            if (_mode == BELL_MODE_TODAY) {
                bool isLast = true;
                for (uint8_t j = 0; j < _entryCount; j++) {
                    if (_entries[j].hour * 60 + _entries[j].minute > curMin) { isLast = false; break; }
                }
                if (isLast) _mode = BELL_MODE_ON;
            }
            break;
        }
    }
}

void BellManager::setBellMode(uint8_t mode) { if (mode > 2) mode = 0; _mode = mode; }
uint8_t BellManager::getBellMode() { return _mode; }

int BellManager::getSecondsToNextEvent() {
    if (!network.isTimeSynced() || _mode == BELL_MODE_OFF || _entryCount == 0) return -1;
    struct tm t = network.getTimeInfo();
    if (_mode == BELL_MODE_ON && isWeekend()) return -1;
    int curSec = t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
    int minDiff = 999999;
    for (uint8_t i = 0; i < _entryCount; i++) {
        int diff = (_entries[i].hour * 3600 + _entries[i].minute * 60) - curSec;
        if (diff > 0 && diff < minDiff) minDiff = diff;
    }
    return (minDiff == 999999) ? -1 : minDiff;
}

String BellManager::getNextEventTimeStr() {
    if (!network.isTimeSynced() || _mode == BELL_MODE_OFF || _entryCount == 0) return "--:--";
    struct tm t = network.getTimeInfo();
    if (_mode == BELL_MODE_ON && isWeekend()) return "--:--";
    int curMin = t.tm_hour * 60 + t.tm_min, nextMin = 9999;
    for (uint8_t i = 0; i < _entryCount; i++) {
        int eMin = _entries[i].hour * 60 + _entries[i].minute;
        if (eMin > curMin && eMin < nextMin) nextMin = eMin;
    }
    if (nextMin == 9999) return "--:--";
    char buf[6]; sprintf(buf, "%02d:%02d", nextMin / 60, nextMin % 60);
    return String(buf);
}

String BellManager::getTodayDateStr() {
    struct tm t = network.getTimeInfo();
    char buf[11]; sprintf(buf, "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    return String(buf);
}

bool BellManager::isWeekend() {
    struct tm t = network.getTimeInfo();
    return (t.tm_wday == 0 || t.tm_wday == 6);
}
// BellManager.cpp – SchoolLive S3.54
#include "BellManager.h"
#include "SnapcastClient.h"
#include "Config.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <string.h>

const BellEntry BellManager::HARDCODED_DEFAULT[] = {
    { 7, 30, BellType::SIGNAL, "jelzocsengo.mp3" },
    { 7, 55, BellType::SIGNAL, "jelzocsengo.mp3" },
    { 8,  0, BellType::MAIN,   "kibecsengo.mp3"  },
    { 8, 45, BellType::MAIN,   "kibecsengo.mp3"  },
    { 8, 53, BellType::SIGNAL, "jelzocsengo.mp3" },
    { 8, 55, BellType::MAIN,   "kibecsengo.mp3"  },
    { 9, 40, BellType::MAIN,   "kibecsengo.mp3"  },
    { 9, 53, BellType::SIGNAL, "jelzocsengo.mp3" },
    { 9, 55, BellType::MAIN,   "kibecsengo.mp3"  },
    {10, 40, BellType::MAIN,   "kibecsengo.mp3"  },
    {10, 48, BellType::SIGNAL, "jelzocsengo.mp3" },
    {10, 50, BellType::MAIN,   "kibecsengo.mp3"  },
    {11, 35, BellType::MAIN,   "kibecsengo.mp3"  },
    {11, 53, BellType::SIGNAL, "jelzocsengo.mp3" },
    {11, 55, BellType::MAIN,   "kibecsengo.mp3"  },
    {12, 40, BellType::MAIN,   "kibecsengo.mp3"  },
    {12, 48, BellType::SIGNAL, "jelzocsengo.mp3" },
    {12, 50, BellType::MAIN,   "kibecsengo.mp3"  },
    {13, 35, BellType::MAIN,   "kibecsengo.mp3"  },
    {14, 10, BellType::SIGNAL, "jelzocsengo.mp3" },
    {14, 15, BellType::MAIN,   "kibecsengo.mp3"  },
    {15,  0, BellType::MAIN,   "kibecsengo.mp3"  },
    {15, 13, BellType::SIGNAL, "jelzocsengo.mp3" },
    {15, 15, BellType::MAIN,   "kibecsengo.mp3"  },
    {16,  0, BellType::MAIN,   "kibecsengo.mp3"  },
};
const uint8_t BellManager::HARDCODED_DEFAULT_COUNT =
    sizeof(BellManager::HARDCODED_DEFAULT) / sizeof(BellManager::HARDCODED_DEFAULT[0]);

BellManager::BellManager(AudioManager& audioMgr,
                         NetworkManager& netMgr,
                         BackendClient& be)
    : audio(audioMgr), network(netMgr), backend(be) {}

void BellManager::begin() {
    _entryCount          = 0;
    _syncedToday         = false;
    _syncedFromServer    = false;
    _lastVersionCheckMs  = millis();
    _scheduleSource      = "";
    _loadedDate          = "";
    _todayVersionKnown   = "";
    _defaultVersionKnown = "";
}

void BellManager::loop() {
    if (!network.isTimeSynced()) return;
    if (_mode == BELL_MODE_OFF)  return;
    maybeSyncSchedule();
    if (_entryCount > 0) checkSchedule();
}

void BellManager::maybeSyncSchedule() {
    String today = getTodayDateStr();

    if (_loadedDate != today) {
        _syncedToday         = false;
        _syncedFromServer    = false;
        _entryCount          = 0;
        _todayVersionKnown   = "";
        _lastVersionCheckMs  = 0;
    }

    if (_lastVersionCheckMs != 0 &&
        (millis() - _lastVersionCheckMs) < VERSION_CHECK_MS) return;

    _lastVersionCheckMs = millis();

    if (network.isConnected()) {
        String serverTodayVer, serverDefaultVer;
        bool   serverIsHoliday = false;

        bool versionOk = fetchVersion(serverTodayVer, serverDefaultVer, serverIsHoliday);

        if (versionOk) {
            bool todayChanged   = (serverTodayVer   != _todayVersionKnown);
            bool defaultChanged = (serverDefaultVer != _defaultVersionKnown);

            if (todayChanged || defaultChanged || !_syncedToday) {
                if (fetchFullSync()) {
                    _loadedDate       = today;
                    _syncedToday      = true;
                    _syncedFromServer = true;
                    return;
                }
            } else {
                Serial.printf("[BELL] Version match (%s), no sync needed\n",
                              serverTodayVer.c_str());
                if (!_syncedToday) {
                    if (loadTodayFromNVS(today, serverTodayVer)) {
                        _loadedDate       = today;
                        _syncedToday      = true;
                        _syncedFromServer = true;
                        _scheduleSource   = "nvs";
                        Serial.printf("[BELL] Loaded from NVS cache (%d entries)\n", _entryCount);
                    } else {
                        if (fetchFullSync()) {
                            _loadedDate       = today;
                            _syncedToday      = true;
                            _syncedFromServer = true;
                        }
                    }
                }
                return;
            }
        }
    }

    if (_syncedToday) return;

    if (loadTodayFromNVS(today, "")) {
        _loadedDate     = today;
        _syncedToday    = true;
        _scheduleSource = "nvs";
        Serial.printf("[BELL] Offline: loaded NVS cache (%d entries)\n", _entryCount);
        return;
    }

    if (loadDefaultFromNVS("")) {
        _loadedDate     = today;
        _syncedToday    = true;
        _scheduleSource = "nvs-default";
        Serial.printf("[BELL] Offline: loaded NVS default (%d entries)\n", _entryCount);
        return;
    }

    loadHardcodedDefault();
    _loadedDate     = today;
    _syncedToday    = true;
    _scheduleSource = "hardcoded";
    Serial.printf("[BELL] Offline: using hardcoded default (%d entries)\n", _entryCount);
}

bool BellManager::fetchVersion(String& outTodayVer, String& outDefaultVer, bool& outIsHoliday) {
    if (!backend.isReady()) return false;
    JsonDocument resp; int code = 0;
    if (!backend.getJson("/bells/version", resp, code)) return false;
    outTodayVer   = resp["todayVersion"]   | "";
    outDefaultVer = resp["defaultVersion"] | "";
    outIsHoliday  = resp["isHoliday"]      | false;
    return (outTodayVer.length() > 0);
}

bool BellManager::fetchFullSync() {
    if (!backend.isReady()) return false;
    JsonDocument resp; int code = 0;
    if (!backend.getJson("/bells/sync", resp, code)) {
        Serial.printf("[BELL] GET /bells/sync -> %d\n", code);
        return false;
    }
    Serial.printf("[BELL] GET /bells/sync -> %d\n", code);

    String todayVer   = resp["todayVersion"]   | "";
    String defaultVer = resp["defaultVersion"] | "";
    bool   isHoliday  = resp["isHoliday"]      | false;

    _entryCount = 0;
    if (isHoliday) {
        _scheduleSource = "server-holiday";
        Serial.println("[BELL] Today is holiday – no bells");
    } else {
        JsonArray bells = resp["bells"].as<JsonArray>();
        if (!bells.isNull()) {
            for (JsonObject b : bells) {
                if (_entryCount >= MAX_BELL_ENTRIES) break;
                BellEntry& e = _entries[_entryCount];
                e.hour   = b["hour"]   | 0;
                e.minute = b["minute"] | 0;
                String t = b["type"]   | "MAIN";
                e.type   = (t == "SIGNAL") ? BellType::SIGNAL : BellType::MAIN;
                String sf = b["soundFile"] | "kibecsengo.mp3";
                strncpy(e.soundFile, sf.c_str(), sizeof(e.soundFile) - 1);
                e.soundFile[sizeof(e.soundFile) - 1] = '\0';
                _entryCount++;
            }
        }
        _scheduleSource = "server";
        Serial.printf("[BELL] Today: %d entries (ver: %s)\n", _entryCount, todayVer.c_str());
    }

    saveTodayToNVS(getTodayDateStr(), todayVer);
    _todayVersionKnown = todayVer;

    JsonArray defaultBells = resp["defaultBells"].as<JsonArray>();
    if (!defaultBells.isNull() && defaultVer != _defaultVersionKnown) {
        BellEntry defEntries[MAX_BELL_ENTRIES];
        uint8_t defCount = 0;
        for (JsonObject b : defaultBells) {
            if (defCount >= MAX_BELL_ENTRIES) break;
            BellEntry& e = defEntries[defCount];
            e.hour   = b["hour"]   | 0;
            e.minute = b["minute"] | 0;
            String t = b["type"]   | "MAIN";
            e.type   = (t == "SIGNAL") ? BellType::SIGNAL : BellType::MAIN;
            String sf = b["soundFile"] | "kibecsengo.mp3";
            strncpy(e.soundFile, sf.c_str(), sizeof(e.soundFile) - 1);
            e.soundFile[sizeof(e.soundFile) - 1] = '\0';
            defCount++;
        }
        saveDefaultToNVS(defaultVer, defEntries, defCount);
        _defaultVersionKnown = defaultVer;
        Serial.printf("[BELL] Default schedule saved to NVS (%d entries, ver: %s)\n",
                      defCount, defaultVer.c_str());
    }

    JsonArray sounds = resp["sounds"].as<JsonArray>();
    if (!sounds.isNull()) {
        String serverFiles[MAX_BELL_ENTRIES];
        uint8_t serverFileCount = 0;
        for (JsonObject s : sounds) {
            String fn = s["filename"] | "";
            if (fn.isEmpty()) continue;
            if (fn[0] != '/') fn = "/" + fn;
            if (serverFileCount < MAX_BELL_ENTRIES) serverFiles[serverFileCount++] = fn;
        }

        int dlRemoved = 0;
        File root = LittleFS.open("/");
        if (root && root.isDirectory()) {
            File entry = root.openNextFile();
            while (entry) {
                String entryName = "/" + String(entry.name());
                entry.close();
                if (entryName.endsWith(".mp3")) {
                    bool found = false;
                    for (uint8_t i = 0; i < serverFileCount; i++) {
                        if (serverFiles[i] == entryName) { found = true; break; }
                    }
                    if (!found) {
                        Serial.printf("[BELL] Sound removed (not on server): %s\n", entryName.c_str());
                        LittleFS.remove(entryName);
                        dlRemoved++;
                    }
                }
                entry = root.openNextFile();
            }
            root.close();
        }

        int dlOk = 0, dlSkip = 0, dlFail = 0;
        for (JsonObject s : sounds) {
            String filename  = s["filename"]  | "";
            String url       = s["url"]       | "";
            size_t sizeBytes = s["sizeBytes"] | 0;
            if (filename.isEmpty() || url.isEmpty()) continue;
            String localPath = filename.startsWith("/") ? filename : "/" + filename;
            if (sizeBytes > 0 && LittleFS.exists(localPath)) {
                File f = LittleFS.open(localPath, "r");
                if (f && (size_t)f.size() == sizeBytes) {
                    f.close();
                    Serial.printf("[BELL] Sound OK (exists): %s\n", filename.c_str());
                    dlSkip++;
                    continue;
                }
                if (f) f.close();
            }
            Serial.printf("[BELL] Downloading sound: %s (%d bytes)\n", filename.c_str(), sizeBytes);
            bool ok = backend.downloadFile(url, localPath, sizeBytes);
            if (ok) { Serial.printf("[BELL] Sound downloaded: %s\n", filename.c_str()); dlOk++; }
            else    { Serial.printf("[BELL] Sound FAILED: %s\n", filename.c_str()); dlFail++; }
        }
        Serial.printf("[BELL] Sounds: %d ok, %d skip, %d fail, %d removed\n",
                      dlOk, dlSkip, dlFail, dlRemoved);
    }

    return true;
}

void BellManager::saveTodayToNVS(const String& dateStr, const String& version) {
    Preferences prefs;
    if (!prefs.begin(NVS_BELL_NS, false)) return;
    prefs.putString(NVS_BELL_DATE, dateStr);
    prefs.putString(NVS_BELL_TODAY_VER, version);
    prefs.putUChar(NVS_BELL_COUNT, _entryCount);
    if (_entryCount > 0) prefs.putBytes(NVS_BELL_DATA, _entries, _entryCount * sizeof(BellEntry));
    prefs.end();
}

bool BellManager::loadTodayFromNVS(const String& dateStr, const String& version) {
    Preferences prefs;
    if (!prefs.begin(NVS_BELL_NS, true)) return false;
    String savedDate = prefs.getString(NVS_BELL_DATE, "");
    if (savedDate != dateStr) { prefs.end(); return false; }
    if (version.length() > 0) {
        String savedVer = prefs.getString(NVS_BELL_TODAY_VER, "");
        if (savedVer != version) { prefs.end(); return false; }
    }
    uint8_t count = prefs.getUChar(NVS_BELL_COUNT, 0);
    if (count == 0) {
        _todayVersionKnown = prefs.getString(NVS_BELL_TODAY_VER, "");
        prefs.end(); _entryCount = 0; return true;
    }
    if (count > MAX_BELL_ENTRIES) { prefs.end(); return false; }
    size_t bytes = prefs.getBytes(NVS_BELL_DATA, _entries, count * sizeof(BellEntry));
    String ver = prefs.getString(NVS_BELL_TODAY_VER, "");
    prefs.end();
    if (bytes != count * sizeof(BellEntry)) return false;
    _entryCount = count; _todayVersionKnown = ver;
    return true;
}

void BellManager::saveDefaultToNVS(const String& version, const BellEntry* entries, uint8_t count) {
    Preferences prefs;
    if (!prefs.begin(NVS_BELL_DEF_NS, false)) return;
    prefs.putString(NVS_BELL_DEF_VER, version);
    prefs.putUChar(NVS_BELL_DEF_COUNT, count);
    if (count > 0) prefs.putBytes(NVS_BELL_DEF_DATA, entries, count * sizeof(BellEntry));
    prefs.end();
}

bool BellManager::loadDefaultFromNVS(const String& version) {
    Preferences prefs;
    if (!prefs.begin(NVS_BELL_DEF_NS, true)) return false;
    if (version.length() > 0) {
        String savedVer = prefs.getString(NVS_BELL_DEF_VER, "");
        if (savedVer != version) { prefs.end(); return false; }
    }
    uint8_t count = prefs.getUChar(NVS_BELL_DEF_COUNT, 0);
    if (count == 0 || count > MAX_BELL_ENTRIES) { prefs.end(); return false; }
    size_t bytes = prefs.getBytes(NVS_BELL_DEF_DATA, _entries, count * sizeof(BellEntry));
    String ver = prefs.getString(NVS_BELL_DEF_VER, "");
    prefs.end();
    if (bytes != count * sizeof(BellEntry)) return false;
    _entryCount = count; _defaultVersionKnown = ver;
    return true;
}

void BellManager::loadHardcodedDefault() {
    _entryCount = min((uint8_t)MAX_BELL_ENTRIES, HARDCODED_DEFAULT_COUNT);
    memcpy(_entries, HARDCODED_DEFAULT, _entryCount * sizeof(BellEntry));
}

// ── checkSchedule ─────────────────────────────────────────────────────────────
// Ha Snap csatlakozva van: a szerver Snapcaston játssza a csengőt
// Ha Snap offline: lokális MP3 lejátszás
void BellManager::checkSchedule() {
    struct tm t = network.getTimeInfo();
    int curMin = t.tm_hour * 60 + t.tm_min;
    if (_lastDay == t.tm_yday && _lastMinute == curMin) return;

    for (uint8_t i = 0; i < _entryCount; i++) {
        if (_entries[i].hour   == (uint8_t)t.tm_hour &&
            _entries[i].minute == (uint8_t)t.tm_min) {

            const char* sf = _entries[i].soundFile[0]
                             ? _entries[i].soundFile
                             : (_entries[i].type == BellType::SIGNAL
                                ? "jelzocsengo.mp3"
                                : "kibecsengo.mp3");

            Serial.printf("[BELL] %s @ %02d:%02d  file:%s (src:%s)\n",
                          _entries[i].type == BellType::SIGNAL ? "SIGNAL" : "MAIN",
                          t.tm_hour, t.tm_min, sf, _scheduleSource.c_str());

            _lastDay    = t.tm_yday;
            _lastMinute = curMin;

            if (_snap && _snap->isConnected()) {
                // Snap online – szerver elküldi a csengetést Snapcaston
                // Lokálisan ne játsszunk, hogy ne legyen dupla hang
                Serial.println("[BELL] Snap aktív – lokális lejátszás kihagyva");
            } else {
                // Snap offline fallback: lokális MP3
                String path = sf[0] == '/' ? String(sf) : "/" + String(sf);
                audio.playFile(path.c_str());
                Serial.println("[BELL] Offline: lokális MP3 lejátszás");
            }

            if (_mode == BELL_MODE_TODAY) {
                bool isLast = true;
                for (uint8_t j = 0; j < _entryCount; j++) {
                    if (_entries[j].hour * 60 + _entries[j].minute > curMin) {
                        isLast = false; break;
                    }
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
    if (!network.isTimeSynced()) return -1;
    if (_mode == BELL_MODE_OFF)  return -1;
    if (_entryCount == 0)        return -1;
    struct tm t = network.getTimeInfo();
    int curSec  = t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
    int minDiff = 999999;
    for (uint8_t i = 0; i < _entryCount; i++) {
        int diff = (_entries[i].hour * 3600 + _entries[i].minute * 60) - curSec;
        if (diff > 0 && diff < minDiff) minDiff = diff;
    }
    return (minDiff == 999999) ? -1 : minDiff;
}

String BellManager::getNextEventTimeStr() {
    if (!network.isTimeSynced()) return "--:--";
    if (_mode == BELL_MODE_OFF)  return "OFF";
    if (_entryCount == 0)        return "--:--";
    struct tm t  = network.getTimeInfo();
    int curMin   = t.tm_hour * 60 + t.tm_min;
    int nextMin  = 9999;
    for (uint8_t i = 0; i < _entryCount; i++) {
        int eMin = _entries[i].hour * 60 + _entries[i].minute;
        if (eMin > curMin && eMin < nextMin) nextMin = eMin;
    }
    if (nextMin == 9999) return "--:--";
    char buf[6];
    sprintf(buf, "%02d:%02d", nextMin / 60, nextMin % 60);
    return String(buf);
}

String BellManager::getTodayDateStr() {
    struct tm t = network.getTimeInfo();
    char buf[11];
    sprintf(buf, "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    return String(buf);
}
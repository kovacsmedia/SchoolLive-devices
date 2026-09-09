#include "BellManager.h"
#include "Config.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <string.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// Hardcoded fallback
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Konstruktor / begin
// ---------------------------------------------------------------------------
BellManager::BellManager(AudioManager& audioMgr,
                         SLNetworkManager& netMgr,
                         BackendClient& be)
    : audio(audioMgr), network(netMgr), backend(be) {}

void BellManager::begin() {
    _entryCount          = 0;
    _syncedToday         = false;
    _syncedFromServer    = false;
    _lastVersionCheckMs  = millis();  // ne azonnal szinkronizáljon
    _scheduleSource      = "";
    _loadedDate          = "";
    _todayVersionKnown   = "";
    _defaultVersionKnown = "";
    _fullYearVersionKnown = "";
}

// ---------------------------------------------------------------------------
// loop
// ---------------------------------------------------------------------------
void BellManager::loop() {
    if (!network.isTimeSynced()) return;
    if (_mode == BELL_MODE_OFF)  return;

    maybeSyncSchedule();

    if (_entryCount > 0) checkSchedule();
}

// ---------------------------------------------------------------------------
// loadScheduleFromCache – hálózat nélküli betöltés
// ---------------------------------------------------------------------------
// Ugyanaz a lánc, mint a maybeSyncSchedule() offline ágában, de HTTP-hívás
// NÉLKÜL. Azért kell külön, mert a `loop()` (és benne a szinkron) csak akkor
// fut, ha az eszköz TELJESEN offline – egy félig-online állapotban (pl. a
// backend-folyamat halott, de a snapclient még kapcsolódik) az induló eszköz
// rendje üres maradna, és egyetlen csengetés sem szólalna meg.
bool BellManager::loadScheduleFromCache(const String& today) {
    if (loadTodayFromNVS(today, "")) {
        _loadedDate     = today;
        _scheduleSource = "nvs";
        Serial.printf("[BELL] Cache bootstrap: napi NVS (%d bejegyzes)\n", _entryCount);
        return true;
    }

    bool fyIsHoliday = false;
    if (resolveFullYearForDate(today, fyIsHoliday)) {
        _loadedDate     = today;
        _scheduleSource = fyIsHoliday ? "nvs-fullyear-holiday" : "nvs-fullyear";
        Serial.printf("[BELL] Cache bootstrap: tanevnyi naptar %s (%d bejegyzes, holiday=%d)\n",
                      today.c_str(), _entryCount, fyIsHoliday ? 1 : 0);
        return true;
    }

    if (loadDefaultFromNVS("")) {
        _loadedDate     = today;
        _scheduleSource = "nvs-default";
        Serial.printf("[BELL] Cache bootstrap: NVS default sablon (%d bejegyzes)\n", _entryCount);
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Csak a csengetés-figyelés, HTTP ütemezés-szinkron NÉLKÜL. A main loop ezt
// hívja minden körben (online állapotban is), hogy a checkSchedule() saját
// szabálya dönthessen az offline lejátszásról – korábban a figyelés maga is
// csak akkor futott, ha az eszköz teljesen offline volt, ezért egy fél-hibás
// állapotban (pl. backend-deploy: WS halott, snapclient él) senki nem szólt.
// A `maybeSyncSchedule()` szándékosan marad a `loop()`-ban: online állapotban
// az ütemezést a WS SCHEDULE_SYNC push frissíti, nem kell HTTP-vel pollozni.
void BellManager::checkBells() {
    if (!network.isTimeSynced()) return;
    if (_mode == BELL_MODE_OFF)  return;

    // Ha még nincs betöltött rend (pl. az eszköz úgy indult, hogy a backend
    // nem érhető el, vagy csak félig – ilyenkor a `loop()` szinkron-ága sem
    // fut), a TÁROLT rendet töltjük be. Enélkül `_entryCount == 0` maradna,
    // és egyetlen csengetés sem szólalna meg.
    // Ritkítva: ez a metódus 100 ms-enként fut, a tanévnyi naptár JSON
    // beolvasása+parse-olása viszont drága. Üres cache-nél 30 mp-enként
    // próbáljuk újra.
    if (_entryCount == 0 &&
        (_lastCacheLoadMs == 0 || millis() - _lastCacheLoadMs > 30000UL)) {
        _lastCacheLoadMs = millis();
        loadScheduleFromCache(getTodayDateStr());
    }

    if (_entryCount > 0) checkSchedule();
}

// ---------------------------------------------------------------------------
// maybeSyncSchedule
// ---------------------------------------------------------------------------
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

        bool versionOk = fetchVersion(serverTodayVer, serverDefaultVer,
                                      serverIsHoliday);

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
                        Serial.printf("[BELL] Loaded from NVS cache (%d entries)\n",
                                      _entryCount);
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

    // ── Offline ág ──
    if (_syncedToday) return;

    if (loadTodayFromNVS(today, "")) {
        _loadedDate     = today;
        _syncedToday    = true;
        _scheduleSource = "nvs";
        Serial.printf("[BELL] Offline: loaded NVS cache (%d entries)\n", _entryCount);
        return;
    }

    // A napi "ma" cache csak arra az egy napra érvényes, amikor mentődött –
    // ha a dátum időközben (offline állapotban) fordult, ez itt már meghal.
    // A teljes tanévnyi naptárból viszont BÁRMELYIK napra fel tudjuk oldani
    // a helyes csengetési rendet (naptár-kivétel/ünnepnap/hétvége/default),
    // nem csak a legutóbb cache-elt napra.
    bool fyIsHoliday = false;
    if (resolveFullYearForDate(today, fyIsHoliday)) {
        _loadedDate     = today;
        _syncedToday    = true;
        _scheduleSource = fyIsHoliday ? "nvs-fullyear-holiday" : "nvs-fullyear";
        Serial.printf("[BELL] Offline: full-year calendar resolved for %s (%d entries, holiday=%d)\n",
                      today.c_str(), _entryCount, fyIsHoliday ? 1 : 0);
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

// ---------------------------------------------------------------------------
// fetchVersion – GET /bells/version
// ---------------------------------------------------------------------------
bool BellManager::fetchVersion(String& outTodayVer,
                               String& outDefaultVer,
                               bool& outIsHoliday) {
    if (!backend.isReady()) return false;

    JsonDocument resp;
    int code = 0;
    if (!backend.getJson("/bells/version", resp, code)) return false;

    outTodayVer   = resp["todayVersion"]   | "";
    outDefaultVer = resp["defaultVersion"] | "";
    outIsHoliday  = resp["isHoliday"]      | false;

    return (outTodayVer.length() > 0);
}

// ---------------------------------------------------------------------------
// fetchFullSync – GET /bells/sync + hangfájlok letöltése
// ---------------------------------------------------------------------------
bool BellManager::fetchFullSync() {
    if (!backend.isReady()) return false;

    JsonDocument resp;
    int code = 0;
    if (!backend.getJson("/bells/sync", resp, code)) {
        Serial.printf("[BELL] GET /bells/sync -> %d\n", code);
        return false;
    }
    Serial.printf("[BELL] GET /bells/sync -> %d\n", code);

    String todayVer   = resp["todayVersion"]   | "";
    String defaultVer = resp["defaultVersion"] | "";
    bool   isHoliday  = resp["isHoliday"]      | false;

    // ── Mai schedule ──
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
        Serial.printf("[BELL] Today: %d entries (ver: %s)\n",
                      _entryCount, todayVer.c_str());
    }

    saveTodayToNVS(getTodayDateStr(), todayVer);
    _todayVersionKnown = todayVer;

    // ── Default schedule ──
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

    // ── Teljes tanévnyi naptár (sablonok + naptár-kivételek) ──
    // Additív mezők a /bells/sync válaszban (régi backend ezeket nem küldi,
    // ilyenkor resp["fullYearVersion"] üres marad és kimarad a mentés –
    // a régi napi/default cache-mechanizmus változatlanul működik).
    String fullYearVer = resp["fullYearVersion"] | "";
    if (fullYearVer.length() > 0 && fullYearVer != _fullYearVersionKnown) {
        saveFullYearToNVS(fullYearVer, resp);
        _fullYearVersionKnown = fullYearVer;
        Serial.printf("[BELL] Full-year calendar saved to NVS (ver: %s)\n",
                      fullYearVer.c_str());
    }

    // ── Hangfájlok szinkronizálása LittleFS-re ──
    // Lépések:
    //   1. Összeállítjuk a szerver által ismert fájlnévlistát
    //   2. LittleFS-en lévő .mp3 fájlok közül töröljük azokat,
    //      amelyek NEM szerepelnek a szerver listájában
    //      (= felhasználó törölte a frontenden)
    //   3. Letöltjük a hiányzó / megváltozott fájlokat
    JsonArray sounds = resp["sounds"].as<JsonArray>();
    if (!sounds.isNull()) {

        // 1. Szerver által ismert fájlnevek összegyűjtése
        //    (max MAX_BELL_ENTRIES elemet tárolunk, ami bőven elég)
        String serverFiles[MAX_BELL_ENTRIES];
        uint8_t serverFileCount = 0;
        for (JsonObject s : sounds) {
            String fn = s["filename"] | "";
            if (fn.isEmpty()) continue;
            if (fn[0] != '/') fn = "/" + fn;
            if (serverFileCount < MAX_BELL_ENTRIES) {
                serverFiles[serverFileCount++] = fn;
            }
        }

        // 2. LittleFS cleanup – töröljük a szerver listájából hiányzó .mp3 fájlokat
        int dlRemoved = 0;
        File root = LittleFS.open("/");
        if (root && root.isDirectory()) {
            File entry = root.openNextFile();
            while (entry) {
                String entryName = "/" + String(entry.name());
                entry.close();

                // Csak .mp3 fájlokat kezeljük
                if (entryName.endsWith(".mp3")) {
                    bool found = false;
                    for (uint8_t i = 0; i < serverFileCount; i++) {
                        if (serverFiles[i] == entryName) { found = true; break; }
                    }
                    // A GYÁRI DEFAULT hangokat SOHA nem töröljük.
                    //
                    // Ezek a firmware LittleFS képében (data/) érkeznek, és
                    // minden `soundFile` nélküli csengetés rájuk hivatkozik.
                    // Ha a szerver listája nem tartalmazza őket (pl. egy új,
                    // még seedeletlen tenantnál), a korábbi kód KITÖRÖLTE
                    // őket – és onnantól az adott csengetés NÉMÁN elmaradt.
                    // Ez a rendszer alapvető működése, itt kivétel nincs.
                    const bool isFactoryDefault =
                        entryName.equals(BELL_DEFAULT_SIGNAL) ||
                        entryName.equals(BELL_DEFAULT_MAIN);

                    if (!found && !isFactoryDefault) {
                        Serial.printf("[BELL] Sound removed (not on server): %s\n",
                                      entryName.c_str());
                        LittleFS.remove(entryName);
                        dlRemoved++;
                    } else if (!found && isFactoryDefault) {
                        Serial.printf("[BELL] Gyari default megtartva (nincs a szerver listajan): %s\n",
                                      entryName.c_str());
                    }
                }
                entry = root.openNextFile();
            }
            root.close();
        }

        // 3. Letöltés / frissítés
        int dlOk   = 0;
        int dlSkip = 0;
        int dlFail = 0;
        for (JsonObject s : sounds) {
            String filename  = s["filename"]  | "";
            String url       = s["url"]       | "";
            size_t sizeBytes = s["sizeBytes"] | 0;

            if (filename.isEmpty() || url.isEmpty()) continue;

            String localPath = filename.startsWith("/") ? filename : "/" + filename;

            // Már létezik és mérete egyezik → skip
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

            Serial.printf("[BELL] Downloading sound: %s (%d bytes)\n",
                          filename.c_str(), sizeBytes);

            bool ok = backend.downloadFile(url, localPath, sizeBytes);
            if (ok) {
                Serial.printf("[BELL] Sound downloaded: %s\n", filename.c_str());
                dlOk++;
            } else {
                Serial.printf("[BELL] Sound FAILED: %s\n", filename.c_str());
                dlFail++;
            }
        }
        Serial.printf("[BELL] Sounds: %d ok, %d skip, %d fail, %d removed\n",
                      dlOk, dlSkip, dlFail, dlRemoved);
    }

    return true;
}

// ---------------------------------------------------------------------------
// NVS – napi cache
// ---------------------------------------------------------------------------
void BellManager::saveTodayToNVS(const String& dateStr, const String& version) {
    Preferences prefs;
    if (!prefs.begin(NVS_BELL_NS, false)) return;
    prefs.putString(NVS_BELL_DATE,      dateStr);
    prefs.putString(NVS_BELL_TODAY_VER, version);
    prefs.putUChar(NVS_BELL_COUNT,      _entryCount);
    if (_entryCount > 0) {
        prefs.putBytes(NVS_BELL_DATA, _entries, _entryCount * sizeof(BellEntry));
    }
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
        prefs.end();
        _entryCount = 0;
        return true;
    }
    if (count > MAX_BELL_ENTRIES) { prefs.end(); return false; }

    size_t bytes = prefs.getBytes(NVS_BELL_DATA, _entries,
                                  count * sizeof(BellEntry));
    String ver = prefs.getString(NVS_BELL_TODAY_VER, "");
    prefs.end();

    if (bytes != count * sizeof(BellEntry)) return false;
    _entryCount        = count;
    _todayVersionKnown = ver;
    return true;
}

// ---------------------------------------------------------------------------
// NVS – default schedule
// ---------------------------------------------------------------------------
void BellManager::saveDefaultToNVS(const String& version,
                                    const BellEntry* entries,
                                    uint8_t count) {
    Preferences prefs;
    if (!prefs.begin(NVS_BELL_DEF_NS, false)) return;
    prefs.putString(NVS_BELL_DEF_VER,  version);
    prefs.putUChar(NVS_BELL_DEF_COUNT, count);
    if (count > 0) {
        prefs.putBytes(NVS_BELL_DEF_DATA, entries, count * sizeof(BellEntry));
    }
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

    size_t bytes = prefs.getBytes(NVS_BELL_DEF_DATA, _entries,
                                  count * sizeof(BellEntry));
    String ver = prefs.getString(NVS_BELL_DEF_VER, "");
    prefs.end();

    if (bytes != count * sizeof(BellEntry)) return false;
    _entryCount          = count;
    _defaultVersionKnown = ver;
    return true;
}

// ---------------------------------------------------------------------------
// NVS – teljes tanévnyi naptár
// ---------------------------------------------------------------------------
void BellManager::saveFullYearToNVS(const String& version, const JsonDocument& src) {
    // Csak a szükséges almezőket mentjük (nem a teljes /bells/sync választ,
    // ami a "ma" nézetet is tartalmazza – azt a napi cache már kezeli).
    JsonDocument fy;
    fy["templates"]          = src["templates"];
    fy["calendar"]           = src["calendar"];
    fy["defaultTemplateId"]  = src["defaultTemplateId"];

    String json;
    serializeJson(fy, json);

    // Védőháló: ha valamiért kirívóan nagy lenne (pl. sok naptár-kivétel +
    // sok sablon), inkább kihagyjuk a mentést, mint hogy egy sérült/csonka
    // NVS bejegyzés keletkezzen. ESP-IDF NVS blob/string egy bejegyzésben
    // több oldalra is szétosztható, de a gyakorlatban egy tanév naptára
    // (max 6 sablon × 40 bejegyzés + néhány tucat kivétel-nap) jóval e
    // limit alatt marad.
    const size_t MAX_FY_JSON_BYTES = 24 * 1024;
    if (json.length() > MAX_FY_JSON_BYTES) {
        Serial.printf("[BELL] Full-year JSON túl nagy (%d byte) – mentés kihagyva\n",
                      json.length());
        return;
    }

    Preferences prefs;
    if (!prefs.begin(NVS_BELL_FY_NS, false)) return;
    prefs.putString(NVS_BELL_FY_VER,  version);
    prefs.putString(NVS_BELL_FY_DATA, json);
    prefs.end();
}

bool BellManager::resolveFullYearForDate(const String& dateStr, bool& outIsHoliday) {
    outIsHoliday = false;

    Preferences prefs;
    if (!prefs.begin(NVS_BELL_FY_NS, true)) return false;
    String json = prefs.getString(NVS_BELL_FY_DATA, "");
    prefs.end();
    if (json.isEmpty()) return false;

    JsonDocument fy;
    if (deserializeJson(fy, json) != DeserializationError::Ok) return false;

    JsonArray calendar  = fy["calendar"].as<JsonArray>();
    JsonArray templates = fy["templates"].as<JsonArray>();
    String defaultTemplateId = fy["defaultTemplateId"] | "";

    // 1. Van-e explicit naptár-kivétel erre a napra (ünnepnap vagy egyedi sablon)?
    String targetTemplateId;
    bool   hasCalendarEntry = false;
    if (!calendar.isNull()) {
        for (JsonObject d : calendar) {
            String date = d["date"] | "";
            if (date != dateStr) continue;
            hasCalendarEntry = true;
            if (d["isHoliday"] | false) {
                outIsHoliday = true;
                _entryCount  = 0;
                return true;
            }
            targetTemplateId = d["templateId"] | "";
            break;
        }
    }

    // 2. Nincs naptár-kivétel → hétvégén nincs csengetés (egyezően a backend
    //    online bell.scheduler.ts viselkedésével), egyébként a default sablon.
    if (!hasCalendarEntry) {
        struct tm t = network.getTimeInfo();
        if (t.tm_wday == 0 || t.tm_wday == 6) {
            outIsHoliday = true;
            _entryCount  = 0;
            return true;
        }
        targetTemplateId = defaultTemplateId;
    }

    if (targetTemplateId.isEmpty() || templates.isNull()) return false;

    // 3. A célzott sablon megkeresése és bells másolása _entries-be.
    for (JsonObject tpl : templates) {
        String tid = tpl["id"] | "";
        if (tid != targetTemplateId) continue;

        JsonArray bells = tpl["bells"].as<JsonArray>();
        _entryCount = 0;
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
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Hardcoded fallback
// ---------------------------------------------------------------------------
void BellManager::loadHardcodedDefault() {
    _entryCount = min((uint8_t)MAX_BELL_ENTRIES, HARDCODED_DEFAULT_COUNT);
    memcpy(_entries, HARDCODED_DEFAULT, _entryCount * sizeof(BellEntry));
}

// ---------------------------------------------------------------------------
// resolveLocalSound – "a csengetés sosem maradhat el"
// ---------------------------------------------------------------------------
// A `playFile()` csendben visszatér, ha a fájl nincs meg a LittleFS-en, tehát
// egy törölt/le nem töltött hangnál a csengetés NÉMÁN elmaradna. Itt olyan
// útvonalat keresünk, ami tényleg létezik – a gyári default hangok (a
// firmware LittleFS képéből) a végső mentőöv.
String BellManager::resolveLocalSound(const char* soundFile, BellType type) {
    // 1. A kért fájl
    if (soundFile && soundFile[0]) {
        String p = (soundFile[0] == '/') ? String(soundFile) : "/" + String(soundFile);
        if (LittleFS.exists(p)) return p;
        Serial.printf("[BELL] HIANYZO hangfajl: %s -> default\n", p.c_str());
    }

    // 2. A típushoz tartozó gyári default
    const char* primary = (type == BellType::SIGNAL) ? BELL_DEFAULT_SIGNAL : BELL_DEFAULT_MAIN;
    if (LittleFS.exists(primary)) return String(primary);

    // 3. A másik gyári default
    const char* secondary = (type == BellType::SIGNAL) ? BELL_DEFAULT_MAIN : BELL_DEFAULT_SIGNAL;
    if (LittleFS.exists(secondary)) return String(secondary);

    // 4. Végső esély: bármelyik .mp3 a tárhelyen. Inkább szóljon "valami",
    //    mint hogy egy jelzés teljesen elmaradjon.
    File root = LittleFS.open("/");
    if (root && root.isDirectory()) {
        File e = root.openNextFile();
        while (e) {
            String n = "/" + String(e.name());
            bool isMp3 = n.endsWith(".mp3");
            e.close();
            if (isMp3) {
                Serial.printf("[BELL] VESZHELYZETI hang: %s\n", n.c_str());
                return n;
            }
            e = root.openNextFile();
        }
    }

    Serial.println("[BELL] ⛔ EGYETLEN hangfajl sincs a tarhelyen!");
    return String();
}

// ---------------------------------------------------------------------------
// checkSchedule
// ---------------------------------------------------------------------------
// Napon belüli perc (0..1439) alapú bit-tárolók – ld. BellManager.h.
static inline bool bellBitGet(const uint8_t* bits, int minuteOfDay) {
    if (minuteOfDay < 0 || minuteOfDay >= 1440) return false;
    return (bits[minuteOfDay >> 3] & (1 << (minuteOfDay & 7))) != 0;
}
static inline void bellBitSet(uint8_t* bits, int minuteOfDay, bool v) {
    if (minuteOfDay < 0 || minuteOfDay >= 1440) return;
    if (v) bits[minuteOfDay >> 3] |=  (1 << (minuteOfDay & 7));
    else   bits[minuteOfDay >> 3] &= ~(1 << (minuteOfDay & 7));
}

// A szabály MINDHÁROM kliensen (ESP32 / Linux / Windows) azonos – korábban
// háromféle volt, ami ugyanabban a helyzetben eltérő viselkedést adott:
//   ESP32:   !ws && !snap   → a backend-folyamat leállásakor (deploy!) néma
//                             maradt, mert a snapserver KÜLÖN PM2 processz,
//                             és a snapclient-kapcsolat élve maradt
//   Linux:   !ws            → nem vette észre, ha a snap-kapcsolat halt meg
//   Windows: !snap          → nem vette észre, ha a backend halt meg
//
// Helyesen: az online csengetéshez MINDKETTŐ kell (a backend hajtja a mixert
// – ezt a WS jelzi –, a hang pedig a snap-streamen érkezik), tehát
//     "a backend elérhető"  ==  ws ÉS snap        (ld. setBackendReachable)
//
// Időzítés (a megrendelt viselkedés szerint):
//   • T-60 mp-től folyamatosan figyeljük az elérhetőséget ("felfegyverzés")
//   • T-kor: ha megjött a csengetéshez tartozó BELL PREPARE, ŐT hagyjuk
//     dolgozni – ez a legmegbízhatóbb jel, mert közvetlenül azt méri, hogy az
//     online út működött-e, nem tippel a kapcsolat állapotából. Ez zárja ki a
//     dupla csengetést.
//   • ha T-60-kor nem volt elérhető, vagy most sem az → AZONNAL helyben
//     játszunk
//   • ha bizonytalan (kapcsolat él, de PREPARE nem jött) → türelmi idő, utána
//     mégis helyben játszunk, hogy a csengetés ne maradjon el
void BellManager::checkSchedule() {
    struct tm t = network.getTimeInfo();

    // Napváltáskor nullázzuk a napi állapotot.
    if (_bellStateDay != t.tm_yday) {
        _bellStateDay = t.tm_yday;
        memset(_bellHandledBits, 0, sizeof(_bellHandledBits));
        memset(_bellArmedBits,   0, sizeof(_bellArmedBits));
    }

    const long nowSec = (long)t.tm_hour * 3600 + (long)t.tm_min * 60 + (long)t.tm_sec;

    // Friss BELL PREPARE = az online út él (a backend most játssza le).
    const bool onlineBellEvidence =
        _lastOnlineBellMs != 0 &&
        (millis() - _lastOnlineBellMs) <= BELL_ONLINE_EVIDENCE_MS;

    for (uint8_t i = 0; i < _entryCount; i++) {
        const int  bellMin = _entries[i].hour * 60 + _entries[i].minute;
        if (bellBitGet(_bellHandledBits, bellMin)) continue;

        const long bellSec = (long)bellMin * 60;
        const long dt      = nowSec - bellSec;

        // 1) Előzetes ellenőrzés T-60 mp-től, folyamatosan frissítve.
        if (dt >= -BELL_LEAD_CHECK_S && dt < 0) {
            if (_backendReachable) {
                bellBitSet(_bellArmedBits, bellMin, false);
            } else if (!bellBitGet(_bellArmedBits, bellMin)) {
                bellBitSet(_bellArmedBits, bellMin, true);
                Serial.printf("[BELL] %02d:%02d – a backend nem erheto el (%ld mp-cel elotte) -> offline lejatszasra keszulunk\n",
                              _entries[i].hour, _entries[i].minute, -dt);
            }
            continue;
        }

        if (dt < 0) continue;

        // Felső korlát: ha az eszköz egy már elmúlt csengetés UTÁN indult el
        // (vagy sokáig nem volt pontos ideje), NE pótoljuk utólag – egy
        // délután bekapcsolt hangszóró ne csengessen rá a reggeli időpontokra.
        if (dt > BELL_CATCHUP_MAX_S) {
            bellBitSet(_bellHandledBits, bellMin, true);
            bellBitSet(_bellArmedBits,   bellMin, false);
            continue;
        }

        // 2) A csengetés pillanata (és utána).
        if (onlineBellEvidence) {
            bellBitSet(_bellHandledBits, bellMin, true);
            continue;
        }

        if (!bellBitGet(_bellArmedBits, bellMin) && _backendReachable && dt < BELL_GRACE_S) {
            continue;   // még várunk a PREPARE-re
        }

        const char* sf = _entries[i].soundFile[0]
                         ? _entries[i].soundFile
                         : (_entries[i].type == BellType::SIGNAL
                            ? "jelzocsengo.mp3"
                            : "kibecsengo.mp3");

        // Garantáltan létező útvonal – ha a kért hang hiányzik, a gyári
        // defaultra esünk vissza. Csengetés nem maradhat el.
        String path = resolveLocalSound(sf, _entries[i].type);

        Serial.printf("[BELL] OFFLINE %s @ %02d:%02d  kert:%s jatszott:%s (src:%s, ws+snap=%d, armed=%d)\n",
                      _entries[i].type == BellType::SIGNAL ? "SIGNAL" : "MAIN",
                      _entries[i].hour, _entries[i].minute, sf,
                      path.isEmpty() ? "(nincs)" : path.c_str(),
                      _scheduleSource.c_str(), _backendReachable ? 1 : 0,
                      bellBitGet(_bellArmedBits, bellMin) ? 1 : 0);

        if (!path.isEmpty()) audio.playFile(path.c_str());

        bellBitSet(_bellHandledBits, bellMin, true);
        bellBitSet(_bellArmedBits,   bellMin, false);

        if (_mode == BELL_MODE_TODAY) {
            const int curMin = t.tm_hour * 60 + t.tm_min;
            bool isLast = true;
            for (uint8_t j = 0; j < _entryCount; j++) {
                if (_entries[j].hour * 60 + _entries[j].minute > curMin) {
                    isLast = false; break;
                }
            }
            if (isLast) _mode = BELL_MODE_ON;
        }
        break;   // egyszerre csak egy csengetést indítunk
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
    int curSec  = t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
    int minDiff = 999999;

    for (uint8_t i = 0; i < _entryCount; i++) {
        int diff = (_entries[i].hour * 3600 + _entries[i].minute * 60) - curSec;
        if (diff > 0 && diff < minDiff) minDiff = diff;
    }
    return (minDiff == 999999) ? -1 : minDiff;
}

// ---------------------------------------------------------------------------
// getNextEventTimeStr
// ---------------------------------------------------------------------------
String BellManager::getNextEventTimeStr() {
    if (!network.isTimeSynced()) return "";
    if (_mode == BELL_MODE_OFF)  return "OFF";
    if (_entryCount == 0)        return "";

    struct tm t  = network.getTimeInfo();
    int curMin   = t.tm_hour * 60 + t.tm_min;
    int nextMin  = 9999;

    for (uint8_t i = 0; i < _entryCount; i++) {
        int eMin = _entries[i].hour * 60 + _entries[i].minute;
        if (eMin > curMin && eMin < nextMin) nextMin = eMin;
    }

    if (nextMin == 9999) return "";
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", nextMin / 60, nextMin % 60);
    return String(buf);
}

// ---------------------------------------------------------------------------
// getTodayDateStr
// ---------------------------------------------------------------------------
String BellManager::getTodayDateStr() {
    struct tm t = network.getTimeInfo();
    char buf[40];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    return String(buf);
}

// ---------------------------------------------------------------------------
// onScheduleSync – WS push feldolgozása
// ---------------------------------------------------------------------------
// A szerver SCHEDULE_SYNC üzenetben elküldi az összes szükséges adatot.
// Feldolgozzuk a mai és a default menetrendet, frissítjük az NVS cache-t,
// majd letöltjük a hangfájlokat ha szükséges.
// Ez a metódus a fetchFullSync() adataival teljesen egyenértékű, csak
// a forrás a WS push helyett HTTP.
void BellManager::onScheduleSync(const JsonDocument& msg) {
    Serial.println("[BELL] WS SCHEDULE_SYNC fogadva – schedule frissítés");

    String todayVer   = msg["todayVersion"]   | "";
    String defaultVer = msg["defaultVersion"] | "";
    bool   isHoliday  = msg["isHoliday"]      | false;

    // ── Mai schedule ──
    _entryCount = 0;
    if (isHoliday) {
        _scheduleSource = "ws-holiday";
        Serial.println("[BELL] SCHEDULE_SYNC: ma ünnepnap, nincs csengetés");
    } else {
        JsonArrayConst bells = msg["bells"].as<JsonArrayConst>();
        if (!bells.isNull()) {
            for (JsonVariantConst b : bells) {
                if (_entryCount >= MAX_BELL_ENTRIES) break;
                BellEntry& e = _entries[_entryCount];
                e.hour   = b["hour"]   | 0;
                e.minute = b["minute"] | 0;
                String t  = b["type"]  | "MAIN";
                e.type    = (t == "SIGNAL") ? BellType::SIGNAL : BellType::MAIN;
                String sf = b["soundFile"] | "kibecsengo.mp3";
                strncpy(e.soundFile, sf.c_str(), sizeof(e.soundFile) - 1);
                e.soundFile[sizeof(e.soundFile) - 1] = '\0';
                _entryCount++;
            }
        }
        _scheduleSource = "ws";
        Serial.printf("[BELL] SCHEDULE_SYNC: %d bejegyzés (ver: %s)\n",
                      _entryCount, todayVer.c_str());
    }

    String today = getTodayDateStr();
    saveTodayToNVS(today, todayVer);
    _todayVersionKnown = todayVer;
    _loadedDate        = today;
    _syncedToday       = true;
    _syncedFromServer  = true;

    // ── Default schedule ──
    JsonArrayConst defaultBells = msg["defaultBells"].as<JsonArrayConst>();
    if (!defaultBells.isNull() && defaultVer != _defaultVersionKnown) {
        BellEntry defEntries[MAX_BELL_ENTRIES];
        uint8_t defCount = 0;
        for (JsonVariantConst b : defaultBells) {
            if (defCount >= MAX_BELL_ENTRIES) break;
            BellEntry& e = defEntries[defCount];
            e.hour   = b["hour"]   | 0;
            e.minute = b["minute"] | 0;
            String t  = b["type"]  | "MAIN";
            e.type    = (t == "SIGNAL") ? BellType::SIGNAL : BellType::MAIN;
            String sf = b["soundFile"] | "kibecsengo.mp3";
            strncpy(e.soundFile, sf.c_str(), sizeof(e.soundFile) - 1);
            e.soundFile[sizeof(e.soundFile) - 1] = '\0';
            defCount++;
        }
        saveDefaultToNVS(defaultVer, defEntries, defCount);
        _defaultVersionKnown = defaultVer;
        Serial.printf("[BELL] SCHEDULE_SYNC: default mentve NVS-be (%d bejegyzés)\n", defCount);
    }

    // ── Teljes tanévnyi naptár ──
    // Ld. fetchFullSync() – ugyanaz az additív mező-kezelés, csak WS forrásból.
    String fullYearVer = msg["fullYearVersion"] | "";
    if (fullYearVer.length() > 0 && fullYearVer != _fullYearVersionKnown) {
        saveFullYearToNVS(fullYearVer, msg);
        _fullYearVersionKnown = fullYearVer;
        Serial.printf("[BELL] SCHEDULE_SYNC: teljes tanévnyi naptár mentve (ver: %s)\n",
                      fullYearVer.c_str());
    }

    // ── Hangfájlok szinkronizálása ──
    // Ugyanaz a logika mint fetchFullSync-ben
    JsonArrayConst sounds = msg["sounds"].as<JsonArrayConst>();
    if (!sounds.isNull()) {
        String serverFiles[MAX_BELL_ENTRIES];
        uint8_t serverFileCount = 0;
        for (JsonVariantConst s : sounds) {
            String fn = s["filename"] | "";
            if (fn.isEmpty()) continue;
            if (fn[0] != '/') fn = "/" + fn;
            if (serverFileCount < MAX_BELL_ENTRIES) serverFiles[serverFileCount++] = fn;
        }

        // LittleFS cleanup
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
                    // A gyári default hangokat SOHA nem töröljük – ld. a
                    // fetchFullSync() azonos védelmét. Nélkülük egy
                    // `soundFile` nélküli csengetés némán elmaradna.
                    const bool isFactoryDefault =
                        entryName.equals(BELL_DEFAULT_SIGNAL) ||
                        entryName.equals(BELL_DEFAULT_MAIN);
                    if (!found && !isFactoryDefault) {
                        LittleFS.remove(entryName);
                        Serial.printf("[BELL] Törölt hang: %s\n", entryName.c_str());
                    }
                }
                entry = root.openNextFile();
            }
            root.close();
        }

        // Letöltés
        int dlOk = 0, dlSkip = 0, dlFail = 0;
        for (JsonVariantConst s : sounds) {
            String filename  = s["filename"]  | "";
            String url       = s["url"]       | "";
            size_t sizeBytes = s["sizeBytes"] | 0;
            if (filename.isEmpty() || url.isEmpty()) continue;

            String localPath = filename.startsWith("/") ? filename : "/" + filename;

            // Teljes URL összeállítása ha relatív
            if (url.startsWith("/")) {
                url = String(BACKEND_BASE_URL) + url;
            }

            if (sizeBytes > 0 && LittleFS.exists(localPath)) {
                File f = LittleFS.open(localPath, "r");
                if (f && (size_t)f.size() == sizeBytes) { f.close(); dlSkip++; continue; }
                if (f) f.close();
            }

            bool ok = backend.downloadFile(url, localPath, sizeBytes);
            if (ok) dlOk++; else dlFail++;
        }
        Serial.printf("[BELL] Hangok: %d ok, %d skip, %d fail\n", dlOk, dlSkip, dlFail);
    }

    // Visszajelzés törlése: ha az eszköz mostanáig version-check-et futtatott,
    // a jövőbeli check-ek tudják, hogy a szinkron friss
    _lastVersionCheckMs = millis();
}
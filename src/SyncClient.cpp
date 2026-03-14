// ─────────────────────────────────────────────────────────────────────────────
// SyncClient.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "SyncClient.h"
#include <LittleFS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <sys/time.h>

SyncClient* SyncClient::_instance = nullptr;

// timegm() helyettesítő – UTC struct tm → time_t, timezone offset nélkül
static time_t utcmktime(struct tm* t) {
    // Napok száma hónap szerint (nem szökőév)
    static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int y = t->tm_year + 1900;
    int m = t->tm_mon;  // 0-11
    // Napok 1970.01.01 óta
    long days = (y - 1970) * 365L;
    for (int i = 1970; i < y; i++) {
        if ((i % 4 == 0 && i % 100 != 0) || i % 400 == 0) days++;
    }
    for (int i = 0; i < m; i++) {
        days += mdays[i];
        if (i == 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) days++;
    }
    days += t->tm_mday - 1;
    return (time_t)(days * 86400L + t->tm_hour * 3600L + t->tm_min * 60L + t->tm_sec);
}

// ── Statikus WS callback ─────────────────────────────────────────────────────
void SyncClient::wsEventHandler(WStype_t type, uint8_t* payload, size_t length) {
    if (!_instance) return;
    switch (type) {
        case WStype_CONNECTED:
            _instance->onConnected();
            break;
        case WStype_DISCONNECTED:
            _instance->onDisconnected();
            break;
        case WStype_TEXT:
            _instance->onMessage(String((char*)payload));
            break;
        case WStype_PING:
        case WStype_PONG:
            break;
        default:
            break;
    }
}

// ── begin ────────────────────────────────────────────────────────────────────
void SyncClient::begin(AudioManager& audio, BellManager& bells,
                       const String& deviceKey, const String& tenantId) {
    _instance  = this;
    _audio     = &audio;
    _bells     = &bells;
    _deviceKey = deviceKey;
    _tenantId  = tenantId;

    // PSRAM buffer foglalás
    if (psramFound()) {
        _psramBuf = (uint8_t*)ps_malloc(SYNC_PSRAM_BUF_SIZE);
        if (_psramBuf) {
            Serial.printf("[SYNC] PSRAM buffer: %d KB\n", SYNC_PSRAM_BUF_SIZE / 1024);
        }
    }

    // WebSocket SSL kapcsolat setup
    String path = "/sync?deviceKey=" + _deviceKey;
    _ws.beginSSL(SYNC_WS_HOST, SYNC_WS_PORT, path.c_str());
    _ws.onEvent(wsEventHandler);
    _ws.setReconnectInterval(SYNC_WS_RECONNECT);
    _ws.enableHeartbeat(25000, 3000, 2);  // ping 25s, pong timeout 3s, 2 retry
    _wsSetup = true;

    Serial.printf("[SYNC] WebSocket setup → wss://%s%s\n",
                  SYNC_WS_HOST, path.c_str());
}

// ── loop ─────────────────────────────────────────────────────────────────────
void SyncClient::loop() {
    if (!_wsSetup) return;
    _ws.loop();
}

// ── onConnected ──────────────────────────────────────────────────────────────
void SyncClient::onConnected() {
    _connected = true;
    Serial.println("[SYNC] ✅ WebSocket csatlakozva");
    // Időszinkron kérés
    sendTimeSyncRequest();
}

// ── onDisconnected ───────────────────────────────────────────────────────────
void SyncClient::onDisconnected() {
    _connected = false;
    Serial.println("[SYNC] ❌ WebSocket lecsatlakozva – reconnect...");
}

// ── onMessage ────────────────────────────────────────────────────────────────
void SyncClient::onMessage(const String& msg) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, msg);
    if (err) {
        Serial.printf("[SYNC] JSON parse hiba: %s\n", err.c_str());
        return;
    }

    String type  = doc["type"]  | "";
    String phase = doc["phase"] | "";

    if (type == "HELLO") {
        handleHello(doc);
    } else if (phase == "PREPARE") {
        handlePrepare(doc);
    } else if (phase == "PLAY") {
        handlePlay(doc);
    } else if (doc["action"].is<const char*>()) {
        // Azonnali broadcast (SYNC_BELLS, STOP_PLAYBACK stb.)
        handleImmediate(doc);
    }
}

// ── handleHello ──────────────────────────────────────────────────────────────
void SyncClient::handleHello(const JsonDocument& doc) {
    // Numerikus ms preferencia – timezone parsing nélkül
    int64_t serverMs = 0;
    if (doc["serverNowMs"].is<long long>() || doc["serverNowMs"].is<unsigned long>()) {
        serverMs = (int64_t)doc["serverNowMs"].as<long long>();
    } else {
        // Fallback: ISO parsing
        const char* s = doc["serverNow"] | "";
        if (!s || !*s) return;
        struct tm tm = {}; int ms = 0;
        if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d.%dZ",
                   &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                   &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &ms) >= 6) {
            tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_isdst = 0;
            serverMs = (int64_t)utcmktime(&tm) * 1000 + ms;
        }
    }
    if (serverMs == 0) return;

    _serverOffsetMs = serverMs - nowMs();
    Serial.printf("[SYNC] Szerver offset: %lld ms\n", _serverOffsetMs);

    // Ha az eltérés > 2s → korrigáljuk a rendszerórát
    if (llabs(_serverOffsetMs) > 2000) {
        struct timeval tv;
        tv.tv_sec  = (time_t)(serverMs / 1000);
        tv.tv_usec = (suseconds_t)((serverMs % 1000) * 1000);
        settimeofday(&tv, nullptr);
        _serverOffsetMs = 0;
        Serial.println("[SYNC] Rendszeróra korrigálva szerver időre");
    }

    // Időszinkron visszaigazolás
    JsonDocument resp;
    resp["type"] = "TIME_SYNC";
    resp["seq"]  = (uint32_t)millis();
    String out; serializeJson(resp, out);
    _ws.sendTXT(out);

// ── handlePrepare ─────────────────────────────────────────────────────────────
}

void SyncClient::handlePrepare(const JsonDocument& doc) {
    String commandId = doc["commandId"] | "";
    String action    = doc["action"]    | "";
    String url       = doc["url"]       | "";

    Serial.printf("[SYNC] PREPARE: %s id=%s\n", action.c_str(), commandId.c_str());

    unsigned long prepStart = millis();
    _prep = PreparedCmd{};
    _prep.commandId = commandId;
    _prep.action    = action;
    _prep.url       = url;

    if (url.isEmpty()) {
        // SYNC_BELLS stb. – nincs audio
        _prep.ready = true;
        sendReadyAck(commandId, 0);
        return;
    }

    if (action == "BELL") {
        // Bell hangfájl már a LittleFS-en van
        String filename = url.substring(url.lastIndexOf('/') + 1);
        String path = filename.startsWith("/") ? filename : "/" + filename;
        if (LittleFS.exists(path)) {
            _prep.localPath = path;
            _prep.ready     = true;
            uint32_t bufMs  = millis() - prepStart;
            Serial.printf("[SYNC] BELL READY (LittleFS): %s %dms\n",
                          path.c_str(), bufMs);
            sendReadyAck(commandId, bufMs);
        } else {
            // Nincs LittleFS-en → letöltjük
            bool ok = downloadToLittleFS(url, path);
            _prep.localPath = path;
            _prep.ready     = ok;
            uint32_t bufMs  = millis() - prepStart;
            sendReadyAck(commandId, ok ? bufMs : 9999);
        }

    } else if (action == "TTS") {
        // TTS MP3 letöltése – PSRAM-ba ha van, egyébként LittleFS temp fájl
        if (_psramBuf) {
            bool ok = downloadToPsram(url);
            if (ok) {
                _prep.fromPsram = true;
                _prep.psramBuf  = _psramBuf;
                _prep.ready     = true;
                uint32_t bufMs  = millis() - prepStart;
                Serial.printf("[SYNC] TTS READY (PSRAM %d bytes) %dms\n",
                              (int)_prep.psramLen, bufMs);
                sendReadyAck(commandId, bufMs);
                return;
            }
        }
        // PSRAM fallback: LittleFS temp
        String tempPath = "/tts_sync.mp3";
        bool ok = downloadToLittleFS(url, tempPath);
        _prep.localPath = tempPath;
        _prep.ready     = ok;
        uint32_t bufMs  = millis() - prepStart;
        sendReadyAck(commandId, ok ? bufMs : 9999);

    } else if (action == "PLAY_URL") {
        // Stream – nem lehet pre-buffer, de ellenőrizzük a hálózatot
        _prep.ready = true;
        uint32_t bufMs = millis() - prepStart;
        sendReadyAck(commandId, bufMs);

    } else if (action == "STOP_PLAYBACK") {
        _prep.ready = true;
        sendReadyAck(commandId, 0);
    }
}

// ── handlePlay ───────────────────────────────────────────────────────────────
void SyncClient::handlePlay(const JsonDocument& doc) {
    String commandId = doc["commandId"] | "";
    String playAtStr = doc["playAt"]    | "";

    if (commandId != _prep.commandId || !_prep.ready) {
        Serial.printf("[SYNC] PLAY id mismatch vagy nincs PREPARE: %s\n",
                      commandId.c_str());
        return;
    }

    // playAt → Unix ms (numerikus preferencia)
    int64_t playAtMs = 0;
    if (doc["playAtMs"].is<int64_t>() || doc["playAtMs"].is<uint32_t>()) {
        playAtMs = doc["playAtMs"].as<int64_t>();
    } else {
        // Fallback: ISO parsing
        struct tm tm = {};
        int ms = 0;
        if (sscanf(playAtStr.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d.%dZ",
                   &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                   &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &ms) >= 6) {
            tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_isdst = 0;
            playAtMs = (int64_t)utcmktime(&tm) * 1000 + ms;
        }
    }

    // Várakozás a playAt-ig (NTP + server offset korrigálva)
    int64_t nowWithOffset = nowMs() + _serverOffsetMs;
    int64_t delayMs       = playAtMs - nowWithOffset;

    Serial.printf("[SYNC] PLAY in %lld ms: %s (%s)\n",
                  delayMs, commandId.c_str(), _prep.action.c_str());

    if (delayMs > 0 && delayMs < 5000) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)delayMs));
    }

    // Lejátszás
    if (!_audio) return;
    const String& action = _prep.action;

    if (action == "BELL") {
        if (_prep.localPath.length() > 0) {
            _audio->playFile(_prep.localPath.c_str());
        }
    } else if (action == "TTS") {
        if (_prep.fromPsram && _prep.psramBuf && _prep.psramLen > 0) {
            _audio->playPsram(_prep.psramBuf, _prep.psramLen);
        } else if (_prep.localPath.length() > 0) {
            _audio->playFile(_prep.localPath.c_str());
        }
    } else if (action == "PLAY_URL") {
        if (_prep.url.length() > 0) {
            _audio->playUrl(_prep.url.c_str());
        }
    } else if (action == "STOP_PLAYBACK") {
        _audio->stop();
    }

    // Prep cleanup
    _prep = PreparedCmd{};
}

// ── handleImmediate ───────────────────────────────────────────────────────────
void SyncClient::handleImmediate(const JsonDocument& doc) {
    String action    = doc["action"]    | "";
    String commandId = doc["commandId"] | "";
    String url       = doc["url"]       | "";

    Serial.printf("[SYNC] Azonnali parancs: %s\n", action.c_str());

    if (action == "SYNC_BELLS" && _bells) {
        _bells->requestSync();
    } else if (action == "STOP_PLAYBACK" && _audio) {
        _audio->stop();
    } else if (action == "BELL" && _audio && url.length() > 0) {
        String filename = url.substring(url.lastIndexOf('/') + 1);
        String path = filename.startsWith("/") ? filename : "/" + filename;
        if (LittleFS.exists(path)) {
            _audio->playFile(path.c_str());
        } else {
            _audio->playUrl(url.c_str());
        }
    }
}

// ── sendReadyAck ─────────────────────────────────────────────────────────────
void SyncClient::sendReadyAck(const String& commandId, uint32_t bufferMs) {
    JsonDocument doc;
    doc["type"]       = "READY_ACK";
    doc["commandId"]  = commandId;
    doc["deviceId"]   = _deviceKey;
    doc["bufferMs"]   = bufferMs;

    // ISO timestamp
    int64_t ms = nowMs();
    time_t  sec = ms / 1000;
    struct tm t;
    gmtime_r(&sec, &t);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec, (int)(ms % 1000));
    doc["readyAt"] = buf;

    String out;
    serializeJson(doc, out);
    _ws.sendTXT(out);
    Serial.printf("[SYNC] READY ACK: %s bufferMs=%d\n",
                  commandId.c_str(), bufferMs);
}

// ── sendTimeSyncRequest ───────────────────────────────────────────────────────
void SyncClient::sendTimeSyncRequest() {
    JsonDocument doc;
    doc["type"] = "TIME_SYNC";
    doc["seq"]  = (uint32_t)millis();
    String out;
    serializeJson(doc, out);
    _ws.sendTXT(out);
}

// ── nowMs ─────────────────────────────────────────────────────────────────────
int64_t SyncClient::nowMs() const {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// ── downloadToPsram ───────────────────────────────────────────────────────────
bool SyncClient::downloadToPsram(const String& url) {
    if (!_psramBuf) return false;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(client, url)) return false;

    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }

    int contentLen = http.getSize();
    if (contentLen > SYNC_PSRAM_BUF_SIZE) {
        Serial.printf("[SYNC] TTS túl nagy PSRAM-ba: %d bytes\n", contentLen);
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t total = 0;
    unsigned long t0 = millis();
    uint8_t chunk[1024];

    while (http.connected() && (contentLen < 0 || (int)total < contentLen)) {
        size_t avail = stream->available();
        if (avail == 0) {
            if (millis() - t0 > 10000) break;
            vTaskDelay(1);
            continue;
        }
        size_t toRead = min(avail, sizeof(chunk));
        if (total + toRead > SYNC_PSRAM_BUF_SIZE) break;
        size_t rd = stream->readBytes(chunk, toRead);
        memcpy(_psramBuf + total, chunk, rd);
        total += rd;
        t0 = millis();
    }

    http.end();
    _prep.psramLen = total;
    Serial.printf("[SYNC] PSRAM letöltve: %d bytes\n", (int)total);
    return total > 0;
}

// ── downloadToLittleFS ────────────────────────────────────────────────────────
bool SyncClient::downloadToLittleFS(const String& url, const String& path) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(client, url)) return false;

    int code = http.GET();
    if (code != 200) { http.end(); return false; }

    WiFiClient* stream = http.getStreamPtr();
    File file = LittleFS.open(path, "w");
    if (!file) { http.end(); return false; }

    uint8_t buf[512];
    size_t total = 0;
    int contentLen = http.getSize();
    unsigned long t0 = millis();

    while (http.connected() && (contentLen < 0 || (int)total < contentLen)) {
        size_t avail = stream->available();
        if (avail == 0) {
            if (millis() - t0 > 10000) break;
            vTaskDelay(1);
            continue;
        }
        size_t rd = stream->readBytes(buf, min(avail, sizeof(buf)));
        file.write(buf, rd);
        total += rd;
        t0 = millis();
    }

    file.close();
    http.end();
    Serial.printf("[SYNC] LittleFS letöltve: %s (%d bytes)\n",
                  path.c_str(), (int)total);
    return total > 0;
}
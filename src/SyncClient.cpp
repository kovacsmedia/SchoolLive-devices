// SyncClient.cpp – SchoolLive S3.54
// Változások S3.52 → S3.54:
//   • _notifyActivity() segédfüggvény – _ui->setNetActivity() ha _ui != nullptr
//   • onMessage() – bejövő WS adat esetén jelzés (szerver kommunikál velünk)
//   • sendReadyAck(), sendTimeSyncRequest() – kimenő WS küldés előtt jelzés
//   • downloadToLittleFS() – HTTP letöltés előtt jelzés

#include "SyncClient.h"
#include "UIManager.h"
#include <LittleFS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <sys/time.h>

SyncClient* SyncClient::_instance = nullptr;

// ── UI aktivitás jelzés ───────────────────────────────────────────────────────
void SyncClient::_notifyActivity() {
    if (_ui) _ui->setNetActivity();
}

static time_t utcmktime(struct tm* t) {
    static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int y = t->tm_year + 1900; int m = t->tm_mon;
    long days = (y - 1970) * 365L;
    for (int i = 1970; i < y; i++)
        if ((i%4==0&&i%100!=0)||i%400==0) days++;
    for (int i = 0; i < m; i++) {
        days += mdays[i];
        if (i==1&&((y%4==0&&y%100!=0)||y%400==0)) days++;
    }
    days += t->tm_mday - 1;
    return (time_t)(days*86400L + t->tm_hour*3600L + t->tm_min*60L + t->tm_sec);
}

void SyncClient::wsEventHandler(WStype_t type, uint8_t* payload, size_t length) {
    if (!_instance) return;
    switch (type) {
        case WStype_CONNECTED:    _instance->onConnected(); break;
        case WStype_DISCONNECTED: _instance->onDisconnected(); break;
        case WStype_TEXT:         _instance->onMessage(String((char*)payload)); break;
        default: break;
    }
}

void SyncClient::begin(AudioManager& audio, BellManager& bells,
                       const String& deviceKey, const String& tenantId) {
    _instance  = this;
    _audio     = &audio;
    _bells     = &bells;
    _deviceKey = deviceKey;

    String path = "/sync?deviceKey=" + _deviceKey;
    _ws.beginSSL(SYNC_WS_HOST, SYNC_WS_PORT, path.c_str());
    _ws.onEvent(wsEventHandler);
    _ws.setReconnectInterval(SYNC_WS_RECONNECT);
    _ws.enableHeartbeat(25000, 3000, 2);
    _wsSetup = true;

    Serial.printf("[SYNC] WebSocket setup → wss://%s%s\n", SYNC_WS_HOST, path.c_str());
}

void SyncClient::loop() {
    if (!_wsSetup) return;
    _ws.loop();
}

void SyncClient::onConnected() {
    _connected = true;
    Serial.println("[SYNC] ✅ Csatlakozva");
    sendTimeSyncRequest();
}

void SyncClient::onDisconnected() {
    _connected = false;
    Serial.println("[SYNC] ❌ Lecsatlakozva");
}

// ── onMessage ─────────────────────────────────────────────────────────────────
// Bejövő WS adat → aktivitás jelzés + feldolgozás
void SyncClient::onMessage(const String& msg) {
    _notifyActivity();                          // ← bejövő adat = kommunikáció zajlik

    JsonDocument doc;
    if (deserializeJson(doc, msg) != DeserializationError::Ok) return;

    String type  = doc["type"]  | "";
    String phase = doc["phase"] | "";

    if      (type  == "HELLO")   handleHello(doc);
    else if (phase == "PREPARE") handlePrepare(doc);
    else if (phase == "PLAY")    handlePlay(doc);
    else if (doc["action"].is<const char*>()) handleImmediate(doc);
}

void SyncClient::handleHello(const JsonDocument& doc) {
    int64_t serverMs = doc["serverNowMs"] | (int64_t)0;
    if (serverMs == 0) {
        const char* s = doc["serverNow"] | "";
        struct tm tm = {}; int ms = 0;
        if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d.%dZ",
                   &tm.tm_year,&tm.tm_mon,&tm.tm_mday,
                   &tm.tm_hour,&tm.tm_min,&tm.tm_sec,&ms) >= 6) {
            tm.tm_year -= 1900; tm.tm_mon -= 1;
            serverMs = (int64_t)utcmktime(&tm) * 1000 + ms;
        }
    }
    if (serverMs == 0) return;
    _serverOffsetMs = serverMs - nowMs();
    Serial.printf("[SYNC] Szerver offset: %lld ms\n", _serverOffsetMs);
    if (llabs(_serverOffsetMs) > 2000) {
        struct timeval tv;
        tv.tv_sec  = serverMs / 1000;
        tv.tv_usec = (serverMs % 1000) * 1000;
        settimeofday(&tv, nullptr);
        _serverOffsetMs = 0;
    }
    JsonDocument resp; resp["type"] = "TIME_SYNC"; resp["seq"] = (uint32_t)millis();
    String out; serializeJson(resp, out);
    _notifyActivity();                          // ← kimenő WS küldés
    _ws.sendTXT(out);
}

// ── handlePrepare ─────────────────────────────────────────────────────────────
void SyncClient::handlePrepare(const JsonDocument& doc) {
    String commandId = doc["commandId"] | "";
    String action    = doc["action"]    | "";
    String url       = doc["url"]       | "";

    Serial.printf("[SYNC] PREPARE: %s\n", action.c_str());

    _prep = PreparedCmd{};
    _prep.commandId = commandId;
    _prep.action    = action;
    _prep.url       = url;

    if (action == "BELL" && url.length() > 0) {
        unsigned long t0 = millis();
        String filename = url.substring(url.lastIndexOf('/') + 1);
        String path = "/" + filename;

        if (LittleFS.exists(path)) {
            _prep.localPath = path;
            _prep.ready     = true;
            sendReadyAck(commandId, millis() - t0);
        } else {
            bool ok = downloadToLittleFS(url, path);
            _prep.localPath = path;
            _prep.ready     = ok;
            sendReadyAck(commandId, ok ? millis() - t0 : 9999);
        }
    } else {
        _prep.ready = true;
        sendReadyAck(commandId, 0);
    }
}

// ── handlePlay ────────────────────────────────────────────────────────────────
void SyncClient::handlePlay(const JsonDocument& doc) {
    String commandId = doc["commandId"] | "";
    if (commandId != _prep.commandId || !_prep.ready) return;

    int64_t playAtMs = doc["playAtMs"] | (int64_t)0;
    if (playAtMs == 0) {
        const char* s = doc["playAt"] | "";
        struct tm tm = {}; int ms = 0;
        if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d.%dZ",
                   &tm.tm_year,&tm.tm_mon,&tm.tm_mday,
                   &tm.tm_hour,&tm.tm_min,&tm.tm_sec,&ms) >= 6) {
            tm.tm_year -= 1900; tm.tm_mon -= 1;
            playAtMs = (int64_t)utcmktime(&tm) * 1000 + ms;
        }
    }

    const String& action = _prep.action;
    int32_t startupMs    = (action == "BELL") ? 120 : 0;

    int64_t nowWithOffset = nowMs() + _serverOffsetMs;
    int64_t waitMs        = playAtMs - nowWithOffset - startupMs;

    Serial.printf("[SYNC] PLAY: %s wait=%lld ms\n", action.c_str(), waitMs);

    if (waitMs > 0 && waitMs < 10000) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)waitMs));
    }

    if (!_audio) { _prep = PreparedCmd{}; return; }

    if (action == "BELL") {
        if (_prep.localPath.length() > 0) {
            _audio->playFile(_prep.localPath.c_str());
        }
    } else if (action == "STOP_PLAYBACK") {
        _audio->stop();
    }

    if (_onPlayAction) {
        if      (action == "TTS")           _onPlayAction("UZENET");
        else if (action == "PLAY_URL")      _onPlayAction("RADIO");
        else if (action == "STOP_PLAYBACK") _onPlayAction("");
    }

    _prep = PreparedCmd{};
}

// ── handleImmediate ───────────────────────────────────────────────────────────
void SyncClient::handleImmediate(const JsonDocument& doc) {
    String action = doc["action"] | "";
    String url    = doc["url"]    | "";

    Serial.printf("[SYNC] Azonnali: %s\n", action.c_str());

    if (action == "SYNC_BELLS" && _bells) {
        _bells->requestSync();
    } else if (action == "STOP_PLAYBACK" && _audio) {
        _audio->stop();
        if (_onPlayAction) _onPlayAction("");
    } else if (action == "BELL" && _audio && url.length() > 0) {
        String path = "/" + url.substring(url.lastIndexOf('/') + 1);
        if (LittleFS.exists(path)) {
            _audio->playFile(path.c_str());
        }
    } else if (action == "TTS") {
        if (_onPlayAction) _onPlayAction("UZENET");
    } else if (action == "PLAY_URL") {
        if (_onPlayAction) _onPlayAction("RADIO");
    }
}

// ── sendReadyAck ──────────────────────────────────────────────────────────────
void SyncClient::sendReadyAck(const String& commandId, uint32_t bufferMs) {
    JsonDocument doc;
    doc["type"]      = "READY_ACK";
    doc["commandId"] = commandId;
    doc["deviceId"]  = _deviceKey;
    doc["bufferMs"]  = bufferMs;

    int64_t ms = nowMs();
    time_t sec = ms / 1000;
    struct tm t; gmtime_r(&sec, &t);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             t.tm_year+1900, t.tm_mon+1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec, (int)(ms%1000));
    doc["readyAt"] = buf;

    String out; serializeJson(doc, out);
    _notifyActivity();                          // ← kimenő WS küldés
    _ws.sendTXT(out);
    Serial.printf("[SYNC] READY ACK: %s bufMs=%d\n", commandId.c_str(), bufferMs);
}

// ── sendTimeSyncRequest ───────────────────────────────────────────────────────
void SyncClient::sendTimeSyncRequest() {
    JsonDocument doc; doc["type"] = "TIME_SYNC"; doc["seq"] = (uint32_t)millis();
    String out; serializeJson(doc, out);
    _notifyActivity();                          // ← kimenő WS küldés
    _ws.sendTXT(out);
}

// ── Segédfüggvények ───────────────────────────────────────────────────────────
int64_t SyncClient::nowMs() const {
    struct timeval tv; gettimeofday(&tv, nullptr);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

bool SyncClient::downloadToLittleFS(const String& url, const String& path) {
    _notifyActivity();                          // ← HTTP letöltés indul
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.setTimeout(10000);
    if (!http.begin(client, url)) return false;
    int code = http.GET();
    if (code != 200) { http.end(); return false; }
    WiFiClient* stream = http.getStreamPtr();
    File file = LittleFS.open(path, "w");
    if (!file) { http.end(); return false; }
    uint8_t buf[512]; size_t total = 0;
    int contentLen = http.getSize(); unsigned long t0 = millis();
    while (http.connected() && (contentLen < 0 || (int)total < contentLen)) {
        size_t avail = stream->available();
        if (avail == 0) { if (millis()-t0>10000) break; vTaskDelay(1); continue; }
        size_t rd = stream->readBytes(buf, min(avail, sizeof(buf)));
        file.write(buf, rd); total += rd; t0 = millis();
    }
    file.close(); http.end();
    Serial.printf("[SYNC] LittleFS: %s (%d bytes)\n", path.c_str(), (int)total);
    return total > 0;
}
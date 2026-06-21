#include "DeviceAgent.h"

void DeviceAgent::begin(
    SLNetworkManager& net,
    AudioManager& audio,
    UIManager& ui,
    BackendClient& backend,
    DeviceTelemetry& tel,
    WsClient& ws,
    BellManager& bells,
    SnapcastClient& snap,
    PersistStore& store
) {
    _net     = &net;
    _audio   = &audio;
    _ui      = &ui;
    _backend = &backend;
    _tel     = &tel;
    _ws      = &ws;
    _bells   = &bells;
    _snap    = &snap;
    _store   = &store;
}

// ── loop ──────────────────────────────────────────────────────────────────────

void DeviceAgent::loop() {
    if (isPlaybackQuietActive()) return;

    maybeClearEmergencyOverride();

    if (_audio && _audio->isInCooldown()) return;

    if (!(_audio && _audio->isBusy())) {
        sendBeaconIfDue();
    }
}

// ── WS üzenet dispatch ────────────────────────────────────────────────────────

void DeviceAgent::onWsMessage(const JsonDocument& msg) {
    String type = msg["type"] | "";
    String phase = msg["phase"] | "";

    if (type == "HELLO") {
        handleHello(msg);
    } else if (phase == "PREPARE") {
        handlePrepare(msg);
    } else if (phase == "PLAY") {
        handlePlay(msg);
    } else if (type == "COMMAND") {
        handleCommand(msg);
    } else if (type == "BEACON_ACK") {
        handleBeaconAck(msg);
    } else if (type == "SCHEDULE_SYNC") {
        if (_bells) _bells->onScheduleSync(msg);
    }
    // TIME_SYNC_RESPONSE, stb. egyelőre ignorálva
}

// ── HELLO ─────────────────────────────────────────────────────────────────────

void DeviceAgent::handleHello(const JsonDocument& msg) {
    _deviceId = msg["deviceId"] | "";

    String snapHost = msg["snapHost"] | "";
    int    snapPort = msg["snapPort"] | 0;

    if (snapHost.length() > 0 && snapPort > 0 && _deviceId.length() > 0) {
        _backend->setSnapConfig(snapHost, (uint16_t)snapPort, _deviceId);
        Serial.printf("[AGENT] HELLO: snapHost=%s port=%d deviceId=%s\n",
                      snapHost.c_str(), snapPort, _deviceId.c_str());
    }

    if (_bells) _bells->setOnlineMode(true);

    Serial.printf("[AGENT] HELLO fogadva – szerver online\n");
}

// ── PREPARE ───────────────────────────────────────────────────────────────────

void DeviceAgent::handlePrepare(const JsonDocument& msg) {
    String commandId = msg["commandId"] | "";
    if (commandId.isEmpty()) return;

    // Ellenőrzés: ez az eszköz szerepel-e az unmutedDeviceIds-ben?
    // Ha az unmutedDeviceIds üres, mindenki lejátszik.
    bool shouldPlay = false;
    JsonArrayConst unmuted = msg["unmutedDeviceIds"].as<JsonArrayConst>();
    if (unmuted.isNull() || unmuted.size() == 0) {
        shouldPlay = true;
    } else {
        for (JsonVariantConst id : unmuted) {
            if (id.as<String>() == _deviceId) { shouldPlay = true; break; }
        }
    }

    String action = msg["action"] | "";
    Serial.printf("[AGENT] PREPARE: cmd=%s action=%s shouldPlay=%d\n",
                  commandId.c_str(), action.c_str(), shouldPlay ? 1 : 0);

    if (shouldPlay && looksLikeAudioCommand(msg.as<JsonVariantConst>(), action)) {
        enterPlaybackQuiet(msg.as<JsonVariantConst>(), action);
    }

    // READY_ACK küldése – a Snapcast buffer ~1000ms
    if (_ws && _ws->isConnected()) {
        JsonDocument ack;
        ack["type"]      = "READY_ACK";
        ack["commandId"] = commandId;
        ack["deviceId"]  = _deviceId;
        ack["bufferMs"]  = 1000;
        char ts[32];
        snprintf(ts, sizeof(ts), "%llu", (unsigned long long)millis());
        ack["readyAt"]   = ts;
        _ws->sendJson(ack);
    }
}

// ── PLAY ──────────────────────────────────────────────────────────────────────

void DeviceAgent::handlePlay(const JsonDocument& msg) {
    // Snapcast maga kezeli az időzítést; nekünk nincs teendőnk.
    // A playback quiet mód már a PREPARE-kor be lett állítva.
    Serial.printf("[AGENT] PLAY: cmd=%s playAt=%s\n",
                  (msg["commandId"] | ""), (msg["playAt"] | ""));
}

// ── COMMAND (WS-push) ────────────────────────────────────────────────────────

void DeviceAgent::handleCommand(const JsonDocument& msg) {
    String commandId = msg["commandId"] | "";
    if (commandId.isEmpty()) return;

    JsonVariantConst payload = msg["payload"].as<JsonVariantConst>();
    String action = detectAction(payload);
    Serial.printf("[AGENT] COMMAND push: id=%s action=%s\n",
                  commandId.c_str(), action.c_str());

    executeCommand(commandId, payload);
}

// ── BEACON_ACK ────────────────────────────────────────────────────────────────

void DeviceAgent::handleBeaconAck(const JsonDocument& msg) {
    String snapHost = msg["snapHost"] | "";
    int    snapPort = msg["snapPort"] | 0;
    if (snapHost.length() > 0 && snapPort > 0 && _deviceId.length() > 0) {
        _backend->setSnapConfig(snapHost, (uint16_t)snapPort, _deviceId);
    }
}

// ── executeCommand ────────────────────────────────────────────────────────────

bool DeviceAgent::executeCommand(const String& commandId, JsonVariantConst payload) {
    String action = detectAction(payload);

    if (looksLikeAudioCommand(payload, action)) {
        enterPlaybackQuiet(payload, action.length() > 0 ? action : "AUDIO");

        if (_ws && _ws->isConnected()) {
            JsonDocument ack;
            ack["type"]      = "CMD_ACK";
            ack["commandId"] = commandId;
            ack["ok"]        = true;
            _ws->sendJson(ack);
        }
        return true;
    }

    String err;
    bool ok = false;

    if (action == "SET_VOLUME") {
        ok = handleSetVolume(payload, err);
    } else if (action == "SHOW_MESSAGE") {
        ok = handleShowMessage(payload, err);
    } else if (action == "SYNC_BELLS") {
        ok = true;
    } else if (action == "SET_CHANNEL_MODE") {
        ok = handleSetChannelMode(payload, err);
    } else {
        err = "Unknown action: " + action;
    }

    if (_ws && _ws->isConnected()) {
        JsonDocument ack;
        ack["type"]      = "CMD_ACK";
        ack["commandId"] = commandId;
        ack["ok"]        = ok;
        if (!ok) ack["error"] = err;
        _ws->sendJson(ack);
    }

    return ok;
}

// ── Beacon küldése WS-en ──────────────────────────────────────────────────────

void DeviceAgent::sendBeaconIfDue() {
    if (!_ws || !_ws->isConnected()) return;

    unsigned long now = millis();
    if ((now - _lastBeaconMs) < BEACON_INTERVAL_MS) return;
    _lastBeaconMs = now;

    JsonDocument beacon;
    beacon["type"]            = "BEACON";
    beacon["volume"]          = _audio ? _audio->getVolume() : 50;
    beacon["muted"]           = _audio ? _audio->isMuted()   : false;
    beacon["firmwareVersion"] = _fw;

    if (_tel) {
        JsonDocument statusDoc;
        _tel->fillJson(statusDoc);
        beacon["statusPayload"] = statusDoc;
    }

    bool ok = _ws->sendJson(beacon);
    if (_tel) {
        if (ok) { _tel->beaconOk++; _tel->markServerOk(); }
        else    { _tel->beaconErr++; _tel->markServerErr("beacon ws failed"); }
    }
}

// ── isPlaybackQuietActive / playbackQuietRemainingMs ─────────────────────────

bool DeviceAgent::isPlaybackQuietActive() const {
    if (_playbackQuietUntilMs == 0) return false;
    return (long)(_playbackQuietUntilMs - millis()) > 0;
}

unsigned long DeviceAgent::playbackQuietRemainingMs() const {
    if (!isPlaybackQuietActive()) return 0;
    return _playbackQuietUntilMs - millis();
}

// ── Segédfüggvények ───────────────────────────────────────────────────────────

String DeviceAgent::detectAction(JsonVariantConst payload) const {
    String action = payload["action"] | "";
    if (action.length() > 0) return action;
    if (payload["url"].is<const char*>()) return "PLAY_URL";
    if (payload["text"].is<const char*>()) return "TTS";
    return "";
}

bool DeviceAgent::looksLikeAudioCommand(JsonVariantConst payload, const String& action) const {
    if (action == "PLAY_URL" || action == "TTS" || action == "BELL" ||
        action == "PLAY_AUDIO" || action == "MIC_AUDIO" || action == "VOICE_MESSAGE") {
        return true;
    }
    if (payload["url"].is<const char*>()) {
        String url = payload["url"].as<const char*>();
        if (url.endsWith(".wav") || url.endsWith(".mp3") || url.indexOf("/audio/") >= 0) return true;
    }
    if (payload["text"].is<const char*>()) {
        String text = payload["text"].as<const char*>();
        if (text.length() > 0) return true;
    }
    return false;
}

bool DeviceAgent::handleSetChannelMode(JsonVariantConst payload, String& err) {
    String mode = payload["mode"] | "";
    mode.toUpperCase();
    dsp_channel_mode_t m;
    if      (mode == "LEFT")  m = DSP_CHANNEL_LEFT;
    else if (mode == "RIGHT") m = DSP_CHANNEL_RIGHT;
    else if (mode == "MIXED") m = DSP_CHANNEL_MIXED;
    else { err = "Invalid mode: " + mode; return false; }

    if (_snap) _snap->setChannelMode(m);
    if (_store) _store->setChannelMode(m);
    Serial.printf("[AGENT] SET_CHANNEL_MODE: %s\n", mode.c_str());
    return true;
}

bool DeviceAgent::handleSetVolume(JsonVariantConst payload, String& err) {
    if (!payload["volume"].is<int>()) { err = "No volume"; return false; }
    int vol = payload["volume"].as<int>();
    if (_audio) _audio->setVolume(vol);
    if (_ui)    _ui->showVolumeScreen();
    return true;
}

bool DeviceAgent::handleShowMessage(JsonVariantConst payload, String& err) {
    String msg = payload["message"] | "";
    if (msg.isEmpty()) { err = "No message"; return false; }
    Serial.printf("[AGENT] SHOW_MESSAGE: %s\n", msg.c_str());
    return true;
}

unsigned long DeviceAgent::getPlaybackQuietMs(JsonVariantConst payload) const {
    long durationMs = 0;
    if      (payload["durationMs"].is<long>())          durationMs = payload["durationMs"].as<long>();
    else if (payload["duration"].is<long>())             durationMs = payload["duration"].as<long>();
    else if (payload["estimatedDurationMs"].is<long>())  durationMs = payload["estimatedDurationMs"].as<long>();

    if (durationMs <= 0) return DEFAULT_PLAYBACK_QUIET_MS;

    unsigned long quietMs = (unsigned long)durationMs + PLAYBACK_SAFETY_MS;
    if (quietMs < 20000UL)  quietMs = 20000UL;
    if (quietMs > 300000UL) quietMs = 300000UL;
    return quietMs;
}

void DeviceAgent::enterPlaybackQuiet(JsonVariantConst payload, const String& action) {
    unsigned long quietMs = getPlaybackQuietMs(payload);
    _playbackQuietUntilMs = millis() + quietMs;

    String kind = payload["kind"] | "";
    if (kind.length() == 0) kind = payload["source"] | "";
    _currentPlaybackAction = kind.length() > 0 ? kind : action;

    _lastBeaconMs = millis();

    bool emergency = payload["emergency"].is<bool>() && payload["emergency"].as<bool>();
    if (emergency && _audio) {
        _audio->setVolumeOverride(10);
        _emergencyOverrideActive = true;
        _currentPlaybackAction = "EMERGENCY";   // felülírja a kind/action értéket
        Serial.println("[AGENT] EMERGENCY: volume override -> 10");
    }

    Serial.printf("[AGENT] Playback quiet: action=%s quietMs=%lu\n",
                  action.c_str(), quietMs);
}

void DeviceAgent::maybeClearEmergencyOverride() {
    if (!_emergencyOverrideActive) return;
    if (_audio) _audio->clearVolumeOverride();
    _emergencyOverrideActive = false;
    Serial.println("[AGENT] EMERGENCY override törölve");
}

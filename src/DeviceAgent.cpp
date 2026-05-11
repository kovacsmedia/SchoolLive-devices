#include "DeviceAgent.h"

void DeviceAgent::begin(
    SLNetworkManager& net,
    AudioManager& audio,
    UIManager& ui,
    BackendClient& backend,
    DeviceTelemetry& tel
) {
    _net = &net;
    _audio = &audio;
    _ui = &ui;
    _backend = &backend;
    _tel = &tel;
}

void DeviceAgent::loop() {
    /*
     * Lejátszás közben semmilyen HTTP aktivitás nem futhat.
     * Ez védi a Snapcast TCP streamet és az I2S audio taskot.
     */
    if (isPlaybackQuietActive()) {
        return;
    }

    // Ha a quiet mode lejárt és vészhelyzeti override volt aktív, töröljük.
    // Itt biztos vagyunk, hogy a lejátszás is befejeződött (a quiet mode a
    // payload duration + safety margin), tehát ekkor visszaadhatjuk a
    // hangerő-vezérlést a manuálisnak.
    maybeClearEmergencyOverride();

    if (_audio && _audio->isInCooldown()) {
        return;
    }

    bool audioBusy = _audio && _audio->isBusy();

    if (!audioBusy) {
        sendBeaconIfDue();
        pollIfDue();
    }
}

bool DeviceAgent::isPlaybackQuietActive() const {
    if (_playbackQuietUntilMs == 0) return false;

    long remaining = (long)(_playbackQuietUntilMs - millis());

    return remaining > 0;
}

unsigned long DeviceAgent::playbackQuietRemainingMs() const {
    if (!isPlaybackQuietActive()) return 0;

    return _playbackQuietUntilMs - millis();
}

void DeviceAgent::sendBeaconIfDue() {
    unsigned long now = millis();

    if ((now - _lastBeaconMs) < BEACON_INTERVAL_MS) return;

    _lastBeaconMs = now;

    if (!_net || !_net->isConnected()) return;

    JsonDocument status;

    if (_tel) {
        _tel->fillJson(status);
    }

    _backend->sendBeacon(
        _audio ? _audio->getVolume() : 50,
        _audio ? _audio->isMuted() : false,
        _fw,
        status
    );
}

void DeviceAgent::pollIfDue() {
    unsigned long now = millis();

    if ((now - _lastPollMs) < POLL_INTERVAL_MS) return;

    _lastPollMs = now;

    if (!_net || !_net->isConnected()) return;

    PolledCommand cmd;

    if (!_backend->poll(cmd)) return;
    if (!cmd.hasCommand) return;

    executeAndAck(cmd);
}

bool DeviceAgent::executeAndAck(const PolledCommand& cmd) {
    JsonVariantConst payload = cmd.payload;

    String action = detectAction(payload);

    Serial.printf(
        "[AGENT] Command received: id=%s action=%s hasUrl=%d hasText=%d hasDuration=%d\n",
        cmd.id.c_str(),
        action.c_str(),
        payload["url"].is<const char*>() ? 1 : 0,
        payload["text"].is<const char*>() ? 1 : 0,
        payload["durationMs"].is<long>() ? 1 : 0
    );

    /*
     * Kritikus:
     * Ha ez hangos parancs, ELŐBB lépünk playback quiet módba,
     * és csak utána ACK-olunk.
     *
     * Így az ACK HTTP hívás után már nem indulhat újabb poll/beacon/bell sync.
     */
    if (looksLikeAudioCommand(payload, action)) {
        enterPlaybackQuiet(payload, action.length() > 0 ? action : "SNAP_AUDIO");

        Serial.println("[AGENT] Snapcast audio command acknowledged");

        bool ackOk = _backend->ack(cmd.id, true, "Snapcast audio command accepted");

        return ackOk;
    }

    String err;
    bool ok = false;

    if (action == "SET_VOLUME") {
        ok = handleSetVolume(payload, err);
    } else if (action == "SHOW_MESSAGE") {
        ok = handleShowMessage(payload, err);
    } else {
        err = "Unknown action: " + action;
        ok = false;
    }

    bool ackOk = _backend->ack(cmd.id, ok, err);

    return ackOk && ok;
}

String DeviceAgent::detectAction(JsonVariantConst payload) const {
    String action = payload["action"] | "";

    if (action.length() > 0) {
        return action;
    }

    /*
     * Régi polling kompatibilitás:
     * a backend sok esetben csak url/text mezőket küld explicit action nélkül.
     */
    if (payload["url"].is<const char*>()) {
        return "PLAY_URL";
    }

    if (payload["text"].is<const char*>()) {
        return "TTS";
    }

    return "";
}

bool DeviceAgent::looksLikeAudioCommand(
    JsonVariantConst payload,
    const String& action
) const {
    if (
        action == "PLAY_URL" ||
        action == "TTS" ||
        action == "BELL" ||
        action == "PLAY_AUDIO" ||
        action == "MIC_AUDIO" ||
        action == "VOICE_MESSAGE"
    ) {
        return true;
    }

    /*
     * Action nélküli hangos parancs fallback.
     */
    if (payload["url"].is<const char*>()) {
        String url = payload["url"].as<const char*>();

        if (
            url.endsWith(".wav") ||
            url.endsWith(".mp3") ||
            url.indexOf("/audio/") >= 0
        ) {
            return true;
        }
    }

    if (payload["text"].is<const char*>()) {
        String text = payload["text"].as<const char*>();

        if (text.length() > 0) {
            return true;
        }
    }

    return false;
}

bool DeviceAgent::handleSetVolume(JsonVariantConst payload, String& err) {
    if (!payload["volume"].is<int>()) {
        err = "No volume";
        return false;
    }

    int vol = payload["volume"].as<int>();

    if (_audio) {
        _audio->setVolume(vol);
    }

    return true;
}

bool DeviceAgent::handleShowMessage(JsonVariantConst payload, String& err) {
    String msg = payload["message"] | "";

    if (msg.isEmpty()) {
        err = "No message";
        return false;
    }

    Serial.printf("[AGENT] SHOW_MESSAGE: %s\n", msg.c_str());

    return true;
}

unsigned long DeviceAgent::getPlaybackQuietMs(JsonVariantConst payload) const {
    long durationMs = 0;

    if (payload["durationMs"].is<long>()) {
        durationMs = payload["durationMs"].as<long>();
    } else if (payload["duration"].is<long>()) {
        durationMs = payload["duration"].as<long>();
    } else if (payload["estimatedDurationMs"].is<long>()) {
        durationMs = payload["estimatedDurationMs"].as<long>();
    }

    if (durationMs <= 0) {
        return DEFAULT_PLAYBACK_QUIET_MS;
    }

    unsigned long quietMs = (unsigned long)durationMs + PLAYBACK_SAFETY_MS;

    if (quietMs < 20000UL) quietMs = 20000UL;
    if (quietMs > 300000UL) quietMs = 300000UL;

    return quietMs;
}

void DeviceAgent::enterPlaybackQuiet(JsonVariantConst payload, const String& action) {
    unsigned long quietMs = getPlaybackQuietMs(payload);

    _playbackQuietUntilMs = millis() + quietMs;

    /*
     * Azonnal eltoljuk a következő poll/beacon időpontokat,
     * hogy a quiet mód lejárta után se induljanak egyszerre azonnal.
     */
    _lastPollMs = millis();
    _lastBeaconMs = millis();

    /*
     * Vészhelyzeti hangerő-override:
     * a backend `emergency: true` flagjére az AudioManager-ben max hangerőt
     * forszírozunk a teljes quiet mode idejére. A callback a Snapcast
     * streamre is azonnal érvényesíti.
     * A manuális hangerő (currentVolume) változatlan marad - a felhasználó
     * tovább nyomhatja a gombokat, csak nem érvényesül a riasztás alatt.
     */
    bool emergency = payload["emergency"].is<bool>() && payload["emergency"].as<bool>();

    if (emergency && _audio) {
        _audio->setVolumeOverride(10);
        _emergencyOverrideActive = true;
        Serial.println("[AGENT] EMERGENCY flag set: volume override -> 10");
    }

    Serial.printf(
        "[AGENT] Playback quiet mode: action=%s quietMs=%lu emergency=%d\n",
        action.c_str(),
        quietMs,
        emergency ? 1 : 0
    );
}

void DeviceAgent::maybeClearEmergencyOverride() {
    if (!_emergencyOverrideActive) return;

    // Csak akkor töröljük, ha a quiet mode lejárt - a quiet mode aktív
    // állapotot a fenti `if (isPlaybackQuietActive()) return;` szűrte.
    if (_audio) {
        _audio->clearVolumeOverride();
    }
    _emergencyOverrideActive = false;
    Serial.println("[AGENT] EMERGENCY override cleared after playback quiet expired");
}
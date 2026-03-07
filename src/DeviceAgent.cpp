#include "DeviceAgent.h"

void DeviceAgent::begin(NetworkManager& net, AudioManager& audio,
                        UIManager& ui, BackendClient& backend,
                        DeviceTelemetry& tel) {
    _net     = &net;
    _audio   = &audio;
    _ui      = &ui;
    _backend = &backend;
    _tel     = &tel;
}

void DeviceAgent::loop() {
    // 1. Cooldown alatt (audio EOF után) – ne nyissunk HTTP kapcsolatot
    if (_audio && _audio->isInCooldown()) return;

    // 2. Lejátszás közben csak beacon mehet, poll nem
    bool playing = _audio && _audio->isPlaying();

    // 3. Beacon
    sendBeaconIfDue();

    // 4. Poll (csak ha nem játszik és nincs cooldown)
    if (!playing) {
        pollIfDue();
    }
}

void DeviceAgent::sendBeaconIfDue() {
    unsigned long now = millis();
    if ((now - _lastBeaconMs) < BEACON_INTERVAL_MS) return;
    _lastBeaconMs = now;

    if (!_net || !_net->isConnected()) return;

    JsonDocument status;
    if (_tel) _tel->fillJson(status);

    _backend->sendBeacon(
        _audio ? _audio->getVolume() : 50,
        _audio ? _audio->isMuted()   : false,
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
    if (!cmd.hasCommand)      return;

    executeAndAck(cmd);
}

bool DeviceAgent::executeAndAck(const PolledCommand& cmd) {
    String action = cmd.payload["action"] | "";
    JsonVariantConst payload = cmd.payload["payload"];

    String err;
    bool ok = false;

    if (action == "PLAY_URL") {
        // ACK előre – az audio SSL socket megnyitása előtt
        _backend->ack(cmd.id, true, "");
        ok = handlePlayUrl(payload, err);
        return ok;
    }

    if      (action == "SET_VOLUME")   ok = handleSetVolume(payload, err);
    else if (action == "SHOW_MESSAGE") ok = handleShowMessage(payload, err);
    else { err = "Unknown action: " + action; ok = false; }

    _backend->ack(cmd.id, ok, err);
    return ok;
}

bool DeviceAgent::handlePlayUrl(JsonVariantConst payload, String& err) {
    String url = payload["url"] | "";
    if (url.isEmpty()) { err = "No URL"; return false; }
    Serial.printf("[AGENT] Playing URL: %s\n", url.c_str());
    _audio->playUrl(url.c_str());   // FIX: String → const char*
    return true;
}

bool DeviceAgent::handleSetVolume(JsonVariantConst payload, String& err) {
    if (!payload["volume"].is<int>()) { err = "No volume"; return false; }
    int vol = payload["volume"].as<int>();
    _audio->setVolume(vol);
    return true;
}

bool DeviceAgent::handleShowMessage(JsonVariantConst payload, String& err) {
    String msg = payload["message"] | "";
    if (msg.isEmpty()) { err = "No message"; return false; }
    // UIManager::showMessage() jelenleg nem létezik
    // Ha szükséges, add hozzá: void showMessage(const String& msg);
    return true;
}
// ─────────────────────────────────────────────────────────────────────────────
// DeviceAgent.cpp – SchoolLive S3.54
// Változások:
//   • Első beacon AZONNAL megy (nem vár 30s-ot):
//     _lastBeaconMs = -(BEACON_INTERVAL_MS) inicializáció helyett
//     _firstBeacon flag – az első loop() hívásban azonnal küld, ha WiFi OK
//   • beacon ok/fail → _ui->setBackendOnline(true/false)
//   • poll fail → _ui->setBackendOnline(false)
// ─────────────────────────────────────────────────────────────────────────────
#include "DeviceAgent.h"
#include <LittleFS.h>

void DeviceAgent::begin(NetworkManager& net, AudioManager& audio,
                        UIManager& ui, BackendClient& backend,
                        DeviceTelemetry& tel) {
    _net     = &net;
    _audio   = &audio;
    _ui      = &ui;
    _backend = &backend;
    _tel     = &tel;
    // _lastBeaconMs = 0 alapértelmezés → az első híváskor
    // (now - 0) >= BEACON_INTERVAL_MS csak ~30s után lenne igaz.
    // Ezért _firstBeacon = true flaggel kezeljük az azonnali első küldést.
    _firstBeacon = true;
}

void DeviceAgent::loop() {
    sendBeaconIfDue();
    pollIfDue();
}

// ── Beacon ────────────────────────────────────────────────────────────────────
void DeviceAgent::sendBeaconIfDue() {
    unsigned long now = millis();

    // Első beacon: azonnal, amint WiFi elérhető
    if (_firstBeacon) {
        if (!_net || !_net->isConnected()) return;  // vár WiFi-ra, de nem blokkolja
        _firstBeacon  = false;
        _lastBeaconMs = now;
        _sendBeacon();
        return;
    }

    if ((now - _lastBeaconMs) < BEACON_INTERVAL_MS) return;
    _lastBeaconMs = now;
    if (!_net || !_net->isConnected()) return;
    _sendBeacon();
}

void DeviceAgent::_sendBeacon() {
    JsonDocument status;
    if (_tel) _tel->fillJson(status);

    bool ok = _backend->sendBeacon(
        _audio ? _audio->getVolume() : 5,
        _audio ? _audio->isMuted()   : false,
        _fw, status
    );

    if (_ui) _ui->setBackendOnline(ok);

    if (_tel) {
        if (ok) { _tel->beaconOk++;  _tel->markServerOk(); }
        else    { _tel->beaconErr++; _tel->markServerErr("beacon failed"); }
    }

    Serial.printf("[AGENT] Beacon: %s\n", ok ? "OK" : "FAIL");
}

// ── Poll (fallback ha WebSocket nem elérhető) ─────────────────────────────────
void DeviceAgent::pollIfDue() {
    unsigned long now = millis();
    if ((now - _lastPollMs) < POLL_INTERVAL_MS) return;
    _lastPollMs = now;
    if (!_net || !_net->isConnected()) return;

    PolledCommand cmd;
    bool ok = _backend->poll(cmd);

    if (!ok && _ui) _ui->setBackendOnline(false);

    if (_tel) {
        if (ok) _tel->pollOk++;
        else  { _tel->pollErr++; return; }
    }
    if (!cmd.hasCommand) return;
    executeAndAck(cmd);
}

// ── executeAndAck ─────────────────────────────────────────────────────────────
bool DeviceAgent::executeAndAck(const PolledCommand& cmd) {
    String action = cmd.payload["action"] | "";
    String url    = cmd.payload["url"]    | "";
    String text   = cmd.payload["text"]   | "";
    String title  = cmd.payload["title"]  | "";

    String err;
    bool ok = executeCommand(cmd.id, action, url, text, title);
    if (action != "PLAY_URL" && action != "TTS") {
        bool ackOk = _backend->ack(cmd.id, ok, err);
        if (_tel) { if (ackOk) _tel->ackOk++; else _tel->ackErr++; }
    }
    return ok;
}

// ── executeCommand ────────────────────────────────────────────────────────────
bool DeviceAgent::executeCommand(const String& commandId,
                                  const String& action,
                                  const String& url,
                                  const String& text,
                                  const String& title) {
    String err;
    Serial.printf("[AGENT] Parancs: %s url=%.60s\n", action.c_str(), url.c_str());

    if (action == "PLAY_URL") {
        _backend->ack(commandId, true, "");
        return handlePlayUrl(JsonVariant(), err);
    }

    if      (action == "BELL")          return handleBell(JsonVariant(), err);
    else if (action == "TTS")           return handleTts(JsonVariant(), err);
    else if (action == "STOP_PLAYBACK") return handleStop(err);
    else if (action == "SET_VOLUME")    return false;
    else if (action == "SHOW_MESSAGE")  return false;
    else if (action == "SYNC_BELLS")    return true;
    else {
        Serial.printf("[AGENT] Ismeretlen akció: %s\n", action.c_str());
        return false;
    }
}

bool DeviceAgent::handlePlayUrl(JsonVariantConst p, String& err) {
    String url = p["url"] | "";
    if (url.isEmpty()) { err = "No URL"; return false; }
    _audio->playUrl(url.c_str());
    return true;
}

bool DeviceAgent::handleBell(JsonVariantConst p, String& err) {
    String url = p["url"] | "";
    if (url.isEmpty()) { err = "No URL"; return false; }
    String filename = url.substring(url.lastIndexOf('/') + 1);
    String path = filename.startsWith("/") ? filename : "/" + filename;
    if (LittleFS.exists(path)) {
        _audio->playFile(path.c_str());
    } else {
        _audio->playUrl(url.c_str());
    }
    return true;
}

bool DeviceAgent::handleTts(JsonVariantConst p, String& err) {
    String url = p["url"] | "";
    if (url.isEmpty()) { err = "No URL"; return false; }
    _backend->ack(p["id"] | "", true, "");
    _audio->playUrl(url.c_str());
    return true;
}

bool DeviceAgent::handleStop(String& err) {
    if (_audio) _audio->stop();
    return true;
}

bool DeviceAgent::handleSetVolume(JsonVariantConst p, String& err) {
    if (!p["volume"].is<int>()) { err = "No volume"; return false; }
    _audio->setVolume(p["volume"].as<int>());
    return true;
}

bool DeviceAgent::handleShowMessage(JsonVariantConst p, String& err) {
    return true;
}
#include "DeviceAgent.h"

void DeviceAgent::begin(NetworkManager& net,
                        AudioManager& audio,
                        UIManager& ui,
                        BackendClient& backend,
                        DeviceTelemetry& tel) {
  _net = &net;
  _audio = &audio;
  _ui = &ui;
  _backend = &backend;
  _tel = &tel;

  _lastBeaconMs = 0;
  _lastPollMs = 0;
}

void DeviceAgent::loop() {
  if (!_net || !_audio || !_ui || !_backend || !_tel) return;

  _tel->wifiConnected = _net->isConnected();
  _tel->ip = _net->getIP();
  _tel->rssi = _net->getRSSI();
  _tel->timeSynced = _net->isTimeSynced();

  if (!_net->isConnected()) return;
  if (!_backend->isReady()) return;

  sendBeaconIfDue();
  pollIfDue();
}

void DeviceAgent::sendBeaconIfDue() {
  // Ne küldjünk beacon-t amíg stream lejátszás folyik (heap védelem)
  if (_audio->isPlaying() && _audio->isStreamMode()) return;

  const unsigned long now = millis();
  if (_lastBeaconMs != 0 && (now - _lastBeaconMs) < BEACON_INTERVAL_MS) return;

  JsonDocument status;
  status["ip"] = _net->getIP();
  status["rssi"] = _net->getRSSI();
  status["timeSynced"] = _net->isTimeSynced();

  bool ok = _backend->sendBeacon(_audio->getVolume(), false, _fw, status);

  if (ok) {
    _tel->beaconOk++;
    _tel->markServerOk();
    _lastBeaconMs = now;
  } else {
    _tel->beaconErr++;
    _tel->markServerErr("beacon failed");
  }
}

void DeviceAgent::pollIfDue() {
  // Ne pollozzunk amíg stream lejátszás folyik (heap védelem)
  if (_audio->isPlaying() && _audio->isStreamMode()) return;

  const unsigned long now = millis();
  if (_lastPollMs != 0 && (now - _lastPollMs) < POLL_INTERVAL_MS) return;

  PolledCommand cmd;
  bool ok = _backend->poll(cmd);

  _lastPollMs = now;

  if (!ok) {
    _tel->pollErr++;
    _tel->markServerErr("poll failed");
    return;
  }

  _tel->pollOk++;
  _tel->markServerOk();

  if (!cmd.hasCommand) return;

  executeAndAck(cmd);
}

bool DeviceAgent::executeAndAck(const PolledCommand& cmd) {
  JsonVariantConst p = cmd.payload.as<JsonVariantConst>();

  String err;
  bool ok = false;

  String action = "";
  if (p["action"].is<const char*>()) action = p["action"].as<String>();
  else if (p["type"].is<const char*>()) action = p["type"].as<String>();

  Serial.printf("[AGENT] action: %s\n", action.c_str());

  if (action == "PLAY_URL") {
    ok = handlePlayUrl(p, err);
  } else if (action == "SET_VOLUME") {
    ok = handleSetVolume(p, err);
  } else if (action == "SHOW_MESSAGE") {
    ok = handleShowMessage(p, err);
  } else {
    ok = false;
    err = "Unknown action: " + action;
    Serial.printf("[AGENT] Unknown action: %s\n", action.c_str());
  }

  _tel->lastCommandId = cmd.id;
  _tel->lastCommandOk = ok;

  bool ackOk = _backend->ack(cmd.id, ok, err);
  if (ackOk) { _tel->ackOk++; _tel->markServerOk(); }
  else        { _tel->ackErr++; _tel->markServerErr("ack failed"); }

  return ok;
}

bool DeviceAgent::handlePlayUrl(JsonVariantConst payload, String& err) {
  if (!payload["url"].is<const char*>()) {
    err = "Missing url";
    return false;
  }

  String url = payload["url"].as<String>();
  if (url.length() == 0) {
    err = "Empty url";
    return false;
  }

  if (payload["scheduledAt"].is<const char*>()) {
    String scheduledAt = payload["scheduledAt"].as<String>();
    if (scheduledAt.length() > 0 && scheduledAt != "null") {
      Serial.printf("[AGENT] Scheduled at: %s (playing immediately)\n", scheduledAt.c_str());
    }
  }

  Serial.printf("[AGENT] PLAY_URL: %s\n", url.c_str());
  _audio->playUrl(url.c_str());
  return true;
}

bool DeviceAgent::handleSetVolume(JsonVariantConst payload, String& err) {
  if (!payload["volume"].is<int>()) {
    err = "Missing/invalid volume";
    return false;
  }

  int v = payload["volume"].as<int>();
  if (v < 0) v = 0;
  if (v > 21) v = 21;

  _audio->setVolume((uint8_t)v);
  return true;
}

bool DeviceAgent::handleShowMessage(JsonVariantConst payload, String& err) {
  String title = payload["title"].is<const char*>() ? payload["title"].as<String>() : String("MESSAGE");
  String text  = payload["text"].is<const char*>()  ? payload["text"].as<String>()  : String("");

  if (title.length() == 0 && text.length() == 0) {
    err = "Missing title/text";
    return false;
  }

  _ui->drawBootStatus(title, text);
  return true;
}
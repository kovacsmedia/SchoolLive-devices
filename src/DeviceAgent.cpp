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

  // --- WiFi státusz frissítése (UI-nak is jó) ---
  _tel->wifiConnected = _net->isConnected();
  _tel->ip = _net->getIP();
  _tel->rssi = _net->getRSSI();
  _tel->timeSynced = _net->isTimeSynced();

  // Ha nincs WiFi, itt vége (offline üzemet a BellManager intézi)
  if (!_net->isConnected()) return;

  // Ha nincs deviceKey / baseUrl, nem tudunk szerverrel beszélni
  if (!_backend->isReady()) return;

  sendBeaconIfDue();
  pollIfDue();
}

void DeviceAgent::sendBeaconIfDue() {
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
    // _lastBeaconMs-t nem léptetjük, így retry hamarabb történik (következő loopokban)
  }
}

void DeviceAgent::pollIfDue() {
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
  JsonVariantConst p = cmd.payload["payload"];

  String err;
  bool ok = false;

  String type = p["type"].is<const char*>() ? p["type"].as<String>() : String("");

  if (type == "SET_VOLUME") {
    ok = handleSetVolume(p, err);
  } else if (type == "SHOW_MESSAGE") {
    ok = handleShowMessage(p, err);
  } else {
    ok = false;
    err = "Unknown command type: " + type;
  }

  // Telemetria: last command
  _tel->lastCommandId = cmd.id;
  _tel->lastCommandOk = ok;

  // ACK (és annak sikeressége külön számláló)
  bool ackOk = _backend->ack(cmd.id, ok, err);
  if (ackOk) {
    _tel->ackOk++;
    _tel->markServerOk();
  } else {
    _tel->ackErr++;
    _tel->markServerErr("ack failed");
  }

  return ok;
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

  // Kijelzés a meglévő UI metódussal
  _ui->drawBootStatus(title, text);
  return true;
}
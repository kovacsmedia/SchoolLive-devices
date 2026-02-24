#include "DeviceAgent.h"

void DeviceAgent::begin(NetworkManager& net, AudioManager& audio, BackendClient& backend) {
  _net = &net;
  _audio = &audio;
  _backend = &backend;
  _lastBeaconMs = 0;
  _lastPollMs = 0;
}

void DeviceAgent::loop() {
  if (!_net || !_audio || !_backend) return;
  if (!_net->isConnected()) return;
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
  if (ok) _lastBeaconMs = now;
}

void DeviceAgent::pollIfDue() {
  const unsigned long now = millis();
  if (_lastPollMs != 0 && (now - _lastPollMs) < POLL_INTERVAL_MS) return;

  PolledCommand cmd;
  bool ok = _backend->poll(cmd);
  _lastPollMs = now;

  if (!ok) return;
  if (!cmd.hasCommand) return;

  executeAndAck(cmd);
}

bool DeviceAgent::executeAndAck(const PolledCommand& cmd) {
  // Backend payload: tetszőleges JSON object.
  // Mi elvárjuk: payload.type és payload.volume (SET_VOLUME esetén)
  JsonVariantConst p = cmd.payload["payload"];
  String err;

  bool ok = false;
  String type = p["type"].as<String>();
  if (type == "SET_VOLUME") {
    ok = handleSetVolume(p, err);
  } else {
    ok = false;
    err = "Unknown command type: " + type;
  }

  _backend->ack(cmd.id, ok, err);
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
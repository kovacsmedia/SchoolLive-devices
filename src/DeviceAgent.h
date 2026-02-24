#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "AudioManager.h"
#include "NetworkManager.h"
#include "BackendClient.h"

class DeviceAgent {
public:
  void begin(NetworkManager& net, AudioManager& audio, BackendClient& backend);
  void loop();

  void setFirmwareVersion(const String& v) { _fw = v; }

private:
  NetworkManager* _net = nullptr;
  AudioManager* _audio = nullptr;
  BackendClient* _backend = nullptr;

  String _fw = "dev";

  unsigned long _lastBeaconMs = 0;
  unsigned long _lastPollMs = 0;

  // finomhangolható (később backend küldi)
  const unsigned long BEACON_INTERVAL_MS = 30000;
  const unsigned long POLL_INTERVAL_MS   = 1500;

  void sendBeaconIfDue();
  void pollIfDue();
  bool executeAndAck(const PolledCommand& cmd);

  // parancs parser
  bool handleSetVolume(JsonVariantConst payload, String& err);
};
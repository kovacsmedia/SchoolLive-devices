#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "DeviceTelemetry.h"
#include "AudioManager.h"
#include "NetworkManager.h"
#include "BackendClient.h"
#include "UIManager.h"

class DeviceAgent {
public:
  void loop();
  DeviceTelemetry* _tel = nullptr;
  void begin(NetworkManager& net, AudioManager& audio, UIManager& ui, BackendClient& backend, DeviceTelemetry& tel);
  void setFirmwareVersion(const String& v) { _fw = v; }

private:
  NetworkManager* _net = nullptr;
  AudioManager* _audio = nullptr;
  UIManager* _ui = nullptr;
  BackendClient* _backend = nullptr;

  String _fw = "dev";

  unsigned long _lastBeaconMs = 0;
  unsigned long _lastPollMs = 0;

  const unsigned long BEACON_INTERVAL_MS = 30000;
  const unsigned long POLL_INTERVAL_MS   = 1500;

  bool _pendingAck = false;
  String _pendingAckId;
  bool _pendingAckOk = false;
  String _pendingAckErr;
  unsigned long _ackReadyMs = 0;
  const unsigned long ACK_DELAY_MS = 3000;

  void sendBeaconIfDue();
  void pollIfDue();
  bool executeAndAck(const PolledCommand& cmd);

  bool handlePlayUrl(JsonVariantConst payload, String& err);
  bool handleSetVolume(JsonVariantConst payload, String& err);
  bool handleShowMessage(JsonVariantConst payload, String& err);
};
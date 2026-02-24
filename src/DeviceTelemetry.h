#pragma once
#include <Arduino.h>

struct DeviceTelemetry {
  // Identity
  String deviceId;        // pl. ESP32 MAC
  String firmwareVersion; // "3.5"

  // WiFi
  bool wifiConnected = false;
  String ip = "";
  int rssi = 0;
  bool timeSynced = false;

  // Server “online” (kommunikáció alapján)
  bool serverReachable = false;       // utolsó poll/beacon siker alapján
  unsigned long lastServerOkMs = 0;   // millis()

  // Error counters
  uint32_t beaconOk = 0, beaconErr = 0;
  uint32_t pollOk = 0, pollErr = 0;
  uint32_t ackOk = 0, ackErr = 0;

  // Last error
  String lastError = "";

  // Last command info
  String lastCommandId = "";
  bool lastCommandOk = true;

  // helpers
  void markServerOk() {
    serverReachable = true;
    lastServerOkMs = millis();
    lastError = "";
  }

  void markServerErr(const String& err) {
    // serverReachable-t nem feltétlen rögtön dobjuk false-ra,
    // inkább “stale” logika lesz a UI-ban (pl. 60s)
    lastError = err;
  }
};
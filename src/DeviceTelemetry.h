#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

struct DeviceTelemetry {
  // Identity
  String deviceId;
  String firmwareVersion;

  // WiFi
  bool   wifiConnected = false;
  String ip            = "";
  int    rssi          = 0;
  bool   timeSynced    = false;

  // Server "online" (kommunikáció alapján)
  bool          serverReachable = false;
  unsigned long lastServerOkMs  = 0;

  // Error counters
  uint32_t beaconOk  = 0, beaconErr  = 0;
  uint32_t pollOk    = 0, pollErr    = 0;
  uint32_t ackOk     = 0, ackErr     = 0;

  // Last error
  String lastError = "";

  // Last command info
  String lastCommandId = "";
  bool   lastCommandOk = true;

  // ── Helpers ──────────────────────────────────────────────────────────────

  void markServerOk() {
    serverReachable = true;
    lastServerOkMs  = millis();
    lastError       = "";
  }

  void markServerErr(const String& err) {
    // serverReachable-t nem rögtön dobjuk false-ra,
    // inkább "stale" logika a UI-ban (pl. 60s)
    lastError = err;
  }

  void fillJson(JsonDocument& doc) const {
    doc["deviceId"]        = deviceId;
    doc["firmware"]        = firmwareVersion;
    doc["wifiConnected"]   = wifiConnected;
    doc["ip"]              = ip;
    doc["rssi"]            = rssi;
    doc["timeSynced"]      = timeSynced;
    doc["serverReachable"] = serverReachable;
    doc["beaconOk"]        = beaconOk;
    doc["beaconErr"]       = beaconErr;
    doc["pollOk"]          = pollOk;
    doc["pollErr"]         = pollErr;
    doc["lastError"]       = lastError;
    doc["lastCommandId"]   = lastCommandId;
    doc["lastCommandOk"]   = lastCommandOk;
  }
};
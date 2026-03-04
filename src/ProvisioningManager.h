#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "PersistStore.h"
#include "Config.h"

// Provisioning állapotok
enum class ProvState {
  IDLE,
  CONNECTING_WIFI,    // Szervíz WiFi-re csatlakozik
  WIFI_CONNECTED,     // Csatlakozott, regisztrálhat
  REGISTERING,        // POST /provision/register folyamatban
  WAITING_ACTIVATION, // Polloz – várja az admin aktiválást
  ACTIVATED,          // Megkapta a config-ot
  FAILED              // Hiba, újraindítás szükséges
};

struct ProvConfig {
  String deviceId;
  String deviceName;
  String wifiSsid;
  String wifiPassword;
  String deviceKey;  // ← ÚJ
};

class ProvisioningManager {
public:
  ProvisioningManager(PersistStore& store);

  void begin();
  void loop();

  ProvState getState() const { return _state; }
  String getPendingId() const { return _pendingId; }
  String getMac() const { return _mac; }
  String getIP() const { return WiFi.localIP().toString(); }
  bool isActivated() const { return _state == ProvState::ACTIVATED; }
  bool isFailed() const { return _state == ProvState::FAILED; }

  // Meghívható UI-ból: ha aktivált, elmenti a config-ot és reboot
  void applyAndReboot();

private:
  PersistStore& _store;
  ProvState _state = ProvState::IDLE;

  String _mac;
  String _pendingId;
  ProvConfig _activatedConfig;

  unsigned long _lastPoll = 0;
  unsigned long _startTime = 0;
  int _registerRetry = 0;

  void connectServiceWifi();
  bool doRegister();
  bool doPollStatus();
  String httpPost(const String& url, const String& body);
  String httpGet(const String& url);
};
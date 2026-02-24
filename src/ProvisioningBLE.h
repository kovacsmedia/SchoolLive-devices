#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "PersistStore.h"
#include "UIManager.h"

// ESP32 Arduino BLE (beépített)
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

class ProvisioningBLE {
public:
  void begin(PersistStore& store, UIManager& ui);
  void loop();
  bool isActive() const { return _active; }

private:
  PersistStore* _store = nullptr;
  UIManager* _ui = nullptr;

  bool _active = false;
  bool _received = false;

  unsigned long _startedMs = 0;
  const unsigned long TIMEOUT_MS = 120000; // 2 perc

  // BLE
  BLEServer* _server = nullptr;
  BLEService* _service = nullptr;
  BLECharacteristic* _configChar = nullptr;

  // ide mentjük a bejövő configot
  String _ssid;
  String _pass;
  String _token;

  void startBle();
  void stopBle();

  bool saveWifiTxtCompat(const String& ssid, const String& pass);

  // callback
  class ConfigCallbacks : public BLECharacteristicCallbacks {
  public:
    explicit ConfigCallbacks(ProvisioningBLE* parent) : _p(parent) {}
    void onWrite(BLECharacteristic* c) override;
  private:
    ProvisioningBLE* _p;
  };
};
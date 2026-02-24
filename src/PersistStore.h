#pragma once
#include <Arduino.h>
#include <Preferences.h>

class PersistStore {
public:
  bool begin();

  // deviceKey
  bool hasDeviceKey() const;
  String getDeviceKey() const;
  bool setDeviceKey(const String& key);
  void clearDeviceKey();

  // (előre készítjük a provisioninghez)
  bool hasWifi() const;
  String getWifiSsid() const;
  String getWifiPass() const;
  bool setWifi(const String& ssid, const String& pass);
  void clearWifi();

  // “factory reset to provision”
  void factoryReset();

private:
  mutable Preferences _prefs;
  static constexpr const char* NS = "schoollive";
};
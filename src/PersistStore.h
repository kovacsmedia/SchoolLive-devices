#pragma once
#include <Arduino.h>
#include <Preferences.h>

class PersistStore {
public:
  bool begin();

  bool hasProvisionToken() const;
  String getProvisionToken() const;
  bool setProvisionToken(const String& token);
  void clearProvisionToken();

  bool hasDeviceKey() const;
  String getDeviceKey() const;
  bool setDeviceKey(const String& key);
  void clearDeviceKey();

  bool hasWifi() const;
  String getWifiSsid() const;
  String getWifiPass() const;
  String getWifiUser() const;           // ÚJ: WPA2 Enterprise user
  bool setWifi(const String& ssid, const String& pass);
  bool setWifiUser(const String& user); // ÚJ
  void clearWifi();

  void factoryReset();

private:
  mutable Preferences _prefs;
  static constexpr const char* NS = "schoollive";
};
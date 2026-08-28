#pragma once
#include <Arduino.h>
#include <Preferences.h>

extern "C" {
#include "dsp_processor.h"
}

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

  // Multi-node cluster: a device saját tenantId-ja (a provisioning-confirm
  // válaszából tanult meg, ld. BackendClient::confirmProvisioning) – ez
  // teszi lehetővé, hogy WsClient node-váltás esetén a
  // GET /cluster/locate?tenantId= hívást tudja indítani.
  bool hasTenantId() const;
  String getTenantId() const;
  bool setTenantId(const String& tenantId);

  // Multi-node cluster: a legutóbb ismert, tényleg működő node hostname-je
  // (pl. "api1.schoollive.hu"). Bootkor ezt próbáljuk elsőként a
  // Config.h BACKEND_BASE_URL-ből származó alapérték helyett – így egy
  // korábbi node-váltás túléli az újraindítást is.
  bool hasCachedNodeHost() const;
  String getCachedNodeHost() const;
  bool setCachedNodeHost(const String& host);

  bool hasWifi() const;
  String getWifiSsid() const;
  String getWifiPass() const;
  String getWifiUser() const;           // WPA2 Enterprise username (EAP)
  String getWifiSecurity() const;       // "WPA2_PERSONAL" | "WPA2_ENTERPRISE"
  bool setWifi(const String& ssid, const String& pass);
  bool setWifiUser(const String& user);
  bool setWifiSecurity(const String& security);
  void clearWifi();

  void factoryReset();
  uint8_t getVolume(uint8_t defaultVal = 9) const;
  bool    setVolume(uint8_t vol);

  dsp_channel_mode_t getChannelMode() const;
  bool               setChannelMode(dsp_channel_mode_t mode);


private:
  mutable Preferences _prefs;
  static constexpr const char* NS = "schoollive";
};
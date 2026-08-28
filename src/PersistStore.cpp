#include "PersistStore.h"

bool PersistStore::begin() {
  return _prefs.begin(NS, false);
}

bool PersistStore::hasDeviceKey() const {
  return _prefs.isKey("deviceKey") && _prefs.getString("deviceKey", "").length() > 0;
}

String PersistStore::getDeviceKey() const {
  return _prefs.getString("deviceKey", "");
}

bool PersistStore::setDeviceKey(const String& key) {
  if (key.length() == 0) return false;
  return _prefs.putString("deviceKey", key) > 0;
}

void PersistStore::clearDeviceKey() {
  _prefs.remove("deviceKey");
}

bool PersistStore::hasTenantId() const {
  return _prefs.isKey("tenantId") && _prefs.getString("tenantId", "").length() > 0;
}

String PersistStore::getTenantId() const {
  return _prefs.getString("tenantId", "");
}

bool PersistStore::setTenantId(const String& tenantId) {
  if (tenantId.length() == 0) return false;
  return _prefs.putString("tenantId", tenantId) > 0;
}

bool PersistStore::hasCachedNodeHost() const {
  return _prefs.isKey("nodeHost") && _prefs.getString("nodeHost", "").length() > 0;
}

String PersistStore::getCachedNodeHost() const {
  return _prefs.getString("nodeHost", "");
}

bool PersistStore::setCachedNodeHost(const String& host) {
  if (host.length() == 0) return false;
  return _prefs.putString("nodeHost", host) > 0;
}

bool PersistStore::hasWifi() const {
  return _prefs.isKey("wifiSsid") && _prefs.getString("wifiSsid", "").length() > 0;
}

String PersistStore::getWifiSsid() const {
  return _prefs.getString("wifiSsid", "");
}

String PersistStore::getWifiPass() const {
  return _prefs.getString("wifiPass", "");
}

String PersistStore::getWifiUser() const {
  String s = _prefs.getString("wifiUser", "");
  // Defenzív: korábbi rossz provisioning miatt az NVS-ben a "null" STRING
  // szerepelhet (ArduinoJson `.as<String>()` null JSON value-ra a "null"
  // négybetűs stringet adja). Ezt itt is kiszűrjük, hogy a régi NVS-ből
  // induló eszközök se kapjanak rossz user-t.
  if (s == "null") s = "";
  return s;
}

String PersistStore::getWifiSecurity() const {
  // Default: WPA2 Personal. Az enterprise mód CSAK explicit beállításra fut.
  String s = _prefs.getString("wifiSec", "WPA2_PERSONAL");
  // Defenzív null/empty fallback (lásd getWifiUser komment).
  if (s == "null" || s.length() == 0) s = "WPA2_PERSONAL";
  return s;
}

bool PersistStore::setWifi(const String& ssid, const String& pass) {
  if (ssid.length() == 0) return false;
  _prefs.putString("wifiSsid", ssid);
  _prefs.putString("wifiPass", pass);
  return true;
}

bool PersistStore::setWifiUser(const String& user) {
  _prefs.putString("wifiUser", user);
  return true;
}

bool PersistStore::setWifiSecurity(const String& security) {
  _prefs.putString("wifiSec", security);
  return true;
}

void PersistStore::clearWifi() {
  _prefs.remove("wifiSsid");
  _prefs.remove("wifiPass");
  _prefs.remove("wifiUser");
  _prefs.remove("wifiSec");
}

void PersistStore::factoryReset() {
  _prefs.clear();
}

bool PersistStore::hasProvisionToken() const {
  return _prefs.isKey("provToken") && _prefs.getString("provToken", "").length() > 0;
}

String PersistStore::getProvisionToken() const {
  return _prefs.getString("provToken", "");
}

bool PersistStore::setProvisionToken(const String& token) {
  if (token.length() == 0) return false;
  return _prefs.putString("provToken", token) > 0;
}

void PersistStore::clearProvisionToken() {
  _prefs.remove("provToken");
}

uint8_t PersistStore::getVolume(uint8_t defaultVal) const {
  return _prefs.getUChar("volume", defaultVal);
}

bool PersistStore::setVolume(uint8_t vol) {
  return _prefs.putUChar("volume", vol) > 0;
}

dsp_channel_mode_t PersistStore::getChannelMode() const {
  uint8_t v = _prefs.getUChar("chanMode", (uint8_t)DSP_CHANNEL_MIXED);
  if (v > (uint8_t)DSP_CHANNEL_STEREO) v = (uint8_t)DSP_CHANNEL_MIXED;
  return (dsp_channel_mode_t)v;
}

bool PersistStore::setChannelMode(dsp_channel_mode_t mode) {
  return _prefs.putUChar("chanMode", (uint8_t)mode) > 0;
}
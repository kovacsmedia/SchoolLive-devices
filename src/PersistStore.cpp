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

bool PersistStore::hasWifi() const {
  return _prefs.isKey("wifiSsid") && _prefs.getString("wifiSsid", "").length() > 0;
}

String PersistStore::getWifiSsid() const {
  return _prefs.getString("wifiSsid", "");
}

String PersistStore::getWifiPass() const {
  return _prefs.getString("wifiPass", "");
}

bool PersistStore::setWifi(const String& ssid, const String& pass) {
  if (ssid.length() == 0) return false;
  _prefs.putString("wifiSsid", ssid);
  _prefs.putString("wifiPass", pass);
  return true;
}

void PersistStore::clearWifi() {
  _prefs.remove("wifiSsid");
  _prefs.remove("wifiPass");
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
#include "ProvisioningManager.h"

ProvisioningManager::ProvisioningManager(PersistStore& store)
  : _store(store) {}

void ProvisioningManager::begin() {
  _mac = WiFi.macAddress();
  _mac.toUpperCase();
  _state = ProvState::CONNECTING_WIFI;
  _startTime = millis();
  _registerRetry = 0;
  connectServiceWifi();
  Serial.println("[PROV] begin, MAC=" + _mac);
}

void ProvisioningManager::loop() {
  // Timeout: 10 perc után reboot
  if (millis() - _startTime > PROV_POLL_TIMEOUT) {
    Serial.println("[PROV] Timeout, restarting...");
    delay(1000);
    ESP.restart();
  }

  switch (_state) {

    case ProvState::CONNECTING_WIFI:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[PROV] Service WiFi connected, IP=" + getIP());
        _state = ProvState::WIFI_CONNECTED;
      }
      break;

    case ProvState::WIFI_CONNECTED:
      _state = ProvState::REGISTERING;
      break;

    case ProvState::REGISTERING:
      if (doRegister()) {
        _state = ProvState::WAITING_ACTIVATION;
        _lastPoll = millis();
        Serial.println("[PROV] Registered, pendingId=" + _pendingId);
      } else {
        _registerRetry++;
        if (_registerRetry > 10) {
          _state = ProvState::FAILED;
          Serial.println("[PROV] Register failed too many times");
        } else {
          delay(3000);
        }
      }
      break;

    case ProvState::WAITING_ACTIVATION:
      if (millis() - _lastPoll >= PROV_POLL_INTERVAL) {
        _lastPoll = millis();
        if (doPollStatus()) {
          _state = ProvState::ACTIVATED;
          Serial.println("[PROV] Activated! name=" + _activatedConfig.deviceName);
        }
      }
      break;

    case ProvState::ACTIVATED:
      // Főloop kezeli – applyAndReboot() hívandó
      break;

    case ProvState::FAILED:
      // Főloop kezeli – pl. hibaüzenet + reboot
      break;

    default:
      break;
  }
}

void ProvisioningManager::connectServiceWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(PROV_WIFI_SSID, PROV_WIFI_PASS);
  Serial.println("[PROV] Connecting to service WiFi: " + String(PROV_WIFI_SSID));
}

bool ProvisioningManager::doRegister() {
  String url = String(BACKEND_BASE_URL) + "/provision/register";

  JsonDocument doc;
  doc["mac"] = _mac;
  doc["firmwareVersion"] = FW_VERSION;
  doc["ipAddress"] = getIP();

  String body;
  serializeJson(doc, body);

  String resp = httpPost(url, body);
  if (resp.isEmpty()) return false;

  JsonDocument respDoc;
  if (deserializeJson(respDoc, resp) != DeserializationError::Ok) return false;

  if (respDoc["ok"].as<bool>() != true) return false;

  _pendingId = respDoc["pendingId"].as<String>();
  return _pendingId.length() > 0;
}

bool ProvisioningManager::doPollStatus() {
  if (_pendingId.isEmpty()) return false;

  String url = String(BACKEND_BASE_URL) + "/provision/status/" + _pendingId;
  String resp = httpGet(url);
  if (resp.isEmpty()) return false;

  JsonDocument doc;
  if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;

  String status = doc["status"].as<String>();
  if (status != "activated") return false;

  JsonObject cfg = doc["config"];
  if (cfg.isNull()) return false;

  _activatedConfig.deviceId     = cfg["deviceId"].as<String>();
  _activatedConfig.deviceName   = cfg["deviceName"].as<String>();
  _activatedConfig.wifiSsid     = cfg["wifiSsid"].as<String>();
  _activatedConfig.wifiPassword = cfg["wifiPassword"].as<String>();
  _activatedConfig.deviceKey    = cfg["deviceKey"].as<String>();  // ← ÚJ

  return _activatedConfig.deviceKey.length() > 0;
}

void ProvisioningManager::applyAndReboot() {
  _store.setWifi(_activatedConfig.wifiSsid, _activatedConfig.wifiPassword);
  _store.setDeviceKey(_activatedConfig.deviceKey);  // ← most már megvan

  Serial.println("[PROV] WiFi + deviceKey saved, rebooting...");
  delay(500);
  ESP.restart();
}

String ProvisioningManager::httpPost(const String& url, const String& body) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) return "";

  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);

  if (code != 200 && code != 201) {
    Serial.printf("[PROV] POST %s -> %d\n", url.c_str(), code);
    http.end();
    return "";
  }

  String resp = http.getString();
  http.end();
  return resp;
}

String ProvisioningManager::httpGet(const String& url) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, url)) return "";

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[PROV] GET %s -> %d\n", url.c_str(), code);
    http.end();
    return "";
  }

  String resp = http.getString();
  http.end();
  return resp;
}
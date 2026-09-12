#include "ProvisioningManager.h"
#include <LittleFS.h>

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

        // A várakozás eddig TELJESEN NÉMA volt: ha az eszköz itt ragadt vagy
        // elhalt, a soros logból nem derült ki, hogy egyáltalán próbálkozik-e.
        // Tízpercenként... nem: minden tizedik kör elég, hogy lássuk, él.
        static uint32_t polls = 0;
        if ((polls++ % 10) == 0) {
          Serial.printf("[PROV] Varakozas aktivalasra (%lu. lekerdezes, %lu mp)\n",
                        (unsigned long)polls,
                        (unsigned long)((millis() - _startTime) / 1000UL));
        }

        if (doPollStatus()) {
          _state = ProvState::ACTIVATED;
          Serial.println("[PROV] Activated! name=" + _activatedConfig.deviceName);
        }
      }
      break;

    case ProvState::ACTIVATED:
      break;

    case ProvState::FAILED:
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
  _activatedConfig.deviceKey    = cfg["deviceKey"].as<String>();
  // Multi-node cluster: opcionális mező, régi backend válaszban hiányozhat.
  // A `.isNull()` explicit check szükséges, mert `.as<String>()` egy hiányzó/
  // null JSON mezőre a "null" NÉGYBETŰS STRING-et adná (ld. safeStr komment
  // lent) – ezt itt is elkerüljük, üres String marad, nem hiba.
  {
    JsonVariantConst tv = cfg["tenantId"];
    _activatedConfig.tenantId = tv.isNull() ? String("") : tv.as<String>();
  }

  // WPA2 Enterprise mezők (opcionális, default empty/PERSONAL).
  //
  // FONTOS: az ArduinoJson `.as<String>()` egy `null` JSON value-ra
  // a "null" négybetűs STRING-et adja vissza, NEM üres stringet. Tehát
  // a backend `wifiUser: null` / `wifiSecurity: null` után nem `""`-t,
  // hanem `"null"`-t kapnánk, ami eltörné a security típus check-et.
  // Ezért explicit nullSafe wrapper:
  auto safeStr = [&](const char* key, const char* fallback) -> String {
    JsonVariantConst v = cfg[key];
    if (v.isNull()) return String(fallback);
    const char* s = v.as<const char*>();
    if (s == nullptr) return String(fallback);
    String result(s);
    if (result == "null" || result.length() == 0) return String(fallback);
    return result;
  };

  _activatedConfig.wifiUser     = safeStr("wifiUser",     "");
  _activatedConfig.wifiSecurity = safeStr("wifiSecurity", "WPA2_PERSONAL");

  Serial.printf("[PROV] cfg parsed: ssid='%s' user='%s' security='%s'\n",
                _activatedConfig.wifiSsid.c_str(),
                _activatedConfig.wifiUser.c_str(),
                _activatedConfig.wifiSecurity.c_str());

  return _activatedConfig.deviceKey.length() > 0;
}

void ProvisioningManager::applyAndReboot() {
  /*
   * LÉPÉSENKÉNTI JELZÉS. Ez a metódus flash-írásokat végez (NVS + LittleFS),
   * és ha bármelyik beragad, az eszköz némán megáll – pontosan ez történt
   * 2026-09-12-én. Minden lépés után `flush()`: így a soros log akkor is
   * megmutatja, hol álltunk meg, ha utána már semmi nem fut.
   */
  auto step = [](const char* what) {
    Serial.printf("[PROV] > %s\n", what);
    Serial.flush();
  };

  step("NVS: wifi");
  _store.setWifi(_activatedConfig.wifiSsid, _activatedConfig.wifiPassword);
  step("NVS: wifiUser");
  _store.setWifiUser(_activatedConfig.wifiUser);
  step("NVS: wifiSecurity");
  _store.setWifiSecurity(_activatedConfig.wifiSecurity);
  step("NVS: deviceKey");
  _store.setDeviceKey(_activatedConfig.deviceKey);
  step("NVS: visszaolvasas");

  /*
   * VISSZAOLVASÁS. A Preferences/NVS írás CSENDBEN elbukik, ha a partíció
   * megtelt – a hívók pedig eddig nem nézték a visszatérési értéket. Egy ilyen
   * eszköz aktiválás után is `hasWifi=0 hasKey=0`-val indult, azaz ÖRÖKRE
   * provisioning módban ragadt, és a soros logból sem derült ki, miért.
   * (A 2026-09-12-i eset oka: a tanévnyi csengetési rend NVS-be került,
   *  miközben az egész partíció 20 kB – ld. BellManager.h.)
   *
   * Innentől ha a mentés nem sikerül, azt HANGOSAN kiírjuk, és NEM indítunk
   * újra – az újraindítás úgyis csak visszahozná ugyanide. Így a készülék
   * aktiválható marad, amint a hely felszabadult.
   */
  const bool okWifi = _store.hasWifi();
  const bool okKey  = _store.hasDeviceKey();
  if (!okWifi || !okKey) {
    // CSAK EGYSZER jelentünk: az állapotgép addig hívja ezt a metódust, amíg
    // aktivált állapotban van, és a másodpercenkénti ismétlés csak elárasztaná
    // a soros portot – pont azt a hibát okozva, ami ellen máshol küzdünk.
    static bool reported = false;
    if (!reported) {
      reported = true;
      Serial.printf("[PROV] ❌ MENTES SIKERTELEN (wifi=%d key=%d)\n", okWifi, okKey);
      Serial.println("[PROV] A tarolo nem irhato. Az eszkoz NEM indul ujra, mert azzal");
      Serial.println("[PROV] ugyanide jutna vissza. Teljes torles: pio run -t erase_flash");
    }
    return;
  }
  if (_activatedConfig.tenantId.length() > 0) {
    _store.setTenantId(_activatedConfig.tenantId);
  }

  step("LittleFS: wifi.txt");

  // wifi.txt írása – SLNetworkManager ebből olvas
  File f = LittleFS.open("/wifi.txt", "w");
  if (f) {
    f.print("\"");
    f.print(_activatedConfig.wifiSsid);
    f.print("\",\"");
    f.print(_activatedConfig.wifiPassword);
    f.println("\"");
    f.close();
    Serial.println("[PROV] wifi.txt saved");
  } else {
    Serial.println("[PROV] wifi.txt write FAILED");
  }

  Serial.println("[PROV] Rebooting...");
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
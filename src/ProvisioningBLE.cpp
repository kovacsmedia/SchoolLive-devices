#include "ProvisioningBLE.h"

static const char* SERVICE_UUID = "f55d0f10-6f58-4c18-9b77-0a8f6fdd1001";
static const char* CONFIG_UUID  = "f55d0f10-6f58-4c18-9b77-0a8f6fdd1002";

void ProvisioningBLE::begin(PersistStore& store, UIManager& ui) {
  _store = &store;
  _ui = &ui;

  _ssid = "";
  _pass = "";
  _token = "";
  _received = false;

  startBle();
  _startedMs = millis();
  _active = true;

  _ui->drawBootStatus("PROVISION", "BLE waiting...");
}

void ProvisioningBLE::loop() {
  if (!_active) return;

  // timeout → újrakezdés (restart a legegyszerűbb determinisztikusan)
  const unsigned long now = millis();
  if (!_received && (now - _startedMs) > TIMEOUT_MS) {
    _ui->drawBootStatus("PROVISION", "Timeout -> restart");
    delay(500);
    stopBle();
    delay(200);
    ESP.restart();
  }

  // ha megjött config → mentés → BLE off → reboot
  if (_received) {
    _ui->drawBootStatus("PROVISION", "Saving config...");
    delay(300);

    _store->setWifi(_ssid, _pass);
    _store->setProvisionToken(_token);

    // kompatibilitás: jelenlegi NetworkManager wifi.txt-ről dolgozik
    saveWifiTxtCompat(_ssid, _pass);

    _ui->drawBootStatus("PROVISION", "BLE OFF, reboot");
    delay(300);

    stopBle();
    delay(200);
    ESP.restart();
  }
}

void ProvisioningBLE::startBle() {
  BLEDevice::init("SchoolLive Provisioning");
  _server = BLEDevice::createServer();

  _service = _server->createService(SERVICE_UUID);

  _configChar = _service->createCharacteristic(
    CONFIG_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  _configChar->setCallbacks(new ConfigCallbacks(this));

  _service->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();
}

void ProvisioningBLE::stopBle() {
  // biztos leállítás
  BLEDevice::deinit(true);
}

bool ProvisioningBLE::saveWifiTxtCompat(const String& ssid, const String& pass) {
  // Legkonzervatívabb: 2 soros forma
  File f = LittleFS.open("/wifi.txt", "w");
  if (!f) return false;
  f.println(ssid);
  f.println(pass);
  f.close();
  return true;
}

void ProvisioningBLE::ConfigCallbacks::onWrite(BLECharacteristic* c) {
  if (!_p) return;

  std::string v = c->getValue();
  if (v.empty()) return;

  // JSON parse
  StaticJsonDocument<384> doc;
  DeserializationError err = deserializeJson(doc, v);
  if (err) return;

  const char* ssid = doc["ssid"];
  const char* pass = doc["pass"];
  const char* token = doc["token"];

  if (!ssid || !token) return;          // ssid+token kötelező
  if (!pass) pass = "";                // lehet üres (ha nyitott wifi)

  _p->_ssid = String(ssid);
  _p->_pass = String(pass);
  _p->_token = String(token);

  _p->_received = true;
}
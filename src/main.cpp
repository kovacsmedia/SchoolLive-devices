#include <Arduino.h>
#include <LittleFS.h>

#include "Config.h"
#include "AudioManager.h"
#include "NetworkManager.h"
#include "BellManager.h"
#include "UIManager.h"

#include "PersistStore.h"
#include "BackendClient.h"
#include "DeviceAgent.h"
#include "DeviceTelemetry.h"
#include <Wire.h>
#include <WiFi.h>
#include "ProvisioningBLE.h"

AudioManager audioManager;
NetworkManager networkManager;
BellManager bellManager(audioManager, networkManager);
UIManager uiManager(audioManager, networkManager, bellManager);

PersistStore store;
BackendClient backend;
DeviceAgent agent;
TaskHandle_t TaskNetworkHandle = nullptr;
ProvisioningBLE provisioning;
DeviceTelemetry telemetry;

void TaskNetwork(void * pvParameters) {
  (void)pvParameters;

  networkManager.begin();

  for (;;) {
    networkManager.loop();
    agent.loop();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void setup() {
  // Később provisioningnél ezt feltételessé tesszük. Most maradhat.
  btStop();

  Wire.begin(I2C_SDA, I2C_SCL);

  // FS init
  LittleFS.begin(true);

  // NVS init
  store.begin();

  // App init
  audioManager.begin();
  uiManager.begin();
  bellManager.begin();
 
  uiManager.drawBootStatus("System check", "Reading config file");
  

  // A jelenlegi firmware logikát nem bántjuk: wifi.txt alapján csatlakozik
  bool success = networkManager.syncTimeBlocking();
  if (!success) {
    uiManager.drawBootStatus("WIFI FAILED!", "Check wifi.txt");
    delay(3000);
  } else {
    uiManager.drawBootStatus("WIFI OK!", networkManager.getIP());
    delay(1000);
  }

  // Backend init
  backend.begin(String(BACKEND_BASE_URL));
  // deviceKey: NVS-ből (ha van), különben DEV defaultból
  String dk = store.getDeviceKey();
  if (dk.length() == 0 && String(DEVICE_KEY_DEFAULT).length() > 0) {
    dk = String(DEVICE_KEY_DEFAULT);
    store.setDeviceKey(dk);
  }
  backend.setDeviceKey(dk);
  telemetry.firmwareVersion = String(FW_VERSION);
  telemetry.deviceId = WiFi.macAddress();
  agent.begin(networkManager, audioManager, uiManager, backend, telemetry);
  agent.setFirmwareVersion(String(FW_VERSION));

  // Network task (0. core)
  xTaskCreatePinnedToCore(
    TaskNetwork,
    "NetworkTask",
    10000,
    NULL,
    1,
    &TaskNetworkHandle,
    0
  );
    store.begin();

  // Backend init (kell confirmhoz is)
  backend.begin(String(BACKEND_BASE_URL));

  const bool hasKey = store.hasDeviceKey();
  const bool hasWifi = store.hasWifi();
  const bool hasToken = store.hasProvisionToken();

  if (!hasKey) {
    // PROVISIONING / CONFIRM MODE
    // BT-t itt NEM állítjuk le, mert BLE kell.
    // WiFi csak akkor indul, ha token+wifi már megvan.

    audioManager.begin();
    uiManager.begin();
    uiManager.setTelemetry(&telemetry);
    bellManager.begin();

    if (hasWifi && hasToken) {
      // Megvan a wifi+token → próbáljuk a WiFi-t és confirmot
      btStop(); // itt már BLE nem kell, WiFi-nek kell a rádió
      uiManager.drawBootStatus("PROVISION", "WiFi + Confirm...");
      delay(300);

      bool wifiOk = networkManager.syncTimeBlocking();
      if (!wifiOk) {
        // rossz wifi → vissza provisioning
        uiManager.drawBootStatus("PROVISION", "WiFi fail -> BLE");
        delay(500);
        store.clearWifi();
        store.clearProvisionToken();
        ESP.restart();
      }

      String dk, ssid2, pass2;
      bool ok = backend.confirmProvisioning(store.getProvisionToken(), dk, ssid2, pass2);
      if (!ok) {
        // token lejárt / invalid → provisioning újra
        uiManager.drawBootStatus("PROVISION", "Token invalid -> BLE");
        delay(500);
        store.clearProvisionToken();
        ESP.restart();
      }

      // siker: deviceKey mentés, token törlés
      store.setDeviceKey(dk);
      store.clearProvisionToken();

      // backend esetleg visszaad wifi-t (ha akarjuk frissíteni)
      if (ssid2.length() > 0) {
        store.setWifi(ssid2, pass2);
        // wifi.txt kompatibilitás frissítése
        File f = LittleFS.open("/wifi.txt", "w");
        if (f) { f.println(ssid2); f.println(pass2); f.close(); }
      }

      uiManager.drawBootStatus("PROVISION", "OK -> reboot");
      delay(500);
      ESP.restart();
    }

    // nincs még wifi/token → BLE provisioning
    provisioning.begin(store, uiManager);

    // NEM indítunk network taskot ilyenkor
    return;
  }

  // --- NORMÁL ONLINE MÓD ---
  btStop();
}

void loop() {
  audioManager.loop();
  uiManager.loop();
  bellManager.loop();
    if (provisioning.isActive()) {
    provisioning.loop();
    return;
  }
}
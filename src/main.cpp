#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include <WiFi.h>

#include "Config.h"
#include "PersistStore.h"
#include "ProvisioningManager.h"
#include "AudioManager.h"
#include "NetworkManager.h"
#include "BellManager.h"
#include "UIManager.h"
#include "BackendClient.h"
#include "DeviceAgent.h"
#include "DeviceTelemetry.h"

// --- Globális objektumok ---
NetworkManager  networkManager;
AudioManager    audioManager;
PersistStore    store;
BackendClient   backend;
BellManager     bellManager(audioManager, networkManager, backend);
DeviceAgent     agent;
DeviceTelemetry telemetry;

// --- Pointerek – setup()-ban példányosítjuk ---
UIManager*           uiManager   = nullptr;
ProvisioningManager* provManager = nullptr;

bool inProvisioningMode = false;
TaskHandle_t TaskNetworkHandle = nullptr;

// --- Provisioning task (core 0) ---
void TaskProvisioning(void* pvParameters) {
  (void)pvParameters;

  uiManager->enterProvisioningMode();

  for (;;) {
    provManager->loop();

    ProvState state = provManager->getState();
    String mac = provManager->getMac();
    String ip  = provManager->getIP();

    switch (state) {
      case ProvState::CONNECTING_WIFI:
        uiManager->updateProvisioningDisplay(mac, "", "WiFi csatlakozas...");
        break;
      case ProvState::WIFI_CONNECTED:
      case ProvState::REGISTERING:
        uiManager->updateProvisioningDisplay(mac, ip, "Regisztracio...");
        break;
      case ProvState::WAITING_ACTIVATION:
        uiManager->updateProvisioningDisplay(mac, ip, "Var aktivalasra...");
        break;
      case ProvState::ACTIVATED:
        uiManager->updateProvisioningDisplay(mac, ip, "Aktivalva! Indul...");
        delay(2000);
        provManager->applyAndReboot();
        break;
      case ProvState::FAILED:
        uiManager->updateProvisioningDisplay(mac, ip, "HIBA! Ujraindul...");
        delay(5000);
        ESP.restart();
        break;
      default:
        break;
    }

    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

// --- Normál network task (core 0) ---
// beacon, poll, bell szinkron – mind egymás után, ugyanazon a core-on
// így a BackendClient cooldown hatékonyan véd az SSL socket ütközések ellen
void TaskNetwork(void* pvParameters) {
  (void)pvParameters;
  networkManager.begin();
  for (;;) {
    networkManager.loop();
    agent.loop();
    bellManager.loop();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// --- Provisioning mód ---
void startProvisioningMode() {
  inProvisioningMode = true;
  Serial.println("[MAIN] Starting PROVISIONING mode");

  provManager = new ProvisioningManager(store);
  provManager->begin();

  xTaskCreatePinnedToCore(
    TaskProvisioning,
    "ProvTask",
    16384,
    NULL,
    1,
    NULL,
    0
  );
}

// --- Normál mód ---
void startNormalMode() {
  inProvisioningMode = false;
  Serial.println("[MAIN] Starting NORMAL mode");

  uiManager->drawBootStatus("System check", "WiFi + time sync");
  delay(300);

  bool wifiOk = networkManager.syncTimeBlocking();
  if (!wifiOk) {
    uiManager->drawBootStatus("WIFI FAILED!", "Check wifi config");
    delay(3000);
  } else {
    uiManager->drawBootStatus("WIFI OK!", networkManager.getIP().c_str());
    delay(1000);
  }

  backend.begin(String(BACKEND_BASE_URL));

  String dk = store.getDeviceKey();
  if (dk.length() == 0 && String(DEVICE_KEY_DEFAULT).length() > 0) {
    dk = String(DEVICE_KEY_DEFAULT);
    store.setDeviceKey(dk);
  }
  backend.setDeviceKey(dk);

  telemetry.firmwareVersion = String(FW_VERSION);
  telemetry.deviceId = WiFi.macAddress();

  agent.begin(networkManager, audioManager, *uiManager, backend, telemetry);
  agent.setFirmwareVersion(String(FW_VERSION));

  xTaskCreatePinnedToCore(
    TaskNetwork,
    "NetworkTask",
    12000,
    NULL,
    1,
    &TaskNetworkHandle,
    0
  );
}

// --- SETUP ---
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=== SETUP START ===");
  Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
  Serial.printf("Reset reason: %d\n", (int)esp_reset_reason());

  btStop();
  Wire.begin(I2C_SDA, I2C_SCL);
  LittleFS.begin(true, "/littlefs", 10, "littlefs");
  store.begin();

  audioManager.begin(&store);

  uiManager = new UIManager(audioManager, networkManager, bellManager, store);
  uiManager->begin();
  uiManager->setTelemetry(&telemetry);

  bellManager.begin();

  bool hasWifi   = store.hasWifi();
  bool hasKey    = store.hasDeviceKey();
  bool needsProv = !hasWifi || !hasKey;

  Serial.printf("[MAIN] hasWifi=%d hasKey=%d needsProv=%d\n", hasWifi, hasKey, needsProv);

  if (needsProv) {
    startProvisioningMode();
  } else {
    startNormalMode();
  }
}

// --- LOOP (core 1) ---
// Csak audio és UI – HTTP hívás nincs itt, az mind a TaskNetwork-ben van
void loop() {
  if (!inProvisioningMode) {
    audioManager.loop();
    uiManager->loop();
    // bellManager.loop() → TaskNetwork-be költözött (core 0)
  } else {
    uiManager->loop();
    delay(50);
  }
}
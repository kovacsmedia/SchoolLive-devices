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

// --- Singletons ---
AudioManager audioManager;
NetworkManager networkManager;
BellManager bellManager(audioManager, networkManager);
UIManager uiManager(audioManager, networkManager, bellManager);

PersistStore store;
BackendClient backend;
DeviceAgent agent;
DeviceTelemetry telemetry;

ProvisioningManager* provManager = nullptr;
bool inProvisioningMode = false;

TaskHandle_t TaskNetworkHandle = nullptr;

// --- Provisioning task (core 0) ---
void TaskProvisioning(void* pvParameters) {
  (void)pvParameters;
  for (;;) {
    provManager->loop();

    if (provManager->getState() == ProvState::ACTIVATED) {
      // Kiírjuk a kijelzőre és alkalmazzuk
      uiManager.drawBootStatus("Aktivalva!", provManager->getPendingId().substring(0, 8).c_str());
      delay(2000);
      provManager->applyAndReboot();
    }

    if (provManager->isFailed()) {
      uiManager.drawBootStatus("PROV HIBA", "Ujraindul 5s...");
      delay(5000);
      ESP.restart();
    }

    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

// --- Normál network task (core 0) ---
void TaskNetwork(void* pvParameters) {
  (void)pvParameters;
  networkManager.begin();
  for (;;) {
    networkManager.loop();
    agent.loop();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// --- Provisioning mód indítása ---
void startProvisioningMode() {
  inProvisioningMode = true;
  Serial.println("[MAIN] Starting PROVISIONING mode");

  uiManager.drawBootStatus("PROVISIONING", WiFi.macAddress().c_str());
  delay(1000);

  provManager = new ProvisioningManager(store);
  provManager->begin();

  // Kijelzőn mutatjuk a MAC és IP-t
  uiManager.drawBootStatus(
    ("MAC: " + WiFi.macAddress()).c_str(),
    "Var aktivalasra..."
  );

  xTaskCreatePinnedToCore(
    TaskProvisioning,
    "ProvTask",
    8192,
    NULL,
    1,
    NULL,
    0
  );
}

// --- Normál mód indítása ---
void startNormalMode() {
  inProvisioningMode = false;
  Serial.println("[MAIN] Starting NORMAL mode");

  uiManager.drawBootStatus("System check", "WiFi + time sync");
  delay(300);

  bool wifiOk = networkManager.syncTimeBlocking();
  if (!wifiOk) {
    uiManager.drawBootStatus("WIFI FAILED!", "Check wifi config");
    delay(3000);
  } else {
    uiManager.drawBootStatus("WIFI OK!", networkManager.getIP().c_str());
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

  agent.begin(networkManager, audioManager, uiManager, backend, telemetry);
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

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("Reset reason: %d\n", (int)esp_reset_reason());

  btStop();
  Wire.begin(I2C_SDA, I2C_SCL);
  LittleFS.begin(true);
  store.begin();

  audioManager.begin();
  uiManager.begin();
  uiManager.setTelemetry(&telemetry);
  bellManager.begin();

  // Döntés: provisioning vagy normál mód?
  bool hasWifi    = store.hasWifi();
  bool hasKey     = store.hasDeviceKey();
  bool needsProv  = !hasWifi || !hasKey;

  Serial.printf("[MAIN] hasWifi=%d hasKey=%d needsProv=%d\n", hasWifi, hasKey, needsProv);

  if (needsProv) {
    startProvisioningMode();
  } else {
    startNormalMode();
  }
}

void loop() {
  if (!inProvisioningMode) {
    audioManager.loop();
    uiManager.loop();
    bellManager.loop();
  } else {
    // Provisioning módban csak az UI fut a loop-ban
    uiManager.loop();
    delay(50);
  }
}
#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include <WiFi.h>

#include "Config.h"
#include "AudioManager.h"
#include "NetworkManager.h"
#include "BellManager.h"
#include "UIManager.h"

#include "PersistStore.h"
#include "BackendClient.h"
#include "DeviceAgent.h"
#include "DeviceTelemetry.h"

// --- Singletons / modules ---
AudioManager audioManager;
NetworkManager networkManager;
BellManager bellManager(audioManager, networkManager);
UIManager uiManager(audioManager, networkManager, bellManager);

PersistStore store;
BackendClient backend;
DeviceAgent agent;
DeviceTelemetry telemetry;

TaskHandle_t TaskNetworkHandle = nullptr;

void TaskNetwork(void *pvParameters) {
  (void)pvParameters;

  networkManager.begin();

  for (;;) {
    networkManager.loop();
    agent.loop();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("Reset reason: %d\n", (int)esp_reset_reason());

  // BT off (BLE provisioning kikerült)
  btStop();

  // I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // FS init
  LittleFS.begin(true);

  // NVS init
  store.begin();

  // App init
  audioManager.begin();
  uiManager.begin();
  uiManager.setTelemetry(&telemetry);
  bellManager.begin();

  uiManager.drawBootStatus("System check", "WiFi + time sync");
  delay(300);

  // WiFi + NTP (a meglévő logika: wifi.txt alapján)
  bool wifiOk = networkManager.syncTimeBlocking();
  if (!wifiOk) {
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

  // Telemetry identity
  telemetry.firmwareVersion = String(FW_VERSION);
  telemetry.deviceId = WiFi.macAddress();

  // Agent init
  agent.begin(networkManager, audioManager, uiManager, backend, telemetry);
  agent.setFirmwareVersion(String(FW_VERSION));

  // Network task (core 0)
  xTaskCreatePinnedToCore(
    TaskNetwork,
    "NetworkTask",
    12000,   // kis plusz headroom a hálózati stacknek
    NULL,
    1,
    &TaskNetworkHandle,
    0
  );
}

void loop() {
  audioManager.loop();
  uiManager.loop();
  bellManager.loop();
}
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

#include <Wire.h>
#include <WiFi.h>

AudioManager audioManager;
NetworkManager networkManager;
BellManager bellManager(audioManager, networkManager);
UIManager uiManager(audioManager, networkManager, bellManager);

PersistStore store;
BackendClient backend;
DeviceAgent agent;

TaskHandle_t TaskNetworkHandle = nullptr;

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

  uiManager.drawBootStatus("Config Check", "Reading config file");
  delay(500);

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

  agent.begin(networkManager, audioManager, backend);
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
}

void loop() {
  audioManager.loop();
  uiManager.loop();
  bellManager.loop();
}
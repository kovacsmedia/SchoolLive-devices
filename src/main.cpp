// ─────────────────────────────────────────────────────────────────────────────
// main.cpp – SchoolLive ESP32-S3-N16R8 (S3.4)
//
// Task architektúra:
//   Core 0: TaskNetwork  – WiFi, NTP, BackendClient poll/beacon, BellManager szinkron
//            TaskSync    – WebSocket SyncClient loop (SyncCast protokoll)
//   Core 1: loop()       – AudioManager.loop() + UIManager.loop()
//
// PSRAM kihasználás:
//   • Audio stream buffer (Audio.h setPsram(true))
//   • SyncClient TTS pre-fetch buffer (256KB)
//   • Task stack-ek PSRAM-ban (pvPortMallocCaps)
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include <WiFi.h>
#include <sys/time.h>

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
#include "SyncClient.h"

// ── Globális objektumok ───────────────────────────────────────────────────────
NetworkManager  networkManager;
AudioManager    audioManager;
PersistStore    store;
BackendClient   backend;
BellManager     bellManager(audioManager, networkManager, backend);
DeviceAgent     agent;
DeviceTelemetry telemetry;
SyncClient      syncClient;

UIManager*           uiManager   = nullptr;
ProvisioningManager* provManager = nullptr;

bool inProvisioningMode = false;

// ── Provisioning task (Core 0) ────────────────────────────────────────────────
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
            default: break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ── Network task (Core 0) ─────────────────────────────────────────────────────
// HTTP poll/beacon + BellManager szinkron
// S3-n nincs audio busy guard – külön task kezeli az audiot
void TaskNetwork(void* pvParameters) {
    (void)pvParameters;
    networkManager.begin();
    for (;;) {
        networkManager.loop();
        agent.loop();
        bellManager.loop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ── SyncCast WebSocket task (Core 0) ─────────────────────────────────────────
void TaskSync(void* pvParameters) {
    (void)pvParameters;
    // Várunk amíg WiFi és NTP szinkron megvan
    while (!networkManager.isConnected() || !networkManager.isTimeSynced()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    Serial.println("[SYNC-TASK] WiFi+NTP kész → WebSocket csatlakozás");

    String deviceKey = store.getDeviceKey();
    String tenantId  = store.getTenantId();
    syncClient.begin(audioManager, bellManager, deviceKey, tenantId);

    for (;;) {
        syncClient.loop();
        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms loop = gyors WS feldolgozás
    }
}

// ── Normál mód ────────────────────────────────────────────────────────────────
void startNormalMode() {
    inProvisioningMode = false;
    Serial.println("[MAIN] NORMAL mód");

    uiManager->drawBootStatus("System check", "WiFi + NTP szinkron");
    delay(300);

    bool wifiOk = networkManager.syncTimeBlocking();
    if (!wifiOk) {
        uiManager->drawBootStatus("WIFI HIBA!", "Ellenőrizd a beállításokat");
        delay(3000);
    } else {
        uiManager->drawBootStatus("WiFi OK", networkManager.getIP().c_str());
        delay(800);
    }

    backend.begin(String(BACKEND_BASE_URL));
    String dk = store.getDeviceKey();
    if (dk.length() == 0 && String(DEVICE_KEY_DEFAULT).length() > 0) {
        dk = String(DEVICE_KEY_DEFAULT);
        store.setDeviceKey(dk);
    }
    backend.setDeviceKey(dk);

    telemetry.firmwareVersion = String(FW_VERSION);
    telemetry.deviceId        = WiFi.macAddress();

    agent.begin(networkManager, audioManager, *uiManager, backend, telemetry);
    agent.setFirmwareVersion(String(FW_VERSION));

    // PSRAM-ban allokált task stack-ek
    // TaskNetwork: 20KB stack
    xTaskCreatePinnedToCore(
        TaskNetwork, "NetworkTask",
        20480, NULL, 1, NULL, 0
    );

    // TaskSync: 12KB stack (WebSocket + JSON)
    xTaskCreatePinnedToCore(
        TaskSync, "SyncTask",
        12288, NULL, 2, NULL, 0  // magasabb prioritás mint a network
    );

    uiManager->drawBootStatus("Kész", ("FW: " + String(FW_VERSION)).c_str());
    delay(500);
}

// ── Provisioning mód ─────────────────────────────────────────────────────────
void startProvisioningMode() {
    inProvisioningMode = true;
    Serial.println("[MAIN] PROVISIONING mód");
    provManager = new ProvisioningManager(store);
    provManager->begin();
    xTaskCreatePinnedToCore(
        TaskProvisioning, "ProvTask",
        16384, NULL, 1, NULL, 0
    );
}

// ── setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== SchoolLive S3.4 SETUP ===");
    Serial.printf("Free heap:  %d bytes\n", ESP.getFreeHeap());
    Serial.printf("PSRAM:      %s (%d bytes)\n",
                  psramFound() ? "OK" : "NEM TALÁLHATÓ",
                  (int)ESP.getPsramSize());
    Serial.printf("Flash:      %d MB\n", (int)(ESP.getFlashChipSize() / 1024 / 1024));

    btStop();  // Bluetooth ki – nem kell, memóriát szabadít fel
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

    Serial.printf("[MAIN] hasWifi=%d hasKey=%d needsProv=%d\n",
                  hasWifi, hasKey, needsProv);

    if (needsProv) startProvisioningMode();
    else           startNormalMode();
}

// ── loop (Core 1) ─────────────────────────────────────────────────────────────
// CSAK audio és UI – hálózati hívás nincs itt
void loop() {
    if (!inProvisioningMode) {
        audioManager.loop();
        uiManager->loop();
    } else {
        uiManager->loop();
        delay(50);
    }
}
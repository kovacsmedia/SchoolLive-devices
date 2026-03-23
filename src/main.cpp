// ─────────────────────────────────────────────────────────────────────────────
// main.cpp – SchoolLive S3.54
//
// Változások S3.52 → S3.54:
//   • setOnPlayAction lambda: const char* → const String&
//   • backend.setUIManager(uiManager)   – HTTP aktivitás jelzés
//   • syncClient.setUIManager(uiManager) – WS aktivitás jelzés
//   • snapClient.setOnConnected / setOnDisconnected → ui->setSnapStatus()
//   • Verzió string: S3.52 → S3.54
//
// Task architektúra:
//   Core 0: TaskNetwork   – WiFi, NTP, BackendClient poll/beacon, BellManager
//            TaskSync     – WebSocket SyncClient (SyncCast, csak BELL)
//            TaskSnapcast – Snapcast TCP kliens (PCM stream → I2S)
//   Core 1: loop()        – AudioManager.loop() + UIManager.loop()
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
#include "SnapcastClient.h"
#include "OtaManager.h"

// ── Globális objektumok ───────────────────────────────────────────────────────
NetworkManager  networkManager;
AudioManager    audioManager;
PersistStore    store;
BackendClient   backend;
BellManager     bellManager(audioManager, networkManager, backend);
DeviceAgent     agent;
DeviceTelemetry telemetry;
SyncClient      syncClient;
SnapcastClient  snapClient;
OtaManager      otaManager;

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
void TaskNetwork(void* pvParameters) {
    (void)pvParameters;
    networkManager.begin();
    for (;;) {
        networkManager.loop();
        agent.loop();
        bellManager.loop();
        otaManager.loop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ── SyncCast WebSocket task (Core 0) ──────────────────────────────────────────
void TaskSync(void* pvParameters) {
    (void)pvParameters;
    while (!networkManager.isConnected() || !networkManager.isTimeSynced()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    Serial.println("[SYNC-TASK] WiFi+NTP kész → WebSocket csatlakozás");

    String deviceKey = store.getDeviceKey();
    syncClient.begin(audioManager, bellManager, deviceKey, "");

    syncClient.setOnPlayAction([](const String& action) {
        if (uiManager) uiManager->setPlayingState(action);
    });

    for (;;) {
        syncClient.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── Snapcast task (Core 0) ────────────────────────────────────────────────────
void TaskSnapcast(void* pvParameters) {
    (void)pvParameters;

    while (!networkManager.isConnected() || !networkManager.isTimeSynced()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    while (!backend.isReady()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    Serial.println("[SNAP-TASK] WiFi+NTP+backend kész → Snapcast port lekérése");

    uint16_t snapPort = 0;
    uint8_t  retries  = 0;
    while (snapPort == 0 && retries < 10) {
        snapPort = backend.fetchSnapPort();
        if (snapPort == 0) {
            Serial.printf("[SNAP-TASK] Port lekérés sikertelen, újra 5s múlva (retry %d/10)\n", ++retries);
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    if (snapPort == 0) {
        Serial.println("[SNAP-TASK] ❌ Snapcast port nem elérhető – task leáll");
        // Snap véglegesen offline – UI értesítés
        if (uiManager) uiManager->setSnapStatus(false, "no port");
        vTaskDelete(NULL);
        return;
    }

    Serial.printf("[SNAP-TASK] Snapcast port: %d → csatlakozás\n", snapPort);

    // ── S3.54: Snap connected / disconnected → UIManager ─────────────────
    snapClient.setOnConnected([](){ 
        Serial.println("[SNAP-TASK] Snap CONNECTED");
        if (uiManager) uiManager->setSnapStatus(true);
    });
    snapClient.setOnDisconnected([](){
        Serial.println("[SNAP-TASK] Snap DISCONNECTED");
        if (uiManager) uiManager->setSnapStatus(false);
    });

    String  mac = WiFi.macAddress();
    uint8_t vol = (uint8_t)map(audioManager.getVolume(), 1, 10, 10, 100);
    snapClient.begin(mac, vol, snapPort);

    for (;;) {
        snapClient.loop();
        vTaskDelay(pdMS_TO_TICKS(1));  // 1ms – gyorsabb adatolvasás
    }
}

// ── Normál mód ────────────────────────────────────────────────────────────────
void startNormalMode() {
    inProvisioningMode = false;
    Serial.println("[MAIN] NORMAL mód – S3.54 (Snapcast)");

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

    // ── S3.54: UI pointer regisztráció ────────────────────────────────────
    backend.setUIManager(uiManager);
    syncClient.setUIManager(uiManager);

    telemetry.firmwareVersion = String(FW_VERSION);
    telemetry.deviceId        = WiFi.macAddress();

    agent.begin(networkManager, audioManager, *uiManager, backend, telemetry);
    agent.setFirmwareVersion(String(FW_VERSION));
    otaManager.begin(backend, uiManager);

    xTaskCreatePinnedToCore(TaskNetwork,  "NetworkTask", 20480, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(TaskSync,     "SyncTask",    12288, NULL, 2, NULL, 0);
    // SnapTask: nagyobb stack + magasabb prioritás – gyorsabb adatolvasás
    // vTaskDelay(1ms) a loop-ban hogy ne blokkolja a NetworkTask-ot
    xTaskCreatePinnedToCore(TaskSnapcast, "SnapTask",    16384, NULL, 3, NULL, 0);

    uiManager->drawBootStatus("Kész", ("FW: " + String(FW_VERSION)).c_str());
    delay(500);
}

// ── Provisioning mód ──────────────────────────────────────────────────────────
void startProvisioningMode() {
    inProvisioningMode = true;
    Serial.println("[MAIN] PROVISIONING mód");
    provManager = new ProvisioningManager(store);
    provManager->begin();
    xTaskCreatePinnedToCore(TaskProvisioning, "ProvTask", 16384, NULL, 1, NULL, 0);
}

// ── setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== SchoolLive S3.54 SETUP ===");
    Serial.printf("Free heap:  %d bytes\n", ESP.getFreeHeap());
    Serial.printf("PSRAM:      %s (%d bytes)\n",
                  psramFound() ? "OK" : "NEM",
                  (int)ESP.getPsramSize());

    btStop();
    Wire.begin(I2C_SDA, I2C_SCL);
    LittleFS.begin(true, "/littlefs", 10, "littlefs");
    store.begin();
    audioManager.begin(&store);

    uiManager = new UIManager(audioManager, networkManager, bellManager, store);
    uiManager->begin();
    uiManager->setTelemetry(&telemetry);
    bellManager.begin();

    bool needsProv = !store.hasWifi() || !store.hasDeviceKey();
    Serial.printf("[MAIN] needsProv=%d\n", needsProv);

    if (needsProv) startProvisioningMode();
    else           startNormalMode();
}

// ── loop (Core 1) – csak audio és UI ─────────────────────────────────────────
void loop() {
    if (!inProvisioningMode) {
        audioManager.loop();
        uiManager->loop();
    } else {
        uiManager->loop();
        delay(50);
    }
}
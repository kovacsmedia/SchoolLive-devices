#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <ArduinoJson.h>

#include "Config.h"
#include "PersistStore.h"
#include "ProvisioningManager.h"
#include "AudioManager.h"
#include "SLNetworkManager.h"
#include "BellManager.h"
#include "UIManager.h"
#include "BackendClient.h"
#include "DeviceAgent.h"
#include "DeviceTelemetry.h"
#include "SnapcastClient.h"

// --- Globális objektumok ---
SLNetworkManager networkManager;
AudioManager audioManager;
PersistStore store;
BackendClient backend;
BellManager bellManager(audioManager, networkManager, backend);
DeviceAgent agent;
DeviceTelemetry telemetry;
SnapcastClient snapClient;

// --- Pointerek – setup()-ban példányosítjuk ---
UIManager* uiManager = nullptr;
ProvisioningManager* provManager = nullptr;

bool inProvisioningMode = false;

TaskHandle_t TaskNetworkHandle = nullptr;

// --- I2S arbitration callbackok ---

void beforeLocalPlayback() {
    Serial.println("[MAIN] beforeLocalPlayback: pause Snapcast");
    snapClient.pauseForLocalPlayback();
}

void afterLocalPlayback() {
    Serial.println("[MAIN] afterLocalPlayback: resume Snapcast");
    snapClient.resumeAfterLocalPlayback();
}

// --- Snapcast indítás, ha már van backend config ---

void tryStartSnapcastClient() {
    if (!backend.hasSnapConfig()) return;
    if (snapClient.isStarted()) return;

    Serial.println("[MAIN] Starting Snapcast client from backend config");

    snapClient.begin(
        backend.getSnapHost(),
        backend.getSnapPort(),
        backend.getDeviceId(),
        audioManager.getEffectiveVolume()
    );

    snapClient.start();
}

// --- Induláskori beacon, hogy a Snapcast config még poll előtt meglegyen ---

void sendInitialBeaconAndStartSnapcast() {
    JsonDocument status;
    status["boot"] = true;
    status["mode"] = "normal";
    status["ip"] = WiFi.localIP().toString();
    status["mac"] = WiFi.macAddress();
    status["snapcast"] = "requested";

    Serial.println("[MAIN] Sending initial beacon before polling");

    bool ok = backend.sendBeacon(
        audioManager.getVolume(),
        false,
        String(FW_VERSION),
        status
    );

    if (!ok) {
        Serial.println("[MAIN] Initial beacon failed, Snapcast config not available yet");
        return;
    }

    tryStartSnapcastClient();
}

// --- Provisioning task (core 0) ---

void TaskProvisioning(void* pvParameters) {
    (void)pvParameters;

    uiManager->enterProvisioningMode();

    for (;;) {
        provManager->loop();

        ProvState state = provManager->getState();
        String mac = provManager->getMac();
        String ip = provManager->getIP();

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

void TaskNetwork(void* pvParameters) {
    (void)pvParameters;

    networkManager.begin();

    for (;;) {
        networkManager.loop();

        tryStartSnapcastClient();

        /*
         * Lejátszás közben:
         * - nincs polling;
         * - nincs beacon;
         * - nincs bell sync;
         * - nem futtatunk semmilyen extra HTTP kérést.
         *
         * Ekkor a Snapcast hálózati task és az audio task kap prioritást.
         */
        if (agent.isPlaybackQuietActive()) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        /*
         * Ha Snapcast kapcsolat él, a bell syncet akkor sem futtatjuk.
         * A hangos üzemmódot Snapcast viszi, az offline bell sync ráér,
         * vagy csak akkor fontos, ha nincs Snapcast kapcsolat.
         */
        bool snapConnected = snapClient.isConnected();

        /*
         * Polling csak akkor megy, ha éppen nincs védett lejátszási időszak.
         * A pollingból jön a célzott régi kompatibilitási parancs,
         * amit ACK-olunk, majd DeviceAgent playback quiet módba lép.
         */
        agent.loop();

        tryStartSnapcastClient();

        /*
         * Bell sync / offline bell kezelés csak akkor,
         * ha nincs Snapcast kapcsolat.
         */
        if (!snapConnected) {
            if (!audioManager.isBusy() && !audioManager.isInCooldown()) {
                bellManager.loop();
            }
        }

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

    /*
     * Kritikus:
     * még az agent.begin() és a polling előtt kérünk Snapcast configot,
     * majd elindítjuk a Snapcast klienst.
     */
    sendInitialBeaconAndStartSnapcast();

    agent.begin(networkManager, audioManager, *uiManager, backend, telemetry);
    agent.setFirmwareVersion(String(FW_VERSION));

    // A UI top-bar állapotjelzőihez (S kör = snap connected, MESSAGE/RADIO/SIGNAL
    // villogás aktív lejátszás esetén) szükségesek a DeviceAgent és a
    // SnapcastClient pointerek.
    uiManager->setAgent(&agent);
    uiManager->setSnapClient(&snapClient);

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

    /*
     * I2S arbitration:
     * helyi/offline csengetés idejére a Snapcast engedje el az I2S-t.
     */
    audioManager.setI2SCallbacks(
        beforeLocalPlayback,
        afterLocalPlayback
    );

    /*
     * Volume‐láncolás:
     * a manuális hangerő (gombnyomás vagy backend SET_VOLUME parancs) és az
     * emergency override is azonnal érvényesüljön a Snapcast streamen.
     * AudioManager az effective volume-ot (override vagy manual) küldi át.
     */
    audioManager.setVolumeChangedCallback([](uint8_t effectiveVol) {
        snapClient.setLocalVolume(effectiveVol);
    });

    uiManager = new UIManager(audioManager, networkManager, bellManager, store);
    uiManager->begin();
    uiManager->setTelemetry(&telemetry);

    bellManager.begin();

    bool hasWifi = store.hasWifi();
    bool hasKey = store.hasDeviceKey();
    bool needsProv = !hasWifi || !hasKey;

    Serial.printf(
        "[MAIN] hasWifi=%d hasKey=%d needsProv=%d\n",
        hasWifi,
        hasKey,
        needsProv
    );

    if (needsProv) {
        startProvisioningMode();
    } else {
        startNormalMode();
    }
}

// --- LOOP (core 1) ---

void loop() {
    if (!inProvisioningMode) {
        /*
         * A Snapcast audio külön taskban fut.
         * Itt csak UI és offline AudioManager loop marad.
         */
        audioManager.loop();
        uiManager->loop();
    } else {
        uiManager->loop();
        delay(50);
    }
}
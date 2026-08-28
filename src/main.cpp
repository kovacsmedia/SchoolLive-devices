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
#include "WsClient.h"
#include "DeviceAgent.h"
#include "DeviceTelemetry.h"
#include "SnapcastClient.h"
#include "OtaManager.h"

extern "C" {
#include "snap_app.h"
}

// --- Globális objektumok ---
SLNetworkManager networkManager;
AudioManager audioManager;
PersistStore store;
BackendClient backend;
WsClient wsClient;
BellManager bellManager(audioManager, networkManager, backend);
DeviceAgent agent;
DeviceTelemetry telemetry;
SnapcastClient snapClient;
OtaManager otaManager;

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

// --- WS kapcsolat indítása (beacon és poll helyett) ---

// Multi-node cluster: a legutóbb ismert (cache-elt) node host-ot használjuk,
// ha van; egyébként a Config.h BACKEND_BASE_URL-ből származtatott alapértelmezett
// hostra esünk vissza (a "https://" séma-előtag levágásával).
String resolveWsHost() {
    if (store.hasCachedNodeHost()) {
        return store.getCachedNodeHost();
    }
    String base = String(BACKEND_BASE_URL);
    int schemeEnd = base.indexOf("://");
    return schemeEnd >= 0 ? base.substring(schemeEnd + 3) : base;
}

void startWsConnection(const String& deviceKey) {
    // wss://<host>:443/sync?deviceKey=<key>
    wsClient.begin(resolveWsHost(), 443, deviceKey);

    wsClient.onMessage([](const JsonDocument& msg) {
        agent.onWsMessage(msg);
    });

    // Multi-node cluster: N sikertelen újracsatlakozás után megkérdezzük a
    // backendet (a jelenlegi, esetleg elavult host felé – a /cluster/locate
    // tulajdonjog-kapu nélküli, bármelyik node válaszol rá), hogy a tenant
    // időközben másik node-ra került-e.
    wsClient.onNeedsRelocate([deviceKey]() {
        String tenantId = store.getTenantId();
        if (tenantId.length() == 0) {
            Serial.println("[MAIN] Relocate szükséges lenne, de nincs tárolt tenantId — kihagyva");
            return;
        }

        String newHost;
        if (backend.locateNode(tenantId, newHost)) {
            String current = resolveWsHost();
            if (newHost != current) {
                Serial.printf("[MAIN] Node-váltás: %s → %s\n", current.c_str(), newHost.c_str());
                store.setCachedNodeHost(newHost);
            } else {
                Serial.println("[MAIN] locateNode: jelenlegi host továbbra is érvényes");
            }
            // Mindig újraindítjuk a WS kapcsolatot (akár változott a host, akár
            // nem), hogy a WebSocketsClient és a backoff-számláló tiszta legyen.
            wsClient.begin(newHost, 443, deviceKey);
        } else {
            Serial.println("[MAIN] locateNode sikertelen — marad a jelenlegi hoston, backoff folytatódik");
        }
    });

    Serial.println("[MAIN] WsClient indítva");
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

        // WS loop – event feldolgozás + automata reconnect
        wsClient.loop();

        tryStartSnapcastClient();

        if (agent.isPlaybackQuietActive()) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        bool snapConnected = snapClient.isConnected();
        bool wsConnected   = wsClient.isConnected();

        // Agent loop: beacon küldés WS-en, playback quiet kezelés
        agent.loop();

        tryStartSnapcastClient();

        // Offline bell lejátszás: csak ha nincs WS kapcsolat ÉS nincs Snapcast
        if (!wsConnected && !snapConnected) {
            if (!audioManager.isBusy() && !audioManager.isInCooldown()) {
                bellManager.loop();
            }
        }

        // Ha WS lecsatlakozott, az online módot töröljük → BellManager lokálisan játszhat
        if (!wsConnected) {
            bellManager.setOnlineMode(false);
        }

        otaManager.loop();

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

    // WS kapcsolat indítása – a HELLO üzenetből érkezik majd a Snapcast konfig
    startWsConnection(dk);

    // Mentett csatorna-mód alkalmazása (a snap_app_start után kell)
    {
        dsp_channel_mode_t cm = store.getChannelMode();
        snap_app_set_channel_mode(cm);
        Serial.printf("[MAIN] Channel mode loaded from NVS: %d\n", (int)cm);
    }

    agent.begin(networkManager, audioManager, *uiManager, backend, telemetry, wsClient, bellManager, snapClient, store, otaManager);
    agent.setFirmwareVersion(String(FW_VERSION));

    // A UI top-bar állapotjelzőihez (S kör = snap connected, MESSAGE/RADIO/SIGNAL
    // villogás aktív lejátszás esetén) szükségesek a DeviceAgent és a
    // SnapcastClient pointerek.
    uiManager->setAgent(&agent);
    uiManager->setSnapClient(&snapClient);

    // OTA manager - 30 percenként ellenőrzi a backend-en az új release-eket,
    // és ha mandatory frissítés érhető el, automatikusan flash-eli.
    // A snap stream először leáll, hogy a flash közben semmi se ütközzön.
    otaManager.begin(
        networkManager,
        backend,
        snapClient,
        telemetry,
        String(FW_VERSION),
        "SPEAKER",
        "ESP32_S3"
    );

    xTaskCreatePinnedToCore(
        TaskNetwork,
        "NetworkTask",
        16000,
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
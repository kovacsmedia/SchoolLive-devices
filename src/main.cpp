#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <ArduinoJson.h>

#include "Config.h"
#include <esp_heap_caps.h>

// Összeomlás-morzsa a snap lejátszóból (components/lightsnapcast/player.c).
// A fejlécet szándékosan nem húzzuk be – I2S/snapcast típusokat vonzana ide.
extern "C" void     player_mark(int mark);
extern "C" uint32_t player_last_crash_mark(void);
extern "C" uint32_t player_last_crash_uptime(void);
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
#include "ServiceButton.h"

extern "C" {
#include "snap_app.h"
}

// --- Globális objektumok ---
SLNetworkManager networkManager;
AudioManager audioManager;
PersistStore store;
ServiceButton serviceButton;
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
    player_mark(16 /* APP_MARK_NET_SNAPSTART */);
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
        player_mark(15 /* APP_MARK_NET_WS */);
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

        // A backend akkor és csak akkor tudja lejátszani a csengetést, ha
        // MINDKETTŐ él: a WS (a backend folyamat hajtja a mixert) ÉS a
        // snapclient-kapcsolat (azon jön a hang). Korábban a feltétel
        // `!ws && !snap` volt, azaz MINDKETTŐNEK el kellett esnie ahhoz, hogy
        // az eszköz helyben csengessen – csakhogy a snapserver KÜLÖN PM2
        // processz, így egy backend-deploy/összeomlás alatt a snapclient
        // kapcsolat élve maradt, az eszköz "online"-nak hitte magát, és a
        // csengetés SEHOL nem szólalt meg.
        player_mark(17 /* APP_MARK_NET_BELLS */);
        bellManager.setBackendReachable(wsConnected && snapConnected);

        // A csengetés-figyelő MINDIG fut; hogy kell-e helyben lejátszani, azt
        // a BellManager::checkSchedule() dönti el (T-60 mp-es előellenőrzés +
        // BELL PREPARE bizonyíték + türelmi idő). Teljesen offline állapotban
        // a `loop()`-ot hívjuk, mert az a HTTP ütemezés-szinkront is elvégzi.
        if (!audioManager.isBusy() && !audioManager.isInCooldown()) {
            if (!wsConnected && !snapConnected) {
                bellManager.loop();
            } else {
                bellManager.checkBells();
            }
        }

        player_mark(18 /* APP_MARK_NET_OTA */);
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

    uiManager->drawBootStatus("System check", "WiFi + ido");

    // NEM várunk a WiFire.
    //
    // Korábban a `syncTimeBlocking()` akár 30 mp-ig várt a kapcsolatra, majd
    // sikertelenség esetén további 3 mp-ig hibaképernyőt mutatott – és mivel a
    // `_timeSynced` hamis maradt, a BellManager EGYÁLTALÁN NEM CSENGETETT,
    // amíg a WiFi vissza nem jött. Egy elérhetetlen iskolai hálózat mellett
    // újrainduló eszköz így néma maradt, ami a "jelzés nem maradhat el"
    // szabályt sérti.
    //
    // Mostantól: a csatlakozás elindul a háttérben (a TaskNetwork
    // handleWiFi()-je 10 mp-enként újrapróbálja), az idő pedig – szoftveres
    // újraindítás után – az RTC-ből azonnal átvehető, tehát a csengetés
    // működik. Amint a WiFi megjön, az NTP pontosít, a WS csatlakozik, a
    // backend elküldi a SCHEDULE_SYNC-et, és az eszköz magától online módba
    // vált (ld. BellManager::checkSchedule "backend elérhető" szabálya).
    networkManager.begin();
    networkManager.startConnect();

    const bool timeOk = networkManager.adoptRtcTimeIfValid();
    uiManager->drawBootStatus(
        timeOk ? "Ido: RTC OK" : "Ido: NTP-re var",
        timeOk ? "Offline csenges kesz" : "WiFi keresese...");
    delay(300);

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

    // A fájlrendszer tényleges mérete és kihasználtsága. A "Cannot open for
    // write" hibák oka jellemzően ez: tele van, vagy a partíció-geometria
    // eltér a feltöltött LittleFS képtől (ilyenkor a mount sikerül, de a
    // méret nem az, amit a partitions.csv mond → `pio run -t uploadfs`).
    {
        const size_t tot = LittleFS.totalBytes();
        const size_t use = LittleFS.usedBytes();
        Serial.printf("[FS] LittleFS: %u / %u bajt hasznalva (%u szabad)\n",
                      (unsigned)use, (unsigned)tot,
                      (unsigned)(tot > use ? tot - use : 0));
    }

    // ── Memória-riport indulaskor ──────────────────────────────────────────
    // A szűk keresztmetszet a BELSŐ DRAM (~320 kB), nem a PSRAM (8 MB). Ez a
    // sor azonnal megmutatja, hogy a PSRAM egyáltalán felállt-e: ha nem, a
    // JSON-fák és a nagy pufferek visszaesnek a belső heap-re (működik, csak
    // szűkösebben – ld. PsramJson.h).
    {
        const size_t iFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const size_t iBlk  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const size_t pTot  = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        const size_t pFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

        Serial.printf("[MEM] Belso DRAM: %u szabad, legnagyobb blokk %u | PSRAM: %u / %u\n",
                      (unsigned)iFree, (unsigned)iBlk, (unsigned)pFree, (unsigned)pTot);

        if (pTot == 0) {
            Serial.println("[MEM] FIGYELEM: nincs elerheto PSRAM – minden foglalas a belso DRAM-bol megy");
        }
    }

    // ── Összeomlás-morzsa az ELŐZŐ futásból ────────────────────────────────
    // Az RTC memória túléli a pánik utáni újraindulást, ezért megmondja, hol
    // járt a snap lejátszó-út, amikor az eszköz összeomlott. Ez a soros
    // monitor kiváltása: nem kell ott ülni a pánik pillanatában.
    {
        const uint32_t mark = player_last_crash_mark();
        if (mark != 0) {
            static const char* MARK_NAMES[] = {
                "-", "player_task belepes", "i2s beallitas", "timer init",
                "timer start", "timer ISR", "beallitas-valtozas",
                "chunk-varakozas", "chunk feldolgozas", "i2s iras",
                "task kilepes", "insert_pcm_chunk", "start_player",
                "loop: audio", "loop: UI", "net: WS", "net: snap indit",
                "net: csengetes", "net: OTA", "beacon", "WS uzenet"
            };
            const char* name = (mark < (sizeof(MARK_NAMES)/sizeof(MARK_NAMES[0])))
                             ? MARK_NAMES[mark] : "ismeretlen";
            Serial.printf("[CRASH] Elozo futas utolso pontja: %s (#%u), %u mp uzemido utan\n",
                          name, (unsigned)mark, (unsigned)player_last_crash_uptime());
        }
    }

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

    // ── Szervizgomb ───────────────────────────────────────────────────────
    // A mód-döntés ELŐTT indul, hogy provisioning módban is működjön (pl. egy
    // félbemaradt aktiválás után is lehessen újraindítani az eszközt).
    serviceButton.begin(BTN_SERVICE, BTN_FACTORY_RESET_MS);

    serviceButton.onShortPress([]() {
        Serial.println("[MAIN] Szervizgomb: ujrainditas");
        Serial.flush();
        delay(100);
        ESP.restart();
    });

    serviceButton.onLongPress([]() {
        // Aktiválás előtti állapot: a Preferences teljes törlése. Ezzel
        // `hasWifi` és `hasKey` is hamis lesz, tehát a következő induláskor a
        // setup() provisioning módot választ – az pedig a Config.h-beli
        // szerviz-WiFire (PROV_WIFI_SSID = "MP") csatlakozik, és várja az
        // aktiválást. A letöltött csengetőhangok (LittleFS) SZÁNDÉKOSAN
        // megmaradnak: az eszköz így az újraaktiválásig is tud csengetni.
        Serial.println("[MAIN] Szervizgomb: GYARI VISSZAALLITAS (provisioning mod)");
        store.factoryReset();
        Serial.flush();
        delay(200);
        ESP.restart();
    });

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
    // Szervizgomb – MINDKÉT módban (normál és provisioning) figyeljük.
    serviceButton.loop();

    if (!inProvisioningMode) {
        /*
         * A Snapcast audio külön taskban fut.
         * Itt csak UI és offline AudioManager loop marad.
         */
        player_mark(13 /* APP_MARK_LOOP_AUDIO */);
        audioManager.loop();
        player_mark(14 /* APP_MARK_LOOP_UI */);
        uiManager->loop();
    } else {
        uiManager->loop();
        delay(50);
    }
}
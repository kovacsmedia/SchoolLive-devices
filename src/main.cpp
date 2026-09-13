#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <ArduinoJson.h>

#include "Config.h"
#include <esp_log.h>

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

/*
 * ── BERAGADÁS-FELÜGYELET (hang watchdog) ───────────────────────────────────
 *
 * MIÉRT KELL: az ESP-IDF Task Watchdogja nálunk `CONFIG_ESP_TASK_WDT_PANIC`
 * NÉLKÜL futott, azaz elsüléskor CSAK NAPLÓZOTT – a beragadt eszköz beragadva
 * maradt. Ráadásul a TWDT alapból csak az idle taskokat figyeli: ha a
 * `TaskNetwork` egy mutexen vagy egy socketen blokkol örökre, az idle task
 * továbbra is fut, tehát a TWDT észre sem veszi.
 *
 * Egy csengetőrendszernél a beragadás ELFOGADHATATLAN: inkább induljon újra
 * 10 másodperc alatt, mint hogy egy teljes tanítási napon át néma maradjon.
 *
 * MEGOLDÁS: a két periodikus szál KERESZTBE figyeli egymást. Mindkettő lép
 * egy számlálót minden körben; a másik ellenőrzi, hogy az változik-e. Ha
 * valamelyik HANG_TIMEOUT_MS-ig nem mozdul, az eszköz újraindul, és az RTC
 * morzsába bejegyzi, MELYIK szál ragadt be – így a következő induláskor
 * (és a beaconben) látszik az ok.
 *
 * A küszöb SZÁNDÉKOSAN bőkezű: az OTA-letöltés percekig a hálózati szálban
 * tartja a vezérlést. Azt az `OtaManager` progress-visszahívása eteti
 * (`slHeartbeatNet()`), de a tartalék így is nagy.
 */
static const uint32_t HANG_TIMEOUT_MS = 90000UL;

volatile uint32_t slNetTick  = 0;   // TaskNetwork körszámláló
volatile uint32_t slLoopTick = 0;   // Arduino loop() körszámláló

void slHeartbeatNet() { slNetTick = slNetTick + 1; }

/** A beragadt szál nevét kiírjuk a soros portra, majd újraindulunk. */
static void slHangReboot(const char* who) {
    Serial.printf("[HANG] %s nem lepett %lu ms-ig – ujrainditas\n",
                  who, (unsigned long)HANG_TIMEOUT_MS);
    Serial.flush();
    delay(50);          // hogy a soros kiírás biztosan kimenjen
    ESP.restart();
}

/*
 * ── NAPLÓZÁS KAPUZÁSA USB-JELENLÉT SZERINT ────────────────────────────────
 *
 * Két KÜLÖNBÖZŐ kimenetünk van, és csak az egyik olcsó:
 *
 *   • Az Arduino `Serial` ezen a buildon NATÍV USB CDC (ARDUINO_USB_CDC_ON_BOOT).
 *     Ha nincs host, a HWCDC::write nem blokkol – csak forgat egy gyűrűpuffert
 *     és visszatér. Mikroszekundumok, elhanyagolható.
 *
 *   • Az ESP-IDF `ESP_LOGx` viszont a UART0-ra megy (CONFIG_ESP_CONSOLE_UART,
 *     115200 baud), és a UART AKKOR IS kitolja a biteket a drótra, ha a világon
 *     senki nem olvassa. Ez valódi, folyamatos munka: 115200 baud ~11,5 kB/mp,
 *     és ha a napló ennél többet termel, a TX FIFO megtelik, a hívó szál pedig
 *     VÁR. Pontosan ez történt a resync-hullámban (~500 sor 5 mp alatt).
 *
 * Ezért: ha nincs USB-host, az IDF naplózást teljesen elnémítjuk. Csatlakoztatott
 * USB-nél visszakapcsol – a laptopos bench-tesztnél minden látszik, éles
 * üzemben viszont nulla a költsége.
 *
 * A `Serial` bool-operátora a HWCDC kapcsolat-állapotát adja. Az `isPlugged()`
 * néhány ms toleranciájú SOF-figyelőn alapul és tud pillanatnyilag "villódzni",
 * ezért csak másodpercenként nézzük, és csak VÁLTÁSKOR nyúlunk a szinthez.
 */
static void slUpdateLogGating() {
    static bool     enabled   = true;
    static uint32_t lastMs    = 0;

    const uint32_t now = millis();

    // Az első percben SOHA nem némítunk: indulási hibát vadászni napló nélkül
    // reménytelen, és a USB-enumeráció is eltarthat pár másodpercig.
    if (now < 60000UL) return;

    if ((uint32_t)(now - lastMs) < 1000) return;
    lastMs = now;

    // A HWCDC bool-operátora az isCDC_Connected()-et adja vissza, ami a
    // host SOF-csomagjain alapul: igaz, ha van enumerált USB-host – akkor is,
    // ha épp nincs megnyitva a soros monitor.
    // STABIL mérés kell: a HWCDC `isPlugged()` néhány ms toleranciájú
    // SOF-figyelőn alapul, és a keretrendszer saját kommentje szerint
    // "ép kapcsolaton is tud pillanatnyilag hamisra billenni". Egy ilyen
    // villanásra NEM némítunk el mindent – öt egymás utáni azonos mérés kell.
    static uint8_t stable = 0;
    const bool usb = (bool)Serial;
    if (usb == enabled) { stable = 0; return; }
    if (++stable < 5) return;
    stable = 0;

    enabled = usb;
    if (!usb) Serial.println("[LOG] Nincs USB host – ESP-IDF naplozas KI");
    // A CONFIG_LOG_MAXIMUM_LEVEL=3 (INFO) miatt ennél magasabbra nincs értelme.
    esp_log_level_set("*", usb ? ESP_LOG_INFO : ESP_LOG_NONE);
    if (usb) Serial.println("[LOG] USB host eszlelve – ESP-IDF naplozas BE");
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

    // A loop() szál figyeléséhez: utoljára látott érték + mikor változott.
    uint32_t seenLoopTick   = slLoopTick;
    uint32_t seenLoopAtMs   = millis();

    for (;;) {
        slNetTick = slNetTick + 1;

        // A másik szál él-e még?
        if (slLoopTick != seenLoopTick) {
            seenLoopTick = slLoopTick;
            seenLoopAtMs = millis();
        } else if ((uint32_t)(millis() - seenLoopAtMs) > HANG_TIMEOUT_MS) {
            slHangReboot("Fo loop()");
        }

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

        // A backend akkor és csak akkor tudja lejátszani a csengetést, ha
        // MINDKETTŐ él: a WS (a backend folyamat hajtja a mixert) ÉS a
        // snapclient-kapcsolat (azon jön a hang). Korábban a feltétel
        // `!ws && !snap` volt, azaz MINDKETTŐNEK el kellett esnie ahhoz, hogy
        // az eszköz helyben csengessen – csakhogy a snapserver KÜLÖN PM2
        // processz, így egy backend-deploy/összeomlás alatt a snapclient
        // kapcsolat élve maradt, az eszköz "online"-nak hitte magát, és a
        // csengetés SEHOL nem szólalt meg.
        bellManager.setBackendReachable(wsConnected && snapConnected);

        // A csengetés-figyelő MINDIG fut; hogy kell-e helyben lejátszani, azt
        // a BellManager::checkSchedule() dönti el (T-60 mp-es előellenőrzés +
        // BELL PREPARE bizonyíték + türelmi idő). Teljesen offline állapotban
        // a `loop()`-ot hívjuk, mert az a HTTP ütemezés-szinkront is elvégzi.
        /*
         * A csengetés-ÁLLAPOTGÉP MINDIG fut – lejátszás és cooldown alatt is.
         *
         * Korábban az egész blokk `!isBusy() && !isInCooldown()` mögött volt.
         * Egy helyi csengetés (~8 s) + a 10 s-os EOF-cooldown alatt tehát a
         * checkSchedule() ~18-20 másodpercig VAK volt, miközben az online
         * bizonyíték ablaka csak 15 s. Így a backend által már elcsengetett
         * jelzést az eszköz utólag MÉG EGYSZER lejátszotta helyben – az pedig
         * újabb cooldownt indított, és a következő csengetés is ugyanígy járt.
         *
         * Most az állapotgép fut; azt, hogy szabad-e ténylegesen lejátszani,
         * a checkSchedule() dönti el (ott van a busy/cooldown vizsgálat).
         * A `loop()` HTTP-szinkron ága viszont továbbra is várhat: az drága,
         * és lejátszás közben nem szabad a hálózatot terhelnie.
         */
        /*
         * OFFLINE = NINCS SNAP ÉS/VAGY NINCS WS — nem az, hogy nincs WiFi.
         *
         * A HTTP ütemezés-szinkront futtató `loop()` eddig csak TELJESEN
         * offline állapotban futott (`!ws && !snap`). Csakhogy a csengetés
         * szempontjából már az is offline, ha a KETTŐ KÖZÜL BÁRMELYIK hiányzik
         * (ld. setBackendReachable fent) – ilyenkor a rendet nekünk kell
         * frissen tartanunk, mert a backend nem tudja lepusholni.
         */
        const bool backendReachable = wsConnected && snapConnected;

        if (!backendReachable) {
            if (!audioManager.isBusy() && !audioManager.isInCooldown()) {
                bellManager.loop();
            } else {
                bellManager.checkBells();
            }
        } else {
            bellManager.checkBells();
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

    // Csengetés-védelem: az OTA nem indulhat, ha a következő jelzés 5 percen
    // belül esedékes (ld. OTA_BELL_GUARD_S). A frissítés alatt az eszköz ~2
    // percig néma – még a helyi, offline csengetés sem szólal meg.
    otaManager.setBellManager(bellManager);

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
    /*
     * A `setTxTimeoutMs(0)` IDE VOLT BEÍRVA, és VISSZAVETTEM (2026-09-12).
     *
     * Az indoka jó volt: a HWCDC alapból akár 20 x 100 ms-ot is vár, ha a host
     * nem olvas, és egy naplósor így 2 másodpercre megállíthatja a hívó szálat.
     * A 0 viszont a MÁSIK irányba téved: nulla várakozással a sorok NÉMÁN
     * elvesznek, amint a gyűrűpuffer egy pillanatra megtelik – és pont azokat
     * veszítenénk el, amikre a hibakereséshez szükség van.
     *
     * Amíg az eszköz padon, kábelen lóg, a megbízható napló többet ér.
     * A blokkolás elleni védelem a naplófojtás (ld. player.c) és a
     * beragadás-felügyelet, nem a kimenet eldobása.
     */
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
    slLoopTick = slLoopTick + 1;
    slUpdateLogGating();

    // A hálózati szál él-e még? (Provisioning módban nem fut, ott nem nézzük.)
    static uint32_t seenNetTick = 0;
    static uint32_t seenNetAtMs = 0;
    if (!inProvisioningMode) {
        if (seenNetAtMs == 0 || slNetTick != seenNetTick) {
            seenNetTick = slNetTick;
            seenNetAtMs = millis();
        } else if ((uint32_t)(millis() - seenNetAtMs) > HANG_TIMEOUT_MS) {
            slHangReboot("TaskNetwork");
        }
    } else {
        seenNetAtMs = 0;
    }

    // Szervizgomb – MINDKÉT módban (normál és provisioning) figyeljük.
    serviceButton.loop();

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
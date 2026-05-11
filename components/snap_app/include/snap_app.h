/**
 * snap_app.h - SchoolLive Snapcast client public C API.
 *
 * Refaktorált változata a CarlosDerSeher/snapclient `main.c` monolitnak.
 * Az eredeti `app_main()` belépési pont helyett ez a komponens egy
 * paraméterezhető API-t exportál, amit az Arduino oldali adapter osztály
 * (`src/SnapcastClient.cpp`) hív.
 *
 * Kihagyva (a CarlosDerSeher main.c-ből, mert duplikálná a saját rétegeinket):
 *   - settings_manager  (sajátunk: PersistStore/NVS)
 *   - ui_http_server    (sajátunk: backend HTTPS API)
 *   - websocket(_if)
 *   - ota_server        (sajátunk: BackendClient + HTTPUpdate)
 *   - improv_wifi       (sajátunk: ProvisioningManager + BLE)
 *   - tas5805m_settings (MAX98357A-t használunk, nem TAS5805M)
 *   - sntp / net_mdns   (sajátunk: NTP Arduino SLNetworkManager-ben)
 *   - nvs_flash_init    (Arduino oldal már megtette)
 *   - network_if_init   (Arduino oldal csinálja a WiFi-t)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /** Snap szerver hosztneve (pl. "api.schoollive.hu"). */
    const char *host;

    /** Snap szerver portja (pl. 1802). */
    uint16_t port;

    /** Egyedi eszközazonosító, a snap HELLO ID mezőjébe kerül. */
    const char *device_id;

    /** MAX98357A I2S pinek. Negatív érték = unused (pl. MCLK). */
    int8_t bclk_pin;
    int8_t lrc_pin;   // word select / LRC
    int8_t din_pin;   // data out (DIN a DAC oldalán)
    int8_t mclk_pin;  // -1 ha nincs (MAX98357A nem igényli)
} snap_app_config_t;

/**
 * Elindítja a snap klienst.
 *
 * Előfeltétel: WiFi már csatlakozott (Arduino oldal csinálja).
 * Az alábbi belső lépések történnek:
 *   1. lightsnapcast init (üzenet queue)
 *   2. player init (I2S beállítás a cfg pinekkel)
 *   3. dsp_processor init (drift kompenzáció)
 *   4. http_get_task indítás (snap TCP + protokoll)
 *   5. dac_control_task indítás (PCM -> I2S)
 *
 * A funkció nem blokkol — a két task külön fut FreeRTOS schedulerben.
 *
 * @return ESP_OK ha sikerült, hiba egyébként.
 */
esp_err_t snap_app_start(const snap_app_config_t *cfg);

/**
 * Leállítja a snap klienst, felszabadítja az I2S-t és a kapcsolódó
 * erőforrásokat. Hívható csak akkor, ha snap_app_start() előzőleg
 * sikeresen lefutott.
 */
esp_err_t snap_app_stop(void);

/**
 * Helyi hangerő beállítás (0..100). A snap szerver oldali hangerővel
 * a player.c kombinálja effektív hangerőre.
 */
esp_err_t snap_app_set_local_volume(uint8_t volume);

/**
 * Pause/resume — helyi MP3 csengetés idejére az I2S-t átengedi
 * az AudioManager-nek, majd visszakapja.
 */
esp_err_t snap_app_pause(void);
esp_err_t snap_app_resume(void);

/**
 * Állapotlekérdezés. true ha a két task fut és a snap szerverrel
 * van TCP kapcsolat.
 */
bool snap_app_is_running(void);
bool snap_app_is_connected(void);

#ifdef __cplusplus
}
#endif

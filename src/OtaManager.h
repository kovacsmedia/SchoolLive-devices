#pragma once

#include <Arduino.h>
#include "BackendClient.h"

class SLNetworkManager;
class BellManager;
class SnapcastClient;
class AudioManager;
struct DeviceTelemetry;

/**
 * OTA-frissítések kezelője.
 *
 * Feladata:
 *  - rendszeresen (CHECK_INTERVAL_MS-onként) lekérdezi a backend
 *    /firmware/check endpoint-ját az aktuális verzióval,
 *  - ha új release érhető el ÉS mandatory=true (vagy később admin command alapján),
 *    leállítja a snap audio task-ot, letölti és felflasheli az új firmware-t,
 *  - minden átmenetnél jelez a backend /firmware/ota-status endpoint-ra,
 *  - sikeres flash után ESP.restart() (az új partícióról indul a v5+).
 *
 * NEM csinálunk SHA256 verify-t a httpUpdate könyvtár belül — egyébként
 * az MD5 verifikáció már része a HTTPUpdate lib-nek (`updateMD5`).
 *
 * Az OTA letöltés alatt a snap audio CSEND lesz, mert az OTA folyamat
 * blokkoló a TaskNetwork-ön belül. A felhasználó hangtelenül érzékel egy
 * ~30-60 másodperces kihagyást, majd a reboot után visszatér a stream.
 */
// Ennyivel a következő csengetés előtt már NEM indítunk OTA-t. A frissítés
// tipikusan ~2 perc (1,6 MB HTTPS-en + újraindulás), tehát 5 perc kényelmes
// tartalékot ad egy lassabb hálózatra is.
#define OTA_BELL_GUARD_S 300

class OtaManager {
public:
    OtaManager() = default;

    void begin(
        SLNetworkManager& net,
        BackendClient&    backend,
        SnapcastClient&   snap,
        DeviceTelemetry&  tel,
        const String&     currentVersion,
        const String&     deviceClass = "SPEAKER",
        const String&     hwModel     = "ESP32_S3"
    );

    /** A TaskNetwork loop-jából hívjuk, ütemezi a check-eket. */
    /**
     * Csengetés-védelem bekötése (opcionális).
     *
     * Az OTA alatt az eszköz ~2 percig NEM tud hangot adni: a flash írása és
     * az újraindulás alatt még a helyi, offline csengetés sem szólal meg. Ha
     * egy beállított jelzés ebbe az ablakba esik, KIMARAD – ami a rendszer
     * alapszabályát sértené.
     *
     * Ezért ha a következő csengetés BELL_GUARD_S-en belül esedékes, a
     * frissítés elhalasztódik a következő ellenőrzési körre.
     *
     * Ha nincs beállítva, a védelem egyszerűen nem működik – a frissítés
     * ilyenkor is elindul, tehát a bekötés hiánya nem bénítja meg az OTA-t.
     */
    void setBellManager(BellManager& bells) { _bells = &bells; }

    void loop();

    /** Explicit OTA kényszerítés (pl. admin command-ról). */
    void forceCheckNow();

private:
    BellManager*      _bells = nullptr;
    SLNetworkManager* _net      = nullptr;
    BackendClient*    _backend  = nullptr;
    SnapcastClient*   _snap     = nullptr;
    DeviceTelemetry*  _tel      = nullptr;

    String _currentVersion;
    String _deviceClass;
    String _hwModel;

    bool          _enabled       = false;
    unsigned long _lastCheckMs   = 0;
    bool          _updateRunning = false;

    // 30 perces auto-check intervallum. Első check a `begin()` után
    // ~60 másodperc múlva fut (hagyjuk a snap stream-et beállni).
    static constexpr unsigned long CHECK_INTERVAL_MS = 30UL * 60UL * 1000UL;
    static constexpr unsigned long FIRST_CHECK_DELAY_MS = 60UL * 1000UL;

    void runCheckAndMaybeUpdate();
    void performUpdate(const FirmwareCheckResult& fw);
};

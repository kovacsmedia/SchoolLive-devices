#include "OtaManager.h"

#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

#include "SLNetworkManager.h"
#include "SnapcastClient.h"
#include "DeviceTelemetry.h"

void OtaManager::begin(
    SLNetworkManager& net,
    BackendClient&    backend,
    SnapcastClient&   snap,
    DeviceTelemetry&  tel,
    const String&     currentVersion,
    const String&     deviceClass,
    const String&     hwModel
) {
    _net            = &net;
    _backend        = &backend;
    _snap           = &snap;
    _tel            = &tel;
    _currentVersion = currentVersion;
    _deviceClass    = deviceClass;
    _hwModel        = hwModel;

    // Az első check eltolva, hogy a snap stream felálljon és ne ütközzön.
    _lastCheckMs = millis() - CHECK_INTERVAL_MS + FIRST_CHECK_DELAY_MS;
    _enabled = true;

    Serial.printf(
        "[OTA] OtaManager indult: current='%s' class='%s' hwModel='%s' "
        "checkInterval=%lu ms\n",
        _currentVersion.c_str(), _deviceClass.c_str(), _hwModel.c_str(),
        (unsigned long)CHECK_INTERVAL_MS
    );
}

void OtaManager::loop() {
    if (!_enabled || _updateRunning) return;

    unsigned long now = millis();
    if ((unsigned long)(now - _lastCheckMs) < CHECK_INTERVAL_MS) return;

    _lastCheckMs = now;
    runCheckAndMaybeUpdate();
}

void OtaManager::forceCheckNow() {
    if (!_enabled || _updateRunning) return;
    _lastCheckMs = millis();
    runCheckAndMaybeUpdate();
}

void OtaManager::runCheckAndMaybeUpdate() {
    if (!_net || !_net->isConnected()) {
        Serial.println("[OTA] WiFi nincs, check skip");
        return;
    }
    if (!_backend || !_backend->isReady()) {
        Serial.println("[OTA] BackendClient nincs ready, check skip");
        return;
    }

    Serial.printf("[OTA] /firmware/check (current=%s)\n", _currentVersion.c_str());

    FirmwareCheckResult fw;
    if (!_backend->checkFirmware(_currentVersion, _deviceClass, _hwModel, fw)) {
        Serial.println("[OTA] check failed");
        return;
    }

    if (!fw.updateAvailable) {
        Serial.println("[OTA] Up to date");
        return;
    }

    Serial.printf(
        "[OTA] Update available: %s -> %s (size=%d, mandatory=%d) %s\n",
        _currentVersion.c_str(), fw.version.c_str(),
        fw.sizeBytes, fw.mandatory ? 1 : 0,
        fw.url.c_str()
    );

    // Most csak a mandatory release-eket telepítjük automatikusan. A nem-
    // kötelező frissítéseket a backend admin UI tudja triggerelni egy
    // explicit "UPDATE_FIRMWARE" device command-on, vagy később felhasználói
    // beleegyezéssel; egyelőre ezeket csak loggoljuk.
    if (!fw.mandatory) {
        Serial.println("[OTA] Not mandatory, skip auto-install");
        return;
    }

    performUpdate(fw);
}

void OtaManager::performUpdate(const FirmwareCheckResult& fw) {
    _updateRunning = true;

    if (!_backend) {
        _updateRunning = false;
        return;
    }

    // Snap stream leállítása, hogy a flash közben ne fogyasszuk az I2S-t,
    // ne ütközzön HTTP forgalom-orientáció szempontból, és a CPU/memóriát
    // a HTTPUpdate kapja.
    if (_snap) {
        Serial.println("[OTA] Snap stop előtt flash...");
        _snap->stop();
    }

    _backend->reportOtaStatus(fw.version, "DOWNLOADING", 0, "");

    WiFiClientSecure client;
    client.setInsecure();          // a backend cert chain a HTTPClient-ben TLS-szel megy
    client.setTimeout(30);          // sec, a connection-handshake-re

    // HTTPUpdate konfiguráció
    httpUpdate.rebootOnUpdate(true);  // sikeres flash után automatikusan restart
    httpUpdate.setLedPin(-1, LOW);    // nincs LED visszajelzés

    // Progress callback - reportOtaStatus 10%-onként
    static int lastReportedPct = -1;
    lastReportedPct = -1;
    httpUpdate.onProgress([this, &fw](int cur, int total) {
        if (total <= 0) return;
        int pct = (int)((int64_t)cur * 100 / total);
        // Csak 10%-onként report-olunk, hogy ne fojtsuk meg a backendet HTTP-vel.
        if (pct / 10 != lastReportedPct / 10) {
            lastReportedPct = pct;
            Serial.printf("[OTA] Progress: %d%% (%d/%d)\n", pct, cur, total);
            if (_backend) {
                _backend->reportOtaStatus(fw.version, "DOWNLOADING", pct, "");
            }
        }
    });

    httpUpdate.onStart([]() {
        Serial.println("[OTA] HTTPUpdate start");
    });

    httpUpdate.onEnd([]() {
        Serial.println("[OTA] HTTPUpdate end");
    });

    httpUpdate.onError([](int err) {
        Serial.printf("[OTA] HTTPUpdate error: %d\n", err);
    });

    // INSTALLING fázis jelzése
    _backend->reportOtaStatus(fw.version, "INSTALLING", 0, "");

    Serial.printf("[OTA] Letöltés indul: %s\n", fw.url.c_str());
    HTTPUpdateResult result = httpUpdate.update(client, fw.url);

    switch (result) {
        case HTTP_UPDATE_FAILED: {
            String err = httpUpdate.getLastErrorString();
            Serial.printf("[OTA] FAILED: %s\n", err.c_str());
            _backend->reportOtaStatus(fw.version, "FAILED", 0, err);
            break;
        }
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] No updates returned");
            _backend->reportOtaStatus(fw.version, "FAILED", 0, "no updates");
            break;
        case HTTP_UPDATE_OK:
            // A rebootOnUpdate=true miatt erre az ágra valószínűleg nem jutunk
            // (a httpUpdate magától restartol). De ha mégis, jelezzük.
            Serial.println("[OTA] HTTP_UPDATE_OK (várhatóan reboot következik)");
            _backend->reportOtaStatus(fw.version, "SUCCESS", 100, "");
            delay(500);
            ESP.restart();
            break;
    }

    _updateRunning = false;
}

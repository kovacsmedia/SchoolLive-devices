#include "SnapcastClient.h"

#include <WiFi.h>

extern "C" {
#include "snap_app.h"
}

SnapcastClient::SnapcastClient() {}

void SnapcastClient::begin(
    const String& host,
    uint16_t port,
    const String& deviceId,
    uint8_t localVolume
) {
    _host = host;
    _port = port;
    _deviceId = deviceId;
    _localVolume = localVolume;

    Serial.printf(
        "[SNAP] begin host=%s port=%u deviceId=%s\n",
        _host.c_str(),
        _port,
        _deviceId.c_str()
    );
}

void SnapcastClient::start() {
    if (_started) return;

    if (_host.length() == 0 || _port == 0 || _deviceId.length() == 0) {
        Serial.println("[SNAP] start skipped: incomplete config");
        return;
    }

    // A snap_app connection_handler.c IP-cím-string-et vár (ipaddr_aton),
    // hostname-et nem tud feloldani. Arduino oldalon megoldjuk a DNS-t.
    IPAddress resolved;
    if (!WiFi.hostByName(_host.c_str(), resolved)) {
        Serial.printf(
            "[SNAP] DNS resolution failed for host: %s\n",
            _host.c_str()
        );
        return;
    }

    String ipStr = resolved.toString();
    Serial.printf("[SNAP] %s resolved to %s\n",
                  _host.c_str(), ipStr.c_str());

    snap_app_config_t cfg = {};
    cfg.host      = ipStr.c_str();
    cfg.port      = _port;
    cfg.device_id = _deviceId.c_str();
    cfg.bclk_pin  = I2S_BCLK;
    cfg.lrc_pin   = I2S_LRC;
    cfg.din_pin   = I2S_DIN;
    cfg.mclk_pin  = -1;  // MAX98357A: nincs MCLK bemenete

    esp_err_t err = snap_app_start(&cfg);
    if (err != ESP_OK) {
        Serial.printf("[SNAP] snap_app_start failed: %d\n", (int)err);
        return;
    }

    // _localVolume 1..10 (eszköz manuális skála);
    // snap_app_set_local_volume 0..100-at vár (dsp_processor SOFT_VOL).
    snap_app_set_local_volume((uint8_t)(_localVolume * 10));

    _started = true;
    _pausedForLocalPlayback = false;
    Serial.println("[SNAP] snap_app started");
}

void SnapcastClient::stop() {
    if (!_started) return;
    snap_app_stop();
    _started = false;
    _pausedForLocalPlayback = false;
    Serial.println("[SNAP] stopped");
}

bool SnapcastClient::isStarted() const {
    return _started;
}

bool SnapcastClient::isConnected() const {
    return snap_app_is_connected();
}

void SnapcastClient::setLocalVolume(uint8_t volume) {
    if (volume < 1) volume = 1;
    if (volume > 10) volume = 10;
    _localVolume = volume;

    if (_started) {
        // 1..10 skálát konvertáljuk 0..100-ra a snap_app/dsp_processor felé
        snap_app_set_local_volume((uint8_t)(volume * 10));
    }
}

void SnapcastClient::pauseForLocalPlayback() {
    if (_pausedForLocalPlayback) return;
    Serial.println("[SNAP] pauseForLocalPlayback (stub)");
    _pausedForLocalPlayback = true;
    snap_app_pause();
}

void SnapcastClient::resumeAfterLocalPlayback() {
    if (!_pausedForLocalPlayback) return;
    Serial.println("[SNAP] resumeAfterLocalPlayback (stub)");
    snap_app_resume();
    _pausedForLocalPlayback = false;
}

bool SnapcastClient::isPausedForLocalPlayback() const {
    return _pausedForLocalPlayback;
}

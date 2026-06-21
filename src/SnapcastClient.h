#ifndef SNAPCAST_CLIENT_H
#define SNAPCAST_CLIENT_H

/*
 * SnapcastClient - vékony C++ adapter a components/snap_app/ C API köré.
 *
 * Cseréje a régi SnapcastClientESP32-nek. A public interfész szándékosan
 * azonos a régivel, hogy a main.cpp és a BellManager integráció érintetlen
 * maradjon: csak az osztálynevet kell cserélni.
 *
 * Belül a snap_app komponens (CarlosDerSeher-alapú) Opus/FLAC/PCM dekódert,
 * jitter buffert, time-sync-et, és DSP-alapú drift kompenzációt kínál.
 *
 * Megjegyzés a DNS feloldásról: a snap_app::connection_handler.c az
 * ipaddr_aton()-t használja, ami csak numerikus IP-t fogad el. Ezért
 * a start()-ban Arduino oldalon WiFi.hostByName()-mel feloldjuk a hostot,
 * és a numerikus IP-t adjuk át a snap_app_config_t-nek.
 */

#include <Arduino.h>
#include "Config.h"

extern "C" {
#include "dsp_processor.h"
}

class SnapcastClient {
public:
    SnapcastClient();

    void begin(
        const String& host,
        uint16_t port,
        const String& deviceId,
        uint8_t localVolume
    );

    void start();
    void stop();

    bool isStarted() const;
    bool isConnected() const;

    // True, ha az utolsó ~500 ms-on belül volt non-silence audio a stream-en.
    // A UI ezzel dönti el, hogy a MESSAGE/RADIO/SIGNAL villogást mutassa-e
    // (a silence-ffmpeg által generált 0 minták esetén false).
    bool isAudioActive() const;

    void setLocalVolume(uint8_t volume);
    void setChannelMode(dsp_channel_mode_t mode);

    // Helyi/offline MP3 lejátszás miatt I2S átadás az AudioManagernek.
    // A snap_app jelenleg no-op pause/resume-ot ad - a tényleges I2S
    // átengedést a snap_app_stop() / snap_app_start() ciklus jelentené,
    // ami erőforrás-igényes. Egyelőre hagyjuk hogy a két aktív audio
    // forrás egymással felváltva írjon I2S-re; ha vágás van, finomítunk.
    void pauseForLocalPlayback();
    void resumeAfterLocalPlayback();
    bool isPausedForLocalPlayback() const;

private:
    String _host;
    uint16_t _port = 0;
    String _deviceId;
    uint8_t _localVolume = 9;

    bool _started = false;
    bool _pausedForLocalPlayback = false;
};

#endif

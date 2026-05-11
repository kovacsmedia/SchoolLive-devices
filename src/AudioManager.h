#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <Arduino.h>
#include <functional>
#include "Config.h"

class Audio;
class PersistStore;

#define AUDIO_EOF_COOLDOWN_MS 10000
#define URL_START_TIMEOUT_MS 25000

class AudioManager {
public:
    AudioManager();

    void begin(PersistStore* store = nullptr);
    void loop();

    // Manuális (perzisztens) hangerő. 1..10 skála.
    // A getVolume() mindig a manuálisat adja vissza (a UI a felhasználói
    // beállítást szeretné látni); a tényleges kimenetre érvényesülő érték
    // a getEffectiveVolume()-ban van (override aktív esetén az override).
    void setVolume(uint8_t vol);
    uint8_t getVolume() const;

    // Effective volume: override aktív esetén a _volumeOverride, különben
    // a currentVolume. A Snapcast streamre és a helyi MP3 lejátszásra is
    // ez érvényesül.
    uint8_t getEffectiveVolume() const;

    // Vészhelyzeti / távoli ideiglenes felülírás:
    // - setVolumeOverride(10) → a kimenet mostantól max-on, függetlenül attól
    //   hogy a felhasználó miket nyom a gombokon (currentVolume frissül a háttérben,
    //   de a Snapcast/helyi MP3 az override-ot követi);
    // - clearVolumeOverride() → vissza a currentVolume-ra.
    // Az override NEM perzisztálódik (újraindítás után automatikusan elszáll).
    void setVolumeOverride(uint8_t vol);
    void clearVolumeOverride();
    bool hasVolumeOverride() const { return _volumeOverride > 0; }

    // Volume-changed callback: minden olyan eseménynél meghívódik,
    // amikor az effective volume megváltozhat (manuális, override be/ki).
    // A main.cpp ezzel köti össze a SnapcastClient::setLocalVolume()-mal.
    void setVolumeChangedCallback(std::function<void(uint8_t)> cb) {
        _onVolumeChanged = std::move(cb);
    }

    bool isMuted() const {
        return false;
    }

    void playFile(const char* filename);
    void playUrl(const char* url);

    void stop();

    bool isPlaying() const;
    bool isStreamMode() const;

    bool isBusy() const {
        return _urlActive || _localFileActive;
    }

    void notifyEof();
    void notifyError();

    bool isInCooldown() const;

    // I2S arbitration hookok.
    // Helyi/offline csengetés előtt a main leállítja a Snapcast I2S-t,
    // EOF/hiba/stop után pedig visszaengedi.
    void setI2SCallbacks(
        void (*beforeLocalPlayback)(),
        void (*afterLocalPlayback)()
    );

private:
    Audio* audio = nullptr;
    PersistStore* _store = nullptr;

    uint8_t currentVolume = 9;

    // 0 = nincs override, 1..10 = effective volume erre van rögzítve.
    uint8_t _volumeOverride = 0;

    std::function<void(uint8_t)> _onVolumeChanged;

    // Aktuális effective volume újraszámolása + ESP32-audioI2S + callback.
    // Hívni kell minden olyan eseménynél, amikor a manuális vagy az override
    // változott.
    void applyEffectiveVolume();

    bool _streamMode = false;
    bool _eofReceived = false;
    unsigned long _eofTimeMs = 0;

    bool _urlActive = false;
    unsigned long _urlStartMs = 0;
    bool _urlHasPlayed = false;

    bool _localFileActive = false;

    void (*_beforeLocalPlayback)() = nullptr;
    void (*_afterLocalPlayback)() = nullptr;

    void ensureAudio();
    void releaseLocalPlaybackIfNeeded();
};

#endif
#include "AudioManager.h"
#include "PersistStore.h"
#include <LittleFS.h>
#include <WiFi.h>
#include "Audio.h"

static AudioManager* _instance = nullptr;

void audio_info(const char* info) { Serial.printf("[AUDIO] %s\n", info); }

void audio_eof_mp3(const char* info) {
    Serial.printf("[AUDIO] EOF mp3: %s\n", info);
    if (_instance) _instance->notifyEof();
}

void audio_eof_stream(const char* info) {
    Serial.printf("[AUDIO] EOF stream: %s\n", info);
    if (_instance) _instance->notifyEof();
}

void audio_error(const char* info) {
    Serial.printf("[AUDIO] ERROR: %s\n", info);
    if (_instance) _instance->notifyError();
}

AudioManager::AudioManager() {
    currentVolume = 9;
}

void AudioManager::begin(PersistStore* store) {
    _instance = this;
    _store    = store;

    if (_store) {
        currentVolume = _store->getVolume(9);
        Serial.printf("[AUDIO] Restored volume: %d\n", currentVolume);
    }

    if (!audio) {
        audio = new Audio();
    }

    audio->setPinout(I2S_BCLK, I2S_LRC, I2S_DIN);
    audio->forceMono(true);
    setVolume(currentVolume);
}

void AudioManager::loop() {
    if (!audio) return;

    // Ha URL stream aktív és az audio elindult → jegyezzük fel
    if (_urlActive && audio->isRunning()) {
        _urlHasPlayed = true;
    }

    audio->loop();

    // Watchdog – két eset:
    // 1. Elindult, majd callback nélkül megállt → hiba történt
    // 2. 25 másodperc alatt soha nem indult el → sosem fog elindulni
    if (_urlActive) {
        unsigned long elapsed = millis() - _urlStartMs;
        bool playedThenStopped = _urlHasPlayed && !audio->isRunning();
        bool neverStarted      = !_urlHasPlayed && elapsed >= URL_START_TIMEOUT_MS;

        if (playedThenStopped) {
            Serial.println("[AUDIO] Watchdog: stream stopped without EOF callback – cleanup");
            notifyError();
        } else if (neverStarted) {
            Serial.println("[AUDIO] Watchdog: never started within 25s – cleanup");
            notifyError();
        }
    }
}

void AudioManager::setVolume(uint8_t vol) {
    if (vol < 1)  vol = 1;
    if (vol > 10) vol = 10;
    currentVolume = vol;
    uint8_t internalVolume = map(currentVolume, 1, 10, 2, 21);
    if (audio) audio->setVolume(internalVolume);
    if (_store) _store->setVolume(vol);
}

uint8_t AudioManager::getVolume() const {
    return currentVolume;
}

void AudioManager::playFile(const char* filename) {
    if (!audio || !filename) return;
    _eofReceived  = false;
    _eofTimeMs    = 0;
    _urlActive    = false;
    _urlStartMs   = 0;
    _urlHasPlayed = false;
    if (LittleFS.exists(filename)) {
        _streamMode = false;
        audio->connecttoFS(LittleFS, filename);
    }
}

void AudioManager::playUrl(const char* url) {
    if (!audio || !url) return;
    _eofReceived  = false;
    _eofTimeMs    = 0;
    _streamMode   = true;
    _urlHasPlayed = false;
    _urlStartMs   = millis();  // ← előbb az időbélyeg
    _urlActive    = true;      // ← utoljára az active flag
    audio->connecttohost(url);
}

void AudioManager::notifyEof() {
    Serial.println("[AUDIO] EOF – cooldown started");
    _eofReceived  = true;
    _streamMode   = false;
    _urlActive    = false;
    _urlStartMs   = 0;
    _urlHasPlayed = false;
    _eofTimeMs    = millis();
    if (audio) audio->stopSong();
}

void AudioManager::notifyError() {
    Serial.println("[AUDIO] Error – releasing busy lock, cooldown started");
    _eofReceived  = true;
    _streamMode   = false;
    _urlActive    = false;
    _urlStartMs   = 0;
    _urlHasPlayed = false;
    _eofTimeMs    = millis();
    if (audio) audio->stopSong();
}

void AudioManager::stop() {
    if (!audio) return;
    audio->stopSong();
    _streamMode   = false;
    _urlActive    = false;
    _urlStartMs   = 0;
    _urlHasPlayed = false;
    _eofReceived  = false;
    _eofTimeMs    = 0;
}

bool AudioManager::isPlaying() const {
    if (!audio) return false;
    return audio->isRunning();
}

bool AudioManager::isStreamMode() const {
    return _streamMode;
}

bool AudioManager::isInCooldown() const {
    if (!_eofReceived) return false;
    return (millis() - _eofTimeMs) < AUDIO_EOF_COOLDOWN_MS;
}
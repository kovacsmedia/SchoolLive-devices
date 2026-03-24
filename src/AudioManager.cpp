// ─────────────────────────────────────────────────────────────────────────────
// AudioManager.cpp – ESP32-S3-N16R8
// ─────────────────────────────────────────────────────────────────────────────
#include "AudioManager.h"
#include "PersistStore.h"
#include <LittleFS.h>
#include <WiFi.h>
#include "Audio.h"

const char* AudioManager::PSRAM_TEMP_PATH = "/tts_psram_tmp.mp3";

static AudioManager* _instance = nullptr;

void audio_info(const char* info)       { Serial.printf("[AUDIO] %s\n", info); }
void audio_eof_mp3(const char* info)    { if (_instance) _instance->notifyEof();   }
void audio_eof_stream(const char* info) { if (_instance) _instance->notifyEof();   }
void audio_error(const char* info)      { Serial.printf("[AUDIO] ERROR: %s\n", info);
                                          if (_instance) _instance->notifyError(); }

AudioManager::AudioManager() { currentVolume = 9; }

void AudioManager::_initAudio() {
    if (audio) return;
    audio = new Audio();
    audio->setPinout(I2S_BCLK, I2S_LRC, I2S_DIN);
    audio->forceMono(true);
    uint8_t internal = map(currentVolume, 1, 10, 2, 21);
    audio->setVolume(internal);
}

void AudioManager::_deinitAudio() {
    if (!audio) return;
    audio->stopSong();
    delete audio;
    audio = nullptr;
    // Az Audio library destruktora felszabadítja az I2S drivert
    vTaskDelay(pdMS_TO_TICKS(20));
}

void AudioManager::begin(PersistStore* store) {
    _instance = this;
    _store    = store;

    if (_store) {
        currentVolume = _store->getVolume(9);
        Serial.printf("[AUDIO] Volume restored: %d\n", currentVolume);
    }

    _initAudio();

    if (psramFound()) {
        Serial.println("[AUDIO] PSRAM elérhető – audio bufferek PSRAM-ban");
    }
    Serial.println("[AUDIO] AudioManager kész");
}

// ── I2S arbitráció ────────────────────────────────────────────────────────────
// A SnapcastClient hívja csatlakozáskor: felszabadítja az I2S drivert a Snap számára
void AudioManager::suspend() {
    if (_suspended) return;
    Serial.println("[AUDIO] Felfüggesztés – I2S átadva a Snapcastnak");
    _deinitAudio();
    _suspended  = true;
    _streamMode = false;
    _urlActive  = false;
}

// A SnapcastClient hívja lecsatlakozáskor: visszaveszi az I2S drivert
void AudioManager::resume() {
    if (!_suspended) return;
    Serial.println("[AUDIO] Újraindítás – I2S visszavéve az AudioManager számára");
    _suspended = false;
    _initAudio();
}

void AudioManager::loop() {
    if (!audio || _suspended) return;

    if (_urlActive && audio->isRunning()) {
        _urlHasPlayed = true;
    }

    audio->loop();

    if (_urlActive) {
        unsigned long elapsed = millis() - _urlStartMs;
        if (_urlHasPlayed && !audio->isRunning()) {
            notifyError();
        } else if (!_urlHasPlayed && elapsed >= URL_START_TIMEOUT_MS) {
            Serial.println("[AUDIO] Watchdog: stream sosem indult – cleanup");
            notifyError();
        }
    }
}

void AudioManager::setVolume(uint8_t vol) {
    if (vol < 1) vol = 1;
    if (vol > 10) vol = 10;
    currentVolume = vol;
    if (audio) {
        uint8_t internal = map(currentVolume, 1, 10, 2, 21);
        audio->setVolume(internal);
    }
    if (_store) _store->setVolume(vol);
}

uint8_t AudioManager::getVolume() const { return currentVolume; }

void AudioManager::playFile(const char* filename) {
    if (_suspended) {
        Serial.println("[AUDIO] playFile: felfüggesztve (Snap aktív) – skip");
        return;
    }
    if (!audio || !filename) return;
    _eofReceived = false; _eofTimeMs = 0;
    _urlActive   = false; _urlStartMs = 0; _urlHasPlayed = false;
    if (LittleFS.exists(filename)) {
        _streamMode = false;
        audio->connecttoFS(LittleFS, filename);
        Serial.printf("[AUDIO] playFile: %s\n", filename);
    } else {
        Serial.printf("[AUDIO] File nem található: %s\n", filename);
    }
}

void AudioManager::playUrl(const char* url) {
    if (_suspended) {
        Serial.println("[AUDIO] playUrl: felfüggesztve (Snap aktív) – skip");
        return;
    }
    if (!audio || !url) return;
    _eofReceived  = false; _eofTimeMs = 0;
    _streamMode   = true;
    _urlHasPlayed = false;
    _urlStartMs   = millis();
    _urlActive    = true;
    audio->connecttohost(url);
    Serial.printf("[AUDIO] playUrl: %.80s\n", url);
}

void AudioManager::playPsram(const uint8_t* buf, size_t len) {
    if (_suspended) return;
    if (!audio || !buf || len == 0) return;
    File f = LittleFS.open(PSRAM_TEMP_PATH, "w");
    if (!f) return;
    size_t written = f.write(buf, len);
    f.close();
    if (written != len) { LittleFS.remove(PSRAM_TEMP_PATH); return; }
    playFile(PSRAM_TEMP_PATH);
}

void AudioManager::stop() {
    if (!audio) return;
    audio->stopSong();
    _streamMode = false; _urlActive = false;
    _urlStartMs = 0;     _urlHasPlayed = false;
    _eofReceived = false; _eofTimeMs = 0;
    Serial.println("[AUDIO] Stop");
}

bool AudioManager::isPlaying()    const { return !_suspended && audio && audio->isRunning(); }
bool AudioManager::isStreamMode() const { return _streamMode; }
bool AudioManager::isInCooldown() const {
    if (!_eofReceived) return false;
    return (millis() - _eofTimeMs) < AUDIO_EOF_COOLDOWN_MS;
}

void AudioManager::notifyEof() {
    Serial.println("[AUDIO] EOF");
    _eofReceived = true; _streamMode = false; _urlActive = false;
    _urlStartMs  = 0;    _urlHasPlayed = false; _eofTimeMs = millis();
    if (audio) audio->stopSong();
}

void AudioManager::notifyError() {
    Serial.println("[AUDIO] Error → cleanup");
    _eofReceived = true; _streamMode = false; _urlActive = false;
    _urlStartMs  = 0;    _urlHasPlayed = false; _eofTimeMs = millis();
    if (audio) audio->stopSong();
}
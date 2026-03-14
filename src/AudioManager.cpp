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

void AudioManager::begin(PersistStore* store) {
    _instance = this;
    _store    = store;

    if (_store) {
        currentVolume = _store->getVolume(9);
        Serial.printf("[AUDIO] Volume restored: %d\n", currentVolume);
    }

    if (!audio) audio = new Audio();
    audio->setPinout(I2S_BCLK, I2S_LRC, I2S_DIN);
    audio->forceMono(true);

    // S3: PSRAM automatikusan kihasználódik ha BOARD_HAS_PSRAM definiált
    // Az ESP32-audioI2S belső buffereit a heap allokátor kezeli (PSRAM-ba kerül)
    if (psramFound()) {
        Serial.println("[AUDIO] PSRAM elérhető – audio bufferek PSRAM-ban");
    }

    setVolume(currentVolume);
    Serial.println("[AUDIO] AudioManager kész");
}

void AudioManager::loop() {
    if (!audio) return;

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
    uint8_t internal = map(currentVolume, 1, 10, 2, 21);
    if (audio) audio->setVolume(internal);
    if (_store) _store->setVolume(vol);
}

uint8_t AudioManager::getVolume() const { return currentVolume; }

// ── playFile ─────────────────────────────────────────────────────────────────
void AudioManager::playFile(const char* filename) {
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

// ── playUrl ──────────────────────────────────────────────────────────────────
void AudioManager::playUrl(const char* url) {
    if (!audio || !url) return;
    _eofReceived = false; _eofTimeMs = 0;
    _streamMode  = true;
    _urlHasPlayed = false;
    _urlStartMs  = millis();
    _urlActive   = true;
    audio->connecttohost(url);
    Serial.printf("[AUDIO] playUrl: %.80s\n", url);
}

// ── playPsram ─────────────────────────────────────────────────────────────────
// PSRAM tartalmát kiírjuk egy temp fájlba LittleFS-re, majd onnan játsszuk
// (Az ESP32-audioI2S nem tud közvetlenül memóriából játszani, de a LittleFS
//  write gyors – egy 200KB TTS fájl ~150ms alatt kiíródik)
void AudioManager::playPsram(const uint8_t* buf, size_t len) {
    if (!audio || !buf || len == 0) return;

    // Kiírás LittleFS-re
    File f = LittleFS.open(PSRAM_TEMP_PATH, "w");
    if (!f) {
        Serial.println("[AUDIO] playPsram: temp fájl nyitás sikertelen");
        return;
    }
    size_t written = f.write(buf, len);
    f.close();

    if (written != len) {
        Serial.printf("[AUDIO] playPsram: írás csonka %d/%d\n",
                      (int)written, (int)len);
        LittleFS.remove(PSRAM_TEMP_PATH);
        return;
    }

    Serial.printf("[AUDIO] playPsram: %d bytes LittleFS-re írva → lejátszás\n",
                  (int)len);
    playFile(PSRAM_TEMP_PATH);
}

// ── stop ─────────────────────────────────────────────────────────────────────
void AudioManager::stop() {
    if (!audio) return;
    audio->stopSong();
    _streamMode = false; _urlActive = false;
    _urlStartMs = 0;     _urlHasPlayed = false;
    _eofReceived = false; _eofTimeMs = 0;
    Serial.println("[AUDIO] Stop");
}

bool AudioManager::isPlaying()    const { return audio && audio->isRunning(); }
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
#include "AudioManager.h"
#include "PersistStore.h"

#include <LittleFS.h>
#include "Audio.h"

static AudioManager* _instance = nullptr;

void audio_info(const char* info) {
    Serial.printf("[AUDIO] %s\n", info);
}

void audio_eof_mp3(const char* info) {
    Serial.printf("[AUDIO] EOF mp3: %s\n", info);

    if (_instance) {
        _instance->notifyEof();
    }
}

void audio_eof_stream(const char* info) {
    Serial.printf("[AUDIO] EOF stream: %s\n", info);

    if (_instance) {
        _instance->notifyEof();
    }
}

void audio_error(const char* info) {
    Serial.printf("[AUDIO] ERROR: %s\n", info);

    if (_instance) {
        _instance->notifyError();
    }
}

AudioManager::AudioManager() {
    currentVolume = 9;
}

void AudioManager::begin(PersistStore* store) {
    _instance = this;
    _store = store;

    if (_audioMux == nullptr) {
        _audioMux = xSemaphoreCreateRecursiveMutex();
    }

    if (_store) {
        currentVolume = _store->getVolume(9);
        Serial.printf("[AUDIO] Restored volume: %d\n", currentVolume);
    }

    if (currentVolume < 1) currentVolume = 1;
    if (currentVolume > 10) currentVolume = 10;

    /*
     * Fontos:
     * Itt már NEM inicializálunk Audio() példányt és NEM foglalunk I2S-t.
     *
     * Normál online módban az I2S-t a SnapcastClientESP32 birtokolja.
     * Az AudioManager csak akkor hozza létre az ESP32-audioI2S példányt,
     * amikor tényleg helyi/offline MP3 csengetést kell lejátszani.
     */
    Serial.println("[AUDIO] Lazy local playback mode enabled");
}

void AudioManager::setI2SCallbacks(
    void (*beforeLocalPlayback)(),
    void (*afterLocalPlayback)()
) {
    _beforeLocalPlayback = beforeLocalPlayback;
    _afterLocalPlayback = afterLocalPlayback;
}

bool AudioManager::ensureAudio() {
    _releaseAudioPending = false;

    if (audio) {
        if (audio->i2sReady()) return true;

        // Egy korábbi próbálkozás I2S nélkül maradt – ilyen példánnyal
        // lejátszani nem lehet, csak hibát ontana. Eldobjuk, és újra
        // próbáljuk lentebb.
        Serial.println("[AUDIO] A meglevo Audio peldany I2S nelkul van – eldobjuk");
        delete audio;
        audio = nullptr;
    }

    /*
     * Két kísérlet. Az elsőnél a Snapcast már elvileg elengedte az I2S-t
     * (beforeLocalPlayback). Ha mégsem – például épp egy hard resync alatt
     * foglalta vissza –, még egyszer megkérjük rá, és újrapróbáljuk.
     * A csengetés SOSEM maradhat el egy versenyhelyzet miatt.
     */
    for (int attempt = 1; attempt <= 2; attempt++) {
        audio = new Audio();

        if (audio->i2sReady()) {
            audio->setPinout(I2S_BCLK, I2S_LRC, I2S_DIN);
            audio->forceMono(true);

            setVolume(currentVolume);

            Serial.printf(
                "[AUDIO] Local Audio initialized on I2S BCLK=%d LRC=%d DIN=%d\n",
                I2S_BCLK,
                I2S_LRC,
                I2S_DIN
            );
            return true;
        }

        Serial.printf("[AUDIO] I2S foglalas sikertelen (%d. probalkozas)\n", attempt);
        delete audio;
        audio = nullptr;

        if (attempt == 1 && _beforeLocalPlayback) {
            _beforeLocalPlayback();
            delay(150);
        }
    }

    Serial.println("[AUDIO] ❌ Az I2S vezerlot nem sikerult megszerezni – helyi lejatszas kimarad");
    return false;
}

void AudioManager::applyPlaybackPriority(bool playing) {
    if (playing == _prioRaised) return;

    if (playing) {
        _prioSaved = uxTaskPriorityGet(nullptr);
        vTaskPrioritySet(nullptr, _prioSaved + 1);
        _prioRaised = true;
    } else {
        vTaskPrioritySet(nullptr, _prioSaved);
        _prioRaised = false;
    }
}

void AudioManager::destroyAudioIfPending() {
    if (!_releaseAudioPending) return;
    _releaseAudioPending = false;

    if (audio) {
        /*
         * A destruktor letiltja és felszabadítja az I2S csatornát, így a
         * Snapcast lejátszó vissza tudja venni. Ez a HELYE: az
         * `Audio::loop()` már visszatért, nem az objektum belsejéből törlünk.
         */
        delete audio;
        audio = nullptr;
        Serial.println("[AUDIO] Helyi lejatszo lezarva, I2S elengedve");
    }

    if (_afterLocalPlayback) {
        _afterLocalPlayback();
    }
}

void AudioManager::loop() {
    lockAudio();

    if (!audio) {
        // A felszabadítás akkor is le kell fusson, ha az Audio példány már
        // eltűnt – ilyenkor csak az afterLocalPlayback callback marad hátra.
        destroyAudioIfPending();
        applyPlaybackPriority(false);
        unlockAudio();
        return;
    }

    // A dekódolás erre a körre elsőbbséget kap a snap taskok előtt.
    applyPlaybackPriority(_localFileActive || _urlActive);

    if (_urlActive && audio->isRunning()) {
        _urlHasPlayed = true;
    }

    audio->loop();

    if (_urlActive) {
        unsigned long elapsed = millis() - _urlStartMs;

        bool playedThenStopped = _urlHasPlayed && !audio->isRunning();
        bool neverStarted = !_urlHasPlayed && elapsed >= URL_START_TIMEOUT_MS;

        if (playedThenStopped) {
            Serial.println("[AUDIO] Watchdog: stream stopped without EOF callback – cleanup");
            notifyError();
        } else if (neverStarted) {
            Serial.println("[AUDIO] Watchdog: never started within 25s – cleanup");
            notifyError();
        }
    }

    if (_localFileActive && !audio->isRunning()) {
        /*
         * Biztonsági út:
         * ha az ESP32-audioI2S nem hív EOF callbacket, de a helyi MP3 már nem fut,
         * akkor visszaengedjük a Snapcastot.
         */
        Serial.println("[AUDIO] Local file stopped without EOF callback – cleanup");
        notifyEof();
    }

    // Az `audio->loop()` már visszatért: innen biztonságos törölni.
    destroyAudioIfPending();

    if (!_localFileActive && !_urlActive) applyPlaybackPriority(false);

    unlockAudio();
}

void AudioManager::setVolume(uint8_t vol) {
    if (vol < 1) vol = 1;
    if (vol > 10) vol = 10;

    currentVolume = vol;

    if (_store) {
        _store->setVolume(vol);
    }

    Serial.printf("[AUDIO] Manual volume=%d (override=%d)\n", currentVolume, _volumeOverride);

    /*
     * A tényleges kimenetre az effective volume érvényesül:
     * - ha override aktív → override
     * - egyébként → currentVolume
     *
     * Tehát override alatt a felhasználó még nyomhat gombokat, mi tároljuk
     * a manuális beállítást, de a Snapcast / helyi MP3 az override-ot követi
     * amíg az aktív.
     */
    applyEffectiveVolume();
}

uint8_t AudioManager::getVolume() const {
    return currentVolume;
}

uint8_t AudioManager::getEffectiveVolume() const {
    return _volumeOverride > 0 ? _volumeOverride : currentVolume;
}

void AudioManager::setVolumeOverride(uint8_t vol) {
    if (vol < 1) vol = 1;
    if (vol > 10) vol = 10;

    if (_volumeOverride == vol) return;

    _volumeOverride = vol;
    Serial.printf("[AUDIO] Volume override ON: %d (manual stays %d)\n", _volumeOverride, currentVolume);

    applyEffectiveVolume();
}

void AudioManager::clearVolumeOverride(void) {
    if (_volumeOverride == 0) return;

    _volumeOverride = 0;
    Serial.printf("[AUDIO] Volume override OFF, back to manual: %d\n", currentVolume);

    applyEffectiveVolume();
}

void AudioManager::applyEffectiveVolume() {
    uint8_t eff = getEffectiveVolume();
    uint8_t internalVolume = map(eff, 1, 10, 2, 21);

    lockAudio();
    if (audio) {
        audio->setVolume(internalVolume);
    }
    unlockAudio();

    if (_onVolumeChanged) {
        // A Snapcast oldali skála (0..100) konverziót a SnapcastClient végzi
        // (setLocalVolume(uint8_t 1..10) → snap_app *10).
        _onVolumeChanged(eff);
    }

    Serial.printf("[AUDIO] Effective volume=%d internal=%d\n", eff, internalVolume);
}

void AudioManager::playFile(const char* filename) {
    if (!filename) return;

    Serial.printf("[AUDIO] playFile requested: %s\n", filename);

    if (!LittleFS.exists(filename)) {
        Serial.printf("[AUDIO] local file missing: %s\n", filename);
        return;
    }

    /*
     * I2S átadás:
     * előbb a Snapcast engedje el az I2S drivert,
     * utána inicializálhat az ESP32-audioI2S.
     */
    lockAudio();

    if (_beforeLocalPlayback) {
        _beforeLocalPlayback();
        delay(100);
    }

    if (!ensureAudio()) {
        // Nincs I2S – a Snapcastot vissza kell engedni, különben némán állna
        // a lejátszó, miközben a csatornát senki nem használja.
        if (_afterLocalPlayback) {
            _afterLocalPlayback();
        }
        unlockAudio();
        return;
    }

    _eofReceived = false;
    _eofTimeMs = 0;

    _streamMode = false;
    _urlActive = false;
    _urlStartMs = 0;
    _urlHasPlayed = false;

    _localFileActive = true;

    audio->connecttoFS(LittleFS, filename);

    unlockAudio();
}

void AudioManager::playUrl(const char* url) {
    /*
     * Az új rendszerben backend hangot nem URL-ből játszunk,
     * hanem Snapcastból. Ezt meghagyjuk kompatibilitásnak, de nem indítjuk el.
     */
    Serial.printf(
        "[AUDIO] playUrl ignored in Snapcast mode: %s\n",
        url ? url : "(null)"
    );

    _streamMode = false;
    _urlActive = false;
    _urlStartMs = 0;
    _urlHasPlayed = false;
}

void AudioManager::releaseLocalPlaybackIfNeeded() {
    if (!_localFileActive) return;

    lockAudio();

    _localFileActive = false;

    if (audio) {
        audio->stopSong();
    }

    /*
     * Az Audio példány törlése (és vele az I2S csatorna felszabadítása) NEM
     * történhet itt: ez a metódus az EOF callbackből, azaz az `Audio::loop()`
     * belsejéből is hívódik, és a saját objektumát törölné ki maga alól.
     * A loop() végén, a visszatérés után zárjuk le – ott hívjuk az
     * afterLocalPlayback callbacket is, amikor az I2S tényleg szabad.
     */
    _releaseAudioPending = true;

    unlockAudio();
}

void AudioManager::notifyEof() {
    Serial.println("[AUDIO] EOF – cooldown started");

    _eofReceived = true;
    _streamMode = false;

    _urlActive = false;
    _urlStartMs = 0;
    _urlHasPlayed = false;

    _eofTimeMs = millis();

    releaseLocalPlaybackIfNeeded();
}

void AudioManager::notifyError() {
    Serial.println("[AUDIO] Error – releasing busy lock, cooldown started");

    _eofReceived = true;
    _streamMode = false;

    _urlActive = false;
    _urlStartMs = 0;
    _urlHasPlayed = false;

    _eofTimeMs = millis();

    releaseLocalPlaybackIfNeeded();
}

void AudioManager::stop() {
    lockAudio();

    if (audio) {
        audio->stopSong();
    }

    _streamMode = false;

    _urlActive = false;
    _urlStartMs = 0;
    _urlHasPlayed = false;

    _eofReceived = false;
    _eofTimeMs = 0;

    releaseLocalPlaybackIfNeeded();

    unlockAudio();
}

bool AudioManager::isPlaying() const {
    lockAudio();
    const bool running = (audio != nullptr) && audio->isRunning();
    unlockAudio();
    return running;
}

bool AudioManager::isStreamMode() const {
    return _streamMode;
}

bool AudioManager::isInCooldown() const {
    if (!_eofReceived) return false;

    return (millis() - _eofTimeMs) < AUDIO_EOF_COOLDOWN_MS;
}
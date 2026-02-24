#include "AudioManager.h"

AudioManager::AudioManager() { currentVolume = 5; }

void AudioManager::begin() {
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DIN);
    setVolume(currentVolume); 
    audio.forceMono(true);
}

void AudioManager::loop() { audio.loop(); }

void AudioManager::setVolume(uint8_t vol) {
    if (vol < 1) vol = 1; if (vol > 10) vol = 10;
    currentVolume = vol;
    uint8_t internalVolume = map(currentVolume, 1, 10, 2, 21);
    audio.setVolume(internalVolume);
}

uint8_t AudioManager::getVolume() { return currentVolume; }

void AudioManager::playFile(const char* filename) {
    if (LittleFS.exists(filename)) {
        // FONTOS: Ez jelzi a rendszernek, hogy NEM stream szól
        _streamMode = false; 
        audio.connecttoFS(LittleFS, filename);
    }
}

void AudioManager::playUrl(const char* url) {
    // FONTOS: Ez jelzi, hogy stream szól
    _streamMode = true; 
    audio.connecttohost(url);
}

void AudioManager::stop() { 
    audio.stopSong(); 
    _streamMode = false; 
}

bool AudioManager::isPlaying() { return audio.isRunning(); }
bool AudioManager::isStreamMode() { return _streamMode; }

void audio_info(const char *info) { }
void audio_eof_mp3(const char *info) { }
#include "AudioManager.h"

#include <LittleFS.h>
#include "Audio.h"

// A library callback-jei globális függvényeket keresnek.
// Üresen hagyjuk őket, hogy ne termeljenek logot (és ne nőjön a bináris).
void audio_info(const char *info) { (void)info; }
void audio_eof_mp3(const char *info) { (void)info; }

AudioManager::AudioManager() {
  currentVolume = 5;
}

void AudioManager::begin() {
  if (!audio) {
    audio = new Audio();
  }

  audio->setPinout(I2S_BCLK, I2S_LRC, I2S_DIN);
  audio->forceMono(true);

  setVolume(currentVolume);
}

void AudioManager::loop() {
  if (!audio) return;
  audio->loop();
}

void AudioManager::setVolume(uint8_t vol) {
  if (vol < 1) vol = 1;
  if (vol > 10) vol = 10;

  currentVolume = vol;

  // ESP32-audioI2S belső volume skála (0..21 körül)
  uint8_t internalVolume = map(currentVolume, 1, 10, 2, 21);

  if (audio) {
    audio->setVolume(internalVolume);
  }
}

uint8_t AudioManager::getVolume() const {
  return currentVolume;
}

void AudioManager::playFile(const char* filename) {
  if (!audio || !filename) return;

  if (LittleFS.exists(filename)) {
    _streamMode = false; // helyi lejátszás
    audio->connecttoFS(LittleFS, filename);
  }
}

void AudioManager::playUrl(const char* url) {
  if (!audio || !url) return;

  _streamMode = true; // URL/stream lejátszás
  audio->connecttohost(url);
}

void AudioManager::stop() {
  if (!audio) return;

  audio->stopSong();
  _streamMode = false;
}

bool AudioManager::isPlaying() const {
  if (!audio) return false;
  return audio->isRunning();
}

bool AudioManager::isStreamMode() const {
  return _streamMode;
}
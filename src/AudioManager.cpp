#include "AudioManager.h"
#include <LittleFS.h>
#include <WiFi.h>
#include "Audio.h"

// Globális pointer az EOF callback-hez
static AudioManager* _instance = nullptr;

void audio_info(const char *info) { Serial.printf("[AUDIO] %s\n", info); }

void audio_eof_mp3(const char *info) {
  Serial.printf("[AUDIO] EOF mp3: %s\n", info);
}

void audio_eof_stream(const char *info) {
  Serial.printf("[AUDIO] EOF stream: %s\n", info);
  if (_instance) _instance->notifyEof();
}

AudioManager::AudioManager() {
  currentVolume = 10;
}

void AudioManager::begin() {
  _instance = this;

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
  _eofReceived = false;
  if (LittleFS.exists(filename)) {
    _streamMode = false;
    audio->connecttoFS(LittleFS, filename);
  }
}

void AudioManager::playUrl(const char* url) {
  if (!audio || !url) return;
  _eofReceived = false;
  _streamMode = true;
  audio->connecttohost(url);
}

void AudioManager::notifyEof() {
  Serial.println("[AUDIO] notifyEof – restarting in 2s...");
  _eofReceived = true;
  _streamMode = false;
  if (audio) audio->stopSong();
  delay(2000);
  ESP.restart();
}

void AudioManager::stop() {
  if (!audio) return;
  audio->stopSong();
  _streamMode = false;
  _eofReceived = false;
}

bool AudioManager::isPlaying() const {
  if (!audio) return false;
  return audio->isRunning();
}

bool AudioManager::isStreamMode() const {
  return _streamMode;
}
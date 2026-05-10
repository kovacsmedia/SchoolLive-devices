#ifndef SNAPCAST_CLIENT_ESP32_H
#define SNAPCAST_CLIENT_ESP32_H

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "Config.h"

class SnapcastClientESP32 {
public:
    SnapcastClientESP32();

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

    void setLocalVolume(uint8_t volume);

    // Helyi/offline MP3 lejátszás miatt I2S átadás az AudioManagernek.
    void pauseForLocalPlayback();
    void resumeAfterLocalPlayback();
    bool isPausedForLocalPlayback() const;

private:
    struct PcmBlock {
        uint8_t* data;
        size_t len;
        uint32_t serverMs;
    };

    String _host;
    uint16_t _port = 0;
    String _deviceId;

    uint8_t _localVolume = 9;
    uint8_t _serverVolume = 100;
    bool _serverMuted = false;

    volatile bool _started = false;
    volatile bool _connected = false;
    volatile bool _running = false;
    volatile bool _pausedForLocalPlayback = false;

    int _sampleRate = 48000;
    int _channels = 2;
    int _bits = 16;

    WiFiClient _client;

    TaskHandle_t _netTaskHandle = nullptr;
    TaskHandle_t _audioTaskHandle = nullptr;

    QueueHandle_t _pcmQueue = nullptr;

    bool _i2sReady = false;

    static void netTaskThunk(void* arg);
    static void audioTaskThunk(void* arg);

    void netTask();
    void audioTask();

    bool connectToServer();
    void disconnect();

    bool sendHello();
    bool sendTimeRequest();

    bool readLoop();
    bool readFully(uint8_t* buf, size_t len, uint32_t timeoutMs);

    void handleCodecHeader(const uint8_t* payload, size_t len);
    void handleWireChunk(const uint8_t* payload, size_t len);
    void handleServerSettings(const uint8_t* payload, size_t len);
    void handleTimeResponse(uint32_t sec, uint32_t usec);

    bool initI2S();
    void stopI2S();

    void enqueuePcm(const uint8_t* data, size_t len, uint32_t serverMs);
    void clearQueue();

    void applyVolumeInPlace(uint8_t* data, size_t len);

    static uint16_t readU16LE(const uint8_t* p);
    static uint32_t readU32LE(const uint8_t* p);
    static void writeU16LE(uint8_t* p, uint16_t v);
    static void writeU32LE(uint8_t* p, uint32_t v);

    void buildHeader(uint8_t* out, uint16_t type, uint32_t payloadSize);
};

#endif
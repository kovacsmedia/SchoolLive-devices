#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SnapcastClient.h – Snapcast TCP kliens ESP32-S3-ra
//
// Snapcast protokoll v2:
//   TCP 1704 → Hello → ServerSettings → CodecHeader → WireChunk stream
//   PCM 48000:16:2 → I2S közvetlen output (driver I2S API)
//
// PSRAM ring buffer biztosítja a bufferMs-nyi pufferelést.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"

// Snapcast üzenettípusok
#define SNAP_MSG_CODEC_HEADER    0
#define SNAP_MSG_WIRE_CHUNK      1
#define SNAP_MSG_SERVER_SETTINGS 2
#define SNAP_MSG_TIME            3
#define SNAP_MSG_HELLO           4
#define SNAP_MSG_STREAM_TAGS     5

// Üzenet header: 26 byte
struct SnapHeader {
    uint16_t type;
    uint16_t id;
    uint16_t refersTo;
    int32_t  sent_sec;
    int32_t  sent_usec;
    int32_t  recv_sec;
    int32_t  recv_usec;
    uint32_t size;
};

// WireChunk timestamp
struct SnapTimestamp {
    int32_t sec;
    int32_t usec;
};

class SnapcastClient {
public:
    SnapcastClient() {}

    void begin(const String& mac, uint8_t volume = 70, uint16_t port = 1800);    
    void loop();   // Network task-ból hívandó ~10ms-enként
    void stop();

    bool isConnected() const { return _connected; }
    bool isPlaying()   const { return _playing;   }

    void setVolume(uint8_t vol);  // 0-100

    // Statisztika
    uint32_t getBufferFillMs() const { return _bufFillMs; }

private:
    WiFiClient   _client;
    String       _mac;
    String       _host = SNAPCAST_HOST;
    uint16_t     _port = SNAPCAST_PORT;

    bool         _connected    = false;
    bool         _playing      = false;
    bool         _headerRecvd  = false;
    uint8_t      _volume       = 70;

    // Server paraméterek
    uint32_t     _bufferMs     = 1000;
    uint32_t     _latencyMs    = 0;
    bool         _muted        = false;
    int32_t      _serverVolume = 100;

    // Időszinkron
    int64_t      _serverOffsetUs = 0;   // szerver - lokális (mikrosec)
    uint32_t     _lastTimeSyncMs = 0;

    // PCM ring buffer (PSRAM)
    uint8_t*     _ringBuf      = nullptr;
    size_t       _ringSize     = 0;
    size_t       _ringWrite    = 0;
    size_t       _ringRead     = 0;
    size_t       _ringFill     = 0;
    uint32_t     _bufFillMs    = 0;

    // I2S
    bool         _i2sInstalled = false;

    // Üzenet beérkezési buffer
    uint8_t      _hdrBuf[26];
    size_t       _hdrRead = 0;
    uint8_t*     _bodyBuf = nullptr;
    size_t       _bodySize = 0;
    size_t       _bodyRead = 0;
    bool         _readingBody = false;
    SnapHeader   _curHdr;

    // Reconnect
    uint32_t     _lastConnectMs = 0;
    uint32_t     _reconnectMs   = 3000;
    uint8_t      _msgId         = 0;

    void connect();
    void disconnect();
    void processIncoming();
    void processMessage(const SnapHeader& hdr, const uint8_t* body, uint32_t size);

    void handleServerSettings(const uint8_t* body, uint32_t size);
    void handleCodecHeader(const uint8_t* body, uint32_t size);
    void handleWireChunk(const uint8_t* body, uint32_t size);
    void handleTime(const uint8_t* body, uint32_t size);

    void sendHello();
    void sendTime(int32_t latSec, int32_t latUsec);
    void sendMessage(uint16_t type, uint16_t refersTo,
                     const uint8_t* payload, uint32_t size);

    void initI2S();
    void deinitI2S();
    void drainToI2S();

    int64_t nowUs() const;  // mikrosec
    void    ringWrite(const uint8_t* data, size_t len);
    size_t  ringRead(uint8_t* out, size_t maxLen);
};

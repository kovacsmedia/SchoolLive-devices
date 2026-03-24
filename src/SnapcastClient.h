#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SnapcastClient.h
// I2S arbitráció: setAudioManager() – AudioManager suspend/resume koordináció
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"

class AudioManager;  // forward declaration

#define SNAP_MSG_BASE            0
#define SNAP_MSG_CODEC_HEADER    1
#define SNAP_MSG_WIRE_CHUNK      2
#define SNAP_MSG_SERVER_SETTINGS 3
#define SNAP_MSG_TIME            4
#define SNAP_MSG_HELLO           5
#define SNAP_MSG_STREAM_TAGS     6

struct SnapHeader {
    uint16_t type; uint16_t id; uint16_t refersTo;
    int32_t  sent_sec; int32_t sent_usec;
    int32_t  recv_sec; int32_t recv_usec;
    uint32_t size;
};

class SnapcastClient {
public:
    SnapcastClient() {}

    void begin(const String& mac, uint8_t volume = 70, uint16_t port = SNAPCAST_PORT);
    void loop();
    void stop();

    // I2S arbitrációhoz: AudioManager referencia beállítása
    void setAudioManager(AudioManager* am) { _audioMgr = am; }

    void setOnConnected(void (*cb)())    { _onConnected    = cb; }
    void setOnDisconnected(void (*cb)()) { _onDisconnected = cb; }

    bool isConnected() const { return _connected; }
    bool isPlaying()   const { return _playing;   }

    void setVolume(uint8_t vol);
    uint32_t getBufferFillMs() const { return _bufFillMs; }

private:
    WiFiClient   _client;
    AudioManager* _audioMgr = nullptr;  // I2S arbitrációhoz
    void (*_onConnected)()    = nullptr;
    void (*_onDisconnected)() = nullptr;
    String       _mac;
    String       _host = SNAPCAST_HOST;
    uint16_t     _port = SNAPCAST_PORT;
    bool          _connecting        = false;
    unsigned long _connectingStartMs = 0;
    bool         _connected    = false;
    bool         _playing      = false;
    bool         _headerRecvd  = false;
    uint8_t      _volume       = 70;

    uint32_t     _bufferMs     = 1000;
    uint32_t     _latencyMs    = 0;
    bool         _muted        = false;
    int32_t      _serverVolume = 100;

    int64_t      _serverOffsetUs = 0;
    uint32_t     _lastTimeSyncMs = 0;

    uint8_t*     _ringBuf      = nullptr;
    size_t       _ringSize     = 0;
    size_t       _ringWrite    = 0;
    size_t       _ringRead     = 0;
    size_t       _ringFill     = 0;
    uint32_t     _bufFillMs    = 0;

    bool         _i2sInstalled = false;

    uint8_t      _hdrBuf[26];
    size_t       _hdrRead = 0;
    uint8_t*     _bodyBuf = nullptr;
    size_t       _bodySize = 0;
    size_t       _bodyRead = 0;
    bool         _readingBody = false;
    SnapHeader   _curHdr;

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
    void sendMessage(uint16_t type, uint16_t refersTo, const uint8_t* payload, uint32_t size);
    void initI2S();
    void deinitI2S();
    void drainToI2S();
    int64_t nowUs() const;
    void    ringWrite(const uint8_t* data, size_t len);
    size_t  ringRead(uint8_t* out, size_t maxLen);
};
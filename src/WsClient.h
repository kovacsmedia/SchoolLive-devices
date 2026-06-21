#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <functional>

using WsMsgCb = std::function<void(const JsonDocument&)>;

class WsClient {
public:
    void begin(const String& host, uint16_t port, const String& deviceKey);
    void loop();

    bool isConnected() const { return _connected; }
    bool sendJson(const JsonDocument& doc);

    // Összes bejövő JSON üzenet erre a callback-re kerül
    void onMessage(WsMsgCb cb) { _msgCb = cb; }

private:
    WebSocketsClient _ws;

    String   _host;
    uint16_t _port       = 443;
    String   _deviceKey;
    bool     _connected  = false;
    bool     _started    = false;

    // Exponenciális backoff: 1s → 2s → 4s → … → max 10s
    unsigned long _disconnectedAtMs      = 0;
    unsigned long _reconnectIntervalMs   = 1000UL;
    static const unsigned long MAX_RECONNECT_MS = 10000UL;

    WsMsgCb _msgCb;

    void wsEvent(WStype_t type, uint8_t* payload, size_t length);
};

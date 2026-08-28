#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <functional>

using WsMsgCb = std::function<void(const JsonDocument&)>;
// Multi-node cluster: jelzés, hogy N egymást követő sikertelen (újra)csatlakozás
// történt – a hívó ilyenkor a `/cluster/locate` végponton ellenőrizheti, hogy a
// tenant időközben másik node-ra került-e. Ld. WsClient.cpp komment: az ESP32-n
// vendorolt WebSockets könyvtár NEM adja tovább a close code-ot, ezért a
// detekció a sikertelen próbálkozások számlálásán alapul, nem a 4009 kódon.
using WsRelocateCb = std::function<void()>;

class WsClient {
public:
    void begin(const String& host, uint16_t port, const String& deviceKey);
    void loop();

    bool isConnected() const { return _connected; }
    bool sendJson(const JsonDocument& doc);

    // Összes bejövő JSON üzenet erre a callback-re kerül
    void onMessage(WsMsgCb cb) { _msgCb = cb; }

    // Ld. WsRelocateCb komment fent.
    void onNeedsRelocate(WsRelocateCb cb) { _relocateCb = cb; }

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

    // Relocate-detekció: egymást követő sikertelen csatlakozások számlálása.
    int  _consecutiveFailures = 0;
    bool _relocatePending     = false;
    static const int RELOCATE_AFTER_FAILURES = 5;

    WsMsgCb       _msgCb;
    WsRelocateCb  _relocateCb;

    void wsEvent(WStype_t type, uint8_t* payload, size_t length);
};

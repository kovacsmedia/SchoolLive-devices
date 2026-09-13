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

// Ekkora WS-üzenet fölött a JSON-fát PSRAM-ban építjük (ld. WsClient.cpp).
// 2 kB alatt minden vezérlő üzenet elfér (PREPARE/PLAY/COMMAND/BEACON_ACK);
// e fölé gyakorlatilag csak a SCHEDULE_SYNC megy.
#define WS_PSRAM_PARSE_THRESHOLD 2048

class WsClient {
public:
    void begin(const String& host, uint16_t port, const String& deviceKey);
    void loop();

    /*
     * "ÉL-E A BACKEND FELÉ AZ ÚT" – nem az, hogy a könyvtár mit hisz.
     *
     * A `_connected` flaget kizárólag a WebSockets könyvtár CONNECTED/
     * DISCONNECTED eseményei állították. Ha a WiFi megvan, de a hálózat
     * mögötte megszűnik (nincs upstream az AP-n), a TCP csendben elhal, és a
     * könyvtár ezt CSAK a következő írási hibából veszi észre – a mérés
     * szerint ~70 másodperc múlva. Addig az eszköz ONLINE-nak hitte magát,
     * a BellManager a backend PREPARE-jére várt, és a csengetés késett.
     *
     * Ezért a kapcsolatot ÉLŐJEL alapján is mérjük: ha a szerver felől
     * WS_SILENCE_TIMEOUT_MS ideig SEMMI nem érkezett (se üzenet, se pong),
     * az út halott, függetlenül attól, mit mond a könyvtár.
     */
    bool isConnected() const { return _connected && _linkAlive; }
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

    // Élőjel-figyelés (ld. isConnected()). A heartbeat 10 mp-enként pingel,
    // a BEACON_ACK 30 mp-enként érkezik – 25 mp néma csatorna tehát már
    // legalább két kimaradt pongot jelent.
    unsigned long _lastRxMs   = 0;
    bool          _linkAlive  = false;
    static const unsigned long WS_SILENCE_TIMEOUT_MS = 25000UL;

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

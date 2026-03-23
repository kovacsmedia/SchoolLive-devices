// ─────────────────────────────────────────────────────────────────────────────
// SnapcastClient.cpp – SchoolLive S3.54
// Változások:
//   • connect(): sendHello() az initI2S() ELŐTT – az I2S init lassú, addigra
//     timeout-olhatott volna a kapcsolat
//   • sendMessage(): _client.flush() a write() után – kényszeríti a TCP küldést,
//     nélküle a WiFiClient bufferben tartja az adatot és a szerver
//     "Error reading message header of length 0"-t lát
// ─────────────────────────────────────────────────────────────────────────────
#include "SnapcastClient.h"
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <sys/time.h>

#define SNAP_I2S_PORT           I2S_NUM_1
#define SNAP_START_THRESHOLD_MS 200
#define SNAP_BYTES_PER_MS       192   // 48000 * 2ch * 2byte / 1000

// ── begin ─────────────────────────────────────────────────────────────────────
void SnapcastClient::begin(const String& mac, uint8_t volume, uint16_t port) {
    _mac    = mac;
    _volume = volume;
    _port   = port;

    _ringSize = SNAP_BUF_SIZE;
    _ringBuf  = (uint8_t*)ps_malloc(_ringSize);
    if (!_ringBuf) {
        Serial.println("[SNAP] PSRAM alloc sikertelen!");
        _ringSize = 32 * 1024;
        _ringBuf  = (uint8_t*)malloc(_ringSize);
    }
    memset(_ringBuf, 0, _ringSize);
    Serial.printf("[SNAP] Ring buffer: %d KB (%s)\n",
                  _ringSize / 1024, psramFound() ? "PSRAM" : "HEAP");

    _bodyBuf = (uint8_t*)ps_malloc(8192);
    if (!_bodyBuf) _bodyBuf = (uint8_t*)malloc(8192);
    _bodySize = 8192;

    Serial.println("[SNAP] SnapcastClient kész");
}

// ── loop ──────────────────────────────────────────────────────────────────────
// loop() – háromállapotú state machine:
//   1. Nincs kapcsolat (_connecting=false, _connected=false) → reconnect timer
//   2. Kapcsolódás folyamatban (_connecting=true, _connected=false) → vár ServerSettings-re
//   3. Csatlakozva (_connected=true) → PCM stream feldolgozás
void SnapcastClient::loop() {
    uint32_t now = millis();

    if (!_connecting && !_connected) {
        // Állapot 1: nincs aktív kapcsolat → reconnect timer
        if (now - _lastConnectMs > _reconnectMs) {
            _lastConnectMs = now;
            connect();
        }
        return;
    }

    if (_connecting && !_connected) {
        // Állapot 2: Hello elküldve, vár ServerSettings-re
        // Timeout: ha 10 másodpercen belül nem jön válasz → disconnect + retry
        if (now - _connectingStartMs > 10000) {
            Serial.println("[SNAP] ServerSettings timeout – újracsatlakozás");
            _connecting = false;
            disconnect();
            return;
        }
        // TCP kapcsolat elszakadt? → disconnect
        if (!_client.connected() && _client.available() == 0) {
            Serial.println("[SNAP] TCP kapcsolat megszakadt várakozás közben");
            _connecting = false;
            disconnect();
            return;
        }
        // Bejövő adat olvasása (ServerSettings érkezhet)
        processIncoming();
        return;
    }

    // Állapot 3: teljesen csatlakozva
    processIncoming();
    drainToI2S();
}

// ── connect ───────────────────────────────────────────────────────────────────
void SnapcastClient::connect() {
    if (!WiFi.isConnected()) return;

    // Előző kapcsolat explicit lezárása (RST elkerülése)
    if (_client.connected() || _connecting) {
        _client.stop();
        delay(10);
    }

    _connected        = false;
    _connecting       = false;
    _headerRecvd      = false;
    _hdrRead          = 0;
    _bodyRead         = 0;
    _readingBody      = false;
    _ringWrite        = _ringRead = _ringFill = 0;
    _playing          = false;
    _msgId            = 0;

    Serial.printf("[SNAP] Csatlakozás: %s:%d\n", _host.c_str(), _port);
    if (!_client.connect(_host.c_str(), _port, 5000)) {
        Serial.println("[SNAP] Kapcsolódás sikertelen");
        return;
    }
    _client.setNoDelay(true);

    // Connecting állapot: Hello el van küldve, vár ServerSettings-re
    _connecting       = true;
    _connectingStartMs = millis();

    // Rövid várakozás a TCP kapcsolat stabilizálódásához
    // (a lwIP stack feldolgozza a SYN-ACK-ot és beállítja a socketet)
    vTaskDelay(pdMS_TO_TICKS(100));

    // Hello ELŐBB, I2S init UTÁNA – az I2S init lassú (~50-200ms)
    sendHello();
    Serial.println("[SNAP] Hello elküldve, várakozás ServerSettings-re...");

    initI2S();
}

// ── disconnect ────────────────────────────────────────────────────────────────
void SnapcastClient::disconnect() {
    _client.stop();
    _connected  = false;
    _connecting = false;
    _playing    = false;
    deinitI2S();
    if (_onDisconnected) _onDisconnected();
    Serial.println("[SNAP] Lecsatlakozva");
}

// ── stop ──────────────────────────────────────────────────────────────────────
void SnapcastClient::stop() {
    disconnect();
}

// ── setVolume ─────────────────────────────────────────────────────────────────
void SnapcastClient::setVolume(uint8_t vol) {
    _volume = vol;
}

// ── processIncoming ───────────────────────────────────────────────────────────
void SnapcastClient::processIncoming() {
    // Ha _connecting=true (vár ServerSettings-re), türelmesebben kezeljük
    // – csak akkor disconnectálunk, ha nincs pending adat sem
    if (!_client.connected() && _client.available() == 0) {
        if (_connected || _connecting) {
            Serial.println("[SNAP] Kapcsolat megszakadt");
            disconnect();
        }
        return;
    }

    while (!_readingBody && _client.available() > 0) {
        int b = _client.read();
        if (b < 0) break;
        _hdrBuf[_hdrRead++] = (uint8_t)b;
        if (_hdrRead < 26) continue;

        _hdrRead = 0;
        uint8_t* p = _hdrBuf;
        _curHdr.type      = p[0] | (p[1] << 8);
        _curHdr.id        = p[2] | (p[3] << 8);
        _curHdr.refersTo  = p[4] | (p[5] << 8);
        _curHdr.sent_sec  = (int32_t)(p[6]|(p[7]<<8)|(p[8]<<16)|(p[9]<<24));
        _curHdr.sent_usec = (int32_t)(p[10]|(p[11]<<8)|(p[12]<<16)|(p[13]<<24));
        _curHdr.recv_sec  = (int32_t)(p[14]|(p[15]<<8)|(p[16]<<16)|(p[17]<<24));
        _curHdr.recv_usec = (int32_t)(p[18]|(p[19]<<8)|(p[20]<<16)|(p[21]<<24));
        _curHdr.size      = p[22]|(p[23]<<8)|(p[24]<<16)|((uint32_t)p[25]<<24);

        _bodyRead    = 0;
        _readingBody = true;
        break;
    }

    if (_readingBody && _curHdr.size > 0) {
        if (_curHdr.size > _bodySize) {
            free(_bodyBuf);
            _bodySize = _curHdr.size + 256;
            _bodyBuf  = (uint8_t*)ps_malloc(_bodySize);
            if (!_bodyBuf) _bodyBuf = (uint8_t*)malloc(_bodySize);
        }

        while (_bodyRead < _curHdr.size && _client.available() > 0) {
            size_t want = _curHdr.size - _bodyRead;
            size_t got  = _client.read(_bodyBuf + _bodyRead, want);
            _bodyRead  += got;
        }
    }

    if (_readingBody && _bodyRead >= _curHdr.size) {
        _readingBody = false;
        processMessage(_curHdr, _bodyBuf, _curHdr.size);
    }
}

// ── processMessage ────────────────────────────────────────────────────────────
void SnapcastClient::processMessage(const SnapHeader& hdr,
                                    const uint8_t* body, uint32_t size) {
    switch (hdr.type) {
        case SNAP_MSG_SERVER_SETTINGS:
            handleServerSettings(body, size);
            _connecting = false;   // ServerSettings megérkezett – connecting vége
            _connected  = true;
            Serial.println("[SNAP] ✅ ServerSettings OK – Snap csatlakozva!");
            if (_onConnected) _onConnected();
            break;
        case SNAP_MSG_CODEC_HEADER:
            handleCodecHeader(body, size);
            break;
        case SNAP_MSG_WIRE_CHUNK:
            handleWireChunk(body, size);
            break;
        case SNAP_MSG_TIME:
            handleTime(body, size);
            break;
        default:
            break;
    }
}

// ── handleServerSettings ──────────────────────────────────────────────────────
void SnapcastClient::handleServerSettings(const uint8_t* body, uint32_t size) {
    JsonDocument doc;
    String json((char*)body, size);
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    _bufferMs     = doc["bufferMs"]  | 1000;
    _latencyMs    = doc["latency"]   | 0;
    _muted        = doc["muted"]     | false;
    _serverVolume = doc["volume"]    | 100;

    Serial.printf("[SNAP] ServerSettings: bufferMs=%d latency=%d vol=%d\n",
                  _bufferMs, _latencyMs, _serverVolume);

    // Első TIME küldése, throttle timer indítása
    _lastTimeSyncMs = millis();
    int64_t us = nowUs();
    sendTime((int32_t)(us / 1000000), (int32_t)(us % 1000000));
}

// ── handleCodecHeader ─────────────────────────────────────────────────────────
void SnapcastClient::handleCodecHeader(const uint8_t* body, uint32_t size) {
    Serial.printf("[SNAP] CodecHeader: %d bytes\n", size);
    _headerRecvd = true;
}

// ── handleWireChunk ───────────────────────────────────────────────────────────
void SnapcastClient::handleWireChunk(const uint8_t* body, uint32_t size) {
    if (size < 8) return;

    int32_t chunkSec  = body[0]|(body[1]<<8)|(body[2]<<16)|(body[3]<<24);
    int32_t chunkUsec = body[4]|(body[5]<<8)|(body[6]<<16)|(body[7]<<24);
    int64_t chunkUs   = (int64_t)chunkSec * 1000000 + chunkUsec;

    const uint8_t* pcm = body + 8;
    uint32_t pcmLen    = size - 8;

    int64_t playAtUs = chunkUs - _serverOffsetUs + (int64_t)_bufferMs * 1000;
    int64_t nowUs_   = nowUs();
    int64_t deltaUs  = playAtUs - nowUs_;

    // Debug: első chunk érkezésekor logolás
    static uint32_t chunkCount = 0;
    if (chunkCount++ < 3) {
        Serial.printf("[SNAP] WireChunk #%d: size=%d pcmLen=%d deltaMs=%.1f bufFill=%d ms\n",
                      chunkCount, size, pcmLen, deltaUs/1000.0f, _bufFillMs);
    }

    if (deltaUs < -100000) {
        static uint32_t lateCount = 0;
        if (lateCount++ < 3) Serial.printf("[SNAP] Késő chunk dobva (delta=%.0f ms)\n", deltaUs/1000.0f);
        return;
    }

    ringWrite(pcm, pcmLen);
    _bufFillMs = _ringFill / SNAP_BYTES_PER_MS;
}

// ── handleTime ────────────────────────────────────────────────────────────────
// Snapcast TIME üzenet: a szerver visszaküldi az ESP32 által küldött
// időbélyeget (sent_*) + a szerver saját fogadási idejét (recv_*).
// A body 8 byte-ja a szerver latency értéke (nem az offset!).
//
// RTT alapú offset számítás:
//   RTT = nowUs - sendTimeUs  (a header sent_* az ESP32 küldési ideje)
//   offset = server_recv - (sendTimeUs + RTT/2)
//          = recv_sec:recv_usec - sendTime - RTT/2
// De a szerver recv idejét nem tudjuk közvetlenül – csak a latency-t küldi.
// Ezért egyszerűsítve: ne számítsunk offsetet TIME-ból, csak válaszoljunk.
// Az időszinkron az NTP-re támaszkodik (már szinkronizálva van).
//
// Loop elkerülés: TIME-ra csak 10 másodpercenként válaszolunk.
void SnapcastClient::handleTime(const uint8_t* body, uint32_t size) {
    if (size < 8) return;

    uint32_t now = millis();

    // Throttle: ne válaszoljunk minden TIME-ra, max 10mp-enként egyszer
    if (now - _lastTimeSyncMs < 10000) return;
    _lastTimeSyncMs = now;

    // Saját aktuális idő visszaküldése a szervernek (RTT méréshez)
    int64_t us = nowUs();
    sendTime((int32_t)(us / 1000000), (int32_t)(us % 1000000));
    Serial.println("[SNAP] TimeSync: válasz elküldve");
}

// ── drainToI2S ────────────────────────────────────────────────────────────────
void SnapcastClient::drainToI2S() {
    if (!_i2sInstalled) return;
    if (_ringFill == 0) return;

    // Debug: első drainkor logolás
    static bool _drainLogged = false;
    if (!_drainLogged && _ringFill > 0) {
        Serial.printf("[SNAP] drainToI2S: ringFill=%d bufFillMs=%d playing=%d\n",
                      _ringFill, _bufFillMs, _playing);
        _drainLogged = true;
    }

    if (!_playing && _bufFillMs < SNAP_START_THRESHOLD_MS) return;

    static uint8_t i2sBuf[1920];
    size_t toRead = min((size_t)1920, _ringFill);
    size_t got    = ringRead(i2sBuf, toRead);
    if (got == 0) return;

    if (_volume < 100) {
        int16_t* samples = (int16_t*)i2sBuf;
        size_t   count   = got / 2;
        for (size_t i = 0; i < count; i++) {
            int32_t s = (int32_t)samples[i] * _volume / 100;
            samples[i] = (int16_t)s;
        }
    }

    size_t written = 0;
    i2s_write(SNAP_I2S_PORT, i2sBuf, got, &written, 10);
    _playing = (written > 0);
}

// ── initI2S ───────────────────────────────────────────────────────────────────
void SnapcastClient::initI2S() {
    if (_i2sInstalled) return;

    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = SNAPCAST_SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 256,
        .use_apll             = false,
        .tx_desc_auto_clear   = true,
        .fixed_mclk           = 0,
    };

    i2s_pin_config_t pins = {
        .bck_io_num   = I2S_BCLK,
        .ws_io_num    = I2S_LRC,
        .data_out_num = I2S_DIN,
        .data_in_num  = I2S_PIN_NO_CHANGE,
    };

    esp_err_t err = i2s_driver_install(SNAP_I2S_PORT, &cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[SNAP] I2S install hiba: %d\n", err);
        return;
    }
    i2s_set_pin(SNAP_I2S_PORT, &pins);
    i2s_zero_dma_buffer(SNAP_I2S_PORT);
    _i2sInstalled = true;
    Serial.println("[SNAP] I2S inicializálva (port 1)");
}

// ── deinitI2S ─────────────────────────────────────────────────────────────────
void SnapcastClient::deinitI2S() {
    if (!_i2sInstalled) return;
    i2s_driver_uninstall(SNAP_I2S_PORT);
    _i2sInstalled = false;
    _playing      = false;
}

// ── sendHello ─────────────────────────────────────────────────────────────────
// Snapcast 0.31 Hello payload formátum: uint32_t JSON hossz (LE) + JSON adat
// (régi verzióban csak nyers JSON volt, 0.29+ verzióban jött a hossz prefix)
void SnapcastClient::sendHello() {
    JsonDocument doc;
    doc["Arch"]                      = "xtensa";
    doc["ClientName"]                = "SchoolLive";
    doc["HostName"]                  = "ESP32-S3";
    doc["ID"]                        = _mac;
    doc["Instance"]                  = 1;
    doc["MAC"]                       = _mac;
    doc["OS"]                        = "FreeRTOS";
    doc["SnapStreamProtocolVersion"] = 2;
    doc["Version"]                   = "0.27.0";

    String json;
    serializeJson(doc, json);
    uint32_t jsonLen = json.length();

    // Payload: 4 byte hossz prefix (little-endian) + JSON
    size_t payloadSize = 4 + jsonLen;
    uint8_t* payload = (uint8_t*)malloc(payloadSize);
    if (!payload) {
        Serial.println("[SNAP] sendHello: malloc hiba!");
        return;
    }
    payload[0] = jsonLen & 0xff;
    payload[1] = (jsonLen >> 8) & 0xff;
    payload[2] = (jsonLen >> 16) & 0xff;
    payload[3] = (jsonLen >> 24) & 0xff;
    memcpy(payload + 4, json.c_str(), jsonLen);

    sendMessage(SNAP_MSG_HELLO, 0, payload, payloadSize);
    free(payload);
    Serial.printf("[SNAP] Hello: %s (json=%d bytes)\n", _mac.c_str(), jsonLen);
}

// ── sendTime ──────────────────────────────────────────────────────────────────
void SnapcastClient::sendTime(int32_t sec, int32_t usec) {
    uint8_t buf[8];
    buf[0] = sec & 0xff;        buf[1] = (sec>>8)&0xff;
    buf[2] = (sec>>16)&0xff;    buf[3] = (sec>>24)&0xff;
    buf[4] = usec & 0xff;       buf[5] = (usec>>8)&0xff;
    buf[6] = (usec>>16)&0xff;   buf[7] = (usec>>24)&0xff;
    sendMessage(SNAP_MSG_TIME, 0, buf, 8);
}

// ── sendMessage ───────────────────────────────────────────────────────────────
// Header külön, payload külön – nincs heap allokáció.
// setNoDelay(true) miatt a lwIP Nagle nélkül küldi azonnal.
// Két kis write() egymás után egyetlen TCP szegmensben mehet ki (Nagle off).
void SnapcastClient::sendMessage(uint16_t type, uint16_t refersTo,
                                  const uint8_t* payload, uint32_t size) {
    if (!_client.connected()) return;

    int64_t us   = nowUs();
    int32_t sec  = (int32_t)(us / 1000000);
    int32_t usec = (int32_t)(us % 1000000);

    uint8_t hdr[26];
    hdr[0] = type & 0xff;       hdr[1] = (type>>8)&0xff;
    hdr[2] = _msgId & 0xff;     hdr[3] = (_msgId>>8)&0xff;
    hdr[4] = refersTo & 0xff;   hdr[5] = (refersTo>>8)&0xff;
    hdr[6]  = sec & 0xff;       hdr[7]  = (sec>>8)&0xff;
    hdr[8]  = (sec>>16)&0xff;   hdr[9]  = (sec>>24)&0xff;
    hdr[10] = usec & 0xff;      hdr[11] = (usec>>8)&0xff;
    hdr[12] = (usec>>16)&0xff;  hdr[13] = (usec>>24)&0xff;
    hdr[14] = sec & 0xff;       hdr[15] = (sec>>8)&0xff;
    hdr[16] = (sec>>16)&0xff;   hdr[17] = (sec>>24)&0xff;
    hdr[18] = usec & 0xff;      hdr[19] = (usec>>8)&0xff;
    hdr[20] = (usec>>16)&0xff;  hdr[21] = (usec>>24)&0xff;
    hdr[22] = size & 0xff;      hdr[23] = (size>>8)&0xff;
    hdr[24] = (size>>16)&0xff;  hdr[25] = (size>>24)&0xff;

    size_t w1 = _client.write(hdr, 26);
    size_t w2 = (payload && size > 0) ? _client.write(payload, size) : 0;

    Serial.printf("[SNAP] sendMessage type=%d total=%d written=%d+%d\n",
                  type, 26+size, w1, w2);

    if (w1 != 26 || (size > 0 && w2 != size)) {
        Serial.printf("[SNAP] Write hiba! w1=%d/26 w2=%d/%d\n", w1, w2, size);
        disconnect();
        return;
    }

    _msgId++;
}

// ── nowUs ─────────────────────────────────────────────────────────────────────
int64_t SnapcastClient::nowUs() const {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

// ── Ring buffer ───────────────────────────────────────────────────────────────
void SnapcastClient::ringWrite(const uint8_t* data, size_t len) {
    if (!_ringBuf || len == 0) return;
    size_t space = _ringSize - _ringFill;
    if (len > space) len = space;

    for (size_t i = 0; i < len; i++) {
        _ringBuf[_ringWrite] = data[i];
        _ringWrite = (_ringWrite + 1) % _ringSize;
    }
    _ringFill += len;
}

size_t SnapcastClient::ringRead(uint8_t* out, size_t maxLen) {
    if (!_ringBuf || _ringFill == 0) return 0;
    size_t toRead = min(maxLen, _ringFill);

    for (size_t i = 0; i < toRead; i++) {
        out[i]    = _ringBuf[_ringRead];
        _ringRead = (_ringRead + 1) % _ringSize;
    }
    _ringFill -= toRead;
    return toRead;
}
// ─────────────────────────────────────────────────────────────────────────────
// SnapcastClient.cpp – SchoolLive S3.54
// I2S arbitráció: AudioManager.suspend() / resume() hívása
// ─────────────────────────────────────────────────────────────────────────────
#include "SnapcastClient.h"
#include "AudioManager.h"
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <sys/time.h>

#define SNAP_I2S_PORT           I2S_NUM_0
#define SNAP_START_THRESHOLD_MS 150
#define SNAP_BYTES_PER_MS       192

void SnapcastClient::begin(const String& mac, uint8_t volume, uint16_t port) {
    _mac = mac; _volume = volume; _port = port;
    _ringSize = SNAP_BUF_SIZE;
    _ringBuf  = (uint8_t*)ps_malloc(_ringSize);
    if (!_ringBuf) { _ringSize = 32*1024; _ringBuf = (uint8_t*)malloc(_ringSize); }
    memset(_ringBuf, 0, _ringSize);
    Serial.printf("[SNAP] Ring buffer: %d KB (%s)\n", _ringSize/1024, psramFound()?"PSRAM":"HEAP");
    _bodyBuf = (uint8_t*)ps_malloc(8192);
    if (!_bodyBuf) _bodyBuf = (uint8_t*)malloc(8192);
    _bodySize = 8192;
    Serial.println("[SNAP] SnapcastClient kész");
}

void SnapcastClient::loop() {
    uint32_t now = millis();
    if (!_connecting && !_connected) {
        if (now - _lastConnectMs > _reconnectMs) { _lastConnectMs = now; connect(); }
        return;
    }
    if (_connecting && !_connected) {
        if (now - _connectingStartMs > 10000) { _connecting = false; disconnect(); return; }
        if (!_client.connected() && _client.available() == 0) { _connecting = false; disconnect(); return; }
        processIncoming(); return;
    }
    processIncoming();
    drainToI2S();
}

void SnapcastClient::connect() {
    if (!WiFi.isConnected()) return;
    if (_client.connected() || _connecting) { _client.stop(); delay(10); }
    _connected = _connecting = _headerRecvd = false;
    _hdrRead = _bodyRead = 0; _readingBody = false;
    _ringWrite = _ringRead = _ringFill = 0;
    _playing = false; _msgId = 0;

    Serial.printf("[SNAP] Csatlakozás: %s:%d\n", _host.c_str(), _port);
    if (!_client.connect(_host.c_str(), _port, 5000)) { Serial.println("[SNAP] Kapcsolódás sikertelen"); return; }
    _client.setNoDelay(true);
    _connecting = true; _connectingStartMs = millis();
    vTaskDelay(pdMS_TO_TICKS(100));
    sendHello();
    Serial.println("[SNAP] Hello elküldve, várakozás ServerSettings-re...");
    initI2S();
}

void SnapcastClient::disconnect() {
    _client.stop(); _connected = _connecting = _playing = false;
    deinitI2S();
    if (_onDisconnected) _onDisconnected();
    Serial.println("[SNAP] Lecsatlakozva");
}

void SnapcastClient::stop() { disconnect(); }
void SnapcastClient::setVolume(uint8_t vol) { _volume = vol; }

void SnapcastClient::processIncoming() {
    if (!_client.connected() && _client.available() == 0) {
        if (_connected || _connecting) { Serial.println("[SNAP] Kapcsolat megszakadt"); disconnect(); }
        return;
    }
    while (!_readingBody && _client.available() > 0) {
        int b = _client.read(); if (b < 0) break;
        _hdrBuf[_hdrRead++] = (uint8_t)b;
        if (_hdrRead < 26) continue;
        _hdrRead = 0;
        uint8_t* p = _hdrBuf;
        _curHdr.type      = p[0]|(p[1]<<8);
        _curHdr.id        = p[2]|(p[3]<<8);
        _curHdr.refersTo  = p[4]|(p[5]<<8);
        _curHdr.sent_sec  = (int32_t)(p[6]|(p[7]<<8)|(p[8]<<16)|(p[9]<<24));
        _curHdr.sent_usec = (int32_t)(p[10]|(p[11]<<8)|(p[12]<<16)|(p[13]<<24));
        _curHdr.recv_sec  = (int32_t)(p[14]|(p[15]<<8)|(p[16]<<16)|(p[17]<<24));
        _curHdr.recv_usec = (int32_t)(p[18]|(p[19]<<8)|(p[20]<<16)|(p[21]<<24));
        _curHdr.size      = p[22]|(p[23]<<8)|(p[24]<<16)|((uint32_t)p[25]<<24);
        _bodyRead = 0; _readingBody = true; break;
    }
    if (_readingBody && _curHdr.size > 0) {
        if (_curHdr.size > _bodySize) {
            free(_bodyBuf); _bodySize = _curHdr.size+256;
            _bodyBuf = (uint8_t*)ps_malloc(_bodySize);
            if (!_bodyBuf) _bodyBuf = (uint8_t*)malloc(_bodySize);
        }
        while (_bodyRead < _curHdr.size && _client.available() > 0) {
            size_t got = _client.read(_bodyBuf+_bodyRead, _curHdr.size-_bodyRead);
            _bodyRead += got;
        }
    }
    if (_readingBody && _bodyRead >= _curHdr.size) {
        _readingBody = false;
        processMessage(_curHdr, _bodyBuf, _curHdr.size);
    }
}

void SnapcastClient::processMessage(const SnapHeader& hdr, const uint8_t* body, uint32_t size) {
    switch (hdr.type) {
        case SNAP_MSG_SERVER_SETTINGS:
            handleServerSettings(body, size);
            _connecting = false; _connected = true;
            Serial.println("[SNAP] ✅ ServerSettings OK – Snap csatlakozva!");
            if (_onConnected) _onConnected();
            break;
        case SNAP_MSG_CODEC_HEADER: handleCodecHeader(body, size); break;
        case SNAP_MSG_WIRE_CHUNK:   handleWireChunk(body, size);   break;
        case SNAP_MSG_TIME:         handleTime(body, size);         break;
    }
}

void SnapcastClient::handleServerSettings(const uint8_t* body, uint32_t size) {
    JsonDocument doc; String json((char*)body, size);
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;
    _bufferMs = doc["bufferMs"]|1000; _latencyMs = doc["latency"]|0;
    _muted = doc["muted"]|false; _serverVolume = doc["volume"]|100;
    Serial.printf("[SNAP] ServerSettings: bufferMs=%d latency=%d vol=%d\n", _bufferMs, _latencyMs, _serverVolume);
    _lastTimeSyncMs = millis();
    int64_t us = nowUs();
    sendTime((int32_t)(us/1000000), (int32_t)(us%1000000));
}

void SnapcastClient::handleCodecHeader(const uint8_t* body, uint32_t size) {
    Serial.printf("[SNAP] CodecHeader: %d bytes\n", size);
    _headerRecvd = true;
}

void SnapcastClient::handleWireChunk(const uint8_t* body, uint32_t size) {
    if (size < 8) return;
    const uint8_t* pcm = body + 8;
    uint32_t pcmLen = size - 8;
    if (_ringFill + pcmLen > _ringSize) {
        uint32_t toDrop = (_ringFill + pcmLen) - _ringSize;
        if (toDrop > _ringFill) toDrop = _ringFill;
        _ringRead = (_ringRead + toDrop) % _ringSize;
        _ringFill -= toDrop;
    }
    ringWrite(pcm, pcmLen);
    _bufFillMs = _ringFill / SNAP_BYTES_PER_MS;
}

void SnapcastClient::handleTime(const uint8_t* body, uint32_t size) {
    if (size < 8) return;
    uint32_t now = millis();
    if (now - _lastTimeSyncMs < 10000) return;
    _lastTimeSyncMs = now;
    int64_t us = nowUs();
    sendTime((int32_t)(us/1000000), (int32_t)(us%1000000));
    Serial.println("[SNAP] TimeSync: válasz elküldve");
}

void SnapcastClient::drainToI2S() {
    if (!_i2sInstalled || _ringFill == 0) return;
    if (!_playing && _bufFillMs < SNAP_START_THRESHOLD_MS) return;
    if (!_playing) { Serial.printf("[SNAP] Lejátszás indul: bufFill=%d ms\n", _bufFillMs); _playing = true; }
    static uint8_t i2sBuf[1920];
    size_t got = ringRead(i2sBuf, min((size_t)1920, _ringFill));
    if (got == 0) return;
    if (_volume < 100) {
        int16_t* s = (int16_t*)i2sBuf; size_t cnt = got/2;
        for (size_t i = 0; i < cnt; i++) { int32_t v = (int32_t)s[i]*_volume/100; s[i]=(int16_t)v; }
    }
    size_t written = 0;
    i2s_write(SNAP_I2S_PORT, i2sBuf, got, &written, 10);
    if (written == 0) _playing = false;
}

void SnapcastClient::initI2S() {
    if (_i2sInstalled) return;
    // AudioManager felszabadítja az I2S drivert a Snapcast számára
    if (_audioMgr) { _audioMgr->suspend(); vTaskDelay(pdMS_TO_TICKS(30)); }
    i2s_config_t cfg = {
        .mode=(i2s_mode_t)(I2S_MODE_MASTER|I2S_MODE_TX),
        .sample_rate=SNAPCAST_SAMPLE_RATE,
        .bits_per_sample=I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format=I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format=I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags=ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count=8, .dma_buf_len=256,
        .use_apll=false, .tx_desc_auto_clear=true, .fixed_mclk=0,
    };
    i2s_pin_config_t pins = {
        .bck_io_num=I2S_BCLK, .ws_io_num=I2S_LRC,
        .data_out_num=I2S_DIN, .data_in_num=I2S_PIN_NO_CHANGE,
    };
    esp_err_t err = i2s_driver_install(SNAP_I2S_PORT, &cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[SNAP] I2S install hiba: %d\n", err);
        if (_audioMgr) _audioMgr->resume();
        return;
    }
    i2s_set_pin(SNAP_I2S_PORT, &pins);
    i2s_zero_dma_buffer(SNAP_I2S_PORT);
    _i2sInstalled = true;
    Serial.println("[SNAP] I2S inicializálva (Snap átvette I2S_NUM_0)");
}

void SnapcastClient::deinitI2S() {
    if (!_i2sInstalled) return;
    i2s_driver_uninstall(SNAP_I2S_PORT);
    _i2sInstalled = false; _playing = false;
    if (_audioMgr) { vTaskDelay(pdMS_TO_TICKS(30)); _audioMgr->resume(); }
    Serial.println("[SNAP] I2S felszabadítva – AudioManager visszakapta");
}

void SnapcastClient::sendHello() {
    JsonDocument doc;
    doc["Arch"]="xtensa"; doc["ClientName"]="SchoolLive"; doc["HostName"]="ESP32-S3";
    doc["ID"]=_mac; doc["Instance"]=1; doc["MAC"]=_mac; doc["OS"]="FreeRTOS";
    doc["SnapStreamProtocolVersion"]=2; doc["Version"]="0.27.0";
    String json; serializeJson(doc, json);
    uint32_t jsonLen = json.length();
    size_t pSize = 4+jsonLen; uint8_t* p = (uint8_t*)malloc(pSize); if (!p) return;
    p[0]=jsonLen&0xff; p[1]=(jsonLen>>8)&0xff; p[2]=(jsonLen>>16)&0xff; p[3]=(jsonLen>>24)&0xff;
    memcpy(p+4, json.c_str(), jsonLen);
    sendMessage(SNAP_MSG_HELLO, 0, p, pSize); free(p);
    Serial.printf("[SNAP] Hello: %s\n", _mac.c_str());
}

void SnapcastClient::sendTime(int32_t sec, int32_t usec) {
    uint8_t buf[8];
    buf[0]=sec&0xff; buf[1]=(sec>>8)&0xff; buf[2]=(sec>>16)&0xff; buf[3]=(sec>>24)&0xff;
    buf[4]=usec&0xff; buf[5]=(usec>>8)&0xff; buf[6]=(usec>>16)&0xff; buf[7]=(usec>>24)&0xff;
    sendMessage(SNAP_MSG_TIME, 0, buf, 8);
}

void SnapcastClient::sendMessage(uint16_t type, uint16_t refersTo, const uint8_t* payload, uint32_t size) {
    if (!_client.connected()) return;
    int64_t us=nowUs(); int32_t sec=(int32_t)(us/1000000); int32_t usec=(int32_t)(us%1000000);
    uint8_t hdr[26];
    hdr[0]=type&0xff; hdr[1]=(type>>8)&0xff; hdr[2]=_msgId&0xff; hdr[3]=(_msgId>>8)&0xff;
    hdr[4]=refersTo&0xff; hdr[5]=(refersTo>>8)&0xff;
    hdr[6]=sec&0xff; hdr[7]=(sec>>8)&0xff; hdr[8]=(sec>>16)&0xff; hdr[9]=(sec>>24)&0xff;
    hdr[10]=usec&0xff; hdr[11]=(usec>>8)&0xff; hdr[12]=(usec>>16)&0xff; hdr[13]=(usec>>24)&0xff;
    hdr[14]=sec&0xff; hdr[15]=(sec>>8)&0xff; hdr[16]=(sec>>16)&0xff; hdr[17]=(sec>>24)&0xff;
    hdr[18]=usec&0xff; hdr[19]=(usec>>8)&0xff; hdr[20]=(usec>>16)&0xff; hdr[21]=(usec>>24)&0xff;
    hdr[22]=size&0xff; hdr[23]=(size>>8)&0xff; hdr[24]=(size>>16)&0xff; hdr[25]=(size>>24)&0xff;
    size_t w1=_client.write(hdr,26);
    size_t w2=(payload&&size>0)?_client.write(payload,size):0;
    if (w1!=26||(size>0&&w2!=size)) { Serial.printf("[SNAP] Write hiba!\n"); disconnect(); return; }
    _msgId++;
}

int64_t SnapcastClient::nowUs() const {
    struct timeval tv; gettimeofday(&tv,nullptr);
    return (int64_t)tv.tv_sec*1000000+tv.tv_usec;
}

void SnapcastClient::ringWrite(const uint8_t* data, size_t len) {
    if (!_ringBuf||len==0) return;
    size_t space=_ringSize-_ringFill; if (len>space) len=space;
    for (size_t i=0;i<len;i++) { _ringBuf[_ringWrite]=data[i]; _ringWrite=(_ringWrite+1)%_ringSize; }
    _ringFill+=len;
}

size_t SnapcastClient::ringRead(uint8_t* out, size_t maxLen) {
    if (!_ringBuf||_ringFill==0) return 0;
    size_t toRead=min(maxLen,_ringFill);
    for (size_t i=0;i<toRead;i++) { out[i]=_ringBuf[_ringRead]; _ringRead=(_ringRead+1)%_ringSize; }
    _ringFill-=toRead; return toRead;
}
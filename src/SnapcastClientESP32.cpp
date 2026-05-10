#include "SnapcastClientESP32.h"

#include <driver/i2s.h>

#define SNAP_TYPE_CODEC_HEADER     1
#define SNAP_TYPE_WIRE_CHUNK       2
#define SNAP_TYPE_SERVER_SETTINGS  3
#define SNAP_TYPE_TIME             4
#define SNAP_TYPE_HELLO            5

#define SNAP_HEADER_SIZE           26
#define SNAP_RECONNECT_DELAY_MS    3000
#define SNAP_READ_TIMEOUT_MS       8000

#define SNAP_QUEUE_LENGTH          240
#define SNAP_INITIAL_PREBUFFER     75

#define SNAP_I2S_PORT              I2S_NUM_0

SnapcastClientESP32::SnapcastClientESP32() {
}

void SnapcastClientESP32::begin(
    const String& host,
    uint16_t port,
    const String& deviceId,
    uint8_t localVolume
) {
    _host = host;
    _port = port;
    _deviceId = deviceId;
    _localVolume = localVolume;

    if (_pcmQueue == nullptr) {
        _pcmQueue = xQueueCreate(SNAP_QUEUE_LENGTH, sizeof(PcmBlock));
    }

    Serial.printf(
        "[SNAP] begin host=%s port=%u deviceId=%s\n",
        _host.c_str(),
        _port,
        _deviceId.c_str()
    );
}

void SnapcastClientESP32::start() {
    if (_started) return;

    if (_host.length() == 0 || _port == 0 || _deviceId.length() == 0) {
        Serial.println("[SNAP] start skipped: incomplete config");
        return;
    }

    _running = true;
    _started = true;
    _pausedForLocalPlayback = false;

    if (_pcmQueue == nullptr) {
        _pcmQueue = xQueueCreate(SNAP_QUEUE_LENGTH, sizeof(PcmBlock));
    }

    xTaskCreatePinnedToCore(
        netTaskThunk,
        "SnapNet",
        12288,
        this,
        1,
        &_netTaskHandle,
        0
    );

    xTaskCreatePinnedToCore(
        audioTaskThunk,
        "SnapAudio",
        8192,
        this,
        2,
        &_audioTaskHandle,
        1
    );

    Serial.println("[SNAP] tasks started");
}

void SnapcastClientESP32::stop() {
    _running = false;
    _started = false;
    _pausedForLocalPlayback = false;

    disconnect();
    clearQueue();
    stopI2S();
}

bool SnapcastClientESP32::isStarted() const {
    return _started;
}

bool SnapcastClientESP32::isConnected() const {
    return _connected;
}

void SnapcastClientESP32::setLocalVolume(uint8_t volume) {
    if (volume < 1) volume = 1;
    if (volume > 10) volume = 10;

    _localVolume = volume;
}

void SnapcastClientESP32::pauseForLocalPlayback() {
    if (_pausedForLocalPlayback) return;

    Serial.println("[SNAP] pauseForLocalPlayback: releasing I2S");

    _pausedForLocalPlayback = true;

    clearQueue();
    stopI2S();
}

void SnapcastClientESP32::resumeAfterLocalPlayback() {
    if (!_pausedForLocalPlayback) return;

    Serial.println("[SNAP] resumeAfterLocalPlayback: Snapcast may take I2S again");

    clearQueue();
    _pausedForLocalPlayback = false;
}

bool SnapcastClientESP32::isPausedForLocalPlayback() const {
    return _pausedForLocalPlayback;
}

void SnapcastClientESP32::netTaskThunk(void* arg) {
    static_cast<SnapcastClientESP32*>(arg)->netTask();
}

void SnapcastClientESP32::audioTaskThunk(void* arg) {
    static_cast<SnapcastClientESP32*>(arg)->audioTask();
}

void SnapcastClientESP32::netTask() {
    while (_running) {
        if (!_client.connected()) {
            _connected = false;
            clearQueue();

            if (connectToServer()) {
                _connected = true;

                sendHello();
                delay(150);
                sendTimeRequest();

                Serial.println("[SNAP] connected and hello sent");

                readLoop();
            }

            _connected = false;
            disconnect();
            clearQueue();

            if (_running) {
                delay(SNAP_RECONNECT_DELAY_MS);
            }
        }

        delay(10);
    }

    vTaskDelete(nullptr);
}

void SnapcastClientESP32::audioTask() {
    bool buffered = false;

    while (_running) {
        if (_pausedForLocalPlayback) {
            buffered = false;
            delay(50);
            continue;
        }

        if (!_i2sReady) {
            if (!initI2S()) {
                delay(500);
                continue;
            }
        }

        if (!buffered) {
            if (_pcmQueue == nullptr || uxQueueMessagesWaiting(_pcmQueue) < SNAP_INITIAL_PREBUFFER) {
                delay(10);
                continue;
            }

            Serial.printf(
                "[SNAP] audio prebuffer ready: %u chunks\n",
                (unsigned)uxQueueMessagesWaiting(_pcmQueue)
            );

            buffered = true;
        }

        PcmBlock block{};

        if (xQueueReceive(_pcmQueue, &block, pdMS_TO_TICKS(500)) != pdTRUE) {
            Serial.println("[SNAP] audio queue underrun, waiting");
            buffered = false;
            continue;
        }

        if (_pausedForLocalPlayback) {
            if (block.data) free(block.data);
            buffered = false;
            continue;
        }

        if (block.data == nullptr || block.len == 0) {
            if (block.data) free(block.data);
            continue;
        }

        applyVolumeInPlace(block.data, block.len);

        size_t offset = 0;

        while (_running && !_pausedForLocalPlayback && offset < block.len) {
            size_t partWritten = 0;

            esp_err_t err = i2s_write(
                SNAP_I2S_PORT,
                block.data + offset,
                block.len - offset,
                &partWritten,
                portMAX_DELAY
            );

            if (err != ESP_OK) {
                Serial.printf("[SNAP] i2s_write error: %d\n", (int)err);
                break;
            }

            if (partWritten == 0) {
                delay(1);
                continue;
            }

            offset += partWritten;
        }

        free(block.data);
    }

    stopI2S();
    vTaskDelete(nullptr);
}

bool SnapcastClientESP32::connectToServer() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[SNAP] WiFi not connected");
        return false;
    }

    Serial.printf("[SNAP] connecting %s:%u\n", _host.c_str(), _port);

    _client.stop();
    _client.setTimeout(SNAP_READ_TIMEOUT_MS / 1000);

    bool ok = _client.connect(_host.c_str(), _port, 5000);

    if (!ok) {
        Serial.println("[SNAP] connect failed");
        return false;
    }

    Serial.println("[SNAP] TCP connected");
    return true;
}

void SnapcastClientESP32::disconnect() {
    if (_client.connected()) {
        _client.stop();
    }

    _connected = false;
}

bool SnapcastClientESP32::sendHello() {
    if (!_client.connected()) return false;

    JsonDocument doc;

    doc["MAC"] = WiFi.macAddress();
    doc["HostName"] = _deviceId;
    doc["Version"] = "0.26.0";
    doc["ClientName"] = _deviceId;
    doc["OS"] = "ESP32-S3";
    doc["Arch"] = "xtensa";
    doc["Instance"] = 1;
    doc["ID"] = _deviceId;
    doc["SnapStreamProtocolVersion"] = 2;

    String json;
    serializeJson(doc, json);

    const uint32_t jsonLen = json.length();
    const uint32_t payloadSize = 4 + jsonLen;

    uint8_t header[SNAP_HEADER_SIZE];
    buildHeader(header, SNAP_TYPE_HELLO, payloadSize);

    uint8_t* payload = (uint8_t*)malloc(payloadSize);
    if (!payload) return false;

    writeU32LE(payload, jsonLen);
    memcpy(payload + 4, json.c_str(), jsonLen);

    _client.write(header, SNAP_HEADER_SIZE);
    _client.write(payload, payloadSize);
    _client.flush();

    free(payload);

    Serial.printf("[SNAP] HELLO sent id=%s\n", _deviceId.c_str());
    return true;
}

bool SnapcastClientESP32::sendTimeRequest() {
    if (!_client.connected()) return false;

    uint64_t nowUs = (uint64_t)millis() * 1000ULL;

    uint8_t payload[8];
    writeU32LE(payload + 0, (uint32_t)(nowUs / 1000000ULL));
    writeU32LE(payload + 4, (uint32_t)(nowUs % 1000000ULL));

    uint8_t header[SNAP_HEADER_SIZE];
    buildHeader(header, SNAP_TYPE_TIME, sizeof(payload));

    _client.write(header, SNAP_HEADER_SIZE);
    _client.write(payload, sizeof(payload));
    _client.flush();

    Serial.println("[SNAP] TIME request sent");
    return true;
}

bool SnapcastClientESP32::readLoop() {
    uint8_t header[SNAP_HEADER_SIZE];

    while (_running && _client.connected()) {
        if (!readFully(header, SNAP_HEADER_SIZE, SNAP_READ_TIMEOUT_MS)) {
            Serial.println("[SNAP] header read failed");
            return false;
        }

        uint16_t type = readU16LE(header + 0);

        uint32_t sec = readU32LE(header + 6);
        uint32_t usec = readU32LE(header + 10);

        uint32_t payloadSize = readU32LE(header + 22);

        if (payloadSize > 512 * 1024) {
            Serial.printf("[SNAP] invalid payload size: %u\n", payloadSize);
            return false;
        }

        uint8_t* payload = nullptr;

        if (payloadSize > 0) {
            payload = (uint8_t*)malloc(payloadSize);

            if (!payload) {
                Serial.printf("[SNAP] malloc failed: %u\n", payloadSize);
                return false;
            }

            if (!readFully(payload, payloadSize, SNAP_READ_TIMEOUT_MS)) {
                Serial.println("[SNAP] payload read failed");
                free(payload);
                return false;
            }
        }

        switch (type) {
            case SNAP_TYPE_CODEC_HEADER:
                handleCodecHeader(payload, payloadSize);
                break;

            case SNAP_TYPE_WIRE_CHUNK:
                handleWireChunk(payload, payloadSize);
                break;

            case SNAP_TYPE_SERVER_SETTINGS:
                handleServerSettings(payload, payloadSize);
                break;

            case SNAP_TYPE_TIME:
                handleTimeResponse(sec, usec);
                break;

            default:
                break;
        }

        if (payload) free(payload);
    }

    return false;
}

bool SnapcastClientESP32::readFully(uint8_t* buf, size_t len, uint32_t timeoutMs) {
    size_t offset = 0;
    unsigned long start = millis();

    while (_running && offset < len) {
        if (!_client.connected()) return false;

        int available = _client.available();

        if (available <= 0) {
            if (millis() - start > timeoutMs) {
                return false;
            }

            delay(1);
            continue;
        }

        int n = _client.read(buf + offset, len - offset);

        if (n < 0) return false;

        if (n > 0) {
            offset += n;
            start = millis();
        }
    }

    return offset == len;
}

void SnapcastClientESP32::handleCodecHeader(const uint8_t* payload, size_t len) {
    if (!payload || len < 8) return;

    uint32_t nameLen = readU32LE(payload);

    if (nameLen == 0 || 4 + nameLen > len) {
        Serial.printf("[SNAP] invalid codec name length: %u\n", nameLen);
        return;
    }

    String codec;
    codec.reserve(nameLen + 1);

    for (uint32_t i = 0; i < nameLen; i++) {
        codec += (char)payload[4 + i];
    }

    Serial.printf("[SNAP] codec=%s\n", codec.c_str());

    size_t pos = 4 + nameLen;

    if (!codec.equalsIgnoreCase("pcm")) {
        Serial.println("[SNAP] only PCM is supported for now");
        return;
    }

    if (pos + 4 > len) return;

    uint32_t headerSize = readU32LE(payload + pos);
    pos += 4;

    if (headerSize >= 28 && pos + 28 <= len) {
        pos += 20;

        uint16_t audioFormat = readU16LE(payload + pos);
        pos += 2;

        uint16_t ch = readU16LE(payload + pos);
        pos += 2;

        uint32_t rate = readU32LE(payload + pos);
        pos += 4;

        pos += 4;
        pos += 2;

        uint16_t bits = readU16LE(payload + pos);

        _sampleRate = (int)rate;
        _channels = (int)ch;
        _bits = (int)bits;

        Serial.printf(
            "[SNAP] PCM format: fmt=%u ch=%u rate=%u bits=%u\n",
            audioFormat,
            ch,
            rate,
            bits
        );

        if (!_pausedForLocalPlayback) {
            stopI2S();
            initI2S();
        }

        clearQueue();
    }
}

void SnapcastClientESP32::handleWireChunk(const uint8_t* payload, size_t len) {
    if (!payload || len <= 12) return;

    uint32_t sec = readU32LE(payload + 0);
    uint32_t usec = readU32LE(payload + 4);
    uint32_t pcmSize = readU32LE(payload + 8);

    if (pcmSize == 0) return;

    if (len < 12 + pcmSize) {
        Serial.printf(
            "[SNAP] WireChunk too short: len=%u pcmSize=%u\n",
            (unsigned)len,
            (unsigned)pcmSize
        );
        return;
    }

    if (_pausedForLocalPlayback) {
        return;
    }

    uint32_t serverMs = sec * 1000UL + usec / 1000UL;

    // Fontos: PCM a 12. bájttól indul, nem a 8.-tól.
    enqueuePcm(payload + 12, pcmSize, serverMs);
}

void SnapcastClientESP32::handleServerSettings(const uint8_t* payload, size_t len) {
    if (!payload || len < 4) return;

    uint32_t jsonLen = readU32LE(payload);

    if (jsonLen == 0 || 4 + jsonLen > len) {
        Serial.printf("[SNAP] invalid settings json length: %u\n", jsonLen);
        return;
    }

    String jsonStr;
    jsonStr.reserve(jsonLen + 1);

    for (uint32_t i = 0; i < jsonLen; i++) {
        jsonStr += (char)payload[4 + i];
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonStr);

    if (err) {
        Serial.printf("[SNAP] settings JSON error: %s\n", err.c_str());
        return;
    }

    _serverMuted = doc["muted"] | false;
    _serverVolume = doc["volume"] | 100;

    if (_serverVolume > 100) _serverVolume = 100;

    Serial.printf(
        "[SNAP] settings: muted=%d volume=%u\n",
        _serverMuted ? 1 : 0,
        _serverVolume
    );
}

void SnapcastClientESP32::handleTimeResponse(uint32_t sec, uint32_t usec) {
    Serial.printf("[SNAP] TIME response: %u.%06u\n", sec, usec);
}

bool SnapcastClientESP32::initI2S() {
    if (_pausedForLocalPlayback) return false;
    if (_i2sReady) return true;

    if (_sampleRate <= 0) _sampleRate = 48000;
    if (_channels <= 0) _channels = 2;
    if (_bits <= 0) _bits = 16;

    i2s_config_t config = {};
    config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate = _sampleRate;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 12;
    config.dma_buf_len = 512;
    config.use_apll = false;
    config.tx_desc_auto_clear = true;
    config.fixed_mclk = 0;

    esp_err_t err = i2s_driver_install(SNAP_I2S_PORT, &config, 0, nullptr);

    if (err != ESP_OK) {
        Serial.printf("[SNAP] i2s_driver_install failed: %d\n", (int)err);
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.bck_io_num = I2S_BCLK;
    pins.ws_io_num = I2S_LRC;
    pins.data_out_num = I2S_DIN;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    pins.mck_io_num = I2S_PIN_NO_CHANGE;

    err = i2s_set_pin(SNAP_I2S_PORT, &pins);

    if (err != ESP_OK) {
        Serial.printf("[SNAP] i2s_set_pin failed: %d\n", (int)err);
        i2s_driver_uninstall(SNAP_I2S_PORT);
        return false;
    }

    i2s_zero_dma_buffer(SNAP_I2S_PORT);

    _i2sReady = true;

    Serial.printf(
        "[SNAP] I2S ready: rate=%d ch=%d bits=%d BCLK=%d LRC=%d DIN=%d\n",
        _sampleRate,
        _channels,
        _bits,
        I2S_BCLK,
        I2S_LRC,
        I2S_DIN
    );

    return true;
}

void SnapcastClientESP32::stopI2S() {
    if (!_i2sReady) return;

    i2s_driver_uninstall(SNAP_I2S_PORT);
    _i2sReady = false;

    Serial.println("[SNAP] I2S stopped");
}

void SnapcastClientESP32::enqueuePcm(const uint8_t* data, size_t len, uint32_t serverMs) {
    if (!_pcmQueue || !data || len == 0) return;
    if (_pausedForLocalPlayback) return;

    uint8_t* copy = nullptr;

    #if CONFIG_SPIRAM_SUPPORT
    copy = (uint8_t*)ps_malloc(len);
    #endif

    if (!copy) {
        copy = (uint8_t*)malloc(len);
    }

    if (!copy) {
        Serial.printf("[SNAP] PCM malloc failed: %u\n", (unsigned)len);
        return;
    }

    memcpy(copy, data, len);

    PcmBlock block;
    block.data = copy;
    block.len = len;
    block.serverMs = serverMs;

    if (xQueueSend(_pcmQueue, &block, pdMS_TO_TICKS(250)) != pdTRUE) {
        Serial.println("[SNAP] PCM queue full, dropping block");
        free(copy);
    }
}

void SnapcastClientESP32::clearQueue() {
    if (!_pcmQueue) return;

    PcmBlock block{};

    while (xQueueReceive(_pcmQueue, &block, 0) == pdTRUE) {
        if (block.data) {
            free(block.data);
        }
    }
}

void SnapcastClientESP32::applyVolumeInPlace(uint8_t* data, size_t len) {
    if (!data || len < 2) return;

    int effective = 0;

    if (!_serverMuted) {
        effective = ((int)_serverVolume * (int)_localVolume) / 10;
    }

    if (effective > 100) effective = 100;
    if (effective < 0) effective = 0;

    if (effective == 100) return;

    size_t samples = len / 2;

    for (size_t i = 0; i < samples; i++) {
        int16_t s = (int16_t)((data[i * 2 + 1] << 8) | data[i * 2]);

        int32_t v = ((int32_t)s * effective) / 100;

        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;

        int16_t out = (int16_t)v;

        data[i * 2] = out & 0xFF;
        data[i * 2 + 1] = (out >> 8) & 0xFF;
    }
}

uint16_t SnapcastClientESP32::readU16LE(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t SnapcastClientESP32::readU32LE(const uint8_t* p) {
    return
        ((uint32_t)p[0]) |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

void SnapcastClientESP32::writeU16LE(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}

void SnapcastClientESP32::writeU32LE(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

void SnapcastClientESP32::buildHeader(uint8_t* out, uint16_t type, uint32_t payloadSize) {
    memset(out, 0, SNAP_HEADER_SIZE);

    writeU16LE(out + 0, type);
    writeU16LE(out + 2, 0);
    writeU16LE(out + 4, 0);

    writeU32LE(out + 6, 0);
    writeU32LE(out + 10, 0);

    writeU32LE(out + 14, 0);
    writeU32LE(out + 18, 0);

    writeU32LE(out + 22, payloadSize);
}
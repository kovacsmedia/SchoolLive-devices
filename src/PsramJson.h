#ifndef PSRAMJSON_H
#define PSRAMJSON_H

/*
 * PSRAM-alapú ArduinoJson allokátor.
 *
 * MIÉRT KELL
 * ----------
 * Az ESP32-S3 N16R8-on 8 MB külső PSRAM van, de a BELSŐ DRAM csak ~320 kB, és
 * a mért mélypont 61 kB volt – veszélyesen kevés. A belső DRAM-ot a FreeRTOS
 * task-stackek, a WiFi/LWIP DMA pufferek és az mbedTLS foglalják; ezek nem
 * mozgathatók. Ami VISZONT igen: a JSON-fák.
 *
 * A sdkconfig `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` beállítása miatt minden
 * ennél kisebb malloc a belső DRAM-ba megy. A nagy JSON-dokumentumok viszont
 * nem egy darabban foglalnak, hanem sok kis blokkban (ArduinoJson pool-onként
 * növekszik), tehát a küszöb önmagában NEM tereli őket PSRAM-ba – kell ez az
 * explicit allokátor.
 *
 * MEKKORA A TÉT
 * -------------
 * A `/bells/sync` válasza (teljes tanév: sablonok + naptár-kivételek) és a
 * belőle készülő NVS-mentés egyszerre HÁROM másolatot tartott a belső DRAM-ban
 * (a válasz-fa, a kimásolt `fy` fa, és a szerializált String, ami akár 24 kB).
 * Ez volt a heap-mélypont fő oka.
 *
 * VISSZAESÉS PSRAM NÉLKÜL
 * -----------------------
 * Egy bináris megy minden okoshangszóróra, és nem minden panelon lesz PSRAM.
 * Ha a PSRAM-foglalás nem sikerül (nincs PSRAM, vagy elfogyott), automatikusan
 * a belső heap-re esünk vissza – tehát a működés SOSEM bukik el ezen, csak a
 * memória-előny marad el.
 *
 * SEBESSÉG
 * --------
 * A PSRAM lassabb (OPI 80 MHz), de a JSON-feldolgozás nem valós idejű út: a
 * csengetés időzítését a már kiolvasott `_entries[]` tömb hajtja, nem a fa.
 * Az audio-út (I2S DMA, snap chunk) érintetlen – az explicit MALLOC_CAP_DMA /
 * belső foglalásokkal dolgozik.
 */

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

class PsramJsonAllocator : public ArduinoJson::Allocator {
public:
    void* allocate(size_t size) override {
        void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) return p;
        // Nincs (vagy elfogyott a) PSRAM – belső heap, hogy a működés megmaradjon.
        return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    void deallocate(void* pointer) override {
        heap_caps_free(pointer);
    }

    void* reallocate(void* ptr, size_t new_size) override {
        void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) return p;
        return heap_caps_realloc(ptr, new_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    /** Közös példány – állapotmentes, szálbiztos. */
    static PsramJsonAllocator& instance() {
        static PsramJsonAllocator inst;
        return inst;
    }
};

/** Nagy JSON-dokumentumokhoz: `PSRAM_JSON_DOC(doc);` */
#define PSRAM_JSON_DOC(name) JsonDocument name(&PsramJsonAllocator::instance())

/**
 * Nagy szöveg-puffer PSRAM-ban, automatikus felszabadítással.
 * A String helyett használjuk ott, ahol tíz kB nagyságrendű JSON-t
 * szerializálunk vagy olvasunk NVS-ből.
 */
class PsramBuffer {
public:
    explicit PsramBuffer(size_t bytes) {
        _p = (char*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_p) _p = (char*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (_p) { _size = bytes; _p[0] = '\0'; }
    }
    ~PsramBuffer() { if (_p) heap_caps_free(_p); }

    PsramBuffer(const PsramBuffer&)            = delete;
    PsramBuffer& operator=(const PsramBuffer&) = delete;

    char*  data()  const { return _p; }
    size_t size()  const { return _size; }
    bool   valid() const { return _p != nullptr; }

private:
    char*  _p    = nullptr;
    size_t _size = 0;
};

#endif

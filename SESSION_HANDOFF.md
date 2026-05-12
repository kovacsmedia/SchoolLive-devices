# SchoolLive ESP32 snapclient — session handoff

Dátum: 2026-05-12
Téma: ESP32-S3 snapclient Opus stabilizálása, kezdeti hang-megtorpanás

---

## 1. Eredeti probléma

ESP32-S3 snapclient Opus codec lejátszáskor a hang akadozott / megtorpant
csengetés és TTS elején. A backenden (snapserver) és a klienseken is friss
Opus codec van bevezetve.

## 2. Diagnózis menete

### Megfigyelt tünet
A monitor logban **kb. 2 másodpercenként** `RESYNCING HARD 2` üzenetek,
mindig negatív `age` értékkel (-30..-660 ms). Minden hard resync mute +
DMA restart = hallható megakadás.

### Próbált hipotézisek

1. **Opus chunk-méret eltérés** — KIZÁRVA debug logokkal:
   - `spf_hdr=960, decoded=960, alloc_bytes=3840, ts_delta_us=20000`
   - Minden chunk pontosan 20ms @ 48kHz stereo

2. **Snapserver hiba** — KIZÁRVA:
   - A snapserver `onResync` értékek sub-ms-ek (0.02-1.5 ms)
   - Csak az idle→playing átmenetnél van nagy érték (604ms)

3. **Network jitter + szigorú threshold** — IGAZOLVA:
   - `hardResyncThreshold = 2000us` (2ms) abszurd szűk
   - TIME message median filter zaja simán átlépi
   - Minden ~2 sec-re az age median elsodródik → hard resync

4. **ESP32-S3 drift-korrekció hiányzik** — IGAZOLVA:
   - `rtc_clk_apll_coeff_calc()` **NINCS implementálva ESP32-S3-ra** az
     ESP-IDF 5.5.4-ben. Csak ESP32, ESP32-S2, ESP32-P4 verziókban van.
     → A `player.c` 3 db `ERROR, fi2s_clk` üzenete induláskor
     → Az `adjust_apll()` no-op az S3-on
   - `CONFIG_USE_SAMPLE_INSERTION` nem volt definiálva a sdkconfig-ban
   - Tehát **semmilyen drift korrekció nem volt aktív**

## 3. Alkalmazott fix

### 3.1. `components/lightsnapcast/player.c:69`

```c
// Régen: #define USE_SAMPLE_INSERTION CONFIG_USE_SAMPLE_INSERTION  // → 0 (undef)
#define USE_SAMPLE_INSERTION 1
```

**Miért:** ezzel aktiválódik a sample insertion-alapú drift korrekció.
- Független az APLL-től, működik ESP32-S3-on
- Kapacitása: 1 sample/chunk @ 48kHz = 20.83us korrekció / 20ms chunk
  = 0.1% korrekciós sebesség, bőven elég normál crystal drift-re
- Mellékhatás: a `player_setup_i2s` DMA paraméterei változnak
  - Régi: `dma_buf_len=480, dma_buf_count=4` (40ms total)
  - Új: `dma_buf_len=1023, dma_buf_count=2` (42ms total)

### 3.2. `components/lightsnapcast/player.c:1821`

```c
// Régen: const int64_t hardResyncThreshold = 2000;
const int64_t hardResyncThreshold = 50000;  // µs
```

**Miért:** csak nagy, valódi driftekre triggerel. A mérési zaj 20-100ms-os
spike-jai már nem verik ki.

### 3.3. `components/snap_app/snap_app.c` — debug log

A `handle_chunk_message()` OPUS ágához hozzáadtam egy debug logot ami
chunkonként loggolja: `spf_hdr`, `decoded`, `alloc_bytes`, `ts_delta_us`,
`enc_bytes`. A diagnózishoz kellett, **érdemes ezt később levenni** a
final commit előtt. (Az első 10 chunk + minden 50. loggol.)

## 4. Eredmények

| | Régi firmware | Új firmware |
|---|---|---|
| Hard resync | ~2 sec-onként | csak 1× (TTS indulásakor) |
| Audio körülbelül 33 sec alatt | 16+ akadás | 1 akadás |
| `fi2s_clk` ERROR | jelen | eltűnt |
| Audio minőség (user) | rossz | "jobb, de még valamennyi van" |

A megmaradó **egyetlen** glitch a TTS / csengetés legelején van
(első ~800ms), amikor a snapserver pipe-source idle→playing átmenetet csinál.

## 5. Még megoldatlan: snapserver idle→playing glitch

### Ok

A snapserver logban látszik:
```
[Info] (AsioStream) No data since 120 ms, switching to idle
...később...
[Info] (PcmStream) State changed: idle => playing
[Info] (Server) onResync (SL-1802): 604.6 ms
```

Tehát a snapserver azt mondja **120 ms-nyi adatra várt hiába**, ezért
idle-be ment. Amikor új adat jött (TTS audio), egy 604ms-os resync-et
csinált. A kliens ezt ~750ms-os age ugrásként látja → 1 hard resync.

### Hol keletkezik

A backend `audio-mixer.ts` egy `setInterval(20ms)` timer-rel ír csendet a
FIFO-ba (`tickSilence()` → `fifoStream.write(SILENCE_TICK_BUF)`).

**De Node.js setInterval NEM garantáltan pontos.** GC szünet, sync IO
(pl. `execSync` PM2 hívásokra), vagy más blocking műveletek miatt
120+ ms-ot is csúszhat. Akkor a snapserver idle-be megy.

### Megoldási irányok (még nem implementáltak)

**A. Snapserver `idle_threshold` paraméter** (preferált, ha létezik):
A snapcast `pipe://` source-nak van egy paramétere, ami szabályozza
mennyi idő után megy idle-be. Növelni kell 5-10 sec-re:
```typescript
// snapcast.service.ts:124
source = pipe://${this.fifoPath}?name=...&codec=opus&bitrate=192&chunk_ms=20&idle_threshold=5000
```
**Ellenőrizni kell:** a futó snapserver verziójában támogatott-e
ez a paraméter. `man snapserver` vagy a Snapcast verzió dokumentációja.

**B. Dedikált csend-folyamat** (komplikáltabb):
Külön ffmpeg `-f anullsrc` subprocess folyamatosan ír csendet a FIFO-ba,
a TenantAudioMixer pedig erre keveri / felülírja a job audio-t. Több
refaktor, de nem függ a Node.js event loop ütemezésétől.

**C. Vastagabb tick** (gyors hack):
`SILENCE_TICK_MS = 20` → `100`, `SILENCE_TICK_BUF` mérete is ennek
megfelelően nő. Ez csak elnyomja a problémát, nem oldja meg
(blocking > 200ms esetén ugyanúgy szakad).

## 6. Detektált, de NEM kijavított kliens-oldali bug

A `player.c` `USE_SAMPLE_INSERTION` ágában van egy konzisztencia-hiba:

- `chunkStart += alreadyWrittenTime_us` ahol `alreadyWritten = written + insertedSamplesWritten`
  → a `chunkStart` (server timestamp tracker) **inserts-szel együtt** halad előre
- `samples_written += (written / framesToBytes)` (csak written, insert nélkül)
  → a `samples_written` counter **inserts nélkül** halad

Ez egy SZIMMETRIKUS hiba: a `chunkStart` over-advance-elődik, a
`samples_written` under-advance-elődik. Ha sok insertion történik,
ez a kettő szétcsúszik, és az `age` számítás torzul (artificially negative).

A bug helye: `player.c` ~1937-1952 sor körül (chunk-vég ág):
```c
#if USE_SAMPLE_INSERTION
  alreadyWritten = written + insertedSamplesWritten;  // ← inserts hozzáadva
#else
  alreadyWritten = written;
#endif
  alreadyWrittenTime_us = ...;
  chunkStart += alreadyWrittenTime_us;  // ← inserts hozzáadva (rossz!)
...
  samples_written += (written / (scSet.ch * (scSet.bits / 8)));  // ← inserts NEM hozzáadva (rossz!)
```

**Helyes:** vagy mindkettő include-olja az inserts-et (akkor consistent),
vagy egyik sem.
- `chunkStart` szempontból: csak a "real chunk samples" számítanak (inserts nem)
  → `alreadyWritten = written` (insert nélkül)
- `samples_written` szempontból: a DAC mindent kijátszik (inserts is)
  → `samples_written += alreadyWritten` (insert-tel együtt)

Most ez a bug látens (a single resync TTS indulásnál valószínűleg ettől
nagyobb mint kellene). Akkor érdemes kijavítani amikor visszatérünk
finomhangolásra.

## 7. Érintett fájlok

| Fájl | Mit változott |
|---|---|
| `components/lightsnapcast/player.c` | Sor 69: `USE_SAMPLE_INSERTION 1`; sor 1821: threshold 2000→50000 |
| `components/snap_app/snap_app.c` | OPUS chunk debug log a `handle_chunk_message()`-ben (le kell venni a final commit előtt) |

## 8. Hardware / hardware környezet

- Board: ESP32-S3-DevKitC-1-N8 (8 MB QD, No PSRAM — bár 8MB PSRAM detektálódik)
- Chip: ESP32-S3 rev v0.2
- Crystal: 40 MHz
- ESP-IDF: 5.5.4
- PlatformIO: `framework-espidf @ 3.50504.0`
- Audio output: I2S BCLK=14, LRC=15, DIN=13, MCLK=-1

## 9. Reset / flash tip

ESP32-S3-DevKitC-1 USB-Serial JTAG-en keresztül **az RTS reset gyakran
nem megbízható**. Ha új firmware nem indul a flash után, a board RST
gombját **manuálisan** meg kell nyomni.

## 10. Következő lépések sorrendben

1. **Snapserver `idle_threshold` paraméter teszt** (`snapcast.service.ts:124`)
   - Ellenőrizni hogy a futó snapserver verzió támogatja-e
   - 5000ms értékkel kipróbálni
   - Ha működik, deploy + ESP32 monitor figyelése
2. **Debug log levétele** `snap_app.c`-ből
3. **Sample insertion bug (6. szakasz) kijavítása** ha még marad audible glitch
4. **Commit** a kliens-oldali fix-eket a SchoolLive-devices repóba
   (jelenleg uncommitted állapot)
5. **Android + Python kliensek Opus átállítása** (külön task, későbbre)

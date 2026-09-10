#ifndef __PLAYER_H__
#define __PLAYER_H__

#include "driver/i2s_std.h"
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "snapcast.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USE_TIMEFILTER  CONFIG_SNAPCLIENT_USE_TIMEFILTER

#define I2S_PORT I2S_NUM_0

// TODO: maybe calculate this dynamically based on chunk duration and buffer
// size?!
#define CHNK_CTRL_CNT 2

/*
 * ── Összeomlás-morzsa (crash breadcrumb) ───────────────────────────────────
 *
 * A pánik pillanatában a soros port csak akkor segít, ha valaki épp nézi –
 * és a csengetéshez kötött hiba óránként egyszer jön elő, távoli iskolában
 * pedig egyáltalán nincs, aki nézze.
 *
 * Ezért a lejátszó-út fontos pontjain beállítunk egy sorszámot az RTC
 * memóriában. Az RTC RAM-ot a pánik utáni újraindulás NEM törli (csak a
 * tápelvétel), így a KÖVETKEZŐ induláskor kiolvasható, hol járt a kód, amikor
 * összeomlott. Az érték a beaconnel a backendre is felmegy, tehát a
 * Hibajelzések ablakból is látszik – kiszállás nélkül.
 *
 * A jelölés egyetlen írás egy RTC-változóba: ISR-ből is biztonságos, és a
 * futásidőre mérhetetlen a hatása.
 */
typedef enum {
  PLAYER_MARK_NONE            = 0,
  PLAYER_MARK_TASK_ENTRY      = 1,   // player_task elindult
  PLAYER_MARK_SETUP_I2S       = 2,   // player_setup_i2s fut
  PLAYER_MARK_TIMER_INIT      = 3,   // tg0_timer_init fut
  PLAYER_MARK_TIMER_START     = 4,   // tg0_timer1_start fut
  PLAYER_MARK_TIMER_ISR       = 5,   // riasztás ISR fut
  PLAYER_MARK_SETTINGS_CHANGE = 6,   // beállítás-változás ága
  PLAYER_MARK_QUEUE_RECV      = 7,   // chunkra vár a sorban
  PLAYER_MARK_CHUNK_PROCESS   = 8,   // chunk feldolgozás / osztások
  PLAYER_MARK_I2S_WRITE       = 9,   // i2s_channel_write / preload
  PLAYER_MARK_TASK_EXIT       = 10,  // takarító ág, a task leáll
  PLAYER_MARK_INSERT_CHUNK    = 11,  // insert_pcm_chunk (http_get_task)
  PLAYER_MARK_START_PLAYER    = 12,  // start_player fut

  // ── Alkalmazásréteg ──────────────────────────────────────────────────────
  // A morzsa NEM csak a snap lejátszóé: ha a pánik az app-kódban van, a
  // lejátszó utolsó állapota félrevezetne. Ezért a periodikusan futó
  // alkalmazás-utak is jelölnek.
  APP_MARK_LOOP_AUDIO         = 13,  // loop(): audioManager.loop()
  APP_MARK_LOOP_UI            = 14,  // loop(): uiManager.loop()
  APP_MARK_NET_WS             = 15,  // TaskNetwork: wsClient.loop()
  APP_MARK_NET_SNAPSTART      = 16,  // TaskNetwork: tryStartSnapcastClient()
  APP_MARK_NET_BELLS          = 17,  // TaskNetwork: bellManager
  APP_MARK_NET_OTA            = 18,  // TaskNetwork: otaManager.loop()
  APP_MARK_BEACON             = 19,  // DeviceAgent: beacon osszeallitas/kuldes
  APP_MARK_WS_MESSAGE         = 20,  // WS uzenet feldolgozas
} player_mark_t;

/** Morzsa beállítása. Egyetlen RTC-írás.
 *  A paraméter SZÁNDÉKOSAN `int` és nem `player_mark_t`: az app-réteg (C++)
 *  a fejléc behúzása nélkül, kézi `extern "C"` deklarációval hívja, és így
 *  a két oldal szignatúrája biztosan egyezik. */
void player_mark(int mark);

/** Az ELŐZŐ futásban utoljára beállított morzsa (0, ha nincs érvényes adat,
 *  pl. tápelvétel után). Csak induláskor van értelme kiolvasni. */
uint32_t player_last_crash_mark(void);

/** Hányadik másodpercben járt az előző futás, amikor a morzsa készült. */
uint32_t player_last_crash_uptime(void);

//#define LATENCY_MEDIAN_FILTER_LEN 199
#define LATENCY_TIME_FILTER_FULL 29

// set to 0 if you do not wish to be the median an average around actual
// median average will be (LATENCY_MEDIAN_FILTER_LEN /
// LATENCY_MEDIAN_AVG_DIVISOR) + 1 samples around median. e.g. if n=4 then
// 2 samples above and below will be added plus the actual median. So in
// reality n+1 samples will be averaged
#define LATENCY_MEDIAN_AVG_DIVISOR 0

#define LATENCY_MEDIAN_FILTER_LEN 199
#define LATENCY_MEDIAN_FILTER_FULL 19

#define SHORT_BUFFER_LEN 99
#define MINI_BUFFER_LEN 19

typedef struct pcm_chunk_fragment pcm_chunk_fragment_t;
struct pcm_chunk_fragment {
  size_t size;
  char *payload;
  pcm_chunk_fragment_t *nextFragment;
};

typedef struct pcmData {
  tv_t timestamp;
  size_t totalSize;
  pcm_chunk_fragment_t *fragment;
  uint32_t caps;
} pcm_chunk_message_t;

typedef enum codec_type_e { NONE = 0, PCM, FLAC, OGG, OPUS } codec_type_t;

typedef struct snapcastSetting_s {
  uint32_t buf_ms;
  uint32_t chkInFrames;
  int32_t cDacLat_ms;

  codec_type_t codec;
  int32_t sr;
  uint8_t ch;
  i2s_data_bit_width_t bits;

  bool muted;
  uint32_t volume;

  char *pcmBuf;
  uint32_t pcmBufSize;
} snapcastSetting_t;

int init_player(i2s_std_gpio_config_t pin_config0_, i2s_port_t i2sNum_);
int deinit_player(void);
int start_player(snapcastSetting_t *setting);

int32_t allocate_pcm_chunk_memory(pcm_chunk_message_t **pcmChunk, size_t bytes);
int32_t insert_pcm_chunk(pcm_chunk_message_t *pcmChunk);

// int8_t insert_pcm_chunk (wire_chunk_message_t *decodedWireChunk);
int8_t free_pcm_chunk(pcm_chunk_message_t *pcmChunk);

#if USE_TIMEFILTER
int32_t player_latency_insert(int64_t newValue, int64_t max_error, int64_t time_added);
#else
int32_t player_latency_insert(int64_t newValue);
#endif

int32_t get_diff_to_server(int64_t *tDiff, int64_t now);
int32_t latency_buffer_full(bool *is_full);

int32_t player_send_snapcast_setting(snapcastSetting_t *setting);
int8_t player_get_snapcast_settings(snapcastSetting_t *setting);

int32_t reset_latency_buffer(void);

int32_t server_now(int64_t *sNow, int64_t *diff2Server);

int32_t pcm_chunk_queue_msg_waiting(void);

/**
 * SchoolLive integráció:
 *
 * Lokális (offline) audio lejátszás idejére fel kell függesztenünk a
 * player_task I2S írását, hogy az AudioManager szabadon vegye át az I2S
 * vezérlést és ne keletkezzen összeakadás (DMA kettős író, glitch).
 *
 * - player_pause(): vTaskSuspend a player_task-on. A http_get_task és a
 *   chunk feldolgozás tovább fut, a jitter buffer halmozza a chunkokat.
 *   I2S kimenet kikapcsol, mert a player_task nem ír többet.
 * - player_resume(): vTaskResume + reset_latency_buffer, hogy a time-sync
 *   tisztán induljon újra (egy hard resync várható, az ESP-n eddig is így volt).
 *
 * Mindkét hívás idempotens. Ha a player_task még nem indult el (még nem
 * érkezett első settings + codec header), no-op.
 */
void player_pause(void);
void player_resume(void);
bool player_is_paused(void);

#ifdef __cplusplus
}
#endif
#endif  // __PLAYER_H__

#include "emulator.h"
#include "link.h"
#include <Core/memory.h>
#include <Core/gb.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#define SB_BOOT_ROM_COUNT 10   /* GB_BOOT_ROM_AGB + 1 */

struct sb_emulator {
    GB_gameboy_t gb;
    uint32_t buffers[2][SB_MAX_W * SB_MAX_H];
    int back;                       /* index of the buffer Core renders into */
    unsigned front_w, front_h;
    pthread_mutex_t fb_mtx;
    pthread_cond_t  frame_cv;       /* signalled when a new frame is produced (present-on-produce) */
    uint64_t        frame_seq;      /* bumps once per completed frame; render waits on changes */
    sb_ring *audio;
    struct { uint8_t *data; size_t len; } boot[SB_BOOT_ROM_COUNT];  /* owned copies */
    const atomic_bool *audio_drop;
    const atomic_int *volume;
    double applied_rewind_seconds;  /* cache: GB_set_rewind_length wipes history, so skip if unchanged */
    GB_palette_t custom_palette;    /* GB_set_palette stores a pointer; keep custom alive here */
    atomic_int rumble_amp;          /* 0..255, latest from rumble_cb */
    pthread_mutex_t printer_mtx;
    uint32_t       *printer_feed;        /* 160 * printer_rows ARGB, malloc/realloc */
    unsigned        printer_rows;
    atomic_uint     printer_generation;
    pthread_mutex_t camera_mtx;
    uint8_t         camera_staging[SB_CAM_W * SB_CAM_H];
    uint8_t         camera_sensor[SB_CAM_W * SB_CAM_H];
    atomic_bool     camera_wanted;   /* consumed by Java poll (exchange) to drive the device camera */
    atomic_bool     camera_pending;  /* a shoot fired; drained on the emu thread next frame to clear busy */
    struct sb_link *link;            /* M8: serial link bridge; emu-thread + parked-caller only */
};

static uint32_t rgb_encode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    (void)gb;
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static void vblank_cb(GB_gameboy_t *gb, GB_vblank_type_t type)
{
    if (type == GB_VBLANK_TYPE_REPEAT) return;   /* nothing new to show */
    sb_emulator *e = GB_get_user_data(gb);
    pthread_mutex_lock(&e->fb_mtx);
    e->front_w = GB_get_screen_width(gb);
    e->front_h = GB_get_screen_height(gb);
    e->back ^= 1;                                 /* swap */
    GB_set_pixels_output(gb, e->buffers[e->back]);
    e->frame_seq++;                               /* new frame ready */
    pthread_cond_signal(&e->frame_cv);            /* wake the render thread (present-on-produce) */
    pthread_mutex_unlock(&e->fb_mtx);
}

static void audio_cb(GB_gameboy_t *gb, GB_sample_t *sample)
{
    sb_emulator *e = GB_get_user_data(gb);
    int16_t l = sample->left, r = sample->right;
    if (e->volume) {
        int v = atomic_load_explicit(e->volume, memory_order_relaxed);
        if (v != 256) { l = (int16_t)(l * v / 256); r = (int16_t)(r * v / 256); }
    }
    if (e->audio_drop && atomic_load_explicit(e->audio_drop, memory_order_relaxed)) {
        sb_ring_try_push(e->audio, l, r);
    }
    else {
        sb_ring_push(e->audio, l, r);
    }
}

static void boot_rom_cb(GB_gameboy_t *gb, GB_boot_rom_t type)
{
    sb_emulator *e = GB_get_user_data(gb);
    if (type < SB_BOOT_ROM_COUNT && e->boot[type].data) {
        GB_load_boot_rom_from_buffer(gb, e->boot[type].data, e->boot[type].len);
        return;
    }
    /* fallbacks mirroring SDL: CGB_E->CGB, AGB_0->AGB */
    if (type == GB_BOOT_ROM_CGB_E && e->boot[GB_BOOT_ROM_CGB].data) {
        GB_load_boot_rom_from_buffer(gb, e->boot[GB_BOOT_ROM_CGB].data, e->boot[GB_BOOT_ROM_CGB].len);
    }
    else if (type == GB_BOOT_ROM_AGB_0 && e->boot[GB_BOOT_ROM_AGB].data) {
        GB_load_boot_rom_from_buffer(gb, e->boot[GB_BOOT_ROM_AGB].data, e->boot[GB_BOOT_ROM_AGB].len);
    }
    /* else: run boot-ROM-less */
}

static void rumble_cb(GB_gameboy_t *gb, double amplitude)
{
    sb_emulator *e = GB_get_user_data(gb);
    if (amplitude < 0) amplitude = 0;
    if (amplitude > 1) amplitude = 1;
    atomic_store(&e->rumble_amp, (int)(amplitude * 255 + 0.5));
}

#define SB_PRINTER_W 160

void sb_printer_append(uint32_t **buf, unsigned *rows, const uint32_t *image,
                       unsigned height, unsigned top, unsigned bottom)
{
    unsigned add = top + height + bottom;
    if (add == 0) return;
    unsigned new_rows = *rows + add;
    uint32_t *grown = realloc(*buf, (size_t)new_rows * SB_PRINTER_W * sizeof(uint32_t));
    if (!grown) return;                 /* keep the old buffer on OOM */
    *buf = grown;
    uint32_t *p = grown + (size_t)*rows * SB_PRINTER_W;
    memset(p, 0xFF, (size_t)top * SB_PRINTER_W * sizeof(uint32_t));   /* white top margin */
    p += (size_t)top * SB_PRINTER_W;
    if (height && image) memcpy(p, image, (size_t)height * SB_PRINTER_W * sizeof(uint32_t));
    p += (size_t)height * SB_PRINTER_W;
    memset(p, 0xFF, (size_t)bottom * SB_PRINTER_W * sizeof(uint32_t)); /* white bottom margin */
    *rows = new_rows;
}

static void print_image_cb(GB_gameboy_t *gb, uint32_t *image, uint8_t height,
                           uint8_t top_margin, uint8_t bottom_margin, uint8_t exposure)
{
    (void)exposure;
    sb_emulator *e = GB_get_user_data(gb);
    pthread_mutex_lock(&e->printer_mtx);
    sb_printer_append(&e->printer_feed, &e->printer_rows, image, height, top_margin, bottom_margin);
    pthread_mutex_unlock(&e->printer_mtx);
    atomic_fetch_add(&e->printer_generation, 1);
}

static void printer_done_cb(GB_gameboy_t *gb)
{
    sb_emulator *e = GB_get_user_data(gb);
    atomic_fetch_add(&e->printer_generation, 1);
}

void sb_emu_connect_printer(sb_emulator *e)
{
    if (e) GB_connect_printer(&e->gb, print_image_cb, printer_done_cb);
}

void sb_emu_disconnect_printer(sb_emulator *e)
{
    if (e) GB_disconnect_serial(&e->gb);
}

unsigned sb_emu_printer_generation(sb_emulator *e)
{
    return e ? atomic_load(&e->printer_generation) : 0;
}

unsigned sb_emu_printer_feed(sb_emulator *e, uint32_t *dst, unsigned max_rows)
{
    if (!e) return 0;
    pthread_mutex_lock(&e->printer_mtx);
    unsigned rows = e->printer_rows;
    if (dst && max_rows) {
        unsigned n = rows < max_rows ? rows : max_rows;
        memcpy(dst, e->printer_feed, (size_t)n * SB_PRINTER_W * sizeof(uint32_t));
    }
    pthread_mutex_unlock(&e->printer_mtx);
    return rows;
}

void sb_emu_printer_clear(sb_emulator *e)
{
    if (!e) return;
    pthread_mutex_lock(&e->printer_mtx);
    free(e->printer_feed);
    e->printer_feed = NULL;
    e->printer_rows = 0;
    pthread_mutex_unlock(&e->printer_mtx);
}

uint8_t sb_camera_read(const uint8_t *buf, int x, int y)
{
    if (x < 0) x = 0; else if (x >= SB_CAM_W) x = SB_CAM_W - 1;
    if (y < 0) y = 0; else if (y >= SB_CAM_H) y = SB_CAM_H - 1;
    return buf[y * SB_CAM_W + x];
}

void sb_camera_promote(sb_emulator *e)
{
    pthread_mutex_lock(&e->camera_mtx);
    memcpy(e->camera_sensor, e->camera_staging, sizeof(e->camera_sensor));
    pthread_mutex_unlock(&e->camera_mtx);
}

static uint8_t cam_get_pixel_cb(GB_gameboy_t *gb, uint8_t x, uint8_t y)
{
    sb_emulator *e = GB_get_user_data(gb);
    return sb_camera_read(e->camera_sensor, x, y);   /* emu thread; sensor only written here-thread */
}

static void cam_update_request_cb(GB_gameboy_t *gb)
{
    /* Fires on the emu thread from GB_camera_write_register, which clears the busy bit
       (SHOOT &= ~1) only AFTER this returns and then stores the shoot value with bit0=1.
       Calling GB_camera_updated() here would clear the OLD (pre-store) value = a no-op,
       latching busy forever and soft-locking the ROM. Instead pull the latest frame now
       and flag a pending clear, drained at the next sb_emu_run_frame (after the store). */
    sb_emulator *e = GB_get_user_data(gb);
    atomic_store(&e->camera_wanted, true);
    sb_camera_promote(e);            /* pull latest delivered frame into the sensor */
    atomic_store(&e->camera_pending, true);
}

bool sb_emu_camera_wanted(sb_emulator *e)
{
    /* Consuming read: true only if a shoot arrived since the last poll, so when the ROM
       stops shooting the Java poller sees false and can idle-stop the device camera. */
    return e ? atomic_exchange(&e->camera_wanted, false) : false;
}

void sb_emu_camera_deliver(sb_emulator *e, const uint8_t *gray)
{
    if (!e || !gray) return;
    pthread_mutex_lock(&e->camera_mtx);
    memcpy(e->camera_staging, gray, sizeof(e->camera_staging));
    pthread_mutex_unlock(&e->camera_mtx);
}

/* --- Link cable (M8) --- */
static void link_bit_start_cb(GB_gameboy_t *gb, bool bit) {
    sb_emulator *e = GB_get_user_data(gb);
    if (e->link) sb_link_bit_start(e->link, gb, bit);
}
static bool link_bit_end_cb(GB_gameboy_t *gb) {
    sb_emulator *e = GB_get_user_data(gb);
    return e->link ? sb_link_bit_end(e->link, gb) : true;
}
void sb_emu_link_set(sb_emulator *e, struct sb_link *link) {
    if (!e) { if (link) sb_link_destroy(link); return; }
    if (e->link) { GB_disconnect_serial(&e->gb); sb_link_destroy(e->link); }
    e->link = link;
    if (link) {
        GB_set_serial_transfer_bit_start_callback(&e->gb, link_bit_start_cb);
        GB_set_serial_transfer_bit_end_callback(&e->gb, link_bit_end_cb);
    }
}
void sb_emu_link_clear(sb_emulator *e) {
    if (!e || !e->link) return;
    GB_disconnect_serial(&e->gb);
    sb_link_destroy(e->link);
    e->link = NULL;
}
bool sb_emu_link_dead(sb_emulator *e) { return e && e->link && sb_link_is_dead(e->link); }
uint8_t sb_emu_peek_sb(sb_emulator *e) { return e ? GB_safe_read_memory(&e->gb, 0xFF01) : 0; }

sb_emulator *sb_emu_create(int model, const uint8_t *rom, size_t rom_len,
                           const uint8_t *sav, size_t sav_len)
{
    sb_emulator *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    pthread_mutex_init(&e->fb_mtx, NULL);
    pthread_cond_init(&e->frame_cv, NULL);
    pthread_mutex_init(&e->printer_mtx, NULL);
    /* ~100 ms ring. sb_ring_push blocks when full, so at runtime the AAudio
       callback's pop rate paces the emulation thread (audio is the master
       clock). Kept small so the emu thread never runs far ahead of playback.
       The host test must therefore drain the ring as it steps frames. */
    e->audio = sb_ring_create(SB_AUDIO_SAMPLE_RATE / 10);
    if (!e->audio) {   /* audio_cb would deref NULL on the first sample */
        pthread_mutex_destroy(&e->printer_mtx);
        pthread_cond_destroy(&e->frame_cv);
        pthread_mutex_destroy(&e->fb_mtx);
        free(e);
        return NULL;
    }
    e->back = 0;
    e->front_w = 160; e->front_h = 144;

    GB_init(&e->gb, (GB_model_t)model);
    GB_set_user_data(&e->gb, e);
    GB_set_boot_rom_load_callback(&e->gb, boot_rom_cb);
    GB_set_vblank_callback(&e->gb, vblank_cb);
    GB_set_rgb_encode_callback(&e->gb, rgb_encode);
    GB_set_pixels_output(&e->gb, e->buffers[e->back]);
    GB_set_sample_rate(&e->gb, SB_AUDIO_SAMPLE_RATE);
    GB_apu_set_sample_callback(&e->gb, audio_cb);
    GB_set_rumble_callback(&e->gb, rumble_cb);
    pthread_mutex_init(&e->camera_mtx, NULL);
    GB_set_camera_get_pixel_callback(&e->gb, cam_get_pixel_cb);
    GB_set_camera_update_request_callback(&e->gb, cam_update_request_cb);

    GB_load_rom_from_buffer(&e->gb, rom, rom_len);
    if (sav && sav_len) GB_load_battery_from_buffer(&e->gb, sav, sav_len);
    GB_set_rewind_length(&e->gb, 120);
    e->applied_rewind_seconds = 120;
    return e;
}

void sb_emu_set_boot_rom(sb_emulator *e, int type, const uint8_t *data, size_t len)
{
    if (type < 0 || type >= SB_BOOT_ROM_COUNT) return;
    free(e->boot[type].data);              /* replace any prior copy */
    e->boot[type].data = NULL;
    e->boot[type].len = 0;
    if (data && len) {
        e->boot[type].data = malloc(len);
        if (!e->boot[type].data) return;   /* keep "no boot ROM" state on OOM */
        memcpy(e->boot[type].data, data, len);
        e->boot[type].len = len;
    }
}

void sb_emu_reset(sb_emulator *e) { GB_reset(&e->gb); }

void sb_emu_run_frame(sb_emulator *e)
{
    GB_run_frame(&e->gb);
    /* A shoot during this frame stored busy=1 (see cam_update_request_cb); clear it now,
       after the store, so the ROM sees the shoot complete on the next frame (~1 frame of
       "processing", like the real cart). Emu-thread only, so no lock needed on the GB. */
    if (atomic_exchange(&e->camera_pending, false)) {
        GB_camera_updated(&e->gb);
    }
    if (e->link) sb_link_slave_poll(e->link, &e->gb);
}

const uint32_t *sb_emu_front_buffer(sb_emulator *e, unsigned *w, unsigned *h)
{
    pthread_mutex_lock(&e->fb_mtx);
    /* front = the buffer NOT currently being rendered into */
    const uint32_t *fb = e->buffers[e->back ^ 1];
    if (w) *w = e->front_w;
    if (h) *h = e->front_h;
    pthread_mutex_unlock(&e->fb_mtx);
    return fb;
}

void sb_emu_copy_front(sb_emulator *e, uint32_t *dst, unsigned *w, unsigned *h)
{
    pthread_mutex_lock(&e->fb_mtx);
    unsigned fw = e->front_w, fh = e->front_h;
    memcpy(dst, e->buffers[e->back ^ 1], (size_t)fw * fh * sizeof(uint32_t));
    pthread_mutex_unlock(&e->fb_mtx);
    if (w) *w = fw;
    if (h) *h = fh;
}

uint32_t sb_emu_screen_size(sb_emulator *e)
{
    if (!e) return 0;
    pthread_mutex_lock(&e->fb_mtx);
    uint32_t packed = (e->front_w << 16) | e->front_h;
    pthread_mutex_unlock(&e->fb_mtx);
    return packed;
}

/* Present-on-produce: block until frame_seq advances past *last_seen (a new frame was
   produced), or timeout_ms elapses (bounds shutdown latency / avoids a permanent stall if
   the emu is paused). Updates *last_seen. Returns 1 if a new frame arrived, 0 on timeout. */
int sb_emu_wait_frame(sb_emulator *e, uint64_t *last_seen, int timeout_ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    ts.tv_sec  += timeout_ms / 1000 + ts.tv_nsec / 1000000000L;
    ts.tv_nsec %= 1000000000L;
    int got = 0;
    pthread_mutex_lock(&e->fb_mtx);
    while (e->frame_seq == *last_seen) {
        if (pthread_cond_timedwait(&e->frame_cv, &e->fb_mtx, &ts) != 0) break;  /* timeout */
    }
    if (e->frame_seq != *last_seen) { *last_seen = e->frame_seq; got = 1; }
    pthread_mutex_unlock(&e->fb_mtx);
    return got;
}

/* Wake any thread blocked in sb_emu_wait_frame (e.g. to observe a stop flag). */
void sb_emu_wake(sb_emulator *e)
{
    pthread_mutex_lock(&e->fb_mtx);
    pthread_cond_broadcast(&e->frame_cv);
    pthread_mutex_unlock(&e->fb_mtx);
}

sb_ring *sb_emu_audio_ring(sb_emulator *e) { return e->audio; }

void sb_emu_set_key(sb_emulator *e, int idx, int pressed)
{
    GB_set_key_state(&e->gb, (GB_key_t)idx, pressed != 0);
}

size_t sb_emu_save_battery(sb_emulator *e, uint8_t **out)
{
    int size = GB_save_battery_size(&e->gb);
    if (size <= 0) { *out = NULL; return 0; }
    uint8_t *buf = malloc(size);
    if (!buf) { *out = NULL; return 0; }
    GB_save_battery_to_buffer(&e->gb, buf, size);
    *out = buf;
    return (size_t)size;
}

void sb_emu_set_audio_drop(sb_emulator *e, const atomic_bool *drop_on_full)
{
    e->audio_drop = drop_on_full;
}

size_t sb_emu_save_state(sb_emulator *e, uint8_t **out)
{
    size_t n = GB_get_save_state_size(&e->gb);
    uint8_t *buf = malloc(n);
    if (!buf) return 0;
    GB_save_state_to_buffer(&e->gb, buf);
    *out = buf;
    return n;
}

int sb_emu_load_state(sb_emulator *e, const uint8_t *buf, size_t n)
{
    GB_model_t model = 0;
    if (GB_get_state_model_from_buffer(buf, n, &model) == 0 &&
        GB_get_model(&e->gb) != model) {
        GB_switch_model_and_reset(&e->gb, model);   /* mirrors iOS loadStateFromFile: */
    }
    return GB_load_state_from_buffer(&e->gb, buf, n);
}

void sb_emu_switch_model(sb_emulator *e, int model)
{
    GB_switch_model_and_reset(&e->gb, (GB_model_t)model);
}

/* --- Cheats --- Core keeps cheats in gb->cheats across GB_reset / GB_switch_model_and_reset /
   GB_load_state_from_buffer; only GB_free drops them. All GB_*cheat* calls assert
   not-running-other-thread, so callers park first. */
int sb_emu_add_cheat(sb_emulator *e, const char *code, const char *desc, int enabled)
{
    return GB_import_cheat(&e->gb, code, desc ? desc : "", enabled != 0) != NULL;
}

void sb_emu_remove_all_cheats(sb_emulator *e) { GB_remove_all_cheats(&e->gb); }
void sb_emu_set_cheats_enabled(sb_emulator *e, int on) { GB_set_cheats_enabled(&e->gb, on != 0); }
size_t sb_emu_cheat_count(sb_emulator *e) { size_t n = 0; GB_get_cheats(&e->gb, &n); return n; }

/* --- MBC7 accelerometer --- plain double stores, no thread assert in Core; any thread. */
bool sb_emu_has_accelerometer(sb_emulator *e) { return GB_has_accelerometer(&e->gb); }
void sb_emu_set_accelerometer(sb_emulator *e, double x, double y) { GB_set_accelerometer_values(&e->gb, x, y); }

void sb_emu_set_rewind_length(sb_emulator *e, double seconds)
{
    GB_set_rewind_length(&e->gb, seconds);
}

void sb_emu_set_turbo(sb_emulator *e, int on)
{
    GB_set_turbo_mode(&e->gb, on != 0, false);   /* frame skip ON in turbo */
}

int sb_emu_rewind_pop(sb_emulator *e)
{
    return GB_rewind_pop(&e->gb) ? 1 : 0;
}

int sb_emu_battery_dirty(sb_emulator *e)
{
    return GB_get_battery_dirty(&e->gb) ? 1 : 0;
}

void sb_emu_clear_battery_dirty(sb_emulator *e)
{
    GB_clear_battery_dirty(&e->gb);
}

int sb_rom_info(const uint8_t *rom, size_t len, char *title, uint32_t *crc32)
{
    /* NOTE: GB_init/GB_reset touch Core's file-static GB_random seed (Core/random.c),
       which is not atomic. Callers run this on a background scan thread; if a game is
       launched mid-scan, a live emulator thread using GB_random (camera/SGB) technically
       races the seed. Effect is cosmetic (RAM/noise randomization), never a crash, and
       Core is unmodifiable here — documented, out of scope (see M3 review). */
    if (len < 0x150) return -1;
    /* GB_gameboy_t is large and embedded by value elsewhere; heap-allocate it
       rather than risk a worker-thread stack. */
    GB_gameboy_t *gb = malloc(sizeof(GB_gameboy_t));
    if (!gb) return -1;
    GB_init(gb, GB_MODEL_CGB_E);
    GB_load_rom_from_buffer(gb, rom, len);
    GB_get_rom_title(gb, title);
    *crc32 = GB_get_rom_crc32(gb);
    GB_free(gb);
    free(gb);
    return 0;
}

void sb_emu_set_volume_ptr(sb_emulator *e, const atomic_int *volume)
{
    e->volume = volume;
}

void sb_emu_apply_settings(sb_emulator *e, const sb_settings *s)
{
    GB_set_color_correction_mode(&e->gb, (GB_color_correction_mode_t)s->color_correction);
    GB_set_light_temperature(&e->gb, s->light_temperature);
    GB_set_border_mode(&e->gb, (GB_border_mode_t)s->border_mode);
    GB_set_highpass_filter_mode(&e->gb, (GB_highpass_mode_t)s->highpass);
    GB_set_rtc_mode(&e->gb, (GB_rtc_mode_t)s->rtc_mode);
    /* GB_set_rewind_length calls GB_rewind_reset (wipes history) — only when it changes */
    if (s->rewind_seconds != e->applied_rewind_seconds) {
        GB_set_rewind_length(&e->gb, s->rewind_seconds);
        e->applied_rewind_seconds = s->rewind_seconds;
    }
    GB_set_turbo_cap(&e->gb, s->turbo_cap);
    GB_set_interference_volume(&e->gb, s->interference);
    GB_set_rumble_mode(&e->gb, (GB_rumble_mode_t)s->rumble_mode);
}

void sb_emu_set_palette(sb_emulator *e, int builtin_index, const uint32_t rgb[4])
{
    static const GB_palette_t *const builtins[] = {
        &GB_PALETTE_GREY, &GB_PALETTE_DMG, &GB_PALETTE_MGB, &GB_PALETTE_GBL,
    };
    if (builtin_index >= 0 && builtin_index < 4) {
        GB_set_palette(&e->gb, builtins[builtin_index]);
        return;
    }
    if (!rgb) return;
    for (int i = 0; i < 4; i++) {
        e->custom_palette.colors[i].r = (rgb[i] >> 16) & 0xFF;
        e->custom_palette.colors[i].g = (rgb[i] >> 8) & 0xFF;
        e->custom_palette.colors[i].b = rgb[i] & 0xFF;
    }
    e->custom_palette.colors[4] = e->custom_palette.colors[3];  /* border = lightest */
    GB_set_palette(&e->gb, &e->custom_palette);
}

int sb_emu_rumble_amplitude(sb_emulator *e)
{
    return atomic_load(&e->rumble_amp);
}

void sb_emu_destroy(sb_emulator *e)
{
    if (!e) return;
    sb_emu_link_clear(e);
    sb_ring_shutdown(e->audio);
    GB_free(&e->gb);
    sb_ring_destroy(e->audio);
    for (int i = 0; i < SB_BOOT_ROM_COUNT; i++) free(e->boot[i].data);
    free(e->printer_feed);
    pthread_mutex_destroy(&e->printer_mtx);
    pthread_mutex_destroy(&e->camera_mtx);
    pthread_cond_destroy(&e->frame_cv);
    pthread_mutex_destroy(&e->fb_mtx);
    free(e);
}

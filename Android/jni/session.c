#include "session.h"
#include "render_gles.h"
#include "audio_aaudio.h"
#include "link.h"
#include "sched_hint.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <android/log.h>

struct sb_session {
    sb_emulator *emu;
    sb_renderer *render;
    sb_audio *audio;
    ANativeWindow *win;
    pthread_t emu_thread;
    atomic_bool running;
    int paused;                 /* protected by pause_mtx */
    atomic_bool turbo;          /* consumed by emu loop */
    atomic_bool rewinding;      /* consumed by emu loop */
    atomic_bool audio_drop;     /* read by audio_cb via sb_emu_set_audio_drop */
    atomic_bool audio_dead;     /* set by AAudio error_cb on disconnect; emu loop reopens */
    atomic_bool battery_dirty;  /* published by emu loop, read by JNI */
    atomic_int volume;          /* 0..256; read by emu audio path */
    atomic_int filter;          /* 0 off, 1 LCD; read by the render thread */
    atomic_int turbo_frame_ns;  /* min ns per frame while turbo (cap); 0 = uncapped */
    int parked;                 /* emu thread is waiting in pause_cv (under pause_mtx) */
    pthread_mutex_t pause_mtx;
    pthread_cond_t pause_cv;
    pthread_cond_t parked_cv;   /* signalled when the emu thread parks */
    atomic_int  link_status;      /* SB_LINK_* */
    pthread_t   link_thread;
    bool        link_thread_live;
    char        link_host[64];    /* connect target */
    int         link_port;
    int         link_is_listen;   /* 1 listen, 0 connect */
    atomic_bool link_cancel;              /* set to abort a blocking listen/connect */
    _Atomic(sb_transport *) link_pending; /* worker → emu thread: transport awaiting attach */
};

static void *emu_loop(void *arg)
{
    sb_session *s = arg;
    /* Audio-clocked producer: keep it off a cold little core so it never wakes late
       (else the ring underruns -> audio crackle AND the frame lands late -> hitch). */
    sb_sched_boost_current_thread(-19 /* THREAD_PRIORITY_URGENT_AUDIO */);
    int applied_turbo = -1; /* -1: force first-iteration apply — core turbo state persists across stop/start */
    int audio_retry_in = 0; /* frames until the next (re)open attempt while running without audio */
    int64_t turbo_next = 0; /* CLOCK_MONOTONIC deadline for the next capped turbo frame */
    while (atomic_load(&s->running)) {
        pthread_mutex_lock(&s->pause_mtx);
        while (s->paused && atomic_load(&s->running)) {
            s->parked = 1;
            pthread_cond_broadcast(&s->parked_cv);
            pthread_cond_wait(&s->pause_cv, &s->pause_mtx);
        }
        s->parked = 0;
        pthread_mutex_unlock(&s->pause_mtx);
        if (!atomic_load(&s->running)) break;

        /* Attach a link the worker built, on THIS (emu) thread at a frame boundary — never
           from the worker thread (that would race a running frame). Deferred while paused;
           harmless, since no serial runs while paused. */
        sb_transport *pt = atomic_exchange(&s->link_pending, NULL);
        if (pt) sb_emu_link_set(s->emu, sb_link_create(pt));

        /* AAudio stream disconnected (route change): its callbacks are dead, so
           nothing pops the ring anymore. Close it from this thread (AAudio forbids
           close from the error callback; error_cb flushed the ring so a blocked push
           already got unstuck) and fall into the reopen path below. */
        if (s->audio && atomic_load(&s->audio_dead)) {
            pthread_mutex_lock(&s->pause_mtx);
            sb_audio *dead = s->audio;   /* unlink under pause_mtx vs. pause/stop */
            s->audio = NULL;
            pthread_mutex_unlock(&s->pause_mtx);
            sb_audio_stop(dead);
            audio_retry_in = 0;
        }
        /* No stream (start failed, or the new route wasn't ready when the old one died —
           openStream fails transiently mid-BT-handover): keep retrying about once a second
           rather than staying silent for the rest of the session. */
        if (!s->audio && audio_retry_in-- <= 0) {
            sb_audio *fresh = sb_audio_start(sb_emu_audio_ring(s->emu), &s->audio_dead);
            pthread_mutex_lock(&s->pause_mtx);
            s->audio = fresh;            /* NULL → silent fallback pacing below */
            pthread_mutex_unlock(&s->pause_mtx);
            audio_retry_in = 60;
        }

        int turbo = atomic_load(&s->turbo) ? 1 : 0;
        if (turbo != applied_turbo) {
            sb_emu_set_turbo(s->emu, turbo);
            applied_turbo = turbo;
        }

        if (atomic_load(&s->rewinding)) {
            /* pop 2 → run 1: net one frame backwards per iteration (iOS shape).
               On empty history, hold position — never creep forward under the
               user's finger. */
            sb_emu_rewind_pop(s->emu);
            if (!sb_emu_rewind_pop(s->emu)) {
                struct timespec ts = {0, 16600000};
                nanosleep(&ts, NULL);
                continue;
            }
        }

        sb_emu_run_frame(s->emu);   /* blocks on the audio ring => paced */
        /* Turbo drops audio, so the ring no longer paces; the Core's own wall-clock sync is
           compiled out (GB_DISABLE_TIMEKEEPING — two pacers fought and clicked), so the
           cap is enforced here against CLOCK_MONOTONIC. */
        int fns = turbo ? atomic_load(&s->turbo_frame_ns) : 0;
        if (fns) {
            struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
            int64_t t = (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;
            if (t > turbo_next + fns) turbo_next = t;   /* first frame / after a stall: resync */
            turbo_next += fns;
            if (turbo_next > t) {
                struct timespec ts = { 0, (long)(turbo_next - t) };
                nanosleep(&ts, NULL);
            }
        }
        else turbo_next = 0;
        atomic_store(&s->battery_dirty, sb_emu_battery_dirty(s->emu) != 0);
        if (sb_emu_link_dead(s->emu)) {
            int exp = SB_LINK_CONNECTED;   /* don't clobber a concurrent disconnect's IDLE */
            atomic_compare_exchange_strong(&s->link_status, &exp, SB_LINK_ERROR);
        }
        if (!s->audio) {
            sb_ring_flush(sb_emu_audio_ring(s->emu));
            struct timespec ts = {0, 16600000};
            nanosleep(&ts, NULL);
        }
    }
    return NULL;
}

sb_session *sb_session_create(int model, const uint8_t *rom, size_t rom_len,
                              const uint8_t *sav, size_t sav_len)
{
    sb_emulator *emu = sb_emu_create(model, rom, rom_len, sav, sav_len);
    if (!emu) return NULL;
    sb_session *s = calloc(1, sizeof(*s));
    if (!s) { sb_emu_destroy(emu); return NULL; }
    s->emu = emu;
    atomic_init(&s->link_status, SB_LINK_IDLE);
    atomic_init(&s->link_cancel, false);
    atomic_init(&s->link_pending, NULL);
    atomic_init(&s->turbo, false);
    atomic_init(&s->rewinding, false);
    atomic_init(&s->audio_drop, false);
    atomic_init(&s->audio_dead, false);
    atomic_init(&s->battery_dirty, false);
    atomic_init(&s->volume, 256);
    atomic_init(&s->filter, 0);
    atomic_init(&s->turbo_frame_ns, 0);
    sb_emu_set_volume_ptr(emu, &s->volume);
    sb_emu_set_audio_drop(emu, &s->audio_drop);
    pthread_mutex_init(&s->pause_mtx, NULL);
    pthread_cond_init(&s->pause_cv, NULL);
    pthread_cond_init(&s->parked_cv, NULL);
    return s;
}

void sb_session_set_boot_rom(sb_session *s, int type, const uint8_t *data, size_t len)
{ if (s) sb_emu_set_boot_rom(s->emu, type, data, len); }

/* Park the emu thread for a blocking op; restore the previous pause state after.
   Nested-safe with an outer explicit pause (menu open): if already paused, the
   op runs parked and park_end leaves it paused. */
static int park_begin(sb_session *s)
{
    pthread_mutex_lock(&s->pause_mtx);
    int was = s->paused;
    pthread_mutex_unlock(&s->pause_mtx);
    if (!was) sb_session_pause(s, 1);
    return was;
}

static void park_end(sb_session *s, int was_paused)
{
    if (!was_paused) sb_session_pause(s, 0);
}

void sb_session_reset(sb_session *s)
{
    if (!s) return;
    int was = park_begin(s);
    sb_emu_reset(s->emu);
    park_end(s, was);
}

size_t sb_session_save_battery(sb_session *s, uint8_t **out)
{
    if (!s) return 0;
    int was = park_begin(s);
    size_t n = sb_emu_save_battery(s->emu, out);
    park_end(s, was);
    return n;
}

void sb_session_set_turbo(sb_session *s, int on)
{
    if (!s) return;
    if (on) {
        atomic_store(&s->audio_drop, true);
        sb_ring_flush(sb_emu_audio_ring(s->emu));   /* unblock producer + drop latency (SDL clears its queue too) */
        atomic_store(&s->turbo, true);
    }
    else {
        atomic_store(&s->turbo, false);
        atomic_store(&s->audio_drop, false);
    }
}

void sb_session_set_rewinding(sb_session *s, int on)
{
    if (!s) return;
    atomic_store(&s->rewinding, on != 0);
}

int sb_session_battery_dirty(sb_session *s)
{
    return s ? (atomic_load(&s->battery_dirty) ? 1 : 0) : 0;
}

void sb_session_clear_battery_dirty(sb_session *s)
{
    if (!s) return;
    int was = park_begin(s);
    sb_emu_clear_battery_dirty(s->emu);
    atomic_store(&s->battery_dirty, false);
    park_end(s, was);
}

size_t sb_session_save_state(sb_session *s, uint8_t **out)
{
    if (!s) return 0;
    int was = park_begin(s);
    size_t n = sb_emu_save_state(s->emu, out);
    park_end(s, was);
    return n;
}

int sb_session_load_state(sb_session *s, const uint8_t *buf, size_t n)
{
    if (!s) return -1;
    int was = park_begin(s);
    int ret = sb_emu_load_state(s->emu, buf, n);
    park_end(s, was);
    return ret;
}

void sb_session_switch_model(sb_session *s, int model)
{
    if (!s) return;
    int was = park_begin(s);
    sb_emu_switch_model(s->emu, model);
    park_end(s, was);
}

void sb_session_apply_settings(sb_session *s, const sb_settings *cfg)
{
    if (!s) return;
    /* 16.742 ms = one frame at 59.7275 Hz; cap N× → at least that/N per turbo frame. */
    atomic_store(&s->turbo_frame_ns, cfg->turbo_cap > 0 ? (int)(16742706.0 / cfg->turbo_cap) : 0);
    int was = park_begin(s);
    sb_emu_apply_settings(s->emu, cfg);
    park_end(s, was);
}

void sb_session_set_palette(sb_session *s, int builtin_index, const uint32_t rgb[4])
{
    if (!s) return;
    int was = park_begin(s);
    sb_emu_set_palette(s->emu, builtin_index, rgb);
    park_end(s, was);
}

void sb_session_set_volume(sb_session *s, int volume_256)
{
    if (!s) return;
    if (volume_256 < 0) volume_256 = 0;
    if (volume_256 > 256) volume_256 = 256;
    atomic_store(&s->volume, volume_256);
}

void sb_session_set_filter(sb_session *s, int mode)
{
    if (!s) return;
    atomic_store(&s->filter, mode == 1 ? 1 : 0);   /* unknown mode → off */
}

int sb_session_rumble_amplitude(sb_session *s)
{
    return s ? sb_emu_rumble_amplitude(s->emu) : 0;
}

int sb_session_add_cheat(sb_session *s, const char *code, const char *desc, int enabled)
{
    if (!s) return 0;
    int was = park_begin(s);
    int ok = sb_emu_add_cheat(s->emu, code, desc, enabled);
    park_end(s, was);
    return ok;
}

void sb_session_remove_all_cheats(sb_session *s)
{
    if (!s) return;
    int was = park_begin(s);
    sb_emu_remove_all_cheats(s->emu);
    park_end(s, was);
}

void sb_session_set_cheats_enabled(sb_session *s, int on)
{
    if (!s) return;
    int was = park_begin(s);
    sb_emu_set_cheats_enabled(s->emu, on);
    park_end(s, was);
}

bool sb_session_has_accelerometer(sb_session *s)
{
    return s ? sb_emu_has_accelerometer(s->emu) : false;
}

void sb_session_set_accelerometer(sb_session *s, double x, double y)
{
    if (s) sb_emu_set_accelerometer(s->emu, x, y);
}

void sb_session_connect_printer(sb_session *s)
{
    if (!s) return;
    sb_session_link_disconnect(s);   /* printer + link are mutually exclusive on the serial port */
    int was = park_begin(s);
    sb_emu_connect_printer(s->emu);
    park_end(s, was);
}

void sb_session_disconnect_printer(sb_session *s)
{
    if (!s) return;
    int was = park_begin(s);
    sb_emu_disconnect_printer(s->emu);
    park_end(s, was);
}

unsigned sb_session_printer_generation(sb_session *s)
{
    return s ? sb_emu_printer_generation(s->emu) : 0;
}

unsigned sb_session_printer_feed(sb_session *s, uint32_t *dst, unsigned max_rows)
{
    return s ? sb_emu_printer_feed(s->emu, dst, max_rows) : 0;
}

void sb_session_printer_clear(sb_session *s)
{
    if (s) sb_emu_printer_clear(s->emu);
}

bool sb_session_camera_wanted(sb_session *s)
{
    return s ? sb_emu_camera_wanted(s->emu) : false;
}

void sb_session_camera_deliver(sb_session *s, const uint8_t *gray)
{
    if (s) sb_emu_camera_deliver(s->emu, gray);
}

static void *link_worker(void *arg) {
    sb_session *s = arg;
    /* Blocking accept/connect, but cancellable via link_cancel (bounded ~250 ms). */
    sb_transport *t = s->link_is_listen
        ? sb_transport_tcp_listen(s->link_port, &s->link_cancel)
        : sb_transport_tcp_connect(s->link_host, s->link_port, &s->link_cancel);
    if (!t) {
        /* cancelled → disconnect owns the status; real failure → ERROR */
        if (!atomic_load(&s->link_cancel)) atomic_store(&s->link_status, SB_LINK_ERROR);
        return NULL;
    }
    if (atomic_load(&s->link_cancel)) { t->close(t); return NULL; }   /* aborted post-connect */
    /* Hand the transport to the emu thread; it attaches at a frame boundary (no cross-thread
       park). CONNECTED now — the socket is up even if attach waits for unpause. */
    atomic_store(&s->link_pending, t);
    atomic_store(&s->link_status, SB_LINK_CONNECTED);
    return NULL;
}

static void link_join_if_live(sb_session *s) {
    if (s->link_thread_live) { pthread_join(s->link_thread, NULL); s->link_thread_live = false; }
}

void sb_session_link_listen(sb_session *s, int port) {
    if (!s) return;
    sb_session_link_disconnect(s);           /* tear down any prior */
    sb_session_disconnect_printer(s);        /* printer + link are mutually exclusive on the serial port */
    atomic_store(&s->link_cancel, false);
    s->link_is_listen = 1; s->link_port = port;
    atomic_store(&s->link_status, SB_LINK_LISTENING);
    if (pthread_create(&s->link_thread, NULL, link_worker, s) == 0) s->link_thread_live = true;
    else atomic_store(&s->link_status, SB_LINK_ERROR);
}

void sb_session_link_connect(sb_session *s, const char *host, int port) {
    if (!s || !host) return;
    sb_session_link_disconnect(s);
    sb_session_disconnect_printer(s);
    strncpy(s->link_host, host, sizeof(s->link_host) - 1);
    s->link_host[sizeof(s->link_host) - 1] = 0;
    atomic_store(&s->link_cancel, false);
    s->link_is_listen = 0; s->link_port = port;
    atomic_store(&s->link_status, SB_LINK_CONNECTING);
    if (pthread_create(&s->link_thread, NULL, link_worker, s) == 0) s->link_thread_live = true;
    else atomic_store(&s->link_status, SB_LINK_ERROR);
}

void sb_session_link_disconnect(sb_session *s) {
    if (!s) return;
    atomic_store(&s->link_cancel, true);     /* abort a blocking accept/connect (~250 ms) */
    link_join_if_live(s);                     /* bounded join — worker can no longer block forever */
    /* Free a transport the worker published but the emu thread hadn't attached yet. */
    sb_transport *pt = atomic_exchange(&s->link_pending, NULL);
    if (pt) pt->close(pt);
    /* Detach any attached link, parked (UI thread; GB_disconnect_serial needs the emu stopped). */
    int was = park_begin(s);
    sb_emu_link_clear(s->emu);
    park_end(s, was);
    atomic_store(&s->link_status, SB_LINK_IDLE);
}

int sb_session_link_status(sb_session *s) { return s ? atomic_load(&s->link_status) : SB_LINK_IDLE; }

void sb_session_copy_frame(sb_session *s, uint32_t *dst, unsigned *w, unsigned *h)
{
    if (!s) { *w = *h = 0; return; }
    sb_emu_copy_front(s->emu, dst, w, h);   /* fb_mtx-protected copy */
}

uint32_t sb_session_screen_size(sb_session *s)
{
    return s ? sb_emu_screen_size(s->emu) : 0;
}

void sb_session_start(sb_session *s, ANativeWindow *win)
{
    if (!s || atomic_load(&s->running)) {
        if (win) ANativeWindow_release(win);
        return;
    }
    s->win = win;
    atomic_store(&s->running, true);
    s->paused = 0; /* pre-thread: no lock needed, pthread_create provides the happens-before */
    s->audio = sb_audio_start(sb_emu_audio_ring(s->emu), &s->audio_dead);
    if (!s->audio) {
        __android_log_print(ANDROID_LOG_WARN, "SameBoy",
                            "audio start failed; continuing without audio");
    }
    s->render = sb_render_start(win, s->emu, &s->filter);
    if (pthread_create(&s->emu_thread, NULL, emu_loop, s) != 0) {
        atomic_store(&s->running, false);
        sb_render_stop(s->render); s->render = NULL;
        sb_audio_stop(s->audio);   s->audio = NULL;
        if (win) ANativeWindow_release(win);
        s->win = NULL;
        return;
    }
}

void sb_session_stop(sb_session *s)
{
    if (!s || !atomic_load(&s->running)) return;
    atomic_store(&s->running, false);
    pthread_mutex_lock(&s->pause_mtx);
    s->paused = 0;
    pthread_cond_broadcast(&s->pause_cv);
    pthread_mutex_unlock(&s->pause_mtx);
    /* unblock the emu thread if it's waiting on a full audio ring */
    sb_ring_flush(sb_emu_audio_ring(s->emu));
    pthread_join(s->emu_thread, NULL);
    sb_render_stop(s->render); s->render = NULL;
    pthread_mutex_lock(&s->pause_mtx);
    sb_audio *audio = s->audio;   /* take under pause_mtx so a racing pause can't UAF */
    s->audio = NULL;
    pthread_mutex_unlock(&s->pause_mtx);
    sb_audio_stop(audio);
    if (s->win) { ANativeWindow_release(s->win); s->win = NULL; }
}

void sb_session_pause(sb_session *s, int paused)
{
    if (!s) return;
    pthread_mutex_lock(&s->pause_mtx);
    s->paused = paused;
    pthread_cond_broadcast(&s->pause_cv);
    pthread_mutex_unlock(&s->pause_mtx);
    if (paused) {
        /* Synchronous pause: the caller (battery save) must not observe a
           frame mid-run. Unblock a frame stuck on a full audio ring so it
           can finish, then wait until the emu thread has actually parked. */
        sb_ring_flush(sb_emu_audio_ring(s->emu));
        pthread_mutex_lock(&s->pause_mtx);
        while (!s->parked && atomic_load(&s->running))
            pthread_cond_wait(&s->parked_cv, &s->pause_mtx);
        pthread_mutex_unlock(&s->pause_mtx);
    }
    pthread_mutex_lock(&s->pause_mtx);
    if (s->audio) sb_audio_set_paused(s->audio, paused);   /* under pause_mtx vs. stop */
    pthread_mutex_unlock(&s->pause_mtx);
}

void sb_session_set_key(sb_session *s, int idx, int pressed) { if (s) sb_emu_set_key(s->emu, idx, pressed); }

void sb_session_destroy(sb_session *s)
{
    if (!s) return;
    if (atomic_load(&s->running)) sb_session_stop(s);
    sb_session_link_disconnect(s);
    sb_emu_destroy(s->emu);
    pthread_mutex_destroy(&s->pause_mtx);
    pthread_cond_destroy(&s->pause_cv);
    pthread_cond_destroy(&s->parked_cv);
    free(s);
}

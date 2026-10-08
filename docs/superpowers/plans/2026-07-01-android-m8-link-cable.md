# SameBoy Android M8 — Link Cable Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** TCP link cable between two SameBoy instances — a byte-level master/slave serial
bridge over a pluggable transport, connect UI, graceful disconnect. No new Core files, no
new Gradle dependency. Spec: `specs/2026-07-01-android-m8-link-cable.md`.

**Clocking model (from the spec — read it):** master reads the whole outgoing byte at the
first `bit_start` (`GB_safe_read_memory(gb,0xFF01)`), sends it, blocking-recvs the peer byte
(timeout→0xFF), feeds it back through the 8 `bit_end` returns MSB-first. Slave (external
clock) is polled each frame: non-blocking recv of the master byte → send our SB back →
`GB_serial_set_data_bit`×8. Ordering: master send → slave recv → slave send → master recv.

**Coverage:** 1. Transport (loopback+TCP): Task 1 · 2. sb_link bridge: Task 2 ·
3. emulator glue + two-core host test: Task 3 · 4. Session helper-thread FSM: Task 4 ·
5. JNI: Task 5 · 6. Link UI + INTERNET: Task 6 · 7. Integration: Task 7.

**Conventions:** Work only from your task brief. Never modify `Core/`. No formatters/linters.
Build: `cd Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android
./gradlew :app:assembleDebug`. Host tests: `Android/jni/test/run_host_tests.sh`.

---

## Task 1 — Native transport (link.h + loopback + TCP)

**Files (new):** `Android/jni/link.h`, `Android/jni/link.c`.

### link.h
```c
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "emulator.h"    /* sb_emulator, GB types via <Core/gb.h> */

/* --- Transport: a reliable ordered byte pipe (TCP now; Bluetooth later; loopback in tests) --- */
typedef struct sb_transport sb_transport;
struct sb_transport {
    bool (*send)(sb_transport *t, uint8_t byte);              /* true on success */
    bool (*recv)(sb_transport *t, uint8_t *out, int timeout_ms); /* 0 = non-blocking; false on timeout/closed */
    void (*close)(sb_transport *t);                           /* idempotent; unblocks a pending recv, frees t */
};
sb_transport *sb_transport_tcp_listen(int port);                 /* blocks for one peer; NULL on error */
sb_transport *sb_transport_tcp_connect(const char *host, int port); /* blocks; NULL on error */
sb_transport *sb_transport_loopback_pair(sb_transport **other);  /* test: cross-wired ends of one FIFO */

/* --- Link bridge (Task 2) --- */
typedef struct sb_link sb_link;
sb_link *sb_link_create(sb_transport *t);   /* takes ownership of t */
void     sb_link_destroy(sb_link *s);       /* closes+frees transport */
void     sb_link_bit_start(sb_link *s, GB_gameboy_t *gb, bool bit);  /* master: 1st bit → exchange */
bool     sb_link_bit_end(sb_link *s, GB_gameboy_t *gb);              /* master: feed peer byte bit */
void     sb_link_slave_poll(sb_link *s, GB_gameboy_t *gb);           /* slave: per-frame, emu thread */
```

### link.c — transport half
```c
#include "link.h"
#include <Core/gb.h>
#include <Core/memory.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>

/* ---------- loopback (host tests): two ends over one mutex+cond byte FIFO ---------- */
#define LB_CAP 4096
typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
    uint8_t q[2][LB_CAP];
    unsigned head[2], tail[2];   /* q[i]: bytes flowing toward end i */
    int refs;
    bool closed;
} lb_shared;

typedef struct { sb_transport base; lb_shared *sh; int idx; } lb_transport;

static bool lb_send(sb_transport *t, uint8_t byte) {
    lb_transport *l = (lb_transport *)t;
    lb_shared *sh = l->sh;
    int dst = l->idx ^ 1;                 /* send toward the other end */
    pthread_mutex_lock(&sh->mtx);
    if (sh->closed) { pthread_mutex_unlock(&sh->mtx); return false; }
    unsigned n = (sh->tail[dst] + 1) % LB_CAP;
    if (n == sh->head[dst]) { pthread_mutex_unlock(&sh->mtx); return false; } /* full */
    sh->q[dst][sh->tail[dst]] = byte;
    sh->tail[dst] = n;
    pthread_cond_broadcast(&sh->cv);
    pthread_mutex_unlock(&sh->mtx);
    return true;
}
static bool lb_recv(sb_transport *t, uint8_t *out, int timeout_ms) {
    lb_transport *l = (lb_transport *)t;
    lb_shared *sh = l->sh;
    int me = l->idx;
    pthread_mutex_lock(&sh->mtx);
    while (sh->head[me] == sh->tail[me] && !sh->closed) {
        if (timeout_ms == 0) { pthread_mutex_unlock(&sh->mtx); return false; }
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        if (pthread_cond_timedwait(&sh->cv, &sh->mtx, &ts) == ETIMEDOUT) {
            pthread_mutex_unlock(&sh->mtx); return false;
        }
    }
    if (sh->head[me] == sh->tail[me]) { pthread_mutex_unlock(&sh->mtx); return false; } /* closed+empty */
    *out = sh->q[me][sh->head[me]];
    sh->head[me] = (sh->head[me] + 1) % LB_CAP;
    pthread_mutex_unlock(&sh->mtx);
    return true;
}
static void lb_close(sb_transport *t) {
    lb_transport *l = (lb_transport *)t;
    lb_shared *sh = l->sh;
    pthread_mutex_lock(&sh->mtx);
    sh->closed = true;
    pthread_cond_broadcast(&sh->cv);
    int last = (--sh->refs == 0);
    pthread_mutex_unlock(&sh->mtx);
    if (last) { pthread_mutex_destroy(&sh->mtx); pthread_cond_destroy(&sh->cv); free(sh); }
    free(l);
}
sb_transport *sb_transport_loopback_pair(sb_transport **other) {
    lb_shared *sh = calloc(1, sizeof(*sh));
    if (!sh) return NULL;
    pthread_mutex_init(&sh->mtx, NULL);
    pthread_cond_init(&sh->cv, NULL);
    sh->refs = 2;
    lb_transport *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    if (!a || !b) { free(a); free(b); pthread_mutex_destroy(&sh->mtx); pthread_cond_destroy(&sh->cv); free(sh); return NULL; }
    a->base.send = lb_send; a->base.recv = lb_recv; a->base.close = lb_close; a->sh = sh; a->idx = 0;
    b->base.send = lb_send; b->base.recv = lb_recv; b->base.close = lb_close; b->sh = sh; b->idx = 1;
    *other = &b->base;
    return &a->base;
}

/* ---------- TCP ---------- */
typedef struct { sb_transport base; int fd; } tcp_transport;

static bool tcp_send(sb_transport *t, uint8_t byte) {
    tcp_transport *c = (tcp_transport *)t;
    if (c->fd < 0) return false;
    while (1) {
        ssize_t n = send(c->fd, &byte, 1, MSG_NOSIGNAL);
        if (n == 1) return true;
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
}
static bool tcp_recv(sb_transport *t, uint8_t *out, int timeout_ms) {
    tcp_transport *c = (tcp_transport *)t;
    if (c->fd < 0) return false;
    struct pollfd pfd = { .fd = c->fd, .events = POLLIN };
    int pr = poll(&pfd, 1, timeout_ms);
    if (pr <= 0) return false;                 /* 0 = timeout, <0 = error */
    while (1) {
        ssize_t n = recv(c->fd, out, 1, 0);
        if (n == 1) return true;
        if (n < 0 && errno == EINTR) continue;
        return false;                          /* 0 = peer closed, <0 = error */
    }
}
static void tcp_close(sb_transport *t) {
    tcp_transport *c = (tcp_transport *)t;
    if (c->fd >= 0) { shutdown(c->fd, SHUT_RDWR); close(c->fd); c->fd = -1; }
    free(c);
}
static sb_transport *tcp_wrap(int fd) {
    int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    tcp_transport *c = calloc(1, sizeof(*c));
    if (!c) { close(fd); return NULL; }
    c->base.send = tcp_send; c->base.recv = tcp_recv; c->base.close = tcp_close; c->fd = fd;
    return &c->base;
}
sb_transport *sb_transport_tcp_listen(int port) {
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) return NULL;
    int one = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((uint16_t)port);
    if (bind(ls, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(ls, 1) < 0) { close(ls); return NULL; }
    int fd = accept(ls, NULL, NULL);
    close(ls);
    if (fd < 0) return NULL;
    return tcp_wrap(fd);
}
sb_transport *sb_transport_tcp_connect(const char *host, int port) {
    char portstr[16]; snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return NULL;
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return NULL; }
    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) { close(fd); freeaddrinfo(res); return NULL; }
    freeaddrinfo(res);
    return tcp_wrap(fd);
}
```
**Acceptance:** compiles (exercised by Task 3's test). No behavior change to existing code.

---

## Task 2 — Native sb_link bridge (link.c continued)

**Files:** `Android/jni/link.c` (append; do not disturb Task 1's transport code).

```c
/* ---------- sb_link: byte-level master/slave serial bridge ---------- */
#define SB_LINK_TIMEOUT_MS 1500   /* per-byte master wait; peer gone → 0xFF, no hang */

struct sb_link {
    sb_transport *t;
    uint8_t in_byte;   /* peer byte for the current master transfer */
    int     bits;      /* master bit_end index 0..7; 0 = start of a fresh byte */
};

sb_link *sb_link_create(sb_transport *t) {
    sb_link *s = calloc(1, sizeof(*s));
    if (!s) { if (t) t->close(t); return NULL; }
    s->t = t;
    return s;
}
void sb_link_destroy(sb_link *s) {
    if (!s) return;
    if (s->t) s->t->close(s->t);
    free(s);
}

/* master: fired per bit. On the first bit of a byte (bits==0) SB holds the whole outgoing
   byte — exchange it now and stash the peer byte for the 8 bit_end reads. */
void sb_link_bit_start(sb_link *s, GB_gameboy_t *gb, bool bit) {
    (void)bit;
    if (!s || s->bits != 0) return;
    uint8_t out = GB_safe_read_memory(gb, 0xFF01);   /* SB */
    if (!s->t->send(s->t, out) || !s->t->recv(s->t, &s->in_byte, SB_LINK_TIMEOUT_MS)) {
        s->in_byte = 0xFF;   /* peer gone / timeout: link idle-high */
    }
}
/* master: return the peer byte MSB-first over 8 calls; wraps bits back to 0 after the 8th. */
bool sb_link_bit_end(sb_link *s, GB_gameboy_t *gb) {
    (void)gb;
    if (!s) return true;
    bool r = (s->in_byte >> (7 - s->bits)) & 1;
    s->bits = (s->bits + 1) & 7;
    return r;
}
/* slave: per-frame on the emu thread. If externally clocked and a master byte is waiting,
   send our SB back and clock the 8 bits in (MSB-first). */
void sb_link_slave_poll(sb_link *s, GB_gameboy_t *gb) {
    if (!s) return;
    uint8_t sc = GB_safe_read_memory(gb, 0xFF02);
    if ((sc & 0x81) != 0x80) return;                 /* not armed as slave */
    uint8_t m;
    if (!s->t->recv(s->t, &m, 0)) return;            /* non-blocking; nothing yet */
    uint8_t out = GB_safe_read_memory(gb, 0xFF01);   /* our outgoing byte, before clocking */
    s->t->send(s->t, out);
    for (int i = 0; i < 8; i++) GB_serial_set_data_bit(gb, (m >> (7 - i)) & 1);
}
```
Notes: `bit_start` only exchanges when `bits==0` — the first `bit_start` per byte (from the
SC write); Core's mid-byte `bit_start`s (bits 1..7) are no-ops. After 8 `bit_end`s `bits`
wraps to 0 and Core stops firing (serial_count==0) until the next SC write. `GB_safe_read_
memory`/`GB_serial_set_data_bit` are public Core APIs (memory.h/gb.h) — no Core edits.

**Acceptance:** compiles; logic matches the spec's clocking (verified by Task 3's test).

---

## Task 3 — emulator.c glue + two-core host test

**Files:** `Android/jni/emulator.h`, `Android/jni/emulator.c`,
`Android/jni/test/test_link.c` (new), `Android/jni/test/run_host_tests.sh`.

### emulator.h — add after the camera block
```c
/* --- Link cable (M8) --- */
struct sb_link;   /* fwd */
void sb_emu_link_set(sb_emulator *e, struct sb_link *link);  /* attach (call parked); replaces any old */
void sb_emu_link_clear(sb_emulator *e);                      /* detach + destroy (call parked) */
```

### emulator.c
- `#include "link.h"` and `#include <Core/memory.h>` at the top (after `#include "emulator.h"`).
  `link.h` provides the link decls; `memory.h` provides `GB_safe_read_memory` for `sb_emu_peek_sb`.
- Add field to `struct sb_emulator`: `struct sb_link *link;` (after the camera fields).
- Master trampolines (static, above `sb_emu_create`):
```c
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
```
- In `sb_emu_run_frame` (already has the camera drain), add after `GB_run_frame`:
```c
    if (e->link) sb_link_slave_poll(e->link, &e->gb);
```
- In `sb_emu_destroy`: `sb_emu_link_clear(e);` (before freeing — safe if NULL).

### test/test_link.c (new)
```c
/* Two cores + a loopback transport, each running a hand-written serial ROM, on their own
   threads. Master sends 0xA5 (internal clock), slave 0x3C (external clock). After the
   transfer both SB registers must hold the OTHER byte. alarm() nets any deadlock. */
#include "../emulator.h"
#include "../link.h"
#include <Core/memory.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* MBC-less 32KB ROM: entry jp 0x150; program at 0x150 loads SB, starts SC, spins on bit7. */
static uint8_t *make_link_rom(uint8_t sb, uint8_t sc, size_t *len) {
    size_t n = 32 * 1024;
    uint8_t *rom = calloc(1, n);
    rom[0x100] = 0x00; rom[0x101] = 0xC3; rom[0x102] = 0x50; rom[0x103] = 0x01;  /* jp 0x150 */
    uint8_t prog[] = {
        0x3E, sb,   0xE0, 0x01,   /* ld a,sb ; ldh (SB),a   */
        0x3E, sc,   0xE0, 0x02,   /* ld a,sc ; ldh (SC),a   */
        0xF0, 0x02, 0xE6, 0x80,   /* ldh a,(SC) ; and 0x80  */
        0x20, 0xFA,               /* jr nz,-6  (wait bit7)  */
        0x18, 0xFE,               /* jr -2     (done spin)  */
    };
    memcpy(&rom[0x150], prog, sizeof(prog));
    rom[0x147] = 0x00; rom[0x148] = 0x00; rom[0x149] = 0x00;   /* ROM only, no RAM */
    uint8_t c = 0; for (int i = 0x134; i <= 0x14C; i++) c = c - rom[i] - 1;
    rom[0x14D] = c;
    *len = n;
    return rom;
}

typedef struct { sb_emulator *e; int done_after; } runner;
static void *run_core(void *arg) {
    runner *r = arg;
    for (int i = 0; i < r->done_after; i++) sb_emu_run_frame(r->e);
    return NULL;
}

int main(void) {
    alarm(30);
    /* loopback transport unit check */
    sb_transport *x, *y = NULL; x = sb_transport_loopback_pair(&y);
    assert(x && y);
    assert(x->send(x, 0x11));
    uint8_t g; assert(y->recv(y, &g, 100) && g == 0x11);
    assert(!y->recv(y, &g, 0));          /* non-blocking empty → false */
    x->close(x); y->close(y);

    /* two-core exchange */
    size_t ml, sl;
    uint8_t *mrom = make_link_rom(0xA5, 0x81, &ml);   /* master: internal clock */
    uint8_t *srom = make_link_rom(0x3C, 0x80, &sl);   /* slave:  external clock */
    sb_emulator *m = sb_emu_create(0x002, mrom, ml, NULL, 0);
    sb_emulator *s = sb_emu_create(0x002, srom, sl, NULL, 0);
    assert(m && s);
    sb_emu_reset(m); sb_emu_reset(s);

    sb_transport *ta, *tb = NULL; ta = sb_transport_loopback_pair(&tb);
    sb_emu_link_set(m, sb_link_create(ta));
    sb_emu_link_set(s, sb_link_create(tb));

    pthread_t mt, st;
    runner mr = { m, 600 }, sr = { s, 600 };   /* ~10s of frames; transfer completes early */
    pthread_create(&st, NULL, run_core, &sr);   /* start slave first so it polls */
    pthread_create(&mt, NULL, run_core, &mr);
    pthread_join(mt, NULL); pthread_join(st, NULL);

    /* Read SB via a tiny accessor rather than poking the opaque struct: */
    assert(sb_emu_peek_sb(m) == 0x3C);   /* master received slave's byte */
    assert(sb_emu_peek_sb(s) == 0xA5);   /* slave received master's byte */

    sb_emu_link_clear(m); sb_emu_link_clear(s);
    sb_emu_destroy(m); sb_emu_destroy(s);
    free(mrom); free(srom);
    printf("link: all tests passed\n");
    return 0;
}
```
**Add a tiny test accessor** (emulator.h + emulator.c) so the test reads SB without poking
the opaque struct:
```c
/* emulator.h */ uint8_t sb_emu_peek_sb(sb_emulator *e);   /* test/debug: current SB (0xFF01) */
/* emulator.c */ uint8_t sb_emu_peek_sb(sb_emulator *e) { return e ? GB_safe_read_memory(&e->gb, 0xFF01) : 0; }
```
`GB_safe_read_memory` is public (memory.h). Add the accessor decl to emulator.h and the body
to emulator.c (both in Task 3), so `test_link.c` reads SB without touching the opaque struct.

### run_host_tests.sh — add after the session block
```sh
echo "== link =="
eval cc $CFLAGS test/test_link.c link.c emulator.c ring_buffer.c $CORE_SRC -lpthread -lm -o /tmp/sb_tlink
/tmp/sb_tlink
```

### Android.mk — no edit needed
`Android/jni/Android.mk` builds `BRIDGE_SOURCES := $(wildcard $(LOCAL_PATH)/*.c)`, so the
new `link.c` is picked up automatically. (Do NOT edit Android.mk.) The host test compiles
`link.c` explicitly via the `run_host_tests.sh` line above.

**Acceptance:** `run_host_tests.sh` prints `link: all tests passed` — the two cores swap
bytes (master SB 0xA5→0x3C, slave 0x3C→0xA5), proving the clocking bridge. Existing tests
still green. 4-ABI build still links (Android.mk includes link.c).

---

## Task 4 — Session layer (helper-thread connect FSM)

**Files:** `Android/jni/session.c`, `Android/jni/session.h`.

### session.h — add after the printer/camera decls
```c
enum { SB_LINK_IDLE = 0, SB_LINK_LISTENING = 1, SB_LINK_CONNECTING = 2,
       SB_LINK_CONNECTED = 3, SB_LINK_ERROR = 4 };
void sb_session_link_listen(sb_session *s, int port);
void sb_session_link_connect(sb_session *s, const char *host, int port);
void sb_session_link_disconnect(sb_session *s);
int  sb_session_link_status(sb_session *s);
```

### session.c
Add `#include "link.h"` and to `struct sb_session`:
```c
    atomic_int  link_status;      /* SB_LINK_* */
    pthread_t   link_thread;
    bool        link_thread_live;
    char        link_host[64];    /* connect target */
    int         link_port;
    int         link_is_listen;   /* 1 listen, 0 connect */
```
Helper thread + API:
```c
static void *link_worker(void *arg) {
    sb_session *s = arg;
    sb_transport *t = s->link_is_listen
        ? sb_transport_tcp_listen(s->link_port)
        : sb_transport_tcp_connect(s->link_host, s->link_port);
    if (!t) { atomic_store(&s->link_status, SB_LINK_ERROR); return NULL; }
    /* wire into the emulator, parked (registers serial callbacks) */
    int was = park_begin(s);
    sb_emu_link_set(s->emu, sb_link_create(t));
    park_end(s, was);
    atomic_store(&s->link_status, SB_LINK_CONNECTED);
    return NULL;
}
static void link_join_if_live(sb_session *s) {
    if (s->link_thread_live) { pthread_join(s->link_thread, NULL); s->link_thread_live = false; }
}
void sb_session_link_listen(sb_session *s, int port) {
    if (!s) return;
    sb_session_link_disconnect(s);           /* tear down any prior */
    s->link_is_listen = 1; s->link_port = port;
    atomic_store(&s->link_status, SB_LINK_LISTENING);
    if (pthread_create(&s->link_thread, NULL, link_worker, s) == 0) s->link_thread_live = true;
    else atomic_store(&s->link_status, SB_LINK_ERROR);
}
void sb_session_link_connect(sb_session *s, const char *host, int port) {
    if (!s || !host) return;
    sb_session_link_disconnect(s);
    strncpy(s->link_host, host, sizeof(s->link_host) - 1);
    s->link_host[sizeof(s->link_host) - 1] = 0;
    s->link_is_listen = 0; s->link_port = port;
    atomic_store(&s->link_status, SB_LINK_CONNECTING);
    if (pthread_create(&s->link_thread, NULL, link_worker, s) == 0) s->link_thread_live = true;
    else atomic_store(&s->link_status, SB_LINK_ERROR);
}
void sb_session_link_disconnect(sb_session *s) {
    if (!s) return;
    /* Clearing the link closes the transport → unblocks a pending accept/recv in the worker. */
    int was = park_begin(s);
    sb_emu_link_clear(s->emu);
    park_end(s, was);
    link_join_if_live(s);
    atomic_store(&s->link_status, SB_LINK_IDLE);
}
int sb_session_link_status(sb_session *s) { return s ? atomic_load(&s->link_status) : SB_LINK_IDLE; }
```
- Init `atomic_store(&s->link_status, SB_LINK_IDLE)` in `sb_session_create`.
- In `sb_session_destroy`: `sb_session_link_disconnect(s);` before `sb_emu_destroy`.

**Concurrency note (address in review):** if `disconnect` runs while the worker is still
blocked in `accept()`/`connect()` (not yet CONNECTED), `sb_emu_link_clear` is a no-op (link
not set yet) and the worker may still be blocking. `tcp_listen`/`connect` block on their own
socket which `disconnect` can't see yet → the join could hang. **Mitigation for this slice:**
`disconnect` sets status IDLE and joins; a pre-connect listen/accept is only abortable once
connected. Keep the listen port bounded and document that a never-connected listener is
freed at `destroy`. (A cancellable acceptor via a self-pipe is a follow-up.) The worker,
once `tcp_*` returns, checks status and if it's already IDLE, closes the transport and
exits without attaching. Implement that check:
```c
    if (atomic_load(&s->link_status) == SB_LINK_IDLE) { t->close(t); return NULL; }  /* aborted */
```
(place right after the `if (!t)` check in `link_worker`.)

**Acceptance:** compiles; session test still green; status FSM transitions correct.

---

## Task 5 — JNI bridge

**Files:** `Android/jni/sameboy_jni.c`, `Android/app/src/main/java/io/sameboy/android/NativeBridge.java`.

### NativeBridge.java — after the printer/camera natives
```java
    public static native void nativeLinkListen(long ctx, int port);
    public static native void nativeLinkConnect(long ctx, String host, int port);
    public static native void nativeLinkDisconnect(long ctx);
    public static native int nativeLinkStatus(long ctx);   // 0 idle 1 listen 2 connecting 3 connected 4 error
```

### sameboy_jni.c
```c
JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeLinkListen(JNIEnv *env, jclass c, jlong ctx, jint port)
{ (void)env; (void)c; sb_session_link_listen((sb_session *)(uintptr_t)ctx, (int)port); }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeLinkConnect(JNIEnv *env, jclass c, jlong ctx, jstring host, jint port)
{
    (void)c;
    sb_session *s = (sb_session *)(uintptr_t)ctx;
    const char *h = host ? (*env)->GetStringUTFChars(env, host, NULL) : NULL;
    sb_session_link_connect(s, h ? h : "", (int)port);
    if (h) (*env)->ReleaseStringUTFChars(env, host, h);
}

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeLinkDisconnect(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; sb_session_link_disconnect((sb_session *)(uintptr_t)ctx); }

JNIEXPORT jint JNICALL
Java_io_sameboy_android_NativeBridge_nativeLinkStatus(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; return (jint)sb_session_link_status((sb_session *)(uintptr_t)ctx); }
```
**Acceptance:** clean 4-ABI build; `nm` shows the 4 new `NativeBridge_native*` symbols
(total 32).

---

## Task 6 — Link UI + INTERNET permission

**Files (new):** `Android/app/src/main/java/io/sameboy/android/LinkActivity.java`.
**Edit:** `GameMenuDialog.java`, `EmulatorActivity.java`, `AndroidManifest.xml`,
`res/values/strings.xml`.

### AndroidManifest.xml — add at top (after existing permissions)
```xml
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.ACCESS_WIFI_STATE" />
```
And register the activity inside `<application>`:
```xml
        <activity android:name=".LinkActivity" android:exported="false"
                  android:configChanges="orientation|screenSize|keyboardHidden" />
```

### strings.xml — add
```xml
    <string name="link_cable">Link cable</string>
    <string name="link_host">Host</string>
    <string name="link_join">Join</string>
    <string name="link_disconnect">Disconnect</string>
    <string name="link_hint_join">Peer IP (e.g. 192.168.1.5)</string>
    <string name="accessory_link">Link cable</string>
```

### GameMenuDialog.java
- Add `void onLinkCable();` to `Host`.
- Add `"Link cable"` to `showAccessory`'s options (extend to None / Game Boy Printer / Link
  cable) OR add a top-level menu item. Simplest + consistent: add a dedicated menu item.
  Extend the items array + switch: insert `"Link cable"` after `"Printer feed"`, so:
  `{ "Resume","Save state","Load state","Reset","Model","Connect accessory","Printer feed",
     "Link cable","Settings","Exit" }` and shift Settings→8, Exit→9. Case 7:
  `chained[0] = true; h.onLinkCable(); return;` (launches LinkActivity, like Settings).
  Update cases 8/9 accordingly. **Re-read the current switch before editing** and keep every
  existing case's behavior; only indices shift.

### EmulatorActivity.java — implement `onLinkCable`
```java
    @Override public void onLinkCable() {
        menuOpen = false;   // LinkActivity takes over; onResume re-applies
        android.content.Intent i = new android.content.Intent(EmulatorActivity.this, LinkActivity.class);
        i.putExtra(LinkActivity.EXTRA_CTX, ctx);
        startActivity(i);
    }
```

### LinkActivity.java (new)
```java
package io.sameboy.android;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.Gravity;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

/** Link-cable connect screen: Host (listen on 1989) or Join (peer IP), with a live status
 *  line polling NativeBridge.nativeLinkStatus. TCP over the local network. */
public final class LinkActivity extends AppCompatActivity {
    public static final String EXTRA_CTX = "io.sameboy.ctx";
    private static final int PORT = 1989;
    private long ctx;
    private TextView status;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final String[] names = { "Idle", "Listening", "Connecting", "Connected", "Error" };
    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (ctx != 0) {
                int st = NativeBridge.nativeLinkStatus(ctx);
                status.setText("Status: " + names[st >= 0 && st < names.length ? st : 0]);
            }
            handler.postDelayed(this, 500);
        }
    };

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        if (b != null) { finish(); return; }          // process-death: stale ctx
        ctx = getIntent().getLongExtra(EXTRA_CTX, 0);
        if (ctx == 0) { finish(); return; }

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(48, 48, 48, 48);

        TextView ip = new TextView(this);
        ip.setText("This device: " + localIp() + "  (port " + PORT + ")");
        root.addView(ip);

        Button host = new Button(this); host.setText(R.string.link_host);
        root.addView(host);

        final EditText peer = new EditText(this);
        peer.setInputType(InputType.TYPE_CLASS_TEXT);
        peer.setHint(R.string.link_hint_join);
        root.addView(peer);
        Button join = new Button(this); join.setText(R.string.link_join);
        root.addView(join);

        Button disc = new Button(this); disc.setText(R.string.link_disconnect);
        root.addView(disc);

        status = new TextView(this);
        status.setPadding(0, 32, 0, 0);
        root.addView(status);
        setContentView(root);

        host.setOnClickListener(v -> NativeBridge.nativeLinkListen(ctx, PORT));
        join.setOnClickListener(v -> {
            String h = peer.getText().toString().trim();
            if (!h.isEmpty()) NativeBridge.nativeLinkConnect(ctx, h, PORT);
        });
        disc.setOnClickListener(v -> NativeBridge.nativeLinkDisconnect(ctx));
    }

    @Override protected void onResume() { super.onResume(); handler.post(poll); }
    @Override protected void onPause() { super.onPause(); handler.removeCallbacks(poll); }

    private String localIp() {
        try {
            java.util.Enumeration<java.net.NetworkInterface> ifs = java.net.NetworkInterface.getNetworkInterfaces();
            while (ifs.hasMoreElements()) {
                java.net.NetworkInterface nif = ifs.nextElement();
                if (!nif.isUp() || nif.isLoopback()) continue;
                java.util.Enumeration<java.net.InetAddress> addrs = nif.getInetAddresses();
                while (addrs.hasMoreElements()) {
                    java.net.InetAddress a = addrs.nextElement();
                    if (!a.isLoopbackAddress() && a instanceof java.net.Inet4Address) return a.getHostAddress();
                }
            }
        } catch (Exception ignored) {}
        return "?";
    }
}
```
Note: the link does **not** require pausing/parking from the UI — `nativeLink*` are session
calls that park internally. Leaving LinkActivity returns to the game (`onResume` re-applies).

**Acceptance:** builds; menu shows "Link cable"; LinkActivity opens with Host/Join/Disconnect
+ a live status line; INTERNET permission in the manifest.

---

## Task 7 — Integration: build + host tests + symbol/permission

**Files:** none (verification only).

Run:
```
Android/jni/test/run_host_tests.sh                 # must include "link: all tests passed"
cd Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew clean :app:assembleDebug
unzip -l app/build/outputs/apk/debug/app-debug.apk | grep -c 'libsameboy_core.so'   # 4
nm -D --defined-only $(find app/build -name libsameboy_core.so | head -1) | grep -c NativeBridge_native  # 32
$HOME/Android/build-tools/*/aapt2 dump permissions app/build/outputs/apk/debug/app-debug.apk | grep -i internet
```
**Acceptance:** host tests pass incl. link; clean 4-ABI build; 4 `.so`; 32 JNI symbols;
INTERNET permission present.

---

## Task ordering & concurrency
Mostly a dependency chain (tight coupling):
- Task 1 → Task 2 (same file `link.c`).
- Task 3 depends on 1+2 (edits emulator.c/.h + Android.mk + new test).
- Task 4 depends on 3 (`sb_emu_link_set`). Task 5 depends on 4. Task 6 depends on 5 (JNI
  names) + edits GameMenuDialog/EmulatorActivity/manifest/strings.
- Task 7 last.
Suggested: **[1]→[2]→[3]→[4]→[5]→[6]→[7]** sequential. (Task 6's LinkActivity could be
drafted in parallel with 4/5 from the JNI signatures, but the menu-index edit in
GameMenuDialog must not race Task 7's build — keep 6 after 5.)

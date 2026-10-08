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
#include <stdatomic.h>

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

/* Same shell, arbitrary program at 0x150. */
static uint8_t *make_rom(const uint8_t *prog, size_t plen, size_t *len) {
    size_t n = 32 * 1024;
    uint8_t *rom = calloc(1, n);
    rom[0x100] = 0x00; rom[0x101] = 0xC3; rom[0x102] = 0x50; rom[0x103] = 0x01;  /* jp 0x150 */
    memcpy(&rom[0x150], prog, plen);
    uint8_t c = 0; for (int i = 0x134; i <= 0x14C; i++) c = c - rom[i] - 1;
    rom[0x14D] = c;
    *len = n;
    return rom;
}

typedef struct { sb_emulator *e; int done_after; int start_delay_ms; } runner;
static void *run_core(void *arg) {
    runner *r = arg;
    if (r->start_delay_ms) {
        struct timespec ts = { r->start_delay_ms / 1000, (long)(r->start_delay_ms % 1000) * 1000000L };
        nanosleep(&ts, NULL);
    }
    for (int i = 0; i < r->done_after; i++) sb_emu_run_frame(r->e);
    return NULL;
}

/* TCP smoke: a listener thread accepts one peer and hands the transport back. */
typedef struct { int port; sb_transport *t; } listener;
static void *tcp_listener(void *arg) {
    listener *l = arg;
    l->t = sb_transport_tcp_listen(l->port, NULL);
    return NULL;
}
typedef struct { sb_transport *t; } closer;
static void *tcp_closer(void *arg) {
    closer *c = arg;
    struct timespec ts = {0, 200000000};   /* 200 ms, let the peer block in recv first */
    nanosleep(&ts, NULL);
    c->t->close(c->t);
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

    /* TCP smoke over 127.0.0.1: connect ↔ listen, byte both ways, peer-close unblocks recv */
    {
        int port = 48222;
        listener l = { port, NULL };
        pthread_t lt; pthread_create(&lt, NULL, tcp_listener, &l);
        struct timespec ts = {0, 100000000}; nanosleep(&ts, NULL);  /* let listen() bind */
        sb_transport *c = sb_transport_tcp_connect("127.0.0.1", port, NULL);
        pthread_join(lt, NULL);
        assert(c && l.t);                       /* both ends up */
        assert(c->send(c, 0x5A));
        uint8_t b; assert(l.t->recv(l.t, &b, 1000) && b == 0x5A);   /* connect → listen */
        assert(l.t->send(l.t, 0xC3));
        assert(c->recv(c, &b, 1000) && b == 0xC3);                  /* listen → connect */
        assert(!c->recv(c, &b, 0));             /* non-blocking empty → false */
        /* peer close must unblock a pending recv promptly */
        closer cl = { l.t };
        pthread_t ct; pthread_create(&ct, NULL, tcp_closer, &cl);
        assert(!c->recv(c, &b, 5000));          /* returns on peer close, well under 5 s */
        pthread_join(ct, NULL);
        c->close(c);
    }

    /* two-core exchange */
    size_t ml, sl;
    uint8_t *mrom = make_link_rom(0xA5, 0x81, &ml);   /* master: internal clock */
    uint8_t *srom = make_link_rom(0x3C, 0x80, &sl);   /* slave:  external clock */
    sb_emulator *m = sb_emu_create(0x002, mrom, ml, NULL, 0);
    sb_emulator *s = sb_emu_create(0x002, srom, sl, NULL, 0);
    assert(m && s);
    sb_emu_reset(m); sb_emu_reset(s);
    /* No audio consumer on these threads: drop samples so the ~100 ms ring
       never fills and blocks run_frame (see test_audio_drop_nonblocking). */
    static atomic_bool drop; atomic_init(&drop, true);
    sb_emu_set_audio_drop(m, &drop);
    sb_emu_set_audio_drop(s, &drop);

    sb_transport *ta, *tb = NULL; ta = sb_transport_loopback_pair(&tb);
    sb_emu_link_set(m, sb_link_create(ta));
    sb_emu_link_set(s, sb_link_create(tb));

    pthread_t mt, st;
    runner mr = { m, 600, 0 }, sr = { s, 600, 0 };   /* ~10s of frames; transfer completes early */
    pthread_create(&st, NULL, run_core, &sr);   /* start slave first so it polls */
    pthread_create(&mt, NULL, run_core, &mr);
    pthread_join(mt, NULL); pthread_join(st, NULL);

    /* Read SB via a tiny accessor rather than poking the opaque struct: */
    assert(sb_emu_peek_sb(m) == 0x3C);   /* master received slave's byte */
    assert(sb_emu_peek_sb(s) == 0xA5);   /* slave received master's byte */

    sb_emu_link_clear(m); sb_emu_link_clear(s);
    sb_emu_destroy(m); sb_emu_destroy(s);
    free(mrom); free(srom);

    /* Peer parked (in its menu / still in a pre-link screen): the slave thread doesn't run
       for 2 s real time, longer than the master's 1.5 s per-byte timeout. The master retries
       until it reads 0x3C. Before the fix the first timeout dead-latched the master and every
       later exchange returned 0xFF instantly, so it never saw the slave come up. */
    {
        static const uint8_t mprog[] = {
            0x3E, 0xA5, 0xE0, 0x01,   /* loop: ld a,0xA5 ; ldh (SB),a */
            0x3E, 0x81, 0xE0, 0x02,   /*       ld a,0x81 ; ldh (SC),a */
            0xF0, 0x02, 0xE6, 0x80,   /* wait: ldh a,(SC) ; and 0x80  */
            0x20, 0xFA,               /*       jr nz,wait             */
            0xF0, 0x01, 0xFE, 0x3C,   /*       ldh a,(SB) ; cp 0x3C   */
            0x20, 0xEC,               /*       jr nz,loop             */
            0x18, 0xFE,               /*       jr -2                  */
        };
        mrom = make_rom(mprog, sizeof(mprog), &ml);
        srom = make_link_rom(0x3C, 0x80, &sl);
        m = sb_emu_create(0x002, mrom, ml, NULL, 0);
        s = sb_emu_create(0x002, srom, sl, NULL, 0);
        assert(m && s);
        sb_emu_reset(m); sb_emu_reset(s);
        sb_emu_set_audio_drop(m, &drop);
        sb_emu_set_audio_drop(s, &drop);
        ta = sb_transport_loopback_pair(&tb);
        sb_emu_link_set(m, sb_link_create(ta));
        sb_emu_link_set(s, sb_link_create(tb));
        runner mr2 = { m, 600, 0 }, sr2 = { s, 600, 2000 };
        pthread_create(&st, NULL, run_core, &sr2);
        pthread_create(&mt, NULL, run_core, &mr2);
        pthread_join(mt, NULL); pthread_join(st, NULL);
        assert(sb_emu_peek_sb(m) == 0x3C);   /* not 0xFF: master survived the unarmed phase */
        assert(sb_emu_peek_sb(s) == 0xA5);
    }
    sb_emu_link_clear(m); sb_emu_link_clear(s);
    sb_emu_destroy(m); sb_emu_destroy(s);
    free(mrom); free(srom);
    printf("link: all tests passed\n");
    return 0;
}

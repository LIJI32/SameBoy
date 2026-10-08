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
    int pr;
    do { pr = poll(&pfd, 1, timeout_ms); } while (pr < 0 && errno == EINTR);
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
static bool cancelled(atomic_bool *cancel) { return cancel && atomic_load(cancel); }

/* Poll a listening/connecting socket in ~250 ms slices so `cancel` aborts a blocking
   accept/connect promptly (disconnect must never block the UI thread indefinitely). */
sb_transport *sb_transport_tcp_listen(int port, atomic_bool *cancel) {
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) return NULL;
    int one = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((uint16_t)port);
    if (bind(ls, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(ls, 1) < 0) { close(ls); return NULL; }
    fcntl(ls, F_SETFL, fcntl(ls, F_GETFL, 0) | O_NONBLOCK);
    int fd = -1;
    while (!cancelled(cancel)) {
        struct pollfd pfd = { .fd = ls, .events = POLLIN };
        int pr = poll(&pfd, 1, 250);
        if (pr < 0 && errno == EINTR) continue;
        if (pr < 0) break;
        if (pr == 0) continue;                 /* timeout: re-check cancel */
        fd = accept(ls, NULL, NULL);
        if (fd >= 0) break;
        if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
        break;
    }
    close(ls);
    if (fd < 0) return NULL;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);   /* blocking for send/recv */
    return tcp_wrap(fd);
}
sb_transport *sb_transport_tcp_connect(const char *host, int port, atomic_bool *cancel) {
    char portstr[16]; snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST;   /* UI collects an IP; a DNS lookup is uncancellable and disconnect joins this thread */
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return NULL;
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return NULL; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    int rc = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    bool ok = false;
    if (rc == 0) ok = true;
    else if (errno == EINPROGRESS) {
        while (!cancelled(cancel)) {
            struct pollfd pfd = { .fd = fd, .events = POLLOUT };
            int pr = poll(&pfd, 1, 250);
            if (pr < 0 && errno == EINTR) continue;
            if (pr < 0) break;
            if (pr == 0) continue;             /* still connecting: re-check cancel */
            int soerr = 0; socklen_t l = sizeof(soerr);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &l);
            ok = (soerr == 0);
            break;
        }
    }
    if (!ok) { close(fd); return NULL; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);   /* blocking for send/recv */
    return tcp_wrap(fd);
}

/* ---------- sb_link: byte-level master/slave serial bridge ---------- */
#define SB_LINK_TIMEOUT_MS 1500   /* per-byte master wait; peer gone → 0xFF, no hang */
#define SB_LINK_DEAD_AFTER 4      /* consecutive failed exchanges before latching dead (~6 s) */

struct sb_link {
    sb_transport *t;
    uint8_t in_byte;   /* peer byte for the current master transfer */
    int     bits;      /* master bit_end index 0..7; 0 = start of a fresh byte */
    int     fails;     /* consecutive master exchange failures */
    bool    dead;      /* peer gone → treat as an unplugged cable (instant 0xFF) */
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
bool sb_link_is_dead(sb_link *s) { return s && s->dead; }

/* master: fired per bit. A fresh byte is precisely serial_count==0 at bit_start: memory.c's
   SC-write path resets serial_count to 0 then fires; timing.c only fires mid-byte with
   serial_count 1..7. Using serial_count (not our own shadow) self-heals a mid-byte SC
   rewrite (Pokémon handshake retry) or a mid-transfer attach that would otherwise wedge
   the shadow counter. On the fresh bit, SB holds the whole outgoing byte — exchange it. */
void sb_link_bit_start(sb_link *s, GB_gameboy_t *gb, bool bit) {
    (void)bit;
    if (!s) return;
    if (gb->serial_count == 0) s->bits = 0;      /* fresh byte (SC write) — resync */
    else if (s->bits != 0) return;               /* mid-byte continuation — no-op */
    if (s->dead) { s->in_byte = 0xFF; return; }  /* unplugged: instant, no per-byte stall */
    /* Strict ping-pong: the RX queue must be empty at the start of a byte. Anything present
       is a stale reply to a previously timed-out exchange — drain it to avoid a permanent
       off-by-one desync. */
    uint8_t junk; while (s->t->recv(s->t, &junk, 0)) {}
    uint8_t out = GB_safe_read_memory(gb, 0xFF01);   /* SB */
    if (!s->t->send(s->t, out) || !s->t->recv(s->t, &s->in_byte, SB_LINK_TIMEOUT_MS)) {
        s->in_byte = 0xFF;   /* peer gone / timeout: link idle-high */
        /* One timeout is normal — the peer may be parked in its menu, or still in a
           pre-link screen. Latch only after several in a row (a closed socket fails fast,
           so a real disconnect still latches within a frame). */
        if (++s->fails >= SB_LINK_DEAD_AFTER) s->dead = true;
    }
    else s->fails = 0;
}
/* master: return the peer byte MSB-first over 8 calls; wraps bits back to 0 after the 8th. */
bool sb_link_bit_end(sb_link *s, GB_gameboy_t *gb) {
    (void)gb;
    if (!s) return true;
    bool r = (s->in_byte >> (7 - s->bits)) & 1;
    s->bits = (s->bits + 1) & 7;
    return r;
}
/* slave: per-frame on the emu thread. Answer every master byte, armed or not, exactly like
   the Core's own external-clock path: a disabled serial port yields 0 bits and ignores the
   clocked-in data (GB_serial_set_data_bit is a no-op then), and an internally-clocked port
   reads as 1s. Leaving an unarmed master byte unanswered would stall the master for the
   full timeout every time one side reaches link mode first (Tetris 2P, Cable Club). */
void sb_link_slave_poll(sb_link *s, GB_gameboy_t *gb) {
    if (!s || s->dead) return;
    uint8_t m;
    if (!s->t->recv(s->t, &m, 0)) return;            /* non-blocking; nothing yet */
    uint8_t sc = GB_safe_read_memory(gb, 0xFF02);
    uint8_t out = !(sc & 0x80) ? 0x00 : (sc & 1) ? 0xFF : GB_safe_read_memory(gb, 0xFF01);
    if (!s->t->send(s->t, out)) { s->dead = true; return; }
    for (int i = 0; i < 8; i++) GB_serial_set_data_bit(gb, (m >> (7 - i)) & 1);
}

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <Core/gb.h>     /* GB_gameboy_t */
#include "emulator.h"    /* sb_emulator */

/* --- Transport: a reliable ordered byte pipe (TCP now; Bluetooth later; loopback in tests) --- */
typedef struct sb_transport sb_transport;
struct sb_transport {
    bool (*send)(sb_transport *t, uint8_t byte);              /* true on success */
    bool (*recv)(sb_transport *t, uint8_t *out, int timeout_ms); /* 0 = non-blocking; false on timeout/closed */
    void (*close)(sb_transport *t);   /* close once; frees t. Caller ensures no thread is in send/recv on t. */
};
/* cancel (nullable): set true to abort a blocking listen/connect within ~250 ms. */
sb_transport *sb_transport_tcp_listen(int port, atomic_bool *cancel);              /* blocks for one peer; NULL on error/cancel */
sb_transport *sb_transport_tcp_connect(const char *host, int port, atomic_bool *cancel); /* blocks; NULL on error/cancel */
sb_transport *sb_transport_loopback_pair(sb_transport **other);  /* test: cross-wired ends of one FIFO */

/* --- Link bridge (Task 2) --- */
typedef struct sb_link sb_link;
sb_link *sb_link_create(sb_transport *t);   /* takes ownership of t */
void     sb_link_destroy(sb_link *s);       /* closes+frees transport */
void     sb_link_bit_start(sb_link *s, GB_gameboy_t *gb, bool bit);  /* master: 1st bit → exchange */
bool     sb_link_bit_end(sb_link *s, GB_gameboy_t *gb);              /* master: feed peer byte bit */
void     sb_link_slave_poll(sb_link *s, GB_gameboy_t *gb);           /* slave: per-frame, emu thread */
bool     sb_link_is_dead(sb_link *s);                               /* transport failed → link should be torn down */

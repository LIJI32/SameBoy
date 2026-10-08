#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct sb_ring sb_ring;

sb_ring *sb_ring_create(size_t capacity_frames);
/* Precondition: call sb_ring_shutdown() and join the producer thread before
 * destroying; destroying while a producer is blocked in sb_ring_push() is
 * undefined. */
void     sb_ring_destroy(sb_ring *r);
void     sb_ring_push(sb_ring *r, int16_t left, int16_t right);
/* Non-blocking push: returns 1 if stored, 0 if dropped (full or shut down). */
int      sb_ring_try_push(sb_ring *r, int16_t left, int16_t right);
size_t   sb_ring_pop(sb_ring *r, int16_t *dst_interleaved, size_t frames);
void     sb_ring_flush(sb_ring *r);
void     sb_ring_shutdown(sb_ring *r);

#pragma once
#include "ring_buffer.h"
#include <stdatomic.h>

typedef struct sb_audio sb_audio;

/* dead: set true (and the ring flushed, unblocking a stuck producer) from
   AAudio's error callback when the stream disconnects; the owner must then
   sb_audio_stop() the dead stream and sb_audio_start() a fresh one.
   sb_audio_start clears *dead before opening. */
sb_audio *sb_audio_start(sb_ring *ring, atomic_bool *dead);
void      sb_audio_set_paused(sb_audio *a, int paused);
void      sb_audio_stop(sb_audio *a);

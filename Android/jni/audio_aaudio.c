#include "audio_aaudio.h"
#include "emulator.h"     /* SB_AUDIO_SAMPLE_RATE */
#include <aaudio/AAudio.h>
#include <stdlib.h>

struct sb_audio {
    AAudioStream *stream;
    sb_ring *ring;
    atomic_bool *dead;
};

static aaudio_data_callback_result_t data_cb(AAudioStream *stream, void *user,
                                             void *audio_data, int32_t num_frames)
{
    (void)stream;
    struct sb_audio *a = user;
    sb_ring_pop(a->ring, (int16_t *)audio_data, (size_t)num_frames);  /* zero-fills underrun */
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

/* Device disconnect (headphone unplug, BT route change): AAudio stops firing
   data_cb, so the ring would fill and sb_ring_push would block the emu thread
   forever. Mark the stream dead and flush the ring to unblock the producer;
   the emu thread reopens the stream. Must not stop/close from this thread. */
static void error_cb(AAudioStream *stream, void *user, aaudio_result_t error)
{
    (void)stream; (void)error;
    struct sb_audio *a = user;
    atomic_store(a->dead, true);
    sb_ring_flush(a->ring);
}

sb_audio *sb_audio_start(sb_ring *ring, atomic_bool *dead)
{
    AAudioStreamBuilder *b = NULL;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return NULL;

    struct sb_audio *a = calloc(1, sizeof(*a));
    if (!a) { AAudioStreamBuilder_delete(b); return NULL; }
    a->ring = ring;
    a->dead = dead;
    atomic_store(dead, false);

    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, SB_AUDIO_SAMPLE_RATE);
    AAudioStreamBuilder_setDataCallback(b, data_cb, a);
    AAudioStreamBuilder_setErrorCallback(b, error_cb, a);

    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &a->stream);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) { free(a); return NULL; }

    if (AAudioStream_requestStart(a->stream) != AAUDIO_OK) {
        AAudioStream_close(a->stream);
        free(a);
        return NULL;
    }
    return a;
}

void sb_audio_set_paused(sb_audio *a, int paused)
{
    if (!a || !a->stream) return;
    if (paused) AAudioStream_requestPause(a->stream);
    else        AAudioStream_requestStart(a->stream);
}

void sb_audio_stop(sb_audio *a)
{
    if (!a) return;
    if (a->stream) {
        AAudioStream_requestStop(a->stream);
        /* On API 26-29 close() can race a mid-flight data callback, which would
           touch `a` after free(). Wait for the stream to leave STOPPING first. */
        aaudio_stream_state_t state = AAUDIO_STREAM_STATE_STOPPING;
        AAudioStream_waitForStateChange(a->stream, AAUDIO_STREAM_STATE_STOPPING,
                                        &state, 300 * 1000000LL /* 300 ms */);
        AAudioStream_close(a->stream);
    }
    free(a);
}

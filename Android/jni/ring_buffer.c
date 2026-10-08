#include "ring_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct sb_ring {
    int16_t *buf;          /* interleaved L,R */
    size_t   cap;          /* frames */
    size_t   head, tail;   /* frame indices */
    size_t   count;        /* frames stored */
    pthread_mutex_t mtx;
    pthread_cond_t  not_full;
    int shutdown;
};

sb_ring *sb_ring_create(size_t capacity_frames)
{
    if (capacity_frames == 0) return NULL;
    sb_ring *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->cap = capacity_frames;
    r->buf = calloc(capacity_frames * 2, sizeof(int16_t));
    if (!r->buf) { free(r); return NULL; }
    pthread_mutex_init(&r->mtx, NULL);
    pthread_cond_init(&r->not_full, NULL);
    return r;
}

void sb_ring_destroy(sb_ring *r)
{
    if (!r) return;
    pthread_mutex_destroy(&r->mtx);
    pthread_cond_destroy(&r->not_full);
    free(r->buf);
    free(r);
}

void sb_ring_push(sb_ring *r, int16_t left, int16_t right)
{
    pthread_mutex_lock(&r->mtx);
    while (r->count == r->cap && !r->shutdown) {
        pthread_cond_wait(&r->not_full, &r->mtx);
    }
    if (r->shutdown) { pthread_mutex_unlock(&r->mtx); return; }
    r->buf[r->head * 2]     = left;
    r->buf[r->head * 2 + 1] = right;
    r->head = (r->head + 1) % r->cap;
    r->count++;
    pthread_mutex_unlock(&r->mtx);
}

int sb_ring_try_push(sb_ring *r, int16_t left, int16_t right)
{
    pthread_mutex_lock(&r->mtx);
    if (r->count == r->cap || r->shutdown) {
        pthread_mutex_unlock(&r->mtx);
        return 0;
    }
    r->buf[r->head * 2]     = left;
    r->buf[r->head * 2 + 1] = right;
    r->head = (r->head + 1) % r->cap;
    r->count++;
    pthread_mutex_unlock(&r->mtx);
    return 1;
}

size_t sb_ring_pop(sb_ring *r, int16_t *dst, size_t frames)
{
    pthread_mutex_lock(&r->mtx);
    size_t got = 0;
    while (got < frames && r->count > 0) {
        dst[got * 2]     = r->buf[r->tail * 2];
        dst[got * 2 + 1] = r->buf[r->tail * 2 + 1];
        r->tail = (r->tail + 1) % r->cap;
        r->count--;
        got++;
    }
    if (got > 0) pthread_cond_signal(&r->not_full);
    pthread_mutex_unlock(&r->mtx);
    /* zero-fill underrun */
    for (size_t i = got; i < frames; i++) {
        dst[i * 2] = 0;
        dst[i * 2 + 1] = 0;
    }
    return got;
}

void sb_ring_flush(sb_ring *r)
{
    pthread_mutex_lock(&r->mtx);
    r->head = r->tail = r->count = 0;
    pthread_cond_signal(&r->not_full);
    pthread_mutex_unlock(&r->mtx);
}

void sb_ring_shutdown(sb_ring *r)
{
    pthread_mutex_lock(&r->mtx);
    r->shutdown = 1;
    pthread_cond_broadcast(&r->not_full);
    pthread_mutex_unlock(&r->mtx);
}

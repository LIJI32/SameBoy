#include "../ring_buffer.h"
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static void test_push_pop_fifo(void)
{
    sb_ring *r = sb_ring_create(8);
    sb_ring_push(r, 100, -100);
    sb_ring_push(r, 200, -200);
    int16_t out[4] = {0};
    size_t n = sb_ring_pop(r, out, 2);
    assert(n == 2);
    assert(out[0] == 100 && out[1] == -100);
    assert(out[2] == 200 && out[3] == -200);
    sb_ring_destroy(r);
}

static void test_underrun_zero_fill(void)
{
    sb_ring *r = sb_ring_create(8);
    sb_ring_push(r, 7, 7);
    int16_t out[6];
    for (int i = 0; i < 6; i++) out[i] = 0x5a;
    size_t n = sb_ring_pop(r, out, 3);   // only 1 frame available
    assert(n == 1);
    assert(out[0] == 7 && out[1] == 7);
    assert(out[2] == 0 && out[3] == 0 && out[4] == 0 && out[5] == 0); // zero-filled
    sb_ring_destroy(r);
}

struct pc { sb_ring *r; };
static void *producer(void *arg)
{
    sb_ring *r = ((struct pc *)arg)->r;
    for (int i = 0; i < 10000; i++) sb_ring_push(r, (int16_t)i, (int16_t)-i);
    return NULL;
}

static void test_blocking_pacing(void)
{
    sb_ring *r = sb_ring_create(64);   // small: producer must block
    struct pc arg = { r };
    pthread_t t;
    pthread_create(&t, NULL, producer, &arg);
    int16_t out[2];
    for (int i = 0; i < 10000; i++) {
        while (sb_ring_pop(r, out, 1) == 0) {}
        assert(out[0] == (int16_t)i);
    }
    pthread_join(t, NULL);
    sb_ring_destroy(r);
}

static volatile int shutdown_producer_done;
static void *shutdown_producer(void *arg)
{
    sb_ring *r = arg;
    for (int i = 0; i < 5; i++) sb_ring_push(r, (int16_t)i, (int16_t)i);
    shutdown_producer_done = 1;
    return NULL;
}

static void test_shutdown_unblocks_producer(void)
{
    sb_ring *r = sb_ring_create(2);
    pthread_t t;
    pthread_create(&t, NULL, shutdown_producer, r);
    usleep(100 * 1000);              /* let producer fill and block on 3rd push */
    sb_ring_shutdown(r);
    usleep(100 * 1000);
    assert(shutdown_producer_done == 1); /* clean failure instead of a join hang */
    pthread_join(t, NULL);
    sb_ring_destroy(r);
}

static void test_flush_empties(void)
{
    sb_ring *r = sb_ring_create(8);
    sb_ring_push(r, 1, 1);
    sb_ring_push(r, 2, 2);
    sb_ring_push(r, 3, 3);
    sb_ring_flush(r);
    int16_t out[2] = {0x5a, 0x5a};
    assert(sb_ring_pop(r, out, 1) == 0);       /* empty after flush */
    sb_ring_push(r, 42, -42);                  /* ring still works */
    assert(sb_ring_pop(r, out, 1) == 1);
    assert(out[0] == 42 && out[1] == -42);
    sb_ring_destroy(r);
}

static void test_try_push_drop_on_full(void)
{
    sb_ring *r = sb_ring_create(2);
    assert(sb_ring_try_push(r, 1, -1) == 1);
    assert(sb_ring_try_push(r, 2, -2) == 1);
    assert(sb_ring_try_push(r, 3, -3) == 0);   /* full: dropped, must not block */
    int16_t out[2];
    assert(sb_ring_pop(r, out, 1) == 1);
    assert(out[0] == 1 && out[1] == -1);       /* FIFO intact, drop lost only the new frame */
    assert(sb_ring_try_push(r, 4, -4) == 1);   /* space again */
    assert(sb_ring_pop(r, out, 1) == 1 && out[0] == 2);
    assert(sb_ring_pop(r, out, 1) == 1 && out[0] == 4);
    sb_ring_destroy(r);
}

int main(void)
{
    test_push_pop_fifo();
    test_underrun_zero_fill();
    test_blocking_pacing();
    test_shutdown_unblocks_producer();
    test_flush_empties();
    test_try_push_drop_on_full();
    printf("ring_buffer: all tests passed\n");
    return 0;
}

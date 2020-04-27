#include "audio.h"
#include <portaudio.h>
#include <pthread.h>


#define AUDIO_FREQUENCY 96000
static PaStream *stream;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static size_t buffer_position = 0, buffer_size = 0, buffer_needed = 0;
static GB_sample_t *buffer = NULL;
static bool playing = false;


bool GB_audio_is_playing(void)
{
    return playing;
}

void GB_audio_set_paused(bool paused)
{
    if (!stream) return;
    
    if (paused) {
        Pa_Sleep(1000);
        pthread_mutex_lock(&lock);
        playing = false;
        GB_audio_clear_queue();
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&lock);
        Pa_StopStream(stream);
    }
    else {
        Pa_StartStream(stream);
        playing = true;
    }
}

void GB_audio_clear_queue(void)
{
    buffer_position = 0;
}

unsigned GB_audio_get_frequency(void)
{
    return stream? Pa_GetStreamInfo(stream)->sampleRate : 0;
}

size_t GB_audio_get_queue_length(void)
{
    return buffer_position;
}

void GB_audio_queue_sample(GB_sample_t *sample)
{
    if (!stream) return;
    
    pthread_mutex_lock(&lock);
    if (!GB_audio_is_playing()) {
        pthread_mutex_unlock(&lock);
        return;
    }
    
    if (buffer_position == buffer_size) {
        if (buffer_size >= 0x4000) {
            buffer_position = 0;
            pthread_mutex_unlock(&lock);
            return;
        }
        
        if (buffer_size == 0) {
            buffer_size = 512;
        }
        else {
            buffer_size += buffer_size >> 2;
        }
        buffer = realloc(buffer, sizeof(*sample) * buffer_size);
    }
    buffer[buffer_position++] = *sample;
    if (buffer_position == buffer_needed) {
        pthread_cond_signal(&cond);
        buffer_needed = 0;
    }
    pthread_mutex_unlock(&lock);
}

static int callback(const void *in, void *_out,
                    unsigned long frames,
                    const PaStreamCallbackTimeInfo *time_info,
                    PaStreamCallbackFlags status_flags,
                    void *unused)
{
    GB_sample_t *out = (GB_sample_t *)_out;
    pthread_mutex_lock(&lock);

    if (buffer_position < frames) {
        buffer_needed = frames;
        pthread_cond_wait(&cond, &lock);
    }
    
    if (!playing) {
        memset(out, 0, frames * sizeof(*out));
        pthread_mutex_unlock(&lock);
        return 0;
    }
    
    if (buffer_position >= frames && buffer_position < frames + 4800) {
        memcpy(out, buffer, frames * sizeof(*buffer));
        memmove(buffer, buffer + frames, (buffer_position - frames) * sizeof(*buffer));
        buffer_position -= frames;
    }
    else {
        memcpy(out, buffer + (buffer_position - frames), frames * sizeof(*buffer));
        buffer_position = 0;
    }
    pthread_mutex_unlock(&lock);

    return 0;
}

void GB_audio_init(void)
{
    Pa_Initialize();
    Pa_OpenDefaultStream(&stream,
                         0,
                         2,
                         paInt16,
                         AUDIO_FREQUENCY,
                         paFramesPerBufferUnspecified,
                         callback,
                         NULL);
    
}

#include "audio.h"
#include <portaudio.h>


#define AUDIO_FREQUENCY 96000
static PaStream *stream;
static size_t buffer_position = 0;
static GB_sample_t buffer[4096];
static bool blocking_mode = false;

bool GB_audio_set_sync_mode(bool sync_mode)
{
    blocking_mode = sync_mode;
    return true;
}

bool GB_audio_is_playing(void)
{
    return stream && Pa_IsStreamActive(stream);
}

void GB_audio_set_paused(bool paused)
{
    if (!stream) return;
    
    if (paused) {
        Pa_StopStream(stream);
    }
    else {
        Pa_StartStream(stream);
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
    
    if (buffer_position < sizeof(buffer) / sizeof(buffer[0])) {
        buffer[buffer_position++] = *sample;
        if (blocking_mode) {
            return;
        }
    }
    else {
        if (blocking_mode) {
            Pa_WriteStream(stream, buffer, sizeof(buffer) / sizeof(buffer[0]));
            buffer_position = 0;
            return;
        }
    }
    
    
    size_t write_legnth = Pa_GetStreamWriteAvailable(stream);
    
    if (write_legnth > buffer_position) {
        write_legnth = buffer_position;
    }
    
    if (write_legnth) {
        Pa_WriteStream(stream, buffer, write_legnth);
        memmove(buffer, buffer + write_legnth, sizeof(buffer[0]) * (buffer_position - write_legnth));
        buffer_position -= write_legnth;
    }
}

void GB_audio_init(void)
{
    Pa_Initialize();
    Pa_OpenDefaultStream(&stream,
                         0,
                         2,
                         paInt16,
                         AUDIO_FREQUENCY,
                         sizeof(buffer) / sizeof(buffer[0]),
                         NULL,
                         NULL);
    
}

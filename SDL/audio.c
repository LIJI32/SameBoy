#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <Core/gb.h>
#include "audio/audio.h"
#include "configuration.h"

#define likely(x)   GB_likely(x)
#define unlikely(x) GB_unlikely(x)

static const GB_audio_driver_t *driver = NULL;

bool GB_audio_init(void)
{
    const GB_audio_driver_t *drivers[] = {
#ifdef _WIN32
        GB_AUDIO_DRIVER_REF(XAudio2),
#endif
        GB_AUDIO_DRIVER_REF(SDL),
#ifdef ENABLE_OPENAL
        GB_AUDIO_DRIVER_REF(OpenAL),
#endif
    };
    
    // First try the preferred driver
    for (unsigned i = 0; i < sizeof(drivers) / sizeof(drivers[0]); i++) {
        driver = drivers[i];
        if (strcmp(driver->name, configuration.audio_driver) != 0) {
            continue;
        }
        if (driver->audio_init()) {
            if (driver->audio_deinit) {
                atexit(driver->audio_deinit);
            }
            return true;
        }
    }
    
    // Else go by priority
    for (unsigned i = 0; i < sizeof(drivers) / sizeof(drivers[0]); i++) {
        driver = drivers[i];
        if (driver->audio_init()) {
            atexit(driver->audio_deinit);
            return true;
        }
    }
    
    driver = NULL;
    return false;
}

bool GB_audio_is_playing(void)
{
    if (unlikely(!driver)) return false;
    return driver->audio_is_playing();
}

void GB_audio_set_paused(bool paused)
{
    if (unlikely(!driver)) return;
    return driver->audio_set_paused(paused);
}

void GB_audio_clear_queue(void)
{
    if (unlikely(!driver)) return;
    return driver->audio_clear_queue();
}

unsigned GB_audio_get_frequency(void)
{
    if (unlikely(!driver)) return 0;
    return driver->audio_get_frequency();
}

size_t GB_audio_get_queue_length(void)
{
    if (unlikely(!driver)) return 0;
    return driver->audio_get_queue_length();
}

void GB_audio_queue_sample(GB_sample_t *sample)
{
    if (unlikely(!driver)) return;
    return driver->audio_queue_sample(sample);
}

const char *GB_audio_driver_name(void)
{
    if (unlikely(!driver)) return "None";
    return driver->name;
}

const char *GB_audio_driver_name_at_index(unsigned index)
{
    const GB_audio_driver_t *drivers[] = {
#ifdef _WIN32
        GB_AUDIO_DRIVER_REF(XAudio2),
#endif
        GB_AUDIO_DRIVER_REF(SDL),
#ifdef ENABLE_OPENAL
        GB_AUDIO_DRIVER_REF(OpenAL),
#endif
    };
    if (index >= sizeof(drivers) / sizeof(drivers[0])) {
        return "";
    }
    return drivers[index]->name;
}

void GB_audio_deinit(void)
{
    if (unlikely(!driver)) return;
    if (driver->audio_deinit) {
        driver->audio_deinit();
    }
    driver = NULL;
}

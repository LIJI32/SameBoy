#ifndef apu_h
#define apu_h
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "gb_struct_def.h"


#ifdef GB_INTERNAL
/* Speed = 1 / Length (in seconds) */
#define DAC_DECAY_SPEED 500.0

/* Divides nicely and never overflows with 4 channels and 8 (1-8) volume levels */
#ifdef WIIU
/* Todo: Remove this hack once https://github.com/libretro/RetroArch/issues/6252 is fixed*/
#define MAX_CH_AMP (0xFF0 / 2)
#else
#define MAX_CH_AMP 0xFF0
#endif
#define CH_STEP (MAX_CH_AMP/0xF/8)
#endif



/* APU ticks are 2MHz, triggered by an internal APU clock. */

typedef struct
{
    int16_t left;
    int16_t right;
} GB_sample_t;

typedef struct
{
    double left;
    double right;
} GB_double_sample_t;

enum GB_CHANNELS {
    GB_SQUARE_1,
    GB_SQUARE_2,
    GB_WAVE,
    GB_NOISE,
    GB_N_CHANNELS
};

typedef struct
{
    bool global_enable;
    uint8_t apu_cycles;
    
    uint8_t samples[GB_N_CHANNELS];
    bool is_active[GB_N_CHANNELS];
    
    uint8_t div_divider; // The DIV register ticks the APU at 512Hz, but is then divided
                         // once more to generate 128Hz and 64Hz clocks
    
    uint8_t lf_div; // The APU runs in 2MHz, but channels 1, 2 and 4 run in 1MHZ so we divide
                    // need to divide the signal.
    
    uint8_t square_sweep_countdown; // In 128Hz
    uint8_t square_sweep_calculate_countdown; // In 2 MHz
    uint16_t new_sweep_sample_legnth;
    uint16_t shadow_sweep_sample_legnth;
    bool sweep_enabled;
    bool sweep_decreasing;
    
    struct {
        uint16_t pulse_length; // Reloaded from NRX1 (xorred), in 256Hz DIV ticks
        uint8_t current_volume; // Reloaded from NRX2
        uint8_t volume_countdown; // Reloaded from NRX2
        uint8_t current_sample_index;
        
        uint16_t sample_countdown; // in APU ticks (Reloaded from sample_length, xorred $7FF)
        uint16_t sample_length; // From NRX3, NRX4, in APU ticks
        bool length_enabled; // NRX4

    } square_channels[2];
    
    struct {
        bool enable; // NR30
        uint16_t pulse_length; // Reloaded from NR31 (xorred), in 256Hz DIV ticks
        uint8_t shift; // NR32
        uint16_t sample_length; // NR33, NR34, in APU ticks
        bool length_enabled; // NR34
        
        uint16_t sample_countdown; // in APU ticks (Reloaded from sample_length, xorred $7FF)
        uint8_t current_sample_index;
        uint8_t current_sample; // Current sample before shifting.
        
        int8_t wave_form[32];
        bool wave_form_just_read;
    } wave_channel;
    
    struct {
        uint16_t pulse_length; // Reloaded from NR41 (xorred), in 256Hz DIV ticks
        uint8_t current_volume; // Reloaded from NR42
        uint8_t volume_countdown; // Reloaded from NR42
        uint16_t lfsr;
        bool narrow;
        
        uint16_t sample_countdown; // in APU ticks (Reloaded from sample_length)
        uint16_t sample_length; // From NR43, in APU ticks
        bool length_enabled; // NR44
        
        uint8_t alignment; // If (NR43 & 7) != 0, samples are aligned to 512KHz clock instead of
                           // 1MHz. This variable keeps track of the alignment.
        
    } noise_channel;
    
    bool skip_div_event;
    
} GB_apu_t;

typedef enum {
    GB_HIGHPASS_OFF, // Do not apply any filter, keep DC offset
    GB_HIGHPASS_ACCURATE, // Apply a highpass filter similar to the one used on hardware
    GB_HIGHPASS_REMOVE_DC_OFFSET, // Remove DC Offset without affecting the waveform
    GB_HIGHPASS_MAX
} GB_highpass_mode_t;

typedef struct {
    unsigned sample_rate;
    
    GB_sample_t *buffer;
    size_t buffer_size;
    size_t buffer_position;
    
    bool stream_started; /* detects first copy request to minimize lag */
    volatile bool copy_in_progress;
    volatile bool lock;
    
    double sample_cycles; // In 8 MHz units
    
    // Samples are NOT normalized to MAX_CH_AMP * 4 at this stage!
    unsigned cycles_since_render;
    unsigned last_update[GB_N_CHANNELS];
    GB_sample_t current_sample[GB_N_CHANNELS];
    GB_sample_t summed_samples[GB_N_CHANNELS];
    double dac_discharge[GB_N_CHANNELS];
    
    GB_highpass_mode_t highpass_mode;
    double highpass_rate;
    GB_double_sample_t highpass_diff;
} GB_apu_output_t;

void GB_set_sample_rate(GB_gameboy_t *gb, unsigned int sample_rate);
void GB_apu_copy_buffer(GB_gameboy_t *gb, GB_sample_t *dest, size_t count);
size_t GB_apu_get_current_buffer_length(GB_gameboy_t *gb);
void GB_set_highpass_filter_mode(GB_gameboy_t *gb, GB_highpass_mode_t mode);

#ifdef GB_INTERNAL
void GB_apu_write(GB_gameboy_t *gb, uint8_t reg, uint8_t value);
uint8_t GB_apu_read(GB_gameboy_t *gb, uint8_t reg);
void GB_apu_div_event(GB_gameboy_t *gb);
void GB_apu_init(GB_gameboy_t *gb);
void GB_apu_run(GB_gameboy_t *gb);
#endif

#endif /* apu_h */

#include <stdint.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include "gb.h"

/* Band limited synthesis based on: http://www.slack.net/~ant/bl-synth/ */
static int32_t band_limited_steps[GB_BAND_LIMITED_PHASES][GB_BAND_LIMITED_WIDTH];

static void __attribute__((constructor)) band_limited_init(void)
{
    const unsigned master_size = GB_BAND_LIMITED_WIDTH * GB_BAND_LIMITED_PHASES;
    double *master = malloc(master_size  * sizeof(*master));
    
    const unsigned sine_size = 256 * GB_BAND_LIMITED_PHASES + 2;
    const unsigned max_harmonic = sine_size / 2 / GB_BAND_LIMITED_PHASES;
    nounroll for (unsigned harmonic = 1; harmonic <= max_harmonic; harmonic += 2) {
        double amplitude = 1.0 / harmonic / 2;
        double to_angle = M_PI * 2 / sine_size * harmonic;
        nounroll for (unsigned i = 0; i < master_size; i++) {
            master[i] += sin(((signed)(i + 1) - (signed)master_size / 2) * to_angle) * amplitude;
        }
    }
    
    // Normalize master waveform
    nounroll for (unsigned i = 0; i < master_size - 1; i++) {
        master[i] += master[master_size - 1];
        master[i] /= master[master_size - 1] * 2;
    }
    master[master_size - 1] = 1;
    
    nounroll for (unsigned phase = 0; phase < GB_BAND_LIMITED_PHASES; phase++) {
        int32_t error = GB_BAND_LIMITED_ONE;
        int32_t prev = 0;
        nounroll for (unsigned i = 0; i < GB_BAND_LIMITED_WIDTH; i++) {
            int32_t cur = master[(GB_BAND_LIMITED_PHASES - 1 - phase) + i * GB_BAND_LIMITED_PHASES] * GB_BAND_LIMITED_ONE;
            int32_t delta = cur - prev;
            error = error - delta;
            prev = cur;
            band_limited_steps[phase][i] = delta;
        }
        
        // Make sure the deltas sum to 1.0
        band_limited_steps[phase][GB_BAND_LIMITED_WIDTH / 2 - 1] += error / 2;
        band_limited_steps[phase][0] += error - (error / 2);
    }
    free(master);
}

static void band_limited_update(GB_band_limited_t *band_limited, const GB_sample_t *input, unsigned phase)
{
    if (input->packed == band_limited->input.packed) return;
    unsigned delay = phase / GB_BAND_LIMITED_PHASES;
    phase = phase & (GB_BAND_LIMITED_PHASES - 1);
    
    GB_sample_t delta = {
        .left = input->left - band_limited->input.left,
        .right = input->right - band_limited->input.right,
    };
    band_limited->input.packed = input->packed;
    
    for (unsigned i = 0; i < GB_BAND_LIMITED_WIDTH; i++) {
        unsigned offset = (i + band_limited->pos + delay) & (sizeof(band_limited->buffer) / sizeof(band_limited->buffer[0]) - 1);
        band_limited->buffer[offset].left += delta.left * band_limited_steps[phase][i];
        band_limited->buffer[offset].right += delta.right * band_limited_steps[phase][i];
    }
}

static void band_limited_update_unfiltered(GB_band_limited_t *band_limited, const GB_sample_t *input, unsigned delay)
{
    if (input->packed == band_limited->input.packed) return;
    
    GB_sample_t delta = {
        .left = input->left - band_limited->input.left,
        .right = input->right - band_limited->input.right,
    };
    band_limited->input.packed = input->packed;
    
    unsigned offset = (band_limited->pos + delay) & (sizeof(band_limited->buffer) / sizeof(band_limited->buffer[0]) - 1);
    band_limited->buffer[offset].left += delta.left * GB_BAND_LIMITED_ONE;
    band_limited->buffer[offset].right += delta.right * GB_BAND_LIMITED_ONE;
}

static void band_limited_read(GB_band_limited_t *band_limited, GB_sample_t *output, uint32_t multiplier)
{
    band_limited->output.left += band_limited->buffer[band_limited->pos].left;
    band_limited->output.right += band_limited->buffer[band_limited->pos].right;
    
    band_limited->buffer[band_limited->pos].left = band_limited->buffer[band_limited->pos].right = 0;
    band_limited->pos = (band_limited->pos + 1) & (sizeof(band_limited->buffer) / sizeof(band_limited->buffer[0]) - 1);
    
    output->left = band_limited->output.left * multiplier / GB_BAND_LIMITED_ONE;
    output->right = band_limited->output.right * multiplier / GB_BAND_LIMITED_ONE;
}

static const uint8_t duties[] = {
    0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 1, 1, 1,
    0, 1, 1, 1, 1, 1, 1, 0,
};

bool GB_apu_is_DAC_enabled(GB_gameboy_t *gb, GB_channel_t index)
{
    if (gb->model > GB_MODEL_CGB_E) {
        /* On the AGB, mixing is done digitally, so there are no per-channel
           DACs. Instead, all channels are summed digital regardless of
           whatever the DAC state would be on a CGB or earlier model. */
        return true;
    }
    
    switch (index) {
        case GB_SQUARE_1:
            return gb->io_registers[GB_IO_NR12] & 0xF8;

        case GB_SQUARE_2:
            return gb->io_registers[GB_IO_NR22] & 0xF8;

        case GB_WAVE:
            return gb->apu.wave_channel.enable;

        case GB_NOISE:
            return gb->io_registers[GB_IO_NR42] & 0xF8;
            
        nodefault;
    }

    return false;
}

static uint8_t agb_bias_for_channel(GB_gameboy_t *gb, GB_channel_t index)
{
    if (!gb->apu.is_active[index]) return 0;
    
    switch (index) {
        case GB_SQUARE_1:
            return gb->apu.square_channels[GB_SQUARE_1].current_volume;
        case GB_SQUARE_2:
            return gb->apu.square_channels[GB_SQUARE_2].current_volume;
        case GB_WAVE:
            return 0;
        case GB_NOISE:
            return gb->apu.noise_channel.current_volume;
            
        nodefault;
    }
}

static void update_sample(GB_gameboy_t *gb, GB_channel_t index, int8_t value, unsigned cycles_offset)
{
    if (gb->model > GB_MODEL_CGB_E) {
        /* On the AGB, because no analog mixing is done, the behavior of NR51 is a bit different.
           A channel that is not connected to a terminal is idenitcal to a connected channel
           playing PCM sample 0. */
        gb->apu.samples[index] = value;
        
        if (gb->apu_output.sample_rate) {
            unsigned right_volume = (gb->io_registers[GB_IO_NR50] & 7) + 1;
            unsigned left_volume = ((gb->io_registers[GB_IO_NR50] >> 4) & 7) + 1;
            int8_t silence = 0;
            if (index == GB_WAVE) {
                /* For some reason, channel 3 is inverted on the AGB, and has a different "silence" value */
                value ^= 0xF;
                silence = 7;
            }
            
            uint8_t bias = agb_bias_for_channel(gb, index);
            
            bool left = gb->io_registers[GB_IO_NR51] & (0x10 << index);
            bool right = gb->io_registers[GB_IO_NR51] & (1 << index);
            
            GB_sample_t output = {
                .left = (0xF - (left? value : silence) * 2 + bias) * left_volume,
                .right = (0xF - (right? value : silence) * 2 + bias) * right_volume
            };
            
            if (unlikely(gb->apu_output.channel_muted[index])) {
                output.left = output.right = 0;
            }
            
            if (unlikely(gb->apu_output.max_cycles_per_sample == 1)) {
                band_limited_update_unfiltered(&gb->apu_output.band_limited[index], &output, cycles_offset);
            }
            else {
                band_limited_update(&gb->apu_output.band_limited[index],
                                    &output,
                                    (gb->apu_output.cycles_since_render + cycles_offset) * GB_BAND_LIMITED_PHASES / gb->apu_output.max_cycles_per_sample);
            }
        }
        
        return;
    }
    
    if (value == 0 && gb->apu.samples[index] == 0) return;
    
    if (!GB_apu_is_DAC_enabled(gb, index)) {
        value = gb->apu.samples[index];
    }
    else {
        gb->apu.samples[index] = value;
    }

    if (gb->apu_output.sample_rate) {
        unsigned right_volume = 0;
        if (gb->io_registers[GB_IO_NR51] & (1 << index)) {
            right_volume = (gb->io_registers[GB_IO_NR50] & 7) + 1;
        }
        unsigned left_volume = 0;
        if (gb->io_registers[GB_IO_NR51] & (0x10 << index)) {
            left_volume = ((gb->io_registers[GB_IO_NR50] >> 4) & 7) + 1;
        }
        GB_sample_t output = {0, 0};
        if (likely(!gb->apu_output.channel_muted[index])) {
            output = (GB_sample_t){(0xF - value * 2) * left_volume, (0xF - value * 2) * right_volume};
        }
        if (unlikely(gb->apu_output.max_cycles_per_sample == 1)) {
            band_limited_update_unfiltered(&gb->apu_output.band_limited[index], &output, cycles_offset);
        }
        else {
            band_limited_update(&gb->apu_output.band_limited[index],
                                &output,
                                (gb->apu_output.cycles_since_render + cycles_offset) * GB_BAND_LIMITED_PHASES / gb->apu_output.max_cycles_per_sample);
        }
    }
}

static double smooth(double x)
{
    return 3*x*x - 2*x*x*x;
}

static signed interference(GB_gameboy_t *gb)
{
    /* These aren't scientifically measured, but based on ear based on several recordings */
    signed ret = 0;
    if (gb->halted) {
        if (gb->model <= GB_MODEL_CGB_E) {
            ret -= MAX_CH_AMP / 5;
        }
        else {
            ret -= MAX_CH_AMP / 12;
        }
    }
    if (gb->io_registers[GB_IO_LCDC] & GB_LCDC_ENABLE) {
        ret += MAX_CH_AMP / 7;
        if ((gb->io_registers[GB_IO_STAT] & 3) == 3 && gb->model <= GB_MODEL_CGB_E) {
            ret += MAX_CH_AMP / 14;
        }
        else if ((gb->io_registers[GB_IO_STAT] & 3) == 1) {
            ret -= MAX_CH_AMP / 7;
        }
    }
    
    if (gb->apu.global_enable) {
        ret += MAX_CH_AMP / 10;
    }
    
    if (GB_is_cgb(gb) && gb->model <= GB_MODEL_CGB_E && (gb->io_registers[GB_IO_RP] & 1)) {
        ret += MAX_CH_AMP / 10;
    }
    
    if (!GB_is_cgb(gb)) {
        ret /= 4;
    }
    
    ret += rand() % (MAX_CH_AMP / 12);
    
    return ret;
}

static void render(GB_gameboy_t *gb)
{
    GB_sample_t output = {0, 0};

    unrolled for (unsigned i = 0; i < GB_N_CHANNELS; i++) {
        double multiplier = CH_STEP;
        
        if (gb->model <= GB_MODEL_CGB_E) {
            if (!GB_apu_is_DAC_enabled(gb, i)) {
                gb->apu_output.dac_discharge[i] -= ((double) DAC_DECAY_SPEED) / gb->apu_output.sample_rate;
                if (gb->apu_output.dac_discharge[i] < 0) {
                    multiplier = 0;
                    gb->apu_output.dac_discharge[i] = 0;
                }
                else {
                    multiplier *= smooth(gb->apu_output.dac_discharge[i]);
                }
            }
            else {
                gb->apu_output.dac_discharge[i] += ((double) DAC_ATTACK_SPEED) / gb->apu_output.sample_rate;
                if (gb->apu_output.dac_discharge[i] > 1) {
                    gb->apu_output.dac_discharge[i] = 1;
                }
                else {
                    multiplier *= smooth(gb->apu_output.dac_discharge[i]);
                }
            }
        }
        
        GB_sample_t channel_output;
        band_limited_read(&gb->apu_output.band_limited[i], &channel_output, multiplier);

        output.left += channel_output.left;
        output.right += channel_output.right;
    }
    gb->apu_output.cycles_since_render = 0;
    
    if (gb->sgb && gb->sgb->intro_animation < GB_SGB_INTRO_ANIMATION_LENGTH) return;

    GB_sample_t filtered_output = gb->apu_output.highpass_mode?
        (GB_sample_t) {output.left  - gb->apu_output.highpass_diff.left,
                       output.right - gb->apu_output.highpass_diff.right} :
        output;

    switch (gb->apu_output.highpass_mode) {
        case GB_HIGHPASS_OFF:
            gb->apu_output.highpass_diff = (GB_double_sample_t) {0, 0};
            break;
        case GB_HIGHPASS_ACCURATE:
            gb->apu_output.highpass_diff = (GB_double_sample_t) {
                output.left  - (output.left  - gb->apu_output.highpass_diff.left)  * gb->apu_output.highpass_rate,
                output.right - (output.right - gb->apu_output.highpass_diff.right) * gb->apu_output.highpass_rate
            };
            break;
        case GB_HIGHPASS_REMOVE_DC_OFFSET: {
            unsigned mask = gb->io_registers[GB_IO_NR51];
            unsigned left_volume = 0;
            unsigned right_volume = 0;
            unrolled for (unsigned i = GB_N_CHANNELS; i--;) {
                if (GB_apu_is_DAC_enabled(gb, i)) {
                    if (mask & 1) {
                        left_volume += ((gb->io_registers[GB_IO_NR50] & 7) + 1) * CH_STEP * 0xF;
                    }
                    if (mask & 0x10) {
                        right_volume += (((gb->io_registers[GB_IO_NR50] >> 4) & 7) + 1) * CH_STEP * 0xF;
                    }
                }
                mask >>= 1;
            }
            gb->apu_output.highpass_diff = (GB_double_sample_t) {
                left_volume  * (1 - gb->apu_output.highpass_rate) + gb->apu_output.highpass_diff.left * gb->apu_output.highpass_rate,
                right_volume * (1 - gb->apu_output.highpass_rate) + gb->apu_output.highpass_diff.right * gb->apu_output.highpass_rate
            };

        case GB_HIGHPASS_MAX:;
        }

    }
    
    
    if (gb->apu_output.interference_volume) {
        signed interference_bias = interference(gb);
        int16_t interference_sample = (interference_bias - gb->apu_output.interference_highpass);
        gb->apu_output.interference_highpass = gb->apu_output.interference_highpass * gb->apu_output.highpass_rate +
        (1 - gb->apu_output.highpass_rate) * interference_sample;
        interference_bias *= gb->apu_output.interference_volume;
        
        filtered_output.left = MAX(MIN(filtered_output.left + interference_bias, 0x7FFF), -0x8000);
        filtered_output.right = MAX(MIN(filtered_output.right + interference_bias, 0x7FFF), -0x8000);
    }
    assert(gb->apu_output.sample_callback);
    gb->apu_output.sample_callback(gb, &filtered_output);
    if (unlikely(gb->apu_output.output_file)) {
#ifdef GB_BIG_ENDIAN
        if (gb->apu_output.output_format == GB_AUDIO_FORMAT_WAV) {
            filtered_output.left = LE16(filtered_output.left);
            filtered_output.right = LE16(filtered_output.right);
        }
#endif
        if (fwrite(&filtered_output, sizeof(filtered_output), 1, gb->apu_output.output_file) != 1) {
            fclose(gb->apu_output.output_file);
            gb->apu_output.output_file = NULL;
            gb->apu_output.output_error = errno;
        }
    }
}

static void update_square_sample(GB_gameboy_t *gb, GB_channel_t index, unsigned cycles)
{
    if (gb->apu.square_channels[index].sample_surpressed) {
        if (gb->model > GB_MODEL_CGB_E) {
            update_sample(gb, index, gb->apu.samples[index], 0);
        }
        return;
    }

    uint8_t duty = gb->io_registers[index == GB_SQUARE_1? GB_IO_NR11 :GB_IO_NR21] >> 6;
    update_sample(gb, index,
                  duties[gb->apu.square_channels[index].current_sample_index + duty * 8]?
                  gb->apu.square_channels[index].current_volume : 0,
                  cycles);
}

static inline void update_wave_sample(GB_gameboy_t *gb, unsigned cycles)
{
    if (gb->apu.wave_channel.current_sample_index & 1) {
        update_sample(gb, GB_WAVE,
                      (gb->apu.wave_channel.current_sample_byte & 0xF) >> gb->apu.wave_channel.shift,
                      cycles);
    }
    else {
        update_sample(gb, GB_WAVE,
                      (gb->apu.wave_channel.current_sample_byte >> 4) >> gb->apu.wave_channel.shift,
                      cycles);
    }
}

static inline void set_envelope_clock(GB_envelope_clock_t *clock, bool value, bool direction, uint8_t volume)
{
    if (clock->clock == value) return;
    if (value) {
        clock->clock = true;
        clock->should_lock = (volume == 0xF && direction) || (volume == 0x0 && !direction);
    }
    else {
        clock->clock = false;
        clock->locked |= clock->should_lock;
    }
}

static void _nrx2_glitch(uint8_t *volume, uint8_t value, uint8_t old_value, uint8_t *countdown, GB_envelope_clock_t *lock)
{
    if (lock->clock) {
        *countdown = value & 7;
    }
    bool should_tick = (value & 7) && !(old_value & 7) && !lock->locked;
    bool should_invert = (value & 8) ^ (old_value & 8);
    
    if ((value & 0xF) == 8 && (old_value & 0xF) == 8 && !lock->locked) {
        should_tick = true;
    }
    
    if (should_invert) {
        // The weird and over-the-top way clocks for this counter are connected cause
        // some weird ways for it to invert
        if (value & 8) {
            if (!(old_value & 7) && !lock->locked) {
                *volume ^= 0xF;
            }
            else {
                *volume = 0xE - *volume;
                *volume &= 0xF;
            }
            should_tick = false; // Somehow prevents ticking?
        }
        else {
            *volume = 0x10 - *volume;
            *volume &= 0xF;
        }
    }
    if (should_tick) {
        if (value & 8) {
            (*volume)++;
        }
        else {
            (*volume)--;
        }
        *volume &= 0xF;
    }
    else if (!(value & 7) && lock->clock) {
        set_envelope_clock(lock, false, 0, 0);
    }
}

static void nrx2_glitch(GB_gameboy_t *gb, uint8_t *volume, uint8_t value, uint8_t old_value, uint8_t *countdown, GB_envelope_clock_t *lock)
{
    /* Note: on pre-CGB models *some* of these are non-deterministic. Specifically,
       $x0 writes seem to be  non-deterministic while  $x8 always work as expected.
       TODO: Might be useful  to find which cases are  non-deterministic, and allow
       the debugger to issue  warnings when they're used.  I suspect writes to/from
       $xF are guaranteed to be deterministic. */
    if (gb->model <= GB_MODEL_CGB_C) {
        _nrx2_glitch(volume, 0xFF, old_value, countdown, lock);
        _nrx2_glitch(volume, value, 0xFF, countdown, lock);
    }
    else {
        _nrx2_glitch(volume, value, old_value, countdown, lock);
    }
}

static void tick_square_envelope(GB_gameboy_t *gb, GB_channel_t index)
{
    set_envelope_clock(&gb->apu.square_channels[index].envelope_clock, false, 0, 0);
    if (gb->apu.square_channels[index].envelope_clock.locked) return;
    uint8_t nrx2 = gb->io_registers[index == GB_SQUARE_1? GB_IO_NR12 : GB_IO_NR22];
    
    if (!(nrx2 & 7)) return;
    if (gb->cgb_double_speed) {
        if (index == GB_SQUARE_1) {
            gb->apu.pcm_mask[0] &= gb->apu.square_channels[GB_SQUARE_1].current_volume | 0xF1;
        }
        else {
            gb->apu.pcm_mask[0] &= (gb->apu.square_channels[GB_SQUARE_2].current_volume << 2) | 0x1F;
        }
    }
    
    set_envelope_clock(&gb->apu.square_channels[index].envelope_clock, false, 0, 0);
    
    if (nrx2 & 8) {
            gb->apu.square_channels[index].current_volume++;
        }
        else {
            gb->apu.square_channels[index].current_volume--;
        }

    if (gb->apu.is_active[index]) {
        update_square_sample(gb, index, 0);
    }
}

static void tick_noise_envelope(GB_gameboy_t *gb)
{
    set_envelope_clock(&gb->apu.noise_channel.envelope_clock, false, 0, 0);
    if (gb->apu.noise_channel.envelope_clock.locked) return;
    
    uint8_t nr42 = gb->io_registers[GB_IO_NR42];
    if (!(nr42 & 7)) return;

    if (gb->cgb_double_speed) {
        gb->apu.pcm_mask[0] &= (gb->apu.noise_channel.current_volume << 2) | 0x1F;
    }
    
    if (nr42 & 8) {
            gb->apu.noise_channel.current_volume++;
        }
        else {
            gb->apu.noise_channel.current_volume--;
        }

    if (gb->apu.is_active[GB_NOISE]) {
        update_sample(gb, GB_NOISE,
                      (gb->apu.noise_channel.lfsr & 1) ?
                      gb->apu.noise_channel.current_volume : 0,
                      0);
    }
}

static void sweep_calculation_done(GB_gameboy_t *gb, unsigned cycles)
{
    /* APU bug: sweep frequency is checked after adding the sweep delta twice */
    if (gb->apu.channel_1_restart_hold == 0) {
        gb->apu.shadow_sweep_sample_length = gb->apu.square_channels[GB_SQUARE_1].sample_length;
    }
    if (gb->io_registers[GB_IO_NR10] & 8) {
        gb->apu.sweep_length_addend ^= 0x7FF;
    }
    if (gb->apu.shadow_sweep_sample_length + gb->apu.sweep_length_addend > 0x7FF && !(gb->io_registers[GB_IO_NR10] & 8)) {
        gb->apu.is_active[GB_SQUARE_1] = false;
        update_sample(gb, GB_SQUARE_1, 0, gb->apu.square_sweep_calculate_countdown * 2 - cycles);
    }
    gb->apu.channel1_completed_addend = gb->apu.sweep_length_addend;
}

static void trigger_sweep_calculation(GB_gameboy_t *gb)
{
    if ((gb->io_registers[GB_IO_NR10] & 0x70) && gb->apu.square_sweep_countdown == 7) {
        if (gb->io_registers[GB_IO_NR10] & 0x07) {
            gb->apu.square_channels[GB_SQUARE_1].sample_length =
            gb->apu.sweep_length_addend + gb->apu.shadow_sweep_sample_length + !!(gb->io_registers[GB_IO_NR10] & 0x8);
            gb->apu.square_channels[GB_SQUARE_1].sample_length &= 0x7FF;
        }
        if (gb->apu.channel_1_restart_hold == 0) {
            gb->apu.sweep_length_addend = gb->apu.square_channels[GB_SQUARE_1].sample_length;
            gb->apu.sweep_length_addend >>= (gb->io_registers[GB_IO_NR10] & 7);
        }
        
        /* Recalculation and overflow check only occurs after a delay */
        gb->apu.square_sweep_calculate_countdown = gb->io_registers[GB_IO_NR10] & 0x7;
        // TODO: this is a hack because DIV write timing is inaccurate. Will probably break on odd mode.
        gb->apu.square_sweep_calculate_countdown_reload_timer = 1 + gb->apu.lf_div;
        if (!gb->cgb_double_speed && gb->during_div_write) {
            gb->apu.square_sweep_calculate_countdown_reload_timer = 1;
        }
        gb->apu.unshifted_sweep = !(gb->io_registers[GB_IO_NR10] & 0x7);
        gb->apu.square_sweep_countdown = ((gb->io_registers[GB_IO_NR10] >> 4) & 7) ^ 7;
        if (gb->apu.square_sweep_calculate_countdown == 0) {
            gb->apu.square_sweep_instant_calculation_done = true;
        }
    }
}

void GB_apu_div_event(GB_gameboy_t *gb)
{
    GB_apu_run(gb, true);
    if (!gb->apu.global_enable) return;
    if (gb->apu.skip_div_event == GB_SKIP_DIV_EVENT_SKIP) {
        gb->apu.skip_div_event = GB_SKIP_DIV_EVENT_SKIPPED;
        return;
    }
    if (gb->apu.skip_div_event == GB_SKIP_DIV_EVENT_SKIPPED) {
        gb->apu.skip_div_event = GB_SKIP_DIV_EVENT_INACTIVE;
    }
    else {
        gb->apu.div_divider++;
    }

    if ((gb->apu.div_divider & 7) == 7) {
        unrolled for (unsigned i = GB_SQUARE_2 + 1; i--;) {
            if (!gb->apu.square_channels[i].envelope_clock.clock) {
                gb->apu.square_channels[i].volume_countdown--;
                gb->apu.square_channels[i].volume_countdown &= 7;
            }
        }
        if (!gb->apu.noise_channel.envelope_clock.clock) {
            gb->apu.noise_channel.volume_countdown--;
            gb->apu.noise_channel.volume_countdown &= 7;
        }
    }

    unrolled for (unsigned i = GB_SQUARE_2 + 1; i--;) {
        if (gb->apu.square_channels[i].envelope_clock.clock) {
            tick_square_envelope(gb, i);
        }
    }
    
    if (gb->apu.noise_channel.envelope_clock.clock) {
        tick_noise_envelope(gb);
    }
    
    if ((gb->apu.div_divider & 1) == 1) {
        unrolled for (unsigned i = GB_SQUARE_2 + 1; i--;) {
            if (gb->apu.square_channels[i].length_enabled) {
                if (gb->apu.square_channels[i].pulse_length) {
                    if (!--gb->apu.square_channels[i].pulse_length) {
                        gb->apu.is_active[i] = false;
                        update_sample(gb, i, 0, 0);
                    }
                }
            }
        }

        if (gb->apu.wave_channel.length_enabled) {
            if (gb->apu.wave_channel.pulse_length) {
                if (!--gb->apu.wave_channel.pulse_length) {
                    if (gb->apu.is_active[GB_WAVE] && gb->model > GB_MODEL_CGB_E) {
                        if (gb->apu.wave_channel.sample_countdown == 0) {
                            gb->apu.wave_channel.current_sample_byte =
                                gb->io_registers[GB_IO_WAV_START + (((gb->apu.wave_channel.current_sample_index + 1) & 0xF) >> 1)];
                        }
                        else if (gb->apu.wave_channel.sample_countdown == 9) {
                            // TODO: wtf?
                            gb->apu.wave_channel.current_sample_byte = gb->io_registers[GB_IO_WAV_START];
                        }
                    }
                    gb->apu.is_active[GB_WAVE] = false;
                    update_sample(gb, GB_WAVE, 0, 0);
                }
            }
        }

        if (gb->apu.noise_channel.length_enabled) {
            if (gb->apu.noise_channel.pulse_length) {
                if (!--gb->apu.noise_channel.pulse_length) {
                    gb->apu.is_active[GB_NOISE] = false;
                    update_sample(gb, GB_NOISE, 0, 0);
                }
            }
        }
    }

    if ((gb->apu.div_divider & 3) == 3) {
        gb->apu.square_sweep_countdown++;
        gb->apu.square_sweep_countdown &= 7;
        trigger_sweep_calculation(gb);
    }
}

void GB_apu_div_secondary_event(GB_gameboy_t *gb)
{
    GB_apu_run(gb, true);
    if (!gb->apu.global_enable) return;
    unrolled for (unsigned i = GB_SQUARE_2 + 1; i--;) {
        uint8_t nrx2 = gb->io_registers[i == GB_SQUARE_1? GB_IO_NR12 : GB_IO_NR22];
        if (gb->apu.is_active[i] && gb->apu.square_channels[i].volume_countdown == 0) {
            set_envelope_clock(&gb->apu.square_channels[i].envelope_clock,
                               (gb->apu.square_channels[i].volume_countdown = nrx2 & 7),
                               nrx2 & 8,
                               gb->apu.square_channels[i].current_volume);

        }
    }
    
    if (gb->apu.is_active[GB_NOISE] && gb->apu.noise_channel.volume_countdown == 0) {
        set_envelope_clock(&gb->apu.noise_channel.envelope_clock,
                           (gb->apu.noise_channel.volume_countdown = gb->io_registers[GB_IO_NR42] & 7),
                           gb->io_registers[GB_IO_NR42] & 8,
                           gb->apu.noise_channel.current_volume);
    }
}

static void step_lfsr(GB_gameboy_t *gb, unsigned cycles_offset)
{
    unsigned high_bit_mask = gb->apu.noise_channel.narrow ? 0x4040 : 0x4000;
    bool new_high_bit = (gb->apu.noise_channel.lfsr ^ (gb->apu.noise_channel.lfsr >> 1) ^ 1) & 1;
    gb->apu.noise_channel.lfsr >>= 1;
    
    if (new_high_bit) {
        gb->apu.noise_channel.lfsr |= high_bit_mask;
    }
    else {
        /* This code is not redundent, it's relevant when switching LFSR widths */
        gb->apu.noise_channel.lfsr &= ~high_bit_mask;
    }
    
    gb->apu.noise_channel.current_lfsr_sample = gb->apu.noise_channel.lfsr & 1;
    if (gb->apu.is_active[GB_NOISE]) {
        update_sample(gb, GB_NOISE,
                      gb->apu.noise_channel.current_lfsr_sample ?
                      gb->apu.noise_channel.current_volume : 0,
                      cycles_offset);
    }
}

void GB_apu_run(GB_gameboy_t *gb, bool force)
{
    uint32_t clock_rate = GB_get_clock_rate(gb);
    bool orig_force = force;
    
restart:;
    uint16_t cycles = gb->apu.apu_cycles;

    if (force ||
        (cycles + gb->apu_output.cycles_since_render >= gb->apu_output.max_cycles_per_sample) ||
        (gb->apu_output.sample_cycles >= clock_rate) ||
        (gb->apu.square_sweep_calculate_countdown || gb->apu.channel_1_restart_hold || gb->apu.square_sweep_calculate_countdown_reload_timer) ||
        (gb->model <= GB_MODEL_CGB_E && (gb->apu.wave_channel.bugged_read_countdown || (gb->apu.wave_channel.enable && gb->apu.wave_channel.pulsed)))) {
        force = true;
    }
    if (!force) {
        return;
    }
    
    /* Force renders to never be more than max_cycles_per_sample apart by spliting runs. */
    while (cycles + gb->apu_output.cycles_since_render > gb->apu_output.max_cycles_per_sample) {
        /* We're already past max_cycles_per_sample. This can happen when changing clock rates, etc.
           Let this sample render normally. */
        if (unlikely(gb->apu_output.cycles_since_render > gb->apu_output.max_cycles_per_sample)) break;
        
        gb->apu.apu_cycles = gb->apu_output.max_cycles_per_sample - gb->apu_output.cycles_since_render;
        
        if (gb->apu.apu_cycles) {
            // Run for just enough cycles to reach max_cycles_per_sample
            cycles -= gb->apu.apu_cycles;
            GB_apu_run(gb, true);
            // Re-evaluate force if needed
            if (!orig_force) {
                force = false;
                gb->apu.apu_cycles = cycles;
                goto restart;
            }
            // Check if we need another batch
            continue;
        }
        
        // Render if needed
        if (gb->apu_output.sample_cycles >= clock_rate) {
            gb->apu_output.sample_cycles -= clock_rate;
            render(gb);
        }
        break;
    }

    gb->apu.apu_cycles = 0;
    if (!cycles) {
        /* This can happen in pre-CGB stop mode */
        while (unlikely(gb->apu_output.sample_cycles >= clock_rate)) {
            gb->apu_output.sample_cycles -= clock_rate;
            render(gb);
        }
        return;
    }
    
    if (unlikely(gb->apu.wave_channel.bugged_read_countdown)) {
        uint16_t cycles_left = cycles;
        while (cycles_left) {
            cycles_left--;
            if (--gb->apu.wave_channel.bugged_read_countdown == 0) {
                    gb->apu.wave_channel.current_sample_byte =
                gb->io_registers[GB_IO_WAV_START + (gb->address_bus & 0xF)];
                if (gb->apu.is_active[GB_WAVE]) {
                    update_wave_sample(gb, 0);
                }
                break;
            }
        }
    }
    
    bool start_ch4 = false;
    if (likely(!gb->stopped || GB_is_cgb(gb))) {
        if (gb->apu.noise_channel.dmg_delayed_start) {
            if (gb->apu.noise_channel.dmg_delayed_start == cycles) {
                gb->apu.noise_channel.dmg_delayed_start = 0;
                start_ch4 = true;
            }
            else if (gb->apu.noise_channel.dmg_delayed_start > cycles) {
                gb->apu.noise_channel.dmg_delayed_start -= cycles;
            }
            else {
                /* Split it into two */
                cycles -= gb->apu.noise_channel.dmg_delayed_start;
                gb->apu.apu_cycles = gb->apu.noise_channel.dmg_delayed_start;
                GB_apu_run(gb, true);
            }
        }
        /* To align the square signal to 1MHz */
        gb->apu.lf_div ^= cycles & 1;
        gb->apu.noise_channel.alignment += cycles;
        
        unsigned sweep_cycles = cycles / 2;
        if ((cycles & 1) && !gb->apu.lf_div) {
            sweep_cycles++;
        }

        if (gb->apu.square_sweep_calculate_countdown_reload_timer > sweep_cycles) {
            gb->apu.square_sweep_calculate_countdown_reload_timer -= sweep_cycles;
            sweep_cycles = 0;
        }
        else {
            if (gb->apu.square_sweep_calculate_countdown_reload_timer && !gb->apu.square_sweep_calculate_countdown && gb->apu.square_sweep_instant_calculation_done) {
                sweep_calculation_done(gb, cycles);
            }
            gb->apu.square_sweep_instant_calculation_done = false;
            sweep_cycles -= gb->apu.square_sweep_calculate_countdown_reload_timer;
            gb->apu.square_sweep_calculate_countdown_reload_timer = 0;
        }
        
        if (gb->apu.square_sweep_calculate_countdown &&
            (((gb->io_registers[GB_IO_NR10] & 7) || gb->apu.unshifted_sweep))) { // Calculation is paused if the lower bits are 0
            if (gb->apu.square_sweep_calculate_countdown > sweep_cycles) {
                gb->apu.square_sweep_calculate_countdown -= sweep_cycles;
            }
            else {
                gb->apu.square_sweep_calculate_countdown = 0;
                sweep_calculation_done(gb, cycles);
            }
        }
        
        if (gb->apu.channel_1_restart_hold) {
            if (gb->apu.channel_1_restart_hold > cycles) {
                gb->apu.channel_1_restart_hold -= cycles;
            }
            else {
                gb->apu.channel_1_restart_hold = 0;
            }
        }

        unrolled for (unsigned i = GB_SQUARE_1; i <= GB_SQUARE_2; i++) {
            if (gb->apu.is_active[i]) {
                uint16_t cycles_left = cycles;
                if (unlikely(gb->apu.square_channels[i].delay)) {
                    if (gb->apu.square_channels[i].delay < cycles_left) {
                        gb->apu.square_channels[i].delay = 0;
                    }
                    else {
                        gb->apu.square_channels[i].delay -= cycles_left;
                    }
                }
                while (unlikely(cycles_left > gb->apu.square_channels[i].sample_countdown)) {
                    cycles_left -= gb->apu.square_channels[i].sample_countdown + 1;
                    gb->apu.square_channels[i].sample_countdown = (gb->apu.square_channels[i].sample_length ^ 0x7FF) * 2 + 1;
                    gb->apu.square_channels[i].current_sample_index++;
                    gb->apu.square_channels[i].current_sample_index &= 0x7;
                    gb->apu.square_channels[i].sample_surpressed = false;
                    if (cycles_left == 0 && gb->apu.samples[i] == 0) {
                        gb->apu.pcm_mask[0] &= i == GB_SQUARE_1? 0xF0 : 0x0F;
                    }
                    gb->apu.square_channels[i].did_tick = true;
                    update_square_sample(gb, i, cycles - cycles_left);

                    uint8_t duty = gb->io_registers[i == GB_SQUARE_1? GB_IO_NR11 :GB_IO_NR21] >> 6;
                    uint8_t edge_sample_index = inline_const(uint8_t[], {7, 7, 5, 1})[duty];
                    if (gb->apu.square_channels[i].current_sample_index == edge_sample_index) {
                        gb->apu_output.edge_triggered[i] = true;
                    }
                }
                gb->apu.square_channels[i].just_reloaded = cycles_left == 0;
                if (cycles_left) {
                    gb->apu.square_channels[i].sample_countdown -= cycles_left;
                }
            }
        }

        gb->apu.wave_channel.wave_form_just_read = false;
        if (gb->apu.is_active[GB_WAVE]) {
            uint16_t cycles_left = cycles;
            while (unlikely(cycles_left > gb->apu.wave_channel.sample_countdown)) {
                cycles_left -= gb->apu.wave_channel.sample_countdown + 1;
                gb->apu.wave_channel.sample_countdown = gb->apu.wave_channel.sample_length ^ 0x7FF;
                gb->apu.wave_channel.current_sample_index++;
                gb->apu.wave_channel.current_sample_index &= 0x1F;
                gb->apu.wave_channel.current_sample_byte =
                    gb->io_registers[GB_IO_WAV_START + (gb->apu.wave_channel.current_sample_index >> 1)];
                update_wave_sample(gb, cycles - cycles_left);
                gb->apu.wave_channel.wave_form_just_read = true;
                if (gb->apu.wave_channel.current_sample_index == 0) {
                    gb->apu_output.edge_triggered[GB_WAVE] = true;
                }
            }
            if (cycles_left) {
                gb->apu.wave_channel.sample_countdown -= cycles_left;
                gb->apu.wave_channel.wave_form_just_read = false;
            }
        }
        else if (gb->apu.wave_channel.enable && gb->apu.wave_channel.pulsed && gb->model <= GB_MODEL_CGB_E) {
            uint16_t cycles_left = cycles;
            while (unlikely(cycles_left > gb->apu.wave_channel.sample_countdown)) {
                cycles_left -= gb->apu.wave_channel.sample_countdown + 1;
                gb->apu.wave_channel.sample_countdown = gb->apu.wave_channel.sample_length ^ 0x7FF;
                if (cycles_left) {
                    gb->apu.wave_channel.current_sample_byte =
                    gb->io_registers[GB_IO_WAV_START + (gb->address_bus & 0xF)];
                }
                else {
                    gb->apu.wave_channel.bugged_read_countdown = 1;
                }
            }
            if (cycles_left) {
                gb->apu.wave_channel.sample_countdown -= cycles_left;
            }
            if (gb->apu.wave_channel.sample_countdown == 0) {
                gb->apu.wave_channel.bugged_read_countdown = 2;
            }
        }
        
        // The noise channel can step even if inactive on the DMG
        if (gb->apu.is_active[GB_NOISE] || !GB_is_cgb(gb)) {
            uint16_t cycles_left = cycles;
            unsigned divisor = (gb->io_registers[GB_IO_NR43] & 0x07) << 2;
            if (!divisor) divisor = 2;
            if (gb->apu.noise_channel.counter_countdown == 0) {
                gb->apu.noise_channel.counter_countdown = divisor;
            }
            // This while doesn't get an unlikely because the noise channel steps frequently enough
            while (cycles_left >= gb->apu.noise_channel.counter_countdown) {
                cycles_left -= gb->apu.noise_channel.counter_countdown;
                gb->apu.noise_channel.counter_countdown = divisor + gb->apu.noise_channel.delta;
                gb->apu.noise_channel.delta = 0;
                bool old_bit = (gb->apu.noise_channel.counter >> (gb->io_registers[GB_IO_NR43] >> 4)) & 1;
                gb->apu.noise_channel.counter++;
                gb->apu.noise_channel.counter &= 0x3FFF;
                bool new_bit = (gb->apu.noise_channel.counter >> (gb->io_registers[GB_IO_NR43] >> 4)) & 1;

                /* Step LFSR */
                if (new_bit && !old_bit) {
                    if (cycles_left == 0 && gb->apu.samples[GB_NOISE] == 0) {
                        gb->apu.pcm_mask[1] &= 0x0F;
                    }
                    step_lfsr(gb, cycles - cycles_left);
                }
            }
            if (cycles_left) {
                gb->apu.noise_channel.counter_countdown -= cycles_left;
                gb->apu.noise_channel.countdown_reloaded = false;
            }
            else {
                gb->apu.noise_channel.countdown_reloaded = true;
                gb->apu_output.edge_triggered[GB_NOISE] = true;
            }
        }
    }

    if (gb->apu_output.sample_rate) {
        gb->apu_output.cycles_since_render += cycles;

        if (gb->apu_output.sample_cycles >= clock_rate) {
            gb->apu_output.sample_cycles -= clock_rate;
            render(gb);
        }
    }
    if (start_ch4) {
        GB_apu_write(gb, GB_IO_NR44, gb->io_registers[GB_IO_NR44] | 0x80);
    }
}

void GB_apu_init(GB_gameboy_t *gb)
{
    memset(&gb->apu, 0, sizeof(gb->apu));
    gb->apu.apu_cycles_in_2mhz = true;
    gb->apu.lf_div = 1;
    gb->apu.wave_channel.shift = 4;
    /* APU glitch: When turning the APU on while DIV's bit 4 (or 5 in double speed mode) is on,
       the first DIV/APU event is skipped. */
    if (gb->div_counter & (gb->cgb_double_speed? 0x2000 : 0x1000)) {
        gb->apu.skip_div_event = GB_SKIP_DIV_EVENT_SKIP;
        gb->apu.div_divider = 1;
    }
    gb->apu.square_channels[GB_SQUARE_1].sample_countdown = -1;
    gb->apu.square_channels[GB_SQUARE_2].sample_countdown = -1;
}

uint8_t GB_apu_read(GB_gameboy_t *gb, uint8_t reg)
{
    GB_apu_run(gb, true);
    if (reg == GB_IO_NR52) {
        uint8_t value = 0;
        for (unsigned i = 0; i < GB_N_CHANNELS; i++) {
            value >>= 1;
            if (gb->apu.is_active[i]) {
                value |= 0x8;
            }
        }
        if (gb->apu.global_enable) {
            value |= 0x80;
        }
        value |= 0x70;
        return value;
    }

    static const char read_mask[GB_IO_WAV_END - GB_IO_NR10 + 1] = {
     /* NRX0  NRX1  NRX2  NRX3  NRX4 */
        0x80, 0x3F, 0x00, 0xFF, 0xBF, // NR1X
        0xFF, 0x3F, 0x00, 0xFF, 0xBF, // NR2X
        0x7F, 0xFF, 0x9F, 0xFF, 0xBF, // NR3X
        0xFF, 0xFF, 0x00, 0x00, 0xBF, // NR4X
        0x00, 0x00, 0x70, 0xFF, 0xFF, // NR5X

        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Unused
        // Wave RAM
        0, /* ... */
    };

    if (reg >= GB_IO_WAV_START && reg <= GB_IO_WAV_END && gb->apu.is_active[GB_WAVE]) {
        if (!GB_is_cgb(gb) && !gb->apu.wave_channel.wave_form_just_read) {
            return 0xFF;
        }
        if (gb->model > GB_MODEL_CGB_E) {
            return 0xFF;
        }
        reg = GB_IO_WAV_START + gb->apu.wave_channel.current_sample_index / 2;
    }

    return gb->io_registers[reg] | read_mask[reg - GB_IO_NR10];
}

static inline uint16_t effective_channel4_counter(GB_gameboy_t *gb)
{
    /*
       TODO: On revisions  older than the CGB-D,  this behaves differently  when
             the counter advanced this exact T-cycle.  Also, in these revisions,
             it seems that "passive" changes (due to the temporary FF value NR43
             has during writes) behave slightly different from non-passive ones.
    */
    uint16_t effective_counter = gb->apu.noise_channel.counter;
    /* Ladies and gentlemen, I present you the holy grail glitch of revision detection! */
    switch (gb->model) {
            /* Pre CGB revisions are assumed to be like CGB-C, A and 0 for the lack of a better guess.
             TODO: It could be verified with audio based test ROMs. */
        case GB_MODEL_CGB_B:
            if (effective_counter & 8) {
                effective_counter |= 0xE; // Seems to me F under some circumstances?
            }
            if (effective_counter & 0x80) {
                effective_counter |= 0xFF;
            }
            if (effective_counter & 0x100) {
                effective_counter |= 0x1;
            }
            if (effective_counter & 0x200) {
                effective_counter |= 0x2;
            }
            if (effective_counter & 0x400) {
                effective_counter |= 0x4;
            }
            if (effective_counter & 0x800) {
                effective_counter |= 0x408; // TODO: Only my CGB-B does that! Others behave like C!
            }
            if (effective_counter & 0x1000) {
                effective_counter |= 0x10;
            }
            if (effective_counter & 0x2000) {
                effective_counter |= 0x20;
            }
            break;
        case GB_MODEL_DMG_B:
        case GB_MODEL_MGB:
        case GB_MODEL_SGB_NTSC:
        case GB_MODEL_SGB_PAL:
        case GB_MODEL_SGB_NTSC_NO_SFC:
        case GB_MODEL_SGB_PAL_NO_SFC:
        case GB_MODEL_SGB2:
        case GB_MODEL_SGB2_NO_SFC:
        case GB_MODEL_CGB_0:
        case GB_MODEL_CGB_A:
        case GB_MODEL_CGB_C:
            if (effective_counter & 8) {
                effective_counter |= 0xE; // Sometimes F on some instances
            }
            if (effective_counter & 0x80) {
                effective_counter |= 0xFF;
            }
            if (effective_counter & 0x100) {
                effective_counter |= 0x1;
            }
            if (effective_counter & 0x200) {
                effective_counter |= 0x2;
            }
            if (effective_counter & 0x400) {
                effective_counter |= 0x4;
            }
            if (effective_counter & 0x800) {
                if ((gb->io_registers[GB_IO_NR43] & 8)) {
                    effective_counter |= 0x400;
                }
                effective_counter |= 0x8;
            }
            if (effective_counter & 0x1000) {
                effective_counter |= 0x10;
            }
            if (effective_counter & 0x2000) {
                effective_counter |= 0x20;
            }
            break;
        case GB_MODEL_CGB_D:
            if (effective_counter & ((gb->io_registers[GB_IO_NR43] & 8)? 0x40 : 0x80)) { // This is so weird
                effective_counter |= 0xFF;
            }
            if (effective_counter & 0x100) {
                effective_counter |= 0x1;
            }
            if (effective_counter & 0x200) {
                effective_counter |= 0x2;
            }
            if (effective_counter & 0x400) {
                effective_counter |= 0x4;
            }
            if (effective_counter & 0x800) {
                effective_counter |= 0x8;
            }
            if (effective_counter & 0x1000) {
                effective_counter |= 0x10;
            }
            break;
        case GB_MODEL_CGB_E:
            if (effective_counter & ((gb->io_registers[GB_IO_NR43] & 8)? 0x40 : 0x80)) { // This is so weird
                effective_counter |= 0xFF;
            }
            if (effective_counter & 0x1000) {
                effective_counter |= 0x10;
            }
            break;
        case GB_MODEL_AGB_A:
        case GB_MODEL_GBP_A:
            /* TODO: AGBs are not affected, but AGSes are. They don't seem to follow a simple
               pattern like the other revisions. */
            /* For the most part, AGS seems to do:
               0x20   -> 0xA0
               0x200  -> 0xA00
               0x1000 -> 0x1010, but only if wide
             */
            break;
    }
    return effective_counter;
}

static noinline void nr10_write_glitch(GB_gameboy_t *gb, uint8_t value)
{
    // TODO: Check all of these in APU odd mode
    if (gb->model <= GB_MODEL_CGB_C) {
        if (gb->apu.square_sweep_calculate_countdown_reload_timer == 1 && !gb->apu.lf_div) {
            if (gb->cgb_double_speed) {
                /* This is some instance-specific data corruption. It might also be affect by revision.
                 At least for my CGB-0 (haven't tested any other CGB-0s), the '3' case is non-deterministic. */
                static const uint8_t corruption[8] =    {7, 7, 5, 7, 3, 3, 5, 7}; // Two of my CGB-Cs, CGB-A
                // static const uint8_t corruption[8] = {7, 7, 1, 3, 3, 3, 5, 7}; // My other CGB-C, Coffee Bat's CGB-C
                // static const uint8_t corruption[8] = {7, 1, 1, 3, 3, 5, 5, 7}; // My CGB-B
                // static const uint8_t corruption[8] = {7, 7, 1, *, 3, 3, 5, 7}; // My CGB-0
                                
                // static const uint8_t corruption[8] = {7, 5, 1, 3, 3, 1, 5, 7}; // PinoBatch's CGB-B
                // static const uint8_t corruption[8] = {7, 5, 1, 3, 3, *, 5, 7}; // GenericHeroGuy CGB-C
                

                // TODO: How does this affect actual frequency calculation?
                
                gb->apu.square_sweep_calculate_countdown = corruption[gb->apu.square_sweep_calculate_countdown & 7];
                /* TODO: the value of 1 needs special handling, but it doesn't occur with the instance I'm emulating here */
            }
        }
        else if (gb->apu.square_sweep_calculate_countdown_reload_timer > 1) {
            if (gb->cgb_double_speed) {
                // TODO: How does this affect actual frequency calculation?
                gb->apu.square_sweep_calculate_countdown = value & 7;
            }
        }
        else if (gb->apu.square_sweep_calculate_countdown) {
            // No clue why 1 is a special case here
            bool should_zombie_step = false;
            if (!(gb->io_registers[GB_IO_NR10] & 7)) {
                should_zombie_step = gb->apu.lf_div ^ gb->cgb_double_speed;
            }
            else if (gb->cgb_double_speed && gb->apu.square_sweep_calculate_countdown == 1) {
                should_zombie_step = true;
            }
            
            if (should_zombie_step) {
                gb->apu.square_sweep_calculate_countdown--;
                if (gb->apu.square_sweep_calculate_countdown <= 1) {
                    gb->apu.square_sweep_calculate_countdown = 0;
                    sweep_calculation_done(gb, 0);
                }
            }
        }
    }
    else {
        if (gb->apu.square_sweep_calculate_countdown_reload_timer == 2) {
            // Countdown just reloaded, re-reload it
            gb->apu.square_sweep_calculate_countdown = value & 0x7;
            if (!gb->apu.square_sweep_calculate_countdown) {
                gb->apu.square_sweep_calculate_countdown_reload_timer = 0;
            }
            else {
                // TODO: How does this affect actual frequency calculation?
            }
        }
        if ((value & 7) && !(gb->io_registers[GB_IO_NR10] & 7) && !gb->apu.lf_div && gb->apu.square_sweep_calculate_countdown > 1) {
            // TODO: Another odd glitch? Ditto
            gb->apu.square_sweep_calculate_countdown--;
            if (!gb->apu.square_sweep_calculate_countdown) {
                sweep_calculation_done(gb, 0);
            }
        }
    }

}

void GB_apu_write(GB_gameboy_t *gb, uint8_t reg, uint8_t value)
{
    GB_apu_run(gb, true);
    if (!gb->apu.global_enable && reg != GB_IO_NR52 && reg < GB_IO_WAV_START && (GB_is_cgb(gb) ||
                                                                                (
                                                                                reg != GB_IO_NR11 &&
                                                                                reg != GB_IO_NR21 &&
                                                                                reg != GB_IO_NR31 &&
                                                                                reg != GB_IO_NR41
                                                                                )
                                                                                )) {
        return;
    }

    if (reg >= GB_IO_WAV_START && reg <= GB_IO_WAV_END && gb->apu.is_active[GB_WAVE]) {
        if ((!GB_is_cgb(gb) && !gb->apu.wave_channel.wave_form_just_read) || gb->model > GB_MODEL_CGB_E) {
            return;
        }
        reg = GB_IO_WAV_START + gb->apu.wave_channel.current_sample_index / 2;
    }

    /* Todo: this can and should be rewritten with a function table. */
    switch (reg) {
        /* Globals */
        case GB_IO_NR50:
        case GB_IO_NR51:
            gb->io_registers[reg] = value;
            /* These registers affect the output of all 4 channels (but not the output of the PCM registers).*/
            /* We call update_samples with the current value so the APU output is updated with the new outputs */
            for (unsigned i = GB_N_CHANNELS; i--;) {
                int8_t sample = gb->apu.samples[i];
                gb->apu.samples[i] = 0x10; // Invalidate to force update
                update_sample(gb, i, sample, 0);
            }
            break;
        case GB_IO_NR52: {

            uint8_t old_pulse_lengths[] = {
                gb->apu.square_channels[0].pulse_length,
                gb->apu.square_channels[1].pulse_length,
                gb->apu.wave_channel.pulse_length,
                gb->apu.noise_channel.pulse_length
            };
            if ((value & 0x80) && !gb->apu.global_enable) {
                GB_apu_init(gb);
                gb->apu.global_enable = true;
            }
            else if (!(value & 0x80) && gb->apu.global_enable)  {
                for (unsigned i = GB_N_CHANNELS; i--;) {
                    update_sample(gb, i, 0, 0);
                }
                memset(&gb->apu, 0, sizeof(gb->apu));
                memset(gb->io_registers + GB_IO_NR10, 0, GB_IO_WAV_START - GB_IO_NR10);
                gb->apu.global_enable = false;
                gb->apu.apu_cycles_in_2mhz = true;
            }

            if (!GB_is_cgb(gb) && (value & 0x80)) {
                gb->apu.square_channels[0].pulse_length = old_pulse_lengths[0];
                gb->apu.square_channels[1].pulse_length = old_pulse_lengths[1];
                gb->apu.wave_channel.pulse_length = old_pulse_lengths[2];
                gb->apu.noise_channel.pulse_length = old_pulse_lengths[3];
            }
        }
        break;

        /* Square channels */
        case GB_IO_NR10: {
            if (unlikely(gb->apu.square_sweep_calculate_countdown || gb->apu.square_sweep_calculate_countdown_reload_timer)) {
                nr10_write_glitch(gb, value);
            }
            bool old_negate = gb->io_registers[GB_IO_NR10] & 8;
            gb->io_registers[GB_IO_NR10] = value;
            if (gb->model <= GB_MODEL_CGB_C) {
                old_negate = true;
            }
            if (gb->apu.shadow_sweep_sample_length + gb->apu.channel1_completed_addend + old_negate > 0x7FF &&
                !(value & 8)) {
                gb->apu.is_active[GB_SQUARE_1] = false;
                update_sample(gb, GB_SQUARE_1, 0, 0);
            }
            trigger_sweep_calculation(gb);
            break;
        }

        case GB_IO_NR11:
        case GB_IO_NR21: {
            GB_channel_t index = reg == GB_IO_NR21? GB_SQUARE_2: GB_SQUARE_1;
            gb->apu.square_channels[index].pulse_length = (0x40 - (value & 0x3F));
            if (!gb->apu.global_enable) {
                value &= 0x3F;
            }
            break;
        }

        case GB_IO_NR12:
        case GB_IO_NR22: {
            GB_channel_t index = reg == GB_IO_NR22? GB_SQUARE_2: GB_SQUARE_1;
            if ((value & 0xF8) == 0) {
                /* This disables the DAC */
                gb->io_registers[reg] = value;
                gb->apu.is_active[index] = false;
                update_sample(gb, index, 0, 0);
            }
            else if (gb->apu.is_active[index]) {
                nrx2_glitch(gb, &gb->apu.square_channels[index].current_volume,
                            value, gb->io_registers[reg], &gb->apu.square_channels[index].volume_countdown,
                            &gb->apu.square_channels[index].envelope_clock);
                update_square_sample(gb, index, 0);
            }

            break;
        }

        case GB_IO_NR13:
        case GB_IO_NR23: {
            GB_channel_t index = reg == GB_IO_NR23? GB_SQUARE_2: GB_SQUARE_1;
            gb->apu.square_channels[index].sample_length &= ~0xFF;
            gb->apu.square_channels[index].sample_length |= value & 0xFF;
            if (gb->apu.square_channels[index].just_reloaded) {
                gb->apu.square_channels[index].sample_countdown = (gb->apu.square_channels[index].sample_length ^ 0x7FF) * 2 + 1;
            }
            break;
        }

        case GB_IO_NR14:
        case GB_IO_NR24: {
            GB_channel_t index = reg == GB_IO_NR24? GB_SQUARE_2: GB_SQUARE_1;
            bool was_active = gb->apu.is_active[index];
            /* TODO: When the sample length changes right before being updated from ≥$700 to <$700, the countdown
                     should change to the old length, but the current sample should not change. Because our write
                     timing isn't accurate to the T-cycle, we hack around it by stepping the sample index backwards. */
            if ((value & 0x80) == 0 && gb->apu.is_active[index] && (gb->io_registers[reg] & 0x7) == 7 && (value & 7) != 7) {
                /* On an AGB, as well as on CGB C and earlier (TODO: Tested: 0, B and C), it behaves slightly different on
                   double speed. */
                if (gb->model == GB_MODEL_CGB_E || gb->model == GB_MODEL_CGB_D || gb->apu.square_channels[index].sample_countdown & 1) {
                    if (gb->apu.square_channels[index].did_tick &&
                        gb->apu.square_channels[index].sample_countdown >> 1 == (gb->apu.square_channels[index].sample_length ^ 0x7FF)) {
                        gb->apu.square_channels[index].current_sample_index--;
                        gb->apu.square_channels[index].current_sample_index &= 7;
                        gb->apu.square_channels[index].sample_surpressed = false;
                    }
                }
            }

            uint16_t old_sample_length = gb->apu.square_channels[index].sample_length;
            gb->apu.square_channels[index].sample_length &= 0xFF;
            gb->apu.square_channels[index].sample_length |= (value & 7) << 8;
            if (gb->apu.square_channels[index].just_reloaded) {
                gb->apu.square_channels[index].sample_countdown = (gb->apu.square_channels[index].sample_length ^ 0x7FF) * 2 + 1;
            }
            if (value & 0x80) {
                /* Current sample index remains unchanged when restarting channels 1 or 2. It is only reset by
                   turning the APU off. */
                gb->apu.square_channels[index].envelope_clock.locked = false;
                gb->apu.square_channels[index].envelope_clock.clock = false;
                gb->apu.square_channels[index].did_tick = false;
                bool force_unsurpressed = false;
                if (!gb->apu.is_active[index]) {
                    if (gb->model == GB_MODEL_CGB_E || gb->model == GB_MODEL_CGB_D) {
                        if (!(value & 4) && !(((gb->apu.square_channels[index].sample_countdown - gb->apu.square_channels[index].delay) / 2) & 0x400)) {
                            gb->apu.square_channels[index].current_sample_index++;
                            gb->apu.square_channels[index].current_sample_index &= 0x7;
                            force_unsurpressed = true;
                        }
                    }
                    gb->apu.square_channels[index].delay = 6 - gb->apu.lf_div;
                    gb->apu.square_channels[index].sample_countdown = (gb->apu.square_channels[index].sample_length ^ 0x7FF) * 2 + gb->apu.square_channels[index].delay;
                }
                else {
                    unsigned extra_delay = 0;
                    if (gb->model == GB_MODEL_CGB_E || gb->model == GB_MODEL_CGB_D) {
                        if (!gb->apu.square_channels[index].just_reloaded && !(value & 4) && !(((gb->apu.square_channels[index].sample_countdown - 1 - gb->apu.square_channels[index].delay) / 2) & 0x400)) {
                            gb->apu.square_channels[index].current_sample_index++;
                            gb->apu.square_channels[index].current_sample_index &= 0x7;
                            gb->apu.square_channels[index].sample_surpressed = false;
                        }
                        /* Todo: verify with the schematics what's going on in here */
                        else if (gb->apu.square_channels[index].sample_length == 0x7FF &&
                                 old_sample_length != 0x7FF &&
                                 (gb->apu.square_channels[index].sample_surpressed)) {
                            extra_delay += 2;
                        }
                    }
                    /* Timing quirk: if already active, sound starts 2 (2MHz) ticks earlier.*/
                    gb->apu.square_channels[index].delay = 4 - gb->apu.lf_div + extra_delay;
                    gb->apu.square_channels[index].sample_countdown = (gb->apu.square_channels[index].sample_length ^ 0x7FF) * 2 + gb->apu.square_channels[index].delay;
                }
                gb->apu.square_channels[index].current_volume = gb->io_registers[index == GB_SQUARE_1 ? GB_IO_NR12 : GB_IO_NR22] >> 4;
                /* The volume changes caused by NRX4 sound start take effect instantly (i.e. the effect the previously
                   started sound). The playback itself is not instant which is why we don't update the sample for other
                   cases. */
                if (gb->apu.is_active[index]) {
                    update_square_sample(gb, index, 0);
                }

                gb->apu.square_channels[index].volume_countdown = gb->io_registers[index == GB_SQUARE_1 ? GB_IO_NR12 : GB_IO_NR22] & 7;

                if ((gb->io_registers[index == GB_SQUARE_1 ? GB_IO_NR12 : GB_IO_NR22] & 0xF8) != 0 && !gb->apu.is_active[index]) {
                    gb->apu.is_active[index] = true;
                    update_sample(gb, index, 0, 0);
                    gb->apu.square_channels[index].sample_surpressed = true && !force_unsurpressed;
                }
                if (gb->apu.square_channels[index].pulse_length == 0) {
                    gb->apu.square_channels[index].pulse_length = 0x40;
                    gb->apu.square_channels[index].length_enabled = false;
                }

                if (index == GB_SQUARE_1) {
                    gb->apu.square_sweep_instant_calculation_done = false;
                    gb->apu.shadow_sweep_sample_length = 0;
                    gb->apu.channel1_completed_addend = 0;
                    if (gb->io_registers[GB_IO_NR10] & 7) {
                        /* APU bug: if shift is nonzero, overflow check also occurs on trigger */
                        gb->apu.square_sweep_calculate_countdown = gb->io_registers[GB_IO_NR10] & 0x7;
                        if ((gb->apu.lf_div ^ !gb->cgb_double_speed) && gb->model <= GB_MODEL_CGB_C) {
                            gb->apu.square_sweep_calculate_countdown_reload_timer = 3;
                        }
                        else {
                            gb->apu.square_sweep_calculate_countdown_reload_timer = 2;
                        }
                        gb->apu.unshifted_sweep = false;
                        if (!was_active) {
                            gb->apu.square_sweep_calculate_countdown_reload_timer++;
                        }
                        gb->apu.sweep_length_addend = gb->apu.square_channels[GB_SQUARE_1].sample_length;
                        gb->apu.sweep_length_addend >>= (gb->io_registers[GB_IO_NR10] & 7);
                    }
                    else {
                        gb->apu.sweep_length_addend = 0;
                    }
                    gb->apu.channel_1_restart_hold = 2 - gb->apu.lf_div + (GB_is_cgb(gb) && gb->model != GB_MODEL_CGB_D) * 2;
                    gb->apu.square_sweep_countdown = ((gb->io_registers[GB_IO_NR10] >> 4) & 7) ^ 7;
                }
            }

            /* APU glitch - if length is enabled while the DIV-divider's LSB is 1, tick the length once. */
            if (((value & 0x40) || (GB_is_cgb(gb) && gb->model <= GB_MODEL_CGB_B)) && // Current value is irrelevant on CGB-B and older
                !gb->apu.square_channels[index].length_enabled &&
                (gb->apu.div_divider & 1) &&
                gb->apu.square_channels[index].pulse_length) {
                gb->apu.square_channels[index].pulse_length--;
                if (gb->apu.square_channels[index].pulse_length == 0) {
                    if (value & 0x80) {
                        gb->apu.square_channels[index].pulse_length = 0x3F;
                    }
                    else {
                        gb->apu.is_active[index] = false;
                        update_sample(gb, index, 0, 0);
                    }
                }
            }
            gb->apu.square_channels[index].length_enabled = value & 0x40;
            break;
        }

        /* Wave channel */
        case GB_IO_NR30:
            gb->apu.wave_channel.enable = value & 0x80;
            if (!gb->apu.wave_channel.enable) {
                gb->apu.wave_channel.pulsed = false;
                if (gb->apu.is_active[GB_WAVE]) {
                    // Todo: I assume this happens on pre-CGB models; test this with an audible test
                    if (gb->apu.wave_channel.sample_countdown == 0 && gb->model <= GB_MODEL_CGB_E) {
                        gb->apu.wave_channel.current_sample_byte = gb->io_registers[GB_IO_WAV_START + (gb->pc & 0xF)];
                    }
                    else if (gb->apu.wave_channel.wave_form_just_read && gb->model <= GB_MODEL_CGB_C) {
                        gb->apu.wave_channel.current_sample_byte = gb->io_registers[GB_IO_WAV_START + (GB_IO_NR30 & 0xF)];
                    }
                }
                gb->apu.is_active[GB_WAVE] = false;
                update_sample(gb, GB_WAVE, 0, 0);
            }
            break;
        case GB_IO_NR31:
            gb->apu.wave_channel.pulse_length = (0x100 - value);
            break;
        case GB_IO_NR32:
            gb->apu.wave_channel.shift = inline_const(uint8_t[], {4, 0, 1, 2})[(value >> 5) & 3];
            if (gb->apu.is_active[GB_WAVE]) {
                update_wave_sample(gb, 0);
            }
            break;
        case GB_IO_NR33:
            gb->apu.wave_channel.sample_length &= ~0xFF;
            gb->apu.wave_channel.sample_length |= value & 0xFF;
            if (gb->apu.wave_channel.bugged_read_countdown == 1) { // Just reloaded countdown
                /* TODO: not verified with a test ROM yet */
                gb->apu.wave_channel.sample_countdown = gb->apu.wave_channel.sample_length ^ 0x7FF;
            }
            break;
        case GB_IO_NR34:
            gb->apu.wave_channel.sample_length &= 0xFF;
            gb->apu.wave_channel.sample_length |= (value & 7) << 8;
            if (value & 0x80) {
                gb->apu.wave_channel.pulsed = true;
                /* DMG bug: wave RAM gets corrupted if the channel is retriggerred 1 cycle before the APU
                            reads from it. */
                if (!GB_is_cgb(gb) &&
                    gb->apu.is_active[GB_WAVE] &&
                    gb->apu.wave_channel.sample_countdown == 0) {
                    unsigned offset = ((gb->apu.wave_channel.current_sample_index + 1) >> 1) & 0xF;

                    /* This glitch varies between models and even specific instances:
                       DMG-B:     Most of them behave as emulated. A few behave differently.
                       SGB:       As far as I know, all tested instances behave as emulated.
                       MGB, SGB2: Most instances behave non-deterministically, a few behave as emulated.
                     
                       For DMG-B emulation I emulate the most common behavior, which blargg's tests expect (not my own DMG-B, which fails it)
                       For MGB emulation, I emulate my Game Boy Light, which happens to be deterministic.

                      Additionally, I believe DMGs, including those we behave differently than emulated,
                      are all deterministic. */
                    if (offset < 4 && gb->model != GB_MODEL_MGB) {
                        gb->io_registers[GB_IO_WAV_START] = gb->io_registers[GB_IO_WAV_START + offset];
                    }
                    else {
                        memcpy(gb->io_registers + GB_IO_WAV_START,
                               gb->io_registers + GB_IO_WAV_START + (offset & ~3),
                               4);
                    }
                }
                gb->apu.wave_channel.current_sample_index = 0;
                if (gb->apu.is_active[GB_WAVE] && gb->apu.wave_channel.sample_countdown == 0) {
                    gb->apu.wave_channel.current_sample_byte = gb->io_registers[GB_IO_WAV_START];
                }
                if (gb->apu.wave_channel.enable) {
                    gb->apu.is_active[GB_WAVE] = true;
                    update_sample(gb, GB_WAVE,
                                  (gb->apu.wave_channel.current_sample_byte >> 4) >> gb->apu.wave_channel.shift,
                                  0);
                }
                gb->apu.wave_channel.sample_countdown = (gb->apu.wave_channel.sample_length ^ 0x7FF) + 3;
                if (gb->apu.wave_channel.pulse_length == 0) {
                    gb->apu.wave_channel.pulse_length = 0x100;
                    gb->apu.wave_channel.length_enabled = false;
                }
                /* Note that we don't change the sample just yet! This was verified on hardware. */
            }

            /* APU glitch - if length is enabled while the DIV-divider's LSB is 1, tick the length once. */
            if (((value & 0x40) || (GB_is_cgb(gb) && gb->model <= GB_MODEL_CGB_B)) && // Current value is irrelevant on CGB-B and older
                !gb->apu.wave_channel.length_enabled &&
                (gb->apu.div_divider & 1) &&
                gb->apu.wave_channel.pulse_length) {
                gb->apu.wave_channel.pulse_length--;
                if (gb->apu.wave_channel.pulse_length == 0) {
                    if (value & 0x80) {
                        gb->apu.wave_channel.pulse_length = 0xFF;
                    }
                    else {
                        gb->apu.is_active[GB_WAVE] = false;
                        update_sample(gb, GB_WAVE, 0, 0);
                    }
                }
            }
            gb->apu.wave_channel.length_enabled = value & 0x40;

            break;

        /* Noise Channel */

        case GB_IO_NR41: {
            gb->apu.noise_channel.pulse_length = (0x40 - (value & 0x3F));
            break;
        }

        case GB_IO_NR42: {
            if ((value & 0xF8) == 0) {
                /* This disables the DAC */
                gb->io_registers[reg] = value;
                gb->apu.is_active[GB_NOISE] = false;
                update_sample(gb, GB_NOISE, 0, 0);
            }
            else if (gb->apu.is_active[GB_NOISE]) {
                nrx2_glitch(gb, &gb->apu.noise_channel.current_volume,
                            value, gb->io_registers[reg], &gb->apu.noise_channel.volume_countdown,
                            &gb->apu.noise_channel.envelope_clock);
                update_sample(gb, GB_NOISE,
                              gb->apu.noise_channel.current_lfsr_sample ?
                              gb->apu.noise_channel.current_volume : 0,
                              0);
            }
            break;
        }

        case GB_IO_NR43: {
            gb->apu.noise_channel.narrow = value & 8;
            uint16_t effective_counter = effective_channel4_counter(gb);
            bool old_bit = (effective_counter >> (gb->io_registers[GB_IO_NR43] >> 4)) & 1;
            gb->io_registers[GB_IO_NR43] = value;
            bool new_bit = (effective_counter >> (gb->io_registers[GB_IO_NR43] >> 4)) & 1;
            if (gb->apu.noise_channel.countdown_reloaded) {
                unsigned divisor = (gb->io_registers[GB_IO_NR43] & 0x07) << 2;
                if (!divisor) divisor = 2;
                if (gb->model > GB_MODEL_CGB_C) {
                    gb->apu.noise_channel.counter_countdown =
                    divisor + (divisor == 2? 0 : inline_const(uint8_t[], {2, 1, 0, 3})[(gb->apu.noise_channel.alignment) & 3]);
                }
                else {
                    gb->apu.noise_channel.counter_countdown =
                    divisor + (divisor == 2? 0 : inline_const(uint8_t[], {2, 1, 4, 3})[(gb->apu.noise_channel.alignment) & 3]);
                }
                gb->apu.noise_channel.delta = 0;
            }
            /* Step LFSR */
            if (new_bit && (!old_bit || gb->model <= GB_MODEL_CGB_C)) {
                if (gb->model <= GB_MODEL_CGB_C) {
                    bool previous_narrow = gb->apu.noise_channel.narrow;
                    gb->apu.noise_channel.narrow = true;
                    step_lfsr(gb, 0);
                    gb->apu.noise_channel.narrow = previous_narrow;
                }
                else {
                    step_lfsr(gb, 0);
                }
            }
            break;
        }

        case GB_IO_NR44: {
            if (value & 0x80) {
                gb->apu.noise_channel.envelope_clock.locked = false;
                gb->apu.noise_channel.envelope_clock.clock = false;
                if (!GB_is_cgb(gb) && (gb->apu.noise_channel.alignment & 3) != 0) {
                    gb->apu.noise_channel.dmg_delayed_start = 6;
                }
                else {
                    unsigned divisor = (gb->io_registers[GB_IO_NR43] & 0x07) << 2;
                    if (!divisor) divisor = 2;
                    gb->apu.noise_channel.delta = 0;
                    gb->apu.noise_channel.counter_countdown = divisor + 4;
                    if (divisor == 2) {
                        if (gb->model <= GB_MODEL_CGB_C) {
                            gb->apu.noise_channel.counter_countdown += gb->apu.lf_div;
                            if (!gb->cgb_double_speed) {
                                gb->apu.noise_channel.counter_countdown -= 1;
                            }
                        }
                        else {
                            gb->apu.noise_channel.counter_countdown += 1 - gb->apu.lf_div;
                        }
                    }
                    else {
                        if (gb->model <= GB_MODEL_CGB_C) {
                            gb->apu.noise_channel.counter_countdown += inline_const(uint8_t[], {2, 1, 4, 3})[gb->apu.noise_channel.alignment & 3];
                        }
                        else {
                            gb->apu.noise_channel.counter_countdown += inline_const(uint8_t[], {2, 1, 0, 3})[gb->apu.noise_channel.alignment & 3];
                        }
                        if (((gb->apu.noise_channel.alignment + 1) & 3) < 2) {
                            if ((gb->io_registers[GB_IO_NR43] & 0x07) == 1) {
                                gb->apu.noise_channel.counter_countdown -= 2;
                                gb->apu.noise_channel.delta = 2;
                            }
                            else {
                                gb->apu.noise_channel.counter_countdown -= 4;
                            }
                        }
                    }
                    
                    /* TODO: These are quite weird. Verify further */
                    if (gb->model <= GB_MODEL_CGB_C) {
                        if (gb->cgb_double_speed) {
                            if (!(gb->io_registers[GB_IO_NR43] & 0xF0) && (gb->io_registers[GB_IO_NR43] & 0x07)) {
                                 gb->apu.noise_channel.counter_countdown -= 1;
                            }
                            else if ((gb->io_registers[GB_IO_NR43] & 0xF0) && !(gb->io_registers[GB_IO_NR43] & 0x07)) {
                                gb->apu.noise_channel.counter_countdown += 1;
                            }
                        }
                        else {
                            gb->apu.noise_channel.counter_countdown -= 2;
                        }
                    }
   
                    gb->apu.noise_channel.current_volume = gb->io_registers[GB_IO_NR42] >> 4;

                    /* The volume changes caused by NRX4 sound start take effect instantly (i.e. the effect the previously
                     started sound). The playback itself is not instant which is why we don't update the sample for other
                     cases. */
                    if (gb->apu.is_active[GB_NOISE]) {
                        update_sample(gb, GB_NOISE,
                                      gb->apu.noise_channel.current_lfsr_sample ?
                                      gb->apu.noise_channel.current_volume : 0,
                                      0);
                    }
                    gb->apu.noise_channel.lfsr = 0;
                    gb->apu.noise_channel.current_lfsr_sample = false;
                    gb->apu.noise_channel.volume_countdown = gb->io_registers[GB_IO_NR42] & 7;

                    if (!gb->apu.is_active[GB_NOISE] && (gb->io_registers[GB_IO_NR42] & 0xF8) != 0) {
                        gb->apu.is_active[GB_NOISE] = true;
                        update_sample(gb, GB_NOISE, 0, 0);
                    }

                    if (gb->apu.noise_channel.pulse_length == 0) {
                        gb->apu.noise_channel.pulse_length = 0x40;
                        gb->apu.noise_channel.length_enabled = false;
                    }
                }
            }

            /* APU glitch - if length is enabled while the DIV-divider's LSB is 1, tick the length once. */
            if ((value & 0x40) &&
                !gb->apu.noise_channel.length_enabled &&
                (gb->apu.div_divider & 1) &&
                gb->apu.noise_channel.pulse_length) {
                gb->apu.noise_channel.pulse_length--;
                if (gb->apu.noise_channel.pulse_length == 0) {
                    if (value & 0x80) {
                        gb->apu.noise_channel.pulse_length = 0x3F;
                    }
                    else {
                        gb->apu.is_active[GB_NOISE] = false;
                        update_sample(gb, GB_NOISE, 0, 0);
                    }
                }
            }
            gb->apu.noise_channel.length_enabled = value & 0x40;
            break;
        }
    }
    gb->io_registers[reg] = value;
}

void GB_set_sample_rate(GB_gameboy_t *gb, unsigned sample_rate)
{
    gb->apu_output.sample_rate = sample_rate;
    if (sample_rate) {
        gb->apu_output.highpass_rate = pow(0.999958, GB_get_clock_rate(gb) / (double)sample_rate);
        gb->apu_output.max_cycles_per_sample = ceil(GB_get_clock_rate(gb) / 2.0 / sample_rate);
    }
    else {
        gb->apu_output.max_cycles_per_sample = 0x400;
    }
}

void GB_set_sample_rate_by_clocks(GB_gameboy_t *gb, double cycles_per_sample)
{

    if (cycles_per_sample == 0) {
        GB_set_sample_rate(gb, 0);
        return;
    }
    gb->apu_output.sample_rate = GB_get_clock_rate(gb) / cycles_per_sample * 2;
    gb->apu_output.highpass_rate = pow(0.999958, cycles_per_sample);
    gb->apu_output.max_cycles_per_sample = ceil(cycles_per_sample / 4);
}

unsigned GB_get_sample_rate(GB_gameboy_t *gb)
{
    return gb->apu_output.sample_rate;
}

void GB_apu_set_sample_callback(GB_gameboy_t *gb, GB_sample_callback_t callback)
{
    gb->apu_output.sample_callback = callback;
}

void GB_set_highpass_filter_mode(GB_gameboy_t *gb, GB_highpass_mode_t mode)
{
    gb->apu_output.highpass_mode = mode;
}

void GB_set_interference_volume(GB_gameboy_t *gb, double volume)
{
    gb->apu_output.interference_volume = volume;
}

typedef struct __attribute__((packed)) {
    uint32_t format_chunk; // = BE32('FORM')
    uint32_t size; // = BE32(file size - 8)
    uint32_t format; // = BE32('AIFC')
    
    uint32_t fver_chunk; // = BE32('FVER')
    uint32_t fver_size; // = BE32(4)
    uint32_t fver;
    
    uint32_t comm_chunk; // = BE32('COMM')
    uint32_t comm_size; // = BE32(0x18)
    
    uint16_t channels; // = BE16(2)
    uint32_t samples_per_channel; // = BE32(total number of samples / 2)
    uint16_t bit_depth; // = BE16(16)
    uint16_t frequency_exponent;
    uint64_t frequency_significand;
    uint32_t compression_type; // = 'NONE' (BE) or 'twos' (LE)
    uint16_t compression_name; // = 0
    
    uint32_t ssnd_chunk; // = BE32('SSND')
    uint32_t ssnd_size; // = BE32(length of samples - 8)
    uint32_t ssnd_offset; // = 0
    uint32_t ssnd_block; // = 0
} aiff_header_t;

typedef struct __attribute__((packed)) {
    uint32_t marker; // = BE32('RIFF')
    uint32_t size; // = LE32(file size - 8)
    uint32_t type; // = BE32('WAVE')
    
    uint32_t fmt_chunk; // = BE32('fmt ')
    uint32_t fmt_size; // = LE16(16)
    uint16_t format; // = LE16(1)
    uint16_t channels; // = LE16(2)
    uint32_t sample_rate; // = LE32(sample_rate)
    uint32_t byte_rate; // = LE32(sample_rate * 4)
    uint16_t frame_size;  // = LE32(4)
    uint16_t bit_depth; // = LE16(16)
    
    uint32_t data_chunk; // = BE32('data')
    uint32_t data_size; // = LE32(length of samples)
} wav_header_t;


int GB_start_audio_recording(GB_gameboy_t *gb, const char *path, GB_audio_format_t format)
{
    if (gb->apu_output.sample_rate == 0) {
        return EINVAL;
    }
    
    if (gb->apu_output.output_file) {
        GB_stop_audio_recording(gb);
    }
    gb->apu_output.output_file = fopen(path, "wb");
    if (!gb->apu_output.output_file) return errno;
    
    gb->apu_output.output_format = format;
    switch (format) {
        case GB_AUDIO_FORMAT_RAW:
            return 0;
        case GB_AUDIO_FORMAT_AIFF: {
            aiff_header_t header = {0,};
            if (fwrite(&header, sizeof(header), 1, gb->apu_output.output_file) != 1) {
                int ret = errno ?: EIO;
                fclose(gb->apu_output.output_file);
                gb->apu_output.output_file = NULL;
                return ret;
            }
            return 0;
        }
        case GB_AUDIO_FORMAT_WAV: {
            wav_header_t header = {0,};
            if (fwrite(&header, sizeof(header), 1, gb->apu_output.output_file) != 1) {
                int ret = errno ?: EIO;
                fclose(gb->apu_output.output_file);
                gb->apu_output.output_file = NULL;
                return ret;
            }
            return 0;
        }
        default:
            fclose(gb->apu_output.output_file);
            gb->apu_output.output_file = NULL;
            return EINVAL;
    }
}
int GB_stop_audio_recording(GB_gameboy_t *gb)
{
    if (!gb->apu_output.output_file) {
        int ret  = gb->apu_output.output_error ?: -1;
        gb->apu_output.output_error = 0;
        return ret;
    }
    gb->apu_output.output_error = 0;
    switch (gb->apu_output.output_format) {
        case GB_AUDIO_FORMAT_RAW:
            break;
        case GB_AUDIO_FORMAT_AIFF: {
            size_t file_size = ftell(gb->apu_output.output_file);
            size_t frames = (file_size - sizeof(aiff_header_t)) / sizeof(GB_sample_t);
            aiff_header_t header = {
                .format_chunk = BE32('FORM'),
                .size = BE32(file_size - 8),
                .format = BE32('AIFC'),
                
                .fver_chunk = BE32('FVER'),
                .fver_size = BE32(4),
                .fver = BE32(0xA2805140),
                
                .comm_chunk = BE32('COMM'),
                .comm_size = BE32(0x18),
                .channels = BE16(2),
                .samples_per_channel = BE32(frames),
                .bit_depth = BE16(16),
#ifdef GB_BIG_ENDIAN
                .compression_type = 'NONE',
#else
                .compression_type = 'twos',
#endif
                .compression_name = 0,
                .ssnd_chunk = BE32('SSND'),
                .ssnd_size = BE32(frames * sizeof(GB_sample_t) - 8),
                .ssnd_offset = 0,
                .ssnd_block = 0,
            };
            
            uint64_t significand = gb->apu_output.sample_rate;
            uint16_t exponent = 0x403E;
            while ((int64_t)significand > 0) {
                significand <<= 1;
                exponent--;
            }
            header.frequency_exponent = BE16(exponent);
            header.frequency_significand = BE64(significand);
            
            fseek(gb->apu_output.output_file, 0, SEEK_SET);
            if (fwrite(&header, sizeof(header), 1, gb->apu_output.output_file) != 1) {
                gb->apu_output.output_error = errno;
            }
            break;
        }
        case GB_AUDIO_FORMAT_WAV: {
            size_t file_size = ftell(gb->apu_output.output_file);
            size_t frames = (file_size - sizeof(wav_header_t)) / sizeof(GB_sample_t);
            wav_header_t header = {
                .marker = BE32('RIFF'),
                .size = LE32(file_size - 8),
                .type = BE32('WAVE'),
                
                .fmt_chunk = BE32('fmt '),
                .fmt_size = LE16(16),
                .format = LE16(1),
                .channels = LE16(2),
                .sample_rate = LE32(gb->apu_output.sample_rate),
                .byte_rate = LE32(gb->apu_output.sample_rate * 4),
                .frame_size = LE32(4),
                .bit_depth = LE16(16),
                
                .data_chunk = BE32('data'),
                .data_size = LE32(frames * sizeof(GB_sample_t)),
            };
            
            fseek(gb->apu_output.output_file, 0, SEEK_SET);
            if (fwrite(&header, sizeof(header), 1, gb->apu_output.output_file) != 1) {
                gb->apu_output.output_error = errno;
            }
            break;
        }
    }
    fclose(gb->apu_output.output_file);
    gb->apu_output.output_file = NULL;
    
    int ret  = gb->apu_output.output_error;
    gb->apu_output.output_error = 0;
    return ret;
}


void GB_set_channel_muted(GB_gameboy_t *gb, GB_channel_t channel, bool muted)
{
    assert(channel < GB_N_CHANNELS);
    gb->apu_output.channel_muted[channel] = muted;
}

bool GB_is_channel_muted(GB_gameboy_t *gb, GB_channel_t channel)
{
    return gb->apu_output.channel_muted[channel];
}

// Note: this intentionally does not check to see if the channel is muted.
uint8_t GB_get_channel_volume(GB_gameboy_t *gb, GB_channel_t channel)
{
    switch (channel) {
        case GB_SQUARE_1:
        case GB_SQUARE_2:
            return gb->apu.square_channels[channel].current_volume;

        case GB_WAVE:
            return inline_const(uint8_t[], {0xF, 8, 4, 0, 0})[gb->apu.wave_channel.shift];

        case GB_NOISE:
            return gb->apu.noise_channel.current_volume;

        default:
            return 0;
    }
}

uint8_t GB_get_channel_amplitude(GB_gameboy_t *gb, GB_channel_t channel)
{
    return gb->apu.is_active[channel] ? gb->apu.samples[channel] : 0;
}

uint16_t GB_get_channel_period(GB_gameboy_t *gb, GB_channel_t channel)
{
    switch (channel) {
        case GB_SQUARE_1:
        case GB_SQUARE_2:
            return gb->apu.square_channels[channel].sample_length;

        case GB_WAVE:
            return gb->apu.wave_channel.sample_length;

        case GB_NOISE:
            return (gb->io_registers[GB_IO_NR43] & 7) << (gb->io_registers[GB_IO_NR43] >> 4);

        default:
            return 0;
    }
}

// wave_table is a user allocated uint8_t[32] array
void GB_get_apu_wave_table(GB_gameboy_t *gb, uint8_t *wave_table)
{
    for (unsigned i = GB_IO_WAV_START; i <= GB_IO_WAV_END; i++) {
        wave_table[2 * (i - GB_IO_WAV_START)] = gb->io_registers[i] >> 4;
        wave_table[2 * (i - GB_IO_WAV_START) + 1] = gb->io_registers[i] & 0xF;
    }
}

bool GB_get_channel_edge_triggered(GB_gameboy_t *gb, GB_channel_t channel)
{
    bool edge_triggered = gb->apu_output.edge_triggered[channel];
    gb->apu_output.edge_triggered[channel] = false;
    return edge_triggered;
}

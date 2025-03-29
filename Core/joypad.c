#include "gb.h"
#include <assert.h>
#include <math.h>

static inline bool should_bounce(GB_gameboy_t *gb)
{
    // Bouncing is super rare on an AGS, so don't emulate it on GB_MODEL_AGB_B (when addeed)
    return !GB_is_sgb(gb) && !gb-> no_bouncing_emulation && !(gb->model & GB_MODEL_GBP_BIT) /*&& gb->model != GB_MODEL_AGB_B*/;
}

static inline uint16_t bounce_for_key(GB_gameboy_t *gb, GB_key_t key)
{
    if (gb->model > GB_MODEL_CGB_E) {
        // AGB are less bouncy
        return 0xBFF;
    }
    if (key == GB_KEY_START || key == GB_KEY_SELECT) {
        return 0x1FFF;
    }
    return 0xFFF;
}

static inline bool get_input(GB_gameboy_t *gb, uint8_t player, GB_key_t key)
{
    if (gb->use_faux_analog[player] && key <= GB_KEY_DOWN) {
        if (gb->keys[player][key]) return true;
        uint8_t pattern = 0;
        uint8_t index_in_pattern;
        switch (key) {
            // Most games only sample inputs in 30FPS, so we shift right once.
            case GB_KEY_RIGHT:
                if (gb->faux_analog_inputs[player].x <= 0) return false;
                pattern = gb->faux_analog_inputs[player].x - 1;
                index_in_pattern = gb->faux_analog_ticks;
                break;
            case GB_KEY_LEFT:
                if (gb->faux_analog_inputs[player].x >= 0) return false;
                pattern = -gb->faux_analog_inputs[player].x - 1;
                index_in_pattern = gb->faux_analog_ticks;
                break;
            case GB_KEY_UP:
                if (gb->faux_analog_inputs[player].y >= 0) return false;
                pattern = -gb->faux_analog_inputs[player].y - 1;
                index_in_pattern = gb->faux_analog_ticks + 2;
                break;
            case GB_KEY_DOWN:
                if (gb->faux_analog_inputs[player].y <= 0) return false;
                pattern = gb->faux_analog_inputs[player].y - 1;
                index_in_pattern = gb->faux_analog_ticks + 2;
                break;
            nodefault;
        }
        if (pattern == 7) return true;
        /* Dithering pattern */
        static const uint8_t patterns[] = {
            0x1,
            0x11,
            0x94,
            0x55,
            0x6d,
            0x77,
            0x7f
        };
        return patterns[pattern] & (1 << (index_in_pattern & 6));
    }
    if (player != 0) {
        return gb->keys[player][key];
    }
    bool ret = gb->keys[player][key];
    
    if (likely(gb->key_bounce_timing[key] == 0)) return ret;
    if (likely((gb->key_bounce_timing[key] & 0x3FF) > 0x300)) return ret;
    uint16_t semi_random = ((((key << 5) + gb->div_counter) * 17) ^ ((gb->apu.apu_cycles + gb->display_cycles) * 13));
    semi_random >>= 3;
    if (semi_random < gb->key_bounce_timing[key]) {
        ret ^= true;
    }
    return ret;
}

void GB_update_joyp(GB_gameboy_t *gb)
{
    if (gb->model & GB_MODEL_NO_SFC_BIT) return;
    
    uint8_t key_selection = 0;
    uint8_t previous_state = 0;

    previous_state = gb->io_registers[GB_IO_JOYP] & 0xF;
    key_selection = (gb->io_registers[GB_IO_JOYP] >> 4) & 3;
    gb->io_registers[GB_IO_JOYP] &= 0xF0;
    uint8_t current_player = gb->sgb? gb->sgb->current_player : 0;
    switch (key_selection) {
        case 3:
            if (gb->sgb && gb->sgb->player_count > 1) {
                gb->io_registers[GB_IO_JOYP] |= 0xF - current_player;
            }
            else {
                /* Nothing is wired, all up */
                gb->io_registers[GB_IO_JOYP] |= 0x0F;
            }
            break;

        case 2:
            /* Direction keys */
            for (uint8_t i = 0; i < 4; i++) {
                gb->io_registers[GB_IO_JOYP] |= (!get_input(gb, current_player, i)) << i;
            }
            /* Forbid pressing two opposing keys, this breaks a lot of games; even if it's somewhat possible. */
            if (likely(!gb->illegal_inputs_allowed)) {
                if (!(gb->io_registers[GB_IO_JOYP] & 1)) {
                    gb->io_registers[GB_IO_JOYP] |= 2;
                }
                if (!(gb->io_registers[GB_IO_JOYP] & 4)) {
                    gb->io_registers[GB_IO_JOYP] |= 8;
                }
            }
            break;

        case 1:
            /* Other keys */
            for (uint8_t i = 0; i < 4; i++) {
                gb->io_registers[GB_IO_JOYP] |= (!get_input(gb, current_player, i + 4)) << i;
            }
            break;

        case 0:
            for (uint8_t i = 0; i < 4; i++) {
                gb->io_registers[GB_IO_JOYP] |= (!(get_input(gb, current_player, i) || get_input(gb, current_player, i + 4))) << i;
            }
            break;

        nodefault;
    }
    
    // TODO: Implement the lame anti-debouncing mechanism as seen on the DMG schematics
    if (previous_state & ~(gb->io_registers[GB_IO_JOYP] & 0xF)) {
        if (!(gb->io_registers[GB_IO_IF] & 0x10)) {
            gb->joyp_accessed = true;
            gb->io_registers[GB_IO_IF] |= 0x10;
        }
    }
    
    gb->io_registers[GB_IO_JOYP] |= 0xC0;
}

void GB_icd_set_joyp(GB_gameboy_t *gb, uint8_t value)
{
    uint8_t previous_state = gb->io_registers[GB_IO_JOYP] & 0xF;
    gb->io_registers[GB_IO_JOYP] &= 0xF0;
    gb->io_registers[GB_IO_JOYP] |= value & 0xF;
    
    if (previous_state & ~(gb->io_registers[GB_IO_JOYP] & 0xF)) {
        if (!(gb->io_registers[GB_IO_IF] & 0x10)) {
            gb->joyp_accessed = true;
            gb->io_registers[GB_IO_IF] |= 0x10;
        }
    }
    gb->io_registers[GB_IO_JOYP] |= 0xC0;
}

void GB_set_key_state(GB_gameboy_t *gb, GB_key_t index, bool pressed)
{
    assert(index >= 0 && index < GB_KEY_MAX);
    if (should_bounce(gb) && pressed != gb->keys[0][index]) {
        gb->joypad_is_stable = false;
        gb->key_bounce_timing[index] = bounce_for_key(gb, index);
    }
    gb->keys[0][index] = pressed;
    GB_update_joyp(gb);
}

void GB_set_key_state_for_player(GB_gameboy_t *gb, GB_key_t index, unsigned player, bool pressed)
{
    assert(index >= 0 && index < GB_KEY_MAX);
    assert(player < 4);
    if (should_bounce(gb) && pressed != gb->keys[player][index]) {
        gb->joypad_is_stable = false;
        gb->key_bounce_timing[index] = bounce_for_key(gb, index);
    }
    gb->keys[player][index] = pressed;
    GB_update_joyp(gb);
}

void GB_set_key_mask(GB_gameboy_t *gb, GB_key_mask_t mask)
{
    for (unsigned i = 0; i < GB_KEY_MAX; i++) {
        bool pressed = mask & (1 << i);
        if (should_bounce(gb) && pressed != gb->keys[0][i]) {
            gb->joypad_is_stable = false;
            gb->key_bounce_timing[i] = bounce_for_key(gb, i);
        }
        gb->keys[0][i] = pressed;
    }
    
    GB_update_joyp(gb);
}

void GB_set_key_mask_for_player(GB_gameboy_t *gb, GB_key_mask_t mask, unsigned player)
{
    for (unsigned i = 0; i < GB_KEY_MAX; i++) {
        bool pressed = mask & (1 << i);
        if (should_bounce(gb) && pressed != gb->keys[player][i]) {
            gb->joypad_is_stable = false;
            gb->key_bounce_timing[i] = bounce_for_key(gb, i);
        }
        gb->keys[player][i] = pressed;
    }
    
    GB_update_joyp(gb);
}

void GB_joypad_run(GB_gameboy_t *gb, unsigned cycles)
{
    if (gb->joypad_is_stable) return;
    bool should_update_joyp = gb->use_faux_analog[gb->sgb? gb->sgb->current_player : 0];
    gb->joypad_is_stable = true;
    if (gb->joyp_switching_delay) {
        gb->joypad_is_stable = false;
        if (gb->joyp_switching_delay > cycles) {
            gb->joyp_switching_delay -= cycles;
        }
        else {
            gb->joyp_switching_delay = 0;
            gb->io_registers[GB_IO_JOYP] = (gb->joyp_switch_value & 0xF0) | (gb->io_registers[GB_IO_JOYP] & 0x0F);
            should_update_joyp = true;
        }
    }
    
    for (unsigned i = 0; i < GB_KEY_MAX; i++) {
        if (gb->key_bounce_timing[i]) {
            gb->joypad_is_stable = false;
            should_update_joyp = true;
            if (gb->key_bounce_timing[i] > cycles) {
                gb->key_bounce_timing[i] -= cycles;
            }
            else {
                gb->key_bounce_timing[i] = 0;
            }
        }
    }
    
    if (should_update_joyp) {
        GB_update_joyp(gb);
    }
}

bool GB_get_joyp_accessed(GB_gameboy_t *gb)
{
    return gb->joyp_accessed;
}

void GB_clear_joyp_accessed(GB_gameboy_t *gb)
{
    gb->joyp_accessed = false;
}

void GB_set_allow_illegal_inputs(GB_gameboy_t *gb, bool allow)
{
    gb->illegal_inputs_allowed = allow;
}

void GB_set_emulate_joypad_bouncing(GB_gameboy_t *gb, bool emulate)
{
    gb->no_bouncing_emulation = !emulate;
}

void GB_set_update_input_hint_callback(GB_gameboy_t *gb, GB_update_input_hint_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->update_input_hint_callback = callback;
}

void GB_set_use_faux_analog_inputs(GB_gameboy_t *gb, unsigned player, bool use)
{
    if (gb->use_faux_analog[player] == use) return;
    gb->use_faux_analog[player] = use;
    for (GB_key_t key = GB_KEY_RIGHT; key <= GB_KEY_DOWN; key++) {
        gb->keys[player][key] = false;
    }
    GB_update_joyp(gb);
}

void GB_set_faux_analog_inputs(GB_gameboy_t *gb, unsigned player, double x, double y)
{

    if (x > 1) x = 1;
    else if (x < -1) x = -1;
    if (y > 1) y = 1;
    else if (y < -1) y = -1;
    
    double abs_x = fabs(x), abs_y = fabs(y);
    if (abs_x <= 0.1) x = abs_x = 0;
    if (abs_y <= 0.1) y = abs_y = 0;
    if (!x && !y) {
        gb->faux_analog_inputs[player].x = gb->faux_analog_inputs[player].y = 0;
    }
    else {
        if (x) {
            abs_x = (abs_x - 0.1) / 0.9;
            x = x > 0? abs_x : -abs_x;
        }
        if (y) {
            abs_y = (abs_y - 0.1) / 0.9;
            y = y > 0? abs_y : -abs_y;
        }
        // Square the circle
        double distance = MIN(sqrt(x * x + y * y), 1);
        double multiplier = 8 * distance / MAX(abs_x, abs_y);
        
        gb->faux_analog_inputs[player].x = round(x * multiplier);
        gb->faux_analog_inputs[player].y = round(y * multiplier);
    }
    GB_update_joyp(gb);
}

void GB_update_faux_analog(GB_gameboy_t *gb)
{
    gb->faux_analog_ticks++;
    for (unsigned i = 0; i < 4; i++) {
        if (!gb->use_faux_analog[i]) continue;
        if ((gb->faux_analog_inputs[i].x != 0 &&
             gb->faux_analog_inputs[i].x != 8 &&
             gb->faux_analog_inputs[i].x != -8) ||
            (gb->faux_analog_inputs[i].y != 0 &&
             gb->faux_analog_inputs[i].y != 8 &&
             gb->faux_analog_inputs[i].y != -8)) {
            gb->joypad_is_stable = false;
            return;
        }
    }
}

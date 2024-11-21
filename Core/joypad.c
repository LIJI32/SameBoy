#include "gb.h"
#include <assert.h>

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
    bool should_update_joyp = false;
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
    gb->update_input_hint_callback = callback;
}

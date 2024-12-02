#pragma once
#include "defs.h"
#include <stdbool.h>

typedef enum {
    GB_KEY_RIGHT,
    GB_KEY_LEFT,
    GB_KEY_UP,
    GB_KEY_DOWN,
    GB_KEY_A,
    GB_KEY_B,
    GB_KEY_SELECT,
    GB_KEY_START,
    GB_KEY_MAX
} GB_key_t;

typedef enum {
    GB_KEY_RIGHT_MASK = 1 << GB_KEY_RIGHT,
    GB_KEY_LEFT_MASK = 1 << GB_KEY_LEFT,
    GB_KEY_UP_MASK = 1 << GB_KEY_UP,
    GB_KEY_DOWN_MASK = 1 << GB_KEY_DOWN,
    GB_KEY_A_MASK = 1 << GB_KEY_A,
    GB_KEY_B_MASK = 1 << GB_KEY_B,
    GB_KEY_SELECT_MASK = 1 << GB_KEY_SELECT,
    GB_KEY_START_MASK = 1 << GB_KEY_START,
} GB_key_mask_t;

typedef void (*GB_update_input_hint_callback_t)(GB_gameboy_t *gb);

void GB_set_key_state(GB_gameboy_t *gb, GB_key_t index, bool pressed);
void GB_set_key_state_for_player(GB_gameboy_t *gb, GB_key_t index, unsigned player, bool pressed);
void GB_set_key_mask(GB_gameboy_t *gb, GB_key_mask_t mask);
void GB_set_key_mask_for_player(GB_gameboy_t *gb, GB_key_mask_t mask, unsigned player);
void GB_icd_set_joyp(GB_gameboy_t *gb, uint8_t value);
bool GB_get_joyp_accessed(GB_gameboy_t *gb);
void GB_clear_joyp_accessed(GB_gameboy_t *gb);
void GB_set_allow_illegal_inputs(GB_gameboy_t *gb, bool allow);
void GB_set_emulate_joypad_bouncing(GB_gameboy_t *gb, bool emulate);
void GB_set_update_input_hint_callback(GB_gameboy_t *gb, GB_update_input_hint_callback_t callback);

#ifdef GB_INTERNAL
internal void GB_update_joyp(GB_gameboy_t *gb);
internal void GB_joypad_run(GB_gameboy_t *gb, unsigned cycles);
#endif

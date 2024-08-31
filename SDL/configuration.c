#include "configuration.h"

configuration_t configuration =
{
    .keys = {
        SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_LEFT,
        SDL_SCANCODE_UP,
        SDL_SCANCODE_DOWN,
        SDL_SCANCODE_X,
        SDL_SCANCODE_Z,
        SDL_SCANCODE_BACKSPACE,
        SDL_SCANCODE_RETURN,
        SDL_SCANCODE_SPACE
    },
    .keys_2 = {
        SDL_SCANCODE_TAB,
        SDL_SCANCODE_LSHIFT,
    },
    .joypad_configuration = {
        13,
        14,
        11,
        12,
        0,
        1,
        9,
        8,
        10,
        4,
        -1,
        5,
        // The rest are unmapped by default
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    },
    .joypad_axises = {
        0,
        1,
    },
    .color_correction_mode = GB_COLOR_CORRECTION_MODERN_BALANCED,
    .highpass_mode = GB_HIGHPASS_ACCURATE,
    .scaling_mode = GB_SDL_SCALING_INTEGER_FACTOR,
    .blending_mode = GB_FRAME_BLENDING_MODE_ACCURATE,
    .rewind_length = 60 * 2,
    .model = MODEL_AUTO,
    .volume = 100,
    .rumble_mode = GB_RUMBLE_CARTRIDGE_ONLY,
    .default_scale = 2,
    .color_temperature = 10,
    .cgb_revision = GB_MODEL_CGB_E - GB_MODEL_CGB_0,
    .dmg_palette = 1, // Replacing the old default (0) as of 0.15.2
    .agb_revision = GB_MODEL_AGB_A,
};

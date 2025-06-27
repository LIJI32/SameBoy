#pragma once

#include <SDL.h>
#include <Core/gb.h>
#include "shader.h"

enum scaling_mode {
    GB_SDL_SCALING_ENTIRE_WINDOW,
    GB_SDL_SCALING_KEEP_RATIO,
    GB_SDL_SCALING_INTEGER_FACTOR,
    GB_SDL_SCALING_MAX,
};

enum {
    GB_CONF_KEYS_RIGHT = GB_KEY_RIGHT,
    GB_CONF_KEYS_LEFT = GB_KEY_LEFT,
    GB_CONF_KEYS_UP = GB_KEY_UP,
    GB_CONF_KEYS_DOWN = GB_KEY_DOWN,
    GB_CONF_KEYS_A = GB_KEY_A,
    GB_CONF_KEYS_B = GB_KEY_B,
    GB_CONF_KEYS_SELECT = GB_KEY_SELECT,
    GB_CONF_KEYS_START = GB_KEY_START,
    GB_CONF_KEYS_TURBO,
    GB_CONF_KEYS_COUNT,
};

enum {
    GB_CONF_KEYS2_REWIND,
    GB_CONF_KEYS2_UNDERCLOCK,
    GB_CONF_KEYS2_RAPID_A,
    GB_CONF_KEYS2_RAPID_B,
    GB_CONF_KEYS2_COUNT = 32,
};

typedef enum {
    JOYPAD_BUTTON_RIGHT,
    JOYPAD_BUTTON_LEFT,
    JOYPAD_BUTTON_UP,
    JOYPAD_BUTTON_DOWN,
    JOYPAD_BUTTON_A,
    JOYPAD_BUTTON_B,
    JOYPAD_BUTTON_SELECT,
    JOYPAD_BUTTON_START,
    JOYPAD_BUTTON_MENU,
    JOYPAD_BUTTON_TURBO,
    JOYPAD_BUTTON_REWIND,
    JOYPAD_BUTTON_SLOW_MOTION,
    JOYPAD_BUTTON_HOTKEY_1,
    JOYPAD_BUTTON_HOTKEY_2,
    JOYPAD_BUTTON_RAPID_A,
    JOYPAD_BUTTON_RAPID_B,
    JOYPAD_BUTTONS_MAX
} joypad_button_t;

typedef enum {
    JOYPAD_AXISES_X,
    JOYPAD_AXISES_Y,
    JOYPAD_AXISES_MAX
} joypad_axis_t;

typedef enum {
    HOTKEY_NONE,
    HOTKEY_PAUSE,
    HOTKEY_MUTE,
    HOTKEY_RESET,
    HOTKEY_QUIT,
    HOTKEY_SAVE_STATE_1,
    HOTKEY_LOAD_STATE_1,
    HOTKEY_SAVE_STATE_2,
    HOTKEY_LOAD_STATE_2,
    HOTKEY_SAVE_STATE_3,
    HOTKEY_LOAD_STATE_3,
    HOTKEY_SAVE_STATE_4,
    HOTKEY_LOAD_STATE_4,
    HOTKEY_SAVE_STATE_5,
    HOTKEY_LOAD_STATE_5,
    HOTKEY_SAVE_STATE_6,
    HOTKEY_LOAD_STATE_6,
    HOTKEY_SAVE_STATE_7,
    HOTKEY_LOAD_STATE_7,
    HOTKEY_SAVE_STATE_8,
    HOTKEY_LOAD_STATE_8,
    HOTKEY_SAVE_STATE_9,
    HOTKEY_LOAD_STATE_9,
    HOTKEY_SAVE_STATE_10,
    HOTKEY_LOAD_STATE_10,
    HOTKEY_MAX = HOTKEY_LOAD_STATE_10,
} hotkey_action_t;

typedef struct {
    SDL_Scancode keys[GB_CONF_KEYS_COUNT];
    GB_color_correction_mode_t color_correction_mode;
    enum scaling_mode scaling_mode;
    uint8_t blending_mode;
    
    GB_highpass_mode_t highpass_mode;
    
    bool _deprecated_div_joystick;
    bool _deprecated_flip_joystick_bit_1;
    bool _deprecated_swap_joysticks_bits_1_and_2;
    
    char filter[32];
    enum {
        MODEL_DMG,
        MODEL_CGB,
        MODEL_AGB,
        MODEL_SGB,
        MODEL_MGB,
        MODEL_AUTO,
        MODEL_MAX,
    } model;
    
    /* v0.11 */
    uint32_t rewind_length;
    SDL_Scancode keys_2[GB_CONF_KEYS2_COUNT]; /* Rewind and underclock, + padding for the future */
    uint8_t joypad_configuration[32]; /* 14 Keys + padding for the future*/;
    uint8_t joypad_axises[JOYPAD_AXISES_MAX];
    
    /* v0.12 */
    enum {
        SGB_NTSC,
        SGB_PAL,
        SGB_2,
        SGB_MAX
    } sgb_revision;
    
    /* v0.13 */
    uint8_t dmg_palette;
    GB_border_mode_t border_mode;
    uint8_t volume;
    GB_rumble_mode_t rumble_mode;
    
    uint8_t default_scale;
    
    /* v0.14 */
    unsigned padding;
    uint8_t color_temperature;
    char bootrom_path[4096];
    uint8_t interference_volume;
    GB_rtc_mode_t rtc_mode;
    
    /* v0.14.4 */
    bool osd;
    
    struct __attribute__((packed, aligned(4))) {
        /* v0.15 */
        bool allow_mouse_controls;
        uint8_t cgb_revision;
        /* v0.15.1 */
        char audio_driver[16];
        /* v0.15.2 */
        bool allow_background_controllers;
        bool gui_palette_enabled; // Change the GUI palette only once the user changed the DMG palette
        char dmg_palette_name[25];
        hotkey_action_t hotkey_actions[2];
        uint16_t agb_revision;
        
        /* v1.0 */
        bool windows_associations_prompted; // Windows only
        
        /* v1.0.1 */
        bool disable_rounded_corners; // Windows only
        bool use_faux_analog_inputs;
        
        /* v1.0.2 */
        int8_t vsync_mode;
    };
} configuration_t;

extern configuration_t configuration;

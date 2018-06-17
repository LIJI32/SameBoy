#ifndef gui_h
#define gui_h

#include <SDL2/SDL.h>
#include <Core/gb.h>
#include <stdbool.h> 
#include "shader.h"


extern SDL_Window *window;
extern SDL_Renderer *renderer;
extern SDL_Texture *texture;
extern SDL_PixelFormat *pixel_format;
extern shader_t shader;

enum scaling_mode {
    GB_SDL_SCALING_ENTIRE_WINDOW,
    GB_SDL_SCALING_KEEP_RATIO,
    GB_SDL_SCALING_INTEGER_FACTOR,
    GB_SDL_SCALING_MAX,
};


enum pending_command {
    GB_SDL_NO_COMMAND,
    GB_SDL_SAVE_STATE_COMMAND,
    GB_SDL_LOAD_STATE_COMMAND,
    GB_SDL_RESET_COMMAND,
    GB_SDL_NEW_FILE_COMMAND,
    GB_SDL_QUIT_COMMAND,
};

extern enum pending_command pending_command;
extern unsigned command_parameter;
typedef struct {
    SDL_Scancode keys[9];
    GB_color_correction_mode_t color_correction_mode;
    enum scaling_mode scaling_mode;
    bool blend_frames;
    
    GB_highpass_mode_t highpass_mode;
    
    bool div_joystick;
    bool flip_joystick_bit_1;
    bool swap_joysticks_bits_1_and_2;
    
    char filter[32];
    enum {
        MODEL_DMG,
        MODEL_CGB,
        MODEL_AGB,
        MODEL_MAX,
    } model;
} configuration_t;

extern configuration_t configuration;

void update_viewport(void);
void run_gui(bool is_running);
unsigned fix_joypad_button(unsigned button);
unsigned fix_joypad_axis(unsigned axis);
void render_texture(void *pixels, void *previous);

#endif

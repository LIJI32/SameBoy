#pragma once

#include <SDL.h>
#include <Core/gb.h>
#include <stdbool.h> 
#include "shader.h"
#include "configuration.h"

#define JOYSTICK_HIGH 0x4000
#define JOYSTICK_LOW 0x3800

#ifdef __APPLE__
#define MODIFIER KMOD_GUI
#else
#define MODIFIER KMOD_CTRL
#endif

extern GB_gameboy_t gb;

extern SDL_Window *window;
extern SDL_Renderer *renderer;
extern SDL_Texture *texture;
extern SDL_PixelFormat *pixel_format;
extern SDL_Joystick *joystick;
extern SDL_Haptic *haptic;
extern shader_t shader;

enum pending_command {
    GB_SDL_NO_COMMAND,
    GB_SDL_SAVE_STATE_COMMAND,
    GB_SDL_LOAD_STATE_COMMAND,
    GB_SDL_RESET_COMMAND,
    GB_SDL_NEW_FILE_COMMAND,
    GB_SDL_QUIT_COMMAND,
    GB_SDL_LOAD_STATE_FROM_FILE_COMMAND,
    GB_SDL_CART_SWAP_COMMAND,
    GB_SDL_DEBUGGER_INTERRUPT_COMMAND,
#ifdef _WIN32
    GB_SDL_HIDE_DEBUGGER_COMMAND,
#endif
};

#define GB_SDL_DEFAULT_SCALE_MAX 8

extern enum pending_command pending_command;
extern unsigned command_parameter;
extern char *dropped_state_file;
extern bool screen_manually_resized;

void update_viewport(void);
void run_gui(bool is_running);
void render_texture(void *pixels, void *previous);
void connect_joypad(void);

joypad_button_t get_joypad_button(uint8_t physical_button);
joypad_axis_t get_joypad_axis(uint8_t physical_axis);

static SDL_Scancode event_hotkey_code(SDL_Event *event)
{
    if (event->key.keysym.sym >= SDLK_a && event->key.keysym.sym < SDLK_z) {
        return SDL_SCANCODE_A + event->key.keysym.sym - SDLK_a;
    }
    
    return event->key.keysym.scancode;
}

void draw_text(uint32_t *buffer, unsigned width, unsigned height, unsigned x, signed y, const char *string, uint32_t color, uint32_t border, bool is_osd);
void show_osd_text(const char *text);
extern const char *osd_text;
extern unsigned osd_countdown;
extern unsigned osd_text_lines;
void convert_mouse_coordinates(signed *x, signed *y);
const GB_palette_t *current_dmg_palette(void);
void update_swap_interval(void);

#ifdef _WIN32
void configure_window_corners(void);
#endif

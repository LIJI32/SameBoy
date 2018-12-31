#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "gui.h"
#include "font.h"
#include "wide_gb.h"

static const SDL_Color gui_palette[4] = {{8, 24, 16,}, {57, 97, 57,}, {132, 165, 99}, {198, 222, 140}};
static uint32_t gui_palette_native[4];

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture *screen_texture = NULL;
SDL_PixelFormat *pixel_format = NULL;
enum pending_command pending_command;
unsigned command_parameter;

#ifdef __APPLE__
#define MODIFIER_NAME " " CMD_STRING
#else
#define MODIFIER_NAME CTRL_STRING
#endif

shader_t shader;
SDL_Rect viewport;
wide_gb wgb;
SDL_Texture *wgb_textures[WIDE_GB_MAX_TILES];

struct scale {
    double x;
    double y;
};
struct scale compute_viewport_scale(void);

// TODO:
// - Move updated tiles to WGB_*
// - zoom in / out
// - Scenes detection
// - Save/restore backgrounds on the disk

SDL_Texture* sdl_texture_for_wgb_tile(int tile_index)
{
    SDL_Texture *texture = wgb_textures[tile_index];
    if (texture == NULL) {
        texture = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_STREAMING, 160, 144);
        if (texture == NULL) {
            fprintf(stderr, "Failed to create SDL texture for tile %i\n", tile_index);
        }
        wgb_textures[tile_index] = texture;
    }
    return texture;
}

SDL_Rect screen_rect_to_window(SDL_Rect screen_rect)
{
    SDL_Point viewport_offset = { viewport.x, viewport.y };
    struct scale viewport_scale = compute_viewport_scale();

    SDL_Rect result = screen_rect;
    result = WGB_scale_rect(result, viewport_scale.x, viewport_scale.y);
    result = WGB_offset_rect(result, viewport_offset);
    return result;
}

void render_texture(void *pixels, void *previous, void *background_pixels)
{
    /*---------------------------- Update Tiles ------------------------------*/

    // Update WideGB tiles with the pixels currently visible on screen
    uint32_t *source_pixels = background_pixels ? background_pixels : pixels;
    WGB_write_screen(&wgb, source_pixels);

    /*---------------------------- Update Tiles textures ---------------------*/

    // For each tile…
    size_t tiles_count = WGB_tiles_count(&wgb);
    for (int i = 0; i < tiles_count; i++) {
        // if the tile pixel buffer has been updated…
        WGB_tile *tile = WGB_tile_at_index(&wgb, i);
        if (tile->dirty) {
            // update the associated SDL texture.
            SDL_Texture *tile_texture = sdl_texture_for_wgb_tile(i);
            SDL_UpdateTexture(tile_texture, NULL, tile->pixel_buffer, 160 * sizeof (uint32_t));
        }
    }

    /*----------------------------  Rendering --------------------------------*/

    if (renderer) {
        // 1. Clear the surface
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0xff);
        SDL_RenderClear(renderer);

        // 2. Display each WideGB tile
        size_t tiles_count = WGB_tiles_count(&wgb);
        for (int i = 0; i < tiles_count; i++) {
            WGB_tile *tile = WGB_tile_at_index(&wgb, i);
            SDL_Texture *tile_texture = sdl_texture_for_wgb_tile(i);
            SDL_Rect tile_rect = WGB_rect_for_tile(&wgb, tile);
            SDL_Rect tile_rect_in_window = screen_rect_to_window(tile_rect);

            SDL_RenderCopy(renderer, tile_texture, NULL, &tile_rect_in_window);
#if WIDE_GB_DEBUG
            SDL_SetRenderDrawColor(renderer, 0x60, 0x60, 0x60, 0xff);
            SDL_RenderDrawRect(renderer, &tile_rect_in_window);
#endif
        }

        // 3. Draw opaquely the background portions non-overlapped by the GB window
        //
        // +-----------------+
        // |                 |
        // |   part1         |
        // |                 |
        // |.......+---------|
        // | part2 |  window |
        // +-----------------+

        if (pixels) {
            SDL_UpdateTexture(screen_texture, NULL, pixels, 160 * sizeof (uint32_t));
        }

        SDL_Rect window_rect = wgb.window_enabled ? WGB_get_window_rect(&wgb) : (SDL_Rect){ 160, 144, 0, 0 };
        SDL_Rect part1_rect = { 0, 0, 160, window_rect.y };
        SDL_Rect part2_rect = { 0, window_rect.y, window_rect.x, 144 - window_rect.y };
        SDL_Rect part1_rect_in_window = screen_rect_to_window(part1_rect);
        SDL_Rect part2_rect_in_window = screen_rect_to_window(part2_rect);
        SDL_SetTextureBlendMode(screen_texture, SDL_BLENDMODE_NONE);
        SDL_RenderCopy(renderer, screen_texture, &part1_rect, &part1_rect_in_window);
        SDL_RenderCopy(renderer, screen_texture, &part2_rect, &part2_rect_in_window);

#if WIDE_GB_DEBUG
        SDL_SetRenderDrawColor(renderer, 0xff, 0x0, 0x0, 0x7f);
        SDL_RenderDrawRect(renderer, &viewport);
#endif
        // 4. Draw the background part overlappy by the window with a transluscency effect
        if (wgb.window_enabled) {
            SDL_Rect window_rect_in_window = screen_rect_to_window(window_rect);
            SDL_SetTextureBlendMode(screen_texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(screen_texture, 170);
            SDL_RenderCopy(renderer, screen_texture, &window_rect, &window_rect_in_window);
        }

        // 5. Finish rendering
        SDL_RenderPresent(renderer);
    }
    else {
        // TODO: implement WideGB OpenGL rendering
        static void *_pixels = NULL;
        if (pixels) {
            _pixels = pixels;
        }
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        render_bitmap_with_shader(&shader, _pixels, previous, viewport.x, viewport.y, viewport.w, viewport.h);
        SDL_GL_SwapWindow(window);
    }
}

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
    },
    .joypad_axises = {
        0,
        1,
    },
    .color_correction_mode = GB_COLOR_CORRECTION_EMULATE_HARDWARE,
    .highpass_mode = GB_HIGHPASS_ACCURATE,
    .scaling_mode = GB_SDL_SCALING_INTEGER_FACTOR,
    .blend_frames = true,
    .rewind_length = 60 * 2,
    .model = MODEL_CGB
};


static const char *help[] ={
"Drop a GB or GBC ROM\n"
"file to play.\n"
"\n"
"Keyboard Shortcuts:\n"
" Open Menu:        Escape\n"
" Reset:             " MODIFIER_NAME "+R\n"
" Pause:             " MODIFIER_NAME "+P\n"
" Save state:    " MODIFIER_NAME "+(0-9)\n"
" Load state:  " MODIFIER_NAME "+" SHIFT_STRING "+(0-9)\n"
" Toggle Fullscreen  " MODIFIER_NAME "+F\n"
#ifdef __APPLE__
" Mute/Unmute:     " MODIFIER_NAME "+" SHIFT_STRING "+M\n"
#else
" Mute/Unmute:       " MODIFIER_NAME "+M\n"
#endif
" Break Debugger:    " CTRL_STRING "+C"
};

struct scale compute_viewport_scale(void)
{
    struct scale scale;

    int win_width, win_height;
    SDL_GL_GetDrawableSize(window, &win_width, &win_height);
    scale.x = win_width / 160.0;
    scale.y = win_height / 144.0;

    if (configuration.scaling_mode == GB_SDL_SCALING_INTEGER_FACTOR) {
        scale.x = (int)(scale.x);
        scale.y = (int)(scale.y);
    }
    
    if (configuration.scaling_mode != GB_SDL_SCALING_ENTIRE_WINDOW) {
        if (scale.x > scale.y) {
            scale.x = scale.y;
        }
        else {
            scale.y = scale.x;
        }
    }

    return scale;
}

void update_viewport(void)
{
    int win_width, win_height;
    SDL_GL_GetDrawableSize(window, &win_width, &win_height);

    struct scale scale = compute_viewport_scale();
    unsigned new_width = 160 * scale.x;
    unsigned new_height = 144 * scale.y;

    viewport = (SDL_Rect){(win_width  - new_width) / 2, (win_height - new_height) /2,
        new_width, new_height};
    
    if (renderer) {
        #if !WIDE_GB_ENABLED
        SDL_RenderSetClipRect(renderer, &viewport);
        #endif
    }
    else {
        glViewport(viewport.x, viewport.y, viewport.w, viewport.h);
    }
}

/* Does NOT check for bounds! */
static void draw_char(uint32_t *buffer, unsigned char ch, uint32_t color)
{
    if (ch < ' ' || ch > font_max) {
        ch = '?';
    }
    
    uint8_t *data = &font[(ch - ' ') * GLYPH_WIDTH * GLYPH_HEIGHT];
    
    for (unsigned y = GLYPH_HEIGHT; y--;) {
        for (unsigned x = GLYPH_WIDTH; x--;) {
            if (*(data++)) {
                (*buffer) = color;
            }
            buffer++;
        }
        buffer += 160 - GLYPH_WIDTH;
    }
}

static void draw_unbordered_text(uint32_t *buffer, unsigned x, unsigned y, const char *string, uint32_t color)
{
    unsigned orig_x = x;
    while (*string) {
        if (*string == '\n') {
            x = orig_x;
            y += GLYPH_HEIGHT + 4;
            string++;
            continue;
        }
        
        if (x > 160 - GLYPH_WIDTH || y == 0 || y > 144 - GLYPH_HEIGHT) {
            break;
        }
        
        draw_char(&buffer[x + 160 * y], *string, color);
        x += GLYPH_WIDTH;
        string++;
    }
}

static void draw_text(uint32_t *buffer, unsigned x, unsigned y, const char *string, uint32_t color, uint32_t border)
{
    draw_unbordered_text(buffer, x - 1, y, string, border);
    draw_unbordered_text(buffer, x + 1, y, string, border);
    draw_unbordered_text(buffer, x, y - 1, string, border);
    draw_unbordered_text(buffer, x, y + 1, string, border);
    draw_unbordered_text(buffer, x, y, string, color);
}

enum decoration {
    DECORATION_NONE,
    DECORATION_SELECTION,
    DECORATION_ARROWS,
};

static void draw_text_centered(uint32_t *buffer, unsigned y, const char *string, uint32_t color, uint32_t border, enum decoration decoration)
{
    unsigned x = 160 / 2 - (unsigned) strlen(string) * GLYPH_WIDTH / 2;
    draw_text(buffer, x, y, string, color, border);
    switch (decoration) {
        case DECORATION_SELECTION:
            draw_text(buffer, x - GLYPH_WIDTH, y, SELECTION_STRING, color, border);
            break;
        case DECORATION_ARROWS:
            draw_text(buffer, x - GLYPH_WIDTH, y, LEFT_ARROW_STRING, color, border);
            draw_text(buffer, 160 - x, y, RIGHT_ARROW_STRING, color, border);
            break;
            
        case DECORATION_NONE:
            break;
    }
}

struct menu_item {
    const char *string;
    void (*handler)(unsigned);
    const char *(*value_getter)(unsigned);
    void (*backwards_handler)(unsigned);
};
static const struct menu_item *current_menu = NULL;
static const struct menu_item *root_menu = NULL;
static unsigned current_selection = 0;

static enum {
    SHOWING_DROP_MESSAGE,
    SHOWING_MENU,
    SHOWING_HELP,
    WAITING_FOR_KEY,
    WAITING_FOR_JBUTTON,
} gui_state;

static unsigned joypad_configuration_progress = 0;
static uint8_t joypad_axis_temp;

static void item_exit(unsigned index)
{
    pending_command = GB_SDL_QUIT_COMMAND;
}

static unsigned current_help_page = 0;
static void item_help(unsigned index)
{
    current_help_page = 0;
    gui_state = SHOWING_HELP;
}

static void enter_emulation_menu(unsigned index);
static void enter_graphics_menu(unsigned index);
static void enter_controls_menu(unsigned index);
static void enter_joypad_menu(unsigned index);
static void enter_audio_menu(unsigned index);

static const struct menu_item paused_menu[] = {
    {"Resume", NULL},
    {"Emulation Options", enter_emulation_menu},
    {"Graphic Options", enter_graphics_menu},
    {"Audio Options", enter_audio_menu},
    {"Keyboard", enter_controls_menu},
    {"Joypad", enter_joypad_menu},
    {"Help", item_help},
    {"Quit SameBoy", item_exit},
    {NULL,}
};

static const struct menu_item *const nonpaused_menu = &paused_menu[1];

static void return_to_root_menu(unsigned index)
{
    current_menu = root_menu;
    current_selection = 0;
}

static void cycle_model(unsigned index)
{
    
    configuration.model++;
    if (configuration.model == MODEL_MAX) {
        configuration.model = 0;
    }
    pending_command = GB_SDL_RESET_COMMAND;
}

static void cycle_model_backwards(unsigned index)
{
    if (configuration.model == 0) {
        configuration.model = MODEL_MAX;
    }
    configuration.model--;
    pending_command = GB_SDL_RESET_COMMAND;
}

const char *current_model_string(unsigned index)
{
    return (const char *[]){"Game Boy", "Game Boy Color", "Game Boy Advance"}
        [configuration.model];
}

static const uint32_t rewind_lengths[] = {0, 10, 30, 60, 60 * 2, 60 * 5, 60 * 10};
static const char *rewind_strings[] = {"Disabled",
                                       "10 Seconds",
                                       "30 Seconds",
                                       "1 Minute",
                                       "2 Minutes",
                                       "5 Minutes",
                                       "10 Minutes",
};

static void cycle_rewind(unsigned index)
{
    for (unsigned i = 0; i < sizeof(rewind_lengths) / sizeof(rewind_lengths[0]) - 1; i++) {
        if (configuration.rewind_length == rewind_lengths[i]) {
            configuration.rewind_length = rewind_lengths[i + 1];
            GB_set_rewind_length(&gb, configuration.rewind_length);
            return;
        }
    }
    configuration.rewind_length = rewind_lengths[0];
    GB_set_rewind_length(&gb, configuration.rewind_length);
}

static void cycle_rewind_backwards(unsigned index)
{
    for (unsigned i = 1; i < sizeof(rewind_lengths) / sizeof(rewind_lengths[0]); i++) {
        if (configuration.rewind_length == rewind_lengths[i]) {
            configuration.rewind_length = rewind_lengths[i - 1];
            GB_set_rewind_length(&gb, configuration.rewind_length);
            return;
        }
    }
    configuration.rewind_length = rewind_lengths[sizeof(rewind_lengths) / sizeof(rewind_lengths[0]) - 1];
    GB_set_rewind_length(&gb, configuration.rewind_length);
}

const char *current_rewind_string(unsigned index)
{
    for (unsigned i = 0; i < sizeof(rewind_lengths) / sizeof(rewind_lengths[0]); i++) {
        if (configuration.rewind_length == rewind_lengths[i]) {
            return rewind_strings[i];
        }
    }
    return "Custom";
}

static const struct menu_item emulation_menu[] = {
    {"Emulated Model:", cycle_model, current_model_string, cycle_model_backwards},
    {"Rewind Length:", cycle_rewind, current_rewind_string, cycle_rewind_backwards},
    {"Back", return_to_root_menu},
    {NULL,}
};

static void enter_emulation_menu(unsigned index)
{
    current_menu = emulation_menu;
    current_selection = 0;
}

const char *current_scaling_mode(unsigned index)
{
    return (const char *[]){"Fill Entire Window", "Retain Aspect Ratio", "Retain Integer Factor"}
        [configuration.scaling_mode];
}

const char *current_color_correction_mode(unsigned index)
{
    return (const char *[]){"Disabled", "Correct Color Curves", "Emulate Hardware", "Preserve Brightness"}
        [configuration.color_correction_mode];
}

void cycle_scaling(unsigned index)
{
    configuration.scaling_mode++;
    if (configuration.scaling_mode == GB_SDL_SCALING_MAX) {
        configuration.scaling_mode = 0;
    }
    update_viewport();
    render_texture(NULL, NULL, NULL);
}

void cycle_scaling_backwards(unsigned index)
{
    if (configuration.scaling_mode == 0) {
        configuration.scaling_mode = GB_SDL_SCALING_MAX - 1;
    }
    else {
        configuration.scaling_mode--;
    }
    update_viewport();
    render_texture(NULL, NULL, NULL);
}

static void cycle_color_correction(unsigned index)
{
    if (configuration.color_correction_mode == GB_COLOR_CORRECTION_PRESERVE_BRIGHTNESS) {
        configuration.color_correction_mode = GB_COLOR_CORRECTION_DISABLED;
    }
    else {
        configuration.color_correction_mode++;
    }
}

static void cycle_color_correction_backwards(unsigned index)
{
    if (configuration.color_correction_mode == GB_COLOR_CORRECTION_DISABLED) {
        configuration.color_correction_mode = GB_COLOR_CORRECTION_PRESERVE_BRIGHTNESS;
    }
    else {
        configuration.color_correction_mode--;
    }
}

struct shader_name {
    const char *file_name;
    const char *display_name;
} shaders[] =
{
    {"NearestNeighbor", "Nearest Neighbor"},
    {"Bilinear", "Bilinear"},
    {"SmoothBilinear", "Smooth Bilinear"},
    {"LCD", "LCD Display"},
    {"CRT", "CRT Display"},
    {"Scale2x", "Scale2x"},
    {"Scale4x", "Scale4x"},
    {"AAScale2x", "Anti-aliased Scale2x"},
    {"AAScale4x", "Anti-aliased Scale4x"},
    {"HQ2x", "HQ2x"},
    {"OmniScale", "OmniScale"},
    {"OmniScaleLegacy", "OmniScale Legacy"},
    {"AAOmniScaleLegacy", "AA OmniScale Legacy"},
};

static void cycle_filter(unsigned index)
{
    unsigned i = 0;
    for (; i < sizeof(shaders) / sizeof(shaders[0]); i++) {
        if (strcmp(shaders[i].file_name, configuration.filter) == 0) {
            break;
        }
    }
    

    i += 1;
    if (i >= sizeof(shaders) / sizeof(shaders[0])) {
        i -= sizeof(shaders) / sizeof(shaders[0]);
    }
    
    strcpy(configuration.filter, shaders[i].file_name);
    free_shader(&shader);
    if (!init_shader_with_name(&shader, configuration.filter)) {
        init_shader_with_name(&shader, "NearestNeighbor");
    }
}

static void cycle_filter_backwards(unsigned index)
{
    unsigned i = 0;
    for (; i < sizeof(shaders) / sizeof(shaders[0]); i++) {
        if (strcmp(shaders[i].file_name, configuration.filter) == 0) {
            break;
        }
    }
    
    i -= 1;
    if (i >= sizeof(shaders) / sizeof(shaders[0])) {
        i = sizeof(shaders) / sizeof(shaders[0]) - 1;
    }
    
    strcpy(configuration.filter, shaders[i].file_name);
    free_shader(&shader);
    if (!init_shader_with_name(&shader, configuration.filter)) {
        init_shader_with_name(&shader, "NearestNeighbor");
    }

}
const char *current_filter_name(unsigned index)
{
    unsigned i = 0;
    for (; i < sizeof(shaders) / sizeof(shaders[0]); i++) {
        if (strcmp(shaders[i].file_name, configuration.filter) == 0) {
            break;
        }
    }
    
    if (i == sizeof(shaders) / sizeof(shaders[0])) {
        i = 0;
    }
    
    return shaders[i].display_name;
}

static void toggle_blend_frames(unsigned index)
{
    configuration.blend_frames ^= true;
}

const char *blend_frames_string(unsigned index)
{
    return configuration.blend_frames? "Enabled" : "Disabled";
}

static const struct menu_item graphics_menu[] = {
    {"Scaling Mode:", cycle_scaling, current_scaling_mode, cycle_scaling_backwards},
    {"Scaling Filter:", cycle_filter, current_filter_name, cycle_filter_backwards},
    {"Color Correction:", cycle_color_correction, current_color_correction_mode, cycle_color_correction_backwards},
    {"Blend Frames:", toggle_blend_frames, blend_frames_string, toggle_blend_frames},
    {"Back", return_to_root_menu},
    {NULL,}
};

static void enter_graphics_menu(unsigned index)
{
    current_menu = graphics_menu;
    current_selection = 0;
}

const char *highpass_filter_string(unsigned index)
{
    return (const char *[]){"None (Keep DC Offset)", "Accurate", "Preserve Waveform"}
        [configuration.highpass_mode];
}

void cycle_highpass_filter(unsigned index)
{
    configuration.highpass_mode++;
    if (configuration.highpass_mode == GB_HIGHPASS_MAX) {
        configuration.highpass_mode = 0;
    }
}

void cycle_highpass_filter_backwards(unsigned index)
{
    if (configuration.highpass_mode == 0) {
        configuration.highpass_mode = GB_HIGHPASS_MAX - 1;
    }
    else {
        configuration.highpass_mode--;
    }
}

static const struct menu_item audio_menu[] = {
    {"Highpass Filter:", cycle_highpass_filter, highpass_filter_string, cycle_highpass_filter_backwards},
    {"Back", return_to_root_menu},
    {NULL,}
};

static void enter_audio_menu(unsigned index)
{
    current_menu = audio_menu;
    current_selection = 0;
}

static void modify_key(unsigned index)
{
    gui_state = WAITING_FOR_KEY;
}

static void enter_controls_menu_2(unsigned index);
static const char *key_name(unsigned index);

static const struct menu_item controls_menu[] = {
    {"Right:", modify_key, key_name,},
    {"Left:", modify_key, key_name,},
    {"Up:", modify_key, key_name,},
    {"Down:", modify_key, key_name,},
    {"A:", modify_key, key_name,},
    {"B:", modify_key, key_name,},
    {"Select:", modify_key, key_name,},
    {"Start:", modify_key, key_name,},
    {"Next Page", enter_controls_menu_2},
    {"Back", return_to_root_menu},
    {NULL,}
};

static const struct menu_item controls_menu_2[] = {
    {"Turbo:", modify_key, key_name,},
    {"Rewind:", modify_key, key_name,},
    {"Slow-Motion:", modify_key, key_name,},
    {"Back", return_to_root_menu},
    {NULL,}
};

static const char *key_name(unsigned index)
{
    if (current_menu == controls_menu_2) {
        if (index == 0) {
            return SDL_GetScancodeName(configuration.keys[8]);
        }
        return SDL_GetScancodeName(configuration.keys_2[index - 1]);
    }
    return SDL_GetScancodeName(configuration.keys[index]);
}

static void enter_controls_menu(unsigned index)
{
    current_menu = controls_menu;
    current_selection = 0;
}

static void enter_controls_menu_2(unsigned index)
{
    current_menu = controls_menu_2;
    current_selection = 0;
}

static unsigned joypad_index = 0;
static SDL_Joystick *joystick = NULL;
static SDL_GameController *controller = NULL;

const char *current_joypad_name(unsigned index)
{
    static char name[23] = {0,};
    const char *orig_name = joystick? SDL_JoystickName(joystick) : NULL;
    if (!orig_name) return "Not Found";
    unsigned i = 0;
    
    // SDL returns a name with repeated and trailing spaces
    while (*orig_name && i < sizeof(name) - 2) {
        if (orig_name[0] != ' ' || orig_name[1] != ' ') {
            name[i++] = *orig_name;
        }
        orig_name++;
    }
    if (i && name[i - 1] == ' ') {
        i--;
    }
    name[i] = 0;
    
    return name;
}

static void cycle_joypads(unsigned index)
{
    joypad_index++;
    if (joypad_index >= SDL_NumJoysticks()) {
        joypad_index = 0;
    }
    if (controller) {
        SDL_GameControllerClose(controller);
        controller = NULL;
    }
    else if (joystick) {
        SDL_JoystickClose(joystick);
        joystick = NULL;
    }
    if ((controller = SDL_GameControllerOpen(joypad_index))){
        joystick = SDL_GameControllerGetJoystick(controller);
    }
    else {
        joystick = SDL_JoystickOpen(joypad_index);
    }
}

static void cycle_joypads_backwards(unsigned index)
{
    joypad_index++;
    if (joypad_index >= SDL_NumJoysticks()) {
        joypad_index = SDL_NumJoysticks() - 1;
    }
    if (controller) {
        SDL_GameControllerClose(controller);
        controller = NULL;
    }
    else if (joystick) {
        SDL_JoystickClose(joystick);
        joystick = NULL;
    }
    if ((controller = SDL_GameControllerOpen(joypad_index))){
        joystick = SDL_GameControllerGetJoystick(controller);
    }
    else {
        joystick = SDL_JoystickOpen(joypad_index);
    }
}

static void detect_joypad_layout(unsigned index)
{
    gui_state = WAITING_FOR_JBUTTON;
    joypad_configuration_progress = 0;
    joypad_axis_temp = -1;
}

static const struct menu_item joypad_menu[] = {
    {"Joypad:", cycle_joypads, current_joypad_name, cycle_joypads_backwards},
    {"Configure layout", detect_joypad_layout},
    {"Back", return_to_root_menu},
    {NULL,}
};

static void enter_joypad_menu(unsigned index)
{
    current_menu = joypad_menu;
    current_selection = 0;
}

joypad_button_t get_joypad_button(uint8_t physical_button)
{
    for (unsigned i = 0; i < JOYPAD_BUTTONS_MAX; i++) {
        if (configuration.joypad_configuration[i] == physical_button) {
            return i;
        }
    }
    return JOYPAD_BUTTONS_MAX;
}

joypad_axis_t get_joypad_axis(uint8_t physical_axis)
{
    for (unsigned i = 0; i < JOYPAD_AXISES_MAX; i++) {
        if (configuration.joypad_axises[i] == physical_axis) {
            return i;
        }
    }
    return JOYPAD_AXISES_MAX;
}


void connect_joypad(void)
{
    if (joystick && !SDL_NumJoysticks()) {
        if (controller) {
            SDL_GameControllerClose(controller);
            controller = NULL;
            joystick = NULL;
        }
        else {
            SDL_JoystickClose(joystick);
            joystick = NULL;
        }
    }
    else if (!joystick && SDL_NumJoysticks()) {
        if ((controller = SDL_GameControllerOpen(0))){
            joystick = SDL_GameControllerGetJoystick(controller);
        }
        else {
            joystick = SDL_JoystickOpen(0);
        }
    }
}

extern void set_filename(const char *new_filename, bool new_should_free);
void run_gui(bool is_running)
{
    connect_joypad();
    
    /* Draw the background screen */
    static SDL_Surface *converted_background = NULL;
    if (!converted_background) {
        SDL_Surface *background = SDL_LoadBMP(resource_path("background.bmp"));
        SDL_SetPaletteColors(background->format->palette, gui_palette, 0, 4);
        converted_background = SDL_ConvertSurface(background, pixel_format, 0);
        SDL_LockSurface(converted_background);
        SDL_FreeSurface(background);
        
        for (unsigned i = 4; i--; ) {
            gui_palette_native[i] = SDL_MapRGB(pixel_format, gui_palette[i].r, gui_palette[i].g, gui_palette[i].b);
        }
    }

    uint32_t pixels[160 * 144];
    SDL_Event event = {0,};
    gui_state = is_running? SHOWING_MENU : SHOWING_DROP_MESSAGE;
    bool should_render = true;
    current_menu = root_menu = is_running? paused_menu : nonpaused_menu;
    current_selection = 0;
    do {
        /* Convert Joypad events (We only generate down events) */
        if (gui_state != WAITING_FOR_KEY && gui_state != WAITING_FOR_JBUTTON) {
            switch (event.type) {
                case SDL_JOYBUTTONDOWN:
                    event.type = SDL_KEYDOWN;
                    joypad_button_t button = get_joypad_button(event.jbutton.button);
                    if (button == JOYPAD_BUTTON_A) {
                        event.key.keysym.scancode = SDL_SCANCODE_RETURN;
                    }
                    else if (button == JOYPAD_BUTTON_MENU) {
                        event.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
                    }
                    else if (button == JOYPAD_BUTTON_UP) event.key.keysym.scancode = SDL_SCANCODE_UP;
                    else if (button == JOYPAD_BUTTON_DOWN) event.key.keysym.scancode = SDL_SCANCODE_DOWN;
                    else if (button == JOYPAD_BUTTON_LEFT) event.key.keysym.scancode = SDL_SCANCODE_LEFT;
                    else if (button == JOYPAD_BUTTON_RIGHT) event.key.keysym.scancode = SDL_SCANCODE_RIGHT;
                    break;

                case SDL_JOYHATMOTION: {
                    uint8_t value = event.jhat.value;
                    if (value != 0) {
                        uint32_t scancode =
                            value == SDL_HAT_UP ? SDL_SCANCODE_UP
                            : value == SDL_HAT_DOWN ? SDL_SCANCODE_DOWN
                            : value == SDL_HAT_LEFT ? SDL_SCANCODE_LEFT
                            : value == SDL_HAT_RIGHT ? SDL_SCANCODE_RIGHT
                            : 0;

                        if (scancode != 0) {
                            event.type = SDL_KEYDOWN;
                            event.key.keysym.scancode = scancode;
                        }
                    }
               }
                    
                case SDL_JOYAXISMOTION: {
                    static bool axis_active[2] = {false, false};
                    joypad_axis_t axis = get_joypad_axis(event.jaxis.axis);
                    if (axis == JOYPAD_AXISES_X) {
                        if (!axis_active[0] && event.jaxis.value > JOYSTICK_HIGH) {
                            axis_active[0] = true;
                            event.type = SDL_KEYDOWN;
                            event.key.keysym.scancode = SDL_SCANCODE_RIGHT;
                        }
                        else if (!axis_active[0] && event.jaxis.value < -JOYSTICK_HIGH) {
                            axis_active[0] = true;
                            event.type = SDL_KEYDOWN;
                            event.key.keysym.scancode = SDL_SCANCODE_LEFT;
                            
                        }
                        else if (axis_active[0] && event.jaxis.value < JOYSTICK_LOW && event.jaxis.value > -JOYSTICK_LOW) {
                            axis_active[0] = false;
                        }
                    }
                    else if (axis == JOYPAD_AXISES_Y) {
                        if (!axis_active[1] && event.jaxis.value > JOYSTICK_HIGH) {
                            axis_active[1] = true;
                            event.type = SDL_KEYDOWN;
                            event.key.keysym.scancode = SDL_SCANCODE_DOWN;
                        }
                        else if (!axis_active[1] && event.jaxis.value < -JOYSTICK_HIGH) {
                            axis_active[1] = true;
                            event.type = SDL_KEYDOWN;
                            event.key.keysym.scancode = SDL_SCANCODE_UP;
                        }
                        else if (axis_active[1] && event.jaxis.value < JOYSTICK_LOW && event.jaxis.value > -JOYSTICK_LOW) {
                            axis_active[1] = false;
                        }
                    }
                }
            }
        }
        switch (event.type) {
            case SDL_QUIT: {
                if (!is_running) {
                    exit(0);
                }
                else {
                    pending_command = GB_SDL_QUIT_COMMAND;
                    return;
                }
                
            }
            case SDL_WINDOWEVENT: {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    update_viewport();
                    render_texture(NULL, NULL, NULL);
                }
                break;
            }
            case SDL_DROPFILE: {
                set_filename(event.drop.file, true);
                pending_command = GB_SDL_NEW_FILE_COMMAND;
                return;
            }
            case SDL_JOYBUTTONDOWN:
            {
                if (gui_state == WAITING_FOR_JBUTTON && joypad_configuration_progress != JOYPAD_BUTTONS_MAX) {
                    should_render = true;
                    configuration.joypad_configuration[joypad_configuration_progress++] = event.jbutton.button;
                }
                break;
            }
                
            case SDL_JOYAXISMOTION: {
                if (gui_state == WAITING_FOR_JBUTTON &&
                    joypad_configuration_progress == JOYPAD_BUTTONS_MAX &&
                    abs(event.jaxis.value) >= 0x4000) {
                    if (joypad_axis_temp == (uint8_t)-1) {
                        joypad_axis_temp = event.jaxis.axis;
                    }
                    else if (joypad_axis_temp != event.jaxis.axis) {
                        if (joypad_axis_temp < event.jaxis.axis) {
                            configuration.joypad_axises[JOYPAD_AXISES_X] = joypad_axis_temp;
                            configuration.joypad_axises[JOYPAD_AXISES_Y] = event.jaxis.axis;
                        }
                        else {
                            configuration.joypad_axises[JOYPAD_AXISES_Y] = joypad_axis_temp;
                            configuration.joypad_axises[JOYPAD_AXISES_X] = event.jaxis.axis;
                        }
                        
                        gui_state = SHOWING_MENU;
                        should_render = true;
                    }
                }
                break;
            }

            case SDL_KEYDOWN:
                if (event.key.keysym.scancode == SDL_SCANCODE_RETURN && gui_state == WAITING_FOR_JBUTTON) {
                    should_render = true;
                    if (joypad_configuration_progress != JOYPAD_BUTTONS_MAX) {
                        configuration.joypad_configuration[joypad_configuration_progress] = -1;
                    }
                    else {
                        configuration.joypad_axises[0] = -1;
                        configuration.joypad_axises[1] = -1;
                    }
                    joypad_configuration_progress++;
                    
                    if (joypad_configuration_progress > JOYPAD_BUTTONS_MAX) {
                        gui_state = SHOWING_MENU;
                    }
                }
                else if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    if (is_running) {
                        return;
                    }
                    else {
                        if (gui_state == SHOWING_DROP_MESSAGE) {
                            gui_state = SHOWING_MENU;
                        }
                        else if (gui_state == SHOWING_MENU) {
                            gui_state = SHOWING_DROP_MESSAGE;
                        }
                        current_selection = 0;
                        current_menu = root_menu;
                        should_render = true;
                    }
                }
                else if (gui_state == SHOWING_MENU) {
                    if (event.key.keysym.scancode == SDL_SCANCODE_DOWN && current_menu[current_selection + 1].string) {
                        current_selection++;
                        should_render = true;
                    }
                    else if (event.key.keysym.scancode == SDL_SCANCODE_UP && current_selection) {
                        current_selection--;
                        should_render = true;
                    }
                    else if (event.key.keysym.scancode == SDL_SCANCODE_RETURN  && !current_menu[current_selection].backwards_handler) {
                        if (current_menu[current_selection].handler) {
                            current_menu[current_selection].handler(current_selection);
                            if (pending_command == GB_SDL_RESET_COMMAND && !is_running) {
                                pending_command = GB_SDL_NO_COMMAND;
                            }
                            if (pending_command) {
                                if (!is_running && pending_command == GB_SDL_QUIT_COMMAND) {
                                    exit(0);
                                }
                                return;
                            }
                            should_render = true;
                        }
                        else {
                            return;
                        }
                    }
                    else if (event.key.keysym.scancode == SDL_SCANCODE_RIGHT && current_menu[current_selection].backwards_handler) {
                        current_menu[current_selection].handler(current_selection);
                        should_render = true;
                    }
                    else if (event.key.keysym.scancode == SDL_SCANCODE_LEFT && current_menu[current_selection].backwards_handler) {
                        current_menu[current_selection].backwards_handler(current_selection);
                        should_render = true;
                    }
                }
                else if (gui_state == SHOWING_HELP) {
                    current_help_page++;
                    if (current_help_page == sizeof(help) / sizeof(help[0])) {
                        gui_state = SHOWING_MENU;
                    }
                    should_render = true;
                }
                else if (gui_state == WAITING_FOR_KEY) {
                    if (current_menu == controls_menu_2) {
                        if (current_selection == 0) {
                            configuration.keys[8] = event.key.keysym.scancode;
                        }
                        else {
                            configuration.keys_2[current_selection - 1] = event.key.keysym.scancode;
                        }
                    }
                    else {
                        configuration.keys[current_selection] = event.key.keysym.scancode;
                    }
                    gui_state = SHOWING_MENU;
                    should_render = true;
                }
                break;
        }
        
        if (should_render) {
            should_render = false;
            memcpy(pixels, converted_background->pixels, sizeof(pixels));
            
            switch (gui_state) {
                case SHOWING_DROP_MESSAGE:
                    draw_text_centered(pixels, 8, "Press ESC for menu", gui_palette_native[3], gui_palette_native[0], false);
                    draw_text_centered(pixels, 116, "Drop a GB or GBC", gui_palette_native[3], gui_palette_native[0], false);
                    draw_text_centered(pixels, 128, "file to play", gui_palette_native[3], gui_palette_native[0], false);
                    break;
                case SHOWING_MENU:
                    draw_text_centered(pixels, 8, "SameBoy", gui_palette_native[3], gui_palette_native[0], false);
                    unsigned i = 0, y = 24;
                    for (const struct menu_item *item = current_menu; item->string; item++, i++) {
                        if (item->value_getter && !item->backwards_handler) {
                            char line[25];
                            snprintf(line, sizeof(line), "%s%*s", item->string, 24 - (int)strlen(item->string), item->value_getter(i));
                            draw_text_centered(pixels, y, line, gui_palette_native[3], gui_palette_native[0],
                                               i == current_selection ? DECORATION_SELECTION : DECORATION_NONE);
                            y += 12;
                            
                        }
                        else {
                            draw_text_centered(pixels, y, item->string, gui_palette_native[3], gui_palette_native[0],
                                               i == current_selection && !item->value_getter ? DECORATION_SELECTION : DECORATION_NONE);
                            y += 12;
                            if (item->value_getter) {
                                draw_text_centered(pixels, y, item->value_getter(i), gui_palette_native[3], gui_palette_native[0],
                                                   i == current_selection ? DECORATION_ARROWS : DECORATION_NONE);
                                y += 12;
                            }
                        }
                    }
                    break;
                case SHOWING_HELP:
                    draw_text(pixels, 2, 2, help[current_help_page], gui_palette_native[3], gui_palette_native[0]);
                    break;
                case WAITING_FOR_KEY:
                    draw_text_centered(pixels, 68, "Press a Key", gui_palette_native[3], gui_palette_native[0], DECORATION_NONE);
                    break;
                case WAITING_FOR_JBUTTON:
                    draw_text_centered(pixels, 68,
                                       joypad_configuration_progress != JOYPAD_BUTTONS_MAX ? "Press button for" : "Move the Analog Stick",
                                       gui_palette_native[3], gui_palette_native[0], DECORATION_NONE);
                    draw_text_centered(pixels, 80,
                                      (const char *[])
                                       {
                                           "Right",
                                           "Left",
                                           "Up",
                                           "Down",
                                           "A",
                                           "B",
                                           "Select",
                                           "Start",
                                           "Open Menu",
                                           "Turbo",
                                           "Rewind",
                                           "Slow-Motion",
                                           "",
                                       } [joypad_configuration_progress],
                                       gui_palette_native[3], gui_palette_native[0], DECORATION_NONE);
                    draw_text_centered(pixels, 104, "Press Enter to skip", gui_palette_native[3], gui_palette_native[0], DECORATION_NONE);
                    break;
            }

            render_texture(pixels, NULL, NULL);
        }
    } while (SDL_WaitEvent(&event));
}

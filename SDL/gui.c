#include <OpenDialog/open_dialog.h>
#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include "utils.h"
#include "gui.h"
#include "font.h"
#include "audio/audio.h"

#ifdef _WIN32
#include <dwmapi.h>
#include "windows_associations.h"
#include <SDL_syswm.h>
#endif

static const SDL_Color gui_palette[4] = {{8, 24, 16,}, {57, 97, 57,}, {132, 165, 99}, {198, 222, 140}};
static uint32_t gui_palette_native[4];

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture *texture = NULL;
SDL_PixelFormat *pixel_format = NULL;
enum pending_command pending_command;
unsigned command_parameter;
char *dropped_state_file = NULL;

static char **custom_palettes;
static unsigned n_custom_palettes;


#ifdef __APPLE__
#define MODIFIER_NAME " " CMD_STRING
#else
#define MODIFIER_NAME CTRL_STRING
#endif

shader_t shader;
static SDL_Rect rect;
static unsigned factor;

static SDL_Surface *converted_background = NULL;

bool screen_manually_resized = false;

void render_texture(void *pixels,  void *previous)
{
    if (renderer) {
        if (pixels) {
            SDL_UpdateTexture(texture, NULL, pixels, GB_get_screen_width(&gb) * sizeof (uint32_t));
        }
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }
    else {
        static void *_pixels = NULL;
        if (pixels) {
            _pixels = pixels;
        }
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        GB_frame_blending_mode_t mode = configuration.blending_mode;
        if (!previous) {
            mode = GB_FRAME_BLENDING_MODE_DISABLED;
        }
        else if (mode == GB_FRAME_BLENDING_MODE_ACCURATE) {
            if (GB_is_sgb(&gb)) {
                mode = GB_FRAME_BLENDING_MODE_SIMPLE;
            }
            else {
                mode = GB_is_odd_frame(&gb)? GB_FRAME_BLENDING_MODE_ACCURATE_ODD : GB_FRAME_BLENDING_MODE_ACCURATE_EVEN;
            }
        }
        render_bitmap_with_shader(&shader, _pixels, previous,
                                  GB_get_screen_width(&gb), GB_get_screen_height(&gb),
                                  rect.x, rect.y, rect.w, rect.h,
                                  mode);
        SDL_GL_SwapWindow(window);
    }
}

static const char *help[] = {
"Keyboard Shortcuts:\n"
" Open Menu:        Escape\n"
" Menu Navigation:  Arrow keys or hjkl\n"
" Open ROM:          " MODIFIER_NAME "+O\n"
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
" Toggle channel: " ALT_STRING "+(1-4)\n"
" Break Debugger:    " CTRL_STRING "+C",
"\n"
"SameBoy\n"
"Version " GB_VERSION "\n\n"
"Copyright " COPYRIGHT_STRING " 2015-" GB_COPYRIGHT_YEAR "\n"
"Lior Halphon\n\n"
"Licensed under the Expat\n"
"License, see LICENSE for\n"
"more details."
};

void update_viewport(void)
{
    int win_width, win_height;
    SDL_GL_GetDrawableSize(window, &win_width, &win_height);
    int logical_width, logical_height;
    SDL_GetWindowSize(window, &logical_width, &logical_height);
    factor = win_width / logical_width;
    
    double x_factor = win_width / (double) GB_get_screen_width(&gb);
    double y_factor = win_height / (double) GB_get_screen_height(&gb);
    
    if (configuration.scaling_mode == GB_SDL_SCALING_INTEGER_FACTOR) {
        x_factor = (unsigned)(x_factor);
        y_factor = (unsigned)(y_factor);
    }
    
    if (configuration.scaling_mode != GB_SDL_SCALING_ENTIRE_WINDOW) {
        if (x_factor > y_factor) {
            x_factor = y_factor;
        }
        else {
            y_factor = x_factor;
        }
    }
    
    unsigned new_width = x_factor * GB_get_screen_width(&gb);
    unsigned new_height = y_factor * GB_get_screen_height(&gb);
    
    rect = (SDL_Rect){(win_width  - new_width) / 2, (win_height - new_height) /2,
        new_width, new_height};
    
    if (renderer) {
        SDL_RenderSetViewport(renderer, &rect);
    }
    else {
        glViewport(rect.x, rect.y, rect.w, rect.h);
    }
}

static void rescale_window(void)
{
    SDL_SetWindowSize(window, GB_get_screen_width(&gb) * configuration.default_scale, GB_get_screen_height(&gb) * configuration.default_scale);
}

static void draw_char(uint32_t *buffer, unsigned width, unsigned height, unsigned char ch, uint32_t color, uint32_t *mask_top, uint32_t *mask_bottom)
{
    if (ch < ' ' || ch > font_max) {
        ch = '?';
    }

    // Huge dirty hack
    if ((signed char)ch == CHECKBOX_ON_STRING[0] || (signed char)ch == CHECKBOX_OFF_STRING[0]) {
        if (color == gui_palette_native[3]) {
            color = gui_palette_native[0];
        }
        else if (color == gui_palette_native[0]) {
            color = gui_palette_native[3];
            ch = CHECKBOX_OFF_STRING[0];
        }
    }
    
    uint8_t *data = &font[(ch - ' ') * GLYPH_WIDTH * GLYPH_HEIGHT];
    
    for (unsigned y = GLYPH_HEIGHT; y--;) {
        for (unsigned x = GLYPH_WIDTH; x--;) {
            if (*(data++) && buffer >= mask_top && buffer < mask_bottom) {
                (*buffer) = color;
            }
            buffer++;
        }
        buffer += width - GLYPH_WIDTH;
    }
}

static signed scroll = 0;
static void draw_unbordered_text(uint32_t *buffer, unsigned width, unsigned height, unsigned x, signed y, const char *string, uint32_t color, bool is_osd)
{
    if (!is_osd) {
        y -= scroll;
    }
    unsigned orig_x = x;
    unsigned y_offset = is_osd? 0 : (GB_get_screen_height(&gb) - 144) / 2;
    while (*string) {
        if (*string == '\n') {
            x = orig_x;
            y += GLYPH_HEIGHT + 4;
            string++;
            continue;
        }
        
        if (x > width - GLYPH_WIDTH) {
            break;
        }
        
        draw_char(&buffer[(signed)(x + width * y)], width, height, *string, color, &buffer[width * y_offset], &buffer[width * (is_osd? GB_get_screen_height(&gb) : y_offset + 144)]);
        x += GLYPH_WIDTH;
        string++;
    }
}

void draw_text(uint32_t *buffer, unsigned width, unsigned height, unsigned x, signed y, const char *string, uint32_t color, uint32_t border, bool is_osd)
{
    draw_unbordered_text(buffer, width, height, x - 1, y, string, border, is_osd);
    draw_unbordered_text(buffer, width, height, x + 1, y, string, border, is_osd);
    draw_unbordered_text(buffer, width, height, x, y - 1, string, border, is_osd);
    draw_unbordered_text(buffer, width, height, x, y + 1, string, border, is_osd);
    draw_unbordered_text(buffer, width, height, x, y, string, color, is_osd);
}

const char *osd_text = NULL;
unsigned osd_countdown = 0;
unsigned osd_text_lines = 1;

void show_osd_text(const char *text)
{
    osd_text_lines = 1;
    osd_text = text;
    osd_countdown = 30;
    while (*text++) {
        if (*text == '\n') {
            osd_text_lines++;
            osd_countdown += 30;
        }
    }
}


enum style {
    STYLE_LEFT,
    STYLE_INDENT,
    STYLE_CENTER,
    STYLE_SELECTION,
    STYLE_ARROWS,
};

static void draw_styled_text(uint32_t *buffer, unsigned width, unsigned height, unsigned y, const char *string, uint32_t color, uint32_t border, enum style style)
{
    unsigned x = GLYPH_WIDTH * 2 + (width - 160) / 2;
    if (style == STYLE_CENTER || style == STYLE_ARROWS) {
        x = width / 2 - (unsigned) strlen(string) * GLYPH_WIDTH / 2;
    }
    else if (style == STYLE_LEFT) {
        x = 6 + (width - 160) / 2;
    }
    
    draw_text(buffer, width, height, x, y, string, color, border, false);
    switch (style) {
        case STYLE_SELECTION:
            draw_text(buffer, width, height, x - GLYPH_WIDTH, y, SELECTION_STRING, color, border, false);
            break;
        case STYLE_ARROWS:
            draw_text(buffer, width, height, x - GLYPH_WIDTH, y, LEFT_ARROW_STRING, color, border, false);
            draw_text(buffer, width, height, width - x, y, RIGHT_ARROW_STRING, color, border, false);
            break;
            
        default:
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
static unsigned menu_height;
static unsigned scrollbar_size;
static bool mouse_scroling = false;

static unsigned current_selection = 0;

static enum {
    SHOWING_DROP_MESSAGE,
    SHOWING_MENU,
    SHOWING_HELP,
    WAITING_FOR_KEY,
    WAITING_FOR_JBUTTON,
    TEXT_INPUT,
} gui_state;

static char text_input_title[26];
static char text_input_title2[26];
static char text_input[26];

static void (*text_input_callback)(char ch) = NULL;

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

static void about(unsigned index)
{
    current_help_page = 1;
    gui_state = SHOWING_HELP;
}

static void enter_emulation_menu(unsigned index);
static void enter_graphics_menu(unsigned index);
static void enter_keyboard_menu(unsigned index);
static void enter_joypad_menu(unsigned index);
static void enter_audio_menu(unsigned index);
static void enter_controls_menu(unsigned index);
static void enter_help_menu(unsigned index);
static void enter_options_menu(unsigned index);
static void toggle_audio_recording(unsigned index);

extern void set_filename(const char *new_filename, typeof(free) *new_free_function);

static void nop(unsigned index){}

static void open_rom(unsigned index)
{
    char *filename = do_open_rom_dialog();
    if (filename) {
        set_filename(filename, free);
        pending_command = GB_SDL_NEW_FILE_COMMAND;
    }
}

static void cart_swap(unsigned index)
{
    char *filename = do_open_rom_dialog();
    if (filename) {
        set_filename(filename, free);
        pending_command = GB_SDL_CART_SWAP_COMMAND;
    }
}

static void recalculate_menu_height(void)
{
    menu_height = 24;
    scrollbar_size = 0;
    if (gui_state == SHOWING_MENU) {
        for (const struct menu_item *item = current_menu; item->string; item++) {
            menu_height += 12;
            if (item->backwards_handler) {
                menu_height += 12;
            }
        }
    }
    if (menu_height > 144) {
        scrollbar_size = 144 * 140 / menu_height;
    }
}

#if SDL_COMPILEDVERSION < 2014
int SDL_OpenURL(const char *url)
{
    char *string = NULL;
#ifdef __APPLE__
    asprintf(&string, "open '%s'", url);
#else
#ifdef _WIN32
    asprintf(&string, "explorer '%s'", url);
#else
    asprintf(&string, "xdg-open '%s'", url);
#endif
#endif
    int ret = system(string);
    free(string);
    return ret;
}
#endif

static char audio_recording_menu_item[] = "Start Audio Recording";

static void sponsor(unsigned index)
{
    SDL_OpenURL("https://github.com/sponsors/LIJI32");
}

static void debugger_help(unsigned index)
{
    SDL_OpenURL("https://sameboy.github.io/debugger/");
}

static void return_to_root_menu(unsigned index)
{
    current_menu = root_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

#ifdef _WIN32
static void associate_rom_files(unsigned index);
#endif

static
#ifndef _WIN32
const
#endif
struct menu_item options_menu[] = {
    {"Emulation Options", enter_emulation_menu},
    {"Graphic Options", enter_graphics_menu},
    {"Audio Options", enter_audio_menu},
    {"Control Options", enter_controls_menu},
#ifdef _WIN32
    {"Associate ROM Files", associate_rom_files},
#endif
    {"Back", return_to_root_menu},
    {NULL,}
};

#ifdef _WIN32
static void associate_rom_files(unsigned index)
{
    if (GB_do_windows_association()) {
        options_menu[index].string = "ROM Files Associated";
    }
    else {
        options_menu[index].string = "Files Association Failed";
    }
}
#endif

static void enter_options_menu(unsigned index)
{
    current_menu = options_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

static const GB_cheat_t *current_cheat = NULL;

extern struct menu_item modify_cheat_menu[];

static void save_cheats(void)
{
    extern char *filename;
    size_t path_length = strlen(filename);
    char cheat_path[path_length + 5];
    replace_extension(filename, path_length, cheat_path, ".cht");
    GB_save_cheats(&gb, cheat_path);
}

static void rename_callback(char ch)
{
    if (ch == '\b' && text_input[0]) {
        text_input[strlen(text_input) - 1] = 0;
        return;
    }
    if (ch == '\n') {
        GB_update_cheat(&gb,
                        current_cheat,
                        text_input,
                        current_cheat->address,
                        current_cheat->bank,
                        current_cheat->value,
                        current_cheat->old_value,
                        current_cheat->use_old_value,
                        current_cheat->enabled);
        modify_cheat_menu[0].string = current_cheat->description;
        gui_state = SHOWING_MENU;
        save_cheats();
        SDL_StopTextInput();
        return;
    }
    if (ch < ' ') return;
    size_t len = strlen(text_input);
    if (len < 21) {
        text_input[len] = ch;
        text_input[len + 1] = 0;
    }
}

static void rename_cheat(unsigned index)
{
    strcpy(text_input_title, "Rename Cheat");
    text_input_title2[0] = 0;
    memcpy(text_input, current_cheat->description, 24);
    text_input[24] = 0;
    gui_state = TEXT_INPUT;
    text_input_callback = rename_callback;
    SDL_StartTextInput();
    GB_update_cheat(&gb,
                    current_cheat,
                    current_cheat->description,
                    current_cheat->address,
                    current_cheat->bank,
                    current_cheat->value,
                    current_cheat->old_value,
                    current_cheat->use_old_value,
                    current_cheat->enabled);
    save_cheats();
}


static void toggle_cheat(unsigned index)
{
    GB_update_cheat(&gb,
                    current_cheat,
                    current_cheat->description,
                    current_cheat->address,
                    current_cheat->bank,
                    current_cheat->value,
                    current_cheat->old_value,
                    current_cheat->use_old_value,
                    !current_cheat->enabled);
    save_cheats();
}

static const char *active_cheat_checkbox(unsigned index)
{
    return current_cheat->enabled? CHECKBOX_ON_STRING : CHECKBOX_OFF_STRING;
}

static const char *get_cheat_address(unsigned index)
{
    static char ret[12];
    if (current_cheat->bank == GB_CHEAT_ANY_BANK) {
        sprintf(ret, "$%04X", current_cheat->address);
    }
    else {
        sprintf(ret, "$%02X:$%04X", current_cheat->bank, current_cheat->address);
    }
    
    return ret;
}

static void change_cheat_address_callback(char ch)
{
    if (ch == '\b' && text_input[1]) {
        size_t len = strlen(text_input);
        text_input[len - 1] = 0;
        if (text_input[len - 2] == ':') {
            text_input[len - 2] = 0;
        }
        return;
    }
    if (ch == '\n') {
        uint16_t bank = GB_CHEAT_ANY_BANK;
        uint16_t address = 0;
        
        const char *s = text_input + 1;
        while (*s) {
            if (*s == ':') {
                bank = address;
                address = 0;
            }
            else if (*s >= '0' && *s <= '9'){
                address *= 0x10;
                address += *s - '0';
            }
            else if (*s >= 'A' && *s <= 'F'){
                address *= 0x10;
                address += *s - 'A' + 10;
            }
            s++;
        }
        
        GB_update_cheat(&gb,
                        current_cheat,
                        current_cheat->description,
                        address,
                        bank,
                        current_cheat->value,
                        current_cheat->old_value,
                        current_cheat->use_old_value,
                        current_cheat->enabled);
        save_cheats();
        gui_state = SHOWING_MENU;
        SDL_StopTextInput();
        return;
    }
    size_t len = strlen(text_input);
    if (len >= 12) return;
    if (ch == ':' && (len >= 2) && !strchr(text_input, ':')) {
        text_input[len] = ':';
        text_input[len + 1] = '$';
        text_input[len + 2] = 0;
        return;
    }
    ch = toupper(ch);
    if (!isxdigit(ch)) return;
    
    unsigned digit_count = 0;
    const char *s = text_input + 1;
    while (*s) {
        if (*s == ':') {
            s += 2;
            digit_count = 0;
        }
        else {
            s++;
            digit_count++;
        }
    }
    
    if (digit_count == 4) return;

    text_input[len] = ch;
    text_input[len + 1] = 0;
}

static void change_cheat_address(unsigned index)
{
    strcpy(text_input_title, "Enter Cheat Address");
    text_input_title2[0] = 0;
    strcpy(text_input, get_cheat_address(0));
    gui_state = TEXT_INPUT;
    text_input_callback = change_cheat_address_callback;
    SDL_StartTextInput();
}

static const char *get_cheat_value(unsigned index)
{
    static char ret[4];
    sprintf(ret, "$%02X", current_cheat->value);
    
    return ret;
}

static void change_cheat_value_callback(char ch)
{
    if (ch == '\b' && text_input[1]) {
        size_t len = strlen(text_input);
        text_input[len - 1] = 0;
        return;
    }
    if (ch == '\n') {
        uint8_t value = 0;
        
        const char *s = text_input + 1;
        while (*s) {
            if (*s >= '0' && *s <= '9'){
                value *= 0x10;
                value += *s - '0';
            }
            else if (*s >= 'A' && *s <= 'F'){
                value *= 0x10;
                value += *s - 'A' + 10;
            }
            s++;
        }
        
        GB_update_cheat(&gb,
                        current_cheat,
                        current_cheat->description,
                        current_cheat->address,
                        current_cheat->bank,
                        value,
                        current_cheat->old_value,
                        current_cheat->use_old_value,
                        current_cheat->enabled);
        save_cheats();
        gui_state = SHOWING_MENU;
        SDL_StopTextInput();
        return;
    }
    if (!isxdigit(ch)) return;
    ch = toupper(ch);
    size_t len = strlen(text_input);
    if (len == 3) {
        text_input[1] = text_input[2];
        text_input[2] = ch;
        return;
    }
        
    text_input[len] = ch;
    text_input[len + 1] = 0;
}

static void change_cheat_value(unsigned index)
{
    strcpy(text_input_title, "Enter Cheat Value");
    text_input_title2[0] = 0;
    strcpy(text_input, get_cheat_value(0));
    gui_state = TEXT_INPUT;
    text_input_callback = change_cheat_value_callback;
    SDL_StartTextInput();
}

static const char *get_cheat_old_value(unsigned index)
{
    if (!current_cheat->use_old_value) {
        return "Any";
    }
    static char ret[4];
    sprintf(ret, "$%02X", current_cheat->old_value);
    
    return ret;
}

static void change_cheat_old_value_callback(char ch)
{
    if (ch == '\b' && strcmp(text_input, "Any") != 0) {
        size_t len = strlen(text_input);
        if (len == 2) {
            strcpy(text_input, "Any");
            return;
        }
        text_input[len - 1] = 0;
        return;
    }
    if (ch == '\n') {
        uint8_t value = 0;
        
        bool use_old_value = strcmp(text_input, "Any") != 0;
        if (use_old_value) {
            const char *s = text_input + 1;
            while (*s) {
                if (*s >= '0' && *s <= '9'){
                    value *= 0x10;
                    value += *s - '0';
                }
                else if (*s >= 'A' && *s <= 'F'){
                    value *= 0x10;
                    value += *s - 'A' + 10;
                }
                s++;
            }
        }
        
        GB_update_cheat(&gb,
                        current_cheat,
                        current_cheat->description,
                        current_cheat->address,
                        current_cheat->bank,
                        current_cheat->value,
                        value,
                        use_old_value,
                        current_cheat->enabled);
        save_cheats();
        gui_state = SHOWING_MENU;
        SDL_StopTextInput();
        return;
    }
    if (!isxdigit(ch)) return;
    ch = toupper(ch);
    if (strcmp(text_input, "Any") == 0) {
        strcpy(text_input, "$");
    }
    size_t len = strlen(text_input);
    if (len == 3) {
        text_input[1] = text_input[2];
        text_input[2] = ch;
        return;
    }
    
    text_input[len] = ch;
    text_input[len + 1] = 0;
}

static void change_cheat_old_value(unsigned index)
{
    strcpy(text_input_title, "Enter Cheat Old Value");
    text_input_title2[0] = 0;
    strcpy(text_input, get_cheat_old_value(0));
    gui_state = TEXT_INPUT;
    text_input_callback = change_cheat_old_value_callback;
    SDL_StartTextInput();
}

static void enter_cheats_menu(unsigned index);

static void delete_cheat(unsigned index)
{
    GB_remove_cheat(&gb, current_cheat);
    save_cheats();
    enter_cheats_menu(0);
}

struct menu_item modify_cheat_menu[] = {
    {"", rename_cheat},
    {"Enable", toggle_cheat, active_cheat_checkbox},
    {"Address:", change_cheat_address, get_cheat_address},
    {"Value:", change_cheat_value, get_cheat_value},
    {"Old Value:", change_cheat_old_value, get_cheat_old_value},
    {"Delete Cheat", delete_cheat},
    {"Back", enter_cheats_menu},
    {NULL,}
};

static void toggle_cheats(unsigned index)
{
    GB_set_cheats_enabled(&gb, !GB_cheats_enabled(&gb));
}

static void add_cheat(unsigned index)
{
    current_cheat = GB_add_cheat(&gb, "New Cheat", 0, 0, 0, 0, false, true);
    modify_cheat_menu[0].string = current_cheat->description;
    current_menu = modify_cheat_menu;
    current_selection = 0;
    scroll = 0;
    save_cheats();
}

static void import_cheat_callback(char ch)
{
    if (ch == '\b' && text_input[0]) {
        size_t len = strlen(text_input);
        text_input[len - 1] = 0;
        return;
    }
    if (ch == '\n') {
        if (!text_input[0]) {
            gui_state = SHOWING_MENU;
            SDL_StopTextInput();
            return;
        }
        
        current_cheat = GB_import_cheat(&gb, text_input, "Imported Cheat", true);
        if (current_cheat) {
            gui_state = SHOWING_MENU;
            modify_cheat_menu[0].string = current_cheat->description;
            current_menu = modify_cheat_menu;
            current_selection = 0;
            scroll = 0;
            save_cheats();
            SDL_StopTextInput();
            return;
        }
        
        strcpy(text_input_title, "Invalid Code.");
        strcpy(text_input_title2, "Press Enter to Cancel");
        text_input[0] = 0;
        return;
    }
    if (ch != '-' && !isxdigit(ch)) return;
    ch = toupper(ch);
    size_t len = strlen(text_input);
    if (len >= 12) {
        return;
    }
    
    text_input[len] = ch;
    text_input[len + 1] = 0;
    if (text_input_title[0] != 'E') {
        strcpy(text_input_title, "Enter a GameShark");
        strcpy(text_input_title2, "or Game Genie Code");
    }

}


static void import_cheat(unsigned index)
{
    strcpy(text_input_title, "Enter a GameShark");
    strcpy(text_input_title2, "or Game Genie Code");
    text_input[0] = 0;
    gui_state = TEXT_INPUT;
    text_input_callback = import_cheat_callback;
    save_cheats();
    SDL_StartTextInput();
}

static void modify_cheat(unsigned index)
{
    const GB_cheat_t *const *cheats = GB_get_cheats(&gb, NULL);
    current_cheat = cheats[index - 3];
    modify_cheat_menu[0].string = current_cheat->description;
    current_menu = modify_cheat_menu;
    current_selection = 0;
    scroll = 0;
}

static const char *checkbox_for_cheat(unsigned index)
{
    const GB_cheat_t *const *cheats = GB_get_cheats(&gb, NULL);
    return cheats[index - 3]->enabled? CHECKBOX_ON_STRING : CHECKBOX_OFF_STRING;
}

static const char *cheats_global_checkbox(unsigned index)
{
    return GB_cheats_enabled(&gb)? CHECKBOX_ON_STRING : CHECKBOX_OFF_STRING;
}

static void enter_cheats_menu(unsigned index)
{
    struct menu_item *cheats_menu = NULL;
    if (cheats_menu) {
        free(cheats_menu);
    }
    size_t cheat_count;
    const GB_cheat_t *const *cheats = GB_get_cheats(&gb, &cheat_count);
    cheats_menu = calloc(cheat_count + 5, sizeof(struct menu_item));
    cheats_menu[0] = (struct menu_item){"New Cheat", add_cheat};
    cheats_menu[1] = (struct menu_item){"Import Cheat", import_cheat};
    cheats_menu[2] = (struct menu_item){"Enable Cheats", toggle_cheats, cheats_global_checkbox};
    for (size_t i = 0; i < cheat_count; i++) {
        cheats_menu[i + 3] = (struct menu_item){cheats[i]->description, modify_cheat, checkbox_for_cheat};
    }
    cheats_menu[cheat_count + 3] = (struct menu_item){"Back", return_to_root_menu};
    current_menu = cheats_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

static const struct menu_item paused_menu[] = {
    {"Resume", NULL},
    {"Open ROM", open_rom},
    {"Hot Swap Cartridge", cart_swap},
    {"Options", enter_options_menu},
    {"Cheats", enter_cheats_menu},
    {audio_recording_menu_item, toggle_audio_recording},
    {"Help & About", enter_help_menu},
    {"Sponsor SameBoy", sponsor},
    {"Quit SameBoy", item_exit},
    {NULL,}
};

static struct menu_item nonpaused_menu[sizeof(paused_menu) / sizeof(paused_menu[0]) - 3];

static void __attribute__((constructor)) build_nonpaused_menu(void)
{
    const struct menu_item *in = paused_menu;
    struct menu_item *out = nonpaused_menu;
    while (in->string) {
        if (in->handler == NULL || in->handler == cart_swap || in->handler == enter_cheats_menu) {
            in++;
            continue;
        }
        *out = *in;
        out++;
        in++;
    }
}

static const struct menu_item help_menu[] = {
    {"Shortcuts", item_help},
    {"Debugger Help", debugger_help},
    {"About SameBoy", about},
    {"Back", return_to_root_menu},
    {NULL,}
};

static void enter_help_menu(unsigned index)
{
    current_menu = help_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}


static void cycle_model(unsigned index)
{
    switch (configuration.model) {
        case MODEL_DMG:  configuration.model = MODEL_MGB;  break;
        case MODEL_MGB:  configuration.model = MODEL_SGB;  break;
        case MODEL_SGB:  configuration.model = MODEL_CGB;  break;
        case MODEL_CGB:  configuration.model = MODEL_AGB;  break;
        case MODEL_AGB:  configuration.model = MODEL_AUTO; break;
        case MODEL_AUTO: configuration.model = MODEL_DMG;  break;
        default: configuration.model = MODEL_AUTO;
    }
    pending_command = GB_SDL_RESET_COMMAND;
}

static void cycle_model_backwards(unsigned index)
{
    switch (configuration.model) {
        case MODEL_MGB:  configuration.model = MODEL_DMG;  break;
        case MODEL_SGB:  configuration.model = MODEL_MGB;  break;
        case MODEL_CGB:  configuration.model = MODEL_SGB;  break;
        case MODEL_AGB:  configuration.model = MODEL_CGB;  break;
        case MODEL_AUTO: configuration.model = MODEL_AGB;  break;
        case MODEL_DMG:  configuration.model = MODEL_AUTO; break;
        default: configuration.model = MODEL_AUTO;
    }
    pending_command = GB_SDL_RESET_COMMAND;
}

static const char *current_model_string(unsigned index)
{
    return GB_inline_const(const char *[], {"Game Boy", "Game Boy Color", "Game Boy Advance", "Super Game Boy", "Game Boy Pocket", "Pick Automatically"})
        [configuration.model];
}

static void cycle_cgb_revision(unsigned index)
{
    
    if (configuration.cgb_revision == GB_MODEL_CGB_E - GB_MODEL_CGB_0) {
        configuration.cgb_revision = 0;
    }
    else {
        configuration.cgb_revision++;
    }
    pending_command = GB_SDL_RESET_COMMAND;
}

static void cycle_cgb_revision_backwards(unsigned index)
{
    if (configuration.cgb_revision == 0) {
        configuration.cgb_revision = GB_MODEL_CGB_E - GB_MODEL_CGB_0;
    }
    else {
        configuration.cgb_revision--;
    }
    pending_command = GB_SDL_RESET_COMMAND;
}

static const char *current_cgb_revision_string(unsigned index)
{
    return GB_inline_const(const char *[], {
        "CPU CGB 0",
        "CPU CGB A",
        "CPU CGB B",
        "CPU CGB C",
        "CPU CGB D",
        "CPU CGB E",
    })
    [configuration.cgb_revision];
}

static void cycle_sgb_revision(unsigned index)
{
    
    configuration.sgb_revision++;
    if (configuration.sgb_revision == SGB_MAX) {
        configuration.sgb_revision = 0;
    }
    pending_command = GB_SDL_RESET_COMMAND;
}

static void cycle_sgb_revision_backwards(unsigned index)
{
    if (configuration.sgb_revision == 0) {
        configuration.sgb_revision = SGB_MAX;
    }
    configuration.sgb_revision--;
    pending_command = GB_SDL_RESET_COMMAND;
}

static const char *current_sgb_revision_string(unsigned index)
{
    return GB_inline_const(const char *[], {"Super Game Boy NTSC", "Super Game Boy PAL", "Super Game Boy 2"})
        [configuration.sgb_revision];
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

static const char *current_rewind_string(unsigned index)
{
    for (unsigned i = 0; i < sizeof(rewind_lengths) / sizeof(rewind_lengths[0]); i++) {
        if (configuration.rewind_length == rewind_lengths[i]) {
            return rewind_strings[i];
        }
    }
    return "Custom";
}

static const char *current_bootrom_string(unsigned index)
{
    if (!configuration.bootrom_path[0]) {
        return "Built-in Boot ROMs";
    }
    size_t length = strlen(configuration.bootrom_path);
    static char ret[24] = {0,};
    if (length <= 23) {
        strcpy(ret, configuration.bootrom_path);
    }
    else {
        memcpy(ret, configuration.bootrom_path, 11);
        memcpy(ret + 12, configuration.bootrom_path + length - 11, 11);
    }
    for (unsigned i = 0; i < 24; i++) {
        if (ret[i] < 0) {
            ret[i] = MOJIBAKE_STRING[0];
        }
    }
    if (length > 23) {
        ret[11] = ELLIPSIS_STRING[0];
    }
    return ret;
}

static void toggle_bootrom(unsigned index)
{
    if (configuration.bootrom_path[0]) {
        configuration.bootrom_path[0] = 0;
    }
    else {
        char *folder = do_open_folder_dialog();
        if (!folder) return;
        if (strlen(folder) < sizeof(configuration.bootrom_path) - 1) {
            strcpy(configuration.bootrom_path, folder);
        }
        free(folder);
    }
}

static void toggle_rtc_mode(unsigned index)
{
    configuration.rtc_mode = !configuration.rtc_mode;
}

static const char *current_rtc_mode_string(unsigned index)
{
    switch (configuration.rtc_mode) {
        case GB_RTC_MODE_SYNC_TO_HOST: return "Sync to System Clock";
        case GB_RTC_MODE_ACCURATE: return "Accurate";
    }
    return "";
}

static void cycle_agb_revision(unsigned index)
{
    
    configuration.agb_revision ^= GB_MODEL_GBP_BIT;
    pending_command = GB_SDL_RESET_COMMAND;
}

static const char *current_agb_revision_string(unsigned index)
{
    if (configuration.agb_revision == GB_MODEL_GBP_A) {
        return "CPU AGB A (GBP)";
    }
    return "CPU AGB A (AGB)";
}

static const struct menu_item emulation_menu[] = {
    {"Emulated Model:", cycle_model, current_model_string, cycle_model_backwards},
    {"SGB Revision:", cycle_sgb_revision, current_sgb_revision_string, cycle_sgb_revision_backwards},
    {"GBC Revision:", cycle_cgb_revision, current_cgb_revision_string, cycle_cgb_revision_backwards},
    {"GBA Revision:", cycle_agb_revision, current_agb_revision_string, cycle_agb_revision},
    {"Boot ROMs Folder:", toggle_bootrom, current_bootrom_string, toggle_bootrom},
    {"Rewind Length:", cycle_rewind, current_rewind_string, cycle_rewind_backwards},
    {"Real Time Clock:", toggle_rtc_mode, current_rtc_mode_string, toggle_rtc_mode},
    {"Back", enter_options_menu},
    {NULL,}
};

static void enter_emulation_menu(unsigned index)
{
    current_menu = emulation_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

static const char *current_scaling_mode(unsigned index)
{
    return GB_inline_const(const char *[], {"Fill Entire Window", "Retain Aspect Ratio", "Retain Integer Factor"})
        [configuration.scaling_mode];
}

static const char *current_default_scale(unsigned index)
{
    return GB_inline_const(const char *[], {"1x", "2x", "3x", "4x", "5x", "6x", "7x", "8x"})
        [configuration.default_scale - 1];
}

const char *current_color_correction_mode(unsigned index)
{
    return GB_inline_const(const char *[], {"Disabled", "Correct Color Curves", "Modern - Balanced", "Modern - Boost Contrast", "Reduce Contrast", "Harsh Reality", "Modern - Accurate"})
        [configuration.color_correction_mode];
}

const char *current_color_temperature(unsigned index)
{
    static char ret[22];
    strcpy(ret, SLIDER_STRING);
    ret[configuration.color_temperature] = SELECTED_SLIDER_STRING[configuration.color_temperature];
    return ret;
}


const char *current_palette(unsigned index)
{
    if (configuration.dmg_palette == 4) {
        return configuration.dmg_palette_name;
    }
    return GB_inline_const(const char *[], {"Greyscale", "Lime (Game Boy)", "Olive (Pocket)", "Teal (Light)"})
        [configuration.dmg_palette];
}

const char *current_border_mode(unsigned index)
{
    return GB_inline_const(const char *[], {"SGB Only", "Never", "Always"})
        [configuration.border_mode];
}

static void cycle_scaling(unsigned index)
{
    configuration.scaling_mode++;
    if (configuration.scaling_mode == GB_SDL_SCALING_MAX) {
        configuration.scaling_mode = 0;
    }
    update_viewport();
    render_texture(NULL, NULL);
}

static void cycle_scaling_backwards(unsigned index)
{
    if (configuration.scaling_mode == 0) {
        configuration.scaling_mode = GB_SDL_SCALING_MAX - 1;
    }
    else {
        configuration.scaling_mode--;
    }
    update_viewport();
    render_texture(NULL, NULL);
    screen_manually_resized = false;
}

static void cycle_default_scale(unsigned index)
{
    if (configuration.default_scale == GB_SDL_DEFAULT_SCALE_MAX) {
        configuration.default_scale = 1;
    }
    else {
        configuration.default_scale++;
    }

    rescale_window();
    update_viewport();
    screen_manually_resized = false;
}

static void cycle_default_scale_backwards(unsigned index)
{
    if (configuration.default_scale == 1) {
        configuration.default_scale = GB_SDL_DEFAULT_SCALE_MAX;
    }
    else {
        configuration.default_scale--;
    }

    rescale_window();
    update_viewport();
    screen_manually_resized = false;
}

static void cycle_color_correction(unsigned index)
{
    if (configuration.color_correction_mode == GB_COLOR_CORRECTION_LOW_CONTRAST) {
        configuration.color_correction_mode = GB_COLOR_CORRECTION_DISABLED;
    }
    else if (configuration.color_correction_mode == GB_COLOR_CORRECTION_MODERN_BALANCED) {
        configuration.color_correction_mode = GB_COLOR_CORRECTION_MODERN_ACCURATE;
    }
    else if (configuration.color_correction_mode == GB_COLOR_CORRECTION_MODERN_ACCURATE) {
        configuration.color_correction_mode = GB_COLOR_CORRECTION_MODERN_BOOST_CONTRAST;
    }
    else {
        configuration.color_correction_mode++;
    }
}

static void cycle_color_correction_backwards(unsigned index)
{
    if (configuration.color_correction_mode == GB_COLOR_CORRECTION_DISABLED) {
        configuration.color_correction_mode = GB_COLOR_CORRECTION_LOW_CONTRAST;
    }
    else if (configuration.color_correction_mode == GB_COLOR_CORRECTION_MODERN_ACCURATE) {
        configuration.color_correction_mode = GB_COLOR_CORRECTION_MODERN_BALANCED;
    }
    else if (configuration.color_correction_mode == GB_COLOR_CORRECTION_MODERN_BOOST_CONTRAST) {
        configuration.color_correction_mode = GB_COLOR_CORRECTION_MODERN_ACCURATE;
    }
    else {
        configuration.color_correction_mode--;
    }
}

static void decrease_color_temperature(unsigned index)
{
    if (configuration.color_temperature < 20) {
        configuration.color_temperature++;
    }
}

static void increase_color_temperature(unsigned index)
{
    if (configuration.color_temperature > 0) {
        configuration.color_temperature--;
    }
}

const GB_palette_t *current_dmg_palette(void)
{
    typedef struct __attribute__ ((packed)) {
        uint32_t magic;
        uint8_t flags;
        struct GB_color_s colors[5];
        int32_t brightness_bias;
        uint32_t hue_bias;
        uint32_t hue_bias_strength;
    } theme_t;
    
    static theme_t theme;
    
    if (configuration.dmg_palette == 4) {
        char *path = resource_path("Palettes");
        sprintf(path + strlen(path), "/%s.sbp", configuration.dmg_palette_name);
        FILE *file = fopen(path, "rb");
        if (!file) return &GB_PALETTE_GREY;
        memset(&theme, 0, sizeof(theme));
        fread(&theme, sizeof(theme), 1, file);
        fclose(file);
#ifdef GB_BIG_ENDIAN
        theme.magic = __builtin_bswap32(theme.magic);
#endif
        if (theme.magic != 'SBPL') return &GB_PALETTE_GREY;
        return (GB_palette_t *)&theme.colors;
    }
    
    switch (configuration.dmg_palette) {
        case 1:  return &GB_PALETTE_DMG;
        case 2:  return &GB_PALETTE_MGB;
        case 3:  return &GB_PALETTE_GBL;
        default: return &GB_PALETTE_GREY;
    }
}

static void update_gui_palette(void)
{
    const GB_palette_t *palette = current_dmg_palette();
    
    SDL_Color colors[4];
    for (unsigned i = 4; i--; ) {
        gui_palette_native[i] = SDL_MapRGB(pixel_format, palette->colors[i].r, palette->colors[i].g, palette->colors[i].b);
        colors[i].r = palette->colors[i].r;
        colors[i].g = palette->colors[i].g;
        colors[i].b = palette->colors[i].b;
    }
    
    SDL_Surface *background = SDL_LoadBMP(resource_path("background.bmp"));
    
    /* Create a blank background if background.bmp could not be loaded */
    if (!background) {
        background = SDL_CreateRGBSurface(0, 160, 144, 8, 0, 0, 0, 0);
    }
    SDL_SetPaletteColors(background->format->palette, colors, 0, 4);
    converted_background = SDL_ConvertSurface(background, pixel_format, 0);
    SDL_FreeSurface(background);
}

static void cycle_palette(unsigned index)
{
    if (configuration.dmg_palette == 3) {
        if (n_custom_palettes == 0) {
            configuration.dmg_palette = 0;
        }
        else {
            configuration.dmg_palette = 4;
            strcpy(configuration.dmg_palette_name, custom_palettes[0]);
        }
    }
    else if (configuration.dmg_palette == 4) {
        for (unsigned i = 0; i < n_custom_palettes; i++) {
            if (strcmp(custom_palettes[i], configuration.dmg_palette_name) == 0) {
                if (i == n_custom_palettes - 1) {
                    configuration.dmg_palette = 0;
                }
                else {
                    strcpy(configuration.dmg_palette_name, custom_palettes[i + 1]);
                }
                break;
            }
        }
    }
    else {
        configuration.dmg_palette++;
    }
    configuration.gui_palette_enabled = true;
    update_gui_palette();
}

static void cycle_palette_backwards(unsigned index)
{
    if (configuration.dmg_palette == 0) {
        if (n_custom_palettes == 0) {
            configuration.dmg_palette = 3;
        }
        else {
            configuration.dmg_palette = 4;
            strcpy(configuration.dmg_palette_name, custom_palettes[n_custom_palettes - 1]);
        }
    }
    else if (configuration.dmg_palette == 4) {
        for (unsigned i = 0; i < n_custom_palettes; i++) {
            if (strcmp(custom_palettes[i], configuration.dmg_palette_name) == 0) {
                if (i == 0) {
                    configuration.dmg_palette = 3;
                }
                else {
                    strcpy(configuration.dmg_palette_name, custom_palettes[i - 1]);
                }
                break;
            }
        }
    }
    else {
        configuration.dmg_palette--;
    }
    configuration.gui_palette_enabled = true;
    update_gui_palette();
}

static void cycle_border_mode(unsigned index)
{
    if (configuration.border_mode == GB_BORDER_ALWAYS) {
        configuration.border_mode = GB_BORDER_SGB;
    }
    else {
        configuration.border_mode++;
    }
}

static void cycle_border_mode_backwards(unsigned index)
{
    if (configuration.border_mode == GB_BORDER_SGB) {
        configuration.border_mode = GB_BORDER_ALWAYS;
    }
    else {
        configuration.border_mode--;
    }
}

extern bool uses_gl(void);
struct shader_name {
    const char *file_name;
    const char *display_name;
} shaders[] =
{
    {"NearestNeighbor", "Nearest Neighbor"},
    {"Bilinear", "Bilinear"},
    {"SmoothBilinear", "Smooth Bilinear"},
    {"MonoLCD", "Monochrome LCD"},
    {"LCD", "LCD Display"},
    {"CRT", "CRT Display"},
    {"FlatCRT", "Flat CRT Display"},
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
    if (!uses_gl()) return;
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
    if (!uses_gl()) return;
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
static const char *current_filter_name(unsigned index)
{
    if (!uses_gl()) return "Requires OpenGL 3.2+";
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

static void cycle_blending_mode(unsigned index)
{
        if (!uses_gl()) return;
    if (configuration.blending_mode == GB_FRAME_BLENDING_MODE_ACCURATE) {
        configuration.blending_mode = GB_FRAME_BLENDING_MODE_DISABLED;
    }
    else {
        configuration.blending_mode++;
    }
}

static void cycle_blending_mode_backwards(unsigned index)
{
    if (!uses_gl()) return;
    if (configuration.blending_mode == GB_FRAME_BLENDING_MODE_DISABLED) {
        configuration.blending_mode = GB_FRAME_BLENDING_MODE_ACCURATE;
    }
    else {
        configuration.blending_mode--;
    }
}

static const char *blending_mode_string(unsigned index)
{
    if (!uses_gl()) return "Requires OpenGL 3.2+";
    return GB_inline_const(const char *[], {"Disabled", "Simple", "Accurate"})
        [configuration.blending_mode];
}

static void toggle_osd(unsigned index)
{
    osd_countdown = 0;
    configuration.osd = !configuration.osd;
}

static const char *current_osd_mode(unsigned index)
{
    return configuration.osd? "Enabled" : "Disabled";
}

#ifdef _WIN32

// Don't use the standard header definitions because we might not have the newest headers
typedef enum {
    DWM_CORNER_DEFAULT = 0,
    DWM_CORNER_SQUARE = 1,
    DWM_CORNER_ROUND = 2,
    DWM_CORNER_ROUNDSMALL = 3
} DMW_corner_settings_t;

#define DWM_CORNER_PREFERENCE 33

void configure_window_corners(void)
{
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);
    HWND hwnd = wmInfo.info.win.window;
    DMW_corner_settings_t pref = configuration.disable_rounded_corners? DWM_CORNER_SQUARE : DWM_CORNER_DEFAULT;
    DwmSetWindowAttribute(hwnd, DWM_CORNER_PREFERENCE, &pref, sizeof(pref));
}

static void toggle_corners(unsigned index)
{
    configuration.disable_rounded_corners = !configuration.disable_rounded_corners;
    configure_window_corners();
}

static const char *current_corner_mode(unsigned index)
{
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);
    HWND hwnd = wmInfo.info.win.window;
    DMW_corner_settings_t pref;
    
    if (DwmGetWindowAttribute(hwnd, DWM_CORNER_PREFERENCE, &pref, sizeof(pref)) ||
        pref == DWM_CORNER_SQUARE) {
        return "Square";
    }
    return "Rounded";
}
#endif

static const struct menu_item graphics_menu[] = {
    {"Scaling Mode:", cycle_scaling, current_scaling_mode, cycle_scaling_backwards},
    {"Default Window Scale:", cycle_default_scale, current_default_scale, cycle_default_scale_backwards},
    {"Scaling Filter:", cycle_filter, current_filter_name, cycle_filter_backwards},
    {"Color Correction:", cycle_color_correction, current_color_correction_mode, cycle_color_correction_backwards},
    {"Ambient Light Temp.:", decrease_color_temperature, current_color_temperature, increase_color_temperature},
    {"Frame Blending:", cycle_blending_mode, blending_mode_string, cycle_blending_mode_backwards},
    {"Mono Palette:", cycle_palette, current_palette, cycle_palette_backwards},
    {"Display Border:", cycle_border_mode, current_border_mode, cycle_border_mode_backwards},
    {"On-Screen Display:", toggle_osd, current_osd_mode, toggle_osd},
#ifdef _WIN32
    {"Window Corners:", toggle_corners, current_corner_mode, toggle_corners},
#endif
    {"Back", enter_options_menu},
    {NULL,}
};

static void enter_graphics_menu(unsigned index)
{
    current_menu = graphics_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

static const char *highpass_filter_string(unsigned index)
{
    return GB_inline_const(const char *[], {"None (Keep DC Offset)", "Accurate", "Preserve Waveform"})
        [configuration.highpass_mode];
}

static void cycle_highpass_filter(unsigned index)
{
    configuration.highpass_mode++;
    if (configuration.highpass_mode == GB_HIGHPASS_MAX) {
        configuration.highpass_mode = 0;
    }
}

static void cycle_highpass_filter_backwards(unsigned index)
{
    if (configuration.highpass_mode == 0) {
        configuration.highpass_mode = GB_HIGHPASS_MAX - 1;
    }
    else {
        configuration.highpass_mode--;
    }
}

static const char *volume_string(unsigned index)
{
    static char ret[5];
    sprintf(ret, "%d%%", configuration.volume);
    return ret;
}

static void increase_volume(unsigned index)
{
    configuration.volume += 5;
    if (configuration.volume > 100) {
        configuration.volume = 100;
    }
}

static void decrease_volume(unsigned index)
{
    configuration.volume -= 5;
    if (configuration.volume > 100) {
        configuration.volume = 0;
    }
}

static const char *interference_volume_string(unsigned index)
{
    static char ret[5];
    sprintf(ret, "%d%%", configuration.interference_volume);
    return ret;
}

static void increase_interference_volume(unsigned index)
{
    configuration.interference_volume += 5;
    if (configuration.interference_volume > 100) {
        configuration.interference_volume = 100;
    }
}

static void decrease_interference_volume(unsigned index)
{
    configuration.interference_volume -= 5;
    if (configuration.interference_volume > 100) {
        configuration.interference_volume = 0;
    }
}

static const char *audio_driver_string(unsigned index)
{
    return GB_audio_driver_name();
}

static const char *preferred_audio_driver_string(unsigned index)
{
    if (configuration.audio_driver[0] == 0) {
        return "Auto";
    }
    return configuration.audio_driver;
}

static void audio_driver_changed(void);

static void cycle_prefrered_audio_driver(unsigned index)
{
    audio_driver_changed();
    if (configuration.audio_driver[0] == 0) {
        strcpy(configuration.audio_driver, GB_audio_driver_name_at_index(0));
        return;
    }
    unsigned i = 0;
    while (true) {
        const char *name = GB_audio_driver_name_at_index(i);
        if (name[0] == 0) { // Not a supported driver? Switch to auto
            configuration.audio_driver[0] = 0;
            return;
        }
        if (strcmp(configuration.audio_driver, name) == 0) {
            strcpy(configuration.audio_driver, GB_audio_driver_name_at_index(i + 1));
            return;
        }
        i++;
    }
}

static void cycle_preferred_audio_driver_backwards(unsigned index)
{
    audio_driver_changed();
    if (configuration.audio_driver[0] == 0) {
        unsigned i = 0;
        while (true) {
            const char *name = GB_audio_driver_name_at_index(i);
            if (name[0] == 0) {
                strcpy(configuration.audio_driver, GB_audio_driver_name_at_index(i - 1));
                return;
            }
            i++;
        }
        return;
    }
    unsigned i = 0;
    while (true) {
        const char *name = GB_audio_driver_name_at_index(i);
        if (name[0] == 0) { // Not a supported driver? Switch to auto
            configuration.audio_driver[0] = 0;
            return;
        }
        if (strcmp(configuration.audio_driver, name) == 0) {
            strcpy(configuration.audio_driver, GB_audio_driver_name_at_index(i - 1));
            return;
        }
        i++;
    }
}

static struct menu_item audio_menu[] = {
    {"Highpass Filter:", cycle_highpass_filter, highpass_filter_string, cycle_highpass_filter_backwards},
    {"Volume:", increase_volume, volume_string, decrease_volume},
    {"Interference Volume:", increase_interference_volume, interference_volume_string, decrease_interference_volume},
    {"Preferred Audio Driver:", cycle_prefrered_audio_driver, preferred_audio_driver_string, cycle_preferred_audio_driver_backwards},
    {"Active Driver:", nop, audio_driver_string},
    {"Back", enter_options_menu},
    {NULL,}
};

static void audio_driver_changed(void)
{
    audio_menu[4].value_getter = NULL;
    audio_menu[4].string = "Relaunch to apply";
}

static void enter_audio_menu(unsigned index)
{
    current_menu = audio_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

static void modify_key(unsigned index)
{
    gui_state = WAITING_FOR_KEY;
}

static const char *key_name(unsigned index);

static const struct menu_item keyboard_menu[] = {
    {"Right:", modify_key, key_name,},
    {"Left:", modify_key, key_name,},
    {"Up:", modify_key, key_name,},
    {"Down:", modify_key, key_name,},
    {"A:", modify_key, key_name,},
    {"B:", modify_key, key_name,},
    {"Select:", modify_key, key_name,},
    {"Start:", modify_key, key_name,},
    {"Turbo:", modify_key, key_name,},
    {"Rewind:", modify_key, key_name,},
    {"Slow-Motion:", modify_key, key_name,},
    {"Rapid A:", modify_key, key_name,},
    {"Rapid B:", modify_key, key_name,},
    {"Back", enter_controls_menu},
    {NULL,}
};

static const char *key_name(unsigned index)
{
    SDL_Scancode code = index >= GB_CONF_KEYS_COUNT? configuration.keys_2[index - GB_CONF_KEYS_COUNT] : configuration.keys[index];
    if (!code) return "Not Set";
    return SDL_GetScancodeName(code);
}

static void enter_keyboard_menu(unsigned index)
{
    current_menu = keyboard_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

static unsigned joypad_index = 0;
static SDL_GameController *controller = NULL;
SDL_Haptic *haptic = NULL;
SDL_Joystick *joystick = NULL;

static const char *current_joypad_name(unsigned index)
{
    static char name[23] = {0,};
    const char *orig_name = joystick? SDL_JoystickName(joystick) : NULL;
    if (!orig_name) return "Not Found";
    unsigned i = 0;
    
    // SDL returns a name with repeated and trailing spaces
    while (*orig_name && i < sizeof(name) - 2) {
        if (orig_name[0] != ' ' || orig_name[1] != ' ') {
            name[i++] = *orig_name > 0? *orig_name : MOJIBAKE_STRING[0];
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
    
    if (haptic) {
        SDL_HapticClose(haptic);
        haptic = NULL;
    }
    
    if (controller) {
        SDL_GameControllerClose(controller);
        controller = NULL;
    }
    else if (joystick) {
        SDL_JoystickClose(joystick);
        joystick = NULL;
    }
    if ((controller = SDL_GameControllerOpen(joypad_index))) {
        joystick = SDL_GameControllerGetJoystick(controller);
    }
    else {
        joystick = SDL_JoystickOpen(joypad_index);
    }
    if (joystick) {
        haptic = SDL_HapticOpenFromJoystick(joystick);
    }
}

static void cycle_joypads_backwards(unsigned index)
{
    joypad_index--;
    if (joypad_index >= SDL_NumJoysticks()) {
        joypad_index = SDL_NumJoysticks() - 1;
    }
    
    if (haptic) {
        SDL_HapticClose(haptic);
        haptic = NULL;
    }
    
    if (controller) {
        SDL_GameControllerClose(controller);
        controller = NULL;
    }
    else if (joystick) {
        SDL_JoystickClose(joystick);
        joystick = NULL;
    }
    if ((controller = SDL_GameControllerOpen(joypad_index))) {
        joystick = SDL_GameControllerGetJoystick(controller);
    }
    else {
        joystick = SDL_JoystickOpen(joypad_index);
    }
    if (joystick) {
        haptic = SDL_HapticOpenFromJoystick(joystick);
    }}

static void detect_joypad_layout(unsigned index)
{
    gui_state = WAITING_FOR_JBUTTON;
    joypad_configuration_progress = 0;
    joypad_axis_temp = -1;
}

static void cycle_rumble_mode(unsigned index)
{
    if (configuration.rumble_mode == GB_RUMBLE_ALL_GAMES) {
        configuration.rumble_mode = GB_RUMBLE_DISABLED;
    }
    else {
        configuration.rumble_mode++;
    }
}

static void cycle_rumble_mode_backwards(unsigned index)
{
    if (configuration.rumble_mode == GB_RUMBLE_DISABLED) {
        configuration.rumble_mode = GB_RUMBLE_ALL_GAMES;
    }
    else {
        configuration.rumble_mode--;
    }
}

static const char *current_rumble_mode(unsigned index)
{
    return GB_inline_const(const char *[], {"Disabled", "Rumble Game Paks Only", "All Games"})
        [configuration.rumble_mode];
}

static void toggle_use_faux_analog_inputs(unsigned index)
{
    configuration.use_faux_analog_inputs ^= true;
}

static const char *current_faux_analog_inputs(unsigned index)
{
    return configuration.use_faux_analog_inputs? "Faux Analog" : "Digital";
}

static void toggle_allow_background_controllers(unsigned index)
{
    configuration.allow_background_controllers ^= true;
    
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,
                configuration.allow_background_controllers? "1" : "0");
}

static const char *current_background_control_mode(unsigned index)
{
    return configuration.allow_background_controllers? "Always" : "During Window Focus Only";
}

static void cycle_hotkey(unsigned index)
{
    if (configuration.hotkey_actions[index - 2] == HOTKEY_MAX) {
        configuration.hotkey_actions[index - 2] = 0;
    }
    else {
        configuration.hotkey_actions[index - 2]++;
    }
}

static void cycle_hotkey_backwards(unsigned index)
{
    if (configuration.hotkey_actions[index - 2] == 0) {
        configuration.hotkey_actions[index - 2] = HOTKEY_MAX;
    }
    else {
        configuration.hotkey_actions[index - 2]--;
    }
}

static const char *current_hotkey(unsigned index)
{
    return GB_inline_const(const char *[], {
        "None",
        "Toggle Pause",
        "Toggle Mute",
        "Reset", 
        "Quit SameBoy",
        "Save State Slot 1",
        "Load State Slot 1",
        "Save State Slot 2",
        "Load State Slot 2",
        "Save State Slot 3",
        "Load State Slot 3",
        "Save State Slot 4",
        "Load State Slot 4",
        "Save State Slot 5",
        "Load State Slot 5",
        "Save State Slot 6",
        "Load State Slot 6",
        "Save State Slot 7",
        "Load State Slot 7",
        "Save State Slot 8",
        "Load State Slot 8",
        "Save State Slot 9",
        "Load State Slot 9",
        "Save State Slot 10",
        "Load State Slot 10",
    }) [configuration.hotkey_actions[index - 2]];
}

static const struct menu_item joypad_menu[] = {
    {"Joypad:", cycle_joypads, current_joypad_name, cycle_joypads_backwards},
    {"Configure layout", detect_joypad_layout},
    {"Hotkey 1 Action:", cycle_hotkey, current_hotkey, cycle_hotkey_backwards},
    {"Hotkey 2 Action:", cycle_hotkey, current_hotkey, cycle_hotkey_backwards},
    {"Rumble Mode:", cycle_rumble_mode, current_rumble_mode, cycle_rumble_mode_backwards},
    {"Analog Stick Behavior:", toggle_use_faux_analog_inputs, current_faux_analog_inputs, toggle_use_faux_analog_inputs},
    {"Enable Control:", toggle_allow_background_controllers, current_background_control_mode, toggle_allow_background_controllers},
    {"Back", enter_controls_menu},
    {NULL,}
};

static void enter_joypad_menu(unsigned index)
{
    current_menu = joypad_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
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
        if ((controller = SDL_GameControllerOpen(0))) {
            joystick = SDL_GameControllerGetJoystick(controller);
        }
        else {
            joystick = SDL_JoystickOpen(0);
        }
    }
    if (joystick) {
        haptic = SDL_HapticOpenFromJoystick(joystick);
    }
}

static void toggle_mouse_control(unsigned index)
{
    configuration.allow_mouse_controls = !configuration.allow_mouse_controls;
}

static const char *mouse_control_string(unsigned index)
{
    return configuration.allow_mouse_controls? "Allow mouse control" : "Disallow mouse control";
}

static const struct menu_item controls_menu[] = {
    {"Keyboard Options", enter_keyboard_menu},
    {"Joypad Options", enter_joypad_menu},
    {"Motion-controlled games:", toggle_mouse_control, mouse_control_string, toggle_mouse_control},
    {"Back", enter_options_menu},
    {NULL,}
};

static void enter_controls_menu(unsigned index)
{
    current_menu = controls_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

static void toggle_audio_recording(unsigned index)
{
    if (!GB_is_inited(&gb)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Cannot start audio recording, open a ROM file first.", window);
        return;
    }
    static bool is_recording = false;
    if (is_recording) {
        is_recording = false;
        show_osd_text("Audio recording ended");
        int error = GB_stop_audio_recording(&gb);
        if (error) {
            char *message = NULL;
            asprintf(&message, "Could not finalize recording: %s", strerror(error));
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", message, window);
            free(message);
        }
        static const char item_string[] = "Start Audio Recording";
        memcpy(audio_recording_menu_item, item_string, sizeof(item_string));
        return;
    }
    char *filename = do_save_recording_dialog(GB_get_sample_rate(&gb));
    
    /* Drop events as it SDL seems to catch several in-dialog events */
    SDL_Event event;
    while (SDL_PollEvent(&event));
    
    if (filename) {
        GB_audio_format_t format = GB_AUDIO_FORMAT_RAW;
        size_t length = strlen(filename);
        if (length >= 5) {
            if (strcasecmp(".aiff", filename + length - 5) == 0) {
                format = GB_AUDIO_FORMAT_AIFF;
            }
            else if (strcasecmp(".aifc", filename + length - 5) == 0) {
                format = GB_AUDIO_FORMAT_AIFF;
            }
            else if (length >= 4) {
                if (strcasecmp(".aif", filename + length - 4) == 0) {
                    format = GB_AUDIO_FORMAT_AIFF;
                }
                else if (strcasecmp(".wav", filename + length - 4) == 0) {
                    format = GB_AUDIO_FORMAT_WAV;
                }
            }
        }
        
        int error = GB_start_audio_recording(&gb, filename, format);
        free(filename);
        if (error) {
            char *message = NULL;
            asprintf(&message, "Could not finalize recording: %s", strerror(error));
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", message, window);
            free(message);
            return;
        }
        
        is_recording = true;
        static const char item_string[] = "Stop Audio Recording";
        memcpy(audio_recording_menu_item, item_string, sizeof(item_string));
        show_osd_text("Audio recording started");
    }
}

void convert_mouse_coordinates(signed *x, signed *y)
{
    signed width = GB_get_screen_width(&gb);
    signed height = GB_get_screen_height(&gb);
    signed x_offset = (width - 160) / 2;
    signed y_offset = (height - 144) / 2;

    *x = (signed)(*x - rect.x / factor) * width / (signed)(rect.w / factor) - x_offset;
    *y = (signed)(*y - rect.y / factor) * height / (signed)(rect.h / factor) - y_offset;

    if (strcmp("CRT", configuration.filter) == 0) {
        *y = *y * 8 / 7;
        *y -= 144 / 16;
    }
}

void update_swap_interval(void)
{
    SDL_DisplayMode mode;
    SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window), &mode);
    if (mode.refresh_rate >= 60) {
        if (SDL_GL_SetSwapInterval(1)) {
            SDL_GL_SetSwapInterval(0);
        }
    }
    else {
        SDL_GL_SetSwapInterval(0);
    }
}

void run_gui(bool is_running)
{
    SDL_ShowCursor(SDL_ENABLE);
    connect_joypad();
    
    /* Draw the background screen */
    if (!converted_background) {
        if (configuration.gui_palette_enabled) {
            update_gui_palette();
        }
        else {
            SDL_Surface *background = SDL_LoadBMP(resource_path("background.bmp"));
            
            /* Create a blank background if background.bmp could not be loaded */
            if (!background) {
                background = SDL_CreateRGBSurface(0, 160, 144, 8, 0, 0, 0, 0);
            }
            SDL_SetPaletteColors(background->format->palette, gui_palette, 0, 4);
            converted_background = SDL_ConvertSurface(background, pixel_format, 0);
            SDL_FreeSurface(background);
    
            for (unsigned i = 4; i--; ) {
                gui_palette_native[i] = SDL_MapRGB(pixel_format, gui_palette[i].r, gui_palette[i].g, gui_palette[i].b);
            }
        }
    }

    unsigned width = GB_get_screen_width(&gb);
    unsigned height = GB_get_screen_height(&gb);
    unsigned x_offset = (width - 160) / 2;
    unsigned y_offset = (height - 144) / 2;
    uint32_t pixels[width * height];
    
    if (width != 160 || height != 144) {
        for (unsigned i = 0; i < width * height; i++) {
            pixels[i] = gui_palette_native[0];
        }
    }
    
    SDL_Event event = {0,};
    gui_state = is_running? SHOWING_MENU : SHOWING_DROP_MESSAGE;
    bool should_render = true;
    current_menu = root_menu = is_running? paused_menu : nonpaused_menu;
    recalculate_menu_height();
    current_selection = 0;
    scroll = 0;
    
    bool scrollbar_drag = false;
    signed scroll_mouse_start = 0;
    signed scroll_start = 0;
    while (true) {
        SDL_WaitEvent(&event);
        /* Convert Joypad and mouse events (We only generate down events) */
        if (gui_state != WAITING_FOR_KEY && gui_state != WAITING_FOR_JBUTTON && gui_state != TEXT_INPUT) {
            switch (event.type) {
                case SDL_KEYDOWN:
                    if (gui_state == WAITING_FOR_KEY) break;
                    if (event.key.keysym.mod != 0) break;
                    switch (event.key.keysym.scancode) {
                        // Do not remap these keys to prevent deadlocking
                        case SDL_SCANCODE_ESCAPE:
                        case SDL_SCANCODE_RETURN:
                        case SDL_SCANCODE_RIGHT:
                        case SDL_SCANCODE_LEFT:
                        case SDL_SCANCODE_UP:
                        case SDL_SCANCODE_DOWN:
                        case SDL_SCANCODE_H:
                        case SDL_SCANCODE_J:
                        case SDL_SCANCODE_K:
                        case SDL_SCANCODE_L:
                            break;
                            
                        default:
                                 if (event.key.keysym.scancode == configuration.keys[GB_KEY_RIGHT]) event.key.keysym.scancode = SDL_SCANCODE_RIGHT;
                            else if (event.key.keysym.scancode == configuration.keys[GB_KEY_LEFT]) event.key.keysym.scancode = SDL_SCANCODE_LEFT;
                            else if (event.key.keysym.scancode == configuration.keys[GB_KEY_UP]) event.key.keysym.scancode = SDL_SCANCODE_UP;
                            else if (event.key.keysym.scancode == configuration.keys[GB_KEY_DOWN]) event.key.keysym.scancode = SDL_SCANCODE_DOWN;
                            else if (event.key.keysym.scancode == configuration.keys[GB_KEY_A]) event.key.keysym.scancode = SDL_SCANCODE_RETURN;
                            else if (event.key.keysym.scancode == configuration.keys[GB_KEY_START]) event.key.keysym.scancode = SDL_SCANCODE_RETURN;
                            else if (event.key.keysym.scancode == configuration.keys[GB_KEY_B]) event.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
                            break;
                    }
                    break;

                case SDL_WINDOWEVENT:
                    should_render = true;
                    break;
                case SDL_MOUSEBUTTONUP:
                    scrollbar_drag = false;
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (gui_state == SHOWING_HELP) {
                        event.type = SDL_KEYDOWN;
                        event.key.keysym.scancode = SDL_SCANCODE_RETURN;
                    }
                    else if (gui_state == SHOWING_DROP_MESSAGE) {
                        event.type = SDL_KEYDOWN;
                        event.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
                    }
                    else if (gui_state == SHOWING_MENU) {
                        signed x = event.button.x;
                        signed y = event.button.y;
                        convert_mouse_coordinates(&x, &y);
                        if (x >= 160 - 6 && x < 160 && menu_height > 144) {
                            unsigned scrollbar_offset = (140 - scrollbar_size) * scroll / (menu_height - 144);
                            if (scrollbar_offset + scrollbar_size > 140) {
                                scrollbar_offset = 140 - scrollbar_size;
                            }
                            
                            if (y < scrollbar_offset || y > scrollbar_offset + scrollbar_size) {
                                scroll = (menu_height - 144) * y / 143;
                                should_render = true;
                            }

                            scrollbar_drag = true;
                            mouse_scroling = true;
                            scroll_mouse_start = y;
                            scroll_start = scroll;
                            break;
                        }
                        y += scroll;
                        
                        if (x < 0 || x >= 160 || y < 24) {
                            continue;
                        }
                        
                        unsigned item_y = 24;
                        unsigned index = 0;
                        for (const struct menu_item *item = current_menu; item->string; item++, index++) {
                            if (!item->backwards_handler) {
                                if (y >= item_y && y < item_y + 12) {
                                    break;
                                }
                                item_y += 12;
                            }
                            else {
                                if (y >= item_y && y < item_y + 24) {
                                    break;
                                }
                                item_y += 24;
                            }
                        }
                        
                        if (!current_menu[index].string) continue;
                        
                        current_selection = index;
                        event.type = SDL_KEYDOWN;
                        if (current_menu[index].backwards_handler) {
                            event.key.keysym.scancode = x < 80? SDL_SCANCODE_LEFT : SDL_SCANCODE_RIGHT;
                        }
                        else {
                            event.key.keysym.scancode = SDL_SCANCODE_RETURN;
                        }

                    }
                    break;
                case SDL_JOYBUTTONDOWN:
                    event.type = SDL_KEYDOWN;
                    joypad_button_t button = get_joypad_button(event.jbutton.button);
                    if (button == JOYPAD_BUTTON_A) {
                        event.key.keysym.scancode = SDL_SCANCODE_RETURN;
                    }
                    else if (button == JOYPAD_BUTTON_MENU || button == JOYPAD_BUTTON_B) {
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
                    break;
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
            case SDL_DISPLAYEVENT:
                update_swap_interval();
                break;
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
                    render_texture(NULL, NULL);
                    screen_manually_resized = true;
                }
                if (event.window.type == SDL_WINDOWEVENT_MOVED
#if SDL_COMPILEDVERSION > 2018
                    || event.window.type == SDL_WINDOWEVENT_DISPLAY_CHANGED
#endif
                    ) {
                    update_swap_interval();
                }
                break;
            }
            case SDL_DROPFILE: {
                if (GB_is_save_state(event.drop.file)) {
                    if (GB_is_inited(&gb)) {
                        dropped_state_file = event.drop.file;
                        pending_command = GB_SDL_LOAD_STATE_FROM_FILE_COMMAND;
                    }
                    else {
                        SDL_free(event.drop.file);
                    }
                    break;
                }
                else {
                    set_filename(event.drop.file, SDL_free);
                    pending_command = GB_SDL_NEW_FILE_COMMAND;
                    return;
                }
            }
            case SDL_JOYBUTTONDOWN: {
                if (gui_state == WAITING_FOR_JBUTTON && joypad_configuration_progress != JOYPAD_BUTTONS_MAX) {
                    should_render = true;
                    configuration.joypad_configuration[joypad_configuration_progress++] = event.jbutton.button;
                }
                break;
            }
            case SDL_JOYHATMOTION: {
                if (gui_state == WAITING_FOR_JBUTTON && joypad_configuration_progress == JOYPAD_BUTTON_RIGHT) {
                    should_render = true;
                    configuration.joypad_configuration[joypad_configuration_progress++] = -1;
                    configuration.joypad_configuration[joypad_configuration_progress++] = -1;
                    configuration.joypad_configuration[joypad_configuration_progress++] = -1;
                    configuration.joypad_configuration[joypad_configuration_progress++] = -1;
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
                
            case SDL_MOUSEWHEEL: {
                if (menu_height > 144) {
                    scroll -= event.wheel.y;
                    if (scroll < 0) {
                        scroll = 0;
                    }
                    if (scroll >= menu_height - 144) {
                        scroll = menu_height - 144;
                    }

                    mouse_scroling = true;
                    should_render = true;
                }
                break;
            }
                
            case SDL_MOUSEMOTION: {
                if (scrollbar_drag && scrollbar_size < 140 && scrollbar_size > 0) {
                    signed x = event.motion.x;
                    signed y = event.motion.y;
                    convert_mouse_coordinates(&x, &y);
                    signed delta = scroll_mouse_start - y;
                    scroll = scroll_start - delta * (signed)(menu_height - 144) / (signed)(140 - scrollbar_size);
                    if (scroll < 0) {
                        scroll = 0;
                    }
                    if (scroll >= menu_height - 144) {
                        scroll = menu_height - 144;
                    }

                    should_render = true;
                }
                break;
            }
                

            case SDL_TEXTINPUT:
                if (gui_state == TEXT_INPUT) {
                    char *s = event.text.text;
                    while (*s) {
                        text_input_callback(*(s++));
                    }
                    should_render = true;
                }
                break;
            case SDL_KEYDOWN:
                scrollbar_drag = false;
                if (gui_state == TEXT_INPUT) {
                    if (event.key.keysym.sym == SDLK_v && (event.key.keysym.mod & MODIFIER)) {
                        char *s = SDL_GetClipboardText();
                        while (*s) {
                            text_input_callback(*(s++));
                        }
                        should_render = true;
                    }
                    else if (event.key.keysym.scancode == SDL_SCANCODE_BACKSPACE) {
                        text_input_callback('\b');
                        should_render = true;
                    }
                    else if (event.key.keysym.scancode == SDL_SCANCODE_RETURN ||
                             event.key.keysym.scancode == SDL_SCANCODE_RETURN2 ||
                             event.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) {
                        text_input_callback('\n');
                        should_render = true;
                    }
                }
                else if (gui_state == WAITING_FOR_KEY) {
                    if (current_selection > 8) {
                        configuration.keys_2[current_selection - GB_CONF_KEYS_COUNT] = event.key.keysym.scancode;
                    }
                    else {
                        configuration.keys[current_selection] = event.key.keysym.scancode;
                    }
                    gui_state = SHOWING_MENU;
                    should_render = true;
                }
                else if (event_hotkey_code(&event) == SDL_SCANCODE_F && event.key.keysym.mod & MODIFIER) {
                    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == false) {
                        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                    }
                    else {
                        SDL_SetWindowFullscreen(window, 0);
                    }
                    update_swap_interval();
                    update_viewport();
                    screen_manually_resized = true;
                }
                else if (event_hotkey_code(&event) == SDL_SCANCODE_O) {
                    if (event.key.keysym.mod & MODIFIER) {
                        char *filename = do_open_rom_dialog();
                        if (filename) {
                            set_filename(filename, free);
                            pending_command = GB_SDL_NEW_FILE_COMMAND;
                            return;
                        }
                    }
                }
                else if (event.key.keysym.scancode == SDL_SCANCODE_RETURN && gui_state == WAITING_FOR_JBUTTON) {
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
                    if (gui_state == SHOWING_MENU && current_menu != root_menu) {
                        for (const struct menu_item *item = current_menu; item->string; item++) {
                            if (strcmp(item->string, "Back") == 0) {
                                item->handler(0);
                                goto handle_pending;
                                break;
                            }
                        }
                        should_render = true;
                    }
                    else if (is_running) {
                        SDL_StopTextInput();
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
                        mouse_scroling = false;
                        scroll = 0;
                        current_menu = root_menu;
                        recalculate_menu_height();
                        should_render = true;
                    }
                }
                else if (gui_state == SHOWING_MENU) {
                    if ((event.key.keysym.scancode == SDL_SCANCODE_DOWN || event.key.keysym.scancode == SDL_SCANCODE_J) && current_menu[current_selection + 1].string) {
                        current_selection++;
                        mouse_scroling = false;
                        should_render = true;
                    }
                    else if ((event.key.keysym.scancode == SDL_SCANCODE_UP || event.key.keysym.scancode == SDL_SCANCODE_K) && current_selection) {
                        current_selection--;
                        mouse_scroling = false;
                        should_render = true;
                    }
                    else if (event.key.keysym.scancode == SDL_SCANCODE_RETURN  && !current_menu[current_selection].backwards_handler) {
                        if (current_menu[current_selection].handler) {
                            current_menu[current_selection].handler(current_selection);
                            handle_pending:
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
                    else if ((event.key.keysym.scancode == SDL_SCANCODE_RIGHT || event.key.keysym.scancode == SDL_SCANCODE_L) && current_menu[current_selection].backwards_handler) {
                        current_menu[current_selection].handler(current_selection);
                        should_render = true;
                    }
                    else if ((event.key.keysym.scancode == SDL_SCANCODE_LEFT || event.key.keysym.scancode == SDL_SCANCODE_H) && current_menu[current_selection].backwards_handler) {
                        current_menu[current_selection].backwards_handler(current_selection);
                        should_render = true;
                    }
                }
                else if (gui_state == SHOWING_HELP) {
                    gui_state = SHOWING_MENU;
                    should_render = true;
                }
                break;
        }
        
        if (should_render) {
            should_render = false;
            rerender:
            SDL_LockSurface(converted_background);
            if (width == 160 && height == 144) {
                memcpy(pixels, converted_background->pixels, sizeof(pixels));
            }
            else {
                for (unsigned i = 0; i < width * height; i++) {
                    pixels[i] = gui_palette_native[0];
                }
                for (unsigned y = 0; y < 144; y++) {
                    memcpy(pixels + x_offset + width * (y + y_offset), ((uint32_t *)converted_background->pixels) + 160 * y, 160 * 4);
                }
            }
            SDL_UnlockSurface(converted_background);
            
            switch (gui_state) {
                case SHOWING_DROP_MESSAGE:
                    draw_styled_text(pixels, width, height, 8 + y_offset, "Press ESC for menu", gui_palette_native[3], gui_palette_native[0], STYLE_CENTER);
                    draw_styled_text(pixels, width, height, 116 + y_offset, "Drop a GB or GBC", gui_palette_native[3], gui_palette_native[0], STYLE_CENTER);
                    draw_styled_text(pixels, width, height, 128 + y_offset, "file to play", gui_palette_native[3], gui_palette_native[0], STYLE_CENTER);
                    break;
                case SHOWING_MENU:
                    draw_styled_text(pixels, width, height, 8 + y_offset, "SameBoy", gui_palette_native[3], gui_palette_native[0], STYLE_LEFT);
                    unsigned i = 0, y = 24;
                    for (const struct menu_item *item = current_menu; item->string; item++, i++) {
                        if (i == current_selection && !mouse_scroling) {
                            if (i == 0) {
                                if (y < scroll) {
                                    scroll = (y - 4) / 12 * 12;
                                    goto rerender;
                                }
                            }
                            else {
                                if (y < scroll + 24) {
                                    scroll = (y - 24) / 12 * 12;
                                    goto rerender;
                                }
                            }
                        }
                        if (i == current_selection && i == 0 && scroll != 0 && !mouse_scroling) {
                            scroll = 0;
                            goto rerender;
                        }
                        if (item->value_getter && !item->backwards_handler) {
                            char line[25];
                            snprintf(line, sizeof(line), "%s%*s", item->string, 23 - (unsigned)strlen(item->string), item->value_getter(i));
                            draw_styled_text(pixels, width, height, y + y_offset, line, gui_palette_native[3], gui_palette_native[0],
                                               i == current_selection ? STYLE_SELECTION : STYLE_INDENT);
                            y += 12;
                            
                        }
                        else {
                            if (item->value_getter) {
                                draw_styled_text(pixels, width, height, y + y_offset, item->string, gui_palette_native[3], gui_palette_native[0],
                                                 STYLE_LEFT);

                            }
                            else {
                                draw_styled_text(pixels, width, height, y + y_offset, item->string, gui_palette_native[3], gui_palette_native[0],
                                                   i == current_selection ? STYLE_SELECTION : STYLE_INDENT);
                            }
                            y += 12;
                            if (item->value_getter) {
                                draw_styled_text(pixels, width, height, y + y_offset - 1, item->value_getter(i), gui_palette_native[3], gui_palette_native[0],
                                                   i == current_selection ? STYLE_ARROWS : STYLE_CENTER);
                                y += 12;
                            }
                        }
                        if (i == current_selection && !mouse_scroling) {
                            if (y > scroll + 144) {
                                scroll = (y - 144) / 12 * 12;
                                if (scroll > menu_height - 144) {
                                    scroll = menu_height - 144;
                                }
                                goto rerender;
                            }
                        }

                    }
                    if (scrollbar_size) {
                        unsigned scrollbar_offset = (140 - scrollbar_size) * scroll / (menu_height - 144);
                        if (scrollbar_offset + scrollbar_size > 140) {
                            scrollbar_offset = 140 - scrollbar_size;
                        }
                        for (unsigned y = 0; y < 140; y++) {
                            uint32_t *pixel = pixels + x_offset + 156 + width * (y + y_offset + 2);
                            if (y >= scrollbar_offset && y < scrollbar_offset + scrollbar_size) {
                                pixel[0] = pixel[1] = gui_palette_native[2];
                            }
                            else {
                                pixel[0] = pixel[1] = gui_palette_native[1];
                            }
                            
                        }
                    }
                    break;
                case SHOWING_HELP:
                    draw_text(pixels, width, height, 2 + x_offset, 2 + y_offset, help[current_help_page], gui_palette_native[3], gui_palette_native[0], false);
                    break;
                case WAITING_FOR_KEY:
                    draw_styled_text(pixels, width, height, 68 + y_offset, "Press a Key", gui_palette_native[3], gui_palette_native[0], STYLE_CENTER);
                    break;
                case WAITING_FOR_JBUTTON:
                    draw_styled_text(pixels, width, height, 68 + y_offset,
                                       joypad_configuration_progress != JOYPAD_BUTTONS_MAX ? "Press button for" : "Move the Analog Stick",
                                       gui_palette_native[3], gui_palette_native[0], STYLE_CENTER);
                    draw_styled_text(pixels, width, height, 80 + y_offset,
                                      GB_inline_const(const char *[], {
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
                                           "Hotkey 1",
                                           "Hotkey 2",
                                           "Rapid A",
                                           "Rapid B",
                                           "",
                                      }) [joypad_configuration_progress],
                                      gui_palette_native[3], gui_palette_native[0], STYLE_CENTER);
                    draw_styled_text(pixels, width, height, 104 + y_offset, "Press Enter to skip", gui_palette_native[3], gui_palette_native[0], STYLE_CENTER);
                    break;
                case TEXT_INPUT:
                    draw_styled_text(pixels, width, height, 32 + y_offset, text_input_title, gui_palette_native[3], gui_palette_native[0], STYLE_CENTER);
                    draw_styled_text(pixels, width, height, 44 + y_offset, text_input_title2, gui_palette_native[3], gui_palette_native[0], STYLE_CENTER);
                    draw_styled_text(pixels, width, height, 64 + y_offset, text_input, gui_palette_native[3], gui_palette_native[0], STYLE_CENTER);
                    break;
            }
            
            render_texture(pixels, NULL);
#ifdef _WIN32
            /* Required for some Windows 10 machines, god knows why */
            render_texture(pixels, NULL);
#endif
        }
    }
}

static void __attribute__ ((constructor)) list_custom_palettes(void)
{
    char *path = resource_path("Palettes");
    if (!path) return;
    if (strlen(path) > 1024 - 30) {
        // path too long to safely concat filenames
        return;
    }
    DIR *dir = opendir(path);
    if (!dir) return;
    
    struct dirent *ent;
    
    while ((ent = readdir(dir))) {
        unsigned length = strlen(ent->d_name);
        if (length < 5 || length > 28) {
            continue;
        }
        if (strcmp(ent->d_name + length - 4, ".sbp")) continue;
        ent->d_name[length - 4] = 0;
        custom_palettes = realloc(custom_palettes,
                                  sizeof(custom_palettes[0]) * (n_custom_palettes + 1));
        custom_palettes[n_custom_palettes++] = strdup(ent->d_name);
    }
    
    closedir(dir);
}

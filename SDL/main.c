#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <OpenDialog/open_dialog.h>
#include <SDL.h>
#include <Core/gb.h>
#include "utils.h"
#include "gui.h"
#include "shader.h"
#include "audio/audio.h"
#include "console.h"
#include <fcntl.h>

#ifdef _WIN32
#include <Windows.h>
#include "windows_associations.h"
#endif

static bool stop_on_start = false;
GB_gameboy_t gb;
static bool paused = false;
static uint32_t pixel_buffer_1[256 * 224], pixel_buffer_2[256 * 224];
static uint32_t *active_pixel_buffer = pixel_buffer_1, *previous_pixel_buffer = pixel_buffer_2;
static bool underclock_down = false, rewind_down = false, do_rewind = false, rewind_paused = false, turbo_down = false;
static bool rapid_a = false, rapid_b = false;
static uint8_t rapid_a_count = 0, rapid_b_count = 0;
static double clock_mutliplier = 1.0;

char *filename = NULL;
static typeof(free) *free_function = NULL;
static char *battery_save_path_ptr = NULL;
static SDL_GLContext gl_context = NULL;
static bool console_supported = false;

bool uses_gl(void)
{
    return gl_context;
}

void rerender_screen(void)
{
    render_texture(active_pixel_buffer, configuration.blending_mode? previous_pixel_buffer : NULL);
#ifdef _WIN32
    /* Required for some Windows 10 machines, god knows why */
    render_texture(active_pixel_buffer, configuration.blending_mode? previous_pixel_buffer : NULL);
#endif
}

void set_filename(const char *new_filename, typeof(free) *new_free_function)
{
    if (filename && free_function) {
        free_function(filename);
    }
    filename = (char *) new_filename;
    free_function = new_free_function;
    GB_rewind_reset(&gb);
}

static char *completer(const char *substring, uintptr_t *context)
{
    if (!GB_is_inited(&gb)) return NULL;
    char *temp = strdup(substring);
    char *ret = GB_debugger_complete_substring(&gb, temp, context);
    free(temp);
    return ret;
}

static void log_callback(GB_gameboy_t *gb, const char *string, GB_log_attributes_t attributes)
{
    CON_attributes_t con_attributes = {0,};
    con_attributes.bold = attributes & GB_LOG_BOLD;
    con_attributes.underline = attributes & GB_LOG_UNDERLINE;
    if (attributes & GB_LOG_DASHED_UNDERLINE) {
        while (*string) {
            con_attributes.underline ^= true;
            CON_attributed_printf("%c", &con_attributes, *string);
            string++;
        }
    }
    else {
        CON_attributed_print(string, &con_attributes);
    }
}

static void handle_eof(void)
{
    CON_set_async_prompt("");
    char *line = CON_readline("Quit? [y]/n > ");
    if (line[0] == 'n' || line[0] == 'N') {
        free(line);
        CON_set_async_prompt("> ");
    }
    else {
        exit(0);
    }
}

static char *input_callback(GB_gameboy_t *gb)
{
    if (CON_no_csi_mode()) {
        fprintf(stdout, "> ");
        fflush(stdout);
    }
#ifdef _WIN32
    DWORD pid;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid == GetCurrentProcessId()) {
        BringWindowToTop(GetConsoleWindow());
    }
#endif
retry: {
    CON_set_async_prompt("Stopped> ");
    char *ret = CON_readline_async();
    if (!ret) {
#ifdef _WIN32
        HWND window = GetConsoleWindow();
        if (pending_command == GB_SDL_HIDE_DEBUGGER_COMMAND || !window) return strdup("c");
        ShowWindow(window, SW_SHOW);
#endif
        SDL_Event event;
        SDL_WaitEvent(&event);
        if (pending_command == GB_SDL_QUIT_COMMAND) {
            return strdup("c");
        }
        switch (event.type) {
            case SDL_DISPLAYEVENT:
                update_swap_interval();
                break;
                
            case SDL_WINDOWEVENT: {
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    screen_manually_resized = true;
                    update_viewport();
                }
                if (event.window.type == SDL_WINDOWEVENT_MOVED
#if SDL_COMPILEDVERSION > 2018
                    || event.window.type == SDL_WINDOWEVENT_DISPLAY_CHANGED
#endif
                    ) {
                    update_swap_interval();
                }
                rerender_screen();
                break;
            }
            case SDL_QUIT:
                pending_command = GB_SDL_QUIT_COMMAND;
                return strdup("c");
            case SDL_KEYDOWN:
                fputc('\a', stdout);
                fflush(stdout);
                break;
            default:
                break;
        }
        goto retry;
    }
    if (strcmp(ret, CON_EOF) == 0) {
        free(ret);
        handle_eof();
        goto retry;
    }
    else if (!CON_no_csi_mode()) {
        CON_attributes_t attributes = {.bold = true};
        CON_attributed_printf("> %s\n", &attributes, ret);
    }
    CON_set_async_prompt("> ");
    return ret;
}
}

static char *async_input_callback(GB_gameboy_t *gb)
{
retry: {
    char *ret = CON_readline_async();
    if (ret && strcmp(ret, CON_EOF) == 0) {
        handle_eof();
        free(ret);
        goto retry;
    }
    else if (ret) {
        CON_attributes_t attributes = {.bold = true};
        CON_attributed_printf("> %s\n", &attributes, ret);
    }
    return ret;
}
}


static char *captured_log = NULL;

static void log_capture_callback(GB_gameboy_t *gb, const char *string, GB_log_attributes_t attributes)
{
    size_t current_len = strlen(captured_log);
    size_t len_to_add = strlen(string);
    captured_log = realloc(captured_log, current_len + len_to_add + 1);
    memcpy(captured_log + current_len, string, len_to_add);
    captured_log[current_len + len_to_add] = 0;
}

static void *start_capturing_logs(void)
{
    void *previous = captured_log;
    captured_log = malloc(1);
    captured_log[0] = 0;
    GB_set_log_callback(&gb, log_capture_callback);
    return previous;
}

static void end_capturing_logs(bool show_popup, bool should_exit, uint32_t popup_flags, const char *title, void *previous)
{
    if (!previous) {
        GB_set_log_callback(&gb, console_supported? log_callback : NULL);
    }
    if (captured_log[0] != 0) {
        if (show_popup) {
            SDL_ShowSimpleMessageBox(popup_flags, title, captured_log, window);
        }
        if (should_exit) {
            exit(1);
        }
    }
    free(captured_log);
    captured_log = previous;
}

static void update_palette(void)
{
    GB_set_palette(&gb, current_dmg_palette());
}

static void screen_size_changed(bool resize_window)
{
    SDL_DestroyTexture(texture);
    texture = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_STREAMING,
                                GB_get_screen_width(&gb), GB_get_screen_height(&gb));
    
    SDL_SetWindowMinimumSize(window, GB_get_screen_width(&gb), GB_get_screen_height(&gb));
    
    if (resize_window) {
        signed current_window_width, current_window_height;
        SDL_GetWindowSize(window, &current_window_width, &current_window_height);

        signed width = GB_get_screen_width(&gb) * configuration.default_scale;
        signed height = GB_get_screen_height(&gb) * configuration.default_scale;
        signed x, y;
        SDL_GetWindowPosition(window, &x, &y);
        SDL_SetWindowSize(window, width, height);
        SDL_SetWindowPosition(window, x - (width - current_window_width) / 2, y - (height - current_window_height) / 2);
    }
    
    update_viewport();
}

static void open_menu(void)
{
    bool audio_playing = GB_audio_is_playing();
    if (audio_playing) {
        GB_audio_set_paused(true);
    }
    size_t previous_width = GB_get_screen_width(&gb);
    size_t previous_height = GB_get_screen_height(&gb);
    run_gui(true);
    rerender_screen();
    SDL_ShowCursor(SDL_DISABLE);
    if (audio_playing) {
        GB_audio_set_paused(false);
    }
    GB_set_color_correction_mode(&gb, configuration.color_correction_mode);
    GB_set_light_temperature(&gb, (configuration.color_temperature - 10.0) / 10.0);
    GB_set_interference_volume(&gb, configuration.interference_volume / 100.0);
    GB_set_border_mode(&gb, configuration.border_mode);
    update_palette();
    GB_set_highpass_filter_mode(&gb, configuration.highpass_mode);
    GB_set_rewind_length(&gb, configuration.rewind_length);
    GB_set_rtc_mode(&gb, configuration.rtc_mode);
    if (previous_width != GB_get_screen_width(&gb)) {
        signed current_window_width, current_window_height;
        SDL_GetWindowSize(window, &current_window_width, &current_window_height);

        screen_size_changed(current_window_width == previous_width * configuration.default_scale &&
                            current_window_height == previous_height * configuration.default_scale);
    }
}

static void console_line_ready(void)
{
    static SDL_Event event = {
        .type = SDL_USEREVENT
    };
    SDL_PushEvent(&event);
}

static void configure_console(void)
{
    CON_set_async_prompt("> ");
    CON_set_repeat_empty(true);
    CON_set_line_ready_callback(console_line_ready);
    GB_set_log_callback(&gb, log_callback);
    GB_set_input_callback(&gb, input_callback);
    GB_set_async_input_callback(&gb, async_input_callback);
}

static void handle_events(GB_gameboy_t *gb)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_DISPLAYEVENT:
                update_swap_interval();
                break;
            case SDL_QUIT:
                pending_command = GB_SDL_QUIT_COMMAND;
                break;
                
            case SDL_DROPFILE: {
                if (GB_is_save_state(event.drop.file)) {
                    dropped_state_file = event.drop.file;
                    pending_command = GB_SDL_LOAD_STATE_FROM_FILE_COMMAND;
                }
                else {
                    set_filename(event.drop.file, SDL_free);
                    pending_command = GB_SDL_NEW_FILE_COMMAND;
                }
                break;
            }
                
            case SDL_WINDOWEVENT: {
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    screen_manually_resized = true;
                    update_viewport();
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
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                if (GB_has_accelerometer(gb) && configuration.allow_mouse_controls) {
                    GB_set_key_state(gb, GB_KEY_A, event.type == SDL_MOUSEBUTTONDOWN);
                }
                break;
            }
                
            case SDL_MOUSEMOTION: {
                if (GB_has_accelerometer(gb) && configuration.allow_mouse_controls) {
                    signed x = event.motion.x;
                    signed y = event.motion.y;
                    convert_mouse_coordinates(&x, &y);
                    x = SDL_max(SDL_min(x, 160), 0);
                    y = SDL_max(SDL_min(y, 144), 0);
                    GB_set_accelerometer_values(gb, (x - 80) / -80.0, (y - 72) / -72.0);
                }
                break;
            }
                
            case SDL_JOYDEVICEREMOVED:
                if (joystick && event.jdevice.which == SDL_JoystickInstanceID(joystick)) {
                    SDL_JoystickClose(joystick);
                    joystick = NULL;
                }
            case SDL_JOYDEVICEADDED:
                connect_joypad();
                break;
                
            case SDL_JOYBUTTONUP:
            case SDL_JOYBUTTONDOWN: {
                joypad_button_t button = get_joypad_button(event.jbutton.button);
                if ((GB_key_t) button < GB_KEY_MAX) {
                    GB_set_key_state(gb, (GB_key_t) button, event.type == SDL_JOYBUTTONDOWN);
                }
                else if (button == JOYPAD_BUTTON_TURBO) {
                    GB_audio_clear_queue();
                    turbo_down = event.type == SDL_JOYBUTTONDOWN;
                    GB_set_turbo_mode(gb, turbo_down, turbo_down && rewind_down);
                }
                else if (button == JOYPAD_BUTTON_SLOW_MOTION) {
                    underclock_down = event.type == SDL_JOYBUTTONDOWN;
                }
                else if (button == JOYPAD_BUTTON_REWIND) {
                    rewind_down = event.type == SDL_JOYBUTTONDOWN;
                    if (event.type == SDL_JOYBUTTONUP) {
                        rewind_paused = false;
                    }
                    GB_set_turbo_mode(gb, turbo_down, turbo_down && rewind_down);
                }
                else if (button == JOYPAD_BUTTON_MENU && event.type == SDL_JOYBUTTONDOWN) {
                    open_menu();
                }
                else if ((button == JOYPAD_BUTTON_HOTKEY_1 || button == JOYPAD_BUTTON_HOTKEY_2) && event.type == SDL_JOYBUTTONDOWN) {
                    hotkey_action_t action = configuration.hotkey_actions[button - JOYPAD_BUTTON_HOTKEY_1];
                    switch (action) {
                        case HOTKEY_NONE:
                            break;
                        case HOTKEY_PAUSE:
                            paused = !paused;
                            break;
                        case HOTKEY_MUTE:
                            GB_audio_set_paused(GB_audio_is_playing());
                            break;
                        case HOTKEY_RESET:
                            pending_command = GB_SDL_RESET_COMMAND;
                            break;
                        case HOTKEY_QUIT:
                            pending_command = GB_SDL_QUIT_COMMAND;
                            break;
                        default:
                            command_parameter = (action - HOTKEY_SAVE_STATE_1) / 2 + 1;
                            pending_command = ((action - HOTKEY_SAVE_STATE_1) % 2)? GB_SDL_LOAD_STATE_COMMAND:GB_SDL_SAVE_STATE_COMMAND;
                            break;
                        case HOTKEY_SAVE_STATE_10:
                            command_parameter = 0;
                            pending_command = GB_SDL_SAVE_STATE_COMMAND;
                            break;
                        case HOTKEY_LOAD_STATE_10:
                            command_parameter = 0;
                            pending_command = GB_SDL_LOAD_STATE_COMMAND;
                            break;
                    }
                }
                else if (button == JOYPAD_BUTTON_RAPID_A) {
                    rapid_a = event.type == SDL_JOYBUTTONDOWN;
                    rapid_a_count = 0;
                    GB_set_key_state(gb, GB_KEY_A, event.type == SDL_JOYBUTTONDOWN);
                }
                else if (button == JOYPAD_BUTTON_RAPID_B) {
                    rapid_b = event.type == SDL_JOYBUTTONDOWN;
                    rapid_b_count = 0;
                    GB_set_key_state(gb, GB_KEY_B, event.type == SDL_JOYBUTTONDOWN);
                }
            }
            break;
                
            case SDL_JOYAXISMOTION: {
                static bool axis_active[2] = {false, false};
                static double accel_values[2] = {0, 0};
                static double axis_values[2] = {0, 0};
                joypad_axis_t axis = get_joypad_axis(event.jaxis.axis);
                if (axis == JOYPAD_AXISES_X) {
                    if (GB_has_accelerometer(gb)) {
                        accel_values[0] = event.jaxis.value / (double)32768.0;
                        GB_set_accelerometer_values(gb, -accel_values[0], -accel_values[1]);
                    }
                    else if (configuration.use_faux_analog_inputs) {
                        axis_values[0] = event.jaxis.value / (double)32768.0;
                    }
                    else if (event.jaxis.value > JOYSTICK_HIGH) {
                        axis_active[0] = true;
                        GB_set_use_faux_analog_inputs(gb, 0, false);
                        GB_set_key_state(gb, GB_KEY_RIGHT, true);
                        GB_set_key_state(gb, GB_KEY_LEFT, false);
                    }
                    else if (event.jaxis.value < -JOYSTICK_HIGH) {
                        axis_active[0] = true;
                        GB_set_use_faux_analog_inputs(gb, 0, false);
                        GB_set_key_state(gb, GB_KEY_RIGHT, false);
                        GB_set_key_state(gb, GB_KEY_LEFT, true);
                    }
                    else if (axis_active[0] && event.jaxis.value < JOYSTICK_LOW && event.jaxis.value > -JOYSTICK_LOW) {
                        axis_active[0] = false;
                        GB_set_key_state(gb, GB_KEY_RIGHT, false);
                        GB_set_key_state(gb, GB_KEY_LEFT, false);
                    }
                }
                else if (axis == JOYPAD_AXISES_Y) {
                    if (GB_has_accelerometer(gb)) {
                        accel_values[1] = event.jaxis.value / (double)32768.0;
                        GB_set_accelerometer_values(gb, -accel_values[0], -accel_values[1]);
                    }
                    else if (configuration.use_faux_analog_inputs) {
                        axis_values[1] = event.jaxis.value / (double)32768.0;
                    }
                    else if (event.jaxis.value > JOYSTICK_HIGH) {
                        axis_active[1] = true;
                        GB_set_use_faux_analog_inputs(gb, 0, false);
                        GB_set_key_state(gb, GB_KEY_DOWN, true);
                        GB_set_key_state(gb, GB_KEY_UP, false);
                    }
                    else if (event.jaxis.value < -JOYSTICK_HIGH) {
                        axis_active[1] = true;
                        GB_set_use_faux_analog_inputs(gb, 0, false);
                        GB_set_key_state(gb, GB_KEY_DOWN, false);
                        GB_set_key_state(gb, GB_KEY_UP, true);
                    }
                    else if (axis_active[1] && event.jaxis.value < JOYSTICK_LOW && event.jaxis.value > -JOYSTICK_LOW) {
                        axis_active[1] = false;
                        GB_set_key_state(gb, GB_KEY_DOWN, false);
                        GB_set_key_state(gb, GB_KEY_UP, false);
                    }
                }
                if (configuration.use_faux_analog_inputs && !GB_has_accelerometer(gb)) {
                    GB_set_use_faux_analog_inputs(gb, 0, true);
                    GB_set_faux_analog_inputs(gb, 0, axis_values[0], axis_values[1]);
                }
                break;
            }
                
            case SDL_JOYHATMOTION: {
                uint8_t value = event.jhat.value;
                int8_t updown =
                value == SDL_HAT_LEFTUP || value == SDL_HAT_UP || value == SDL_HAT_RIGHTUP ? -1 : (value == SDL_HAT_LEFTDOWN || value == SDL_HAT_DOWN || value == SDL_HAT_RIGHTDOWN ? 1 : 0);
                int8_t leftright =
                value == SDL_HAT_LEFTUP || value == SDL_HAT_LEFT || value == SDL_HAT_LEFTDOWN ? -1 : (value == SDL_HAT_RIGHTUP || value == SDL_HAT_RIGHT || value == SDL_HAT_RIGHTDOWN ? 1 : 0);
                
                GB_set_use_faux_analog_inputs(gb, 0, false);
                GB_set_key_state(gb, GB_KEY_LEFT, leftright == -1);
                GB_set_key_state(gb, GB_KEY_RIGHT, leftright == 1);
                GB_set_key_state(gb, GB_KEY_UP, updown == -1);
                GB_set_key_state(gb, GB_KEY_DOWN, updown == 1);
                break;
            };
                
            case SDL_KEYDOWN:
                switch (event_hotkey_code(&event)) {
                    case SDL_SCANCODE_ESCAPE: {
                        open_menu();
                        break;
                    }
                    case SDL_SCANCODE_C:
                        if (event.type == SDL_KEYDOWN && (event.key.keysym.mod & KMOD_CTRL)) {
                            pending_command = GB_SDL_DEBUGGER_INTERRUPT_COMMAND;
                        }
                        break;
                        
                    case SDL_SCANCODE_R:
                        if (event.key.keysym.mod & MODIFIER) {
                            pending_command = GB_SDL_RESET_COMMAND;
                            paused = false;
                        }
                        break;
                        
                    case SDL_SCANCODE_O: {
                        if (event.key.keysym.mod & MODIFIER) {
                            char *filename = do_open_rom_dialog();
                            if (filename) {
                                set_filename(filename, free);
                                pending_command = GB_SDL_NEW_FILE_COMMAND;
                            }
                        }
                        break;
                    }
                        
                    case SDL_SCANCODE_P:
                        if (event.key.keysym.mod & MODIFIER) {
                            paused = !paused;
                        }
                        break;
                    case SDL_SCANCODE_M:
                        if (event.key.keysym.mod & MODIFIER) {
#ifdef __APPLE__
                            // Can't override CMD+M (Minimize) in SDL
                            if (!(event.key.keysym.mod & KMOD_SHIFT)) {
                                break;
                            }
#endif
                            GB_audio_set_paused(GB_audio_is_playing());
                        }
                        break;
                        
                    case SDL_SCANCODE_F:
                        if (event.key.keysym.mod & MODIFIER) {
                            if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP)) {
                                SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                            }
                            else {
                                SDL_SetWindowFullscreen(window, 0);
                            }
                            update_swap_interval();
                            update_viewport();
                            screen_manually_resized = true;
                        }
                        break;
                        
                    default:
                        /* Save states */
                        if (event.key.keysym.scancode >= SDL_SCANCODE_1 && event.key.keysym.scancode <= SDL_SCANCODE_0) {
                            if (event.key.keysym.mod & MODIFIER) {
                                command_parameter = (event.key.keysym.scancode - SDL_SCANCODE_1 + 1) % 10;
                                
                                if (event.key.keysym.mod & KMOD_SHIFT) {
                                    pending_command = GB_SDL_LOAD_STATE_COMMAND;
                                }
                                else {
                                    pending_command = GB_SDL_SAVE_STATE_COMMAND;
                                }
                            }
                            else if ((event.key.keysym.mod & KMOD_ALT) && event.key.keysym.scancode <= SDL_SCANCODE_4) {
                                GB_channel_t channel = event.key.keysym.scancode - SDL_SCANCODE_1;
                                bool state = !GB_is_channel_muted(gb, channel);
                                
                                GB_set_channel_muted(gb, channel, state);
                                
                                static char message[18];
                                sprintf(message, "Channel %d %smuted", channel + 1, state? "" : "un");
                                show_osd_text(message);
                            }
                        }
                        break;
                }
            case SDL_KEYUP: // Fallthrough
                if (event.key.keysym.scancode == configuration.keys[GB_CONF_KEYS_TURBO]) {
                    turbo_down = event.type == SDL_KEYDOWN;
                    GB_audio_clear_queue();
                    GB_set_turbo_mode(gb, turbo_down, turbo_down && rewind_down);
                }
                else if (event.key.keysym.scancode == configuration.keys_2[GB_CONF_KEYS2_REWIND]) {
                    rewind_down = event.type == SDL_KEYDOWN;
                    if (event.type == SDL_KEYUP) {
                        rewind_paused = false;
                    }
                    GB_set_turbo_mode(gb, turbo_down, turbo_down && rewind_down);
                }
                else if (event.key.keysym.scancode == configuration.keys_2[GB_CONF_KEYS2_UNDERCLOCK]) {
                    underclock_down = event.type == SDL_KEYDOWN;
                }
                else if (event.key.keysym.scancode == configuration.keys_2[GB_CONF_KEYS2_RAPID_A]) {
                    rapid_a = event.type == SDL_KEYDOWN;
                    rapid_a_count = 0;
                    GB_set_key_state(gb, GB_KEY_A, event.type == SDL_KEYDOWN);
                }
                else if (event.key.keysym.scancode == configuration.keys_2[GB_CONF_KEYS2_RAPID_B]) {
                    rapid_b = event.type == SDL_KEYDOWN;
                    rapid_b_count = 0;
                    GB_set_key_state(gb, GB_KEY_B, event.type == SDL_KEYDOWN);
                }
                else {
                    for (unsigned i = 0; i < GB_KEY_MAX; i++) {
                        if (event.key.keysym.scancode == configuration.keys[i]) {
                            if (i <= GB_KEY_DOWN) {
                                GB_set_use_faux_analog_inputs(gb, 0, false);
                            }
                            GB_set_key_state(gb, i, event.type == SDL_KEYDOWN);
                        }
                    }
                }
                break;
            default:
                break;
        }
    }
}

static uint32_t rgb_encode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    return SDL_MapRGB(pixel_format, r, g, b);
}

static void vblank(GB_gameboy_t *gb, GB_vblank_type_t type)
{
    if (underclock_down && clock_mutliplier > 0.5) {
        clock_mutliplier -= 1.0/16;
        GB_set_clock_multiplier(gb, clock_mutliplier);
    }
    else if (!underclock_down && clock_mutliplier < 1.0) {
        clock_mutliplier += 1.0/16;
        GB_set_clock_multiplier(gb, clock_mutliplier);
    }
    
    if (rapid_a) {
        rapid_a_count++;
        GB_set_key_state(gb, GB_KEY_A, !(rapid_a_count & 2));
    }
    if (rapid_b) {
        rapid_b_count++;
        GB_set_key_state(gb, GB_KEY_B, !(rapid_b_count & 2));
    }
    
    if (turbo_down) {
        show_osd_text("Fast forward...");
    }
    else if (underclock_down) {
        show_osd_text("Slow motion...");
    }
    else if (rewind_down) {
        show_osd_text("Rewinding...");
    }
    
    if (osd_countdown && configuration.osd) {
        unsigned width = GB_get_screen_width(gb);
        unsigned height = GB_get_screen_height(gb);
        draw_text(active_pixel_buffer,
                  width, height, 8, height - 8 - osd_text_lines * 12, osd_text,
                  rgb_encode(gb, 255, 255, 255), rgb_encode(gb, 0, 0, 0),
                  true);
        osd_countdown--;
    }
    if (type != GB_VBLANK_TYPE_REPEAT) {
        if (configuration.blending_mode) {
            render_texture(active_pixel_buffer, previous_pixel_buffer);
            uint32_t *temp = active_pixel_buffer;
            active_pixel_buffer = previous_pixel_buffer;
            previous_pixel_buffer = temp;
            GB_set_pixels_output(gb, active_pixel_buffer);
        }
        else {
            render_texture(active_pixel_buffer, NULL);
        }
    }
    do_rewind = rewind_down;
    handle_events(gb);
}

static void rumble(GB_gameboy_t *gb, double amp)
{
    SDL_HapticRumblePlay(haptic, amp, 250);
}

static void debugger_interrupt(int ignore)
{
#ifndef _WIN32
    if (!GB_is_inited(&gb)) {
        exit(0);
    }
    if (GB_debugger_is_stopped(&gb)) {
        pending_command = GB_SDL_QUIT_COMMAND;
        console_line_ready(); // Force the debugger wait-loop to process the command
        return;
    }
#endif
    pending_command = GB_SDL_DEBUGGER_INTERRUPT_COMMAND;

}

#ifndef _WIN32
static void debugger_reset(int ignore)
{
    pending_command = GB_SDL_RESET_COMMAND;
}
#endif

static void gb_audio_callback(GB_gameboy_t *gb, GB_sample_t *sample)
{    
    if (turbo_down) {
        static unsigned skip = 0;
        skip++;
        if (skip == GB_audio_get_frequency() / 8) {
            skip = 0;
        }
        if (skip > GB_audio_get_frequency() / 16) {
            return;
        }
    }
    
    if (GB_audio_get_queue_length() > GB_audio_get_frequency() / 8) { // Maximum lag of 0.125s
        return;
    }
    
    if (configuration.volume != 100) {
        sample->left = sample->left * configuration.volume / 100;
        sample->right = sample->right * configuration.volume / 100;
    }
    
    GB_audio_queue_sample(sample);
    
}

#ifdef _WIN32
static BOOL windows_console_handler(DWORD signal)
{
    /*
     Hack: prevents process termination on console close
     https://twitter.com/yo_yo_yo_jbo/status/1904592584326218069
     Thanks JBO!
    */
    if (signal == CTRL_C_EVENT) {
        /* Only happens in no-csi mode */
        pending_command = GB_SDL_DEBUGGER_INTERRUPT_COMMAND;
        TerminateThread(GetCurrentThread(), 0);
    }
    
    pending_command = GB_SDL_HIDE_DEBUGGER_COMMAND;
    console_line_ready();
    TerminateThread(GetCurrentThread(), 0);
    return false;
}

static void initialize_windows_console(void)
{
    if (AllocConsole()) {
        SetConsoleTitle("SameBoy Debugger Console");
        freopen("CONIN$", "r", stdin);
        setvbuf(stdin, NULL, _IONBF, 0);
        freopen("CONOUT$", "w", stdout);
        setvbuf(stdout, NULL, _IONBF, 0);
        freopen("CONOUT$", "w", stderr);
        setvbuf(stderr, NULL, _IONBF, 0);
        
        console_supported = CON_start(completer);
        if (console_supported) {
            configure_console();
        }
        
        /* I would set a callback via SetConsoleCtrlHandler, but the function (CtrlRoutine) that
           eventually calls our callback takes a lock and  doesn't release it (as it expects the
           process to exit afterwards). The solution is to take a more violent approach and hook
           it instead. */
        
#if defined(__x86_64__) || defined(__i386__)
        uint8_t *patch_address = (void *)(GetProcAddress(GetModuleHandleA("KernelBase.dll"), "CtrlRoutine") ?:
                                          GetProcAddress(GetModuleHandleA("Kernel32.dll"), "CtrlRoutine"));
#else
        uint8_t *patch_address = NULL;
#endif
        if (!patch_address) {
            EnableMenuItem(GetSystemMenu(GetConsoleWindow(), false), SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
        }
        else {
            DWORD old_protection;
            VirtualProtect(patch_address, 0x20, PAGE_READWRITE, &old_protection);
            if (sizeof(&windows_console_handler) == 8) {
                *(patch_address++) = 0x48; // movabs
            }
            *(patch_address++) = 0xb8; // mov
            (*(uintptr_t *)patch_address) = (uintptr_t)&windows_console_handler;
            patch_address += sizeof(&windows_console_handler);
            // jmp rax/eax
            *(patch_address++) = 0xff;
            *(patch_address++) = 0xe0;
            VirtualProtect(patch_address, 0x20, old_protection, &old_protection);
        }
    }
}

#endif
    
static bool doing_hot_swap = false;
static bool handle_pending_command(void)
{
    switch (pending_command) {
        case GB_SDL_LOAD_STATE_COMMAND:
        case GB_SDL_SAVE_STATE_COMMAND: {
            char save_path[strlen(filename) + 5];
            char save_extension[] = ".s0";
            save_extension[2] += command_parameter;
            replace_extension(filename, strlen(filename), save_path, save_extension);
            
            void *previous = start_capturing_logs();
            bool success;
            if (pending_command == GB_SDL_LOAD_STATE_COMMAND) {
                int result = GB_load_state(&gb, save_path);
                if (result == ENOENT) {
                    char save_extension[] = ".sn0";
                    save_extension[3] += command_parameter;
                    replace_extension(filename, strlen(filename), save_path, save_extension);
                    start_capturing_logs();
                    result = GB_load_state(&gb, save_path);
                }
                success = result == 0;
            }
            else {
                success = GB_save_state(&gb, save_path) == 0;
            }
            end_capturing_logs(true,
                               false,
                               success? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_ERROR,
                               success? "Notice" : "Error",
                               previous);
            if (success) {
                show_osd_text(pending_command == GB_SDL_LOAD_STATE_COMMAND? "State loaded" : "State saved");
            }
            return false;
        }
    
        case GB_SDL_LOAD_STATE_FROM_FILE_COMMAND: {
            void *previous = start_capturing_logs();
            bool success = GB_load_state(&gb, dropped_state_file) == 0;
            end_capturing_logs(true,
                               false,
                               success? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_ERROR,
                               success? "Notice" : "Error",
                               previous);
            SDL_free(dropped_state_file);
            if (success) {
                show_osd_text("State loaded");
            }
            return false;
        }
            
        case GB_SDL_NO_COMMAND:
            return false;
            
        case GB_SDL_CART_SWAP_COMMAND:
            doing_hot_swap = true;
        case GB_SDL_RESET_COMMAND:
        case GB_SDL_NEW_FILE_COMMAND:
            GB_save_battery(&gb, battery_save_path_ptr);
            return true;
            
        case GB_SDL_QUIT_COMMAND:
            GB_save_battery(&gb, battery_save_path_ptr);
            exit(0);
        case GB_SDL_DEBUGGER_INTERRUPT_COMMAND:
            if (!GB_is_inited(&gb)) exit(0);
            
#ifdef _WIN32
            initialize_windows_console();
#endif

            /* ^C twice to exit */
            if (GB_debugger_is_stopped(&gb)) {
#ifndef _WIN32
                GB_save_battery(&gb, battery_save_path_ptr);
                exit(0);
#else
                break;
#endif
            }
            if (console_supported) {
                CON_print("^C\n");
            }
            GB_debugger_break(&gb);
            break;
#if _WIN32
        case GB_SDL_HIDE_DEBUGGER_COMMAND:
            HWND console_window = GetConsoleWindow();
            ShowWindow(console_window, SW_HIDE);
            FreeConsole();
            SDL_RaiseWindow(window);
            break;
#endif

    }
    return false;
}

static void load_boot_rom(GB_gameboy_t *gb, GB_boot_rom_t type)
{
    static const char *const names[] = {
        [GB_BOOT_ROM_DMG_0] = "dmg0_boot.bin",
        [GB_BOOT_ROM_DMG] = "dmg_boot.bin",
        [GB_BOOT_ROM_MGB] = "mgb_boot.bin",
        [GB_BOOT_ROM_SGB] = "sgb_boot.bin",
        [GB_BOOT_ROM_SGB2] = "sgb2_boot.bin",
        [GB_BOOT_ROM_CGB_0] = "cgb0_boot.bin",
        [GB_BOOT_ROM_CGB] = "cgb_boot.bin",
        [GB_BOOT_ROM_CGB_E] = "cgbE_boot.bin",
        [GB_BOOT_ROM_AGB_0] = "agb0_boot.bin",
        [GB_BOOT_ROM_AGB] = "agb_boot.bin",
    };
    bool use_built_in = true;
    if (configuration.bootrom_path[0]) {
        static char path[PATH_MAX + 1];
        snprintf(path, sizeof(path), "%s/%s", configuration.bootrom_path, names[type]);
        use_built_in = GB_load_boot_rom(gb, path);
    }
    if (use_built_in) {
        void *previous = start_capturing_logs();
        if (GB_load_boot_rom(gb, resource_path(names[type]))) {
            if (type == GB_BOOT_ROM_CGB_E) {
                end_capturing_logs(false, false, 0, NULL, previous);
                load_boot_rom(gb, GB_BOOT_ROM_CGB);
                return;
            }
            if (type == GB_BOOT_ROM_AGB_0) {
                end_capturing_logs(false, false, 0, NULL, previous);
                load_boot_rom(gb, GB_BOOT_ROM_AGB);
                return;
            }
        }
        end_capturing_logs(true, false, SDL_MESSAGEBOX_ERROR, "Error", previous);
    }
}

static bool is_path_writeable(const char *path)
{
    if (!access(path, W_OK)) return true;
    int fd = creat(path, 0644);
    if (fd == -1) return false;
    close(fd);
    unlink(path);
    return true;
}

static void debugger_reload_callback(GB_gameboy_t *gb)
{
    size_t path_length = strlen(filename);
    char extension[4] = {0,};
    if (path_length > 4) {
        if (filename[path_length - 4] == '.') {
            extension[0] = tolower((unsigned char)filename[path_length - 3]);
            extension[1] = tolower((unsigned char)filename[path_length - 2]);
            extension[2] = tolower((unsigned char)filename[path_length - 1]);
        }
    }
    if (strcmp(extension, "isx") == 0) {
        GB_load_isx(gb, filename);
    }
    else {
        GB_load_rom(gb, filename);
    }
    
    GB_load_battery(gb, battery_save_path_ptr);
    
    GB_debugger_clear_symbols(gb);
    GB_debugger_load_symbol_file(gb, resource_path("registers.sym"));
    
    char symbols_path[path_length + 5];
    replace_extension(filename, path_length, symbols_path, ".sym");
    GB_debugger_load_symbol_file(gb, symbols_path);
    
    GB_reset(gb);
}

static GB_model_t model_to_use(void)
{
    typeof(configuration.model) gui_model = configuration.model;
    if (gui_model == MODEL_AUTO) {
        uint8_t *rom = GB_get_direct_access(&gb, GB_DIRECT_ACCESS_ROM, NULL, NULL);
        if (!rom) {
            gui_model = MODEL_CGB;
        }
        else if (rom[0x143] & 0x80) { // Has CGB features
            gui_model = MODEL_CGB;
        }
        else if (rom[0x146] == 3) { // Has SGB features
            gui_model = MODEL_SGB;
        }
        else if (rom[0x14B] == 1) { // Nintendo-licensed (most likely has boot ROM palettes)
            gui_model = MODEL_CGB;
        }
        else if (rom[0x14B] == 0x33 &&
                 rom[0x144] == '0' &&
                 rom[0x145] == '1') { // Ditto
            gui_model = MODEL_CGB;
        }
        else {
            gui_model = MODEL_DMG;
        }
    }
    
    return (GB_model_t [])
    {
        [MODEL_DMG] = GB_MODEL_DMG_B,
        [MODEL_CGB] = GB_MODEL_CGB_0 + configuration.cgb_revision,
        [MODEL_AGB] = configuration.agb_revision,
        [MODEL_MGB] = GB_MODEL_MGB,
        [MODEL_SGB] = (GB_model_t [])
        {
            [SGB_NTSC] = GB_MODEL_SGB_NTSC,
            [SGB_PAL] = GB_MODEL_SGB_PAL,
            [SGB_2] = GB_MODEL_SGB2,
        }[configuration.sgb_revision],
    }[gui_model];
}

static void run(void)
{
    SDL_ShowCursor(SDL_DISABLE);
    GB_model_t model;
    pending_command = GB_SDL_NO_COMMAND;
restart:;
    model = model_to_use();
    bool should_resize = !screen_manually_resized;
    signed current_window_width, current_window_height;
    SDL_GetWindowSize(window, &current_window_width, &current_window_height);

    
    if (GB_is_inited(&gb)) {
        should_resize =
            current_window_width == GB_get_screen_width(&gb) * configuration.default_scale &&
            current_window_height == GB_get_screen_height(&gb) * configuration.default_scale;
        
        if (doing_hot_swap) {
            doing_hot_swap = false;
        }
        else {
            GB_switch_model_and_reset(&gb, model);
        }
    }
    else {
        GB_init(&gb, model);
        
        GB_set_boot_rom_load_callback(&gb, load_boot_rom);
        GB_set_vblank_callback(&gb, (GB_vblank_callback_t) vblank);
        GB_set_pixels_output(&gb, active_pixel_buffer);
        GB_set_rgb_encode_callback(&gb, rgb_encode);
        GB_set_rumble_callback(&gb, rumble);
        GB_set_rumble_mode(&gb, configuration.rumble_mode);
        GB_set_sample_rate(&gb, GB_audio_get_frequency());
        GB_set_color_correction_mode(&gb, configuration.color_correction_mode);
        GB_set_light_temperature(&gb, (configuration.color_temperature - 10.0) / 10.0);
        GB_set_interference_volume(&gb, configuration.interference_volume / 100.0);
        update_palette();
        if ((unsigned)configuration.border_mode <= GB_BORDER_ALWAYS) {
            GB_set_border_mode(&gb, configuration.border_mode);
        }
        GB_set_highpass_filter_mode(&gb, configuration.highpass_mode);
        GB_set_rewind_length(&gb, configuration.rewind_length);
        GB_set_rtc_mode(&gb, configuration.rtc_mode);
        GB_set_update_input_hint_callback(&gb, handle_events);
        GB_apu_set_sample_callback(&gb, gb_audio_callback);
        
        if (console_supported) {
            configure_console();
        }
        
        GB_debugger_set_reload_callback(&gb, debugger_reload_callback);
    }
    if (stop_on_start) {
        stop_on_start = false;
        GB_debugger_break(&gb);
    }

    bool error = false;
    GB_debugger_clear_symbols(&gb);
    void *previous = start_capturing_logs();
    size_t path_length = strlen(filename);
    char extension[4] = {0,};
    if (path_length > 4) {
        if (filename[path_length - 4] == '.') {
            extension[0] = tolower((unsigned char)filename[path_length - 3]);
            extension[1] = tolower((unsigned char)filename[path_length - 2]);
            extension[2] = tolower((unsigned char)filename[path_length - 1]);
        }
    }
    if (strcmp(extension, "isx") == 0) {
        error = GB_load_isx(&gb, filename);
        /* Configure battery */
        char battery_save_path[path_length + 5]; /* At the worst case, size is strlen(path) + 4 bytes for .sav + NULL */
        replace_extension(filename, path_length, battery_save_path, ".ram");
        battery_save_path_ptr = battery_save_path;
        GB_load_battery(&gb, battery_save_path);
    }
    else {
        GB_load_rom(&gb, filename);
    }
    GB_model_t updated_model = model_to_use(); // Could change after loading ROM with auto setting
    if (model != updated_model) {
        model = updated_model;
        GB_switch_model_and_reset(&gb, model);
    }
    
    if (should_resize) {
        signed width = GB_get_screen_width(&gb) * configuration.default_scale;
        signed height = GB_get_screen_height(&gb) * configuration.default_scale;
        signed x, y;
        SDL_GetWindowPosition(window, &x, &y);
        SDL_SetWindowSize(window, width, height);
        SDL_SetWindowPosition(window, x - (width - current_window_width) / 2, y - (height - current_window_height) / 2);
    }
    
    /* Configure battery */
    char battery_save_path[path_length + 5]; /* At the worst case, size is strlen(path) + 4 bytes for .sav + NULL */
    replace_extension(filename, path_length, battery_save_path, ".sav");
    battery_save_path_ptr = battery_save_path;
    GB_load_battery(&gb, battery_save_path);
    if (GB_save_battery_size(&gb)) {
        if (!is_path_writeable(battery_save_path)) {
            GB_log(&gb, "The save path for this ROM is not writeable, progress will not be saved.\n");
        }
    }
    
    char cheat_path[path_length + 5];
    replace_extension(filename, path_length, cheat_path, ".cht");
    // Remove all cheats before loading, so they're cleared even if loading fails.
    GB_remove_all_cheats(&gb);
    GB_load_cheats(&gb, cheat_path, false);
    
    end_capturing_logs(true, error, SDL_MESSAGEBOX_WARNING, "Warning", previous);
    
    static char start_text[64];
    static char title[17];
    GB_get_rom_title(&gb, title);
    sprintf(start_text, "SameBoy v" GB_VERSION "\n%s\n%08X", title, GB_get_rom_crc32(&gb));
    show_osd_text(start_text);

    /* Configure symbols */
    GB_debugger_load_symbol_file(&gb, resource_path("registers.sym"));
    
    char symbols_path[path_length + 5];
    replace_extension(filename, path_length, symbols_path, ".sym");
    GB_debugger_load_symbol_file(&gb, symbols_path);
        
    screen_size_changed(false);

    /* Run emulation */
    while (true) {
        if (paused || rewind_paused) {
            SDL_WaitEvent(NULL);
            handle_events(&gb);
        }
        else {
            if (do_rewind) {
                GB_rewind_pop(&gb);
                if (turbo_down) {
                    GB_rewind_pop(&gb);
                }
                if (!GB_rewind_pop(&gb)) {
                    rewind_paused = true;
                }
                do_rewind = false;
            }
            GB_run(&gb);
        }
        
        /* These commands can't run in the handle_event function, because they're not safe in a vblank context. */
        if (handle_pending_command()) {
            pending_command = GB_SDL_NO_COMMAND;
            goto restart;
        }
        pending_command = GB_SDL_NO_COMMAND;
    }
}

static char prefs_path[PATH_MAX + 1] = {0, };

static void save_configuration(void)
{
    FILE *prefs_file = fopen(prefs_path, "wb");
    if (prefs_file) {
        fwrite(&configuration, 1, sizeof(configuration), prefs_file);
        fclose(prefs_file);
    }
}

static void stop_recording(void)
{
    GB_stop_audio_recording(&gb);
}

static bool get_arg_flag(const char *flag, int *argc, char **argv)
{
    for (unsigned i = 1; i < *argc; i++) {
        if (strcmp(argv[i], flag) == 0) {
            (*argc)--;
            argv[i] = argv[*argc];
            return true;
        }
    }
    return false;
}

static const char *get_arg_option(const char *option, int *argc, char **argv)
{
    for (unsigned i = 1; i < *argc - 1; i++) {
        if (strcmp(argv[i], option) == 0) {
            const char *ret = argv[i + 1];
            memmove(argv + i, argv + i + 2, (*argc - i - 2) * sizeof(argv[0]));
            (*argc) -= 2;
            return ret;
        }
    }
    return NULL;
}

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
static void enable_smooth_scrolling(void)
{
    CFPreferencesSetAppValue(CFSTR("AppleMomentumScrollSupported"), kCFBooleanTrue, kCFPreferencesCurrentApplication);
}
#endif

static void handle_model_option(const char *model_string)
{
    static const struct {
        const char *name;
        GB_model_t model;
        const char *description;
    } name_to_model[] = {
        {"auto", -1, "Pick automatically"},
        {"dmg-b", GB_MODEL_DMG_B, "Game Boy, DMG-CPU B"},
        {"dmg", GB_MODEL_DMG_B, "Alias of dmg-b"},
        {"sgb-ntsc", GB_MODEL_SGB_NTSC, "Super Game Boy (NTSC)"},
        {"sgb-pal", GB_MODEL_SGB_PAL, "Super Game Boy (PAL)"},
        {"sgb2", GB_MODEL_SGB2, "Super Game Boy 2"},
        {"sgb", GB_MODEL_SGB, "Alias of sgb-ntsc"},
        {"mgb", GB_MODEL_MGB, "Game Boy Pocket/Light"},
        {"cgb-0", GB_MODEL_CGB_0, "Game Boy Color, CPU CGB"},
        {"cgb-a", GB_MODEL_CGB_A, "Game Boy Color, CPU CGB A"},
        {"cgb-b", GB_MODEL_CGB_B, "Game Boy Color, CPU CGB B"},
        {"cgb-c", GB_MODEL_CGB_C, "Game Boy Color, CPU CGB C"},
        {"cgb-d", GB_MODEL_CGB_D, "Game Boy Color, CPU CGB D"},
        {"cgb-e", GB_MODEL_CGB_E, "Game Boy Color, CPU CGB E"},
        {"cgb", GB_MODEL_CGB_E, "Alias of cgb-e"},
        {"agb-a", GB_MODEL_AGB_A, "Game Boy Advance, CPU AGB A"},
        {"agb", GB_MODEL_AGB_A, "Alias of agb-a"},
        {"gbp-a", GB_MODEL_GBP_A, "Game Boy Player, CPU AGB A"},
        {"gbp", GB_MODEL_GBP_A, "Alias of gbp-a"},
    };
    
    GB_model_t model = -1;
    for (unsigned i = 0; i < sizeof(name_to_model) / sizeof(name_to_model[0]); i++) {
        if (strcmp(model_string, name_to_model[i].name) == 00) {
            model = name_to_model[i].model;
            break;
        }
    }
    if (model == -1) {
        fprintf(stderr, "'%s' is not a valid model. Valid options are:\n", model_string);
        for (unsigned i = 0; i < sizeof(name_to_model) / sizeof(name_to_model[0]); i++) {
            fprintf(stderr, "%s - %s\n", name_to_model[i].name, name_to_model[i].description);
        }
        exit(1);
    }
    
    switch (model) {
        case GB_MODEL_DMG_B:
            configuration.model = MODEL_DMG;
            break;
        case GB_MODEL_SGB_NTSC:
            configuration.model = MODEL_SGB;
            configuration.sgb_revision = SGB_NTSC;
            break;
        case GB_MODEL_SGB_PAL:
            configuration.model = MODEL_SGB;
            configuration.sgb_revision = SGB_PAL;
            break;
        case GB_MODEL_SGB2:
            configuration.model = MODEL_SGB;
            configuration.sgb_revision = SGB_2;
            break;
        case GB_MODEL_MGB:
            configuration.model = MODEL_DMG;
            break;
        case GB_MODEL_CGB_0:
        case GB_MODEL_CGB_A:
        case GB_MODEL_CGB_B:
        case GB_MODEL_CGB_C:
        case GB_MODEL_CGB_D:
        case GB_MODEL_CGB_E:
            configuration.model = MODEL_CGB;
            configuration.cgb_revision = model - GB_MODEL_CGB_0;
            break;
        case GB_MODEL_AGB_A:
        case GB_MODEL_GBP_A:
            configuration.model = MODEL_AGB;
            configuration.agb_revision = model;
            break;
            
        default:
            configuration.model = MODEL_AUTO;
            break;
    }
}

#ifdef _WIN32
/* raise is buggy and for some reason not always go through our signal handler, so
   let's just place the implementation with a direct call to debugger_interrupt. */
int raise(int signal)
{
    debugger_interrupt(signal);
    return 0;
}
#endif

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetProcessDPIAware();
#endif
#ifdef __APPLE__
    enable_smooth_scrolling();
#endif

    const char *model_string = get_arg_option("--model", &argc, argv);
    bool fullscreen = get_arg_flag("--fullscreen", &argc, argv) || get_arg_flag("-f", &argc, argv);
    bool nogl = get_arg_flag("--nogl", &argc, argv);
    stop_on_start = get_arg_flag("--stop-debugger", &argc, argv) || get_arg_flag("-s", &argc, argv);
    

    if (argc > 2 || (argc == 2 && argv[1][0] == '-')) {
        fprintf(stderr, "SameBoy v" GB_VERSION "\n");
        fprintf(stderr, "Usage: %s [--fullscreen|-f] [--nogl] [--stop-debugger|-s] [--model <model>] <rom>\n", argv[0]);
        exit(1);
    }
    
    if (argc == 2) {
        filename = argv[1];
    }

    signal(SIGINT, debugger_interrupt);
#ifndef _WIN32
    signal(SIGUSR1, debugger_reset);
#endif

    SDL_Init(SDL_INIT_EVERYTHING & ~SDL_INIT_AUDIO);
    // This is, essentially, best-effort.
    // This function will not be called if the process is terminated in any way, anyhow.
    atexit(SDL_Quit);

    if ((console_supported = CON_start(completer))) {
        CON_set_repeat_empty(true);
        CON_printf("SameBoy v" GB_VERSION "\n");
    }
    else {
        fprintf(stderr, "SameBoy v" GB_VERSION "\n");
    }
    
    strcpy(prefs_path, resource_path("prefs.bin"));
    if (access(prefs_path, R_OK | W_OK) != 0) {
        char *prefs_dir = SDL_GetPrefPath("", "SameBoy");
        snprintf(prefs_path, sizeof(prefs_path) - 1, "%sprefs.bin", prefs_dir);
        SDL_free(prefs_dir);
    }
    
    FILE *prefs_file = fopen(prefs_path, "rb");
    if (prefs_file) {
        fread(&configuration, 1, sizeof(configuration), prefs_file);
        fclose(prefs_file);
        
        /* Sanitize for stability */
        configuration.color_correction_mode %= GB_COLOR_CORRECTION_MODERN_ACCURATE + 1;
        configuration.scaling_mode %= GB_SDL_SCALING_MAX;
        configuration.default_scale %= GB_SDL_DEFAULT_SCALE_MAX + 1;
        configuration.blending_mode %= GB_FRAME_BLENDING_MODE_ACCURATE + 1;
        configuration.highpass_mode %= GB_HIGHPASS_MAX;
        configuration.sgb_revision %= SGB_MAX;
        configuration.dmg_palette %= 5;
        if (configuration.dmg_palette) {
            configuration.gui_palette_enabled = true;
        }
        configuration.border_mode %= GB_BORDER_ALWAYS + 1;
        configuration.rumble_mode %= GB_RUMBLE_ALL_GAMES + 1;
        configuration.color_temperature %= 21;
        configuration.bootrom_path[sizeof(configuration.bootrom_path) - 1] = 0;
        configuration.cgb_revision %= GB_MODEL_CGB_E - GB_MODEL_CGB_0 + 1;
        configuration.audio_driver[15] = 0;
        configuration.dmg_palette_name[24] = 0;
        // Fix broken defaults, should keys 12-31 should be unmapped by default
        if (configuration.joypad_configuration[31] == 0) {
            memset(configuration.joypad_configuration + 12 , -1, 32 - 12);
        }
        if ((configuration.agb_revision & ~GB_MODEL_GBP_BIT) != GB_MODEL_AGB_A) {
            configuration.agb_revision = GB_MODEL_AGB_A;
        }
    }
    
    if (configuration.model >= MODEL_MAX) {
        configuration.model = MODEL_CGB;
    }

    if (configuration.default_scale == 0) {
        configuration.default_scale = 2;
    }
    
    if (model_string) {
        handle_model_option(model_string);
    }
    
    atexit(save_configuration);
    atexit(stop_recording);
    
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,
                configuration.allow_background_controllers? "1" : "0");

    window = SDL_CreateWindow("SameBoy v" GB_VERSION, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              (configuration.border_mode == GB_BORDER_ALWAYS? 256 : 160) * configuration.default_scale,
                              (configuration.border_mode == GB_BORDER_ALWAYS? 224 : 144) * configuration.default_scale,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == NULL) {
        fputs(SDL_GetError(), stderr);
        exit(1);
    }
    SDL_SetWindowMinimumSize(window, 160, 144);
    
    if (fullscreen) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }

#ifdef _WIN32
    configure_window_corners();
#endif
    
    gl_context = nogl? NULL : SDL_GL_CreateContext(window);
    
    GLint major = 0, minor = 0;
    if (gl_context) {
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        update_swap_interval();
    }
    
    if (gl_context && major * 0x100 + minor < 0x302) {
        SDL_GL_DeleteContext(gl_context);
        gl_context = NULL;
    }
    
    if (gl_context == NULL) {
        renderer = SDL_CreateRenderer(window, -1, 0);
        texture = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_STREAMING, 160, 144);
        pixel_format = SDL_AllocFormat(SDL_GetWindowPixelFormat(window));
    }
    else {
        pixel_format = SDL_AllocFormat(SDL_PIXELFORMAT_ABGR8888);
    }
    
    GB_audio_init();

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    
    if (!init_shader_with_name(&shader, configuration.filter)) {
        init_shader_with_name(&shader, "NearestNeighbor");
    }
    update_viewport();
    
#ifdef _WIN32
    if (!configuration.windows_associations_prompted) {
        configuration.windows_associations_prompted = true;
        save_configuration();
        SDL_MessageBoxButtonData buttons[2] = {
            {
                .flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
                .buttonid = 0,
                .text = "No",
            },
            {
                .flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,
                .buttonid = 1,
                .text = "Yes",
            },
        };
        SDL_MessageBoxData box = {
            .title = "Associate SameBoy with Game Boy ROMs",
            .message = "Would you like to associate SameBoy with Game Boy ROMs?\nThis can be also done later in the Options menu.",
            .numbuttons = 2,
            .buttons = buttons,
        };
        int button;
        SDL_ShowMessageBox(&box, &button);
        if (button) {
            GB_do_windows_association();
        }
    }
#endif
    
    if (filename == NULL) {
        stop_on_start = false;
        run_gui(false);
    }
    else {
        connect_joypad();
    }
    GB_audio_set_paused(false);
    run(); // Never returns
    return 0;
}

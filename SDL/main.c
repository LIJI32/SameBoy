#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <SDL2/SDL.h>
#include <Core/gb.h>
#include <Misc/wide_gb.h>
#include <inttypes.h>
#include "utils.h"
#include "gui.h"
#include "shader.h"


#ifndef _WIN32
#define AUDIO_FREQUENCY 96000
#else
/* Windows (well, at least my VM) can't handle 96KHz sound well :( */

/* felsqualle says: For SDL 2.0.6+ using the WASAPI driver, the highest freq.
   we can get is 48000. 96000 also works, but always has some faint crackling in
   the audio, no matter how high or low I set the buffer length...
   Not quite satisfied with that solution, because acc. to SDL2 docs,
   96k + WASAPI *should* work. */

#define AUDIO_FREQUENCY 48000
#endif

GB_gameboy_t gb;
static bool paused = false;
static uint32_t pixel_buffer_1[160*144], pixel_buffer_2[160*144], bg_pixel_buffer[160*144];
static uint32_t *active_pixel_buffer = pixel_buffer_1, *previous_pixel_buffer = pixel_buffer_2;
static bool underclock_down = false, rewind_down = false, do_rewind = false, rewind_paused = false, turbo_down = false;
static double clock_mutliplier = 1.0;

static char *filename = NULL;
static bool should_free_filename = false;
static char *battery_save_path_ptr;

SDL_AudioDeviceID device_id;

static const GB_model_t sdl_to_internal_model[] =
{
    [MODEL_DMG] = GB_MODEL_DMG_B,
    [MODEL_CGB] = GB_MODEL_CGB_E,
    [MODEL_AGB] = GB_MODEL_AGB
};

void set_filename(const char *new_filename, bool new_should_free)
{
    if (filename && should_free_filename) {
        SDL_free(filename);
    }
    filename = (char *) new_filename;
    should_free_filename = new_should_free;
}

static SDL_AudioSpec want_aspec, have_aspec;

static char *captured_log = NULL;

static void log_capture_callback(GB_gameboy_t *gb, const char *string, GB_log_attributes attributes)
{
    size_t current_len = strlen(captured_log);
    size_t len_to_add = strlen(string);
    captured_log = realloc(captured_log, current_len + len_to_add + 1);
    memcpy(captured_log + current_len, string, len_to_add);
    captured_log[current_len + len_to_add] = 0;
}

static void start_capturing_logs(void)
{
    if (captured_log != NULL) {
        free(captured_log);
    }
    captured_log = malloc(1);
    captured_log[0] = 0;
    GB_set_log_callback(&gb, log_capture_callback);
}

static const char *end_capturing_logs(bool show_popup, bool should_exit)
{
    GB_set_log_callback(&gb, NULL);
    if (captured_log[0] == 0) {
        free(captured_log);
        captured_log = NULL;
    }
    else {
        if (show_popup) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", captured_log, window);
        }
        if (should_exit) {
            exit(1);
        }
    }
    return captured_log;
}

static void open_menu(void)
{
    bool audio_playing = SDL_GetAudioDeviceStatus(device_id) == SDL_AUDIO_PLAYING;
    if (audio_playing) {
        SDL_PauseAudioDevice(device_id, 1);
    }
    run_gui(true);
    if (audio_playing) {
        SDL_PauseAudioDevice(device_id, 0);
    }
    GB_set_color_correction_mode(&gb, configuration.color_correction_mode);
    GB_set_highpass_filter_mode(&gb, configuration.highpass_mode);
}

static void handle_events(GB_gameboy_t *gb)
{
#ifdef __APPLE__
#define MODIFIER KMOD_GUI
#else
#define MODIFIER KMOD_CTRL
#endif
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type) {
            case SDL_QUIT:
                pending_command = GB_SDL_QUIT_COMMAND;
                break;
                
            case SDL_DROPFILE: {
                set_filename(event.drop.file, true);
                pending_command = GB_SDL_NEW_FILE_COMMAND;
                break;
            }
                
            case SDL_WINDOWEVENT: {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    update_viewport();
                }
                break;
            }
                
            case SDL_JOYBUTTONUP:
            case SDL_JOYBUTTONDOWN: {
                joypad_button_t button = get_joypad_button(event.jbutton.button);
                if ((GB_key_t) button < GB_KEY_MAX) {
                    GB_set_key_state(gb, (GB_key_t) button, event.type == SDL_JOYBUTTONDOWN);
                }
                else if (button == JOYPAD_BUTTON_TURBO) {
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
            }
            break;
                
            case SDL_JOYAXISMOTION: {
                static bool axis_active[2] = {false, false};
                joypad_axis_t axis = get_joypad_axis(event.jaxis.axis);
                if (axis == JOYPAD_AXISES_X) {
                    if (event.jaxis.value > JOYSTICK_HIGH) {
                        axis_active[0] = true;
                        GB_set_key_state(gb, GB_KEY_RIGHT, true);
                        GB_set_key_state(gb, GB_KEY_LEFT, false);
                    }
                    else if (event.jaxis.value < -JOYSTICK_HIGH) {
                        axis_active[0] = true;
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
                    if (event.jaxis.value > JOYSTICK_HIGH) {
                        axis_active[1] = true;
                        GB_set_key_state(gb, GB_KEY_DOWN, true);
                        GB_set_key_state(gb, GB_KEY_UP, false);
                    }
                    else if (event.jaxis.value < -JOYSTICK_HIGH) {
                        axis_active[1] = true;
                        GB_set_key_state(gb, GB_KEY_DOWN, false);
                        GB_set_key_state(gb, GB_KEY_UP, true);
                    }
                    else if (axis_active[1] && event.jaxis.value < JOYSTICK_LOW && event.jaxis.value > -JOYSTICK_LOW) {
                        axis_active[1] = false;
                        GB_set_key_state(gb, GB_KEY_DOWN, false);
                        GB_set_key_state(gb, GB_KEY_UP, false);
                    }
                }
            }
            break;

            case SDL_JOYHATMOTION:
            {
                uint8_t value = event.jhat.value;
                int8_t updown =
                    value == SDL_HAT_LEFTUP || value == SDL_HAT_UP || value == SDL_HAT_RIGHTUP ? -1 : (value == SDL_HAT_LEFTDOWN || value == SDL_HAT_DOWN || value == SDL_HAT_RIGHTDOWN ? 1 : 0);
                int8_t leftright =
                    value == SDL_HAT_LEFTUP || value == SDL_HAT_LEFT || value == SDL_HAT_LEFTDOWN ? -1 : (value == SDL_HAT_RIGHTUP || value == SDL_HAT_RIGHT || value == SDL_HAT_RIGHTDOWN ? 1 : 0);
                
                GB_set_key_state(gb, GB_KEY_LEFT, leftright == -1);
                GB_set_key_state(gb, GB_KEY_RIGHT, leftright == 1);
                GB_set_key_state(gb, GB_KEY_UP, updown == -1);
                GB_set_key_state(gb, GB_KEY_DOWN, updown == 1);
                break;
           };
                
            case SDL_KEYDOWN:
                switch (event.key.keysym.scancode) {
                    case SDL_SCANCODE_ESCAPE: {
                        open_menu();
                        break;
                    }
                    case SDL_SCANCODE_C:
                        if (event.type == SDL_KEYDOWN && (event.key.keysym.mod & KMOD_CTRL)) {
                            GB_debugger_break(gb);
                            
                        }
                        break;
                        
                    case SDL_SCANCODE_R:
                        if (event.key.keysym.mod & MODIFIER) {
                            pending_command = GB_SDL_RESET_COMMAND;
                        }
                        break;
                    
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
                            bool audio_playing = SDL_GetAudioDeviceStatus(device_id) == SDL_AUDIO_PLAYING;
                            if (audio_playing) {
                                SDL_PauseAudioDevice(device_id, 1);
                            }
                            else if (!audio_playing) {
                                SDL_PauseAudioDevice(device_id, 0);
                            }
                        }
                    break;
                    
                    case SDL_SCANCODE_F:
                        if (event.key.keysym.mod & MODIFIER) {
                            if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == false) {
                                SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                            }
                            else { 
                                SDL_SetWindowFullscreen(window, 0);
                            }
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
                        }
                        break;
                }
            case SDL_KEYUP: // Fallthrough
                if (event.key.keysym.scancode == configuration.keys[8]) {
                    turbo_down = event.type == SDL_KEYDOWN;
                    GB_set_turbo_mode(gb, turbo_down, turbo_down && rewind_down);
                }
                else if (event.key.keysym.scancode == configuration.keys_2[0]) {
                    rewind_down = event.type == SDL_KEYDOWN;
                    if (event.type == SDL_KEYUP) {
                        rewind_paused = false;
                    }
                    GB_set_turbo_mode(gb, turbo_down, turbo_down && rewind_down);
                }
                else if (event.key.keysym.scancode == configuration.keys_2[1]) {
                    underclock_down = event.type == SDL_KEYDOWN;
                }
                else {
                    for (unsigned i = 0; i < GB_KEY_MAX; i++) {
                        if (event.key.keysym.scancode == configuration.keys[i]) {
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

static void rgb_decode(uint32_t pixel, uint8_t *r, uint8_t *g, uint8_t *b)
{
    SDL_GetRGB(pixel, pixel_format, r, g, b);
}

static uint32_t rgb_encode(uint8_t r, uint8_t g, uint8_t b)
{
    return SDL_MapRGB(pixel_format, r, g, b);
}

static uint32_t gb_rgb_encode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    return SDL_MapRGB(pixel_format, r, g, b);
}

static void vblank(GB_gameboy_t *gb)
{
    // Adjust clock speed
    if (underclock_down && clock_mutliplier > 0.5) {
        clock_mutliplier -= 0.1;
        GB_set_clock_multiplier(gb, clock_mutliplier);
    }
    else if (!underclock_down && clock_mutliplier < 1.0) {
        clock_mutliplier += 0.1;
        GB_set_clock_multiplier(gb, clock_mutliplier);
    }

    // Notify WideGB of hardware updates (scroll, window position, background pixels)
    int scrollX = ((uint8_t *)GB_get_direct_access(gb, GB_DIRECT_ACCESS_IO, NULL, NULL))[GB_IO_SCX];
    int scrollY = ((uint8_t *)GB_get_direct_access(gb, GB_DIRECT_ACCESS_IO, NULL, NULL))[GB_IO_SCY];
    int wX = ((uint8_t *)GB_get_direct_access(gb, GB_DIRECT_ACCESS_IO, NULL, NULL))[GB_IO_WX] - 7;
    int wY = ((uint8_t *)GB_get_direct_access(gb, GB_DIRECT_ACCESS_IO, NULL, NULL))[GB_IO_WY];
    bool is_window_enabled = ((uint8_t *)GB_get_direct_access(gb, GB_DIRECT_ACCESS_IO, NULL, NULL))[GB_IO_LCDC] & 0x20;
    WGB_update_hardware_values(&wgb, scrollX, scrollY, wX, wY, is_window_enabled);

    WGB_update_screen(&wgb, bg_pixel_buffer, rgb_decode);

    // Present frame
    if (configuration.blend_frames) {
        render_texture(active_pixel_buffer, previous_pixel_buffer);
        // swap active and previous frame
        uint32_t *temp = active_pixel_buffer;
        active_pixel_buffer = previous_pixel_buffer;
        previous_pixel_buffer = temp;
        GB_set_pixels_output(gb, active_pixel_buffer);
    }
    else {
        render_texture(active_pixel_buffer, NULL);
    }

    do_rewind = rewind_down;
    handle_events(gb);
}

static void debugger_interrupt(int ignore)
{
    if (!GB_is_inited(&gb)) return;
    /* ^C twice to exit */
    if (GB_debugger_is_stopped(&gb)) {
        GB_save_battery(&gb, battery_save_path_ptr);
        exit(0);
    }
    GB_debugger_break(&gb);
}


static void audio_callback(void *gb, Uint8 *stream, int len)
{
    if (GB_is_inited(gb)) {
        GB_apu_copy_buffer(gb, (GB_sample_t *) stream, len / sizeof(GB_sample_t));
    }
    else {
        memset(stream, 0, len);
    }
}
    
static bool handle_pending_command(void)
{
    switch (pending_command) {
        case GB_SDL_LOAD_STATE_COMMAND:
        case GB_SDL_SAVE_STATE_COMMAND: {
            char save_path[strlen(filename) + 4];
            char save_extension[] = ".s0";
            save_extension[2] += command_parameter;
            replace_extension(filename, strlen(filename), save_path, save_extension);
            
            start_capturing_logs();
            if (pending_command == GB_SDL_LOAD_STATE_COMMAND) {
                GB_load_state(&gb, save_path);
            }
            else {
                GB_save_state(&gb, save_path);
            }
            end_capturing_logs(true, false);
            return false;
        }
            
        case GB_SDL_RESET_COMMAND:
            GB_save_battery(&gb, battery_save_path_ptr);
            return true;
            
        case GB_SDL_NO_COMMAND:
            return false;
            
        case GB_SDL_NEW_FILE_COMMAND:
            return true;
            
        case GB_SDL_QUIT_COMMAND:
            GB_save_battery(&gb, battery_save_path_ptr);
            exit(0);
    }
    return false;
}

static void run(void)
{
    pending_command = GB_SDL_NO_COMMAND;
restart:
    if (GB_is_inited(&gb)) {
        GB_switch_model_and_reset(&gb, sdl_to_internal_model[configuration.model]);
    }
    else {
        GB_init(&gb, sdl_to_internal_model[configuration.model]);
        
        GB_set_vblank_callback(&gb, (GB_vblank_callback_t) vblank);
        GB_set_pixels_output(&gb, active_pixel_buffer);
        GB_set_bg_pixels_output(&gb, bg_pixel_buffer);
        GB_set_rgb_encode_callback(&gb, gb_rgb_encode);
        GB_set_sample_rate(&gb, have_aspec.freq);
        GB_set_color_correction_mode(&gb, configuration.color_correction_mode);
        GB_set_highpass_filter_mode(&gb, configuration.highpass_mode);
        GB_set_rewind_length(&gb, configuration.rewind_length);
    }
    
    bool error = false;
    start_capturing_logs();
    const char * const boot_roms[] = {"dmg_boot.bin", "cgb_boot.bin", "agb_boot.bin"};
    error = GB_load_boot_rom(&gb, resource_path(boot_roms[configuration.model]));
    end_capturing_logs(true, error);
    
    start_capturing_logs();
    error = GB_load_rom(&gb, filename);
    end_capturing_logs(true, error);
    
    size_t path_length = strlen(filename);
    
    /* Configure battery */
    char battery_save_path[path_length + 5]; /* At the worst case, size is strlen(path) + 4 bytes for .sav + NULL */
    replace_extension(filename, path_length, battery_save_path, ".sav");
    battery_save_path_ptr = battery_save_path;
    GB_load_battery(&gb, battery_save_path);
    
    /* Configure symbols */
    GB_debugger_load_symbol_file(&gb, resource_path("registers.sym"));
    
    char symbols_path[path_length + 5];
    replace_extension(filename, path_length, symbols_path, ".sym");
    GB_debugger_load_symbol_file(&gb, symbols_path);

    /* Configure WideGB */
    char wgb_save_path[path_length + 8];
    replace_extension(filename, path_length, wgb_save_path, ".widegb");
    wgb = WGB_init_from_path(wgb_save_path, rgb_encode);

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

static char prefs_path[1024] = {0, };

static void save_configuration(void)
{
    FILE *prefs_file = fopen(prefs_path, "wb");
    if (prefs_file) {
        fwrite(&configuration, 1, sizeof(configuration), prefs_file);
        fclose(prefs_file);
    }
}

static void save_wide_gb(void)
{
    int path_length = strlen(filename);
    char save_path[path_length + 8];
    replace_extension(filename, path_length, save_path, ".widegb");

    WGB_save_to_path(&wgb, save_path, rgb_decode);
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

int main(int argc, char **argv)
{
#define str(x) #x
#define xstr(x) str(x)
    fprintf(stderr, "SameBoy v" xstr(VERSION) "\n");
    
    bool fullscreen = get_arg_flag("--fullscreen", &argc, argv);

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [--fullscreen] [rom]\n", argv[0]);
        exit(1);
    }
    
    if (argc == 2) {
        filename = argv[1];
    }

    signal(SIGINT, debugger_interrupt);

    SDL_Init(SDL_INIT_EVERYTHING);
    
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    window = SDL_CreateWindow("SameBoy + WideGB v0.1", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              160 * 2, 144 * 2, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_SetWindowMinimumSize(window, 160, 144);
    
    if (fullscreen) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    if (major * 0x100 + minor < 0x302) {
        SDL_GL_DeleteContext(gl_context);
        gl_context = NULL;
    }
    
    if (gl_context == NULL) {
        renderer = SDL_CreateRenderer(window, -1, 0);
        pixel_format = SDL_AllocFormat(SDL_GetWindowPixelFormat(window));
    }
    else {
        pixel_format = SDL_AllocFormat(SDL_PIXELFORMAT_ABGR8888);
    }

    /* Configure Audio */
    memset(&want_aspec, 0, sizeof(want_aspec));
    want_aspec.freq = AUDIO_FREQUENCY;
    want_aspec.format = AUDIO_S16SYS;
    want_aspec.channels = 2;
    want_aspec.samples = 512;
    
    SDL_version _sdl_version;
    SDL_GetVersion(&_sdl_version);
    unsigned sdl_version = _sdl_version.major * 1000 + _sdl_version.minor * 100 + _sdl_version.patch;
    
#ifndef _WIN32
    /* SDL 2.0.5 on macOS and Linux introduced a bug where certain combinations of buffer lengths and frequencies
       fail to produce audio correctly. */
    if (sdl_version >= 2005) {
        want_aspec.samples = 2048;
    }
#else
    if (sdl_version >= 2006) {
        /* SDL 2.0.6 offers WASAPI support which allows for much lower audio buffer lengths which at least
         theoretically reduces lagging. */
        want_aspec.samples = 32;
    }
    else {
        /* Since WASAPI audio was introduced in SDL 2.0.6, we have to lower the audio frequency
         to 44100 because otherwise we would get garbled audio output.*/
        want_aspec.freq = 44100;
    }
#endif
    

    want_aspec.callback = audio_callback;
    want_aspec.userdata = &gb;
    device_id = SDL_OpenAudioDevice(0, 0, &want_aspec, &have_aspec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    
    /* Start Audio */

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    
    char *prefs_dir = SDL_GetPrefPath("", "SameBoy");
    snprintf(prefs_path, sizeof(prefs_path) - 1, "%sprefs.bin", prefs_dir);
    SDL_free(prefs_dir);
    
    FILE *prefs_file = fopen(prefs_path, "rb");
    if (prefs_file) {
        fread(&configuration, 1, sizeof(configuration), prefs_file);
        fclose(prefs_file);
    }
    if (configuration.model >= MODEL_MAX) {
        configuration.model = MODEL_CGB;
    }
    
    atexit(save_configuration);
    atexit(save_wide_gb);

    if (!init_shader_with_name(&shader, configuration.filter)) {
        init_shader_with_name(&shader, "NearestNeighbor");
    }
    update_viewport();
    
    if (filename == NULL) {
        run_gui(false);
    }
    else {
        connect_joypad();
    }
    SDL_PauseAudioDevice(device_id, 0);
    run(); // Never returns
    return 0;
}

// SameBoy JSON Mode — JSON-RPC frontend for headless emulation
// Reads JSON commands from stdin, returns JSON responses to stdout
// Optional SDL display for visual feedback

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <SDL.h>
#include <png.h>

#include "Core/gb.h"
#include "Core/memory.h"
#include "Core/display.h"
#include "configuration.h"
#include "audio/audio.h"
#include "utils.h"
#include "json_mode.h"

// Configuration struct required by audio subsystem
configuration_t configuration = { 0 };

// Global gameboy instance
GB_gameboy_t gb;

static volatile sig_atomic_t json_shutdown_requested = 0;
static bool json_running = false; // When true, main loop runs frames
static bool json_breakpoint_hit = false; // Set when debugger stops at a breakpoint
static char *current_rom = NULL;

static void json_signal_handler(int sig)
{
    (void) sig;
    json_shutdown_requested = 1;
    // Waking the loop via a self-pipe would be ideal, but the loop
    // returns from GB_run at least every frame, so the flag is enough.
}

static uint32_t screen_buffer[160 * 144];

// PNG memory write callback
typedef struct {
    uint8_t *buffer;
    size_t size;
    size_t pos;
} png_mem_write_t;

static void png_mem_write(png_structp png, png_bytep data, png_size_t length)
{
    png_mem_write_t *mem = png_get_io_ptr(png);
    if (mem->pos + length > mem->size) {
        mem->size *= 2;
        mem->buffer = realloc(mem->buffer, mem->size);
    }
    memcpy(mem->buffer + mem->pos, data, length);
    mem->pos += length;
}

// SDL display state
static SDL_Window *json_window = NULL;
static SDL_Renderer *json_renderer = NULL;
static SDL_Texture *json_texture = NULL;
static SDL_PixelFormat *json_pixel_format = NULL;
static bool json_sdl_enabled = false;

static bool json_audio_enabled = false;

// Audio callback — queues samples to SDL audio hardware
static void json_audio_callback(GB_gameboy_t *gb, GB_sample_t *sample)
{
    if (GB_audio_get_queue_length() > GB_audio_get_frequency() / 8) {
        return;
    }
    GB_audio_queue_sample(sample);
}

// Default key mapping (same as regular SameBoy frontend)
static const SDL_Scancode json_key_map[GB_KEY_MAX] = {
    [GB_KEY_RIGHT] = SDL_SCANCODE_RIGHT,
    [GB_KEY_LEFT] = SDL_SCANCODE_LEFT,
    [GB_KEY_UP] = SDL_SCANCODE_UP,
    [GB_KEY_DOWN] = SDL_SCANCODE_DOWN,
    [GB_KEY_A] = SDL_SCANCODE_X,
    [GB_KEY_B] = SDL_SCANCODE_Z,
    [GB_KEY_SELECT] = SDL_SCANCODE_BACKSPACE,
    [GB_KEY_START] = SDL_SCANCODE_RETURN,
};

static void json_sdl_init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return;
    }

    json_window = SDL_CreateWindow("SameBoy JSON", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                   160 * 3, 144 * 3, SDL_WINDOW_HIDDEN);
    if (!json_window) {
        fprintf(stderr, "SDL window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return;
    }

    json_renderer = SDL_CreateRenderer(json_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!json_renderer) {
        fprintf(stderr, "SDL renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(json_window);
        SDL_Quit();
        return;
    }

    json_texture = SDL_CreateTexture(json_renderer, SDL_GetWindowPixelFormat(json_window),
                                     SDL_TEXTUREACCESS_STREAMING, 160, 144);
    if (!json_texture) {
        fprintf(stderr, "SDL texture creation failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(json_renderer);
        SDL_DestroyWindow(json_window);
        SDL_Quit();
        return;
    }

    json_pixel_format = SDL_AllocFormat(SDL_GetWindowPixelFormat(json_window));
    json_sdl_enabled = true;
}

static void json_audio_init(void)
{
    if (GB_audio_init()) {
        GB_set_sample_rate(&gb, GB_audio_get_frequency());
        GB_apu_set_sample_callback(&gb, json_audio_callback);
        GB_audio_set_paused(false);
        json_audio_enabled = true;
        fprintf(stderr, "Audio initialized: %s (%u Hz)\n", GB_audio_driver_name(), GB_audio_get_frequency());
    }
    else {
        fprintf(stderr, "Audio initialization failed, continuing without audio\n");
    }
}

// Render current frame to SDL window and process events
static void json_sdl_render(void)
{
    if (!json_sdl_enabled || !json_texture) return;

    SDL_UpdateTexture(json_texture, NULL, screen_buffer, 160 * sizeof(uint32_t));
    SDL_RenderClear(json_renderer);
    SDL_RenderCopy(json_renderer, json_texture, NULL, NULL);
    SDL_RenderPresent(json_renderer);

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            bool pressed = (event.type == SDL_KEYDOWN);
            for (unsigned i = 0; i < GB_KEY_MAX; i++) {
                if (event.key.keysym.scancode == json_key_map[i]) {
                    GB_set_key_state(&gb, i, pressed);
                    break;
                }
            }
        }
        if (event.type == SDL_QUIT) {
            json_shutdown_requested = 1;
        }
    }
}

static void json_sdl_cleanup(void)
{
    if (json_texture) SDL_DestroyTexture(json_texture);
    if (json_renderer) SDL_DestroyRenderer(json_renderer);
    if (json_window) SDL_DestroyWindow(json_window);
    if (json_pixel_format) SDL_FreeFormat(json_pixel_format);
    if (json_sdl_enabled) SDL_Quit();
    json_sdl_enabled = false;
    json_pixel_format = NULL;
}

// RGB encoder — uses SDL_MapRGB for correct pixel format
static uint32_t json_rgb_encode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    (void) gb;
    if (json_pixel_format) return SDL_MapRGB(json_pixel_format, r, g, b);
    return ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b | 0xFF000000;
}

// Minimal JSON parser — good enough for our protocol

static const char *json_skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static const char *json_parse_string(const char *p, char *out, size_t out_size)
{
    if (*p != '"') return NULL;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        if (*p == '\\') {
            p++;
            switch (*p) {
                case '"': out[i++] = '"'; break;
                case '\\': out[i++] = '\\'; break;
                case '/': out[i++] = '/'; break;
                case 'n': out[i++] = '\n'; break;
                case 't': out[i++] = '\t'; break;
                case 'r': out[i++] = '\r'; break;
                default: out[i++] = *p; break;
            }
        }
        else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    if (*p == '"') p++;
    return p;
}

// Skip a scalar, string, array or object; returns the first character after it
static const char *json_skip_value(const char *p)
{
    p = json_skip_ws(p);
    if (*p == '"') {
        p++;
        while (*p && *p != '"') {
            if (*p == '\\') p++;
            p++;
        }
        return *p ? p + 1 : p;
    }
    if (*p == '{' || *p == '[') {
        char closing = *p == '{' ? '}' : ']';
        unsigned depth = 0;
        for (; *p; p++) {
            if (*p == '"') {
                p++;
                while (*p && *p != '"') {
                    if (*p == '\\') p++;
                    p++;
                }
                if (!*p) return p;
            }
            else if (*p == '{' || *p == '[') depth++;
            else if (*p == '}' || *p == ']') {
                if (*p == closing && --depth == 0) return p + 1;
            }
        }
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    return p;
}

// Find the value of a key in the top-level object, respecting nesting
static const char *json_object_find(const char *json, const char *key)
{
    const char *p = json_skip_ws(json);
    if (*p != '{') return NULL;
    p = json_skip_ws(p + 1);
    while (*p && *p != '}') {
        char name[128];
        const char *value = json_parse_string(p, name, sizeof(name));
        if (!value) return NULL;
        value = json_skip_ws(value);
        if (*value != ':') return NULL;
        value = json_skip_ws(value + 1);
        if (strcmp(name, key) == 0) return value;
        p = json_skip_ws(json_skip_value(value));
        if (*p == ',') p = json_skip_ws(p + 1);
    }
    return NULL;
}

// Strings extracted from a command only need to live until the command is
// done; a pool reset per command avoids per-string allocation. The pool is
// sized for the maximum command line, so it cannot overflow in practice.
#define JSON_MAX_LINE 65536
static char json_pool[JSON_MAX_LINE * 2];
static size_t json_pool_pos = 0;

static char *json_pool_strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    if (json_pool_pos + len > sizeof(json_pool)) {
        return NULL;
    }
    char *result = &json_pool[json_pool_pos];
    memcpy(result, s, len);
    json_pool_pos += len;
    return result;
}

static const char *json_get_string(const char *json, const char *key, const char *default_val)
{
    const char *value = json_object_find(json, key);
    if (!value || *value != '"') return default_val;
    char parsed[1024];
    if (!json_parse_string(value, parsed, sizeof(parsed))) return default_val;
    char *copy = json_pool_strdup(parsed);
    return copy ? copy : default_val;
}

static double json_get_number(const char *json, const char *key, double default_val)
{
    const char *value = json_object_find(json, key);
    if (!value || *value == '"') return default_val;
    return strtod(value, NULL);
}

static bool json_get_bool(const char *json, const char *key, bool default_val)
{
    const char *value = json_object_find(json, key);
    if (!value) return default_val;
    if (strncmp(value, "true", 4) == 0) return true;
    if (strncmp(value, "false", 5) == 0) return false;
    return default_val;
}

static bool json_has_key(const char *json, const char *key)
{
    const char *value = json_object_find(json, key);
    return value && strncmp(value, "null", 4) != 0;
}

static const char *json_get_params(const char *json)
{
    const char *value = json_object_find(json, "params");
    return (value && *value == '{') ? value : "{}";
}

// Output building — a writer over a shared buffer keeps array construction
// O(n) and off the stack; the loop is single-threaded, so one buffer is enough.
typedef struct {
    char *buffer;
    size_t size;
    size_t pos;
} json_writer_t;

static void json_write(json_writer_t *writer, const char *fmt, ...)
{
    if (writer->pos + 1 >= writer->size) return;
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(writer->buffer + writer->pos, writer->size - writer->pos, fmt, args);
    va_end(args);
    if (written > 0) writer->pos += (size_t) written;
    if (writer->pos >= writer->size) writer->pos = writer->size - 1;
}

static char out_buf[JSON_MAX_LINE];
static char esc_buf[JSON_MAX_LINE];

static void json_send_response(unsigned id, const char *result_json)
{
    fprintf(stdout, "{\"id\":%u,\"result\":%s}\n", id, result_json);
    fflush(stdout);
}

static void json_send_error(unsigned id, const char *error_msg)
{
    fprintf(stdout, "{\"id\":%u,\"error\":\"%s\"}\n", id, error_msg);
    fflush(stdout);
}

static void json_send_notification(const char *method, const char *params_json)
{
    fprintf(stdout, "{\"method\":\"%s\",\"params\":%s}\n", method, params_json);
    fflush(stdout);
}

// Capture log output
static char *captured_log = NULL;

static void json_log_callback(GB_gameboy_t *gb, const char *string, GB_log_attributes_t attributes)
{
    (void) gb;
    (void) attributes;
    // Send debugger log to stderr (not stdout) to avoid mixing with JSON
    fputs(string, stderr);
    // Also capture for context commands
    size_t current_len = captured_log ? strlen(captured_log) : 0;
    size_t len_to_add = strlen(string);
    captured_log = realloc(captured_log, current_len + len_to_add + 1);
    memcpy(captured_log + current_len, string, len_to_add);
    captured_log[current_len + len_to_add] = '\0';
}

// Execute a debugger command and capture its output; caller must free the result
static char *json_exec_debugger_cmd(const char *cmd)
{
    free(captured_log);
    captured_log = malloc(1);
    captured_log[0] = '\0';

    char *cmd_copy = strdup(cmd);
    GB_debugger_execute_command(&gb, cmd_copy);

    char *result = captured_log;
    captured_log = NULL;
    return result;
}

static void json_escape_string(const char *src, char *dst, size_t dst_size)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_size - 2; i++) {
        switch (src[i]) {
            case '"': if (j + 2 < dst_size) { dst[j++] = '\\'; dst[j++] = '"'; } break;
            case '\\': if (j + 2 < dst_size) { dst[j++] = '\\'; dst[j++] = '\\'; } break;
            case '\n': if (j + 2 < dst_size) { dst[j++] = '\\'; dst[j++] = 'n'; } break;
            case '\r': if (j + 2 < dst_size) { dst[j++] = '\\'; dst[j++] = 'r'; } break;
            case '\t': if (j + 2 < dst_size) { dst[j++] = '\\'; dst[j++] = 't'; } break;
            default: dst[j++] = src[i]; break;
        }
    }
    dst[j] = '\0';
}

// Run a debugger command and return its captured output as {"key": "..."}
static void json_send_command_output(unsigned id, const char *key, const char *cmd)
{
    char *result = json_exec_debugger_cmd(cmd);
    json_escape_string(result ? result : "", esc_buf, sizeof(esc_buf));
    free(result);
    snprintf(out_buf, sizeof(out_buf), "{\"%s\":\"%s\"}", key, esc_buf);
    json_send_response(id, out_buf);
}

static void json_get_registers(GB_gameboy_t *gb, char *buf, size_t buf_size)
{
    GB_registers_t *regs = GB_get_registers(gb);
    snprintf(buf, buf_size,
        "{\"af\":%u,\"bc\":%u,\"de\":%u,\"hl\":%u,\"sp\":%u,\"pc\":%u,\"ime\":%u}",
        regs->af, regs->bc, regs->de, regs->hl, regs->sp, regs->pc, gb->ime);
}

static const char *const json_register_names[] = {
    "a", "f", "b", "c", "d", "e", "h", "l", "af", "bc", "de", "hl", "sp", "pc",
};

static const char *json_canonical_register_name(const char *name)
{
    for (unsigned i = 0; i < sizeof(json_register_names) / sizeof(json_register_names[0]); i++) {
        if (strcmp(name, json_register_names[i]) == 0) return json_register_names[i];
    }
    return NULL;
}

static uint16_t json_get_register(GB_gameboy_t *gb, const char *name)
{
    GB_registers_t *regs = GB_get_registers(gb);
    switch (name[0]) {
        case 'a': return name[1] == 'f' ? regs->af : regs->a;
        case 'f': return regs->f;
        case 'b': return name[1] == 'c' ? regs->bc : regs->b;
        case 'c': return regs->c;
        case 'd': return name[1] == 'e' ? regs->de : regs->d;
        case 'e': return regs->e;
        case 'h': return name[1] == 'l' ? regs->hl : regs->h;
        case 'l': return regs->l;
        case 's': return regs->sp;
        case 'p': return regs->pc;
    }
    return 0;
}

// VRAM tile as structured data
static void json_vram_tile_data(GB_gameboy_t *gb, uint8_t tile_id, char *buf, size_t buf_size)
{
    uint8_t *vram = GB_get_direct_access(gb, GB_DIRECT_ACCESS_VRAM, NULL, NULL);
    uint16_t offset = tile_id * 16;
    if (!vram || offset > 0x3FF0) {
        snprintf(buf, buf_size, "null");
        return;
    }

    json_writer_t writer = {buf, buf_size, 0};
    json_write(&writer, "{\"tile_id\":%u,\"pixels\":[", tile_id);
    for (unsigned y = 0; y < 8; y++) {
        json_write(&writer, "%s[", y > 0 ? "," : "");
        for (unsigned x = 0; x < 8; x++) {
            // Each row is two bitplanes, bit 7 - x of each
            uint8_t lo = vram[offset + y * 2];
            uint8_t hi = vram[offset + y * 2 + 1];
            uint8_t pixel = ((hi >> (7 - x)) & 1) | (((lo >> (7 - x)) & 1) << 1);
            json_write(&writer, "%s%u", x > 0 ? "," : "", pixel);
        }
        json_write(&writer, "]");
    }
    json_write(&writer, "]}");
}

static void json_oam_sprite(GB_gameboy_t *gb, uint8_t sprite_id, char *buf, size_t buf_size)
{
    uint8_t *oam = GB_get_direct_access(gb, GB_DIRECT_ACCESS_OAM, NULL, NULL);
    if (!oam || sprite_id >= 40) {
        snprintf(buf, buf_size, "null");
        return;
    }

    uint8_t *s = &oam[sprite_id * 4];
    snprintf(buf, buf_size,
        "{\"id\":%u,\"y\":%u,\"x\":%u,\"tile\":%u,\"attributes\":{\"flip_y\":%u,\"flip_x\":%u,\"priority\":%u,\"palette\":%u}}",
        sprite_id, s[0] - 16, s[1] - 8, s[2],
        (s[3] >> 6) & 1, (s[3] >> 5) & 1, (s[3] >> 4) & 1, s[3] & 0xF);
}

// Context snapshot: registers + disassembly
static void json_context_snapshot(GB_gameboy_t *gb, uint16_t addr, uint16_t range, char *buf, size_t buf_size)
{
    char regs_json[256];
    json_get_registers(gb, regs_json, sizeof(regs_json));

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "disassemble $%04x/%u", addr, range * 2 + 1);
    char *disasm = json_exec_debugger_cmd(cmd);

    json_escape_string(disasm ? disasm : "", esc_buf, sizeof(esc_buf));
    free(disasm);

    snprintf(buf, buf_size,
        "{\"registers\":%s,\"pc\":%u,\"disassembly\":\"%s\"}",
        regs_json, addr, esc_buf);
}

static void json_send_stop_notification(GB_gameboy_t *gb)
{
    char regs_json[256];
    json_get_registers(gb, regs_json, sizeof(regs_json));

    GB_registers_t *regs = GB_get_registers(gb);
    char params_json[512];
    snprintf(params_json, sizeof(params_json),
        "{\"pc\":%u,\"registers\":%s}", regs->pc, regs_json);

    json_send_notification("debugger.stopped", params_json);
}

// Input callback (blocking — called when the debugger stops)
static char *json_input_callback(GB_gameboy_t *gb)
{
    (void) gb;
    // Return NULL to let the debugger exit the run loop
    return NULL;
}

static char *json_async_input_callback(GB_gameboy_t *gb)
{
    (void) gb;
    return NULL; // No async input in JSON mode
}

// Command handlers. Returning true requests shutdown after responding.

static bool handle_quit(unsigned id, const char *params)
{
    (void) params;
    json_send_response(id, "\"ok\"");
    return true;
}

static bool handle_rom_load(unsigned id, const char *params)
{
    const char *path = json_get_string(params, "path", "");
    if (!path || !path[0]) {
        json_send_error(id, "missing path");
        return false;
    }
    if (GB_load_rom(&gb, path) != 0) {
        json_send_error(id, "failed to load ROM");
        return false;
    }
    free(current_rom);
    current_rom = strdup(path);
    json_send_response(id, "\"loaded\"");
    return false;
}

static bool handle_emulator_reset(unsigned id, const char *params)
{
    if (json_get_bool(params, "reload", false)) {
        GB_reset(&gb);
    }
    else {
        GB_quick_reset(&gb);
    }
    json_send_response(id, "\"ok\"");
    return false;
}

static bool handle_emulator_pause(unsigned id, const char *params)
{
    (void) params;
    json_running = false;
    GB_debugger_break(&gb);
    json_send_response(id, "\"paused\"");
    return false;
}

static bool handle_emulator_resume(unsigned id, const char *params)
{
    (void) params;
    gb.debug_stopped = false;
    json_breakpoint_hit = false;
    json_running = true;
    json_send_response(id, "\"running\"");
    return false;
}

static bool handle_cpu_step(unsigned id, const char *params)
{
    (void) params;
    json_send_command_output(id, "output", "step");
    return false;
}

static bool handle_cpu_next(unsigned id, const char *params)
{
    (void) params;
    json_send_command_output(id, "output", "next");
    return false;
}

static bool handle_cpu_finish(unsigned id, const char *params)
{
    (void) params;
    json_send_command_output(id, "output", "finish");
    return false;
}

static bool handle_cpu_backstep(unsigned id, const char *params)
{
    (void) params;
    json_send_command_output(id, "output", "backstep");
    return false;
}

static bool handle_cpu_undo(unsigned id, const char *params)
{
    (void) params;
    json_send_command_output(id, "output", "undo");
    return false;
}

static bool handle_registers_read(unsigned id, const char *params)
{
    const char *name = json_get_string(params, "name", NULL);
    if (!name) {
        json_get_registers(&gb, out_buf, sizeof(out_buf));
        json_send_response(id, out_buf);
        return false;
    }
    const char *canonical = json_canonical_register_name(name);
    if (!canonical) {
        json_send_error(id, "unknown register");
        return false;
    }
    uint16_t value = json_get_register(&gb, canonical);
    snprintf(out_buf, sizeof(out_buf), "{\"%s\":%u,\"hex\":\"$%04x\"}", canonical, value, value);
    json_send_response(id, out_buf);
    return false;
}

static bool handle_registers_write(unsigned id, const char *params)
{
    const char *name = json_get_string(params, "name", "");
    uint16_t value = (uint16_t) json_get_number(params, "value", 0);
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "%s = $%04x", name, value);
    free(json_exec_debugger_cmd(cmd));
    json_send_response(id, "\"ok\"");
    return false;
}

static bool handle_memory_read(unsigned id, const char *params)
{
    uint16_t address = (uint16_t) json_get_number(params, "address", 0);
    uint16_t size = (uint16_t) json_get_number(params, "size", 1);

    json_writer_t writer = {out_buf, sizeof(out_buf), 0};
    json_write(&writer, "{\"data\":[");
    for (uint16_t i = 0; i < size && i < 65535; i++) {
        json_write(&writer, "%s%u", i > 0 ? "," : "", GB_read_memory(&gb, address + i));
    }
    json_write(&writer, "]}");
    json_send_response(id, out_buf);
    return false;
}

static bool handle_memory_write(unsigned id, const char *params)
{
    uint16_t address = (uint16_t) json_get_number(params, "address", 0);
    const char *data = json_object_find(params, "data");
    if (data && *data == '[') {
        data = json_skip_ws(data + 1);
        while (*data && *data != ']') {
            if (*data == ',') {
                data = json_skip_ws(data + 1);
                continue;
            }
            char *end;
            double value = strtod(data, &end);
            if (end == data) break; // Not a number, stop instead of spinning
            GB_write_memory(&gb, address++, (uint8_t) value);
            data = json_skip_ws(end);
        }
    }
    json_send_response(id, "\"ok\"");
    return false;
}

static bool handle_memory_dump(unsigned id, const char *params)
{
    uint16_t address = (uint16_t) json_get_number(params, "address", 0);
    uint16_t size = (uint16_t) json_get_number(params, "size", 16);
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "examine $%04x/%u", address, size);
    json_send_command_output(id, "dump", cmd);
    return false;
}

static bool handle_breakpoint_add(unsigned id, const char *params)
{
    uint16_t address = (uint16_t) json_get_number(params, "address", 0);
    const char *condition = json_get_string(params, "condition", NULL);
    uint16_t range_end = (uint16_t) json_get_number(params, "range_end", 0);
    bool inclusive = json_get_bool(params, "inclusive", false);

    json_writer_t writer = {out_buf, sizeof(out_buf), 0};
    json_write(&writer, "breakpoint $%04x", address);
    if (range_end) {
        json_write(&writer, " to $%04x%s", range_end, inclusive ? " inclusive" : "");
    }
    if (condition) {
        json_write(&writer, " if %s", condition);
    }
    free(json_exec_debugger_cmd(out_buf));
    json_send_response(id, "\"ok\"");
    return false;
}

static bool handle_breakpoint_remove(unsigned id, const char *params)
{
    char cmd[64] = "delete";
    if (json_has_key(params, "id")) {
        snprintf(cmd + strlen(cmd), sizeof(cmd) - strlen(cmd), " %u",
                 (unsigned) json_get_number(params, "id", 0));
    }
    free(json_exec_debugger_cmd(cmd));
    json_send_response(id, "\"ok\"");
    return false;
}

static bool handle_breakpoint_list(unsigned id, const char *params)
{
    (void) params;
    json_send_command_output(id, "breakpoints", "list");
    return false;
}

static bool handle_watchpoint_add(unsigned id, const char *params)
{
    uint16_t address = (uint16_t) json_get_number(params, "address", 0);
    const char *type = json_get_string(params, "type", "w");
    const char *condition = json_get_string(params, "condition", NULL);

    json_writer_t writer = {out_buf, sizeof(out_buf), 0};
    json_write(&writer, "watch /%s $%04x", type, address);
    if (condition) {
        json_write(&writer, " if %s", condition);
    }
    free(json_exec_debugger_cmd(out_buf));
    json_send_response(id, "\"ok\"");
    return false;
}

static bool handle_watchpoint_remove(unsigned id, const char *params)
{
    char cmd[64] = "unwatch";
    if (json_has_key(params, "id")) {
        snprintf(cmd + strlen(cmd), sizeof(cmd) - strlen(cmd), " %u",
                 (unsigned) json_get_number(params, "id", 0));
    }
    free(json_exec_debugger_cmd(cmd));
    json_send_response(id, "\"ok\"");
    return false;
}

static bool handle_watchpoint_list(unsigned id, const char *params)
{
    (void) params;
    json_send_command_output(id, "watchpoints", "list");
    return false;
}

static bool handle_disassemble(unsigned id, const char *params)
{
    bool has_address = json_has_key(params, "address");
    uint16_t address = has_address ? (uint16_t) json_get_number(params, "address", 0) : GB_get_registers(&gb)->pc;
    uint16_t count = (uint16_t) json_get_number(params, "count", 5);

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "disassemble $%04x/%u", address, count);
    json_send_command_output(id, "disassembly", cmd);
    return false;
}

static bool handle_eval(unsigned id, const char *params)
{
    const char *expression = json_get_string(params, "expression", "");
    uint16_t result, bank;
    if (GB_debugger_evaluate(&gb, expression, &result, &bank)) {
        snprintf(out_buf, sizeof(out_buf), "{\"result\":%u,\"hex\":\"$%04x\",\"bank\":%u}", result, result, bank);
        json_send_response(id, out_buf);
    }
    else {
        json_send_error(id, "evaluation failed");
    }
    return false;
}

static bool handle_backtrace(unsigned id, const char *params)
{
    (void) params;
    json_send_command_output(id, "backtrace", "backtrace");
    return false;
}

static bool handle_state_save(unsigned id, const char *params)
{
    unsigned slot = (unsigned) json_get_number(params, "slot", 0);
    if (slot > 9) slot = 0;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "savestate %u", slot);
    free(json_exec_debugger_cmd(cmd));
    json_send_response(id, "\"ok\"");
    return false;
}

static bool handle_state_load(unsigned id, const char *params)
{
    unsigned slot = (unsigned) json_get_number(params, "slot", 0);
    if (slot > 9) slot = 0;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "loadstate %u", slot);
    free(json_exec_debugger_cmd(cmd));
    json_send_response(id, "\"ok\"");
    return false;
}

static bool handle_symbol_load(unsigned id, const char *params)
{
    const char *path = json_get_string(params, "path", "");
    GB_debugger_load_symbol_file(&gb, path);
    json_send_response(id, "\"ok\"");
    return false;
}

static bool handle_input_press(unsigned id, const char *params)
{
    unsigned key = (unsigned) json_get_number(params, "key", 0);
    bool state = json_get_bool(params, "state", true);
    if (key < GB_KEY_MAX) {
        GB_set_key_state(&gb, key, state);
    }
    json_send_response(id, "\"ok\"");
    return false;
}

static bool handle_apu_state(unsigned id, const char *params)
{
    (void) params;
    json_send_command_output(id, "apu", "apu");
    return false;
}

static bool handle_apu_wave(unsigned id, const char *params)
{
    (void) params;
    json_send_command_output(id, "wave", "wave");
    return false;
}

static bool handle_lcd_state(unsigned id, const char *params)
{
    (void) params;
    json_send_command_output(id, "lcd", "lcd");
    return false;
}

static bool handle_cartridge_info(unsigned id, const char *params)
{
    (void) params;
    json_send_command_output(id, "cartridge", "cartridge");
    return false;
}

static bool handle_vram_read(unsigned id, const char *params)
{
    uint16_t offset = (uint16_t) json_get_number(params, "offset", 0);
    uint16_t size = (uint16_t) json_get_number(params, "size", 256);

    uint8_t *vram = GB_get_direct_access(&gb, GB_DIRECT_ACCESS_VRAM, NULL, NULL);
    if (!vram) {
        json_send_error(id, "VRAM access failed");
        return false;
    }

    json_writer_t writer = {out_buf, sizeof(out_buf), 0};
    json_write(&writer, "{\"data\":[");
    for (uint16_t i = 0; i < size && offset + i < 0x2000; i++) {
        json_write(&writer, "%s%u", i > 0 ? "," : "", vram[offset + i]);
    }
    json_write(&writer, "]}");
    json_send_response(id, out_buf);
    return false;
}

static bool handle_vram_tile(unsigned id, const char *params)
{
    uint8_t tile_id = (uint8_t) json_get_number(params, "tile_id", 0);
    json_vram_tile_data(&gb, tile_id, out_buf, sizeof(out_buf));
    json_send_response(id, out_buf);
    return false;
}

static bool handle_vram_tiles(unsigned id, const char *params)
{
    (void) params;
    uint8_t *vram = GB_get_direct_access(&gb, GB_DIRECT_ACCESS_VRAM, NULL, NULL);
    if (!vram) {
        json_send_error(id, "VRAM access failed");
        return false;
    }

    bool cgb = GB_is_cgb_in_cgb_mode(&gb);
    json_writer_t writer = {out_buf, sizeof(out_buf), 0};
    json_write(&writer, "{\"count\":%u,\"bank\":%u,\"data\":[",
               cgb ? 512 : 256, cgb ? (vram[0x40] >> 7) : 0);
    for (size_t i = 0; i < 0x2000; i++) {
        json_write(&writer, "%s%u", i > 0 ? "," : "", vram[i]);
    }
    json_write(&writer, "]}");
    json_send_response(id, out_buf);
    return false;
}

static void json_write_all_sprites(json_writer_t *writer)
{
    char sprite_json[256];
    for (uint8_t i = 0; i < 40; i++) {
        json_oam_sprite(&gb, i, sprite_json, sizeof(sprite_json));
        json_write(writer, "%s%s", i > 0 ? "," : "", sprite_json);
    }
}

static bool handle_oam_read(unsigned id, const char *params)
{
    if (json_has_key(params, "sprite_id")) {
        uint8_t sprite_id = (uint8_t) json_get_number(params, "sprite_id", 0);
        json_oam_sprite(&gb, sprite_id, out_buf, sizeof(out_buf));
        json_send_response(id, out_buf);
        return false;
    }

    json_writer_t writer = {out_buf, sizeof(out_buf), 0};
    json_write(&writer, "{\"sprites\":[");
    json_write_all_sprites(&writer);
    json_write(&writer, "]}");
    json_send_response(id, out_buf);
    return false;
}

static bool handle_oam_list(unsigned id, const char *params)
{
    (void) params;
    json_writer_t writer = {out_buf, sizeof(out_buf), 0};
    json_write(&writer, "[");
    json_write_all_sprites(&writer);
    json_write(&writer, "]");
    json_send_response(id, out_buf);
    return false;
}

static bool handle_ppu_state(unsigned id, const char *params)
{
    (void) params;
    uint8_t lcdc = GB_read_memory(&gb, 0xFF40);
    uint8_t stat = GB_read_memory(&gb, 0xFF41);
    uint8_t scy = GB_read_memory(&gb, 0xFF42);
    uint8_t scx = GB_read_memory(&gb, 0xFF43);
    uint8_t ly = GB_read_memory(&gb, 0xFF44);
    uint8_t lyc = GB_read_memory(&gb, 0xFF45);

    snprintf(out_buf, sizeof(out_buf),
        "{\"lcdc\":%u,\"stat\":%u,\"scy\":%u,\"scx\":%u,\"ly\":%u,\"lyc\":%u,"
        "\"lcd_enabled\":%u,\"bg_enabled\":%u,\"sprites_enabled\":%u,\"window_enabled\":%u,\"mode\":%u}",
        lcdc, stat, scy, scx, ly, lyc,
        (lcdc >> 7) & 1, (lcdc >> 0) & 1, (lcdc >> 1) & 1, (lcdc >> 5) & 1, stat & 3);
    json_send_response(id, out_buf);
    return false;
}

static bool handle_ppu_palette(unsigned id, const char *params)
{
    (void) params;
    snprintf(out_buf, sizeof(out_buf), "{\"bgp\":%u,\"opb0\":%u,\"opb1\":%u}",
             GB_read_memory(&gb, 0xFF47), GB_read_memory(&gb, 0xFF48), GB_read_memory(&gb, 0xFF49));
    json_send_response(id, out_buf);
    return false;
}

static bool handle_context_snapshot(unsigned id, const char *params)
{
    uint16_t address = json_has_key(params, "address") ?
        (uint16_t) json_get_number(params, "address", 0) : GB_get_registers(&gb)->pc;
    uint16_t range = (uint16_t) json_get_number(params, "range", 8);

    json_context_snapshot(&gb, address, range, out_buf, sizeof(out_buf));
    json_send_response(id, out_buf);
    return false;
}

static bool handle_context_history(unsigned id, const char *params)
{
    (void) params;
    static char snapshot[JSON_MAX_LINE];
    json_context_snapshot(&gb, GB_get_registers(&gb)->pc, 8, snapshot, sizeof(snapshot));
    snprintf(out_buf, sizeof(out_buf), "{\"snapshots\":[%s]}", snapshot);
    json_send_response(id, out_buf);
    return false;
}

static bool handle_screenshot(unsigned id, const char *params)
{
    (void) params;
    uint32_t *pixels = GB_get_pixels_output(&gb);
    if (!pixels) {
        json_send_error(id, "no pixel buffer");
        return false;
    }
    uint16_t width = GB_get_screen_width(&gb);
    uint16_t height = GB_get_screen_height(&gb);

    // Convert pixels to RGB888 rows for PNG
    png_byte *rows[256];
    for (uint16_t y = 0; y < height; y++) {
        rows[y] = malloc(width * 3);
        for (uint16_t x = 0; x < width; x++) {
            uint32_t p = pixels[y * width + x];
            rows[y][x * 3] = (p >> 16) & 0xFF;
            rows[y][x * 3 + 1] = (p >> 8) & 0xFF;
            rows[y][x * 3 + 2] = p & 0xFF;
        }
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        for (uint16_t y = 0; y < height; y++) free(rows[y]);
        json_send_error(id, "png create failed");
        return false;
    }
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        for (uint16_t y = 0; y < height; y++) free(rows[y]);
        png_destroy_write_struct(&png_ptr, NULL);
        json_send_error(id, "png info failed");
        return false;
    }

    png_mem_write_t mem_write = {malloc(65536), 65536, 0};
    png_set_write_fn(png_ptr, &mem_write, png_mem_write, NULL);

    png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);
    png_write_image(png_ptr, rows);
    png_write_end(png_ptr, NULL);

    // Base64 encode the PNG data
    const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t b64_len = 4 * ((mem_write.pos + 2) / 3) + 1;
    char *b64 = malloc(b64_len);
    size_t out_pos = 0;
    for (size_t i = 0; i < mem_write.pos; i += 3) {
        uint32_t val = mem_write.buffer[i] << 16;
        if (i + 1 < mem_write.pos) val |= mem_write.buffer[i + 1] << 8;
        if (i + 2 < mem_write.pos) val |= mem_write.buffer[i + 2];
        b64[out_pos++] = b64_chars[(val >> 18) & 0x3F];
        b64[out_pos++] = b64_chars[(val >> 12) & 0x3F];
        b64[out_pos++] = i + 1 < mem_write.pos ? b64_chars[(val >> 6) & 0x3F] : '=';
        b64[out_pos++] = i + 2 < mem_write.pos ? b64_chars[val & 0x3F] : '=';
    }
    b64[out_pos] = '\0';

    size_t resp_len = strlen(b64) + 64;
    char *resp = malloc(resp_len);
    snprintf(resp, resp_len, "{\"png\":\"%s\",\"width\":%u,\"height\":%u}", b64, width, height);
    json_send_response(id, resp);

    for (uint16_t y = 0; y < height; y++) free(rows[y]);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    free(mem_write.buffer);
    free(b64);
    free(resp);
    return false;
}

typedef bool (*json_command_handler_t)(unsigned id, const char *params);

typedef struct {
    const char *name;
    json_command_handler_t handler;
} json_command_t;

static const json_command_t json_commands[] = {
    {"quit", handle_quit},
    {"rom.load", handle_rom_load},
    {"emulator.reset", handle_emulator_reset},
    {"emulator.pause", handle_emulator_pause},
    {"emulator.resume", handle_emulator_resume},
    {"cpu.step", handle_cpu_step},
    {"cpu.next", handle_cpu_next},
    {"cpu.finish", handle_cpu_finish},
    {"cpu.backstep", handle_cpu_backstep},
    {"cpu.undo", handle_cpu_undo},
    {"cpu.registers.read", handle_registers_read},
    {"cpu.registers.write", handle_registers_write},
    {"memory.read", handle_memory_read},
    {"memory.write", handle_memory_write},
    {"memory.dump", handle_memory_dump},
    {"breakpoint.add", handle_breakpoint_add},
    {"breakpoint.remove", handle_breakpoint_remove},
    {"breakpoint.list", handle_breakpoint_list},
    {"watchpoint.add", handle_watchpoint_add},
    {"watchpoint.remove", handle_watchpoint_remove},
    {"watchpoint.list", handle_watchpoint_list},
    {"disassemble", handle_disassemble},
    {"eval", handle_eval},
    {"backtrace", handle_backtrace},
    {"state.save", handle_state_save},
    {"state.load", handle_state_load},
    {"symbol.load", handle_symbol_load},
    {"input.press", handle_input_press},
    {"apu.state", handle_apu_state},
    {"apu.wave", handle_apu_wave},
    {"lcd.state", handle_lcd_state},
    {"cartridge.info", handle_cartridge_info},
    {"vram.read", handle_vram_read},
    {"vram.tile", handle_vram_tile},
    {"vram.tiles", handle_vram_tiles},
    {"oam.read", handle_oam_read},
    {"oam.list", handle_oam_list},
    {"ppu.state", handle_ppu_state},
    {"ppu.palette", handle_ppu_palette},
    {"context.snapshot", handle_context_snapshot},
    {"context.history", handle_context_history},
    {"screenshot", handle_screenshot},
};

// Returns true if the command loop should stop
static bool json_handle_command(const char *line)
{
    json_pool_pos = 0;

    unsigned id = (unsigned) json_get_number(line, "id", 0);

    const char *method_value = json_object_find(line, "method");
    if (!method_value) {
        json_send_error(id, "missing method");
        return false;
    }
    char method[128];
    if (!json_parse_string(method_value, method, sizeof(method))) {
        json_send_error(id, "invalid method");
        return false;
    }

    const char *params = json_get_params(line);
    for (const json_command_t *command = json_commands;
         command < json_commands + sizeof(json_commands) / sizeof(json_commands[0]); command++) {
        if (strcmp(method, command->name) == 0) {
            return command->handler(id, params);
        }
    }
    json_send_error(id, "unknown method");
    return false;
}

static void json_boot_rom_callback(GB_gameboy_t *gb, GB_boot_rom_t type)
{
    static const char *const names[] = {
        [GB_BOOT_ROM_DMG_0] = "dmg0_boot.bin",
        [GB_BOOT_ROM_DMG] = "dmg_boot.bin",
        [GB_BOOT_ROM_CGB] = "cgb_boot.bin",
        [GB_BOOT_ROM_AGB] = "agb_boot.bin",
    };
    // CGB_E falls back to CGB
    if (type == GB_BOOT_ROM_CGB_E) type = GB_BOOT_ROM_CGB;
    if ((size_t) type < sizeof(names) / sizeof(names[0])) {
        GB_load_boot_rom(gb, resource_path(names[type]));
    }
}

void json_mode_init(void)
{
    signal(SIGPIPE, SIG_IGN);
}

void json_mode_run(const char *rom_path)
{
    GB_init(&gb, GB_MODEL_CGB_E);
    GB_set_log_callback(&gb, json_log_callback);
    GB_set_input_callback(&gb, json_input_callback);
    GB_set_async_input_callback(&gb, json_async_input_callback);
    GB_set_boot_rom_load_callback(&gb, json_boot_rom_callback);
    // Disable debugger to prevent GB_run from blocking
    gb.debug_disable = true;

    // Initialize SDL display FIRST (needed for pixel format)
    json_sdl_init();

    // Register signal handlers AFTER SDL_Init (SDL may override them)
    signal(SIGTERM, json_signal_handler);
    signal(SIGINT, json_signal_handler);

    GB_set_rgb_encode_callback(&gb, json_rgb_encode);
    GB_set_pixels_output(&gb, screen_buffer);

    if (rom_path) {
        if (GB_load_rom(&gb, rom_path) != 0) {
            fprintf(stderr, "Failed to load ROM: %s\n", rom_path);
            return;
        }
        current_rom = strdup(rom_path);

        json_audio_init();

        // Run frames until the LCD is enabled, then start running
        for (unsigned i = 0; i < 10000; i++) {
            GB_run_frame(&gb);
            json_sdl_render();
            if (GB_read_memory(&gb, 0xFF40) & 0x80) break;
        }
        json_running = true;
    }

    json_send_notification("ready", rom_path ? "{\"rom\":\"loaded\"}" : "{}");
    if (json_window) SDL_ShowWindow(json_window);

    // Command loop — non-blocking stdin so the emulator and SDL stay responsive
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    static char line[JSON_MAX_LINE];
    size_t line_pos = 0;
    while (!json_shutdown_requested) {
        // Run one frame worth of instructions with real-time pacing
        if (json_running && !json_breakpoint_hit) {
            do {
                GB_run(&gb);
                if (json_breakpoint_hit) break;
            } while (!gb.vblank_just_occured);
            if (json_breakpoint_hit) {
                json_send_stop_notification(&gb);
                json_running = false;
                json_breakpoint_hit = false;
            }
        }

        json_sdl_render();

        char c;
        ssize_t n;
        bool input_eof = false;
        while ((n = read(STDIN_FILENO, &c, 1)) != 0) {
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
                input_eof = true;
                break;
            }
            if (c == '\n') {
                line[line_pos] = '\0';
                if (line_pos > 0 && json_handle_command(line)) {
                    json_shutdown_requested = 1;
                }
                line_pos = 0;
            }
            else if (c != '\r' && line_pos < sizeof(line) - 1) {
                line[line_pos++] = c;
            }
        }
        if (n == 0 || input_eof) break; // stdin closed

        // When idle, sleep to avoid busy-waiting
        if (!json_running) {
            usleep(50000); // 50ms
        }
    }
}

void json_mode_cleanup(void)
{
    if (json_audio_enabled) {
        GB_audio_deinit();
        json_audio_enabled = false;
    }
    json_sdl_cleanup();
    free(captured_log);
    captured_log = NULL;
    free(current_rom);
    current_rom = NULL;
    GB_free(&gb);
}

void json_mode_usage(void)
{
    fprintf(stderr, "Usage: sameboy-json [rom_path]\n");
    fprintf(stderr, "Reads JSON commands from stdin, writes JSON responses to stdout.\n");
}

int main(int argc, char *argv[])
{
    json_mode_init();

    const char *rom_path = NULL;
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            json_mode_usage();
            return 0;
        }
        rom_path = argv[1];
    }

    json_mode_run(rom_path);
    json_mode_cleanup();
    return 0;
}

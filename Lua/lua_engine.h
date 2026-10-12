#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <Core/gb.h>

/* A BizHawk-compatible Lua scripting environment, shared by the Cocoa and SDL
   frontends. All functions that touch the Lua state must be called either from
   the emulation thread, or while the emulator is not running. */

typedef struct GB_lua_engine_s GB_lua_engine_t;

typedef struct {
    /* Script output. Not necessarily valid UTF-8, and not NULL terminated. */
    void (*log)(void *user_data, const char *text, size_t length);
    /* A script error, including a traceback. If NULL, errors are sent to log. */
    void (*error)(void *user_data, const char *message);
    void (*clear)(void *user_data);
    /* gui.addmessage */
    void (*message)(void *user_data, const char *text);
    /* Called when a script starts or stops */
    void (*state_changed)(void *user_data);
    /* The engine installs this as the GB_gameboy_t's execution callback when a
       script registers an event.onmemoryexecute callback. Because only the
       frontend knows how to find the engine from a GB_gameboy_t, it must
       forward these calls to GB_lua_engine_executed_address. */
    GB_execution_callback_t execution_callback;
} GB_lua_engine_callbacks_t;

GB_lua_engine_t *GB_lua_engine_create(GB_gameboy_t *gb, const GB_lua_engine_callbacks_t *callbacks, void *user_data);
void GB_lua_engine_free(GB_lua_engine_t *engine);

/* Reported by gameinfo.getromname. If NULL or empty, the ROM header title is used. */
void GB_lua_engine_set_rom_name(GB_lua_engine_t *engine, const char *name);
/* NULL if no script was started yet */
const char *GB_lua_engine_get_script_path(GB_lua_engine_t *engine);
bool GB_lua_engine_is_running(GB_lua_engine_t *engine);

bool GB_lua_engine_start_script(GB_lua_engine_t *engine, const char *path);
void GB_lua_engine_stop_script(GB_lua_engine_t *engine);

/* Must be called by the frontend on every vblank */
void GB_lua_engine_frame_ended(GB_lua_engine_t *engine);
void GB_lua_engine_executed_address(GB_lua_engine_t *engine, uint16_t address, uint8_t opcode);

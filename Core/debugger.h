#pragma once
#ifndef GB_DISABLE_DEBUGGER
#include <stdbool.h>
#include <stdint.h>
#include "defs.h"
#include "symbol_hash.h"

void GB_debugger_break(GB_gameboy_t *gb);
#ifdef GB_INTERNAL
bool /* Returns true if debugger waits for more commands. Not relevant for non-GB_INTERNAL */
#else
void
#endif
GB_debugger_execute_command(GB_gameboy_t *gb, char *input); /* Destroys input. */
char *GB_debugger_complete_substring(GB_gameboy_t *gb, char *input, uintptr_t *context);  /* Destroys input, result requires free */
void GB_debugger_load_symbol_file(GB_gameboy_t *gb, const char *path);
const char *GB_debugger_name_for_address(GB_gameboy_t *gb, uint16_t addr);
/* Use -1 for bank to use the currently mapped bank */
const char *GB_debugger_describe_address(GB_gameboy_t *gb, uint16_t addr, uint16_t bank, bool exact_match, bool prefer_local);
bool GB_debugger_evaluate(GB_gameboy_t *gb, const char *string, uint16_t *result, uint16_t *result_bank); /* result_bank is -1 if unused. */
bool GB_debugger_is_stopped(GB_gameboy_t *gb);
void GB_debugger_set_disabled(GB_gameboy_t *gb, bool disabled);
void GB_debugger_clear_symbols(GB_gameboy_t *gb);

#ifdef GB_INTERNAL
internal void GB_debugger_run(GB_gameboy_t *gb);
internal void GB_debugger_handle_async_commands(GB_gameboy_t *gb);
internal void GB_debugger_call_hook(GB_gameboy_t *gb, uint16_t call_addr);
internal void GB_debugger_ret_hook(GB_gameboy_t *gb);
internal void GB_debugger_test_write_watchpoint(GB_gameboy_t *gb, uint16_t addr, uint8_t value);
internal void GB_debugger_test_read_watchpoint(GB_gameboy_t *gb, uint16_t addr);
internal const GB_bank_symbol_t *GB_debugger_find_symbol(GB_gameboy_t *gb, uint16_t addr, bool prefer_local);
internal void GB_debugger_add_symbol(GB_gameboy_t *gb, uint16_t bank, uint16_t address, const char *symbol);
#ifndef GB_DISABLE_CHEAT_SEARCH
internal bool GB_debugger_evaluate_cheat_filter(GB_gameboy_t *gb, const char *string, bool *result, uint16_t old, uint16_t new);
#endif
#endif

#else // GB_DISABLE_DEBUGGER
#ifdef GB_INTERNAL
#define GB_debugger_run(gb) (void)0
#define GB_debugger_handle_async_commands(gb) (void)0
#define GB_debugger_ret_hook(gb) (void)0
#define GB_debugger_call_hook(gb, addr) (void)addr
#define GB_debugger_test_write_watchpoint(gb, addr, value) ((void)addr, (void)value)
#define GB_debugger_test_read_watchpoint(gb, addr) (void)addr
#define GB_debugger_add_symbol(gb, bank, address, symbol) ((void)bank, (void)address, (void)symbol)
#define GB_debugger_break(gb) (void)0
#endif // GB_INTERNAL

#endif // GB_DISABLE_DEBUGGER

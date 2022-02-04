#ifndef memory_h
#define memory_h
#include "defs.h"
#include <stdint.h>

typedef uint8_t (*GB_read_memory_callback_t)(GB_gameboy_t *gb, uint16_t addr, uint8_t data);
typedef bool (*GB_write_memory_callback_t)(GB_gameboy_t *gb, uint16_t addr, uint8_t data); // Return false to prevent the write
void GB_set_read_memory_callback(GB_gameboy_t *gb, GB_read_memory_callback_t callback);
void GB_set_write_memory_callback(GB_gameboy_t *gb, GB_write_memory_callback_t callback);

uint8_t GB_read_memory(GB_gameboy_t *gb, uint16_t addr);
uint8_t GB_safe_read_memory(GB_gameboy_t *gb, uint16_t addr); // Without side effects
void GB_write_memory(GB_gameboy_t *gb, uint16_t addr, uint8_t value);
#ifdef GB_INTERNAL
internal void GB_dma_run(GB_gameboy_t *gb);
internal bool GB_is_dma_active(GB_gameboy_t *gb);
internal void GB_hdma_run(GB_gameboy_t *gb);
internal void GB_trigger_oam_bug(GB_gameboy_t *gb, uint16_t address);
#endif

#endif /* memory_h */

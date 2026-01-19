#pragma once

/* Macros to make the GB_gameboy_t struct more future compatible when state saving */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "model.h"
#include "defs.h"

#define GB_PADDING(type, old_usage) type old_usage##__do_not_use

#ifdef __cplusplus
/* For bsnes integration. C++ code does not need section information, and throws a fit over certain types such
   as anonymous enums inside unions */
#if __clang__
#define GB_SECTION(name, ...) __attribute__ ((aligned (8))) __VA_ARGS__
#else
// GCC's handling of attributes is so awfully bad, that it is alone a good enough reason to never use that compiler
#define GB_SECTION(name, ...) _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wpedantic\"") alignas(8) char _align_##name[0]; __VA_ARGS__ _Pragma("GCC diagnostic pop")
#endif
#else
#define GB_SECTION(name, ...) union __attribute__ ((aligned (8))) {uint8_t name##_section_start; struct {__VA_ARGS__};}; uint8_t name##_section_end[0];
#ifdef GB_INTERNAL
#define GB_SECTION_OFFSET(name) (offsetof(GB_gameboy_t, name##_section_start))
#define GB_SECTION_SIZE(name) (offsetof(GB_gameboy_t, name##_section_end) - offsetof(GB_gameboy_t, name##_section_start))
/* This roundabout way to get the section offset is because GCC 9 is a bad compiler and will false-positively complain
   about memset buffer overflows otherwise */
#define GB_GET_SECTION(gb, name) (void *)((uint8_t *)(gb) + GB_SECTION_OFFSET(name))
#endif
#endif

#if __clang_major__ >= 8 || __GNUC__ >= 13 || defined(__cplusplus)
#define GB_ENUM(type, ...) enum : type __VA_ARGS__
#else
#define GB_ENUM(type, ...) __typeof__((type)((enum __VA_ARGS__)0))
#endif

/* Public calls related to save states */
int GB_save_state(GB_gameboy_t *gb, const char *path);
size_t GB_get_save_state_size(GB_gameboy_t *gb);
/* Assumes buffer is big enough to contain the save state. Use with GB_get_save_state_size(). */
void GB_save_state_to_buffer(GB_gameboy_t *gb, uint8_t *buffer);

int GB_load_state(GB_gameboy_t *gb, const char *path);
int GB_load_state_from_buffer(GB_gameboy_t *gb, const uint8_t *buffer, size_t length);
bool GB_is_save_state(const char *path);
int GB_get_state_model(const char *path, GB_model_t *model);
int GB_get_state_model_from_buffer(const uint8_t *buffer, size_t length, GB_model_t *model);

#ifdef GB_INTERNAL
static inline uint32_t GB_state_magic(void)
{
    if (sizeof(bool) == 1) return 'SAME';
    return 'S4ME';
}

/* For internal in-memory save states (rewind, debugger) that do not need BESS */
internal size_t GB_get_save_state_size_no_bess(GB_gameboy_t *gb);
internal void GB_save_state_to_buffer_no_bess(GB_gameboy_t *gb, uint8_t *buffer);
#endif

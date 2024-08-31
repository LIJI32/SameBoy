#pragma once
#ifndef GB_DISABLE_CHEAT_SEARCH
#include "defs.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint16_t addr;
    uint16_t bank;
    uint16_t value;
} GB_cheat_search_result_t;

typedef enum {
    GB_CHEAT_SEARCH_DATA_TYPE_8BIT = 0,
    GB_CHEAT_SEARCH_DATA_TYPE_16BIT = 1,
    GB_CHEAT_SEARCH_DATA_TYPE_BE_BIT = 2, // Not used alone
    GB_CHEAT_SEARCH_DATA_TYPE_16BIT_BE = GB_CHEAT_SEARCH_DATA_TYPE_16BIT | GB_CHEAT_SEARCH_DATA_TYPE_BE_BIT,
} GB_cheat_search_data_type_t;

void GB_cheat_search_reset(GB_gameboy_t *gb);
bool GB_cheat_search_filter(GB_gameboy_t *gb, const char *expression, GB_cheat_search_data_type_t data_type);
size_t GB_cheat_search_result_count(GB_gameboy_t *gb);
void GB_cheat_search_get_results(GB_gameboy_t *gb, GB_cheat_search_result_t *results);
#endif

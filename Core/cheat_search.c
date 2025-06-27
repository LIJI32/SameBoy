#include "gb.h"

void GB_cheat_search_reset(GB_gameboy_t *gb)
{
    if (gb->cheat_search_data) {
        free(gb->cheat_search_data);
        gb->cheat_search_data = NULL;
    }
    if (gb->cheat_search_bitmap) {
        free(gb->cheat_search_bitmap);
        gb->cheat_search_bitmap = NULL;
    }
    gb->cheat_search_count = 0;
}

bool GB_cheat_search_filter(GB_gameboy_t *gb, const char *expression, GB_cheat_search_data_type_t data_type)
{
    GB_ASSERT_NOT_RUNNING(gb)
    
    // Make sure the expression is valid first
    if (GB_debugger_evaluate_cheat_filter(gb, expression, NULL, 0, 0)) {
        return false;
    }
    gb->cheat_search_data_type = data_type;

    if (gb->cheat_search_count == 0) {
        GB_cheat_search_reset(gb);
        gb->cheat_search_count = gb->ram_size + gb->mbc_ram_size + sizeof(gb->hram);
        gb->cheat_search_data = malloc(gb->cheat_search_count);
        gb->cheat_search_bitmap = malloc((gb->cheat_search_count + 7) / 8);
        memset(gb->cheat_search_data, 0, gb->cheat_search_count);
        memset(gb->cheat_search_bitmap, 0, (gb->cheat_search_count + 7) / 8);
    }
    
    uint8_t mask = 1;
    uint8_t *old_data = gb->cheat_search_data;
    uint8_t *bitmap = gb->cheat_search_bitmap;
    uint8_t *new_data = gb->ram;
    
    for (unsigned i = gb->ram_size + gb->mbc_ram_size + sizeof(gb->hram); i--;) {
        if (*bitmap & mask) {
            goto skip;
        }
        bool result = false;
        if (data_type & GB_CHEAT_SEARCH_DATA_TYPE_16BIT) {
            // The last byte of each section always fails on 16-bit searches
            if ((new_data != gb->ram + gb->ram_size - 1 &&
                 new_data != gb->mbc_ram + gb->mbc_ram_size - 1 &&
                 new_data != gb->hram + sizeof(gb->hram) - 1)) {
                uint16_t old = old_data[0] | (old_data[1] << 8);
                uint16_t new = new_data[0] | (new_data[1] << 8);
                if (data_type & GB_CHEAT_SEARCH_DATA_TYPE_BE_BIT) {
                    old = __builtin_bswap16(old);
                    new = __builtin_bswap16(new);
                }
                GB_debugger_evaluate_cheat_filter(gb, expression, &result, old, new);
            }
        }
        else {
            GB_debugger_evaluate_cheat_filter(gb, expression, &result, *old_data, *new_data);
        }
        if (result) {
            // Filter passed, update old value
            *old_data = *new_data;
            if (data_type & GB_CHEAT_SEARCH_DATA_TYPE_16BIT) {
                old_data[1] = new_data[1];
            }
        }
        else {
            // Did not pass filter, remove address
            *bitmap |= mask;
            gb->cheat_search_count--;
        }
    skip:;
        old_data++;
        if (new_data == gb->ram + gb->ram_size - 1 && gb->mbc_ram_size) {
            new_data = gb->mbc_ram;
        }
        else if (new_data == gb->mbc_ram + gb->mbc_ram_size - 1) {
            new_data = gb->hram;
        }
        else {
            new_data++;
        }
        mask <<= 1;
        if (mask == 0) {
            mask = 1;
            bitmap++;
        }
    }
    
    return true;
}

size_t GB_cheat_search_result_count(GB_gameboy_t *gb)
{
    return gb->cheat_search_count;
}

void GB_cheat_search_get_results(GB_gameboy_t *gb, GB_cheat_search_result_t *results)
{
    uint8_t mask = 1;
    uint8_t *old_data = gb->cheat_search_data;
    uint8_t *bitmap = gb->cheat_search_bitmap;
    size_t count = gb->cheat_search_count;
    while (count) {
        if (!(*bitmap & mask)) {
            count--;
            if (gb->cheat_search_data_type & GB_CHEAT_SEARCH_DATA_TYPE_16BIT) {
                // Do not check for end of section, data_type is required to be the same as the last filter call
                uint16_t old = old_data[0] | (old_data[1] << 8);
                if (gb->cheat_search_data_type & GB_CHEAT_SEARCH_DATA_TYPE_BE_BIT) {
                    old = __builtin_bswap16(old);
                }
                results->value = old;
            }
            else {
                results->value = *old_data;
            }
            size_t offset = old_data - gb->cheat_search_data;
            if (offset < gb->ram_size) {
                results->bank = offset / 0x1000;
                results->addr = (offset & 0xfff) + (results->bank? 0xd000 : 0xc000);
            }
            else if (offset < gb->ram_size + gb->mbc_ram_size) {
                results->addr = (offset & 0x1fff) + 0xa000;
                results->bank = (offset - gb->ram_size) / 0x2000;
            }
            else {
                results->addr = (offset & 0x7f) + 0xff80;
                results->bank = 0;
            }
            results++;
        }
        old_data++;
        mask <<= 1;
        if (mask == 0) {
            mask = 1;
            bitmap++;
        }
    }
}

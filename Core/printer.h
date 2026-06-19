#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "defs.h"
#include "save_state.h"

#define GB_PRINTER_MAX_COMMAND_LENGTH 0x280
#define GB_PRINTER_DATA_SIZE 0x280

typedef void (*GB_print_image_callback_t)(GB_gameboy_t *gb,
                                          uint32_t *image,
                                          uint8_t height,
                                          uint8_t top_margin,
                                          uint8_t bottom_margin,
                                          uint8_t exposure);

typedef void (*GB_printer_done_callback_t)(GB_gameboy_t *gb);

typedef struct
{
    /* Communication state machine */

    GB_ENUM(uint8_t, {
        GB_PRINTER_COMMAND_MAGIC1,
        GB_PRINTER_COMMAND_MAGIC2,
        GB_PRINTER_COMMAND_ID,
        GB_PRINTER_COMMAND_COMPRESSION,
        GB_PRINTER_COMMAND_LENGTH_LOW,
        GB_PRINTER_COMMAND_LENGTH_HIGH,
        GB_PRINTER_COMMAND_DATA,
        GB_PRINTER_COMMAND_CHECKSUM_LOW,
        GB_PRINTER_COMMAND_CHECKSUM_HIGH,
        GB_PRINTER_COMMAND_ACTIVE,
        GB_PRINTER_COMMAND_STATUS,
    }) command_state;
    GB_ENUM(uint8_t, {
        GB_PRINTER_INIT_COMMAND = 1,
        GB_PRINTER_START_COMMAND = 2,
        GB_PRINTER_DATA_COMMAND = 4,
        GB_PRINTER_NOP_COMMAND = 0xF,
    }) command_id;
    bool compression;
    uint16_t length_left;
    uint8_t command_data[GB_PRINTER_MAX_COMMAND_LENGTH];
    uint16_t command_length;
    uint16_t checksum;
    uint8_t status;
    uint8_t byte_to_send;
    
    uint8_t image[160 * 200];
    uint16_t image_offset;

    uint32_t idle_time;
    uint32_t time_remaining;
    
    uint8_t compression_run_lenth;
    bool compression_run_is_compressed;
    
    uint8_t bits_received;
    uint8_t byte_being_received;
    bool bit_to_send;
} GB_printer_t;


void GB_connect_printer(GB_gameboy_t *gb, GB_print_image_callback_t callback, GB_printer_done_callback_t done_callback);

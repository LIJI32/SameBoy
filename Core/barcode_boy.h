#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "defs.h"

/*
    Namcot Barcode Boy (Japan, 1992)

    Protocol (per Shonumi's "Dan Docs", public domain):
    - The Game Boy, using its internal clock, sends a handshake of 10 07 10 07.
      A powered-on Barcode Boy answers FF FF 10 07.
    - The game then switches to the external clock and waits. When a card is
      swiped, the Barcode Boy becomes the clock master and sends 30 bytes:
      02 <13 ASCII digits> 03 02 <13 ASCII digits> 03
    - After a scan, the game must perform the handshake again.
*/

#define GB_BARCODE_BOY_DIGITS 13
#define GB_BARCODE_BOY_PACKET_SIZE 30

typedef struct {
    /* Handshake (Game Boy is clock master) */
    uint8_t byte_being_received;
    uint8_t bits_received;
    uint8_t byte_to_send;
    bool bit_to_send;
    uint8_t handshake_position;
    bool handshake_done;

    /* Scan (Barcode Boy is clock master) */
    uint8_t packet[GB_BARCODE_BOY_PACKET_SIZE];
    uint8_t packet_position;
    uint8_t bit_position;
    bool scan_pending;
    uint32_t cycle_counter;
} GB_barcode_boy_t;

void GB_connect_barcode_boy(GB_gameboy_t *gb);
/* digits must be exactly 13 characters '0'-'9'. Returns false if the input is invalid
   or if the Barcode Boy is not connected. The scan is sent as soon as the game is ready. */
bool GB_barcode_boy_scan(GB_gameboy_t *gb, const char *digits);
/* True once the game has completed the handshake and is waiting for a card */
bool GB_barcode_boy_is_waiting_for_scan(GB_gameboy_t *gb);
/* True while a scan is queued or being transmitted */
bool GB_barcode_boy_is_scanning(GB_gameboy_t *gb);

#ifdef GB_INTERNAL
internal void GB_barcode_boy_run(GB_gameboy_t *gb, unsigned cycles);
#endif

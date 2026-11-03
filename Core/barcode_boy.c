#include "gb.h"
#include <string.h>

/*
    Barcode Boy emulation. See barcode_boy.h for the protocol summary.

    Timing of the scan is not documented; the values below are conservative.
    The games wait on the serial interrupt and re-arm SC after each byte, so the
    exact speed is not critical. Units are the ones passed to GB_advance_cycles'
    accessory hooks (8 MiHz in both speed modes).
*/

#define BIT_PERIOD (1024)           // ~8 KHz serial clock
#define BYTE_GAP   (0x4000)         // ~2 ms between bytes

static const uint8_t handshake[4] = {0x10, 0x07, 0x10, 0x07};
static const uint8_t handshake_reply[4] = {0xFF, 0xFF, 0x10, 0x07};

static void byte_received(GB_gameboy_t *gb, uint8_t byte)
{
    GB_barcode_boy_t *bb = &gb->barcode_boy;

    if (bb->handshake_done) {
        /* After the handshake the scanner answers FF to anything sent with the internal clock */
        if (byte == handshake[0]) {
            /* The game restarted the handshake */
            bb->handshake_done = false;
            bb->handshake_position = 1;
            bb->byte_to_send = handshake_reply[1];
            return;
        }
        bb->byte_to_send = 0xFF;
        return;
    }

    if (byte != handshake[bb->handshake_position]) {
        bb->handshake_position = 0;
        if (byte != handshake[0]) {
            bb->byte_to_send = 0xFF;
            return;
        }
    }

    bb->handshake_position++;
    if (bb->handshake_position == sizeof(handshake)) {
        bb->handshake_position = 0;
        bb->handshake_done = true;
        bb->byte_to_send = 0xFF;
        bb->cycle_counter = 0;
        return;
    }
    /* Reply for the next byte is decided now, because the reply bits are shifted
       out while the next byte is shifted in. */
    bb->byte_to_send = handshake_reply[bb->handshake_position];
}

static void serial_start(GB_gameboy_t *gb, bool bit_received)
{
    GB_barcode_boy_t *bb = &gb->barcode_boy;
    bb->byte_being_received <<= 1;
    bb->byte_being_received |= bit_received;
    bb->bits_received++;
    if (bb->bits_received == 8) {
        byte_received(gb, bb->byte_being_received);
        bb->bits_received = 0;
        bb->byte_being_received = 0;
    }
}

static bool serial_end(GB_gameboy_t *gb)
{
    GB_barcode_boy_t *bb = &gb->barcode_boy;
    bool ret = bb->bit_to_send;
    bb->bit_to_send = bb->byte_to_send & 0x80;
    bb->byte_to_send <<= 1;
    bb->byte_to_send |= 1; // Idle line is high
    return ret;
}

void GB_connect_barcode_boy(GB_gameboy_t *gb)
{
    GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    memset(&gb->barcode_boy, 0, sizeof(gb->barcode_boy));
    gb->barcode_boy.byte_to_send = 0xFF;
    gb->barcode_boy.bit_to_send = true;
    GB_set_serial_transfer_bit_start_callback(gb, serial_start);
    GB_set_serial_transfer_bit_end_callback(gb, serial_end);
    gb->accessory = GB_ACCESSORY_BARCODE_BOY;
}

bool GB_barcode_boy_scan(GB_gameboy_t *gb, const char *digits)
{
    if (gb->accessory != GB_ACCESSORY_BARCODE_BOY) return false;
    if (!digits || strlen(digits) != GB_BARCODE_BOY_DIGITS) return false;
    for (unsigned i = 0; i < GB_BARCODE_BOY_DIGITS; i++) {
        if (digits[i] < '0' || digits[i] > '9') return false;
    }

    GB_barcode_boy_t *bb = &gb->barcode_boy;
    uint8_t *p = bb->packet;
    for (unsigned copy = 0; copy < 2; copy++) {
        *p++ = 0x02; // STX
        memcpy(p, digits, GB_BARCODE_BOY_DIGITS);
        p += GB_BARCODE_BOY_DIGITS;
        *p++ = 0x03; // ETX
    }
    bb->packet_position = 0;
    bb->bit_position = 0;
    bb->cycle_counter = 0;
    bb->scan_pending = true;
    return true;
}

bool GB_barcode_boy_is_waiting_for_scan(GB_gameboy_t *gb)
{
    return gb->accessory == GB_ACCESSORY_BARCODE_BOY &&
           gb->barcode_boy.handshake_done &&
           !gb->barcode_boy.scan_pending;
}

bool GB_barcode_boy_is_scanning(GB_gameboy_t *gb)
{
    return gb->accessory == GB_ACCESSORY_BARCODE_BOY && gb->barcode_boy.scan_pending;
}

void GB_barcode_boy_run(GB_gameboy_t *gb, unsigned cycles)
{
    GB_barcode_boy_t *bb = &gb->barcode_boy;
    if (likely(!bb->scan_pending || !bb->handshake_done)) return;

    bb->cycle_counter += cycles;

    while (true) {
        /* The scanner only clocks bits while the game listens on the external clock */
        if ((gb->io_registers[GB_IO_SC] & 0x81) != 0x80) {
            /* Not armed yet; restart the inter-byte gap once it is */
            if (bb->bit_position == 0 && bb->cycle_counter > BYTE_GAP) {
                bb->cycle_counter = BYTE_GAP;
            }
            return;
        }

        unsigned needed = bb->bit_position == 0? BYTE_GAP : BIT_PERIOD;
        if (bb->cycle_counter < needed) return;
        bb->cycle_counter -= needed;

        uint8_t byte = bb->packet[bb->packet_position];
        GB_serial_set_data_bit(gb, (byte >> (7 - bb->bit_position)) & 1);
        bb->bit_position++;

        if (bb->bit_position == 8) {
            bb->bit_position = 0;
            bb->packet_position++;
            if (bb->packet_position == GB_BARCODE_BOY_PACKET_SIZE) {
                /* Done; the game must handshake again before the next card */
                bb->scan_pending = false;
                bb->handshake_done = false;
                bb->handshake_position = 0;
                bb->packet_position = 0;
                bb->cycle_counter = 0;
                return;
            }
        }
    }
}

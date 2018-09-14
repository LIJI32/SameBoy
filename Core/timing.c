#include "gb.h"
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif
#include <Windows.h>
#else
#include <sys/time.h>
#endif

static const unsigned int GB_TAC_RATIOS[] = {1024, 16, 64, 256};

#ifndef DISABLE_TIMEKEEPING
static int64_t get_nanoseconds(void)
{
#ifndef _WIN32
    struct timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_usec) * 1000 + now.tv_sec * 1000000000L;
#else
    FILETIME time;
    GetSystemTimeAsFileTime(&time);
    return (((int64_t)time.dwHighDateTime << 32) | time.dwLowDateTime) * 100L;
#endif
}

static void nsleep(uint64_t nanoseconds)
{
#ifndef _WIN32
    struct timespec sleep = {0, nanoseconds};
    nanosleep(&sleep, NULL);
#else
    HANDLE timer;
    LARGE_INTEGER time;
    timer = CreateWaitableTimer(NULL, true, NULL);
    time.QuadPart = -(nanoseconds / 100L);
    SetWaitableTimer(timer, &time, 0, NULL, NULL, false);
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
#endif
}

bool GB_timing_sync_turbo(GB_gameboy_t *gb)
{
    if (!gb->turbo_dont_skip) {
        int64_t nanoseconds = get_nanoseconds();
        if (nanoseconds <= gb->last_sync + (1000000000LL * LCDC_PERIOD / GB_get_clock_rate(gb))) {
            return true;
        }
        gb->last_sync = nanoseconds;
    }
    return false;
}

void GB_timing_sync(GB_gameboy_t *gb)
{
    if (gb->turbo) {
        gb->cycles_since_last_sync = 0;
        return;
    }
    /* Prevent syncing if not enough time has passed.*/
    if (gb->cycles_since_last_sync < LCDC_PERIOD / 4) return;

    uint64_t target_nanoseconds = gb->cycles_since_last_sync * 1000000000LL / 2 / GB_get_clock_rate(gb); /* / 2 because we use 8MHz units */
    int64_t nanoseconds = get_nanoseconds();
    int64_t time_to_sleep = target_nanoseconds + gb->last_sync - nanoseconds;
    if (time_to_sleep > 0 && time_to_sleep < LCDC_PERIOD * 1000000000LL / GB_get_clock_rate(gb)) {
        nsleep(time_to_sleep);
        gb->last_sync += target_nanoseconds;
    }
    else {
        gb->last_sync = nanoseconds;
    }

    gb->cycles_since_last_sync = 0;
}
#else

bool GB_timing_sync_turbo(GB_gameboy_t *gb)
{
    return false;
}

void GB_timing_sync(GB_gameboy_t *gb)
{
}

#endif
static void GB_ir_run(GB_gameboy_t *gb)
{
    if (gb->ir_queue_length == 0) return;
    if (gb->cycles_since_input_ir_change >= gb->ir_queue[0].delay) {
        gb->cycles_since_input_ir_change -= gb->ir_queue[0].delay;
        gb->infrared_input = gb->ir_queue[0].state;
        gb->ir_queue_length--;
        memmove(&gb->ir_queue[0], &gb->ir_queue[1], sizeof(gb->ir_queue[0]) * (gb->ir_queue_length));
    }
}

static void advance_tima_state_machine(GB_gameboy_t *gb)
{
    if (gb->tima_reload_state == GB_TIMA_RELOADED) {
        gb->tima_reload_state = GB_TIMA_RUNNING;
    }
    else if (gb->tima_reload_state == GB_TIMA_RELOADING) {
        gb->io_registers[GB_IO_IF] |= 4;
        gb->tima_reload_state = GB_TIMA_RELOADED;
    }
}

static bool counter_overflow_check(uint32_t old, uint32_t new, uint32_t max)
{
    return (old & (max >> 1)) && !(new & (max >> 1));
}

static void increase_tima(GB_gameboy_t *gb)
{
    gb->io_registers[GB_IO_TIMA]++;
    if (gb->io_registers[GB_IO_TIMA] == 0) {
        gb->io_registers[GB_IO_TIMA] = gb->io_registers[GB_IO_TMA];
        gb->tima_reload_state = GB_TIMA_RELOADING;
    }
}

static void GB_set_internal_div_counter(GB_gameboy_t *gb, uint32_t value)
{
    /* TIMA increases when a specific high-bit becomes a low-bit. */
    value &= INTERNAL_DIV_CYCLES - 1;
    if ((gb->io_registers[GB_IO_TAC] & 4) &&
        counter_overflow_check(gb->div_counter, value, GB_TAC_RATIOS[gb->io_registers[GB_IO_TAC] & 3])) {
        increase_tima(gb);
    }
    
    /* TODO: Can switching to double speed mode trigger an event? */
    if (counter_overflow_check(gb->div_counter, value, gb->cgb_double_speed? 0x4000 : 0x2000)) {
        GB_apu_run(gb);
        GB_apu_div_event(gb);
    }
    gb->div_counter = value;
}

static void GB_timers_run(GB_gameboy_t *gb, uint8_t cycles)
{
    GB_STATE_MACHINE(gb, div, cycles, 1) {
        GB_STATE(gb, div, 1);
        GB_STATE(gb, div, 2);
        GB_STATE(gb, div, 3);
    }
    
    GB_set_internal_div_counter(gb, 0);
main:
    GB_SLEEP(gb, div, 1, 3);
    while (true) {
        advance_tima_state_machine(gb);
        GB_set_internal_div_counter(gb, gb->div_counter + 4);
        gb->apu.apu_cycles += 4 << !gb->cgb_double_speed;
        GB_SLEEP(gb, div, 2, 4);
    }
    
    /* Todo: This is ugly to allow compatibility with 0.11 save states. Fix me when breaking save compatibility */
    {
        div3:
        /* Compensate for lack of prefetch emulation, as well as DIV's internal initial value */
        GB_set_internal_div_counter(gb, 8);
        goto main;
    }
}

void GB_advance_cycles(GB_gameboy_t *gb, uint8_t cycles)
{   
    // Affected by speed boost
    gb->dma_cycles += cycles;

    GB_timers_run(gb, cycles);

    uint16_t previous_serial_cycles = gb->serial_cycles;
    gb->serial_cycles += cycles;
    if (gb->serial_length) {
        if ((gb->serial_cycles & gb->serial_length) != (previous_serial_cycles & gb->serial_length)) {
            gb->serial_length = 0;
            gb->io_registers[GB_IO_SC] &= ~0x80;
            /* TODO: Does SB "update" bit by bit? */
            if (gb->serial_transfer_end_callback) {
                gb->io_registers[GB_IO_SB] = gb->serial_transfer_end_callback(gb);
            }
            else {
                gb->io_registers[GB_IO_SB] = 0xFF;
            }
            
            gb->io_registers[GB_IO_IF] |= 8;
        }
    }

    gb->debugger_ticks += cycles;

    if (!gb->cgb_double_speed) {
        cycles <<= 1;
    }

    // Not affected by speed boost
    gb->hdma_cycles += cycles;
    gb->apu_output.sample_cycles += cycles;
    gb->cycles_since_ir_change += cycles;
    gb->cycles_since_input_ir_change += cycles;
    gb->cycles_since_last_sync += cycles;
    gb->cycles_since_run += cycles;
    GB_dma_run(gb);
    GB_hdma_run(gb);
    GB_apu_run(gb);
    GB_display_run(gb, cycles);
    GB_ir_run(gb);
}

/* 
   This glitch is based on the expected results of mooneye-gb rapid_toggle test.
   This glitch happens because how TIMA is increased, see GB_set_internal_div_counter.
   According to GiiBiiAdvance, GBC's behavior is different, but this was not tested or implemented.
*/
void GB_emulate_timer_glitch(GB_gameboy_t *gb, uint8_t old_tac, uint8_t new_tac)
{
    /* Glitch only happens when old_tac is enabled. */
    if (!(old_tac & 4)) return;

    unsigned int old_clocks = GB_TAC_RATIOS[old_tac & 3];
    unsigned int new_clocks = GB_TAC_RATIOS[new_tac & 3];

    /* The bit used for overflow testing must have been 1 */
    if (gb->div_counter & (old_clocks >> 1)) {
        /* And now either the timer must be disabled, or the new bit used for overflow testing be 0. */
        if (!(new_tac & 4) || gb->div_counter & (new_clocks >> 1)) {
            increase_tima(gb);
        }
    }
}

void GB_rtc_run(GB_gameboy_t *gb)
{
    if ((gb->rtc_real.high & 0x40) == 0) { /* is timer running? */
        time_t current_time = time(NULL);
        while (gb->last_rtc_second < current_time) {
            gb->last_rtc_second++;
            if (++gb->rtc_real.seconds == 60)
            {
                gb->rtc_real.seconds = 0;
                if (++gb->rtc_real.minutes == 60)
                {
                    gb->rtc_real.minutes = 0;
                    if (++gb->rtc_real.hours == 24)
                    {
                        gb->rtc_real.hours = 0;
                        if (++gb->rtc_real.days == 0)
                        {
                            if (gb->rtc_real.high & 1) /* Bit 8 of days*/
                            {
                                gb->rtc_real.high |= 0x80; /* Overflow bit */
                            }
                            gb->rtc_real.high ^= 1;
                        }
                    }
                }
            }
        }
    }
}

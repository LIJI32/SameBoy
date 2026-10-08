#pragma once

#include "defs.h"
#include <time.h>

typedef enum {
    GB_RTC_MODE_SYNC_TO_HOST,
    GB_RTC_MODE_ACCURATE,
} GB_rtc_mode_t;

/* RTC emulation mode */
void GB_set_rtc_mode(GB_gameboy_t *gb, GB_rtc_mode_t mode);

/* Speed multiplier for the RTC, mostly for TAS syncing */
void GB_set_rtc_multiplier(GB_gameboy_t *gb, double multiplier);

/* Time source for RTC emulation and battery/save state timestamps; NULL
   (the default) means time(NULL). Setting a callback re-anchors the RTC. */
typedef time_t (*GB_time_callback_t)(GB_gameboy_t *gb);
void GB_set_time_callback(GB_gameboy_t *gb, GB_time_callback_t callback);

#ifdef GB_INTERNAL
internal void GB_advance_cycles(GB_gameboy_t *gb, uint8_t cycles);
internal void GB_emulate_timer_glitch(GB_gameboy_t *gb, uint8_t old_tac, uint8_t new_tac);
internal bool GB_timing_sync_turbo(GB_gameboy_t *gb); /* Returns true if should skip frame */
internal void GB_timing_sync(GB_gameboy_t *gb);
internal void GB_set_internal_div_counter(GB_gameboy_t *gb, uint16_t value);
internal void GB_serial_master_edge(GB_gameboy_t *gb);
internal void GB_rtc_set_time(GB_gameboy_t *gb, uint64_t time);
internal time_t GB_host_time(GB_gameboy_t *gb);

#define GB_SLEEP(gb, unit, state, cycles) do {\
    (gb)->unit##_cycles -= (cycles) * __state_machine_divisor; \
    if (unlikely((gb)->unit##_cycles <= 0)) {\
        (gb)->unit##_state = state;\
        return;\
        unit##state:; \
    }\
} while (0)

#define GB_BATCHPOINT(gb, unit, state, cycles) do {\
unit##state:; \
if (likely(__state_machine_allow_batching && (gb)->unit##_cycles < (cycles * 2))) {\
    (gb)->unit##_state = state;\
    return;\
}\
} while (0)

#define GB_BATCHED_CYCLES(gb, unit) ((gb)->unit##_cycles / __state_machine_divisor)

#define GB_STATE_MACHINE(gb, unit, cycles, divisor) \
static const int __state_machine_divisor = divisor;\
(gb)->unit##_cycles += cycles; \
if ((gb)->unit##_cycles <= 0) {\
    return;\
}\
switch ((gb)->unit##_state)

#define GB_BATCHABLE_STATE_MACHINE(gb, unit, cycles, divisor, allow_batching) \
const bool __state_machine_allow_batching = (allow_batching); \
GB_STATE_MACHINE(gb, unit, cycles, divisor)

#define GB_STATE(gb, unit, state) case state: goto unit##state
#endif

#define GB_UNIT(unit) int32_t unit##_cycles, unit##_state

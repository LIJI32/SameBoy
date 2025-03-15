#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdalign.h>
#include <time.h>

#ifndef GB_DISABLE_CHEAT_SEARCH
#ifdef GB_DISABLE_CHEATS
#define GB_DISABLE_CHEAT_SEARCH
#else
#ifdef GB_DISABLE_DEBUGGER
#define GB_DISABLE_CHEAT_SEARCH
#endif
#endif
#endif

#include "model.h"
#include "defs.h"
#include "save_state.h"

#include "apu.h"
#include "camera.h"
#include "debugger.h"
#include "display.h"
#include "joypad.h"
#include "mbc.h"
#include "memory.h"
#include "printer.h"
#include "timing.h"
#include "rewind.h"
#include "sm83_cpu.h"
#include "symbol_hash.h"
#include "sgb.h"
#include "cheats.h"
#include "cheat_search.h"
#include "rumble.h"
#include "workboy.h"
#include "random.h"

#ifdef GB_INTERNAL
#define STRUCT_VERSION 15
#endif

#define GB_REWIND_FRAMES_PER_KEY 255

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define GB_BIG_ENDIAN
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define GB_LITTLE_ENDIAN
#else
#error Unable to detect endianess
#endif

#ifdef GB_BIG_ENDIAN
#define GB_REGISTER_ORDER a, f, \
                          b, c, \
                          d, e, \
                          h, l
#else
#define GB_REGISTER_ORDER f, a, \
                          c, b, \
                          e, d, \
                          l, h
#endif

typedef union {
    struct {
        uint8_t seconds;
        uint8_t minutes;
        uint8_t hours;
        uint8_t days;
        uint8_t high;
    };
    struct {
        uint8_t seconds;
        uint8_t minutes;
        uint8_t hours:5;
        uint8_t weekday:3;
        uint8_t weeks;
    } tpp1;
    uint8_t data[5];
} GB_rtc_time_t;

typedef struct __attribute__((packed)) {
    uint64_t last_rtc_second;
    uint16_t minutes;
    uint16_t days;
    uint16_t alarm_minutes, alarm_days;
    uint8_t alarm_enabled;
} GB_huc3_rtc_time_t;

enum {
    GB_REGISTER_AF,
    GB_REGISTER_BC,
    GB_REGISTER_DE,
    GB_REGISTER_HL,
    GB_REGISTER_SP,
    GB_REGISTER_PC,
    GB_REGISTERS_16_BIT /* Count */
};

/* Todo: Actually use these! */
enum {
    GB_CARRY_FLAG = 0x10,
    GB_HALF_CARRY_FLAG = 0x20,
    GB_SUBTRACT_FLAG = 0x40,
    GB_ZERO_FLAG = 0x80,
};

enum {
    GB_LCDC_BG_EN = 1,
    GB_LCDC_OBJ_EN = 2,
    GB_LCDC_OBJ_SIZE = 4,
    GB_LCDC_BG_MAP = 8,
    GB_LCDC_TILE_SEL = 0x10,
    GB_LCDC_WIN_ENABLE = 0x20,
    GB_LCDC_WIN_MAP = 0x40,
    GB_LCDC_ENABLE = 0x80,
};

typedef enum {
    GB_BORDER_SGB,
    GB_BORDER_NEVER,
    GB_BORDER_ALWAYS,
} GB_border_mode_t;

enum {
    /* Joypad and Serial */
    GB_IO_JOYP       = 0x00, // Joypad (R/W)
    GB_IO_SB         = 0x01, // Serial transfer data (R/W)
    GB_IO_SC         = 0x02, // Serial Transfer Control (R/W)

    /* Missing */

    /* Timers */
    GB_IO_DIV        = 0x04, // Divider Register (R/W)
    GB_IO_TIMA       = 0x05, // Timer counter (R/W)
    GB_IO_TMA        = 0x06, // Timer Modulo (R/W)
    GB_IO_TAC        = 0x07, // Timer Control (R/W)

    /* Missing */

    GB_IO_IF         = 0x0F, // Interrupt Flag (R/W)

    /* Sound */
    GB_IO_NR10       = 0x10, // Channel 1 Sweep register (R/W)
    GB_IO_NR11       = 0x11, // Channel 1 Sound length/Wave pattern duty (R/W)
    GB_IO_NR12       = 0x12, // Channel 1 Volume Envelope (R/W)
    GB_IO_NR13       = 0x13, // Channel 1 Frequency lo (Write Only)
    GB_IO_NR14       = 0x14, // Channel 1 Frequency hi (R/W)
    /* NR20 does not exist */
    GB_IO_NR21       = 0x16, // Channel 2 Sound Length/Wave Pattern Duty (R/W)
    GB_IO_NR22       = 0x17, // Channel 2 Volume Envelope (R/W)
    GB_IO_NR23       = 0x18, // Channel 2 Frequency lo data (W)
    GB_IO_NR24       = 0x19, // Channel 2 Frequency hi data (R/W)
    GB_IO_NR30       = 0x1A, // Channel 3 Sound on/off (R/W)
    GB_IO_NR31       = 0x1B, // Channel 3 Sound Length
    GB_IO_NR32       = 0x1C, // Channel 3 Select output level (R/W)
    GB_IO_NR33       = 0x1D, // Channel 3 Frequency's lower data (W)
    GB_IO_NR34       = 0x1E, // Channel 3 Frequency's higher data (R/W)
    /* NR40 does not exist */
    GB_IO_NR41       = 0x20, // Channel 4 Sound Length (R/W)
    GB_IO_NR42       = 0x21, // Channel 4 Volume Envelope (R/W)
    GB_IO_NR43       = 0x22, // Channel 4 Polynomial Counter (R/W)
    GB_IO_NR44       = 0x23, // Channel 4 Counter/consecutive, Initial (R/W)
    GB_IO_NR50       = 0x24, // Channel control / ON-OFF / Volume (R/W)
    GB_IO_NR51       = 0x25, // Selection of Sound output terminal (R/W)
    GB_IO_NR52       = 0x26, // Sound on/off

    /* Missing */

    GB_IO_WAV_START  = 0x30, // Wave pattern start
    GB_IO_WAV_END    = 0x3F, // Wave pattern end

    /* Graphics */
    GB_IO_LCDC       = 0x40, // LCD Control (R/W)
    GB_IO_STAT       = 0x41, // LCDC Status (R/W)
    GB_IO_SCY        = 0x42, // Scroll Y (R/W)
    GB_IO_SCX        = 0x43, // Scroll X (R/W)
    GB_IO_LY         = 0x44, // LCDC Y-Coordinate (R)
    GB_IO_LYC        = 0x45, // LY Compare (R/W)
    GB_IO_DMA        = 0x46, // DMA Transfer and Start Address (W)
    GB_IO_BGP        = 0x47, // BG Palette Data (R/W) - Non CGB Mode Only
    GB_IO_OBP0       = 0x48, // Object Palette 0 Data (R/W) - Non CGB Mode Only
    GB_IO_OBP1       = 0x49, // Object Palette 1 Data (R/W) - Non CGB Mode Only
    GB_IO_WY         = 0x4A, // Window Y Position (R/W)
    GB_IO_WX         = 0x4B, // Window X Position minus 7 (R/W)
    // Controls DMG mode and PGB mode
    GB_IO_KEY0 = 0x4C,

    /* General CGB features */
    GB_IO_KEY1       = 0x4D, // CGB Mode Only - Prepare Speed Switch

    /* Missing */

    GB_IO_VBK        = 0x4F, // CGB Mode Only - VRAM Bank
    GB_IO_BANK       = 0x50, // Write to disable the boot ROM mapping

    /* CGB DMA */
    GB_IO_HDMA1      = 0x51, // CGB Mode Only - New DMA Source, High
    GB_IO_HDMA2      = 0x52, // CGB Mode Only - New DMA Source, Low
    GB_IO_HDMA3      = 0x53, // CGB Mode Only - New DMA Destination, High
    GB_IO_HDMA4      = 0x54, // CGB Mode Only - New DMA Destination, Low
    GB_IO_HDMA5      = 0x55, // CGB Mode Only - New DMA Length/Mode/Start

    /* IR */
    GB_IO_RP         = 0x56, // CGB Mode Only - Infrared Communications Port

    /* Missing */

    /* CGB Palettes */
    GB_IO_BGPI       = 0x68, // CGB Mode Only - Background Palette Index
    GB_IO_BGPD       = 0x69, // CGB Mode Only - Background Palette Data
    GB_IO_OBPI       = 0x6A, // CGB Mode Only - Object Palette Index
    GB_IO_OBPD       = 0x6B, // CGB Mode Only - Object Palette Data
    GB_IO_OPRI       = 0x6C, // Affects object priority (X based or index based)

    /* Missing */

    GB_IO_SVBK       = 0x70, // CGB Mode Only - WRAM Bank
    GB_IO_PSM        = 0x71, // Palette Selection Mode, controls the PSW and key combo
    GB_IO_PSWX       = 0x72, // X position of the palette switching window
    GB_IO_PSWY       = 0x73, // Y position of the palette switching window
    GB_IO_PSW        = 0x74, // Key combo to trigger the palette switching window
    GB_IO_PGB        = 0x75, // Bits 0-2 control PHI, A15 and ¬CS, respectively.  Bits 4-6 control the I/O directions of bits 0-2 (0 is R, 1 is W)
    GB_IO_PCM12      = 0x76, // Channels 1 and 2 amplitudes
    GB_IO_PCM34      = 0x77, // Channels 3 and 4 amplitudes
};

static const typeof(GB_IO_PGB) __attribute__((deprecated("Use GB_IO_PGB instead"))) GB_IO_UNKNOWN5 = GB_IO_PGB;

typedef enum {
    GB_LOG_BOLD = 1,
    GB_LOG_DASHED_UNDERLINE = 2,
    GB_LOG_UNDERLINE = 4,
    GB_LOG_UNDERLINE_MASK =  GB_LOG_DASHED_UNDERLINE | GB_LOG_UNDERLINE
} GB_log_attributes_t;

typedef enum {
    GB_BOOT_ROM_DMG_0,
    GB_BOOT_ROM_DMG,
    GB_BOOT_ROM_MGB,
    GB_BOOT_ROM_SGB,
    GB_BOOT_ROM_SGB2,
    GB_BOOT_ROM_CGB_0,
    GB_BOOT_ROM_CGB,
    GB_BOOT_ROM_CGB_E,
    GB_BOOT_ROM_AGB_0,
    GB_BOOT_ROM_AGB,
} GB_boot_rom_t;

#ifdef GB_INTERNAL
#define LCDC_PERIOD 70224
#define CPU_FREQUENCY 0x400000
#define SGB_NTSC_FREQUENCY (21477272 / 5)
#define SGB_PAL_FREQUENCY (21281370 / 5)
#define DIV_CYCLES (0x100)

#ifdef GB_DISABLE_REWIND
#define GB_rewind_reset(...)
#define GB_rewind_push(...)
#define GB_rewind_invalidate_for_backstepping(...)
#endif

#endif

typedef void (*GB_log_callback_t)(GB_gameboy_t *gb, const char *string, GB_log_attributes_t attributes);
typedef char *(*GB_input_callback_t)(GB_gameboy_t *gb);
typedef void (*GB_infrared_callback_t)(GB_gameboy_t *gb, bool on);
typedef void (*GB_rumble_callback_t)(GB_gameboy_t *gb, double rumble_amplitude);
typedef void (*GB_serial_transfer_bit_start_callback_t)(GB_gameboy_t *gb, bool bit_to_send);
typedef bool (*GB_serial_transfer_bit_end_callback_t)(GB_gameboy_t *gb);
typedef void (*GB_joyp_write_callback_t)(GB_gameboy_t *gb, uint8_t value);
typedef void (*GB_icd_pixel_callback_t)(GB_gameboy_t *gb, uint8_t row);
typedef void (*GB_icd_hreset_callback_t)(GB_gameboy_t *gb);
typedef void (*GB_icd_vreset_callback_t)(GB_gameboy_t *gb);
typedef void (*GB_boot_rom_load_callback_t)(GB_gameboy_t *gb, GB_boot_rom_t type);

typedef void (*GB_execution_callback_t)(GB_gameboy_t *gb, uint16_t address, uint8_t opcode);
typedef void (*GB_lcd_line_callback_t)(GB_gameboy_t *gb, uint8_t line);
typedef void (*GB_lcd_status_callback_t)(GB_gameboy_t *gb, bool on);

struct GB_breakpoint_s;
struct GB_watchpoint_s;

typedef struct {
    uint32_t magic;
    uint8_t track_count;
    uint8_t first_track;
    uint16_t load_address;
    uint16_t init_address;
    uint16_t play_address;
    uint16_t sp;
    uint8_t TMA;
    uint8_t TAC;
    char title[32];
    char author[32];
    char copyright[32];
} GB_gbs_header_t;

typedef struct {
    uint8_t track_count;
    uint8_t first_track;
    char title[33];
    char author[33];
    char copyright[33];
} GB_gbs_info_t;

/* Duplicated so it can remain anonymous in GB_gameboy_t */
typedef union {
    uint16_t registers[GB_REGISTERS_16_BIT];
    struct {
        uint16_t af,
                 bc,
                 de,
                 hl,
                 sp,
                 pc;
    };
    struct {
        uint8_t GB_REGISTER_ORDER;
    };
} GB_registers_t;

typedef GB_ENUM(uint8_t, {
    GB_ACCESSORY_NONE,
    GB_ACCESSORY_PRINTER,
    GB_ACCESSORY_WORKBOY,
}) GB_accessory_t;

/* When state saving, each section is dumped independently of other sections.
   This allows adding data to the end of the section without worrying about future compatibility.
   Some other changes might be "safe" as well.
   This struct is not packed, but dumped sections exclusively use types that have the same alignment in both 32 and 64
   bit platforms. */

#ifdef GB_INTERNAL
struct GB_gameboy_s {
#else
struct GB_gameboy_internal_s {
#endif
    GB_SECTION(header,
        /* The magic makes sure a state file is:
            - Indeed a SameBoy state file.
            - Has the same endianess has the current platform. */
        volatile uint32_t magic;
        /* The version field makes sure we don't load save state files with a completely different structure.
           This happens when struct fields are removed/resized in an backward incompatible manner. */
        uint32_t version;
    )

    GB_SECTION(core_state,
        /* Registers */
        union {
            uint16_t registers[GB_REGISTERS_16_BIT];
            struct {
                uint16_t af,
                         bc,
                         de,
                         hl,
                         sp,
                         pc;
            };
            struct {
                uint8_t GB_REGISTER_ORDER;
            };
        };
        uint8_t ime;
        uint8_t interrupt_enable;
        uint8_t cgb_ram_bank;

        /* CPU and General Hardware Flags*/
        GB_model_t model;
        bool cgb_mode;
        bool cgb_double_speed;
        bool halted;
        bool stopped;
        bool boot_rom_finished;
        bool ime_toggle; /* ei has delayed a effect.*/
        bool halt_bug;
        bool just_halted;

        /* Misc state */
        bool infrared_input;
        uint8_t extra_oam[0xFF00 - 0xFEA0];
        uint32_t ram_size; // Different between CGB and DMG
               
        int32_t ir_sensor;
        bool effective_ir_input;
        uint16_t address_bus;
        uint8_t data_bus; // cart data bus (MAIN)
        uint32_t data_bus_decay_countdown;
    )

    /* DMA and HDMA */
    GB_SECTION(dma,
        bool hdma_on;
        bool hdma_on_hblank;
        uint8_t hdma_steps_left;
        uint16_t hdma_current_src, hdma_current_dest;

        uint8_t dma_current_dest;
        uint8_t last_dma_read;
        uint16_t dma_current_src;
        uint16_t dma_cycles;
        int8_t dma_cycles_modulo;
        bool dma_ppu_vram_conflict;
        uint16_t dma_ppu_vram_conflict_addr;
        bool allow_hdma_on_wake;
        bool dma_restarting;
    )
    
    /* MBC */
    GB_SECTION(mbc,
        uint16_t mbc_rom_bank;
        uint16_t mbc_rom0_bank; /* For multicart mappings . */
        uint8_t mbc_ram_bank;
        uint32_t mbc_ram_size;
        bool mbc_ram_enable;
        union {
            struct {
                uint8_t bank_low:5;
                uint8_t bank_high:2;
                uint8_t mode:1;
            } mbc1;

            struct {
                uint8_t rom_bank:4;
            } mbc2;

            struct {
                uint8_t rom_bank:8;
                uint8_t ram_bank:3;
                bool rtc_mapped:1;
            } mbc3;

            struct {
                uint8_t rom_bank_low;
                uint8_t rom_bank_high:1;
                uint8_t ram_bank:4;
            } mbc5; // Also used for GB_CAMERA
               
            struct {
                uint16_t x_latch;
                uint16_t y_latch;
                uint8_t rom_bank;
                bool latch_ready:1;
                bool eeprom_do:1;
                bool eeprom_di:1;
                bool eeprom_clk:1;
                bool eeprom_cs:1;
                uint16_t eeprom_command:11;
                uint16_t read_bits;
                uint8_t argument_bits_left:5;
                bool secondary_ram_enable:1;
                bool eeprom_write_enabled:1;
            } mbc7;
               
            struct {
                uint8_t rom_bank_low:5;
                uint8_t rom_bank_mid:2;
                bool mbc1_mode:1;
               
                uint8_t rom_bank_mask:4;
                uint8_t rom_bank_high:2;
                uint8_t ram_bank_low:2;
               
                uint8_t ram_bank_high:2;
                uint8_t ram_bank_mask:2;

                bool locked:1;
                bool mbc1_mode_disable:1;
                bool multiplex_mode:1;
            } mmm01;
            
            struct {
                uint8_t bank_low:6;
                uint8_t bank_high:3;
                bool ir_mode:1;
            } huc1;

            struct {
                uint8_t rom_bank:7;
                uint8_t padding:1;
                uint8_t ram_bank:4;
                uint8_t mode:4;
                uint16_t minutes, days;
                uint16_t alarm_minutes, alarm_days;
                uint8_t access_index;
                bool alarm_enabled;
                uint8_t read;
                uint8_t access_flags;
            } huc3;
               
           struct {
               uint16_t rom_bank;
               uint8_t ram_bank;
               uint8_t mode;
           } tpp1;
        };
        uint8_t rumble_strength;
        bool cart_ir;
               
        bool camera_registers_mapped;
        uint8_t camera_registers[0x36];
        uint8_t camera_alignment;
        int32_t camera_countdown;
    )

    /* HRAM and HW Registers */
    GB_SECTION(hram,
        uint8_t hram[0xFFFF - 0xFF80];
        uint8_t io_registers[0x80];
    )

    /* Timing */
    GB_SECTION(timing,
        GB_UNIT(display);
        GB_UNIT(div);
        uint16_t div_counter;
        GB_ENUM(uint8_t, {
            GB_TIMA_RUNNING = 0,
            GB_TIMA_RELOADING = 1,
            GB_TIMA_RELOADED = 2
        }) tima_reload_state; /* After TIMA overflows, it becomes 0 for 4 cycles before actually reloading. */
        bool serial_master_clock;
        uint8_t serial_mask;
        uint8_t double_speed_alignment;
        uint8_t serial_count;
        int32_t speed_switch_halt_countdown;
        uint8_t speed_switch_countdown; // To compensate for the lack of pipeline emulation
        uint8_t speed_switch_freeze; // Solely for realigning the PPU, should be removed when the odd modes are implemented
        /* For timing of the vblank callback */
        uint32_t cycles_since_vblank_callback;
        bool lcd_disabled_outside_of_vblank;
        int32_t allowed_pending_cycles;
        uint16_t mode3_batching_length;
        uint8_t joyp_switching_delay;
        uint8_t joyp_switch_value;
        uint16_t key_bounce_timing[GB_KEY_MAX];
    )

    /* APU */
    GB_SECTION(apu,
        GB_apu_t apu;
    )

    /* RTC */
    GB_SECTION(rtc,
        GB_rtc_time_t rtc_real, rtc_latched;
        uint64_t last_rtc_second;
        uint32_t rtc_cycles;
        uint8_t tpp1_mr4;
    )

    /* Video Display */
    GB_SECTION(video,
        uint32_t vram_size; // Different between CGB and DMG
        bool cgb_vram_bank;
        uint8_t oam[0xA0];
        uint8_t background_palettes_data[0x40];
        uint8_t object_palettes_data[0x40];
        uint8_t position_in_line;
        bool stat_interrupt_line;
        uint8_t window_y;
        /* The LCDC will skip the first frame it renders after turning it on.
           On the CGB, a frame is not skipped if the previous frame was skipped as well.
           See https://www.reddit.com/r/EmuDev/comments/6exyxu/ */
               
        /* TODO: Drop this and properly emulate the dropped vreset signal*/
        GB_ENUM(uint8_t, {
            GB_FRAMESKIP_LCD_TURNED_ON, // On a DMG, the LCD renders a blank screen during this state,
                                        // on a CGB, the previous frame is repeated (which might be
                                        // blank if the LCD was off for more than a few cycles)
            GB_FRAMESKIP_FIRST_FRAME_RENDERED,
        }) frame_skip_state;
        bool oam_read_blocked;
        bool vram_read_blocked;
        bool oam_write_blocked;
        bool vram_write_blocked;
        uint8_t current_line;
        uint16_t ly_for_comparison;
        GB_fifo_t bg_fifo, oam_fifo;
        uint8_t fetcher_y;
        uint16_t cycles_for_line;
        uint8_t current_tile;
        uint8_t current_tile_attributes;
        uint8_t current_tile_data[2];
        uint8_t fetcher_state;
        bool window_is_being_fetched;
        GB_PADDING(bool, wx166_glitch);
        bool wx_triggered;
        uint8_t visible_objs[10];
        uint8_t objects_x[10];
        uint8_t objects_y[10];
        uint8_t object_tile_data[2];
        uint8_t mode2_y_bus;
        // They're the same bus
        union {
            uint8_t mode2_x_bus;
            uint8_t object_flags;
        };
        uint8_t n_visible_objs;
        uint8_t orig_n_visible_objs;
        uint8_t oam_search_index;
        uint8_t accessed_oam_row;
        uint8_t mode_for_interrupt;
        bool lyc_interrupt_line;
        bool cgb_palettes_blocked;
        uint8_t current_lcd_line; // The LCD can go out of sync since the vsync signal is skipped in some cases.
        uint8_t object_priority;
        bool oam_ppu_blocked;
        bool vram_ppu_blocked;
        bool cgb_palettes_ppu_blocked;
        bool object_fetch_aborted;
        bool during_object_fetch;
        uint16_t object_low_line_address;
        bool wy_triggered;
        uint8_t window_tile_x;
        uint8_t lcd_x; // The LCD can go out of sync since the push signal is skipped in some cases.
        bool is_odd_frame;
        uint16_t last_tile_data_address;
        uint16_t last_tile_index_address;
        uint8_t data_for_sel_glitch;
        bool delayed_glitch_hblank_interrupt;
        uint32_t frame_repeat_countdown;
        bool disable_window_pixel_insertion_glitch;
        bool insert_bg_pixel;
        uint8_t cpu_vram_bus;
        uint32_t frame_parity_ticks;
        bool last_tileset;
        bool cgb_wx_glitch;
        bool line_has_fractional_scrolling;
        uint8_t wy_check_modulo;
        bool wy_check_scheduled;
        bool wy_just_checked;
        bool wx_166_interrupt_glitch;
    )
    
    GB_SECTION(accessory,
        GB_accessory_t accessory;
        union {
            GB_printer_t printer;
            GB_workboy_t workboy;
        };
    )

    /* Unsaved data. This includes all pointers, as well as everything that shouldn't be on a save state */
    /* This data is reserved on reset and must come last in the struct */
    GB_SECTION(unsaved,
        /* ROM */
        uint8_t *rom;
        uint32_t rom_size;
        const GB_cartridge_t *cartridge_type;
        enum {
            GB_STANDARD_MBC1_WIRING,
            GB_MBC1M_WIRING,
        } mbc1_wiring;
        bool is_mbc30;

        unsigned pending_cycles;
               
        /* Various RAMs */
        uint8_t *ram;
        uint8_t *vram;
        uint8_t *mbc_ram;

        /* I/O */
        uint32_t *screen;
        uint32_t background_palettes_rgb[0x20];
        uint32_t object_palettes_rgb[0x20];
        const GB_palette_t *dmg_palette;
        GB_color_correction_mode_t color_correction_mode;
        double light_temperature;
        bool keys[4][GB_KEY_MAX];
        double accelerometer_x, accelerometer_y;
        GB_border_mode_t border_mode;
        GB_sgb_border_t borrowed_border;
        bool tried_loading_sgb_border;
        bool has_sgb_border;
        bool objects_disabled;
        bool background_disabled;
        bool joyp_accessed;
        bool illegal_inputs_allowed;
        bool no_bouncing_emulation;
        bool joypad_is_stable;

        /* Timing */
        uint64_t last_sync;
        uint64_t cycles_since_last_sync; // In 8MHz units
        GB_rtc_mode_t rtc_mode;
        uint32_t rtc_second_length;
        uint32_t clock_rate;
        uint32_t unmultiplied_clock_rate;
        uint32_t data_bus_decay;

        /* Audio */
        GB_apu_output_t apu_output;

        /* Callbacks */
        void *user_data;
        GB_log_callback_t log_callback;
        GB_input_callback_t input_callback;
        GB_input_callback_t async_input_callback;
        GB_rgb_encode_callback_t rgb_encode_callback;
        GB_vblank_callback_t vblank_callback;
        GB_infrared_callback_t infrared_callback;
        GB_camera_get_pixel_callback_t camera_get_pixel_callback;
        GB_camera_update_request_callback_t camera_update_request_callback;
        GB_rumble_callback_t rumble_callback;
        GB_serial_transfer_bit_start_callback_t serial_transfer_bit_start_callback;
        GB_serial_transfer_bit_end_callback_t serial_transfer_bit_end_callback;
        GB_update_input_hint_callback_t update_input_hint_callback;
        GB_joyp_write_callback_t joyp_write_callback;
        GB_icd_pixel_callback_t icd_pixel_callback;
        GB_icd_vreset_callback_t icd_hreset_callback;
        GB_icd_vreset_callback_t icd_vreset_callback;
        GB_read_memory_callback_t read_memory_callback;
        GB_write_memory_callback_t write_memory_callback;
        GB_boot_rom_load_callback_t boot_rom_load_callback;
        GB_print_image_callback_t printer_callback;
        GB_printer_done_callback_t printer_done_callback;
        GB_workboy_set_time_callback_t workboy_set_time_callback;
        GB_workboy_get_time_callback_t workboy_get_time_callback;
        GB_execution_callback_t execution_callback;
        GB_lcd_line_callback_t lcd_line_callback;
        GB_lcd_status_callback_t lcd_status_callback;
               
#ifndef GB_DISABLE_DEBUGGER
        /*** Debugger ***/
        volatile bool debug_stopped, debug_disable;
        bool debug_fin_command, debug_next_command;
        bool debug_active; // Cached value determining if GB_debugger_run does anything
        bool help_shown;
        uint32_t backstep_instructions;

        /* Breakpoints */
        uint16_t n_breakpoints;
        struct GB_breakpoint_s *breakpoints;
        bool has_jump_to_breakpoints, has_software_breakpoints;
        void *nontrivial_jump_state;
        bool non_trivial_jump_breakpoint_occured;

        signed debug_call_depth;
        uint16_t sp_for_call_depth[0x200]; /* Should be much more than enough */
        uint16_t addr_for_call_depth[0x200];

        /* Backtrace */
        unsigned backtrace_size;
        uint16_t backtrace_sps[0x200];
        struct {
            uint16_t bank;
            uint16_t addr;
        } backtrace_returns[0x200];

        /* Watchpoints */
        uint16_t n_watchpoints;
        struct GB_watchpoint_s *watchpoints;

        /* Symbol tables */
        GB_symbol_map_t **bank_symbols;
        size_t n_symbol_maps;
        GB_reversed_symbol_map_t reversed_symbol_map;

        /* Ticks command */
        uint64_t debugger_ticks;
        uint64_t absolute_debugger_ticks;
               
        /* Undo */
        uint8_t *undo_state;
        const char *undo_label;
               
        /* Callbacks */
        GB_debugger_reload_callback_t debugger_reload_callback;
#endif

#ifndef GB_DISABLE_REWIND
        /* Rewind */
        size_t rewind_buffer_length;
        size_t rewind_state_size;
        struct {
            uint8_t *key_state;
            uint8_t *compressed_states[GB_REWIND_FRAMES_PER_KEY];
            uint32_t instruction_count[GB_REWIND_FRAMES_PER_KEY + 1];
            unsigned pos;
        } *rewind_sequences; // lasts about 4 seconds
        size_t rewind_pos;
        bool rewind_disable_invalidation;
#endif
               
        /* SGB - saved and allocated optionally */
        GB_sgb_t *sgb;
        
        double sgb_intro_jingle_phases[7];
        double sgb_intro_sweep_phase;
        double sgb_intro_sweep_previous_sample;
               
#ifndef GB_DISABLE_CHEATS
       /* Cheats */
        bool cheat_enabled;
        size_t cheat_count;
        GB_cheat_t **cheats;
        GB_cheat_hash_t *cheat_hash[256];
#endif
#ifndef GB_DISABLE_CHEAT_SEARCH
        uint8_t *cheat_search_data;
        uint8_t *cheat_search_bitmap;
        size_t cheat_search_count;
        GB_cheat_search_data_type_t cheat_search_data_type;
#endif

        /* Misc */
        bool turbo;
        bool turbo_dont_skip;
        bool disable_rendering;
        uint8_t boot_rom[0x900];
        bool vblank_just_occured; // For slow operations involving syscalls; these should only run once per vblank
        unsigned cycles_since_run; // How many cycles have passed since the last call to GB_run(), in 8MHz units
        double clock_multiplier;
        GB_rumble_mode_t rumble_mode;
        uint32_t rumble_on_cycles;
        uint32_t rumble_off_cycles;
               
        /* Temporary state */
        bool wx_just_changed;
        bool tile_sel_glitch;
        bool disable_oam_corruption; // For safe memory reads
        bool in_dma_read;
        bool hdma_in_progress;
        bool returned_open_bus;
        uint16_t addr_for_hdma_conflict;
        bool during_div_write;
               
        /* Thread safety (debug only) */
        void *running_thread_id;
               
        GB_gbs_header_t gbs_header;
   )
};
    
#ifndef GB_INTERNAL
struct GB_gameboy_s {
    alignas(struct GB_gameboy_internal_s) uint8_t __internal[sizeof(struct GB_gameboy_internal_s)];
};
#endif


#ifndef __printflike
/* Missing from Linux headers. */
#define __printflike(fmtarg, firstvararg) \
__attribute__((__format__ (__printf__, fmtarg, firstvararg)))
#endif

/*
    There are two instance allocation styles – one where you manage your
    own instance allocation, and one where you use provided allocators.
 
    Managing allocations yourself:
    GB_gameboy_t gb;
    GB_init(&gb, model);
    ...
    GB_free(&gb);
 
    Using the provided allocators:
    GB_gameboy_t *gb = GB_init(GB_alloc(), model);
    ...
    GB_free(gb); // optional
    GB_dealloc(gb);
 
*/
GB_gameboy_t *GB_init(GB_gameboy_t *gb, GB_model_t model);
void GB_free(GB_gameboy_t *gb);
GB_gameboy_t *GB_alloc(void);
void GB_dealloc(GB_gameboy_t *gb);

// For when you want to use your own malloc implementation without having to rely on the header struct
size_t GB_allocation_size(void);
    
bool GB_is_inited(GB_gameboy_t *gb);
bool GB_is_cgb(const GB_gameboy_t *gb);
bool GB_is_cgb_in_cgb_mode(GB_gameboy_t *gb);
bool GB_is_sgb(GB_gameboy_t *gb); // Returns true if the model is SGB or SGB2
bool GB_is_hle_sgb(GB_gameboy_t *gb); // Returns true if the model is SGB or SGB2 and the SFC/SNES side is HLE'd
GB_model_t GB_get_model(GB_gameboy_t *gb);
void GB_reset(GB_gameboy_t *gb);
void GB_quick_reset(GB_gameboy_t *gb); // Similar to the cart reset line
void GB_switch_model_and_reset(GB_gameboy_t *gb, GB_model_t model);

/* Returns the time passed, in 8MHz ticks. */
unsigned GB_run(GB_gameboy_t *gb);
/* Returns the time passed since the last frame, in nanoseconds */
uint64_t GB_run_frame(GB_gameboy_t *gb);

typedef enum {
    GB_DIRECT_ACCESS_ROM,
    GB_DIRECT_ACCESS_RAM,
    GB_DIRECT_ACCESS_CART_RAM,
    GB_DIRECT_ACCESS_VRAM,
    GB_DIRECT_ACCESS_HRAM,
    GB_DIRECT_ACCESS_IO, /* Warning: Some registers can only be read/written correctly via GB_read/write_memory. */
    GB_DIRECT_ACCESS_BOOTROM,
    GB_DIRECT_ACCESS_OAM,
    GB_DIRECT_ACCESS_BGP,
    GB_DIRECT_ACCESS_OBP,
    GB_DIRECT_ACCESS_IE,
    GB_DIRECT_ACCESS_ROM0, // Identical to ROM, but returns the correct rom0 bank in the bank output argument
} GB_direct_access_t;

/* Returns a mutable pointer to various hardware memories. If that memory is banked, the current bank
   is returned at *bank, even if only a portion of the memory is banked. */
void *GB_get_direct_access(GB_gameboy_t *gb, GB_direct_access_t access, size_t *size, uint16_t *bank);
GB_registers_t *GB_get_registers(GB_gameboy_t *gb);

void *GB_get_user_data(GB_gameboy_t *gb);
void GB_set_user_data(GB_gameboy_t *gb, void *data);

int GB_load_boot_rom(GB_gameboy_t *gb, const char *path);
void GB_load_boot_rom_from_buffer(GB_gameboy_t *gb, const unsigned char *buffer, size_t size);
int GB_load_rom(GB_gameboy_t *gb, const char *path);
void GB_load_rom_from_buffer(GB_gameboy_t *gb, const uint8_t *buffer, size_t size);
int GB_load_isx(GB_gameboy_t *gb, const char *path);
int GB_load_gbs_from_buffer(GB_gameboy_t *gb, const uint8_t *buffer, size_t size, GB_gbs_info_t *info);
int GB_load_gbs(GB_gameboy_t *gb, const char *path, GB_gbs_info_t *info);
void GB_gbs_switch_track(GB_gameboy_t *gb, uint8_t track);

int GB_save_battery_size(GB_gameboy_t *gb);
int GB_save_battery_to_buffer(GB_gameboy_t *gb, uint8_t *buffer, size_t size);
int GB_save_battery(GB_gameboy_t *gb, const char *path);

void GB_load_battery_from_buffer(GB_gameboy_t *gb, const uint8_t *buffer, size_t size);
int GB_load_battery(GB_gameboy_t *gb, const char *path);

void GB_set_turbo_mode(GB_gameboy_t *gb, bool on, bool no_frame_skip);
void GB_set_rendering_disabled(GB_gameboy_t *gb, bool disabled);
    
void GB_log(GB_gameboy_t *gb, const char *fmt, ...) __printflike(2, 3);
void GB_attributed_log(GB_gameboy_t *gb, GB_log_attributes_t attributes, const char *fmt, ...) __printflike(3, 4);

uint32_t *GB_get_pixels_output(GB_gameboy_t *gb);
void GB_set_border_mode(GB_gameboy_t *gb, GB_border_mode_t border_mode);
    
void GB_set_infrared_input(GB_gameboy_t *gb, bool state);
    
void GB_set_log_callback(GB_gameboy_t *gb, GB_log_callback_t callback);
void GB_set_input_callback(GB_gameboy_t *gb, GB_input_callback_t callback);
void GB_set_async_input_callback(GB_gameboy_t *gb, GB_input_callback_t callback);
void GB_set_infrared_callback(GB_gameboy_t *gb, GB_infrared_callback_t callback);
void GB_set_rumble_callback(GB_gameboy_t *gb, GB_rumble_callback_t callback);
/* Called when a new boot ROM is needed. The callback should call GB_load_boot_rom or GB_load_boot_rom_from_buffer */
void GB_set_boot_rom_load_callback(GB_gameboy_t *gb, GB_boot_rom_load_callback_t callback);
    
void GB_set_execution_callback(GB_gameboy_t *gb, GB_execution_callback_t callback);
void GB_set_lcd_line_callback(GB_gameboy_t *gb, GB_lcd_line_callback_t callback);
void GB_set_lcd_status_callback(GB_gameboy_t *gb, GB_lcd_status_callback_t callback);

/* These APIs are used when using internal clock */
void GB_set_serial_transfer_bit_start_callback(GB_gameboy_t *gb, GB_serial_transfer_bit_start_callback_t callback);
void GB_set_serial_transfer_bit_end_callback(GB_gameboy_t *gb, GB_serial_transfer_bit_end_callback_t callback);

/* These APIs are used when using external clock */
bool GB_serial_get_data_bit(GB_gameboy_t *gb);
void GB_serial_set_data_bit(GB_gameboy_t *gb, bool data);
    
void GB_disconnect_serial(GB_gameboy_t *gb);
GB_accessory_t GB_get_built_in_accessory(GB_gameboy_t *gb);
    
/* For cartridges with an alarm clock */
bool GB_rom_supports_alarms(GB_gameboy_t *gb);
unsigned GB_time_to_alarm(GB_gameboy_t *gb); // 0 if no alarm

/* For cartridges motion controls */
bool GB_has_accelerometer(GB_gameboy_t *gb);
// In units of g (gravity's acceleration).
// Values within ±4 recommended
void GB_set_accelerometer_values(GB_gameboy_t *gb, double x, double y);
    
// Time it takes for a value in the data bus to decay to FF, in 8MHz units. (0 to never decay, like e.g. an EverDrive)
void GB_set_open_bus_decay_time(GB_gameboy_t *gb, uint32_t decay);
    
/* For integration with SFC/SNES emulators */
void GB_set_joyp_write_callback(GB_gameboy_t *gb, GB_joyp_write_callback_t callback);
void GB_set_icd_pixel_callback(GB_gameboy_t *gb, GB_icd_pixel_callback_t callback);
void GB_set_icd_hreset_callback(GB_gameboy_t *gb, GB_icd_hreset_callback_t callback);
void GB_set_icd_vreset_callback(GB_gameboy_t *gb, GB_icd_vreset_callback_t callback);
    
uint32_t GB_get_clock_rate(GB_gameboy_t *gb);
uint32_t GB_get_unmultiplied_clock_rate(GB_gameboy_t *gb);
void GB_set_clock_multiplier(GB_gameboy_t *gb, double multiplier);

/* Handy ROM info APIs */
// `title` must be at least 17 bytes in size
void GB_get_rom_title(GB_gameboy_t *gb, char *title);
uint32_t GB_get_rom_crc32(GB_gameboy_t *gb);

#ifdef GB_INTERNAL
internal void GB_borrow_sgb_border(GB_gameboy_t *gb);
internal void GB_update_clock_rate(GB_gameboy_t *gb);
#endif
    
#ifdef GB_INTERNAL

#ifndef NDEBUG
#define GB_CONTEXT_SAFETY
#endif
    
#ifdef GB_CONTEXT_SAFETY
#include <assert.h>
internal void *GB_get_thread_id(void);
internal void GB_set_running_thread(GB_gameboy_t *gb);
internal void GB_clear_running_thread(GB_gameboy_t *gb);
#define GB_ASSERT_NOT_RUNNING(gb) if (gb->running_thread_id) {GB_log(gb, "Function %s must not be called in a running context.\n", __FUNCTION__); assert(!gb->running_thread_id);}
#define GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb) if (gb->running_thread_id && gb->running_thread_id != GB_get_thread_id()) {GB_log(gb, "Function %s must not be called while running in another thread.\n", __FUNCTION__); assert(!gb->running_thread_id || gb->running_thread_id == GB_get_thread_id());}

#else
#define GB_ASSERT_NOT_RUNNING(gb)
#define GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
#define GB_set_running_thread(gb)
#define GB_clear_running_thread(gb)
#endif
    
#endif

#ifdef __cplusplus
}
#endif

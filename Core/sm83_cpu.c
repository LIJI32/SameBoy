#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include "gb.h"


typedef void opcode_t(GB_gameboy_t *gb, uint8_t opcode);

typedef enum {
    /* Default behavior. If the CPU writes while another component reads, it reads the old value */
    GB_CONFLICT_READ_OLD,
    /* If the CPU writes while another component reads, it reads the new value */
    GB_CONFLICT_READ_NEW,
    /* If the CPU and another component write at the same time, the CPU's value "wins" */
    GB_CONFLICT_WRITE_CPU,
    /* Register specific values */
    GB_CONFLICT_STAT_CGB,
    GB_CONFLICT_STAT_DMG,
    GB_CONFLICT_PALETTE_DMG,
    GB_CONFLICT_PALETTE_CGB,
    GB_CONFLICT_DMG_LCDC,
    GB_CONFLICT_SGB_LCDC,
    GB_CONFLICT_WX_DMG,
    GB_CONFLICT_LCDC_CGB,
    GB_CONFLICT_LCDC_CGB_DOUBLE,
    GB_CONFLICT_STAT_CGB_DOUBLE,
    GB_CONFLICT_NR10_CGB_DOUBLE,
    GB_CONFLICT_SCX_CGB_DOUBLE,
} conflict_t;

static const conflict_t cgb_conflict_map[0x80] = {
    [GB_IO_LCDC] = GB_CONFLICT_LCDC_CGB,
    [GB_IO_IF] = GB_CONFLICT_WRITE_CPU,
    [GB_IO_LYC] = GB_CONFLICT_WRITE_CPU,
    [GB_IO_WY] = GB_CONFLICT_READ_OLD,
    [GB_IO_STAT] = GB_CONFLICT_STAT_CGB,
    [GB_IO_BGP] = GB_CONFLICT_PALETTE_CGB,
    [GB_IO_OBP0] = GB_CONFLICT_PALETTE_CGB,
    [GB_IO_OBP1] = GB_CONFLICT_PALETTE_CGB,
    [GB_IO_SCX] = GB_CONFLICT_READ_OLD,
    [GB_IO_WX] = GB_CONFLICT_WRITE_CPU,
};

static const conflict_t cgb_double_conflict_map[0x80] = {
    [GB_IO_LCDC] = GB_CONFLICT_LCDC_CGB_DOUBLE,
    [GB_IO_IF] = GB_CONFLICT_WRITE_CPU,
    [GB_IO_LYC] = GB_CONFLICT_READ_OLD,
    [GB_IO_WY] = GB_CONFLICT_READ_OLD,
    [GB_IO_STAT] = GB_CONFLICT_STAT_CGB_DOUBLE,
    [GB_IO_NR10] = GB_CONFLICT_NR10_CGB_DOUBLE,
    [GB_IO_SCX] = GB_CONFLICT_SCX_CGB_DOUBLE,
    [GB_IO_WX] = GB_CONFLICT_READ_OLD,
};

/* Todo: verify on an MGB */
static const conflict_t dmg_conflict_map[0x80] = {
    [GB_IO_IF] = GB_CONFLICT_WRITE_CPU,
    [GB_IO_LYC] = GB_CONFLICT_READ_OLD,
    [GB_IO_LCDC] = GB_CONFLICT_DMG_LCDC,
    [GB_IO_SCY] = GB_CONFLICT_READ_NEW,
    [GB_IO_STAT] = GB_CONFLICT_STAT_DMG,
 
    [GB_IO_BGP] = GB_CONFLICT_PALETTE_DMG,
    [GB_IO_OBP0] = GB_CONFLICT_PALETTE_DMG,
    [GB_IO_OBP1] = GB_CONFLICT_PALETTE_DMG,
    [GB_IO_WY] = GB_CONFLICT_READ_OLD,
    [GB_IO_WX] = GB_CONFLICT_WX_DMG,
    
    /* Todo: these were not verified at all */
    [GB_IO_SCX] = GB_CONFLICT_READ_NEW,
};

/* Todo: Verify on an SGB1 */
static const conflict_t sgb_conflict_map[0x80] = {
    [GB_IO_IF] = GB_CONFLICT_WRITE_CPU,
    [GB_IO_LYC] = GB_CONFLICT_READ_OLD,
    [GB_IO_LCDC] = GB_CONFLICT_SGB_LCDC,
    [GB_IO_SCY] = GB_CONFLICT_READ_NEW,
    [GB_IO_STAT] = GB_CONFLICT_STAT_DMG,
    
    [GB_IO_BGP] = GB_CONFLICT_READ_NEW,
    [GB_IO_OBP0] = GB_CONFLICT_READ_NEW,
    [GB_IO_OBP1] = GB_CONFLICT_READ_NEW,
    [GB_IO_WY] = GB_CONFLICT_READ_OLD,
    [GB_IO_WX] = GB_CONFLICT_WX_DMG,
    
    /* Todo: these were not verified at all */
    [GB_IO_SCX] = GB_CONFLICT_READ_NEW,
};

static uint8_t cycle_read(GB_gameboy_t *gb, uint16_t addr)
{
    if (gb->pending_cycles) {
        GB_advance_cycles(gb, gb->pending_cycles);
    }
    gb->address_bus = addr;
    uint8_t ret = GB_read_memory(gb, addr);
    gb->pending_cycles = 4;
    return ret;
}

/* A special case for IF during ISR, returns the old value of IF. */
/* TODO: Verify the timing, it might be wrong in cases where, in the same M cycle, IF
   is both read be the CPU, modified by the ISR, and modified by an actual interrupt.
   If this timing proves incorrect, the ISR emulation must be updated so IF reads are
   timed correctly. */
/* TODO: Does this affect the address bus? Verify. */
static uint8_t cycle_write_if(GB_gameboy_t *gb, uint8_t value)
{
    assert(gb->pending_cycles);
    GB_advance_cycles(gb, gb->pending_cycles);
    gb->address_bus = 0xFF00 + GB_IO_IF;
    uint8_t old = (gb->io_registers[GB_IO_IF]) & 0x1F;
    GB_write_memory(gb, 0xFF00 + GB_IO_IF, value);
    gb->pending_cycles = 4;
    return old;
}

static void cycle_write(GB_gameboy_t *gb, uint16_t addr, uint8_t value)
{
    assert(gb->pending_cycles);
    conflict_t conflict = GB_CONFLICT_READ_OLD;
    if ((addr & 0xFF80) == 0xFF00) {
        const conflict_t *map = NULL;
        if (GB_is_cgb(gb)) {
            map = gb->cgb_double_speed? cgb_double_conflict_map : cgb_conflict_map;
        }
        else if (GB_is_sgb(gb)) {
            map = sgb_conflict_map;
        }
        else {
            map = dmg_conflict_map;
        }
        conflict = map[addr & 0x7F];
    }
    switch (conflict) {
        case GB_CONFLICT_READ_OLD:
            GB_advance_cycles(gb, gb->pending_cycles);
            GB_write_memory(gb, addr, value);
            gb->pending_cycles = 4;
            break;
            
        case GB_CONFLICT_READ_NEW:
            GB_advance_cycles(gb, gb->pending_cycles - 1);
            GB_write_memory(gb, addr, value);
            gb->pending_cycles = 5;
            break;
            
        case GB_CONFLICT_WRITE_CPU:
            GB_advance_cycles(gb, gb->pending_cycles + 1);
            GB_write_memory(gb, addr, value);
            gb->pending_cycles = 3;
            break;
        
        /* The DMG STAT-write bug is basically the STAT register being read as FF for a single T-cycle */
        case GB_CONFLICT_STAT_DMG:
            GB_advance_cycles(gb, gb->pending_cycles);
            GB_display_sync(gb);
            /* State 7 is the edge between HBlank and OAM mode, and it behaves a bit weird.
             The OAM interrupt seems to be blocked by HBlank interrupts in that case, despite
             the timing not making much sense for that.
             This is a hack to simulate this effect */
            if (gb->display_state == 7 && (gb->io_registers[GB_IO_STAT] & 0x28) == 0x08) {
                GB_write_memory(gb, addr, ~0x20);
            }
            else {
                GB_write_memory(gb, addr, 0xFF);
            }
            GB_advance_cycles(gb, 1);
            GB_write_memory(gb, addr, value);
            gb->pending_cycles = 3;
            break;
        
        case GB_CONFLICT_STAT_CGB: {
            /* Todo: Verify this with SCX adjustments */
            /* The LYC bit behaves differently */
            uint8_t old_value = gb->io_registers[GB_IO_STAT];
            GB_advance_cycles(gb, gb->pending_cycles);
            GB_write_memory(gb, addr, (old_value & 0x40) | (value & ~0x40));
            GB_advance_cycles(gb, 1);
            GB_write_memory(gb, addr, value);
            gb->pending_cycles = 3;
            break;
        }
            
        case GB_CONFLICT_STAT_CGB_DOUBLE: {
            uint8_t old_value = gb->io_registers[GB_IO_STAT];
            GB_advance_cycles(gb, gb->pending_cycles);
            GB_write_memory(gb, addr, (value & ~8) | (old_value & 8));
            GB_advance_cycles(gb, 1);
            GB_write_memory(gb, addr, value);
            gb->pending_cycles = 3;
            break;
        }
        
        /* There is some "time travel" going on with these two values, as it appears
           that there's some off-by-1-T-cycle timing issue in the PPU implementation.
         
           This is should be accurate for every measureable scenario, though. */
            
        case GB_CONFLICT_PALETTE_DMG: {
            GB_advance_cycles(gb, gb->pending_cycles - 2);
            uint8_t old_value = GB_read_memory(gb, addr);
            GB_write_memory(gb, addr, value | old_value);
            GB_advance_cycles(gb, 1);
            GB_write_memory(gb, addr, value);
            gb->pending_cycles = 5;
            break;
        }
            
        case GB_CONFLICT_PALETTE_CGB: {
            if (gb->model >= GB_MODEL_CGB_D) {
                GB_advance_cycles(gb, gb->pending_cycles - 2);
                GB_write_memory(gb, addr, value);
                gb->pending_cycles = 6;
            }
            else {
                GB_advance_cycles(gb, gb->pending_cycles - 1);
                GB_write_memory(gb, addr, value);
                gb->pending_cycles = 5;
            }
            break;
        }
            
        case GB_CONFLICT_DMG_LCDC: {
            /* Similar to the palette registers, these interact directly with the LCD, so they appear to be affected by
               it. Both my DMG (B, blob) and Game Boy Light behave this way though.
             
               Additionally, LCDC.1 is very nasty because it is read both by the FIFO when popping pixels, and the
               object-fetching state machine, and both behave differently when it comes to access conflicts. Hacks ahead.
             */
            
            uint8_t old_value = GB_read_memory(gb, addr);
            GB_advance_cycles(gb, gb->pending_cycles - 2);
            GB_display_sync(gb);
            if (gb->model != GB_MODEL_MGB && gb->position_in_line == 0 && (old_value & GB_LCDC_OBJ_EN) && !(value & GB_LCDC_OBJ_EN)) {
                old_value &= ~GB_LCDC_OBJ_EN;
            }
            
            GB_write_memory(gb, addr, old_value | (value & GB_LCDC_BG_EN));
            GB_advance_cycles(gb, 1);
            GB_write_memory(gb, addr, value);
            
            if ((old_value & GB_LCDC_WIN_ENABLE) && !(value & GB_LCDC_WIN_ENABLE) && gb->window_is_being_fetched) {
                gb->disable_window_pixel_insertion_glitch = true;
            }
            gb->pending_cycles = 5;
            break;
        }
            
        case GB_CONFLICT_SGB_LCDC: {
            /* Simplified version of the above */
            
            uint8_t old_value = GB_read_memory(gb, addr);
            GB_advance_cycles(gb, gb->pending_cycles - 2);
            /* Hack to force aborting object fetch */
            GB_write_memory(gb, addr, value);
            GB_write_memory(gb, addr, old_value);
            GB_advance_cycles(gb, 1);
            GB_write_memory(gb, addr, value);
            gb->pending_cycles = 5;
            break;
        }
            
        case GB_CONFLICT_WX_DMG:
            GB_advance_cycles(gb, gb->pending_cycles);
            GB_write_memory(gb, addr, value);
            gb->wx_just_changed = true;
            GB_advance_cycles(gb, 1);
            gb->wx_just_changed = false;
            gb->pending_cycles = 3;
            break;
            
        case GB_CONFLICT_LCDC_CGB: {
            uint8_t old = gb->io_registers[GB_IO_LCDC];
            if ((~value & old) & GB_LCDC_TILE_SEL) {
                GB_advance_cycles(gb, gb->pending_cycles);
                GB_write_memory(gb, addr, value);
                gb->tile_sel_glitch = true;
                GB_advance_cycles(gb, 1);
                gb->tile_sel_glitch = false;
                gb->pending_cycles = 3;
            }
            else {
                GB_advance_cycles(gb, gb->pending_cycles);
                GB_write_memory(gb, addr, value);
                gb->pending_cycles = 4;
            }
            break;
        }
        case GB_CONFLICT_LCDC_CGB_DOUBLE: {
            uint8_t old = gb->io_registers[GB_IO_LCDC];
            // TODO: Verify for CGB ≤ C for BG_EN and OBJ_EN.
            GB_advance_cycles(gb, gb->pending_cycles - 2);
            GB_write_memory(gb, addr, (value & ~(GB_LCDC_BG_EN | GB_LCDC_ENABLE)) | (old & (GB_LCDC_BG_EN | GB_LCDC_ENABLE)));
            // TODO: This condition is different from single speed mode. Why? What about odd modes?
            gb->tile_sel_glitch = ((value ^ old) & GB_LCDC_TILE_SEL);
            GB_advance_cycles(gb, 2);
            gb->tile_sel_glitch = false;
            GB_write_memory(gb, addr, value);
            gb->pending_cycles = 4;
            break;
        }
            
        case GB_CONFLICT_SCX_CGB_DOUBLE:
            GB_advance_cycles(gb, gb->pending_cycles - 2);
            GB_write_memory(gb, addr, value);
            gb->pending_cycles = 6;
            break;
        
        case GB_CONFLICT_NR10_CGB_DOUBLE: {
            GB_advance_cycles(gb, gb->pending_cycles - 1);
            gb->apu_output.square_sweep_disable_stepping = gb->model <= GB_MODEL_CGB_C && (value & 7) == 0;
            GB_advance_cycles(gb, 1);
            gb->apu_output.square_sweep_disable_stepping = false;
            GB_write_memory(gb, addr, value);
            gb->pending_cycles = 4;
            break;
        }
    }
    gb->address_bus = addr;
}

static void cycle_no_access(GB_gameboy_t *gb)
{
    gb->pending_cycles += 4;
}

static void cycle_oam_bug(GB_gameboy_t *gb, uint8_t register_id)
{
    if (gb->pending_cycles) {
        GB_advance_cycles(gb, gb->pending_cycles);
    }
    gb->address_bus = gb->registers[register_id];
    GB_trigger_oam_bug(gb, gb->registers[register_id]); /* Todo: test T-cycle timing */
    gb->pending_cycles = 4;
}

static void flush_pending_cycles(GB_gameboy_t *gb)
{
    if (gb->pending_cycles) {
        GB_advance_cycles(gb, gb->pending_cycles);
    }
    gb->pending_cycles = 0;
}

/* Todo: test if multi-byte opcodes trigger the OAM bug correctly */

static void ill(GB_gameboy_t *gb, uint8_t opcode)
{
    GB_log(gb, "Illegal Opcode. Halting.\n");
    gb->interrupt_enable = 0;
    gb->halted = true;
}

static void nop(GB_gameboy_t *gb, uint8_t opcode)
{
}

static void enter_stop_mode(GB_gameboy_t *gb)
{
    GB_write_memory(gb, 0xFF00 + GB_IO_DIV, 0);
    if (!gb->ime) { // TODO: I don't trust this if,
        gb->div_cycles = -4; // Emulate the CPU-side DIV-reset signal being held
    }
    gb->stopped = true;
    gb->allow_hdma_on_wake = (gb->io_registers[GB_IO_STAT] & 3);
    gb->oam_ppu_blocked = !gb->oam_read_blocked;
    gb->vram_ppu_blocked = !gb->vram_read_blocked;
    gb->cgb_palettes_ppu_blocked = !gb->cgb_palettes_blocked;
}

static void leave_stop_mode(GB_gameboy_t *gb)
{
    gb->stopped = false;
    if (gb->hdma_on_hblank && (gb->io_registers[GB_IO_STAT] & 3) == 0 && gb->allow_hdma_on_wake) {
        gb->hdma_on = true;
    }
    // TODO: verify this
    gb->dma_cycles = 4;
    GB_dma_run(gb);
    gb->oam_ppu_blocked = false;
    gb->vram_ppu_blocked = false;
    gb->cgb_palettes_ppu_blocked = false;
}

/* TODO: Speed switch timing needs far more tests. Double to single is wrong to avoid odd mode. */
static void stop(GB_gameboy_t *gb, uint8_t opcode)
{
    flush_pending_cycles(gb);
    GB_read_memory(gb, gb->pc); // Timing is completely unverified, and only affects STOP triggering the OAM bug
    if ((gb->io_registers[GB_IO_JOYP] & 0x30) != 0x30) {
        gb->joyp_accessed = true;
    }
    bool exit_by_joyp = ((gb->io_registers[GB_IO_JOYP] & 0xF) != 0xF);
    bool speed_switch = (gb->io_registers[GB_IO_KEY1] & 0x1) && !exit_by_joyp;
    bool immediate_exit = speed_switch || exit_by_joyp;
    bool interrupt_pending = (gb->interrupt_enable & gb->io_registers[GB_IO_IF] & 0x1F);
    // When entering with IF&IE, the 2nd byte of STOP is actually executed
    if (!exit_by_joyp) {
        if (!immediate_exit) {
            GB_dma_run(gb);
        }
        enter_stop_mode(gb);
    }
    
    if (!interrupt_pending) {
        cycle_read(gb, gb->pc++);
    }
    
    /* Todo: speed switching takes 2 extra T-cycles (so 2 PPU ticks in single->double and 1 PPU tick in double->single) */
    if (speed_switch) {
        flush_pending_cycles(gb);
        
        if (gb->io_registers[GB_IO_LCDC] & GB_LCDC_ENABLE && gb->cgb_double_speed) {
            GB_log(gb, "ROM triggered a PPU odd mode, which is currently not supported. Reverting to even-mode.\n");
            if (gb->double_speed_alignment & 7) {
                gb->speed_switch_freeze = 2;
            }
        }
        if (gb->apu.global_enable && gb->cgb_double_speed) {
            GB_log(gb, "ROM triggered an APU odd mode, which is currently not tested.\n");
        }
        if (gb->cartridge_type->mbc_type == GB_CAMERA && (gb->camera_registers[GB_CAMERA_SHOOT_AND_1D_FLAGS] & 1) && !gb->cgb_double_speed) {
            GB_log(gb, "ROM entered double speed mode with a camera cartridge, this could damage a real cartridge's camera.\n");
        }
        
        if (gb->cgb_double_speed) {
            gb->cgb_double_speed = false;
        }
        else {
            gb->speed_switch_countdown = 6;
            gb->speed_switch_freeze = 1;
        }
        
        if (!interrupt_pending) {
            gb->speed_switch_halt_countdown = 0x20008;
            gb->speed_switch_freeze = 5;
        }
        
        gb->io_registers[GB_IO_KEY1] = 0;
    }
    
    if (immediate_exit) {
        leave_stop_mode(gb);
        if (!interrupt_pending) {
            GB_dma_run(gb);
            gb->halted = true;
            gb->just_halted = true;
            gb->allow_hdma_on_wake = (gb->io_registers[GB_IO_STAT] & 3);
        }
        else {
            gb->speed_switch_halt_countdown = 0;
        }
    }
}

/* Operand naming conventions for functions:
   r = 8-bit register
   lr = low 8-bit register
   hr = high 8-bit register
   rr = 16-bit register
   d8 = 8-bit imm
   d16 = 16-bit imm
   d.. = [..]
   cc = condition code (z, nz, c, nc)
   */

static void ld_rr_d16(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t register_id;
    uint16_t value;
    register_id = (opcode >> 4) + 1;
    value = cycle_read(gb, gb->pc++);
    value |= cycle_read(gb, gb->pc++) << 8;
    gb->registers[register_id] = value;
}

static void ld_drr_a(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t register_id;
    register_id = (opcode >> 4) + 1;
    cycle_write(gb, gb->registers[register_id], gb->af >> 8);
}

static void inc_rr(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t register_id = (opcode >> 4) + 1;
    cycle_oam_bug(gb, register_id);
    gb->registers[register_id]++;
}

static void inc_hr(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t register_id;
    register_id = ((opcode >> 4) + 1) & 0x03;
    gb->registers[register_id] += 0x100;
    gb->af &= ~(GB_SUBTRACT_FLAG | GB_ZERO_FLAG | GB_HALF_CARRY_FLAG);

    if ((gb->registers[register_id] & 0x0F00) == 0) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }

    if ((gb->registers[register_id] & 0xFF00) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}
static void dec_hr(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t register_id;
    register_id = ((opcode >> 4) + 1) & 0x03;
    gb->registers[register_id] -= 0x100;
    gb->af &= ~(GB_ZERO_FLAG | GB_HALF_CARRY_FLAG);
    gb->af |= GB_SUBTRACT_FLAG;

    if ((gb->registers[register_id] & 0x0F00) == 0xF00) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }

    if ((gb->registers[register_id] & 0xFF00) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void ld_hr_d8(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t register_id;
    register_id = ((opcode >> 4) + 1) & 0x03;
    gb->registers[register_id] &= 0xFF;
    gb->registers[register_id] |= cycle_read(gb, gb->pc++) << 8;
}

static void rlca(GB_gameboy_t *gb, uint8_t opcode)
{
    bool carry = (gb->af & 0x8000) != 0;

    gb->af = (gb->af & 0xFF00) << 1;
    if (carry) {
        gb->af |= GB_CARRY_FLAG | 0x0100;
    }
}

static void rla(GB_gameboy_t *gb, uint8_t opcode)
{
    bool bit7 = (gb->af & 0x8000) != 0;
    bool carry = (gb->af & GB_CARRY_FLAG) != 0;

    gb->af = (gb->af & 0xFF00) << 1;
    if (carry) {
        gb->af |= 0x0100;
    }
    if (bit7) {
        gb->af |= GB_CARRY_FLAG;
    }
}

static void ld_da16_sp(GB_gameboy_t *gb, uint8_t opcode)
{
    /* Todo: Verify order is correct */
    uint16_t addr;
    addr = cycle_read(gb, gb->pc++);
    addr |= cycle_read(gb, gb->pc++) << 8;
    cycle_write(gb, addr, gb->sp & 0xFF);
    cycle_write(gb, addr + 1, gb->sp >> 8);
}

static void add_hl_rr(GB_gameboy_t *gb, uint8_t opcode)
{
    uint16_t hl = gb->hl;
    uint16_t rr;
    uint8_t register_id;
    cycle_no_access(gb);
    register_id = (opcode >> 4) + 1;
    rr = gb->registers[register_id];
    gb->hl = hl + rr;
    gb->af &= ~(GB_SUBTRACT_FLAG | GB_CARRY_FLAG | GB_HALF_CARRY_FLAG);

    /* The meaning of the Half Carry flag is really hard to track -_- */
    if (((hl & 0xFFF) + (rr & 0xFFF)) & 0x1000) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }

    if ( ((unsigned) hl + (unsigned) rr) & 0x10000) {
        gb->af |= GB_CARRY_FLAG;
    }
}

static void ld_a_drr(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t register_id;
    register_id = (opcode >> 4) + 1;
    gb->af &= 0xFF;
    gb->af |= cycle_read(gb, gb->registers[register_id]) << 8;
}

static void dec_rr(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t register_id = (opcode >> 4) + 1;
    cycle_oam_bug(gb, register_id);
    gb->registers[register_id]--;
}

static void inc_lr(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t register_id;
    uint8_t value;
    register_id = (opcode >> 4) + 1;

    value = (gb->registers[register_id] & 0xFF) + 1;
    gb->registers[register_id] = (gb->registers[register_id] & 0xFF00) | value;

    gb->af &= ~(GB_SUBTRACT_FLAG | GB_ZERO_FLAG | GB_HALF_CARRY_FLAG);

    if ((gb->registers[register_id] & 0x0F) == 0) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }

    if ((gb->registers[register_id] & 0xFF) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}
static void dec_lr(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t register_id;
    uint8_t value;
    register_id = (opcode >> 4) + 1;

    value = (gb->registers[register_id] & 0xFF) - 1;
    gb->registers[register_id] = (gb->registers[register_id] & 0xFF00) | value;

    gb->af &= ~(GB_ZERO_FLAG | GB_HALF_CARRY_FLAG);
    gb->af |= GB_SUBTRACT_FLAG;

    if ((gb->registers[register_id] & 0x0F) == 0xF) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }

    if ((gb->registers[register_id] & 0xFF) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void ld_lr_d8(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t register_id;
    register_id = (opcode >> 4) + 1;
    gb->registers[register_id] &= 0xFF00;
    gb->registers[register_id] |= cycle_read(gb, gb->pc++);
}

static void rrca(GB_gameboy_t *gb, uint8_t opcode)
{
    bool carry = (gb->af & 0x100) != 0;

    gb->af = (gb->af >> 1) & 0xFF00;
    if (carry) {
        gb->af |= GB_CARRY_FLAG | 0x8000;
    }
}

static void rra(GB_gameboy_t *gb, uint8_t opcode)
{
    bool bit1 = (gb->af & 0x0100) != 0;
    bool carry = (gb->af & GB_CARRY_FLAG) != 0;

    gb->af = (gb->af >> 1) & 0xFF00;
    if (carry) {
        gb->af |= 0x8000;
    }
    if (bit1) {
        gb->af |= GB_CARRY_FLAG;
    }
}

static void jr_r8(GB_gameboy_t *gb, uint8_t opcode)
{
    int8_t offset = (int8_t)cycle_read(gb, gb->pc++);
    cycle_oam_bug(gb, GB_REGISTER_PC);
    gb->pc += offset;
}

static bool condition_code(GB_gameboy_t *gb, uint8_t opcode)
{
    switch ((opcode >> 3) & 0x3) {
        case 0:
            return !(gb->af & GB_ZERO_FLAG);
        case 1:
            return (gb->af & GB_ZERO_FLAG);
        case 2:
            return !(gb->af & GB_CARRY_FLAG);
        case 3:
            return (gb->af & GB_CARRY_FLAG);
        nodefault;
    }

    return false;
}

static void jr_cc_r8(GB_gameboy_t *gb, uint8_t opcode)
{
    int8_t offset = cycle_read(gb, gb->pc++);
    if (condition_code(gb, opcode)) {
        gb->pc += offset;
        cycle_oam_bug(gb, GB_REGISTER_PC);
    }
}

static void daa(GB_gameboy_t *gb, uint8_t opcode)
{
    int16_t result = gb->af >> 8;

    gb->af &= ~(0xFF00 | GB_ZERO_FLAG);

    if (gb->af & GB_SUBTRACT_FLAG) {
        if (gb->af & GB_HALF_CARRY_FLAG) {
            result = (result - 0x06) & 0xFF;
        }

        if (gb->af & GB_CARRY_FLAG) {
            result -= 0x60;
        }
    }
    else {
        if ((gb->af & GB_HALF_CARRY_FLAG) || (result & 0x0F) > 0x09) {
            result += 0x06;
        }

        if ((gb->af & GB_CARRY_FLAG) || result > 0x9F) {
            result += 0x60;
        }
    }

    if ((result & 0xFF) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }

    if ((result & 0x100) == 0x100) {
        gb->af |= GB_CARRY_FLAG;
    }

    gb->af &= ~GB_HALF_CARRY_FLAG;
    gb->af |= result << 8;
}

static void cpl(GB_gameboy_t *gb, uint8_t opcode)
{
    gb->af ^= 0xFF00;
    gb->af |= GB_HALF_CARRY_FLAG | GB_SUBTRACT_FLAG;
}

static void scf(GB_gameboy_t *gb, uint8_t opcode)
{
    gb->af |= GB_CARRY_FLAG;
    gb->af &= ~(GB_HALF_CARRY_FLAG | GB_SUBTRACT_FLAG);
}

static void ccf(GB_gameboy_t *gb, uint8_t opcode)
{
    gb->af ^= GB_CARRY_FLAG;
    gb->af &= ~(GB_HALF_CARRY_FLAG | GB_SUBTRACT_FLAG);
}

static void ld_dhli_a(GB_gameboy_t *gb, uint8_t opcode)
{
    cycle_write(gb, gb->hl++, gb->af >> 8);
}

static void ld_dhld_a(GB_gameboy_t *gb, uint8_t opcode)
{
    cycle_write(gb, gb->hl--, gb->af >> 8);
}

static void ld_a_dhli(GB_gameboy_t *gb, uint8_t opcode)
{
    gb->af &= 0xFF;
    gb->af |= cycle_read(gb, gb->hl++) << 8;
}

static void ld_a_dhld(GB_gameboy_t *gb, uint8_t opcode)
{
    gb->af &= 0xFF;
    gb->af |= cycle_read(gb, gb->hl--) << 8;
}

static void inc_dhl(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value;
    value = cycle_read(gb, gb->hl) + 1;
    cycle_write(gb, gb->hl, value);

    gb->af &= ~(GB_SUBTRACT_FLAG | GB_ZERO_FLAG | GB_HALF_CARRY_FLAG);
    if ((value & 0x0F) == 0) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }

    if ((value & 0xFF) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void dec_dhl(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value;
    value = cycle_read(gb, gb->hl) - 1;
    cycle_write(gb, gb->hl, value);

    gb->af &= ~( GB_ZERO_FLAG | GB_HALF_CARRY_FLAG);
    gb->af |= GB_SUBTRACT_FLAG;
    if ((value & 0x0F) == 0x0F) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }

    if ((value & 0xFF) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void ld_dhl_d8(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t data = cycle_read(gb, gb->pc++);
    cycle_write(gb, gb->hl, data);
}

static uint8_t get_src_value(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t src_register_id;
    uint8_t src_low;
    src_register_id = ((opcode >> 1) + 1) & 3;
    src_low = opcode & 1;
    if (src_register_id == GB_REGISTER_AF) {
        if (src_low) {
            return gb->af >> 8;
        }
        return cycle_read(gb, gb->hl);
    }
    if (src_low) {
        return gb->registers[src_register_id] & 0xFF;
    }
    return gb->registers[src_register_id] >> 8;
}

static void set_src_value(GB_gameboy_t *gb, uint8_t opcode, uint8_t value)
{
    uint8_t src_register_id;
    uint8_t src_low;
    src_register_id = ((opcode >> 1) + 1) & 3;
    src_low = opcode & 1;

    if (src_register_id == GB_REGISTER_AF) {
        if (src_low) {
            gb->af &= 0xFF;
            gb->af |= value << 8;
        }
        else {
            cycle_write(gb, gb->hl, value);
        }
    }
    else {
        if (src_low) {
            gb->registers[src_register_id] &= 0xFF00;
            gb->registers[src_register_id] |= value;
        }
        else {
            gb->registers[src_register_id] &= 0xFF;
            gb->registers[src_register_id] |= value << 8;
        }
    }
}

/* The LD r,r instruction is extremely common and extremely simple. Decoding this opcode at runtime is a significent
   performance hit, so we generate functions for every ld x,y couple (including [hl]) at compile time using macros. */

/* Todo: It's probably wise to do the same to all opcodes. */

#define LD_X_Y(x, y) \
static void ld_##x##_##y(GB_gameboy_t *gb, uint8_t opcode) \
{ \
    gb->x = gb->y;\
}

#define LD_X_DHL(x) \
static void ld_##x##_##dhl(GB_gameboy_t *gb, uint8_t opcode) \
{ \
gb->x = cycle_read(gb, gb->hl); \
}

#define LD_DHL_Y(y) \
static void ld_##dhl##_##y(GB_gameboy_t *gb, uint8_t opcode) \
{ \
cycle_write(gb, gb->hl, gb->y); \
}

            LD_X_Y(b,c) LD_X_Y(b,d) LD_X_Y(b,e) LD_X_Y(b,h) LD_X_Y(b,l) LD_X_DHL(b) LD_X_Y(b,a)
LD_X_Y(c,b)             LD_X_Y(c,d) LD_X_Y(c,e) LD_X_Y(c,h) LD_X_Y(c,l) LD_X_DHL(c) LD_X_Y(c,a)
LD_X_Y(d,b) LD_X_Y(d,c)             LD_X_Y(d,e) LD_X_Y(d,h) LD_X_Y(d,l) LD_X_DHL(d) LD_X_Y(d,a)
LD_X_Y(e,b) LD_X_Y(e,c) LD_X_Y(e,d)             LD_X_Y(e,h) LD_X_Y(e,l) LD_X_DHL(e) LD_X_Y(e,a)
LD_X_Y(h,b) LD_X_Y(h,c) LD_X_Y(h,d) LD_X_Y(h,e)             LD_X_Y(h,l) LD_X_DHL(h) LD_X_Y(h,a)
LD_X_Y(l,b) LD_X_Y(l,c) LD_X_Y(l,d) LD_X_Y(l,e) LD_X_Y(l,h)             LD_X_DHL(l) LD_X_Y(l,a)
LD_DHL_Y(b) LD_DHL_Y(c) LD_DHL_Y(d) LD_DHL_Y(e) LD_DHL_Y(h) LD_DHL_Y(l)             LD_DHL_Y(a)
LD_X_Y(a,b) LD_X_Y(a,c) LD_X_Y(a,d) LD_X_Y(a,e) LD_X_Y(a,h) LD_X_Y(a,l) LD_X_DHL(a)

// fire the debugger if software breakpoints are enabled
static void ld_b_b(GB_gameboy_t *gb, uint8_t opcode)
{
#ifndef GB_DISABLE_DEBUGGER
    if (gb->has_software_breakpoints) {
        GB_debugger_break(gb);
    }
#endif
}

static void add_a_r(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value, a;
    value = get_src_value(gb, opcode);
    a = gb->af >> 8;
    gb->af = (a + value) << 8;
    if ((uint8_t)(a + value) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
    if ((a & 0xF) + (value & 0xF) > 0x0F) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }
    if (((unsigned) a) + ((unsigned) value) > 0xFF) {
        gb->af |= GB_CARRY_FLAG;
    }
}

static void adc_a_r(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value, a, carry;
    value = get_src_value(gb, opcode);
    a = gb->af >> 8;
    carry = (gb->af & GB_CARRY_FLAG) != 0;
    gb->af = (a + value + carry) << 8;

    if ((uint8_t)(a + value + carry) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
    if ((a & 0xF) + (value & 0xF) + carry > 0x0F) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }
    if (((unsigned) a) + ((unsigned) value) + carry > 0xFF) {
        gb->af |= GB_CARRY_FLAG;
    }
}

static void sub_a_r(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value, a;
    value = get_src_value(gb, opcode);
    a = gb->af >> 8;
    gb->af = ((a - value) << 8) | GB_SUBTRACT_FLAG;
    if (a == value) {
        gb->af |= GB_ZERO_FLAG;
    }
    if ((a & 0xF) < (value & 0xF)) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }
    if (a < value) {
        gb->af |= GB_CARRY_FLAG;
    }
}

static void sbc_a_r(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value, a, carry;
    value = get_src_value(gb, opcode);
    a = gb->af >> 8;
    carry = (gb->af & GB_CARRY_FLAG) != 0;
    gb->af = (((uint8_t)(a - value - carry)) << 8) | GB_SUBTRACT_FLAG;

    if ((uint8_t) (a - value - carry) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
    if ((a & 0xF) < (value & 0xF) + carry) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }
    if (((unsigned) a) - ((unsigned) value) - carry > 0xFF) {
        gb->af |= GB_CARRY_FLAG;
    }
}

static void and_a_r(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value, a;
    value = get_src_value(gb, opcode);
    a = gb->af >> 8;
    gb->af = ((a & value) << 8) | GB_HALF_CARRY_FLAG;
    if ((a & value) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void xor_a_r(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value, a;
    value = get_src_value(gb, opcode);
    a = gb->af >> 8;
    gb->af = (a ^ value) << 8;
    if ((a ^ value) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void or_a_r(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value, a;
    value = get_src_value(gb, opcode);
    a = gb->af >> 8;
    gb->af = (a | value) << 8;
    if ((a | value) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void cp_a_r(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value, a;
    value = get_src_value(gb, opcode);
    a = gb->af >> 8;
    gb->af &= 0xFF00;
    gb->af |= GB_SUBTRACT_FLAG;
    if (a == value) {
        gb->af |= GB_ZERO_FLAG;
    }
    if ((a & 0xF) < (value & 0xF)) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }
    if (a < value) {
        gb->af |= GB_CARRY_FLAG;
    }
}

static void halt(GB_gameboy_t *gb, uint8_t opcode)
{
    cycle_read(gb, gb->pc);
    assert(gb->pending_cycles == 4);
    gb->pending_cycles = 0;
    
    /* Despite what some online documentations say, the HALT bug also happens on a CGB, in both CGB and DMG modes. */
    if (((gb->interrupt_enable & gb->io_registers[GB_IO_IF] & 0x1F) != 0)) {
        if (gb->ime) {
            gb->halted = false;
            gb->pc--;
        }
        else {
            gb->halted = false;
            gb->halt_bug = true;
        }
    }
    else {
        gb->halted = true;
        gb->allow_hdma_on_wake = (gb->io_registers[GB_IO_STAT] & 3);
    }
    gb->just_halted = true;
}

static void pop_rr(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t register_id;
    register_id = ((opcode >> 4) + 1) & 3;
    gb->registers[register_id] = cycle_read(gb, gb->sp++);
    gb->registers[register_id] |= cycle_read(gb, gb->sp++) << 8;
    gb->af &= 0xFFF0; // Make sure we don't set impossible flags on F! See Blargg's PUSH AF test.
}

static void jp_cc_a16(GB_gameboy_t *gb, uint8_t opcode)
{
    uint16_t addr = cycle_read(gb, gb->pc++);
    addr |= (cycle_read(gb, gb->pc++) << 8);
    if (condition_code(gb, opcode)) {
        cycle_no_access(gb);
        gb->pc = addr;
    }
}

static void jp_a16(GB_gameboy_t *gb, uint8_t opcode)
{
    uint16_t addr = cycle_read(gb, gb->pc);
    addr |= (cycle_read(gb, gb->pc + 1) << 8);
    cycle_no_access(gb);
    gb->pc = addr;
    
}

static void call_cc_a16(GB_gameboy_t *gb, uint8_t opcode)
{
    uint16_t call_addr = gb->pc - 1;
    uint16_t addr = cycle_read(gb, gb->pc++);
    addr |= (cycle_read(gb, gb->pc++) << 8);
    if (condition_code(gb, opcode)) {
        cycle_oam_bug(gb, GB_REGISTER_SP);
        cycle_write(gb, --gb->sp, (gb->pc) >> 8);
        cycle_write(gb, --gb->sp, (gb->pc) & 0xFF);
        gb->pc = addr;

        GB_debugger_call_hook(gb, call_addr);
    }
}

static void push_rr(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t register_id;
    cycle_oam_bug(gb, GB_REGISTER_SP);
    register_id = ((opcode >> 4) + 1) & 3;
    cycle_write(gb, --gb->sp, (gb->registers[register_id]) >> 8);
    cycle_write(gb, --gb->sp, (gb->registers[register_id]) & 0xFF);
}

static void add_a_d8(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value, a;
    value = cycle_read(gb, gb->pc++);
    a = gb->af >> 8;
    gb->af = (a + value) << 8;
    if ((uint8_t) (a + value) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
    if ((a & 0xF) + (value & 0xF) > 0x0F) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }
    if (((unsigned) a) + ((unsigned) value) > 0xFF) {
        gb->af |= GB_CARRY_FLAG;
    }
}

static void adc_a_d8(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value, a, carry;
    value = cycle_read(gb, gb->pc++);
    a = gb->af >> 8;
    carry = (gb->af & GB_CARRY_FLAG) != 0;
    gb->af = (a + value + carry) << 8;

    if (gb->af == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
    if ((a & 0xF) + (value & 0xF) + carry > 0x0F) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }
    if (((unsigned) a) + ((unsigned) value) + carry > 0xFF) {
        gb->af |= GB_CARRY_FLAG;
    }
}

static void sub_a_d8(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value, a;
    value = cycle_read(gb, gb->pc++);
    a = gb->af >> 8;
    gb->af = ((a - value) << 8) | GB_SUBTRACT_FLAG;
    if (a == value) {
        gb->af |= GB_ZERO_FLAG;
    }
    if ((a & 0xF) < (value & 0xF)) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }
    if (a < value) {
        gb->af |= GB_CARRY_FLAG;
    }
}

static void sbc_a_d8(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value, a, carry;
    value = cycle_read(gb, gb->pc++);
    a = gb->af >> 8;
    carry = (gb->af & GB_CARRY_FLAG) != 0;
    gb->af = ((a - value - carry) << 8) | GB_SUBTRACT_FLAG;

    if ((uint8_t) (a - value - carry) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
    if ((a & 0xF) < (value & 0xF) + carry) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }
    if (((unsigned) a) - ((unsigned) value) - carry > 0xFF) {
        gb->af |= GB_CARRY_FLAG;
    }
}

static void and_a_d8(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value, a;
    value = cycle_read(gb, gb->pc++);
    a = gb->af >> 8;
    gb->af = ((a & value) << 8) | GB_HALF_CARRY_FLAG;
    if ((a & value) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void xor_a_d8(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value, a;
    value = cycle_read(gb, gb->pc++);
    a = gb->af >> 8;
    gb->af = (a ^ value) << 8;
    if ((a ^ value) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void or_a_d8(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value, a;
    value = cycle_read(gb, gb->pc++);
    a = gb->af >> 8;
    gb->af = (a | value) << 8;
    if ((a | value) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void cp_a_d8(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value, a;
    value = cycle_read(gb, gb->pc++);
    a = gb->af >> 8;
    gb->af &= 0xFF00;
    gb->af |= GB_SUBTRACT_FLAG;
    if (a == value) {
        gb->af |= GB_ZERO_FLAG;
    }
    if ((a & 0xF) < (value & 0xF)) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }
    if (a < value) {
        gb->af |= GB_CARRY_FLAG;
    }
}

static void rst(GB_gameboy_t *gb, uint8_t opcode)
{
    uint16_t call_addr = gb->pc - 1;
    cycle_oam_bug(gb, GB_REGISTER_SP);
    cycle_write(gb, --gb->sp, (gb->pc) >> 8);
    cycle_write(gb, --gb->sp, (gb->pc) & 0xFF);
    gb->pc = opcode ^ 0xC7;
    GB_debugger_call_hook(gb, call_addr);
}

static void ret(GB_gameboy_t *gb, uint8_t opcode)
{
    GB_debugger_ret_hook(gb);
    gb->pc = cycle_read(gb, gb->sp++);
    gb->pc |= cycle_read(gb, gb->sp++) << 8;
    cycle_no_access(gb);
}

static void reti(GB_gameboy_t *gb, uint8_t opcode)
{
    ret(gb, opcode);
    gb->ime = true;
}

static void ret_cc(GB_gameboy_t *gb, uint8_t opcode)
{
    if (condition_code(gb, opcode)) {
        cycle_no_access(gb);
        ret(gb, opcode);
    }
    else {
        cycle_no_access(gb);
    }
}

static void call_a16(GB_gameboy_t *gb, uint8_t opcode)
{
    uint16_t call_addr = gb->pc - 1;
    uint16_t addr = cycle_read(gb, gb->pc++);
    addr |= (cycle_read(gb, gb->pc++) << 8);
    cycle_oam_bug(gb, GB_REGISTER_SP);
    cycle_write(gb, --gb->sp, (gb->pc) >> 8);
    cycle_write(gb, --gb->sp, (gb->pc) & 0xFF);
    gb->pc = addr;
    GB_debugger_call_hook(gb, call_addr);
}

static void ld_da8_a(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t temp = cycle_read(gb, gb->pc++);
    cycle_write(gb, 0xFF00 + temp, gb->af >> 8);
}

static void ld_a_da8(GB_gameboy_t *gb, uint8_t opcode)
{
    gb->af &= 0xFF;
    uint8_t temp = cycle_read(gb, gb->pc++);
    gb->af |= cycle_read(gb, 0xFF00 + temp) << 8;
}

static void ld_dc_a(GB_gameboy_t *gb, uint8_t opcode)
{
    cycle_write(gb, 0xFF00 + (gb->bc & 0xFF), gb->af >> 8);
}

static void ld_a_dc(GB_gameboy_t *gb, uint8_t opcode)
{
    gb->af &= 0xFF;
    gb->af |= cycle_read(gb, 0xFF00 + (gb->bc & 0xFF)) << 8;
}

static void add_sp_r8(GB_gameboy_t *gb, uint8_t opcode)
{
    int16_t offset;
    uint16_t sp = gb->sp;
    offset = (int8_t) cycle_read(gb, gb->pc++);
    cycle_no_access(gb);
    cycle_no_access(gb);
    gb->sp += offset;

    gb->af &= 0xFF00;

    /* A new instruction, a new meaning for Half Carry! */
    if ((sp & 0xF) + (offset & 0xF) > 0xF) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }

    if ((sp & 0xFF) + (offset & 0xFF) > 0xFF)  {
        gb->af |= GB_CARRY_FLAG;
    }
}

static void jp_hl(GB_gameboy_t *gb, uint8_t opcode)
{
    gb->pc = gb->hl;
}

static void ld_da16_a(GB_gameboy_t *gb, uint8_t opcode)
{
    uint16_t addr;
    addr = cycle_read(gb, gb->pc++);
    addr |= cycle_read(gb, gb->pc++) << 8;
    cycle_write(gb, addr, gb->af >> 8);
}

static void ld_a_da16(GB_gameboy_t *gb, uint8_t opcode)
{
    uint16_t addr;
    gb->af &= 0xFF;
    addr = cycle_read(gb, gb->pc++);
    addr |= cycle_read(gb, gb->pc++) << 8;
    gb->af |= cycle_read(gb, addr) << 8;
}

static void di(GB_gameboy_t *gb, uint8_t opcode)
{
    /* DI is NOT delayed, not even on a CGB. Mooneye's di_timing-GS test fails on a CGB
       for different reasons. */
    gb->ime = false;
}

static void ei(GB_gameboy_t *gb, uint8_t opcode)
{
    /* ei is actually "disable interrupts for one instruction, then enable them". */
    if (!gb->ime && !gb->ime_toggle) {
        gb->ime_toggle = true;
    }
}

static void ld_hl_sp_r8(GB_gameboy_t *gb, uint8_t opcode)
{
    int16_t offset;
    gb->af &= 0xFF00;
    offset = (int8_t) cycle_read(gb, gb->pc++);
    cycle_no_access(gb);
    gb->hl = gb->sp + offset;

    if ((gb->sp & 0xF) + (offset & 0xF) > 0xF) {
        gb->af |= GB_HALF_CARRY_FLAG;
    }

    if ((gb->sp & 0xFF)  + (offset & 0xFF) > 0xFF) {
        gb->af |= GB_CARRY_FLAG;
    }
}

static void ld_sp_hl(GB_gameboy_t *gb, uint8_t opcode)
{
    gb->sp = gb->hl;
    cycle_oam_bug(gb, GB_REGISTER_HL);
}

static void rlc_r(GB_gameboy_t *gb, uint8_t opcode)
{
    bool carry;
    uint8_t value;
    value = get_src_value(gb, opcode);
    carry = (value & 0x80) != 0;
    gb->af &= 0xFF00;
    set_src_value(gb, opcode, (value << 1) | carry);
    if (carry) {
        gb->af |= GB_CARRY_FLAG;
    }
    if (value == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void rrc_r(GB_gameboy_t *gb, uint8_t opcode)
{
    bool carry;
    uint8_t value;
    value = get_src_value(gb, opcode);
    carry = (value & 0x01) != 0;
    gb->af &= 0xFF00;
    value = (value >> 1) | (carry << 7);
    set_src_value(gb, opcode, value);
    if (carry) {
        gb->af |= GB_CARRY_FLAG;
    }
    if (value == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void rl_r(GB_gameboy_t *gb, uint8_t opcode)
{
    bool carry;
    uint8_t value;
    bool bit7;
    value = get_src_value(gb, opcode);
    carry = (gb->af & GB_CARRY_FLAG) != 0;
    bit7 = (value & 0x80) != 0;

    gb->af &= 0xFF00;
    value = (value << 1) | carry;
    set_src_value(gb, opcode, value);
    if (bit7) {
        gb->af |= GB_CARRY_FLAG;
    }
    if (value == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void rr_r(GB_gameboy_t *gb, uint8_t opcode)
{
    bool carry;
    uint8_t value;
    bool bit1;

    value = get_src_value(gb, opcode);
    carry = (gb->af & GB_CARRY_FLAG) != 0;
    bit1 = (value & 0x1) != 0;

    gb->af &= 0xFF00;
    value = (value >> 1) | (carry << 7);
    set_src_value(gb, opcode, value);
    if (bit1) {
        gb->af |= GB_CARRY_FLAG;
    }
    if (value == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void sla_r(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value;
    bool carry;
    value = get_src_value(gb, opcode);
    carry = (value & 0x80) != 0;
    gb->af &= 0xFF00;
    set_src_value(gb, opcode, (value << 1));
    if (carry) {
        gb->af |= GB_CARRY_FLAG;
    }
    if ((value & 0x7F) == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void sra_r(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t bit7;
    uint8_t value;
    value = get_src_value(gb, opcode);
    bit7 = value & 0x80;
    gb->af &= 0xFF00;
    if (value & 1) {
        gb->af |= GB_CARRY_FLAG;
    }
    value = (value >> 1) | bit7;
    set_src_value(gb, opcode, value);
    if (value == 0) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void srl_r(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value;
    value = get_src_value(gb, opcode);
    gb->af &= 0xFF00;
    set_src_value(gb, opcode, (value >> 1));
    if (value & 1) {
        gb->af |= GB_CARRY_FLAG;
    }
    if (!(value >> 1)) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void swap_r(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value;
    value = get_src_value(gb, opcode);
    gb->af &= 0xFF00;
    set_src_value(gb, opcode, (value >> 4) | (value << 4));
    if (!value) {
        gb->af |= GB_ZERO_FLAG;
    }
}

static void bit_r(GB_gameboy_t *gb, uint8_t opcode)
{
    uint8_t value;
    uint8_t bit;
    value = get_src_value(gb, opcode);
    bit = 1 << ((opcode >> 3) & 7);
    if ((opcode & 0xC0) == 0x40) { /* Bit */
        gb->af &= 0xFF00 | GB_CARRY_FLAG;
        gb->af |= GB_HALF_CARRY_FLAG;
        if (!(bit & value)) {
            gb->af |= GB_ZERO_FLAG;
        }
    }
    else if ((opcode & 0xC0) == 0x80) { /* res */
        set_src_value(gb, opcode, value & ~bit);
    }
    else if ((opcode & 0xC0) == 0xC0) { /* set */
        set_src_value(gb, opcode, value | bit);
    }
}

static void cb_prefix(GB_gameboy_t *gb, uint8_t opcode)
{
    opcode = cycle_read(gb, gb->pc++);
    switch (opcode >> 3) {
        case 0:
            rlc_r(gb, opcode);
            break;
        case 1:
            rrc_r(gb, opcode);
            break;
        case 2:
            rl_r(gb, opcode);
            break;
        case 3:
            rr_r(gb, opcode);
            break;
        case 4:
            sla_r(gb, opcode);
            break;
        case 5:
            sra_r(gb, opcode);
            break;
        case 6:
            swap_r(gb, opcode);
            break;
        case 7:
            srl_r(gb, opcode);
            break;
        default:
            bit_r(gb, opcode);
            break;
    }
}

static opcode_t *opcodes[256] = {
/*  X0          X1          X2          X3          X4          X5          X6          X7                */
/*  X8          X9          Xa          Xb          Xc          Xd          Xe          Xf                */
    nop,        ld_rr_d16,  ld_drr_a,   inc_rr,     inc_hr,     dec_hr,     ld_hr_d8,   rlca,       /* 0X */
    ld_da16_sp, add_hl_rr,  ld_a_drr,   dec_rr,     inc_lr,     dec_lr,     ld_lr_d8,   rrca,
    stop,       ld_rr_d16,  ld_drr_a,   inc_rr,     inc_hr,     dec_hr,     ld_hr_d8,   rla,        /* 1X */
    jr_r8,      add_hl_rr,  ld_a_drr,   dec_rr,     inc_lr,     dec_lr,     ld_lr_d8,   rra,
    jr_cc_r8,   ld_rr_d16,  ld_dhli_a,  inc_rr,     inc_hr,     dec_hr,     ld_hr_d8,   daa,        /* 2X */
    jr_cc_r8,   add_hl_rr,  ld_a_dhli,  dec_rr,     inc_lr,     dec_lr,     ld_lr_d8,   cpl,
    jr_cc_r8,   ld_rr_d16,  ld_dhld_a,  inc_rr,     inc_dhl,    dec_dhl,    ld_dhl_d8,  scf,        /* 3X */
    jr_cc_r8,   add_hl_rr,  ld_a_dhld,  dec_rr,     inc_hr,     dec_hr,     ld_hr_d8,   ccf,
    ld_b_b,     ld_b_c,     ld_b_d,     ld_b_e,     ld_b_h,     ld_b_l,     ld_b_dhl,   ld_b_a,     /* 4X */
    ld_c_b,     nop,        ld_c_d,     ld_c_e,     ld_c_h,     ld_c_l,     ld_c_dhl,   ld_c_a,
    ld_d_b,     ld_d_c,     nop,        ld_d_e,     ld_d_h,     ld_d_l,     ld_d_dhl,   ld_d_a,     /* 5X */
    ld_e_b,     ld_e_c,     ld_e_d,     nop,        ld_e_h,     ld_e_l,     ld_e_dhl,   ld_e_a,
    ld_h_b,     ld_h_c,     ld_h_d,     ld_h_e,     nop,        ld_h_l,     ld_h_dhl,   ld_h_a,     /* 6X */
    ld_l_b,     ld_l_c,     ld_l_d,     ld_l_e,     ld_l_h,     nop,        ld_l_dhl,   ld_l_a,
    ld_dhl_b,   ld_dhl_c,   ld_dhl_d,   ld_dhl_e,   ld_dhl_h,   ld_dhl_l,   halt,       ld_dhl_a,   /* 7X */
    ld_a_b,     ld_a_c,     ld_a_d,     ld_a_e,     ld_a_h,     ld_a_l,     ld_a_dhl,   nop,
    add_a_r,    add_a_r,    add_a_r,    add_a_r,    add_a_r,    add_a_r,    add_a_r,    add_a_r,    /* 8X */
    adc_a_r,    adc_a_r,    adc_a_r,    adc_a_r,    adc_a_r,    adc_a_r,    adc_a_r,    adc_a_r,
    sub_a_r,    sub_a_r,    sub_a_r,    sub_a_r,    sub_a_r,    sub_a_r,    sub_a_r,    sub_a_r,    /* 9X */
    sbc_a_r,    sbc_a_r,    sbc_a_r,    sbc_a_r,    sbc_a_r,    sbc_a_r,    sbc_a_r,    sbc_a_r,
    and_a_r,    and_a_r,    and_a_r,    and_a_r,    and_a_r,    and_a_r,    and_a_r,    and_a_r,    /* aX */
    xor_a_r,    xor_a_r,    xor_a_r,    xor_a_r,    xor_a_r,    xor_a_r,    xor_a_r,    xor_a_r,
    or_a_r,     or_a_r,     or_a_r,     or_a_r,     or_a_r,     or_a_r,     or_a_r,     or_a_r,     /* bX */
    cp_a_r,     cp_a_r,     cp_a_r,     cp_a_r,     cp_a_r,     cp_a_r,     cp_a_r,     cp_a_r,
    ret_cc,     pop_rr,     jp_cc_a16,  jp_a16,     call_cc_a16,push_rr,    add_a_d8,   rst,        /* cX */
    ret_cc,     ret,        jp_cc_a16,  cb_prefix,  call_cc_a16,call_a16,   adc_a_d8,   rst,
    ret_cc,     pop_rr,     jp_cc_a16,  ill,        call_cc_a16,push_rr,    sub_a_d8,   rst,        /* dX */
    ret_cc,     reti,       jp_cc_a16,  ill,        call_cc_a16,ill,        sbc_a_d8,   rst,
    ld_da8_a,   pop_rr,     ld_dc_a,    ill,        ill,        push_rr,    and_a_d8,   rst,        /* eX */
    add_sp_r8,  jp_hl,      ld_da16_a,  ill,        ill,        ill,        xor_a_d8,   rst,
    ld_a_da8,   pop_rr,     ld_a_dc,    di,         ill,        push_rr,    or_a_d8,    rst,        /* fX */
    ld_hl_sp_r8,ld_sp_hl,   ld_a_da16,  ei,         ill,        ill,        cp_a_d8,    rst,
};
void GB_cpu_run(GB_gameboy_t *gb)
{
    if (unlikely(gb->stopped)) {
        GB_timing_sync(gb);
        GB_advance_cycles(gb, 4);
        if ((gb->io_registers[GB_IO_JOYP] & 0x30) != 0x30) {
            gb->joyp_accessed = true;
        }
        if ((gb->io_registers[GB_IO_JOYP] & 0xF) != 0xF) {
            leave_stop_mode(gb);
            GB_advance_cycles(gb, 8);
        }
        return;
    }
    
    if ((gb->interrupt_enable & 0x10) && (gb->ime || gb->halted)) {
        GB_timing_sync(gb);
    }
    
    if (gb->halted && !GB_is_cgb(gb) && !gb->just_halted) {
        GB_advance_cycles(gb, 2);
    }
    
    uint8_t interrupt_queue = gb->interrupt_enable & gb->io_registers[GB_IO_IF] & 0x1F;
    
    if (gb->halted) {
        GB_advance_cycles(gb, (GB_is_cgb(gb) || gb->just_halted) ? 4 : 2);
    }
    gb->just_halted = false;

    bool effective_ime = gb->ime;
    if (gb->ime_toggle) {
        gb->ime = !gb->ime;
        gb->ime_toggle = false;
    }

    /* Wake up from HALT mode without calling interrupt code. */
    if (gb->halted && !effective_ime && interrupt_queue) {
        gb->halted = false;
        if (gb->hdma_on_hblank && (gb->io_registers[GB_IO_STAT] & 3) == 0 && gb->allow_hdma_on_wake) {
            gb->hdma_on = true;
        }
        gb->dma_cycles = 4;
        GB_dma_run(gb);
        gb->speed_switch_halt_countdown = 0;
    }
    
    /* Call interrupt */
    else if (unlikely(effective_ime && interrupt_queue)) {
        gb->halted = false;
        if (gb->hdma_on_hblank && (gb->io_registers[GB_IO_STAT] & 3) == 0 && gb->allow_hdma_on_wake) {
            gb->hdma_on = true;
        }
        // TODO: verify the timing!
        gb->dma_cycles = 4;
        GB_dma_run(gb);
        gb->speed_switch_halt_countdown = 0;
        uint16_t call_addr = gb->pc;
        
        cycle_read(gb, gb->pc++);
        cycle_oam_bug(gb, GB_REGISTER_PC);
        gb->pc--;
        GB_trigger_oam_bug(gb, gb->sp); /* Todo: test T-cycle timing */
        cycle_no_access(gb);
        
        cycle_write(gb, --gb->sp, (gb->pc) >> 8);
        interrupt_queue = gb->interrupt_enable;
        
        if (gb->sp == GB_IO_IF + 0xFF00 + 1) {
            gb->sp--;
            interrupt_queue &= cycle_write_if(gb, (gb->pc) & 0xFF);
        }
        else {
            cycle_write(gb, --gb->sp, (gb->pc) & 0xFF);
            interrupt_queue &= (gb->io_registers[GB_IO_IF]) & 0x1F;
        }
        
        if (interrupt_queue) {
            uint8_t interrupt_bit = 0;
            while (!(interrupt_queue & 1)) {
                interrupt_queue >>= 1;
                interrupt_bit++;
            }
            assert(gb->pending_cycles > 2);
            gb->pending_cycles -= 2;
            flush_pending_cycles(gb);
            gb->pending_cycles = 2;
            gb->io_registers[GB_IO_IF] &= ~(1 << interrupt_bit);
            gb->pc = interrupt_bit * 8 + 0x40;
        }
        else {
            gb->pc = 0;
        }
        gb->ime = false;
        GB_debugger_call_hook(gb, call_addr);
    }
    /* Run mode */
    else if (!gb->halted) {
        uint8_t opcode = cycle_read(gb, gb->pc++);
        if (unlikely(gb->hdma_on)) {
            GB_hdma_run(gb);
        }
        if (unlikely(gb->execution_callback)) {
            gb->execution_callback(gb, gb->pc - 1, opcode);
        }
        if (unlikely(gb->halt_bug)) {
            gb->pc--;
            gb->halt_bug = false;
        }
        opcodes[opcode](gb, opcode);
    }
    
    flush_pending_cycles(gb);
}

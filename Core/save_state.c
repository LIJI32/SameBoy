#include "gb.h"
#include <stdio.h>
#include <errno.h>
#include <assert.h>

#ifdef GB_BIG_ENDIAN
#define BESS_NAME "SameBoy v" GB_VERSION " (Big Endian)"
#else
#define BESS_NAME "SameBoy v" GB_VERSION
#endif

_Static_assert((GB_SECTION_OFFSET(core_state) & 7) == 0, "Section core_state is not aligned");
_Static_assert((GB_SECTION_OFFSET(dma) & 7) == 0, "Section dma is not aligned");
_Static_assert((GB_SECTION_OFFSET(mbc) & 7) == 0, "Section mbc is not aligned");
_Static_assert((GB_SECTION_OFFSET(hram) & 7) == 0, "Section hram is not aligned");
_Static_assert((GB_SECTION_OFFSET(timing) & 7) == 0, "Section timing is not aligned");
_Static_assert((GB_SECTION_OFFSET(apu) & 7) == 0, "Section apu is not aligned");
_Static_assert((GB_SECTION_OFFSET(rtc) & 7) == 0, "Section rtc is not aligned");
_Static_assert((GB_SECTION_OFFSET(video) & 7) == 0, "Section video is not aligned");

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t size;
} BESS_block_t;

typedef struct __attribute__((packed)) {
    uint32_t size;
    uint32_t offset;
} BESS_buffer_t;

typedef struct __attribute__((packed)) {
    uint32_t start_offset;
    uint32_t magic;
} BESS_footer_t;

typedef struct __attribute__((packed)) {
    BESS_block_t header;
    uint16_t  major;
    uint16_t  minor;
    union {
        struct {
            char family;
            char model;
            char revision;
            char padding;
        };
        uint32_t full_model;
    };
    
    uint16_t pc;
    uint16_t af;
    uint16_t bc;
    uint16_t de;
    uint16_t hl;
    uint16_t sp;
    
    uint8_t ime;
    uint8_t ie;
    uint8_t execution_mode; // 0 = running; 1 = halted; 2 = stopped
    uint8_t _padding;
    
    uint8_t io_registers[0x80];
    
    BESS_buffer_t ram;
    BESS_buffer_t vram;
    BESS_buffer_t mbc_ram;
    BESS_buffer_t oam;
    BESS_buffer_t hram;
    BESS_buffer_t background_palettes;
    BESS_buffer_t object_palettes;
} BESS_CORE_t;

typedef struct __attribute__((packed)) {
    BESS_block_t header;
    uint8_t extra_oam[96];
} BESS_XOAM_t;

typedef struct __attribute__((packed)) {
    BESS_block_t header;
    BESS_buffer_t border_tiles;
    BESS_buffer_t border_tilemap;
    BESS_buffer_t border_palettes;
    
    BESS_buffer_t active_palettes;
    BESS_buffer_t ram_palettes;
    BESS_buffer_t attribute_map;
    BESS_buffer_t attribute_files;
    
    uint8_t multiplayer_state;
} BESS_SGB_t;

typedef struct __attribute__((packed)){
    BESS_block_t header;
    char title[0x10];
    uint8_t checksum[2];
} BESS_INFO_t;

/* Same RTC format as used by VBA, BGB and SameBoy in battery saves*/
typedef struct __attribute__((packed)){
    BESS_block_t header;
    struct {
        uint8_t seconds;
        uint8_t padding1[3];
        uint8_t minutes;
        uint8_t padding2[3];
        uint8_t hours;
        uint8_t padding3[3];
        uint8_t days;
        uint8_t padding4[3];
        uint8_t high;
        uint8_t padding5[3];
    } real, latched;
    uint64_t last_rtc_second;
} BESS_RTC_t;

/* Same HuC3 RTC format as used by SameBoy and BGB in battery saves */
typedef struct __attribute__((packed)){
    BESS_block_t header;
    GB_huc3_rtc_time_t data;
} BESS_HUC3_t;

typedef struct __attribute__((packed)){
    BESS_block_t header;
    uint64_t last_rtc_second;
    uint8_t real_rtc_data[4];
    uint8_t latched_rtc_data[4];
    uint8_t mr4;
} BESS_TPP1_t;

typedef struct  __attribute__((packed)) {
    uint16_t address;
    uint8_t value;
} BESS_MBC_pair_t;

typedef struct virtual_file_s virtual_file_t;
struct virtual_file_s
{
    size_t (*read)(virtual_file_t *file, void *dest, size_t length);
    size_t (*write)(virtual_file_t *file, const void *dest, size_t length);
    void (*seek)(virtual_file_t *file, ssize_t ammount, int origin);
    size_t (*tell)(virtual_file_t *file);
    union {
        FILE *file;
        struct {
            uint8_t *buffer;
            size_t position;
            size_t size;
        };
    };
};

static size_t file_read(virtual_file_t *file, void *dest, size_t length)
{
    return fread(dest, 1, length, file->file);
}

static void file_seek(virtual_file_t *file, ssize_t ammount, int origin)
{
    fseek(file->file, ammount, origin);
}

static size_t file_write(virtual_file_t *file, const void *src, size_t length)
{
    return fwrite(src, 1, length, file->file);
}

static size_t file_tell(virtual_file_t *file)
{
    return ftell(file->file);
}

static size_t buffer_read(virtual_file_t *file, void *dest, size_t length)
{
    if (length & 0x80000000) {
        return 0;
    }
    errno = 0;
    if (length > file->size - file->position) {
        errno = EIO;
        length = file->size - file->position;
    }
    
    memcpy(dest, file->buffer + file->position, length);
    file->position += length;
    
    return length;
}

static void buffer_seek(virtual_file_t *file, ssize_t ammount, int origin)
{
    switch (origin) {
        case SEEK_SET:
            file->position = ammount;
            break;
        case SEEK_CUR:
            file->position += ammount;
            break;
        case SEEK_END:
            file->position = file->size + ammount;
            break;
        default:
            break;
    }
    
    if (file->position > file->size) {
        file->position = file->size;
    }
}

static size_t buffer_write(virtual_file_t *file, const void *src, size_t size)
{
    memcpy(file->buffer + file->position, src, size);
    file->position += size;
    return size;
}

static size_t buffer_tell(virtual_file_t *file)
{
    return file->position;
}

static size_t bess_size_for_cartridge(const GB_cartridge_t *cart)
{
    switch (cart->mbc_type) {
        default:
        case GB_NO_MBC: return 0;
        case GB_MBC1:
            return sizeof(BESS_block_t) + 4 * sizeof(BESS_MBC_pair_t);
        case GB_MBC2:
            return sizeof(BESS_block_t) + 2 * sizeof(BESS_MBC_pair_t);
        case GB_MBC3:
            return sizeof(BESS_block_t) + 3 * sizeof(BESS_MBC_pair_t) + (cart->has_rtc? sizeof(BESS_RTC_t) : 0);
        case GB_MBC5:
            return sizeof(BESS_block_t) + 4 * sizeof(BESS_MBC_pair_t);
        case GB_HUC1:
            return sizeof(BESS_block_t) + 4 * sizeof(BESS_MBC_pair_t);
        case GB_HUC3:
            return sizeof(BESS_block_t) + 3 * sizeof(BESS_MBC_pair_t) + sizeof(BESS_HUC3_t);
        case GB_TPP1:
            return sizeof(BESS_block_t) + 4 * sizeof(BESS_MBC_pair_t) + sizeof(BESS_TPP1_t);
    }
}

size_t GB_get_save_state_size_no_bess(GB_gameboy_t *gb)
{
    return GB_SECTION_SIZE(header)
    + GB_SECTION_SIZE(core_state) + sizeof(uint32_t)
    + GB_SECTION_SIZE(dma       ) + sizeof(uint32_t)
    + GB_SECTION_SIZE(mbc       ) + sizeof(uint32_t)
    + GB_SECTION_SIZE(hram      ) + sizeof(uint32_t)
    + GB_SECTION_SIZE(timing    ) + sizeof(uint32_t)
    + GB_SECTION_SIZE(apu       ) + sizeof(uint32_t)
    + GB_SECTION_SIZE(rtc       ) + sizeof(uint32_t)
    + GB_SECTION_SIZE(video     ) + sizeof(uint32_t)
    + (GB_is_hle_sgb(gb)? sizeof(*gb->sgb) + sizeof(uint32_t) : 0)
    + gb->mbc_ram_size
    + gb->ram_size
    + gb->vram_size;
}

size_t GB_get_save_state_size(GB_gameboy_t *gb)
{
    return GB_get_save_state_size_no_bess(gb) +
    // BESS
    + sizeof(BESS_block_t) // NAME
    + sizeof(BESS_NAME) - 1
    + sizeof(BESS_INFO_t) // INFO
    + sizeof(BESS_CORE_t)
    + sizeof(BESS_XOAM_t)
    + (gb->sgb? sizeof(BESS_SGB_t) : 0)
    + bess_size_for_cartridge(gb->cartridge_type) // MBC & RTC/HUC3/TPP1 block
    + sizeof(BESS_block_t) // END block
    + sizeof(BESS_footer_t);
}

static bool verify_and_update_state_compatibility(GB_gameboy_t *gb, GB_gameboy_t *save, bool *attempt_bess)
{
    *attempt_bess = false;
    
    if (gb->version != save->version) {
        GB_log(gb, "The save state is for a different version of SameBoy.\n");
        *attempt_bess = true;
        return false;
    }
    
    if (GB_is_cgb(gb) != GB_is_cgb(save) || GB_is_hle_sgb(gb) != GB_is_hle_sgb(save)) {
        GB_log(gb, "The save state is for a different Game Boy model. Try changing the emulated model.\n");
        return false;
    }
    
    if (gb->mbc_ram_size < save->mbc_ram_size) {
        GB_log(gb, "The save state has non-matching MBC RAM size.\n");
        return false;
    }
    
    if (gb->vram_size != save->vram_size) {
        GB_log(gb, "The save state has non-matching VRAM size. Try changing the emulated model.\n");
        return false;
    }
    
    if (GB_is_hle_sgb(gb) != GB_is_hle_sgb(save)) {
        GB_log(gb, "The save state is %sfor a Super Game Boy. Try changing the emulated model.\n", GB_is_hle_sgb(save)? "" : "not ");
        return false;
    }
    
    if (gb->ram_size != save->ram_size) {
        GB_log(gb, "The save state has non-matching RAM size. Try changing the emulated model.\n");
        return false;
    }
    
    switch (save->model) {
        case GB_MODEL_DMG_B: return true;
        case GB_MODEL_SGB_NTSC: return true;
        case GB_MODEL_SGB_PAL: return true;
        case GB_MODEL_SGB_NTSC_NO_SFC: return true;
        case GB_MODEL_SGB_PAL_NO_SFC: return true;
        case GB_MODEL_MGB: return true;
        case GB_MODEL_SGB2: return true;
        case GB_MODEL_SGB2_NO_SFC: return true;
        case GB_MODEL_CGB_0: return true;
        case GB_MODEL_CGB_A: return true;
        case GB_MODEL_CGB_B: return true;
        case GB_MODEL_CGB_C: return true;
        case GB_MODEL_CGB_D: return true;
        case GB_MODEL_CGB_E: return true;
        case GB_MODEL_AGB_A: return true;
    }
    if ((gb->model & GB_MODEL_FAMILY_MASK) == (save->model & GB_MODEL_FAMILY_MASK)) {
        save->model = gb->model;
        return true;
    }
    GB_log(gb, "This save state is for an unknown Game Boy model\n");
    return false;
}

static void sanitize_state(GB_gameboy_t *gb)
{
    for (unsigned i = 0; i < 32; i++) {
        GB_palette_changed(gb, false, i * 2);
        GB_palette_changed(gb, true, i * 2);
    }
    
    gb->bg_fifo.read_end &= 0xF;
    gb->bg_fifo.write_end &= 0xF;
    gb->oam_fifo.read_end &= 0xF;
    gb->oam_fifo.write_end &= 0xF;
    gb->last_tile_index_address &= 0x1FFF;
    gb->window_tile_x &= 0x1F;
    
    /* These are kind of DOS-ish if too large */
    if (abs(gb->display_cycles) > 0x80000) {
        gb->display_cycles = 0;
    }
    
    if (abs(gb->div_cycles) > 0x8000) {
        gb->div_cycles = 0;
    }
    
    if (!GB_is_cgb(gb)) {
        gb->cgb_mode = false;
    }
    
    if (gb->ram_size == 0x8000) {
        gb->cgb_ram_bank &= 0x7;
    }
    else {
        gb->cgb_ram_bank = 1;
    }
    if (gb->vram_size != 0x4000) {
        gb->cgb_vram_bank = 0;
    }
    if (!GB_is_cgb(gb)) {
        gb->current_tile_attributes = 0;
    }

    gb->object_low_line_address &= gb->vram_size & ~1;
    if (gb->lcd_x > gb->position_in_line) {
        gb->lcd_x = gb->position_in_line;
    }
    
    if (gb->sgb) {
        if (gb->sgb->player_count != 1 && gb->sgb->player_count != 2 && gb->sgb->player_count != 4) {
            gb->sgb->player_count = 1;
        }
        gb->sgb->current_player &= gb->sgb->player_count - 1;
    }
    GB_update_clock_rate(gb);
}

static bool dump_section(virtual_file_t *file, const void *src, uint32_t size)
{
    if (file->write(file, &size, sizeof(size)) != sizeof(size)) {
        return false;
    }
    
    if (file->write(file, src, size) != size) {
        return false;
    }
    
    return true;
}

#define DUMP_SECTION(gb, f, section) dump_section(f, GB_GET_SECTION(gb, section), GB_SECTION_SIZE(section))

static int save_bess_mbc_block(GB_gameboy_t *gb, virtual_file_t *file)
{
    
    BESS_block_t mbc_block = {BE32('MBC '), 0};
    BESS_MBC_pair_t pairs[4];
    switch (gb->cartridge_type->mbc_type) {
        default:
        case GB_NO_MBC: return 0;
        case GB_MBC1:
            pairs[0] = (BESS_MBC_pair_t){LE16(0x0000), gb->mbc_ram_enable? 0xA : 0x0};
            pairs[1] = (BESS_MBC_pair_t){LE16(0x2000), gb->mbc1.bank_low};
            pairs[2] = (BESS_MBC_pair_t){LE16(0x4000), gb->mbc1.bank_high};
            pairs[3] = (BESS_MBC_pair_t){LE16(0x6000), gb->mbc1.mode};
            mbc_block.size = 4 * sizeof(pairs[0]);
            break;
        case GB_MBC2:
            pairs[0] = (BESS_MBC_pair_t){LE16(0x0000), gb->mbc_ram_enable? 0xA : 0x0};
            pairs[1] = (BESS_MBC_pair_t){LE16(0x0100), gb->mbc2.rom_bank};
            mbc_block.size = 2 * sizeof(pairs[0]);
            break;
        case GB_MBC3:
            pairs[0] = (BESS_MBC_pair_t){LE16(0x0000), gb->mbc_ram_enable? 0xA : 0x0};
            pairs[1] = (BESS_MBC_pair_t){LE16(0x2000), gb->mbc3.rom_bank};
            pairs[2] = (BESS_MBC_pair_t){LE16(0x4000), gb->mbc3.ram_bank | (gb->mbc3.rtc_mapped? 8 : 0)};
            mbc_block.size = 3 * sizeof(pairs[0]);
            break;
        case GB_MBC5:
            pairs[0] = (BESS_MBC_pair_t){LE16(0x0000), gb->mbc_ram_enable? 0xA : 0x0};
            pairs[1] = (BESS_MBC_pair_t){LE16(0x2000), gb->mbc5.rom_bank_low};
            pairs[2] = (BESS_MBC_pair_t){LE16(0x3000), gb->mbc5.rom_bank_high};
            pairs[3] = (BESS_MBC_pair_t){LE16(0x4000), gb->mbc5.ram_bank};
            mbc_block.size = 4 * sizeof(pairs[0]);
            break;
        case GB_HUC1:
            pairs[0] = (BESS_MBC_pair_t){LE16(0x0000), gb->huc1.ir_mode? 0xE : 0x0};
            pairs[1] = (BESS_MBC_pair_t){LE16(0x2000), gb->huc1.bank_low};
            pairs[2] = (BESS_MBC_pair_t){LE16(0x4000), gb->huc1.bank_high};
            pairs[3] = (BESS_MBC_pair_t){LE16(0x6000), gb->huc1.mode};
            mbc_block.size = 4 * sizeof(pairs[0]);
            
        case GB_HUC3:
            pairs[0] = (BESS_MBC_pair_t){LE16(0x0000), gb->huc3.mode};
            pairs[1] = (BESS_MBC_pair_t){LE16(0x2000), gb->huc3.rom_bank};
            pairs[2] = (BESS_MBC_pair_t){LE16(0x4000), gb->huc3.ram_bank};
            mbc_block.size = 3 * sizeof(pairs[0]);
            break;
        
        case GB_TPP1:
            pairs[0] = (BESS_MBC_pair_t){LE16(0x0000), gb->tpp1.rom_bank};
            pairs[1] = (BESS_MBC_pair_t){LE16(0x0001), gb->tpp1.rom_bank >> 8};
            pairs[2] = (BESS_MBC_pair_t){LE16(0x0002), gb->tpp1.rom_bank};
            pairs[3] = (BESS_MBC_pair_t){LE16(0x0003), gb->tpp1.mode};
            mbc_block.size = 4 * sizeof(pairs[0]);
            break;
    }
    
    mbc_block.size = LE32(mbc_block.size);
    
    if (file->write(file, &mbc_block, sizeof(mbc_block)) != sizeof(mbc_block)) {
        return errno;
    }
    
    if (file->write(file, &pairs, LE32(mbc_block.size)) != LE32(mbc_block.size)) {
        return errno;
    }
    
    return 0;
}

static int save_state_internal(GB_gameboy_t *gb, virtual_file_t *file, bool append_bess)
{
    if (file->write(file, GB_GET_SECTION(gb, header), GB_SECTION_SIZE(header)) != GB_SECTION_SIZE(header)) goto error;
    if (!DUMP_SECTION(gb, file, core_state)) goto error;
    if (!DUMP_SECTION(gb, file, dma       )) goto error;
    if (!DUMP_SECTION(gb, file, mbc       )) goto error;
    uint32_t hram_offset = file->tell(file) + 4;
    if (!DUMP_SECTION(gb, file, hram      )) goto error;
    if (!DUMP_SECTION(gb, file, timing    )) goto error;
    if (!DUMP_SECTION(gb, file, apu       )) goto error;
    if (!DUMP_SECTION(gb, file, rtc       )) goto error;
    uint32_t video_offset = file->tell(file) + 4;
    if (!DUMP_SECTION(gb, file, video     )) goto error;
    
    uint32_t sgb_offset = 0;
    
    if (GB_is_hle_sgb(gb)) {
        sgb_offset = file->tell(file) + 4;
        if (!dump_section(file, gb->sgb, sizeof(*gb->sgb))) goto error;
    }
    
    
    BESS_CORE_t bess_core = {0,};
    
    bess_core.mbc_ram.offset = LE32(file->tell(file));
    bess_core.mbc_ram.size = LE32(gb->mbc_ram_size);
    if (file->write(file, gb->mbc_ram, gb->mbc_ram_size) != gb->mbc_ram_size) {
        goto error;
    }
    
    bess_core.ram.offset = LE32(file->tell(file));
    bess_core.ram.size = LE32(gb->ram_size);
    if (file->write(file, gb->ram, gb->ram_size) != gb->ram_size) {
        goto error;
    }
    
    bess_core.vram.offset = LE32(file->tell(file));
    bess_core.vram.size = LE32(gb->vram_size);
    if (file->write(file, gb->vram, gb->vram_size) != gb->vram_size) {
        goto error;
    }
    
    if (!append_bess) return 0;
    
    BESS_footer_t bess_footer = {
        .start_offset = LE32(file->tell(file)),
        .magic = BE32('BESS'),
    };
    
    /* BESS NAME */
    
    static const BESS_block_t bess_name = {BE32('NAME'), LE32(sizeof(BESS_NAME) - 1)};
    
    if (file->write(file, &bess_name, sizeof(bess_name)) != sizeof(bess_name)) {
        goto error;
    }
    
    if (file->write(file, BESS_NAME, sizeof(BESS_NAME) - 1) != sizeof(BESS_NAME) - 1) {
        goto error;
    }
    
    /* BESS INFO */
    
    static const BESS_block_t bess_info = {BE32('INFO'), LE32(sizeof(BESS_INFO_t) - sizeof(BESS_block_t))};
    
    if (file->write(file, &bess_info, sizeof(bess_info)) != sizeof(bess_info)) {
        goto error;
    }
    
    if (file->write(file, gb->rom + 0x134, 0x10) != 0x10) {
        goto error;
    }
    
    if (file->write(file, gb->rom + 0x14E, 2) != 2) {
        goto error;
    }
    
    /* BESS CORE */
    
    bess_core.header = (BESS_block_t){BE32('CORE'), LE32(sizeof(bess_core) - sizeof(bess_core.header))};
    bess_core.major = LE16(1);
    bess_core.minor = LE16(1);
    switch (gb->model) {

        case GB_MODEL_DMG_B: bess_core.full_model = BE32('GDB '); break;
        case GB_MODEL_MGB: bess_core.full_model = BE32('GM  '); break;
            
        case GB_MODEL_SGB_NTSC:
        case GB_MODEL_SGB_NTSC_NO_SFC:
            bess_core.full_model = BE32('SN  '); break;
            
        case GB_MODEL_SGB_PAL_NO_SFC:
        case GB_MODEL_SGB_PAL:
            bess_core.full_model = BE32('SP  '); break;
        
        case GB_MODEL_SGB2_NO_SFC:
        case GB_MODEL_SGB2:
            bess_core.full_model = BE32('S2  '); break;
 
        case GB_MODEL_CGB_0: bess_core.full_model = BE32('CC0 '); break;
        case GB_MODEL_CGB_A: bess_core.full_model = BE32('CCA '); break;
        case GB_MODEL_CGB_B: bess_core.full_model = BE32('CCB '); break;
        case GB_MODEL_CGB_C: bess_core.full_model = BE32('CCC '); break;
        case GB_MODEL_CGB_D: bess_core.full_model = BE32('CCD '); break;
        case GB_MODEL_CGB_E: bess_core.full_model = BE32('CCE '); break;
        case GB_MODEL_AGB_A: bess_core.full_model = BE32('CAA '); break;
    }
    
    bess_core.pc = LE16(gb->pc);
    bess_core.af = LE16(gb->af);
    bess_core.bc = LE16(gb->bc);
    bess_core.de = LE16(gb->de);
    bess_core.hl = LE16(gb->hl);
    bess_core.sp = LE16(gb->sp);
    
    bess_core.ime = gb->ime;
    bess_core.ie = gb->interrupt_enable;
    bess_core.execution_mode = 0;
    if (gb->halted) {
        bess_core.execution_mode = 1;
    }
    else if (gb->stopped) {
        bess_core.execution_mode = 2;
    }
    
    memcpy(bess_core.io_registers, gb->io_registers, sizeof(gb->io_registers));
    bess_core.io_registers[GB_IO_DIV] = gb->div_counter >> 8;
    bess_core.io_registers[GB_IO_BANK] = gb->boot_rom_finished;
    bess_core.io_registers[GB_IO_KEY1] |= gb->cgb_double_speed? 0x80 : 0;
    bess_core.hram.size = LE32(sizeof(gb->hram));
    bess_core.hram.offset = LE32(hram_offset + offsetof(GB_gameboy_t, hram) - GB_SECTION_OFFSET(hram));
    bess_core.oam.size = LE32(sizeof(gb->oam));
    bess_core.oam.offset = LE32(video_offset + offsetof(GB_gameboy_t, oam) - GB_SECTION_OFFSET(video));
    if (GB_is_cgb(gb)) {
        bess_core.background_palettes.size = LE32(sizeof(gb->background_palettes_data));
        bess_core.background_palettes.offset = LE32(video_offset + offsetof(GB_gameboy_t, background_palettes_data) - GB_SECTION_OFFSET(video));
        bess_core.object_palettes.size = LE32(sizeof(gb->object_palettes_data));
        bess_core.object_palettes.offset = LE32(video_offset + offsetof(GB_gameboy_t, object_palettes_data) - GB_SECTION_OFFSET(video));
    }
    
    if (file->write(file, &bess_core, sizeof(bess_core)) != sizeof(bess_core)) {
        goto error;
    }
        
    /* BESS XOAM */
    
    BESS_XOAM_t bess_xoam = {0,};
    bess_xoam.header = (BESS_block_t){BE32('XOAM'), LE32(sizeof(bess_xoam) - sizeof(bess_xoam.header))};
    if (GB_is_cgb(gb)) {
        memcpy(bess_xoam.extra_oam, gb->extra_oam, sizeof(bess_xoam.extra_oam));
    }
    
    if (file->write(file, &bess_xoam, sizeof(bess_xoam)) != sizeof(bess_xoam)) {
        goto error;
    }
    
    save_bess_mbc_block(gb, file);
    if (gb->cartridge_type->has_rtc) {
        if (gb->cartridge_type ->mbc_type == GB_TPP1) {
            BESS_TPP1_t bess_tpp1 = {0,};
            bess_tpp1.header = (BESS_block_t){BE32('TPP1'), LE32(sizeof(bess_tpp1) - sizeof(bess_tpp1.header))};
            
            bess_tpp1.last_rtc_second = LE64(gb->last_rtc_second);
            unrolled for (unsigned i = 4; i--;) {
                bess_tpp1.real_rtc_data[i] = gb->rtc_real.data[i ^ 3];
                bess_tpp1.latched_rtc_data[i] = gb->rtc_latched.data[i ^ 3];
            }
            bess_tpp1.mr4 = gb->tpp1_mr4;

            if (file->write(file, &bess_tpp1, sizeof(bess_tpp1)) != sizeof(bess_tpp1)) {
                goto error;
            }
        }
        else if (gb->cartridge_type ->mbc_type != GB_HUC3) {
            BESS_RTC_t bess_rtc = {0,};
            bess_rtc.header = (BESS_block_t){BE32('RTC '), LE32(sizeof(bess_rtc) - sizeof(bess_rtc.header))};
            bess_rtc.real.seconds = gb->rtc_real.seconds;
            bess_rtc.real.minutes = gb->rtc_real.minutes;
            bess_rtc.real.hours = gb->rtc_real.hours;
            bess_rtc.real.days = gb->rtc_real.days;
            bess_rtc.real.high = gb->rtc_real.high;
            bess_rtc.latched.seconds = gb->rtc_latched.seconds;
            bess_rtc.latched.minutes = gb->rtc_latched.minutes;
            bess_rtc.latched.hours = gb->rtc_latched.hours;
            bess_rtc.latched.days = gb->rtc_latched.days;
            bess_rtc.latched.high = gb->rtc_latched.high;
            bess_rtc.last_rtc_second = LE64(gb->last_rtc_second);
            if (file->write(file, &bess_rtc, sizeof(bess_rtc)) != sizeof(bess_rtc)) {
                goto error;
            }
        }
        else {
            BESS_HUC3_t bess_huc3 = {0,};
            bess_huc3.header = (BESS_block_t){BE32('HUC3'), LE32(sizeof(bess_huc3) - sizeof(bess_huc3.header))};

            bess_huc3.data = (GB_huc3_rtc_time_t) {
                LE64(gb->last_rtc_second),
                LE16(gb->huc3.minutes),
                LE16(gb->huc3.days),
                LE16(gb->huc3.alarm_minutes),
                LE16(gb->huc3.alarm_days),
                gb->huc3.alarm_enabled,
            };
            if (file->write(file, &bess_huc3, sizeof(bess_huc3)) != sizeof(bess_huc3)) {
                goto error;
            }
        }
    }
    
    bool needs_sgb_padding = false;
    if (gb->sgb) {
        /* BESS SGB */
        if (gb->sgb->disable_commands) {
            needs_sgb_padding = true;
        }
        else {
            BESS_SGB_t bess_sgb = {{BE32('SGB '), LE32(sizeof(bess_sgb) - sizeof(bess_sgb.header))}, };
            
            bess_sgb.border_tiles    = (BESS_buffer_t){LE32(sizeof(gb->sgb->pending_border.tiles)),
                                                       LE32(sgb_offset + offsetof(GB_sgb_t, pending_border.tiles))};
            bess_sgb.border_tilemap  = (BESS_buffer_t){LE32(sizeof(gb->sgb->pending_border.map)),
                                                       LE32(sgb_offset + offsetof(GB_sgb_t, pending_border.map))};
            bess_sgb.border_palettes = (BESS_buffer_t){LE32(sizeof(gb->sgb->pending_border.palette)),
                                                       LE32(sgb_offset + offsetof(GB_sgb_t, pending_border.palette))};
            
            bess_sgb.active_palettes = (BESS_buffer_t){LE32(sizeof(gb->sgb->effective_palettes)),
                                                       LE32(sgb_offset + offsetof(GB_sgb_t, effective_palettes))};
            bess_sgb.ram_palettes    = (BESS_buffer_t){LE32(sizeof(gb->sgb->ram_palettes)),
                                                       LE32(sgb_offset + offsetof(GB_sgb_t, ram_palettes))};
            bess_sgb.attribute_map   = (BESS_buffer_t){LE32(sizeof(gb->sgb->attribute_map)),
                                                       LE32(sgb_offset + offsetof(GB_sgb_t, attribute_map))};
            bess_sgb.attribute_files = (BESS_buffer_t){LE32(sizeof(gb->sgb->attribute_files)),
                                                       LE32(sgb_offset + offsetof(GB_sgb_t, attribute_files))};
            
            bess_sgb.multiplayer_state = (gb->sgb->player_count << 4) | gb->sgb->current_player;
            if (file->write(file, &bess_sgb, sizeof(bess_sgb)) != sizeof(bess_sgb)) {
                goto error;
            }
        }
    }
    
    /* BESS END */
    
    static const BESS_block_t bess_end = {BE32('END '), 0};

    if (file->write(file, &bess_end, sizeof(bess_end)) != sizeof(bess_end)) {
        goto error;
    }
    
    if (needs_sgb_padding) {
        static const uint8_t sgb_padding[sizeof(BESS_SGB_t)] = {0,};
        file->write(file, sgb_padding, sizeof(sgb_padding));
    }
    
    /* BESS Footer */
    
    if (file->write(file, &bess_footer, sizeof(bess_footer)) != sizeof(bess_footer)) {
        goto error;
    }
    
    errno = 0;
error:
    return errno;
}

int GB_save_state(GB_gameboy_t *gb, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        GB_log(gb, "Could not open save state: %s.\n", strerror(errno));
        return errno;
    }
    virtual_file_t file = {
        .write = file_write,
        .seek = file_seek,
        .tell = file_tell,
        .file = f,
    };
    int ret = save_state_internal(gb, &file, true);
    fclose(f);
    return ret;
}

void GB_save_state_to_buffer(GB_gameboy_t *gb, uint8_t *buffer)
{
    virtual_file_t file = {
        .write = buffer_write,
        .seek = buffer_seek,
        .tell = buffer_tell,
        .buffer = (uint8_t *)buffer,
        .position = 0,
    };
    
    save_state_internal(gb, &file, true);
    assert(file.position == GB_get_save_state_size(gb));
}

void GB_save_state_to_buffer_no_bess(GB_gameboy_t *gb, uint8_t *buffer)
{
    virtual_file_t file = {
        .write = buffer_write,
        .seek = buffer_seek,
        .tell = buffer_tell,
        .buffer = (uint8_t *)buffer,
        .position = 0,
    };
    
    save_state_internal(gb, &file, false);
    assert(file.position == GB_get_save_state_size_no_bess(gb));
}

static bool read_section(virtual_file_t *file, void *dest, uint32_t size, bool fix_broken_windows_saves)
{
    uint32_t saved_size = 0;
    if (file->read(file, &saved_size, sizeof(size)) != sizeof(size)) {
        return false;
    }
    
    if (fix_broken_windows_saves) {
        if (saved_size < 4) {
            return false;
        }
        saved_size -= 4;
        file->seek(file, 4, SEEK_CUR);
    }
    
    if (saved_size <= size) {
        if (file->read(file, dest, saved_size) != saved_size) {
            return false;
        }
    }
    else {
        if (file->read(file, dest, size) != size) {
            return false;
        }
        file->seek(file, saved_size - size, SEEK_CUR);
    }
    
    return true;
}

static void read_bess_buffer(const BESS_buffer_t *buffer, virtual_file_t *file, uint8_t *dest, size_t max_size)
{
    size_t pos = file->tell(file);
    file->seek(file, LE32(buffer->offset), SEEK_SET);
    file->read(file, dest, MIN(LE32(buffer->size), max_size));
    file->seek(file, pos, SEEK_SET);
    
    if (LE32(buffer->size) < max_size) {
        memset(dest + LE32(buffer->size), 0, max_size - LE32(buffer->size));
    }
}

static int load_bess_save(GB_gameboy_t *gb, virtual_file_t *file, bool is_sameboy)
{
    char emulator_name[65] = {0,};
    file->seek(file, -sizeof(BESS_footer_t), SEEK_END);
    BESS_footer_t footer = {0, };
    file->read(file, &footer, sizeof(footer));
    if (footer.magic != BE32('BESS')) {
        // Not a BESS file
        if (!is_sameboy) {
            GB_log(gb, "The file is not a save state, or is from an incompatible operating system.\n");
        }
        return -1;
    }
    
    GB_gameboy_t save;
    GB_init(&save, gb->model);
    save.cartridge_type = gb->cartridge_type;
    
    file->seek(file, LE32(footer.start_offset), SEEK_SET);
    bool found_core = false;
    BESS_CORE_t core = {0,};
    bool found_sgb = false;
    BESS_SGB_t sgb = {0,};
    while (true) {
        BESS_block_t block;
        if (file->read(file, &block, sizeof(block)) != sizeof(block)) goto error;
        switch (block.magic) {
            case BE32('CORE'):
                if (found_core) goto parse_error;
                found_core = true;
                if (LE32(block.size) > sizeof(core) - sizeof(block)) {
                    if (file->read(file, &core.header + 1, sizeof(core) - sizeof(block)) != sizeof(core) - sizeof(block)) goto error;
                    file->seek(file, LE32(block.size) - (sizeof(core) - sizeof(block)), SEEK_CUR);
                }
                else {
                    if (file->read(file, &core.header + 1, LE32(block.size)) != LE32(block.size)) goto error;
                }
                
                if (core.major != LE16(1)) {
                    GB_log(gb, "This save state uses an incompatible version of the BESS specification");
                    GB_free(&save);
                    return -1;
                }
                
                switch (core.family) {
                    case 'C':
                        if (!GB_is_cgb(&save)) goto wrong_model;
                        break;
                    case 'S':
                        if (!GB_is_sgb(&save)) goto wrong_model;
                        break;
                    case 'G':
                        if (GB_is_cgb(&save) || GB_is_sgb(&save)) goto wrong_model;
                        break;
                    default:
                    wrong_model:
                        GB_log(gb, "The save state is for a different model. Try changing the emulated model.\n");
                        GB_free(&save);
                        return -1;
                }

                
                save.pc = LE16(core.pc);
                save.af = LE16(core.af);
                save.bc = LE16(core.bc);
                save.de = LE16(core.de);
                save.hl = LE16(core.hl);
                save.sp = LE16(core.sp);
                
                save.ime = core.ime;
                save.interrupt_enable = core.ie;
                
                save.halted = core.execution_mode == 1;
                save.stopped = core.execution_mode == 2;
                              
                // Done early for compatibility with 0.14.x
                GB_write_memory(&save, 0xFF00 + GB_IO_SVBK, core.io_registers[GB_IO_SVBK]);
                // CPU related
                
                // Determines DMG mode
                GB_write_memory(&save, 0xFF00 + GB_IO_KEY0, core.io_registers[GB_IO_KEY0]);
                save.boot_rom_finished = core.io_registers[GB_IO_BANK];
                GB_write_memory(&save, 0xFF00 + GB_IO_KEY1, core.io_registers[GB_IO_KEY1]);
                if (save.cgb_mode) {
                    save.cgb_double_speed = core.io_registers[GB_IO_KEY1] & 0x80;
                    save.object_priority = GB_OBJECT_PRIORITY_INDEX;
                }
                else {
                    save.object_priority = GB_OBJECT_PRIORITY_X;
                }
                
                // Timers, Joypad and Serial
                GB_write_memory(&save, 0xFF00 + GB_IO_JOYP, core.io_registers[GB_IO_JOYP]);
                GB_write_memory(&save, 0xFF00 + GB_IO_SB, core.io_registers[GB_IO_SB]);
                save.io_registers[GB_IO_SC] = core.io_registers[GB_IO_SC];
                save.div_counter = core.io_registers[GB_IO_DIV] << 8;
                GB_write_memory(&save, 0xFF00 + GB_IO_TIMA, core.io_registers[GB_IO_TIMA]);
                GB_write_memory(&save, 0xFF00 + GB_IO_TMA, core.io_registers[GB_IO_TMA]);
                GB_write_memory(&save, 0xFF00 + GB_IO_TAC, core.io_registers[GB_IO_TAC]);
                
                // APU
                GB_write_memory(&save, 0xFF00 + GB_IO_NR52, core.io_registers[GB_IO_NR52]);
                for (unsigned i = GB_IO_NR10; i < GB_IO_NR52; i++) {
                    uint8_t value = core.io_registers[i];
                    if (i == GB_IO_NR14 || i == GB_IO_NR24 || i == GB_IO_NR34 || i == GB_IO_NR44) {
                        value &= ~0x80;
                    }
                    GB_write_memory(&save, 0xFF00 + i, value);
                }
                
                for (unsigned i = GB_IO_WAV_START; i <= GB_IO_WAV_END; i++) {
                    GB_write_memory(&save, 0xFF00 + i, core.io_registers[i]);
                }
                
                // PPU
                GB_write_memory(&save, 0xFF00 + GB_IO_LCDC, core.io_registers[GB_IO_LCDC]);
                GB_write_memory(&save, 0xFF00 + GB_IO_STAT, core.io_registers[GB_IO_STAT]);
                GB_write_memory(&save, 0xFF00 + GB_IO_SCY, core.io_registers[GB_IO_SCY]);
                GB_write_memory(&save, 0xFF00 + GB_IO_SCX, core.io_registers[GB_IO_SCX]);
                GB_write_memory(&save, 0xFF00 + GB_IO_LYC, core.io_registers[GB_IO_LYC]);
                save.io_registers[GB_IO_DMA] = core.io_registers[GB_IO_DMA];
                GB_write_memory(&save, 0xFF00 + GB_IO_BGP, core.io_registers[GB_IO_BGP]);
                GB_write_memory(&save, 0xFF00 + GB_IO_OBP0, core.io_registers[GB_IO_OBP0]);
                GB_write_memory(&save, 0xFF00 + GB_IO_OBP1, core.io_registers[GB_IO_OBP1]);
                GB_write_memory(&save, 0xFF00 + GB_IO_WX, core.io_registers[GB_IO_WX]);
                GB_write_memory(&save, 0xFF00 + GB_IO_WY, core.io_registers[GB_IO_WY]);
                
                // Other registers
                GB_write_memory(&save, 0xFF00 + GB_IO_VBK, core.io_registers[GB_IO_VBK]);
                GB_write_memory(&save, 0xFF00 + GB_IO_HDMA1, core.io_registers[GB_IO_HDMA1]);
                GB_write_memory(&save, 0xFF00 + GB_IO_HDMA2, core.io_registers[GB_IO_HDMA2]);
                GB_write_memory(&save, 0xFF00 + GB_IO_HDMA3, core.io_registers[GB_IO_HDMA3]);
                GB_write_memory(&save, 0xFF00 + GB_IO_HDMA4, core.io_registers[GB_IO_HDMA4]);
                GB_write_memory(&save, 0xFF00 + GB_IO_RP, core.io_registers[GB_IO_RP]);
                GB_write_memory(&save, 0xFF00 + GB_IO_BGPI, core.io_registers[GB_IO_BGPI]);
                GB_write_memory(&save, 0xFF00 + GB_IO_OBPI, core.io_registers[GB_IO_OBPI]);
                GB_write_memory(&save, 0xFF00 + GB_IO_OPRI, core.io_registers[GB_IO_OPRI]);

                // Interrupts
                GB_write_memory(&save, 0xFF00 + GB_IO_IF, core.io_registers[GB_IO_IF]);
                
                break;
            case BE32('NAME'):
                if (LE32(block.size) > sizeof(emulator_name) - 1) {
                    file->seek(file, LE32(block.size), SEEK_CUR);
                }
                else {
                    file->read(file, emulator_name, LE32(block.size));
                }
                break;
            case BE32('INFO'): {
                BESS_INFO_t bess_info = {0,};
                if (LE32(block.size) != sizeof(bess_info) - sizeof(block)) goto parse_error;
                if (file->read(file, &bess_info.header + 1, LE32(block.size)) != LE32(block.size)) goto error;
                if (memcmp(bess_info.title, gb->rom + 0x134, sizeof(bess_info.title))) {
                    char ascii_title[0x11] = {0,};
                    for (unsigned i = 0; i < 0x10; i++) {
                        if (bess_info.title[i] < 0x20 || bess_info.title[i] > 0x7E) break;
                        ascii_title[i] = bess_info.title[i];
                    }
                    GB_log(gb, "Save state was made on another ROM: '%s'\n", ascii_title);
                }
                else if (memcmp(bess_info.checksum, gb->rom + 0x14E, 2)) {
                    GB_log(gb, "Save state was potentially made on another revision of the same ROM.\n");
                }
                break;
            }
            case BE32('XOAM'):
                if (!found_core) goto parse_error;
                if (LE32(block.size) != 96) goto parse_error;
                file->read(file, save.extra_oam, sizeof(save.extra_oam));
                break;
            case BE32('MBC '):
                if (!found_core) goto parse_error;
                if (LE32(block.size) % 3 != 0) goto parse_error;
                if (LE32(block.size) > 0x1000) goto parse_error; 
                for (unsigned i = LE32(block.size); i > 0;  i -= 3) {
                    BESS_MBC_pair_t pair;
                    file->read(file, &pair, sizeof(pair));
                    if (LE16(pair.address) >= 0x8000 && LE16(pair.address) < 0xA000) goto parse_error;
                    if (LE16(pair.address) >= 0xC000) goto parse_error;
                    GB_write_memory(&save, LE16(pair.address), pair.value);
                }
                break;
            case BE32('RTC '):
                if (!found_core) goto parse_error;
                BESS_RTC_t bess_rtc;
                if (LE32(block.size) != sizeof(bess_rtc) - sizeof(block)) goto parse_error;
                if (file->read(file, &bess_rtc.header + 1, LE32(block.size)) != LE32(block.size)) goto error;
                if (!gb->cartridge_type->has_rtc || gb->cartridge_type->mbc_type != GB_MBC3) break;
                save.rtc_real.seconds = bess_rtc.real.seconds;
                save.rtc_real.minutes = bess_rtc.real.minutes;
                save.rtc_real.hours = bess_rtc.real.hours;
                save.rtc_real.days = bess_rtc.real.days;
                save.rtc_real.high = bess_rtc.real.high;
                save.rtc_latched.seconds = bess_rtc.latched.seconds;
                save.rtc_latched.minutes = bess_rtc.latched.minutes;
                save.rtc_latched.hours = bess_rtc.latched.hours;
                save.rtc_latched.days = bess_rtc.latched.days;
                save.rtc_latched.high = bess_rtc.latched.high;
                if (gb->rtc_mode == GB_RTC_MODE_SYNC_TO_HOST) {
                    save.last_rtc_second = MIN(LE64(bess_rtc.last_rtc_second), time(NULL));
                }
                
                break;
            case BE32('HUC3'):
                if (!found_core) goto parse_error;
                BESS_HUC3_t bess_huc3;
                if (LE32(block.size) != sizeof(bess_huc3) - sizeof(block)) goto parse_error;
                if (file->read(file, &bess_huc3.header + 1, LE32(block.size)) != LE32(block.size)) goto error;
                if (gb->cartridge_type->mbc_type != GB_HUC3) break;
                if (gb->rtc_mode == GB_RTC_MODE_SYNC_TO_HOST) {
                    save.last_rtc_second = MIN(LE64(bess_huc3.data.last_rtc_second), time(NULL));
                }
                save.huc3.minutes = LE16(bess_huc3.data.minutes);
                save.huc3.days = LE16(bess_huc3.data.days);
                save.huc3.alarm_minutes = LE16(bess_huc3.data.alarm_minutes);
                save.huc3.alarm_days = LE16(bess_huc3.data.alarm_days);
                save.huc3.alarm_enabled = bess_huc3.data.alarm_enabled;
                break;
            case BE32('TPP1'):
                if (!found_core) goto parse_error;
                BESS_TPP1_t bess_tpp1;
                if (LE32(block.size) != sizeof(bess_tpp1) - sizeof(block)) goto parse_error;
                if (file->read(file, &bess_tpp1.header + 1, LE32(block.size)) != LE32(block.size)) goto error;
                if (gb->cartridge_type->mbc_type != GB_TPP1) break;
                if (gb->rtc_mode == GB_RTC_MODE_SYNC_TO_HOST) {
                    save.last_rtc_second = MIN(LE64(bess_tpp1.last_rtc_second), time(NULL));
                }
                unrolled for (unsigned i = 4; i--;) {
                    save.rtc_real.data[i ^ 3] = bess_tpp1.real_rtc_data[i];
                    save.rtc_latched.data[i ^ 3] = bess_tpp1.latched_rtc_data[i];
                }
                save.tpp1_mr4 = bess_tpp1.mr4;
                break;
            case BE32('SGB '):
                if (!found_core) goto parse_error;
                if (!gb->sgb) goto parse_error;
                if (LE32(block.size) > sizeof(sgb) - sizeof(block)) {
                    if (file->read(file, &sgb.header + 1, sizeof(sgb) - sizeof(block)) != sizeof(sgb) - sizeof(block)) goto error;
                    file->seek(file, LE32(block.size) - (sizeof(sgb) - sizeof(block)), SEEK_CUR);
                }
                else {
                    if (file->read(file, &sgb.header + 1, LE32(block.size)) != LE32(block.size)) goto error;
                }
                found_sgb = true;
                break;
            case BE32('END '):
                if (!found_core) goto parse_error;
                if (LE32(block.size) != 0) goto parse_error;
                goto done;
            default:
                file->seek(file, LE32(block.size), SEEK_CUR);
                break;
        }
    }
done:
    save.mbc_ram_size = gb->mbc_ram_size;
    memcpy(gb, &save, GB_SECTION_OFFSET(unsaved));
    assert(GB_get_save_state_size(gb) == GB_get_save_state_size(&save));
    GB_free(&save);
    read_bess_buffer(&core.ram, file, gb->ram, gb->ram_size);
    read_bess_buffer(&core.vram, file, gb->vram, gb->vram_size);
    read_bess_buffer(&core.mbc_ram, file, gb->mbc_ram, gb->mbc_ram_size);
    read_bess_buffer(&core.oam, file, gb->oam, sizeof(gb->oam));
    read_bess_buffer(&core.hram, file, gb->hram, sizeof(gb->hram));
    read_bess_buffer(&core.background_palettes, file, gb->background_palettes_data, sizeof(gb->background_palettes_data));
    read_bess_buffer(&core.object_palettes, file, gb->object_palettes_data, sizeof(gb->object_palettes_data));
    if (gb->sgb) {
        memset(gb->sgb, 0, sizeof(*gb->sgb));
        GB_sgb_load_default_data(gb);
        if (gb->boot_rom_finished) {
            gb->sgb->intro_animation = GB_SGB_INTRO_ANIMATION_LENGTH;
            if (!found_sgb) {
                gb->sgb->disable_commands = true;
            }
            else {
                read_bess_buffer(&sgb.border_tiles, file, gb->sgb->border.tiles, sizeof(gb->sgb->border.tiles));
                read_bess_buffer(&sgb.border_tilemap, file, (void *)gb->sgb->border.map, sizeof(gb->sgb->border.map));
                read_bess_buffer(&sgb.border_palettes, file, (void *)gb->sgb->border.palette, sizeof(gb->sgb->border.palette));
                
                read_bess_buffer(&sgb.active_palettes, file, (void *)gb->sgb->effective_palettes, sizeof(gb->sgb->effective_palettes));
                read_bess_buffer(&sgb.ram_palettes, file, (void *)gb->sgb->ram_palettes, sizeof(gb->sgb->ram_palettes));
                read_bess_buffer(&sgb.attribute_map, file, (void *)gb->sgb->attribute_map, sizeof(gb->sgb->attribute_map));
                read_bess_buffer(&sgb.attribute_files, file, (void *)gb->sgb->attribute_files, sizeof(gb->sgb->attribute_files));
                
                gb->sgb->effective_palettes[12] = gb->sgb->effective_palettes[8] =
                gb->sgb->effective_palettes[4] = gb->sgb->effective_palettes[0];
                
                gb->sgb->player_count = sgb.multiplayer_state >> 4;
                gb->sgb->current_player = sgb.multiplayer_state & 0xF;
                if (gb->sgb->player_count > 4 || gb->sgb->player_count == 3 || gb->sgb->player_count == 0) {
                    gb->sgb->player_count = 1;
                    gb->sgb->current_player = 0;
                }
            }
        }
        else {
            // Effectively reset if didn't finish the boot ROM
            gb->pc = 0;
        }
    }
    if (emulator_name[0]) {
        GB_log(gb, "Save state imported from %s.\n", emulator_name);
    }
    else {
        GB_log(gb, "Save state imported from another emulator.\n"); // SameBoy always contains a NAME block
    }
    for (unsigned i = 0; i < 32; i++) {
        GB_palette_changed(gb, false, i * 2);
        GB_palette_changed(gb, true, i * 2);
    }
    return 0;
parse_error:
    errno = -1;
error:
    if (emulator_name[0]) {
        GB_log(gb, "Attempted to import a save state from %s, but the save state is invalid.\n", emulator_name);
    }
    else {
        GB_log(gb, "Attempted to import a save state from a different emulator or incompatible version, but the save state is invalid.\n");
    }
    GB_free(&save);
    sanitize_state(gb);
    return errno;
}

static int load_state_internal(GB_gameboy_t *gb, virtual_file_t *file)
{
    GB_gameboy_t save;
    
    /* Every unread value should be kept the same. */
    memcpy(&save, gb, sizeof(save));
    /* ...Except ram size, we use it to detect old saves with incorrect ram sizes */
    save.ram_size = 0;
    
    bool fix_broken_windows_saves = false;
    
    if (file->read(file, GB_GET_SECTION(&save, header), GB_SECTION_SIZE(header)) != GB_SECTION_SIZE(header)) return errno;
    if (save.magic == 0) {
        /* Potentially legacy, broken Windows save state*/
        
        file->seek(file, 4, SEEK_SET);
        if (file->read(file, GB_GET_SECTION(&save, header), GB_SECTION_SIZE(header)) != GB_SECTION_SIZE(header)) return errno;
        fix_broken_windows_saves = true;
    }
    if (gb->magic != save.magic) {
        return load_bess_save(gb, file, false);
    }
#define READ_SECTION(gb, file, section) read_section(file, GB_GET_SECTION(gb, section), GB_SECTION_SIZE(section), fix_broken_windows_saves)
    if (!READ_SECTION(&save, file, core_state)) return errno ?: EIO;
    if (!READ_SECTION(&save, file, dma       )) return errno ?: EIO;
    if (!READ_SECTION(&save, file, mbc       )) return errno ?: EIO;
    if (!READ_SECTION(&save, file, hram      )) return errno ?: EIO;
    if (!READ_SECTION(&save, file, timing    )) return errno ?: EIO;
    if (!READ_SECTION(&save, file, apu       )) return errno ?: EIO;
    if (!READ_SECTION(&save, file, rtc       )) return errno ?: EIO;
    if (!READ_SECTION(&save, file, video     )) return errno ?: EIO;
#undef READ_SECTION
    
    
    bool attempt_bess = false;
    if (!verify_and_update_state_compatibility(gb, &save, &attempt_bess)) {
        if (attempt_bess) {
            return load_bess_save(gb, file, true);
        }
        return errno;
    }
    
    if (GB_is_hle_sgb(gb)) {
        if (!read_section(file, gb->sgb, sizeof(*gb->sgb), false)) return errno ?: EIO;
    }
    
    memset(gb->mbc_ram + save.mbc_ram_size, 0xFF, gb->mbc_ram_size - save.mbc_ram_size);
    if (file->read(file, gb->mbc_ram, save.mbc_ram_size) != save.mbc_ram_size) {
        return errno ?: EIO;
    }
    
    if (file->read(file, gb->ram, gb->ram_size) != gb->ram_size) {
        return errno ?: EIO;
    }
    
    /* Fix for 0.11 save states that allocate twice the amount of RAM in CGB instances */
    file->seek(file, save.ram_size - gb->ram_size, SEEK_CUR);
    
    if (file->read(file, gb->vram, gb->vram_size) != gb->vram_size) {
        return errno ?: EIO;
    }
    
    size_t orig_ram_size = gb->ram_size;
    memcpy(gb, &save, sizeof(save));
    gb->ram_size = orig_ram_size;
    
    sanitize_state(gb);
    
    return 0;
}

int GB_load_state(GB_gameboy_t *gb, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        GB_log(gb, "Could not open save state: %s.\n", strerror(errno));
        return errno;
    }
    virtual_file_t file = {
        .read = file_read,
        .seek = file_seek,
        .tell = file_tell,
        .file = f,
    };
    int ret = load_state_internal(gb, &file);
    fclose(f);
    return ret;
}

int GB_load_state_from_buffer(GB_gameboy_t *gb, const uint8_t *buffer, size_t length)
{
    virtual_file_t file = {
        .read = buffer_read,
        .seek = buffer_seek,
        .tell = buffer_tell,
        .buffer = (uint8_t *)buffer,
        .position = 0,
        .size = length,
    };
    
    return load_state_internal(gb, &file);
}


bool GB_is_save_state(const char *path)
{
    bool ret = false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint32_t magic = 0;
    fread(&magic, sizeof(magic), 1, f);
    if (magic == state_magic()) {
        ret = true;
        goto exit;
    }
    
    // Legacy corrupted Windows save state
    if (magic == 0) {
        fread(&magic, sizeof(magic), 1, f);
        if (magic == state_magic()) {
            ret = true;
            goto exit;
        }
    }
    
    fseek(f, -sizeof(magic), SEEK_END);
    fread(&magic, sizeof(magic), 1, f);
    if (magic == BE32('BESS')) {
        ret = true;
    }
    
exit:
    fclose(f);
    return ret;
}

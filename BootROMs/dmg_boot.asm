; SameBoy DMG bootstrap ROM

include "sameboot.inc"

SECTION "BootCode", ROM0[$0000]
Start:
; Init stack pointer
    ld sp, $FFFE

; Clear memory VRAM
    ld hl, _VRAM
    xor a

.clearVRAMLoop
    ldi [hl], a
    bit 5, h
    jr z, .clearVRAMLoop

; Init Audio
    ld a, AUDENA_ON
    ldh [rNR52], a
    assert AUDENA_ON == AUDLEN_DUTY_50
    ldh [rNR11], a
    ld a, $F3
    ldh [rNR12], a ; Envelope $F, decreasing, sweep $3
    ldh [rNR51], a ; Channels 1+2+3+4 left, channels 1+2 right
    ld a, $77
    ldh [rNR50], a ; Volume $7, left and right

; Init BG palette
    ld a, %01_01_01_00
    ldh [rBGP], a

; Load logo from ROM.
; A nibble represents a 4-pixels line, 2 bytes represent a 4x4 tile, scaled to 8x8.
; Tiles are ordered left to right, top to bottom.
    ld de, NintendoLogo
    ld hl, _VRAM + $10 ; This is where we load the tiles in VRAM

.loadLogoLoop
    ld a, [de] ; Read 2 rows
    ld b, a
    call DoubleBitsAndWriteRow
    call DoubleBitsAndWriteRow
    inc de
    ld a, e
    xor LOW(NintendoLogoEnd)
    jr nz, .loadLogoLoop

; Load trademark symbol
    ld de, TrademarkSymbol
    ld c, TrademarkSymbolEnd - TrademarkSymbol
.loadTrademarkSymbolLoop:
    ld a, [de]
    inc de
    ldi [hl], a
    inc hl
    dec c
    jr nz, .loadTrademarkSymbolLoop

; Set up tilemap
    ld a, $19                           ; Trademark symbol tile ID
    ld [_SCRN0 + 8 * SCRN_VX_B + 16], a ; ... put in the superscript position
    ld hl, _SCRN0 + 9 * SCRN_VX_B + 15  ; Bottom right corner of the logo
    ld c, 12                            ; Tiles in a logo row
.tilemapLoop
    dec a
    jr z, .tilemapDone
    ldd [hl], a
    dec c
    jr nz, .tilemapLoop
    ld l, $0F ; Jump to top row
    jr .tilemapLoop
.tilemapDone

    ld a, 30
    ldh [rSCY], a

    ; Turn on LCD
    ld a, LCDCF_ON | LCDCF_BLK01 | LCDCF_BGON
    ldh [rLCDC], a

    ld d, LOW(-119)
    ld c, 15

.animate
    call WaitFrame
    ld a, d
    sra a
    sra a
    ldh [rSCY], a
    ld a, d
    add c
    ld d, a
    ld a, c
    cp 8
    jr nz, .noPaletteChange
    ld a, %10_10_10_00
    ldh [rBGP], a
.noPaletteChange
    dec c
    jr nz, .animate
    ld a, %11_11_11_00
    ldh [rBGP], a

    ; Play first sound
    ld a, $83
    call PlaySound
    ld b, 5
    call WaitBFrames
    ; Play second sound
    ld a, $C1
    call PlaySound



; Wait ~1 second
    ld b, 60
    call WaitBFrames

; Set registers to match the original DMG boot
IF DEF(MGB)
    lb hl, BOOTUP_A_MGB, %10110000
ELSE
    lb hl, BOOTUP_A_DMG, %10110000
ENDC
    push hl
    pop af
    ld hl, HeaderChecksum
    lb bc, 0, LOW(rNR13) ; $0013
    lb de, 0, $D8        ; $00D8

; Boot the game
    jp BootGame


DoubleBitsAndWriteRow:
; Double the most significant 4 bits, b is shifted by 4
    ld a, 4
    ld c, 0
.doubleCurrentBit
    sla b
    push af
    rl c
    pop af
    rl c
    dec a
    jr nz, .doubleCurrentBit
    ld a, c
; Write as two rows
    ldi [hl], a
    inc hl
    ldi [hl], a
    inc hl
    ret

WaitFrame:
    push hl
    ld hl, rIF
    res 0, [hl]
.wait
    bit 0, [hl]
    jr z, .wait
    pop hl
    ret

WaitBFrames:
    call WaitFrame
    dec b
    jr nz, WaitBFrames
    ret

PlaySound:
    ldh [rNR13], a
    ld a, AUDHIGH_RESTART | $7
    ldh [rNR14], a
    ret


TrademarkSymbol:
    pusho
    opt b.X
    db %..XXXX..
    db %.X....X.
    db %X.XXX..X
    db %X.X..X.X
    db %X.XXX..X
    db %X.X..X.X
    db %.X....X.
    db %..XXXX..
    popo
TrademarkSymbolEnd:

SECTION "BootGame", ROM0[$00FE]
BootGame:
    ldh [rBANK], a ; unmap boot ROM

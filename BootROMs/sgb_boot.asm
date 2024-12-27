; SameBoy SGB bootstrap ROM

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

; Init BG palette to white
    ld a, %00_00_00_00
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

    ; Turn on LCD
    ld a, LCDCF_ON | LCDCF_BLK01 | LCDCF_BGON
    ldh [rLCDC], a

    ld a, $F1 ; Packet magic, increases by 2 for every packet
    ldh [hCommand], a
    ld hl, NintendoLogo ; Header start

    xor a
    ld c, a ; JOYP

.sendCommand
    xor a
    ldh [c], a
    ld a, $30
    ldh [c], a

    ldh a, [hCommand]
    call SendByte
    push hl

    ld b, 14
    ld d, 0
.checksumLoop
    call ReadHeaderByte
    add d
    ld d, a
    dec b
    jr nz, .checksumLoop

    ; Send checksum
    call SendByte
    pop hl

    ld b, 14
.sendLoop
    call ReadHeaderByte
    call SendByte
    dec b
    jr nz, .sendLoop

    ; Done bit
    ld a, $20
    ldh [c], a
    ld a, $30
    ldh [c], a

    ; Wait 4 frames
    ld e, 4
    ld a, 1
    ldh [rIE], a
    xor a
.waitLoop
    ldh [rIF], a
    halt
    nop
    dec e
    jr nz, .waitLoop
    ldh [rIE], a

    ; Update command
    ldh a, [hCommand]
    add 2
    ldh [hCommand], a

    ld a, $58
    cp l
    jr nz, .sendCommand

    ; Write to sound registers for DMG compatibility
    ld c, LOW(rNR13)
    ld a, $C1
    ldh [c], a
    inc c
    ld a, $7
    ldh [c], a

    ; Init BG palette
    ld a, %11_11_11_00
    ldh [rBGP], a

; Set registers to match the original SGB boot
IF DEF(SGB2)
    ld a, BOOTUP_A_MGB
ELSE
    ld a, BOOTUP_A_DMG
ENDC
    ld hl, $C060

; Boot the game
    jp BootGame

ReadHeaderByte:
    ld a, $4F
    cp l
    jr c, .zero
    ld a, [hli]
    ret
.zero:
    inc hl
    xor a
    ret

SendByte:
    ld e, a
    ld d, 8
.loop
    ld a, $10
    rr e
    jr c, .zeroBit
    add a ; 10 -> 20
.zeroBit
    ldh [c], a
    ld a, $30
    ldh [c], a
    dec d
    ret z
    jr .loop

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
    ldh [rBANK], a

SECTION "HRAM", HRAM[_HRAM]
hCommand:
    ds 1

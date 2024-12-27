; SameBoy CGB bootstrap ROM

include "sameboot.inc"

SECTION "BootCode", ROM0[$0000]
Start:
; Init stack pointer
    ld sp, $FFFE

; Clear memory VRAM
    call ClearMemoryVRAM

; Clear OAM
    ld h, HIGH(_OAMRAM)
    ld c, sizeof_OAM_ATTRS * OAM_COUNT
.clearOAMLoop
    ldi [hl], a
    dec c
    jr nz, .clearOAMLoop

IF !DEF(CGB0)
; Init waveform
    ld c, 16
    ld hl, _AUD3WAVERAM
.waveformLoop
    ldi [hl], a
    cpl
    dec c
    jr nz, .waveformLoop
ENDC

; Clear chosen input palette
    ldh [hInputPalette], a
; Clear title checksum
    ldh [hTitleChecksum], a

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
    ld a, %11_11_11_00
    ldh [rBGP], a

; Load logo from ROM.
; A nibble represents a 4-pixels line, 2 bytes represent a 4x4 tile, scaled to 8x8.
; Tiles are ordered left to right, top to bottom.
; These tiles are not used, but are required for DMG compatibility. This is done
; by the original CGB Boot ROM as well.
    ld de, NintendoLogo
    ld hl, _VRAM + $10 ; This is where we load the tiles in VRAM

.loadLogoLoop
    ld a, [de] ; Read 2 rows
    ld b, a
    call DoubleBitsAndWriteRowTwice
    inc de
    ld a, e
    cp LOW(NintendoLogoEnd)
    jr nz, .loadLogoLoop
    call ReadTrademarkSymbol

; Clear the second VRAM bank
    ld a, 1
    ldh [rVBK], a
    call ClearMemoryVRAM
    call LoadTileset

    ld b, 3
IF DEF(FAST)
    xor a
    ldh [rVBK], a
ELSE
; Load Tilemap
    ld hl, _SCRN0 + 6 * SCRN_VX_B + 2
    ld d, 3
    ld a, 8

.tilemapLoop
    ld c, 16

.tilemapRowLoop

    call .write_with_palette

    ; Repeat the 3 tiles common between E and B. This saves 27 bytes after
    ; compression, with a cost of 17 bytes of code.
    push af
    sub $20
    sub $3
    jr nc, .notspecial
    add $20
    call .write_with_palette
    dec c
.notspecial
    pop af

    add d ; d = 3 for SameBoy logo, d = 1 for Nintendo logo
    dec c
    jr nz, .tilemapRowLoop
    sub 44
    push de
    ld de, $10
    add hl, de
    pop de
    dec b
    jr nz, .tilemapLoop

    dec d
    jr z, .endTilemap
    dec d

    ld a, $38
    ld l, $A7
    lb bc, 1, 7 ; $0107
    jr .tilemapRowLoop

.write_with_palette
    push af
    ; Switch to second VRAM Bank
    ld a, 1
    ldh [rVBK], a
    ld [hl], 8
    ; Switch to back first VRAM Bank
    xor a
    ldh [rVBK], a
    pop af
    ldi [hl], a
    ret
.endTilemap
ENDC

    ; Expand Palettes
    ld de, AnimationColors
    ld c, 8
    ld hl, hBgPalettes
    xor a
.expandPalettesLoop:
    cpl
    ; One white
    ld [hli], a
    ld [hli], a

    ; Mixed with white
    ld a, [de]
    inc e
    or $20
    ld b, a

    ld a, [de]
    dec e
    or $84
    rra
    rr b
    ld [hl], b
    inc l
    ld [hli], a

    ; One black
    xor a
    ld [hli], a
    ld [hli], a

    ; One color
    ld a, [de]
    inc e
    ld [hli], a
    ld a, [de]
    inc e
    ld [hli], a

    xor a
    dec c
    jr nz, .expandPalettesLoop

    call LoadPalettesFromHRAM

    ; Turn on LCD
    ld a, LCDCF_ON | LCDCF_BLK01 | LCDCF_BGON
    ldh [rLCDC], a

IF !DEF(FAST)
    call DoIntroAnimation

    ld a, 48 ; frames to wait after playing the chime
    ldh [hWaitLoopCounter], a
    ld b, 4 ; frames to wait before playing the chime
    call WaitBFrames

    ; Play first sound
    ld a, $83
    call PlaySound
    ld b, 5
    call WaitBFrames
    ; Play second sound
    ld a, $C1
    call PlaySound

.waitLoop
    call GetInputPaletteIndex
    call WaitFrame
    ld hl, hWaitLoopCounter
    dec [hl]
    jr nz, .waitLoop
ELSE
    ld a, $C1
    call PlaySound
ENDC
    call Preboot
IF DEF(AGB)
    inc b
ENDC
    jr BootGame

HDMAData:
MACRO hdma_data ; source, destination, length
    db HIGH(\1), LOW(\1)
    db HIGH(\2), LOW(\2)
    db (\3)
ENDM
    hdma_data _RAMBANK, _SCRN0 + 5 * SCRN_VX_B + 0, 18
    hdma_data _RAMBANK, _VRAM, 64

SECTION "BootGame", ROM0[$00FE]
BootGame:
    ldh [rBANK], a ; unmap boot ROM

SECTION "BootData", ROM0[$0200]
; Game Palettes Data
TitleChecksums:
    db $00 ; Default
    db $88 ; ALLEY WAY
    db $16 ; YAKUMAN
    db $36 ; BASEBALL, (Game and Watch 2)
    db $D1 ; TENNIS
    db $DB ; TETRIS
    db $F2 ; QIX
    db $3C ; DR.MARIO
    db $8C ; RADARMISSION
    db $92 ; F1RACE
    db $3D ; YOSSY NO TAMAGO
    db $5C ;
    db $58 ; X
    db $C9 ; MARIOLAND2
    db $3E ; YOSSY NO COOKIE
    db $70 ; ZELDA
    db $1D ;
    db $59 ;
    db $69 ; TETRIS FLASH
    db $19 ; DONKEY KONG
    db $35 ; MARIO'S PICROSS
    db $A8 ;
    db $14 ; POKEMON RED, (GAMEBOYCAMERA G)
    db $AA ; POKEMON GREEN
    db $75 ; PICROSS 2
    db $95 ; YOSSY NO PANEPON
    db $99 ; KIRAKIRA KIDS
    db $34 ; GAMEBOY GALLERY
    db $6F ; POCKETCAMERA
    db $15 ;
    db $FF ; BALLOON KID
    db $97 ; KINGOFTHEZOO
    db $4B ; DMG FOOTBALL
    db $90 ; WORLD CUP
    db $17 ; OTHELLO
    db $10 ; SUPER RC PRO-AM
    db $39 ; DYNABLASTER
    db $F7 ; BOY AND BLOB GB2
    db $F6 ; MEGAMAN
    db $A2 ; STAR WARS-NOA
    db $49 ;
    db $4E ; WAVERACE
    db $43 ;
    db $68 ; LOLO2
    db $E0 ; YOSHI'S COOKIE
    db $8B ; MYSTIC QUEST
    db $F0 ;
    db $CE ; TOPRANKINGTENNIS
    db $0C ; MANSELL
    db $29 ; MEGAMAN3
    db $E8 ; SPACE INVADERS
    db $B7 ; GAME&WATCH
    db $86 ; DONKEYKONGLAND95
    db $9A ; ASTEROIDS/MISCMD
    db $52 ; STREET FIGHTER 2
    db $01 ; DEFENDER/JOUST
    db $9D ; KILLERINSTINCT95
    db $71 ; TETRIS BLAST
    db $9C ; PINOCCHIO
    db $BD ;
    db $5D ; BA.TOSHINDEN
    db $6D ; NETTOU KOF 95
    db $67 ;
    db $3F ; TETRIS PLUS
    db $6B ; DONKEYKONGLAND 3
; For these games, the 4th letter is taken into account
FirstChecksumWithDuplicate:
    ; Let's play hangman!
    db $B3 ; ???[B]????????
    db $46 ; SUP[E]R MARIOLAND
    db $28 ; GOL[F]
    db $A5 ; SOL[A]RSTRIKER
    db $C6 ; GBW[A]RS
    db $D3 ; KAE[R]UNOTAMENI
    db $27 ; ???[B]????????
    db $61 ; POK[E]MON BLUE
    db $18 ; DON[K]EYKONGLAND
    db $66 ; GAM[E]BOY GALLERY2
    db $6A ; DON[K]EYKONGLAND 2
    db $BF ; KID[ ]ICARUS
    db $0D ; TET[R]IS2
    db $F4 ; ???[-]????????
    db $B3 ; MOG[U]RANYA
    db $46 ; ???[R]????????
    db $28 ; GAL[A]GA&GALAXIAN
    db $A5 ; BT2[R]AGNAROKWORLD
    db $C6 ; KEN[ ]GRIFFEY JR
    db $D3 ; ???[I]????????
    db $27 ; MAG[N]ETIC SOCCER
    db $61 ; VEG[A]S STAKES
    db $18 ; ???[I]????????
    db $66 ; MIL[L]I/CENTI/PEDE
    db $6A ; MAR[I]O & YOSHI
    db $BF ; SOC[C]ER
    db $0D ; POK[E]BOM
    db $F4 ; G&W[ ]GALLERY
    db $B3 ; TET[R]IS ATTACK
ChecksumsEnd:

PalettePerChecksum:
MACRO palette_index ; palette[, flags]
    IF _NARG == 1
        db (\1)
    ELSE
        db (\1) | (\2) ; flag $80 means game requires DMG boot tilemap
    ENDC
ENDM
    palette_index  0 ; Default Palette
    palette_index  4 ; ALLEY WAY
    palette_index  5 ; YAKUMAN
    palette_index 35 ; BASEBALL, (Game and Watch 2)
    palette_index 34 ; TENNIS
    palette_index  3 ; TETRIS
    palette_index 31 ; QIX
    palette_index 15 ; DR.MARIO
    palette_index 10 ; RADARMISSION
    palette_index  5 ; F1RACE
    palette_index 19 ; YOSSY NO TAMAGO
    palette_index 36 ;
    palette_index  7, $80 ; X
    palette_index 37 ; MARIOLAND2
    palette_index 30 ; YOSSY NO COOKIE
    palette_index 44 ; ZELDA
    palette_index 21 ;
    palette_index 32 ;
    palette_index 31 ; TETRIS FLASH
    palette_index 20 ; DONKEY KONG
    palette_index  5 ; MARIO'S PICROSS
    palette_index 33 ;
    palette_index 13 ; POKEMON RED, (GAMEBOYCAMERA G)
    palette_index 14 ; POKEMON GREEN
    palette_index  5 ; PICROSS 2
    palette_index 29 ; YOSSY NO PANEPON
    palette_index  5 ; KIRAKIRA KIDS
    palette_index 18 ; GAMEBOY GALLERY
    palette_index  9 ; POCKETCAMERA
    palette_index  3 ;
    palette_index  2 ; BALLOON KID
    palette_index 26 ; KINGOFTHEZOO
    palette_index 25 ; DMG FOOTBALL
    palette_index 25 ; WORLD CUP
    palette_index 41 ; OTHELLO
    palette_index 42 ; SUPER RC PRO-AM
    palette_index 26 ; DYNABLASTER
    palette_index 45 ; BOY AND BLOB GB2
    palette_index 42 ; MEGAMAN
    palette_index 45 ; STAR WARS-NOA
    palette_index 36 ;
    palette_index 38 ; WAVERACE
    palette_index 26, $80 ;
    palette_index 42 ; LOLO2
    palette_index 30 ; YOSHI'S COOKIE
    palette_index 41 ; MYSTIC QUEST
    palette_index 34 ;
    palette_index 34 ; TOPRANKINGTENNIS
    palette_index  5 ; MANSELL
    palette_index 42 ; MEGAMAN3
    palette_index  6 ; SPACE INVADERS
    palette_index  5 ; GAME&WATCH
    palette_index 33 ; DONKEYKONGLAND95
    palette_index 25 ; ASTEROIDS/MISCMD
    palette_index 42 ; STREET FIGHTER 2
    palette_index 42 ; DEFENDER/JOUST
    palette_index 40 ; KILLERINSTINCT95
    palette_index  2 ; TETRIS BLAST
    palette_index 16 ; PINOCCHIO
    palette_index 25 ;
    palette_index 42 ; BA.TOSHINDEN
    palette_index 42 ; NETTOU KOF 95
    palette_index  5 ;
    palette_index  0 ; TETRIS PLUS
    palette_index 39 ; DONKEYKONGLAND 3
    palette_index 36 ;
    palette_index 22 ; SUPER MARIOLAND
    palette_index 25 ; GOLF
    palette_index  6 ; SOLARSTRIKER
    palette_index 32 ; GBWARS
    palette_index 12 ; KAERUNOTAMENI
    palette_index 36 ;
    palette_index 11 ; POKEMON BLUE
    palette_index 39 ; DONKEYKONGLAND
    palette_index 18 ; GAMEBOY GALLERY2
    palette_index 39 ; DONKEYKONGLAND 2
    palette_index 24 ; KID ICARUS
    palette_index 31 ; TETRIS2
    palette_index 50 ;
    palette_index 17 ; MOGURANYA
    palette_index 46 ;
    palette_index  6 ; GALAGA&GALAXIAN
    palette_index 27 ; BT2RAGNAROKWORLD
    palette_index  0 ; KEN GRIFFEY JR
    palette_index 47 ;
    palette_index 41 ; MAGNETIC SOCCER
    palette_index 41 ; VEGAS STAKES
    palette_index  0 ;
    palette_index  0 ; MILLI/CENTI/PEDE
    palette_index 19 ; MARIO & YOSHI
    palette_index 34 ; SOCCER
    palette_index 23 ; POKEBOM
    palette_index 18 ; G&W GALLERY
    palette_index 29 ; TETRIS ATTACK

Dups4thLetterArray:
    db "BEFAARBEKEK R-URAR INAILICE R"

; We assume the last three arrays fit in the same $100 byte page!

PaletteCombinations:
MACRO palette_comb ; Obj0, Obj1, Bg
    db (\1) * 8, (\2) * 8, (\3) *8
ENDM
MACRO raw_palette_comb ; Obj0, Obj1, Bg
    db (\1) * 2, (\2) * 2, (\3) * 2
ENDM
    palette_comb  4,  4, 29 ;  0, Right + A
    palette_comb 18, 18, 18 ;  1, Right
    palette_comb 20, 20, 20 ;  2
    palette_comb 24, 24, 24 ;  3, Down + A
    palette_comb  9,  9,  9 ;  4
    palette_comb  0,  0,  0 ;  5, Up
    palette_comb 27, 27, 27 ;  6, Right + B
    palette_comb  5,  5,  5 ;  7, Left + B
    palette_comb 12, 12, 12 ;  8, Down
    palette_comb 26, 26, 26 ;  9
    palette_comb 16,  8,  8 ; 10
    palette_comb  4, 28, 28 ; 11
    palette_comb  4,  2,  2 ; 12
    palette_comb  3,  4,  4 ; 13
    palette_comb  4, 29, 29 ; 14
    palette_comb 28,  4, 28 ; 15
    palette_comb  2, 17,  2 ; 16
    palette_comb 16, 16,  8 ; 17
    palette_comb  4,  4,  7 ; 18
    palette_comb  4,  4, 18 ; 19
    palette_comb  4,  4, 20 ; 20
    palette_comb 19, 19,  9 ; 21
    raw_palette_comb 4 * 4 - 1, 4 * 4 - 1, 11 * 4 ; 22
    palette_comb 17, 17,  2 ; 23
    palette_comb  4,  4,  2 ; 24
    palette_comb  4,  4,  3 ; 25
    palette_comb 28, 28,  0 ; 26
    palette_comb  3,  3,  0 ; 27
    palette_comb  0,  0,  1 ; 28, Up + B
    palette_comb 18, 22, 18 ; 29
    palette_comb 20, 22, 20 ; 30
    palette_comb 24, 22, 24 ; 31
    palette_comb 16, 22,  8 ; 32
    palette_comb 17,  4, 13 ; 33
    raw_palette_comb 28 * 4 - 1, 0 * 4, 14 * 4 ; 34
    raw_palette_comb 28 * 4 - 1, 4 * 4, 15 * 4 ; 35
    raw_palette_comb 19 * 4, 23 * 4 - 1, 9 * 4 ; 36
    palette_comb 16, 28, 10 ; 37
    palette_comb  4, 23, 28 ; 38
    palette_comb 17, 22,  2 ; 39
    palette_comb  4,  0,  2 ; 40, Left + A
    palette_comb  4, 28,  3 ; 41
    palette_comb 28,  3,  0 ; 42
    palette_comb  3, 28,  4 ; 43, Up + A
    palette_comb 21, 28,  4 ; 44
    palette_comb  3, 28,  0 ; 45
    palette_comb 25,  3, 28 ; 46
    palette_comb  0, 28,  8 ; 47
    palette_comb  4,  3, 28 ; 48, Left
    palette_comb 28,  3,  6 ; 49, Down + B
    palette_comb  4, 28, 29 ; 50
    ; SameBoy "Exclusives"
    palette_comb 30, 30, 30 ; 51, Right + A + B, CGA
    palette_comb 31, 31, 31 ; 52, Left + A + B, DMG LCD
    palette_comb 28,  4,  1 ; 53, Up + A + B
    palette_comb  0,  0,  2 ; 54, Down + A + B

Palettes:
    dw $7FFF, $32BF, $00D0, $0000 ;  0
    dw $639F, $4279, $15B0, $04CB ;  1
    dw $7FFF, $6E31, $454A, $0000 ;  2
    dw $7FFF, $1BEF, $0200, $0000 ;  3
    dw $7FFF, $421F, $1CF2, $0000 ;  4
    dw $7FFF, $5294, $294A, $0000 ;  5
    dw $7FFF, $03FF, $012F, $0000 ;  6
    dw $7FFF, $03EF, $01D6, $0000 ;  7
    dw $7FFF, $42B5, $3DC8, $0000 ;  8
    dw $7E74, $03FF, $0180, $0000 ;  9
    dw $67FF, $77AC, $1A13, $2D6B ; 10
    dw $7ED6, $4BFF, $2175, $0000 ; 11
    dw $53FF, $4A5F, $7E52, $0000 ; 12
    dw $4FFF, $7ED2, $3A4C, $1CE0 ; 13
    dw $03ED, $7FFF, $255F, $0000 ; 14
    dw $036A, $021F, $03FF, $7FFF ; 15
    dw $7FFF, $01DF, $0112, $0000 ; 16
    dw $231F, $035F, $00F2, $0009 ; 17
    dw $7FFF, $03EA, $011F, $0000 ; 18
    dw $299F, $001A, $000C, $0000 ; 19
    dw $7FFF, $027F, $001F, $0000 ; 20
    dw $7FFF, $03E0, $0206, $0120 ; 21
    dw $7FFF, $7EEB, $001F, $7C00 ; 22
    dw $7FFF, $3FFF, $7E00, $001F ; 23
    dw $7FFF, $03FF, $001F, $0000 ; 24
    dw $03FF, $001F, $000C, $0000 ; 25
    dw $7FFF, $033F, $0193, $0000 ; 26
    dw $0000, $4200, $037F, $7FFF ; 27
    dw $7FFF, $7E8C, $7C00, $0000 ; 28
    dw $7FFF, $1BEF, $6180, $0000 ; 29
    ; SameBoy "Exclusives"
    dw $7FFF, $7FEA, $7D5F, $0000 ; 30, CGA 1
    dw $4778, $3290, $1D87, $0861 ; 31, DMG LCD

KeyCombinationPalettes:
MACRO palette_comb_id ; PaletteCombinations ID
    db (\1) * 3
ENDM
    palette_comb_id  1 ;  1, Right
    palette_comb_id 48 ;  2, Left
    palette_comb_id  5 ;  3, Up
    palette_comb_id  8 ;  4, Down
    palette_comb_id  0 ;  5, Right + A
    palette_comb_id 40 ;  6, Left + A
    palette_comb_id 43 ;  7, Up + A
    palette_comb_id  3 ;  8, Down + A
    palette_comb_id  6 ;  9, Right + B
    palette_comb_id  7 ; 10, Left + B
    palette_comb_id 28 ; 11, Up + B
    palette_comb_id 49 ; 12, Down + B
    ; SameBoy "Exclusives"
    palette_comb_id 51 ; 13, Right + A + B
    palette_comb_id 52 ; 14, Left + A + B
    palette_comb_id 53 ; 15, Up + A + B
    palette_comb_id 54 ; 16, Down + A + B

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

SameBoyLogo:
    incbin "SameBoyLogo.pb12"


AnimationColors:
    dw $7FFF ; White
    dw $774F ; Cyan
    dw $22C7 ; Green
    dw $039F ; Yellow
    dw $017D ; Orange
    dw $241D ; Red
    dw $6D38 ; Purple
IF DEF(AGB)
    dw $6D60 ; Blue
ELSE
    dw $5500 ; Blue
ENDC

AnimationColorsEnd:

; Helper Functions
DoubleBitsAndWriteRowTwice:
    call .twice
.twice
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
    call GetInputPaletteIndex
    call WaitFrame
    dec b
    jr nz, WaitBFrames
    ret

PlaySound:
    ldh [rNR13], a
    ld a, $87
    ldh [rNR14], a
    ret

ClearMemoryVRAM:
    ld hl, _VRAM
; Clear from HL to HL | 0x2000
ClearMemoryPage:
    xor a
    ldi [hl], a
    bit 5, h
    jr z, ClearMemoryPage
    ret

ReadTwoTileLines:
    call ReadTileLine
; c = $F0 for even lines, $0F for odd lines.
ReadTileLine:
    ld a, [de]
    and c
    ld b, a
    inc e
    inc e
    ld a, [de]
    dec e
    dec e
    and c
    swap a
    or b
    bit 0, c
    jr z, .dontSwap
    swap a
.dontSwap
    inc hl
    ldi [hl], a
    swap c
    ret


ReadCGBLogoHalfTile:
    call .do_twice
.do_twice
    call ReadTwoTileLines
    inc e
    ld a, e
    ret

; LoadTileset using PB12 codec, 2020 Jakub Kądziołka
; (based on PB8 codec, 2019 Damian Yerrick)

LoadTileset:
    ld hl, SameBoyLogo         ; source
    ld de, _VRAM + $80 - 1     ; destination
    ld c, (128 * 24) / (8 * 8) ; length
.refill
    ; Register map for PB12 decompression
    ; HL: source address in boot ROM
    ; DE: destination address in VRAM
    ; A: Current literal value
    ; B: Repeat bits, terminated by 1000...
    ; Source address in HL lets the repeat bits go straight to B,
    ; bypassing A and avoiding spilling registers to the stack.
    ld b, [hl]
    dec b
    jr z, .sameboyLogoEnd
    inc b
    inc hl

    ; Shift a 1 into lower bit of shift value.  Once this bit
    ; reaches the carry, B becomes 0 and the byte is over
    scf
    rl b

.loop
    ; If not a repeat, load a literal byte
    jr c, .simple_repeat
    sla b
    jr c, .shifty_repeat
    ld a, [hli]
    jr .got_byte
.shifty_repeat
    sla b
    jr nz, .no_refill_during_shift
    ld b, [hl] ; see above. Also, no, factoring it out into a callable
    inc hl ; routine doesn't save bytes, even with conditional calls
    scf
    rl b
.no_refill_during_shift
    ld c, a
    jr nc, .shift_left
    srl a
    db $FE ; eat the `add a` with `cp d8`
.shift_left
    add a
    sla b
    jr c, .go_and
    or c
    db $FE ; eat the `and c` with `cp d8`
.go_and
    and c
    jr .got_byte
.simple_repeat
    sla b
    jr c, .got_byte
    ; far repeat
    dec de
    ld a, [de]
    inc de
.got_byte
    inc de
    ld [de], a
    sla b
    jr nz, .loop
    jr .refill

; End PB12 decoding.  The rest uses HL as the destination
.sameboyLogoEnd
    ld h, d
    ld l, $80

; Copy (unresized) ROM logo
    ld de, NintendoLogo
.CGBROMLogoLoop
    ld c, $F0
    call ReadCGBLogoHalfTile
    add a, 22
    ld e, a
    call ReadCGBLogoHalfTile
    sub a, 22
    ld e, a
    cp $1C
    jr nz, .CGBROMLogoLoop
    inc hl
    ; fallthrough
ReadTrademarkSymbol:
    ld de, TrademarkSymbol
    ld c, TrademarkSymbolEnd - TrademarkSymbol
.loadTrademarkSymbolLoop:
    ld a, [de]
    inc de
    ldi [hl], a
    inc hl
    dec c
    jr nz, .loadTrademarkSymbolLoop
    ret

DoIntroAnimation:
    ; Animate the intro
    ld a, 1
    ldh [rVBK], a
    ld d, 26
.animationLoop
    ld b, 2
    call WaitBFrames
    ld hl, _SCRN0 + 6 * SCRN_VX_B + 0
    ld c, 3 ; Row count
.loop
    ld a, [hl]
    cp $F ; Already blue
    jr z, .nextTile
    inc [hl]
    and $7
    jr z, .nextLine ; Changed a white tile, go to next line
.nextTile
    inc hl
    jr .loop
.nextLine
    ld a, l
    or $1F
    ld l, a
    inc hl
    dec c
    jr nz, .loop
    dec d
    jr nz, .animationLoop
    ret

Preboot:
IF !DEF(FAST)
    ld b, 32 ; 32 times to fade
.fadeLoop
    ld c, (hBgPalettesEnd - hBgPalettes) / 2 ; 32 colors to fade
    ld hl, hBgPalettes
.frameLoop
    push bc

    ; Brighten Color
    ld a, [hli]
    ld e, a
    ld a, [hld]
    ld d, a
    ; RGB(1,1,1)
    ld bc, $0421

   ; Is blue maxed?
    ld a, e
    and $1F
    cp $1F
    jr nz, .blueNotMaxed
    dec c
.blueNotMaxed

    ; Is green maxed?
    ld a, e
    cp $E0
    jr c, .greenNotMaxed
    ld a, d
    and $3
    cp $3
    jr nz, .greenNotMaxed
    res 5, c
.greenNotMaxed

    ; Is red maxed?
    ld a, d
    and $7C
    cp $7C
    jr nz, .redNotMaxed
    res 2, b
.redNotMaxed

    ld a, e
    add c
    ld [hli], a
    ld a, d
    adc b
    ld [hli], a
    pop bc

    dec c
    jr nz, .frameLoop

    call WaitFrame
    call LoadPalettesFromHRAM
    call WaitFrame
    dec b
    jr nz, .fadeLoop
ENDC
    ld a, 2
    ldh [rSVBK], a
    ; Clear RAM Bank 2 (Like the original boot ROM)
    ld hl, _RAMBANK
    call ClearMemoryPage
    inc a
    call ClearVRAMViaHDMA
    call _ClearVRAMViaHDMA
    call ClearVRAMViaHDMA ; A = $40, so it's bank 0
    xor a
    ldh [rSVBK], a
    cpl
    ldh [rJOYP], a

    ; Final values for CGB mode
    ld d, a
    ld e, c
    ld l, $0D

    ld a, [CGBFlag]
    bit 7, a
    call z, EmulateDMG
    bit 7, a

    ldh [rKEY0], a ; write CGB compatibility byte, CGB mode
    ldh a, [hTitleChecksum]
    ld b, a

    jr z, .skipDMGForCGBCheck
    ldh a, [hInputPalette]
    and a
    jr nz, .emulateDMGForCGBGame
.skipDMGForCGBCheck
IF DEF(AGB)
    ; Set registers to match the original AGB-CGB boot
    ; AF = $1100, C = 0
    xor a
    ld c, a
    add a, BOOTUP_A_CGB
    ld h, c
    ; B is set to BOOTUP_B_AGB (1) after ret
ELSE
    ; Set registers to match the original CGB boot
    ; AF = $1180, C = 0
    xor a
    ld c, a
    ld a, BOOTUP_A_CGB
    ld h, c
    ; B is set to the title checksum (BOOTUP_B_CGB, 0)
ENDC
    ret

.emulateDMGForCGBGame
    call EmulateDMG
    ldh [rKEY0], a ; write $04, DMG emulation mode
    ld a, $1
    ret

GetKeyComboPalette:
    ld hl, KeyCombinationPalettes - 1 ; Return value is 1-based, 0 means nothing down
    ld c, a
    ld b, 0
    add hl, bc
    ld a, [hl]
    ret

EmulateDMG:
    ld a, 1
    ldh [rOPRI], a ; DMG Emulation sprite priority
    call GetPaletteIndex
    bit 7, a
    call nz, LoadDMGTilemap
    res 7, a
    ld b, a
    add b
    add b
    ld b, a
    ldh a, [hInputPalette]
    and a
    jr z, .nothingDown
    call GetKeyComboPalette
    jr .paletteFromKeys
.nothingDown
    ld a, b
.paletteFromKeys
    call WaitFrame
    call LoadPalettesFromIndex
    ld a, 4
    ; Set the final values for DMG mode
    ld de, 8
    ld l, $7C
    ret

GetPaletteIndex:
    ld hl, OldLicenseeCode
    ld a, [hl]
    cp $33
    jr z, .newLicensee
    dec a ; 1 = Nintendo
    jr nz, .notNintendo
    jr .doChecksum
.newLicensee
    ld l, LOW(NewLicenseeCode)
    ld a, [hli]
    cp "0"
    jr nz, .notNintendo
    ld a, [hl]
    cp "1"
    jr nz, .notNintendo

.doChecksum
    ld l, LOW(Title)
    ld c, 16
    xor a
.checksumLoop
    add [hl]
    inc l
    dec c
    jr nz, .checksumLoop
    ldh [hTitleChecksum], a
    ld b, a

    ; c = 0
    ld hl, TitleChecksums

.searchLoop
    ld a, l
    sub LOW(ChecksumsEnd) ; use sub to zero out a
    ret z
    ld a, [hli]
    cp b
    jr nz, .searchLoop

    ; We might have a match, Do duplicate/4th letter check
    ld a, l
    sub FirstChecksumWithDuplicate - TitleChecksums + 1
    jr c, .match ; Does not have a duplicate, must be a match!
    ; Has a duplicate; check 4th letter
    push hl
    ld a, l
    add Dups4thLetterArray - FirstChecksumWithDuplicate - 1 ; -1 since hl was incremented
    ld l, a
    ld a, [hl]
    pop hl
    ld c, a
    ld a, [Title + 3] ; Get 4th letter
    cp c
    jr nz, .searchLoop ; Not a match, continue

.match
    ld a, l
    add PalettePerChecksum - TitleChecksums - 1; -1 since hl was incremented
    ld l, a
    ld a, b
    ldh [hTitleChecksum], a
    ld a, [hl]
    ret

.notNintendo
    xor a
    ret

; optimizations in callers rely on this returning with b = 0
GetPaletteCombo:
    ld hl, PaletteCombinations
    ld b, 0
    ld c, a
    add hl, bc
    ret

LoadPalettesFromIndex: ; a = index of combination
    call GetPaletteCombo

    ; Obj Palettes
    ld e, 0
.loadObjPalette
    ld a, [hli]
    push hl
    ld hl, Palettes
    ; b is already 0
    ld c, a
    add hl, bc
    ld d, 4 * 2
    ld c, LOW(rOBPI)
    call LoadPalettes
    pop hl
    bit OAMB_BANK1, e
    jr nz, .loadBGPalette
    ld e, OAMF_BANK1
    jr .loadObjPalette
.loadBGPalette
    ;BG Palette
    ld c, [hl]
    ; b is already 0
    ld hl, Palettes
    add hl, bc
    ld d, 8
    jr LoadBGPalettes

LoadPalettesFromHRAM:
    ld hl, hBgPalettes
    ld d, hBgPalettesEnd - hBgPalettes
LoadBGPalettes:
    ld e, 0
    ld c, LOW(rBGPI)
LoadPalettes:
    ld a, $80
    or e
    ldh [c], a
    inc c
.loop
    ld a, [hli]
    ldh [c], a
    dec d
    jr nz, .loop
    ret

ClearVRAMViaHDMA:
    ldh [rVBK], a
    ld hl, HDMAData
_ClearVRAMViaHDMA:
    call WaitFrame ; Wait for vblank
    ld c, LOW(rHDMA1)
    ld b, 5
.loop
    ld a, [hli]
    ldh [c], a
    inc c
    dec b
    jr nz, .loop
    ret

; clobbers AF and HL
GetInputPaletteIndex:
    ld a, P1F_GET_DPAD
    ldh [rJOYP], a
    ldh a, [rJOYP]
    cpl
    and $F
    ret z ; No direction keys pressed, no palette

    ld l, 0
.directionLoop
    inc l
    rra
    jr nc, .directionLoop

    ; c = 1: Right, 2: Left, 3: Up, 4: Down

    ld a, P1F_GET_BTN
    ldh [rJOYP], a
    ldh a, [rJOYP]
    cpl
    rla
    rla
    and $C
    add l
    ld l, a
    ldh a, [hInputPalette]
    cp l
    ret z ; No change, don't load
    ld a, l
    ldh [hInputPalette], a
    ; Slide into change Animation Palette

ChangeAnimationPalette:
    push bc
    push de
    call GetKeyComboPalette
    call GetPaletteCombo
    inc l
    inc l
    ld c, [hl]
    ld hl, Palettes + 1
    add hl, bc
    ld a, [hld]
    cp $7F ; Is white color?
    jr nz, .isWhite
    inc hl
    inc hl
.isWhite
    push af
    ld a, [hli]

    push hl
    ld hl, hBgPalettes ; First color, all palettes
    call ReplaceColorInAllPalettes
    ld l, LOW(hBgPalettes + 2)  ; Second color, all palettes
    call ReplaceColorInAllPalettes
    pop hl
    ldh [hBgPalettes + 6], a ; Fourth color, first palette

    ld a, [hli]
    push hl
    ld hl, hBgPalettes + 1 ; First color, all palettes
    call ReplaceColorInAllPalettes
    ld l, LOW(hBgPalettes + 3) ; Second color, all palettes
    call ReplaceColorInAllPalettes
    pop hl
    ldh [hBgPalettes + 7], a ; Fourth color, first palette

    pop af
    jr z, .isNotWhite
    inc hl
    inc hl
.isNotWhite
    ; Mixing code by ISSOtm
    ldh a, [hBgPalettes + 7 * 8 + 2]
    and ~$21
    ld b, a
    ld a, [hli]
    and ~$21
    add a, b
    ld b, a
    ld a, [hBgPalettes + 7 * 8 + 3]
    res 2, a ; and ~$04, but not touching carry
    ld c, [hl]
    res 2, c ; and ~$04, but not touching carry
    adc a, c
    rra ; Carry sort of "extends" the accumulator, we're bringing that bit back home
    ld [hBgPalettes + 7 * 8 + 3], a
    ld a, b
    rra
    ld [hBgPalettes + 7 * 8 + 2], a
    dec l

    ld a, [hli]
    ldh [hBgPalettes + 7 * 8 + 6], a ; Fourth color, 7th palette
    ld a, [hli]
    ldh [hBgPalettes + 7 * 8 + 7], a ; Fourth color, 7th palette

    ld a, [hli]
    ldh [hBgPalettes + 4], a ; Third color, first palette
    ld a, [hli]
    ldh [hBgPalettes + 5], a ; Third color, first palette


    call WaitFrame
    call LoadPalettesFromHRAM
    ; Delay the wait loop while the user is selecting a palette
    ld a, 48
    ldh [hWaitLoopCounter], a
    pop de
    pop bc
    ret

ReplaceColorInAllPalettes:
    ld de, 8
    ld c, e
.loop
    ld [hl], a
    add hl, de
    dec c
    jr nz, .loop
    ret

LoadDMGTilemap:
    push af
    call WaitFrame
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
    pop af
    ret

BootEnd:
IF BootEnd > $0900
    FAIL "BootROM overflowed: {BootEnd}"
ENDC
    ds $100 + $800 - @ ; Ensure that the ROM is padded up to standard size.

SECTION "HRAM", HRAM[_HRAM]
hTitleChecksum:
    ds 1
hBgPalettes:
    ds 8 * 4 * 2
hBgPalettesEnd:
hInputPalette:
    ds 1
hWaitLoopCounter:
    ds 1

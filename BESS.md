# BESS – Best Effort Save State 1.0

## Motivation

BESS is a save state format specification designed to allow different emulators, as well as majorly different versions of the same emulator, to import save states from other BESS-compliant save states. BESS works by appending additional, implementation-agnostic information about the emulation state. This allows a single save state file to be read as both a fully-featured, implementation specific save state which includes detailed timing information; and as a portable "best effort" save state that represents a state accurately enough to be restored in casual use-cases.

## Specification

Every integer used in the BESS specification is stored in Little Endian encoding.

### BESS footer

BESS works by appending a detectable footer at the end of an existing save state format. The footer uses the following format:

| Offset from end of file | Content                                               |
|-------------------------|-------------------------------------------------------|
| -8                      | Offset to the first BESS Block, from the file's start |
| -4                      | The ASCII string 'BESS'                               |

### BESS blocks

BESS uses a block format where each block contains the following header:

| Offset | Content                               |
|--------|-----------------------------------------------------------|
| 0      | A four-letter ASCII identifier                            |
| 4      | Length of the block as a 32-bit integer, excluding header |

Every block is followed by another block, until the END block is reached. If an implementation encounters an unsupported block, it should be completely ignored (Should not have any effect and should not trigger a failure). 

#### NAME block

The NAME block uses the `'NAME'` identifier, and is an optional block that contains the name of the emulator that created this save state. While optional, it is highly recommended to be included in every implementation – it allows the user to know which emulator and version is compatible with the native save state format contained in this file. When used, this block should come first.

The length of the NAME block is variable, and it only contains the name and version of the originating emulator in ASCII.


#### INFO block

The INFO block uses the `'INFO'` identifier, and is an optional block that contains information about the ROM this save state originates from. When used, this block should come before `CORE` but after `NAME`. This block is 0x12 bytes long, and it follows this structure:

| Offset | Content                                          |
|--------|--------------------------------------------------|
| 0x00   | Bytes 0x134-0x143 from the ROM (Title)           |
| 0x10   | Bytes 0x14E-0x14F from the ROM (Global checksum) |

#### CORE block

The CORE block uses the `'CORE'` identifier, and is a required block that contains both core state information, as well as basic information about the BESS version used. This block must be the first block, unless the `NAME` or `INFO` blocks exist then it must come directly after them. An implementation should not enforce block order on blocks unknown to it for future compatibility.

The length of the CORE block is 0xD0 bytes, but implementations are expected to ignore any excess bytes. Following the BESS block header, the structure is as follows:

| Offset | Content                                |
|--------|----------------------------------------|
| 0x00   | Major BESS version as a 16-bit integer |
| 0x02   | Minor BESS version as a 16-bit integer |

Both major and minor versions should be 1. Implementations are expected to reject incompatible majors, but still attempt to read newer minor versions.

| Offset | Content                                |
|--------|----------------------------------------|
| 0x04   | A four-character ASCII model identifier |

BESS uses a four-character string to identify Game Boy models:

 * The first letter represents mutually-incompatible families of models and is required. The allowed values are `'G'` for the original Game Boy family, `'S'` for the Super Game Boy family, and `'C'` for the Game Boy Color and Advance family.
* The second letter represents a specific model within the family, and is optional (If an implementation does not distinguish between specific models in a family, a space character may be used). The allowed values for family G are `'D'` for DMG and `'M'` for MGB; the allowed values for family S are `'N'` for NTSC, `'P'` for PAL, and `'2'` for SGB2; and the allowed values for family C are `'C'` for CGB, and `'A'` for the various GBA line models.
* The third letter represents a specific CPU revision within a model, and is optional (If an implementation does not distinguish between revisions, a space character may be used). The allowed values for model GD (DMG) are `'0'` and `'A'`, through `'C'`; the allowed values for model CC (CGB) are `'0'` and `'A'`, through `'E'`; the allowed values for model CA (AGB, AGS, GBP) are `'0'`, `'A'` and `'B'`; and for every other model this value must be a space character.
* The last character is used for padding and must be a space character.

For example; `'GD  '` represents a DMG of an unspecified revision, `'S   '` represents some model of the SGB family, and `'CCE '` represent a CGB using CPU revision E.

| Offset | Content                                                |
|--------|--------------------------------------------------------|
| 0x08   | The value of the PC register                           |
| 0x0A   | The value of the AF register                           |
| 0x0C   | The value of the BC register                           |
| 0x0E   | The value of the DE register                           |
| 0x10   | The value of the HL register                           |
| 0x12   | The value of the SP register                           |
| 0x14   | The value of IME (0 or 1)                              |
| 0x15   | The value of the IE register                           |
| 0x16   | Execution state (0 = running; 1 = halted; 2 = stopped) |
| 0x17   | Reserved, must be 0                                    |
| 0x18   | The values of every memory-mapped register (128 bytes) |

The values of memory-mapped registers should be written 'as-is' to memory as if the actual ROM wrote them, with the following exceptions and note:
* Unused registers have Don't-Care values which should be ignored
* Unused register bits have Don't-Care values which should be ignored
* If the model is CGB or newer, the value of KEY0 (FF4C) must be valid as it determines DMG mode
    * Bit 2 determines DMG mode. A value of 0x04 usually denotes DMG mode, while a value of `0x80` usually denotes CGB mode.
* Object priority is derived from KEY0 (FF4C) instead of OPRI (FF6C) because OPRI can be modified after booting, but only the value of OPRI during boot ROM execution takes effect
* If a register doesn't exist on the emulated model (For example, KEY0 (FF4C) on a DMG), its value should be ignored.
* BANK (FF50) should be 0 if the boot ROM is still mapped, and 1 otherwise, and must be valid.
* Implementations should not start a serial transfer when writing the value of SB
* Similarly, no value of NRx4 should trigger a sound pulse on save state load
* And similarly again, implementations should not trigger DMA transfers when writing the values of DMA or HDMA5
* The value store for DIV will be used to set the internal divisor to `DIV << 8`
* Implementation should apply care when ordering the write operations (For example, writes to NR52 must come before writes to the other APU registers)

| Offset | Content                                                            |
|--------|--------------------------------------------------------------------|
| 0x98   | The size of RAM (32-bit integer)                                   |
| 0x9C   | The offset of RAM from file start (32-bit integer)                 |
| 0xA0   | The size of VRAM (32-bit integer)                                  |
| 0xA4   | The offset of VRAM from file start (32-bit integer)                |
| 0xA8   | The size of MBC RAM (32-bit integer)                               |
| 0xAC   | The offset of MBC RAM from file start (32-bit integer)             |
| 0xB0   | The size of OAM (=0xA0, 32-bit integer)                            |
| 0xB4   | The offset of OAM from file start (32-bit integer)                 |
| 0xB8   | The size of HRAM (=0x7F, 32-bit integer)                           |
| 0xBC   | The offset of HRAM from file start (32-bit integer)                |
| 0xC0   | The size of background palettes (=0x40 or 0, 32-bit integer)       |
| 0xC4   | The offset of background palettes from file start (32-bit integer) |
| 0xC8   | The size of object palettes (=0x40 or 0, 32-bit integer)           |
| 0xCC   | The offset of object palettes from file start (32-bit integer)     |

The contents of large buffers are stored outside of BESS structure so data from an implementation's native save state format can be reused. The offsets are absolute offsets from the save state file's start. Background and object palette sizes must be 0 for models prior to Game Boy Color.

An implementation needs handle size mismatches gracefully. For example, if too large MBC RAM size is specified, the superfluous data should be ignored. On the other hand, if a too small VRAM size is specified (For example, if it's a save state from an emulator emulating a CGB in DMG mode, and it didn't save the second CGB VRAM bank), the implementation is expected to set that extra bank to all zeros.

#### XOAM block

The XOAM block uses the `'XOAM'` identifier, and is an optional block that contains the data of extra OAM (addresses `0xFEA0-0xFEFF`). This block length must be `0x60`. Implementations that do not emulate this extra range are free to ignore the excess bytes, and to not create this block.


#### MBC block

The MBC block uses the `'MBC '` identifier, and is an optional block that is only used when saving states of ROMs that use an MBC. The length of this block is variable and must be divisible by 3.

This block contains an MBC-specific number of 3-byte-long pairs that represent the values of each MBC register. For example, for MBC5 the contents would look like:

| Offset | Content                               |
|--------|---------------------------------------|
| 0x0    | The value 0x0000 as a 16-bit integer  |
| 0x2    | 0x0A if RAM is enabled, 0 otherwise   |
| 0x3    | The value 0x2000 as a 16-bit integer  |
| 0x5    | The lower 8 bits of the ROM bank      |
| 0x6    | The value 0x3000 as a 16-bit integer  |
| 0x8    | The bit 9 of the ROM bank             |
| 0x9    | The value 0x4000 as a 16-bit integer  |
| 0xB    | The current RAM bank                  |

An implementation should parse this block as a series of writes to be made. Values outside the `0x0000-0x7FFF` and `0xA000-0xBFFF`  ranges are not allowed. Implementations must perform the writes in order (i.e. not reverse, sorted, or any other transformation on their order)

#### RTC block
The RTC block uses the `'RTC '` identifier, and is an optional block that is used while emulating an MBC3 with an RTC. The contents of this block are identical to 64-bit RTC saves from VBA, which are also used by SameBoy and different emulators such as BGB.

The length of this block is 0x30 bytes long and it follows the following structure:

| Offset | Content                                                                |
|--------|------------------------------------------------------------------------|
| 0x00   | Current seconds (1 byte), followed by 3 bytes of padding               |
| 0x04   | Current minutes (1 byte), followed by 3 bytes of padding               |
| 0x08   | Current hours (1 byte), followed by 3 bytes of padding                 |
| 0x0C   | Current days (1 byte), followed by 3 bytes of padding                  |
| 0x10   | Current high/overflow/running (1 byte), followed by 3 bytes of padding |
| 0x14   | Latched seconds (1 byte), followed by 3 bytes of padding               |
| 0x18   | Latched minutes (1 byte), followed by 3 bytes of padding               |
| 0x1C   | Latched hours (1 byte), followed by 3 bytes of padding                 |
| 0x20   | Latched days (1 byte), followed by 3 bytes of padding                  |
| 0x24   | Latched high/overflow/running (1 byte), followed by 3 bytes of padding |
| 0x28   | UNIX timestamp at the time of the save state (64-bit)                  |

#### HUC3 block
The HUC3 block uses the `'HUC3'` identifier, and is an optional block that is used while emulating an HuC3 cartridge to store RTC and alarm information. The contents of this block are identical to HuC3 RTC saves from SameBoy.

The length of this block is 0x11 bytes long and it follows the following structure:

| Offset | Content                                               |
|--------|-------------------------------------------------------|
| 0x00   | UNIX timestamp at the time of the save state (64-bit) |
| 0x08   | RTC minutes (16-bit)                                  |
| 0x0A   | RTC days (16-bit)                                     |
| 0x0C   | Scheduled alarm time minutes (16-bit)                 |
| 0x0E   | Scheduled alarm time days (16-bit)                    |
| 0x10   | Alarm enabled flag (8-bits, either 0 or 1)            |

#### TPP1 block
The TPP1 block uses the `'TPP1'` identifier, and is an optional block that is used while emulating a TPP1 cartridge to store RTC information. This block can be omitted if the ROM header does not specify the inclusion of a RTC.

The length of this block is 0x11 bytes long and it follows the following structure:

| Offset | Content                                               |
|--------|-------------------------------------------------------|
| 0x00   | UNIX timestamp at the time of the save state (64-bit) |
| 0x08   | The current RTC data (4 bytes)                        |
| 0x0C   | The latched RTC data (4 bytes)                        |
| 0x10   | The value of the MR4 register (8-bits)                |


#### MBC7 block
The MBC7 block uses the `'MBC7'` identifier, and is an optional block that is used while emulating an MBC7 cartridge to store the EEPROM communication state and motion control state.

The length of this block is 0xA bytes long and it follows the following structure:

| Offset | Content                                               |
|--------|-------------------------------------------------------|
| 0x00   | Flags (8-bits)                                        |
| 0x01   | Argument bits left (8-bits)                           |
| 0x02   | Current EEPROM command (16-bits)                      |
| 0x04   | Pending bits to read (16-bits)                        |
| 0x06   | Latched gyro X value (16-bits)                        |
| 0x08   | Latched gyro Y value (16-bits)                        |

The meaning of the individual bits in flags are:
 * Bit 0: Latch ready; set after writing `0x55` to `0xAX0X` and reset after writing `0xAA` to `0xAX1X`
 * Bit 1: EEPROM DO line
 * Bit 2: EEPROM DI line
 * Bit 3: EEPROM CLK line
 * Bit 4: EEPROM CS line
 * Bit 5: EEPROM write enable; set after an `EWEN` command, reset after an `EWDS` command
 * Bits 6-7: Unused.

The current EEPROM command field has bits pushed to its LSB first, padded with zeros. For example, if the ROM clocked a single `1` bit, this field should contain `0b1`; if the ROM later clocks a `0` bit, this field should contain `0b10`.

If the currently transmitted command has an argument, the "Argument bits left" field should contain the number argument bits remaining. Otherwise, it should contain 0.

The "Pending bits to read" field contains the pending bits waiting to be shifted into the DO signal, MSB first, padded with ones.

#### SGB block

The SGB block uses the `'SGB '` identifier, and is an optional block that is only used while emulating an SGB or SGB2 *and* SGB commands enabled. Implementations must not save this block on other models or when SGB commands are disabled, and should assume SGB commands are disabled if this block is missing.

The length of this block is 0x39 bytes, but implementations should allow and ignore excess data in this block for extensions. The block follows the following structure:

| Offset | Content                                                                                                                  |
|--------|--------------------------------------------------------------------------------------------------------------------------|
| 0x00   | The size of the border tile data (=0x2000, 32-bit integer)                                                               |
| 0x04   | The offset of the border tile data (SNES tile format, 32-bit integer)                                                    |
| 0x08   | The size of the border tilemap (=0x800, 32-bit integer)                                                                  |
| 0x0C   | The offset of the border tilemap (LE 16-bit sequences, 32-bit integer)                                                   |
| 0x10   | The size of the border palettes (=0x80, 32-bit integer)                                                                  |
| 0x14   | The offset of the border palettes (LE 16-bit sequences, 32-bit integer)                                                  |
| 0x18   | The size of active colorization palettes (=0x20, 32-bit integer)                                                         |
| 0x1C   | The offset of the active colorization palettes (LE 16-bit sequences, 32-bit integer)                                     |
| 0x20   | The size of RAM colorization palettes (=0x1000, 32-bit integer)                                                          |
| 0x24   | The offset of the RAM colorization palettes (LE 16-bit sequences, 32-bit integer)                                        |
| 0x28   | The size of the attribute map (=0x168, 32-bit integer)                                                                   |
| 0x2C   | The offset of the attribute map (32-bit integer)                                                                         |
| 0x30   | The size of the attribute files (=0xfd2, 32-bit integer)                                                                 |
| 0x34   | The offset of the attribute files (32-bit integer)                                                                       |
| 0x38   | Multiplayer status (1 byte); high nibble is player count (1, 2 or 4), low nibble is current player (Where Player 1 is 0) |

If only some of the size-offset pairs are available (for example, partial HLE SGB implementation), missing fields are allowed to have 0 as their size, and implementations are expected to fall back to a sane default.

#### END block
The END block uses the `'END '` identifier, and is a required block that marks the end of BESS data. Naturally, it must be the last block. The length of the END block must be 0.

## Validation and Failures

Other than previously specified required fail conditions, an implementation is free to decide what format errors should abort the loading of a save file. Structural errors (e.g. a block with an invalid length, a file offset that is outside the file's range, or a missing END block) should be considered as irrecoverable errors. Other errors that are considered fatal by SameBoy's implementation:
* Duplicate CORE block
* A known block, other than NAME, appearing before CORE
* An invalid length for the XOAM, RTC, SGB or HUC3 blocks
* An invalid length of MBC (not a multiple of 3)
* A write outside the $0000-$7FFF and $A000-$BFFF ranges in the MBC block
* An SGB block on a save state targeting another model
* An END block with non-zero length

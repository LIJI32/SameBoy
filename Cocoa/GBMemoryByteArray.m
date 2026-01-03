#import "GBMemoryByteArray.h"
#import "GBCompleteByteSlice.h"


@implementation GBMemoryByteArray
{
    Document *_document;
}

- (instancetype) initWithDocument:(Document *)document
{
    if ((self = [super init])) {
        _document = document;
    }
    return self;
}

- (unsigned long long)length
{
    switch (_mode) {
        case GBMemoryEntireSpace:
            return 0x10000;
        case GBMemoryROM:
            return 0x8000;
        case GBMemoryVRAM:
            return 0x2000;
        case GBMemoryExternalRAM:
            return 0x2000;
        case GBMemoryRAM:
            return 0x2000;
    }
}

- (uint16_t)base
{
    switch (_mode) {
        case GBMemoryEntireSpace: return 0;
        case GBMemoryROM: return 0;
        case GBMemoryVRAM: return 0x8000;
        case GBMemoryExternalRAM: return 0xA000;
        case GBMemoryRAM: return 0xC000;
    }
}

- (void)copyBytes:(unsigned char *)dst range:(HFRange)range
{
    // Do everything in 0x1000 chunks, never cross a 0x1000 boundary
    if ((range.location & 0xF000) != ((range.location + range.length) & 0xF000)) {
        size_t partial = 0x1000 - (range.location & 0xFFF);
        [self copyBytes:dst + partial range:HFRangeMake(range.location + partial, range.length - partial)];
        range.length = partial;
    }
    range.location += self.base;
    
    GB_gameboy_t *gb = _document.gameboy;

    switch (range.location >> 12) {
        case 0x0:
        case 0x1:
        case 0x2:
        case 0x3: {
            uint16_t bank;
            uint8_t *data = GB_get_direct_access(gb, GB_DIRECT_ACCESS_ROM0, NULL, &bank);
            memcpy(dst, data + bank * 0x4000 + range.location, range.length);
            break;
        }
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7: {
            uint16_t bank;
            size_t size;
            uint8_t *data = GB_get_direct_access(gb, GB_DIRECT_ACCESS_ROM, &size, &bank);
            if (_mode != GBMemoryEntireSpace) {
                bank = self.selectedBank & (size / 0x4000 - 1);
            }
            memcpy(dst, data + bank * 0x4000 + range.location - 0x4000, range.length);
            break;
        }
        case 0x8:
        case 0x9: {
            uint16_t bank;
            size_t size;
            uint8_t *data = GB_get_direct_access(gb, GB_DIRECT_ACCESS_VRAM, &size, &bank);
            if (_mode != GBMemoryEntireSpace) {
                bank = self.selectedBank & (size / 0x2000 - 1);
            }
            memcpy(dst, data + bank * 0x2000 + range.location - 0x8000, range.length);
            break;
        }
        case 0xA:
        case 0xB: {
            // Some carts are special, use memory read directly in full mem mode
            if (_mode == GBMemoryEntireSpace) {
        case 0xF:
        slow_path:
                [_document performAtomicBlock:^{
                    for (unsigned i = 0; i < range.length; i++) {
                        dst[i] = GB_safe_read_memory(gb, range.location + i);
                    }
                }];
                break;
            }
            else {
                uint16_t bank;
                size_t size;
                uint8_t *data = GB_get_direct_access(gb, GB_DIRECT_ACCESS_CART_RAM, &size, &bank);
                bank = self.selectedBank & (size / 0x2000 - 1);
                if (size == 0) {
                    memset(dst, 0xFF, range.length);
                }
                else if (range.location + range.length - 0xA000 > size)  {
                    goto slow_path;
                }
                else {
                    memcpy(dst, data + bank * 0x2000 + range.location - 0xA000, range.length);
                }
                break;
            }
        }
        case 0xC:
        case 0xE: {
            uint8_t *data = GB_get_direct_access(gb, GB_DIRECT_ACCESS_RAM, NULL, NULL);
            memcpy(dst, data + (range.location & 0xFFF), range.length);
            break;
        }
            
        case 0xD: {
            uint16_t bank;
            size_t size;
            uint8_t *data = GB_get_direct_access(gb, GB_DIRECT_ACCESS_RAM, &size, &bank);
            if (_mode != GBMemoryEntireSpace) {
                bank = self.selectedBank & (size / 0x1000 - 1);
            }
            memcpy(dst, data + bank * 0x1000 + range.location - 0xD000, range.length);
            break;
        }
    }
}

- (NSArray *)byteSlices
{
    return @[[[GBCompleteByteSlice alloc] initWithByteArray:self]];
}

- (HFByteArray *)subarrayWithRange:(HFRange)range
{
    unsigned char arr[range.length];
    [self copyBytes:arr range:range];
    HFByteArray *ret = [[HFBTreeByteArray alloc] init];
    HFFullMemoryByteSlice *slice = [[HFFullMemoryByteSlice alloc] initWithData:[NSData dataWithBytes:arr length:range.length]];
    [ret insertByteSlice:slice inRange:HFRangeMake(0, 0)];
    return ret;
}

- (void)insertByteSlice:(HFByteSlice *)slice inRange:(HFRange)range
{
    if (slice.length != range.length) return; /* Insertion is not allowed, only overwriting. */
    // Do everything in 0x1000 chunks, never cross a 0x1000 boundary
    if ((range.location & 0xF000) != ((range.location + range.length) & 0xF000)) {
        size_t partial = 0x1000 - (range.location & 0xFFF);
        if (slice.length - partial) {
            [self insertByteSlice:[slice subsliceWithRange:HFRangeMake(partial, slice.length - partial)]
                            inRange:HFRangeMake(range.location + partial, range.length - partial)];
        }
        range.length = partial;
    }
    range.location += self.base;
    
    GB_gameboy_t *gb = _document.gameboy;
    
    
    switch (range.location >> 12) {
        case 0x0:
        case 0x1:
        case 0x2:
        case 0x3:
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7: {
            uint16_t bank;
            size_t size;
            uint8_t *data = GB_get_direct_access(gb, range.location < 0x4000? GB_DIRECT_ACCESS_ROM0 : GB_DIRECT_ACCESS_ROM, &size, &bank);
            if (_mode != GBMemoryEntireSpace && range.location >= 0x4000) {
                bank = self.selectedBank & (size / 0x4000 - 1);
            }
            uint8_t sliceData[range.length];
            [slice copyBytes:sliceData range:HFRangeMake(0, range.length)];
            memcpy(data + bank * 0x4000 + (range.location & 0x3FFF), sliceData, range.length);
            [_document setROMModified];
            break;
        }
        case 0x8:
        case 0x9: {
            uint16_t bank;
            size_t size;
            uint8_t *data = GB_get_direct_access(gb, GB_DIRECT_ACCESS_VRAM, &size, &bank);
            if (_mode != GBMemoryEntireSpace) {
                bank = self.selectedBank & (size / 0x2000 - 1);
            }
            uint8_t sliceData[range.length];
            [slice copyBytes:sliceData range:HFRangeMake(0, range.length)];
            memcpy(data + bank * 0x2000 + range.location - 0x8000, sliceData, range.length);
            break;
        }
        case 0xA:
        case 0xB: {
            // Some carts are special, use memory write directly in full mem mode
            if (_mode == GBMemoryEntireSpace) {
        case 0xF:
        slow_path:
                [_document performAtomicBlock:^{
                    uint8_t sliceData[range.length];
                    [slice copyBytes:sliceData range:HFRangeMake(0, range.length)];
                    for (unsigned i = 0; i < range.length; i++) {
                        GB_write_memory(gb, range.location + i, sliceData[i]);
                    }
                }];
                break;
            }
            else {
                uint16_t bank;
                size_t size;
                uint8_t *data = GB_get_direct_access(gb, GB_DIRECT_ACCESS_CART_RAM, &size, &bank);
                bank = self.selectedBank & (size / 0x2000 - 1);
                if (size == 0) {
                    // Nothing to write to
                }
                else if (range.location + range.length - 0xA000 > size)  {
                    goto slow_path;
                }
                else {
                    uint8_t sliceData[range.length];
                    [slice copyBytes:sliceData range:HFRangeMake(0, range.length)];
                    memcpy(data + bank * 0x2000 + range.location - 0xA000, sliceData, range.length);
                }
                break;
            }
        }
        case 0xC:
        case 0xE: {
            uint8_t *data = GB_get_direct_access(gb, GB_DIRECT_ACCESS_RAM, NULL, NULL);
            uint8_t sliceData[range.length];
            [slice copyBytes:sliceData range:HFRangeMake(0, range.length)];
            memcpy(data + (range.location & 0xFFF), sliceData, range.length);
            break;
        }
            
        case 0xD: {
            uint16_t bank;
            size_t size;
            uint8_t *data = GB_get_direct_access(gb, GB_DIRECT_ACCESS_RAM, &size, &bank);
            if (_mode != GBMemoryEntireSpace) {
                bank = self.selectedBank & (size / 0x1000 - 1);
            }
            uint8_t sliceData[range.length];
            [slice copyBytes:sliceData range:HFRangeMake(0, range.length)];

            memcpy(data + bank * 0x1000 + range.location - 0xD000, sliceData, range.length);
            break;
        }
    }
}

@end

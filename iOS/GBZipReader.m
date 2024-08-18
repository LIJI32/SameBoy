#import "GBZipReader.h"
#import <compression.h>
#import <sys/mman.h>
#import <mach/vm_region.h>
#pragma clang diagnostic ignored "-Wimplicit-retain-self"

@implementation GBZipReader
{
    void (^_freeCallback)(void);
    const void *_buffer;
    size_t _size;
    
    const struct __attribute__((packed)) {
        uint32_t magic;
        uint8_t skip[6];
        uint16_t fileCount;
        uint32_t cdSize;
        uint32_t cdOffset;
        uint16_t commentSize;
    } *_eocd;
}

- (instancetype)initWithBuffer:(const void *)buffer size:(size_t)size free:(void (^)(void))callback
{
    self = [super init];
    if (!self) return nil;
    
    _buffer = buffer;
    _size = size;
    _freeCallback = callback;
    
    if (_size < sizeof(*_eocd)) return nil;
    
    for (unsigned i = 0; i < 0x10000; i++) {
        _eocd = (void *)((uint8_t *)buffer + size - sizeof(*_eocd) - i);
        if ((void *)_eocd < buffer) return nil;
        if (_eocd->magic == htonl('PK\05\06')) {
            break;
        }
    }
    
    return self;
}

- (instancetype)initWithPath:(NSString *)path
{
    int fd = open(path.UTF8String, O_RDONLY);
    if (fd < 0) return nil;
    size_t size = lseek(fd, 0, SEEK_END);
    size_t alignedSize = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    void *mapping = mmap(NULL, alignedSize, PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0);
    close(fd);
    if (!mapping) return nil;
    
    return [self initWithBuffer:mapping size:size free:^{
        munmap(mapping, alignedSize);
    }];
}

- (void)iterateFiles:(bool (^)(NSString *filename, size_t uncompressedSize, bool (^getData)(void *), bool (^writeToPath)(NSString *)))callback {
    const struct __attribute__((packed)) {
        uint32_t magic;
        uint8_t skip[6];
        uint16_t compressionMethod;
        uint8_t skip2[8];
        uint32_t compressedSize;
        uint32_t uncompressedSize;
        uint16_t nameLength;
        uint16_t extraLength;
        uint16_t commentLength;
        uint8_t skip3[8];
        uint32_t localHeaderOffset;
        char name[0];
    } *entry = (void *)((uint8_t *)_buffer + _eocd->cdOffset);
    for (unsigned i = _eocd->fileCount; i--;) {
        if ((uint8_t *)entry + sizeof(*entry) - (uint8_t *)_buffer >= _size) return;
        if (entry->magic != htonl('PK\01\02')) return;
        
        typeof(entry) next = (void *)((uint8_t *)entry + sizeof(*entry) +
                                      entry->nameLength + entry->extraLength + entry->commentLength);
        if ((uint8_t *)next - (uint8_t *)_buffer >= _size) return;
        
        
        bool (^getData)(void *) = ^bool(void *output) {
            // Directory, no data
            if (entry->name[entry->nameLength - 1] == '/') return false;
            
            if (entry->uncompressedSize == 0xffffffff || entry->compressedSize == 0xffffffff) {
                // ZIP64
                return false;
            }
            
            const struct __attribute__((packed)) {
                uint32_t magic;
                uint8_t skip[4];
                uint16_t compressionMethod;
                uint8_t skip2[8];
                uint32_t compressedSize;
                uint32_t uncompressedSize;
                uint16_t nameLength;
                uint16_t extraLength;
                char name[0];
            } *localEntry = (void *)((uint8_t *)_buffer + entry->localHeaderOffset);
            
            if ((uint8_t *)localEntry + sizeof(*localEntry) - (uint8_t *)_buffer >= _size) return nil;
            if ((uint8_t *)localEntry + sizeof(*localEntry) +
                localEntry->nameLength + localEntry->extraLength +
                entry->compressedSize - (uint8_t *)_buffer >= _size) {
                return false;
            }
            
            if (localEntry->magic != htonl('PK\03\04')) return nil;
            if (entry->uncompressedSize != localEntry->uncompressedSize) return nil;
            
            const void *dataStart = &localEntry->name[localEntry->nameLength + localEntry->extraLength];
            if (localEntry->compressionMethod == 0) {
                if (localEntry->uncompressedSize != entry->compressedSize) return false;
                memcpy(output, dataStart, localEntry->uncompressedSize);
                return true;
            }
            else if (localEntry->compressionMethod != 8) {
                // Unsupported compression
                return false;
            }
            if (compression_decode_buffer(output, localEntry->uncompressedSize,
                                          dataStart, entry->compressedSize,
                                          NULL, COMPRESSION_ZLIB) != localEntry->uncompressedSize) {
                return false;
            }
            return true;
        };
        
        bool (^writeToPath)(NSString *) = ^bool(NSString *path) {
            int fd = open(path.UTF8String, O_CREAT | O_RDWR, 0644);
            if (!fd) return false;
            if (ftruncate(fd, entry->uncompressedSize) != 0) {
                close(fd);
                unlink(path.UTF8String);
                return false;
            }
            size_t alignedSize = (entry->uncompressedSize + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            void *mapping = mmap(NULL, alignedSize, PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, fd, 0);
            close(fd);
            if (!mapping) {
                unlink(path.UTF8String);
                return false;
            }
            
            bool ret = getData(mapping);
            if (!ret) {
                unlink(path.UTF8String);
            }            
            munmap(mapping, alignedSize);

            return ret;
        };

        
        if (!callback([[NSString alloc] initWithBytes:entry->name
                                               length:entry->nameLength
                                             encoding:NSUTF8StringEncoding],
                      entry->uncompressedSize,
                      getData,
                      writeToPath)) {
            return;
        }
        
        entry = next;
    }
}

- (void)dealloc
{
    if (_freeCallback) {
        _freeCallback();
    }
}

@end

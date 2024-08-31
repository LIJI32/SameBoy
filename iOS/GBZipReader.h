#import <Foundation/Foundation.h>

@interface GBZipReader : NSObject
- (instancetype)initWithBuffer:(const void *)buffer size:(size_t)size free:(void (^)(void))callback;
- (instancetype)initWithPath:(NSString *)path;
- (void)iterateFiles:(bool (^)(NSString *filename, size_t uncompressedSize, bool (^getData)(void *), bool (^writeToPath)(NSString *)))callback;
@end

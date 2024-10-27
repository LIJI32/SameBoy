#import <Foundation/Foundation.h>
#import "GBCommunicator.h"

@interface GBPhoneManager : GBCommunicator
+ (instancetype)sharedManager;
@property (readonly) NSString *saveStatePath;
@property (readonly) NSString *pngPath;
@property (readonly) NSString *romPath;
@property (readonly) NSString *metadataPath;
@property (readonly) NSString *batteryPath;
- (void)updateSaveState:(void (^)(NSString *error))completion;
- (void)validateUUID:(void (^)(bool valid))completion;
- (void)refreshSettings:(void (^)(void))completion;
@end

#import <Foundation/Foundation.h>
#import "GBCommunicator.h"

@interface GBPhoneManager : GBCommunicator
+ (instancetype)sharedManager;
- (NSString *)saveStatePath;
- (NSString *)pngPath;
- (NSString *)romPath;
- (NSString *)metadataPath;
- (void)updateSaveState:(void (^)(NSString *error))completion;
- (void)validateUUID:(void (^)(bool valid))completion;
- (void)refreshSettings:(void (^)(void))completion;
@end

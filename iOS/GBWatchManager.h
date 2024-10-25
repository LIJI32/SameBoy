#import <Foundation/Foundation.h>
#import "GBCommunicator.h"

@interface GBWatchManager : GBCommunicator;
+ (instancetype)sharedManager;
- (void)loadROM:(NSString *)rom completion:(void (^)(NSString *error))completion;
- (void)closeROM:(void (^)(NSString *error))completion;
- (void)getSaveState:(void (^)(NSString *error, NSData *saveState, NSData *png, NSUUID *uuid))completion;
- (void)getUUID:(void (^)(NSString *error, NSUUID *uuid))completion;
- (void)updateBootROMs:(void (^)(NSString *error))completion;
- (void)updateSettings:(void (^)(NSString *error))completion;
- (void)runWhenReachable:(void (^)(void))completion;
- (void)cancelRunWhenReachable;
@property (readonly) bool isPaired;
@property (readonly) bool isReachable;
@end

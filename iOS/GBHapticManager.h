#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

@interface GBHapticManager : NSObject
+ (instancetype)sharedManager;
- (void)doTapHaptic;
- (void)setRumbleStrength:(double)rumble;
@property (weak) GCController *controller;
@end

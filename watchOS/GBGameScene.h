#import <SpriteKit/SpriteKit.h>
#import <WatchKit/WatchKit.h>
#import <Core/gb.h>

@interface GBGameScene : SKScene
- (void)holdButton:(GB_key_t)button duration:(double)seconds;
- (void)showHoldAt:(CGPoint)position;
- (void)hideHold;
- (void)setInput:(GB_key_mask_t)mask;
- (void)start;
- (void)stop;
- (void)setSpeedMultiplayer:(double)factor;
- (void)rewindFrames:(unsigned)count;
@end

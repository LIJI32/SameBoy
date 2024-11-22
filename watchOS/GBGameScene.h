#import <SpriteKit/SpriteKit.h>
#import <WatchKit/WatchKit.h>
#import <Core/gb.h>

// This ratio guaranteed equal area for each button
#define BUTTON_WIDTH 0.2763932022500210303590826331268723764559381640388474275729102754

@interface GBGameScene : SKScene
- (NSTimer *)holdButton:(GB_key_t)button duration:(double)seconds;
- (void)showHoldAt:(CGPoint)position;
- (void)hideHold;
- (void)setInput:(GB_key_mask_t)mask;
- (void)start;
- (void)stop;
- (void)stopAndSave;
- (void)setSpeedMultiplayer:(double)factor;
- (void)rewindFrames:(unsigned)count;
@property (readonly) bool isRunning;
@end

#import <Cocoa/Cocoa.h>
#include <Core/gb.h>
#import "GBJoystickListener.h"

@interface GBView<GBJoystickListener> : NSView
- (void) flip;
- (uint32_t *) pixels;
@property GB_gameboy_t *gb;
@property (nonatomic) BOOL shouldBlendFrameWithPrevious;
@property (getter=isMouseHidingEnabled) BOOL mouseHidingEnabled;
@property bool isRewinding;
@property NSView *internalView;
- (void) createInternalView;
- (CGContextRef)currentBuffer;
- (CGContextRef)previousBuffer;

- (NSRect) viewport;

- (void)screenSizeChanged;
@end

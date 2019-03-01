#import <Cocoa/Cocoa.h>
#include <Core/gb.h>
#include <Misc/wide_gb.h>
#import "GBJoystickListener.h"

@interface GBView<GBJoystickListener> : NSView
- (void) flip;

// Input buffers where the emulator draws the pixels
- (uint32_t *) pixels;    // whole screen buffer
- (uint32_t *) bg_pixels; // BG-only buffer

@property GB_gameboy_t *gb;
@property wide_gb *wgb;

@property (nonatomic) BOOL shouldBlendFrameWithPrevious;
@property (nonatomic) BOOL widescreenEnabled;
@property (getter=isMouseHidingEnabled) BOOL mouseHidingEnabled;
@property bool isRewinding;
@property NSView *internalView;
- (void) createInternalView;

// Output composited buffers
- (CGContextRef)currentBuffer;
- (CGContextRef)previousBuffer;

// The rectangle in which the actual emulated screen is displayed (in view cordinates)
- (NSRect) viewport;

- (void)screenSizeChanged;
@end

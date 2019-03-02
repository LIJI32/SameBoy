#import <Carbon/Carbon.h>
#import <Misc/wide_gb.h>
#import "GBView.h"
#import "GBViewGL.h"
#import "GBViewMetal.h"
#import "GBButtons.h"
#import "NSString+StringForKey.h"

#define JOYSTICK_HIGH 0x4000
#define JOYSTICK_LOW 0x3800


NSRect NSRectFromWGBRect(WGB_Rect rect) { return NSMakeRect(rect.x, rect.y, rect.w, rect.h); }
WGB_Rect WGBRectFromNSRect(NSRect rect) { return (WGB_Rect) { rect.origin.x, rect.origin.y, rect.size.width, rect.size.height }; }

@implementation GBView
{
    CGContextRef image_buffers[3];
    CGContextRef bg_image_buffers[3];
    CGContextRef composited_buffers[3];
    CGColorSpaceRef colorSpace;
    NSMutableDictionary *tileContexts;
    NSRect viewport;
    bool needsCompositing;
    unsigned char current_buffer;
    BOOL mouse_hidden;
    NSTrackingArea *tracking_area;
    BOOL _mouseHidingEnabled;
    bool axisActive[2];
    bool underclockKeyDown;
    double clockMultiplier;
    NSEventModifierFlags previousModifiers;
}

+ (instancetype)alloc
{
    return [self allocWithZone:NULL];
}

+ (instancetype)allocWithZone:(struct _NSZone *)zone
{
    if (self == [GBView class]) {
        if ([GBViewMetal isSupported]) {
            return [GBViewMetal allocWithZone: zone];
        }
        return [GBViewGL allocWithZone: zone];
    }
    return [super allocWithZone:zone];
}

- (void) createInternalView
{
    assert(false && "createInternalView must not be inherited");
}

- (void) _init
{    
    _shouldBlendFrameWithPrevious = 1;
    colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
    tileContexts = [NSMutableDictionary new];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(ratioKeepingChanged) name:@"GBAspectChanged" object:nil];
    tracking_area = [ [NSTrackingArea alloc] initWithRect:(NSRect){}
                                                  options:NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect
                                                    owner:self
                                                 userInfo:nil];
    [self addTrackingArea:tracking_area];
    clockMultiplier = 1.0;
    [self createInternalView];
    [self addSubview:self.internalView];
    self.internalView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
}

- (void) setWidescreenEnabled:(BOOL)enabled
{
    _widescreenEnabled = enabled;
    [self updateViewFrame];
}

- (void)screenSizeChanged
{
    if (image_buffers[0]) CGContextRelease(image_buffers[0]);
    if (image_buffers[1]) CGContextRelease(image_buffers[1]);
    if (image_buffers[2]) CGContextRelease(image_buffers[2]);
    if (bg_image_buffers[0]) CGContextRelease(bg_image_buffers[0]);
    if (bg_image_buffers[1]) CGContextRelease(bg_image_buffers[1]);
    if (bg_image_buffers[2]) CGContextRelease(bg_image_buffers[2]);
    
    NSSize bufferSize = NSMakeSize(GB_get_screen_width(_gb), GB_get_screen_height(_gb));

    image_buffers[0] = [self createBitmapContextWithSize:bufferSize];
    image_buffers[1] = [self createBitmapContextWithSize:bufferSize];
    image_buffers[2] = [self createBitmapContextWithSize:bufferSize];
    bg_image_buffers[0] = [self createBitmapContextWithSize:bufferSize];
    bg_image_buffers[1] = [self createBitmapContextWithSize:bufferSize];
    bg_image_buffers[2] = [self createBitmapContextWithSize:bufferSize];
    
    dispatch_async(dispatch_get_main_queue(), ^{
        [self setFrame:self.superview.bounds];
    });
}

- (CGContextRef) createBitmapContextWithSize:(NSSize)size
{
    // Let the API allocate memory for the buffer by itself
    return [self createBitmapContextWithSize:size data:NULL];
}

- (CGContextRef) createBitmapContextWithSize:(NSSize)size data:(void*)data
{  
    // Use an RGBA pixel format
    size_t bitsPerComponent = 8;
    size_t bytesPerPixel = sizeof (uint32_t);
    uint32_t bitmapInfo = kCGImageAlphaPremultipliedLast;

    CGContextRef context = CGBitmapContextCreate(
        data,
        size.width,
        size.height,
        bitsPerComponent,
        bytesPerPixel * size.width,
        colorSpace,
        bitmapInfo);
    
    if (!context) {
        NSLog(@"Failed to create context with size %@", NSStringFromSize(size));
    }
    return context;
}

- (void) updateViewFrame
{
    [self setFrame:self.superview.bounds];
}

- (void) setShouldBlendFrameWithPrevious:(BOOL)shouldBlendFrameWithPrevious
{
    _shouldBlendFrameWithPrevious = shouldBlendFrameWithPrevious;
    [self setNeedsDisplay:YES];
}

- (unsigned char) numberOfBuffers
{
    return _shouldBlendFrameWithPrevious? 3 : 2;
}

- (void)dealloc
{
    CGContextRelease(image_buffers[0]);
    CGContextRelease(image_buffers[1]);
    CGContextRelease(image_buffers[2]);
    CGContextRelease(bg_image_buffers[0]);
    CGContextRelease(bg_image_buffers[1]);
    CGContextRelease(bg_image_buffers[2]);
    CGContextRelease(composited_buffers[0]);
    CGContextRelease(composited_buffers[1]);
    CGContextRelease(composited_buffers[2]);
    CGColorSpaceRelease(colorSpace);

    for (NSValue *contextValue in [tileContexts allValues]) {
        CGContextRelease(contextValue.pointerValue);
    }

    if (mouse_hidden) {
        mouse_hidden = false;
        [NSCursor unhide];
    }
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
    if (!(self = [super initWithCoder:coder]))
    {
        return self;
    }
    [self _init];
    return self;
}

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (!(self = [super initWithFrame:frameRect]))
    {
        return self;
    }
    [self _init];
    return self;
}

- (void)setFrame:(NSRect)frame
{
    frame = self.superview.bounds;
    [super setFrame:frame];

    // Update viewport
    viewport = frame;

    bool keepAspectRatio = ![[NSUserDefaults standardUserDefaults] boolForKey:@"GBAspectRatioUnkept"] || self.widescreenEnabled;
    if (_gb && keepAspectRatio) {
        double ratio = frame.size.width / frame.size.height;
        double width = GB_get_screen_width(_gb);
        double height = GB_get_screen_height(_gb);
        if (ratio >= width / height) {
            double new_width = round(frame.size.height / height * width);
            viewport.origin.x = floor((frame.size.width - new_width) / 2);
            viewport.size.width = new_width;
            viewport.origin.y = 0;
        }
        else {
            double new_height = round(frame.size.width / width * height);
            viewport.origin.y = floor((frame.size.height - new_height) / 2);
            viewport.size.height = new_height;
            viewport.origin.x = 0;
        }
    }

    if (self.widescreenEnabled) {
        float scaleFactor = 0.8;
        NSRect scaledviewport = CGRectInset(viewport, viewport.size.width * (1 - scaleFactor) / 2, viewport.size.height * (1 - scaleFactor) / 2);
        viewport = NSIntegralRectWithOptions(scaledviewport, NSAlignAllEdgesNearest);
    }

    if (composited_buffers[0]) CGContextRelease(composited_buffers[0]);
    if (composited_buffers[1]) CGContextRelease(composited_buffers[1]);
    if (composited_buffers[2]) CGContextRelease(composited_buffers[2]);

    NSSize compositedBufferSize = self.widescreenEnabled ? [self screenRectFromView:frame].size : [self screenRectFromView:viewport].size;
    composited_buffers[0] = [self createBitmapContextWithSize:compositedBufferSize];
    composited_buffers[1] = [self createBitmapContextWithSize:compositedBufferSize];
    composited_buffers[2] = [self createBitmapContextWithSize:compositedBufferSize];

    [self setNeedsCompositing];
}

- (void) flip
{
    if (underclockKeyDown && clockMultiplier > 0.5) {
        clockMultiplier -= 0.1;
        GB_set_clock_multiplier(_gb, clockMultiplier);
    }
    if (!underclockKeyDown && clockMultiplier < 1.0) {
        clockMultiplier += 0.1;
        GB_set_clock_multiplier(_gb, clockMultiplier);
    }
    current_buffer = (current_buffer + 1) % self.numberOfBuffers;
    [self setNeedsCompositing];
}

- (uint32_t *) pixels
{
    CGContextRef image_buffer = image_buffers[(current_buffer + 1) % self.numberOfBuffers];
    return CGBitmapContextGetData(image_buffer);
}

- (NSRect) viewport
{
    return viewport;
}

- (uint32_t *) bg_pixels
{
    CGContextRef bg_image_buffer = bg_image_buffers[(current_buffer + 1) % self.numberOfBuffers];
    return CGBitmapContextGetData(bg_image_buffer);
}

-(void)keyDown:(NSEvent *)theEvent
{
    unsigned short keyCode = theEvent.keyCode;
    bool handled = false;

    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    unsigned player_count = GB_get_player_count(_gb);
    for (unsigned player = 0; player < player_count; player++) {
        for (GBButton button = 0; button < GBButtonCount; button++) {
            NSNumber *key = [defaults valueForKey:button_to_preference_name(button, player)];
            if (!key) continue;

            if (key.unsignedShortValue == keyCode) {
                handled = true;
                switch (button) {
                    case GBTurbo:
                        GB_set_turbo_mode(_gb, true, self.isRewinding);
                        break;
                        
                    case GBRewind:
                        self.isRewinding = true;
                        GB_set_turbo_mode(_gb, false, false);
                        break;
                        
                    case GBUnderclock:
                        underclockKeyDown = true;
                        break;
                        
                    default:
                        GB_set_key_state_for_player(_gb, (GB_key_t)button, player, true);
                        break;
                }
            }
        }
    }

    if (!handled && [theEvent type] != NSEventTypeFlagsChanged) {
        [super keyDown:theEvent];
    }
}

-(void)keyUp:(NSEvent *)theEvent
{
    unsigned short keyCode = theEvent.keyCode;
    bool handled = false;

    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    unsigned player_count = GB_get_player_count(_gb);
    for (unsigned player = 0; player < player_count; player++) {
        for (GBButton button = 0; button < GBButtonCount; button++) {
            NSNumber *key = [defaults valueForKey:button_to_preference_name(button, player)];
            if (!key) continue;
            
            if (key.unsignedShortValue == keyCode) {
                handled = true;
                switch (button) {
                    case GBTurbo:
                        GB_set_turbo_mode(_gb, false, false);
                        break;
                        
                    case GBRewind:
                        self.isRewinding = false;
                        break;
                        
                    case GBUnderclock:
                        underclockKeyDown = false;
                        break;
                        
                    default:
                        GB_set_key_state_for_player(_gb, (GB_key_t)button, player, false);
                        break;
                }
            }
        }
    }
    if (!handled && [theEvent type] != NSEventTypeFlagsChanged) {
        [super keyUp:theEvent];
    }
}

- (void) joystick:(NSString *)joystick_name button: (unsigned)button changedState: (bool) state
{
    unsigned player_count = GB_get_player_count(_gb);

    UpdateSystemActivity(UsrActivity);
    for (unsigned player = 0; player < player_count; player++) {
        NSString *preferred_joypad = [[[NSUserDefaults standardUserDefaults] dictionaryForKey:@"GBDefaultJoypads"]
                                      objectForKey:[NSString stringWithFormat:@"%u", player]];
        if (player_count != 1 && // Single player, accpet inputs from all joypads
            !(player == 0 && !preferred_joypad) && // Multiplayer, but player 1 has no joypad configured, so it takes inputs from all joypads
            ![preferred_joypad isEqualToString:joystick_name]) {
            continue;
        }
        NSDictionary *mapping = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"GBJoypadMappings"][joystick_name];
        
        for (GBButton i = 0; i < GBButtonCount; i++) {
            NSNumber *mapped_button = [mapping objectForKey:GBButtonNames[i]];
            if (mapped_button && [mapped_button integerValue] == button) {
                switch (i) {
                    case GBTurbo:
                        GB_set_turbo_mode(_gb, state, state && self.isRewinding);
                        break;
                        
                    case GBRewind:
                        self.isRewinding = state;
                        if (state) {
                            GB_set_turbo_mode(_gb, false, false);
                        }
                        break;
                    
                    case GBUnderclock:
                        underclockKeyDown = state;
                        break;
                        
                    default:
                        GB_set_key_state_for_player(_gb, (GB_key_t)i, player, state);
                        break;
                }
            }
        }
    }
}

- (void) joystick:(NSString *)joystick_name axis: (unsigned)axis movedTo: (signed) value
{
    unsigned player_count = GB_get_player_count(_gb);

    UpdateSystemActivity(UsrActivity);
    for (unsigned player = 0; player < player_count; player++) {
        NSString *preferred_joypad = [[[NSUserDefaults standardUserDefaults] dictionaryForKey:@"GBDefaultJoypads"]
                                      objectForKey:[NSString stringWithFormat:@"%u", player]];
        if (player_count != 1 && // Single player, accpet inputs from all joypads
            !(player == 0 && !preferred_joypad) && // Multiplayer, but player 1 has no joypad configured, so it takes inputs from all joypads
            ![preferred_joypad isEqualToString:joystick_name]) {
            continue;
        }
        
        NSDictionary *mapping = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"GBJoypadMappings"][joystick_name];
        NSNumber *x_axis = [mapping objectForKey:@"XAxis"];
        NSNumber *y_axis = [mapping objectForKey:@"YAxis"];
        
        if (axis == [x_axis integerValue]) {
            if (value > JOYSTICK_HIGH) {
                axisActive[0] = true;
                GB_set_key_state_for_player(_gb, GB_KEY_RIGHT, player, true);
                GB_set_key_state_for_player(_gb, GB_KEY_LEFT, player, false);
            }
            else if (value < -JOYSTICK_HIGH) {
                axisActive[0] = true;
                GB_set_key_state_for_player(_gb, GB_KEY_RIGHT, player, false);
                GB_set_key_state_for_player(_gb, GB_KEY_LEFT, player, true);
            }
            else if (axisActive[0] && value < JOYSTICK_LOW && value > -JOYSTICK_LOW) {
                axisActive[0] = false;
                GB_set_key_state_for_player(_gb, GB_KEY_RIGHT, player, false);
                GB_set_key_state_for_player(_gb, GB_KEY_LEFT, player, false);
            }
        }
        else if (axis == [y_axis integerValue]) {
            if (value > JOYSTICK_HIGH) {
                axisActive[1] = true;
                GB_set_key_state_for_player(_gb, GB_KEY_DOWN, player, true);
                GB_set_key_state_for_player(_gb, GB_KEY_UP, player, false);
            }
            else if (value < -JOYSTICK_HIGH) {
                axisActive[1] = true;
                GB_set_key_state_for_player(_gb, GB_KEY_DOWN, player, false);
                GB_set_key_state_for_player(_gb, GB_KEY_UP, player, true);
            }
            else if (axisActive[1] && value < JOYSTICK_LOW && value > -JOYSTICK_LOW) {
                axisActive[1] = false;
                GB_set_key_state_for_player(_gb, GB_KEY_DOWN, player, false);
                GB_set_key_state_for_player(_gb, GB_KEY_UP, player, false);
            }
        }
    }
}


- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)mouseEntered:(NSEvent *)theEvent
{
    if (!mouse_hidden) {
        mouse_hidden = true;
        if (_mouseHidingEnabled) {
            [NSCursor hide];
        }
    }
    [super mouseEntered:theEvent];
}

- (void)mouseExited:(NSEvent *)theEvent
{
    if (mouse_hidden) {
        mouse_hidden = false;
        if (_mouseHidingEnabled) {
            [NSCursor unhide];
        }
    }
    [super mouseExited:theEvent];
}

- (void)setMouseHidingEnabled:(BOOL)mouseHidingEnabled
{
    if (mouseHidingEnabled == _mouseHidingEnabled) return;

    _mouseHidingEnabled = mouseHidingEnabled;
    
    if (mouse_hidden && _mouseHidingEnabled) {
        [NSCursor hide];
    }

    if (mouse_hidden && !_mouseHidingEnabled) {
        [NSCursor unhide];
    }
}

- (BOOL)isMouseHidingEnabled
{
    return _mouseHidingEnabled;
}

- (void) flagsChanged:(NSEvent *)event
{
    if (event.modifierFlags > previousModifiers) {
        [self keyDown:event];
    }
    else {
        [self keyUp:event];
    }
    
    previousModifiers = event.modifierFlags;
}

- (void)setNeedsCompositing {
    needsCompositing = true;
}

- (CGContextRef) contextForTile:(WGB_tile*)tile atIndex:(uint)tileIndex
{
    NSValue *tileValue = [NSValue valueWithPointer:tile];
    NSValue *contextValue = tileContexts[tileValue];
    
    // Create a context for the tile if none exist yet
    if (!contextValue) {
        CGContextRef context = [self createBitmapContextWithSize:NSMakeSize(WIDE_GB_TILE_WIDTH, WIDE_GB_TILE_HEIGHT) data:tile->pixel_buffer];
        contextValue = [NSValue valueWithPointer:context];
        tileContexts[tileValue] = contextValue;
    }

    return contextValue.pointerValue;
}

- (CGContextRef)currentBuffer
{
    if (needsCompositing && _gb) {
        needsCompositing = false;
        [self composeScreen:image_buffers[current_buffer] toContext:composited_buffers[current_buffer]];
    }

    return composited_buffers[current_buffer];
}

- (CGContextRef)previousBuffer
{
    return composited_buffers[(current_buffer + 2) % self.numberOfBuffers];
}

- (void) composeScreen:(CGContextRef)screenContext toContext:(CGContextRef)outputContext
{
    NSRect viewRectInScreenSpace = [self screenRectFromView:self.bounds];
    NSRect viewRectInContextSpace = [self contextRectFromScreen:viewRectInScreenSpace];
    NSRect viewportInScreenSpace = [self screenRectFromView:viewport];
    NSRect viewportInContextSpace = [self contextRectFromScreen:viewportInScreenSpace];

    // Fill the background color
    NSColor* backgroundColor = NSColor.blackColor;
    CGContextSetFillColorWithColor(outputContext, backgroundColor.CGColor);
    CGContextFillRect(outputContext, viewRectInContextSpace);

    // Draw tiles
    if (self.widescreenEnabled) {
        WGB_Rect wgbViewRectInScreenSpace = WGBRectFromNSRect(viewRectInScreenSpace);
        // For each tile…
        size_t tiles_count = WGB_tiles_count(_wgb);
        for (int i = 0; i < tiles_count; i++) {
            WGB_tile *tile = WGB_tile_at_index(_wgb, i);
            // Culling: skip non-visible tiles
            if (!WGB_is_tile_visible(_wgb, tile, wgbViewRectInScreenSpace)) {
                continue;
            }
            // Draw the tile
            //
            // (We need a CGImage to draw to the outputContext.
            // Ideally we would cache the CGImage objects – but they are immutable,
            // and so need to be recreated every time the tile underlying content changes.
            //
            // Fortunately, CGImage doesn't copy the context data (except if the data change),
            // so they are cheap to create.)
            CGContextRef tileContext = [self contextForTile:tile atIndex:i];
            CGImageRef tileImage = CGBitmapContextCreateImage(tileContext); // won't copy until a write
            NSRect tileRectInScreen = NSRectFromWGBRect(WGB_rect_for_tile(_wgb, tile));
            NSRect tileRectInContext = [self contextRectFromScreen:tileRectInScreen];
            CGContextDrawImage(outputContext, tileRectInContext, tileImage);
            CGImageRelease(tileImage);
        }
    }

    // Draw the screen
    CGImageRef screenImage = CGBitmapContextCreateImage(screenContext); // won't copy until a write
    CGContextDrawImage(outputContext, viewportInContextSpace, screenImage);
    CGImageRelease(screenImage);

    // Draw a border around the screen
    if (self.widescreenEnabled) {
        float borderWidth = 1.0;
        NSRect borderRect = NSInsetRect(viewportInContextSpace, -(borderWidth / 2), -(borderWidth / 2));
        CGContextSetLineJoin(outputContext, kCGLineJoinMiter);
        CGContextSetStrokeColorWithColor(outputContext, [NSColor colorWithWhite:0.7 alpha:1].CGColor);
        CGContextSetBlendMode(outputContext, kCGBlendModeHardLight);
        CGContextStrokeRectWithWidth(outputContext, borderRect, borderWidth);
        CGContextSetBlendMode(outputContext, kCGBlendModeNormal);
    }
}

#pragma mark - Geometry utils

// Convert a rectangle from view-space to screen-space
- (NSRect) screenRectFromView:(NSRect)viewRect
{
    NSSize screenSize = _gb ? NSMakeSize(GB_get_screen_width(_gb), GB_get_screen_height(_gb)) : NSMakeSize(160, 144);
    NSPoint viewportScale = NSMakePoint(
        viewport.size.width / screenSize.width,
        viewport.size.height / screenSize.height
    );

    NSRect result = NSMakeRect(
        (viewRect.origin.x - viewport.origin.x) / viewportScale.x,
        (viewRect.origin.y - viewport.origin.y) / viewportScale.y,
        viewRect.size.width  / viewportScale.x,
        viewRect.size.height / viewportScale.y
    );
    return NSIntegralRectWithOptions(result, NSAlignAllEdgesNearest);
}

// Convert a rectangle from screen-space to context-space
// This accounts for the inverted coordinate system of CoreGraphics (origin is at bottom-left)
- (NSRect) contextRectFromScreen:(NSRect)screenRect
{
    NSRect contextFrameInScreen = self.widescreenEnabled ? [self screenRectFromView:self.bounds] : [self screenRectFromView:viewport];
    NSSize contextSize = contextFrameInScreen.size;

    NSRect contextRect = screenRect;
    contextRect.origin.x = screenRect.origin.x - contextFrameInScreen.origin.x;
    contextRect.origin.y = screenRect.origin.y - contextFrameInScreen.origin.y;

    NSRect flippedContextRect = contextRect;
    flippedContextRect.origin.y = - (contextRect.origin.y + screenRect.size.height - contextSize.height);
    
    return flippedContextRect;
}

@end

#import <Carbon/Carbon.h>
#import "GBView.h"
#import "GBViewGL.h"
#import "GBViewMetal.h"
#import "GBButtons.h"
#import "NSString+StringForKey.h"

@implementation GBView
{
    uint32_t *image_buffers[3];
    unsigned char current_buffer;
    BOOL mouse_hidden;
    NSTrackingArea *tracking_area;
    BOOL _mouseHidingEnabled;
    bool enableAnalog;
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
    image_buffers[0] = malloc(160 * 144 * 4);
    image_buffers[1] = malloc(160 * 144 * 4);
    image_buffers[2] = malloc(160 * 144 * 4);
    _shouldBlendFrameWithPrevious = 1;
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

- (void) ratioKeepingChanged
{
    [self setFrame:self.superview.frame];
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
    free(image_buffers[0]);
    free(image_buffers[1]);
    free(image_buffers[2]);
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
    frame = self.superview.frame;
    if (![[NSUserDefaults standardUserDefaults] boolForKey:@"GBAspectRatioUnkept"]) {
        double ratio = frame.size.width / frame.size.height;
        if (ratio >= 160.0/144.0) {
            double new_width = round(frame.size.height / 144.0 * 160.0);
            frame.origin.x = floor((frame.size.width - new_width) / 2);
            frame.size.width = new_width;
            frame.origin.y = 0;
        }
        else {
            double new_height = round(frame.size.width / 160.0 * 144.0);
            frame.origin.y = floor((frame.size.height - new_height) / 2);
            frame.size.height = new_height;
            frame.origin.x = 0;
        }
    }

    [super setFrame:frame];
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
    dispatch_async(dispatch_get_main_queue(), ^{
        [self setNeedsDisplay:YES];
    });
}

- (uint32_t *) pixels
{
    return image_buffers[(current_buffer + 1) % self.numberOfBuffers];
}

-(void)keyDown:(NSEvent *)theEvent
{
    unsigned short keyCode = theEvent.keyCode;
    bool handled = false;

    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    for (GBButton i = 0; i < GBButtonCount; i++) {
        if ([defaults integerForKey:button_to_preference_name(i)] == keyCode) {
            handled = true;
            switch (i) {
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
                    GB_set_key_state(_gb, (GB_key_t)i, true);
                    break;
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
    for (GBButton i = 0; i < GBButtonCount; i++) {
        if ([defaults integerForKey:button_to_preference_name(i)] == keyCode) {
            handled = true;
            switch (i) {
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
                    GB_set_key_state(_gb, (GB_key_t)i, false);
                    break;
            }
        }
    }

    if (!handled && [theEvent type] != NSEventTypeFlagsChanged) {
        [super keyUp:theEvent];
    }
}

- (void) joystick:(NSString *)joystick_name button: (unsigned)button changedState: (bool) state
{
    UpdateSystemActivity(UsrActivity);
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
                    if (i < GB_KEY_A) {
                        enableAnalog = false;
                    }
                    GB_set_key_state(_gb, (GB_key_t)i, state);
                    break;
            }
        }
    }
}

- (void) joystick:(NSString *)joystick_name axis: (unsigned)axis movedTo: (signed) value
{
    UpdateSystemActivity(UsrActivity);
    NSDictionary *mapping = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"GBJoypadMappings"][joystick_name];
    NSNumber *x_axis = [mapping objectForKey:@"XAxis"];
    NSNumber *y_axis = [mapping objectForKey:@"YAxis"];
    
    if (value > 0x4000 ||  value < -0x4000) {
        enableAnalog = true;
    }
    if (!enableAnalog) return;
    
    if (x_axis && [x_axis integerValue] == axis) {
        GB_set_key_state(_gb, GB_KEY_LEFT, value < -0x4000);
        GB_set_key_state(_gb, GB_KEY_RIGHT, value > 0x4000);
    }
    else if (y_axis && [y_axis integerValue] == axis) {
        GB_set_key_state(_gb, GB_KEY_UP, value < -0x4000);
        GB_set_key_state(_gb, GB_KEY_DOWN, value > 0x4000);
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

- (uint32_t *)currentBuffer
{
    return image_buffers[current_buffer];
}

- (uint32_t *)previousBuffer
{
    return image_buffers[(current_buffer + 2) % self.numberOfBuffers];
}

@end

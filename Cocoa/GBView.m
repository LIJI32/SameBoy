#import <IOKit/pwr_mgt/IOPMLib.h>
#import <Carbon/Carbon.h>
#import "GBView.h"
#import "GBViewGL.h"
#import "GBViewMetal.h"
#import "GBButtons.h"
#import "NSString+StringForKey.h"
#import "NSObject+DefaultsObserver.h"
#import "Document.h"

#define JOYSTICK_HIGH 0x4000
#define JOYSTICK_LOW 0x3800

static const uint8_t workboy_ascii_to_key[] = {
    ['0'] = GB_WORKBOY_0,
    ['`'] = GB_WORKBOY_UMLAUT,
    ['1'] = GB_WORKBOY_1,
    ['2'] = GB_WORKBOY_2,
    ['3'] = GB_WORKBOY_3,
    ['4'] = GB_WORKBOY_4,
    ['5'] = GB_WORKBOY_5,
    ['6'] = GB_WORKBOY_6,
    ['7'] = GB_WORKBOY_7,
    ['8'] = GB_WORKBOY_8,
    ['9'] = GB_WORKBOY_9,
    
    ['\r'] = GB_WORKBOY_ENTER,
    [3] = GB_WORKBOY_ENTER,
    
    ['!'] = GB_WORKBOY_EXCLAMATION_MARK,
    ['$'] = GB_WORKBOY_DOLLAR,
    ['#'] = GB_WORKBOY_HASH,
    ['~'] = GB_WORKBOY_TILDE,
    ['*'] = GB_WORKBOY_ASTERISK,
    ['+'] = GB_WORKBOY_PLUS,
    ['-'] = GB_WORKBOY_MINUS,
    ['('] = GB_WORKBOY_LEFT_PARENTHESIS,
    [')'] = GB_WORKBOY_RIGHT_PARENTHESIS,
    [';'] = GB_WORKBOY_SEMICOLON,
    [':'] = GB_WORKBOY_COLON,
    ['%'] = GB_WORKBOY_PERCENT,
    ['='] = GB_WORKBOY_EQUAL,
    [','] = GB_WORKBOY_COMMA,
    ['<'] = GB_WORKBOY_LT,
    ['.'] = GB_WORKBOY_DOT,
    ['>'] = GB_WORKBOY_GT,
    ['/'] = GB_WORKBOY_SLASH,
    ['?'] = GB_WORKBOY_QUESTION_MARK,
    [' '] = GB_WORKBOY_SPACE,
    ['\''] = GB_WORKBOY_QUOTE,
    ['@'] = GB_WORKBOY_AT,
    
    ['q'] = GB_WORKBOY_Q,
    ['w'] = GB_WORKBOY_W,
    ['e'] = GB_WORKBOY_E,
    ['r'] = GB_WORKBOY_R,
    ['t'] = GB_WORKBOY_T,
    ['y'] = GB_WORKBOY_Y,
    ['u'] = GB_WORKBOY_U,
    ['i'] = GB_WORKBOY_I,
    ['o'] = GB_WORKBOY_O,
    ['p'] = GB_WORKBOY_P,
    ['a'] = GB_WORKBOY_A,
    ['s'] = GB_WORKBOY_S,
    ['d'] = GB_WORKBOY_D,
    ['f'] = GB_WORKBOY_F,
    ['g'] = GB_WORKBOY_G,
    ['h'] = GB_WORKBOY_H,
    ['j'] = GB_WORKBOY_J,
    ['k'] = GB_WORKBOY_K,
    ['l'] = GB_WORKBOY_L,
    ['z'] = GB_WORKBOY_Z,
    ['x'] = GB_WORKBOY_X,
    ['c'] = GB_WORKBOY_C,
    ['v'] = GB_WORKBOY_V,
    ['b'] = GB_WORKBOY_B,
    ['n'] = GB_WORKBOY_N,
    ['m'] = GB_WORKBOY_M,
};

static const uint8_t workboy_vk_to_key[] = {
    [kVK_F1] = GB_WORKBOY_CLOCK,
    [kVK_F2] = GB_WORKBOY_TEMPERATURE,
    [kVK_F3] = GB_WORKBOY_MONEY,
    [kVK_F4] = GB_WORKBOY_CALCULATOR,
    [kVK_F5] = GB_WORKBOY_DATE,
    [kVK_F6] = GB_WORKBOY_CONVERSION,
    [kVK_F7] = GB_WORKBOY_RECORD,
    [kVK_F8] = GB_WORKBOY_WORLD,
    [kVK_F9] = GB_WORKBOY_PHONE,
    [kVK_F10] = GB_WORKBOY_UNKNOWN,
    [kVK_Delete] = GB_WORKBOY_BACKSPACE,
    [kVK_Shift] = GB_WORKBOY_SHIFT_DOWN,
    [kVK_RightShift] = GB_WORKBOY_SHIFT_DOWN,
    [kVK_UpArrow] = GB_WORKBOY_UP,
    [kVK_DownArrow] = GB_WORKBOY_DOWN,
    [kVK_LeftArrow] = GB_WORKBOY_LEFT,
    [kVK_RightArrow] = GB_WORKBOY_RIGHT,
    [kVK_Escape] = GB_WORKBOY_ESCAPE,
    [kVK_ANSI_KeypadDecimal] = GB_WORKBOY_DECIMAL_POINT,
    [kVK_ANSI_KeypadClear] = GB_WORKBOY_M,
    [kVK_ANSI_KeypadMultiply] = GB_WORKBOY_H,
    [kVK_ANSI_KeypadDivide] = GB_WORKBOY_J,
};

@implementation GBView
{
    bool mouse_hidden;
    NSTrackingArea *tracking_area;
    bool _mouseHidingEnabled;
    bool axisActive[2];
    bool underclockKeyDown;
    double clockMultiplier;
    double analogClockMultiplier;
    bool analogClockMultiplierValid;
    NSEventModifierFlags previousModifiers;
    JOYController *lastController;
    bool _turbo;
    bool _mouseControlEnabled;
    NSMutableDictionary<NSNumber *, JOYController *> *_controllerMapping;
    unsigned _lastPlayerCount;
    
    bool _rapidA[4], _rapidB[4];
    uint8_t _rapidACount[4], _rapidBCount[4];
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

- (void) _init
{
    [self registerForDraggedTypes:[NSArray arrayWithObjects: NSFilenamesPboardType, nil]];
    
    __unsafe_unretained GBView *weakSelf = self;
    [self observeStandardDefaultsKey:@"GBAspectRatioUnkept" withBlock:^(id newValue) {
        [weakSelf setFrame:weakSelf.superview.frame];
    }];
    [self observeStandardDefaultsKey:@"JoyKitDefaultControllers" withBlock:^(id newValue) {
        [weakSelf reassignControllers];
    }];
    tracking_area = [ [NSTrackingArea alloc] initWithRect:(NSRect){}
                                                  options:NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect | NSTrackingMouseMoved
                                                    owner:self
                                                 userInfo:nil];
    [self addTrackingArea:tracking_area];
    clockMultiplier = 1.0;
    [self createInternalView];
    [self addSubview:self.internalView];
    self.internalView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [JOYController registerListener:self];
    _mouseControlEnabled = true;
    [self reassignControllers];
}

- (void)controllerConnected:(JOYController *)controller
{
    [self reassignControllers];
}

- (void)controllerDisconnected:(JOYController *)controller
{
    [self reassignControllers];
}

- (unsigned)playerCount
{
    if (self.document.partner) {
        return 2;
    }
    if (!_gb) {
        return 1;
    }
    return GB_get_player_count(_gb);
}

- (void)reassignControllers
{
    unsigned playerCount = self.playerCount;
    /* Don't assign controlelrs if there's only one player, allow all controllers. */
    if (playerCount == 1) {
        _controllerMapping = [NSMutableDictionary dictionary];
        return;
    }
    
    if (!_controllerMapping) {
        _controllerMapping = [NSMutableDictionary dictionary];
    }
    
    for (NSNumber *player in [_controllerMapping copy]) {
        if (player.unsignedIntValue >= playerCount || !_controllerMapping[player].connected) {
            [_controllerMapping removeObjectForKey:player];
        }
    }
    
    _lastPlayerCount = playerCount;
    for (unsigned i = 0; i < playerCount; i++) {
        NSString *preferredJoypad = [[[NSUserDefaults standardUserDefaults] dictionaryForKey:@"JoyKitDefaultControllers"]
                                      objectForKey:n2s(i)];
        for (JOYController *controller in [JOYController allControllers]) {
            if (!controller.connected) continue;
            if ([controller.uniqueID isEqual:preferredJoypad]) {
                _controllerMapping[@(i)] = controller;
                break;
            }
        }
    }
}

- (void)tryAssigningController:(JOYController *)controller
{
    unsigned playerCount = self.playerCount;
    if (playerCount == 1) return;
    if (_controllerMapping.count == playerCount) return;
    if ([_controllerMapping.allValues containsObject:controller]) return;
    for (unsigned i = 0; i < playerCount; i++) {
        if (!_controllerMapping[@(i)]) {
            _controllerMapping[@(i)] = controller;
            return;
        }
    }
}

- (NSDictionary<NSNumber *, JOYController *> *)controllerMapping
{
    if (_lastPlayerCount != self.playerCount) {
        [self reassignControllers];
    }
    
    return _controllerMapping;
}

- (void)screenSizeChanged
{
    [super screenSizeChanged];
    
    dispatch_async(dispatch_get_main_queue(), ^{
        [self setFrame:self.superview.frame];
    });
}

- (void)dealloc
{
    if (mouse_hidden) {
        mouse_hidden = false;
        [NSCursor unhide];
    }
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [self setRumble:0];
    [JOYController unregisterListener:self];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
    if (!(self = [super initWithCoder:coder])) { 
        return self;
    }
    [self _init];
    return self;
}

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (!(self = [super initWithFrame:frameRect])) { 
        return self;
    }
    [self _init];
    return self;
}

- (void)setFrame:(NSRect)frame
{
    frame = self.superview.frame;
    if (_gb && ![[NSUserDefaults standardUserDefaults] boolForKey:@"GBAspectRatioUnkept"]) {
        double ratio = frame.size.width / frame.size.height;
        double width = GB_get_screen_width(_gb);
        double height = GB_get_screen_height(_gb);
        if (ratio >= width / height) {
            double new_width = round(frame.size.height / height * width);
            frame.origin.x = floor((frame.size.width - new_width) / 2);
            frame.size.width = new_width;
            frame.origin.y = 0;
        }
        else {
            double new_height = round(frame.size.width / width * height);
            frame.origin.y = floor((frame.size.height - new_height) / 2);
            frame.size.height = new_height;
            frame.origin.x = 0;
        }
    }

    [super setFrame:frame];
}

- (void) flip
{
    if (analogClockMultiplierValid && [[NSUserDefaults standardUserDefaults] boolForKey:@"GBAnalogControls"]) {
        clockMultiplier = 1.0;
        GB_set_clock_multiplier(_gb, analogClockMultiplier);
        if (self.document.partner) {
            GB_set_clock_multiplier(self.document.partner.gb, analogClockMultiplier);
        }
        if (analogClockMultiplier == 1.0) {
            analogClockMultiplierValid = false;
        }
        if (analogClockMultiplier < 2.0 && analogClockMultiplier > 1.0) {
            GB_set_turbo_mode(_gb, false, false);
            if (self.document.partner) {
                GB_set_turbo_mode(self.document.partner.gb, false, false);
            }
        }
    }
    else {
        if (underclockKeyDown && clockMultiplier > 0.5) {
            clockMultiplier -= 1.0/16;
            GB_set_clock_multiplier(_gb, clockMultiplier);
            if (self.document.partner) {
                GB_set_clock_multiplier(self.document.partner.gb, clockMultiplier);
            }
        }
        if (!underclockKeyDown && clockMultiplier < 1.0) {
            clockMultiplier += 1.0/16;
            GB_set_clock_multiplier(_gb, clockMultiplier);
            if (self.document.partner) {
                GB_set_clock_multiplier(self.document.partner.gb, clockMultiplier);
            }
        }
    }
    if ((!analogClockMultiplierValid && clockMultiplier > 1) ||
        _turbo || (analogClockMultiplierValid && analogClockMultiplier > 1)) {
        [self.osdView displayText:@"Fast forwarding…"];
    }
    else if ((!analogClockMultiplierValid && clockMultiplier < 1) ||
             (analogClockMultiplierValid && analogClockMultiplier < 1)) {
        [self.osdView displayText:@"Slow motion…"];
    }
    
    for (unsigned i = GB_get_player_count(_gb); i--;) {
        if (_rapidA[i]) {
            _rapidACount[i]++;
            GB_set_key_state_for_player(_gb, GB_KEY_A, i, !(_rapidACount[i] & 2));
        }
        if (_rapidB[i]) {
            _rapidBCount[i]++;
            GB_set_key_state_for_player(_gb, GB_KEY_B, i, !(_rapidBCount[i] & 2));
        }
    }
    [super flip];
}

-(void)keyDown:(NSEvent *)theEvent
{
    if ([theEvent type] != NSEventTypeFlagsChanged && theEvent.isARepeat) return;
    unsigned short keyCode = theEvent.keyCode;
    if (GB_workboy_is_enabled(_gb)) {
        if (theEvent.keyCode < sizeof(workboy_vk_to_key) && workboy_vk_to_key[theEvent.keyCode]) {
            GB_workboy_set_key(_gb, workboy_vk_to_key[theEvent.keyCode]);
            return;
        }
        unichar c = [theEvent type] != NSEventTypeFlagsChanged? [theEvent.charactersIgnoringModifiers.lowercaseString characterAtIndex:0] : 0;
        if (c < sizeof(workboy_ascii_to_key) && workboy_ascii_to_key[c]) {
            GB_workboy_set_key(_gb, workboy_ascii_to_key[c]);
            return;
        }
    }
    
    bool handled = false;

    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    unsigned player_count = GB_get_player_count(_gb);
    if (self.document.partner) {
        player_count = 2;
    }
    for (unsigned player = 0; player < player_count; player++) {
        for (GBButton button = 0; button < GBKeyboardButtonCount; button++) {
            NSNumber *key = [defaults valueForKey:button_to_preference_name(button, player)];
            if (!key) continue;

            if (key.unsignedShortValue == keyCode) {
                handled = true;
                switch (button) {
                    case GBTurbo:
                        if (self.document.isSlave) {
                            GB_set_turbo_mode(self.document.partner.gb, true, false);
                        }
                        else {
                            GB_set_turbo_mode(_gb, true, self.isRewinding);
                        }
                        _turbo = true;
                        analogClockMultiplierValid = false;
                        break;
                        
                    case GBRewind:
                        if (!self.document.partner) {
                            self.isRewinding = true;
                            GB_set_turbo_mode(_gb, false, false);
                            _turbo = false;
                        }
                        break;
                        
                    case GBUnderclock:
                        underclockKeyDown = true;
                        analogClockMultiplierValid = false;
                        break;
                        
                    case GBRapidA:
                        _rapidA[player] = true;
                        _rapidACount[player] = 0;
                        GB_set_key_state_for_player(_gb, GB_KEY_A, player, true);
                        break;
                        
                    case GBRapidB:
                        _rapidB[player] = true;
                        _rapidBCount[player] = 0;
                        GB_set_key_state_for_player(_gb, GB_KEY_B, player, true);
                        break;
                        
                    default:
                        if (self.document.partner) {
                            if (player == 0) {
                                GB_set_key_state_for_player(_gb, (GB_key_t)button, 0, true);
                            }
                            else {
                                GB_set_key_state_for_player(self.document.partner.gb, (GB_key_t)button, 0, true);
                            }
                        }
                        else {
                            GB_set_key_state_for_player(_gb, (GB_key_t)button, player, true);
                        }
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
    if (GB_workboy_is_enabled(_gb)) {
        if (keyCode == kVK_Shift || keyCode == kVK_RightShift) {
            GB_workboy_set_key(_gb, GB_WORKBOY_SHIFT_UP);
        }
        else {
            GB_workboy_set_key(_gb, GB_WORKBOY_NONE);
        }

    }
    bool handled = false;

    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    unsigned player_count = GB_get_player_count(_gb);
    if (self.document.partner) {
        player_count = 2;
    }
    for (unsigned player = 0; player < player_count; player++) {
        for (GBButton button = 0; button < GBKeyboardButtonCount; button++) {
            NSNumber *key = [defaults valueForKey:button_to_preference_name(button, player)];
            if (!key) continue;
            
            if (key.unsignedShortValue == keyCode) {
                handled = true;
                switch (button) {
                    case GBTurbo:
                        if (self.document.isSlave) {
                            GB_set_turbo_mode(self.document.partner.gb, false, false);
                        }
                        else {
                            GB_set_turbo_mode(_gb, false, false);
                        }
                        _turbo = false;
                        analogClockMultiplierValid = false;
                        break;
                        
                    case GBRewind:
                        self.isRewinding = false;
                        break;
                        
                    case GBUnderclock:
                        underclockKeyDown = false;
                        analogClockMultiplierValid = false;
                        break;
                    
                    case GBRapidA:
                        _rapidA[player] = false;
                        GB_set_key_state_for_player(_gb, GB_KEY_A, player, false);
                        break;
                        
                    case GBRapidB:
                        _rapidB[player] = false;
                        GB_set_key_state_for_player(_gb, GB_KEY_B, player, false);
                        break;
                        
                    default:
                        if (self.document.partner) {
                            if (player == 0) {
                                GB_set_key_state_for_player(_gb, (GB_key_t)button, 0, false);
                            }
                            else {
                                GB_set_key_state_for_player(self.document.partner.gb, (GB_key_t)button, 0, false);
                            }
                        }
                        else {
                            GB_set_key_state_for_player(_gb, (GB_key_t)button, player, false);
                        }
                        break;
                }
            }
        }
    }
    if (!handled && [theEvent type] != NSEventTypeFlagsChanged) {
        [super keyUp:theEvent];
    }
}

- (void)setRumble:(double)amp
{
    [lastController setRumbleAmplitude:amp];
}

- (bool)shouldControllerUseJoystickForMotion:(JOYController *)controller
{
    if (!_gb) return false;
    if (!GB_has_accelerometer(_gb)) return false;
    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBMBC7JoystickOverride"]) return true;
    for (JOYAxes3D *axes in controller.axes3D) {
        if (axes.usage == JOYAxes3DUsageOrientation || axes.usage == JOYAxes3DUsageAcceleration) {
            return false;
        }
    }
    return true;
}

- (bool)allowController
{
    if ([self.window isMainWindow]) return true;
    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBAllowBackgroundControllers"]) {
        if ([(Document *)[NSApplication sharedApplication].orderedDocuments.firstObject mainWindow] == self.window) {
            return true;
        }
    }
    return false;
}

- (void)controller:(JOYController *)controller movedAxis:(JOYAxis *)axis
{
    if (!_gb) return;
    if (![self allowController]) return;

    NSDictionary *mapping = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"JoyKitInstanceMapping"][controller.uniqueID];
    if (!mapping) {
        mapping = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"JoyKitNameMapping"][controller.deviceName];
    }
    
    if ((axis.usage == JOYAxisUsageR1 && !mapping) ||
        axis.uniqueID == [mapping[@"AnalogUnderclock"] unsignedLongValue]){
        analogClockMultiplier = MIN(MAX(1 - axis.value + 0.05, 1.0 / 3), 1.0);
        analogClockMultiplierValid = true;
    }
    
    else if ((axis.usage == JOYAxisUsageL1 && !mapping) ||
        axis.uniqueID == [mapping[@"AnalogTurbo"] unsignedLongValue]){
        analogClockMultiplier = MIN(MAX(axis.value * 3 + 0.95, 1.0), 3.0);
        analogClockMultiplierValid = true;
    }
}

- (void)controller:(JOYController *)controller movedAxes2D:(JOYAxes2D *)axes
{
    if (!_gb) return;
    if ([self shouldControllerUseJoystickForMotion:controller]) {
        if (!self.mouseControlsActive) {
            GB_set_accelerometer_values(_gb, -axes.value.x, -axes.value.y);
        }
    }
}

- (void)controller:(JOYController *)controller movedAxes3D:(JOYAxes3D *)axes
{
    if (!_gb) return;
    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBMBC7JoystickOverride"]) return;
    if (self.mouseControlsActive) return;
    if (controller != lastController) return;
    // When using Joy-Cons in dual-controller grip, ignore motion data from the left Joy-Con
    if (controller.joyconType == JOYJoyConTypeDual) {
        for (JOYController *child in [(JOYCombinedController *)controller children]) {
            if (child.joyconType != JOYJoyConTypeRight && [child.axes3D containsObject:axes]) {
                return;
            }
        }
    }
    
    NSDictionary<NSNumber *, JOYController *> *controllerMapping = [self controllerMapping];
    GB_gameboy_t *effectiveGB = _gb;
    
    if (self.document.partner) {
        if (controllerMapping[@1] == controller) {
            effectiveGB = self.document.partner.gb;
        }
        if (controllerMapping[@0] != controller) {
            return;
        }
        
    }

    if (axes.usage == JOYAxes3DUsageOrientation) {
        for (JOYAxes3D *axes in controller.axes3D) {
            // Only use orientation if there's no acceleration axes
            if (axes.usage == JOYAxes3DUsageAcceleration) {
                return;
            }
        }
        JOYPoint3D point = axes.normalizedValue;
        GB_set_accelerometer_values(effectiveGB, point.x, point.z);
    }
    else if (axes.usage == JOYAxes3DUsageAcceleration) {
        JOYPoint3D point = axes.gUnitsValue;
        GB_set_accelerometer_values(effectiveGB, point.x, point.z);
    }
}

- (void)controller:(JOYController *)controller buttonChangedState:(JOYButton *)button
{
    if (!_gb) return;
    if (![self allowController]) return;
    _mouseControlEnabled = false;
    if (button.type == JOYButtonTypeAxes2DEmulated && [self shouldControllerUseJoystickForMotion:controller]) return;
    
    [self tryAssigningController:controller];
    
    unsigned playerCount = self.playerCount;

    IOPMAssertionID assertionID;
    IOPMAssertionDeclareUserActivity(CFSTR(""), kIOPMUserActiveLocal, &assertionID);
    
    
    NSDictionary<NSNumber *, JOYController *> *controllerMapping = [self controllerMapping];
    for (unsigned player = 0; player < playerCount; player++) {
        JOYController *preferredJoypad = controllerMapping[@(player)];
        if (preferredJoypad && preferredJoypad != controller) continue; // The player has a different assigned controller
        if (!preferredJoypad && playerCount != 1) continue; // The player has no assigned controller in multiplayer mode, prevent controller inputs
        
        dispatch_async(dispatch_get_main_queue(), ^{
            [controller setPlayerLEDs:[controller LEDMaskForPlayer:player]];
        });
        NSDictionary *mapping = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"JoyKitInstanceMapping"][controller.uniqueID];
        if (!mapping) {
            mapping = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"JoyKitNameMapping"][controller.deviceName];
        }
        
        JOYButtonUsage usage = ((JOYButtonUsage)[mapping[n2s(button.uniqueID)] unsignedIntValue]) ?: button.usage;
        if (!mapping && usage >= JOYButtonUsageGeneric0) {
            usage = GB_inline_const(JOYButtonUsage[], {JOYButtonUsageY, JOYButtonUsageA, JOYButtonUsageB, JOYButtonUsageX})[(usage - JOYButtonUsageGeneric0) & 3];
        }
        
        GB_gameboy_t *effectiveGB = _gb;
        unsigned effectivePlayer = player;
        
        if (player && self.document.partner) {
            effectiveGB = self.document.partner.gb;
            effectivePlayer = 0;
            if (controller != self.document.partner.view->lastController) {
                [self setRumble:0];
                self.document.partner.view->lastController = controller;
            }
        }
        else {
            if (controller != lastController) {
                [self setRumble:0];
                lastController = controller;
            }
        }
        
        switch ((unsigned)usage) {
                
            case JOYButtonUsageNone: break;
            case JOYButtonUsageA: GB_set_key_state_for_player(effectiveGB, GB_KEY_A, effectivePlayer, button.isPressed); break;
            case JOYButtonUsageB: GB_set_key_state_for_player(effectiveGB, GB_KEY_B, effectivePlayer, button.isPressed); break;
            case JOYButtonUsageC: break;
            case JOYButtonUsageStart:
            case JOYButtonUsageX: GB_set_key_state_for_player(effectiveGB, GB_KEY_START, effectivePlayer, button.isPressed); break;
            case JOYButtonUsageSelect:
            case JOYButtonUsageY: GB_set_key_state_for_player(effectiveGB, GB_KEY_SELECT, effectivePlayer, button.isPressed); break;
            case JOYButtonUsageR2:
            case JOYButtonUsageL2:
            case JOYButtonUsageZ: {
                self.isRewinding = button.isPressed;
                if (button.isPressed) {
                    if (self.document.isSlave) {
                        GB_set_turbo_mode(self.document.partner.gb, false, false);
                    }
                    else {
                        GB_set_turbo_mode(_gb, false, false);
                    }
                    _turbo = false;
                }
                break;
            }
        
            case JOYButtonUsageL1: {
                if (!analogClockMultiplierValid || analogClockMultiplier == 1.0 || !button.isPressed) {
                    if (self.document.isSlave) {
                        GB_set_turbo_mode(self.document.partner.gb, button.isPressed, false);
                    }
                    else {
                        GB_set_turbo_mode(_gb, button.isPressed, button.isPressed && self.isRewinding);
                    }
                    _turbo = button.isPressed;
                }
                break;
            }

            case JOYButtonUsageR1: underclockKeyDown = button.isPressed; break;
            case JOYButtonUsageDPadLeft: GB_set_key_state_for_player(effectiveGB, GB_KEY_LEFT, effectivePlayer, button.isPressed); break;
            case JOYButtonUsageDPadRight: GB_set_key_state_for_player(effectiveGB, GB_KEY_RIGHT, effectivePlayer, button.isPressed); break;
            case JOYButtonUsageDPadUp: GB_set_key_state_for_player(effectiveGB, GB_KEY_UP, effectivePlayer, button.isPressed); break;
            case JOYButtonUsageDPadDown: GB_set_key_state_for_player(effectiveGB, GB_KEY_DOWN, effectivePlayer, button.isPressed); break;

            case GBJoyKitRapidA:
                _rapidA[effectivePlayer] = button.isPressed;
                _rapidACount[effectivePlayer] = 0;
                GB_set_key_state_for_player(_gb, GB_KEY_A, effectivePlayer, button.isPressed);
                break;
                
            case GBJoyKitRapidB:
                _rapidB[effectivePlayer] = button.isPressed;
                _rapidBCount[effectivePlayer] = 0;
                GB_set_key_state_for_player(_gb, GB_KEY_B, effectivePlayer, button.isPressed);
                break;
        }
    }
}

- (BOOL)acceptsFirstResponder
{
    return true;
}

- (bool)mouseControlsActive
{
    return _gb && GB_is_inited(_gb) && GB_has_accelerometer(_gb) &&
           _mouseControlEnabled && [[NSUserDefaults standardUserDefaults] boolForKey:@"GBMBC7AllowMouse"];
}

- (void)mouseEntered:(NSEvent *)theEvent
{
    if (!mouse_hidden) {
        mouse_hidden = true;
        if (_mouseHidingEnabled &&
            !self.mouseControlsActive) {
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

- (void)mouseDown:(NSEvent *)event
{
    _mouseControlEnabled = true;
    if (self.mouseControlsActive) {
        if (event.type == NSEventTypeLeftMouseDown) {
            GB_set_key_state(_gb, GB_KEY_A, true);
        }
    }
}

- (void)mouseUp:(NSEvent *)event
{
    if (self.mouseControlsActive) {
        if (event.type == NSEventTypeLeftMouseUp) {
            GB_set_key_state(_gb, GB_KEY_A, false);
        }
    }
}

- (void)mouseMoved:(NSEvent *)event
{
    if (self.mouseControlsActive) {
        NSPoint point = [self convertPoint:[event locationInWindow] toView:nil];
        
        point.x /= self.frame.size.width;
        point.x *= 2;
        point.x -= 1;
        
        point.y /= self.frame.size.height;
        point.y *= 2;
        point.y -= 1;
        
        if (GB_get_screen_width(_gb) != 160) { // has border
            point.x *= 256 / 160.0;
            point.y *= 224 / 114.0;
        }
        GB_set_accelerometer_values(_gb, -point.x, point.y);
    }
}

- (void)setMouseHidingEnabled:(bool)mouseHidingEnabled
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

- (bool)isMouseHidingEnabled
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

-(NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    NSPasteboard *pboard = [sender draggingPasteboard];
    
    if ( [[pboard types] containsObject:NSURLPboardType] ) {
        NSURL *fileURL = [NSURL URLFromPasteboard:pboard];
        if (GB_is_save_state(fileURL.fileSystemRepresentation)) {
            return NSDragOperationGeneric;
        }
    }
    return NSDragOperationNone;
}

-(BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    NSPasteboard *pboard = [sender draggingPasteboard];
    
    if ( [[pboard types] containsObject:NSURLPboardType] ) {
        NSURL *fileURL = [NSURL URLFromPasteboard:pboard];
        return [_document loadStateFile:fileURL.fileSystemRepresentation noErrorOnNotFound:false];
    }

    return false;
}

- (NSImage *)renderToImage;
{
    /* Not going to support this on OpenGL, OpenGL is too much of a terrible API for me
       to bother figuring out how the hell something so trivial can be done. */
    return nil;
}
@end

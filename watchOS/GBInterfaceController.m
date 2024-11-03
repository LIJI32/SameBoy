#import "GBInterfaceController.h"
#import "GBGameScene.h"
#import "GBPhoneManager.h"

#define main(...) RegisterDefaults(void)
#include "../iOS/main.m"
#undef main

@interface GBInterfaceController ()
@property (strong, nonatomic) IBOutlet WKInterfaceSKScene *skInterface;
@end

@implementation GBInterfaceController
{
    GBGameScene *_scene;
    NSTimer *_crownIdleTimer;
    bool _forceBegin;
}
- (void)awakeWithContext:(id)context
{
    RegisterDefaults();
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [[GBPhoneManager sharedManager] refreshSettings:nil];
    });
    [super awakeWithContext:context];

    // Load the SKScene from 'GameScene.sks'
    _scene = [GBGameScene nodeWithFileNamed:@"GBGameScene"];
    _scene.size = [WKInterfaceDevice currentDevice].screenBounds.size;
    
    // Set the scale mode to scale to fit the window
    _scene.scaleMode = SKSceneScaleModeAspectFill;
    
    // Present the scene
    [self.skInterface presentScene:_scene];
    
    // Use a value that will maintain consistent frame rate
    self.skInterface.preferredFramesPerSecond = 60;
}

- (void)stop
{
    [_scene stop];
}

- (void)start
{
    [_scene start];
}

- (bool)isRunning
{
    return _scene.isRunning;
}

- (void)willActivate
{
    // This method is called when watch view controller is about to be visible to user
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [[GBPhoneManager sharedManager] refreshSettings:nil];
    });
    [super willActivate];
    // This slight delay fixes audio start-up issues
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.25 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [_scene start];
    });
    self.crownSequencer.delegate = self;
    [self.crownSequencer focus];
}

- (void)didDeactivate
{
    // This method is called when watch view controller is no longer visible
    [_scene stop];
    [super didDeactivate];
}

- (IBAction)touchEvent:(WKLongPressGestureRecognizer *)sender
{
    static NSTimer *_timer = nil;
    static bool isLong = false, isTap = false;;
    static bool down = false;
    
    if (!down && sender.state != WKGestureRecognizerStateBegan) {
        _forceBegin = true;
    }
    else {
        _forceBegin = false;
    }
    
    if (sender.state == WKGestureRecognizerStateEnded) {
        down = false;
    }
    if (sender.state == WKGestureRecognizerStateBegan || _forceBegin) {
        down = true;
        isLong = false;
        isTap = !_forceBegin;
        if (![[NSUserDefaults standardUserDefaults] boolForKey:@"GBWatchSwipe"]) {
            double x = sender.locationInObject.x / _scene.size.width;
            double y = sender.locationInObject.y / _scene.size.height;
            if (x < BUTTON_WIDTH || x > (1 - BUTTON_WIDTH) ||
                y < BUTTON_WIDTH || y > (1 - BUTTON_WIDTH)) {
                isTap = false;
                [self pan:sender];
                return;
            }
        }
        else {
            [self pan:sender];
        }
        _timer = [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:false block:^(NSTimer *timer) {
            if (sender.state != WKGestureRecognizerStateBegan) {
                return;
            }
            isLong = true;
            isTap = false;
            timer = nil;
            [self longPress:sender];
        }];
    }
    else {
        if (sender.state == WKGestureRecognizerStateEnded && isTap) {
            [self singleTap:sender];
        }
        else if (sender.state == WKGestureRecognizerStateChanged) {
            isTap = false;
        }
        [_timer invalidate];
        _timer = nil;
        if (isLong) {
            [self longPress:sender];
        }
        else if (!isTap) {
            [self pan:sender];
        }
    }
}

- (IBAction)singleTap:(WKLongPressGestureRecognizer *)sender
{    
    if (![[NSUserDefaults standardUserDefaults] boolForKey:@"GBWatchSwipe"]) {
        double x = sender.locationInObject.x / _scene.size.width;
        double y = sender.locationInObject.y / _scene.size.height;
        if (x < BUTTON_WIDTH || x > (1 - BUTTON_WIDTH) ||
            y < BUTTON_WIDTH || y > (1 - BUTTON_WIDTH)) {
            return;
        }
    }
    const char *action = [[NSUserDefaults standardUserDefaults] stringForKey:@"GBWatchDefaultAction"].UTF8String;
    if (strchr(action, 'A')) {
        [_scene holdButton:GB_KEY_A duration:0.25];
    }
    if (strchr(action, 'B')) {
        [_scene holdButton:GB_KEY_B duration:0.25];
    }

}

- (IBAction)longPress:(WKLongPressGestureRecognizer *)sender
{
    static CGPoint start;
    if (sender.state == WKGestureRecognizerStateBegan || _forceBegin) {
        start = sender.locationInObject;
    }
    
    if (![[NSUserDefaults standardUserDefaults] boolForKey:@"GBWatchSwipe"]) {
        double x = start.x / _scene.size.width;
        double y = start.y / _scene.size.height;
        if (x < BUTTON_WIDTH || x > (1 - BUTTON_WIDTH) ||
            y < BUTTON_WIDTH || y > (1 - BUTTON_WIDTH)) {
            return;
        }
    }
    
    if (sender.state == WKGestureRecognizerStateBegan || _forceBegin) {
        [_scene showHoldAt:sender.locationInObject];
    }
    else if (sender.state == WKGestureRecognizerStateEnded) {
        NSString *action = [[NSUserDefaults standardUserDefaults] stringForKey:@"GBWatchDefaultAction"];
        CGPoint end = sender.locationInObject;
        if (![action isEqual:@"A+B"]) {
            double angle = atan2(end.x - start.x, end.y - start.y) / M_PI * 180;

            if (angle > 60) {
                [_scene holdButton:GB_KEY_START duration:0.25];
            }
            else if (angle < -60) {
                [_scene holdButton:GB_KEY_SELECT duration:0.25];
            }
            else {
                [_scene holdButton:[action isEqual:@"A"]? GB_KEY_B :  GB_KEY_A duration:0.25];
            }
        }
        else {
            if (end.y >= start.y) {
                if (end.x >= start.x) {
                    [_scene holdButton:GB_KEY_A duration:0.25];
                }
                else {
                    [_scene holdButton:GB_KEY_B duration:0.25];
                }
            }
            else {
                if (end.x >= start.x) {
                    [_scene holdButton:GB_KEY_START duration:0.25];
                }
                else {
                    [_scene holdButton:GB_KEY_SELECT duration:0.25];
                }
            }
        }
        [_scene hideHold];
    }
}

- (IBAction)pan:(WKLongPressGestureRecognizer *)sender
{
    if (![[NSUserDefaults standardUserDefaults] boolForKey:@"GBWatchSwipe"]) {
        if (sender.state == WKGestureRecognizerStateBegan ||
            sender.state == WKGestureRecognizerStateChanged) {
            double x = sender.locationInObject.x / _scene.size.width;
            double y = sender.locationInObject.y / _scene.size.height;
            
            GB_key_mask_t mask = 0;
            if (x < BUTTON_WIDTH) {
                mask |= GB_KEY_LEFT_MASK;
            }
            else if (x > (1 - BUTTON_WIDTH)) {
                mask |= GB_KEY_RIGHT_MASK;
            }
            
            if (y < BUTTON_WIDTH) {
                mask |= GB_KEY_UP_MASK;
            }
            else if (y > (1 - BUTTON_WIDTH)) {
                mask |= GB_KEY_DOWN_MASK;
            }
            
            if (mask == 0) {
                mask = GB_KEY_A_MASK;
            }
            [_scene setInput:mask];
            return;

        }
        else {
            [_scene setInput:0];
        }
        return;
    }
    static CGPoint start;
    if (sender.state == WKGestureRecognizerStateBegan || _forceBegin) {
        start = sender.locationInObject;
    }
    else if (sender.state == WKGestureRecognizerStateChanged) {
        CGPoint end = sender.locationInObject;
        double distance = sqrt(pow(end.x - start.x, 2) + pow(end.y - start.y, 2));
        if (distance > 32) {
            start.x = end.x + (start.x - end.x) / distance * 32;
            start.y = end.y + (start.y - end.y) / distance * 32;
        }
        double angle = atan2(end.x - start.x, end.y - start.y) / M_PI * 180;
        if (angle < -22.5 - 135) {
            [_scene setInput: GB_KEY_UP_MASK];
        }
        else if (angle < -22.5 - 90) {
            [_scene setInput:GB_KEY_UP_MASK | GB_KEY_LEFT_MASK];
        }
        else if (angle < -22.5 - 45) {
            [_scene setInput: GB_KEY_LEFT_MASK];
        }
        else if (angle < -22.5) {
            [_scene setInput:GB_KEY_DOWN_MASK | GB_KEY_LEFT_MASK];
        }
        else if (angle < 22.5) {
            [_scene setInput:GB_KEY_DOWN_MASK];
        }
        else if (angle < 22.5 + 45) {
            [_scene setInput:GB_KEY_DOWN_MASK | GB_KEY_RIGHT_MASK];
        }
        else if (angle < 22.5 + 90) {
            [_scene setInput:GB_KEY_RIGHT_MASK];
        }
        else if (angle < 22.5 + 135) {
            [_scene setInput:GB_KEY_UP_MASK | GB_KEY_RIGHT_MASK];
        }
        else {
            [_scene setInput:GB_KEY_UP_MASK];
        }
    }
    else {
        [_scene setInput:0];
    }
}

- (void)crownDidBecomeIdle:(WKCrownSequencer *)crownSequencer
{
    [_crownIdleTimer invalidate];
    _crownIdleTimer = [NSTimer scheduledTimerWithTimeInterval:0.5
                                                      repeats:false
                                                        block:^(NSTimer *timer) {
        [_scene setSpeedMultiplayer:1.0];
        [_scene setInput:0];
        [_scene start];
        _crownIdleTimer = nil;
    }];
}

- (void)crownDidRotate:(WKCrownSequencer *)crownSequencer rotationalDelta:(double)rotationalDelta
{
    [_crownIdleTimer invalidate];
    _crownIdleTimer = nil;
    static bool rapidFire = false;
    if (rotationalDelta > 0) {
        [_scene start];
        NSString *action = [[NSUserDefaults standardUserDefaults] stringForKey:@"GBWatchCrownForward"];
        rapidFire ^= true;
        if ([action hasSuffix:@"b"]) {
            [_scene setInput:rapidFire? GB_KEY_B_MASK : 0];
        }
        else if ([action hasSuffix:@"a"]) {
            [_scene setInput:rapidFire? GB_KEY_A_MASK : 0];
        }
        
        if ([action hasPrefix:@"turbo"]) {
            [_scene setSpeedMultiplayer:rotationalDelta * 32 + 1];
        }
    }
    else {
        NSString *action = [[NSUserDefaults standardUserDefaults] stringForKey:@"GBWatchCrownBackward"];
        if ([action isEqual:@"rewind"]) {
            [_scene stop];
            [_scene rewindFrames:ceil(-rotationalDelta * 32)];
            return;
        }
        rapidFire ^= true;
        static NSDictionary *mapping = nil;
        static dispatch_once_t onceToken;
        dispatch_once(&onceToken, ^{
            mapping = @{
                @"a": @(GB_KEY_A_MASK),
                @"b": @(GB_KEY_B_MASK),
                @"start": @(GB_KEY_START_MASK),
                @"select": @(GB_KEY_SELECT_MASK),
            };
        });
        [_scene setInput:rapidFire? [mapping[action] unsignedIntValue] : 0];

    }
}

@end

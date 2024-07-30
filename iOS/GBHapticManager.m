#import "GBHapticManager.h"
#import "GBHapticManagerLegacy.h"
#import <CoreHaptics/CoreHaptics.h>

@implementation GBHapticManager
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
    CHHapticEngine *_engine;
    CHHapticEngine *_externalEngine;
    id<CHHapticPatternPlayer> _rumblePlayer;
#pragma clang diagnostic pop
    __weak GCController *_controller;
    double _rumble;
    dispatch_queue_t _queue;
}

+ (instancetype)sharedManager
{
    static dispatch_once_t onceToken;
    static GBHapticManager *manager;
    dispatch_once(&onceToken, ^{
        manager = [[self alloc] init];
    });
    return manager;
}

- (instancetype)init
{
    self = [super init];
    if (!self) return nil;
    if (self.class != [GBHapticManager class]) return self;
    
    if (@available(iOS 13.0, *)) {
            _engine = [[CHHapticEngine alloc] initAndReturnError:nil];
            _engine.playsHapticsOnly = true;
            _engine.autoShutdownEnabled = true;
    }
    if (!_engine) return [[GBHapticManagerLegacy alloc] init];
    _queue = dispatch_queue_create("SameBoy Haptic Queue", NULL);
    return self;
}

#pragma clang diagnostic ignored "-Wpartial-availability"

- (CHHapticEvent *) eventWithType:(CHHapticEventType)type
                        sharpness:(double)sharpness
                        intensity:(double)intensity
                         duration:(NSTimeInterval)duration
{
    return [[CHHapticEvent alloc] initWithEventType:type
                                         parameters:@[[[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness
                                                                                                    value:sharpness],
                                                      [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity
                                                                                                    value:intensity]]
                                                       relativeTime:CHHapticTimeImmediate
                                                           duration:duration];
}

- (void)doTapHaptic
{
    double intensity = [[NSUserDefaults standardUserDefaults] doubleForKey:@"GBHapticsStrength"];
    if (_rumble > intensity) return;

    dispatch_async(_queue, ^{
        CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[[self eventWithType:CHHapticEventTypeHapticTransient
                                                                                       sharpness:0.25
                                                                                       intensity:intensity
                                                                                        duration:1.0]]
                                                                parameters:nil
                                                                     error:nil];
        @try {
            id<CHHapticPatternPlayer> player = [_engine createPlayerWithPattern:pattern error:nil];
            [player startAtTime:0 error:nil];
        }
        @catch (NSException *exception) {}
    });
}

- (void)setRumbleStrength:(double)rumble
{
    if (!_controller) { // Controller disconnected
        _externalEngine = nil;
    }
    if (!_externalEngine && _controller && !_controller.isAttachedToDevice) {
        /* We have a controller with no rumble support which is not attached to the device,
           don't rumble since the user is holding neither the device nor a haptic-enabled
           controller. */
        rumble = 0;
    }
    if (rumble == 0) {
        @try {
            /* Why must every method from this framework randomly throw exceptions whenever
               anything remotely unusual happens? CoreHaptic sucks.*/
            [_rumblePlayer stopAtTime:0 error:nil];
        }
        @catch (NSException *exception) {}
        _rumblePlayer = nil;
        _rumble = 0;
        return;
    }
    
    // No change
    if (rumble == _rumble) return;
    _rumble = rumble;
    
    dispatch_async(_queue, ^{
        CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[[self eventWithType:CHHapticEventTypeHapticContinuous
                                                                                       sharpness:0.75
                                                                                       intensity:rumble
                                                                                        duration:1.0]]
                                                                parameters:nil
                                                                     error:nil];
        @try {
            id<CHHapticPatternPlayer> newPlayer = [_externalEngine ?: _engine createPlayerWithPattern:pattern error:nil];
            [newPlayer startAtTime:0 error:nil];
            [_rumblePlayer stopAtTime:0 error:nil];
            _rumblePlayer = newPlayer;
        }
        @catch (NSException *exception) {
            if (_externalEngine) {
                // Something might have happened with our controller? Delete and try again
                _externalEngine = nil;
                [self setRumbleStrength: rumble];
            }
        }
    });
}

- (void)setController:(GCController *)controller
{
    if (_controller != controller) {
        if (@available(iOS 14.0, *)) {
            _externalEngine = [controller.haptics createEngineWithLocality:GCHapticsLocalityDefault];
            _externalEngine.playsHapticsOnly = true;
            _externalEngine.autoShutdownEnabled = true;

        }
        _controller = controller;
    }
}

- (GCController *)controller
{
    return _controller;
}

@end

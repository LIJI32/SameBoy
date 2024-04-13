#import "GBHapticManager.h"
#import "GBHapticManagerLegacy.h"
#import <CoreHaptics/CoreHaptics.h>

@implementation GBHapticManager
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
    CHHapticEngine *_engine;
    id<CHHapticPatternPlayer> _rumblePlayer;
#pragma clang diagnostic pop
    double _rumble;
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
#ifndef APPSTORE
    if (!_engine) return [[GBHapticManagerLegacy alloc] init];
#endif
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
    if (_rumble) return;

    CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[[self eventWithType:CHHapticEventTypeHapticTransient
                                                                                    sharpness:0.25
                                                                                    intensity:0.75
                                                                                     duration:1.0]]
                                                             parameters:nil
                                                                  error:nil];
    id<CHHapticPatternPlayer> player = [_engine createPlayerWithPattern:pattern error:nil];
    
    [player startAtTime:0 error:nil];
}

- (void)setRumbleStrength:(double)rumble
{
    if (rumble == 0) {
        [_rumblePlayer stopAtTime:0 error:nil];
        _rumblePlayer = nil;
        _rumble = 0;
        return;
    }
    _rumble = rumble;
    CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[[self eventWithType:CHHapticEventTypeHapticContinuous
                                                                                   sharpness:0.75
                                                                                   intensity:rumble
                                                                                    duration:1.0]]
                                                            parameters:nil
                                                                 error:nil];
    id<CHHapticPatternPlayer> newPlayer = [_engine createPlayerWithPattern:pattern error:nil];
    
    [newPlayer startAtTime:0 error:nil];
    [_rumblePlayer stopAtTime:0 error:nil];
    _rumblePlayer = newPlayer;
}

@end

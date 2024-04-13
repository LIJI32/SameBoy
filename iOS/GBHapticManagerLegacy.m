#import "GBHapticManagerLegacy.h"
#import <UIKit/UIKit.h>
#import <AudioToolbox/AudioToolbox.h>

#ifndef APPSTORE

@implementation GBHapticManagerLegacy

- (void)doTapHaptic
{
    [[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium] impactOccurred];
}

- (void)setRumbleStrength:(double)rumble
{
    void AudioServicesStopSystemSound(SystemSoundID inSystemSoundID);
    void AudioServicesPlaySystemSoundWithVibration(SystemSoundID inSystemSoundID, id arg, NSDictionary* vibratePattern);
    if (rumble) {
        AudioServicesPlaySystemSoundWithVibration(kSystemSoundID_Vibrate, nil, @{@"Intensity": @(rumble),
                                                                                 @"OffDuration": @0,
                                                                                 @"OnDuration": @100,
                                                                                 @"VibePattern": @[@YES, @1000],
        });
    }
    else {
        AudioServicesStopSystemSound(kSystemSoundID_Vibrate);
    }
}
@end

#endif

#import <UIKit/UIKit.h>
#import <Core/gb.h>
#import "GBViewController.h"
#import "GBView.h"

int main(int argc, char * argv[])
{
    @autoreleasepool {
        [[NSUserDefaults standardUserDefaults] registerDefaults:@{
            @"GBFilter": @"NearestNeighbor",
            @"GBColorCorrection": @(GB_COLOR_CORRECTION_MODERN_BALANCED),
            @"GBAudioMode": @"switch",
            @"GBHighpassFilter": @(GB_HIGHPASS_ACCURATE),
            @"GBRewindLength": @(120),
            @"GBFrameBlendingMode": @(GB_FRAME_BLENDING_MODE_ACCURATE),
            
            @"GBDMGModel": @(GB_MODEL_DMG_B),
            @"GBCGBModel": @(GB_MODEL_CGB_E),
            @"GBAGBModel": @(GB_MODEL_AGB_A),
            @"GBSGBModel": @(GB_MODEL_SGB2),
            @"GBRumbleMode": @(GB_RUMBLE_CARTRIDGE_ONLY),
            @"GBButtonHaptics": @YES,
            @"GBHapticsStrength": @0.75,
            @"GBTurboSpeed": @1,
            @"GBRewindSpeed": @1,
            @"GBDynamicSpeed": @NO,
            
            @"GBInterfaceTheme": @"SameBoy",
            @"GBControllersHideInterface": @YES,
                        
            @"GBCurrentTheme": @"Lime (Game Boy)",
            // Default themes
            @"GBThemes": @{
                    @"Desert": @{
                            @"BrightnessBias": @0.0,
                            @"Colors": @[@0xff302f3e, @0xff576674, @0xff839ba4, @0xffb1d0d2, @0xffb7d7d8],
                            @"DisabledLCDColor": @YES,
                            @"HueBias": @0.10087773904382469,
                            @"HueBiasStrength": @0.062142056772908363,
                            @"Manual": @NO,
                    },
                    @"Evening": @{
                            @"BrightnessBias": @-0.10168700106441975,
                            @"Colors": @[@0xff362601, @0xff695518, @0xff899853, @0xffa6e4ae, @0xffa9eebb],
                            @"DisabledLCDColor": @YES,
                            @"HueBias": @0.60027079191058874,
                            @"HueBiasStrength": @0.33816297305747867,
                            @"Manual": @NO,
                    },
                    @"Fog": @{
                            @"BrightnessBias": @0.0,
                            @"Colors": @[@0xff373c34, @0xff737256, @0xff9da386, @0xffc3d2bf, @0xffc7d8c6],
                            @"DisabledLCDColor": @YES,
                            @"HueBias": @0.55750435756972117,
                            @"HueBiasStrength": @0.18424738545816732,
                            @"Manual": @NO,
                    },
                    @"Magic Eggplant": @{
                            @"BrightnessBias": @0.0,
                            @"Colors": @[@0xff3c2136, @0xff942e84, @0xffc7699d, @0xfff1e4b0, @0xfff6f9b2],
                            @"DisabledLCDColor": @YES,
                            @"HueBias": @0.87717878486055778,
                            @"HueBiasStrength": @0.65018052788844627,
                            @"Manual": @NO,
                    },
                    @"Radioactive Pea": @{
                            @"BrightnessBias": @-0.48079556772908372,
                            @"Colors": @[@0xff215200, @0xff1f7306, @0xff169e34, @0xff03ceb8, @0xff00d4d1],
                            @"DisabledLCDColor": @YES,
                            @"HueBias": @0.3795131972111554,
                            @"HueBiasStrength": @0.34337649402390436,
                            @"Manual": @NO,
                    },
                    @"Seaweed": @{
                            @"BrightnessBias": @-0.28532744023904377,
                            @"Colors": @[@0xff3f0015, @0xff426532, @0xff58a778, @0xff95e0df, @0xffa0e7ee],
                            @"DisabledLCDColor": @YES,
                            @"HueBias": @0.2694067480079681,
                            @"HueBiasStrength": @0.51565612549800799,
                            @"Manual": @NO,
                    },
                    @"Twilight": @{
                            @"BrightnessBias": @-0.091789093625498031,
                            @"Colors": @[@0xff3f0015, @0xff461286, @0xff6254bd, @0xff97d3e9, @0xffa0e7ee],
                            @"DisabledLCDColor": @YES,
                            @"HueBias": @0.0,
                            @"HueBiasStrength": @0.49710532868525897,
                            @"Manual": @NO,
                    },
            },
        }];
    }
    return UIApplicationMain(argc, argv, nil, NSStringFromClass([GBViewController class]));
}

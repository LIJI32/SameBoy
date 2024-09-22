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
                    @"Canyon": @{
                            @"BrightnessBias": @0.1227009965823247,
                            @"Colors": @[@0xff0c1e20, @0xff122b91, @0xff466aa2, @0xfff1efae, @0xfff1efae],
                            @"DisabledLCDColor": @NO,
                            @"HueBias": @0.01782661816105247,
                            @"HueBiasStrength": @1,
                            @"Manual": @NO,
                    },
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
                    @"Green Slate": @{
                            @"BrightnessBias": @0.2210012227296829,
                            @"Colors": @[@0xff343117, @0xff6a876f, @0xff98b4a1, @0xffc3daca, @0xffc8decf],
                            @"DisabledLCDColor": @YES,
                            @"HueBias": @0.1887667975388467,
                            @"HueBiasStrength": @0.1272283345460892,
                            @"Manual": @NO,
                    },
                    @"Green Tea": @{
                            @"BrightnessBias": @-0.4946326622596153,
                            @"Colors": @[@0xff1a1d08, @0xff1d5231, @0xff3b9774, @0xff97e4c6, @0xffa9eed1],
                            @"DisabledLCDColor": @YES,
                            @"HueBias": @0.1912955007245464,
                            @"HueBiasStrength": @0.3621708039314516,
                            @"Manual": @NO,
                    },
                    @"Lavender": @{
                            @"BrightnessBias": @0.10072476038566,
                            @"Colors": @[@0xff2b2a3a, @0xff8c507c, @0xffbf82a8, @0xffe9bcce, @0xffeec3d3],
                            @"DisabledLCDColor": @YES,
                            @"HueBias": @0.7914529587142169,
                            @"HueBiasStrength": @0.2498168498277664,
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
                    @"Mystic Blue": @{
                            @"BrightnessBias": @-0.3291049897670746,
                            @"Colors": @[@0xff3b2306, @0xffa27807, @0xffd1b523, @0xfff6ebbe, @0xfffaf1e4],
                            @"DisabledLCDColor": @YES,
                            @"HueBias": @0.5282051088288426,
                            @"HueBiasStrength": @0.7699633836746216,
                            @"Manual": @NO,
                    },
                    @"Pink Pop": @{
                            @"BrightnessBias": @0.624908447265625,
                            @"Colors": @[@0xff28140a, @0xff7c42cb, @0xffaa83de, @0xffd1ceeb, @0xffd5d8ec],
                            @"DisabledLCDColor": @YES,
                            @"HueBias": @0.9477411056868732,
                            @"HueBiasStrength": @0.3433764940239044,
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
                    @"Rose": @{
                            @"BrightnessBias": @0.2727272808551788,
                            @"Colors": @[@0xff001500, @0xff4e1fae, @0xff865ac4, @0xffb7e6d3, @0xffbdffd4],
                            @"DisabledLCDColor": @YES,
                            @"HueBias": @0.9238900924101472,
                            @"HueBiasStrength": @0.9957716464996338,
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

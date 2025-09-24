#import "GBSettingsViewController.h"
#import "GBSlider.h"
#import "GBViewBase.h"
#import "GBThemesViewController.h"
#import "GBPalettePicker.h"
#import "GBHapticManager.h"
#import "GCControllerGetElements.h"
#import <objc/runtime.h>

static NSString const *typeSubmenu = @"submenu";
static NSString const *typeOptionSubmenu = @"optionSubmenu";
static NSString const *typeBlock = @"block";
static NSString const *typeRadio = @"radio";
static NSString const *typeCheck = @"check";
static NSString const *typeDisabled = @"disabled";
static NSString const *typeSeparator = @"separator";
static NSString const *typeSlider = @"slider";
static NSString const *typeLightTemp = @"lightTemp";
static NSString const *typeTurboSlider = @"turboSlider";

@implementation GBSettingsViewController
{
    NSArray<NSDictionary *> *_structure;
    UINavigationController *_detailsNavigation;
    NSArray<NSArray<GBTheme *> *> *_themes; // For prewarming
    bool _iPadRoot;
    __weak UISlider *_turboSlider;
}

+ (UIImage *)settingsImageNamed:(NSString *)name
{
    UIImage *base = [UIImage imageNamed:name];
    UIGraphicsBeginImageContextWithOptions(base.size, false, base.scale);
    UIBezierPath *path = [UIBezierPath bezierPathWithRoundedRect:(CGRect){{0, 0}, base.size} cornerRadius:8];
    CGContextSaveGState(UIGraphicsGetCurrentContext());
    [path addClip];
    [base drawInRect:path.bounds];
    if (@available(iOS 19.0, *)) {
        CGContextRestoreGState(UIGraphicsGetCurrentContext());
        UIImage *overlay = [UIImage imageNamed:@"settingsOverlay"];
        [overlay drawInRect:path.bounds];
    }
    UIImage *ret = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return ret;
}

+ (NSArray<NSDictionary *> *)rootStructure
{
#define QUICK_SUBMENU(title, ...) @{@"type": typeOptionSubmenu, @"title": title, @"submenu": @[@{@"items": __VA_ARGS__}]}
    NSArray<NSDictionary *> *emulationMenu = @[
        @{
            @"header": @"Rewind Duration",
            @"items": @[
                @{@"type": typeRadio, @"pref": @"GBRewindLength", @"title": @"Disabled",   @"value": @0,},
                @{@"type": typeRadio, @"pref": @"GBRewindLength", @"title": @"10 Seconds", @"value": @10,},
                @{@"type": typeRadio, @"pref": @"GBRewindLength", @"title": @"30 Seconds", @"value": @30,},
                @{@"type": typeRadio, @"pref": @"GBRewindLength", @"title": @"1 Minute",   @"value": @60,},
                @{@"type": typeRadio, @"pref": @"GBRewindLength", @"title": @"2 Minutes",  @"value": @120,},
                @{@"type": typeRadio, @"pref": @"GBRewindLength", @"title": @"5 Minutes",  @"value": @300,},
                @{@"type": typeRadio, @"pref": @"GBRewindLength", @"title": @"10 Minutes", @"value": @600,},
            ]
        },
        @{
            @"header": @"Real Time Clock Emulation",
            @"items": @[
                @{@"type": typeRadio, @"pref": @"GBRTCMode", @"title": @"Accurate",   @"value": @(GB_RTC_MODE_ACCURATE),},
                @{@"type": typeRadio, @"pref": @"GBRTCMode", @"title": @"Sync to System Clock", @"value": @(GB_RTC_MODE_SYNC_TO_HOST),},
            ]
        },
        @{
            @"header": @"Turbo Speed Cap",
            @"items": @[
                @{@"type": typeCheck, @"pref": @"GBTurboCap", @"title": @"Cap Turbo Speed", @"block": ^void(GBSettingsViewController *controller) {
                    if ([[NSUserDefaults standardUserDefaults] floatForKey:@"GBTurboCap"] == 1.0) {
                        if (controller->_turboSlider) {
                            [[NSUserDefaults standardUserDefaults] setFloat:controller->_turboSlider.value forKey:@"GBTurboCap"];
                            controller->_turboSlider.enabled = true;
                        }
                        else {
                            [[NSUserDefaults standardUserDefaults] setFloat:2.0 forKey:@"GBTurboCap"];
                        }
                    }
                    else {
                        controller->_turboSlider.enabled = false;
                    }
                }},
                    @{@"type": typeTurboSlider, @"pref": @"GBTurboCap", @"min": @1.5, @"max": @4, @"minImage": @"tortoise.fill", @"maxImage": @"hare.fill"},
            ],
            @"footer": ^NSString *(){
                if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBDynamicSpeed"]) {
                    return @"This setting will have no effect because horizontal swipes are configured to dynamically control speed in the “Controls” settings.";
                }
                if ([[NSUserDefaults standardUserDefaults] doubleForKey:@"GBTurboCap"]) {
                    return [NSString stringWithFormat:@"Turbo speed is capped to %u%%", (unsigned)round([[NSUserDefaults standardUserDefaults] doubleForKey:@"GBTurboCap"] * 100)];
                }
                return @"Turbo speed is not capped";
            },
        },
        @{
            @"header": @"Rewind Speed",
            @"items": @[
                    @{@"type": typeRadio, @"pref": @"GBRewindSpeed", @"title": @"100%", @"value": @1,},
                    @{@"type": typeRadio, @"pref": @"GBRewindSpeed", @"title": @"200%", @"value": @2,},
                    @{@"type": typeRadio, @"pref": @"GBRewindSpeed", @"title": @"400%", @"value": @4,},
            ],
            @"footer": ^NSString *(){
                if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBDynamicSpeed"]) {
                    return @"This setting will have no effect because horizontal swipes are configured to dynamically control speed in the “Controls” settings";
                }
                return @"";
            }
        },
        @{
            @"header": @"Boot ROMs",
            @"items": @[
                @{@"type": typeRadio, @"pref": @"GBCustomBootROMs", @"title": @"Use Built-in Boot ROMs", @"value": @NO,},
                @{@"type": typeRadio, @"pref": @"GBCustomBootROMs", @"title": @"Use Boot ROMs from Files", @"value": @YES,},
                @{@"type": typeBlock, @"title": @"Open Boot ROMs Folder", @"block": ^bool(GBSettingsViewController *controller) {
                    
                    NSString *path = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true)[0];
                    path = [path stringByAppendingPathComponent:@"Boot ROMs"];
                    [[NSFileManager defaultManager] createDirectoryAtPath:path
                                              withIntermediateDirectories:true
                                                               attributes:nil
                                                                    error:nil];
                    
                    
                    [[UIApplication sharedApplication] openURL:[NSURL URLWithString:[NSString stringWithFormat:@"shareddocuments://%@", [path stringByReplacingOccurrencesOfString:@" "
                                                                                                                                                                        withString:@"%20"]]]
                                                       options:nil
                                             completionHandler:nil];

                    return false;
                }},
            ],
            @"footer": @"Put your boot ROM files (dmg_boot.bin, cgb_boot.bin, etc.) in the Boot ROMs folder to use them"
        },
        @{
            @"header": @"Emulated Revisions",
            @"items": @[
                QUICK_SUBMENU(@"Game Boy", @[
                    @{@"type": typeDisabled, @"title": @"DMG-CPU 0"},
                    @{@"type": typeDisabled, @"title": @"DMG-CPU A"},
                    @{@"type": typeRadio, @"pref": @"GBDMGModel", @"title": @"DMG-CPU B", @"value": @(GB_MODEL_DMG_B),},
                    @{@"type": typeDisabled, @"title": @"DMG-CPU C"},
                ]),
                QUICK_SUBMENU(@"Super Game Boy", @[
                    @{@"type": typeRadio, @"pref": @"GBSGBModel", @"title": @"Super Game Boy (NTSC)", @"value": @(GB_MODEL_SGB_NTSC),},
                    @{@"type": typeRadio, @"pref": @"GBSGBModel", @"title": @"Super Game Boy (PAL)",  @"value": @(GB_MODEL_SGB_PAL),},
                    @{@"type": typeRadio, @"pref": @"GBSGBModel", @"title": @"Super Game Boy 2",      @"value": @(GB_MODEL_SGB2),},
                ]),
                QUICK_SUBMENU(@"Game Boy Color", @[
                    @{@"type": typeRadio, @"pref": @"GBCGBModel", @"title": @"CPU CGB 0", @"value": @(GB_MODEL_CGB_0),},
                    @{@"type": typeRadio, @"pref": @"GBCGBModel", @"title": @"CPU CGB A", @"value": @(GB_MODEL_CGB_A),},
                    @{@"type": typeRadio, @"pref": @"GBCGBModel", @"title": @"CPU CGB B", @"value": @(GB_MODEL_CGB_B),},
                    @{@"type": typeRadio, @"pref": @"GBCGBModel", @"title": @"CPU CGB C", @"value": @(GB_MODEL_CGB_C),},
                    @{@"type": typeRadio, @"pref": @"GBCGBModel", @"title": @"CPU CGB D", @"value": @(GB_MODEL_CGB_D),},
                    @{@"type": typeRadio, @"pref": @"GBCGBModel", @"title": @"CPU CGB E", @"value": @(GB_MODEL_CGB_E),},
                ]),
                QUICK_SUBMENU(@"Game Boy Advance", @[
                    @{@"type": typeDisabled, @"title": @"CPU AGB 0 (Early GBA)",},
                    @{@"type": typeRadio, @"pref": @"GBAGBModel", @"title": @"CPU AGB A (GBA)",             @"value": @(GB_MODEL_AGB_A),},
                    @{@"type": typeRadio, @"pref": @"GBAGBModel", @"title": @"CPU AGB A (Game Boy Player)", @"value": @(GB_MODEL_GBP_A),},
                    @{@"type": typeDisabled, @"title": @"CPU AGB B (GBA SP)",},
                    @{@"type": typeDisabled, @"title": @"CPU AGB E (Late GBA SP)",},
                    @{@"type": typeDisabled, @"title": @"CPU AGB E (Late Game Boy Player)",},
                ]),
            ],
            @"footer": @"Changing the emulated revision on active ROMs will take effect after selecting Reset from the menu or changing the emulated model.",
        },
    ];
    
    NSArray<NSDictionary *> *videoMenu = @[
        @{
            @"items": @[
                @{
                    @"title": @"Graphics Filter",
                    @"type": typeOptionSubmenu,
                    @"submenu": @[
                        @{
                            @"header": @"Standard Filters",
                            @"items": @[
                                @{@"type": typeRadio, @"pref": @"GBFilter", @"title": @"Nearest Neighbor (Pixelated)", @"value": @"NearestNeighbor",},
                                @{@"type": typeRadio, @"pref": @"GBFilter", @"title": @"Bilinear (Blurry)", @"value": @"Bilinear",},
                                @{@"type": typeRadio, @"pref": @"GBFilter", @"title": @"Smooth Bilinear (Less blurry)", @"value": @"SmoothBilinear",},
                            ]
                        },
                        @{
                            @"header": @"Screen Filters",
                            @"items": @[
                                @{@"type": typeRadio, @"pref": @"GBFilter", @"title": @"Monochrome LCD Display", @"value": @"MonoLCD",},
                                @{@"type": typeRadio, @"pref": @"GBFilter", @"title": @"LCD Display", @"value": @"LCD",},
                                @{@"type": typeRadio, @"pref": @"GBFilter", @"title": @"CRT Display", @"value": @"CRT",},
                                @{@"type": typeRadio, @"pref": @"GBFilter", @"title": @"Flat CRT Display", @"value": @"FlatCRT",},
                            ]
                        },
                        @{
                            @"header": @"Upscaling Filters",
                            @"items": @[
                                @{@"type": typeRadio, @"pref": @"GBFilter", @"title": @"Scale2x", @"value": @"Scale2x"},
                                @{@"type": typeRadio, @"pref": @"GBFilter", @"title": @"Scale4x", @"value": @"Scale4x"},
                                @{@"type": typeRadio, @"pref": @"GBFilter", @"title": @"Anti-aliased Scale2x", @"value": @"AAScale2x"},
                                @{@"type": typeRadio, @"pref": @"GBFilter", @"title": @"Anti-aliased Scale4x", @"value": @"AAScale4x"},
                                @{@"type": typeRadio, @"pref": @"GBFilter", @"title": @"HQ2x", @"value": @"HQ2x"},
                                @{@"type": typeRadio, @"pref": @"GBFilter", @"title": @"OmniScale", @"value": @"OmniScale"},
                                @{@"type": typeRadio, @"pref": @"GBFilter", @"title": @"OmniScale Legacy", @"value": @"OmniScaleLegacy"},
                                @{@"type": typeRadio, @"pref": @"GBFilter", @"title": @"Anti-aliased OmniScale Legacy", @"value": @"AAOmniScaleLegacy"},
                            ]
                        },
                    ]
                },
            ]
        },
        @{
            @"header": @"Color Correction",
            @"items": @[
                @{@"type": typeRadio, @"pref": @"GBColorCorrection", @"title": @"Disabled",                     @"value": @(GB_COLOR_CORRECTION_DISABLED),},
                @{@"type": typeRadio, @"pref": @"GBColorCorrection", @"title": @"Correct Color Curves",         @"value": @(GB_COLOR_CORRECTION_CORRECT_CURVES),},
                @{@"type": typeSeparator},
                @{@"type": typeRadio, @"pref": @"GBColorCorrection", @"title": @"Modern – Balanced",            @"value": @(GB_COLOR_CORRECTION_MODERN_BALANCED),},
                @{@"type": typeRadio, @"pref": @"GBColorCorrection", @"title": @"Modern – Accurate",            @"value": @(GB_COLOR_CORRECTION_MODERN_ACCURATE),},
                @{@"type": typeRadio, @"pref": @"GBColorCorrection", @"title": @"Modern – Boost contrast",      @"value": @(GB_COLOR_CORRECTION_MODERN_BOOST_CONTRAST),},
                @{@"type": typeSeparator},
                @{@"type": typeRadio, @"pref": @"GBColorCorrection", @"title": @"Reduce Contrast",              @"value": @(GB_COLOR_CORRECTION_REDUCE_CONTRAST),},
                @{@"type": typeRadio, @"pref": @"GBColorCorrection", @"title": @"Harsh Reality (Low Contrast)", @"value": @(GB_COLOR_CORRECTION_LOW_CONTRAST),},
            ],
            @"footer": ^NSString *(){
                return GB_inline_const(NSString *[], {
                    [GB_COLOR_CORRECTION_DISABLED] = @"Colors are directly interpreted as sRGB, resulting in unbalanced colors and inaccurate hues.",
                    [GB_COLOR_CORRECTION_CORRECT_CURVES] = @"Colors have their brightness corrected, but hues remain unbalanced.",
                    [GB_COLOR_CORRECTION_MODERN_BALANCED] = @"Emulates a modern display. Blue contrast is moderately enhanced at the cost of slight hue inaccuracy.",
                    [GB_COLOR_CORRECTION_MODERN_BOOST_CONTRAST] = @"Like Modern – Balanced, but further boosts the contrast of greens and magentas that is lacking on the original hardware.",
                    [GB_COLOR_CORRECTION_REDUCE_CONTRAST] = @"Slightly reduce the contrast to better represent the tint and contrast of the original display.",
                    [GB_COLOR_CORRECTION_LOW_CONTRAST] = @"Harshly reduce the contrast to accurately represent the tint and low contrast of the original display.",
                    [GB_COLOR_CORRECTION_MODERN_ACCURATE] = @"Emulates a modern display. Colors have their hues and brightness corrected.",
                })[MIN(GB_COLOR_CORRECTION_MODERN_ACCURATE, [[NSUserDefaults standardUserDefaults] integerForKey:@"GBColorCorrection"])];
            },
        },
        @{
            @"header": @"Ambient Light Temperature",
            @"items": @[
                @{@"type": typeLightTemp, @"pref": @"GBLightTemperature", @"min": @-1, @"max": @1}
            ],
        },
        @{
            @"header": @"Frame Blending",
            @"items": @[
                @{@"type": typeRadio, @"pref": @"GBFrameBlendingMode", @"title": @"Disabled", @"value": @(GB_FRAME_BLENDING_MODE_DISABLED),},
                @{@"type": typeRadio, @"pref": @"GBFrameBlendingMode", @"title": @"Simple",   @"value": @(GB_FRAME_BLENDING_MODE_SIMPLE),},
                @{@"type": typeRadio, @"pref": @"GBFrameBlendingMode", @"title": @"Accurate", @"value": @(GB_FRAME_BLENDING_MODE_ACCURATE),},
            ]
        },
        @{
            @"items": @[@{
                @"title": @"Monochrome Palette",
                @"type": typeBlock,
                @"block": ^bool(GBSettingsViewController *controller) {
                    UITableViewStyle style = UITableViewStyleGrouped;
                    if (@available(iOS 13.0, *)) {
                        style = UITableViewStyleInsetGrouped;
                    }
                    [controller.navigationController pushViewController:[[GBPalettePicker alloc] initWithStyle:style] animated:true];
                    return true;
                },
                @"pref": @"GBCurrentTheme",
            }],
            @"footer": @"This palette will be used when emulating a monochrome model such as the original Game Boy."
        }
    ];
    
    NSArray<NSDictionary *> *audioMenu = @[
        @{
            @"header": @"Enable Audio",
            @"items": @[
                @{@"type": typeRadio, @"pref": @"GBAudioMode", @"title": @"Never",                     @"value": @"off",},
                @{@"type": typeRadio, @"pref": @"GBAudioMode", @"title": @"Controlled by Silent Mode", @"value": @"switch",},
                @{@"type": typeRadio, @"pref": @"GBAudioMode", @"title": @"Always",                    @"value": @"on",},
            ],

        },
        @{
            @"header": @"High-pass Filter",
            @"items": @[
                @{@"type": typeRadio, @"pref": @"GBHighpassFilter", @"title": @"Disabled (Keep DC Offset)",   @"value": @(GB_HIGHPASS_OFF),},
                @{@"type": typeRadio, @"pref": @"GBHighpassFilter", @"title": @"Accurate (Emulate Hardware)", @"value": @(GB_HIGHPASS_ACCURATE),},
                @{@"type": typeRadio, @"pref": @"GBHighpassFilter", @"title": @"Preserve Waveform",           @"value": @(GB_HIGHPASS_REMOVE_DC_OFFSET),},
            ],
            @"footer": ^NSString *(){
                return GB_inline_const(NSString *[], {
                    [GB_HIGHPASS_OFF] = @"No high-pass filter will be applied. DC offset will be kept, pausing and resuming will trigger audio pops.",
                    [GB_HIGHPASS_ACCURATE] = @"An accurate high-pass filter will be applied, removing the DC offset while somewhat attenuating the bass.",
                    [GB_HIGHPASS_REMOVE_DC_OFFSET] = @"A high-pass filter will be applied to the DC offset itself, removing the DC offset while preserving the waveform.",
                })[MIN(GB_HIGHPASS_REMOVE_DC_OFFSET, [[NSUserDefaults standardUserDefaults] integerForKey:@"GBHighpassFilter"])];
            },
        },
        @{
            @"header": @"Interference volume",
            @"items": @[
                @{@"type": typeSlider, @"pref": @"GBInterferenceVolume", @"min": @0, @"max": @1, @"minImage": @"speaker.fill", @"maxImage": @"speaker.3.fill"}
            ],
        },
    ];
    
    NSArray<NSDictionary *> *controlsMenu = @[
        @{
            @"items": @[
                @{@"type": typeBlock, @"title": @"Configure Game Controllers", @"block": ^bool(GBSettingsViewController *controller){
                    return [controller configureGameControllers];
                }},
            ],
        },
        @{
            @"header": @"D-pad Style",
            @"items": @[
                @{@"type": typeRadio, @"pref": @"GBSwipeDpad", @"title": @"Standard", @"value": @NO,},
                @{@"type": typeRadio, @"pref": @"GBSwipeDpad", @"title": @"Swipe",    @"value": @YES,},
            ],
            @"footer": ^NSString *(){
                if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBSwipeDpad"]) {
                    return @"Directional input is determined by the swipe direction.";
                }
                return @"Directional input is determined by the touch position.";
            },
        },
        @{
            @"items": @[
                @{@"type": typeCheck, @"pref": @"GBEnableABCombo", @"title": @"Single-Touch A+B"},
            ],
            @"footer": @"Enable this option to allow pressing A+B by touching the space between the two buttons",
        },
        @{
            @"header": @"Horizontal Swipe Behavior",
            @"items": @[
                @{@"type": typeCheck, @"pref": @"GBDynamicSpeed", @"title": @"Dynamically Control Speed"},
                @{@"type": typeCheck, @"pref": @"GBSwipeLock", @"title": @"Lock After Swiping"},
            ],
            @"footer": ^NSString *(){
                if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBSwipeLock"]) {
                    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBDynamicSpeed"]) {
                        return @"Swipe right on the Game Boy screen to play forward, and swipe left to rewind. Tap on the Game Boy screen to return to normal. The forward and rewind speeds are determinied by the swipe distance.";
                    }
                    return @"Swipe right on the Game Boy screen to fast-forward, and swipe left to rewind. Tap on the Game Boy screen to return to normal. The turbo and rewind speeds can be configured under “Emulation” settings.";
                }
                
                if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBDynamicSpeed"]) {
                    return @"Swipe right on the Game Boy screen to play forward, and swipe left to rewind. Raise the touch to return to normal. The forward and rewind speeds are determinied by the swipe distance.";
                }
                return @"Swipe right on the Game Boy screen to fast-forward, and swipe left to rewind. Raise the touch to return to normal. The turbo and rewind speeds can be configured under “Emulation” settings.";
            },
        },
        @{
            @"header": @"Quick Save and Load",
            @"items": @[
                @{@"type": typeCheck, @"pref": @"GBSwipeState", @"title": @"Swipe to Save and Load from Slot 1"},
            ],
            @"footer": ^NSString *(){
                if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBSwipeState"]) {
                    return @"Swipe down on the Game Boy to save the state into state slot 1. Swipe up to load the state from state slot 1.";
                }
                return @" "; // This space is needed, otherwise UITableView spacing breaks
            },
        },
        @{
            @"header": @"Enable Rumble",
            @"items": @[
                @{@"type": typeRadio, @"pref": @"GBRumbleMode", @"title": @"Never",                        @"value": @(GB_RUMBLE_DISABLED),},
                @{@"type": typeRadio, @"pref": @"GBRumbleMode", @"title": @"For Rumble-Enabled Game Paks", @"value": @(GB_RUMBLE_CARTRIDGE_ONLY),},
                @{@"type": typeRadio, @"pref": @"GBRumbleMode", @"title": @"Always",                       @"value": @(GB_RUMBLE_ALL_GAMES),},
            ],
        },
        @{
            @"items": @[
                @{@"type": typeCheck, @"pref": @"GBControllersHideInterface", @"title": @"Hide UI While Using a Controller"},
            ],
            @"footer": @"When enabled, the on-screen user interface will be hidden while a game controller is being used."
        },
        @{
            @"header": @"Controller Joystick Behavior",
            @"items": @[
                @{@"type": typeRadio, @"pref": @"GBFauxAnalogInputs", @"title": @"Digital",     @"value": @NO},
                @{@"type": typeRadio, @"pref": @"GBFauxAnalogInputs", @"title": @"Faux Analog", @"value": @YES},
            ],
        },
        @{
            @"items": @[
                @{@"type": typeCheck, @"pref": @"GBButtonHaptics", @"title": @"Enable Button Haptics"},
                @{@"type": typeSlider, @"pref": @"GBHapticsStrength", @"min": @0.25, @"max": @1, @"minImage": @"waveform.weak", @"maxImage": @"waveform",
                  @"previewBlock": ^void(void){
                      [[GBHapticManager sharedManager] doTapHaptic];
                  }
                }
            ],
        },

    ];
    
    
    NSArray *rootItems = @[
                    @{
                        @"title": @"Emulation",
                        @"type": typeSubmenu,
                        @"submenu": emulationMenu,
                        @"image": [self settingsImageNamed:@"emulationSettings"],
                    },
                    @{
                        @"title": @"Video",
                        @"type": typeSubmenu,
                        @"submenu": videoMenu,
                        @"image": [self settingsImageNamed:@"videoSettings"],
                    },
                    @{
                        @"title": @"Audio",
                        @"type": typeSubmenu,
                        @"submenu": audioMenu,
                        @"image": [self settingsImageNamed:@"audioSettings"],
                    },
                    @{
                        @"title": @"Controls",
                        @"type": typeSubmenu,
                        @"submenu": controlsMenu,
                        @"image": [self settingsImageNamed:@"controlsSettings"],
                    },
                    @{
                        @"title": @"Themes",
                        @"type": typeSubmenu,
                        @"class": [GBThemesViewController class],
                        @"image": [self settingsImageNamed:@"themeSettings"],
                    },
    ];
    

     return @[
        @{
            @"items": rootItems,
        }
    ];
}


+ (UIViewController *)settingsViewControllerWithLeftButton:(UIBarButtonItem *)button
{
    UITableViewStyle style = UITableViewStyleGrouped;
    if (@available(iOS 13.0, *)) {
        style = UITableViewStyleInsetGrouped;
    }
    GBSettingsViewController *root = [[self alloc] initWithStructure:[self rootStructure] title:@"Settings" style:style];
    [root preloadThemePreviews];
    UINavigationController *controller = [[UINavigationController alloc] initWithRootViewController:root];
    [controller.visibleViewController.navigationItem setLeftBarButtonItem:button];
    if ([UIDevice currentDevice].userInterfaceIdiom != UIUserInterfaceIdiomPad) {
        return controller;
    }
    
    UISplitViewController *split = nil;
    if (@available(iOS 14.5, *)) {
        split = [[UISplitViewController alloc] initWithStyle:UISplitViewControllerStyleDoubleColumn];
        split.displayModeButtonVisibility = UISplitViewControllerDisplayModeButtonVisibilityNever;
    }
    else {
        split = [[UISplitViewController alloc] init];
    }
    UIViewController *blank = [[UIViewController alloc] init];
    blank.view.backgroundColor = root.view.backgroundColor;
    root->_detailsNavigation = [[UINavigationController alloc] initWithRootViewController:blank];
    split.viewControllers = @[controller, root->_detailsNavigation];
    split.preferredDisplayMode = UISplitViewControllerDisplayModeAllVisible;
    root->_iPadRoot = true;
    return split;
}

static UIImage *ImageForController(GCController *controller)
{
    if (@available(iOS 13.0, *)) {
        
        NSString *symbolName = @"gamecontroller.fill";
        UIColor *color = [UIColor grayColor];
        
        if (@available(iOS 14.5, *)) {
            if ([controller.extendedGamepad isKindOfClass:[GCDualSenseGamepad class]]) {
                symbolName = @"logo.playstation";
                color = [UIColor colorWithRed:0 green:0x30 / 255.0 blue:0x87 / 255.0 alpha:1.0];
            }
        }
        if (@available(iOS 14.0, *)) {
            if ([controller.extendedGamepad isKindOfClass:[GCDualShockGamepad class]]) {
                symbolName = @"logo.playstation";
                color = [UIColor colorWithRed:0 green:0x30 / 255.0 blue:0x87 / 255.0 alpha:1.0];
            }
            if ([controller.extendedGamepad isKindOfClass:[GCXboxGamepad class]]) {
                symbolName = @"logo.xbox";
                color = [UIColor colorWithRed:0xe / 255.0 green:0x7a / 255.0 blue:0xd / 255.0 alpha:1.0];
            }
        }
        
        UIImage *glyph = [[UIImage systemImageNamed:symbolName] imageWithTintColor:[UIColor whiteColor]];
        if (!glyph) {
            glyph = [[UIImage systemImageNamed:@"gamecontroller.fill"] imageWithTintColor:[UIColor whiteColor]];
        }
        
        UIGraphicsBeginImageContextWithOptions((CGSize){29, 29}, false, [UIScreen mainScreen].scale);
        [color setFill];
        [[UIBezierPath bezierPathWithRoundedRect:CGRectMake(0, 0, 29, 29) cornerRadius:7] fill];
        double height = 25 / glyph.size.width * glyph.size.height;
        [glyph drawInRect:CGRectMake(2, (29 - height) / 2, 25, height)];
        
        UIImage *ret = UIGraphicsGetImageFromCurrentImageContext();
        UIGraphicsEndImageContext();
        return ret;
    }
    return nil;
    
}

+ (GBButton)controller:(GCController *)controller convertUsageToButton:(GBControllerUsage)usage
{
    bool isSony = false;
    if (@available(iOS 14.5, *)) {
        if ([controller.extendedGamepad isKindOfClass:[GCDualSenseGamepad class]]) {
            isSony = true;
        }
    }
    if (@available(iOS 14.0, *)) {
        if ([controller.extendedGamepad isKindOfClass:[GCDualShockGamepad class]]) {
            isSony = true;
        }
    }
    
    NSNumber *mapping = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"GBControllerMappings"][controller.vendorName][[NSString stringWithFormat:@"%u", usage]];
    if (mapping) {
        return mapping.intValue;
    }
    
    switch (usage) {
        case GBUsageButtonA: return isSony? GBB : GBA;
        case GBUsageButtonB: return isSony? GBA : GBB;
        case GBUsageButtonX: return isSony? GBSelect : GBStart;
        case GBUsageButtonY: return isSony? GBStart : GBSelect;
        case GBUsageButtonMenu: return GBStart;
        case GBUsageButtonOptions: return GBSelect;
        case GBUsageButtonHome: return GBStart;
        case GBUsageLeftShoulder: return GBRewind;
        case GBUsageRightShoulder: return GBTurbo;
        case GBUsageLeftTrigger: return GBUnderclock;
        case GBUsageRightTrigger: return GBTurbo;
        default: return GBUnusedButton;
    }
}

static NSString *LocalizedNameForElement(GCControllerElement *element, GBControllerUsage usage)
{
    if (@available(iOS 14.0, *)) {
        NSString *ret = element.localizedName;
        if (ret) {
            return element.localizedName;
        }
    }
    switch (usage) {
        case GBUsageDpad: return @"D-Pad";
        case GBUsageButtonA: return @"A";
        case GBUsageButtonB: return @"B";
        case GBUsageButtonX: return @"X";
        case GBUsageButtonY: return @"Y";
        case GBUsageButtonMenu: return @"Menu";
        case GBUsageButtonOptions: return @"Options";
        case GBUsageButtonHome: return @"Home";
        case GBUsageLeftThumbstick: return @"Left Thumbstick";
        case GBUsageRightThumbstick: return @"Right Thumbstick";
        case GBUsageLeftShoulder: return @"Left Shoulder";
        case GBUsageRightShoulder: return @"Right Shoulder";
        case GBUsageLeftTrigger: return @"Left Trigger";
        case GBUsageRightTrigger: return @"Right Trigger";
        case GBUsageLeftThumbstickButton: return @"Left Thumbstick Button";
        case GBUsageRightThumbstickButton: return @"Right Thumbstick Button";
        case GBUsageTouchpadButton: return @"Touchpad Button";
        default: {
            if (@available(iOS 14.0, *)) {
                NSArray *aliases = [element.aliases.allObjects sortedArrayUsingSelector:@selector(compare:)];
                if (aliases.count) {
                    return aliases[0];
                }
            }
        }
    }
    
    return @"Button";
}

- (void)configureGameController:(GCController *)controller
{
    NSMutableArray *items = [NSMutableArray array];
    NSDictionary <NSNumber *, GCControllerElement *> *elementsDict = GCControllerGetElements(controller);
    for (NSNumber *usage in [[elementsDict allKeys] sortedArrayUsingSelector:@selector(compare:)]) {
        GCControllerElement *element = elementsDict[usage];
        if (![element isKindOfClass:[GCControllerButtonInput class]]) continue;
 
        id (^getter)(void) = ^id(void) {
            return @([GBSettingsViewController controller:controller convertUsageToButton:usage.intValue]);
        };
        
        void (^setter)(id) = ^void(id value) {
            NSMutableDictionary *mapping = ([[NSUserDefaults standardUserDefaults] dictionaryForKey:@"GBControllerMappings"] ?: @{}).mutableCopy;
            
            NSMutableDictionary *vendorMapping = ((NSDictionary *)mapping[controller.vendorName] ?: @{}).mutableCopy;
            vendorMapping[usage.stringValue] = value;
            mapping[controller.vendorName] = vendorMapping;
            [[NSUserDefaults standardUserDefaults] setObject:mapping forKey:@"GBControllerMappings"];
        };

        bool isDpadElement = [element.collection isKindOfClass:[GCControllerDirectionPad class]];
        
        NSDictionary *item = @{
            @"title": LocalizedNameForElement(element, usage.unsignedIntValue),
            @"type": typeOptionSubmenu,
            @"submenu": @[@{@"items": @[
                @{@"type": typeRadio, @"getter": getter, @"setter": setter, @"title": isDpadElement? @"D-pad" : @"None", @"value": @(GBUnusedButton)},
                @{@"type": typeRadio, @"getter": getter, @"setter": setter, @"title": @"Right",                          @"value": @(GBRight)},
                @{@"type": typeRadio, @"getter": getter, @"setter": setter, @"title": @"Left",                           @"value": @(GBLeft)},
                @{@"type": typeRadio, @"getter": getter, @"setter": setter, @"title": @"Up",                             @"value": @(GBUp)},
                @{@"type": typeRadio, @"getter": getter, @"setter": setter, @"title": @"Down",                           @"value": @(GBDown)},
                @{@"type": typeRadio, @"getter": getter, @"setter": setter, @"title": @"A",                              @"value": @(GBA)},
                @{@"type": typeRadio, @"getter": getter, @"setter": setter, @"title": @"B",                              @"value": @(GBB)},
                @{@"type": typeRadio, @"getter": getter, @"setter": setter, @"title": @"Select",                         @"value": @(GBSelect)},
                @{@"type": typeRadio, @"getter": getter, @"setter": setter, @"title": @"Start",                          @"value": @(GBStart)},
                @{@"type": typeRadio, @"getter": getter, @"setter": setter, @"title": @"Rapid A",                        @"value": @(GBRapidA)},
                @{@"type": typeRadio, @"getter": getter, @"setter": setter, @"title": @"Rapid B",                        @"value": @(GBRapidB)},
                @{@"type": typeRadio, @"getter": getter, @"setter": setter, @"title": @"Turbo",                          @"value": @(GBTurbo)},
                @{@"type": typeRadio, @"getter": getter, @"setter": setter, @"title": @"Rewind",                         @"value": @(GBRewind)},
                @{@"type": typeRadio, @"getter": getter, @"setter": setter, @"title": @"Slow-motion",                    @"value": @(GBUnderclock)},
            ]}],
        };
        if (@available(iOS 14.0, *)) {
            UIImage *image = [[UIImage systemImageNamed:element.sfSymbolsName] imageWithTintColor:UIColor.labelColor renderingMode:UIImageRenderingModeAlwaysOriginal];
            if (image) {
                item = [item mutableCopy];
                ((NSMutableDictionary *)item)[@"image"] = image;
            }
        }
        [items addObject:item];
    }
    
    UITableViewStyle style = UITableViewStyleGrouped;
    if (@available(iOS 13.0, *)) {
        style = UITableViewStyleInsetGrouped;
    }
    
    GBSettingsViewController *submenu = [[GBSettingsViewController alloc] initWithStructure:@[@{@"items": items}]
                                                                                      title:controller.vendorName
                                                                                      style:style];
    [self.navigationController pushViewController:submenu animated:true];
}

- (bool)configureGameControllers
{
    
    NSMutableArray *items = [NSMutableArray array];
    for (GCController *controller in [GCController controllers]) {
        if (!GCControllerGetElements(controller)) continue;
        NSDictionary *item = @{
            @"title": controller.vendorName,
            @"type": typeBlock,
            @"block": ^bool(void) {
                [self configureGameController:controller];
                return true;
            }
        };
        UIImage *image = ImageForController(controller);
        if (image) {
            item = [item mutableCopy];
            ((NSMutableDictionary *)item)[@"image"] = image;
        }
            
        [items addObject:item];
    }
    if (items.count) {
        UITableViewStyle style = UITableViewStyleGrouped;
        if (@available(iOS 13.0, *)) {
            style = UITableViewStyleInsetGrouped;
        }
        
        GBSettingsViewController *submenu = [[GBSettingsViewController alloc] initWithStructure:@[@{@"items": items}]
                                                                                           title:@"Configure Game Controllers"
                                                                                           style:style];
        [self.navigationController pushViewController:submenu animated:true];
    }
    else {
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"No Controllers Connected"
                                                                       message:@"There are no connected game controllers to configure"
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"Close"
                                                  style:UIAlertActionStyleCancel
                                                handler:nil]];
        [self presentViewController:alert animated:true completion:nil];
        return false;
        
    }
    return true;
}

- (instancetype)initWithStructure:(NSArray *)structure title:(NSString *)title style:(UITableViewStyle)style
{
    self = [super initWithStyle:style];
    if (!self) return nil;
    self.title = title;
    _structure = structure;
    return self;
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
    return _structure.count;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    return [_structure[section][@"items"] count];
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section
{
    return _structure[section][@"header"];
}

- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section
{
    if ([_structure[section][@"footer"] respondsToSelector:@selector(invoke)]) {
        return ((NSString *(^)(void))_structure[section][@"footer"])();
    }
    return _structure[section][@"footer"];
}

- (NSDictionary *)itemForIndexPath:(NSIndexPath *)indexPath
{
    return _structure[[indexPath indexAtPosition:0]][@"items"][[indexPath indexAtPosition:1]];
}

- (NSDictionary *)followingItemForIndexPath:(NSIndexPath *)indexPath
{
    NSArray *items = _structure[[indexPath indexAtPosition:0]][@"items"];
    if ([indexPath indexAtPosition:1] + 1 >= items.count) {
        return nil;
    }
    return items[[indexPath indexAtPosition:1] + 1];
}

+ (void)fixSliderTint:(UIView *)view
{
    if ([view isKindOfClass:[UIImageView class]]) {
        view.tintColor = [UIColor systemGrayColor];
    }
    for (UIView *subview in view.subviews) {
        [self fixSliderTint:subview];
    }
}

static id ValueForItem(NSDictionary *item)
{
    if (item[@"getter"]) {
        return ((id(^)(void))item[@"getter"])();
    }
    return [[NSUserDefaults standardUserDefaults] objectForKey:item[@"pref"]] ?: @0;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    NSDictionary *item = [self itemForIndexPath:indexPath];
    
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil];
    cell.textLabel.text = item[@"title"];
    NSString *type = item[@"type"];
    if (type == typeSubmenu || type == typeOptionSubmenu || type == typeBlock) {
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
        cell.selectionStyle = UITableViewCellSelectionStyleBlue;
        if (type == typeOptionSubmenu) {
            for (NSDictionary *section in item[@"submenu"]) {
                for (NSDictionary *item in section[@"items"]) {
                    if (item[@"value"] && [ValueForItem(item) isEqual:item[@"value"]]) {
                        cell.detailTextLabel.text = item[@"title"];
                        break;
                    }
                }
            }
        }
        else if (item[@"pref"]) {
            cell.detailTextLabel.text = [[NSUserDefaults standardUserDefaults] stringForKey:item[@"pref"]];
        }
    }
    else if (type == typeRadio) {
        id settingValue = ValueForItem(item);
        id itemValue = item[@"value"];
        if (settingValue == itemValue || [settingValue isEqual:itemValue]) {
            cell.accessoryType = UITableViewCellAccessoryCheckmark;
        }
    }
    else if (type == typeCheck) {
        UISwitch *button = [[UISwitch alloc] init];
        cell.accessoryView = button;
        if ([[NSUserDefaults standardUserDefaults] boolForKey:item[@"pref"]]) {
            button.on = true;
        }
        
        __weak typeof(self) weakSelf = self;
        id block = ^(){
            [[NSUserDefaults standardUserDefaults] setBool:button.on forKey:item[@"pref"]];
            if (item[@"block"]) {
                ((void(^)(GBSettingsViewController *))item[@"block"])(self);
            }
            unsigned section = [indexPath indexAtPosition:0];
            UITableViewHeaderFooterView *view = [weakSelf.tableView footerViewForSection:section];
            view.textLabel.text = [weakSelf tableView:weakSelf.tableView titleForFooterInSection:section];
            [UIView setAnimationsEnabled:false];
            [weakSelf.tableView beginUpdates];
            [view sizeToFit];
            [weakSelf.tableView endUpdates];
            [UIView setAnimationsEnabled:true];
        };
        objc_setAssociatedObject(cell, "RetainedBlock", block, OBJC_ASSOCIATION_RETAIN);
        
        [button addTarget:block action:@selector(invoke) forControlEvents:UIControlEventValueChanged];
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
    }
    else if (type == typeDisabled) {
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
        if (@available(iOS 13.0, *)) {
            cell.textLabel.textColor = [UIColor separatorColor];
        }
        else {
            cell.textLabel.textColor = [UIColor colorWithWhite:0 alpha:0.75];
        }
    }
    else if (type == typeSeparator) {
        cell.backgroundColor = [UIColor clearColor];
        cell.separatorInset = UIEdgeInsetsZero;
    }
    else if (type == typeSlider ||
             type == typeLightTemp ||
             type == typeTurboSlider) {
        CGRect rect = cell.contentView.bounds;
        rect.size.width -= 24;
        rect.size.height -= 24;
        rect.origin.x += 12;
        rect.origin.y += 12;
        UISlider *slider = [type == typeLightTemp? [GBSlider alloc] : [UISlider alloc] initWithFrame:rect];
        slider.continuous = true;
        slider.minimumValue = [item[@"min"] floatValue];
        slider.maximumValue = [item[@"max"] floatValue];
        slider.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        [cell.contentView addSubview:slider];
        if (type == typeTurboSlider) {
            slider.value = [[NSUserDefaults standardUserDefaults] floatForKey:item[@"pref"]] ?: 2.0;
            _turboSlider = slider;
            if (![[NSUserDefaults standardUserDefaults] floatForKey:item[@"pref"]]) {
                slider.enabled = false;
            }
        }
        else {
            slider.value = [[NSUserDefaults standardUserDefaults] floatForKey:item[@"pref"]];
        }
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
        
        if (@available(iOS 13.0, *)) {
            if (item[@"minImage"] && item[@"maxImage"]) {
                slider.minimumValueImage = [UIImage systemImageNamed:item[@"minImage"]] ?: [[UIImage imageNamed:item[@"minImage"]] imageWithRenderingMode:UIImageRenderingModeAlwaysTemplate];
                slider.maximumValueImage = [UIImage systemImageNamed:item[@"maxImage"]] ?: [[UIImage imageNamed:item[@"maxImage"]] imageWithRenderingMode:UIImageRenderingModeAlwaysTemplate];
                [GBSettingsViewController fixSliderTint:slider];
            }
        }
        
        __weak typeof(self) weakSelf = self;
        id block = ^(){
            [[NSUserDefaults standardUserDefaults] setDouble:slider.value forKey:item[@"pref"]];
            if (type == typeTurboSlider) {
                unsigned section = [indexPath indexAtPosition:0];
                UITableViewHeaderFooterView *view = [weakSelf.tableView footerViewForSection:section];
                view.textLabel.text = [weakSelf tableView:weakSelf.tableView titleForFooterInSection:section];
                [UIView setAnimationsEnabled:false];
                [weakSelf.tableView beginUpdates];
                [view sizeToFit];
                [weakSelf.tableView endUpdates];
                [UIView setAnimationsEnabled:true];
            }
        };
        objc_setAssociatedObject(cell, "RetainedBlock", block, OBJC_ASSOCIATION_RETAIN);

        [slider addTarget:block action:@selector(invoke) forControlEvents:UIControlEventValueChanged];
        if (item[@"previewBlock"]) {
            [slider addTarget:item[@"previewBlock"] action:@selector(invoke) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchDown];
        }
    }
    
    if ([self followingItemForIndexPath:indexPath][@"type"] == typeSeparator) {
        cell.separatorInset = UIEdgeInsetsZero;
    }
    cell.imageView.image = item[@"image"];
    if (@available(iOS 19.0, *)) {
        if (_iPadRoot) {
            cell.textLabel.textColor = [UIColor colorWithDynamicProvider:^UIColor *(UITraitCollection *traitCollection) {
                return cell.isSelected? [UIColor whiteColor] : [UIColor labelColor];
            }];
        }
    }
    return cell;
}

- (NSIndexPath *)tableView:(UITableView *)tableView willSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
    NSDictionary *item = [self itemForIndexPath:indexPath];
    NSString *type = item[@"type"];
    if (type == typeSubmenu || type == typeOptionSubmenu) {
        UITableViewStyle style = UITableViewStyleGrouped;
        if (@available(iOS 13.0, *)) {
            style = UITableViewStyleInsetGrouped;
        }
        UITableViewController *submenu = nil;
        
        if (item[@"class"]) {
            submenu = [(UITableViewController *)[item[@"class"] alloc] initWithStyle:style];
            submenu.title = item[@"title"];
        }
        else {
            submenu = [[GBSettingsViewController alloc] initWithStructure:item[@"submenu"]
                                                                    title:item[@"title"]
                                                                    style:style];
        }
        if (_detailsNavigation) {
            [_detailsNavigation setViewControllers:@[submenu] animated:false];
        }
        else {
            [self.navigationController pushViewController:submenu animated:true];
        }
        return indexPath;
    }
    else if (type == typeRadio) {
        if (item[@"setter"]) {
            ((void(^)(id))item[@"setter"])(item[@"value"]);
        }
        else {
            [[NSUserDefaults standardUserDefaults] setObject:item[@"value"] forKey:item[@"pref"]];
        }
        [self.tableView reloadData];
    }
    else if (type == typeBlock) {
        if (((bool(^)(GBSettingsViewController *))item[@"block"])(self)) {
            return indexPath;
        }
    }
    return nil;
}

- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)indexPath
{
    NSDictionary *item = [self itemForIndexPath:indexPath];
    NSString *type = item[@"type"];
    if (type == typeSeparator) {
        return 8;
    }
    if (type == typeSlider ||
        type == typeLightTemp ||
        type == typeTurboSlider) {
        return 63;
    }
    return [super tableView:tableView heightForRowAtIndexPath:indexPath];

}

- (void)viewWillAppear:(BOOL)animated
{
    [super viewWillAppear:animated];
    [self.tableView reloadData];
}

- (void)preloadThemePreviews
{
    /* These take some time to render, preload them when loading the root controller */
    _themes = [GBThemesViewController themes];
    double time = 0;
    for (NSArray *section in _themes) {
        for (GBTheme *theme in section) {
            /* Sadly they can't be safely rendered outside the main thread, but we can
               queue each of them individually to not block the main quote for too long. */
            time += 0.1;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, time * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
                [theme verticalPreview];
                [theme horizontalPreview];
            });
        }
    }
}

+ (GBTheme *)themeNamed:(NSString *)name
{
    NSArray *themes = [GBThemesViewController themes];
    for (NSArray *section in themes) {
        for (GBTheme *theme in section) {
            if ([theme.name isEqualToString:name]) {
                return theme;
            }
        }
    }
    
    return [themes.firstObject firstObject];
}

@end

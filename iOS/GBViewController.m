#import "GBViewController.h"
#import "GBHorizontalLayout.h"
#import "GBVerticalLayout.h"
#import "GBViewMetal.h"
#import "GBAudioClient.h"
#import "GBROMManager.h"
#import "GBLibraryViewController.h"
#import "GBBackgroundView.h"
#import "GBHapticManager.h"
#import "GBMenuViewController.h"
#import "GBOptionViewController.h"
#import "GBAboutController.h"
#import "GBSettingsViewController.h"
#import "GBPalettePicker.h"
#import "GBStatesViewController.h"
#import "GBCheckableAlertController.h"
#import "GBPrinterFeedController.h"
#import "GBCheatsController.h"
#import "GCControllerGetElements.h"
#import "GBZipReader.h"
#import <sys/stat.h>
#import <CoreMotion/CoreMotion.h>
#import <dlfcn.h>
#import <objc/runtime.h>

#if !__has_include(<UIKit/UISliderTrackConfiguration.h>)
/* Building with older SDKs */

typedef NS_ENUM(NSInteger, UIMenuSystemElementGroupPreference) {
    UIMenuSystemElementGroupPreferenceAutomatic = 0,
    UIMenuSystemElementGroupPreferenceRemoved,
    UIMenuSystemElementGroupPreferenceIncluded,
};

API_AVAILABLE(ios(19.0))
@interface UIMainMenuSystemConfiguration : NSObject <NSCopying>
@property (nonatomic, assign) UIMenuSystemElementGroupPreference newScenePreference;
@property (nonatomic, assign) UIMenuSystemElementGroupPreference documentPreference;
@property (nonatomic, assign) UIMenuSystemElementGroupPreference printingPreference;
@property (nonatomic, assign) UIMenuSystemElementGroupPreference findingPreference;
@property (nonatomic, assign) UIMenuSystemElementGroupPreference toolbarPreference;
@property (nonatomic, assign) UIMenuSystemElementGroupPreference sidebarPreference;
@property (nonatomic, assign) UIMenuSystemElementGroupPreference inspectorPreference;
@property (nonatomic, assign) UIMenuSystemElementGroupPreference textFormattingPreference;
@end

API_AVAILABLE(ios(19.0))
@interface UIMainMenuSystem : UIMenuSystem
@property (class, nonatomic, readonly) UIMainMenuSystem *sharedSystem;
- (void)setBuildConfiguration:(UIMainMenuSystemConfiguration *)configuration buildHandler:(void(^)(NSObject<UIMenuBuilder> *builder))buildHandler;
@end

API_AVAILABLE(ios(19.0))
@interface NSObject(UIMenuBuilder)
- (void)insertElements:(NSArray<UIMenuElement *> *)childElements atStartOfMenuForIdentifier:(UIMenuIdentifier)parentIdentifier;
@end

#endif


static UIImage *CreateMenuImage(NSString *name)
{
    static const unsigned size = 20;
    UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(size, size)];
    CGRect destRect = {0,};
    UIImage *source = [UIImage imageNamed:name];
    CGSize sourceSize = source.size;
    if (sourceSize.width > sourceSize.height) {
        destRect.size.width = size;
        destRect.size.height = sourceSize.height * size / sourceSize.width;
        destRect.origin.y = (size - destRect.size.height) / 2;
    }
    else {
        destRect.size.height = size;
        destRect.size.width = sourceSize.width * size / sourceSize.height;
        destRect.origin.x = (size - destRect.size.width) / 2;
    }
    UIImage *image = [renderer imageWithActions:^(UIGraphicsImageRendererContext *myContext) {
        [source drawInRect:destRect];
    }];
    return [image imageWithRenderingMode:UIImageRenderingModeAlwaysTemplate];

}

API_AVAILABLE(ios(13.0))
@implementation UIKeyCommand (KeyCommandWithImage)

+ (instancetype)keyCommandWithInput:(NSString *)input modifierFlags:(UIKeyModifierFlags)modifierFlags action:(SEL)action title:(NSString *)title image:(UIImage *)image
{
    UIKeyCommand *ret = [self keyCommandWithInput:input modifierFlags:modifierFlags action:action];
    ret.title = title;
    ret.image = image;
    return ret;
}

@end

@implementation GBViewController
{
    GB_gameboy_t _gb;
    GBView *_gbView;
    dispatch_queue_t _runQueue;
    
    volatile bool _running;
    volatile bool _stopping;
    bool _rewind;
    bool _rewindOver;
    bool _romLoaded;
    bool _swappingROM;
    bool _skipAutoLoad;
    
    bool _rapidA, _rapidB;
    uint8_t _rapidACount, _rapidBCount;
    
    UIInterfaceOrientation _orientation;
    GBHorizontalLayout *_horizontalLayoutLeft;
    GBHorizontalLayout *_horizontalLayoutRight;
    GBVerticalLayout *_verticalLayout;
    GBBackgroundView *_backgroundView;
    
    NSCondition *_audioLock;
    GB_sample_t *_audioBuffer;
    size_t _audioBufferSize;
    size_t _audioBufferPosition;
    size_t _audioBufferNeeded;
    GBAudioClient *_audioClient;
    
    NSMutableSet *_defaultsObservers;
    GB_palette_t _palette;
    CMMotionManager *_motionManager;
    
    CVImageBufferRef _cameraImage;
    AVCaptureSession *_cameraSession;
    AVCaptureConnection *_cameraConnection;
    AVCaptureVideoDataOutput *_cameraOutput;
    bool _cameraNeedsUpdate;
    NSTimer *_disableCameraTimer;
    AVCaptureDevicePosition _cameraPosition;
    UIButton *_cameraPositionButton;
    UIButton *_changeCameraButton;
    AVCaptureDevice *_frontCaptureDevice;
    AVCaptureDevice *_backCaptureDevice;
    NSMutableArray<NSNumber *> *_zoomLevels;
    unsigned _currentZoomIndex;
    
    __weak GCController *_lastController;
    
    dispatch_queue_t _cameraQueue;
    
    bool _runModeFromController;
    
    UIWindow *_mirrorWindow;
    GBView *_mirrorView;
    
    bool _printerConnected;
    UIButton *_printerButton;
    UIActivityIndicatorView *_printerSpinner;
    NSMutableData *_currentPrinterImageData;
    
    NSString *_lastSavedROM;
    NSDate *_saveDate;
}

static void loadBootROM(GB_gameboy_t *gb, GB_boot_rom_t type)
{
    GBViewController *self = (__bridge GBViewController *)GB_get_user_data(gb);
    [self loadBootROM:type];
}

static void vblank(GB_gameboy_t *gb, GB_vblank_type_t type)
{
    GBViewController *self = (__bridge GBViewController *)GB_get_user_data(gb);
    [self vblankWithType:type];
}


static void printImage(GB_gameboy_t *gb, uint32_t *image, uint8_t height,
                       uint8_t top_margin, uint8_t bottom_margin, uint8_t exposure)
{
    GBViewController *self = (__bridge GBViewController *)GB_get_user_data(gb);
    [self printImage:image height:height topMargin:top_margin bottomMargin:bottom_margin exposure:exposure];
}

static void printDone(GB_gameboy_t *gb)
{
    GBViewController *self = (__bridge GBViewController *)GB_get_user_data(gb);
    [self printDone];
}


static void consoleLog(GB_gameboy_t *gb, const char *string, GB_log_attributes_t attributes)
{
    static NSString *buffer = @"";
    buffer = [buffer stringByAppendingString:@(string)];
    if ([buffer containsString:@"\n"]) {
        NSLog(@"%@", buffer);
        buffer = @"";
    }
}

static uint32_t rgbEncode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    return (r << 0) | (g << 8) | (b << 16) | 0xFF000000;
}

static void audioCallback(GB_gameboy_t *gb, GB_sample_t *sample)
{
    GBViewController *self = (__bridge GBViewController *)GB_get_user_data(gb);
    [self gotNewSample:sample];
}

static void cameraRequestUpdate(GB_gameboy_t *gb)
{
    GBViewController *self = (__bridge GBViewController *)GB_get_user_data(gb);
    [self cameraRequestUpdate];
}

static uint8_t cameraGetPixel(GB_gameboy_t *gb, uint8_t x, uint8_t y)
{
    GBViewController *self = (__bridge GBViewController *)GB_get_user_data(gb);
    return [self cameraGetPixelAtX:x andY:y];
}


static void rumbleCallback(GB_gameboy_t *gb, double amp)
{
    GBViewController *self = (__bridge GBViewController *)GB_get_user_data(gb);
    [self rumbleChanged:amp];
}

- (void)initGameBoy
{
    GB_gameboy_t *gb = &_gb;
    GB_init(gb, [[NSUserDefaults standardUserDefaults] integerForKey:@"GBCGBModel"]);
    GB_set_user_data(gb, (__bridge void *)(self));
    GB_set_boot_rom_load_callback(gb, (GB_boot_rom_load_callback_t)loadBootROM);
    GB_set_vblank_callback(gb, (GB_vblank_callback_t) vblank);
    GB_set_log_callback(gb, (GB_log_callback_t) consoleLog);
    GB_set_camera_get_pixel_callback(gb, cameraGetPixel);
    GB_set_camera_update_request_callback(gb, cameraRequestUpdate);
    [self addDefaultObserver:^(id newValue) {
        GB_set_color_correction_mode(gb, (GB_color_correction_mode_t)[newValue integerValue]);
    } forKey:@"GBColorCorrection"];
    [self addDefaultObserver:^(id newValue) {
        GB_set_light_temperature(gb, [newValue doubleValue]);
    } forKey:@"GBLightTemperature"];
    GB_set_border_mode(gb, GB_BORDER_NEVER);
    __weak typeof(self) weakSelf = self;
    [self addDefaultObserver:^(id newValue) {
        [weakSelf updatePalette];
    } forKey:@"GBCurrentTheme"];
    GB_set_rgb_encode_callback(gb, rgbEncode);
    [self addDefaultObserver:^(id newValue) {
        GB_set_highpass_filter_mode(gb, (GB_highpass_mode_t)[newValue integerValue]);
    } forKey:@"GBHighpassFilter"];
    [self addDefaultObserver:^(id newValue) {
        GB_set_rtc_mode(gb, [newValue integerValue]);
    } forKey:@"GBRTCMode"];
    GB_apu_set_sample_callback(gb, audioCallback);
    GB_set_rumble_callback(gb, rumbleCallback);
    [self addDefaultObserver:^(id newValue) {
        GB_set_rumble_mode(gb, [newValue integerValue]);
    } forKey:@"GBRumbleMode"];
    [self addDefaultObserver:^(id newValue) {
        GB_set_interference_volume(gb, [newValue doubleValue]);
    } forKey:@"GBInterferenceVolume"];
    [self addDefaultObserver:^(id newValue) {
        GB_set_rewind_length(gb, [newValue unsignedIntValue]);
    } forKey:@"GBRewindLength"];
    [self addDefaultObserver:^(id newValue) {
        GB_set_turbo_cap(gb, [newValue doubleValue]);
    } forKey:@"GBTurboCap"];
    [self addDefaultObserver:^(id newValue) {
        [[AVAudioSession sharedInstance] setCategory:[newValue isEqual:@"on"]? AVAudioSessionCategoryPlayback :  AVAudioSessionCategorySoloAmbient
                                                mode:AVAudioSessionModeDefault
                                  routeSharingPolicy:AVAudioSessionRouteSharingPolicyDefault
                                             options:0
                                               error:nil];
    } forKey:@"GBAudioMode"];
}

- (void)addDefaultObserver:(void(^)(id newValue))block forKey:(NSString *)key
{
    if (!_defaultsObservers) {
        _defaultsObservers = [NSMutableSet set];
    }
    block = [block copy];
    [_defaultsObservers addObject:block];
    [[NSUserDefaults standardUserDefaults] addObserver:self
                                            forKeyPath:key
                                               options:NSKeyValueObservingOptionNew
                                               context:(void *)block];
    block([[NSUserDefaults standardUserDefaults] objectForKey:key]);
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey,id> *)change context:(void *)context
{
    ((__bridge void(^)(id))context)(change[NSKeyValueChangeNewKey]);
}

- (NSArray<NSNumber *> *)zoomFactorsForDevice:(AVCaptureDevice *)device
{
    if (@available(iOS 13.0, *)) {
        return device.virtualDeviceSwitchOverVideoZoomFactors;
    }
    double factor = device.dualCameraSwitchOverVideoZoomFactor;
    if (factor == 1.0) {
        return @[];
    }
    return @[@(factor)];
}

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    _window = [[UIWindow alloc] init];
    _window.rootViewController = self;
    [_window makeKeyAndVisible];
    
    _runQueue = dispatch_queue_create("SameBoy Emulation Queue", NULL);
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-retain-cycles"
    [self addDefaultObserver:^(id newValue) {
        GBTheme *theme = [GBSettingsViewController themeNamed:newValue];
        _horizontalLayoutLeft = [[GBHorizontalLayout alloc] initWithTheme:theme cutoutOnRight:false];
        _horizontalLayoutRight = _horizontalLayoutLeft.cutout?
            [[GBHorizontalLayout alloc] initWithTheme:theme cutoutOnRight:true] :
            _horizontalLayoutLeft;
        _verticalLayout = [[GBVerticalLayout alloc] initWithTheme:theme];
        _printerSpinner.color = theme.buttonColor;

        [self willRotateToInterfaceOrientation:[UIApplication sharedApplication].statusBarOrientation
                                          duration:0];
        [_backgroundView reloadThemeImages];
        
        [self setNeedsStatusBarAppearanceUpdate];
    } forKey:@"GBInterfaceTheme"];
#pragma clang diagnostic pop
    
    _backgroundView = [[GBBackgroundView alloc] initWithLayout:_verticalLayout];
    [_window addSubview:_backgroundView];
    self.view = _backgroundView;
    
    
    [self initGameBoy];
    _gbView = _backgroundView.gbView;
    _gbView.hidden = true;
    _gbView.gb = &_gb;
    [_gbView screenSizeChanged];
    
    [self addDefaultObserver:^(id newValue) {
        [[NSNotificationCenter defaultCenter] postNotificationName:@"GBFilterChanged" object:nil];
    } forKey:@"GBFilter"];
    
    __weak GBView *gbview = _gbView;
    [self addDefaultObserver:^(id newValue) {
        gbview.frameBlendingMode = [newValue integerValue];
    } forKey:@"GBFrameBlendingMode"];
    
    __weak GBBackgroundView *backgroundView = _backgroundView;
    [self addDefaultObserver:^(id newValue) {
        backgroundView.usesSwipePad = [newValue boolValue];
    } forKey:@"GBSwipeDpad"];
    
    
    [self willRotateToInterfaceOrientation:[UIApplication sharedApplication].statusBarOrientation
                                  duration:0];
    
    
    _audioLock = [[NSCondition alloc] init];
    
    [[NSNotificationCenter defaultCenter] addObserverForName:@"GBROMChanged"
                                                      object:nil
                                                       queue:nil
                                                  usingBlock:^(NSNotification *note) {
        _swappingROM = true;
        [self stop];
        [self start];
    }];
    
    _motionManager = [[CMMotionManager alloc] init];
    _cameraPosition = AVCaptureDevicePositionBack;

    // Back camera setup
    NSArray *deviceTypes = @[AVCaptureDeviceTypeBuiltInWideAngleCamera,
                             AVCaptureDeviceTypeBuiltInTelephotoCamera,
                             AVCaptureDeviceTypeBuiltInDualCamera];
    if (@available(iOS 13.0, *)) {
        // AVCaptureDeviceTypeBuiltInUltraWideCamera is only available in iOS 13+
        deviceTypes = @[AVCaptureDeviceTypeBuiltInWideAngleCamera,
                        AVCaptureDeviceTypeBuiltInUltraWideCamera,
                        AVCaptureDeviceTypeBuiltInTelephotoCamera,
                        AVCaptureDeviceTypeBuiltInTripleCamera,
                        AVCaptureDeviceTypeBuiltInDualWideCamera,
                        AVCaptureDeviceTypeBuiltInDualCamera];
    }

    // Use a discovery session to gather the capture devices (all back cameras as well as the front camera)
    AVCaptureDeviceDiscoverySession *cameraDiscoverySession = [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:deviceTypes
                                                                                                                     mediaType:AVMediaTypeVideo
                                                                                                                      position:AVCaptureDevicePositionUnspecified];
    for (AVCaptureDevice *device in cameraDiscoverySession.devices) {
        if ([device position] == AVCaptureDevicePositionBack) {
            if (!_backCaptureDevice ||
                [self zoomFactorsForDevice:_backCaptureDevice].count < [self zoomFactorsForDevice:device].count) {
                _backCaptureDevice = device;
            }
        }
        else if ([device position] == AVCaptureDevicePositionFront) {
            _frontCaptureDevice = device;
        }
    }
    
    _zoomLevels = [self zoomFactorsForDevice:_backCaptureDevice].mutableCopy;
    [_zoomLevels insertObject:@1 atIndex:0];
    if (_zoomLevels.count == 3 && _zoomLevels[2].doubleValue > 5.5 && _zoomLevels[1].doubleValue < 3.5) {
        [_zoomLevels insertObject:@4 atIndex:2];
    }

    _cameraPositionButton = [[UIButton alloc] init];
    [self didRotateFromInterfaceOrientation:[UIApplication sharedApplication].statusBarOrientation];
    if (@available(iOS 13.0, *)) {
        [_cameraPositionButton  setImage:[UIImage systemImageNamed:@"camera.rotate"
                                                 withConfiguration:[UIImageSymbolConfiguration configurationWithScale:UIImageSymbolScaleLarge]]
                                forState:UIControlStateNormal];
        _cameraPositionButton.backgroundColor = [UIColor systemBackgroundColor];

        // Configure the change camera button stacked on top of the camera position button
        _changeCameraButton = [[UIButton alloc] init];
        [_changeCameraButton  setImage:[UIImage systemImageNamed:@"camera.aperture"
                                                withConfiguration:[UIImageSymbolConfiguration configurationWithScale:UIImageSymbolScaleLarge]]
                                forState:UIControlStateNormal];
        _changeCameraButton.backgroundColor = [UIColor systemBackgroundColor];
        _changeCameraButton.layer.cornerRadius = 6;
        _changeCameraButton.alpha = 0;
        [_changeCameraButton addTarget:self
                                action:@selector(changeCamera)
                      forControlEvents:UIControlEventTouchUpInside];
        // Only show the change camera button if we have more than one back camera to swap between.
        if (_zoomLevels.count > 1) {
            [_backgroundView addSubview:_changeCameraButton];
        }
    }
    else {
        UIImage *rotateImage = [[UIImage imageNamed:@"CameraRotateTemplate"] imageWithRenderingMode:UIImageRenderingModeAlwaysTemplate];
        [_cameraPositionButton setImage:rotateImage
                               forState:UIControlStateNormal];
        _cameraPositionButton.backgroundColor = [UIColor whiteColor];
    }

    _cameraPositionButton.layer.cornerRadius = 6;
    _cameraPositionButton.alpha = 0;
    [_cameraPositionButton addTarget:self
                              action:@selector(rotateCamera)
                    forControlEvents:UIControlEventTouchUpInside];

    [_backgroundView addSubview:_cameraPositionButton];

    _cameraQueue = dispatch_queue_create("SameBoy Camera Queue", NULL);
    
    [UNUserNotificationCenter currentNotificationCenter].delegate = self;
    [self verifyEntitlements];

    [self setControllerHandlers];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(setControllerHandlers)
                                                 name:GCControllerDidConnectNotification
                                               object:nil];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(controllerDisconnected:)
                                                 name:GCControllerDidDisconnectNotification
                                               object:nil];
    
    for (NSString *name in @[UIScreenDidConnectNotification,
                             UIScreenDidDisconnectNotification,
                             UIScreenModeDidChangeNotification]) {
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(updateMirrorWindow)
                                                     name:name
                                                   object:nil];
    }
    
    _printerButton = [[UIButton alloc] init];
    _printerSpinner = [[UIActivityIndicatorView alloc] init];
    _printerSpinner.activityIndicatorViewStyle = UIActivityIndicatorViewStyleWhite;
    _printerSpinner.color = _verticalLayout.theme.buttonColor;
    [self didRotateFromInterfaceOrientation:[UIApplication sharedApplication].statusBarOrientation];
    
    if (@available(iOS 13.0, *)) {
        [_printerButton  setImage:[UIImage systemImageNamed:@"printer"
                                          withConfiguration:[UIImageSymbolConfiguration configurationWithScale:UIImageSymbolScaleLarge]]
                         forState:UIControlStateNormal];
        _printerButton.backgroundColor = [UIColor systemBackgroundColor];
    }
    else {
        UIImage *rotateImage = [[UIImage imageNamed:@"PrinterTemplate"] imageWithRenderingMode:UIImageRenderingModeAlwaysTemplate];
        [_printerButton setImage:rotateImage
                               forState:UIControlStateNormal];
        _printerButton.backgroundColor = [UIColor whiteColor];
    }
    
    _printerButton.layer.cornerRadius = 6;
    _printerButton.alpha = 0;
    [_printerButton addTarget:self
                       action:@selector(showPrinterFeed)
             forControlEvents:UIControlEventTouchUpInside];
    
    
    [_backgroundView addSubview:_printerButton];
    [_backgroundView addSubview:_printerSpinner];

    
    [self updateMirrorWindow];
    
    if (@available(iOS 26.0, *)) {
        UIMainMenuSystemConfiguration *conf = [[objc_getClass("UIMainMenuSystemConfiguration") alloc] init];
        conf.newScenePreference = UIMenuSystemElementGroupPreferenceRemoved;
        conf.documentPreference = UIMenuSystemElementGroupPreferenceRemoved;
        conf.printingPreference = UIMenuSystemElementGroupPreferenceRemoved;
        conf.findingPreference = UIMenuSystemElementGroupPreferenceRemoved;
        conf.toolbarPreference = UIMenuSystemElementGroupPreferenceRemoved;
        conf.sidebarPreference = UIMenuSystemElementGroupPreferenceRemoved;
        conf.inspectorPreference = UIMenuSystemElementGroupPreferenceRemoved;
        conf.textFormattingPreference =  UIMenuSystemElementGroupPreferenceRemoved;
        
        UIMainMenuSystem *system = (id)[objc_getClass("UIMainMenuSystem") sharedSystem];
        [system setBuildConfiguration:conf
                         buildHandler:^(id<UIMenuBuilder> builder) {
            [builder removeMenuForIdentifier:UIMenuView]; // This menu's always empty
            [builder removeMenuForIdentifier:UIMenuOpenRecent]; // This will list files to re-import, bad
            
            [(id)builder insertElements:@[[UICommand commandWithTitle:@"About SameBoy"
                                                                image:nil
                                                               action:@selector(showAbout)
                                                         propertyList:nil]]
             atStartOfMenuForIdentifier:UIMenuApplication];
            
            UIMenu *emulationMenu = [UIMenu menuWithTitle:@"Emulation" children:@[
                [UIKeyCommand keyCommandWithInput:@"r" modifierFlags:UIKeyModifierCommand action:@selector(reset) title:@"Reset" image:[UIImage systemImageNamed:@"arrow.2.circlepath"]],
                [UICommand commandWithTitle:@"Change Model…" image:CreateMenuImage(@"ModelTemplate") action:@selector(changeModel) propertyList:nil],
                [UICommand commandWithTitle:@"Save States…" image:[UIImage systemImageNamed:@"square.stack"] action:@selector(openStates) propertyList:nil],
                [UIMenu menuWithTitle:@"Cheats" image:CreateMenuImage(@"CheatsTemplate") identifier:nil options:0 children:@[
                    [UIKeyCommand keyCommandWithInput:@"c" modifierFlags:UIKeyModifierCommand | UIKeyModifierShift action:@selector(toggleCheats) title:@"Enable Cheats" image:nil],
                    [UICommand commandWithTitle:@"Show Cheats…" image:nil action:@selector(openCheats) propertyList:nil],
                ]],
                [UIMenu menuWithTitle:@"Connect" image:CreateMenuImage(@"LinkCableTemplate") identifier:nil options:0 children:@[
                    [UICommand commandWithTitle:@"None" image:nil action:@selector(disconnectCable) propertyList:nil],
                    [UICommand commandWithTitle:@"Printer" image:[UIImage systemImageNamed:@"printer"] action:@selector(connectPrinter) propertyList:nil],
                ]],
            ]];
            [builder insertSiblingMenu:emulationMenu beforeMenuForIdentifier:UIMenuWindow];
        }];
    }
    
    return true;
}

- (BOOL)canPerformAction:(SEL)action withSender:(id)sender
{
    if (action == @selector(reset)) {
        if (self.presentedViewController || ![GBROMManager sharedManager].currentROM) return false;
    }
    if (action == @selector(openStates) || action == @selector(changeModel) || action == @selector(openCheats) ||
        action == @selector(toggleCheats) || action == @selector(disconnectCable) || action == @selector(connectPrinter)) {
        if (![GBROMManager sharedManager].currentROM) return false;
    }
    return [super canPerformAction:action withSender:sender];
}

- (void)validateCommand:(UICommand *)command
{
    if (command.action == @selector(toggleCheats)) {
        command.state = GB_is_inited(&_gb) && GB_cheats_enabled(&_gb);
    }
    
    if (command.action == @selector(connectPrinter)) {
        command.state = _printerConnected;
    }
    
    if (command.action == @selector(disconnectCable)) {
        command.state = !_printerConnected;
    }
    
    [super validateCommand:command];
}

- (void)toggleCheats
{
    GB_set_cheats_enabled(&_gb, !GB_cheats_enabled(&_gb));
}

- (void)orderFrontPreferencesPanel:(id)sender
{
    [self openSettings];
}

- (void)open:(id)sender
{
    [self openLibrary];
}

- (void)updateMirrorWindow
{
    if ([UIScreen screens].count == 1) {
        _mirrorWindow = nil;
        _mirrorView = nil;
        return;
    }
    if (_mirrorWindow && ![[UIScreen screens] containsObject:_mirrorWindow.screen]) {
        _mirrorWindow = nil;
        _mirrorView = nil;
    }
    for (UIScreen *screen in [UIScreen screens]) {
        if (screen == UIScreen.mainScreen) continue;
        CGRect rect = screen.bounds;
        rect.size.height = floor(rect.size.height / 144) * 144;
        rect.size.width = rect.size.height / 144 * 160;
        rect.origin.x = (screen.bounds.size.width - rect.size.width) / 2;
        rect.origin.y = (screen.bounds.size.height - rect.size.height) / 2;
        _mirrorWindow = [[UIWindow alloc] initWithFrame:screen.bounds];
        _mirrorWindow.screen = screen;
        _mirrorView = [_gbView mirroredView];
        _mirrorView.frame = rect;
        _mirrorWindow.backgroundColor = [UIColor blackColor];
        [_mirrorWindow addSubview:_mirrorView];
        [_mirrorWindow setHidden:false];
        break;
    }
}

- (void)controllerDisconnected:(NSNotification *)notification
{
    if (notification.object == _lastController) {
        _backgroundView.fullScreenMode = false;
    }
}

- (void)setControllerHandlers
{
    for (GCController *controller in [GCController controllers]) {
        __weak GCController *weakController = controller;
        NSDictionary <NSNumber *, GCControllerElement *> *elements = GCControllerGetElements(controller);
        if (!elements) continue; // Controller not supported
        [elements enumerateKeysAndObjectsUsingBlock:^(NSNumber *usage, GCControllerElement *element, BOOL *stop) {
            if ([element isKindOfClass:[GCControllerButtonInput class]]) {
                [(GCControllerButtonInput *)element setValueChangedHandler:^(GCControllerButtonInput *button, float value, BOOL pressed) {
                    [self controller:weakController buttonChanged:button usage:usage.unsignedIntValue];
                }];
            }
            else if ([element isKindOfClass:[GCControllerDirectionPad class]]) {
                NSMutableSet *childrenUsages = [NSMutableSet set];
                [elements enumerateKeysAndObjectsUsingBlock:^(NSNumber *childUsage, GCControllerElement *child, BOOL *stop) {
                    if (child.collection == element) {
                        [childrenUsages addObject:childUsage];
                    }
                }];
                [(GCControllerDirectionPad *)element setValueChangedHandler:^(GCControllerDirectionPad *dpad, float xValue, float yValue) {
                    [self controller:weakController axisChanged:dpad usage:usage.unsignedIntValue childrenUsages:childrenUsages];
                }];
            }
        }];
        
        if (controller.motion) {
            [controller.motion setValueChangedHandler:^(GCMotion *motion) {
                [self controller:weakController motionChanged:motion];
            }];
        }
    }
}

- (void)updateLastController:(GCController *)controller
{
    if (_lastController == controller) return;
    _lastController = controller;
    [GBHapticManager sharedManager].controller = controller;
}

- (void)controller:(GCController *)controller buttonChanged:(GCControllerButtonInput *)button usage:(GBControllerUsage)usage
{
    [self updateLastController:controller];
    if (_running && button.value > 0.25 &&
        [[NSUserDefaults standardUserDefaults] boolForKey:@"GBControllersHideInterface"]) {
        _backgroundView.fullScreenMode = true;
    }
    
    GBButton gbButton = [GBSettingsViewController controller:controller convertUsageToButton:usage];
    static const double analogThreshold = 0.0625;
    switch (gbButton) {
        case GBRight:
        case GBLeft:
        case GBUp:
        case GBDown:
            GB_set_use_faux_analog_inputs(&_gb, 0, false);
        case GBA:
        case GBB:
        case GBSelect:
        case GBStart:
            GB_set_key_state(&_gb, (GB_key_t)gbButton, button.value > 0.25);
            if (_runMode == GBRunModeRewind || _runMode == GBRunModePaused) {
                self.runMode = GBRunModeNormal;
                [_backgroundView fadeOverlayOut];
            }
            break;
        case GBRapidA:
            _rapidA = button.value > 0.25;
            _rapidACount = 0;
            break;
        case GBRapidB:
            _rapidB = button.value > 0.25;
            _rapidBCount = 0;
            break;
        case GBTurbo:
            if (button.value > analogThreshold) {
                [self setRunMode:GBRunModeTurbo ignoreDynamicSpeed:!button.isAnalog];
                if (button.isAnalog && [[NSUserDefaults standardUserDefaults] boolForKey:@"GBDynamicSpeed"]) {
                    GB_set_clock_multiplier(&_gb, (button.value - analogThreshold) / (1 - analogThreshold) * 3 + 1);
                }
                _runModeFromController = true;
                [_backgroundView fadeOverlayOut];
            }
            else {
                if (self.runMode == GBRunModeTurbo && _runModeFromController) {
                    [self setRunMode:GBRunModeNormal];
                    _runModeFromController = false;
                }
            }
            break;
        case GBRewind:
            if (button.value > analogThreshold) {
                [self setRunMode:GBRunModeRewind ignoreDynamicSpeed:!button.isAnalog];
                if (button.isAnalog && [[NSUserDefaults standardUserDefaults] boolForKey:@"GBDynamicSpeed"]) {
                    GB_set_clock_multiplier(&_gb, (button.value - analogThreshold) / (1 - analogThreshold) * 4);
                }
                _runModeFromController = true;
                [_backgroundView fadeOverlayOut];
            }
            else {
                if ((self.runMode == GBRunModeRewind || self.runMode == GBRunModePaused) && _runModeFromController) {
                    [self setRunMode:GBRunModeNormal];
                    _runModeFromController = false;
                }
            }
            break;
        case GBUnderclock:
            if (button.value > analogThreshold) {
                [self setRunMode:GBRunModeUnderclock ignoreDynamicSpeed:!button.isAnalog];
                if (button.isAnalog && [[NSUserDefaults standardUserDefaults] boolForKey:@"GBDynamicSpeed"]) {
                    GB_set_clock_multiplier(&_gb, 1 - ((button.value - analogThreshold) / (1 - analogThreshold) * 0.75));
                }
                _runModeFromController = true;
                [_backgroundView fadeOverlayOut];
            }
            else {
                if (self.runMode == GBRunModeUnderclock && _runModeFromController) {
                    [self setRunMode:GBRunModeNormal];
                    _runModeFromController = false;
                }
            }
            break;
        default: break;
    }
}

- (void)controller:(GCController *)controller axisChanged:(GCControllerDirectionPad *)axis usage:(GBControllerUsage)usage childrenUsages:(NSSet *)childrenUsages
{
    [self updateLastController:controller];
    bool left = axis.left.value > 0.5;
    bool right = axis.right.value > 0.5;
    bool up = axis.up.value > 0.5;
    bool down = axis.down.value > 0.5;
    
    bool hasUnmappedChild = false;
    for (NSNumber *childUsage in childrenUsages) {
        if ([GBSettingsViewController controller:controller convertUsageToButton:childUsage.unsignedIntValue] != GBUnusedButton) {
            GCControllerButtonInput *child = (id)(GCControllerGetElements(controller)[childUsage]);
            if ([child isKindOfClass:[GCControllerButtonInput class]] && child.pressed) {
                left = right = up = down = false;
            }
        }
        else {
            hasUnmappedChild = true;
        }
    }
    
    if (!hasUnmappedChild) return;
    
    if (_running && (left || right || up || down ) &&
        [[NSUserDefaults standardUserDefaults] boolForKey:@"GBControllersHideInterface"]) {
        _backgroundView.fullScreenMode = true;
    }
    
    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBFauxAnalogInputs"]) {
        GB_set_use_faux_analog_inputs(&_gb, 0, true);
        GB_set_faux_analog_inputs(&_gb, 0, axis.right.value - axis.left.value, axis.down.value - axis.up.value);
    }
    
    GB_set_key_state(&_gb, GB_KEY_LEFT, left);
    GB_set_key_state(&_gb, GB_KEY_RIGHT, right);
    GB_set_key_state(&_gb, GB_KEY_UP, up);
    GB_set_key_state(&_gb, GB_KEY_DOWN, down);
    if (_runMode == GBRunModeRewind || _runMode == GBRunModePaused) {
        self.runMode = GBRunModeNormal;
        [_backgroundView fadeOverlayOut];
    }
}

- (void)controller:(GCController *)controller motionChanged:(GCMotion *)motion
{
    if (controller != _lastController) return;
    GCAcceleration gravity = {0,};
    GCAcceleration userAccel = {0,};
    if (@available(iOS 14.0, *)) {
        if (motion.hasGravityAndUserAcceleration) {
            gravity = motion.gravity;
            userAccel = motion.userAcceleration;
        }
        else {
            gravity = motion.acceleration;
        }
    }
    else {
        gravity = motion.gravity;
        userAccel = motion.userAcceleration;
    }
    GB_set_accelerometer_values(&_gb, -(gravity.x + userAccel.x), gravity.y + userAccel.y);
}


- (void)verifyEntitlements
{
    /*
        Make sure SameBoy is properly signed. If the bundle identifier the Info.plist file does not match the bundle
        identifier in the application-identifier entitlement, iOS will not allow SameBoy to open files.
    */
    typedef void *xpc_object_t;
    void *libxpc = dlopen("/usr/lib/system/libxpc.dylib", RTLD_NOW);
    
    extern xpc_object_t xpc_copy_entitlements_for_self$(void);
    extern const char *xpc_dictionary_get_string$(xpc_object_t *object, const char *key);
    
    typeof(xpc_copy_entitlements_for_self$) *xpc_copy_entitlements_for_self = dlsym(libxpc, "xpc_copy_entitlements_for_self");
    typeof(xpc_dictionary_get_string$) *xpc_dictionary_get_string = dlsym(libxpc, "xpc_dictionary_get_string");
    
    if (!xpc_copy_entitlements_for_self || !xpc_dictionary_get_string) return;
    
    xpc_object_t entitlements = xpc_copy_entitlements_for_self();
    if (!entitlements) return;
    const char *_entIdentifier = xpc_dictionary_get_string(entitlements, "application-identifier");
    NSString *entIdentifier = _entIdentifier? @(_entIdentifier) : nil;
    
    const char *_teamIdentifier = xpc_dictionary_get_string(entitlements, "com.apple.developer.team-identifier");
    NSString *teamIdentifier = _teamIdentifier? @(_teamIdentifier) : nil;
    
    if (!entIdentifier) { // No identifier. Installed using a jailbreak, we're fine.
        return;
    }

    if (teamIdentifier && [entIdentifier hasPrefix:[teamIdentifier stringByAppendingString:@"."]]) {
        entIdentifier = [entIdentifier substringFromIndex:teamIdentifier.length + 1];
    }
    
    NSString *plistIdentifier = [NSBundle mainBundle].bundleIdentifier;
    
    if (![entIdentifier isEqual:plistIdentifier]) {
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"SameBoy is not properly signed and might not be able to open ROMs"
                                                                       message:[NSString stringWithFormat:@"The bundle identifier in the Info.plist file (“%@”) does not match the one in the entitlements (“%@”)", plistIdentifier, entIdentifier]
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"Close"
                                                  style:UIAlertActionStyleCancel
                                                handler:nil]];
        [self presentViewController:alert animated:true completion:nil];
    }
}

- (void)saveStateToFile:(NSString *)file
{
    NSString *tempPath = [file stringByAppendingPathExtension:@"tmp"];
    int error = GB_save_state(&_gb, tempPath.UTF8String);
    if (!error) {
        rename(tempPath.UTF8String, file.UTF8String);
        NSData *data = [NSData dataWithBytes:_gbView.previousBuffer
                                      length:GB_get_screen_width(&_gb) *
                        GB_get_screen_height(&_gb) *
                        sizeof(*_gbView.previousBuffer)];
        UIImage *screenshot = [self imageFromData:data width:GB_get_screen_width(&_gb) height:GB_get_screen_height(&_gb)];
        [UIImagePNGRepresentation(screenshot) writeToFile:[file stringByAppendingPathExtension:@"png"] atomically:false];
    }
    else {
        dispatch_async(dispatch_get_main_queue(), ^{
            UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Could not Save State"
                                                                           message:[NSString stringWithFormat:@"An error occured while attempting to save: %s", strerror(error)]
                                                                    preferredStyle:UIAlertControllerStyleAlert];
            [alert addAction:[UIAlertAction actionWithTitle:@"Close"
                                                      style:UIAlertActionStyleCancel
                                                    handler:nil]];
            UIViewController *top = self;
            while (top.presentedViewController) {
                top = top.presentedViewController;
            }
            [top presentViewController:alert animated:true completion:nil];
        });
    }
}

- (bool)loadStateFromFile:(NSString *)file
{
    _skipAutoLoad = true;
    GB_model_t model;
    if (!GB_get_state_model(file.fileSystemRepresentation, &model)) {
        if (GB_get_model(&_gb) != model) {
            GB_switch_model_and_reset(&_gb, model);
        }
        return GB_load_state(&_gb, file.fileSystemRepresentation) == 0;
    }

    return false;
}

- (void)loadROM
{
    GBROMManager *romManager = [GBROMManager sharedManager];
    if (romManager.romFile) {
        if (!_skipAutoLoad) {
            // Todo: display errors and warnings
            bool needsStateLoad = false;
            if (![_lastSavedROM isEqual:[GBROMManager sharedManager].currentROM]) {
                if ([romManager.romFile.pathExtension.lowercaseString isEqualToString:@"isx"]) {
                    _romLoaded = GB_load_isx(&_gb, romManager.romFile.fileSystemRepresentation) == 0;
                }
                else {
                    _romLoaded = GB_load_rom(&_gb, romManager.romFile.fileSystemRepresentation) == 0;
                }
                needsStateLoad = true;
                if (@available(iOS 16.0, *)) {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        [super setNeedsUpdateOfSupportedInterfaceOrientations];
                    });
                }
            }
            else if (access(romManager.romFile.fileSystemRepresentation, R_OK)) {
                _romLoaded = false;
            }
            if (!_romLoaded) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    romManager.currentROM = nil;
                });
            }
            
            if (!needsStateLoad) {
                NSDate *date = nil;
                [[NSURL fileURLWithPath:[GBROMManager sharedManager].autosaveStateFile] getResourceValue:&date
                                                                                                  forKey:NSURLContentModificationDateKey
                                                                                                   error:nil];
                if (![_saveDate isEqual:date]) {
                    needsStateLoad = true;
                }
            }
            
            if (_romLoaded && needsStateLoad) {
                GB_reset(&_gb);
                GB_load_battery(&_gb, [GBROMManager sharedManager].batterySaveFile.fileSystemRepresentation);
                GB_remove_all_cheats(&_gb);
                GB_load_cheats(&_gb, [GBROMManager sharedManager].cheatsFile.UTF8String, false);
                if (![self loadStateFromFile:[GBROMManager sharedManager].autosaveStateFile]) {
                    if ([_lastSavedROM isEqual:[GBROMManager sharedManager].currentROM]) {
                        /* Something weird just happened: we didn't change a ROM, but we failed to load the
                           latest save state. Save over the existing file, it's probably corrupt in some
                           way. */
                        [self saveStateToFile:[GBROMManager sharedManager].autosaveStateFile];
                    }
                    else {
                        // Newly played ROM, pick the best model
                        uint8_t *rom = GB_get_direct_access(&_gb, GB_DIRECT_ACCESS_ROM, NULL, NULL);
                        
                        if ((rom[0x143] & 0x80)) {
                            if (!GB_is_cgb(&_gb)) {
                                GB_switch_model_and_reset(&_gb, [[NSUserDefaults standardUserDefaults] integerForKey:@"GBCGBModel"]);
                            }
                        }
                        else if ((rom[0x146]  == 3) && !GB_is_sgb(&_gb)) {
                            GB_switch_model_and_reset(&_gb, [[NSUserDefaults standardUserDefaults] integerForKey:@"GBSGBModel"]);
                        }
                    }
                }
                GB_rewind_reset(&_gb);
            }
        }
    }
    else {
        _romLoaded = false;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        _gbView.hidden = !_romLoaded;
    });
    _swappingROM = false;
    _skipAutoLoad = false;
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
    [self start];
}

-(void)presentViewController:(UIViewController *)viewControllerToPresent animated:(BOOL)flag completion:(void (^)(void))completion
{
    [self stop];
    [super presentViewController:viewControllerToPresent
                        animated:flag
                      completion:completion];
}

- (void)reset
{
    UIAlertControllerStyle style = [UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad?
    UIAlertControllerStyleAlert : UIAlertControllerStyleActionSheet;
    UIAlertController *menu = [UIAlertController alertControllerWithTitle:@"Reset Emulation?"
                                                                  message:@"Unsaved progress will be lost."
                                                           preferredStyle:style];
    [menu addAction:[UIAlertAction actionWithTitle:@"Reset"
                                             style:UIAlertActionStyleDestructive
                                           handler:^(UIAlertAction *action) {
        [self stop];
        _skipAutoLoad = true;
        GB_reset(&_gb);
        [self start];
    }]];
    [menu addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                             style:UIAlertActionStyleCancel
                                           handler:nil]];
    [self presentViewController:menu animated:true completion:nil];
}

- (void)openLibrary
{
    static __weak UIViewController *presentedController;
    if (presentedController && self.presentedViewController == presentedController) return;
    if (![self dismissViewControllerIfSafe]) return;
    
    UIViewController *controller = [[GBLibraryViewController alloc] init];
    presentedController = controller;
    
    [self presentViewController:controller
                       animated:true
                     completion:nil];
}

- (void)changeModel
{
    static __weak UIViewController *presentedController;
    if (presentedController && self.presentedViewController == presentedController) return;
    if (![self dismissViewControllerIfSafe]) return;

    GBOptionViewController *controller = [[GBOptionViewController alloc] initWithHeader:@"Select a model to emulate"];
    controller.footer = @"Changing the emulated model will reset the emulator";
    presentedController = controller;
    
    GB_model_t currentModel = GB_get_model(&_gb);
    struct {
        NSString *title;
        NSString *settingKey;
        bool checked;
    } items[] = {
        {@"Game Boy", @"GBDMGModel", currentModel < GB_MODEL_SGB},
        {@"Game Boy Pocket/Light", nil, currentModel == GB_MODEL_MGB},
        {@"Super Game Boy", @"GBSGBModel", GB_is_sgb(&_gb)},
        {@"Game Boy Color", @"GBCGBModel", GB_is_cgb(&_gb) && currentModel <= GB_MODEL_CGB_E},
        {@"Game Boy Advance", @"GBAGBModel", currentModel > GB_MODEL_CGB_E},
    };
    
    for (unsigned i = 0; i <  sizeof(items) / sizeof(items[0]); i++) {
        GB_model_t model = GB_MODEL_MGB;
        if (items[i].settingKey) {
            model = [[NSUserDefaults standardUserDefaults] integerForKey:items[i].settingKey];
        }
        [controller addOption:items[i].title withCheckmark:items[i].checked action:^{
            [self stop];
            _skipAutoLoad = true;
            GB_switch_model_and_reset(&_gb, model);
            if (model > GB_MODEL_CGB_E && ![[NSUserDefaults standardUserDefaults] boolForKey:@"GBShownGBAWarning"]) {
                UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"SameBoy is not a Game Boy Advance Emulator"
                                                                               message:@"SameBoy cannot play GBA games. Changing the model to Game Boy Advance lets you play Game Boy games as if on a Game Boy Advance in Game Boy Color mode."
                                                                        preferredStyle:UIAlertControllerStyleAlert];
                [alert addAction:[UIAlertAction actionWithTitle:@"Close"
                                                          style:UIAlertActionStyleCancel
                                                        handler:^(UIAlertAction *action) {
                    [self start];
                    [[NSUserDefaults standardUserDefaults] setBool:true forKey:@"GBShownGBAWarning"];
                }]];
                [self presentViewController:alert animated:true completion:nil];
            }
            else {
                [self start];
            }
        }];
    }
    controller.title = @"Change Model";
    
    UINavigationController *navController = [[UINavigationController alloc] initWithRootViewController:controller];
    UIBarButtonItem *close = [[UIBarButtonItem alloc] initWithTitle:@"Close"
                                                              style:UIBarButtonItemStylePlain
                                                             target:self
                                                             action:@selector(dismissViewController)];
    [navController.visibleViewController.navigationItem setLeftBarButtonItem:close];

    [self presentViewController:navController animated:true completion:nil];
}

- (void)openStates
{
    static __weak UIViewController *presentedController;
    if (presentedController && self.presentedViewController == presentedController) return;
    if (![self dismissViewControllerIfSafe]) return;
    
    UINavigationController *controller = [[UINavigationController alloc] initWithRootViewController:[[GBStatesViewController alloc] init]];
    presentedController = controller;
    UIVisualEffect *effect = [UIBlurEffect effectWithStyle:(UIBlurEffectStyle)UIBlurEffectStyleProminent];
    UIVisualEffectView *effectView = [[UIVisualEffectView alloc] initWithEffect:effect];
    effectView.frame = controller.view.bounds;
    effectView.autoresizingMask = UIViewAutoresizingFlexibleWidth |  UIViewAutoresizingFlexibleHeight;
    [controller.view insertSubview:effectView atIndex:0];
    UIBarButtonItem *close = [[UIBarButtonItem alloc] initWithTitle:@"Close"
                                                              style:UIBarButtonItemStylePlain
                                                             target:self
                                                             action:@selector(dismissViewController)];
    [controller.visibleViewController.navigationItem setLeftBarButtonItem:close];
    [self presentViewController:controller
                       animated:true
                     completion:nil];
}

- (void)openSettings
{
    static __weak UIViewController *presentedController;
    if (presentedController && self.presentedViewController == presentedController) return;
    if (![self dismissViewControllerIfSafe]) return;

    UIBarButtonItem *close = [[UIBarButtonItem alloc] initWithTitle:@"Close"
                                                              style:UIBarButtonItemStylePlain
                                                             target:self
                                                             action:@selector(dismissViewController)];
    UIViewController *controller = [GBSettingsViewController settingsViewControllerWithLeftButton:close];
    presentedController = controller;
    [self presentViewController:controller
                       animated:true
                     completion:nil];
}

- (void)showAbout
{
    static __weak UIViewController *presentedController;
    if (presentedController && self.presentedViewController == presentedController) return;
    if (![self dismissViewControllerIfSafe]) return;

    UIViewController *controller = [[GBAboutController alloc] init];
    presentedController = controller;

    [self presentViewController:controller animated:true completion:nil];
}

- (void)openCheats
{
    static __weak UIViewController *presentedController;
    if (presentedController && self.presentedViewController == presentedController) return;
    if (![self dismissViewControllerIfSafe]) return;

    UINavigationController *controller = [[UINavigationController alloc] initWithRootViewController:[[GBCheatsController alloc] initWithGameBoy:&_gb]];
    presentedController = controller;
    UIBarButtonItem *close = [[UIBarButtonItem alloc] initWithTitle:@"Close"
                                                              style:UIBarButtonItemStylePlain
                                                             target:self
                                                             action:@selector(dismissViewController)];
    [controller.visibleViewController.navigationItem setLeftBarButtonItem:close];
    [self presentViewController:controller
                       animated:true
                     completion:nil];
}

- (void)dismissViewControllerAnimated:(BOOL)flag completion:(void (^)(void))completion
{
    [super dismissViewControllerAnimated:flag completion:^() {
        if (completion) {
            completion();
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [self start];
        });
    }];
}

- (void)setNeedsUpdateOfSupportedInterfaceOrientations
{
    /* Hack. Some view controllers dismiss without calling the method above. */
    dispatch_async(dispatch_get_main_queue(), ^{
        [self start];
        [super setNeedsUpdateOfSupportedInterfaceOrientations];
    });
}

- (void)dismissViewController
{
    [self dismissViewControllerAnimated:true completion:nil];
}

- (bool)dismissViewControllerIfSafe
{
    if (!self.presentedViewController) return true;
    
    if (![self.presentedViewController isKindOfClass:[UIAlertController class]]) {
        [self dismissViewController];
        return true;
    }
    
    if ([self.presentedViewController isKindOfClass:[GBMenuViewController class]]) {
        [self dismissViewController];
        return true;
    }
    return false;
}

- (void)applicationWillResignActive:(UIApplication *)application
{
    [self stop];
}

- (void)willRotateToInterfaceOrientation:(UIInterfaceOrientation)orientation duration:(NSTimeInterval)duration
{
    if (_orientation != UIInterfaceOrientationUnknown && !((1 << orientation) & self.supportedInterfaceOrientations)) return;
    GBLayout *layout = nil;
    _orientation = orientation;
    switch (orientation) {
        default:
        case UIInterfaceOrientationUnknown:
        case UIInterfaceOrientationPortrait:
        case UIInterfaceOrientationPortraitUpsideDown:
            layout = _verticalLayout;
            break;
        case UIInterfaceOrientationLandscapeRight:
            layout = _horizontalLayoutLeft;
            break;
        case UIInterfaceOrientationLandscapeLeft:
            layout = _horizontalLayoutRight;
            break;
    }
    
    _backgroundView.frame = [layout viewRectForOrientation:orientation];
    _backgroundView.layout = layout;
    if (!self.presentedViewController) {
        _window.backgroundColor = _backgroundView.fullScreenMode? [UIColor blackColor] :
                                                                  layout.theme.backgroundGradientBottom;
    }
}

- (void)didRotateFromInterfaceOrientation:(UIInterfaceOrientation)fromInterfaceOrientation
{
    UIEdgeInsets insets = self.window.safeAreaInsets;
    bool landscape = true;
    if (_orientation == UIInterfaceOrientationPortrait || _orientation == UIInterfaceOrientationPortraitUpsideDown) {
        landscape = false;
    }
        
    
    _cameraPositionButton.frame = CGRectMake(insets.left + 8,
                                             _backgroundView.bounds.size.height - 8 - insets.bottom - 32,
                                             32,
                                             32);
    if (_changeCameraButton) {
        _changeCameraButton.frame = CGRectMake(insets.left + 8 +  (landscape? (32 + 8) : 0 ),
                                               _backgroundView.bounds.size.height - 8 - insets.bottom - 32 - (landscape? 0 : (32 + 8)),
                                               32,
                                               32);
    }
    _printerButton.frame = CGRectMake(_backgroundView.bounds.size.width - 8 - insets.right - 32,
                                      _backgroundView.bounds.size.height - 8 - insets.bottom - 32,
                                      32,
                                      32);
    
    _printerSpinner.frame = CGRectMake(_backgroundView.bounds.size.width - 8 - insets.right - 32 - (landscape? (32 + 4) : 0),
                                       _backgroundView.bounds.size.height - 8 - insets.bottom - 32 - (landscape? 0 : (32 + 4)),
                                       32,
                                       32);

    [super didRotateFromInterfaceOrientation:fromInterfaceOrientation];
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations
{
    if (!self.shouldAutorotate && _orientation != UIInterfaceOrientationUnknown) {
        return 1 << _orientation;
    }
    if ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad) {
        return UIInterfaceOrientationMaskAll;
    }
    if (MAX([UIScreen mainScreen].bounds.size.height, [UIScreen mainScreen].bounds.size.width) <= 568) {
        return UIInterfaceOrientationMaskLandscape;
    }
    return UIInterfaceOrientationMaskAllButUpsideDown;
}

- (BOOL)shouldAutorotate
{
    if (_running && GB_has_accelerometer(&_gb)) {
        return false;
    }
    return true;
}

- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures
{
    return UIRectEdgeTop;
}

- (BOOL)prefersStatusBarHidden
{
    switch (_orientation) {
        case UIInterfaceOrientationLandscapeRight:
        case UIInterfaceOrientationLandscapeLeft:
            return true;
        default:
            return false;
    }
}

- (UIStatusBarStyle)preferredStatusBarStyle
{
    if (@available(iOS 13.0, *)) {
        return (_verticalLayout.theme.isDark || _backgroundView.fullScreenMode)? UIStatusBarStyleLightContent : UIStatusBarStyleDarkContent;
    }
    return (_verticalLayout.theme.isDark || _backgroundView.fullScreenMode)? UIStatusBarStyleLightContent : UIStatusBarStyleDefault;
}


- (void)preRun
{
    dispatch_async(dispatch_get_main_queue(), ^{
        UIApplication.sharedApplication.idleTimerDisabled = true;
    });
    GB_set_pixels_output(&_gb, _gbView.pixels);
    GB_set_sample_rate(&_gb, 96000);
    if (![[[NSUserDefaults standardUserDefaults] stringForKey:@"GBAudioMode"] isEqual:@"off"]) {
        _audioClient = [[GBAudioClient alloc] initWithRendererBlock:^(UInt32 sampleRate, UInt32 nFrames, GB_sample_t *buffer) {
            [_audioLock lock];
            
            if (_audioBufferPosition < nFrames) {
                _audioBufferNeeded = nFrames;
                [_audioLock waitUntilDate:[NSDate dateWithTimeIntervalSinceNow:(double)(_audioBufferNeeded - _audioBufferPosition) / sampleRate]];
                _audioBufferNeeded = 0;
            }
            
            if (_stopping) {
                memset(buffer, 0, nFrames * sizeof(*buffer));
                [_audioLock unlock];
                return;
            }
            
            if (_audioBufferPosition < nFrames) {
                // Not enough audio
                memset(buffer, 0, (nFrames - _audioBufferPosition) * sizeof(*buffer));
                memcpy(buffer, _audioBuffer, _audioBufferPosition * sizeof(*buffer));
                // Do not reset the audio position to avoid more underflows
            }
            else if (_audioBufferPosition < nFrames + 4800) {
                memcpy(buffer, _audioBuffer, nFrames * sizeof(*buffer));
                memmove(_audioBuffer, _audioBuffer + nFrames, (_audioBufferPosition - nFrames) * sizeof(*buffer));
                _audioBufferPosition = _audioBufferPosition - nFrames;
            }
            else {
                memcpy(buffer, _audioBuffer + (_audioBufferPosition - nFrames), nFrames * sizeof(*buffer));
                _audioBufferPosition = 0;
            }
            [_audioLock unlock];
        } andSampleRate:96000];
    }
    
    [_audioClient start];
    if (GB_has_accelerometer(&_gb)) {
        if (@available(iOS 14.0, *)) {
            for (GCController *controller in [GCController controllers]) {
                if (controller.motion.sensorsRequireManualActivation) {
                    [controller.motion setSensorsActive:true];
                }
            }
        }
        [_motionManager startAccelerometerUpdatesToQueue:[NSOperationQueue mainQueue]
                                             withHandler:^(CMAccelerometerData *accelerometerData, NSError *error) {
            if (_lastController.motion) return;
            CMAcceleration data = accelerometerData.acceleration;
            UIInterfaceOrientation orientation = _orientation;
            switch (orientation) {
                case UIInterfaceOrientationUnknown:
                case UIInterfaceOrientationPortrait:
                    break;
                case UIInterfaceOrientationPortraitUpsideDown:
                    data.x = -data.x;
                    data.y = -data.y;
                    break;
                case UIInterfaceOrientationLandscapeLeft: {
                    double tempX = data.x;
                    data.x = data.y;
                    data.y = -tempX;
                    break;
                }
                case UIInterfaceOrientationLandscapeRight:{
                    double tempX = data.x;
                    data.x = -data.y;
                    data.y = tempX;
                    break;
                }
            }
            GB_set_accelerometer_values(&_gb, -data.x, data.y);
        }];
    }
    
    /* Clear pending alarms, don't play alarms while playing */
    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBNotificationsUsed"]) {
        UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
        [center removeDeliveredNotificationsWithIdentifiers:@[[GBROMManager sharedManager].romFile]];
        [center removePendingNotificationRequestsWithIdentifiers:@[[GBROMManager sharedManager].romFile]];
    }
    
    if (GB_rom_supports_alarms(&_gb)) {
        UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
        [center requestAuthorizationWithOptions:UNAuthorizationOptionBadge | UNAuthorizationOptionSound | UNAuthorizationOptionAlert completionHandler:nil];
    }
}

- (void)run
{
    [self loadROM];
    if (!_romLoaded) {
        _running = false;
        _stopping = false;
        return;
    }
    [self preRun];
    while (_running) {
        if (_rewind) {
            _rewind = false;
            GB_rewind_pop(&_gb);
            if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBDynamicSpeed"]) {
                if (!GB_rewind_pop(&_gb)) {
                    dispatch_sync(dispatch_get_main_queue(), ^{
                        if (_runMode == GBRunModeRewind) {
                            self.runMode = GBRunModePaused;
                        }
                    });
                    _rewindOver = true;
                }
            }
            else {
                for (unsigned i = [[NSUserDefaults standardUserDefaults] integerForKey:@"GBRewindSpeed"]; i--;)  {
                    if (!GB_rewind_pop(&_gb)) {
                        dispatch_sync(dispatch_get_main_queue(), ^{
                            if (_runMode == GBRunModeRewind) {
                                self.runMode = GBRunModePaused;
                            }
                        });
                        _rewindOver = true;
                    }
                }
            }
        }
        if (_runMode != GBRunModePaused) {
            GB_run(&_gb);
        }
    }
    [self postRun];
    _stopping = false;
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
didReceiveNotificationResponse:(UNNotificationResponse *)response
         withCompletionHandler:(void (^)(void))completionHandler
{
    if (![response.notification.request.identifier isEqual:[GBROMManager sharedManager].currentROM]) {
        [self application:[UIApplication sharedApplication]
                  openURL:[NSURL fileURLWithPath:response.notification.request.identifier]
                  options:@{}];
    }
    completionHandler();
}

- (UIImage *)imageFromData:(NSData *)data width:(unsigned)width height:(unsigned)height
{
    /* Convert the screenshot to a CGImageRef */
    CGDataProviderRef provider = CGDataProviderCreateWithCFData((CFDataRef)data);
    CGColorSpaceRef colorSpaceRef = CGColorSpaceCreateDeviceRGB();
    CGBitmapInfo bitmapInfo = kCGBitmapByteOrderDefault | kCGImageAlphaNoneSkipLast;
    CGColorRenderingIntent renderingIntent = kCGRenderingIntentDefault;
    
    CGImageRef iref = CGImageCreate(width,
                                    height,
                                    8,
                                    32,
                                    4 * width,
                                    colorSpaceRef,
                                    bitmapInfo,
                                    provider,
                                    NULL,
                                    true,
                                    renderingIntent);

    UIImage *ret = [[UIImage alloc] initWithCGImage:iref];
    CGColorSpaceRelease(colorSpaceRef);
    CGDataProviderRelease(provider);
    CGImageRelease(iref);
    return ret;
}

- (void)postRun
{
    [_audioLock lock];
    _audioBufferPosition = _audioBufferNeeded = 0;
    [_audioLock signal];
    [_audioLock unlock];
    [_audioClient stop];
    _audioClient = nil;

    if (!_swappingROM) {
        GB_save_battery(&_gb, [GBROMManager sharedManager].batterySaveFile.fileSystemRepresentation);
        [self saveStateToFile:[GBROMManager sharedManager].autosaveStateFile];

        NSDate *date;
        [[NSURL fileURLWithPath:[GBROMManager sharedManager].autosaveStateFile] getResourceValue:&date
                                                                                          forKey:NSURLContentModificationDateKey
                                                                                           error:nil];
        _saveDate = date;
        _lastSavedROM = [GBROMManager sharedManager].currentROM;

    }
    [[GBHapticManager sharedManager] setRumbleStrength:0];
    if (@available(iOS 14.0, *)) {
        for (GCController *controller in [GCController controllers]) {
            if (controller.motion.sensorsRequireManualActivation) {
                [controller.motion setSensorsActive:false];
            }
        }
    }
    [_motionManager stopAccelerometerUpdates];
    
    unsigned timeToAlarm = GB_time_to_alarm(&_gb);
    
    if (timeToAlarm) {
        UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
                
        UNMutableNotificationContent *notificationContent = [[UNMutableNotificationContent alloc] init];
        NSString *friendlyName = [[[GBROMManager sharedManager].romFile lastPathComponent] stringByDeletingPathExtension];
        NSRegularExpression *regex = [NSRegularExpression regularExpressionWithPattern:@"\\([^)]+\\)|\\[[^\\]]+\\]" options:0 error:nil];
        friendlyName = [regex stringByReplacingMatchesInString:friendlyName options:0 range:NSMakeRange(0, [friendlyName length]) withTemplate:@""];
        friendlyName = [friendlyName stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        
        notificationContent.title = [NSString stringWithFormat:@"%@ Played an Alarm", friendlyName];
        notificationContent.body = [NSString stringWithFormat:@"%@ requested your attention by playing a scheduled alarm", friendlyName];
        notificationContent.sound = UNNotificationSound.defaultSound;
        
        UNNotificationRequest *request = [UNNotificationRequest requestWithIdentifier:[GBROMManager sharedManager].romFile
                                                                              content:notificationContent
                                                                              trigger:[UNTimeIntervalNotificationTrigger triggerWithTimeInterval:timeToAlarm repeats:false]];
        
        
        [center addNotificationRequest:request withCompletionHandler:nil];
        [[NSUserDefaults standardUserDefaults] setBool:true forKey:@"GBNotificationsUsed"];
    }
    
    dispatch_async(dispatch_get_main_queue(), ^{
        UIApplication.sharedApplication.idleTimerDisabled = false;
    });
}

- (void)start
{
    if (_running) return;
    if (self.presentedViewController) return;
    _running = true;
    dispatch_async(_runQueue, ^{
        [self run];
    });
}

- (void)stop
{
    if (!_running) return;
    [_audioLock lock];
    _stopping = true;
    [_audioLock signal];
    [_audioLock unlock];
    _running = false;
    while (_stopping) {
        [_audioLock lock];
        [_audioLock signal];
        [_audioLock unlock];
    }
    dispatch_sync(_runQueue, ^{});
    dispatch_async(dispatch_get_main_queue(), ^{
        self.runMode = GBRunModeNormal;
        [_backgroundView fadeOverlayOut];
    });
}


- (NSString *)bootROMPathForName:(NSString *)name
{
    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBCustomBootROMs"]) {
        NSString *path = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true)[0];
        path = [path stringByAppendingPathComponent:@"Boot ROMs"];
        path = [path stringByAppendingPathComponent:name];
        path = [path stringByAppendingPathExtension:@"bin"];
        if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
            return path;
        }
    }
    
    return [[NSBundle mainBundle] pathForResource:name ofType:@"bin"];
}

- (void)loadBootROM: (GB_boot_rom_t)type
{
    static NSString *const names[] = {
        [GB_BOOT_ROM_DMG_0] = @"dmg0_boot",
        [GB_BOOT_ROM_DMG] = @"dmg_boot",
        [GB_BOOT_ROM_MGB] = @"mgb_boot",
        [GB_BOOT_ROM_SGB] = @"sgb_boot",
        [GB_BOOT_ROM_SGB2] = @"sgb2_boot",
        [GB_BOOT_ROM_CGB_0] = @"cgb0_boot",
        [GB_BOOT_ROM_CGB] = @"cgb_boot",
        [GB_BOOT_ROM_CGB_E] = @"cgbE_boot",
        [GB_BOOT_ROM_AGB_0] = @"agb0_boot",
        [GB_BOOT_ROM_AGB] = @"agb_boot",
    };
    NSString *name = names[type];
    NSString *path = [self bootROMPathForName:name];
    /* These boot types are not commonly available, and they are indentical
     from an emulator perspective, so fall back to the more common variants
     if they can't be found. */
    if (!path && type == GB_BOOT_ROM_CGB_E) {
        [self loadBootROM:GB_BOOT_ROM_CGB];
        return;
    }
    if (!path && type == GB_BOOT_ROM_AGB_0) {
        [self loadBootROM:GB_BOOT_ROM_AGB];
        return;
    }
    GB_load_boot_rom(&_gb, [path UTF8String]);
}

- (void)vblankWithType:(GB_vblank_type_t)type
{
    if (type != GB_VBLANK_TYPE_REPEAT) {
        [_gbView flip];
        GB_set_pixels_output(&_gb, _gbView.pixels);
    }
    if (_rapidA) {
        _rapidACount++;
        GB_set_key_state(&_gb, GB_KEY_A, !(_rapidACount & 2));
    }
    if (_rapidB) {
        _rapidBCount++;
        GB_set_key_state(&_gb, GB_KEY_B, !(_rapidBCount & 2));
    }
    if (_rapidA || _rapidB) {
        if (_runMode == GBRunModeRewind || _runMode == GBRunModePaused) {
            self.runMode = GBRunModeNormal;
            [_backgroundView fadeOverlayOut];
        }
    }
    _rewind = _runMode == GBRunModeRewind;
}

- (void)gotNewSample:(GB_sample_t *)sample
{
    [_audioLock lock];
    if (_audioClient.isPlaying) {
        if (_audioBufferPosition == _audioBufferSize) {
            if (_audioBufferSize >= 0x4000) {
                _audioBufferPosition = 0;
                [_audioLock unlock];
                return;
            }
            
            if (_audioBufferSize == 0) {
                _audioBufferSize = 512;
            }
            else {
                _audioBufferSize += _audioBufferSize >> 2;
            }
            _audioBuffer = realloc(_audioBuffer, sizeof(*sample) * _audioBufferSize);
        }
        _audioBuffer[_audioBufferPosition++] = *sample;
    }
    if (_audioBufferPosition == _audioBufferNeeded) {
        [_audioLock signal];
        _audioBufferNeeded = 0;
    }
    [_audioLock unlock];
}

- (void)rumbleChanged:(double)amp
{
    [[GBHapticManager sharedManager] setRumbleStrength:amp];
}

- (void)updatePalette
{
    memcpy(&_palette,
           [GBPalettePicker paletteForTheme:[[NSUserDefaults standardUserDefaults] stringForKey:@"GBCurrentTheme"]],
           sizeof(_palette));
    GB_set_palette(&_gb, &_palette);
}

- (bool)handleOpenURLs:(NSArray <NSURL *> *)urls
           openInPlace:(bool)inPlace
{
    NSMutableArray<NSURL *> *validURLs = [NSMutableArray array];
    NSMutableArray<NSString *> *skippedBasenames = [NSMutableArray array];
    NSMutableArray<NSString *> *unusedZips = [NSMutableArray array];
    NSString *tempDir = NSTemporaryDirectory();
    
    __block unsigned tempIndex = 0;
    for (NSURL *url in urls) {
        if ([url.pathExtension.lowercaseString isEqualToString:@"zip"]) {
            GBZipReader *reader = [[GBZipReader alloc] initWithPath:url.path];
            if (!reader) {
                [skippedBasenames addObject:url.lastPathComponent];
                continue;
            }
            __block bool used = false;
            [reader iterateFiles:^bool(NSString *filename, size_t uncompressedSize, bool (^getData)(void *), bool (^writeToPath)(NSString *)) {
                if ([@[@"gb", @"gbc", @"isx"] containsObject:filename.pathExtension.lowercaseString] && uncompressedSize <= 32 * 1024 * 1024) {
                    tempIndex++;
                    NSString *subDir = [tempDir stringByAppendingFormat:@"/%u", tempIndex];
                    mkdir(subDir.UTF8String, 0755);
                    NSString *dest = [subDir stringByAppendingPathComponent:filename.lastPathComponent];
                    if (writeToPath(dest)) {
                        [validURLs addObject:[NSURL fileURLWithPath:dest]];
                        used = true;
                    }
                }
                return true;
            }];
            if (!used) {
                [unusedZips addObject:url.lastPathComponent];
            }
        }
        else if ([@[@"gb", @"gbc", @"isx"] containsObject:url.pathExtension.lowercaseString]) {
            [validURLs addObject:url];
        }
        else {
            [skippedBasenames addObject:url.lastPathComponent];
        }
    }
    
    if (unusedZips.count) {
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"No ROMs in archive"
                                                                       message:[NSString stringWithFormat:@"Could not find any Game Boy ROM files in the following archives:\n%@",
                                                                                [unusedZips componentsJoinedByString:@"\n"]]
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"Close"
                                                  style:UIAlertActionStyleCancel
                                                handler:nil]];
        [self presentViewController:alert animated:true completion:nil];
    }
    
    if (skippedBasenames.count) {
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Unsupported Files"
                                                                       message:[NSString stringWithFormat:@"Could not import the following files because they're not supported:\n%@",
                                                                                [skippedBasenames componentsJoinedByString:@"\n"]]
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"Close"
                                                  style:UIAlertActionStyleCancel
                                                handler:^(UIAlertAction *action) {
            [[NSUserDefaults standardUserDefaults] setBool:false forKey:@"GBShownUTIWarning"]; // Somebody might need a reminder
        }]];
        [self presentViewController:alert animated:true completion:nil];
    }
    
    
    if (validURLs.count == 1 && urls.count == 1) {
        NSURL *url = validURLs.firstObject;
        NSString *potentialROM = [[url.path stringByDeletingLastPathComponent] lastPathComponent];
        if ([[[GBROMManager sharedManager] romFileForROM:potentialROM].stringByStandardizingPath isEqualToString:url.path.stringByStandardizingPath]) {
            [GBROMManager sharedManager].currentROM = potentialROM;
        }
        else {
            [url startAccessingSecurityScopedResource];
            [GBROMManager sharedManager].currentROM =
            [[GBROMManager sharedManager] importROM:url.path
                                       keepOriginal:![url.path hasPrefix:tempDir] && inPlace];
            [url stopAccessingSecurityScopedResource];
        }
        return true;
    }
    for (NSURL *url in validURLs) {
        NSString *potentialROM = [[url.path stringByDeletingLastPathComponent] lastPathComponent];
        if ([[[GBROMManager sharedManager] romFileForROM:potentialROM].stringByStandardizingPath isEqualToString:url.path.stringByStandardizingPath]) {
            // That's an already imported ROM
            continue;
        }
        [url startAccessingSecurityScopedResource];
        [[GBROMManager sharedManager] importROM:url.path
                                   keepOriginal:![url.path hasPrefix:tempDir] && inPlace];
        [url stopAccessingSecurityScopedResource];
    }
    [self openLibrary];
    
    return validURLs.count;
}

- (void)doImportedPaletteNotification
{
    UIVisualEffectView *effectView = [[UIVisualEffectView alloc] initWithEffect:[UIBlurEffect effectWithStyle:UIBlurEffectStyleProminent]];
    effectView.layer.cornerRadius = 8;
    effectView.layer.masksToBounds = true;
    [self.view addSubview:effectView];
    UILabel *tipLabel = [[UILabel alloc] init];
    tipLabel.text = [NSString stringWithFormat:@"Imported palette “%@”", [[NSUserDefaults standardUserDefaults] stringForKey:@"GBCurrentTheme"]];
    if (@available(iOS 13.0, *)) {
        tipLabel.textColor = [UIColor labelColor];
    }
    tipLabel.font = [UIFont systemFontOfSize:16];
    tipLabel.alpha = 0.8;
    [effectView.contentView addSubview:tipLabel];
    
    UIView *view = self.view;
    CGSize outerSize = view.frame.size;
    CGSize size = [tipLabel textRectForBounds:(CGRect){{0, 0},
        {outerSize.width - 32,
            outerSize.height - 32}}
                       limitedToNumberOfLines:1].size;
    size.width = ceil(size.width);
    tipLabel.frame = (CGRect){{8, 8}, size};
    CGRect finalFrame = (CGRect) {
        {round((outerSize.width - size.width - 16) / 2), view.window.safeAreaInsets.top + 12},
        {size.width + 16, size.height + 16}
    };
    
    CGRect initFrame = finalFrame;
    initFrame.origin.y = -initFrame.size.height;
    effectView.frame = initFrame;
    
    effectView.alpha = 0;
    [UIView animateWithDuration:0.5 animations:^{
        effectView.alpha = 1.0;
        effectView.frame = finalFrame;
    }];
    
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(NSEC_PER_SEC * 1.5)), dispatch_get_main_queue(), ^{
        [UIView animateWithDuration:0.5 animations:^{
            effectView.alpha = 0.0;
            effectView.frame = initFrame;
        } completion:^(BOOL finished) {
            if (finished) {
                [effectView removeFromSuperview];
            }
        }];
    });

}

- (BOOL)application:(UIApplication *)app openURL:(NSURL *)url options:(NSDictionary<UIApplicationOpenURLOptionsKey,id> *)options
{
    if (self.presentedViewController && ![self.presentedViewController isKindOfClass:[UIAlertController class]]) {
        [self dismissViewController];
    }
    if ([url.pathExtension.lowercaseString isEqual:@"sbp"]) {
        [url startAccessingSecurityScopedResource];
        bool success = [GBPalettePicker importPalette:url.path];
        [url stopAccessingSecurityScopedResource];
        if (!success) {
            UIAlertController *alertController = [UIAlertController alertControllerWithTitle:@"Palette Import Failed"
                                                                                     message:@"The imported palette file is invalid."
                                                                              preferredStyle:UIAlertControllerStyleAlert];
            [alertController addAction:[UIAlertAction actionWithTitle:@"Close" style:UIAlertActionStyleDefault handler:nil]];
            [self presentViewController:alertController animated:true completion:nil];
        }
        else {
            [self doImportedPaletteNotification];
        }
        return success;
    }
    NSString *potentialROM = [[url.path stringByDeletingLastPathComponent] lastPathComponent];
    if ([[[GBROMManager sharedManager] romFileForROM:potentialROM].stringByStandardizingPath isEqualToString:url.path.stringByStandardizingPath]) {
        [self stop];
        [GBROMManager sharedManager].currentROM = potentialROM;
        [self start];
        return [GBROMManager sharedManager].currentROM != nil;
    }
    return [self handleOpenURLs:@[url]
                    openInPlace:[options[UIApplicationOpenURLOptionsOpenInPlaceKey] boolValue]];
}

- (void)setRunMode:(GBRunMode)runMode ignoreDynamicSpeed:(bool)ignoreDynamicSpeed
{
    if (runMode == GBRunModeRewind && _rewindOver) {
        runMode = GBRunModePaused;
    }
    if (runMode == _runMode) return;
    if (_runMode == GBRunModePaused) {
        [_audioClient start];
    }
    _runMode = runMode;
    if (_runMode == GBRunModePaused) {
        [_audioClient stop];
    }
    
    if (_runMode == GBRunModeNormal || _runMode == GBRunModeTurbo || _runMode == GBRunModeUnderclock) {
        _rewindOver = false;
    }
    
    if (_runMode == GBRunModeNormal  || _runMode == GBRunModeUnderclock || !([[NSUserDefaults standardUserDefaults] boolForKey:@"GBDynamicSpeed"] && !ignoreDynamicSpeed)) {
        if (_runMode == GBRunModeTurbo) {
            GB_set_turbo_mode(&_gb, true, false);
        }
        else if (_runMode == GBRunModeUnderclock) {
            GB_set_clock_multiplier(&_gb, 0.5);
        }
        else {
            GB_set_turbo_mode(&_gb, false, false);
            GB_set_clock_multiplier(&_gb, 1.0);
        }
    }
}

- (void)setRunMode:(GBRunMode)runMode
{
    [self setRunMode:runMode ignoreDynamicSpeed:false];
}

- (AVCaptureDevice *)captureDevice
{
    if (_cameraPosition == AVCaptureDevicePositionFront) {
        return _frontCaptureDevice ?: [AVCaptureDevice defaultDeviceWithMediaType: AVMediaTypeVideo];
    }
    return _backCaptureDevice ?: [AVCaptureDevice defaultDeviceWithMediaType: AVMediaTypeVideo];
}

- (void)cameraRequestUpdate
{
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!_cameraSession) {
            dispatch_async(_cameraQueue, ^{
                @try {
                    if (!_cameraSession) {
                        NSError *error;
                        AVCaptureDevice *device = [self captureDevice];
                        AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice: device error: &error];
                        
                        if (!input) {
                            GB_camera_updated(&_gb);
                            return;
                        }
                        
                        _cameraOutput = [[AVCaptureVideoDataOutput alloc] init];
                        [_cameraOutput setVideoSettings: @{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)}];
                        [_cameraOutput setSampleBufferDelegate:self
                                                         queue:_cameraQueue];
                        
                        
                        
                        _cameraSession = [AVCaptureSession new];
                        _cameraSession.sessionPreset = AVCaptureSessionPreset352x288;
                        
                        [_cameraSession addInput: input];
                        [_cameraSession addOutput: _cameraOutput];
                        _cameraConnection = [_cameraOutput connectionWithMediaType: AVMediaTypeVideo];
                        _cameraConnection.videoOrientation = AVCaptureVideoOrientationPortrait;
                        [_cameraSession startRunning];
                    }
                }
                @catch (NSException *exception) {
                    /* I have not tested camera support on many devices, so we catch exceptions just in case. */
                    GB_camera_updated(&_gb);
                }
            });
        }
        
        _cameraNeedsUpdate = true;
        [_disableCameraTimer invalidate];
        if (!_cameraPositionButton.alpha) {
            [UIView animateWithDuration:0.25 animations:^{
                _cameraPositionButton.alpha = 1;
            }];
        }
        if (_changeCameraButton) {
            // The change camera button is only available when we are using a capture device on the back of the device
            double changeCameraButtonAlpha = (_cameraPosition == AVCaptureDevicePositionFront) ? 0 : 1;
            if (changeCameraButtonAlpha != _changeCameraButton.alpha) {
                [UIView animateWithDuration:0.25 animations:^{
                    _changeCameraButton.alpha = changeCameraButtonAlpha;
                }];
            }
        }

        _disableCameraTimer = [NSTimer scheduledTimerWithTimeInterval:1
                                                             repeats:false
                                                               block:^(NSTimer *timer) {
            if (_cameraPositionButton.alpha) {
                [UIView animateWithDuration:0.25 animations:^{
                    _cameraPositionButton.alpha = 0;
                }];
            }
            if (_changeCameraButton.alpha) {
                [UIView animateWithDuration:0.25 animations:^{
                    _changeCameraButton.alpha = 0;
                }];
            }
            dispatch_async(_cameraQueue, ^{
                [_cameraSession stopRunning];
                _cameraSession = nil;
                _cameraConnection = nil;
                _cameraOutput = nil;
            });
        }];
    });
}

- (uint8_t)cameraGetPixelAtX:(uint8_t)x andY:(uint8_t)y
{
    if (!_cameraImage) {
        return 0;
    }
    if (_cameraNeedsUpdate) {
        return 0;
    }
    
    y += 8; // Equalize X and Y for rotation as a 128x128 image
    
    if (_cameraPosition == AVCaptureDevicePositionFront) {
        x = 127 - x;
    }
    
    switch (_orientation) {
        case UIInterfaceOrientationUnknown:
        case UIInterfaceOrientationPortrait:
            break;
        case UIInterfaceOrientationPortraitUpsideDown:
            x = 127 - x;
            y = 127 - y;
            break;
        case UIInterfaceOrientationLandscapeLeft: {
            uint8_t tempX = x;
            x = y;
            y = 127 - tempX;
            break;
        }
        case UIInterfaceOrientationLandscapeRight:{
            uint8_t tempX = x;
            x = 127 - y;
            y = tempX;
            break;
        }
    }
    
    if (_cameraPosition == AVCaptureDevicePositionFront) {
        x = 127 - x;
    }
    
    // Center the 128*128 image on the 130*XXX (or XXX*130) captured image
    unsigned offsetX = (CVPixelBufferGetWidth(_cameraImage) - 128) / 2;
    unsigned offsetY = (CVPixelBufferGetHeight(_cameraImage) - 112) / 2;
    
    uint8_t *baseAddress = (uint8_t *)CVPixelBufferGetBaseAddress(_cameraImage);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(_cameraImage);
    uint8_t ret = baseAddress[(x + offsetX) * 4 + (y + offsetY) * bytesPerRow + 2];
    
    return ret;
}

- (void)captureOutput:(AVCaptureOutput *)captureOutput didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection *)connection
{
    if (!_cameraNeedsUpdate) return;
    CVImageBufferRef buffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    CIImage *image = [CIImage imageWithCVPixelBuffer:buffer
                                             options:[NSDictionary dictionaryWithObjectsAndKeys:[NSNull null], kCIImageColorSpace, nil]];
    double scale = MAX(130.0 / CVPixelBufferGetWidth(buffer), 130.0 / CVPixelBufferGetHeight(buffer));
    image = [image imageByApplyingTransform:CGAffineTransformMakeScale(scale, scale)];
    if (_cameraImage) {
        CVPixelBufferUnlockBaseAddress(_cameraImage, 0);
        CVBufferRelease(_cameraImage);
        _cameraImage = NULL;
    }
    CGSize size = image.extent.size;
    CVPixelBufferCreate(kCFAllocatorDefault,
                        size.width,
                        size.height,
                        kCVPixelFormatType_32BGRA,
                        NULL,
                        &_cameraImage);
    [[[CIContext alloc] init] render:image toCVPixelBuffer:_cameraImage];
    CVPixelBufferLockBaseAddress(_cameraImage, 0);
    
    GB_camera_updated(&_gb);
    
    _cameraNeedsUpdate = false;
}

- (void)rotateCamera
{
    dispatch_async(_cameraQueue, ^{
        _cameraPosition ^= AVCaptureDevicePositionBack ^ AVCaptureDevicePositionFront;
        [_cameraSession stopRunning];
        _cameraSession = nil;
        _cameraConnection = nil;
        _cameraOutput = nil;
        if (_cameraNeedsUpdate) {
            _cameraNeedsUpdate = false;
            GB_camera_updated(&_gb);
        }
    });
}

- (void)changeCamera
{
    dispatch_async(_cameraQueue, ^{
        if (![_backCaptureDevice lockForConfiguration:nil]) return;
        _currentZoomIndex++;
        if (_currentZoomIndex == _zoomLevels.count) {
            _currentZoomIndex = 0;
        }
        [_backCaptureDevice rampToVideoZoomFactor:_zoomLevels[_currentZoomIndex].doubleValue withRate:2];
        [_backCaptureDevice unlockForConfiguration];
    });
}

- (void)disconnectCable
{
    if (!_printerConnected) return;
    _printerConnected = false;
    _currentPrinterImageData = nil;
    [UIView animateWithDuration:0.25 animations:^{
        _printerButton.alpha = 0;
    }];
    [_printerSpinner stopAnimating];
    GB_disconnect_serial(&_gb);
}

- (void)connectPrinter
{
    if (_printerConnected) return;
    _printerConnected = true;
    GB_connect_printer(&_gb, printImage, printDone);
}

- (void)openConnectMenu
{
    UIAlertControllerStyle style = [UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad?
    UIAlertControllerStyleAlert : UIAlertControllerStyleActionSheet;
    GBCheckableAlertController *menu = [GBCheckableAlertController alertControllerWithTitle:@"Connect Accessory"
                                                                                    message:@"Choose an accessory to connect."
                                                                             preferredStyle:style];
    [menu addAction:[UIAlertAction actionWithTitle:@"None"
                                             style:UIAlertActionStyleDefault
                                           handler:^(UIAlertAction *action) {
        [self disconnectCable];
    }]];
    [menu addAction:[UIAlertAction actionWithTitle:@"Game Boy Printer"
                                             style:UIAlertActionStyleDefault
                                           handler:^(UIAlertAction *action) {
        [self connectPrinter];
    }]];
    menu.selectedAction = menu.actions[_printerConnected];
    [menu addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                            style:UIAlertActionStyleCancel
                                          handler:nil]];
    [self presentViewController:menu animated:true completion:nil];
}

- (void)printImage:(uint32_t *)imageBytes height:(unsigned) height
         topMargin:(unsigned) topMargin bottomMargin: (unsigned) bottomMargin
          exposure:(unsigned) exposure
{
    uint32_t paddedImage[160 * (topMargin + height + bottomMargin)];
    memset(paddedImage, 0xFF, sizeof(paddedImage));
    memcpy(paddedImage + (160 * topMargin), imageBytes, 160 * height * sizeof(imageBytes[0]));
    if (!_currentPrinterImageData) {
        _currentPrinterImageData = [[NSMutableData alloc] init];
    }
    [_currentPrinterImageData appendBytes:paddedImage length:sizeof(paddedImage)];

    dispatch_async(dispatch_get_main_queue(), ^{
        [UIView animateWithDuration:0.25 animations:^{
            _printerButton.alpha = 1;
        }];
        [_printerSpinner startAnimating];
    });
    
}

- (void)printDone
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [_printerSpinner stopAnimating];
    });
}

- (void)showPrinterFeed
{
    UIImage *image = [self imageFromData:_currentPrinterImageData
                                   width:160
                                  height:_currentPrinterImageData.length / 160 / sizeof(uint32_t)];

    _window.backgroundColor = [UIColor blackColor];
    [self presentViewController:[[GBPrinterFeedController alloc] initWithImage:image]
                       animated:true
                     completion:nil];
    
}

- (void)emptyPrinterFeed
{
    _currentPrinterImageData = nil;
    [UIView animateWithDuration:0.25 animations:^{
        _printerButton.alpha = 0;
    }];
    [_printerSpinner stopAnimating];
    [self dismissViewController];
}

@end

/* +[UIColor labelColor] is broken in some contexts in iOS 26 and despite being such a critical method
   Apple isn't going to fix this in time. */
API_AVAILABLE(ios(19.0))
@implementation UIColor(SolariumBugs)
+ (UIColor *)_labelColor
{
    return [UIColor colorWithDynamicProvider:^UIColor *(UITraitCollection *traitCollection) {
        switch (traitCollection.userInterfaceStyle) {
                
            case UIUserInterfaceStyleUnspecified:
            case UIUserInterfaceStyleLight:
                return [UIColor blackColor];
            case UIUserInterfaceStyleDark:
                return [UIColor whiteColor];
        }
    }];
}

+ (void)load
{
    if (@available(iOS 19.0, *)) {
        method_setImplementation(class_getClassMethod(self, @selector(labelColor)),
                                 [self methodForSelector:@selector(_labelColor)]);
    }
}

@end

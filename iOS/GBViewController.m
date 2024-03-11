#import "GBViewController.h"
#import "GBHorizontalLayout.h"
#import "GBVerticalLayout.h"
#import "GBViewMetal.h"
#import "GBAudioClient.h"
#import "GBROMManager.h"
#import "GBLoadROMTableViewController.h"
#import "GBBackgroundView.h"
#import "GBHapticManager.h"
#import "GBMenuViewController.h"
#import "GBOptionViewController.h"
#import "GBAboutController.h"
#import "GBSettingsViewController.h"
#import "GBStatesViewController.h"
#import <CoreMotion/CoreMotion.h>
#import <dlfcn.h>

@implementation GBViewController
{
    GB_gameboy_t _gb;
    GBView *_gbView;
    
    volatile bool _running;
    volatile bool _stopping;
    bool _rewind;
    bool _rewindOver;
    bool _romLoaded;
    
    UIInterfaceOrientation _orientation;
    GBHorizontalLayout *_horizontalLayout;
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

static void consoleLog(GB_gameboy_t *gb, const char *string, GB_log_attributes attributes)
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

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    _window = [[UIWindow alloc] init];
    _window.rootViewController = self;
    [_window makeKeyAndVisible];
    
    _window.backgroundColor = [UIColor colorWithRed:174 / 255.0 green:176 / 255.0 blue:180 / 255.0 alpha:1.0];
    
    _horizontalLayout = [[GBHorizontalLayout alloc] init];
    _verticalLayout = [[GBVerticalLayout alloc] init];
    
    _backgroundView = [[GBBackgroundView alloc] init];
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
    
    [self loadROM];
    [[NSNotificationCenter defaultCenter] addObserverForName:@"GBROMChanged"
                                                      object:nil
                                                       queue:nil
                                                  usingBlock:^(NSNotification *note) {
        [self loadROM];
        [self start];
    }];
    
    _motionManager = [[CMMotionManager alloc] init];
    _cameraPosition = AVCaptureDevicePositionBack;
    _cameraPositionButton = [[UIButton alloc] initWithFrame:CGRectMake(8,
                                                                       _backgroundView.bounds.size.height - 8 - 32,
                                                                       32,
                                                                       32)];
    _cameraPositionButton.autoresizingMask = UIViewAutoresizingFlexibleRightMargin | UIViewAutoresizingFlexibleTopMargin;
    if (@available(iOS 13.0, *)) {
        [_cameraPositionButton  setImage:[UIImage systemImageNamed:@"camera.rotate"
                                                 withConfiguration:[UIImageSymbolConfiguration configurationWithScale:UIImageSymbolScaleLarge]]
                                forState:UIControlStateNormal];
        _cameraPositionButton.backgroundColor = [UIColor systemBackgroundColor];
    }
    else {
        UIImage *image = [[UIImage imageNamed:@"CameraRotateTemplate"] imageWithRenderingMode:UIImageRenderingModeAlwaysTemplate];
        [_cameraPositionButton  setImage:image
                                forState:UIControlStateNormal];
        _cameraPositionButton.backgroundColor = [UIColor whiteColor];
    }
    _cameraPositionButton.layer.cornerRadius = 6;
    _cameraPositionButton.alpha = 0;
    [_cameraPositionButton addTarget:self
                              action:@selector(rotateCamera)
                    forControlEvents:UIControlEventTouchUpInside];
    [_backgroundView addSubview:_cameraPositionButton];
    
    [UNUserNotificationCenter currentNotificationCenter].delegate = self;
    [self verifyEntitlements];

    return true;
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
    extern void xpc_release$ (xpc_object_t *object);
    extern const char *xpc_dictionary_get_string$ (xpc_object_t *object, const char *key);
    
    typeof(xpc_copy_entitlements_for_self$) *xpc_copy_entitlements_for_self = dlsym(libxpc, "xpc_copy_entitlements_for_self");
    typeof(xpc_release$) *xpc_release = dlsym(libxpc, "xpc_release");
    typeof(xpc_dictionary_get_string$) *xpc_dictionary_get_string = dlsym(libxpc, "xpc_dictionary_get_string");
    
    if (!xpc_copy_entitlements_for_self || !xpc_release || !xpc_dictionary_get_string) return;
    
    xpc_object_t entitlements = xpc_copy_entitlements_for_self();
    if (!entitlements) return;
    const char *_entIdentifier = xpc_dictionary_get_string(entitlements, "application-identifier");
    NSString *entIdentifier = _entIdentifier? @(_entIdentifier) : nil;
    
    const char *_teamIdentifier = xpc_dictionary_get_string(entitlements, "com.apple.developer.team-identifier");
    NSString *teamIdentifier = _teamIdentifier? @(_teamIdentifier) : nil;
    
    xpc_release(entitlements);
    
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
        [alert  addAction:[UIAlertAction actionWithTitle:@"Close"
                                                   style:UIAlertActionStyleCancel
                                                 handler:nil]];
        [self presentViewController:alert animated:true completion:nil];
    }
}

- (void)saveStateToFile:(NSString *)file
{
    GB_save_state(&_gb, file.fileSystemRepresentation);
    NSData *data = [NSData dataWithBytes:_gbView.previousBuffer
                                  length:GB_get_screen_width(&_gb) *
                    GB_get_screen_height(&_gb) *
                    sizeof(*_gbView.previousBuffer)];
    UIImage *screenshot = [self imageFromData:data width:GB_get_screen_width(&_gb) height:GB_get_screen_height(&_gb)];
    [UIImagePNGRepresentation(screenshot) writeToFile:[file stringByAppendingPathExtension:@"png"] atomically:false];
}

- (void)loadStateFromFile:(NSString *)file
{
    GB_model_t model;
    if (!GB_get_state_model(file.fileSystemRepresentation, &model)) {
        if (GB_get_model(&_gb) != model) {
            GB_switch_model_and_reset(&_gb, model);
        }
        GB_load_state(&_gb, file.fileSystemRepresentation);
    }
}

- (void)loadROM
{
    [self stop];
    GBROMManager *romManager = [GBROMManager sharedManager];
    if (romManager.romFile) {
        // Todo: display errors and warnings
        if ([romManager.romFile.pathExtension.lowercaseString isEqualToString:@"isx"]) {
            _romLoaded = GB_load_isx(&_gb, romManager.romFile.fileSystemRepresentation) == 0;
        }
        else {
            _romLoaded = GB_load_rom(&_gb, romManager.romFile.fileSystemRepresentation) == 0;
        }
        GB_rewind_reset(&_gb);
        if (_romLoaded) {
            GB_reset(&_gb);
            GB_load_battery(&_gb, [GBROMManager sharedManager].batterySaveFile.fileSystemRepresentation);
            [self loadStateFromFile:[GBROMManager sharedManager].autosaveStateFile];
        }
    }
    else {
        _romLoaded = false;
    }
    _gbView.hidden = !_romLoaded;
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
    if (self.presentedViewController) return;
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
    [self stop];
    GB_reset(&_gb);
    [self start];
}

- (void)openLibrary
{
    UINavigationController *controller = [[UINavigationController alloc] initWithRootViewController:[[GBLoadROMTableViewController alloc] init]];
    UIBarButtonItem *close = [[UIBarButtonItem alloc] initWithTitle:@"Close"
                                                             style:UIBarButtonItemStylePlain
                                                             target:self
                                                             action:@selector(dismissViewController)];
    [controller.visibleViewController.navigationItem setLeftBarButtonItem:close];
    [self presentViewController:controller
                       animated:true
                     completion:nil];
}

- (void)changeModel
{
    GBOptionViewController *controller = [[GBOptionViewController alloc] initWithHeader:@"Select a model to emulate"];
    controller.footer = @"Changing the emulated model will reset the emulator";
    
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
            GB_switch_model_and_reset(&_gb, model);
            [self start];
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
    UINavigationController *controller = [[UINavigationController alloc] initWithRootViewController:[[GBStatesViewController alloc] init]];
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
    UIBarButtonItem *close = [[UIBarButtonItem alloc] initWithTitle:@"Close"
                                                              style:UIBarButtonItemStylePlain
                                                             target:self
                                                             action:@selector(dismissViewController)];
    [self presentViewController:[GBSettingsViewController settingsViewControllerWithLeftButton:close]
                       animated:true
                     completion:nil];
}

- (void)showAbout
{
    [self presentViewController:[[GBAboutController alloc] init] animated:true completion:nil];
}

- (void)dismissViewControllerAnimated:(BOOL)flag completion:(void (^)(void))completion
{
    [super dismissViewControllerAnimated:flag completion:^() {
        if (completion) {
            completion();
        }
        if (!self.presentedViewController) {
            [self start];
        }
    }];
}

- (void)dismissViewController
{
    [self dismissViewControllerAnimated:true completion:nil];
}

- (void)applicationWillResignActive:(UIApplication *)application
{
    [self stop];
}

- (void)willRotateToInterfaceOrientation:(UIInterfaceOrientation)orientation duration:(NSTimeInterval)duration
{
    GBLayout *layout = _horizontalLayout;
    _orientation = orientation;
    if (orientation == UIInterfaceOrientationPortrait || orientation == UIInterfaceOrientationPortraitUpsideDown) {
        layout = _verticalLayout;
    }
    _backgroundView.frame = [layout viewRectForOrientation:orientation];
    _backgroundView.layout = layout;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations
{
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

- (void)preRun
{
    GB_set_pixels_output(&_gb, _gbView.pixels);
    GB_set_sample_rate(&_gb, 96000);
    _audioClient = [[GBAudioClient alloc] initWithRendererBlock:^(UInt32 sampleRate, UInt32 nFrames, GB_sample_t *buffer) {
        [_audioLock lock];
        
        if (_audioBufferPosition < nFrames) {
            _audioBufferNeeded = nFrames;
            [_audioLock waitUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.125]];
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
            _audioBufferPosition = 0;
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
    
    [_audioClient start];
    if (GB_has_accelerometer(&_gb)) {
        [_motionManager startAccelerometerUpdatesToQueue:[NSOperationQueue mainQueue]
                                             withHandler:^(CMAccelerometerData *accelerometerData, NSError *error) {
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
    [self preRun];
    while (_running) {
        if (_rewind) {
            _rewind = false;
            GB_rewind_pop(&_gb);
            if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBDynamicSpeed"]) {
                if (!GB_rewind_pop(&_gb)) {
                    self.runMode = GBRunModePaused;
                    _rewindOver = true;
                }
            }
            else {
                for (unsigned i = [[NSUserDefaults standardUserDefaults] integerForKey:@"GBRewindSpeed"]; i--;)  {
                    if (!GB_rewind_pop(&_gb)) {
                        self.runMode = GBRunModePaused;
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
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, data.bytes, data.length, NULL);
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
    memset(_audioBuffer, 0, (_audioBufferSize - _audioBufferPosition) * sizeof(*_audioBuffer));
    _audioBufferPosition = _audioBufferNeeded;
    [_audioLock signal];
    [_audioLock unlock];
    [_audioClient stop];
    _audioClient = nil;

    GB_save_battery(&_gb, [GBROMManager sharedManager].batterySaveFile.fileSystemRepresentation);
    [self saveStateToFile:[GBROMManager sharedManager].autosaveStateFile];
    [[GBHapticManager sharedManager] setRumbleStrength:0];
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
}

- (void)start
{
    if (!_romLoaded) return;
    if (_running) return;
    _running = true;
    [[[NSThread alloc] initWithTarget:self selector:@selector(run) object:nil] start];
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
        [GB_BOOT_ROM_AGB] = @"agb_boot",
    };
    GB_load_boot_rom(&_gb, [[[NSBundle mainBundle] pathForResource:names[type] ofType:@"bin"] UTF8String]);
}

- (void)vblankWithType:(GB_vblank_type_t)type
{
    if (type != GB_VBLANK_TYPE_REPEAT) {
        [_gbView flip];
        GB_set_pixels_output(&_gb, _gbView.pixels);
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
           [GBSettingsViewController paletteForTheme:[[NSUserDefaults standardUserDefaults] stringForKey:@"GBCurrentTheme"]],
           sizeof(_palette));
    GB_set_palette(&_gb, &_palette);
}

- (BOOL)application:(UIApplication *)app openURL:(NSURL *)url options:(NSDictionary<UIApplicationOpenURLOptionsKey,id> *)options
{
    [self stop];
    NSString *potentialROM = [[url.path stringByDeletingLastPathComponent] lastPathComponent];
    if ([[[GBROMManager sharedManager] romFileForROM:potentialROM].stringByStandardizingPath isEqualToString:url.path.stringByStandardizingPath]) {
        [GBROMManager sharedManager].currentROM = potentialROM;
    }
    else {
        [url startAccessingSecurityScopedResource];
        [GBROMManager sharedManager].currentROM =
            [[GBROMManager sharedManager] importROM:url.path
                                       keepOriginal:[options[UIApplicationOpenURLOptionsOpenInPlaceKey] boolValue]];
        [url stopAccessingSecurityScopedResource];
    }
    [self loadROM];
    [self start];
    return [GBROMManager sharedManager].currentROM != nil;
}

- (void)setRunMode:(GBRunMode)runMode
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
    
    if (_runMode == GBRunModeNormal || _runMode == GBRunModeTurbo) {
        _rewindOver = false;
    }
    
    if (_runMode == GBRunModeNormal || [[NSUserDefaults standardUserDefaults] boolForKey:@"GBDynamicSpeed"]) {
        if (_runMode == GBRunModeTurbo) {
            double multiplier = [[NSUserDefaults standardUserDefaults] doubleForKey:@"GBTurboSpeed"];
            GB_set_turbo_mode(&_gb, multiplier == 1, false);
            GB_set_clock_multiplier(&_gb, multiplier);
        }
        else {
            GB_set_turbo_mode(&_gb, false, false);
            GB_set_clock_multiplier(&_gb, 1.0);
        }
    }
}

- (AVCaptureDevice *)captureDevice
{
    NSArray *devices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
    for (AVCaptureDevice *device in devices) {
        if ([device position] == _cameraPosition) {
            return device;
        }
    }
    return [AVCaptureDevice defaultDeviceWithMediaType: AVMediaTypeVideo];
}

- (void)cameraRequestUpdate
{
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
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
                                                queue:dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0)];
                
                
                
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
        _cameraNeedsUpdate = true;
        [_disableCameraTimer invalidate];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!_cameraPositionButton.alpha) {
                [UIView animateWithDuration:0.25 animations:^{
                    _cameraPositionButton.alpha = 1;
                }];
            }
            _disableCameraTimer = [NSTimer scheduledTimerWithTimeInterval:1
                                                                 repeats:false
                                                                   block:^(NSTimer *timer) {
                if (_cameraPositionButton.alpha) {
                    [UIView animateWithDuration:0.25 animations:^{
                        _cameraPositionButton.alpha = 0;
                    }];
                }
                [_cameraSession stopRunning];
                _cameraSession = nil;
                _cameraConnection = nil;
                _cameraOutput = nil;
            }];
        });
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
    _cameraPosition ^= AVCaptureDevicePositionBack ^ AVCaptureDevicePositionFront;
    [_cameraSession stopRunning];
    _cameraSession = nil;
    _cameraConnection = nil;
    _cameraOutput = nil;
    if (_cameraNeedsUpdate) {
        _cameraNeedsUpdate = false;
        GB_camera_updated(&_gb);
    }

}

@end

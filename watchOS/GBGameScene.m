#import "GBGameScene.h"
#import "GBPhoneManager.h"
#import "GBGizmoAudioClient.h"
#import <AVFAudio/AVFAudio.h>
#import <mach/mach.h>
#import <Core/gb.h>
#pragma clang diagnostic ignored "-Warc-retain-cycles"

@implementation GBGameScene
{
    SKSpriteNode *_screen;
    SKLabelNode *_label;
    SKSpriteNode *_iPhoneIcon;
    SKMutableTexture *_texture;
    GB_gameboy_t _gb;
    uint32_t _pixels[256 * 224 * 2];
    SKSpriteNode *_holdSprite;
    volatile bool _running, _stopping;
    bool _activeBuffer;
    bool _romLoaded;
    NSMutableSet *_defaultsObservers;
    GBGizmoAudioClient *_audioClient;
}

static void nop_log_callback()
{
    
}

static uint32_t rgbEncode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    return (r << 0) | (g << 8) | (b << 16) | 0xFF000000;
}

static void sampleCallback(GB_gameboy_t *gb, GB_sample_t *sample)
{
    GBGameScene *self = (__bridge GBGameScene *)GB_get_user_data(gb);
    [self sampleCallback:sample];
}

static void loadBootROM(GB_gameboy_t *gb, GB_boot_rom_t type)
{
    GBGameScene *self = (__bridge GBGameScene *)GB_get_user_data(gb);
    [self loadBootROM:type];
}

static void vblank(GB_gameboy_t *gb)
{
    GBGameScene *self = (__bridge GBGameScene *)GB_get_user_data(gb);
    [self vblank];
}

- (NSString *)bootROMPathForName:(NSString *)name
{
    NSString *path = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true)[0];
    path = [path stringByAppendingPathComponent:@"Boot ROMs"];
    path = [path stringByAppendingPathComponent:name];
    path = [path stringByAppendingPathExtension:@"bin"];
    if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
        return path;
    }
    
    return [[NSBundle mainBundle] pathForResource:name ofType:@"bin"];
}

- (void) loadBootROM:(GB_boot_rom_t)type
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

- (void)start
{
    if (_running) return;
    if (!_romLoaded) return;
    _running = true;
    if (![[[NSUserDefaults standardUserDefaults] stringForKey:@"GBAudioMode"] isEqual:@"off"]) {
        [_audioClient start];
    }
    [NSThread detachNewThreadWithBlock:^{
        while (_running) {
            GB_run(&_gb);
        }
        _stopping = false;
    }];
}

- (void)stop
{
    if (!_running) return;
    _stopping = true;
    _running = false;
    while (_stopping);
    [_audioClient stop];
    
    NSString *tempPath = [GBPhoneManager.sharedManager.saveStatePath stringByAppendingPathExtension:@"tmp"];
    if (!GB_save_state(&_gb, tempPath.UTF8String)) {
        rename(tempPath.UTF8String, GBPhoneManager.sharedManager.saveStatePath.UTF8String);
    }
    const uint32_t *buffer = !_activeBuffer? _pixels : _pixels + 256 * 224;
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL,
                                                              buffer + 48 + 40 * 256,
                                                              144 * 256 * 4, NULL);
    CGColorSpaceRef colorSpaceRef = CGColorSpaceCreateDeviceRGB();
    CGBitmapInfo bitmapInfo = kCGBitmapByteOrderDefault | kCGImageAlphaNoneSkipLast;
    CGColorRenderingIntent renderingIntent = kCGRenderingIntentDefault;
    
    CGImageRef iref = CGImageCreate(160,
                                    144,
                                    8,
                                    32,
                                    4 * 256,
                                    colorSpaceRef,
                                    bitmapInfo,
                                    provider,
                                    NULL,
                                    true,
                                    renderingIntent);
    
    UIImage *image = [[UIImage alloc] initWithCGImage:iref];
    CGColorSpaceRelease(colorSpaceRef);
    CGDataProviderRelease(provider);
    CGImageRelease(iref);
    [UIImagePNGRepresentation(image) writeToFile:GBPhoneManager.sharedManager.pngPath
                                      atomically:false];
    
    [GBPhoneManager.sharedManager updateSaveState:nil];
    
}

- (void)loadROM
{
    GB_model_t model = GB_MODEL_CGB_E;
    GBPhoneManager *phoneManager = [GBPhoneManager sharedManager];
    NSDictionary *metadata = [NSDictionary dictionaryWithContentsOfFile:phoneManager.metadataPath];
    if (!metadata) {
        _romLoaded = false;
        return;
    }
    if (GB_get_state_model(phoneManager.saveStatePath.UTF8String, &model)) {
        model = [metadata[@"model"] unsignedIntValue];
    }
    
    if ([metadata[@"isx"] boolValue]) {
        _romLoaded = GB_load_isx(&_gb, phoneManager.romPath.UTF8String) == 0;
    }
    else {
        _romLoaded = GB_load_rom(&_gb, phoneManager.romPath.UTF8String) == 0;
    }
    if (!_romLoaded) return;
    GB_switch_model_and_reset(&_gb, model);
    if (GB_load_state(&_gb, phoneManager.saveStatePath.UTF8String)) {
        if (GB_load_battery(&_gb, phoneManager.batteryPath.UTF8String) == 0) {
            GB_save_state(&_gb, phoneManager.saveStatePath.UTF8String);
            unlink(phoneManager.batteryPath.UTF8String);
        }
    }
}

- (const GB_palette_t *)currentPalette
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSString *theme = [defaults stringForKey:@"GBCurrentTheme"];
    if ([theme isEqualToString:@"Greyscale"]) {
        return &GB_PALETTE_GREY;
    }
    if ([theme isEqualToString:@"Lime (Game Boy)"]) {
        return &GB_PALETTE_DMG;
    }
    if ([theme isEqualToString:@"Olive (Pocket)"]) {
        return &GB_PALETTE_MGB;
    }
    if ([theme isEqualToString:@"Teal (Light)"]) {
        return &GB_PALETTE_GBL;
    }
    static GB_palette_t customPalette;
    NSArray *colors = [defaults dictionaryForKey:@"GBThemes"][theme][@"Colors"];
    if (colors.count != 5) return &GB_PALETTE_DMG;
    unsigned i = 0;
    for (NSNumber *color in colors) {
        uint32_t c = [color unsignedIntValue];
        customPalette.colors[i++] = (struct GB_color_s) {c, c >> 8, c >> 16};
    }
    return &customPalette;
}

- (void)setLabelString:(NSString *)string
{
    NSMutableParagraphStyle *style = [NSParagraphStyle defaultParagraphStyle].mutableCopy;
    style.alignment = NSTextAlignmentCenter;
    _label.attributedText = [[NSMutableAttributedString alloc] initWithString:string
                                                                   attributes:@{
        NSParagraphStyleAttributeName: style,
        NSForegroundColorAttributeName: [UIColor whiteColor],
        NSFontAttributeName: [UIFont systemFontOfSize:16],
    }];
}

- (void)sceneDidLoad
{
    // Setup your scene here
    _texture = [[SKMutableTexture alloc] initWithSize:CGSizeMake(256, 224)];
    _texture.filteringMode = SKTextureFilteringNearest;
    _screen = [SKSpriteNode spriteNodeWithTexture:_texture size:CGSizeMake(256, 224)];
    _screen.yScale = -1;
    [self addChild:_screen];
    
    _label = [SKLabelNode labelNodeWithText:@""];
    [self setLabelString:@"Communicating with SameBoy on your iPhone..."];
    _label.horizontalAlignmentMode = SKLabelHorizontalAlignmentModeCenter;
    _label.verticalAlignmentMode = SKLabelVerticalAlignmentModeTop;
    _label.preferredMaxLayoutWidth = [WKInterfaceDevice currentDevice].screenBounds.size.width - 12;
    _label.numberOfLines = 0;
    _label.position = CGPointMake(0, -4);
    [self addChild:_label];
        
    _iPhoneIcon = [SKSpriteNode spriteNodeWithImageNamed:@"iPhoneIcon"];
    _iPhoneIcon.xScale = _iPhoneIcon.yScale = 1.0 / [WKInterfaceDevice currentDevice].screenScale;
    _iPhoneIcon.position = CGPointMake(0, _iPhoneIcon.size.height / 2 + 4);
    [self addChild: _iPhoneIcon];
    
    _audioClient = [[GBGizmoAudioClient alloc] init];
    
    GB_init(&_gb, GB_MODEL_CGB_E);
    GB_set_user_data(&_gb, (__bridge void *)(self));
    GB_set_rgb_encode_callback(&_gb, rgbEncode);
    GB_set_boot_rom_load_callback(&_gb, loadBootROM);
    GB_set_vblank_callback(&_gb, (GB_vblank_callback_t) vblank);
    GB_set_border_mode(&_gb, GB_BORDER_ALWAYS);
    GB_set_pixels_output(&_gb, _pixels);
    GB_set_sample_rate(&_gb, _audioClient.rate);
    GB_apu_set_sample_callback(&_gb, sampleCallback);
        
    GB_gameboy_t *gb = &_gb;
    [self addDefaultObserver:^(id newValue) {
        GB_set_color_correction_mode(gb, (GB_color_correction_mode_t)[newValue integerValue]);
    } forKey:@"GBColorCorrection"];
    [self addDefaultObserver:^(id newValue) {
        GB_set_light_temperature(gb, [newValue doubleValue]);
    } forKey:@"GBLightTemperature"];
    [self addDefaultObserver:^(id newValue) {
        GB_set_palette(gb, [self currentPalette]);
    } forKey:@"GBCurrentTheme"];
    GB_set_rgb_encode_callback(gb, rgbEncode);
    [self addDefaultObserver:^(id newValue) {
        GB_set_highpass_filter_mode(gb, (GB_highpass_mode_t)[newValue integerValue]);
    } forKey:@"GBHighpassFilter"];
    [self addDefaultObserver:^(id newValue) {
        GB_set_interference_volume(gb, [newValue doubleValue]);
    } forKey:@"GBInterferenceVolume"];
    [self addDefaultObserver:^(id newValue) {
        _audioClient.volume = [newValue doubleValue];
    } forKey:@"GBWatchVolume"];
    [self addDefaultObserver:^(id newValue) {
        if (_running) {
            [self stop];
            GB_set_rewind_length(gb, [newValue unsignedIntValue]);
            [self start];
        }
        else {
            GB_set_rewind_length(gb, [newValue unsignedIntValue]);
        }
    } forKey:@"GBRewindLength"];
    [self addDefaultObserver:^(id newValue) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if ([newValue isEqual:@"off"]) {
                GB_set_sample_rate(gb, 0);
                [_audioClient stop];
            }
            else {
                GB_set_sample_rate(gb, _audioClient.rate);
                if (self.isRunning) {
                    [_audioClient start];
                }
            }
            [[AVAudioSession sharedInstance] setCategory:[newValue isEqual:@"on"]? AVAudioSessionCategoryPlayback :  AVAudioSessionCategorySoloAmbient
                                                    mode:AVAudioSessionModeMeasurement // Reduces latency on BT
                                      routeSharingPolicy:AVAudioSessionRouteSharingPolicyDefault
                                                 options:AVAudioSessionCategoryOptionAllowBluetoothA2DP
                                                   error:nil];
        });
    } forKey:@"GBAudioMode"];
    GB_set_log_callback(&_gb, (GB_log_callback_t)nop_log_callback);
    
    [[NSNotificationCenter defaultCenter] addObserverForName:@"GBROMChanged"
                                                      object:nil
                                                       queue:nil
                                                  usingBlock:^(NSNotification *note) {
        [self stop];
        [self loadROM];
        _label.hidden = _romLoaded;
        _iPhoneIcon.hidden = _romLoaded;
        _screen.hidden = !_romLoaded;
        if (!_romLoaded) {
            [self setLabelString:@"Open SameBoy on your iPhone to transfer a ROM to your Apple Watch."];
        }
        [self start];
    }];
    
    [[GBPhoneManager sharedManager] validateUUID:^(bool valid) {
        if (valid) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [[NSNotificationCenter defaultCenter] postNotificationName:@"GBROMChanged" object:nil];
            });
        }
        else {
            [self setLabelString:@"Open SameBoy on your iPhone to transfer a ROM to your Apple Watch."];
        }
    }];
}
    
- (void)vblank
{
    static _Atomic bool busy = false;
    void *buffer = _activeBuffer? _pixels : _pixels + 256 * 224;
    _activeBuffer ^= true;
    GB_set_pixels_output(&_gb, _activeBuffer? _pixels : _pixels + 256 * 224);
    
    dispatch_async(dispatch_get_main_queue(), ^{
        if (busy) return;
        busy = true;
        [_texture modifyPixelDataWithBlock:^(void *pixelData, size_t lengthInBytes) {
            memcpy(pixelData, buffer, lengthInBytes);
        }];
        busy = false;
    });
}

- (void)sampleCallback:(GB_sample_t *)sample
{
    [_audioClient pushSample:sample];
}

- (void)holdButton:(GB_key_t)button duration:(double)seconds
{
    GB_set_key_state(&_gb, button, true);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, seconds * (NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        GB_set_key_state(&_gb, button, false);
    });
}

- (void)showHoldAt:(CGPoint)position
{
    [_holdSprite removeFromParent];
    _holdSprite = [SKSpriteNode spriteNodeWithImageNamed:@"Hold"];
    
    _holdSprite.xScale = _holdSprite.yScale = 1.0 / [WKInterfaceDevice currentDevice].screenScale;

    position.y = self.size.height - position.y;
    position.x -= self.size.width / 2;
    position.y -= self.size.height / 2;
    _holdSprite.position = position;
    [self addChild:_holdSprite];
}

- (void)hideHold
{
    [_holdSprite removeFromParent];
    _holdSprite = nil;
}

- (void)setInput:(GB_key_mask_t)mask
{
    GB_set_key_mask(&_gb, mask);
}

- (void)setSpeedMultiplayer:(double)factor
{
    GB_set_clock_multiplier(&_gb, factor);
}

- (void)rewindFrames:(unsigned)count
{
    if (!_romLoaded) return;
    for (unsigned i = 0; i <= count; i++) {
        if (!GB_rewind_pop(&_gb)) {
            return;
        }
    }
    GB_run_frame(&_gb);
}

- (bool)isRunning
{
    return _running;
}

@end

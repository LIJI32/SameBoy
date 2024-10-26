#import "GBGameScene.h"
#import "GBPhoneManager.h"
#import <mach/mach.h>
#import <Core/gb.h>

@implementation GBGameScene
{
    SKSpriteNode *_screen;
    SKMutableTexture *_texture;
    GB_gameboy_t _gb;
    uint32_t _pixels[256 * 224 * 2];
    SKSpriteNode *_holdSprite;
    volatile bool _running, _stopping;
    bool _activeBuffer;
    bool _romLoaded;
}

static void nop_log_callback()
{
    
}

static uint32_t rgbEncode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    return (r << 0) | (g << 8) | (b << 16) | 0xFF000000;
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

- (void) loadBootROM: (GB_boot_rom_t)type
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
    GB_load_boot_rom(&_gb, [[self bootROMPathForName:names[type]] UTF8String]);
}

- (NSString *)bootROMPathForName:(NSString *)name
{
    return [[NSBundle mainBundle] pathForResource:name ofType:@"bin"];
}

- (void)start
{
    if (_running) return;
    if (!_romLoaded) return;
    _running = true;
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
    
    GB_save_state(&_gb, GBPhoneManager.sharedManager.saveStatePath.UTF8String);
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
    GB_load_state(&_gb, phoneManager.saveStatePath.UTF8String);
}

- (void)sceneDidLoad
{
    // Setup your scene here
    _texture = [[SKMutableTexture alloc] initWithSize:CGSizeMake(256, 224)];
    _texture.filteringMode = SKTextureFilteringNearest;
    _screen = [SKSpriteNode spriteNodeWithTexture:_texture size:CGSizeMake(256, 224)];
    _screen.yScale = -1;
    [self addChild:_screen];
    GB_init(&_gb, GB_MODEL_CGB_E);
    GB_set_user_data(&_gb, (__bridge void *)(self));
    GB_set_rgb_encode_callback(&_gb, rgbEncode);
    GB_set_boot_rom_load_callback(&_gb, loadBootROM);
    GB_set_vblank_callback(&_gb, (GB_vblank_callback_t) vblank);
    GB_set_border_mode(&_gb, GB_BORDER_ALWAYS);
    GB_set_pixels_output(&_gb, _pixels);
    GB_set_color_correction_mode(&_gb, GB_COLOR_CORRECTION_MODERN_BALANCED);
    GB_set_rewind_length(&_gb, 60);
    GB_set_log_callback(&_gb, (GB_log_callback_t)nop_log_callback);
    
    [[GBPhoneManager sharedManager] validateUUID:^(bool valid) {
        if (valid) {
            [self loadROM];
            [self start];
        }
    }];
    [[NSNotificationCenter defaultCenter] addObserverForName:@"GBROMChanged"
                                                      object:nil
                                                       queue:nil
                                                  usingBlock:^(NSNotification * _Nonnull note) {
        [self stop];
        [self loadROM];
        [self start];
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
    position.x *= 2;
    position.y *= 2;

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

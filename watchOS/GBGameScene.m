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
    SKSpriteNode *_hint;
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
    UIImage *_holdImage, *_hintImage;
    NSTimer *_idleTimer;
    bool _invalidating;
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

- (NSAttributedString *)attributedStringForDefaultAction:(UIImageSymbolConfiguration *)configuration
{
    NSString *defaultAction = [[NSUserDefaults standardUserDefaults] stringForKey:@"GBWatchDefaultAction"];
    NSMutableAttributedString *actionString = [[NSMutableAttributedString alloc] init];
    
    if ([defaultAction hasPrefix:@"A"]) {
        NSTextAttachment *attachment = [[NSTextAttachment alloc] init];
        attachment.image = [[UIImage systemImageNamed:@"a.circle" withConfiguration:configuration] imageWithTintColor:[UIColor whiteColor] renderingMode:UIImageRenderingModeAlwaysTemplate];
        [actionString appendAttributedString:[NSAttributedString attributedStringWithAttachment:attachment]];
    }
    
    if (defaultAction.length == 3) {
        NSTextAttachment *attachment = [[NSTextAttachment alloc] init];
        attachment.image = [[UIImage systemImageNamed:@"plus" withConfiguration:configuration] imageWithTintColor:[UIColor whiteColor] renderingMode:UIImageRenderingModeAlwaysTemplate];
        [actionString appendAttributedString:[NSAttributedString attributedStringWithAttachment:attachment]];
    }
    
    if ([defaultAction hasSuffix:@"B"]) {
        NSTextAttachment *attachment = [[NSTextAttachment alloc] init];
        attachment.image = [[UIImage systemImageNamed:@"b.circle" withConfiguration:configuration] imageWithTintColor:[UIColor whiteColor] renderingMode:UIImageRenderingModeAlwaysTemplate];
        [actionString appendAttributedString:[NSAttributedString attributedStringWithAttachment:attachment]];
    }
    return actionString;
}

- (UIImage *)hintImage
{
    if (_hintImage) return _hintImage;
    double factor = [WKInterfaceDevice currentDevice].screenScale;
    UIGraphicsBeginImageContextWithOptions([WKInterfaceDevice currentDevice].screenBounds.size, false, factor);
    
    [[UIColor colorWithWhite:0 alpha:0.4] setFill];
    CGContextFillRect(UIGraphicsGetCurrentContext(), [WKInterfaceDevice currentDevice].screenBounds);
    
    NSShadow *shadow = [[NSShadow alloc] init];
    [shadow setShadowBlurRadius:3];
    [shadow setShadowColor:[UIColor colorWithWhite:0 alpha:0.75]];
    UIImageSymbolConfiguration *configuration = [UIImageSymbolConfiguration configurationWithPointSize:16.0 weight:UIImageSymbolWeightMedium];

    CGContextSaveGState(UIGraphicsGetCurrentContext());
    CGContextSetShadowWithColor(UIGraphicsGetCurrentContext(), CGSizeMake(0, 0), 2, [UIColor colorWithWhite:0 alpha:0.875].CGColor);
    NSMutableParagraphStyle *style = [NSParagraphStyle defaultParagraphStyle].mutableCopy;
    style.alignment = NSTextAlignmentCenter;
    style.paragraphSpacing = 8;
    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBWatchSwipe"]) {
        NSDictionary *attributes = @{
            NSParagraphStyleAttributeName: style,
            NSForegroundColorAttributeName: [UIColor whiteColor],
            NSFontAttributeName: [UIFont systemFontOfSize:16 weight:UIFontWeightMedium],
        };
        NSMutableAttributedString *string = [[NSMutableAttributedString alloc] initWithString:@"Swipe for D-pad movement.\nTap for {action}.\nLong press for other buttons."
                                                                                   attributes:attributes];
        
        [string replaceCharactersInRange:[string.string rangeOfString:@"{action}"] withAttributedString:[self attributedStringForDefaultAction:configuration]];
        
        [string drawInRect:CGRectMake(12, 32, [WKInterfaceDevice currentDevice].screenBounds.size.width - 24, [WKInterfaceDevice currentDevice].screenBounds.size.height - 32)];
    }
    else {
        CGSize screenSize = [WKInterfaceDevice currentDevice].screenBounds.size;
        unsigned width = screenSize.width * BUTTON_WIDTH;
        unsigned height = screenSize.height * BUTTON_WIDTH;
        
        UIBezierPath *path = [UIBezierPath bezierPathWithRect:CGRectMake(width, height,
                                                                         screenSize.width - width * 2,
                                                                         screenSize.height - height * 2)];
        
        UIBezierPath *line = [[UIBezierPath alloc] init];
        [line moveToPoint:CGPointZero];
        [line addLineToPoint:CGPointMake(width, height)];
        [path appendPath: line];
        
        line = [[UIBezierPath alloc] init];
        [line moveToPoint:CGPointMake(0, screenSize.height)];
        [line addLineToPoint:CGPointMake(width, screenSize.height - height)];
        [path appendPath: line];
        
        line = [[UIBezierPath alloc] init];
        [line moveToPoint:CGPointMake(screenSize.width, screenSize.height)];
        [line addLineToPoint:CGPointMake(screenSize.width - width, screenSize.height - height)];
        [path appendPath: line];
        
        line = [[UIBezierPath alloc] init];
        [line moveToPoint:CGPointMake(screenSize.width, 0)];
        [line addLineToPoint:CGPointMake(screenSize.width - width, height)];
        [path appendPath: line];
        
        [[UIColor whiteColor] setStroke];
        path.lineWidth = 2;
        [path stroke];
        
        NSDictionary *attributes = @{
            NSParagraphStyleAttributeName: style,
            NSForegroundColorAttributeName: [UIColor whiteColor],
            NSFontAttributeName: [UIFont systemFontOfSize:13 weight:UIFontWeightMedium],
        };
        
        NSMutableAttributedString *string = [[NSMutableAttributedString alloc] initWithString:@"\u200b{action}\nLong press for other buttons"
                                                                                   attributes:attributes];
        
        [string replaceCharactersInRange:[string.string rangeOfString:@"{action}"] withAttributedString:[self attributedStringForDefaultAction:configuration]];
        CGSize stringSize = [string boundingRectWithSize:CGSizeMake(screenSize.width - width * 2, screenSize.height - height * 2)
                                                 options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingUsesFontLeading
                                                 context:nil].size;
        [string drawInRect:CGRectMake(width, (screenSize.height - stringSize.height) / 2, screenSize.width - width * 2, stringSize.height)];
        
#define DrawCentered(name, x, y) {\
            NSTextAttachment *attachment = [[NSTextAttachment alloc] init]; \
            attachment.image = [[UIImage systemImageNamed:name withConfiguration:configuration] imageWithTintColor:[UIColor whiteColor] renderingMode:UIImageRenderingModeAlwaysTemplate];\
            NSAttributedString *string = [NSAttributedString attributedStringWithAttachment:attachment];\
            CGSize size = [string size];\
            [string drawInRect:(CGRect){{(x) - size.width / 2, (y) - size.height / 2}, size}];\
        }
        
        DrawCentered(@"arrowtriangle.up.circle", screenSize.width / 2, height / 2);
        DrawCentered(@"arrowtriangle.left.circle", width / 2, screenSize.height / 2);
        DrawCentered(@"arrowtriangle.right.circle", screenSize.width - width / 2, screenSize.height / 2);
        DrawCentered(@"arrowtriangle.down.circle", screenSize.width / 2, screenSize.height - height / 2);
#undef DrawCentered
    }
    CGContextRestoreGState(UIGraphicsGetCurrentContext());

    
    _hintImage = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return _hintImage;
}

- (UIImage *)holdImage
{
    if (_holdImage) return _holdImage;
    static const double size = 160;
    static const double ringSize = 72;
    static const double lineWidth = 4;
    static const double lineRadians = lineWidth * 1.5 / ringSize * M_PI;
    static const double labelDistance = 6;
    double factor = [WKInterfaceDevice currentDevice].screenScale;
    
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(size, size), false, factor);
    
    NSArray *labels = nil;
    NSString *defaultAction = [[NSUserDefaults standardUserDefaults] stringForKey:@"GBWatchDefaultAction"];
    if ([defaultAction isEqualToString:@"A+B"]) {
        labels = @[@"A", @"B", @"Select", @"Start"];
    }
    else {
        labels = @[[defaultAction isEqualToString:@"B"]? @"A" : @"B", @"Select", @"Start"];
    }
    UIFont *font = [UIFont systemFontOfSize:13 weight:UIFontWeightSemibold];
    UIFontDescriptor *descriptor = [font.fontDescriptor fontDescriptorWithDesign:UIFontDescriptorSystemDesignRounded];
    font = [UIFont fontWithDescriptor:descriptor size:font.pointSize] ?: font;
    
    double shift = labels.count == 3? 0.25 : 0;
    
    NSShadow *shadow = [[NSShadow alloc] init];
    [shadow setShadowBlurRadius:3];
    [shadow setShadowColor:[UIColor colorWithWhite:0 alpha:0.75]];

    for (unsigned i = labels.count; i--;) {
        UIBezierPath *path = [UIBezierPath bezierPathWithArcCenter:CGPointMake(size / 2, size / 2)
                                                            radius:ringSize / 2 - lineWidth / 2
                                                        startAngle:M_PI * 2 / labels.count * (i + shift) + lineRadians / 2
                                                          endAngle:M_PI * 2 / labels.count * (i + 1 + shift) - lineRadians / 2
                                                         clockwise:true];
        path.lineCapStyle = kCGLineCapRound;
        path.lineWidth = lineWidth + 1;
        [[UIColor blackColor] setStroke];
        CGContextSaveGState(UIGraphicsGetCurrentContext());
        CGContextSetShadowWithColor(UIGraphicsGetCurrentContext(), CGSizeMake(0, 0), 3, [UIColor colorWithWhite:0 alpha:0.75].CGColor);
        [path stroke];
        CGContextRestoreGState(UIGraphicsGetCurrentContext());
        
        path.lineWidth = lineWidth;
        [[UIColor whiteColor] setStroke];
        [path stroke];
        
        NSAttributedString *string = [[NSAttributedString alloc] initWithString:labels[i]
                                                                     attributes:@{
            NSFontAttributeName: font,
            NSForegroundColorAttributeName: [UIColor blackColor],
            NSShadowAttributeName: shadow,
        }];
        
        CGSize labelSize = [string size];
        double labelAngle = M_PI * 2 / labels.count * (i + 0.5 + shift);
        double labelX = round(size / 2 + (ringSize / 2 + labelDistance) * cos(labelAngle));
        double labelY = round(size / 2 + (ringSize / 2 + labelDistance) * sin(labelAngle));
#define sign(x) ((x) > 0? 1: -1)
        if (fabs(labelX - size / 2) < labelDistance) {
            labelY += labelSize.height / 2;
        }
        else {
            labelX += labelSize.width / 2 * sign(labelX - size / 2);
            labelY += labelSize.height / 2 * sign(labelY - size / 2);
        }
#undef sign
        
        CGRect labelRect = CGRectMake(labelX - labelSize.width / 2, labelY - labelSize.height / 2, labelSize.width, labelSize.height);
        
        for (unsigned i = 4; i--;) {
            CGRect strokeRect = labelRect;
            strokeRect.origin.x += cos(M_PI * 2 / 4 * i) / factor;
            strokeRect.origin.y += sin(M_PI * 2 / 4 * i) / factor;
            [string drawInRect:strokeRect];
        }
        
        string = [[NSAttributedString alloc] initWithString:labels[i]
                                                 attributes:@{
            NSFontAttributeName: font,
            NSForegroundColorAttributeName: [UIColor whiteColor],
        }];
        
        [string drawInRect:labelRect];
    }
    
    _holdImage = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return _holdImage;
}

- (void)displayHint
{
    if (!_running) return;
    [_hint removeFromParent];
    if (![[NSUserDefaults standardUserDefaults] boolForKey:@"GBWatchHints"]) return;
    _hint = [SKSpriteNode spriteNodeWithTexture:[SKTexture textureWithImage:self.hintImage]];
    _hint.alpha = 0;
    [self addChild:_hint];
    [_hint runAction:[SKAction sequence:@[
        [SKAction fadeInWithDuration:0.5],
        [SKAction waitForDuration:3],
        [SKAction fadeOutWithDuration:0.5],
    ]]];
}

- (void)start
{
    if (_running) return;
    if (!_romLoaded) return;
    _running = true;
    if (![[[NSUserDefaults standardUserDefaults] stringForKey:@"GBAudioMode"] isEqual:@"off"]) {
        [_audioClient start];
    }
    [_idleTimer invalidate];
    _idleTimer = [NSTimer scheduledTimerWithTimeInterval:8 repeats:false block:^(NSTimer *timer) {
        [self displayHint];
    }];
    [NSThread detachNewThreadWithBlock:^{
        while (_running) {
            GB_run(&_gb);
        }
        _stopping = false;
    }];
    [[GBPhoneManager sharedManager] validateUUID:^(NSString *error) {
        if (error) {
            _invalidating = true;
            [self stop];
            _invalidating = false;
            [self setLabelString:error];
            _romLoaded = false;
            _label.hidden = false;
            _iPhoneIcon.hidden = false;
            _screen.hidden = true;
            [_hint removeFromParent];
            _hint = nil;
        }
    }];
}

- (void)stop
{
    if (!_running) return;
    _stopping = true;
    _running = false;
    while (_stopping);
    [_audioClient stop];
    if (_invalidating) return;
    
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
    GB_rewind_reset(&_gb);
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
    _label.attributedText = [[NSAttributedString alloc] initWithString:string
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
        _holdImage = nil;
        _hintImage = nil;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self hideHold];
        });
    } forKey:@"GBWatchDefaultAction"];
    [self addDefaultObserver:^(id newValue) {
        _hintImage = nil;
    } forKey:@"GBWatchSwipe"];
    
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
                                                    mode:AVAudioSessionModeDefault
                                      routeSharingPolicy:AVAudioSessionRouteSharingPolicyDefault
                                                 options:0
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
            [_hint removeFromParent];
            _hint = nil;
        }
        [self start];
    }];
    
    [[GBPhoneManager sharedManager] validateUUID:^(NSString *error) {
        if (!error) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [[NSNotificationCenter defaultCenter] postNotificationName:@"GBROMChanged" object:nil];
            });
        }
        else {
            [self setLabelString:error];
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

- (NSTimer *)holdButton:(GB_key_t)button duration:(double)seconds
{
    [_idleTimer invalidate];
    _idleTimer = [NSTimer scheduledTimerWithTimeInterval:8 repeats:false block:^(NSTimer *timer) {
        [self displayHint];
    }];
    [_hint removeFromParent];
    _hint = nil;
    GB_set_key_state(&_gb, button, true);
    return [NSTimer scheduledTimerWithTimeInterval:seconds
                                    repeats:false
                                      block:^(NSTimer * _Nonnull timer) {
        GB_set_key_state(&_gb, button, false);
    }];
}

- (void)showHoldAt:(CGPoint)position
{
    [_holdSprite removeFromParent];
    if (!_running) return;
    _holdSprite = [SKSpriteNode spriteNodeWithTexture:[SKTexture textureWithImage:self.holdImage]];
    
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
    [_idleTimer invalidate];
    if (!mask) {
        _idleTimer = [NSTimer scheduledTimerWithTimeInterval:8 repeats:false block:^(NSTimer *timer) {
            [self displayHint];
        }];
    }
    [_hint removeFromParent];
    _hint = nil;
    GB_set_key_mask(&_gb, mask);
}

- (void)setSpeedMultiplayer:(double)factor
{
    [_idleTimer invalidate];
    _idleTimer = [NSTimer scheduledTimerWithTimeInterval:8 repeats:false block:^(NSTimer *timer) {
        [self displayHint];
    }];
    [_hint removeFromParent];
    _hint = nil;
    GB_set_clock_multiplier(&_gb, factor);
}

- (void)rewindFrames:(unsigned)count
{
    if (!_romLoaded) return;
    [_idleTimer invalidate];
    _idleTimer = [NSTimer scheduledTimerWithTimeInterval:8 repeats:false block:^(NSTimer *timer) {
        [self displayHint];
    }];
    [_hint removeFromParent];
    _hint = nil;
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

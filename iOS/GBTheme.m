#import "GBTheme.h"
#import "GBVerticalLayout.h"
#import "GBHorizontalLayout.h"
#import "GBBackgroundView.h"

@interface GBLazyObject : NSProxy
@end

@implementation GBLazyObject
{
    id _target;
    id (^_constructor)(void);
}


- (instancetype)initWithConstructor:(id (^)(void))constructor
{
    _constructor = constructor;
    return self;
}

- (NSMethodSignature *)methodSignatureForSelector:(SEL)sel
{
    if (GB_likely(!_target)) {
        _target = _constructor();
        _constructor = nil;
    }
    return [_target methodSignatureForSelector:sel];
}

- (void)forwardInvocation:(NSInvocation *)invocation
{
    if (GB_likely(!_target)) {
        _target = _constructor();
        _constructor = nil;
    }
    invocation.target = _target;
    [invocation invoke];
}

- (instancetype)self
{
    if (GB_likely(!_target)) {
        _target = _constructor();
        _constructor = nil;
    }
    return _target;
}

@end

#define MakeColor(r, g, b) [UIColor colorWithRed:(r) / 255.0 green:(g) / 255.0 blue:(b) / 255.0 alpha:1.0]

__attribute__((objc_direct_members))
@implementation GBTheme
{
    NSDictionary<NSString *, UIImage *> *_imageOverrides;
    UIImage *_horizontalPreview;
    UIImage *_verticalPreview;
}


// Assumes the image has a purple hue
+ (UIImage *)_recolorImage:(UIImage *)image withColor:(UIColor *)color
{
    double scale = image.scale;
    
    image = [UIImage imageWithCGImage:image.CGImage scale:1.0 orientation:UIImageOrientationUp];

    CIImage *ciImage = [CIImage imageWithCGImage:image.CGImage];
    CIFilter *filter = [CIFilter filterWithName:@"CIColorMatrix"];
    double r, g, b;
    [color getRed:&r green:&g blue:&b alpha:NULL];
    
    [filter setDefaults];
    [filter setValue:ciImage forKey:@"inputImage"];
    
    [filter setValue:[CIVector vectorWithX:r * 1.34  Y:1 - r Z:0 W:0] forKey:@"inputRVector"];
    [filter setValue:[CIVector vectorWithX:g * 1.34  Y:1 - g Z:0 W:0] forKey:@"inputGVector"];
    [filter setValue:[CIVector vectorWithX:b * 1.34  Y:1 - b Z:0 W:0] forKey:@"inputBVector"];
    
    [filter setValue:[CIVector vectorWithX:0 Y:0 Z:0 W:1] forKey:@"inputAVector"];
    

    CIContext *context = [CIContext context];
    CGImageRef cgImage = [context createCGImage:filter.outputImage fromRect:filter.outputImage.extent];
    image = [UIImage imageWithCGImage:cgImage scale:scale orientation:0];
    CGImageRelease(cgImage);

    return image;
}

+ (UIImage *)recolorImage:(UIImage *)image withColor:(UIColor *)color
{
    return (id)[[GBLazyObject alloc] initWithConstructor:^id{
        return [self _recolorImage:image withColor:color];
    }];
}

- (instancetype)initDefaultTheme
{
    self = [super init];
    
    _brandColor = [UIColor colorWithRed:0 / 255.0 green:70 / 255.0 blue:141 / 255.0 alpha:1.0];
    _backgroundGradientTop = [UIColor colorWithRed:192 / 255.0 green:195 / 255.0 blue:199 / 255.0 alpha:1.0];
    _backgroundGradientBottom = [UIColor colorWithRed:174 / 255.0 green:176 / 255.0 blue:180 / 255.0 alpha:1.0];
    
    _bezelsGradientTop = [UIColor colorWithWhite:53 / 255.0 alpha:1.0];
    _bezelsGradientBottom = [UIColor colorWithWhite:45 / 255.0 alpha:1.0];
    
    _name = @"SameBoy";
    
    return self;
}

- (void)setupBackgroundWithColor:(uint32_t)color
{
    uint8_t r = color >> 16;
    uint8_t g = color >> 8;
    uint8_t b = color;
    
    _backgroundGradientTop = MakeColor(r, g, b);
    _backgroundGradientBottom = [UIColor colorWithRed:pow(r / 255.0, 1.125) green:pow(g / 255.0, 1.125) blue:pow(b / 255.0, 1.125) alpha:1.0];
}

- (void)setupButtonsWithColor:(UIColor *)color
{
    _imageOverrides = @{
        @"button": [GBTheme recolorImage:[UIImage imageNamed:@"button"] withColor:color],
        @"buttonPressed": [GBTheme recolorImage:[UIImage imageNamed:@"buttonPressed"] withColor:color],
        
        @"dpad": [GBTheme recolorImage:[UIImage imageNamed:@"dpad-tint"] withColor:color],
        @"swipepad": [GBTheme recolorImage:[UIImage imageNamed:@"swipepad-tint"] withColor:color],
        
        @"button2": [GBTheme recolorImage:[UIImage imageNamed:@"button2-tint"] withColor:color],
        @"button2Pressed": [GBTheme recolorImage:[UIImage imageNamed:@"button2Pressed-tint"] withColor:color],
    };
}

- (instancetype)initDarkTheme
{
    self = [super init];
    
    [self setupBackgroundWithColor:0x181c23];

    _brandColor = [UIColor colorWithRed:0 / 255.0 green:70 / 255.0 blue:141 / 255.0 alpha:1.0];
    _bezelsGradientTop = [UIColor colorWithWhite:53 / 255.0 alpha:1.0];
    _bezelsGradientBottom = [UIColor colorWithWhite:45 / 255.0 alpha:1.0];
    
    [self setupButtonsWithColor:MakeColor(0x08, 0x0c, 0x12)];

    _name = @"SameBoy Dark";
    
    return self;
}

#ifdef APPSTORE
- (instancetype)initDMGTheme
{
    self = [super init];
    
    _brandColor = MakeColor(0x40, 0x41, 0x88);
    [self setupBackgroundWithColor:0xc2bebb];
    
    _bezelsGradientTop = [UIColor colorWithWhite:0x86 / 255.0 alpha:1.0];
    _bezelsGradientBottom = [UIColor colorWithWhite:0x91 / 255.0 alpha:1.0];
    
    UIColor *buttonColor = [UIColor colorWithWhite:0.5 alpha:1.0];
    
    _imageOverrides = @{
        @"button2": [GBTheme recolorImage:[UIImage imageNamed:@"button2-tint"] withColor:buttonColor],
        @"button2Pressed": [GBTheme recolorImage:[UIImage imageNamed:@"button2Pressed-tint"] withColor:buttonColor],
    };
    
    _texture = [UIImage imageNamed:@"TexturedPlastic"];
    
    _name = @"Original Off-White";
    
    return self;
}

- (instancetype)initPlayItLoudBlackTheme
{
    self = [super init];
    
    _brandColor = MakeColor(0xe3, 0x49, 0x79);
    [self setupBackgroundWithColor:0x181c23];
    
    _bezelsGradientTop = [UIColor colorWithWhite:0x68 / 255.0 alpha:1.0];
    _bezelsGradientBottom = [UIColor colorWithWhite:0x70 / 255.0 alpha:1.0];
    
    _texture = [UIImage imageNamed:@"TexturedPlastic"];
    [self setupButtonsWithColor:MakeColor(0x45, 0x45, 0x55)];
    
    _name = @"Loud Deep Black";
    
    return self;
}

- (instancetype)initPlayItLoudThemeWithColor:(uint32_t)color andName:(NSString *)name;
{
    self = [super init];
    
    _brandColor = [UIColor colorWithWhite:7 / 32.0 alpha:7 / 8.0];
    [self setupBackgroundWithColor:color];
    
    _bezelsGradientTop = [UIColor colorWithWhite:11 / 32.0 alpha:1.0];
    _bezelsGradientBottom = [UIColor colorWithWhite:12 / 32.0 alpha:1.0];
    
    static __weak typeof(_imageOverrides) cache = nil;
    if (cache) {
        _imageOverrides = cache;
    }
    else {
        [self setupButtonsWithColor:[UIColor colorWithWhite:3 / 32.0 alpha:1.0]];
        cache = _imageOverrides;
    }
    
    _texture = [UIImage imageNamed:@"TexturedPlastic"];
    
    _name = name;
    
    return self;
}

- (instancetype)initCGBThemeWithColor:(uint32_t)color andName:(NSString *)name
{
    self = [super init];

    _brandColor = [UIColor colorWithWhite:0 alpha:0.4];
    [self setupBackgroundWithColor:color];
    
    _bezelsGradientTop = [UIColor colorWithWhite:45 / 255.0 alpha:1.0];
    _bezelsGradientBottom = [UIColor colorWithWhite:53 / 255.0 alpha:1.0];
    
    static __weak typeof(_imageOverrides) cache = nil;
    if (cache) {
        _imageOverrides = cache;
    }
    else {
        [self setupButtonsWithColor:[UIColor colorWithWhite:1 / 32.0 alpha:1.0]];
        cache = _imageOverrides;
    }
    
    _texture = [UIImage imageNamed:@"TexturedPlastic"];
    _embossLabels = true;
    
    _name = name;
    
    return self;
}

- (instancetype)initAGBThemeWithColor:(uint32_t)color andName:(NSString *)name;
{
    self = [super init];
    
    _brandColor = [UIColor colorWithWhite:0 alpha:0.4];
    [self setupBackgroundWithColor:color];
    
    _bezelsGradientTop = [UIColor colorWithWhite:45 / 255.0 alpha:1.0];
    _bezelsGradientBottom = [UIColor colorWithWhite:53 / 255.0 alpha:1.0];
    
    static __weak typeof(_imageOverrides) cache = nil;
    if (cache) {
        _imageOverrides = cache;
    }
    else {
        [self setupButtonsWithColor:[UIColor colorWithWhite:31 / 32.0 alpha:1.0]];
        cache = _imageOverrides;
    }
    
    _texture = [UIImage imageNamed:@"TexturedPlastic"];
    _embossLabels = true;
    
    _name = name;
    
    return self;
}

- (instancetype)initGameAndWatchTheme
{
    self = [super init];
    
    _brandColor = UIColor.blackColor;
    _backgroundGradientTop = MakeColor(0xe2, 0xd1, 0xb9);
    _backgroundGradientBottom = MakeColor(0xca, 0xbd, 0x91);

    _bezelsGradientTop = MakeColor(0x80, 0x05, 0x12);
    _bezelsGradientBottom = MakeColor(0xa4, 0x1b, 0x2c);
    
    _imageOverrides = @{
        @"button": [GBTheme recolorImage:[UIImage imageNamed:@"button"] withColor:UIColor.redColor],
        @"buttonPressed": [GBTheme recolorImage:[UIImage imageNamed:@"buttonPressed"] withColor:UIColor.redColor],
                
        @"button2": [GBTheme recolorImage:[UIImage imageNamed:@"button2-tint"] withColor:UIColor.whiteColor],
        @"button2Pressed": [GBTheme recolorImage:[UIImage imageNamed:@"button2Pressed-tint"] withColor:UIColor.whiteColor],
    };
    
    _texture = [UIImage imageNamed:@"BrushedMetal"];
    
    _name = @"1980";
    
    return self;
}

- (instancetype)initSFCTheme
{
    self = [super init];
    
    _brandColor = [UIColor colorWithWhite:0 alpha:0.4];
    [self setupBackgroundWithColor:0xd2d2d2];
    
    _bezelsGradientTop = MakeColor(0x99, 0x99, 0x99);
    _bezelsGradientBottom = MakeColor(0xaa, 0xaa, 0xaa);
    
    
    _imageOverrides = @{
        @"buttonA": [GBTheme recolorImage:[UIImage imageNamed:@"button"] withColor:UIColor.redColor],
        @"buttonAPressed": [GBTheme recolorImage:[UIImage imageNamed:@"buttonPressed"] withColor:UIColor.redColor],
        @"buttonB": [GBTheme recolorImage:[UIImage imageNamed:@"button"] withColor:MakeColor(0xff, 0xc0, 0x28)],
        @"buttonBPressed": [GBTheme recolorImage:[UIImage imageNamed:@"buttonPressed"] withColor:MakeColor(0xff, 0xc0, 0x28)],

        
        @"button2": [GBTheme recolorImage:[UIImage imageNamed:@"button2-tint"] withColor:[UIColor colorWithWhite:0.2 alpha:1.0]],
        @"button2Pressed": [GBTheme recolorImage:[UIImage imageNamed:@"button2Pressed-tint"] withColor:[UIColor colorWithWhite:0.2 alpha:1.0]],
        @"dpad": [GBTheme recolorImage:[UIImage imageNamed:@"dpad-tint"] withColor:[UIColor colorWithWhite:0.2 alpha:1.0]],
        @"swipepad": [GBTheme recolorImage:[UIImage imageNamed:@"swipepad-tint"] withColor:[UIColor colorWithWhite:0.2 alpha:1.0]],
    };
    
    _texture = [UIImage imageNamed:@"TexturedPlastic"];
    
    _name = @"Ninties";
    
    return self;
}

- (instancetype)initSNESTheme
{
    self = [super init];
    
    _brandColor = [UIColor colorWithWhite:0 alpha:0.4];
    [self setupBackgroundWithColor:0xcecdd2];
    
    _bezelsGradientTop = MakeColor(0x98, 0x96, 0xa1);
    _bezelsGradientBottom = MakeColor(0xab, 0xa9, 0xac);
    
    
    _imageOverrides = @{
        @"button": [GBTheme recolorImage:[UIImage imageNamed:@"button"] withColor:MakeColor(0x5b, 0x28, 0xd1)],
        @"buttonPressed": [GBTheme recolorImage:[UIImage imageNamed:@"buttonPressed"] withColor:MakeColor(0x5b, 0x28, 0xd1)],
        
        @"button2": [GBTheme recolorImage:[UIImage imageNamed:@"button2-tint"] withColor:[UIColor colorWithWhite:0.2 alpha:1.0]],
        @"button2Pressed": [GBTheme recolorImage:[UIImage imageNamed:@"button2Pressed-tint"] withColor:[UIColor colorWithWhite:0.2 alpha:1.0]],
        @"dpad": [GBTheme recolorImage:[UIImage imageNamed:@"dpad-tint"] withColor:[UIColor colorWithWhite:0.2 alpha:1.0]],
        @"swipepad": [GBTheme recolorImage:[UIImage imageNamed:@"swipepad-tint"] withColor:[UIColor colorWithWhite:0.2 alpha:1.0]],
    };
    
    _texture = [UIImage imageNamed:@"TexturedPlastic"];
    
    _name = @"American Ninties";
    
    return self;
}

- (instancetype)initGCNTheme
{
    self = [super init];
    
    _brandColor = [UIColor colorWithWhite:1.0 alpha:0.78125];
    _embossLabels = true;
    [self setupBackgroundWithColor:0x373f88];
    
    _bezelsGradientTop = [UIColor colorWithWhite:45 / 255.0 alpha:1.0];
    _bezelsGradientBottom = [UIColor colorWithWhite:53 / 255.0 alpha:1.0];

    
    _imageOverrides = @{
        @"buttonA": [GBTheme recolorImage:[UIImage imageNamed:@"button"] withColor:MakeColor(-0x08, 0xff, 0xe8)],
        @"buttonAPressed": [GBTheme recolorImage:[UIImage imageNamed:@"buttonPressed"] withColor:MakeColor(-0x08, 0xff, 0xe8)],
        @"buttonB": [GBTheme recolorImage:[UIImage imageNamed:@"button"] withColor:MakeColor(0xff, -0xf, -0xa)],
        @"buttonBPressed": [GBTheme recolorImage:[UIImage imageNamed:@"buttonPressed"] withColor:MakeColor(0xff, -0xf, -0xa)],
        
        
        @"button2": [GBTheme recolorImage:[UIImage imageNamed:@"button2-tint"] withColor:[UIColor colorWithWhite:31 / 32.0 alpha:1.0]],
        @"button2Pressed": [GBTheme recolorImage:[UIImage imageNamed:@"button2Pressed-tint"] withColor:[UIColor colorWithWhite:31 / 32.0 alpha:1.0]],
        @"dpad": [GBTheme recolorImage:[UIImage imageNamed:@"dpad-tint"] withColor:[UIColor colorWithWhite:31 / 32.0 alpha:1.0]],
        @"swipepad": [GBTheme recolorImage:[UIImage imageNamed:@"swipepad-tint"] withColor:[UIColor colorWithWhite:31 / 32.0 alpha:1.0]],
    };
    
    _texture = [UIImage imageNamed:@"TexturedPlastic"];
    
    _name = @"Cubical";
    
    return self;
}


- (instancetype)initMegaDuckTheme
{
    self = [super init];
    
    UIColor *grey = MakeColor(0x39, 0x42, 0x49);
    UIColor *teal = MakeColor(0x02, 0xe8, 0xf6);

    _brandColor = grey;
    [self setupBackgroundWithColor:0xd3d3d3];
    
    _bezelsGradientTop = MakeColor(0x47, 0x4d, 0x59);
    _bezelsGradientBottom = MakeColor(0x3a, 0x3e, 0x49);
    
    
    _imageOverrides = @{
        @"button": [GBTheme recolorImage:[UIImage imageNamed:@"button"] withColor:teal],
        @"buttonPressed": [GBTheme recolorImage:[UIImage imageNamed:@"buttonPressed"] withColor:teal],
        
        @"button2": [GBTheme recolorImage:[UIImage imageNamed:@"button2-tint"] withColor:grey],
        @"button2Pressed": [GBTheme recolorImage:[UIImage imageNamed:@"button2Pressed-tint"] withColor:grey],
        @"dpad": [GBTheme recolorImage:[UIImage imageNamed:@"dpad-tint"] withColor:teal],
        @"swipepad": [GBTheme recolorImage:[UIImage imageNamed:@"swipepad-tint"] withColor:teal],
    };
    
    _texture = [UIImage imageNamed:@"TexturedPlastic"];
    
    _name = @"Light Duck";
    
    return self;
}

- (instancetype)initDarkMegaDuckTheme
{
    self = [super init];
    
    UIColor *grey = MakeColor(0xad, 0xad, 0xa3);
    UIColor *teal = MakeColor(-0x18, 0x48, 0xff);
    
    _brandColor = grey;
    [self setupBackgroundWithColor:0x1e1e1e];
    
    _bezelsGradientTop = MakeColor(0x47, 0x4d, 0x59);
    _bezelsGradientBottom = MakeColor(0x3a, 0x3e, 0x49);
    
    
    _imageOverrides = @{
        @"button": [GBTheme recolorImage:[UIImage imageNamed:@"button"] withColor:teal],
        @"buttonPressed": [GBTheme recolorImage:[UIImage imageNamed:@"buttonPressed"] withColor:teal],
        
        @"button2": [GBTheme recolorImage:[UIImage imageNamed:@"button2-tint"] withColor:grey],
        @"button2Pressed": [GBTheme recolorImage:[UIImage imageNamed:@"button2Pressed-tint"] withColor:grey],
        @"dpad": [GBTheme recolorImage:[UIImage imageNamed:@"dpad-tint"] withColor:teal],
        @"swipepad": [GBTheme recolorImage:[UIImage imageNamed:@"swipepad-tint"] withColor:teal],
    };
    
    _texture = [UIImage imageNamed:@"TexturedPlastic"];
    
    _name = @"Dark Duck";
    
    return self;
}

#endif

- (bool)isDark
{
    double r, g, b;
    [_backgroundGradientTop getRed:&r green:&g blue:&b alpha:NULL];
    if (r > 0.25) return false;
    if (g > 0.25) return false;
    if (b > 0.25) return false;
    return true;
}

- (UIImage *)imageNamed:(NSString *)name
{
    UIImage *ret = _imageOverrides[name].self ?: [UIImage imageNamed:name];
    if (!ret) {
        if  ([name isEqual:@"buttonA"] || [name isEqual:@"buttonB"]) {
            return [self imageNamed:@"button"];
        }
        if  ([name isEqual:@"buttonAPressed"] || [name isEqual:@"buttonBPressed"]) {
            return [self imageNamed:@"buttonPressed"];
        }
    }
    return ret;
}

- (UIImage *)horizontalPreview
{
    if (_horizontalPreview) return _horizontalPreview;
    _renderingPreview = true;
    GBLayout *layout = [[GBHorizontalLayout alloc] initWithTheme:self cutoutOnRight:false];
    _renderingPreview = false;
    GBBackgroundView *view = [[GBBackgroundView alloc] initWithLayout:layout];
    [view enterPreviewMode:false];
    view.usesSwipePad = [[NSUserDefaults standardUserDefaults] boolForKey:@"GBSwipePad"];
    view.layout = layout;
    view.bounds = CGRectMake(0, 0,
                             MAX(UIScreen.mainScreen.bounds.size.width, UIScreen.mainScreen.bounds.size.height),
                             MIN(UIScreen.mainScreen.bounds.size.width, UIScreen.mainScreen.bounds.size.height));
    UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithSize:(CGSize){view.bounds.size.width / 8,
        view.bounds.size.height / 8,
    }];
    return _horizontalPreview = [renderer imageWithActions:^(UIGraphicsImageRendererContext *rendererContext) {
        CGContextScaleCTM(UIGraphicsGetCurrentContext(), 1 / 8.0, 1 / 8.0);
        [view.layer renderInContext:rendererContext.CGContext];
    }];
}

- (UIImage *)verticalPreview
{
    if (_verticalPreview) return _verticalPreview;
    _renderingPreview = true;
    GBLayout *layout = [[GBVerticalLayout alloc] initWithTheme:self];
    _renderingPreview = false;
    GBBackgroundView *view = [[GBBackgroundView alloc] initWithLayout:layout];
    [view enterPreviewMode:false];
    view.usesSwipePad = [[NSUserDefaults standardUserDefaults] boolForKey:@"GBSwipePad"];
    view.layout = layout;
    view.bounds = CGRectMake(0, 0,
                             MIN(UIScreen.mainScreen.bounds.size.width, UIScreen.mainScreen.bounds.size.height),
                             MAX(UIScreen.mainScreen.bounds.size.width, UIScreen.mainScreen.bounds.size.height));
    UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithSize:(CGSize){view.bounds.size.width / 8,
                                                                                               view.bounds.size.height / 8,
    }];
    return _verticalPreview = [renderer imageWithActions:^(UIGraphicsImageRendererContext *rendererContext) {
        CGContextScaleCTM(UIGraphicsGetCurrentContext(), 1 / 8.0, 1 / 8.0);
        [view.layer renderInContext:rendererContext.CGContext];
    }];
}

@end

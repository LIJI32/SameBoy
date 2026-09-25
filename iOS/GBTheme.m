#import "GBTheme.h"
#import "GBVerticalLayout.h"
#import "GBHorizontalLayout.h"
#import "GBBackgroundView.h"
#import "GBLazyObject.h"

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


- (UIColor *)buttonColor
{
    double r, g, b;
    [_brandColor getRed:&r green:&g blue:&b alpha:NULL];
    if (r == 1.0 && g == 1.0 && b == 1.0) {
        return _backgroundGradientTop;
    }
    return [UIColor colorWithRed:r green:g blue:b alpha:1.0];
}

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

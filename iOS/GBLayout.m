#define GBLayoutInternal
#import "GBLayout.h"

static double StatusBarHeight(void)
{
    static double ret = 0;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        @autoreleasepool {
            UIWindow *window = [[UIWindow alloc] init];
            [window makeKeyAndVisible];
            UIEdgeInsets insets = window.safeAreaInsets;
            ret = MAX(MAX(insets.left, insets.right), MAX(insets.top, insets.bottom)) ?: 20;
            [window setHidden:true];
        }
    });
    return ret;
}

static bool HasHomeBar(void)
{
    static bool ret = false;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        ret = [UIApplication sharedApplication].windows[0].safeAreaInsets.bottom;
    });
    return ret;
}

@implementation GBLayout
{
    bool _isRenderingMask;
}

- (instancetype)initWithTheme:(GBTheme *)theme
{
    self = [super init];
    if (!self) return nil;
    
    _theme = theme;
    _factor = [UIScreen mainScreen].scale;
    _resolution = [UIScreen mainScreen].bounds.size;
    _resolution.width *= _factor;
    _resolution.height *= _factor;
    if (_resolution.width > _resolution.height) {
        _resolution = (CGSize){_resolution.height, _resolution.width};
    }
    
    _minY = StatusBarHeight() * _factor;
    _cutout = _minY <= 24 * _factor? 0 : _minY;
    
    if (HasHomeBar()) {
        _homeBar =  21 * _factor;
    }
    
    // The Plus series will scale things lossily anyway, so no need to bother with integer scale things
    // This also "catches" zoomed display modes
    _hasFractionalPixels = _factor != [UIScreen mainScreen].nativeScale;
    return self;
}

- (CGRect)viewRectForOrientation:(UIInterfaceOrientation)orientation
{
    return CGRectMake(0, 0, self.background.size.width / self.factor, self.background.size.height / self.factor);
}

- (void)drawBackground
{
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGColorRef top = _theme.backgroundGradientTop.CGColor;
    CGColorRef bottom = _theme.backgroundGradientBottom.CGColor;
    CGColorRef colors[] = {top, bottom};
    CFArrayRef colorsArray = CFArrayCreate(NULL, (const void **)colors, 2, &kCFTypeArrayCallBacks);
    
    CGColorSpaceRef colorspace = CGColorSpaceCreateDeviceRGB();
    CGGradientRef gradient = CGGradientCreateWithColors(colorspace, colorsArray, NULL);
    CGContextDrawLinearGradient(context,
                                gradient,
                                (CGPoint){0, 0},
                                (CGPoint){0, self.size.height},
                                0);

    CFRelease(gradient);
    CFRelease(colorsArray);
    CFRelease(colorspace);
    
    UIImage *texture = _theme.texture;
    if (texture) {
        unsigned screenWidth = self.size.width;
        unsigned screenHeight = self.size.height;
        unsigned textureWidth = texture.size.width * texture.scale;
        unsigned textureHeight = texture.size.height * texture.scale;
        for (unsigned y = 0; y < screenHeight; y += textureHeight) {
            for (unsigned x = 0; x < screenWidth; x += textureWidth) {
                [texture drawInRect:CGRectMake(x, y, textureWidth, textureHeight)];
            }
        }
    }
}

- (void)drawScreenBezels
{
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGColorRef top = _theme.bezelsGradientTop.CGColor;
    CGColorRef bottom = _theme.bezelsGradientBottom.CGColor;
    CGColorRef colors[] = {top, bottom};
    CFArrayRef colorsArray = CFArrayCreate(NULL, (const void **)colors, 2, &kCFTypeArrayCallBacks);
    
    double borderWidth = self.screenRect.size.width / 40;
    CGRect bezelRect = self.screenRect;
    bezelRect.origin.x -= borderWidth;
    bezelRect.origin.y -= borderWidth;
    bezelRect.size.width += borderWidth * 2;
    bezelRect.size.height += borderWidth * 2;
    UIBezierPath *path = [UIBezierPath bezierPathWithRoundedRect:bezelRect cornerRadius:borderWidth];
    CGContextSaveGState(context);
    CGContextSetShadowWithColor(context, (CGSize){0, _factor}, _factor, [UIColor colorWithWhite:1 alpha:0.25].CGColor);
    [_theme.backgroundGradientBottom setFill];
    [path fill];
    [path addClip];
    
    CGColorSpaceRef colorspace = CGColorSpaceCreateDeviceRGB();
    CGGradientRef gradient = CGGradientCreateWithColors(colorspace, colorsArray, NULL);
    CGContextDrawLinearGradient(context,
                                gradient,
                                bezelRect.origin,
                                (CGPoint){bezelRect.origin.x, bezelRect.origin.y + bezelRect.size.height},
                                0);
    
    CGContextSetShadowWithColor(context, (CGSize){0, _factor}, _factor, [UIColor colorWithWhite:0 alpha:0.25].CGColor);
    
    path.usesEvenOddFillRule = true;
    [path appendPath:[UIBezierPath bezierPathWithRect:(CGRect){{0, 0}, self.size}]];
    [path fill];
    
    
    CGContextRestoreGState(context);
    
    CGContextSaveGState(context);
    CGContextSetShadowWithColor(context, (CGSize){0, 0}, borderWidth / 4, [UIColor colorWithWhite:0 alpha:0.125].CGColor);
    
    [[UIColor blackColor] setFill];
    UIRectFill(self.screenRect);
    CGContextRestoreGState(context);
    
    CFRelease(gradient);
    CFRelease(colorsArray);
    CFRelease(colorspace);
}

- (void)drawLogoInVerticalRange:(NSRange)range
{
    UIFont *font = [UIFont fontWithName:@"AvenirNext-BoldItalic" size:range.length * 4 / 3];
    
    CGRect rect = CGRectMake(0,
                             range.location - range.length / 3,
                             self.size.width, range.length * 2);
    NSMutableParagraphStyle *style = [NSParagraphStyle defaultParagraphStyle].mutableCopy;
    style.alignment = NSTextAlignmentCenter;
    [@"SAMEBOY" drawInRect:rect
            withAttributes:@{
                NSFontAttributeName: font,
                NSForegroundColorAttributeName:_isRenderingMask? [UIColor whiteColor] : _theme.brandColor,
                NSParagraphStyleAttributeName: style,
            }];
}

- (void)drawThemedLabelsWithBlock:(void (^)(void))block
{
    // Start with a normal normal pass
    block();

    if (!_theme.embossLabels){
        // No emboss, done
        return;
    }
    
    CGContextRef mainContext = UIGraphicsGetCurrentContext();

    
    // Create a mask
    UIGraphicsBeginImageContextWithOptions((CGSize){
        CGBitmapContextGetWidth(mainContext),
        CGBitmapContextGetHeight(mainContext)
    }, true, 1);
    
    if (_theme.renderingPreview) {
        CGContextScaleCTM(UIGraphicsGetCurrentContext(), 1 / 8.0, 1 / 8.0);
    }
    
    _isRenderingMask = true;
    block();
    _isRenderingMask = false;

    UIImage *mask = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    
    // Create an inverted mask
    CGRect rect = (CGRect){{0, 0}, mask.size};
    UIGraphicsBeginImageContextWithOptions(rect.size, false, mask.scale);
    [_theme.backgroundGradientBottom setFill];
    UIRectFill(rect);
    CGContextClipToMask(UIGraphicsGetCurrentContext(), rect, mask.CGImage);
    CGContextClearRect(UIGraphicsGetCurrentContext(), rect);
    UIImage *invertedMask = UIGraphicsGetImageFromCurrentImageContext();
    
    CGContextSaveGState(mainContext);
    if (_theme.renderingPreview) {
        CGContextScaleCTM(UIGraphicsGetCurrentContext(), 8.0, 8.0);
    }
    CGContextTranslateCTM(mainContext, 0, rect.size.height);
    CGContextScaleCTM(mainContext, 1.0, -1.0);
    CGContextClipToMask(mainContext, rect, mask.CGImage);
        UIGraphicsEndImageContext();
    
    // Draws with an inner shadow
    CGContextSetShadowWithColor(mainContext, (CGSize){0, _factor}, _factor, [UIColor colorWithWhite:0 alpha:0.25].CGColor);
    [invertedMask drawAtPoint:(CGPoint){0, 0}];

    CGContextRestoreGState(mainContext);
    
    
    CGContextSaveGState(mainContext);
    if (_theme.renderingPreview) {
        CGContextScaleCTM(UIGraphicsGetCurrentContext(), 8.0, 8.0);
    }
    CGContextClipToMask(mainContext, rect, invertedMask.CGImage);

    CGContextSetShadowWithColor(mainContext, (CGSize){0, _factor}, _factor, [UIColor colorWithWhite:1.0 alpha:0.25].CGColor);
    
    block();
    CGContextRestoreGState(mainContext);

}

- (void)drawRotatedLabel:(NSString *)label withFont:(UIFont *)font origin:(CGPoint)origin distance:(double)distance
{
    CGContextRef context = UIGraphicsGetCurrentContext();
    
    CGContextSaveGState(context);
    CGContextConcatCTM(context, CGAffineTransformMakeTranslation(origin.x, origin.y));
    CGContextConcatCTM(context, CGAffineTransformMakeRotation(-M_PI / 6));

    NSMutableParagraphStyle *style = [NSParagraphStyle defaultParagraphStyle].mutableCopy;
    style.alignment = NSTextAlignmentCenter;

    [label drawInRect:CGRectMake(-256, distance, 512, 256)
            withAttributes:@{
                NSFontAttributeName: font,
                NSForegroundColorAttributeName:_isRenderingMask? [UIColor whiteColor] : _theme.brandColor,
                NSParagraphStyleAttributeName: style,
            }];
    CGContextRestoreGState(context);
}

- (void)drawLabels
{

    UIFont *labelFont = [UIFont fontWithName:@"AvenirNext-Bold" size:24 * _factor];
    UIFont *smallLabelFont = [UIFont fontWithName:@"AvenirNext-DemiBold" size:20 * _factor];
    
    [self drawRotatedLabel:@"A" withFont:labelFont origin:self.aLocation distance:40 * self.factor];
    [self drawRotatedLabel:@"B" withFont:labelFont origin:self.bLocation distance:40 * self.factor];
    [self drawRotatedLabel:@"SELECT" withFont:smallLabelFont origin:self.selectLocation distance:24 * self.factor];
    [self drawRotatedLabel:@"START" withFont:smallLabelFont origin:self.startLocation distance:24 * self.factor];
}

- (CGSize)buttonDeltaForMaxHorizontalDistance:(double)maxDistance
{
    CGSize buttonsDelta = {90 * self.factor, 45 * self.factor};
    if (buttonsDelta.width <= maxDistance) {
        return buttonsDelta;
    }
    return (CGSize){maxDistance, floor(sqrt(100 * 100 * self.factor * self.factor - maxDistance * maxDistance))};
}

- (CGSize)size
{
    return _resolution;
}
@end

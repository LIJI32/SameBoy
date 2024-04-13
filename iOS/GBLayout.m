#define GBLayoutInternal
#import "GBLayout.h"

double StatusBarHeight(void)
{
    double ret = 0;
    @autoreleasepool {
        UIWindow *window = [[UIWindow alloc] init];
        [window makeKeyAndVisible];
        UIEdgeInsets insets = window.safeAreaInsets;
        ret = MAX(MAX(insets.left, insets.right), MAX(insets.top, insets.bottom));
        [window setHidden:true];
    }
    return ret;
}

@implementation GBLayout
- (instancetype)init
{
    self = [super init];
    if (!self) return nil;
    _factor = [UIScreen mainScreen].scale;
    _resolution = [UIScreen mainScreen].bounds.size;
    _resolution.width *= _factor;
    _resolution.height *= _factor;
    if (_resolution.width > _resolution.height) {
        _resolution = (CGSize){_resolution.height, _resolution.width};
    }
    
    _minY = StatusBarHeight() * _factor;
    _cutout = _minY <= 24 * _factor? 0 : _minY;
    
    if ([UIApplication sharedApplication].windows[0].safeAreaInsets.bottom) {
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

- (UIColor *)brandColor
{
    static dispatch_once_t onceToken;
    static UIColor *ret = nil;
    dispatch_once(&onceToken, ^{
        ret = [UIColor colorWithRed:0 / 255.0 green:70 / 255.0 blue:141 / 255.0 alpha:1.0];
    });
    return ret;
}

- (void)drawBackground
{
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGColorRef top = [UIColor colorWithRed:192 / 255.0 green:195 / 255.0 blue:199 / 255.0 alpha:1.0].CGColor;
    CGColorRef bottom = [UIColor colorWithRed:174 / 255.0 green:176 / 255.0 blue:180 / 255.0 alpha:1.0].CGColor;
    CGColorRef colors[] = {top, bottom};
    CFArrayRef colorsArray = CFArrayCreate(NULL, (const void **)colors, 2, &kCFTypeArrayCallBacks);
    
    CGColorSpaceRef colorspace = CGColorSpaceCreateDeviceRGB();
    CGGradientRef gradient = CGGradientCreateWithColors(colorspace, colorsArray, NULL);
    CGContextDrawLinearGradient(context,
                                gradient,
                                (CGPoint){0, 0},
                                (CGPoint){0, CGBitmapContextGetHeight(context)},
                                0);

    CFRelease(gradient);
    CFRelease(colorsArray);
    CFRelease(colorspace);
}

- (void)drawScreenBezels
{
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGColorRef top = [UIColor colorWithWhite:53 / 255.0 alpha:1.0].CGColor;
    CGColorRef bottom = [UIColor colorWithWhite:45 / 255.0 alpha:1.0].CGColor;
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
    CGContextSetShadowWithColor(context, (CGSize){0,}, borderWidth / 2, [UIColor colorWithWhite:0 alpha:1.0].CGColor);
    [[UIColor colorWithWhite:0 alpha:0.25] setFill];
    [path fill];
    [path addClip];
    
    CGColorSpaceRef colorspace = CGColorSpaceCreateDeviceRGB();
    CGGradientRef gradient = CGGradientCreateWithColors(colorspace, colorsArray, NULL);
    CGContextDrawLinearGradient(context,
                                gradient,
                                bezelRect.origin,
                                (CGPoint){bezelRect.origin.x, bezelRect.origin.y + bezelRect.size.height},
                                0);
    
    CGContextRestoreGState(context);
    
    CGContextSaveGState(context);
    CGContextSetShadowWithColor(context, (CGSize){0,}, borderWidth / 2, [UIColor colorWithWhite:0 alpha:0.25].CGColor);
    
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
                             CGBitmapContextGetWidth(UIGraphicsGetCurrentContext()), range.length * 2);
    NSMutableParagraphStyle *style = [NSParagraphStyle defaultParagraphStyle].mutableCopy;
    style.alignment = NSTextAlignmentCenter;
    [@"SAMEBOY" drawInRect:rect
            withAttributes:@{
                NSFontAttributeName: font,
                NSForegroundColorAttributeName:self.brandColor,
                NSParagraphStyleAttributeName: style,
            }];
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
                NSForegroundColorAttributeName:self.brandColor,
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
@end

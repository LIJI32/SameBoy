#define GBLayoutInternal
#import "GBLayout.h"
#import "GBViewController.h" // For hinge status

static UIScreen *ActiveScreen(void)
{
    if (@available(iOS 13.0, *)) {
        return [(UIWindowScene *)[UIApplication sharedApplication].connectedScenes.anyObject screen];
    }
    return [UIScreen mainScreen];
}

static double StatusBarHeight(void)
{
    if (@available(iOS 15.0, *)) {
        UIEdgeInsets insets = [(UIWindowScene *)[UIApplication sharedApplication].connectedScenes.anyObject keyWindow].safeAreaInsets;
        double ret = MAX(MAX(insets.left, insets.right), insets.top) ?: 20;
        if (!ret && [UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad) {
            ret = 32; // iPadOS is buggy af
        }
        return ret;
    }
    static double ret = 0;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        @autoreleasepool {
            UIWindow *window = [[UIWindow alloc] init];
            [window makeKeyAndVisible];
            UIEdgeInsets insets = window.safeAreaInsets;
            ret = MAX(MAX(insets.left, insets.right), MAX(insets.top, insets.bottom)) ?: 20;
            [window setHidden:true];
            if (!ret && [UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad) {
                ret = 32; // iPadOS is buggy af
            }
        }
    });
    return ret;
}

static bool IsFolded(double *sidebarSize)
{
    if (@available(iOS 27.1, *)) {
        if ([(GBViewController *)[[UIApplication sharedApplication] delegate] hingeStatus] != UIHingeStatusClosed) {
            return false;
        }
        UIWindow *window = [(UIWindowScene *)[UIApplication sharedApplication].connectedScenes.anyObject keyWindow];
        UIEdgeInsets insets = window.safeAreaInsets;
        if (insets.left != insets.right) {
            *sidebarSize = MAX(insets.right, insets.left);
            return true;
        }
    }
    return false;
}

static bool IsUnfolded(void)
{
    if (@available(iOS 27.1, *)) {
        return [(GBViewController *)[[UIApplication sharedApplication] delegate] hingeStatus] >= UIHingeStatusPartiallyOpen;
    }
    return false;
}

static bool HasHomeBar(void)
{
    if (@available(iOS 15.0, *)) {
        UIEdgeInsets insets = [(UIWindowScene *)[UIApplication sharedApplication].connectedScenes.anyObject keyWindow].safeAreaInsets;
        return insets.bottom;
    }
    static bool ret = false;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        ret = [UIApplication sharedApplication].windows[0].safeAreaInsets.bottom;
    });
    return ret;
}

static UIEdgeInsets CurrentInsets(UIInterfaceOrientation orientation, bool *forceAsymmetric)
{
    double sidebarSize;
    bool isFolded = IsFolded(&sidebarSize);
    if (!isFolded) {
        double statusBarHeight = IsUnfolded()? 20 : StatusBarHeight();
        double homeBarHeight = HasHomeBar()? 20 : 0;
        bool hasCutout = (statusBarHeight > 24 && [UIDevice currentDevice].userInterfaceIdiom != UIUserInterfaceIdiomPad);
        
        switch (orientation) {
            case UIInterfaceOrientationUnknown:
            case UIInterfaceOrientationPortrait:
            case UIInterfaceOrientationPortraitUpsideDown:
                return UIEdgeInsetsMake(statusBarHeight, 0, homeBarHeight, 0);
            case UIInterfaceOrientationLandscapeLeft:
                return UIEdgeInsetsMake(0, 0, homeBarHeight, hasCutout? statusBarHeight : 0);
            case UIInterfaceOrientationLandscapeRight:
                return UIEdgeInsetsMake(0, hasCutout? statusBarHeight : 0, homeBarHeight, 0);
        }
    }
    
    *forceAsymmetric = true;
    switch (orientation) {
        case UIInterfaceOrientationUnknown:
        case UIInterfaceOrientationPortrait:
        case UIInterfaceOrientationPortraitUpsideDown:
            return UIEdgeInsetsMake(0, 0, 0, sidebarSize);
        case UIInterfaceOrientationLandscapeLeft:
            return UIEdgeInsetsMake(20, 0, sidebarSize, 0);
        case UIInterfaceOrientationLandscapeRight:
            return UIEdgeInsetsMake(sidebarSize, 0, 20, 0);
    }
}

@implementation GBLayout
{
    bool _isRenderingMask;
    bool _forceAsymmetric;
}

- (instancetype)initWithTheme:(GBTheme *)theme
{
    self = [super init];
    if (!self) return nil;
    
    _theme = theme;
    _factor = ActiveScreen().scale;
    _resolution = ActiveScreen().bounds.size;
    _resolution.width *= _factor;
    _resolution.height *= _factor;
    if (_resolution.width > _resolution.height) {
        _resolution = (CGSize){_resolution.height, _resolution.width};
    }
    
    _insets = CurrentInsets(self.orientation, &_forceAsymmetric);
    _insets.left *= _factor;
    _insets.right *= _factor;
    _insets.bottom *= _factor;
    _insets.top *= _factor;

    // The Plus series will scale things lossily anyway, so no need to bother with integer scale things
    // This also "catches" zoomed display modes
    _hasFractionalPixels = _factor != ActiveScreen().nativeScale;
    return self;
}

- (CGRect)viewRectForOrientation:(UIInterfaceOrientation)orientation
{
    if (_theme.renderingPreview) {
        return CGRectMake(0, 0, self.background.size.width / self.factor * 8, self.background.size.height / self.factor * 8);
    }
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
}


static inline UIBezierPath *RoundedRectWithRadii(CGRect rect, CGFloat topLeft, CGFloat topRight, CGFloat bottomRight, CGFloat bottomLeft)
{
    if (@available(iOS 16.0, *)) {
        UIRectCorner corners[] = {UIRectCornerTopLeft, UIRectCornerTopRight, UIRectCornerBottomRight, UIRectCornerBottomLeft};
        CGFloat radii[] = {topLeft, topRight, bottomRight, bottomLeft};
        
        CGPathRef result = CGPathCreateWithRect(rect, NULL);
        
        for (unsigned i = 0; i < 4; i++) {
            UIBezierPath *cornerPath = [UIBezierPath bezierPathWithRoundedRect:rect
                                                             byRoundingCorners:corners[i]
                                                                   cornerRadii:CGSizeMake(radii[i], radii[i])];
            CGPathRef intersected = CGPathCreateCopyByIntersectingPath(result, cornerPath.CGPath, false);
            CGPathRelease(result);
            result = intersected;
            
        }
        
        UIBezierPath *path = [UIBezierPath bezierPathWithCGPath:result];
        CGPathRelease(result);
        return path;
    }
    return nil;
}

- (void)drawScreenBezelsFolded:(bool)folded
{
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGColorRef top = _theme.bezelsGradientTop.CGColor;
    CGColorRef bottom = _theme.bezelsGradientBottom.CGColor;
    CGColorRef colors[] = {top, bottom};
    CFArrayRef colorsArray = CFArrayCreate(NULL, (const void **)colors, 2, &kCFTypeArrayCallBacks);
    
    double borderWidth = MIN(self.screenRect.size.width / 40, 16 * _factor);
    CGRect bezelRect = self.screenRect;
    UIBezierPath *path;
    
    if (folded) {
        double radius = 0;
        if (@available(iOS 26.0, *)) {
            UIWindow *window = [UIApplication sharedApplication].keyWindow;
            UICornerConfiguration *configuration = window.cornerConfiguration;
            window.cornerConfiguration = [UICornerConfiguration configurationWithUniformRadius:[UICornerRadius containerConcentricRadius]];
            radius = [window effectiveRadiusForCorner:UIRectCornerTopRight] * _factor;
            window.cornerConfiguration = configuration;
        }
        
        bezelRect.origin.x = -borderWidth;
        bezelRect.origin.y -= borderWidth;
        bezelRect.size.width = borderWidth + _resolution.width - bezelRect.origin.y;
        bezelRect.size.height += borderWidth * 2;
        
        path = RoundedRectWithRadii(bezelRect, borderWidth, radius - bezelRect.origin.y, borderWidth, borderWidth);
    } else {
        bezelRect.origin.x -= borderWidth;
        bezelRect.origin.y -= borderWidth;
        bezelRect.size.width += borderWidth * 2;
        bezelRect.size.height += borderWidth * 2;
        
        if (bezelRect.origin.y + bezelRect.size.height >= self.size.height - _insets.bottom) {
            bezelRect.origin.y = -32;
            bezelRect.size.height = self.size.height + 32;
        }
        
        path = [UIBezierPath bezierPathWithRoundedRect:bezelRect cornerRadius:borderWidth];
    }
    
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

- (void)drawScreenBezels
{
    [self drawScreenBezelsFolded:false];
}

- (void)drawFoldedScreenBezels
{
    [self drawScreenBezelsFolded:true];
}

- (void)drawLogoInVerticalRange:(NSRange)range controlPadding:(double)padding
{
    UIFont *font = [UIFont fontWithName:@"AvenirNext-BoldItalic" size:range.length * 4 / 3];
    
    CGRect rect = CGRectMake(0,
                             range.location - range.length / 3,
                             self.size.width, range.length * 2);
    if (self.size.width > self.size.height) {
        rect.origin.x += (_insets.left - _insets.right) / 2;
    }
    NSMutableParagraphStyle *style = [NSParagraphStyle defaultParagraphStyle].mutableCopy;
    style.alignment = NSTextAlignmentCenter;
    [@"SAMEBOY" drawInRect:rect
            withAttributes:@{
                NSFontAttributeName: font,
                NSForegroundColorAttributeName:_isRenderingMask? [UIColor whiteColor] : _theme.brandColor,
                NSParagraphStyleAttributeName: style,
            }];
    
    _logoRect = (CGRect){
        {(self.size.width - _screenRect.size.width) / 2 + padding, rect.origin.y},
        {_screenRect.size.width - padding * 2, rect.size.height}
    };
}

- (void)drawThemedLabelsWithBlock:(void (^)(void))block
{
    // Start with a normal normal pass
    block();
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

- (UIInterfaceOrientation)orientation
{
    return UIInterfaceOrientationUnknown;
}

- (bool)asymmetric
{
    return _forceAsymmetric || _insets.left != _insets.right;
}

- (bool)isDark
{
    return _theme.isDark;
}
@end

#import "GBColorWell.h"

@implementation GBColorWell
{
    UIView *_proxyView;
}

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    self.opaque = false;
    [self addTarget:self action:@selector(setNeedsDisplay) forControlEvents:UIControlEventValueChanged];
    return self;
}

- (void)drawRect:(CGRect)rect
{
    if (self.enabled) {
        [[UIColor systemFillColor] set];
    }
    else {
        [[UIColor quaternarySystemFillColor] set];
    }
    rect = self.bounds;
    [[UIBezierPath bezierPathWithRoundedRect:rect cornerRadius:6] fill];
    
    rect.size.width -= 6;
    rect.size.height -= 6;
    rect.origin.x += 3;
    rect.origin.y += 3;
    
    if (!self.enabled) {
        rect.size.width -= 2;
        rect.size.height -= 2;
        rect.origin.x += 1;
        rect.origin.y += 1;
    }
    
    CGContextRef context = UIGraphicsGetCurrentContext();
    
    CGContextSaveGState(context);
    CGContextSetShadowWithColor(context, (CGSize){0, 0}, 2, [UIColor colorWithWhite:0 alpha:self.enabled? 0.25 : 0.125].CGColor);
    [self.selectedColor set];
    [[UIBezierPath bezierPathWithRoundedRect:rect cornerRadius:3] fill];
    CGContextRestoreGState(context);
}

- (void)addSubview:(UIView *)view
{
    if (!_proxyView) {
        _proxyView = [[UIView alloc] initWithFrame:self.bounds];
        [super addSubview:_proxyView];
        _proxyView.layer.mask = [CALayer layer];
    }
    [_proxyView addSubview:view];
}

- (void)setSelectedColor:(UIColor *)selectedColor
{
    [super setSelectedColor:selectedColor];
    [self setNeedsDisplay];
}

- (UIColor *)selectedColor
{
    UIColor *orig = [super selectedColor];
    CGFloat red, green, blue;
    [orig getRed:&red green:&green blue:&blue alpha:nil];
    red   = MIN(MAX(red,   0), 1);
    green = MIN(MAX(green, 0), 1);
    blue  = MIN(MAX(blue,  0), 1);
    return [UIColor colorWithRed:red green:green blue:blue alpha:1.0];
}

@end

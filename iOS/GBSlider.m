#import "GBSlider.h"

static inline void temperature_tint(double temperature, double *r, double *g, double *b)
{
    if (temperature >= 0) {
        *r = 1;
        *g = pow(1 - temperature, 0.375);
        if (temperature >= 0.75) {
            *b = 0;
        }
        else {
            *b = sqrt(0.75 - temperature) / sqrt(0.75);
        }
    }
    else {
        *b = 1;
        double squared = pow(temperature, 2);
        *g = 0.125 * squared + 0.3 * temperature + 1.0;
        *r = 0.21875 * squared + 0.5 * temperature + 1.0;
    }
}

@implementation GBSlider

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    [self addTarget:self action:@selector(valueChanged) forControlEvents:UIControlEventValueChanged];
    return self;
}

- (void)updateTint
{
    if (_style == GBSliderStyleTemperature) {
        double r, g, b;
        temperature_tint(self.value, &r, &g, &b);
        self.thumbTintColor = [UIColor colorWithRed:r green:g blue:b alpha:1.0];
    }
    if (_style == GBSliderStyleHue) {
        double hue = (self.value - self.minimumValue) / (self.maximumValue - self.minimumValue);
        double r = 0, g = 0, b =0 ;
        double t = fmod(hue * 6, 1);
        switch ((int)(hue  * 6) % 6) {
            case 0:
                r = 1;
                g = t;
                break;
            case 1:
                r = 1 - t;
                g = 1;
                break;
            case 2:
                g = 1;
                b = t;
                break;
            case 3:
                g = 1 - t;
                b = 1;
                break;
            case 4:
                b = 1;
                r = t;
                break;
            case 5:
                b = 1 - t;
                r = 1;
                break;
        }
        self.thumbTintColor = [UIColor colorWithRed:r green:g blue:b alpha:1.0];
    }
}

- (void)setValue:(float)value
{
    [super setValue:value];
    [self updateTint];
}

- (void)valueChanged
{
    if (fabsf(self.value) < 0.05 && self.value != 0 && _style != GBSliderStyleHue) {
        self.value = 0;
    }
    [self updateTint];
}

-(UIImage *)maximumTrackImageForState:(UIControlState)state
{
    return [[UIImage alloc] init];
}


-(UIImage *)minimumTrackImageForState:(UIControlState)state
{
    return [[UIImage alloc] init];
}

- (void)drawRect:(CGRect)rect
{
    CGSize size = self.bounds.size;
    UIBezierPath *path = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(2, round(size.height / 2 - 1.5), size.width - 4, 3) cornerRadius:4];
    if (_style != GBSliderStyleHue) {
        [path appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake(round(size.width / 2 - 1.5), 12, 3, size.height - 24) cornerRadius:4]];
    }
    if (_style == GBSliderStyleTicks) {
        [path appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake(0, 12, 3, size.height - 24) cornerRadius:4]];
        [path appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake(size.width - 3, 12, 3, size.height - 24) cornerRadius:4]];
    }
    if (_style == GBSliderStyleHue) {
        UIBezierPath *path = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(3, round(size.height / 2 - 1.5) + 1, size.width - 6, 1) cornerRadius:4];
        CGContextRef context = UIGraphicsGetCurrentContext();
        CGContextSaveGState(context);
        [path addClip];
        
        CGColorSpaceRef colorspace = CGColorSpaceCreateDeviceRGB();
        CGColorRef colors[] = {
            UIColor.redColor.CGColor,
            UIColor.yellowColor.CGColor,
            UIColor.greenColor.CGColor,
            UIColor.cyanColor.CGColor,
            UIColor.blueColor.CGColor,
            UIColor.magentaColor.CGColor,
            UIColor.redColor.CGColor,
        };
        CFArrayRef colorsArray = CFArrayCreate(NULL, (const void **)colors, 7, &kCFTypeArrayCallBacks);
        CGGradientRef gradient = CGGradientCreateWithColors(colorspace, colorsArray, NULL);
        CGContextDrawLinearGradient(context,
                                    gradient,
                                    (CGPoint){3, 0},
                                    (CGPoint){size.width - 3, 0},
                                    0);
        CFRelease(gradient);
        CFRelease(colorsArray);
        CFRelease(colorspace);
        CGContextRestoreGState(context);

    }
    [[UIColor colorWithRed:120 / 255.0
                     green:120 / 255.0
                      blue:130 / 255.0
                     alpha:70 / 255.0] set];
    [path fill];
    
    [super drawRect:rect];
}

- (void)setFrame:(CGRect)frame
{
    [super setFrame:frame];
    [self setNeedsDisplay];
}

@end

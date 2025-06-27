#import "GBHueSliderCell.h"

@interface NSSliderCell(privateAPI)
- (double)_normalizedDoubleValue;
@end

@implementation GBHueSliderCell
{
    bool _drawingTrack;
}

-(NSColor *)colorValue
{
    double hue = self.doubleValue / 360.0;
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
    return [NSColor colorWithRed:r green:g blue:b alpha:1.0];
}

-(void)drawKnob:(NSRect)knobRect
{
    [super drawKnob:knobRect];
    NSBezierPath *path = nil;
    if (@available(macos 26.0, *)) {
        NSRect peekRect = knobRect;
        peekRect.size.height /= 2;
        peekRect.size.width -= peekRect.size.height;
        peekRect.origin.x += peekRect.size.height / 2;
        peekRect.origin.y += peekRect.size.height / 2;
        path = [NSBezierPath bezierPathWithRoundedRect:peekRect xRadius:peekRect.size.height / 2 yRadius:peekRect.size.height / 2];
    }
    else {
        NSRect peekRect = knobRect;
        peekRect.size.width /= 2;
        peekRect.size.height = peekRect.size.width;
        peekRect.origin.x += peekRect.size.width / 2;
        peekRect.origin.y += peekRect.size.height / 2;
        path = [NSBezierPath bezierPathWithOvalInRect:peekRect];

    }
    NSColor *color = self.colorValue;
    if (!self.enabled) {
        color = [color colorWithAlphaComponent:0.5];
    }
    [color setFill];
    [path fill];
    [[NSColor colorWithWhite:0 alpha:0.25] setStroke];
    [path setLineWidth:0.5];
    [path stroke];
}

-(double)_normalizedDoubleValue
{
    if (_drawingTrack) return 0;
    return [super _normalizedDoubleValue];
}

-(void)drawBarInside:(NSRect)rect flipped:(BOOL)flipped
{
    if (!self.enabled) {
        [super drawBarInside:rect flipped:flipped];
        return;
    }
    
    _drawingTrack = true;
    [super drawBarInside:rect flipped:flipped];
    _drawingTrack = false;
    
    NSGradient *gradient = [[NSGradient alloc] initWithColors:@[
        [NSColor redColor],
        [NSColor yellowColor],
        [NSColor greenColor],
        [NSColor cyanColor],
        [NSColor blueColor],
        [NSColor magentaColor],
        [NSColor redColor],
    ]];
    
    rect.origin.y += rect.size.height / 2 - 0.5;
    rect.size.height = 1;
    rect.size.width -= 2;
    rect.origin.x += 1;
    [[NSColor redColor] set];
    NSRectFill(rect);
    
    rect.size.width -= self.knobThickness + 2;
    rect.origin.x += self.knobThickness / 2 - 1;
    
    [gradient drawInRect:rect angle:0];
}

@end

@implementation NSSlider (GBHueSlider)
- (NSColor *)colorValue
{
    return ((GBHueSliderCell *)self.cell).colorValue;
}
@end

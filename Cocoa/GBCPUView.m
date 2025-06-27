#import "GBCPUView.h"

#define SAMPLE_COUNT 0x100 // ~4 seconds

@implementation GBCPUView
{
    double _samples[SAMPLE_COUNT];
    size_t _position;
}

- (void)drawRect:(NSRect)dirtyRect
{
    CGRect bounds = self.bounds;
    NSSize size = bounds.size;
    unsigned factor = [[self.window screen] backingScaleFactor];
    
    NSBitmapImageRep *maskBitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                           pixelsWide:(unsigned)size.width * factor
                                                                           pixelsHigh:(unsigned)size.height * factor
                                                                        bitsPerSample:8
                                                                      samplesPerPixel:2
                                                                             hasAlpha:true
                                                                             isPlanar:false
                                                                       colorSpaceName:NSDeviceWhiteColorSpace
                                                                          bytesPerRow:size.width * 2 * factor
                                                                         bitsPerPixel:16];
    

    
    NSGraphicsContext *mainContext = [NSGraphicsContext currentContext];
    
    
    NSColor *greenColor, *redColor;
    if (@available(macOS 10.10, *)) {
        greenColor = [NSColor systemGreenColor];
        redColor = [NSColor systemRedColor];
    }
    else {
        greenColor = [NSColor colorWithRed:3.0 / 16 green:0.5 blue:5.0 / 16 alpha:1.0];
        redColor = [NSColor colorWithRed:13.0 / 16 green:0.25 blue:0.25 alpha:1.0];
    }
    

    NSBitmapImageRep *colorBitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                            pixelsWide:SAMPLE_COUNT
                                                                            pixelsHigh:1
                                                                         bitsPerSample:8
                                                                       samplesPerPixel:3
                                                                              hasAlpha:false
                                                                              isPlanar:false
                                                                        colorSpaceName:NSDeviceRGBColorSpace
                                                                           bytesPerRow:SAMPLE_COUNT * 4
                                                                          bitsPerPixel:32];

    unsigned lastFill = 0;
    NSBezierPath *line = [NSBezierPath bezierPath];
    bool isRed = false;
    {
        double sample = _samples[_position % SAMPLE_COUNT];
        [line moveToPoint:NSMakePoint(0,
                                      (sample * (size.height - 1) + 0.5) * factor)];
        isRed = sample == 1;
    }
    
    
    [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:colorBitmap]];
    for (unsigned i = 1; i < SAMPLE_COUNT; i++) {
        double sample = _samples[(i + _position) % SAMPLE_COUNT];
        [line lineToPoint:NSMakePoint(size.width * i * factor / (SAMPLE_COUNT - 1),
                                      (sample * (size.height - 1) + 0.5) * factor)];
        
        if (isRed != (sample == 1)) {
            // Color changed
            [(isRed? redColor : greenColor) setFill];
            NSRectFill(CGRectMake(lastFill, 0, i - lastFill, 1));
            lastFill = i;
            
            isRed ^= true;
        }
    }
    [(isRed? redColor : greenColor) setFill];
    NSRectFill(CGRectMake(lastFill, 0, SAMPLE_COUNT - lastFill, 1));
    
    NSBezierPath *fill = [line copy];
    [fill lineToPoint:NSMakePoint(size.width * factor, 0)];
    [fill lineToPoint:NSMakePoint(0, 0)];
    
    NSColor *strokeColor = [NSColor whiteColor];
    NSColor *fillColor = [strokeColor colorWithAlphaComponent:1 / 3.0];
    
    [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:maskBitmap]];
    [fillColor setFill];
    [fill fill];
    
    [strokeColor setStroke];
    [line setLineWidth:factor];
    [line stroke];
    
    CGContextRef maskContext = CGContextRetain([NSGraphicsContext currentContext].graphicsPort);
    [NSGraphicsContext setCurrentContext:mainContext];
    CGContextSaveGState(mainContext.graphicsPort);
    
    CGImageRef maskImage = CGBitmapContextCreateImage(maskContext);
    CGContextClipToMask(mainContext.graphicsPort, bounds, maskImage);
    CGImageRelease(maskImage);
    
    NSImage *colors = [[NSImage alloc] initWithSize:NSMakeSize(SAMPLE_COUNT, 1)];
    [colors addRepresentation:colorBitmap];
    [colors drawInRect:bounds];
    
    CGContextRestoreGState(mainContext.graphicsPort);
    CGContextRelease(maskContext);
    
    
    [super drawRect:dirtyRect];
}

- (void)addSample:(double)sample
{
    _samples[_position++] = sample;
    if (_position == SAMPLE_COUNT) {
        _position = 0;
    }
}

@end

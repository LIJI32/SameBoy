#import "GBImageView.h"

@implementation GBImageViewGridConfiguration
- (instancetype)initWithColor:(NSColor *)color size:(NSUInteger)size
{
    self = [super init];
    self.color = color;
    self.size = size;
    return self;
}
@end

@interface GBGridView : NSView
@end

@implementation GBGridView

- (void)drawRect:(NSRect)dirtyRect
{
    GBImageView *parent = (GBImageView *)self.superview;

    CGFloat y_ratio = parent.frame.size.height / parent.image.size.height;
    CGFloat x_ratio = parent.frame.size.width / parent.image.size.width;
    for (GBImageViewGridConfiguration *conf in parent.verticalGrids) {
        [conf.color set];
        for (CGFloat y = conf.size * y_ratio; y < self.frame.size.height; y += conf.size * y_ratio) {
            NSBezierPath *line = [NSBezierPath bezierPath];
            [line moveToPoint:NSMakePoint(0, y - 0.5)];
            [line lineToPoint:NSMakePoint(self.frame.size.width, y - 0.5)];
            [line setLineWidth:1.0];
            [line stroke];
        }
    }
    
    for (GBImageViewGridConfiguration *conf in parent.horizontalGrids) {
        [conf.color set];
        for (CGFloat x = conf.size * x_ratio; x < self.frame.size.width; x += conf.size * x_ratio) {
            NSBezierPath *line = [NSBezierPath bezierPath];
            [line moveToPoint:NSMakePoint(x + 0.5, 0)];
            [line lineToPoint:NSMakePoint(x + 0.5, self.frame.size.height)];
            [line setLineWidth:1.0];
            [line stroke];
        }
    }
    
    if (parent.displayScrollRect) {
        // CGRectInfinite in NSBezierPath is broken in newer macOS versions
        NSBezierPath *path = [NSBezierPath bezierPathWithRect:CGRectMake(-0x4000, -0x4000, 0x8000, 0x8000)];
        for (unsigned x = 0; x < 2; x++) {
            for (unsigned y = 0; y < 2; y++) {
                NSRect rect = parent.scrollRect;
                rect.origin.x *= x_ratio;
                rect.origin.y *= y_ratio;
                rect.size.width *= x_ratio;
                rect.size.height *= y_ratio;
                rect.origin.y = self.frame.size.height - rect.origin.y - rect.size.height;
                
                rect.origin.x -= self.frame.size.width * x;
                rect.origin.y += self.frame.size.height * y;
                
                
                NSBezierPath *subpath = [NSBezierPath bezierPathWithRect:rect];
                [path appendBezierPath:subpath];
            }
        }
        [path setWindingRule:NSEvenOddWindingRule];
        [path setLineWidth:4.0];
        [path setLineJoinStyle:NSRoundLineJoinStyle];
        [[NSColor colorWithWhite:0.2 alpha:0.5] set];
        [path fill];
        [path addClip];
        [[NSColor colorWithWhite:0.0 alpha:0.6] set];
        [path stroke];
    }
}
@end

@implementation GBImageView
{
    NSTrackingArea *_trackingArea;
    GBGridView *_gridView;
    NSRect _scrollRect;
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
    self = [super initWithCoder:coder];
    self.wantsLayer = true;
    _gridView = [[GBGridView alloc] initWithFrame:self.bounds];
    _gridView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [self addSubview:_gridView];
    return self;
}

-(void)viewWillDraw
{
    [super viewWillDraw];
    for (CALayer *layer in self.layer.sublayers) {
        layer.magnificationFilter = kCAFilterNearest;
    }
}

- (void)setHorizontalGrids:(NSArray *)horizontalGrids
{
    self->_horizontalGrids = horizontalGrids;
    [_gridView setNeedsDisplay:true];
}

- (void)setVerticalGrids:(NSArray *)verticalGrids
{
    self->_verticalGrids = verticalGrids;
    [_gridView setNeedsDisplay:true];
}

- (void)setDisplayScrollRect:(bool)displayScrollRect
{
    self->_displayScrollRect = displayScrollRect;
    [_gridView setNeedsDisplay:true];
}

- (void)updateTrackingAreas
{
    if (_trackingArea != nil) {
        [self removeTrackingArea:_trackingArea];
    }
    
    _trackingArea = [ [NSTrackingArea alloc] initWithRect:[self bounds]
                                                 options:NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingMouseMoved
                                                   owner:self
                                                userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (void)mouseExited:(NSEvent *)theEvent
{
    if ([self.delegate respondsToSelector:@selector(mouseDidLeaveImageView:)]) {
        [self.delegate mouseDidLeaveImageView:self];
    }
}

- (void)mouseMoved:(NSEvent *)theEvent
{
    if ([self.delegate respondsToSelector:@selector(imageView:mouseMovedToX:Y:)]) {
        NSPoint location = [self convertPoint:theEvent.locationInWindow fromView:nil];
        location.x /= self.bounds.size.width;
        location.y /= self.bounds.size.height;
        location.y = 1 - location.y;
        location.x *= self.image.size.width;
        location.y *= self.image.size.height;
        [self.delegate imageView:self mouseMovedToX:(NSUInteger)location.x Y:(NSUInteger)location.y];
    }
}

- (void)setScrollRect:(NSRect)scrollRect
{
    if (memcmp(&scrollRect, &_scrollRect, sizeof(scrollRect)) != 0) {
        _scrollRect = scrollRect;
        [_gridView setNeedsDisplay:true];
    }
}

- (NSRect)scrollRect
{
    return _scrollRect;
}

@end

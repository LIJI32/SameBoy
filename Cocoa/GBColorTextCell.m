#import "GBColorTextCell.h"

@implementation GBColorTextCell

- (void)drawWithFrame:(NSRect)cellFrame inView:(NSView *)controlView
{
    [self.backgroundColor set];
    NSRectFill(cellFrame);
    
    NSBezierPath *path = [NSBezierPath bezierPathWithRect:cellFrame];
    path.lineWidth = 2;
    
[[NSColor colorWithWhite:0 alpha:0.25] setStroke];
    [path addClip];
    [path stroke];
    
    [self drawInteriorWithFrame:cellFrame inView:controlView];
}
@end

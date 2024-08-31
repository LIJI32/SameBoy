#import "GBDeleteButtonCell.h"

@implementation GBDeleteButtonCell

// Image scaling is broken on some older macOS versions
- (void)drawImage:(NSImage *)image withFrame:(NSRect)frame inView:(NSView *)controlView
{
    double size = 13;
    unsigned offset = 1;
    if (@available(macOS 10.10, *)) {
        size = 15;
        offset = 0;
    }
    frame.origin.x += round((frame.size.width - size) / 2) + offset;
    frame.origin.y += round((frame.size.height - size) / 2) - offset;
    frame.size.width = frame.size.height = size;
    [super drawImage:image withFrame:frame inView:controlView];
}

- (void)drawInteriorWithFrame:(NSRect)cellFrame inView:(NSView *)controlView
{
    [self drawImage:self.image withFrame:cellFrame inView:controlView];
}

-(void)drawBezelWithFrame:(NSRect)frame inView:(NSView *)controlView
{
    
}

@end

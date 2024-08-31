#import "GBCenteredTextCell.h"

@implementation GBCenteredTextCell
- (void)drawInteriorWithFrame:(NSRect)cellFrame inView:(NSView *)controlView
{
    double height = round([self.attributedStringValue size].height);
    cellFrame.origin.y += (cellFrame.size.height - height) / 2;
    cellFrame.size.height = height;
    [super drawInteriorWithFrame:cellFrame inView:controlView];
}


- (void)selectWithFrame:(NSRect)rect inView:(NSView *)controlView editor:(NSText *)textObj delegate:(id)delegate start:(NSInteger)selStart length:(NSInteger)selLength
{
    double height = round([self.attributedStringValue size].height);
    rect.origin.y += (rect.size.height - height) / 2;
    rect.size.height = height;
    [super selectWithFrame:rect inView:controlView editor:textObj delegate:delegate start:selStart length:selLength];
}

- (void)editWithFrame:(NSRect)rect inView:(NSView *)controlView editor:(NSText *)textObj delegate:(id)delegate event:(NSEvent *)event
{
    double height = round([self.attributedStringValue size].height);
    rect.origin.y += (rect.size.height - height) / 2;
    rect.size.height = height;
    [super editWithFrame:rect inView:controlView editor:textObj delegate:delegate event:event];

}
@end

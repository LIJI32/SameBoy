#import "GBToolbarPopUpButtonCell.h"

@implementation GBToolbarPopUpButtonCell

-(void)drawInteriorWithFrame:(NSRect)cellFrame inView:(NSView *)controlView
{
    [super drawInteriorWithFrame:CGRectInset(cellFrame, 5, 0) inView:controlView];
}

+ (instancetype)allocWithZone:(struct _NSZone *)zone
{
    if (@available(macOS 26.0, *)) {
        return [super allocWithZone:zone];
    }
    return (id)[NSPopUpButtonCell allocWithZone:zone];
}
@end

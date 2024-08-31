#import "GBPanel.h"

@implementation GBPanel
- (void)becomeKeyWindow
{
    [_ownerWindow makeMainWindow];
    [super becomeKeyWindow];
}
@end

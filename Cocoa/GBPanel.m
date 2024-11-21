#import "GBPanel.h"

@implementation GBPanel
- (void)becomeKeyWindow
{
    if ([_ownerWindow canBecomeMainWindow]) {
        [_ownerWindow makeMainWindow];
    }
    [super becomeKeyWindow];
}
@end

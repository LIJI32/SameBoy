#import "GBWindow.h"

@interface NSWindow(private)
- (void)_zoomFill:(id)sender;
@end

/*
   For some reason, Apple replaced the alt + zoom button behavior to be "fill" rather than zoom.
   I don't like that. It prevents SameBoy's integer scaling from working. Let's restore it.
*/

@implementation GBWindow
- (void)_zoomFill:(id)sender
{
    if (sender == [self standardWindowButton:NSWindowZoomButton] &&
        ((self.currentEvent.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask) == NSEventModifierFlagOption)) {
        [self zoom:sender];
        return;
    }
    [super _zoomFill:sender];
}
@end

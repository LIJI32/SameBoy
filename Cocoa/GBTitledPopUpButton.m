#import "GBTitledPopUpButton.h"

@implementation GBTitledPopUpButton

- (void)setDisplayTitle:(NSString *)displayTitle
{
    if (!displayTitle) {
        ((NSPopUpButtonCell *)self.cell).usesItemFromMenu = true;
        ((NSPopUpButtonCell *)self.cell).menuItem = nil;
        return;
    }
    ((NSPopUpButtonCell *)self.cell).usesItemFromMenu = false;
    ((NSPopUpButtonCell *)self.cell).menuItem = [[NSMenuItem alloc] initWithTitle:displayTitle action:nil keyEquivalent:@""];
}


- (NSString *)displayTitle
{
    return ((NSPopUpButtonCell *)self.cell).menuItem.title;
}

@end

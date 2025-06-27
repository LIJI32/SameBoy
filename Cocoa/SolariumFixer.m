#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

// Comment out to debug
#define NSLog(...)

// Solarium has weird proportions, we need to fix them.
@implementation NSControl (SolariumFixer)

- (void)awakeFromNib
{
    if (@available(macOS 26.0, *)) {
        if ([self.superview isKindOfClass:objc_getClass("NSToolbarItemViewer")]) return;
        
        if ([self isKindOfClass:[NSStepper class]]) {
            NSLog(@"Stepper needs adjustment: %s in window %@", sel_getName(self.action), self.window.title);
            CGRect frame = self.frame;
            frame.origin.y += 1;
            self.frame = frame;
            return;
        }
        if (self.controlSize != NSControlSizeRegular) return;
        
        if ([self isKindOfClass:[NSPopUpButton class]] ) {
            NSLog(@"Popup button needs adjustment: %@ in window %@", ((NSButton *)self).title, self.window.title);
            CGRect frame = self.frame;
            if (frame.size.height != 25) {
                NSLog(@"%@ in window %@ has the wrong height!", ((NSButton *)self).title, self.window.title);
                return;
            }
            frame.size.width -= 2 + 5; // Remove 5 from the right and 2 from the left
            frame.origin.x += 2;
            frame.origin.y += 2;
            self.frame = frame;
        }
        else if ([self isKindOfClass:[NSSegmentedControl class]] ) {
            NSLog(@"Segmented button needs adjustment: %s in window %@", sel_getName(self.action), self.window.title);
            CGRect frame = self.frame;
            if (frame.size.height != 25) {
                NSLog(@"%s in window %@ has the wrong height!", sel_getName(self.action), self.window.title);
                return;
            }
            frame.origin.x += 8;
            frame.origin.y += 1;
            self.frame = frame;
        }
        else if ([self isKindOfClass:[NSTextField class]]) {
            NSTextField *field = (id)self;
            if (![field isBezeled]) return;
            NSLog(@"Text field needs adjustment: %@ in window %@", ((NSTextField *)self).placeholderString, self.window.title);
            CGRect frame = self.frame;
            if (frame.size.height == 21) {
                frame.size.height = 24;
            }
            else {
                NSLog(@"%@ in window %@ has the wrong height!", ((NSTextField *)self).placeholderString, self.window.title);
                return;
            }
            frame.size.width -= 1 + 1; // Remove 1 from the right and 1 from the left
            frame.origin.x += 1;
            frame.origin.y -= 1;

            self.frame = frame;
        }
        else if ([self isKindOfClass:[NSButton class]]) {
            NSLog(@"Button: %@ in window %@", @(sel_getName(self.action)), self.window.title);
            NSButton *button = (id)self;
            if (!button.isBordered) return;
            if (button.bezelStyle == NSBezelStylePush) {
                NSLog(@"Button needs adjustment: %@ in window %@", @(sel_getName(self.action)), self.window.title);
                CGRect frame = self.frame;
                frame.size.width -= 7 + 7; // Remove 7 from the right and 7 from the left
                frame.origin.x += 7;
                frame.origin.y += 5;
                if (frame.size.height == 32) {
                    frame.size.height = 25;
                }
                else {
                    NSLog(@"%@ in window %@ has the wrong height!", @(sel_getName(self.action)), self.window.title);
                }                self.frame = frame;
            }
            else if (button.bezelStyle == NSBezelStyleRegularSquare) {
                CGRect frame = self.frame;
                if (frame.size.height != 18) return;
                NSLog(@"Check/Radio needs adjustment: %@ in window %@", ((NSButton *)self).title, self.window.title);
                frame.size.width -= 2;
                frame.origin.x += 2;
                frame.origin.y += 1;
                frame.size.height = 16;
                self.frame = frame;
            }
            else if (button.bezelStyle == NSBezelStyleHelpButton) {
                CGRect frame = self.frame;
                NSLog(@"Help button needs adjustment: %@ in window %@", ((NSButton *)self).title, self.window.title);
                frame.origin.y += 2;
                self.frame = frame;
            }
        }
        else if ([self isKindOfClass:[NSSlider class]]) {
            NSLog(@"Slider needs adjustment: %s in window %@", sel_getName(self.action), self.window.title);
            CGRect frame = self.frame;
            frame.origin.y += 3;
            self.frame = frame;
        }
    }
}

@end

@implementation NSToolbarItem (SolariumFixer)

- (void)awakeFromNib
{
    if (@available(macOS 26.0, *)) {
        NSLog(@"Toolbar item %@ has view %@", self.label, self.view);
        if ([self.view isKindOfClass:[NSTextField class]]) {
            NSLog(@"Handling (Text field)");
            self.bordered = true;
            
            NSSize maxSize = self.maxSize;
            maxSize.height = 36;
            self.maxSize = maxSize;
            
            NSSize minSize = self.minSize;
            minSize.height = 36;
            self.minSize = minSize;
            
            ((NSTextField *)self.view).backgroundColor = [NSColor clearColor];
            ((NSTextField *)self.view).bezeled = false;
            ((NSTextField *)self.view).bordered = true;

            // Work around even more AppKit bugs
            self.toolbar.displayMode++;
            self.toolbar.displayMode--;
        }
        else if ([self.view isKindOfClass:[NSPopUpButton class]]) {
            NSLog(@"Handling (Pop up button)");
            self.bordered = true;
            
            NSSize maxSize = self.maxSize;
            maxSize.height = 28;
            self.maxSize = maxSize;
            
            NSSize minSize = self.minSize;
            minSize.height = 28;
            self.minSize = minSize;
        }
    }
    else if (@available(macOS 11.0, *)) { // While at it, make macOS 11-15 a bit more consistent
        if ([self.view isKindOfClass:[NSTextField class]]) {
            ((NSTextField *)self.view).bezelStyle = NSTextFieldRoundedBezel;
        }
    }
}

@end

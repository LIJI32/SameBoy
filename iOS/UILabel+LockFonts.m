#import "UILabel+LockFonts.h"
#import <objc/runtime.h>

@implementation UILabel (LockFonts)

- (void)setLocksFonts:(bool)locksFont
{
    objc_setAssociatedObject(self, @selector(locksFonts), @(locksFont), OBJC_ASSOCIATION_RETAIN);
}

- (bool)locksFonts
{
    return [objc_getAssociatedObject(self, _cmd) boolValue];
}

- (void)setFontHook:(UIFont *)font
{
    if (self.locksFonts) return;
    [self setFontHook:font];
}

+ (void)load
{
    method_exchangeImplementations(class_getInstanceMethod(self, @selector(setFontHook:)),
                                   class_getInstanceMethod(self, @selector(setFont:)));
    
}

@end

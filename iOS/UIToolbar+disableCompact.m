#import "UIToolbar+disableCompact.h"
#import <objc/runtime.h>

@implementation UIToolbar (disableCompact)

- (void)setDisableCompactLayout:(bool)disableCompactLayout
{
    objc_setAssociatedObject(self, @selector(disableCompactLayout), @(disableCompactLayout), OBJC_ASSOCIATION_RETAIN);
}

- (bool)disableCompactLayout
{
    return [objc_getAssociatedObject(self, _cmd) boolValue];
}

- (CGSize)sizeThatFitsHook:(CGSize)size
{
    CGSize ret = [self sizeThatFitsHook:size];
    if (!self.disableCompactLayout) return ret;
    if (ret.height < 44) {
        ret.height = 44;
    }
    return ret;
}

+ (void)load
{
    method_exchangeImplementations(class_getInstanceMethod(self, @selector(sizeThatFitsHook:)),
                                   class_getInstanceMethod(self, @selector(sizeThatFits:)));
    
}

@end

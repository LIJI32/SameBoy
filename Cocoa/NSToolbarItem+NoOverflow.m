#import <AppKit/AppKit.h>
#import <objc/runtime.h>

static id nop(id self, SEL _cmd)
{
    return nil;
}

static double minSize(id self, SEL _cmd)
{
    return 80.0;
}

@implementation NSToolbarItem (NoOverflow)

+ (void)load
{
    // Prevent collapsing toolbar items into menu items, they don't work in that form
    method_setImplementation(class_getInstanceMethod(self, @selector(menuFormRepresentation)), (IMP)nop);
    // Prevent over-agressive collapsing of the Printer Feed menu
    method_setImplementation(class_getInstanceMethod(NSClassFromString(@"NSToolbarTitleView"), @selector(minSize)), (IMP)minSize);
}

@end

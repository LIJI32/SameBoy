#import "NSTextFieldCell+Inset.h"
#import <AppKit/AppKit.h>
#import <objc/runtime.h>

@interface NSTextFieldCell ()
@property NSSize textInset;
- (bool)_isEditingInView:(NSView *)view;
@end

@implementation NSTextFieldCell (Inset)

- (void)setTextInset:(NSSize)textInset
{
    objc_setAssociatedObject(self, @selector(textInset), @(textInset), OBJC_ASSOCIATION_RETAIN);
}

- (NSSize)textInset
{
    return [objc_getAssociatedObject(self, _cmd) sizeValue];
}

- (void)drawWithFrameHook:(NSRect)cellFrame inView:(NSView *)controlView
{
    NSSize inset = self.textInset;
    if (self.drawsBackground) {
        [self.backgroundColor setFill];
        if ([self _isEditingInView:controlView]) {
            NSRectFill(cellFrame);
        }
        else {
            NSRectFill(NSMakeRect(cellFrame.origin.x, cellFrame.origin.y,
                                  cellFrame.size.width, inset.height));
            NSRectFill(NSMakeRect(cellFrame.origin.x, cellFrame.origin.y + cellFrame.size.height - inset.height,
                                  cellFrame.size.width, inset.height));
            
            NSRectFill(NSMakeRect(cellFrame.origin.x, cellFrame.origin.y + inset.height,
                                  inset.width, cellFrame.size.height - inset.height * 2));
            NSRectFill(NSMakeRect(cellFrame.origin.x + cellFrame.size.width - inset.width, cellFrame.origin.y + inset.height,
                                  inset.width, cellFrame.size.height - inset.height * 2));
        }
    }
    cellFrame.origin.x += inset.width;
    cellFrame.origin.y += inset.height;
    cellFrame.size.width -= inset.width * 2;
    cellFrame.size.height -= inset.height * 2;
    [self drawWithFrameHook:cellFrame inView:controlView];
}

+ (void)load
{
    method_exchangeImplementations(class_getInstanceMethod(self, @selector(drawWithFrame:inView:)),
                                   class_getInstanceMethod(self, @selector(drawWithFrameHook:inView:)));
}

@end


@implementation NSTextField (Inset)

- (bool)wantsUpdateLayerHook
{
    CGSize inset = ((NSTextFieldCell *)self.cell).textInset;
    if (inset.width || inset.height) return false;
    return [self wantsUpdateLayerHook];
}

+ (void)load
{
    Method method = class_getInstanceMethod(self, @selector(wantsUpdateLayer));
    if (class_addMethod(self, @selector(wantsUpdateLayer), method_getImplementation(method), method_getTypeEncoding(method))) {
        method = class_getInstanceMethod(self, @selector(wantsUpdateLayer));
    }
    method_exchangeImplementations(method,
                                   class_getInstanceMethod(self, @selector(wantsUpdateLayerHook)));
}

@end

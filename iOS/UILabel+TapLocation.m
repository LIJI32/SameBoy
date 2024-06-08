#import "UILabel+TapLocation.h"

@implementation UILabel (TapLocation)

- (unsigned)characterAtTap:(UITapGestureRecognizer *)tap
{
    CGPoint tapLocation = [tap locationInView:self];
    
    NSTextStorage *textStorage = [[NSTextStorage alloc] initWithAttributedString:self.attributedText];
    NSLayoutManager *layoutManager = [[NSLayoutManager alloc] init];
    [textStorage addLayoutManager:layoutManager];
    
    NSTextContainer *textContainer = [[NSTextContainer alloc] initWithSize:CGSizeMake(self.frame.size.width,
                                                                                      self.frame.size.height + 256)];
    textContainer.lineFragmentPadding = 0;
    textContainer.maximumNumberOfLines = 256;
    textContainer.lineBreakMode = self.lineBreakMode;
    
    [layoutManager addTextContainer:textContainer];
    
    return [layoutManager characterIndexForPoint:tapLocation
                                 inTextContainer:textContainer
        fractionOfDistanceBetweenInsertionPoints:NULL];

}

@end

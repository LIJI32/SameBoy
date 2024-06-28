#import <UIKit/UIKit.h>
#import "GBLayout.h"
#import "GBView.h"

@interface GBBackgroundView : UIImageView
- (instancetype)initWithLayout:(GBLayout *)layout;

@property (readonly) GBView *gbView;
@property (nonatomic) GBLayout *layout;
@property (nonatomic) bool usesSwipePad;

- (void)enterPreviewMode:(bool)showLabel;
- (void)reloadThemeImages;
- (void)fadeOverlayOut;
@end

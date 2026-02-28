#import <UIKit/UIKit.h>
#import "GBLayout.h"
#import "GBView.h"
#import "GBSettingsViewController.h"

@interface GBBackgroundView : UIImageView
- (instancetype)initWithLayout:(GBLayout *)layout;

@property (readonly) GBView *gbView;
@property (nonatomic) GBLayout *layout;
@property (nonatomic) bool usesSwipePad;
@property (nonatomic) GBControllerFocus fullScreenMode;

- (void)enterPreviewMode:(bool)showLabel;
- (void)reloadThemeImages;
- (void)fadeOverlayOut;
- (void)saveSwipeFromController:(bool)fromController;
- (void)loadSwipeFromController:(bool)fromController;
@end

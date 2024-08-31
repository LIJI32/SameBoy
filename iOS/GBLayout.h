#import <UIKit/UIKit.h>
#import "GBTheme.h"

@interface GBLayout : NSObject
- (instancetype)initWithTheme:(GBTheme *)theme;
@property (readonly) GBTheme *theme;

@property (readonly) UIImage *background;
@property (readonly) CGRect screenRect;
@property (readonly) CGRect fullScreenRect;
@property (readonly) CGRect logoRect;
@property (readonly) CGPoint dpadLocation;
@property (readonly) CGPoint aLocation;
@property (readonly) CGPoint bLocation;
@property (readonly) CGPoint abComboLocation;
@property (readonly) CGPoint startLocation;
@property (readonly) CGPoint selectLocation;

- (CGRect)viewRectForOrientation:(UIInterfaceOrientation)orientation;
@end

#ifdef GBLayoutInternal

@interface GBLayout()
@property UIImage *background;
@property CGRect screenRect;
@property CGRect fullScreenRect;
@property CGPoint dpadLocation;
@property CGPoint aLocation;
@property CGPoint bLocation;
@property CGPoint abComboLocation;
@property CGPoint startLocation;
@property CGPoint selectLocation;
@property (readonly) CGSize resolution; // Always vertical
@property (readonly) CGSize size; // Size in pixels, override to make horizontal
@property (readonly) unsigned factor;
@property (readonly) unsigned minY;
@property (readonly) unsigned homeBar;
@property (readonly) unsigned cutout;
@property (readonly) bool hasFractionalPixels;

- (void)drawBackground;
- (void)drawScreenBezels;
- (void)drawLogoInVerticalRange:(NSRange)range;
- (void)drawLabels;
- (void)drawThemedLabelsWithBlock:(void (^)(void))block;

- (CGSize)buttonDeltaForMaxHorizontalDistance:(double)distance;
@end

#endif

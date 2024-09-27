#import <UIKit/UIKit.h>

@interface GBTheme : NSObject
@property (readonly, direct) UIColor *brandColor;
@property (readonly, direct) UIColor *backgroundGradientTop;
@property (readonly, direct) UIColor *backgroundGradientBottom;
@property (readonly, direct) UIColor *bezelsGradientTop;
@property (readonly, direct) UIColor *bezelsGradientBottom;

#ifdef APPSTORE
@property (readonly, direct) bool embossLabels;
@property (readonly, direct) UIImage *texture;
#endif

@property (readonly, direct) NSString *name;

@property (readonly, direct) bool renderingPreview; // Kind of a hack

@property (readonly, direct) UIImage *horizontalPreview;
@property (readonly, direct) UIImage *verticalPreview;

@property (readonly, direct) bool isDark;

- (instancetype)initDefaultTheme __attribute__((objc_direct));
- (instancetype)initDarkTheme __attribute__((objc_direct));

#ifdef APPSTORE
- (instancetype)initDMGTheme __attribute__((objc_direct));
- (instancetype)initPlayItLoudBlackTheme __attribute__((objc_direct));
- (instancetype)initPlayItLoudThemeWithColor:(uint32_t)color  andName:(NSString *)name __attribute__((objc_direct));

- (instancetype)initCGBThemeWithColor:(uint32_t)color andName:(NSString *)name __attribute__((objc_direct));
- (instancetype)initAGBThemeWithColor:(uint32_t)color andName:(NSString *)name __attribute__((objc_direct));

- (instancetype)initGameAndWatchTheme __attribute__((objc_direct));
- (instancetype)initSFCTheme __attribute__((objc_direct));
- (instancetype)initSNESTheme __attribute__((objc_direct));
- (instancetype)initGCNTheme __attribute__((objc_direct));

- (instancetype)initMegaDuckTheme __attribute__((objc_direct));
- (instancetype)initDarkMegaDuckTheme __attribute__((objc_direct));
#endif

- (UIImage *)imageNamed:(NSString *)name  __attribute__((objc_direct));

@end

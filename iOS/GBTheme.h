#import <UIKit/UIKit.h>

@interface GBTheme : NSObject
- (UIImage *)imageNamed:(NSString *)name;

@property (readonly) UIColor *brandColor;
@property (readonly) UIColor *backgroundGradientTop;
@property (readonly) UIColor *backgroundGradientBottom;
@property (readonly) UIColor *bezelsGradientTop;
@property (readonly) UIColor *bezelsGradientBottom;

@property (readonly) bool embossLabels;

@property (readonly) UIImage *texture;
@property (readonly) NSString *name;

@property (readonly) bool renderingPreview; // Kind of a hack

@property (readonly) UIImage *horizontalPreview;
@property (readonly) UIImage *verticalPreview;

- (instancetype)initDefaultTheme;
- (instancetype)initDarkTheme;

- (instancetype)initDMGTheme;
- (instancetype)initPlayItLoudBlackTheme;
- (instancetype)initPlayItLoudThemeWithColor:(uint32_t)color  andName:(NSString *)name;

- (instancetype)initCGBThemeWithColor:(uint32_t)color andName:(NSString *)name;
- (instancetype)initAGBThemeWithColor:(uint32_t)color andName:(NSString *)name;

- (instancetype)initGameAndWatchTheme;
- (instancetype)initSFCTheme;
- (instancetype)initSNESTheme;

@end

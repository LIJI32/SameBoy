#import <UIKit/UIKit.h>

typedef enum {
    GBSliderStyleTemperature,
    GBSliderStyleHue,
    GBSliderStyleTicks,
} GBSliderStyle;

@interface GBSlider : UISlider
@property GBSliderStyle style;
@end

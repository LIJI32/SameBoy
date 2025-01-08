#define GBLayoutInternal
#import "GBVerticalLayout.h"

@implementation GBVerticalLayout

- (instancetype)initWithTheme:(GBTheme *)theme
{
    self = [super initWithTheme:theme];
    if (!self) return nil;
    
    CGSize resolution = self.resolution;
    
    CGRect screenRect = {0,};
    screenRect.size.width = self.hasFractionalPixels? resolution.width : floor(resolution.width / 160) * 160;
    screenRect.size.height = screenRect.size.width / 160 * 144;
    
    screenRect.origin.x = (resolution.width - screenRect.size.width) / 2;
    screenRect.origin.y = (resolution.height - screenRect.size.height) / 2;
    self.fullScreenRect = screenRect;
    
    double screenBorderWidth = MIN(screenRect.size.width / 40, 16 * self.factor);
    screenRect.origin.y = self.minY + MIN(screenBorderWidth * 2, 20 * self.factor);
    self.screenRect = screenRect;
    
    double controlAreaStart = screenRect.origin.y + screenRect.size.height + MIN(screenBorderWidth * 2, 20 * self.factor);
    
    self.selectLocation = (CGPoint){
        MIN(resolution.width / 4, 120 * self.factor),
        MIN(resolution.height - 80 * self.factor, (resolution.height - controlAreaStart) * 0.75 + controlAreaStart)
    };
    
    self.startLocation = (CGPoint){
        resolution.width - self.selectLocation.x,
        self.selectLocation.y
    };
        
    double buttonRadius = 36 * self.factor;
    CGSize buttonsDelta = [self buttonDeltaForMaxHorizontalDistance:resolution.width / 2 - buttonRadius * 2 - screenBorderWidth * 2];
    
    self.dpadLocation = (CGPoint) {
        self.selectLocation.x,
        self.selectLocation.y - 140 * self.factor
    };
    
    CGPoint buttonsCenter = {
        resolution.width - self.dpadLocation.x,
        self.dpadLocation.y,
    };
    
    self.aLocation = (CGPoint) {
        round(buttonsCenter.x + buttonsDelta.width / 2),
        round(buttonsCenter.y - buttonsDelta.height / 2)
    };
    
    self.bLocation = (CGPoint) {
        round(buttonsCenter.x - buttonsDelta.width / 2),
        round(buttonsCenter.y + buttonsDelta.height / 2)
    };
    
    self.abComboLocation = buttonsCenter;
    
    double controlsTop = self.dpadLocation.y - 80 * self.factor;
    double middleSpace = self.bLocation.x - buttonRadius - (self.dpadLocation.x + 80 * self.factor);
    
    if (theme.renderingPreview) {
        UIGraphicsBeginImageContextWithOptions((CGSize){resolution.width / 8, resolution.height / 8}, true, 1);
        CGContextScaleCTM(UIGraphicsGetCurrentContext(), 1 / 8.0, 1 / 8.0);
    }
    else {
        UIGraphicsBeginImageContextWithOptions(resolution, true, 1);
    }
    [self drawBackground];
    [self drawScreenBezels];
    
    [self drawThemedLabelsWithBlock:^{
        if (controlsTop - controlAreaStart > 24 * self.factor + screenBorderWidth * 2) {
            [self drawLogoInVerticalRange:(NSRange){controlAreaStart + screenBorderWidth, 24 * self.factor}
                           controlPadding:0];
        }
        else if (middleSpace > 160 * self.factor) {
            [self drawLogoInVerticalRange:(NSRange){controlAreaStart + screenBorderWidth, 24 * self.factor}
                           controlPadding:self.dpadLocation.x * 2];
        }
        
        [self drawLabels];
    }];
    
    self.background = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return self;
}

@end

#define GBLayoutInternal
#import "GBHorizontalLayout.h"

@implementation GBHorizontalLayout

- (instancetype)initWithTheme:(GBTheme *)theme
{
    self = [super initWithTheme:theme];
    if (!self) return nil;
    
    CGSize resolution = {self.resolution.height - self.cutout, self.resolution.width};
    
    CGRect screenRect = {0,};
    screenRect.size.height = self.hasFractionalPixels? (resolution.height - self.homeBar) : floor((resolution.height - self.homeBar) / 144) * 144;
    screenRect.size.width = screenRect.size.height / 144 * 160;
    
    double horizontalMargin, verticalMargin;
    while (true) {
        horizontalMargin = (resolution.width - screenRect.size.width) / 2;
        verticalMargin = (resolution.height - self.homeBar - screenRect.size.height) / 2;
        if (horizontalMargin / self.factor < 164) {
            if (self.hasFractionalPixels) {
                screenRect.size.width = resolution.width - 164 * self.factor * 2;
                screenRect.size.height = screenRect.size.width / 160 * 144;
                continue;
            }
            screenRect.size.width -= 160;
            screenRect.size.height -= 144;
            continue;
        }
        break;
    }
    
    double screenBorderWidth = screenRect.size.width / 40;
    
    screenRect.origin.x = (resolution.width - screenRect.size.width) / 2;
    bool drawSameBoyLogo = false;
    if (verticalMargin * 2 > screenBorderWidth * 7) {
        drawSameBoyLogo = true;
        screenRect.origin.y = (resolution.height - self.homeBar - screenRect.size.height - screenBorderWidth * 5) / 2;
    }
    else {
        screenRect.origin.y = (resolution.height - self.homeBar - screenRect.size.height) / 2;
    }
    
    self.screenRect = screenRect;
    
    self.dpadLocation = (CGPoint){
        round((screenRect.origin.x - screenBorderWidth) / 2),
        round(resolution.height * 3 / 8)
    };
    
    double wingWidth = (resolution.width - screenRect.size.width) / 2 - screenBorderWidth * 5;
    double buttonRadius = 36 * self.factor;
    CGSize buttonsDelta = [self buttonDeltaForMaxHorizontalDistance:wingWidth - buttonRadius * 2];
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

    self.selectLocation = (CGPoint){
        self.dpadLocation.x,
        MIN(round(resolution.height * 3 / 4), self.dpadLocation.y + 180 * self.factor)
    };
    
    self.startLocation = (CGPoint){
        buttonsCenter.x,
        self.selectLocation.y
    };
    
    resolution.width += self.cutout * 2;
    self.screenRect = (CGRect){{self.screenRect.origin.x + self.cutout, self.screenRect.origin.y}, self.screenRect.size};
    self.dpadLocation = (CGPoint){self.dpadLocation.x + self.cutout, self.dpadLocation.y};
    self.aLocation = (CGPoint){self.aLocation.x + self.cutout, self.aLocation.y};
    self.bLocation = (CGPoint){self.bLocation.x + self.cutout, self.bLocation.y};
    self.startLocation = (CGPoint){self.startLocation.x + self.cutout, self.startLocation.y};
    self.selectLocation = (CGPoint){self.selectLocation.x + self.cutout, self.selectLocation.y};
    self.abComboLocation = (CGPoint){(self.aLocation.x + self.bLocation.x) / 2,
                                     (self.aLocation.y + self.bLocation.y) / 2};

    
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
        if (drawSameBoyLogo) {
            double bezelBottom = screenRect.origin.y + screenRect.size.height + screenBorderWidth;
            double freeSpace = resolution.height - bezelBottom;
            [self drawLogoInVerticalRange:(NSRange){bezelBottom + screenBorderWidth * 2, freeSpace - screenBorderWidth * 4}];
        }
        
        [self drawLabels];
    }];
    
    self.background = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return self;
}

- (CGRect)viewRectForOrientation:(UIInterfaceOrientation)orientation
{
    if (orientation == UIInterfaceOrientationLandscapeLeft) {
        return CGRectMake(-(signed)self.cutout / (signed)self.factor, 0, self.background.size.width / self.factor, self.background.size.height / self.factor);
    }
    return CGRectMake(0, 0, self.background.size.width / self.factor, self.background.size.height / self.factor);
}

- (CGSize)size
{
    return (CGSize){self.resolution.height, self.resolution.width};
}

@end

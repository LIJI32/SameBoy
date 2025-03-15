#define GBLayoutInternal
#import "GBHorizontalLayout.h"

@implementation GBHorizontalLayout

- (instancetype)initWithTheme:(GBTheme *)theme cutoutOnRight:(bool)cutoutOnRight
{
    self = [super initWithTheme:theme];
    if (!self) return nil;
    
    CGSize resolution = {self.resolution.height, self.resolution.width};
    
    CGRect screenRect = {0,};
    screenRect.size.height = self.hasFractionalPixels? (resolution.height - self.homeBar) : floor((resolution.height - self.homeBar) / 144) * 144;
    screenRect.size.width = screenRect.size.height / 144 * 160;
    
    screenRect.origin.x = (resolution.width - screenRect.size.width) / 2;
    screenRect.origin.y = (resolution.height - self.homeBar - screenRect.size.height) / 2;
    self.fullScreenRect = screenRect;
    
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
    
    double screenBorderWidth = MIN(screenRect.size.width / 40, 16 * self.factor);

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
        round((screenRect.origin.x - screenBorderWidth) / 2) + (cutoutOnRight? 0 : self.cutout / 2),
        round(resolution.height * 3 / 8)
    };
        
    double longWing = (resolution.width - screenRect.size.width) / 2 - screenBorderWidth * 5;
    double shortWing = longWing - self.cutout;
    double buttonRadius = 36 * self.factor;
    CGSize buttonsDelta = [self buttonDeltaForMaxHorizontalDistance:(cutoutOnRight? shortWing : longWing) - buttonRadius * 2];
    CGPoint buttonsCenter = {
        (resolution.width + screenRect.size.width + screenRect.origin.x) / 2 - (cutoutOnRight? self.cutout / 2 : 0),
        self.dpadLocation.y,
    };
    
    self.abComboLocation = buttonsCenter;
    
    self.aLocation = (CGPoint) {
        round(buttonsCenter.x + buttonsDelta.width / 2),
        round(buttonsCenter.y - buttonsDelta.height / 2)
    };
    
    self.bLocation = (CGPoint) {
        round(buttonsCenter.x - buttonsDelta.width / 2),
        round(buttonsCenter.y + buttonsDelta.height / 2)
    };

    self.selectLocation = (CGPoint){
        self.dpadLocation.x + (cutoutOnRight? self.cutout / 2 : 0),
        MIN(round(resolution.height * 3 / 4), self.dpadLocation.y + 180 * self.factor)
    };
    
    self.startLocation = (CGPoint){
        buttonsCenter.x -  (cutoutOnRight? 0 : self.cutout / 2 ),
        self.selectLocation.y
    };

    
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
            [self drawLogoInVerticalRange:(NSRange){bezelBottom + screenBorderWidth * 2, freeSpace - screenBorderWidth * 4}
                           controlPadding:0];
        }
        
        [self drawLabels];
    }];
    
    self.background = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return self;
}

- (instancetype)initWithTheme:(GBTheme *)theme
{
    assert(false);
    __builtin_unreachable();
}

- (CGRect)viewRectForOrientation:(UIInterfaceOrientation)orientation
{
    return CGRectMake(0, 0, self.background.size.width / self.factor, self.background.size.height / self.factor);
}

- (CGSize)size
{
    return (CGSize){self.resolution.height, self.resolution.width};
}

@end

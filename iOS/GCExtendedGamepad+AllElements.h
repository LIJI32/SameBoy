#import <GameController/GameController.h>

typedef enum {
    GBUsageDpad,
    GBUsageButtonA,
    GBUsageButtonB,
    GBUsageButtonX,
    GBUsageButtonY,
    GBUsageButtonMenu,
    GBUsageButtonOptions,
    GBUsageButtonHome,
    GBUsageLeftThumbstick,
    GBUsageRightThumbstick,
    GBUsageLeftShoulder,
    GBUsageRightShoulder,
    GBUsageLeftTrigger,
    GBUsageRightTrigger,
    GBUsageLeftThumbstickButton,
    GBUsageRightThumbstickButton,
    GBUsageTouchpadButton,
} GBControllerUsage;

@interface GCExtendedGamepad (AllElements)
- (NSDictionary <NSNumber *, GCControllerElement *> *)elementsDictionary;
@end

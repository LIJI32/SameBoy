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
    GBUsageMiscStartIndex, // Add to this
} GBControllerUsage;


NSDictionary <NSNumber *, GCControllerElement *> *GCControllerGetElements(GCController *self);

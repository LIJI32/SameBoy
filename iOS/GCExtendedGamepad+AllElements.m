#import "GCExtendedGamepad+AllElements.h"
#import <objc/runtime.h>

@implementation GCExtendedGamepad (AllElements)

- (NSDictionary <NSNumber *, GCControllerElement *> *)elementsDictionary;
{
    NSMutableDictionary <NSNumber *, GCControllerElement *> *ret = [NSMutableDictionary dictionary];
    if (self.dpad) ret[@(GBUsageDpad)] = self.dpad;
    if (self.buttonA) ret[@(GBUsageButtonA)] = self.buttonA;
    if (self.buttonB) ret[@(GBUsageButtonB)] = self.buttonB;
    if (self.buttonX) ret[@(GBUsageButtonX)] = self.buttonX;
    if (self.buttonY) ret[@(GBUsageButtonY)] = self.buttonY;
    if (@available(iOS 13.0, *)) {
        if (self.buttonMenu) ret[@(GBUsageButtonMenu)] = self.buttonMenu;
        if (self.buttonOptions) ret[@(GBUsageButtonOptions)] = self.buttonOptions;
    }
    // Can't be used
    /* if (@available(iOS 14.0, *)) {
        if (self.buttonHome) ret[@(GBUsageButtonHome)] = self.buttonHome;
    } */
    if (self.leftThumbstick) ret[@(GBUsageLeftThumbstick)] = self.leftThumbstick;
    if (self.rightThumbstick) ret[@(GBUsageRightThumbstick)] = self.rightThumbstick;
    if (self.leftShoulder) ret[@(GBUsageLeftShoulder)] = self.leftShoulder;
    if (self.rightShoulder) ret[@(GBUsageRightShoulder)] = self.rightShoulder;
    if (self.leftTrigger) ret[@(GBUsageLeftTrigger)] = self.leftTrigger;
    if (self.rightTrigger) ret[@(GBUsageRightTrigger)] = self.rightTrigger;
    if (@available(iOS 12.1, *)) {
        if (self.leftThumbstickButton) ret[@(GBUsageLeftThumbstickButton)] = self.leftThumbstickButton;
        if (self.rightThumbstickButton) ret[@(GBUsageRightThumbstickButton)] = self.rightThumbstickButton;
    }
    
    if (@available(iOS 14.0, *)) {
        if ([self isKindOfClass:[GCDualShockGamepad class]]) {
            GCDualShockGamepad *dualShock = (GCDualShockGamepad *)self;
            if (dualShock.touchpadButton) ret[@(GBUsageTouchpadButton)] = dualShock.touchpadButton;
        }
    }
    
    if (@available(iOS 14.5, *)) {
        if ([self isKindOfClass:[GCDualSenseGamepad class]]) {
            GCDualSenseGamepad *dualSense = (GCDualSenseGamepad *)self;
            if (dualSense.touchpadButton) ret[@(GBUsageTouchpadButton)] = dualSense.touchpadButton;
        }
    }
    return ret;
}
@end

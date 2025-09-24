#import "GCControllerGetElements.h"
#import <objc/runtime.h>

static NSDictionary <NSNumber *, GCControllerElement *> *GCControllerGetElementsLegacy(GCExtendedGamepad *self)
{
    if (!self) return nil;
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
    return ret;
}

API_AVAILABLE(ios(14.0))
static NSDictionary <NSNumber *, GCControllerElement *> *GCControllerGetElementsModern(GCPhysicalInputProfile *self)
{
    static NSDictionary * nameToUsage = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        nameToUsage = @{
            GCInputButtonA: @(GBUsageButtonA),
            GCInputButtonB: @(GBUsageButtonB),
            GCInputButtonX: @(GBUsageButtonX),
            GCInputButtonY: @(GBUsageButtonY),
            
            GCInputLeftThumbstick: @(GBUsageLeftThumbstick),
            GCInputRightThumbstick: @(GBUsageRightThumbstick),
            GCInputLeftThumbstickButton: @(GBUsageLeftThumbstickButton),
            GCInputRightThumbstickButton: @(GBUsageRightThumbstickButton),
            GCInputLeftShoulder: @(GBUsageLeftShoulder),
            GCInputRightShoulder: @(GBUsageRightShoulder),
            GCInputLeftTrigger: @(GBUsageLeftTrigger),
            GCInputRightTrigger: @(GBUsageRightTrigger),
            
            GCInputButtonMenu: @(GBUsageButtonMenu),
            GCInputButtonOptions: @(GBUsageButtonOptions),
            GCInputDualShockTouchpadButton: @(GBUsageTouchpadButton),
        };
    });
    
    NSArray<NSString *> *elementNames = [self.elements.allKeys sortedArrayUsingSelector:@selector(compare:)];
    
    NSMutableDictionary <NSNumber *, GCControllerElement *> *ret = [NSMutableDictionary dictionary];
    GBControllerUsage miscUsage = GBUsageMiscStartIndex;
    for (NSString *name in elementNames) {
        if ([name isEqualToString:GCInputButtonHome]) continue;
        GCControllerElement *input = self.elements[name];
        NSNumber *_usage = nameToUsage[name];
        GBControllerUsage usage;
        if (_usage) {
            usage = _usage.unsignedIntValue;
            // These get weird mappings by default
            if ([self.device.vendorName isEqualToString:@"Joy-Con (L)"] ||
                [self.device.vendorName isEqualToString:@"Joy-Con (R)"]) {
                switch (usage) {
                    case GBUsageButtonA: usage = GBUsageButtonB; break;
                    case GBUsageButtonB: usage = GBUsageButtonY; break;
                    case GBUsageButtonX: usage = GBUsageButtonA; break;
                    case GBUsageButtonY: usage = GBUsageButtonX; break;
                    default: break;
                }
            }
        }
        else if ([input isKindOfClass:[GCControllerDirectionPad class]] && !ret[@(GBUsageDpad)]) {
            usage = GBUsageDpad;
        }
        else {
            usage = miscUsage++;
        }
        if ([ret.allValues containsObject:input]) {
            NSNumber *alias = [ret allKeysForObject:input][0];
            if (alias.unsignedIntValue >= GBUsageMiscStartIndex) {
                [ret removeObjectForKey:alias];
            }
            else {
                continue;
            }
        }
        ret[@(usage)] = input;
    }
    
    return ret;
}

NSDictionary <NSNumber *, GCControllerElement *> *GCControllerGetElements(GCController *self)
{
    NSDictionary <NSNumber *, GCControllerElement *> *ret = nil;
    ret = objc_getAssociatedObject(self, GCControllerGetElements);
    if (ret) return ret;
    if (@available(iOS 14.0, *)) {
        ret = GCControllerGetElementsModern(self.physicalInputProfile);
    }
    else {
        ret = GCControllerGetElementsLegacy(self.extendedGamepad);
    }
    objc_setAssociatedObject(self, GCControllerGetElements, ret, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return ret;
}

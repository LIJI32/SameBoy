#import "JOYController.h"
#import "JOYMultiplayerController.h"
#import "JOYElement.h"
#import "JOYSubElement.h"
#import "JOYFullReportElement.h"
#import "JOYButton.h"
#import "JOYEmulatedButton.h"
#import <IOKit/hid/IOHIDLib.h>

#import <AppKit/AppKit.h>
extern NSTextField *globalDebugField;

#define PWM_RESOLUTION 16

static NSString const *JOYAxisGroups = @"JOYAxisGroups";
static NSString const *JOYReportIDFilters = @"JOYReportIDFilters";
static NSString const *JOYButtonUsageMapping = @"JOYButtonUsageMapping";
static NSString const *JOYAxisUsageMapping = @"JOYAxisUsageMapping";
static NSString const *JOYAxes2DUsageMapping = @"JOYAxes2DUsageMapping";
static NSString const *JOYCustomReports = @"JOYCustomReports";
static NSString const *JOYIsSwitch = @"JOYIsSwitch";
static NSString const *JOYJoyCon = @"JOYJoyCon";
static NSString const *JOYRumbleUsage = @"JOYRumbleUsage";
static NSString const *JOYRumbleUsagePage = @"JOYRumbleUsagePage";
static NSString const *JOYConnectedUsage = @"JOYConnectedUsage";
static NSString const *JOYConnectedUsagePage = @"JOYConnectedUsagePage";
static NSString const *JOYRumbleMin = @"JOYRumbleMin";
static NSString const *JOYRumbleMax = @"JOYRumbleMax";
static NSString const *JOYSwapZRz = @"JOYSwapZRz";
static NSString const *JOYActivationReport = @"JOYActivationReport";
static NSString const *JOYIgnoredReports = @"JOYIgnoredReports";
static NSString const *JOYIsDualShock3 = @"JOYIsDualShock3";
static NSString const *JOYIsSony = @"JOYIsSony";
static NSString const *JOYEmulateAxisButtons = @"JOYEmulateAxisButtons";

static NSMutableDictionary<id, JOYController *> *controllers; // Physical controllers
static NSMutableArray<JOYController *> *exposedControllers; // Logical controllers

static NSDictionary *hacksByName = nil;
static NSDictionary *hacksByManufacturer = nil;

static NSMutableSet<id<JOYListener>> *listeners = nil;

static bool axes2DEmulateButtons = false;
static bool hatsEmulateButtons = false;

@interface JOYController ()
+ (void)controllerAdded:(IOHIDDeviceRef) device;
+ (void)controllerRemoved:(IOHIDDeviceRef) device;
- (void)elementChanged:(IOHIDElementRef) element;
- (void)gotReport:(NSData *)report;

@end

@interface JOYButton ()
- (instancetype)initWithElement:(JOYElement *)element;
- (bool)updateState;
@property JOYButtonUsage originalUsage;
@end

@interface JOYAxis ()
- (instancetype)initWithElement:(JOYElement *)element;
- (bool)updateState;
@end

@interface JOYHat ()
- (instancetype)initWithElement:(JOYElement *)element;
- (bool)updateState;
@end

@interface JOYAxes2D ()
- (instancetype)initWithFirstElement:(JOYElement *)element1 secondElement:(JOYElement *)element2;
- (bool)updateState;
@property unsigned rotation; // in 90 degrees units, clockwise
@end

@interface JOYAxes3D ()
{
    @public JOYElement *_element1, *_element2, *_element3;
}
- (instancetype)initWithFirstElement:(JOYElement *)element1 secondElement:(JOYElement *)element2 thirdElement:(JOYElement *)element2;
- (bool)updateState;
@property unsigned rotation; // in 90 degrees units, clockwise
@end

@interface JOYInput ()
@property unsigned combinedIndex;
@end

static NSDictionary *CreateHIDDeviceMatchDictionary(const UInt32 page, const UInt32 usage)
{
    return @{
        @kIOHIDDeviceUsagePageKey: @(page),
        @kIOHIDDeviceUsageKey: @(usage),
    };
}

static void HIDDeviceAdded(void *context, IOReturn result, void *sender, IOHIDDeviceRef device)
{
    [JOYController controllerAdded:device];
}

static void HIDDeviceRemoved(void *context, IOReturn result, void *sender, IOHIDDeviceRef device)
{
    [JOYController controllerRemoved:device];
}

static void HIDInput(void *context, IOReturn result, void *sender, IOHIDValueRef value)
{
    [(__bridge JOYController *)context elementChanged:IOHIDValueGetElement(value)];
}

static void HIDReport(void *context, IOReturn result, void *sender, IOHIDReportType type,
                      uint32_t reportID, uint8_t *report, CFIndex reportLength)
{
    if (reportLength) {
        [(__bridge JOYController *)context gotReport:[[NSData alloc] initWithBytesNoCopy:report length:reportLength freeWhenDone:false]];
    }
}

typedef struct __attribute__((packed)) {
    uint8_t reportID;
    uint8_t sequence;
    uint8_t rumbleData[8];
    uint8_t command;
    uint8_t commandData[26];
} JOYSwitchPacket;

typedef struct __attribute__((packed)) {
    uint8_t reportID;
    uint8_t padding;
    uint8_t rumbleRightDuration;
    uint8_t rumbleRightStrength;
    uint8_t rumbleLeftDuration;
    uint8_t rumbleLeftStrength;
    uint32_t padding2;
    uint8_t ledsEnabled;
    struct {
        uint8_t timeEnabled;
        uint8_t dutyLength;
        uint8_t enabled;
        uint8_t dutyOff;
        uint8_t dutyOn;
    } __attribute__((packed)) led[5];
    uint8_t padding3[13];
} JOYDualShock3Output;

typedef struct __attribute__((packed)) {
    uint8_t reportID;
    uint8_t sequence;
    union {
        uint8_t tag;
        uint8_t reportIDOnUSB;
    };
    uint16_t flags;
    uint8_t rumbleRightStrength; // Weak
    uint8_t rumbleLeftStrength; // Strong
    uint8_t reserved[4];
    uint8_t muteButtonLED;
    uint8_t powerSaveControl;
    uint8_t reserved2[28];
    uint8_t flags2;
    uint8_t reserved3[2];
    uint8_t lightbarSetup;
    uint8_t LEDBrightness;
    uint8_t playerLEDs;
    uint8_t lightbarRed;
    uint8_t lightbarGreen;
    uint8_t lightbarBlue;
    uint8_t bluetoothSpecific[24];
    uint32_t crc32;
} JOYDualSenseOutput;


typedef union {
    JOYSwitchPacket switchPacket;
    JOYDualShock3Output ds3Output;
    JOYDualSenseOutput dualsenseOutput;
} JOYVendorSpecificOutput;

@implementation JOYController
{
    @public // Let JOYCombinedController access everything
    IOHIDDeviceRef _device;
    NSMutableDictionary<JOYElement *, JOYButton *> *_buttons;
    NSMutableDictionary<JOYElement *, JOYAxis *> *_axes;
    NSMutableDictionary<JOYElement *, JOYAxes2D *> *_axes2D;
    NSMutableDictionary<JOYElement *, JOYAxes3D *> *_axes3D;
    NSMutableDictionary<JOYElement *, JOYHat *> *_hats;
    NSMutableDictionary<NSNumber *, JOYFullReportElement *> *_fullReportElements;
    NSMutableDictionary<JOYFullReportElement *, NSArray<JOYElement *> *> *_multiElements;
    JOYAxes3D *_lastAxes3D;
    
    // Button emulation
    NSMutableDictionary<NSNumber *, JOYEmulatedButton *> *_axisEmulatedButtons;
    NSMutableDictionary<NSNumber *, NSArray <JOYEmulatedButton *> *> *_axes2DEmulatedButtons;
    NSMutableDictionary<NSNumber *, NSArray <JOYEmulatedButton *> *> *_hatEmulatedButtons;

    JOYElement *_rumbleElement;
    JOYElement *_connectedElement;
    NSMutableDictionary<NSValue *, JOYElement *> *_iokitToJOY;
    NSString *_serialSuffix;
    bool _isSwitch; // Does this controller use the Switch protocol?
    bool _isDualShock3; // Does this controller use DS3 outputs?
    bool _isSony; // Is this a DS4 or newer Sony controller?
    bool _isDualSense;
    bool _isUSBDualSense;

    JOYVendorSpecificOutput _lastVendorSpecificOutput;
    volatile double _rumbleAmplitude;
    bool _physicallyConnected;
    bool _logicallyConnected;
    
    NSDictionary *_hacks;
    NSMutableData *_lastReport;
    
    // Used when creating inputs
    JOYElement *_previousAxisElement;
    
    uint8_t _playerLEDs;
    double _sentRumbleAmp;
    unsigned _rumbleCounter;
    bool _deviceCantSendReports;
    dispatch_queue_t _rumbleQueue;
    JOYCombinedController *_parent;
}

- (instancetype)initWithDevice:(IOHIDDeviceRef) device hacks:(NSDictionary *)hacks
{
    return [self initWithDevice:device reportIDFilter:nil serialSuffix:nil hacks:hacks];
}

-(void)createOutputForElement:(JOYElement *)element
{
    uint16_t rumbleUsagePage = (uint16_t)[_hacks[JOYRumbleUsagePage] unsignedIntValue];
    uint16_t rumbleUsage = (uint16_t)[_hacks[JOYRumbleUsage] unsignedIntValue];

    if (!_rumbleElement && rumbleUsage && rumbleUsagePage && element.usage == rumbleUsage && element.usagePage == rumbleUsagePage) {
        if (_hacks[JOYRumbleMin]) {
            element.min = [_hacks[JOYRumbleMin] unsignedIntValue];
        }
        if (_hacks[JOYRumbleMax]) {
            element.max = [_hacks[JOYRumbleMax] unsignedIntValue];
        }
        _rumbleElement = element;
    }
}

-(void)createInputForElement:(JOYElement *)element
{
    uint16_t connectedUsagePage = (uint16_t)[_hacks[JOYConnectedUsagePage] unsignedIntValue];
    uint16_t connectedUsage = (uint16_t)[_hacks[JOYConnectedUsage] unsignedIntValue];

    if (!_connectedElement && connectedUsage && connectedUsagePage && element.usage == connectedUsage && element.usagePage == connectedUsagePage) {
        _connectedElement = element;
        _logicallyConnected = element.value != element.min;
        return;
    }
    
    NSDictionary *axisGroups = @{
        @(kHIDUsage_GD_X): @(0),
        @(kHIDUsage_GD_Y): @(0),
        @(kHIDUsage_GD_Z): @(1),
        @(kHIDUsage_GD_Rx): @(2),
        @(kHIDUsage_GD_Ry): @(2),
        @(kHIDUsage_GD_Rz): @(1),
    };
    
    if (element.usagePage == kHIDPage_Sensor) {
        JOYAxes3DUsage usage;
        JOYElement *element1 = nil, *element2 = nil, *element3 = nil;
        
        switch (element.usage) {
            case kHIDUsage_Snsr_Data_Motion_AccelerationAxisX:
            case kHIDUsage_Snsr_Data_Motion_AccelerationAxisY:
            case kHIDUsage_Snsr_Data_Motion_AccelerationAxisZ:
                usage = JOYAxes3DUsageAcceleration;
                break;
            case kHIDUsage_Snsr_Data_Motion_AngularPositionXAxis:
            case kHIDUsage_Snsr_Data_Motion_AngularPositionYAxis:
            case kHIDUsage_Snsr_Data_Motion_AngularPositionZAxis:
                usage = JOYAxes3DUsageOrientation;
                break;
            case kHIDUsage_Snsr_Data_Motion_AngularVelocityXAxis:
            case kHIDUsage_Snsr_Data_Motion_AngularVelocityYAxis:
            case kHIDUsage_Snsr_Data_Motion_AngularVelocityZAxis:
                usage = JOYAxes3DUsageGyroscope;
                break;
            default:
                return;
        }
        
        switch (element.usage) {
            case kHIDUsage_Snsr_Data_Motion_AccelerationAxisX:
            case kHIDUsage_Snsr_Data_Motion_AngularPositionXAxis:
            case kHIDUsage_Snsr_Data_Motion_AngularVelocityXAxis:
                element1 = element;
                if (_lastAxes3D && !_lastAxes3D->_element1 && _lastAxes3D.usage == usage) {
                    element2 = _lastAxes3D->_element2;
                    element3 = _lastAxes3D->_element3;
                }
                break;
            case kHIDUsage_Snsr_Data_Motion_AccelerationAxisY:
            case kHIDUsage_Snsr_Data_Motion_AngularPositionYAxis:
            case kHIDUsage_Snsr_Data_Motion_AngularVelocityYAxis:
                element2 = element;
                if (_lastAxes3D && !_lastAxes3D->_element2 && _lastAxes3D.usage == usage) {
                    element1 = _lastAxes3D->_element1;
                    element3 = _lastAxes3D->_element3;
                }
                break;
            case kHIDUsage_Snsr_Data_Motion_AccelerationAxisZ:
            case kHIDUsage_Snsr_Data_Motion_AngularPositionZAxis:
            case kHIDUsage_Snsr_Data_Motion_AngularVelocityZAxis:
                element3 = element;
                if (_lastAxes3D && !_lastAxes3D->_element3 && _lastAxes3D.usage == usage) {
                    element1 = _lastAxes3D->_element1;
                    element2 = _lastAxes3D->_element2;
                }
                break;
        }
        
        _lastAxes3D = [[JOYAxes3D alloc] initWithFirstElement:element1 secondElement:element2 thirdElement:element3];
        _lastAxes3D.usage = usage;
        if (element1) _axes3D[element1] = _lastAxes3D;
        if (element2) _axes3D[element2] = _lastAxes3D;
        if (element3) _axes3D[element3] = _lastAxes3D;
        
        return;
    }
    
    axisGroups = _hacks[JOYAxisGroups] ?: axisGroups;
    
    if (element.usagePage == kHIDPage_Button ||
        (element.usagePage == kHIDPage_Consumer && (element.usage == kHIDUsage_Csmr_ACHome ||
                                                    element.usage == kHIDUsage_Csmr_ACBack))) {
    button: {
        JOYButton *button = [[JOYButton alloc] initWithElement: element];
        [_buttons setObject:button forKey:element];
        NSNumber *replacementUsage = element.usagePage == kHIDPage_Button? _hacks[JOYButtonUsageMapping][@(button.usage)] : nil;
        if (replacementUsage) {
            button.originalUsage = button.usage = [replacementUsage unsignedIntValue];
        }
        return;
    }
    }
    else if (element.usagePage == kHIDPage_Simulation) {
        switch (element.usage) {
            case kHIDUsage_Sim_Accelerator:
            case kHIDUsage_Sim_Brake:
            case kHIDUsage_Sim_Rudder:
            case kHIDUsage_Sim_Throttle:
            goto single;
        }
    }
    else if (element.usagePage == kHIDPage_GenericDesktop) {
        switch (element.usage) {
            case kHIDUsage_GD_X:
            case kHIDUsage_GD_Y:
            case kHIDUsage_GD_Z:
            case kHIDUsage_GD_Rx:
            case kHIDUsage_GD_Ry:
            case kHIDUsage_GD_Rz: {
                
                JOYElement *other = _previousAxisElement;
                _previousAxisElement = element;
                if (!other) goto single;
                if (other.usage >= element.usage) goto single;
                if (other.reportID != element.reportID) goto single;
                if (![axisGroups[@(other.usage)] isEqual: axisGroups[@(element.usage)]]) goto single;
                if (other.parentID != element.parentID) goto single;
                
                JOYAxes2D *axes = nil;
                if (other.usage == kHIDUsage_GD_Z && element.usage == kHIDUsage_GD_Rz && [_hacks[JOYSwapZRz] boolValue]) {
                    axes = [[JOYAxes2D alloc] initWithFirstElement:element secondElement:other];
                }
                else {
                    axes = [[JOYAxes2D alloc] initWithFirstElement:other secondElement:element];
                }
                NSNumber *replacementUsage = _hacks[JOYAxes2DUsageMapping][@(axes.usage)];
                if (replacementUsage) {
                    axes.usage = [replacementUsage unsignedIntValue];
                }
                
                [_axisEmulatedButtons removeObjectForKey:@(_axes[other].uniqueID)];
                [_axes removeObjectForKey:other];
                _previousAxisElement = nil;
                _axes2D[other] = axes;
                _axes2D[element] = axes;
                
                if (axes2DEmulateButtons) {
                    _axes2DEmulatedButtons[@(axes.uniqueID)] = @[
                        [[JOYEmulatedButton alloc] initWithUsage:JOYButtonUsageDPadLeft type:JOYButtonTypeAxes2DEmulated uniqueID:axes.uniqueID | 0x100000000L],
                        [[JOYEmulatedButton alloc] initWithUsage:JOYButtonUsageDPadRight type:JOYButtonTypeAxes2DEmulated uniqueID:axes.uniqueID | 0x200000000L],
                        [[JOYEmulatedButton alloc] initWithUsage:JOYButtonUsageDPadUp type:JOYButtonTypeAxes2DEmulated  uniqueID:axes.uniqueID | 0x300000000L],
                        [[JOYEmulatedButton alloc] initWithUsage:JOYButtonUsageDPadDown type:JOYButtonTypeAxes2DEmulated uniqueID:axes.uniqueID | 0x400000000L],
                    ];
                }
                break;
            }
            case kHIDUsage_GD_Slider:
            case kHIDUsage_GD_Dial:
            case kHIDUsage_GD_Wheel:
            { single: {
                JOYAxis *axis = [[JOYAxis alloc] initWithElement: element];
                [_axes setObject:axis forKey:element];
                
                NSNumber *replacementUsage = element.usagePage == kHIDPage_GenericDesktop? _hacks[JOYAxisUsageMapping][@(axis.usage)] : nil;
                if (replacementUsage) {
                    axis.usage = [replacementUsage unsignedIntValue];
                }
                
                if ([_hacks[JOYEmulateAxisButtons] boolValue]) {
                    _axisEmulatedButtons[@(axis.uniqueID)] =
                    [[JOYEmulatedButton alloc] initWithUsage:axis.equivalentButtonUsage type:JOYButtonTypeAxisEmulated uniqueID:axis.uniqueID];
                }
                
                
                break;
            }}
            case kHIDUsage_GD_DPadUp:
            case kHIDUsage_GD_DPadDown:
            case kHIDUsage_GD_DPadRight:
            case kHIDUsage_GD_DPadLeft:
            case kHIDUsage_GD_Start:
            case kHIDUsage_GD_Select:
            case kHIDUsage_GD_SystemMainMenu:
                goto button;
                
            case kHIDUsage_GD_Hatswitch: {
                JOYHat *hat = [[JOYHat alloc] initWithElement: element];
                [_hats setObject:hat forKey:element];
                if (hatsEmulateButtons) {
                    _hatEmulatedButtons[@(hat.uniqueID)] = @[
                        [[JOYEmulatedButton alloc] initWithUsage:JOYButtonUsageDPadLeft type:JOYButtonTypeHatEmulated  uniqueID:hat.uniqueID | 0x100000000L],
                        [[JOYEmulatedButton alloc] initWithUsage:JOYButtonUsageDPadRight type:JOYButtonTypeHatEmulated uniqueID:hat.uniqueID | 0x200000000L],
                        [[JOYEmulatedButton alloc] initWithUsage:JOYButtonUsageDPadUp type:JOYButtonTypeHatEmulated  uniqueID:hat.uniqueID | 0x300000000L],
                        [[JOYEmulatedButton alloc] initWithUsage:JOYButtonUsageDPadDown type:JOYButtonTypeHatEmulated uniqueID:hat.uniqueID | 0x400000000L],
                    ];
                }
                break;
            }
        }
    }
}

- (instancetype)initWithDevice:(IOHIDDeviceRef)device reportIDFilter:(NSArray <NSNumber *> *) filter serialSuffix:(NSString *)suffix hacks:(NSDictionary *)hacks
{
    self = [super init];
    if (!self) return self;
    
    _physicallyConnected = true;
    _logicallyConnected = true;
    _device = (IOHIDDeviceRef)CFRetain(device);
    _serialSuffix = suffix;
    _playerLEDs = -1;
    [self obtainInfo];

    IOHIDDeviceRegisterInputValueCallback(device, HIDInput, (void *)self);
    IOHIDDeviceScheduleWithRunLoop(device, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    NSArray *array = CFBridgingRelease(IOHIDDeviceCopyMatchingElements(device, NULL, kIOHIDOptionsTypeNone));
    _buttons = [NSMutableDictionary dictionary];
    _axes = [NSMutableDictionary dictionary];
    _axes2D = [NSMutableDictionary dictionary];
    _axes3D = [NSMutableDictionary dictionary];
    _hats = [NSMutableDictionary dictionary];
    _axisEmulatedButtons = [NSMutableDictionary dictionary];
    _axes2DEmulatedButtons = [NSMutableDictionary dictionary];
    _hatEmulatedButtons = [NSMutableDictionary dictionary];
    _iokitToJOY = [NSMutableDictionary dictionary];
    
    
    _hacks = hacks;
    _isSwitch = [_hacks[JOYIsSwitch] boolValue];
    _isDualShock3 = [_hacks[JOYIsDualShock3] boolValue];
    _isSony = [_hacks[JOYIsSony] boolValue];
    _joyconType = [_hacks[JOYJoyCon] unsignedIntValue];

    NSDictionary *customReports = hacks[JOYCustomReports];
    _lastReport = [NSMutableData dataWithLength:MAX(
                                                    MAX(
                                                        [(__bridge NSNumber *)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDMaxInputReportSizeKey)) unsignedIntValue],
                                                        [(__bridge NSNumber *)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDMaxOutputReportSizeKey)) unsignedIntValue]
                                                        ),
                                                    [(__bridge NSNumber *)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDMaxFeatureReportSizeKey)) unsignedIntValue]
                                                    )];
    IOHIDDeviceRegisterInputReportCallback(device, _lastReport.mutableBytes, _lastReport.length, HIDReport, (void *)self);

    if (hacks[JOYCustomReports]) {
        _multiElements = [NSMutableDictionary dictionary];
        _fullReportElements = [NSMutableDictionary dictionary];

        
        for (NSNumber *_reportID in customReports) {
            signed reportID = [_reportID intValue];
            bool isOutput = false;
            if (reportID < 0) {
                isOutput = true;
                reportID = -reportID;
            }
            
            JOYFullReportElement *element = [[JOYFullReportElement alloc] initWithDevice:device reportID:reportID];
            NSMutableArray *elements = [NSMutableArray array];
            for (NSDictionary <NSString *,NSNumber *> *subElementDef in customReports[_reportID]) {
                if (filter && subElementDef[@"reportID"] && ![filter containsObject:subElementDef[@"reportID"]]) continue;
                JOYSubElement *subElement = [[JOYSubElement alloc] initWithRealElement:element
                                                                                  size:subElementDef[@"size"].unsignedLongValue
                                                                                offset:subElementDef[@"offset"].unsignedLongValue + 8 // Compensate for the reportID
                                                                             usagePage:subElementDef[@"usagePage"].unsignedLongValue
                                                                                 usage:subElementDef[@"usage"].unsignedLongValue
                                                                                   min:subElementDef[@"min"].unsignedIntValue
                                                                                   max:subElementDef[@"max"].unsignedIntValue];
                [elements addObject:subElement];
                if (isOutput) {
                    [self createOutputForElement:subElement];
                }
                else {
                    [self createInputForElement:subElement];
                }
            }
            _multiElements[element] = elements;
            if (!isOutput) {
                _fullReportElements[@(reportID)] = element;
            }
        }
    }
    
    id previous = nil;
    NSSet *ignoredReports = nil;
    if (hacks[JOYIgnoredReports]) {
        ignoredReports = [NSSet setWithArray:hacks[JOYIgnoredReports]];
    }
    
    for (id _element in array) {
        if (_element == previous) continue; // Some elements are reported twice for some reason
        previous = _element;
        JOYElement *element = [[JOYElement alloc] initWithElement:(__bridge IOHIDElementRef)_element];

        bool isOutput = false;
        if (filter && ![filter containsObject:@(element.reportID)]) continue;

        switch (IOHIDElementGetType((__bridge IOHIDElementRef)_element)) {
            /* Handled */
            case kIOHIDElementTypeInput_Misc:
            case kIOHIDElementTypeInput_Button:
            case kIOHIDElementTypeInput_Axis:
                break;
            case kIOHIDElementTypeOutput:
                isOutput = true;
                break;
            /* Ignored */
            default:
            case kIOHIDElementTypeInput_ScanCodes:
            case kIOHIDElementTypeInput_NULL:
            case kIOHIDElementTypeFeature:
            case kIOHIDElementTypeCollection:
                continue;
        }
        if ((!isOutput && [ignoredReports containsObject:@(element.reportID)]) ||
            (isOutput && [ignoredReports containsObject:@(-element.reportID)])) continue;

        
        if (IOHIDElementIsArray((__bridge IOHIDElementRef)_element)) continue;
        
        if (isOutput) {
            [self createOutputForElement:element];
        }
        else {
            [self createInputForElement:element];
        }
        
        _iokitToJOY[@(IOHIDElementGetCookie((__bridge IOHIDElementRef)_element))] = element;
    }
    
    [exposedControllers addObject:self];
    if (_logicallyConnected) {
        for (id<JOYListener> listener in listeners) {
            if ([listener respondsToSelector:@selector(controllerConnected:)]) {
                [listener controllerConnected:self];
            }
        }
    }
    
    if (_hacks[JOYActivationReport]) {
        [self sendReport:hacks[JOYActivationReport]];
    }
    
    if (_isSwitch) {
        [self sendReport:[NSData dataWithBytes:inline_const(uint8_t[], {0x80, 0x04}) length:2]];
        [self sendReport:[NSData dataWithBytes:inline_const(uint8_t[], {0x80, 0x02}) length:2]];
        
        _lastVendorSpecificOutput.switchPacket.reportID = 0x1; // Rumble and LEDs
        _lastVendorSpecificOutput.switchPacket.sequence++;
        _lastVendorSpecificOutput.switchPacket.sequence &= 0xF;
        _lastVendorSpecificOutput.switchPacket.command = 3; // Set input report mode
        _lastVendorSpecificOutput.switchPacket.commandData[0] = 0x30; // Standard full mode
        [self sendReport:[NSData dataWithBytes:&_lastVendorSpecificOutput.switchPacket length:sizeof(_lastVendorSpecificOutput.switchPacket)]];
        
        _lastVendorSpecificOutput.switchPacket.sequence++;
        _lastVendorSpecificOutput.switchPacket.sequence &= 0xF;
        _lastVendorSpecificOutput.switchPacket.command = 0x48; // Set vibration enabled
        _lastVendorSpecificOutput.switchPacket.commandData[0] = 1; // enabled
        [self sendReport:[NSData dataWithBytes:&_lastVendorSpecificOutput.switchPacket length:sizeof(_lastVendorSpecificOutput.switchPacket)]];
        
        // The Joy-Cons don't like having their IMU enabled too quickly
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            _lastVendorSpecificOutput.switchPacket.sequence++;
            _lastVendorSpecificOutput.switchPacket.sequence &= 0xF;
            _lastVendorSpecificOutput.switchPacket.command = 0x40; // Enable/disableIMU
            _lastVendorSpecificOutput.switchPacket.commandData[0] = 1; // Enabled
            [self sendReport:[NSData dataWithBytes:&_lastVendorSpecificOutput.switchPacket length:sizeof(_lastVendorSpecificOutput.switchPacket)]];
        });
    }
    
    if (_isDualShock3) {
        _lastVendorSpecificOutput.ds3Output = (JOYDualShock3Output){
            .reportID = 1,
            .led = {
                {.timeEnabled =  0xFF, .dutyLength = 0x27, .enabled = 0x10, .dutyOff = 0, .dutyOn = 0x32},
                {.timeEnabled =  0xFF, .dutyLength = 0x27, .enabled = 0x10, .dutyOff = 0, .dutyOn = 0x32},
                {.timeEnabled =  0xFF, .dutyLength = 0x27, .enabled = 0x10, .dutyOff = 0, .dutyOn = 0x32},
                {.timeEnabled =  0xFF, .dutyLength = 0x27, .enabled = 0x10, .dutyOff = 0, .dutyOn = 0x32},
                {.timeEnabled =  0,    .dutyLength = 0,    .enabled = 0,    .dutyOff = 0, .dutyOn = 0},
            }
        };
    }
    if (_isSony) {
        _isDualSense = [(__bridge NSNumber *)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey)) unsignedIntValue] == 0xCE6;
    }
    
    if (_isDualSense) {
        _isUSBDualSense = [(__bridge NSString *)IOHIDDeviceGetProperty(_device, CFSTR(kIOHIDTransportKey)) isEqualToString:@"USB"];
        _lastVendorSpecificOutput.dualsenseOutput = (JOYDualSenseOutput){
            .reportID = 0x31,
            .tag = 0x10,
            .flags = 0x1403, // Rumble, lightbar and player LEDs
            .flags2 = 2,
            .lightbarSetup = 2,
            .lightbarBlue = 255,
        };
        if (_isUSBDualSense) {
            _lastVendorSpecificOutput.dualsenseOutput.reportIDOnUSB = 1;
            _lastVendorSpecificOutput.dualsenseOutput.lightbarBlue = 0;
            _lastVendorSpecificOutput.dualsenseOutput.lightbarGreen = 96;
            _lastVendorSpecificOutput.dualsenseOutput.lightbarRed = 255;

        }
        // Send a report to switch the controller to a more capable mode
        [self sendDualSenseOutput];
        _lastVendorSpecificOutput.dualsenseOutput.flags2 = 0;
        _lastVendorSpecificOutput.dualsenseOutput.lightbarSetup = 0;
    }
    
    _rumbleQueue = dispatch_queue_create([NSString stringWithFormat:@"Rumble Queue for %@", self.deviceName].UTF8String,
                                         NULL);
    
    return self;
}


- (void)obtainInfo
{
    _deviceName = IOHIDDeviceGetProperty(_device, CFSTR(kIOHIDProductKey));
    NSString *serial = (__bridge NSString *)IOHIDDeviceGetProperty(_device, CFSTR(kIOHIDSerialNumberKey));
    if (!serial || [(__bridge NSString *)IOHIDDeviceGetProperty(_device, CFSTR(kIOHIDTransportKey)) isEqualToString:@"USB"]) {
        serial = [NSString stringWithFormat:@"%04x%04x%08x",
                  [(__bridge NSNumber *)IOHIDDeviceGetProperty(_device, CFSTR(kIOHIDVendorIDKey)) unsignedIntValue],
                  [(__bridge NSNumber *)IOHIDDeviceGetProperty(_device, CFSTR(kIOHIDProductIDKey)) unsignedIntValue],
                  [(__bridge NSNumber *)IOHIDDeviceGetProperty(_device, CFSTR(kIOHIDLocationIDKey)) unsignedIntValue]];
    }
    if (_serialSuffix) {
        _uniqueID = [NSString stringWithFormat:@"%@-%@", serial, _serialSuffix];
        return;
    }
    _uniqueID = serial;
}

- (JOYControllerCombinedType)combinedControllerType
{
    return _parent? JOYControllerCombinedTypeComponent : JOYControllerCombinedTypeSingle;
}

- (NSString *)description
{
    return [NSString stringWithFormat:@"<%@: %p, %@, %@>", self.className, self, self.deviceName, self.uniqueID];
}

- (NSArray<JOYButton *> *)buttons
{
    NSMutableArray *ret = [[_buttons allValues] mutableCopy];
    [ret addObjectsFromArray:_axisEmulatedButtons.allValues];
    for (NSArray *array in _axes2DEmulatedButtons.allValues) {
        [ret addObjectsFromArray:array];
    }
    for (NSArray *array in _hatEmulatedButtons.allValues) {
        [ret addObjectsFromArray:array];
    }
    return ret;
}

- (NSArray<JOYAxis *> *)axes
{
    return [_axes allValues];
}

- (NSArray<JOYAxes2D *> *)axes2D
{
    return [[NSSet setWithArray:[_axes2D allValues]] allObjects];
}

- (NSArray<JOYAxes3D *> *)axes3D
{
    return [[NSSet setWithArray:[_axes3D allValues]] allObjects];
}

- (NSArray<JOYHat *> *)hats
{
    return [_hats allValues];
}

- (void)gotReport:(NSData *)report
{
    JOYFullReportElement *element = _fullReportElements[@(*(uint8_t *)report.bytes)];
    if (element) {
        [element updateValue:report];
        
        NSArray<JOYElement *> *subElements = _multiElements[element];
        if (subElements) {
            for (JOYElement *subElement in subElements) {
                [self _elementChanged:subElement];
            }
        }
    }
    dispatch_async(_rumbleQueue, ^{
        [self updateRumble];
    });
}

- (void)elementChanged:(IOHIDElementRef)element
{
    JOYElement *_element = _iokitToJOY[@(IOHIDElementGetCookie(element))];
    if (_element) {
        [self _elementChanged:_element];
    }
    else {
        //NSLog(@"Unhandled usage %x (Cookie: %x, Usage: %x)", IOHIDElementGetUsage(element), IOHIDElementGetCookie(element), IOHIDElementGetUsage(element));
    }
}

- (void)_elementChanged:(JOYElement *)element
{
    if (element == _connectedElement) {
        bool old = self.connected;
        _logicallyConnected = _connectedElement.value != _connectedElement.min;
        if (!old && self.connected) {
            for (id<JOYListener> listener in listeners) {
                if ([listener respondsToSelector:@selector(controllerConnected:)]) {
                    [listener controllerConnected:self];
                }
            }
        }
        else if (old && !self.connected) {
            [_parent breakApart];
            for (id<JOYListener> listener in listeners) {
                if ([listener respondsToSelector:@selector(controllerDisconnected:)]) {
                    [listener controllerDisconnected:self];
                }
            }
        }
    }
    
    if (!self.connected) return;
    {
        JOYButton *button = _buttons[element];
        if (button) {
            if ([button updateState]) {
                for (id<JOYListener> listener in listeners) {
                    if ([listener respondsToSelector:@selector(controller:buttonChangedState:)]) {
                        [listener controller:_parent ?: self buttonChangedState:button];
                    }
                }
            }
            return;
        }
    }
    

    {
        JOYAxis *axis = _axes[element];
        if (axis) {
            if ([axis updateState])  {
                for (id<JOYListener> listener in listeners) {
                    if ([listener respondsToSelector:@selector(controller:movedAxis:)]) {
                        [listener controller:_parent ?: self movedAxis:axis];
                    }
                }
                JOYEmulatedButton *button = _axisEmulatedButtons[@(axis.uniqueID & 0xFFFFFFFF)]; // Mask the combined prefix away
                if ([button updateStateFromAxis:axis]) {
                    for (id<JOYListener> listener in listeners) {
                        if ([listener respondsToSelector:@selector(controller:buttonChangedState:)]) {
                            [listener controller:_parent ?: self buttonChangedState:button];
                        }
                    }
                }
            }
            return;
        }
    }
    
    {
        JOYAxes2D *axes = _axes2D[element];
        if (axes) {
            if ([axes updateState]) {
                for (id<JOYListener> listener in listeners) {
                    if ([listener respondsToSelector:@selector(controller:movedAxes2D:)]) {
                        [listener controller:_parent ?: self movedAxes2D:axes];
                    }
                }
                NSArray <JOYEmulatedButton *> *buttons = _axes2DEmulatedButtons[@(axes.uniqueID & 0xFFFFFFFF)]; // Mask the combined prefix away
                for (JOYEmulatedButton *button in buttons) {
                    if ([button updateStateFromAxes2D:axes]) {
                        for (id<JOYListener> listener in listeners) {
                            if ([listener respondsToSelector:@selector(controller:buttonChangedState:)]) {
                                [listener controller:_parent ?: self buttonChangedState:button];
                            }
                        }
                    }
                }
            }
            return;
        }
    }
    
    {
        JOYAxes3D *axes = _axes3D[element];
        if (axes) {
            if ([axes updateState]) {
                for (id<JOYListener> listener in listeners) {
                    if ([listener respondsToSelector:@selector(controller:movedAxes3D:)]) {
                        [listener controller:_parent ?: self movedAxes3D:axes];
                    }
                }
            }
            return;
        }
    }
    
    {
        JOYHat *hat = _hats[element];
        if (hat) {
            if ([hat updateState]) {
                for (id<JOYListener> listener in listeners) {
                    if ([listener respondsToSelector:@selector(controller:movedHat:)]) {
                        [listener controller:_parent ?: self movedHat:hat];
                    }
                }
                
                NSArray <JOYEmulatedButton *> *buttons = _hatEmulatedButtons[@(hat.uniqueID & 0xFFFFFFFF)]; // Mask the combined prefix away
                for (JOYEmulatedButton *button in buttons) {
                    if ([button updateStateFromHat:hat]) {
                        for (id<JOYListener> listener in listeners) {
                            if ([listener respondsToSelector:@selector(controller:buttonChangedState:)]) {
                                [listener controller:_parent ?: self buttonChangedState:button];
                            }
                        }
                    }
                }
            }
            return;
        }
    }
}

- (void)disconnected
{
    _physicallyConnected = false;
    [_parent breakApart];
    if (_logicallyConnected && [exposedControllers containsObject:self]) {
        for (id<JOYListener> listener in listeners) {
            if ([listener respondsToSelector:@selector(controllerDisconnected:)]) {
                [listener controllerDisconnected:self];
            }
        }
    }
    [exposedControllers removeObject:self];
    [self setRumbleAmplitude:0];
    dispatch_sync(_rumbleQueue, ^{
        [self updateRumble];
    });
    _device = nil;
}

- (void)sendReport:(NSData *)report
{
    if (!report.length) return;
    if (!_device) return;
    if (_deviceCantSendReports) return;
    /* Some Macs fail to send reports to some devices, specifically the DS3, returning the bogus(?) error code 1 after
       freezing for 5 seconds. Stop sending reports if that's the case. */
    if (IOHIDDeviceSetReport(_device, kIOHIDReportTypeOutput, *(uint8_t *)report.bytes, report.bytes, report.length) == 1) {
        _deviceCantSendReports = true;
        NSLog(@"This Mac appears to be incapable of sending output reports to %@", self);
    }
}

- (void) sendDualSenseOutput
{
    if (_isUSBDualSense) {
        [self sendReport:[NSData dataWithBytes:&_lastVendorSpecificOutput.dualsenseOutput.reportIDOnUSB length:_lastVendorSpecificOutput.dualsenseOutput.bluetoothSpecific - &_lastVendorSpecificOutput.dualsenseOutput.reportIDOnUSB]];
        return;
    }
    _lastVendorSpecificOutput.dualsenseOutput.sequence += 0x10;
    static const uint32_t table[] = {
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
        0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
        0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
        0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
        0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
        0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
        0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
        0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
        0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
        0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
        0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
        0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
        0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
        0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
        0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
        0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
        0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
        0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
        0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
        0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
        0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
        0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
        0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
        0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
        0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
        0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
        0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
        0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
        0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
        0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
        0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
        0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
        0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
        0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
        0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
        0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
        0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
        0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
    };
    
    const uint8_t *byte = (void *)&_lastVendorSpecificOutput.dualsenseOutput;
    uint32_t size = sizeof(_lastVendorSpecificOutput.dualsenseOutput) - 4;
    uint32_t ret = 0xFFFFFFFF;
    ret = table[(ret ^ 0xA2) & 0xFF] ^ (ret >> 8);

    while (size--) {
        ret = table[(ret ^ *byte++) & 0xFF] ^ (ret >> 8);
    }
    
    _lastVendorSpecificOutput.dualsenseOutput.crc32 = ~ret;
    
    [self sendReport:[NSData dataWithBytes:&_lastVendorSpecificOutput.dualsenseOutput length:sizeof(_lastVendorSpecificOutput.dualsenseOutput)]];
}

- (uint8_t)LEDMaskForPlayer:(unsigned)player
{
    if (_isDualShock3) {
        return 2 << player;
    }
    if (_isDualSense) {
        switch (player) {
            case 0: return 0x04;
            case 1: return 0x0A;
            case 2: return 0x15;
            case 3: return 0x1B;
            default: return 0;
        }
    }
    return 1 << player;
}

- (void)setPlayerLEDs:(uint8_t)mask
{
    if (mask == _playerLEDs) {
        return;
    }
    _playerLEDs = mask;
    if (_isSwitch) {
        _lastVendorSpecificOutput.switchPacket.reportID = 0x1; // Rumble and LEDs
        _lastVendorSpecificOutput.switchPacket.sequence++;
        _lastVendorSpecificOutput.switchPacket.sequence &= 0xF;
        _lastVendorSpecificOutput.switchPacket.command = 0x30; // LED
        _lastVendorSpecificOutput.switchPacket.commandData[0] = mask & 0xF;
        [self sendReport:[NSData dataWithBytes:&_lastVendorSpecificOutput.switchPacket length:sizeof(_lastVendorSpecificOutput.switchPacket)]];
    }
    else if (_isDualShock3) {
        _lastVendorSpecificOutput.ds3Output.reportID = 1;
        _lastVendorSpecificOutput.ds3Output.ledsEnabled = (mask  & 0x1F);
        [self sendReport:[NSData dataWithBytes:&_lastVendorSpecificOutput.ds3Output length:sizeof(_lastVendorSpecificOutput.ds3Output)]];
    }
    else if (_isDualSense) {
        _lastVendorSpecificOutput.dualsenseOutput.playerLEDs = mask & 0x1F;
        [self sendDualSenseOutput];
    }
}

- (void)updateRumble
{
    if (!self.connected) {
        return;
    }
    if (!_rumbleElement && !_isSwitch && !_isDualShock3 && !_isDualSense) {
        return;
    }
    if (_rumbleElement.max == 1 && _rumbleElement.min == 0) {
        double ampToSend = _rumbleCounter < round(_rumbleAmplitude * PWM_RESOLUTION);
        if (ampToSend != _sentRumbleAmp) {
            [_rumbleElement setValue:ampToSend];
            _sentRumbleAmp = ampToSend;
        }
        _rumbleCounter += round(_rumbleAmplitude * PWM_RESOLUTION);
        if (_rumbleCounter >= PWM_RESOLUTION) {
            _rumbleCounter -= PWM_RESOLUTION;
        }
    }
    else {
        if (_rumbleAmplitude == _sentRumbleAmp) {
            return;
        }
        _sentRumbleAmp = _rumbleAmplitude;
        if (_isSwitch) {
            double frequency = 144;
            double amp = _rumbleAmplitude;
            
            uint8_t highAmp = amp * 0x64;
            uint8_t lowAmp = amp * 0x32 + 0x40;
            if (frequency < 0) frequency = 0;
            if (frequency > 1252) frequency = 1252;
            uint8_t encodedFrequency = (uint8_t)round(log2(frequency / 10.0) * 32.0);
            
            uint16_t highFreq = (encodedFrequency - 0x60) * 4;
            uint8_t lowFreq = encodedFrequency - 0x40;
            
            //if (frequency < 82 || frequency > 312) {
            if (amp) {
                highAmp = 0;
            }
            
            if (frequency < 40 || frequency > 626) {
                lowAmp = 0;
            }
            
            _lastVendorSpecificOutput.switchPacket.rumbleData[0] = _lastVendorSpecificOutput.switchPacket.rumbleData[4] = highFreq & 0xFF;
            _lastVendorSpecificOutput.switchPacket.rumbleData[1] = _lastVendorSpecificOutput.switchPacket.rumbleData[5] = (highAmp << 1) + ((highFreq >> 8) & 0x1);
            _lastVendorSpecificOutput.switchPacket.rumbleData[2] = _lastVendorSpecificOutput.switchPacket.rumbleData[6] = lowFreq;
            _lastVendorSpecificOutput.switchPacket.rumbleData[3] = _lastVendorSpecificOutput.switchPacket.rumbleData[7] = lowAmp;
            
            
            _lastVendorSpecificOutput.switchPacket.reportID = 0x10; // Rumble only
            _lastVendorSpecificOutput.switchPacket.sequence++;
            _lastVendorSpecificOutput.switchPacket.sequence &= 0xF;
            _lastVendorSpecificOutput.switchPacket.command = 0; // LED
            [self sendReport:[NSData dataWithBytes:&_lastVendorSpecificOutput.switchPacket length:sizeof(_lastVendorSpecificOutput.switchPacket)]];
        }
        else if (_isDualShock3) {
            _lastVendorSpecificOutput.ds3Output.reportID = 1;
            _lastVendorSpecificOutput.ds3Output.rumbleLeftDuration = _lastVendorSpecificOutput.ds3Output.rumbleRightDuration = _rumbleAmplitude? 0xFF : 0;
            _lastVendorSpecificOutput.ds3Output.rumbleLeftStrength = _lastVendorSpecificOutput.ds3Output.rumbleRightStrength = round(_rumbleAmplitude * 0xFF);
            [self sendReport:[NSData dataWithBytes:&_lastVendorSpecificOutput.ds3Output length:sizeof(_lastVendorSpecificOutput.ds3Output)]];
        }
        else if (_isDualSense) {
            _lastVendorSpecificOutput.dualsenseOutput.rumbleLeftStrength = round(_rumbleAmplitude * _rumbleAmplitude * 0xFF);
            _lastVendorSpecificOutput.dualsenseOutput.rumbleRightStrength = _rumbleAmplitude > 0.25 ? round(pow(_rumbleAmplitude - 0.25, 2) * 0xFF) : 0;
            [self sendDualSenseOutput];
        }
        else {
            [_rumbleElement setValue:_rumbleAmplitude * (_rumbleElement.max - _rumbleElement.min) + _rumbleElement.min];
        }
    }
}

- (void)setRumbleAmplitude:(double)amp /* andFrequency: (double)frequency */
{
    if (amp < 0) amp = 0;
    if (amp > 1) amp = 1;
    _rumbleAmplitude = amp;
}

- (bool)isConnected
{
    return _logicallyConnected && _physicallyConnected;
}

- (NSArray<JOYInput *> *)allInputs
{
    NSMutableArray<JOYInput *> *ret = [NSMutableArray array];
    [ret addObjectsFromArray:self.buttons];
    [ret addObjectsFromArray:self.axes];
    [ret addObjectsFromArray:self.axes2D];
    [ret addObjectsFromArray:self.axes3D];
    [ret addObjectsFromArray:self.hats];
    return ret;
}

- (void)setusesHorizontalJoyConGrip:(bool)usesHorizontalJoyConGrip
{
    if (usesHorizontalJoyConGrip == _usesHorizontalJoyConGrip) return; // Nothing to do
    _usesHorizontalJoyConGrip = usesHorizontalJoyConGrip;
    switch (self.joyconType) {
        case JOYJoyConTypeLeft:
        case JOYJoyConTypeRight: {
            NSArray <JOYButton *> *buttons = _buttons.allValues;  // not self.buttons to skip emulated buttons
            if (!usesHorizontalJoyConGrip) {
                for (JOYAxes2D *axes in self.axes2D) {
                    axes.rotation = 0;
                }
                for (JOYAxes3D *axes in self.axes3D) {
                    axes.rotation = 0;
                }
                for (JOYButton *button in buttons) {
                    button.usage = button.originalUsage;
                }
                return;
            }
            for (JOYAxes2D *axes in self.axes2D) {
                axes.rotation = self.joyconType == JOYJoyConTypeLeft? -1 : 1;
            }
            for (JOYAxes3D *axes in self.axes3D) {
                axes.rotation = self.joyconType == JOYJoyConTypeLeft? -1 : 1;
            }
            if (self.joyconType == JOYJoyConTypeLeft) {
                for (JOYButton *button in buttons) {
                    switch (button.originalUsage) {
                        case JOYButtonUsageDPadLeft: button.usage = JOYButtonUsageB; break;
                        case JOYButtonUsageDPadRight: button.usage = JOYButtonUsageX; break;
                        case JOYButtonUsageDPadUp: button.usage = JOYButtonUsageY; break;
                        case JOYButtonUsageDPadDown: button.usage = JOYButtonUsageA; break;
                        default: button.usage = button.originalUsage; break;
                    }
                }
            }
            else {
                for (JOYButton *button in buttons) {
                    switch (button.originalUsage) {
                        case JOYButtonUsageY: button.usage = JOYButtonUsageX; break;
                        case JOYButtonUsageA: button.usage = JOYButtonUsageB; break;
                        case JOYButtonUsageX: button.usage = JOYButtonUsageA; break;
                        case JOYButtonUsageB: button.usage = JOYButtonUsageY; break;
                        default: button.usage = button.originalUsage; break;
                    }
                }
            }
        }
        default:
            return;
    }
}

+ (void)controllerAdded:(IOHIDDeviceRef) device
{
    NSString *name = (__bridge NSString *)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey));
    NSDictionary *hacks = hacksByName[name];
    if (!hacks) {
        hacks = hacksByManufacturer[(__bridge NSNumber *)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey))];
    }
    NSArray *filters = hacks[JOYReportIDFilters];
    JOYController *controller = nil;
    if (filters) {
        controller = [[JOYMultiplayerController alloc] initWithDevice:device
                                                      reportIDFilters:filters
                                                                hacks:hacks];
    }
    else {
        controller = [[JOYController alloc] initWithDevice:device hacks:hacks];
    }
        
    [controllers setObject:controller forKey:[NSValue valueWithPointer:device]];
}

+ (void)controllerRemoved:(IOHIDDeviceRef) device
{
    [[controllers objectForKey:[NSValue valueWithPointer:device]] disconnected];
    [controllers removeObjectForKey:[NSValue valueWithPointer:device]];
}

+ (NSArray<JOYController *> *)allControllers
{
    return exposedControllers;
}

+ (void)load
{
#import "ControllerConfiguration.inc"
}

+(void)registerListener:(id<JOYListener>)listener
{
    [listeners addObject:listener];
}

+(void)unregisterListener:(id<JOYListener>)listener
{
    [listeners removeObject:listener];
}

+ (void)startOnRunLoop:(NSRunLoop *)runloop withOptions: (NSDictionary *)options
{
    axes2DEmulateButtons = [options[JOYAxes2DEmulateButtonsKey] boolValue];
    hatsEmulateButtons = [options[JOYHatsEmulateButtonsKey] boolValue];
    
    controllers = [NSMutableDictionary dictionary];
    exposedControllers = [NSMutableArray array];
    NSArray *array = @[
        CreateHIDDeviceMatchDictionary(kHIDPage_GenericDesktop, kHIDUsage_GD_Joystick),
        CreateHIDDeviceMatchDictionary(kHIDPage_GenericDesktop, kHIDUsage_GD_GamePad),
        CreateHIDDeviceMatchDictionary(kHIDPage_GenericDesktop, kHIDUsage_GD_MultiAxisController),
        @{@kIOHIDDeviceUsagePageKey: @(kHIDPage_Game)},
    ];

    listeners = [NSMutableSet set];
    static IOHIDManagerRef manager = nil;
    if (manager) {
        CFRelease(manager); // Stop the previous session
    }
    manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    
    if (!manager) return;
    if (IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone)) {
        CFRelease(manager);
        return;
    }
    
    IOHIDManagerSetDeviceMatchingMultiple(manager, (__bridge CFArrayRef)array);
    IOHIDManagerRegisterDeviceMatchingCallback(manager, HIDDeviceAdded, NULL);
    IOHIDManagerRegisterDeviceRemovalCallback(manager, HIDDeviceRemoved, NULL);
    IOHIDManagerScheduleWithRunLoop(manager, [runloop getCFRunLoop], kCFRunLoopDefaultMode);
}

- (void)dealloc
{
    if (_device) {
        CFRelease(_device);
        _device = NULL;
    }
}
@end


@implementation JOYCombinedController
- (instancetype)initWithChildren:(NSArray<JOYController *> *)children
{
    self = [super init];
    // Sorting makes the device name and unique id consistent
    _children = [children sortedArrayUsingComparator:^NSComparisonResult(JOYController *a, JOYController *b) {
        return [a.uniqueID compare:b.uniqueID];
    }];
    
    if (_children.count == 0) return nil;
    
    for (JOYController *child in _children) {
        if (child.combinedControllerType != JOYControllerCombinedTypeSingle) {
            NSLog(@"Cannot combine non-single controller %@", child);
            return nil;
        }
        if (![exposedControllers containsObject:child]) {
            NSLog(@"Cannot combine unexposed controller %@", child);
            return nil;
        }
    }
    
    unsigned index = 0;
    for (JOYController *child in _children) {
        for (id<JOYListener> listener in listeners) {
            if ([listener respondsToSelector:@selector(controllerDisconnected:)]) {
                [listener controllerDisconnected:child];
            }
        }
        child->_parent = self;
        for (JOYInput *input in child.allInputs) {
            input.combinedIndex = index;
        }
        index++;
        [exposedControllers removeObject:child];
    }
    
    [exposedControllers addObject:self];
    for (id<JOYListener> listener in listeners) {
        if ([listener respondsToSelector:@selector(controllerConnected:)]) {
            [listener controllerConnected:self];
        }
    }
    
    return self;
}

- (void)breakApart
{
    if (![exposedControllers containsObject:self]) {
        // Already broken apart
        return;
    }
    
    [exposedControllers removeObject:self];
    for (id<JOYListener> listener in listeners) {
        if ([listener respondsToSelector:@selector(controllerDisconnected:)]) {
            [listener controllerDisconnected:self];
        }
    }

    for (JOYController *child in _children) {
        child->_parent = nil;
        for (JOYInput *input in child.allInputs) {
            input.combinedIndex = 0;
        }
        if (!child.connected) break;
        [exposedControllers addObject:child];
        for (id<JOYListener> listener in listeners) {
            if ([listener respondsToSelector:@selector(controllerConnected:)]) {
                [listener controllerConnected:child];
            }
        }
    }
}

- (NSString *)deviceName
{
    NSString *ret = nil;
    for (JOYController *child in _children) {
        if (ret) {
            ret = [ret stringByAppendingFormat:@" + %@", child.deviceName];
        }
        else {
            ret = child.deviceName;
        }
    }
    return ret;
}

- (NSString *)uniqueID
{
    NSString *ret = nil;
    for (JOYController *child in _children) {
        if (ret) {
            ret = [ret stringByAppendingFormat:@"+%@", child.uniqueID];
        }
        else {
            ret = child.uniqueID;
        }
    }
    return ret;
}

- (JOYControllerCombinedType)combinedControllerType
{
    return JOYControllerCombinedTypeCombined;
}

- (NSArray<JOYButton *> *)buttons
{
    NSArray<JOYButton *> *ret = nil;
    for (JOYController *child in _children) {
        if (ret) {
            ret = [ret arrayByAddingObjectsFromArray:child.buttons];
        }
        else {
            ret = child.buttons;
        }
    }
    return ret;
}

- (NSArray<JOYAxis *> *)axes
{
    NSArray<JOYAxis *> *ret = nil;
    for (JOYController *child in _children) {
        if (ret) {
            ret = [ret arrayByAddingObjectsFromArray:child.axes];
        }
        else {
            ret = child.axes;
        }
    }
    return ret;
}

- (NSArray<JOYAxes2D *> *)axes2D
{
    NSArray<JOYAxes2D *> *ret = nil;
    for (JOYController *child in _children) {
        if (ret) {
            ret = [ret arrayByAddingObjectsFromArray:child.axes2D];
        }
        else {
            ret = child.axes2D;
        }
    }
    return ret;
}

- (NSArray<JOYAxes3D *> *)axes3D
{
    NSArray<JOYAxes3D *> *ret = nil;
    for (JOYController *child in _children) {
        if (ret) {
            ret = [ret arrayByAddingObjectsFromArray:child.axes3D];
        }
        else {
            ret = child.axes3D;
        }
    }
    return ret;
}

- (NSArray<JOYHat *> *)hats
{
    NSArray<JOYHat *> *ret = nil;
    for (JOYController *child in _children) {
        if (ret) {
            ret = [ret arrayByAddingObjectsFromArray:child.hats];
        }
        else {
            ret = child.hats;
        }
    }
    return ret;
}

- (void)setRumbleAmplitude:(double)amp
{
    for (JOYController *child in _children) {
        [child setRumbleAmplitude:amp];
    }
}

- (void)setPlayerLEDs:(uint8_t)mask
{
    // Mask is actually just the player ID in a combined controller to
    // allow combining controllers with different LED layouts
    for (JOYController *child in _children) {
        [child setPlayerLEDs:[child LEDMaskForPlayer:mask]];
    }
}

- (uint8_t)LEDMaskForPlayer:(unsigned)player
{
    return player;
}

- (bool)isConnected
{
    if (![exposedControllers containsObject:self]) {
         // Controller was broken apart
        return false;
    }
    
    for (JOYController *child in _children) {
        if (!child.isConnected) {
            return false; // Should never happen
        }
    }
    
    return true;
}

- (JOYJoyConType)joyconType
{
    if (_children.count != 2) return JOYJoyConTypeNone;
    if (_children[0].joyconType == JOYJoyConTypeLeft &&
        _children[1].joyconType == JOYJoyConTypeRight) {
        return JOYJoyConTypeDual;
    }
    
    if (_children[1].joyconType == JOYJoyConTypeLeft &&
        _children[0].joyconType == JOYJoyConTypeRight) {
        return JOYJoyConTypeDual;
    }
     return JOYJoyConTypeNone;
}

@end

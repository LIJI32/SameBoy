#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import <UserNotifications/UserNotifications.h>
#import "GBBackgroundView.h"

// "Backport" headers so we can build against old SDKs

#if !__has_include(<UIKit/UISliderTrackConfiguration.h>)
/* Building with older SDKs */

typedef NS_ENUM(NSInteger, UIMenuSystemElementGroupPreference) {
    UIMenuSystemElementGroupPreferenceAutomatic = 0,
    UIMenuSystemElementGroupPreferenceRemoved,
    UIMenuSystemElementGroupPreferenceIncluded,
};

API_AVAILABLE(ios(19.0))
@interface UIMainMenuSystemConfiguration : NSObject <NSCopying>
@property (nonatomic, assign) UIMenuSystemElementGroupPreference newScenePreference;
@property (nonatomic, assign) UIMenuSystemElementGroupPreference documentPreference;
@property (nonatomic, assign) UIMenuSystemElementGroupPreference printingPreference;
@property (nonatomic, assign) UIMenuSystemElementGroupPreference findingPreference;
@property (nonatomic, assign) UIMenuSystemElementGroupPreference toolbarPreference;
@property (nonatomic, assign) UIMenuSystemElementGroupPreference sidebarPreference;
@property (nonatomic, assign) UIMenuSystemElementGroupPreference inspectorPreference;
@property (nonatomic, assign) UIMenuSystemElementGroupPreference textFormattingPreference;
@end

API_AVAILABLE(ios(19.0))
@interface UIMainMenuSystem : UIMenuSystem
@property (class, nonatomic, readonly) UIMainMenuSystem *sharedSystem;
- (void)setBuildConfiguration:(UIMainMenuSystemConfiguration *)configuration buildHandler:(void(^)(NSObject<UIMenuBuilder> *builder))buildHandler;
@end

API_AVAILABLE(ios(19.0))
@interface NSObject(UIMenuBuilder)
- (void)insertElements:(NSArray<UIMenuElement *> *)childElements atStartOfMenuForIdentifier:(UIMenuIdentifier)parentIdentifier;
@end

#endif

#if !__has_include(<UIKit/UIHinge.h>)
API_AVAILABLE(ios(27.1))
typedef NS_ENUM(NSInteger, UIHingeStatus) {
    UIHingeStatusUnknown = 0,
    UIHingeStatusClosed = 1,
    UIHingeStatusPartiallyOpen = 2,
    UIHingeStatusFullyOpen = 3,
};

API_AVAILABLE(ios(27.1))
@interface UIHinge : NSObject <NSCopying>
@property (nonatomic, readonly) UIHingeStatus status;
@property (nonatomic, readonly) CGFloat angle;
@end

API_AVAILABLE(ios(27.1))
@interface UIHingeInteractionUpdate : NSObject <NSCopying>
@property (nonatomic, readonly, copy) UIHinge *hinge;
@end

API_AVAILABLE(ios(27.1))
@interface UIHingeInteraction : NSObject <UIInteraction>
- (instancetype)initWithUpdateHandler:(void(^)(UIHingeInteraction *, UIHingeInteractionUpdate *))updateHandler NS_DESIGNATED_INITIALIZER;
@end
#endif


typedef enum {
    GBRunModeNormal,
    GBRunModeTurbo,
    GBRunModeRewind,
    GBRunModePaused,
    GBRunModeUnderclock,
} GBRunMode;

@interface GBViewController : UIViewController <UIApplicationDelegate,
                                                AVCaptureVideoDataOutputSampleBufferDelegate,
                                                UNUserNotificationCenterDelegate>
@property (nonatomic, strong) UIWindow *window;
- (void)reset;
- (void)openLibrary;
- (void)start;
- (void)stop;
- (void)changeModel;
- (void)openStates;
- (void)openSettings;
- (void)showAbout;
- (void)openConnectMenu;
- (void)openCheats;
- (void)emptyPrinterFeed;
- (void)saveStateToFile:(NSString *)file;
- (bool)loadStateFromFile:(NSString *)file;
- (bool)handleOpenURLs:(NSArray <NSURL *> *)urls
           openInPlace:(bool)inPlace;
- (void)dismissViewController;
@property (nonatomic) GBRunMode runMode;
@property (readonly) UIHingeStatus hingeStatus API_AVAILABLE(ios(27.1));
@property (readonly) GBBackgroundView *backgroundView;
@end

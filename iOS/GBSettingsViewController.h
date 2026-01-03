#import <UIKit/UIKit.h>
#import <Core/gb.h>
#import "GCControllerGetElements.h"
#import "GBTheme.h"

typedef enum {
    GBRight,
    GBLeft,
    GBUp,
    GBDown,
    GBA,
    GBB,
    GBSelect,
    GBStart,
    GBTurbo,
    GBRewind,
    GBUnderclock,
    GBRapidA,
    GBRapidB,
    GBSaveState1,
    GBLoadState1,
    GBOpenMenu,
    GBReset,
    GBUnusedButton = 0xFF,
} GBButton;

@interface GBSettingsViewController : UITableViewController
+ (UIViewController *)settingsViewControllerWithLeftButton:(UIBarButtonItem *)button;
+ (GBButton)controller:(GCController *)controller convertUsageToButton:(GBControllerUsage)usage;
+ (GBTheme *)themeNamed:(NSString *)name;
@end

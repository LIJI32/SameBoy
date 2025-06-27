#import <UIKit/UIKit.h>
#import <Core/gb.h>
#import "GCExtendedGamepad+AllElements.h"
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
    // GBHotkey1, // Todo
    // GBHotkey2, // Todo
    GBUnusedButton = 0xFF,
} GBButton;

@interface GBSettingsViewController : UITableViewController
+ (UIViewController *)settingsViewControllerWithLeftButton:(UIBarButtonItem *)button;
+ (GBButton)controller:(GCController *)controller convertUsageToButton:(GBControllerUsage)usage;
+ (GBTheme *)themeNamed:(NSString *)name;
@end

#import "GBLibraryViewController.h"
#import "GBLoadROMTableViewController.h"
#import "GBHubViewController.h"
#import "GBViewController.h"

@implementation GBLibraryViewController

+ (UIViewController *)wrapViewController:(UIViewController *)controller
{
    UINavigationController *ret = [[UINavigationController alloc] initWithRootViewController:controller];
    UIBarButtonItem *close = [[UIBarButtonItem alloc] initWithTitle:@"Close"
                                                              style:UIBarButtonItemStylePlain
                                                             target:[UIApplication sharedApplication].delegate
                                                             action:@selector(dismissViewController)];
    [ret.visibleViewController.navigationItem setLeftBarButtonItem:close];
    return ret;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.viewControllers = @[
        [self.class wrapViewController:[[GBLoadROMTableViewController alloc] init]],
        [self.class wrapViewController:[[GBHubViewController alloc] init]],
    ];
    if (@available(iOS 13.0, *)) {
        UIEdgeInsets insets = [UIApplication sharedApplication].keyWindow.safeAreaInsets;
        bool hasHomeButton = insets.bottom == 0;
        bool isPad = [UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad;
        NSString *symbol = isPad? @"ipad" : @"iphone";
        if (hasHomeButton) {
            symbol = [symbol stringByAppendingString:@".homebutton"];
        }
        else if (!isPad) {
            if (@available(iOS 16.1, *)) {
                if (MAX(insets.left, MAX(insets.right, MAX(insets.top, insets.bottom))) > 51) {
                    symbol = @"iphone.gen3";
                }
                else {
                    symbol = @"iphone.gen2";
                }
            }
        }
        self.viewControllers[0].tabBarItem.image = [UIImage systemImageNamed:symbol] ?: [UIImage systemImageNamed:@"folder.fill"];
        self.viewControllers[1].tabBarItem.image = [UIImage systemImageNamed:@"globe"];
    }
    else {
        self.viewControllers[0].tabBarItem.image = [UIImage imageNamed:@"FolderTemplate"];
        self.viewControllers[1].tabBarItem.image = [UIImage imageNamed:@"GlobeTemplate"];
    }
}

@end

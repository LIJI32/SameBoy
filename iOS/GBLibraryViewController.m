#import "GBLibraryViewController.h"
#import "GBROMViewController.h"
#ifdef APPSTORE
#import "GBCloudROMViewController.h"
#import "GBWatchManager.h"
#endif
#import "GBViewController.h"
#import "GBROMManager.h"

#ifdef APPSTORE
@interface GBLibraryViewController(Animation) <UIViewControllerAnimatedTransitioning, UITabBarDelegate>
@end
#endif

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

#ifdef APPSTORE
- (void)viewDidLoad
{
    [super viewDidLoad];
    self.delegate = (id)self;
    self.viewControllers = @[
        [self.class wrapViewController:[[GBROMViewController alloc] init]],
        [self.class wrapViewController:[[GBCloudROMViewController alloc] init]],
    ];
    if ([GBWatchManager sharedManager].isPaired) {
        NSMutableArray<UIViewController *> *viewControllers = self.viewControllers.mutableCopy;
        [viewControllers insertObject:[self.class wrapViewController:[[GBROMViewController alloc] initForWatch]] atIndex:2],
        self.viewControllers = viewControllers;
        viewControllers[2].tabBarItem.image = [UIImage systemImageNamed:@"applewatch"] ?: [UIImage systemImageNamed:@"clock"];
    }
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
    self.viewControllers[1].tabBarItem.image = [UIImage systemImageNamed:@"icloud"];
    if ([[GBROMManager sharedManager].currentROM hasPrefix:@"icloud/"]) {
        self.selectedIndex = 1;
    }
}
#else
- (instancetype)init
{
    return (GBLibraryViewController *)[self.class wrapViewController:[[GBROMViewController alloc] init]];
}
#endif

#ifdef APPSTORE
- (NSTimeInterval)transitionDuration:(id <UIViewControllerContextTransitioning>)transitionContext
{
    return 0.25;
}

- (void)animateTransition:(id <UIViewControllerContextTransitioning>)transitionContext
{
    UIViewController *fromVC = [transitionContext viewControllerForKey:UITransitionContextFromViewControllerKey];
    UIViewController *toVC = [transitionContext viewControllerForKey:UITransitionContextToViewControllerKey];
    
    UIView *toView = toVC.view;
    UIView *fromView = fromVC.view;
    
    UIView *containerView = [transitionContext containerView];
    [containerView addSubview:toView];
    if ([self.viewControllers indexOfObject:fromVC] < [self.viewControllers indexOfObject:toVC]) {
        CGRect frame = [transitionContext finalFrameForViewController:toVC];
        frame.origin.x += frame.size.width;
        toView.frame = frame;
        
        [UIView animateWithDuration:[self transitionDuration:transitionContext]
                              delay:0.0
                            options:UIViewAnimationOptionCurveEaseInOut
                         animations:^{
            toView.frame = [transitionContext finalFrameForViewController:toVC];
        }
                         completion:^(BOOL finished) {
            toView.frame = [transitionContext finalFrameForViewController:toVC];
            [fromView removeFromSuperview];
            [transitionContext completeTransition:YES];
        }];
    }
    else {
        [containerView bringSubviewToFront:fromView];
        toView.frame = [transitionContext finalFrameForViewController:toVC];
        CGRect frame = fromView.frame;
        frame.origin.x += frame.size.width;
        
        [UIView animateWithDuration:[self transitionDuration:transitionContext]
                              delay:0.0
                            options:UIViewAnimationOptionCurveEaseInOut
                         animations:^{
            fromView.frame = frame;
        }
                         completion:^(BOOL finished) {
            fromView.frame = frame;
            [fromView removeFromSuperview];
            [transitionContext completeTransition:YES];
        }];
    }
}

- (id <UIViewControllerAnimatedTransitioning>)tabBarController:(UITabBarController *)tabBarController
            animationControllerForTransitionFromViewController:(UIViewController *)fromVC
                                              toViewController:(UIViewController *)toVC
{
    return _enableAnimations? self : nil;
}
#endif

@end

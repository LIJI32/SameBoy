#import "GBThemePreviewController.h"
#import "GBVerticalLayout.h"
#import "GBHorizontalLayout.h"
#import "GBBackgroundView.h"
#ifdef APPSTORE
#import "GBSubscriptionManager.h"
#import "GBSubscriptionViewController.h"
#endif

@implementation GBThemePreviewController
{
    GBHorizontalLayout *_horizontalLayout;
    GBVerticalLayout *_verticalLayout;
    GBBackgroundView *_backgroundView;
    bool _isPaid;
}

- (instancetype)initWithTheme:(GBTheme *)theme isPaid:(bool)paid
{
    self = [super init];
    _horizontalLayout = [[GBHorizontalLayout alloc] initWithTheme:theme];
    _verticalLayout = [[GBVerticalLayout alloc] initWithTheme:theme];
    _isPaid = paid;
    return self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    _backgroundView = [[GBBackgroundView alloc] initWithLayout:_verticalLayout];
    self.view.backgroundColor = _verticalLayout.theme.backgroundGradientBottom;
    [self.view addSubview:_backgroundView];
    [_backgroundView enterPreviewMode:true];
    _backgroundView.autoresizingMask = UIViewAutoresizingFlexibleRightMargin | UIViewAutoresizingFlexibleBottomMargin;
    
    [self willRotateToInterfaceOrientation:self.interfaceOrientation duration:0];
    [self.view addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self
                                                                            action:@selector(showPopup)]];
}

- (UIModalPresentationStyle)modalPresentationStyle
{
    return UIModalPresentationFullScreen;
}

- (void)willRotateToInterfaceOrientation:(UIInterfaceOrientation)orientation duration:(NSTimeInterval)duration
{
    GBLayout *layout = _horizontalLayout;
    if (orientation == UIInterfaceOrientationPortrait || orientation == UIInterfaceOrientationPortraitUpsideDown) {
        layout = _verticalLayout;
    }
    _backgroundView.frame = [layout viewRectForOrientation:orientation];
    _backgroundView.layout = layout;
}

- (void)showPopup
{
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Apply “%@” as the current theme?", _verticalLayout.theme.name]
                                                                   message:nil
                                                            preferredStyle:[UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad?
                                              UIAlertControllerStyleAlert : UIAlertControllerStyleActionSheet];
#ifdef APPSTORE
    if (_isPaid && GBSubscriptionManager.defaultManager.state == GBSubscriptionInactive) {
        [alert  addAction:[UIAlertAction actionWithTitle:@"Support SameBoy to Unlock"
                                                   style:UIAlertActionStyleDefault
                                                 handler:^(UIAlertAction *action) {
            UINavigationController *navController = [[UINavigationController alloc] initWithRootViewController:[GBSubscriptionViewController new]];
            UIBarButtonItem *close = [[UIBarButtonItem alloc] initWithTitle:@"Close"
                                                                      style:UIBarButtonItemStylePlain
                                                                     target:self
                                                                     action:@selector(dismissViewController)];
            [navController.visibleViewController.navigationItem setLeftBarButtonItem:close];
            
            [self presentViewController:navController animated:true completion:nil];
        }]];
    }
#else
    if (false) {
        // Not subscription-only themes outside the App Store release
    }
#endif
    else {
        [alert  addAction:[UIAlertAction actionWithTitle:@"Apply Theme"
                                                   style:UIAlertActionStyleDefault
                                                 handler:^(UIAlertAction *action) {
            [[NSUserDefaults standardUserDefaults] setObject:_verticalLayout.theme.name forKey:@"GBInterfaceTheme"];
            [[self presentingViewController] dismissViewControllerAnimated:true completion:nil];
        }]];
    }
    [alert  addAction:[UIAlertAction actionWithTitle:@"Exit Preview"
                                               style:UIAlertActionStyleDefault
                                             handler:^(UIAlertAction *action) {
        [[self presentingViewController] dismissViewControllerAnimated:true completion:nil];
    }]];
    [self presentViewController:alert animated:true completion:^{
        alert.view.superview.userInteractionEnabled = true;
        [alert.view.superview addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self
                                                                                           action:@selector(dismissPopup)]];
        for (UIView *view in alert.view.superview.subviews) {
            if (view.backgroundColor) {
                view.userInteractionEnabled = true;
                [view addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self
                                                                                   action:@selector(dismissPopup)]];

            }
        }
    }];
}

- (void)dismissViewController
{
    [self dismissViewControllerAnimated:true completion:nil];
}

- (void)dismissPopup
{
    [self dismissViewControllerAnimated:true completion:nil];
}

- (UIStatusBarStyle)preferredStatusBarStyle
{
    return _verticalLayout.theme.isDark? UIStatusBarStyleLightContent : UIStatusBarStyleDarkContent;
}

@end

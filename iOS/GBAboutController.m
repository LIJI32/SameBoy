#import "GBAboutController.h"
#ifdef APPSTORE
#import "GBSubscriptionViewController.h"
#endif

@implementation GBAboutController
{
    UILabel *_titleLabel;
    UILabel *_versionLabel;
    UIImageView *_logo;
    UITextView *_licenseView;
    UILabel *_copyrightLabel;
    UIView *_buttonsView;
}

- (UIImage *)buttonImageNamed:(NSString *)name
{
    if (@available(iOS 13.0, *)) {
        return [UIImage systemImageNamed:name withConfiguration:[UIImageSymbolConfiguration configurationWithScale:UIImageSymbolScaleLarge]];
    }
    return nil;
}

- (void)alignButton:(UIButton *)button
{
    if (!button.imageView.image) return;
    CGSize imageSize = button.imageView.frame.size;
    
    button.imageEdgeInsets = UIEdgeInsetsMake(0, (32 - imageSize.width) / 2, 0, 0);
    button.titleEdgeInsets = UIEdgeInsetsMake(0, 32 - imageSize.width, 0, 0);
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    UIVisualEffect *effect = [UIBlurEffect effectWithStyle:(UIBlurEffectStyle)UIBlurEffectStyleProminent];
    self.view = [[UIVisualEffectView alloc] initWithEffect:effect];
    UIView *root = ((UIVisualEffectView *)self.view).contentView;
    
    _titleLabel = [[UILabel alloc] init];
    _titleLabel.text = @"SameBoy";
    _titleLabel.font = [UIFont systemFontOfSize:34 weight:UIFontWeightHeavy];
    [root addSubview:_titleLabel];

    _versionLabel = [[UILabel alloc] init];
    _versionLabel.text = @"Version " GB_VERSION;
    _versionLabel.font = [UIFont systemFontOfSize:24 weight:UIFontWeightLight];
    [root addSubview:_versionLabel];

    _logo = [[UIImageView alloc] init];
    _logo.image = [UIImage imageNamed:@"logo"];
    _logo.contentMode = UIViewContentModeCenter;
    [root addSubview:_logo];

    _licenseView = [[UITextView alloc] init];
    NSData *data = [NSData dataWithContentsOfFile:[[NSBundle mainBundle] pathForResource:@"License" ofType:@"html"]];
    _licenseView.attributedText = [[NSAttributedString alloc] initWithData:data
                                                           options:@{NSDocumentTypeDocumentAttribute: NSHTMLTextDocumentType,
                                                                     NSCharacterEncodingDocumentAttribute: @(NSUTF8StringEncoding)}
                                                documentAttributes:nil
                                                             error:nil];
    _licenseView.editable = false;
    if (@available(iOS 13.0, *)) {
        _licenseView.backgroundColor = [UIColor systemBackgroundColor];
        _licenseView.textColor = [UIColor labelColor];
    }
    else {
        _licenseView.backgroundColor = [UIColor whiteColor];
    }
    _licenseView.hidden = true;
    _licenseView.userInteractionEnabled = false;
    _licenseView.layer.cornerRadius = 6;
    [root addSubview:_licenseView];
    
    _buttonsView = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 320, 480)];
    
    UIButton *websiteButton = [[UIButton alloc] initWithFrame:CGRectMake(20, 0, 280, 37)];
    [websiteButton setTitle:@"Website" forState:UIControlStateNormal];
    [websiteButton setImage:[self buttonImageNamed:@"globe"] forState:UIControlStateNormal];
    websiteButton.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleBottomMargin;
    [websiteButton setTitleColor:websiteButton.tintColor forState:UIControlStateNormal];
    websiteButton.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
    [websiteButton addTarget:self action:@selector(openWebsite) forControlEvents:UIControlEventTouchUpInside];
    [self alignButton:websiteButton];
    [_buttonsView addSubview:websiteButton];
    
    UIButton *sponsorButton = [[UIButton alloc] initWithFrame:CGRectMake(20, 45, 280, 37)];
#ifdef APPSTORE
    [sponsorButton setTitle:@"Support SameBoy" forState:UIControlStateNormal];
#else
    [sponsorButton setTitle:@"Sponsor SameBoy" forState:UIControlStateNormal];
#endif
    [sponsorButton setImage:[self buttonImageNamed:@"heart"] forState:UIControlStateNormal];
    sponsorButton.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleBottomMargin;
    [sponsorButton setTitleColor:sponsorButton.tintColor forState:UIControlStateNormal];
    sponsorButton.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
    [sponsorButton addTarget:self action:@selector(openSponsor) forControlEvents:UIControlEventTouchUpInside];
    [self alignButton:sponsorButton];
    [_buttonsView addSubview:sponsorButton];
    
    UIButton *licenseButton = [[UIButton alloc] initWithFrame:CGRectMake(20, 90, 280, 37)];
    [licenseButton setTitle:@"License" forState:UIControlStateNormal];
    [licenseButton setImage:[self buttonImageNamed:@"doc.plaintext"] forState:UIControlStateNormal];
    licenseButton.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleBottomMargin;
    licenseButton.titleLabel.textColor = licenseButton.tintColor;
    [licenseButton setTitleColor:licenseButton.tintColor forState:UIControlStateNormal];
    licenseButton.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
    [licenseButton addTarget:self action:@selector(showLicense) forControlEvents:UIControlEventTouchUpInside];
    [self alignButton:licenseButton];
    [_buttonsView addSubview:licenseButton];
    
    [root addSubview:_buttonsView];

    _copyrightLabel = [[UILabel alloc] init];
    _copyrightLabel.text = [[NSBundle mainBundle] infoDictionary][@"NSHumanReadableCopyright"];
    _copyrightLabel.textAlignment = NSTextAlignmentCenter;
    _copyrightLabel.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleTopMargin;
    _copyrightLabel.font = [UIFont systemFontOfSize:13];
    [root addSubview:_copyrightLabel];
    
    [self.view addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self
                                                                            action:@selector(dismissSelf)]];
}

- (void)layoutForVerticalLayout
{
    UIView *root = ((UIVisualEffectView *)self.view).contentView;
    CGRect savedFrame = root.frame;
    root.frame = CGRectMake(0, 0, 320, 480);
    
    _titleLabel.frame = CGRectMake(0, 20, 320, 47);
    _titleLabel.textAlignment = NSTextAlignmentCenter;
    _titleLabel.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleBottomMargin;
    
    _versionLabel.frame = CGRectMake(0, 75, 320, 36);
    _versionLabel.textAlignment = NSTextAlignmentCenter;
    _versionLabel.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleBottomMargin;
    
    _logo.frame = CGRectMake(0, 119, 320, 128);
    _logo.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleBottomMargin;
    
    _buttonsView.frame = CGRectMake(0, 255, 320, 176);
    _buttonsView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    
    _licenseView.frame = CGRectMake(20, 255, 280, 176);
    _licenseView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;


    _copyrightLabel.frame = CGRectMake(0, 450, 320, 21);
    _copyrightLabel.textAlignment = NSTextAlignmentCenter;
    _copyrightLabel.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleTopMargin;

    root.frame = savedFrame;
}

- (void)layoutForHorizontalLayout
{
    UIView *root = ((UIVisualEffectView *)self.view).contentView;
    CGRect savedFrame = root.frame;
    root.frame = CGRectMake(0, 0, 568, 320);
    
    _titleLabel.frame = CGRectMake(20, 20, 260, 47);
    _titleLabel.textAlignment = NSTextAlignmentLeft;
    _titleLabel.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                   UIViewAutoresizingFlexibleBottomMargin |
                                   UIViewAutoresizingFlexibleRightMargin;
    
    _versionLabel.frame = CGRectMake(20, 75, 260, 36);
    _versionLabel.textAlignment = NSTextAlignmentLeft;
    _versionLabel.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                     UIViewAutoresizingFlexibleBottomMargin |
                                     UIViewAutoresizingFlexibleRightMargin;
    
    _logo.frame = CGRectMake(0, 119, 284, 152);
    _logo.autoresizingMask = UIViewAutoresizingFlexibleWidth |
    UIViewAutoresizingFlexibleRightMargin |
    UIViewAutoresizingFlexibleHeight;
    
    _buttonsView.frame = _licenseView.frame = CGRectMake(284, 20, 284, 280);
    _buttonsView.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                    UIViewAutoresizingFlexibleHeight |
                                    UIViewAutoresizingFlexibleLeftMargin;
    _licenseView.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                    UIViewAutoresizingFlexibleHeight |
                                    UIViewAutoresizingFlexibleLeftMargin;

    
    _copyrightLabel.frame = CGRectMake(20, 288, 260, 21);
    _copyrightLabel.textAlignment = NSTextAlignmentLeft;
    _copyrightLabel.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                       UIViewAutoresizingFlexibleTopMargin |
                                       UIViewAutoresizingFlexibleRightMargin;

    root.frame = savedFrame;
    CGRect licenseFrame = _licenseView.frame;
    licenseFrame.size.width -= 40;
    licenseFrame.origin.x += 20;
    _licenseView.frame = licenseFrame;
}

- (void)openWebsite
{
    [[UIApplication sharedApplication] openURL:[NSURL URLWithString:@"https://sameboy.github.io"] options:nil completionHandler:nil];
}

- (void)openSponsor
{
#ifdef APPSTORE
    UINavigationController *navController = [[UINavigationController alloc] initWithRootViewController:[GBSubscriptionViewController new]];
    UIBarButtonItem *close = [[UIBarButtonItem alloc] initWithTitle:@"Close"
                                                              style:UIBarButtonItemStylePlain
                                                             target:self
                                                             action:@selector(dismissViewController)];
    [navController.visibleViewController.navigationItem setLeftBarButtonItem:close];
    
    [self presentViewController:navController animated:true completion:nil];
#else
    [[UIApplication sharedApplication] openURL:[NSURL URLWithString:@"https://github.com/sponsors/LIJI32"] options:nil completionHandler:nil];
#endif
}

- (void)showLicense
{
    _buttonsView.hidden = true;
    _licenseView.hidden = false;
    _licenseView.userInteractionEnabled = true;
}

- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];
    if ([UIDevice currentDevice].userInterfaceIdiom != UIUserInterfaceIdiomPad) {
        UIEdgeInsets insets = self.view.window.safeAreaInsets;
        UIView *view = ((UIVisualEffectView *)self.view).contentView;
        CGRect parentFrame = self.view.frame;
        view.frame = CGRectMake(insets.left,
                                0,
                                parentFrame.size.width - insets.left  - insets.right,
                                parentFrame.size.height - insets.bottom);
    }
    if (self.view.frame.size.width > self.view.frame.size.height) {
        [self layoutForHorizontalLayout];
    }
    else {
        [self layoutForVerticalLayout];
    }
}
     
- (void)dismissSelf
{
    [self.presentingViewController dismissViewControllerAnimated:true completion:nil];
}

- (UIModalPresentationStyle)modalPresentationStyle
{
    return UIModalPresentationFormSheet;
}

- (void)dismissViewController
{
    [self dismissViewControllerAnimated:true completion:nil];
}
@end

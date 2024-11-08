#import "GBThemesViewController.h"
#import "GBThemePreviewController.h"
#ifdef APPSTORE
#import "GBSubscriptionManager.h"
#endif
#import "GBTheme.h"

@interface GBThemesViewController ()

@end

@implementation GBThemesViewController
{
    NSArray<NSArray<GBTheme *> *> *_themes;
}

+ (NSArray<NSArray<GBTheme *> *> *)themes
{
    static __weak NSArray<NSArray<GBTheme *> *> *cache = nil;
    if (cache) return cache;
    id ret  = @[
        @[
            [[GBTheme alloc] initDefaultTheme],
            [[GBTheme alloc] initDarkTheme],
        ],
#ifdef APPSTORE
        @[
            [[GBTheme alloc] initDMGTheme],
            [[GBTheme alloc] initPlayItLoudBlackTheme],
            [[GBTheme alloc] initPlayItLoudThemeWithColor:0x2e9a6b andName:@"Loud Gorgeous Green"],
            [[GBTheme alloc] initPlayItLoudThemeWithColor:0xee2a1b andName:@"Loud Radiant Red"],
            [[GBTheme alloc] initPlayItLoudThemeWithColor:0xefb02d andName:@"Loud Vibrant Yellow"],
            [[GBTheme alloc] initPlayItLoudThemeWithColor:0x2b5c96 andName:@"Loud Cool Blue"],
            [[GBTheme alloc] initPlayItLoudThemeWithColor:0xe5e0d1 andName:@"Loud Traditional White"],
        ],
        @[
            [[GBTheme alloc] initCGBThemeWithColor:0xad2142 andName:@"Color Berry"],
            [[GBTheme alloc] initCGBThemeWithColor:0x322592 andName:@"Color Grape"],
            [[GBTheme alloc] initCGBThemeWithColor:0x7daf3f andName:@"Color Kiwi"],
            [[GBTheme alloc] initCGBThemeWithColor:0xd3ad3b andName:@"Color Dandelion"],
            [[GBTheme alloc] initCGBThemeWithColor:0x077172 andName:@"Color Teal"],
        ],
        @[
            [[GBTheme alloc] initAGBThemeWithColor:0x4141a9 andName:@"Advance Indigo"],
            [[GBTheme alloc] initAGBThemeWithColor:0xe1e1e1 andName:@"Advance Arctic"],
            [[GBTheme alloc] initAGBThemeWithColor:0x202020 andName:@"Advance Black"],
            [[GBTheme alloc] initAGBThemeWithColor:0xfa9e3e andName:@"Advance Spice"],
            
        ],
        @[
            [[GBTheme alloc] initGameAndWatchTheme],
            [[GBTheme alloc] initSFCTheme],
            [[GBTheme alloc] initSNESTheme],
            [[GBTheme alloc] initGCNTheme],
            [[GBTheme alloc] initMegaDuckTheme],
            [[GBTheme alloc] initDarkMegaDuckTheme],
        ],
#endif
    ];
    cache = ret;
    return ret;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    
    _themes = [[self class] themes];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
    return _themes.count;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    return _themes[section].count;
}

- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)indexPath
{
    return 60;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    GBTheme *theme = _themes[indexPath.section][indexPath.row];
    cell.textLabel.text = theme.name;

    cell.accessoryType = [[[NSUserDefaults standardUserDefaults] stringForKey:@"GBInterfaceTheme"] isEqual:theme.name]? UITableViewCellAccessoryCheckmark : UITableViewCellAccessoryNone;
    bool horizontal = self.interfaceOrientation >= UIInterfaceOrientationLandscapeRight;
    UIImage *preview = horizontal? [theme horizontalPreview] : [theme verticalPreview];
    UIGraphicsBeginImageContextWithOptions((CGSize){60, 60}, false, self.view.window.screen.scale);
    unsigned width = 60;
    unsigned height = 56;
    if (horizontal) {
        height = round(preview.size.height / preview.size.width * 60);
    }
    else {
        width = round(preview.size.width / preview.size.height * 56);
    }
    UIBezierPath *mask = [UIBezierPath bezierPathWithRoundedRect:CGRectMake((60 - width) / 2, (60 - height) / 2, width, height) cornerRadius:4];
    [mask addClip];
    [preview drawInRect:mask.bounds];
    if (@available(iOS 13.0, *)) {
        [[UIColor tertiaryLabelColor] set];
    }
    else {
        [[UIColor colorWithWhite:0 alpha:0.5] set];
    }
    [mask stroke];
    cell.imageView.image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    
#ifdef APPSTORE
    if (indexPath.section != 0 && GBSubscriptionManager.defaultManager.themeState == GBSubscriptionInactive) {
        NSString *currencySymbol = [NSLocale currentLocale].currencySymbol;
        NSString *currencyName = @{
            @"$":  @"dollar",
            @"¢":  @"cent",
            @"¥":  @"yen",
            @"£":  @"sterling",
            @"₣":  @"franc",
            @"ƒ":  @"florin",
            @"₺":  @"turkishlira",
            @"₽":  @"ruble",
            @"€":  @"euro",
            @"₫":  @"dong",
            @"₹":  @"indianrupee",
            @"₸":  @"tenge",
            @"₧":  @"peseta",
            @"₱":  @"peso",
            @"₭":  @"kip",
            @"₩":  @"won",
            @"₤":  @"lira",
            @"₳":  @"austral",
            @"₴":  @"hryvnia",
            @"₦":  @"naira",
            @"₲":  @"guarani",
            @"₡":  @"coloncurrency",
            @"₵":  @"cedi",
            @"₢":  @"cruzeiro",
            @"₮":  @"tugrik",
            @"₥":  @"mill",
            @"₪":  @"sheqel",
            @"₼":  @"manat",
            @"₨":  @"rupee",
            @"฿":  @"baht",
            @"₾":  @"lari",
            @"R$": @"brazilianreal",
        }[currencySymbol] ?: @"dollar";
        
        UIImage *image = [UIImage systemImageNamed:[NSString stringWithFormat:@"%@sign.circle", currencyName]];
        if (!image) {
            image = [UIImage systemImageNamed:@"dollarsign.circle"];
        }
        cell.accessoryView = [[UIImageView alloc] initWithImage:image];
    }
#endif
        
    return cell;
}

- (void)willRotateToInterfaceOrientation:(UIInterfaceOrientation)toInterfaceOrientation duration:(NSTimeInterval)duration
{
    [self.tableView reloadData];
    [super willRotateToInterfaceOrientation:toInterfaceOrientation duration:duration];
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
    GBTheme *theme = _themes[indexPath.section][indexPath.row];
    GBThemePreviewController *preview = [[GBThemePreviewController alloc] initWithTheme:theme isPaid:indexPath.section != 0];
    [self presentViewController:preview animated:true completion:nil];
}

- (void)viewWillAppear:(BOOL)animated
{
    [self.tableView reloadData];
}

@end

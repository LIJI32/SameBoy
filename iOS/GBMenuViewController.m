#import <objc/runtime.h>
#import "GBMenuViewController.h"
#import "GBMenuButton.h"
#import "GBViewController.h"
#import "GBROMManager.h"

static NSString *const tips[] = {
    @"Tip: AirDrop ROM files from a Mac or another device to play them.",
    @"Tip: Swipe right on the Game Boy screen to fast forward emulation.",
    @"Tip: The D-pad can be replaced with a Swipe-pad in Control Settings. Give it a try!",
    @"Tip: Swipe left on the Game Boy screen to rewind.",
    @"Tip: Enable Quick Save and Load in Control Settings to use save state gestures.",
    @"Tip: The turbo and rewind speeds can be changed in Emulation Settings.",
    @"Tip: Change turbo and rewind behavior to locking in Controls Settings.",
    @"Tip: Single Touch A+B combo can be enabled in Controls Settings.",
    @"Tip: Try different scaling filters in Display Settings.",
    @"Tip: Dynamically control turbo and rewind speed by enabling Dynamic Control in Control Settings.",
    @"Tip: Rumble can be enabled even for games without rumble support in Control Settings.",
    @"Tip: Try different color palettes for monochrome models in Display Settings.",
    @"Tip: Use an external game conrtoller and Screen Mirroring for a big screen experience!",
    @"Did you know? The Game Boy uses a Sharp SM83 CPU.",
    @"Did you know? The Game Boy Color has 6 different hardware revisions.",
    @"Did you know? The Game Boy's frame rate is approximately 59.73 frames per second.",
    @"Did you know? The original Super Game Boy runs slightly faster than other Game Boys.",
    @"Did you know? The Game Boy generates audio at a sample rate of over 2MHz!",
};

@implementation GBMenuViewController
{
    UILabel *_tipLabel;
    UIVisualEffectView *_effectView;
}

+ (instancetype)menu
{
    UIAlertControllerStyle style = [UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad?
                                       UIAlertControllerStyleAlert : UIAlertControllerStyleActionSheet;
    GBMenuViewController *ret = [self alertControllerWithTitle:nil
                                                       message:nil
                                                preferredStyle:style];
    [ret addAction:[UIAlertAction actionWithTitle:@"Close"
                                            style:UIAlertActionStyleCancel
                                          handler:nil]];
    return ret;
}

// The redundant sizeof forces the compiler to validate the selector exists
#define SelectorString(x) (sizeof(@selector(x))? @#x : nil)

- (void)viewWillAppear:(BOOL)animated
{
    [super viewWillAppear:true];
    static const struct {
        NSString *label;
        NSString *image;
        NSString *selector;
        bool requireRunning;
    } buttons[] = {
        {@"Reset", @"arrow.2.circlepath", SelectorString(reset), true},
        {@"Library", @"bookmark", SelectorString(openLibrary)},
        {@"Connect", @"LinkCableTemplate", SelectorString(openConnectMenu), true},
        {@"Model", @"ModelTemplate", SelectorString(changeModel)},
        {@"States", @"square.stack", SelectorString(openStates), true},
        {@"Cheats", @"CheatsTemplate", SelectorString(openCheats), true},
        {@"Settings", @"gear", SelectorString(openSettings)},
        {@"About", @"info.circle", SelectorString(showAbout)},
    };
    
    double width = self.view.frame.size.width / 4;
    double height = 88;
    for (unsigned i = 0; i < 8; i++) {
        unsigned x = i % 4;
        unsigned y = i / 4;
        GBMenuButton *button = [GBMenuButton buttonWithType:UIButtonTypeSystem];
        [button setTitle:buttons[i].label forState:UIControlStateNormal];
        if (@available(iOS 13.0, *)) {
            UIImage *image = [UIImage imageNamed:buttons[i].image] ?: [UIImage systemImageNamed:buttons[i].image];
            [button setImage:image forState:UIControlStateNormal];
        }
        button.frame = CGRectMake(round(width * x), height * y, round(width), height);
        button.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleLeftMargin | UIViewAutoresizingFlexibleRightMargin;
        [self.view addSubview:button];

        if (!buttons[i].selector) {
            button.enabled = false;
            continue;
        }
        SEL selector = NSSelectorFromString(buttons[i].selector);
        if (buttons[i].requireRunning && ![GBROMManager sharedManager].currentROM) {
            button.enabled = false;
            continue;
        }
        id block = ^(){
            [self.presentingViewController dismissViewControllerAnimated:true completion:^{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
                (void)[[UIApplication sharedApplication].delegate performSelector:selector];
#pragma clang diagnostic pop
            }];
        };
        objc_setAssociatedObject(button, "RetainedBlock", block, OBJC_ASSOCIATION_RETAIN);
        [button addTarget:block action:@selector(invoke) forControlEvents:UIControlEventTouchUpInside];
    }
    
    _effectView = [[UIVisualEffectView alloc] initWithEffect:[UIBlurEffect effectWithStyle:UIBlurEffectStyleProminent]];
    _effectView.layer.cornerRadius = 8;
    _effectView.layer.masksToBounds = true;
    [self.view.superview addSubview:_effectView];
    _tipLabel = [[UILabel alloc] init];
    unsigned tipIndex = [[NSUserDefaults standardUserDefaults] integerForKey:@"GBTipIndex"];
    _tipLabel.text = tips[tipIndex % (sizeof(tips) / sizeof(tips[0]))];
    if (@available(iOS 13.0, *)) {
        _tipLabel.textColor = [UIColor labelColor];
    }
    _tipLabel.font = [UIFont systemFontOfSize:14];
    _tipLabel.alpha = 0.8;
    [[NSUserDefaults standardUserDefaults] setInteger:tipIndex + 1 forKey:@"GBTipIndex"];
    _tipLabel.lineBreakMode = NSLineBreakByWordWrapping;
    _tipLabel.numberOfLines = 3;
    [_effectView.contentView addSubview:_tipLabel];
    [self layoutTip];
    _effectView.alpha = 0;
    [UIView animateWithDuration:0.25 animations:^{
        _effectView.alpha = 1.0;
    }];
}

- (void)layoutTip
{
    UIView *view = self.view.superview;
    CGSize outerSize = view.frame.size;
    CGSize size = [_tipLabel textRectForBounds:(CGRect){{0, 0},
                                                        {outerSize.width - 32,
                                                         outerSize.height - 32}}
                        limitedToNumberOfLines:3].size;
    size.width = ceil(size.width);
    _tipLabel.frame = (CGRect){{8, 8}, size};
    _effectView.frame = (CGRect) {
        {round((outerSize.width - size.width - 16) / 2), view.window.safeAreaInsets.top + 12},
        {size.width + 16, size.height + 16}
    };
}


- (void)viewWillDisappear:(BOOL)animated
{
    [UIView animateWithDuration:0.25 animations:^{
        _effectView.alpha = 0;
    }];
    [super viewWillDisappear:animated];
}

- (void)viewDidLayoutSubviews
{
    CGRect frame = self.view.frame;
    if (frame.size.height < 88 * 2) {
        [self.view.heightAnchor constraintEqualToConstant:frame.size.height + 88 * 2].active = true;
    }
    double width = MIN(MIN(UIScreen.mainScreen.bounds.size.width, UIScreen.mainScreen.bounds.size.height) - 16, 400);
    /* Damn I hate NSLayoutConstraints */
    if (frame.size.width != width) {
        for (UIView *subview in self.view.subviews) {
            if (![subview isKindOfClass:[GBMenuButton class]]) {
                for (NSLayoutConstraint *constraint in subview.constraints) {
                    if (constraint.constant == frame.size.width) {
                        constraint.active = false;
                    }
                }
                [subview.widthAnchor constraintEqualToConstant:width].active = true;
                for (UIView *subsubview in subview.subviews) {
                    for (NSLayoutConstraint *constraint in subsubview.constraints) {
                        if (constraint.constant == frame.size.width) {
                            constraint.active = false;
                        }
                    }
                    [subsubview.widthAnchor constraintEqualToConstant:width].active = true;
                }
            }
        }
        [self.view.widthAnchor constraintEqualToConstant:width].active = true;
    }
    [self layoutTip];
    [super viewDidLayoutSubviews];
}

// This is a hack that forces the iPad controller to display the button separator
- (UIViewController *)contentViewController
{
    return [[UIViewController alloc] init];
}

@end

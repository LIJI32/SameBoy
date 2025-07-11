#import "GBStatesViewController.h"
#import "GBSlotButton.h"
#import "GBROMManager.h"
#import "GBViewController.h"
#ifdef APPSTORE
#import <objc/runtime.h>
#endif

@implementation GBStatesViewController

- (void)updateSlotView:(GBSlotButton *)view
{
    NSString *stateFile = [[GBROMManager sharedManager] stateFile:view.tag];
    if ([[NSFileManager defaultManager] fileExistsAtPath:stateFile]) {
        NSDate *date = [[[NSFileManager defaultManager] attributesOfItemAtPath:stateFile error:nil] fileModificationDate];
        if (@available(iOS 13.0, *)) {
            if ((uint64_t)(date.timeIntervalSince1970) == (uint64_t)([NSDate now].timeIntervalSince1970)) {
                view.slotSubtitleLabel.text = @"Just now";
            }
            else {
                NSRelativeDateTimeFormatter *formatter = [[NSRelativeDateTimeFormatter alloc] init];
                view.slotSubtitleLabel.text = [formatter localizedStringForDate:date relativeToDate:[NSDate now]];
            }
        }
        else {
            NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
            formatter.timeStyle = NSDateFormatterShortStyle;
            formatter.dateStyle = NSDateFormatterShortStyle;
            formatter.doesRelativeDateFormatting = true;
            view.slotSubtitleLabel.text = [formatter stringFromDate:date];
        }
        
        view.imageView.image = [UIImage imageWithContentsOfFile:[stateFile stringByAppendingPathExtension:@"png"]];
    }
    else {
        view.slotSubtitleLabel.text = @"Empty";
        view.imageView.image = nil;
    }
}

#ifdef APPSTORE
- (void)performAtomically:(void (^)(void))block
{
    self.view.window.userInteractionEnabled = false;
    UIActivityIndicatorView *activityIndicator = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
    [self navigationItem].rightBarButtonItem = [[UIBarButtonItem alloc] initWithCustomView:activityIndicator];
    [activityIndicator startAnimating];
    [[GBROMManager sharedManager] syncROM:GBROMManager.sharedManager.currentROM
                                   completion:^(NSString *error) {
        block();
        if (error) {
            UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Could not sync save states with iCloud"
                                                                           message:error
                                                                    preferredStyle:UIAlertControllerStyleAlert];
            [alert addAction:[UIAlertAction actionWithTitle:@"Close"
                                                      style:UIAlertActionStyleCancel
                                                    handler:nil]];

            UIImage *image = [UIImage systemImageNamed:@"exclamationmark.triangle.fill"];
            
            id block = ^(){
                [self presentViewController:alert animated:true completion:nil];
            };
            objc_setAssociatedObject(self, "RetainedBlock", block, OBJC_ASSOCIATION_RETAIN);
            

            [self navigationItem].rightBarButtonItem = [[UIBarButtonItem alloc] initWithImage:image
                                                                                        style:UIBarButtonItemStylePlain
                                                                                       target:block
                                                                                       action:@selector(invoke)];
        }
        else {
            [self navigationItem].rightBarButtonItem = nil;
        }
        self.view.window.userInteractionEnabled = true;
    } queue:[NSOperationQueue mainQueue]];
}
#endif

- (void)viewDidLoad
{
    [super viewDidLoad];
    UIView *root = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 0x300, 0x300)];
    [self.view addSubview:root];
    for (unsigned i = 0; i < 9; i++) {
        unsigned x = i % 3;
        unsigned y = i / 3;
        GBSlotButton *slotView = [GBSlotButton buttonWithLabelText:[NSString stringWithFormat:@"Slot %u", i + 1]];
        
        slotView.frame = CGRectMake(0x100 * x,
                                    0x100 * y,
                                    0x100,
                                    0x100);
        
        slotView.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin |
                                    UIViewAutoresizingFlexibleWidth |
                                    UIViewAutoresizingFlexibleRightMargin |
                                    UIViewAutoresizingFlexibleTopMargin |
                                    UIViewAutoresizingFlexibleHeight |
                                    UIViewAutoresizingFlexibleBottomMargin;

        slotView.tag = i + 1;
        [self updateSlotView:slotView];
        [slotView addTarget:self
                     action:@selector(slotSelected:)
           forControlEvents:UIControlEventTouchUpInside];
        [root addSubview:slotView];
    }
    self.edgesForExtendedLayout = 0;
#ifdef APPSTORE
    [self performAtomically:^{
        for (GBSlotButton *slotView in root.subviews) {
            [self updateSlotView:slotView];
        }
    }];
#endif
}

- (void)slotSelected:(GBSlotButton *)slot
{
    
    UIAlertController *controller = [UIAlertController alertControllerWithTitle:slot.label.text
                                                                        message:nil
                                                                 preferredStyle:UIAlertControllerStyleActionSheet];
    
    NSString *stateFile = [[GBROMManager sharedManager] stateFile:slot.tag];
    GBViewController *delegate = (typeof(delegate))[UIApplication sharedApplication].delegate;
    
    void (^saveState)(UIAlertAction *action) = ^(UIAlertAction *action) {
#ifdef APPSTORE
        [self performAtomically:^{
#endif
        [delegate saveStateToFile:stateFile];
        [self updateSlotView:slot];
        [self.presentingViewController dismissViewControllerAnimated:true completion:nil];
        slot.showingMenu = false;
#ifdef APPSTORE
        }];
#endif
    };
    
    if (![[NSFileManager defaultManager] fileExistsAtPath:stateFile]) {
        saveState(nil);
        return;
    }

    [controller addAction:[UIAlertAction actionWithTitle:@"Save state"
                                                   style:UIAlertActionStyleDefault
                                                 handler:saveState]];
    
    [controller addAction:[UIAlertAction actionWithTitle:@"Load state"
                                                   style:UIAlertActionStyleDefault
                                                 handler:^(UIAlertAction *action) {
#ifdef APPSTORE
        [self performAtomically:^{
#endif
        [delegate loadStateFromFile:stateFile];
        [self updateSlotView:slot];
        [self.presentingViewController dismissViewControllerAnimated:true completion:nil];
        slot.showingMenu = false;
#ifdef APPSTORE
        }];
#endif
    }]];
    
    [controller addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                                   style:UIAlertActionStyleCancel
                                                 handler:^(UIAlertAction *action) {
        slot.showingMenu = false;
    }]];
        
    slot.showingMenu = true;
    controller.popoverPresentationController.sourceView = slot;
    [self presentViewController:controller animated:true completion:nil];
}

- (void)viewWillLayoutSubviews
{
    [super viewWillLayoutSubviews];
    self.view.subviews.firstObject.frame = [self.view.safeAreaLayoutGuide layoutFrame];
}

- (NSString *)title
{
    return @"Save States";
}
@end

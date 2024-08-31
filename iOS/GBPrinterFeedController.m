#import "GBPrinterFeedController.h"
#import "GBViewController.h"
#import "GBActivityViewController.h"

@implementation GBPrinterFeedController
{
    UIImage *_image;
}

- (instancetype)initWithImage:(UIImage *)image
{
    _image = image;
    UIViewController *scrollViewController = [[UIViewController alloc] init];
    scrollViewController.title = @"Printer Feed";
    if (@available(iOS 13.0, *)) {
        scrollViewController.view.backgroundColor = [UIColor systemBackgroundColor];
    }
    else {
        scrollViewController.view.backgroundColor = [UIColor whiteColor];
    }
    
    UIScrollView *scrollView = [[UIScrollView alloc] initWithFrame:scrollViewController.view.bounds];
    
    scrollView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    scrollView.scrollEnabled = true;
    scrollView.pagingEnabled = false;
    scrollView.showsVerticalScrollIndicator = true;
    scrollView.showsHorizontalScrollIndicator = false;
    [scrollViewController.view addSubview:scrollView];
    
    CGSize size = image.size;
    while (size.width < 320) {
        size.width *= 2;
        size.height *= 2;
    }
    UIImageView *imageView = [[UIImageView alloc] initWithImage:image];
    imageView.contentMode = UIViewContentModeScaleToFill;
    imageView.frame = (CGRect){{0, 0}, size};
    imageView.layer.magnificationFilter = kCAFilterNearest;
    
    scrollView.contentSize = size;
    [scrollView addSubview:imageView];
    
    CGSize contentSize = size;
    self.preferredContentSize = contentSize;
    
    self = [self initWithRootViewController:scrollViewController];
    UIBarButtonItem *close = [[UIBarButtonItem alloc] initWithTitle:@"Close"
                                                              style:UIBarButtonItemStylePlain
                                                             target:self
                                                             action:@selector(dismissFromParent)];
    [self.visibleViewController.navigationItem setLeftBarButtonItem:close];
    [scrollViewController setToolbarItems:@[
        [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemAction
                                                      target:self
                                                      action:@selector(presentShareSheet)],
        [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace
                                                      target:nil
                                                      action:nil],
        [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemTrash
                                                      target:self
                                                      action:@selector(emptyFeed)],

    ] animated:false];
    [self setToolbarHidden:false animated:false];
    return self;
}

- (UIView *)viewToMask
{
    UIView *targetView = self.view;
    while (targetView.superview != targetView.window &&
           targetView.superview.superview != targetView.window){
        targetView = targetView.superview;
    }
    return targetView;
}

- (void)viewWillLayoutSubviews
{
    [super viewWillLayoutSubviews];
    if ([UIDevice currentDevice].userInterfaceIdiom != UIUserInterfaceIdiomPad) {
        UIView *targetView = self.view;
        CGRect frame = targetView.frame;
        frame.origin.x = ([UIScreen mainScreen].bounds.size.width - 320) / 2;
        frame.size.width = 320;
        targetView.frame = frame;
        
        UIBezierPath *maskPath = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(frame.origin.x, 0,
                                                                                    320, 4096)
                                                       byRoundingCorners:UIRectCornerTopLeft | UIRectCornerTopRight
                                                             cornerRadii:CGSizeMake(10, 10)];
        
        CAShapeLayer *maskLayer = [CAShapeLayer layer];
        maskLayer.frame = self.view.bounds;
        maskLayer.path = maskPath.CGPath;

        self.viewToMask.layer.mask = maskLayer;
        
    }
}

- (void)presentShareSheet
{
    NSURL *url = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:@"Game Boy Printer Image.png"]];
    [UIImagePNGRepresentation(_image) writeToURL:url atomically:false];
    [self presentViewController:[[GBActivityViewController alloc] initWithActivityItems:@[url]
                                                                  applicationActivities:nil]
                       animated:true
                     completion:nil];
}

- (void)emptyFeed
{
    [(GBViewController *)UIApplication.sharedApplication.delegate emptyPrinterFeed];
}

- (void)dismissFromParent
{
    [self.presentingViewController dismissViewControllerAnimated:true completion:nil];
}

- (UIModalPresentationStyle)modalPresentationStyle
{
    return UIModalPresentationFormSheet;
}
@end

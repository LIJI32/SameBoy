#import "GBPrinterFeedController.h"
#import "GBViewController.h"

@implementation GBPrinterFeedController
{
    UIViewController *_scrollViewController;
    UIImage *_image;
}

- (instancetype)initWithImage:(UIImage *)image
{
    _image = image;
    _scrollViewController = [[UIViewController alloc] init];
    _scrollViewController.title = @"Printer Feed";
    _scrollViewController.view.backgroundColor = [UIColor systemBackgroundColor];
    
    UIScrollView *scrollView = [[UIScrollView alloc] initWithFrame:_scrollViewController.view.bounds];
    
    scrollView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    scrollView.scrollEnabled = true;
    scrollView.pagingEnabled = false;
    scrollView.showsVerticalScrollIndicator = true;
    scrollView.showsHorizontalScrollIndicator = false;
    [_scrollViewController.view addSubview:scrollView];
    
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
    
    self = [self initWithRootViewController:_scrollViewController];
    UIBarButtonItem *close = [[UIBarButtonItem alloc] initWithTitle:@"Close"
                                                              style:UIBarButtonItemStylePlain
                                                             target:self
                                                             action:@selector(dismissFromParent)];
    [self.visibleViewController.navigationItem setLeftBarButtonItem:close];
    [_scrollViewController setToolbarItems:@[
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

- (void)viewWillLayoutSubviews
{
    [super viewWillLayoutSubviews];
    if ([UIDevice currentDevice].userInterfaceIdiom != UIUserInterfaceIdiomPad) {
        CGRect frame = self.view.frame;
        frame.origin.x = ([UIScreen mainScreen].bounds.size.width - 320) / 2;
        frame.size.width = 320;
        self.view.frame = frame;
        
        UIBezierPath *maskPath = [UIBezierPath bezierPathWithRoundedRect:self.view.bounds
                                                       byRoundingCorners:UIRectCornerTopLeft | UIRectCornerTopRight
                                                             cornerRadii:CGSizeMake(12.0, 12.0)];
        
        CAShapeLayer *maskLayer = [CAShapeLayer layer];
        maskLayer.frame = self.view.bounds;
        maskLayer.path = maskPath.CGPath;
        
        self.view.layer.mask = maskLayer;
    }
}

- (void)presentShareSheet
{
    NSURL *url = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:@"Game Boy Printer Image.png"]];
    [UIImagePNGRepresentation(_image) writeToURL:url atomically:false];
    [self presentViewController:[[UIActivityViewController alloc] initWithActivityItems:@[url]
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

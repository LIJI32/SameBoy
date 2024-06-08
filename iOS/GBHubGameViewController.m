#import "GBHubGameViewController.h"
#import "GBROMManager.h"
#import "UILabel+TapLocation.h"

@implementation NSMutableAttributedString (append)

- (void)appendWithAttributes:(NSDictionary *)attributes format:(NSString *)format, ...
{
    va_list args;
    va_start(args, format);
    NSString *string = [[NSString alloc] initWithFormat:format arguments:args];
    va_end(args);
    [self appendAttributedString:[[NSAttributedString alloc] initWithString:string attributes:attributes]];
}

@end

@implementation GBHubGameViewController
{
    GBHubGame *_game;
    UIScrollView *_scrollView;
    UIScrollView *_screenshotsScrollView;
    UILabel *_titleLabel;
    UILabel *_descriptionLabel;
}

- (instancetype)initWithGame:(GBHubGame *)game
{
    self = [super init];
    _game = game;
    return self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    UIColor *labelColor, *linkColor, *secondaryLabelColor;
    if (@available(iOS 13.0, *)) {
        self.view.backgroundColor = [UIColor systemBackgroundColor];
        labelColor = UIColor.labelColor;
        linkColor = UIColor.linkColor;
        secondaryLabelColor = UIColor.secondaryLabelColor;

    }
    else {
        self.view.backgroundColor = [UIColor whiteColor];
        labelColor = UIColor.blackColor;
        linkColor = UIColor.blueColor;
        secondaryLabelColor = [UIColor colorWithWhite:0.55 alpha:1.0];
    }
    _scrollView = [[UIScrollView alloc] initWithFrame:self.view.bounds];
    _scrollView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _scrollView.scrollEnabled = true;
    _scrollView.pagingEnabled = false;
    _scrollView.showsVerticalScrollIndicator = true;
    _scrollView.showsHorizontalScrollIndicator = false;
    [self.view addSubview:_scrollView];
    
    _scrollView.contentSize = CGSizeMake(self.view.bounds.size.width, self.view.bounds.size.height * 2);
    
    _titleLabel = [[UILabel alloc] initWithFrame:(CGRectMake(0, 8,
                                                             self.view.bounds.size.width - 16,
                                                             56))];
    NSMutableParagraphStyle *style = [NSParagraphStyle defaultParagraphStyle].mutableCopy;
    style.paragraphSpacing = 4;

    NSMutableAttributedString *titleText = [[NSMutableAttributedString alloc] init];
    [titleText appendWithAttributes:@{
        NSFontAttributeName: [UIFont systemFontOfSize:34 weight:UIFontWeightBold],
        NSForegroundColorAttributeName: labelColor,
        NSParagraphStyleAttributeName: style,
    } format:@"%@", _game.title];
    
    [titleText appendWithAttributes:@{
        NSFontAttributeName: [UIFont systemFontOfSize:20],
        NSForegroundColorAttributeName: secondaryLabelColor,
        NSParagraphStyleAttributeName: style,
    } format:@"\n by %@", _game.developer];
    
    _titleLabel.attributedText = titleText;
    _titleLabel.lineBreakMode = NSLineBreakByWordWrapping;
    _titleLabel.numberOfLines = 0;
    [_scrollView addSubview:_titleLabel];

    
    NSMutableAttributedString *text = [[NSMutableAttributedString alloc] init];
    NSDictionary *labelAttributes = @{
        NSFontAttributeName: [UIFont boldSystemFontOfSize:UIFont.labelFontSize],
        NSForegroundColorAttributeName: labelColor,
    };
    NSDictionary *valueAttributes = @{
        NSFontAttributeName: [UIFont systemFontOfSize:UIFont.labelFontSize],
        NSForegroundColorAttributeName: labelColor,
    };
    
    if (_game.entryDescription) {
        [text appendWithAttributes:valueAttributes format:@"%@\n\n", _game.entryDescription];
    }
    if (_game.publicationDate) {
        [text appendWithAttributes:labelAttributes format:@"Published: "];
        NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
        formatter.dateStyle = NSDateFormatterMediumStyle;
        formatter.timeStyle = NSDateFormatterNoStyle;
        formatter.locale = [NSLocale currentLocale];
        [text appendWithAttributes:valueAttributes format:@"%@\n", [formatter stringFromDate:_game.publicationDate]];
    }
    if (_game.website) {
        [text appendWithAttributes:labelAttributes format:@"Website: "];
        [text appendWithAttributes:@{
            NSFontAttributeName: [UIFont systemFontOfSize:UIFont.labelFontSize],
            @"GBLinkAttribute": _game.website,
            NSForegroundColorAttributeName: linkColor,
        } format:@"%@\n", _game.website];
    }
    if (_game.repository) {
        [text appendWithAttributes:labelAttributes format:@"Repository: "];
        [text appendWithAttributes:@{
            NSFontAttributeName: [UIFont systemFontOfSize:UIFont.labelFontSize],
            @"GBLinkAttribute": _game.repository,
            NSForegroundColorAttributeName: linkColor,
        } format:@"%@\n", _game.repository];
    }
    if (_game.license) {
        [text appendWithAttributes:labelAttributes format:@"License: "];
        [text appendWithAttributes:valueAttributes format:@"%@\n", _game.license];
    }
    if (_game.tags.count) {
        [text appendWithAttributes:labelAttributes format:@"Categories: "];
        bool first = true;
        for (NSString *tag in _game.tags) {
            if (!first) {
                [text appendWithAttributes:valueAttributes format:@", ", _game.license];
            }
            first = false;
            [text appendWithAttributes:@{
                NSFontAttributeName: [UIFont systemFontOfSize:UIFont.labelFontSize],
                @"GBHubTag": tag,
                NSForegroundColorAttributeName: linkColor,
            } format:@"%@", tag];
        }
        [text appendWithAttributes:valueAttributes format:@"\n"];
    }
    
    _descriptionLabel = [[UILabel alloc] init];
    _descriptionLabel.numberOfLines = 0;
    _descriptionLabel.lineBreakMode = NSLineBreakByWordWrapping;
    if (@available(iOS 14.0, *)) {
        _descriptionLabel.lineBreakStrategy = NSLineBreakStrategyNone;
    }
    _descriptionLabel.attributedText = text;
    [_scrollView addSubview:_descriptionLabel];
    
    unsigned screenshotWidth = (unsigned)(MIN(self.view.bounds.size.width, self.view.bounds.size.height) - 16) / 160 * 160;
    unsigned screenshotHeight = screenshotWidth / 160 * 144;
    if (_game.screenshots.count) {
        _screenshotsScrollView = [[UIScrollView alloc] initWithFrame:CGRectMake(0, 0,
                                                                                self.view.bounds.size.width,
                                                                                screenshotHeight + 8)];
        _screenshotsScrollView.scrollEnabled = true;
        _screenshotsScrollView.pagingEnabled = false;
        _screenshotsScrollView.showsVerticalScrollIndicator = false;
        _screenshotsScrollView.showsHorizontalScrollIndicator = true;

        unsigned x = 0;
        for (NSURL *url in _game.screenshots) {
            x += 8;
            UIImageView *imageView = [[UIImageView alloc] initWithFrame:CGRectMake(x, 0,
                                                                                   screenshotWidth,
                                                                                   screenshotHeight)];
            [imageView.layer setMinificationFilter:kCAFilterLinear];
            [imageView.layer setMagnificationFilter:kCAFilterNearest];
            imageView.layer.cornerRadius = 4;
            imageView.layer.borderWidth = 1;
            imageView.layer.masksToBounds = true;

            if (@available(iOS 13.0, *)) {
                imageView.layer.borderColor = [UIColor tertiaryLabelColor].CGColor;
            }
            else {
                imageView.layer.borderColor = [UIColor colorWithWhite:0 alpha:0.5].CGColor;
            }
            
            [_screenshotsScrollView addSubview:imageView];
            [[[NSURLSession sharedSession] downloadTaskWithURL:url completionHandler:^(NSURL *location, NSURLResponse *response, NSError *error) {
                if (location) {
                    UIImage *image = [UIImage imageWithContentsOfFile:location.path];
                    dispatch_async(dispatch_get_main_queue(), ^{
                        CGRect frame = imageView.frame;
                        imageView.image = image;
                        imageView.frame = frame;
                        imageView.contentMode = UIViewContentModeScaleAspectFit;
                    });
                }
            }] resume];
            x += screenshotWidth + 8;
        }
        _screenshotsScrollView.contentSize = CGSizeMake(x, screenshotHeight);
        _screenshotsScrollView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleBottomMargin;
        
        [_scrollView addSubview:_screenshotsScrollView];
    }
    [self viewDidLayoutSubviews];
    _descriptionLabel.userInteractionEnabled = true;
    [_descriptionLabel addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self
                                                                                    action:@selector(tappedLabel:)]];
    
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithTitle:@"Download"
                                                                              style:UIBarButtonItemStylePlain
                                                                             target:self
                                                                             action:@selector(rightButtonPressed)];
    if ([GBROMManager.sharedManager romFileForROM:_game.title]) {
        self.navigationItem.rightBarButtonItem.title = @"Open";
    }
}

- (void)rightButtonPressed
{
    if ([GBROMManager.sharedManager romFileForROM:_game.title]) {
        [GBROMManager sharedManager].currentROM = _game.title;
        [self.navigationController dismissViewControllerAnimated:true completion:nil];
        [[NSNotificationCenter defaultCenter] postNotificationName:@"GBROMChanged" object:nil];
    }
    else {
        UIActivityIndicatorViewStyle style = UIActivityIndicatorViewStyleWhite;
        if (@available(iOS 13.0, *)) {
            style = UIActivityIndicatorViewStyleMedium;
        }
        UIActivityIndicatorView *view = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:style];
        CGRect frame = view.frame;
        frame.size.width += 16;
        view.frame = frame;
        [view startAnimating];
        self.navigationItem.rightBarButtonItem.customView = view;
        [[[NSURLSession sharedSession] downloadTaskWithURL:_game.file completionHandler:^(NSURL *location, NSURLResponse *response, NSError *error) {
            if (!location) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Could not download ROM"
                                                                                   message:@"Could not download this ROM from Homebrew Hub. Please try again later."
                                                                            preferredStyle:UIAlertControllerStyleAlert];
                    [alert  addAction:[UIAlertAction actionWithTitle:@"Close"
                                                               style:UIAlertActionStyleCancel
                                                             handler:nil]];
                    [self presentViewController:alert animated:true completion:nil];
                    self.navigationItem.rightBarButtonItem.customView = nil;
                });
                return;
            }
            NSString *newTempName = [[location.path stringByDeletingLastPathComponent] stringByAppendingPathComponent:_game.file.lastPathComponent];
            [[NSFileManager defaultManager] moveItemAtPath:location.path toPath:newTempName error:nil];
            [[GBROMManager sharedManager] importROM:newTempName withName:_game.title keepOriginal:false];
            dispatch_async(dispatch_get_main_queue(), ^{
                self.navigationItem.rightBarButtonItem.title = @"Open";
                self.navigationItem.rightBarButtonItem.customView = nil;
            });
        }] resume];
    }
}

- (void)tappedLabel:(UITapGestureRecognizer *)tap
{
    unsigned characterIndex = [(UILabel *)tap.view characterAtTap:tap];
    
    NSURL *url = [((UILabel *)tap.view).attributedText attribute:@"GBLinkAttribute" atIndex:characterIndex effectiveRange:NULL];
    
    if (url) {
        [[UIApplication sharedApplication] openURL:url options:nil completionHandler:nil];
        return;
    }
    
    NSString *tag = [((UILabel *)tap.view).attributedText attribute:@"GBHubTag" atIndex:characterIndex effectiveRange:NULL];
    
    if (tag) {
        UINavigationItem *parent = self.navigationController.navigationBar.items[self.navigationController.navigationBar.items.count - 2];
        [self.navigationController popViewControllerAnimated:true];
        if (@available(iOS 13.0, *)) {
            parent.searchController.searchBar.searchTextField.text = @"";
            parent.searchController.searchBar.searchTextField.tokens = nil;
            UISearchToken *token = [UISearchToken tokenWithIcon:nil text:tag];
            token.representedObject = tag;
            [parent.searchController.searchBar.searchTextField insertToken:token atIndex:0];
        }
        else {
            parent.searchController.searchBar.text = tag;
        }
        parent.searchController.active = true;
        return;
    }
    
}

- (void)viewDidLayoutSubviews
{
    unsigned y = 12;
    CGSize size = [_titleLabel sizeThatFits:(CGSize){_scrollView.bounds.size.width - 32, INFINITY}];;
    _titleLabel.frame = (CGRect){{16, y}, {_scrollView.bounds.size.width - 32, size.height}};
    y += size.height + 24;
    
    if (_screenshotsScrollView) {
        _screenshotsScrollView.frame = CGRectMake(0, y, _scrollView.bounds.size.width, _screenshotsScrollView.frame.size.height);
        y += _screenshotsScrollView.frame.size.height + 8;
        if (_game.screenshots.count == 1) {
            CGRect frame = _screenshotsScrollView.frame;
            frame.origin.x = (_scrollView.bounds.size.width - _screenshotsScrollView.contentSize.width) / 2;
            _screenshotsScrollView.frame = frame;
        }
    }
    
    size = [_descriptionLabel sizeThatFits:(CGSize){_scrollView.bounds.size.width - 32, INFINITY}];;
    _descriptionLabel.frame = (CGRect){{16, y}, {_scrollView.bounds.size.width - 32, size.height}};
    y += size.height;
    
    _scrollView.contentSize = CGSizeMake(_scrollView.bounds.size.width, y);
}

@end

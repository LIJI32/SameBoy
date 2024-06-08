#import "GBHubViewController.h"
#import "GBHub.h"
#import "GBHubGameViewController.h"
#import "GBHubCell.h"
#import "UILabel+TapLocation.h"

@interface GBHubViewController() <UISearchResultsUpdating>
@end

@implementation GBHubViewController
{
    UISearchController *_searchController;
    NSMutableDictionary<NSURL *, UIImage *> *_imageCache;
    NSArray<GBHubGame *> *_results;
    NSString *_resultsTitle;
    bool _showingAllGames;
}

- (instancetype)init
{
    self = [self initWithStyle:UITableViewStyleGrouped];
    [[NSNotificationCenter defaultCenter] addObserver:self.tableView
                                             selector:@selector(reloadData)
                                                 name:GBHubStatusChangedNotificationName
                                               object:nil];
    _imageCache = [NSMutableDictionary dictionary];
    self.tableView.rowHeight = UITableViewAutomaticDimension;
    [GBHub.sharedHub refresh];
    return self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.navigationItem.searchController =
        _searchController = [[UISearchController alloc] initWithSearchResultsController:nil];
    _searchController.searchResultsUpdater = self;
    self.tableView.scrollsToTop = true;
    self.navigationItem.hidesSearchBarWhenScrolling = false;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    UIColor *labelColor;
    UIColor *secondaryLabelColor;
    if (@available(iOS 13.0, *)) {
        labelColor = UIColor.labelColor;
        secondaryLabelColor = UIColor.secondaryLabelColor;
    }
    else {
        labelColor = UIColor.blackColor;
        secondaryLabelColor = [UIColor colorWithWhite:0.55 alpha:1.0];
    }
    switch (GBHub.sharedHub.status) {
        case GBHubStatusNotReady: return nil;
        case GBHubStatusInProgress: {
            UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:0];
            UIActivityIndicatorViewStyle style = UIActivityIndicatorViewStyleWhite;
            if (@available(iOS 13.0, *)) {
                style = UIActivityIndicatorViewStyleMedium;
            }
            
            UIActivityIndicatorView *spinner = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:style];
            cell.bounds = spinner.bounds;
            [cell addSubview:spinner];
            [spinner startAnimating];
            spinner.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
            return cell;
        }
        case GBHubStatusReady: {
            if (indexPath.section == 0) {
                GBHubCell *cell = [[GBHubCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:0];
                cell.game = _results? _results[indexPath.item] : GBHub.sharedHub.showcaseGames[indexPath.item];
                NSMutableAttributedString *text = [[NSMutableAttributedString alloc] initWithString:cell.game.title
                                                                                         attributes:@{
                    NSFontAttributeName: [UIFont boldSystemFontOfSize:UIFont.labelFontSize],
                    NSForegroundColorAttributeName: labelColor
                }];
                [text appendAttributedString:[[NSAttributedString alloc] initWithString:[NSString stringWithFormat:@" by %@\n",
                                                                                                                   cell.game.developer]
                                                                             attributes:@{
                    NSFontAttributeName: [UIFont systemFontOfSize:UIFont.labelFontSize],
                    NSForegroundColorAttributeName: labelColor
                }]];
                [text appendAttributedString:[[NSAttributedString alloc] initWithString:cell.game.entryDescription ?: [cell.game.tags componentsJoinedByString:@", "]
                                                                             attributes:@{
                    NSFontAttributeName: [UIFont systemFontOfSize:UIFont.smallSystemFontSize],
                    NSForegroundColorAttributeName: secondaryLabelColor
                }]];
                cell.textLabel.attributedText = text;
                cell.textLabel.numberOfLines = 2;
                cell.textLabel.lineBreakMode = NSLineBreakByTruncatingTail;
                
                static UIImage *emptyImage = nil;
                static dispatch_once_t onceToken;
                dispatch_once(&onceToken, ^{
                    UIGraphicsBeginImageContextWithOptions((CGSize){60, 60}, false, tableView.window.screen.scale);
                    UIBezierPath *mask = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(0, 3, 60, 54) cornerRadius:4];
                    [mask addClip];
                    [[UIColor whiteColor] set];
                    [mask fill];
                    if (@available(iOS 13.0, *)) {
                        [[UIColor tertiaryLabelColor] set];
                    }
                    else {
                        [[UIColor colorWithWhite:0 alpha:0.5] set];
                    }
                    [mask stroke];
                    emptyImage = UIGraphicsGetImageFromCurrentImageContext();
                    UIGraphicsEndImageContext();

                });
                cell.imageView.image = emptyImage;
                return cell;
            }
            UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:0];
            cell.selectionStyle = UITableViewCellSelectionStyleBlue;
            NSString *tag = GBHub.sharedHub.sortedTags[indexPath.item];
            cell.textLabel.text = tag;
            unsigned count = [GBHub.sharedHub countForTag:tag];
            if (count == 1) {
                cell.detailTextLabel.text = @"1 Game";
            }
            else {
                cell.detailTextLabel.text = [NSString stringWithFormat:@"%u Games", count];
            }
            cell.textLabel.numberOfLines = 2;
            cell.textLabel.lineBreakMode = NSLineBreakByWordWrapping;
            return cell;
        }
        case GBHubStatusError: {
            UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:0];
            cell.textLabel.text = @"Could not connect to Homebrew Hub";
            return cell;
        }
    }
    return nil;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    switch (GBHub.sharedHub.status) {
        case GBHubStatusNotReady: return 0;
        case GBHubStatusInProgress: return 1;
        case GBHubStatusReady: {
            if (_results) return _results.count;
            if (section == 0) return GBHub.sharedHub.showcaseGames.count;
            return GBHub.sharedHub.sortedTags.count;
        }
        case GBHubStatusError: return 1;
    }
    return 0;
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
    switch (GBHub.sharedHub.status) {
        case GBHubStatusNotReady: return 0;
        case GBHubStatusInProgress: return 1;
        case GBHubStatusReady: return _results? 1 : 2;
        case GBHubStatusError: return 1;
    }
    return 0;
}

- (NSString *)title
{
    return @"Homebrew Hub";
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section
{
    if (GBHub.sharedHub.status != GBHubStatusReady) return nil;
    if (section == 0) return _results? _resultsTitle : @"Homebrew Showcase";
    return @"Categories";
}

- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (GBHub.sharedHub.status == GBHubStatusReady && indexPath.section == 0) {
        return 60;
    }
    return 45;
}

- (void)tableView:(UITableView *)tableView willDisplayCell:(GBHubCell *)cell forRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (![cell isKindOfClass:[GBHubCell class]]) return;
    if (!cell.game.screenshots.count) return;
    
    NSURL *url = cell.game.screenshots[0];
    UIImage *image = _imageCache[url];
    if ([image isKindOfClass:[UIImage class]]) {
        cell.imageView.image = image;
        return;
    }
    if (!image) {
        _imageCache[url] = (id)[NSNull null];
        [[[NSURLSession sharedSession] downloadTaskWithURL:url
                                         completionHandler:^(NSURL *location, NSURLResponse *response, NSError *error) {
            if (!location) return;
            dispatch_sync(dispatch_get_main_queue(), ^{
                UIGraphicsBeginImageContextWithOptions((CGSize){60, 60}, false, tableView.window.screen.scale);
                UIBezierPath *mask = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(0, 3, 60, 54) cornerRadius:4];
                [mask addClip];
                UIImage *image = [UIImage imageWithContentsOfFile:location.path];
                [image drawInRect:mask.bounds];
                if (@available(iOS 13.0, *)) {
                    [[UIColor tertiaryLabelColor] set];
                }
                else {
                    [[UIColor colorWithWhite:0 alpha:0.5] set];
                }
                [mask stroke];
                _imageCache[url] = cell.imageView.image = UIGraphicsGetImageFromCurrentImageContext();
                UIGraphicsEndImageContext();
                [tableView reloadRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationNone];
            });
        }] resume];
    }
}

- (void)updateSearchResultsForSearchController:(UISearchController *)searchController
{
    static unsigned cookie = 0;
    NSArray<NSString *> *keywords = [GBSearchCanonicalString([searchController.searchBar.text
                                                              stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet])
                                     componentsSeparatedByString:@" "];
    if (keywords.count == 1 && keywords[0].length == 0) {
        keywords = @[];
    }
    NSArray *tokens = nil;
    if (@available(iOS 13.0, *)) {
        tokens = searchController.searchBar.searchTextField.tokens;
    }

    if (!searchController.isActive && tokens.count == 0 && !keywords.count) {
        cookie++;
        _results = nil;
        _showingAllGames = false;
        [self.tableView reloadData];
        [self.tableView scrollToRowAtIndexPath:[NSIndexPath indexPathWithIndexes:(NSUInteger[]){0,0} length:2]
                              atScrollPosition:UITableViewScrollPositionTop
                                      animated:false];
        return;
    }
    if (tokens.count || keywords.count) {
        _showingAllGames = false;
        cookie++;
        unsigned myCookie = cookie;
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            NSMutableArray<GBHubGame *> *results = [NSMutableArray array];
            for (GBHubGame *game in GBHub.sharedHub.allGames.allValues) {
                bool matches = true;
                if (@available(iOS 13.0, *)) {
                    for (UISearchToken *token in tokens) {
                        if (![game.tags containsObject:token.representedObject]) {
                            matches = false;
                            break;
                        }
                    }
                    if (!matches) continue;
                }
                for (NSString *keyword in keywords) {
                    if (keyword.length == 0) continue;
                    if (![game.keywords containsString:keyword]) {
                        matches = false;
                        break;
                    }
                }
                if (matches) {
                    [results addObject:game];
                }
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                if (myCookie != cookie) return;
                if (tokens.count) {
                    if (searchController.searchBar.text.length) {
                        _resultsTitle = [NSString stringWithFormat:@"Showing %@ games matching “%@”", [tokens[0] representedObject], searchController.searchBar.text];
                    }
                    else {
                        _resultsTitle = [NSString stringWithFormat:@"Showing %@ games", [tokens[0] representedObject]];
                    }
                }
                else {
                    _resultsTitle = [NSString stringWithFormat:@"Showing results for “%@”", searchController.searchBar.text];
                }
                _results = results;
                _results = [results sortedArrayUsingComparator:^NSComparisonResult(GBHubGame *obj1, GBHubGame *obj2) {
                    return [obj1.title.lowercaseString compare:obj2.title.lowercaseString];
                }];
                [self.tableView reloadData];
                if (_results.count) {
                    [self.tableView scrollToRowAtIndexPath:[NSIndexPath indexPathWithIndexes:(NSUInteger[]){0,0} length:2]
                                          atScrollPosition:UITableViewScrollPositionTop
                                                  animated:false];
                }
            });
        });
    }
    else {
        if (_showingAllGames) return;
        cookie++;
        _showingAllGames = true;
        _resultsTitle = @"Showing all games";
        _results = [GBHub.sharedHub.allGames.allValues sortedArrayUsingComparator:^NSComparisonResult(GBHubGame *obj1, GBHubGame *obj2) {
            return [obj1.title.lowercaseString compare:obj2.title.lowercaseString];
        }];
        [self.tableView reloadData];
        if (_results.count) {
            [self.tableView scrollToRowAtIndexPath:[NSIndexPath indexPathWithIndexes:(NSUInteger[]){0,0} length:2]
                                  atScrollPosition:UITableViewScrollPositionTop
                                          animated:false];
        }
        return;
    }
}

- (NSIndexPath *)tableView:(UITableView *)tableView willSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (GBHub.sharedHub.status == GBHubStatusReady) return indexPath;
    return nil;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (GBHub.sharedHub.status != GBHubStatusReady) return;
    if (indexPath.section == 1) {
        NSString *tag = GBHub.sharedHub.sortedTags[indexPath.item];
        if (@available(iOS 13.0, *)) {
            UISearchToken *token = [UISearchToken tokenWithIcon:nil
                                                           text:tag];
            token.representedObject = tag;
            [_searchController.searchBar.searchTextField insertToken:token
                                                             atIndex:0];
        }
        else {
            _searchController.searchBar.text = tag;
        }
        [_searchController setActive:true];
        return;
    }
    
    GBHubCell *cell = [tableView cellForRowAtIndexPath:indexPath];
    if ([cell isKindOfClass:[GBHubCell class]]) {
        GBHubGameViewController *controller = [[GBHubGameViewController alloc] initWithGame:cell.game];
        [self.navigationController pushViewController:controller animated:true];
    }
}

- (void)tableView:(UITableView *)tableView willDisplayFooterView:(UIView *)view forSection:(NSInteger)section
{
    UIColor *linkColor;
    if (@available(iOS 13.0, *)) {
        linkColor = UIColor.linkColor;
        
    }
    else {
        linkColor = UIColor.blueColor;
    }
    
    if (section != [self numberOfSectionsInTableView:nil] - 1) return;
    UITableViewHeaderFooterView *footer = (UITableViewHeaderFooterView *)view;
    NSMutableAttributedString *string = footer.textLabel.attributedText.mutableCopy;

    [string addAttributes:@{
        @"GBLinkAttribute": [NSURL URLWithString:@"https://hh.gbdev.io"],
        NSForegroundColorAttributeName: linkColor,
    } range:[string.string rangeOfString:@"Homebrew Hub"]];
    
    footer.textLabel.attributedText = string;
    footer.textLabel.userInteractionEnabled = true;
    [footer.textLabel addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self
                                                                                   action:@selector(tappedFooterLabel:)]];
}

- (void)tappedFooterLabel:(UITapGestureRecognizer *)tap
{
    unsigned characterIndex = [(UILabel *)tap.view characterAtTap:tap];
    
    NSURL *url = [((UILabel *)tap.view).attributedText attribute:@"GBLinkAttribute" atIndex:characterIndex effectiveRange:NULL];
    
    if (url) {
        [[UIApplication sharedApplication] openURL:url options:nil completionHandler:nil];
    }
}

- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section
{
    if (section != [self numberOfSectionsInTableView:tableView] - 1) return nil;
    return @"Powered by Homebrew Hub";
}
@end

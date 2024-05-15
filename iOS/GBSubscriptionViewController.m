#ifdef APPSTORE
#import "GBSubscriptionViewController.h"
#import "GBSubscriptionManager.h"

@interface GBSubscriptionViewController()<SKProductsRequestDelegate>

@end

@implementation GBSubscriptionViewController
{
    SKProductsRequest *_request;
    NSArray<SKProduct *> *_products;
}


- (void)productsRequest:(SKProductsRequest *)request didReceiveResponse:(SKProductsResponse *)response
{
    dispatch_async(dispatch_get_main_queue(), ^{
        _products = [response.products sortedArrayUsingComparator:^NSComparisonResult(id  obj1, id  obj2) {
            if ([obj1 price].doubleValue > [obj2 price].doubleValue) {
                return NSOrderedDescending;
            }
            return NSOrderedAscending;
        }];
        if (_products.count == 0) {
            UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Could not connect to the App Store"
                                                                           message:@"Could not obtain a list of subscription tiers from the App Store, make sure your device is online."
                                                                    preferredStyle:UIAlertControllerStyleAlert];
            [alert  addAction:[UIAlertAction actionWithTitle:@"Close"
                                                       style:UIAlertActionStyleCancel
                                                     handler:^(UIAlertAction *action) {
                [self.presentingViewController dismissViewControllerAnimated:true completion:nil];
            }]];
            [self presentViewController:alert animated:true completion:nil];
            return;
        }
        [self.tableView reloadData];
    });
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
    if (GBSubscriptionManager.defaultManager.state != GBSubscriptionActive) {
        return 2;
    }
    return 1;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    if (section == 1) return 1;
    return _products.count;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (indexPath.section == 1) {
        [[SKPaymentQueue defaultQueue] restoreCompletedTransactions];
        [self deselectRow];
        return;
    }
    
    SKPayment *payment = [SKPayment paymentWithProduct:_products[indexPath.row]];
    
    [[SKPaymentQueue defaultQueue] addPayment:payment];
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    if (indexPath.section == 1) {
        cell.textLabel.text = @"Restore subscription";
        cell.detailTextLabel.text = @"Restore from a different device or Apple ID";
        return cell;
    }
    SKProduct *tier = _products[indexPath.row];
    cell.textLabel.text = tier.localizedTitle;
    UILabel *priceLabel = [[UILabel alloc] init];
    priceLabel.textColor = [UIColor systemBlueColor];
    NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
    formatter.numberStyle = NSNumberFormatterCurrencyStyle;
    formatter.locale = tier.priceLocale;
    
    NSString *periodString = nil;
    unsigned number = tier.subscriptionPeriod.numberOfUnits;
    switch (tier.subscriptionPeriod.unit) {
        case SKProductPeriodUnitDay:
            if (number == 1) periodString = @"day";
            else periodString = [NSString stringWithFormat:@"%u days", number];
            break;
        case SKProductPeriodUnitWeek:
            if (number == 1) periodString = @"week";
            else periodString = [NSString stringWithFormat:@"%u weeks", number];
            break;
        case SKProductPeriodUnitMonth:
            if (number == 1) periodString = @"month";
            else periodString = [NSString stringWithFormat:@"%u months", number];
            break;
        case SKProductPeriodUnitYear:
            if (number == 1) periodString = @"year";
            else periodString = [NSString stringWithFormat:@"%u years", number];
            break;
    }
    priceLabel.text = [NSString stringWithFormat:@"%@/%@",[formatter stringFromNumber:tier.price], periodString];
    
    GBSubscriptionManager *subManager = GBSubscriptionManager.defaultManager;
    NSDateFormatter *dateFormatter = [[NSDateFormatter alloc] init];
    dateFormatter.locale = [NSLocale currentLocale];
    dateFormatter.dateStyle = NSDateFormatterShortStyle;
    
    if ([tier.productIdentifier isEqual:subManager.activeSubscription[@"product_id"]]) {
        if (@available(iOS 13.0, *)) {
            UIImage *check = [[UIImage systemImageNamed:@"checkmark.circle.fill"] imageWithTintColor:[UIColor systemBlueColor]];
            NSMutableAttributedString *string = [NSAttributedString attributedStringWithAttachment:[NSTextAttachment textAttachmentWithImage:check]].mutableCopy;
            priceLabel.text = [NSString stringWithFormat:@" %@", priceLabel.text];
            [string appendAttributedString:priceLabel.attributedText];
            priceLabel.attributedText = string;
            if (subManager.pendingSubscription) {
                cell.detailTextLabel.text = [NSString stringWithFormat:@"Expires on %@", [dateFormatter stringFromDate:subManager.activeSubscription[@"expires_date"]]];
                
            }
        }
    }
    else if ([tier.productIdentifier isEqual:subManager.pendingSubscription[@"product_id"]]) {
        if (@available(iOS 13.0, *)) {
            UIImage *check = [[UIImage systemImageNamed:@"checkmark.circle"] imageWithTintColor:[UIColor systemBlueColor]];
            NSMutableAttributedString *string = [NSAttributedString attributedStringWithAttachment:[NSTextAttachment textAttachmentWithImage:check]].mutableCopy;
            priceLabel.text = [NSString stringWithFormat:@" %@", priceLabel.text];
            [string appendAttributedString:priceLabel.attributedText];
            priceLabel.attributedText = string;
            if (subManager.pendingSubscription) {
                cell.detailTextLabel.text = [NSString stringWithFormat:@"Starts on %@", [dateFormatter stringFromDate:subManager.activeSubscription[@"expires_date"]]];
            }
        }
    }
    
    else if ([tier.productIdentifier isEqual:subManager.expiredSubscription[@"product_id"]]) {
        cell.detailTextLabel.text = [NSString stringWithFormat:@"Expired on %@", [dateFormatter stringFromDate:subManager.expiredSubscription[@"expires_date"]]];
    }
    
    [priceLabel sizeToFit];
    cell.accessoryView = priceLabel;

    
    return cell;
}

- (void)configureHeaderLabel:(UILabel *)label
{
     if (@available(iOS 13.0, *)) {
         NSMutableAttributedString *string = [[NSMutableAttributedString alloc] initWithString:@"Support SameBoy\n"
                                                                                    attributes:@{
            NSFontAttributeName: [UIFont systemFontOfSize:34 weight:UIFontWeightBold],
            NSForegroundColorAttributeName: [UIColor labelColor],
            NSParagraphStyleAttributeName: [NSParagraphStyle defaultParagraphStyle],
         }];
         NSMutableParagraphStyle *style = [NSParagraphStyle defaultParagraphStyle].mutableCopy;
         style.paragraphSpacing = -8;
         NSAttributedString *paragraph = [[NSAttributedString alloc] initWithString:@"\nSupport SameBoy's development with a monthly subscription and gain access to exclusive themes."
         attributes:@{
             NSFontAttributeName: [UIFont preferredFontForTextStyle:UIFontTextStyleCallout],
             NSForegroundColorAttributeName: [UIColor labelColor],
             NSParagraphStyleAttributeName: style,
         }];
         [string appendAttributedString:paragraph];
         
         style = style.mutableCopy;
         style.paragraphSpacing = 0;
         paragraph = [[NSAttributedString alloc] initWithString:@"\n\nAll subscription tiers offer access to all available themes. Choose the price that suits you best."
                                                                         attributes:@{
            NSFontAttributeName: [UIFont preferredFontForTextStyle:UIFontTextStyleCallout],
            NSForegroundColorAttributeName: [UIColor labelColor],
            NSParagraphStyleAttributeName: style,
         }];
         [string appendAttributedString:paragraph];
         label.attributedText = string;
         label.textColor = [UIColor labelColor];
         label.lineBreakMode = NSLineBreakByWordWrapping;
         label.numberOfLines = 0;
     }
}

- (UIView *)tableView:(UITableView *)tableView viewForHeaderInSection:(NSInteger)section
{
    UILabel *label = [[UILabel alloc] init];
    [self configureHeaderLabel:label];
    return label;

}

- (CGFloat)tableView:(UITableView *)tableView heightForHeaderInSection:(NSInteger)section
{
    UILabel *label = [[UILabel alloc] init];
    [self configureHeaderLabel:label];
    return ceil([label textRectForBounds:(CGRect){{0,0}, {tableView.bounds.size.width - 32, INFINITY}} limitedToNumberOfLines:16].size.height + 24);
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section
{
    if (section == 1) return nil;
    return @" ";
}

- (void)tableView:(UITableView *)tableView willDisplayFooterView:(UIView *)view forSection:(NSInteger)section
{
    if (section != [self numberOfSectionsInTableView:nil] - 1) return;
    UITableViewHeaderFooterView *footer = (UITableViewHeaderFooterView *)view;
    NSMutableAttributedString *string = footer.textLabel.attributedText.mutableCopy;
    [string addAttributes:@{
        NSLinkAttributeName: [NSURL URLWithString:@"https://github.com/sponsors/LIJI32"],
    } range:[string.string rangeOfString:@"further support SameBoy's development on GitHub Sponsors"]];
    
    [string addAttributes:@{
        NSLinkAttributeName: [NSURL URLWithString:@"https://sameboy.github.io/privacy/"],
    } range:[string.string rangeOfString:@"Privacy Policy"]];
    
    [string addAttributes:@{
        NSLinkAttributeName: [NSURL URLWithString:@"https://www.apple.com/legal/internet-services/itunes/dev/stdeula/"],
    } range:[string.string rangeOfString:@"standard Apple Terms of Use (EULA)"]];

    
    footer.textLabel.attributedText = string;
    footer.textLabel.userInteractionEnabled = true;
    [footer.textLabel addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self
                                                                                   action:@selector(tappedFooterLabel:)]];
}

- (void)tappedFooterLabel:(UITapGestureRecognizer *)tap
{
    UILabel *textLabel = (UILabel *)tap.view;
    CGPoint tapLocation = [tap locationInView:textLabel];
    
    NSTextStorage *textStorage = [[NSTextStorage alloc] initWithString:textLabel.attributedText.string
                                                            attributes:@{
        NSFontAttributeName: textLabel.font
    }];
    NSLayoutManager *layoutManager = [[NSLayoutManager alloc] init];
    [textStorage addLayoutManager:layoutManager];
    
    NSTextContainer *textContainer = [[NSTextContainer alloc] initWithSize:CGSizeMake(textLabel.frame.size.width,
                                                                                      textLabel.frame.size.height + 256)];
    textContainer.lineFragmentPadding = 0;
    textContainer.maximumNumberOfLines = 16;
    textContainer.lineBreakMode = NSLineBreakByWordWrapping;
    
    [layoutManager addTextContainer:textContainer];
    
    unsigned characterIndex = [layoutManager characterIndexForPoint:tapLocation
                                                    inTextContainer:textContainer
                           fractionOfDistanceBetweenInsertionPoints:NULL];
    
    NSURL *url = [textLabel.attributedText attribute:NSLinkAttributeName atIndex:characterIndex effectiveRange:NULL];

    if (url) {
        [[UIApplication sharedApplication] openURL:url options:nil completionHandler:nil];
    }
}


- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section
{
    if (section != [self numberOfSectionsInTableView:nil] - 1) return nil;
    return @"You can additionally further support SameBoy's development on GitHub Sponsors. Note that GitHub sponsorships do not unlock in-app themes.\n\nSupporter subscriptions are subject to the Privacy Policy and the standard Apple Terms of Use (EULA).";
}

- (void)viewDidLoad
{
    [[NSNotificationCenter defaultCenter] addObserver:self.tableView
                                             selector:@selector(reloadData)
                                                 name:GBSubscriptionInfoUpdatedNotification
                                               object:nil];
    
    if (GBSubscriptionManager.defaultManager.activeSubscription || GBSubscriptionManager.defaultManager.expiredSubscription) {
        [[SKPaymentQueue defaultQueue] restoreCompletedTransactions];
    }
    static SKProductsRequest *request = nil;
    request = [[SKProductsRequest alloc] initWithProductIdentifiers: [NSSet setWithObjects:@"Subscription1", @"Subscription2", @"Subscription3", @"Subscription4", @"Subscription5", @"Subscription6", @"Subscription6", @"Subscription7", @"Subscription8", @"Subscription9", nil]];
    
    [request setDelegate: self];
    [request start];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(deselectRow)
                                                 name:UIApplicationDidBecomeActiveNotification
                                               object:nil];
}

- (void)deselectRow
{
    if (self.tableView.indexPathForSelectedRow) {
        [self.tableView deselectRowAtIndexPath:self.tableView.indexPathForSelectedRow animated:true];
    }
}

- (instancetype)init
{
    if (@available(iOS 13.0, *)) {
        return [super initWithStyle:UITableViewStyleInsetGrouped];
    } else {
        return [super initWithStyle:UITableViewStyleGrouped];
    }
}

- (NSString *)title
{
    return nil;
}

@end
#endif

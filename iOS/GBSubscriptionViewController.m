#ifdef APPSTORE
#import "GBSubscriptionViewController.h"
#import "GBSubscriptionManager.h"
#import "UILabel+TapLocation.h"

@interface GBSubscriptionViewController()<SKProductsRequestDelegate>

@end

@implementation GBSubscriptionViewController
{
    SKProductsRequest *_request;
    NSArray<SKProduct *> *_products;
    unsigned _subscriptionsCount;
    enum {
        SubscriptionsSection,
        LifetimeSection,
        RestoreSection,
    } _sections[3];
}


- (void)productsRequest:(SKProductsRequest *)request didReceiveResponse:(SKProductsResponse *)response
{
    dispatch_async(dispatch_get_main_queue(), ^{
        _products = [response.products sortedArrayUsingComparator:^NSComparisonResult(SKProduct *obj1, SKProduct *obj2) {
            if (!!obj1.subscriptionPeriod != !!obj2.subscriptionPeriod) {
                return obj2.subscriptionPeriod? NSOrderedDescending : NSOrderedAscending;
            }
            
            if ([obj1 price].doubleValue > [obj2 price].doubleValue) {
                return NSOrderedDescending;
            }
            return NSOrderedAscending;
        }];
        for (SKProduct *product in _products) {
            if (product.subscriptionPeriod) {
                _subscriptionsCount++;
            }
        }
        if (_products.count == 0) {
            UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Could not connect to the App Store"
                                                                           message:@"Could not obtain a list of support options from the App Store, make sure your device is online."
                                                                    preferredStyle:UIAlertControllerStyleAlert];
            [alert addAction:[UIAlertAction actionWithTitle:@"Close"
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
    unsigned sections = 0;
    _sections[sections++] = SubscriptionsSection;
    if ((GBSubscriptionManager.defaultManager.themeState != GBSubscriptionPermanent ||
         GBSubscriptionManager.defaultManager.watchState != GBSubscriptionPermanent) &&
        _products.count != _subscriptionsCount) {
        _sections[sections++] = LifetimeSection;
    }
    if ((GBSubscriptionManager.defaultManager.themeState != GBSubscriptionActive &&
         GBSubscriptionManager.defaultManager.themeState != GBSubscriptionPermanent) ||
        (GBSubscriptionManager.defaultManager.watchState != GBSubscriptionActive &&
         GBSubscriptionManager.defaultManager.watchState != GBSubscriptionPermanent)) {
        _sections[sections++] = RestoreSection;
    }
    return sections;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    switch (_sections[section]) {
        case SubscriptionsSection: return _subscriptionsCount;
        case LifetimeSection: return _products.count - _subscriptionsCount;
        case RestoreSection: return 1;
    }
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
    switch (_sections[indexPath.section]) {
        case SubscriptionsSection: {
            SKPayment *payment = [SKPayment paymentWithProduct:_products[indexPath.row]];
            [[SKPaymentQueue defaultQueue] addPayment:payment];
            return;
        }
        case LifetimeSection: {
            SKPayment *payment = [SKPayment paymentWithProduct:_products[indexPath.row + _subscriptionsCount]];
            GBSubscriptionManager *subManager = GBSubscriptionManager.defaultManager;
            if (([payment.productIdentifier hasPrefix:@"Lifetime"] && subManager.themeState == GBSubscriptionPermanent) ||
                ([payment.productIdentifier hasPrefix:@"Watch"] && subManager.watchState == GBSubscriptionPermanent)) {
                [self.tableView deselectRowAtIndexPath:indexPath animated:true];
                return;
            }
            [[SKPaymentQueue defaultQueue] addPayment:payment];
            return;
        }
        case RestoreSection: {
            [[SKPaymentQueue defaultQueue] restoreCompletedTransactions];
            [self deselectRow];
            return;
        }
    }
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    if (_sections[indexPath.section] == RestoreSection) {
        cell.textLabel.text = @"Restore purchases";
        cell.detailTextLabel.text = @"Restore from a different device or Apple ID";
        return cell;
    }
    
    unsigned productIndex = indexPath.row;
    if (_sections[indexPath.section] == LifetimeSection) {
        productIndex += _subscriptionsCount;
    }
    SKProduct *tier = _products[productIndex];
    
    cell.textLabel.text = tier.localizedTitle;
    UILabel *priceLabel = [[UILabel alloc] init];
    priceLabel.textColor = [UIColor systemBlueColor];
    NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
    formatter.numberStyle = NSNumberFormatterCurrencyStyle;
    formatter.locale = tier.priceLocale;
    
    NSString *periodString = @"";
    if (tier.subscriptionPeriod) {
        unsigned number = tier.subscriptionPeriod.numberOfUnits;
        switch (tier.subscriptionPeriod.unit) {
            case SKProductPeriodUnitDay:
                if (number == 1) periodString = @"/day";
                else periodString = [NSString stringWithFormat:@"/%u days", number];
                break;
            case SKProductPeriodUnitWeek:
                if (number == 1) periodString = @"/week";
                else periodString = [NSString stringWithFormat:@"/%u weeks", number];
                break;
            case SKProductPeriodUnitMonth:
                if (number == 1) periodString = @"/month";
                else periodString = [NSString stringWithFormat:@"/%u months", number];
                break;
            case SKProductPeriodUnitYear:
                if (number == 1) periodString = @"/year";
                else periodString = [NSString stringWithFormat:@"/%u years", number];
                break;
        }
    }
    priceLabel.text = [NSString stringWithFormat:@"%@%@",[formatter stringFromNumber:tier.price], periodString];
    
    GBSubscriptionManager *subManager = GBSubscriptionManager.defaultManager;
    NSDateFormatter *dateFormatter = [[NSDateFormatter alloc] init];
    dateFormatter.locale = [NSLocale currentLocale];
    dateFormatter.dateStyle = NSDateFormatterShortStyle;
    
    if (([tier.productIdentifier hasPrefix:@"Lifetime"] && subManager.themeState == GBSubscriptionPermanent) ||
        ([tier.productIdentifier hasPrefix:@"Watch"] && subManager.watchState == GBSubscriptionPermanent)) {
        priceLabel = nil;
        cell.accessoryType = UITableViewCellAccessoryCheckmark;
    }
    else if ([tier.productIdentifier isEqual:subManager.activeSubscription[@"product_id"]]) {
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
            NSParagraphStyleAttributeName: [NSParagraphStyle defaultParagraphStyle],
         }];
         NSMutableParagraphStyle *style = [NSParagraphStyle defaultParagraphStyle].mutableCopy;
         style.paragraphSpacing = -8;
         if (GBSubscriptionManager.defaultManager.themeState == GBSubscriptionPermanent &&
             GBSubscriptionManager.defaultManager.watchState == GBSubscriptionPermanent) {
             NSAttributedString *paragraph = [[NSAttributedString alloc] initWithString:@"\nThank you for purchasing lifetime access for themes and SameBoy for Apple Watch! If you wish to further support SameBoy's development, you can do so with a monthly supporter subscription."
                                                                             attributes:@{
                NSFontAttributeName: [UIFont preferredFontForTextStyle:UIFontTextStyleCallout],
                NSParagraphStyleAttributeName: style,
             }];
             [string appendAttributedString:paragraph];
         }
         else {
             NSAttributedString *paragraph = [[NSAttributedString alloc] initWithString:@"\nSameBoy is free and open source. Support SameBoy's development with a monthly subscription and gain access to SameBoy for Apple Watch and exclusive themes."
                                                                             attributes:@{
                NSFontAttributeName: [UIFont preferredFontForTextStyle:UIFontTextStyleCallout],
                NSParagraphStyleAttributeName: style,
             }];
             [string appendAttributedString:paragraph];
             
             style = style.mutableCopy;
             style.paragraphSpacing = 0;
             paragraph = [[NSAttributedString alloc] initWithString:@"\n\nAll subscription tiers offer access to all available themes and features. Choose the price that suits you best."
                                                         attributes:@{
                NSFontAttributeName: [UIFont preferredFontForTextStyle:UIFontTextStyleCallout],
                NSParagraphStyleAttributeName: style,
             }];
             [string appendAttributedString:paragraph];
         }
         label.attributedText = string;
         label.textColor = [UIColor labelColor];
         label.lineBreakMode = NSLineBreakByWordWrapping;
         label.numberOfLines = 0;
     }
}

- (void)configureLifetimeHeaderLabel:(UILabel *)label
{
    if (@available(iOS 13.0, *)) {
        NSAttributedString *paragraph = [[NSAttributedString alloc] initWithString:@"You can alternatively purchase lifetime access to SameBoy for Apple Watch or all themes with a single payment.\n"
                                                                        attributes:@{
            NSFontAttributeName: [UIFont preferredFontForTextStyle:UIFontTextStyleCallout],
        }];
        
        label.attributedText = paragraph;
        label.textColor = [UIColor labelColor];
        label.lineBreakMode = NSLineBreakByWordWrapping;
        label.numberOfLines = 0;
    }
}

- (UIView *)tableView:(UITableView *)tableView viewForHeaderInSection:(NSInteger)section
{
    switch (_sections[section]) {
        case SubscriptionsSection: {
            UILabel *label = [[UILabel alloc] init];
            [self configureHeaderLabel:label];
            return label;
        }
        case LifetimeSection: {
            UILabel *label = [[UILabel alloc] init];
            [self configureLifetimeHeaderLabel:label];
            return label;
        }
        case RestoreSection:
            return nil;
    }
}

- (CGFloat)tableView:(UITableView *)tableView heightForHeaderInSection:(NSInteger)section
{
    switch (_sections[section]) {
        case SubscriptionsSection: {
            UILabel *label = [[UILabel alloc] init];
            [self configureHeaderLabel:label];
            return ceil([label textRectForBounds:(CGRect){{0,0}, {tableView.bounds.size.width - 32, INFINITY}} limitedToNumberOfLines:16].size.height + 24);
        }
        case LifetimeSection: {
            UILabel *label = [[UILabel alloc] init];
            [self configureLifetimeHeaderLabel:label];
            return ceil([label textRectForBounds:(CGRect){{0,0}, {tableView.bounds.size.width - 32, INFINITY}} limitedToNumberOfLines:16].size.height);
        }
        case RestoreSection:
            return 0;
    }
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section
{
    switch (_sections[section]) {
        case SubscriptionsSection: return @" ";
        case LifetimeSection: return @" ";
        case RestoreSection: return nil;
    }
}

- (void)tableView:(UITableView *)tableView willDisplayFooterView:(UIView *)view forSection:(NSInteger)section
{
    if (section != [self numberOfSectionsInTableView:nil] - 1) return;
    UITableViewHeaderFooterView *footer = (UITableViewHeaderFooterView *)view;
    NSMutableAttributedString *string = footer.textLabel.attributedText.mutableCopy;
    UIColor *linkColor = [UIColor linkColor];
    [string addAttributes:@{
        @"GBLinkAttribute": [NSURL URLWithString:@"https://github.com/sponsors/LIJI32"],
        NSForegroundColorAttributeName: linkColor,
    } range:[string.string rangeOfString:@"further support SameBoy's development on GitHub Sponsors"]];
    
    [string addAttributes:@{
        @"GBLinkAttribute": [NSURL URLWithString:@"https://sameboy.github.io/privacy/"],
        NSForegroundColorAttributeName: linkColor,
    } range:[string.string rangeOfString:@"Privacy Policy"]];
    
    [string addAttributes:@{
        @"GBLinkAttribute": [NSURL URLWithString:@"https://www.apple.com/legal/internet-services/itunes/dev/stdeula/"],
        NSForegroundColorAttributeName: linkColor,
    } range:[string.string rangeOfString:@"standard Apple Terms of Use (EULA)"]];

    
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
    if (section != [self numberOfSectionsInTableView:nil] - 1) return nil;
    return @"You can additionally further support SameBoy's development on GitHub Sponsors. Note that GitHub sponsorships do not unlock in-app features or themes.\n\nTransactions are subject to the Privacy Policy and the standard Apple Terms of Use (EULA).";
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    [[NSNotificationCenter defaultCenter] addObserver:self.tableView
                                             selector:@selector(reloadData)
                                                 name:GBSubscriptionInfoUpdatedNotification
                                               object:nil];
    
    if (GBSubscriptionManager.defaultManager.activeSubscription || GBSubscriptionManager.defaultManager.expiredSubscription) {
        [[SKPaymentQueue defaultQueue] restoreCompletedTransactions];
    }
    static SKProductsRequest *request = nil;
    request = [[SKProductsRequest alloc] initWithProductIdentifiers: [NSSet setWithObjects:
                                                                      @"Subscription1", @"Subscription2", @"Subscription3",
                                                                      @"Subscription4", @"Subscription5", @"Subscription6",
                                                                      @"Subscription7", @"Subscription8", @"Subscription9",
                                                                      @"Lifetime1", @"Lifetime2", @"Lifetime3",
                                                                      @"Lifetime4", @"Lifetime5", @"Lifetime6",
                                                                      @"Lifetime7", @"Lifetime8", @"Lifetime9",
                                                                      @"Watch1", @"Watch2", @"Watch3", nil]];
    
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

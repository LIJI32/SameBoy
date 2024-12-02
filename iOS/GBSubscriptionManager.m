#ifdef APPSTORE
#import "GBSubscriptionManager.h"

@interface GBTrackedStream : NSInputStream
@property (readonly) size_t position;
@end

@implementation GBTrackedStream
{
    NSInputStream *_child;
}

- (NSInteger)read:(uint8_t *)buffer maxLength:(NSUInteger)len
{
    if ([_child read:buffer maxLength:len] != len) {
        @throw [NSException exceptionWithName:@"UnexpectedEndOfStream" reason:nil userInfo:nil];
    }
    _position += len;
    return len;
}

- (BOOL)getBuffer:(uint8_t **)buffer length:(NSUInteger *)len
{
    return [_child getBuffer:buffer length:len];
}

- (BOOL)hasBytesAvailable
{
    return [_child hasBytesAvailable];
}

- (void)open
{
    [_child open];
}

+ (instancetype)inputStreamWithData:(NSData *)data
{
    if (!data) data = [NSData data];
    GBTrackedStream *ret = [[self alloc] init];
    ret->_child = [NSInputStream inputStreamWithData:data];
    return ret;
}

+ (instancetype)inputStreamWithURL:(NSURL *)url
{
    GBTrackedStream *ret = [[self alloc] init];
    ret->_child = [NSInputStream inputStreamWithURL:url];
    return ret;
}

+ (instancetype)inputStreamWithFileAtPath:(NSString *)path
{
    GBTrackedStream *ret = [[self alloc] init];
    ret->_child = [NSInputStream inputStreamWithFileAtPath:path];
    return ret;
}
@end

@interface GBGenericDERObject : NSObject
@property (readonly) uint8_t tag;
@property (readonly) NSData *data;
@property (readonly) NSArray *children;
@end

@implementation GBGenericDERObject
{
@public
    uint8_t _tag;
    NSMutableData *_data;
    NSMutableArray *_children;
}

- (NSString *)description
{
    return [NSString stringWithFormat:@"<%@ %02x %@>", self.class, _tag, _data ?: _children];
}

- (NSString *)debugDescription
{
    return [NSString stringWithFormat:@"<%@ %02x %@>", self.class, _tag, (_data ?: _children).debugDescription];
}

@end

static id ParseDERStream(GBTrackedStream *input)
{
    uint8_t tag = 0xFF;
    [input read:&tag maxLength:1];
    size_t length = 0;
    [input read:(uint8_t *)&length maxLength:1];
    bool indeterminateLength = length == 0x80;
    if (length > 0x80) {
        uint8_t count = length - 0x80;
        length = 0;
        while (count--) {
            length <<= 8;
            [input read:(uint8_t *)&length maxLength:1];
        }
    }

    
    switch (tag) {
        case 2: // Integer
        {
            uint64_t ret = 0;
            while (length--) {
                ret <<= 8;
                [input read:(uint8_t *)&ret maxLength:1];
            }
            return @(ret);
        }
        case 4: { // Octet String
            NSMutableData *ret = [NSMutableData dataWithLength:length];
            [input read:ret.mutableBytes maxLength:length];
            return ret;
        }
        case 5: // NULL
            if (length) @throw [NSException exceptionWithName:@"MalformedNULL" reason:nil userInfo:nil];
            return [NSNull null];
        case 0xC:   // UTF8String
        case 0x16: { // IA5String
            NSMutableData *data = [NSMutableData dataWithLength:length];
            [input read:data.mutableBytes maxLength:length];
            return [[NSString alloc] initWithData:data encoding:tag == 0x16? NSASCIIStringEncoding : NSUTF8StringEncoding];
        }
        case 0x24: // SEQUENCE???
        case 0x30: // SEQUENCE
        case 0x31: // SET
        {
            NSMutableArray *ret = [NSMutableArray array];
            if (indeterminateLength) {
                while (true) {
                    if (!input.hasBytesAvailable) break;
                    GBGenericDERObject *object = ParseDERStream(input);
                    if ([object isKindOfClass:[GBGenericDERObject class]] &&
                        object.tag == 0 &&
                        object.data.length == 0) {
                        break;
                    }
                    [ret addObject:object];
                }
            }
            else {
                size_t end = input.position + length;
                while (input.position < end) {
                    [ret addObject:ParseDERStream(input)];
                }
                if (input.position != end) {
                    @throw [NSException exceptionWithName:@"Bad Length" reason:nil userInfo:nil];
                }
            }
            if (tag == 0x31) {
                return [NSSet setWithArray:ret];
            }
            return ret;
        }
        default: {
            GBGenericDERObject *ret = [[GBGenericDERObject alloc] init];
            ret->_tag = tag;
            if (tag & 0x80) {
                ret->_children = [NSMutableArray array];
                if (indeterminateLength) {
                    while (true) {
                        if (!input.hasBytesAvailable) break;
                        GBGenericDERObject *object = ParseDERStream(input);
                        if ([object isKindOfClass:[GBGenericDERObject class]] &&
                            object.tag == 0 &&
                            object.data.length == 0) {
                            break;
                        }
                        [ret->_children addObject:object];

                    }
                }
                else {
                    size_t end = input.position + length;
                    while (input.position < end) {
                        [ret->_children addObject:ParseDERStream(input)];
                    }
                    if (input.position != end) {
                        @throw [NSException exceptionWithName:@"Bad Length" reason:nil userInfo:nil];
                    }
                }
            }
            else {
                ret->_data = [NSMutableData dataWithLength:length];
                [input read:ret->_data.mutableBytes maxLength:length];
            }
            return ret;
        }
    }
}

static id ParseDER(NSData *data)
{
    GBTrackedStream *input = [GBTrackedStream inputStreamWithData:data];
    [input open];
    return ParseDERStream(input);
}

static NSData *VerifyAndExtractPKCS7(NSArray *data)
{
    if (![data isKindOfClass:[NSArray class]]) return nil;
    if (data.count != 2) return nil;
    GBGenericDERObject *identifier = data[0];
    if (![identifier isKindOfClass:[GBGenericDERObject class]]) return nil;
    if (identifier.tag != 6) return nil;
    if (![identifier.data isEqual:[NSData dataWithBytesNoCopy:"\x2A\x86\x48\x86\xF7\x0D\x01\x07\x02" length:9 freeWhenDone:false]]) return nil;
    
    GBGenericDERObject *content = data[1];
    if (![content isKindOfClass:[GBGenericDERObject class]]) return nil;
    if (content.tag != 0xa0) return nil;
    if (content.children.count != 1) return nil;
    
    NSArray *signedData = content.children[0];
    if (![signedData isKindOfClass:[NSArray class]]) return nil;
    if (signedData.count != 5) return nil;
    
    __unused NSNumber *version         = signedData[0];
    __unused NSSet *digestAlgorithms   = signedData[1];
    NSArray *encapContentInfo = signedData[2];
    __unused NSSet *certificateSet     = signedData[3];
    __unused NSSet *signerInfos        = signedData[4];
    
    if (![encapContentInfo isKindOfClass:[NSArray class]]) return nil;
    if (encapContentInfo.count != 2) return nil;

    identifier = encapContentInfo[0];
    if (![identifier isKindOfClass:[GBGenericDERObject class]]) return nil;
    if (identifier.tag != 6) return nil;
    if (![identifier.data isEqual:[NSData dataWithBytesNoCopy:"\x2A\x86\x48\x86\xF7\x0D\x01\x07\x01" length:9 freeWhenDone:false]]) return nil;
    
    content = encapContentInfo[1];
    if (![content isKindOfClass:[GBGenericDERObject class]]) return nil;
    if (content.tag != 0xa0) return nil;
    if (content.children.count != 1) return nil;
    if (![content.children[0] isKindOfClass:[NSData class]]) {
        // Sometimes this is an array???
        NSArray *innerContent = content.children[0];
        if (![innerContent isKindOfClass:[NSArray class]]) return nil;
        if (innerContent.count != 1) return nil;
        if (![innerContent[0] isKindOfClass:[NSData class]]) return nil;
        
        return innerContent[0];
    }
    
    /* Todo: verify*/
    
    return content.children[0];
}

static NSDate *ParseRFCDate(NSString *string)
{
    NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
    
    [formatter setLocale:[NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"]];
    [formatter setDateFormat:@"yyyy'-'MM'-'dd'T'HH':'mm':'ss'Z'"];
    [formatter setTimeZone:[NSTimeZone timeZoneWithAbbreviation:@"UTC"]];
    
    // Convert the RFC 3339 date time string to an NSDate.
    NSDate *date = [formatter dateFromString:string];
    
    return date;
}

static NSDictionary *ParseIAP(NSSet *der)
{
    NSMutableDictionary *ret = [NSMutableDictionary dictionary];
    for (NSArray *item in der) {
        if (item.count != 3) return nil;
        NSNumber *key = item[0];
        if (![key isKindOfClass:[NSNumber class]]) return nil;
        __unused NSNumber *version = item[1];
        id data = item[2];
        switch (key.unsignedIntValue) {
            case 1701:
                @try {
                    data = ParseDER(data);
                }
                @catch (NSException *exception) {
                    continue;
                }
                if (![data isKindOfClass:[NSString class]]) continue;
                ret[@"quantity"] = data;
                break;
            case 1702:
                @try {
                    data = ParseDER(data);
                }
                @catch (NSException *exception) {
                    continue;
                }
                if (![data isKindOfClass:[NSString class]]) continue;
                ret[@"product_id"] = data;
                break;
            case 1703:
                @try {
                    data = ParseDER(data);
                }
                @catch (NSException *exception) {
                    continue;
                }
                if (![data isKindOfClass:[NSString class]]) continue;
                ret[@"transaction_id"] = data;
                break;
            case 1705:
                @try {
                    data = ParseDER(data);
                }
                @catch (NSException *exception) {
                    continue;
                }
                if (![data isKindOfClass:[NSString class]]) continue;
                ret[@"original_transaction_id"] = data;
                break;
            case 1704:
                @try {
                    data = ParseDER(data);
                }
                @catch (NSException *exception) {
                    continue;
                }
                if (![data isKindOfClass:[NSString class]]) continue;
                ret[@"purchase_date"] = ParseRFCDate(data);
                break;
            case 1706:
                @try {
                    data = ParseDER(data);
                }
                @catch (NSException *exception) {
                    continue;
                }
                if (![data isKindOfClass:[NSString class]]) continue;
                ret[@"original_purchase_date"] = ParseRFCDate(data);
                break;
            case 1708:
                @try {
                    data = ParseDER(data);
                }
                @catch (NSException *exception) {
                    continue;
                }
                if (![data isKindOfClass:[NSString class]]) continue;
                ret[@"expires_date"] = ParseRFCDate(data);
                break;
            case 1719:
                @try {
                    data = ParseDER(data);
                }
                @catch (NSException *exception) {
                    continue;
                }
                if (![data isKindOfClass:[NSString class]]) continue;
                ret[@"is_in_intro_offer_period"] = data;
                break;
            case 1712:
                @try {
                    data = ParseDER(data);
                }
                @catch (NSException *exception) {
                    continue;
                }
                if (![data isKindOfClass:[NSString class]]) continue;
                ret[@"cancellation_date"] = ParseRFCDate(data);
                break;
            case 1711:
                @try {
                    data = ParseDER(data);
                }
                @catch (NSException *exception) {
                    continue;
                }
                if (![data isKindOfClass:[NSString class]]) continue;
                ret[@"web_order_line_item_id"] = data;
                break;
        }
    }
    return ret;
}

static NSDictionary *ParseReceipt(NSSet *der)
{
    NSMutableDictionary *ret = [NSMutableDictionary dictionary];
    for (NSArray *item in der) {
        if (item.count != 3) return nil;
        NSNumber *key = item[0];
        if (![key isKindOfClass:[NSNumber class]]) return nil;
        __unused NSNumber *version = item[1];
        id data = item[2];
        switch (key.unsignedIntValue) {
            case 2:
                @try {
                    data = ParseDER(data);
                }
                @catch (NSException *exception) {
                    continue;
                }
                if (![data isKindOfClass:[NSString class]]) continue;
                ret[@"bundle_id"] = data;
                break;
            case 3:
                @try {
                    data = ParseDER(data);
                }
                @catch (NSException *exception) {
                    continue;
                }
                if (![data isKindOfClass:[NSString class]]) continue;
                ret[@"application_version"] = data;
                break;
            case 4:
                if (![data isKindOfClass:[NSData class]]) continue;
                ret[@"_opaque_value"] = data;
                break;
            case 5:
                if (![data isKindOfClass:[NSData class]]) continue;
                ret[@"_sha1"] = data;
                break;
            case 17:
                @try {
                    data = ParseDER(data);
                }
                @catch (NSException *exception) {
                    continue;
                }
                if (![data isKindOfClass:[NSSet class]]) continue;
                if (!ret[@"in_app"]) {
                    ret[@"in_app"] = [NSMutableSet set];
                }
                [ret[@"in_app"] addObject: ParseIAP(data)];
                break;
            case 19:
                @try {
                    data = ParseDER(data);
                }
                @catch (NSException *exception) {
                    continue;
                }
                if (![data isKindOfClass:[NSString class]]) continue;
                ret[@"original_application_version"] = data;
                break;
            case 12:
                @try {
                    data = ParseDER(data);
                }
                @catch (NSException *exception) {
                    continue;
                }
                if (![data isKindOfClass:[NSString class]]) continue;
                ret[@"expiration_date"] = ParseRFCDate(data);
                break;
            case 21:
                @try {
                    data = ParseDER(data);
                }
                @catch (NSException *exception) {
                    continue;
                }
                if (![data isKindOfClass:[NSString class]]) continue;
                ret[@"receipt_creation_date"] = ParseRFCDate(data);
                break;
        }
    }
    return ret;
}

@interface GBSubscriptionManager () <SKPaymentTransactionObserver>
{
    NSDate *_reciptDate;
    NSMutableArray <NSDictionary *> *_activeSubscriptions; // Sorted chronologically
}
@end

__attribute__((objc_direct_members))
@implementation GBSubscriptionManager
- (void)paymentQueue:(SKPaymentQueue *)queue updatedTransactions:(NSArray<SKPaymentTransaction *> *)transactions
{
    bool needUpdate = false;
    for (SKPaymentTransaction *transaction in transactions) {
        switch (transaction.transactionState) {
            case SKPaymentTransactionStatePurchased:
                needUpdate = true;
            case SKPaymentTransactionStateFailed:
                [[SKPaymentQueue defaultQueue] finishTransaction:transaction];
            default:
                break;
        }
    }
    if (needUpdate) {
        [self updateReceipt];
    }
}

- (void)enterGraceMode
{
    if (_themeState == GBSubscriptionPermanent && _watchState == GBSubscriptionPermanent) return;
    
    NSString *cacheFolder = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, true)[0];
    NSString *path = [cacheFolder stringByAppendingPathComponent:@"SKGrace"];
    NSData *data = [NSData dataWithContentsOfFile:path];
    time_t graceStart = 0;
    if (data.length != sizeof(graceStart)) {
        graceStart = time(NULL);
        data = [NSData dataWithBytes:&graceStart length:sizeof(graceStart)];
        [data writeToFile:path atomically:false];
    }
    else {
        [data getBytes:&graceStart length:sizeof(graceStart)];
    }
    
    time_t restoreTime = 0;
    [[NSData dataWithContentsOfFile:[cacheFolder stringByAppendingPathComponent:@"SKRestore"]] getBytes:&restoreTime length:sizeof(restoreTime)];
    
    time_t graceTime = 60 * 60 * 24 * 7;
    if (self.expiredSubscription) {
        graceTime = MIN([self.expiredSubscription[@"expires_date"] timeIntervalSinceDate:self.expiredSubscription[@"purchase_date"]], 60 * 60 * 24 * 30);
    }
    
    if (restoreTime >= [self.expiredSubscription[@"expires_date"] timeIntervalSince1970] + graceTime) {
        if (_themeState != GBSubscriptionPermanent) _themeState = GBSubscriptionInactive;
        if (_watchState != GBSubscriptionPermanent) _watchState = GBSubscriptionInactive;
        return;
    }
    
    time_t now = time(NULL);
    if (now < graceStart || now > graceStart + graceTime) {
        if (_themeState != GBSubscriptionPermanent) _themeState = GBSubscriptionInactive;
        if (_watchState != GBSubscriptionPermanent) _watchState = GBSubscriptionInactive;
    }
    else {
        if (_themeState != GBSubscriptionPermanent) _themeState = GBSubscriptionGrace;
        if (_watchState != GBSubscriptionPermanent) _watchState = GBSubscriptionGrace;
    }
    
    _watchState = GBSubscriptionPermanent;
}

- (void)enterPermanentMode:(bool)watch
{
    if (watch) {
        _watchState = GBSubscriptionPermanent;
    }
    else {
        _themeState = GBSubscriptionPermanent;
    }
}

- (void)enterActiveMode
{
    if (_themeState == GBSubscriptionPermanent && _watchState == GBSubscriptionPermanent) return;
    if (_themeState != GBSubscriptionPermanent) _themeState = GBSubscriptionActive;
    if (_watchState != GBSubscriptionPermanent) _watchState = GBSubscriptionActive;
    NSString *cacheFolder = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, true)[0];
    NSString *path = [cacheFolder stringByAppendingPathComponent:@"SKGrace"];
    unlink(path.UTF8String);
}

- (void)paymentQueueRestoreCompletedTransactionsFinished:(SKPaymentQueue *)queue
{
    [[NSUserDefaults standardUserDefaults] setBool:true forKey:@"GBDidRestoreTransactions"];
    
    NSString *cacheFolder = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, true)[0];
    NSString *path = [cacheFolder stringByAppendingPathComponent:@"SKRestore"];
    time_t restoreTime = time(NULL);
    NSData *data = [NSData dataWithBytes:&restoreTime length:sizeof(restoreTime)];
    [data writeToFile:path atomically:false];
    
    if (_themeState == GBSubscriptionGrace ||
        _watchState == GBSubscriptionGrace) {
        [self enterGraceMode]; // Re-enter grace modes in case it needs to become inactive
        _watchState = GBSubscriptionPermanent;
    }

    [self updateReceipt];
}

- (void)paymentQueue:(SKPaymentQueue *)queue restoreCompletedTransactionsFailedWithError:(NSError *)error
{
    [self updateReceipt];
}

- (void)updateReceipt
{
    NSURL *url = [[NSBundle mainBundle] appStoreReceiptURL];
    NSDate *date = nil;
    [url getResourceValue:&date forKey:NSURLContentModificationDateKey error:nil];
    if ([date isEqual:_reciptDate]) return;
    _reciptDate = date;
    
    NSData *data = [NSData dataWithContentsOfURL:url];
    if (!data) return;
    
    NSDictionary *receipt = nil;
    @try {
        NSData *content = VerifyAndExtractPKCS7(ParseDER(data));
        NSSet *parsedContents = ParseDER(content);
        receipt = ParseReceipt(parsedContents);
    }
    @catch (NSException *exception) {
        // The receipt is invalid
        [[SKPaymentQueue defaultQueue] restoreCompletedTransactions];
        return;
    }

    NSArray *iap = [receipt[@"in_app"] sortedArrayUsingDescriptors:@[[NSSortDescriptor sortDescriptorWithKey:@"purchase_date" ascending:true]]];

    _activeSubscriptions = [NSMutableArray array];
    NSDate *now = [NSDate date];
    bool hadSubscription = false;
    for (NSDictionary *item in iap) {
        if ([item[@"product_id"] containsString:@"Lifetime"]) {
            [self enterPermanentMode:false];
            continue;
        }
        if ([item[@"product_id"] containsString:@"Watch"]) {
            [self enterPermanentMode:true];
            continue;
        }
        if (!item[@"expires_date"]) continue; // Future one-time purchase
        hadSubscription = true;
        
        if ([now compare:item[@"purchase_date"]] == NSOrderedDescending &&
            [now compare:item[@"expires_date"]] == NSOrderedAscending) {
            [_activeSubscriptions addObject:item];
            _expiredSubscription = nil;
        }
        else if (!_activeSubscriptions.count) {
            _expiredSubscription = item;
        }
    }
    if (_activeSubscriptions.count) {
        [self enterActiveMode];
    }
    else if (hadSubscription) {
        [self enterGraceMode];
        if (self.usesPaidTheme) {
            [[SKPaymentQueue defaultQueue] restoreCompletedTransactions];
        }
    }
    
    
    [[NSNotificationCenter defaultCenter] postNotificationName:GBSubscriptionInfoUpdatedNotification object:nil];
}

- (NSDictionary *)activeSubscription
{
    if (_activeSubscriptions.count) {
        return _activeSubscriptions[0];
    }
    return nil;
}

- (NSDictionary *)pendingSubscription
{
    if (_activeSubscriptions.count > 1) {
        NSDictionary *ret = _activeSubscriptions.lastObject;
        if ([ret[@"product_id"] isEqual:_activeSubscriptions[0][@"product_id"]]) return nil;
        return ret;
    }
    return nil;
}

- (bool)usesPaidTheme
{
    return ![@[@"SameBoy", @"SameBoy Dark"] containsObject:[[NSUserDefaults standardUserDefaults] stringForKey:@"GBInterfaceTheme"]];
}

- (instancetype)init
{
    self = [super init];
    [[SKPaymentQueue defaultQueue] addTransactionObserver:self];
    if (![[NSUserDefaults standardUserDefaults] boolForKey:@"GBDidRestoreTransactions"] && self.usesPaidTheme) {
        [self enterGraceMode];
        [[NSNotificationCenter defaultCenter] postNotificationName:GBSubscriptionInfoUpdatedNotification object:nil];
        [[SKPaymentQueue defaultQueue] restoreCompletedTransactions];
    }
    [self updateReceipt];
    _watchState = GBSubscriptionPermanent;
    return self;
}

+ (GBSubscriptionManager *)defaultManager
{
    static GBSubscriptionManager *ret = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        ret = [[self alloc] init];
    });
    return ret;
}

+ (void)load
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [self defaultManager];
    });
}

@end
#endif

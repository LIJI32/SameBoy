#import <Foundation/Foundation.h>
#import <StoreKit/StoreKit.h>

#define GBSubscriptionInfoUpdatedNotification (@"GBSubscriptionInfoUpdatedNotification")

typedef enum {
    GBSubscriptionInactive,
    GBSubscriptionActive,
    GBSubscriptionGrace, // Current subscription expired, but got not obtain new subscription
    GBSubscriptionPermanent,
} GBSubscriptionState;

@interface GBSubscriptionManager : NSObject
@property (direct, class, readonly) GBSubscriptionManager *defaultManager;
@property (direct, readonly) NSDictionary *activeSubscription;
@property (direct, readonly) NSDictionary *pendingSubscription;
@property (direct, readonly) NSDictionary *expiredSubscription;
@property (direct, readonly) GBSubscriptionState themeState;
@property (direct, readonly) GBSubscriptionState watchState;
@property (direct, readonly) bool usesPaidTheme;
@end

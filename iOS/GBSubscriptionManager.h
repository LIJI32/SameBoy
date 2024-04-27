#import <Foundation/Foundation.h>
#import <StoreKit/StoreKit.h>

#define GBSubscriptionInfoUpdatedNotification (@"GBSubscriptionInfoUpdatedNotification")

typedef enum {
    GBSubscriptionInactive,
    GBSubscriptionActive,
    GBSubscriptionGrace, // Current subscription expired, but got not obtain new subscription
} GBSubscriptionState;

@interface GBSubscriptionManager : NSObject
@property (class, readonly) GBSubscriptionManager *defaultManager;
@property (readonly) NSDictionary *activeSubscription;
@property (readonly) NSDictionary *pendingSubscription;
@property (readonly) NSDictionary *expiredSubscription;
@property (readonly) GBSubscriptionState state;
@property (readonly) bool usesPaidTheme;
@end

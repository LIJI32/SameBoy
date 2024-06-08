#import <Foundation/Foundation.h>

typedef enum {
    GBHubStatusNotReady,
    GBHubStatusInProgress,
    GBHubStatusReady,
    GBHubStatusError,
} GBHubStatus;

static inline NSString *GBSearchCanonicalString(NSString *string)
{
    return [[string.lowercaseString stringByApplyingTransform:NSStringTransformStripDiacritics reverse:false] stringByApplyingTransform:NSStringTransformStripCombiningMarks reverse:false];
}

@interface GBHubGame : NSObject
@property (readonly) NSString *title;
@property (readonly) NSString *developer;
@property (readonly) NSString *type;
@property (readonly) NSString *license;
@property (readonly) NSDate *publicationDate;
@property (readonly) NSArray <NSString *> *tags;
@property (readonly) NSURL *repository;
@property (readonly) NSURL *website;
@property (readonly) NSArray <NSURL *> *screenshots;
@property (readonly) NSURL *file;
@property (readonly) NSString *slug;
@property (readonly) NSString *entryDescription;
@property (readonly) NSString *keywords;
@end

extern NSString *const GBHubStatusChangedNotificationName;

@interface GBHub : NSObject
+ (instancetype)sharedHub;
- (void)refresh;
- (unsigned)countForTag:(NSString *)tag;
@property (readonly) GBHubStatus status;
@property (readonly) NSDictionary<NSString *, GBHubGame *> *allGames;
@property (readonly) NSArray<NSString *> *sortedTags;
@property (readonly) NSArray<GBHubGame *> *showcaseGames;
@end

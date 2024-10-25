#import <Foundation/Foundation.h>
#import <WatchConnectivity/WatchConnectivity.h>

@interface GBCommunicator : NSObject<WCSessionDelegate>
- (void)sendMessage:(NSDictionary<NSString *, id> *)message
       replyHandler:(void (^)(NSDictionary<NSString *, id> *replyMessage))replyHandler
       errorHandler:(void (^)(NSString *error))errorHandler;
@end

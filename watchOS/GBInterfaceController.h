#import <WatchKit/WatchKit.h>
#import <Foundation/Foundation.h>

@interface GBInterfaceController : WKInterfaceController<WKCrownDelegate>
- (void)stop;
- (void)start;
- (void)stopAndSave;
@property (readonly) bool isRunning;
@end

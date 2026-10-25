#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <JoyKit/JoyKit.h>

@interface GBJoyConManager : NSObject<JOYListener, NSTableViewDataSource, NSTableViewDelegate>
+ (instancetype)sharedInstance;
- (IBAction)autopair:(id)sender;

@property (nonatomic) bool arrangementMode;
@property (weak) IBOutlet NSTableView *tableView;
@end


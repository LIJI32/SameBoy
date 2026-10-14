#import <Cocoa/Cocoa.h>

@interface GBLuaConsoleController : NSWindowController
- (instancetype)initWithActionTarget:(id)target;
- (void)appendText:(NSString *)text;
- (void)clear;
- (void)setScriptPath:(NSString *)path running:(bool)running;
@end

#import <UIKit/UIKit.h>

@interface GBROMViewController : UITableViewController<UIDocumentPickerDelegate>
#ifdef APPSTORE
- (void)transitionToSibling:(unsigned)index;
#endif

/* For inheritance */
- (void)romSelectedAtIndex:(unsigned)index;
- (void)deleteROMAtIndex:(unsigned)index;
- (void)renameROM:(NSString *)oldName toName:(NSString *)newName;
- (void)duplicateROMAtIndex:(unsigned)index;
- (NSString *)rootPath;
#ifdef APPSTORE
- (instancetype)initForWatch;
- (UIAction *)transferActionForROMIndex:(unsigned)index API_AVAILABLE(ios(13.0));
#endif

/* To be used by subclasses */
- (UITableViewCell *)cellForROM:(NSString *)rom;
@end

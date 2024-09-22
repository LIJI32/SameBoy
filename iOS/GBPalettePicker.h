#import <UIKit/UIKit.h>
#import <Core/gb.h>

@interface GBPalettePicker : UITableViewController
+ (const GB_palette_t *)paletteForTheme:(NSString *)theme;
+ (bool)importPalette:(NSString *)path;
+ (NSString *)makeUnique:(NSString *)name;
@end

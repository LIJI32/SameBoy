#import <AppKit/AppKit.h>
#import <Core/gb.h>

@interface GBPaletteEditorController : NSObject<NSTableViewDataSource, NSTableViewDelegate>
@property (weak) IBOutlet NSColorWell *colorWell0;
@property (weak) IBOutlet NSColorWell *colorWell1;
@property (weak) IBOutlet NSColorWell *colorWell2;
@property (weak) IBOutlet NSColorWell *colorWell3;
@property (weak) IBOutlet NSColorWell *colorWell4;
@property (weak) IBOutlet NSButton *disableLCDColorCheckbox;
@property (weak) IBOutlet NSButton *manualModeCheckbox;
@property (weak) IBOutlet NSSlider *brightnessSlider;
@property (weak) IBOutlet NSSlider *hueSlider;
@property (weak) IBOutlet NSSlider *hueStrengthSlider;
@property (weak) IBOutlet NSTableView *themesList;
@property (weak) IBOutlet NSMenu *menu;
@property (weak) IBOutlet NSSegmentedControl *segmentControl;
@property IBOutlet NSMenu *segmentMenu;
+ (const GB_palette_t *)userPalette;
@end

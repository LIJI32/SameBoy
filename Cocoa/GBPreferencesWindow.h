#import <Cocoa/Cocoa.h>
#import <JoyKit/JoyKit.h>
#import "GBPaletteEditorController.h"

@interface GBPreferencesWindow : NSWindow <NSTableViewDelegate, NSTableViewDataSource, JOYListener>
@property IBOutlet NSTableView *controlsTableView;
@property IBOutlet NSButton *configureJoypadButton;
@property IBOutlet NSButton *skipButton;
@property IBOutlet NSMenuItem *bootROMsFolderItem;
@property IBOutlet NSPopUpButtonCell *bootROMsButton;
@property IBOutlet NSPopUpButton *preferredJoypadButton;
@property IBOutlet NSPopUpButton *playerListButton;
@property IBOutlet NSSlider *volumeSlider;
@property IBOutlet NSSlider *interferenceSlider;
@property IBOutlet NSSlider *temperatureSlider;
@property IBOutlet GBPaletteEditorController *paletteEditorController;
@property IBOutlet NSWindow *paletteEditor;
@property IBOutlet NSWindow *joyconsSheet;
@property IBOutlet NSPopUpButton *colorCorrectionPopupButton;
@property IBOutlet NSPopUpButton *highpassFilterPopupButton;
@property IBOutlet NSPopUpButton *colorPalettePopupButton;
@property IBOutlet NSPopUpButton *hotkey1PopupButton;
@property IBOutlet NSPopUpButton *hotkey2PopupButton;
@end

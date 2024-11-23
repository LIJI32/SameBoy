#import <Cocoa/Cocoa.h>
#import "GBView.h"
#import "GBImageView.h"
#import "GBSplitView.h"
#import "GBVisualizerView.h"
#import "GBOSDView.h"
#import "GBDebuggerButton.h"

enum model {
    MODEL_NONE,
    MODEL_DMG,
    MODEL_CGB,
    MODEL_AGB,
    MODEL_SGB,
    MODEL_MGB,
    MODEL_AUTO,
    
    MODEL_QUICK_RESET = -1,
};

@class GBCheatWindowController;
@class GBPaletteView;
@class GBObjectView;

@interface Document : NSDocument <NSWindowDelegate, GBImageViewDelegate, NSSplitViewDelegate>
@property (readonly) GB_gameboy_t *gb;
@property IBOutlet GBView *view;
@property IBOutlet NSTextView *consoleOutput;
@property IBOutlet NSPanel *consoleWindow;
@property IBOutlet NSTextField *consoleInput;
@property IBOutlet NSWindow *mainWindow;
@property IBOutlet NSView *memoryView;
@property IBOutlet NSPanel *memoryWindow;
@property (readonly) GB_gameboy_t *gameboy;
@property IBOutlet NSTextField *memoryBankInput;
@property IBOutlet NSToolbarItem *memoryBankItem;
@property IBOutlet NSPopUpButton *memorySpaceButton;
@property IBOutlet GBImageView *tilesetImageView;
@property IBOutlet NSPopUpButton *tilesetPaletteButton;
@property IBOutlet GBImageView *tilemapImageView;
@property IBOutlet NSPopUpButton *tilemapPaletteButton;
@property IBOutlet NSPopUpButton *tilemapMapButton;
@property IBOutlet NSPopUpButton *TilemapSetButton;
@property IBOutlet NSButton *gridButton;
@property IBOutlet NSTabView *vramTabView;
@property IBOutlet NSPanel *vramWindow;
@property IBOutlet NSTextField *vramStatusLabel;
@property IBOutlet GBPaletteView *paletteView;
@property IBOutlet GBObjectView *objectView;
@property IBOutlet NSPanel *printerFeedWindow;
@property IBOutlet NSProgressIndicator *printerSpinner;
@property IBOutlet NSImageView *feedImageView;
@property IBOutlet NSTextView *debuggerSideViewInput;
@property IBOutlet NSTextView *debuggerSideView;
@property IBOutlet GBSplitView *debuggerSplitView;
@property IBOutlet NSBox *debuggerVerticalLine;
@property IBOutlet NSPanel *cheatsWindow;
@property IBOutlet GBCheatWindowController *cheatWindowController;
@property (readonly) Document *partner;
@property (readonly) bool isSlave;
@property IBOutlet NSView *gbsPlayerView;
@property IBOutlet NSTextField *gbsTitle;
@property IBOutlet NSTextField *gbsAuthor;
@property IBOutlet NSTextField *gbsCopyright;
@property IBOutlet NSPopUpButton *gbsTracks;
@property IBOutlet NSButton *gbsPlayPauseButton;
@property IBOutlet NSButton *gbsRewindButton;
@property IBOutlet NSSegmentedControl *gbsNextPrevButton;
@property IBOutlet GBVisualizerView *gbsVisualizer;
@property IBOutlet GBOSDView *osdView;
@property (readonly) GB_oam_info_t *oamInfo;
@property uint8_t oamCount;
@property uint8_t oamHeight;
@property IBOutlet NSView *audioRecordingAccessoryView;
@property IBOutlet NSPopUpButton *audioFormatButton;
@property IBOutlet NSVisualEffectView *debuggerSidebarEffectView API_AVAILABLE(macos(10.10));

@property IBOutlet GBDebuggerButton *debuggerContinueButton;
@property IBOutlet GBDebuggerButton *debuggerNextButton;
@property IBOutlet GBDebuggerButton *debuggerStepButton;
@property IBOutlet GBDebuggerButton *debuggerFinishButton;
@property (strong) IBOutlet GBDebuggerButton *debuggerBackstepButton;


+ (NSImage *) imageFromData:(NSData *)data width:(NSUInteger) width height:(NSUInteger) height scale:(double) scale;
- (void) performAtomicBlock: (void (^)())block;
- (void) connectLinkCable:(NSMenuItem *)sender;
- (int)loadStateFile:(const char *)path noErrorOnNotFound:(bool)noErrorOnFileNotFound;
- (NSString *)captureOutputForBlock: (void (^)())block;
- (NSFont *)debuggerFontOfSize:(unsigned)size;
@end


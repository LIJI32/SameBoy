#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <JoyKit/JoyKit.h>

@interface GBApp : NSApplication <NSApplicationDelegate, NSUserNotificationCenterDelegate, NSMenuDelegate, WebUIDelegate, WebPolicyDelegate, WebFrameLoadDelegate, JOYListener>

@property (nonatomic, strong) IBOutlet NSWindow *preferencesWindow;
@property (nonatomic, strong) IBOutlet NSView *graphicsTab;
@property (nonatomic, strong) IBOutlet NSView *emulationTab;
@property (nonatomic, strong) IBOutlet NSView *audioTab;
@property (nonatomic, strong) IBOutlet NSView *controlsTab;
@property (nonatomic, strong) IBOutlet NSView *updatesTab;
- (IBAction)showPreferences: (id) sender;
- (IBAction)toggleDeveloperMode:(id)sender;
- (IBAction)switchPreferencesTab:(id)sender;
@property (nonatomic, weak) IBOutlet NSMenuItem *linkCableMenuItem;
@property (nonatomic, strong) IBOutlet NSWindow *updateWindow;
@property (nonatomic, strong) IBOutlet WebView *updateChanges;
@property (nonatomic, strong) IBOutlet NSProgressIndicator *updatesSpinner;
@property (strong) IBOutlet NSButton *updatesButton;
@property (strong) IBOutlet NSTextField *updateProgressLabel;
@property (strong) IBOutlet NSButton *updateProgressButton;
@property (strong) IBOutlet NSWindow *updateProgressWindow;
@property (strong) IBOutlet NSProgressIndicator *updateProgressSpinner;
- (void)updateThemesDefault:(bool)overwrite;
@end


#import "GBApp.h"
#import "GBButtons.h"
#import "GBView.h"
#import "Document.h"
#import "GBJoyConManager.h"
#import <Core/gb.h>
#import <Carbon/Carbon.h>
#import <JoyKit/JoyKit.h>
#import <WebKit/WebKit.h>
#import <mach-o/dyld.h>
#include <sys/mount.h>
#include <sys/xattr.h>

#define UPDATE_SERVER "https://sameboy.github.io"

@interface NSToolbarItem(private)
- (NSButton *)_view;
@end

static uint32_t color_to_int(NSColor *color)
{
    color = [color colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
    return (((unsigned)(color.redComponent * 0xFF)) << 16) |
           (((unsigned)(color.greenComponent * 0xFF)) << 8) |
           ((unsigned)(color.blueComponent * 0xFF));
}

@implementation GBApp
{
    NSArray<NSView *> *_preferencesTabs;
    NSString *_lastVersion;
    NSString *_updateURL;
    NSURLSessionDownloadTask *_updateTask;
    enum {
        UPDATE_DOWNLOADING,
        UPDATE_EXTRACTING,
        UPDATE_WAIT_INSTALL,
        UPDATE_INSTALLING,
        UPDATE_FAILED,
    } _updateState;
    NSString *_downloadDirectory;
    AuthorizationRef _auth;
    bool _simulatingMenuEvent;
}

- (void) applicationDidFinishLaunching:(NSNotification *)notification
{
    // Refresh icon if launched via a software update
    if (@available(macOS 26.0, *)) {
        // Severely broken on macOS 26
    }
    else {
        NSImage *icon = [[NSWorkspace sharedWorkspace] iconForFile:[[NSBundle mainBundle] bundlePath]];
        icon.size = [NSApplication sharedApplication].applicationIconImage.size;
        [NSApplication sharedApplication].applicationIconImage = icon;
    }
    
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    for (unsigned i = 0; i < GBKeyboardButtonCount; i++) {
        if ([[defaults objectForKey:button_to_preference_name(i, 0)] isKindOfClass:[NSString class]]) {
            [defaults removeObjectForKey:button_to_preference_name(i, 0)];
        }
    }
    
    bool hasSFMono = false;
    if (@available(macOS 10.15, *)) {
        hasSFMono = [[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular].displayName containsString:@"SF"];
    }
    [[NSUserDefaults standardUserDefaults] registerDefaults:@{
                                                              @"GBRight": @(kVK_RightArrow),
                                                              @"GBLeft": @(kVK_LeftArrow),
                                                              @"GBUp": @(kVK_UpArrow),
                                                              @"GBDown": @(kVK_DownArrow),

                                                              @"GBA": @(kVK_ANSI_X),
                                                              @"GBB": @(kVK_ANSI_Z),
                                                              @"GBSelect": @(kVK_Delete),
                                                              @"GBStart": @(kVK_Return),

                                                              @"GBTurbo": @(kVK_Space),
                                                              @"GBRewind": @(kVK_Tab),
                                                              @"GBSlow-Motion": @(kVK_Shift),

                                                              @"GBFilter": @"NearestNeighbor",
                                                              @"GBColorCorrection": @(GB_COLOR_CORRECTION_MODERN_BALANCED),
                                                              @"GBHighpassFilter": @(GB_HIGHPASS_ACCURATE),
                                                              @"GBRewindLength": @(120),
                                                              @"GBFrameBlendingMode": @([defaults boolForKey:@"DisableFrameBlending"]? GB_FRAME_BLENDING_MODE_DISABLED : GB_FRAME_BLENDING_MODE_ACCURATE),
                                                              
                                                              @"GBDMGModel": @(GB_MODEL_DMG_B),
                                                              @"GBCGBModel": @(GB_MODEL_CGB_E),
                                                              @"GBAGBModel": @(GB_MODEL_AGB_A),
                                                              @"GBSGBModel": @(GB_MODEL_SGB2),
                                                              @"GBRumbleMode": @(GB_RUMBLE_CARTRIDGE_ONLY),
                                                              
                                                              @"GBVolume": @(1.0),
                                                              
                                                              @"GBMBC7JoystickOverride": @NO,
                                                              @"GBMBC7AllowMouse": @YES,
                                                              
                                                              @"GBJoyConAutoPair": @YES,
                                                              @"GBJoyConsDefaultsToHorizontal": @YES,
                                                              
                                                              @"GBEmulatedModel": @(MODEL_AUTO),
                                                              
                                                              @"GBDebuggerFont": hasSFMono? @"SF Mono" : @"Menlo",
                                                              @"GBDebuggerFontSize": @12,
                                                              
                                                              @"GBColorPalette": @1,
                                                              @"GBTurboCap": @0,
                                                              
                                                              // Default themes
                                                              @"GBThemes": @{
                                                                      @"Canyon": @{
                                                                              @"BrightnessBias": @0.1227009965823247,
                                                                              @"Colors": @[@0xff0c1e20, @0xff122b91, @0xff466aa2, @0xfff1efae, @0xfff1efae],
                                                                              @"DisabledLCDColor": @NO,
                                                                              @"HueBias": @0.01782661816105247,
                                                                              @"HueBiasStrength": @1,
                                                                              @"Manual": @NO,
                                                                      },
                                                                      @"Desert": @{
                                                                              @"BrightnessBias": @0.0,
                                                                              @"Colors": @[@0xff302f3e, @0xff576674, @0xff839ba4, @0xffb1d0d2, @0xffb7d7d8],
                                                                              @"DisabledLCDColor": @YES,
                                                                              @"HueBias": @0.10087773904382469,
                                                                              @"HueBiasStrength": @0.062142056772908363,
                                                                              @"Manual": @NO,
                                                                      },
                                                                      @"Evening": @{
                                                                              @"BrightnessBias": @-0.10168700106441975,
                                                                              @"Colors": @[@0xff362601, @0xff695518, @0xff899853, @0xffa6e4ae, @0xffa9eebb],
                                                                              @"DisabledLCDColor": @YES,
                                                                              @"HueBias": @0.60027079191058874,
                                                                              @"HueBiasStrength": @0.33816297305747867,
                                                                              @"Manual": @NO,
                                                                      },
                                                                      @"Fog": @{
                                                                              @"BrightnessBias": @0.0,
                                                                              @"Colors": @[@0xff373c34, @0xff737256, @0xff9da386, @0xffc3d2bf, @0xffc7d8c6],
                                                                              @"DisabledLCDColor": @YES,
                                                                              @"HueBias": @0.55750435756972117,
                                                                              @"HueBiasStrength": @0.18424738545816732,
                                                                              @"Manual": @NO,
                                                                      },
                                                                      @"Green Slate": @{
                                                                              @"BrightnessBias": @0.2210012227296829,
                                                                              @"Colors": @[@0xff343117, @0xff6a876f, @0xff98b4a1, @0xffc3daca, @0xffc8decf],
                                                                              @"DisabledLCDColor": @YES,
                                                                              @"HueBias": @0.1887667975388467,
                                                                              @"HueBiasStrength": @0.1272283345460892,
                                                                              @"Manual": @NO,
                                                                      },
                                                                      @"Green Tea": @{
                                                                              @"BrightnessBias": @-0.4946326622596153,
                                                                              @"Colors": @[@0xff1a1d08, @0xff1d5231, @0xff3b9774, @0xff97e4c6, @0xffa9eed1],
                                                                              @"DisabledLCDColor": @YES,
                                                                              @"HueBias": @0.1912955007245464,
                                                                              @"HueBiasStrength": @0.3621708039314516,
                                                                              @"Manual": @NO,
                                                                      },
                                                                      @"Lavender": @{
                                                                              @"BrightnessBias": @0.10072476038566,
                                                                              @"Colors": @[@0xff2b2a3a, @0xff8c507c, @0xffbf82a8, @0xffe9bcce, @0xffeec3d3],
                                                                              @"DisabledLCDColor": @YES,
                                                                              @"HueBias": @0.7914529587142169,
                                                                              @"HueBiasStrength": @0.2498168498277664,
                                                                              @"Manual": @NO,
                                                                      },
                                                                      @"Magic Eggplant": @{
                                                                              @"BrightnessBias": @0.0,
                                                                              @"Colors": @[@0xff3c2136, @0xff942e84, @0xffc7699d, @0xfff1e4b0, @0xfff6f9b2],
                                                                              @"DisabledLCDColor": @YES,
                                                                              @"HueBias": @0.87717878486055778,
                                                                              @"HueBiasStrength": @0.65018052788844627,
                                                                              @"Manual": @NO,
                                                                      },
                                                                      @"Mystic Blue": @{
                                                                              @"BrightnessBias": @-0.3291049897670746,
                                                                              @"Colors": @[@0xff3b2306, @0xffa27807, @0xffd1b523, @0xfff6ebbe, @0xfffaf1e4],
                                                                              @"DisabledLCDColor": @YES,
                                                                              @"HueBias": @0.5282051088288426,
                                                                              @"HueBiasStrength": @0.7699633836746216,
                                                                              @"Manual": @NO,
                                                                      },
                                                                      @"Pink Pop": @{
                                                                              @"BrightnessBias": @0.624908447265625,
                                                                              @"Colors": @[@0xff28140a, @0xff7c42cb, @0xffaa83de, @0xffd1ceeb, @0xffd5d8ec],
                                                                              @"DisabledLCDColor": @YES,
                                                                              @"HueBias": @0.9477411056868732,
                                                                              @"HueBiasStrength": @0.80024421215057373,
                                                                              @"Manual": @NO,
                                                                      },
                                                                      @"Radioactive Pea": @{
                                                                              @"BrightnessBias": @-0.48079556772908372,
                                                                              @"Colors": @[@0xff215200, @0xff1f7306, @0xff169e34, @0xff03ceb8, @0xff00d4d1],
                                                                              @"DisabledLCDColor": @YES,
                                                                              @"HueBias": @0.3795131972111554,
                                                                              @"HueBiasStrength": @0.34337649402390436,
                                                                              @"Manual": @NO,
                                                                      },
                                                                      @"Rose": @{
                                                                              @"BrightnessBias": @0.2727272808551788,
                                                                              @"Colors": @[@0xff001500, @0xff4e1fae, @0xff865ac4, @0xffb7e6d3, @0xffbdffd4],
                                                                              @"DisabledLCDColor": @YES,
                                                                              @"HueBias": @0.9238900924101472,
                                                                              @"HueBiasStrength": @0.9957716464996338,
                                                                              @"Manual": @NO,
                                                                      },
                                                                      @"Seaweed": @{
                                                                              @"BrightnessBias": @-0.28532744023904377,
                                                                              @"Colors": @[@0xff3f0015, @0xff426532, @0xff58a778, @0xff95e0df, @0xffa0e7ee],
                                                                              @"DisabledLCDColor": @YES,
                                                                              @"HueBias": @0.2694067480079681,
                                                                              @"HueBiasStrength": @0.51565612549800799,
                                                                              @"Manual": @NO,
                                                                      },
                                                                      @"Twilight": @{
                                                                              @"BrightnessBias": @-0.091789093625498031,
                                                                              @"Colors": @[@0xff3f0015, @0xff461286, @0xff6254bd, @0xff97d3e9, @0xffa0e7ee],
                                                                              @"DisabledLCDColor": @YES,
                                                                              @"HueBias": @0.0,
                                                                              @"HueBiasStrength": @0.49710532868525897,
                                                                              @"Manual": @NO,
                                                                      },
                                                              },
                                                              
                                                              @"NSToolbarItemForcesStandardSize": @YES, // Forces Monterey to resepect toolbar item sizes
                                                              @"NSToolbarItemWarnOnMinMaxSize": @NO, // Not going to use Constraints, Apple
                                                              }];
    
    [JOYController startOnRunLoop:[NSRunLoop currentRunLoop] withOptions:@{
        JOYAxes2DEmulateButtonsKey: @YES,
        JOYHatsEmulateButtonsKey: @YES,
    }];
    
    [GBJoyConManager sharedInstance]; // Starts handling Joy-Cons
    
    [JOYController registerListener:self];
    
    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBNotificationsUsed"]) {
        [NSUserNotificationCenter defaultUserNotificationCenter].delegate = self;
    }
    
    [self askAutoUpdates];
    
    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBAutoUpdatesEnabled"]) {
        [self checkForUpdates];
    }
    
    if ([[NSProcessInfo processInfo].arguments containsObject:@"--update-launch"]) {
        [NSApp activateIgnoringOtherApps:true];
    }
    
    if (![[[NSUserDefaults standardUserDefaults] stringForKey:@"GBThemesVersion"] isEqualToString:@(GB_VERSION)]) {
        [self updateThemesDefault:false];
        [[NSUserDefaults standardUserDefaults] setObject:@(GB_VERSION) forKey:@"GBThemesVersion"];
    }
}

- (void)updateThemesDefault:(bool)overwrite
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSMutableDictionary *currentThemes = [defaults dictionaryForKey:@"GBThemes"].mutableCopy;
    [defaults removeObjectForKey:@"GBThemes"];
    NSMutableDictionary *defaultThemes = [defaults dictionaryForKey:@"GBThemes"].mutableCopy;
    if (![[NSUserDefaults standardUserDefaults] stringForKey:@"GBThemesVersion"]) {
        // Force update the Pink Pop theme, it was glitchy in 1.0
        [currentThemes removeObjectForKey:@"Pink Pop"];
    }
    if (overwrite) {
        [currentThemes addEntriesFromDictionary:defaultThemes];
        [defaults setObject:currentThemes forKey:@"GBThemes"];
    }
    else {
        [defaultThemes addEntriesFromDictionary:currentThemes];
        [defaults setObject:defaultThemes forKey:@"GBThemes"];
    }
}

- (IBAction)toggleDeveloperMode:(id)sender
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setBool:![defaults boolForKey:@"DeveloperMode"] forKey:@"DeveloperMode"];
}

- (IBAction)switchPreferencesTab:(id)sender
{
    for (NSView *view in _preferencesTabs) {
        [view removeFromSuperview];
    }
    NSView *tab = _preferencesTabs[[sender tag]];
    NSRect old = [_preferencesWindow frame];
    NSRect new = [_preferencesWindow frameRectForContentRect:tab.frame];
    new.origin.x = old.origin.x;
    new.origin.y = old.origin.y + (old.size.height - new.size.height);
    [_preferencesWindow setFrame:new display:true animate:_preferencesWindow.visible];
    [_preferencesWindow.contentView addSubview:tab];
}

- (BOOL)validateMenuItem:(NSMenuItem *)anItem
{
    if ([anItem action] == @selector(toggleDeveloperMode:)) {
        [(NSMenuItem *)anItem setState:[[NSUserDefaults standardUserDefaults] boolForKey:@"DeveloperMode"]];
    }
    
    if (anItem == self.linkCableMenuItem) {
        return [[NSDocumentController sharedDocumentController] documents].count > 1;
    }
    return true;
}

- (void)menuNeedsUpdate:(NSMenu *)menu
{
    NSMutableArray *items = [NSMutableArray array];
    NSDocument *currentDocument = [[NSDocumentController sharedDocumentController] currentDocument];
    
    for (NSDocument *document in [[NSDocumentController sharedDocumentController] documents]) {
        if (document == currentDocument) continue;
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:document.displayName action:@selector(connectLinkCable:) keyEquivalent:@""];
        item.representedObject = document;
        item.image = [[NSWorkspace sharedWorkspace] iconForFile:document.fileURL.path];
        [item.image setSize:NSMakeSize(16, 16)];
        [items addObject:item];
    }
    [menu removeAllItems];
    for (NSMenuItem *item in items) {
        [menu addItem:item];
    }
}

- (IBAction) showPreferences: (id) sender
{
    NSArray *objects;
    if (!_preferencesWindow) {
        [[NSBundle mainBundle] loadNibNamed:@"Preferences" owner:self topLevelObjects:&objects];
        NSToolbarItem *first_toolbar_item = [_preferencesWindow.toolbar.items firstObject];
        _preferencesWindow.toolbar.selectedItemIdentifier = [first_toolbar_item itemIdentifier];
        _preferencesTabs = @[self.emulationTab, self.graphicsTab, self.audioTab, self.controlsTab, self.updatesTab];
        [self switchPreferencesTab:first_toolbar_item];
        [_preferencesWindow center];
#ifndef UPDATE_SUPPORT
        [_preferencesWindow.toolbar removeItemAtIndex:4];
#endif
        for (unsigned i = _preferencesWindow.toolbar.items.count; i--;) {
            [_preferencesWindow.toolbar.items[i] _view].imageScaling = NSImageScaleNone;
        }
    }
    [_preferencesWindow makeKeyAndOrderFront:self];
}

- (BOOL)applicationOpenUntitledFile:(NSApplication *)sender
{
    [self askAutoUpdates];
    /* Bring an existing panel to the foreground */
    for (NSWindow *window in [[NSApplication sharedApplication] windows]) {
        if ([window isKindOfClass:[NSOpenPanel class]]) {
            [(NSOpenPanel *)window makeKeyAndOrderFront:nil];
            return true;
        }
    }
    [[NSDocumentController sharedDocumentController] openDocument:self];
    return true;
}

- (void)userNotificationCenter:(NSUserNotificationCenter *)center didActivateNotification:(NSUserNotification *)notification
{
    [[NSDocumentController sharedDocumentController] openDocumentWithContentsOfFile:notification.identifier display:true];
}

- (void)updateFound
{
    [[[NSURLSession sharedSession] dataTaskWithURL:[NSURL URLWithString:@UPDATE_SERVER "/raw_changes"] completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
        
        NSColor *linkColor = [NSColor colorWithRed:0.125 green:0.325 blue:1.0 alpha:1.0];
        if (@available(macOS 10.10, *)) {
            linkColor = [NSColor linkColor];
        }
        
        NSString *changes = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
        NSRange cutoffRange = [changes rangeOfString:@"<!--(" GB_VERSION ")-->"];
        if (cutoffRange.location != NSNotFound) {
            changes = [changes substringToIndex:cutoffRange.location];
        }
        
        NSString *html = [NSString stringWithFormat:@"<!DOCTYPE html><html><head><title></title>"
                          "<style>html {background-color:transparent; color: #%06x; line-height:1.5} a:link, a:visited{color:#%06x; text-decoration:none}</style>"
                          "</head><body>%@</body></html>",
                          color_to_int([NSColor textColor]),
                          color_to_int(linkColor),
                          changes];
        
        if ([(NSHTTPURLResponse *)response statusCode] == 200) {
            dispatch_async(dispatch_get_main_queue(), ^{
                NSArray *objects;
                [[NSBundle mainBundle] loadNibNamed:@"UpdateWindow" owner:self topLevelObjects:&objects];
                if (@available(macOS 10.11, *)) {
                    self.updateChanges.preferences.standardFontFamily = @"-apple-system";
                }
                else if (@available(macOS 10.10, *)) {
                    self.updateChanges.preferences.standardFontFamily = @"Helvetica Neue";
                }
                else {
                    self.updateChanges.preferences.standardFontFamily = @"Lucida Grande";
                }
                if (@available(macOS 10.15, *)) {
                    self.updateChanges.preferences.fixedFontFamily = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular].displayName;
                }
                else {
                    self.updateChanges.preferences.fixedFontFamily = @"Menlo";
                }
                self.updateChanges.drawsBackground = false;
                [self.updateChanges.mainFrame loadHTMLString:html baseURL:nil];
            });
        }
    }] resume];
}

- (NSArray *)webView:(WebView *)sender contextMenuItemsForElement:(NSDictionary *)element defaultMenuItems:(NSArray *)defaultMenuItems
{
    // Disable reload context menu
    if ([defaultMenuItems count] <= 2) {
        return nil;
    }
    return defaultMenuItems;
}

- (void)webView:(WebView *)sender didFinishLoadForFrame:(WebFrame *)frame
{
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sender.mainFrame.frameView.documentView.enclosingScrollView.drawsBackground = true;
        sender.mainFrame.frameView.documentView.enclosingScrollView.backgroundColor = [NSColor textBackgroundColor];
        sender.policyDelegate = self;
        [self.updateWindow center];
        [self.updateWindow makeKeyAndOrderFront:nil];
    });
}

- (void)webView:(WebView *)webView decidePolicyForNavigationAction:(NSDictionary *)actionInformation request:(NSURLRequest *)request frame:(WebFrame *)frame decisionListener:(id<WebPolicyDecisionListener>)listener
{
    [listener ignore];
    [[NSWorkspace sharedWorkspace] openURL:[request URL]];
}

- (void)checkForUpdates
{
#ifdef UPDATE_SUPPORT
    [[[NSURLSession sharedSession] dataTaskWithURL:[NSURL URLWithString:@UPDATE_SERVER "/latest_version"] completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.updatesSpinner stopAnimation:nil];
            [self.updatesButton setEnabled:true];
        });
        if ([(NSHTTPURLResponse *)response statusCode] == 200) {
            NSString *string = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
            NSArray <NSString *> *components = [string componentsSeparatedByString:@"|"];
            if (components.count != 2) return;
            _lastVersion = components[0];
            _updateURL = components[1];
            if (![@GB_VERSION isEqualToString:_lastVersion] &&
                ![[[NSUserDefaults standardUserDefaults] stringForKey:@"GBSkippedVersion"] isEqualToString:_lastVersion]) {
                [self updateFound];
            }
        }
    }] resume];
#endif
}

- (IBAction)userCheckForUpdates:(id)sender
{
    if (self.updateWindow) {
        [self.updateWindow makeKeyAndOrderFront:sender];
    }
    else {
        [[NSUserDefaults standardUserDefaults] setObject:nil forKey:@"GBSkippedVersion"];
        [self checkForUpdates];
        [sender setEnabled:false];
        [self.updatesSpinner startAnimation:sender];
    }
}

- (void)askAutoUpdates
{
#ifdef UPDATE_SUPPORT
    if (![[NSUserDefaults standardUserDefaults] boolForKey:@"GBAskedAutoUpdates"]) {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"Should SameBoy check for updates when launched?";
        alert.informativeText = @"SameBoy is frequently updated with new features, accuracy improvements, and bug fixes. This setting can always be changed in the preferences window.";
        [alert addButtonWithTitle:@"Check on Launch"];
        [alert addButtonWithTitle:@"Don't Check on Launch"];
        
        [[NSUserDefaults standardUserDefaults] setBool:[alert runModal] == NSAlertFirstButtonReturn forKey:@"GBAutoUpdatesEnabled"];
        [[NSUserDefaults standardUserDefaults] setBool:true forKey:@"GBAskedAutoUpdates"];
    }
#endif
}

- (IBAction)skipVersion:(id)sender
{
    [[NSUserDefaults standardUserDefaults] setObject:_lastVersion forKey:@"GBSkippedVersion"];
    [self.updateWindow performClose:sender];
}

- (bool)executePath:(NSString *)path withArguments:(NSArray <NSString *> *)arguments
{
    if (!_auth) {
        NSTask *task = [[NSTask alloc] init];
        task.launchPath = path;
        task.arguments = arguments;
        [task launch];
        [task waitUntilExit];
        return task.terminationStatus == 0 && task.terminationReason == NSTaskTerminationReasonExit;
    }
    
    char *argv[arguments.count + 1];
    argv[arguments.count] = NULL;
    for (unsigned i = 0; i < arguments.count; i++) {
        argv[i] = (char *)arguments[i].UTF8String;
    }
    
    return AuthorizationExecuteWithPrivileges(_auth, path.UTF8String, kAuthorizationFlagDefaults, argv, NULL) == errAuthorizationSuccess;
}

- (void)deauthorize
{
    if (_auth) {
        AuthorizationFree(_auth, kAuthorizationFlagDefaults);
        _auth = nil;
    }
}

- (IBAction)installUpdate:(id)sender
{
    bool needsAuthorization = false;
    if ([self executePath:@"/usr/sbin/spctl" withArguments:@[@"--status"]]) { // Succeeds when GateKeeper is on
        // GateKeeper is on, we need to --add ourselves as root, else we might get a GateKeeper crash
        needsAuthorization = true;
    }
    else if (access(_dyld_get_image_name(0), W_OK)) {
        // We don't have write access, so we need authorization to update as root
        needsAuthorization = true;
    }
    
    if (needsAuthorization && !_auth) {
        AuthorizationCreate(NULL, kAuthorizationEmptyEnvironment, kAuthorizationFlagPreAuthorize | kAuthorizationFlagInteractionAllowed, &_auth);
        // Make sure we can modify the bundle
        if (![self executePath:@"/usr/sbin/chown" withArguments:@[@"-R", [NSString stringWithFormat:@"%d:%d", getuid(), getgid()], [NSBundle mainBundle].bundlePath]]) {
            [self deauthorize];
            return;
        }
    }
    
    [self.updateProgressSpinner startAnimation:nil];
    self.updateProgressButton.title = @"Cancel";
    self.updateProgressButton.enabled = true;
    self.updateProgressLabel.stringValue = @"Downloading update…";
    _updateState = UPDATE_DOWNLOADING;
    _updateTask = [[NSURLSession sharedSession] downloadTaskWithURL: [NSURL URLWithString:_updateURL] completionHandler:^(NSURL *location, NSURLResponse *response, NSError *error) {
        _updateTask = nil;
        dispatch_sync(dispatch_get_main_queue(), ^{
            self.updateProgressButton.enabled = false;
            self.updateProgressLabel.stringValue = @"Extracting update…";
            _updateState = UPDATE_EXTRACTING;
        });
        
        _downloadDirectory = [[[NSFileManager defaultManager] URLForDirectory:NSItemReplacementDirectory
                                                                     inDomain:NSUserDomainMask
                                                            appropriateForURL:[[NSBundle mainBundle] bundleURL]
                                                                       create:true
                                                                        error:nil] path];
        if (!_downloadDirectory) {
            [self deauthorize];
            dispatch_sync(dispatch_get_main_queue(), ^{
                self.updateProgressButton.enabled = false;
                self.updateProgressLabel.stringValue = @"Failed to extract update.";
                _updateState = UPDATE_FAILED;
                self.updateProgressButton.title = @"Close";
                self.updateProgressButton.enabled = true;
                [self.updateProgressSpinner stopAnimation:nil];
            });
            return;
        }
        
        NSTask *unzipTask;
        unzipTask = [[NSTask alloc] init];
        unzipTask.launchPath = @"/usr/bin/unzip";
        unzipTask.arguments = @[location.path, @"-d", _downloadDirectory];
        [unzipTask launch];
        [unzipTask waitUntilExit];
        if (unzipTask.terminationStatus != 0 || unzipTask.terminationReason != NSTaskTerminationReasonExit) {
            [self deauthorize];
            [[NSFileManager defaultManager] removeItemAtPath:_downloadDirectory error:nil];
            dispatch_sync(dispatch_get_main_queue(), ^{
                self.updateProgressButton.enabled = false;
                self.updateProgressLabel.stringValue = @"Failed to extract update.";
                _updateState = UPDATE_FAILED;
                self.updateProgressButton.title = @"Close";
                self.updateProgressButton.enabled = true;
                [self.updateProgressSpinner stopAnimation:nil];
            });
            return;
        }
        
        dispatch_sync(dispatch_get_main_queue(), ^{
            self.updateProgressButton.enabled = false;
            self.updateProgressLabel.stringValue = @"Update ready, save your game progress and click Install.";
            _updateState = UPDATE_WAIT_INSTALL;
            self.updateProgressButton.title = @"Install";
            self.updateProgressButton.enabled = true;
            [self.updateProgressSpinner stopAnimation:nil];
        });
    }];
    [_updateTask resume];
    
    self.updateProgressWindow.preventsApplicationTerminationWhenModal = false;
    [self.updateWindow beginSheet:self.updateProgressWindow completionHandler:^(NSModalResponse returnCode) {
        [self.updateWindow close];
    }];
}

- (void)performUpgrade
{
    self.updateProgressButton.enabled = false;
    self.updateProgressLabel.stringValue = @"Instaling update…";
    _updateState = UPDATE_INSTALLING;
    self.updateProgressButton.enabled = false;
    [self.updateProgressSpinner startAnimation:nil];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSString *executablePath = [[NSBundle mainBundle] executablePath];
        NSString *contentsPath = [[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:@"Contents"];
        NSString *contentsTempPath = [[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:@"TempContents"];
        NSString *updateContentsPath = [_downloadDirectory stringByAppendingPathComponent:@"SameBoy.app/Contents"];
        NSError *error = nil;
        [[NSFileManager defaultManager] moveItemAtPath:contentsPath toPath:contentsTempPath error:&error];
        if (error) {
            [self deauthorize];
            [[NSFileManager defaultManager] removeItemAtPath:_downloadDirectory error:nil];
            _downloadDirectory = nil;
            dispatch_sync(dispatch_get_main_queue(), ^{
                self.updateProgressButton.enabled = false;
                self.updateProgressLabel.stringValue = @"Failed to install update.";
                _updateState = UPDATE_FAILED;
                self.updateProgressButton.title = @"Close";
                self.updateProgressButton.enabled = true;
                [self.updateProgressSpinner stopAnimation:nil];
            });
            return;
        }
        [[NSFileManager defaultManager] moveItemAtPath:updateContentsPath toPath:contentsPath error:&error];
        if (error) {
            [self deauthorize];
            [[NSFileManager defaultManager] moveItemAtPath:contentsTempPath toPath:contentsPath error:nil];
            [[NSFileManager defaultManager] removeItemAtPath:_downloadDirectory error:nil];
            _downloadDirectory = nil;
            dispatch_sync(dispatch_get_main_queue(), ^{
                self.updateProgressButton.enabled = false;
                self.updateProgressLabel.stringValue = @"Failed to install update.";
                _updateState = UPDATE_FAILED;
                self.updateProgressButton.title = @"Close";
                self.updateProgressButton.enabled = true;
                [self.updateProgressSpinner stopAnimation:nil];
            });
            return;
        }
        [[NSFileManager defaultManager] removeItemAtPath:_downloadDirectory error:nil];
        [[NSFileManager defaultManager] removeItemAtPath:contentsTempPath error:nil];
        
        // Remove the quarantine flag so we don't have to escape translocation
        NSString *bundlePath = [NSBundle mainBundle].bundlePath;
        removexattr(bundlePath.UTF8String, "com.apple.quarantine", 0);
        for (NSString *path in [[NSFileManager defaultManager] enumeratorAtPath:bundlePath]) {
            removexattr([bundlePath stringByAppendingPathComponent:path].UTF8String, "com.apple.quarantine", 0);
        };
        
        _downloadDirectory = nil;
        atexit_b(^{
            execl(executablePath.UTF8String, executablePath.UTF8String, "--update-launch", NULL);
        });
        
        dispatch_async(dispatch_get_main_queue(), ^{
            [NSApp terminate:nil];
        });
    });
}

- (IBAction)updateAction:(id)sender
{
    switch (_updateState) {
        case UPDATE_DOWNLOADING:
            [_updateTask cancelByProducingResumeData:nil];
            _updateTask = nil;
            [self.updateProgressWindow close];
            break;
        case UPDATE_WAIT_INSTALL:
            [self performUpgrade];
            break;
        case UPDATE_EXTRACTING:
        case UPDATE_INSTALLING:
            break;
        case UPDATE_FAILED:
            [self.updateProgressWindow close];
            break;
    }
}

- (void)orderFrontAboutPanel:(id)sender
{
    // NSAboutPanelOptionApplicationIcon is not available prior to 10.13, but the key is still there and working.
    [[NSApplication sharedApplication] orderFrontStandardAboutPanelWithOptions:@{
        @"ApplicationIcon": [NSImage imageNamed:@"Icon"]
    }];
}

- (void)controller:(JOYController *)controller buttonChangedState:(JOYButton *)button
{
    if (!button.isPressed) return;
    NSDictionary *mapping = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"JoyKitInstanceMapping"][controller.uniqueID];
    if (!mapping) {
        mapping = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"JoyKitNameMapping"][controller.deviceName];
    }
    
    JOYButtonUsage usage = ((JOYButtonUsage)[mapping[n2s(button.uniqueID)] unsignedIntValue]) ?: -1;
    if (!mapping && usage >= JOYButtonUsageGeneric0) {
        usage = GB_inline_const(JOYButtonUsage[], {JOYButtonUsageY, JOYButtonUsageA, JOYButtonUsageB, JOYButtonUsageX})[(usage - JOYButtonUsageGeneric0) & 3];
    }
    
    if (usage == GBJoyKitHotkey1 || usage == GBJoyKitHotkey2) {
        if (_preferencesWindow && self.keyWindow == _preferencesWindow) {
            return;
        }
        if (![[NSUserDefaults standardUserDefaults] boolForKey:@"GBAllowBackgroundControllers"] && !self.keyWindow) {
            return;
        }

        NSString *keyEquivalent = [[NSUserDefaults standardUserDefaults] stringForKey:usage == GBJoyKitHotkey1? @"GBJoypadHotkey1" : @"GBJoypadHotkey2"];
        NSEventModifierFlags flags = NSEventModifierFlagCommand;
        if ([keyEquivalent hasPrefix:@"^"]) {
            flags |= NSEventModifierFlagShift;
            [keyEquivalent substringFromIndex:1];
        }
        _simulatingMenuEvent = true;
        [[NSApplication sharedApplication] sendEvent:[NSEvent keyEventWithType:NSEventTypeKeyDown
                                                                                 location:(NSPoint){0,}
                                                                            modifierFlags:flags
                                                                                timestamp:0
                                                                             windowNumber:0
                                                                                  context:NULL
                                                                               characters:keyEquivalent
                                                              charactersIgnoringModifiers:keyEquivalent
                                                                                isARepeat:false
                                                                                  keyCode:0]];
        _simulatingMenuEvent = false;
    }
}

- (NSWindow *)keyWindow
{
    NSWindow *ret = [super keyWindow];
    if (!ret && _simulatingMenuEvent) {
        ret = [(Document *)self.orderedDocuments.firstObject mainWindow];
    }
    return ret;
}

- (NSWindow *)mainWindow
{
    NSWindow *ret = [super mainWindow];
    if (!ret && _simulatingMenuEvent) {
        ret = [(Document *)self.orderedDocuments.firstObject mainWindow];
    }
    return ret;
}

- (IBAction)openDebuggerHelp:(id)sender
{
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://sameboy.github.io/debugger/"]];
}

- (IBAction)openSponsor:(id)sender
{
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://github.com/sponsors/LIJI32"]];
}

- (void)dealloc
{
    if (_downloadDirectory) {
        [[NSFileManager defaultManager] removeItemAtPath:_downloadDirectory error:nil];
    }
}

- (IBAction)nop:(id)sender
{
}

/* This runs before C constructors. If we need to escape translocation, we should
   do it ASAP to minimize our launch time. */

+ (void)load
{
    if (@available(macOS 10.12, *)) {
        /* Detect and escape translocation so we can safely update ourselves */
        if ([[[NSBundle mainBundle] bundlePath] containsString:@"/AppTranslocation/"]) {
            const char *mountPath = [[[[NSBundle mainBundle] bundlePath] stringByDeletingLastPathComponent] stringByDeletingLastPathComponent].UTF8String;
            struct statfs *mntbuf;
            int mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
            for (unsigned i = 0; i < mntsize; i++) {
                if (strcmp(mntbuf[i].f_mntonname, mountPath) == 0) {
                    NSBundle *origBundle = [NSBundle bundleWithPath:@(mntbuf[i].f_mntfromname)];
                    
                    execl(origBundle.executablePath.UTF8String, origBundle.executablePath.UTF8String, NULL);
                    break;
                }
            }
        }
    }
}
@end

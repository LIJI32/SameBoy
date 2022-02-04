#import "AppDelegate.h"
#include "GBButtons.h"
#include "GBView.h"
#include <Core/gb.h>
#import <Carbon/Carbon.h>
#import <JoyKit/JoyKit.h>
#import <WebKit/WebKit.h>

#define UPDATE_SERVER "https://sameboy.github.io"

static uint32_t color_to_int(NSColor *color)
{
    color = [color colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
    return (((unsigned)(color.redComponent * 0xFF)) << 16) |
           (((unsigned)(color.greenComponent * 0xFF)) << 8) |
           ((unsigned)(color.blueComponent * 0xFF));
}

@implementation AppDelegate
{
    NSWindow *preferences_window;
    NSArray<NSView *> *preferences_tabs;
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
}

- (void) applicationDidFinishLaunching:(NSNotification *)notification
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    for (unsigned i = 0; i < GBButtonCount; i++) {
        if ([[defaults objectForKey:button_to_preference_name(i, 0)] isKindOfClass:[NSString class]]) {
            [defaults removeObjectForKey:button_to_preference_name(i, 0)];
        }
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
                                                              @"GBColorCorrection": @(GB_COLOR_CORRECTION_EMULATE_HARDWARE),
                                                              @"GBHighpassFilter": @(GB_HIGHPASS_REMOVE_DC_OFFSET),
                                                              @"GBRewindLength": @(10),
                                                              @"GBFrameBlendingMode": @([defaults boolForKey:@"DisableFrameBlending"]? GB_FRAME_BLENDING_MODE_DISABLED : GB_FRAME_BLENDING_MODE_ACCURATE),
                                                              
                                                              @"GBDMGModel": @(GB_MODEL_DMG_B),
                                                              @"GBCGBModel": @(GB_MODEL_CGB_E),
                                                              @"GBSGBModel": @(GB_MODEL_SGB2),
                                                              @"GBRumbleMode": @(GB_RUMBLE_CARTRIDGE_ONLY),
                                                              
                                                              @"GBVolume": @(1.0),
                                                              
                                                              @"GBMBC7JoystickOverride": @NO,
                                                              @"GBMBC7AllowMouse": @YES,
                                                              }];
    
    [JOYController startOnRunLoop:[NSRunLoop currentRunLoop] withOptions:@{
        JOYAxes2DEmulateButtonsKey: @YES,
        JOYHatsEmulateButtonsKey: @YES,
    }];
    
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
}

- (IBAction)toggleDeveloperMode:(id)sender
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setBool:![defaults boolForKey:@"DeveloperMode"] forKey:@"DeveloperMode"];
}

- (IBAction)switchPreferencesTab:(id)sender
{
    for (NSView *view in preferences_tabs) {
        [view removeFromSuperview];
    }
    NSView *tab = preferences_tabs[[sender tag]];
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
    menu.itemArray = items;
}

- (IBAction) showPreferences: (id) sender
{
    NSArray *objects;
    if (!_preferencesWindow) {
        [[NSBundle mainBundle] loadNibNamed:@"Preferences" owner:self topLevelObjects:&objects];
        NSToolbarItem *first_toolbar_item = [_preferencesWindow.toolbar.items firstObject];
        _preferencesWindow.toolbar.selectedItemIdentifier = [first_toolbar_item itemIdentifier];
        preferences_tabs = @[self.emulationTab, self.graphicsTab, self.audioTab, self.controlsTab, self.updatesTab];
        [self switchPreferencesTab:first_toolbar_item];
        [_preferencesWindow center];
#ifndef UPDATE_SUPPORT
        [_preferencesWindow.toolbar removeItemAtIndex:4];
#endif
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
                self.updateChanges.preferences.standardFontFamily = [NSFont systemFontOfSize:0].familyName;
                self.updateChanges.preferences.fixedFontFamily = @"Menlo";
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

- (IBAction)installUpdate:(id)sender
{
    [self.updateProgressSpinner startAnimation:nil];
    self.updateProgressButton.title = @"Cancel";
    self.updateProgressButton.enabled = true;
    self.updateProgressLabel.stringValue = @"Downloading update...";
    _updateState = UPDATE_DOWNLOADING;
    _updateTask = [[NSURLSession sharedSession] downloadTaskWithURL: [NSURL URLWithString:_updateURL] completionHandler:^(NSURL *location, NSURLResponse *response, NSError *error) {
        _updateTask = nil;
        dispatch_sync(dispatch_get_main_queue(), ^{
            self.updateProgressButton.enabled = false;
            self.updateProgressLabel.stringValue = @"Extracting update...";
            _updateState = UPDATE_EXTRACTING;
        });
        
        _downloadDirectory = [[[NSFileManager defaultManager] URLForDirectory:NSItemReplacementDirectory
                                                                     inDomain:NSUserDomainMask
                                                            appropriateForURL:[[NSBundle mainBundle] bundleURL]
                                                                       create:true
                                                                        error:nil] path];
        NSTask *unzipTask;
        if (!_downloadDirectory) {
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
        
        unzipTask = [[NSTask alloc] init];
        unzipTask.launchPath = @"/usr/bin/unzip";
        unzipTask.arguments = @[location.path, @"-d", _downloadDirectory];
        [unzipTask launch];
        [unzipTask waitUntilExit];
        if (unzipTask.terminationStatus != 0 || unzipTask.terminationReason != NSTaskTerminationReasonExit) {
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
    self.updateProgressLabel.stringValue = @"Instaling update...";
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

- (void)dealloc
{
    if (_downloadDirectory) {
        [[NSFileManager defaultManager] removeItemAtPath:_downloadDirectory error:nil];
    }
}

- (IBAction)nop:(id)sender
{
}
@end

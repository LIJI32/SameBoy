#import "GBPreferencesWindow.h"
#import "GBJoyConManager.h"
#import "NSString+StringForKey.h"
#import "GBButtons.h"
#import "BigSurToolbar.h"
#import "GBViewMetal.h"
#import "GBWarningPopover.h"
#import <Carbon/Carbon.h>

@implementation GBPreferencesWindow
{
    bool is_button_being_modified;
    NSInteger button_being_modified;
    signed joystick_configuration_state;
    NSString *joystick_being_configured;
    bool joypad_wait;

    NSEventModifierFlags previousModifiers;
}

- (NSWindowToolbarStyle)toolbarStyle
{
    return NSWindowToolbarStyleExpanded;
}

- (void)close
{
    joystick_configuration_state = -1;
    [self.configureJoypadButton setEnabled:true];
    [self.skipButton setEnabled:false];
    [self.configureJoypadButton setTitle:@"Configure Controller"];
    [super close];
}

static inline NSString *keyEquivalentString(NSMenuItem *item)
{
    return [NSString stringWithFormat:@"%s%@", (item.keyEquivalentModifierMask & NSEventModifierFlagShift)? "^":"", item.keyEquivalent];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
    if (self.playerListButton.selectedTag == 0) {
        return GBKeyboardButtonCount;
    }
    return GBPerPlayerButtonCount;
}

- (unsigned) usesForKey:(unsigned) key
{
    unsigned ret = 0;
    for (unsigned player = 4; player--;) {
        for (unsigned button = player == 0? GBKeyboardButtonCount:GBPerPlayerButtonCount; button--;) {
            NSNumber *other = [[NSUserDefaults standardUserDefaults] valueForKey:button_to_preference_name(button, player)];
            if (other && [other unsignedIntValue] == key) {
                ret++;
            }
        }
    }
    return ret;
}

- (id)tableView:(NSTableView *)tableView objectValueForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row
{
    if ([tableColumn.identifier isEqualToString:@"keyName"]) {
        return GBButtonNames[row];
    }

    if (is_button_being_modified && button_being_modified == row) {
        return @"Select a new key…";
    }
    
    NSNumber *key = [[NSUserDefaults standardUserDefaults] valueForKey:button_to_preference_name(row, self.playerListButton.selectedTag)];
    if (key) {
        if ([self usesForKey:[key unsignedIntValue]] > 1) {
            return [[NSAttributedString alloc] initWithString:[NSString displayStringForKeyCode: [key unsignedIntegerValue]]
                                                   attributes:@{NSForegroundColorAttributeName: [NSColor colorWithRed:0.9375 green:0.25 blue:0.25 alpha:1.0],
                                                                NSFontAttributeName: [NSFont boldSystemFontOfSize:[NSFont systemFontSize]]
                                                   }];
        }
        return [NSString displayStringForKeyCode: [key unsignedIntegerValue]];
    }

    return @"";
}

- (BOOL)tableView:(NSTableView *)tableView shouldEditTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row
{

    dispatch_async(dispatch_get_main_queue(), ^{
        is_button_being_modified = true;
        button_being_modified = row;
        tableView.enabled = false;
        self.playerListButton.enabled = false;
        [tableView reloadData];
        [self makeFirstResponder:self];
    });
    return false;
}

-(void)keyDown:(NSEvent *)theEvent
{
    if (!is_button_being_modified) {
        if (self.firstResponder != self.controlsTableView && [theEvent type] != NSEventTypeFlagsChanged) {
            [super keyDown:theEvent];
        }
        return;
    }

    is_button_being_modified = false;

    [[NSUserDefaults standardUserDefaults] setInteger:theEvent.keyCode
                                              forKey:button_to_preference_name(button_being_modified, self.playerListButton.selectedTag)];
    self.controlsTableView.enabled = true;
    self.playerListButton.enabled = true;
    [self.controlsTableView reloadData];
    [self makeFirstResponder:self.controlsTableView];
}

- (void) flagsChanged:(NSEvent *)event
{
    if (event.modifierFlags > previousModifiers) {
        [self keyDown:event];
    }
    
    previousModifiers = event.modifierFlags;
}

- (void)updatePalettesMenu
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSDictionary *themes = [defaults dictionaryForKey:@"GBThemes"];
    NSMenu *menu = _colorPalettePopupButton.menu;
    while (menu.itemArray.count != 4) {
        [menu removeItemAtIndex:4];
    }
    [menu addItem:[NSMenuItem separatorItem]];
    for (NSString *name in [themes.allKeys sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)]) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:name action:nil keyEquivalent:@""];
        item.tag = -2;
        [menu addItem:item];
    }
    if (themes) {
        [menu addItem:[NSMenuItem separatorItem]];
    }
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@"Custom…" action:nil keyEquivalent:@""];
    item.tag = -1;
    [menu addItem:item];
}

- (IBAction)colorPaletteChanged:(id)sender
{
    signed tag = [sender selectedItem].tag;
    if (tag == -2) {
        [[NSUserDefaults standardUserDefaults] setObject:@(-1)
                                                  forKey:@"GBColorPalette"];
        [[NSUserDefaults standardUserDefaults] setObject:[sender selectedItem].title
                                                  forKey:@"GBCurrentTheme"];

    }
    else if (tag == -1) {
        [[NSUserDefaults standardUserDefaults] setObject:@(-1)
                                                  forKey:@"GBColorPalette"];
        [_paletteEditorController awakeFromNib];
        [self beginSheet:_paletteEditor completionHandler:^(NSModalResponse returnCode) {
            [self updatePalettesMenu];
            [_colorPalettePopupButton selectItemWithTitle:[[NSUserDefaults standardUserDefaults] stringForKey:@"GBCurrentTheme"] ?: @""];
        }];
    }
    else {
        [[NSUserDefaults standardUserDefaults] setObject:@([sender selectedItem].tag)
                                                  forKey:@"GBColorPalette"];
    }
    [[NSNotificationCenter defaultCenter] postNotificationName:@"GBColorPaletteChanged" object:nil];
}

- (IBAction)hotkey1Changed:(id)sender
{
    [[NSUserDefaults standardUserDefaults] setObject:keyEquivalentString([sender selectedItem])
                                              forKey:@"GBJoypadHotkey1"];
}

- (IBAction)hotkey2Changed:(id)sender
{
    [[NSUserDefaults standardUserDefaults] setObject:keyEquivalentString([sender selectedItem])
                                              forKey:@"GBJoypadHotkey2"];
}


- (IBAction) configureJoypad:(id)sender
{
    [self.configureJoypadButton setEnabled:false];
    [self.skipButton setEnabled:true];
    joystick_being_configured = nil;
    [self advanceConfigurationStateMachine];
}

- (IBAction) skipButton:(id)sender
{
    [self advanceConfigurationStateMachine];
}

- (void) advanceConfigurationStateMachine
{
    joystick_configuration_state++;
    if (joystick_configuration_state == GBUnderclock) {
        [self.configureJoypadButton setTitle:@"Press Button for Slo-Mo"]; // Full name is too long :<
    }
    else if (joystick_configuration_state < GBTotalButtonCount) {
        [self.configureJoypadButton setTitle:[NSString stringWithFormat:@"Press Button for %@", GBButtonNames[joystick_configuration_state]]];
    }
    else {
        joystick_configuration_state = -1;
        [self.configureJoypadButton setEnabled:true];
        [self.skipButton setEnabled:false];
        [self.configureJoypadButton setTitle:@"Configure Joypad"];
    }
}

- (void)controller:(JOYController *)controller buttonChangedState:(JOYButton *)button
{
    /* Debounce */
    if (joypad_wait) return;
    joypad_wait = true;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.25 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        joypad_wait = false;
    });
        
    if (!button.isPressed) return;
    if (joystick_configuration_state == -1) return;
    if (joystick_configuration_state == GBTotalButtonCount) return;
    if (!joystick_being_configured) {
        joystick_being_configured = controller.uniqueID;
    }
    else if (![joystick_being_configured isEqualToString:controller.uniqueID]) {
        return;
    }
    
    NSMutableDictionary *instance_mappings = [[[NSUserDefaults standardUserDefaults] dictionaryForKey:@"JoyKitInstanceMapping"] mutableCopy];
    
    NSMutableDictionary *name_mappings = [[[NSUserDefaults standardUserDefaults] dictionaryForKey:@"JoyKitNameMapping"] mutableCopy];

    
    if (!instance_mappings) {
        instance_mappings = [[NSMutableDictionary alloc] init];
    }
    
    if (!name_mappings) {
        name_mappings = [[NSMutableDictionary alloc] init];
    }
    
    NSMutableDictionary *mapping = nil;
    if (joystick_configuration_state != 0) {
        mapping = [instance_mappings[controller.uniqueID] mutableCopy];
    }
    else {
        mapping = [[NSMutableDictionary alloc] init];
    }

    
    static const unsigned gb_to_joykit[] = {
    [GBRight] = JOYButtonUsageDPadRight,
    [GBLeft] = JOYButtonUsageDPadLeft,
    [GBUp] = JOYButtonUsageDPadUp,
    [GBDown] = JOYButtonUsageDPadDown,
    [GBA] = JOYButtonUsageA,
    [GBB] = JOYButtonUsageB,
    [GBSelect] = JOYButtonUsageSelect,
    [GBStart] = JOYButtonUsageStart,
    [GBRapidA] = GBJoyKitRapidA,
    [GBRapidB] = GBJoyKitRapidB,
    [GBTurbo] = JOYButtonUsageL1,
    [GBRewind] = JOYButtonUsageL2,
    [GBUnderclock] = JOYButtonUsageR1,
    [GBHotkey1] = GBJoyKitHotkey1,
    [GBHotkey2] = GBJoyKitHotkey2,
    };
    
    if (joystick_configuration_state == GBUnderclock) {
        mapping[@"AnalogUnderclock"] = nil;
        double max = 0;
        for (JOYAxis *axis in controller.axes) {
            if ((axis.value > 0.5 || (axis.equivalentButtonUsage == button.usage)) && axis.value >= max) {
                mapping[@"AnalogUnderclock"] = @(axis.uniqueID);
                break;
            }
        }
    }
    
    if (joystick_configuration_state == GBTurbo) {
        mapping[@"AnalogTurbo"] = nil;
        double max = 0;
        for (JOYAxis *axis in controller.axes) {
            if ((axis.value > 0.5 || (axis.equivalentButtonUsage == button.usage)) && axis.value >= max) {
                max = axis.value;
                mapping[@"AnalogTurbo"] = @(axis.uniqueID);
            }
        }
    }
    
    mapping[n2s(button.uniqueID)] = @(gb_to_joykit[joystick_configuration_state]);
    
    instance_mappings[controller.uniqueID] = mapping;
    name_mappings[controller.deviceName] = mapping;
    [[NSUserDefaults standardUserDefaults] setObject:instance_mappings forKey:@"JoyKitInstanceMapping"];
    [[NSUserDefaults standardUserDefaults] setObject:name_mappings forKey:@"JoyKitNameMapping"];
    [self advanceConfigurationStateMachine];
}

- (void)awakeFromNib
{
    [super awakeFromNib];
    [self updateBootROMFolderButton];
    [[NSDistributedNotificationCenter defaultCenter] addObserver:self.controlsTableView selector:@selector(reloadData) name:(NSString*)kTISNotifySelectedKeyboardInputSourceChanged object:nil];
    [JOYController registerListener:self];
    joystick_configuration_state = -1;
    [self refreshJoypadMenu:nil];
    
    NSString *keyEquivalent = [[NSUserDefaults standardUserDefaults] stringForKey:@"GBJoypadHotkey1"];
    for (NSMenuItem *item in _hotkey1PopupButton.menu.itemArray) {
        if ([keyEquivalent isEqualToString:keyEquivalentString(item)]) {
            [_hotkey1PopupButton selectItem:item];
            break;
        }
    }
    
    keyEquivalent = [[NSUserDefaults standardUserDefaults] stringForKey:@"GBJoypadHotkey2"];
    for (NSMenuItem *item in _hotkey2PopupButton.menu.itemArray) {
        if ([keyEquivalent isEqualToString:keyEquivalentString(item)]) {
            [_hotkey2PopupButton selectItem:item];
            break;
        }
    }
    
    [self updatePalettesMenu];
    NSInteger mode = [[NSUserDefaults standardUserDefaults] integerForKey:@"GBColorPalette"];
    if (mode >= 0) {
        [_colorPalettePopupButton selectItemWithTag:mode];
    }
    else {
        [_colorPalettePopupButton selectItemWithTitle:[[NSUserDefaults standardUserDefaults] stringForKey:@"GBCurrentTheme"] ?: @""];
    }
    
    _fontSizeStepper.intValue = [[NSUserDefaults standardUserDefaults] integerForKey:@"GBDebuggerFontSize"];
    [self updateFonts];
}

- (IBAction)fontSizeChanged:(id)sender
{
    NSString *selectedFont = [[NSUserDefaults standardUserDefaults] stringForKey:@"GBDebuggerFont"];
    [[NSUserDefaults standardUserDefaults] setInteger:[sender intValue] forKey:@"GBDebuggerFontSize"];
    [_fontPopupButton setDisplayTitle:[NSString stringWithFormat:@"%@ %upt", selectedFont, (unsigned)[[NSUserDefaults standardUserDefaults] integerForKey:@"GBDebuggerFontSize"]]];
}

- (IBAction)fontChanged:(id)sender
{
    NSString *selectedFont = _fontPopupButton.selectedItem.title;
    [[NSUserDefaults standardUserDefaults] setObject:selectedFont forKey:@"GBDebuggerFont"];
    [_fontPopupButton setDisplayTitle:[NSString stringWithFormat:@"%@ %upt", selectedFont, (unsigned)[[NSUserDefaults standardUserDefaults] integerForKey:@"GBDebuggerFontSize"]]];

}

- (void)updateFonts
{
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSFontManager *fontManager = [NSFontManager sharedFontManager];
        NSArray *allFamilies = [fontManager availableFontFamilies];
        NSMutableSet *families = [NSMutableSet set];
        for (NSString *family in allFamilies) {
            if ([fontManager fontNamed:family hasTraits:NSFixedPitchFontMask]) {
                [families addObject:family];
            }
        }
        
        bool hasSFMono = false;
        if (@available(macOS 10.15, *)) {
            hasSFMono = [[NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular].displayName containsString:@"SF"];
        }
        
        if (hasSFMono) {
            [families addObject:@"SF Mono"];
        }
    
        NSArray *sortedFamilies = [[families allObjects] sortedArrayUsingSelector:@selector(compare:)];

        dispatch_async(dispatch_get_main_queue(), ^{
            if (![families containsObject:[[NSUserDefaults standardUserDefaults] stringForKey:@"GBDebuggerFont"]]) {
                [[NSUserDefaults standardUserDefaults] removeObjectForKey:@"GBDebuggerFont"];
            }
            
            [_fontPopupButton.menu removeAllItems];
            for (NSString *family in sortedFamilies) {
                [_fontPopupButton addItemWithTitle:family];
            }
            NSString *selectedFont = [[NSUserDefaults standardUserDefaults] stringForKey:@"GBDebuggerFont"];
            [_fontPopupButton selectItemWithTitle:selectedFont];
            [_fontPopupButton setDisplayTitle:[NSString stringWithFormat:@"%@ %upt", selectedFont, (unsigned)[[NSUserDefaults standardUserDefaults] integerForKey:@"GBDebuggerFontSize"]]];
        });
    });
}

- (void)dealloc
{
    [JOYController unregisterListener:self];
    [[NSDistributedNotificationCenter defaultCenter] removeObserver:self.controlsTableView];
}

- (IBAction)selectOtherBootROMFolder:(id)sender
{
    NSOpenPanel *panel = [[NSOpenPanel alloc] init];
    [panel setCanChooseDirectories:true];
    [panel setCanChooseFiles:false];
    [panel setPrompt:@"Select"];
    [panel setDirectoryURL:[[NSUserDefaults standardUserDefaults] URLForKey:@"GBBootROMsFolder"]];
    [panel beginSheetModalForWindow:self completionHandler:^(NSModalResponse result) {
        if (result == NSModalResponseOK) {
            NSURL *url = [[panel URLs] firstObject];
            [[NSUserDefaults standardUserDefaults] setURL:url forKey:@"GBBootROMsFolder"];
        }
        [self updateBootROMFolderButton];
    }];

}

- (void) updateBootROMFolderButton
{
    NSURL *url = [[NSUserDefaults standardUserDefaults] URLForKey:@"GBBootROMsFolder"];
    BOOL is_dir = false;
    [[NSFileManager defaultManager] fileExistsAtPath:[url path] isDirectory:&is_dir];
    if (!is_dir) url = nil;
    
    if (url) {
        [self.bootROMsFolderItem setTitle:[url lastPathComponent]];
        NSImage *icon = [[NSWorkspace sharedWorkspace] iconForFile:[url path]];
        [icon setSize:NSMakeSize(16, 16)];
        [self.bootROMsFolderItem setHidden:false];
        [self.bootROMsFolderItem setImage:icon];
        [self.bootROMsButton selectItemAtIndex:1];
    }
    else {
        [self.bootROMsFolderItem setHidden:true];
        [self.bootROMsButton selectItemAtIndex:0];
    }
}

- (IBAction)useBuiltinBootROMs:(id)sender
{
    [[NSUserDefaults standardUserDefaults] setURL:nil forKey:@"GBBootROMsFolder"];
    [self updateBootROMFolderButton];
}

- (void)controllerConnected:(JOYController *)controller
{
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.25 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [self refreshJoypadMenu:nil];
    });
}

- (void)controllerDisconnected:(JOYController *)controller
{
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.25 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [self refreshJoypadMenu:nil];
    });
}

- (IBAction)refreshJoypadMenu:(id)sender
{
    bool preferred_is_connected = false;
    NSString *player_string = n2s(self.playerListButton.selectedTag);
    NSString *selected_controller = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"JoyKitDefaultControllers"][player_string];
    
    [self.preferredJoypadButton removeAllItems];
    [self.preferredJoypadButton addItemWithTitle:@"None"];
    for (JOYController *controller in [JOYController allControllers]) {
        [self.preferredJoypadButton addItemWithTitle:[NSString stringWithFormat:@"%@ (%@)", controller.deviceName, controller.uniqueID]];
        
        self.preferredJoypadButton.lastItem.identifier = controller.uniqueID;
        
        if ([controller.uniqueID isEqualToString:selected_controller]) {
            preferred_is_connected = true;
            [self.preferredJoypadButton selectItem:self.preferredJoypadButton.lastItem];
        }
    }
    
    if (!preferred_is_connected && selected_controller) {
        [self.preferredJoypadButton addItemWithTitle:[NSString stringWithFormat:@"Unavailable Controller (%@)", selected_controller]];
        self.preferredJoypadButton.lastItem.identifier = selected_controller;
        [self.preferredJoypadButton selectItem:self.preferredJoypadButton.lastItem];
    }
    

    if (!selected_controller) {
        [self.preferredJoypadButton selectItemWithTitle:@"None"];
    }
    [self.controlsTableView reloadData];
}

- (IBAction)changeDefaultJoypad:(id)sender
{
    NSMutableDictionary *default_joypads = [[[NSUserDefaults standardUserDefaults] dictionaryForKey:@"JoyKitDefaultControllers"] mutableCopy];
    if (!default_joypads) {
        default_joypads = [[NSMutableDictionary alloc] init];
    }

    NSString *player_string = n2s(self.playerListButton.selectedTag);
    if ([[sender titleOfSelectedItem] isEqualToString:@"None"]) {
        [default_joypads removeObjectForKey:player_string];
    }
    else {
        default_joypads[player_string] = [[sender selectedItem] identifier];
    }
    [[NSUserDefaults standardUserDefaults] setObject:default_joypads forKey:@"JoyKitDefaultControllers"];
}

- (IBAction)displayColorCorrectionHelp:(id)sender
{
    [GBWarningPopover popoverWithContents:
         GB_inline_const(NSString *[], {
            [GB_COLOR_CORRECTION_DISABLED] = @"Colors are directly interpreted as sRGB, resulting in unbalanced colors and inaccurate hues.",
            [GB_COLOR_CORRECTION_CORRECT_CURVES] = @"Colors have their brightness corrected, but hues remain unbalanced.",
            [GB_COLOR_CORRECTION_MODERN_BALANCED] = @"Emulates a modern display. Blue contrast is moderately enhanced at the cost of slight hue inaccuracy.",
            [GB_COLOR_CORRECTION_MODERN_BOOST_CONTRAST] = @"Like Modern – Balanced, but further boosts the contrast of greens and magentas that is lacking on the original hardware.",
            [GB_COLOR_CORRECTION_REDUCE_CONTRAST] = @"Slightly reduce the contrast to better represent the tint and contrast of the original display.",
            [GB_COLOR_CORRECTION_LOW_CONTRAST] = @"Harshly reduce the contrast to accurately represent the tint and low constrast of the original display.",
            [GB_COLOR_CORRECTION_MODERN_ACCURATE] = @"Emulates a modern display. Colors have their hues and brightness corrected.",
         }) [self.colorCorrectionPopupButton.selectedItem.tag]
                                   title:self.colorCorrectionPopupButton.selectedItem.title
                                   onView:sender
                                  timeout:6
                            preferredEdge:NSRectEdgeMaxX];
}

- (IBAction)displayHighPassHelp:(id)sender
{
    [GBWarningPopover popoverWithContents:
    GB_inline_const(NSString *[], {
        [GB_HIGHPASS_OFF] = @"No high-pass filter will be applied. DC offset will be kept, pausing and resuming will trigger audio pops.",
        [GB_HIGHPASS_ACCURATE] = @"An accurate high-pass filter will be applied, removing the DC offset while somewhat attenuating the bass.",
        [GB_HIGHPASS_REMOVE_DC_OFFSET] = @"A high-pass filter will be applied to the DC offset itself, removing the DC offset while preserving the waveform.",
    }) [self.highpassFilterPopupButton.selectedItem.tag]
                                    title:self.highpassFilterPopupButton.selectedItem.title
                                   onView:sender
                                  timeout:6
                            preferredEdge:NSRectEdgeMaxX];
}

- (IBAction)arrangeJoyCons:(id)sender
{
    [GBJoyConManager sharedInstance].arrangementMode = true;
    [self beginSheet:self.joyconsSheet completionHandler:nil];
}

- (IBAction)closeJoyConsSheet:(id)sender
{
    [self endSheet:self.joyconsSheet];
    [GBJoyConManager sharedInstance].arrangementMode = false;
}

@end

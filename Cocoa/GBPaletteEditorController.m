#import "GBPaletteEditorController.h"
#import "GBHueSliderCell.h"
#import <Core/gb.h>

#define MAGIC 'SBPL'

typedef struct __attribute__ ((packed)) {
    uint32_t magic;
    bool manual:1;
    bool disabled_lcd_color:1;
    unsigned padding:6;
    struct GB_color_s colors[5];
    int32_t brightness_bias;
    uint32_t hue_bias;
    uint32_t hue_bias_strength;
} theme_t;

static double blend(double from, double to, double position)
{
    return from * (1 - position) + to * position;
}

@implementation NSColor (GBColor)

- (struct GB_color_s)gbColor
{
    NSColor *sRGB = [self colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
    return (struct GB_color_s){round(sRGB.redComponent * 255), round(sRGB.greenComponent * 255), round(sRGB.blueComponent * 255)};
}

- (uint32_t)intValue
{
    struct GB_color_s color = self.gbColor;
    return (color.r << 0) | (color.g << 8) | (color.b << 16) | 0xFF000000;
}

@end

@implementation GBPaletteEditorController

- (NSArray<NSColorWell *> *)colorWells
{
    return @[_colorWell0, _colorWell1, _colorWell2, _colorWell3, _colorWell4];
}

- (void)updateEnabledControls
{
    if (self.manualModeCheckbox.state) {
        _brightnessSlider.enabled = false;
        _hueSlider.enabled = false;
        _hueStrengthSlider.enabled = false;
        _colorWell1.enabled = true;
        _colorWell2.enabled = true;
        _colorWell3.enabled = true;
        if (!(_colorWell4.enabled = self.disableLCDColorCheckbox.state)) {
            _colorWell4.color = _colorWell3.color;
        }
    }
    else {
        _colorWell1.enabled = false;
        _colorWell2.enabled = false;
        _colorWell3.enabled = false;
        _colorWell4.enabled = true;
        _brightnessSlider.enabled = true;
        _hueSlider.enabled = true;
        _hueStrengthSlider.enabled = true;
        [self updateAutoColors];
    }
}

- (NSColor *)autoColorAtPositon:(double)position
{
    NSColor *first = [_colorWell0.color colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
    NSColor *second = [_colorWell4.color colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
    double brightness = 1 / pow(4, (_brightnessSlider.doubleValue - 128) / 128.0);
    position = pow(position, brightness);
    NSColor *hue = _hueSlider.colorValue;
    double bias = _hueStrengthSlider.doubleValue / 256.0;
    double red = 1 / pow(4, (hue.redComponent * 2 - 1) * bias);
    double green = 1 / pow(4, (hue.greenComponent * 2 - 1) * bias);
    double blue = 1 / pow(4, (hue.blueComponent * 2 - 1) * bias);
    NSColor *ret = [NSColor colorWithRed:blend(first.redComponent, second.redComponent, pow(position, red))
                                   green:blend(first.greenComponent, second.greenComponent, pow(position, green))
                                    blue:blend(first.blueComponent, second.blueComponent, pow(position, blue))
                                   alpha:1.0];
    return ret;
}

- (IBAction)updateAutoColors:(id)sender
{
    if (!self.manualModeCheckbox.state) {
        [self updateAutoColors];
    }
    else {
        [self savePalette:sender];
    }
}

- (void)updateAutoColors
{
    if (_disableLCDColorCheckbox.state) {
        _colorWell1.color = [self autoColorAtPositon:8 / 25.0];
        _colorWell2.color = [self autoColorAtPositon:16 / 25.0];
        _colorWell3.color = [self autoColorAtPositon:24 / 25.0];
    }
    else {
        _colorWell1.color = [self autoColorAtPositon:1 / 3.0];
        _colorWell2.color = [self autoColorAtPositon:2 / 3.0];
        _colorWell3.color = _colorWell4.color;
    }
    [self savePalette:nil];
}

- (IBAction)disabledLCDColorCheckboxChanged:(id)sender
{
    [self updateEnabledControls];
}

- (IBAction)manualModeChanged:(id)sender
{
    [self updateEnabledControls];
}

- (IBAction)updateColor4:(id)sender
{
    if (!self.disableLCDColorCheckbox.state) {
        self.colorWell4.color = self.colorWell3.color;
    }
    [self savePalette:self];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSDictionary *themes = [defaults dictionaryForKey:@"GBThemes"];
    if (themes.count == 0) {
        [defaults setObject:@"Untitled Palette" forKey:@"GBCurrentTheme"];
        [self savePalette:nil];
        return 1;
    }
    return themes.count;
}

-(void)tableView:(NSTableView *)tableView setObjectValue:(id)object forTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row
{
    NSString *oldName = [self tableView:tableView objectValueForTableColumn:tableColumn row:row];
    if ([oldName isEqualToString:object]) {
        return;
    }
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSMutableDictionary *themes = [[defaults dictionaryForKey:@"GBThemes"] ?: @{} mutableCopy];
    NSString *newName = object;
    unsigned i = 2;
    if (!newName.length) {
        newName = @"Untitled Palette";
    }
    while (themes[newName]) {
        newName = [NSString stringWithFormat:@"%@ %d", object, i];
    }
    themes[newName] = themes[oldName];
    [themes removeObjectForKey:oldName];
    if ([oldName isEqualToString:[defaults stringForKey:@"GBCurrentTheme"]]) {
        [defaults setObject:newName forKey:@"GBCurrentTheme"];
    }
    [defaults setObject:themes forKey:@"GBThemes"];
    [tableView reloadData];
    [self awakeFromNib];
}

- (IBAction)deleteTheme:(id)sender
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSString *name = [defaults stringForKey:@"GBCurrentTheme"];
    NSMutableDictionary *themes = [[defaults dictionaryForKey:@"GBThemes"] ?: @{} mutableCopy];
    [themes removeObjectForKey:name];
    [defaults setObject:themes forKey:@"GBThemes"];
    [_themesList reloadData];
    [self awakeFromNib];
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
    NSString *name = [self tableView:nil objectValueForTableColumn:nil row:_themesList.selectedRow];
    [[NSUserDefaults standardUserDefaults] setObject:name forKey:@"GBCurrentTheme"];
    [self loadPalette];
    [[NSNotificationCenter defaultCenter] postNotificationName:@"GBColorPaletteChanged" object:nil];
}

- (void)tableViewSelectionIsChanging:(NSNotification *)notification
{
    [self tableViewSelectionDidChange:notification];
}

- (void)awakeFromNib
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSDictionary *themes = [defaults dictionaryForKey:@"GBThemes"];
    NSString *theme = [defaults stringForKey:@"GBCurrentTheme"];
    if (theme && themes[theme]) {
        unsigned index = [[themes.allKeys sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)] indexOfObject:theme];
        [_themesList selectRowIndexes:[NSIndexSet indexSetWithIndex:index] byExtendingSelection:false];
    }
    else {
        [_themesList selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:false];
    }
    [self tableViewSelectionDidChange:nil];
}

- (IBAction)addTheme:(id)sender
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSDictionary *themes = [defaults dictionaryForKey:@"GBThemes"];
    NSString *newName = @"Untitled Palette";
    unsigned i = 2;
    while (themes[newName]) {
        newName = [NSString stringWithFormat:@"Untitled Palette %d", i++];
    }
    [defaults setObject:newName forKey:@"GBCurrentTheme"];
    [self savePalette:sender];
    [_themesList reloadData];
    [self awakeFromNib];
}

- (id)tableView:(NSTableView *)tableView objectValueForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSDictionary *themes = [defaults dictionaryForKey:@"GBThemes"];
    return [themes.allKeys sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)][row];
}

- (void)loadPalette
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSDictionary *theme = [defaults dictionaryForKey:@"GBThemes"][[defaults stringForKey:@"GBCurrentTheme"]];
    NSArray *colors = theme[@"Colors"];
    if (colors.count == 5) {
        unsigned i = 0;
        for (NSNumber *color in colors) {
            uint32_t c = [color unsignedIntValue];
            self.colorWells[i++].color = [NSColor colorWithRed:(c & 0xFF) / 255.0
                                                       green:((c >> 8) & 0xFF) / 255.0
                                                        blue:((c >> 16) & 0xFF) / 255.0
                                                       alpha:1.0];
        }
    }
    _disableLCDColorCheckbox.state = [theme[@"DisabledLCDColor"] boolValue];
    _manualModeCheckbox.state = [theme[@"Manual"] boolValue];
    _brightnessSlider.doubleValue = [theme[@"BrightnessBias"] doubleValue] * 128 + 128;
    _hueSlider.doubleValue = [theme[@"HueBias"] doubleValue] * 360;
    _hueStrengthSlider.doubleValue = [theme[@"HueBiasStrength"] doubleValue] * 256;
    [self updateEnabledControls];
}

- (IBAction)savePalette:(id)sender
{
    NSDictionary *theme = @{
        @"Colors":
            @[@(_colorWell0.color.intValue),
              @(_colorWell1.color.intValue),
              @(_colorWell2.color.intValue),
              @(_colorWell3.color.intValue),
              @(_colorWell4.color.intValue)],
        @"DisabledLCDColor": _disableLCDColorCheckbox.state? @YES : @NO,
        @"Manual": _manualModeCheckbox.state? @YES : @NO,
        @"BrightnessBias": @((_brightnessSlider.doubleValue - 128) / 128.0),
        @"HueBias": @(_hueSlider.doubleValue / 360.0),
        @"HueBiasStrength": @(_hueStrengthSlider.doubleValue / 256.0)
    };
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSMutableDictionary *themes = [[defaults dictionaryForKey:@"GBThemes"] ?: @{} mutableCopy];
    themes[[defaults stringForKey:@"GBCurrentTheme"]] = theme;
    [defaults setObject:themes forKey:@"GBThemes"];
    [[NSNotificationCenter defaultCenter] postNotificationName:@"GBColorPaletteChanged" object:nil];
}

+ (const GB_palette_t *)userPalette
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    switch ([defaults integerForKey:@"GBColorPalette"]) {
        case 1: return &GB_PALETTE_DMG;
        case 2: return &GB_PALETTE_MGB;
        case 3: return &GB_PALETTE_GBL;
        default: return &GB_PALETTE_GREY;
        case -1: {
            static GB_palette_t customPalette;
            NSArray *colors = [defaults dictionaryForKey:@"GBThemes"][[defaults stringForKey:@"GBCurrentTheme"]][@"Colors"];
            if (colors.count == 5) {
                unsigned i = 0;
                for (NSNumber *color in colors) {
                    uint32_t c = [color unsignedIntValue];
                    customPalette.colors[i++] = (struct GB_color_s) {c, c >> 8, c >> 16};
                }
            }
            return &customPalette;
        }
    }
}

- (IBAction)export:(id)sender
{
    NSSavePanel *savePanel = [NSSavePanel savePanel];
    [savePanel setAllowedFileTypes:@[@"sbp"]];
    savePanel.nameFieldStringValue = [NSString stringWithFormat:@"%@.sbp", [[NSUserDefaults standardUserDefaults] stringForKey:@"GBCurrentTheme"]];
    if ([savePanel runModal] == NSModalResponseOK) {
        theme_t theme = {0,};
        theme.magic = MAGIC;
        theme.manual = _manualModeCheckbox.state;
        theme.disabled_lcd_color = _disableLCDColorCheckbox.state;
        unsigned i = 0;
        for (NSColorWell *well in self.colorWells) {
            theme.colors[i++] = well.color.gbColor;
        }
        theme.brightness_bias = (_brightnessSlider.doubleValue - 128) * (0x40000000 / 128);
        theme.hue_bias = round(_hueSlider.doubleValue * (0x80000000 / 360.0));
        theme.hue_bias_strength = (_hueStrengthSlider.doubleValue) * (0x80000000 / 256);
        size_t size = sizeof(theme);
        if (theme.manual) {
            size = theme.disabled_lcd_color? 5 + 5 * sizeof(theme.colors[0]) : 5 + 4 * sizeof(theme.colors[0]);
        }
        [[NSData dataWithBytes:&theme length:size] writeToURL:savePanel.URL atomically:false];
    }
}

- (IBAction)import:(id)sender
{
    NSOpenPanel *openPanel = [NSOpenPanel openPanel];
    [openPanel setAllowedFileTypes:@[@"sbp"]];
    if ([openPanel runModal] == NSModalResponseOK) {
        NSData *data = [NSData dataWithContentsOfURL:openPanel.URL];
        theme_t theme = {0,};
        memcpy(&theme, data.bytes, MIN(sizeof(theme), data.length));
        if (theme.magic != MAGIC) {
            NSBeep();
            return;
        }
        _manualModeCheckbox.state = theme.manual;
        _disableLCDColorCheckbox.state = theme.disabled_lcd_color;
        unsigned i = 0;
        for (NSColorWell *well in self.colorWells) {
            well.color = [NSColor colorWithRed:theme.colors[i].r / 255.0
                                         green:theme.colors[i].g / 255.0
                                          blue:theme.colors[i].b / 255.0
                                         alpha:1.0];
            i++;
        }
        if (!theme.disabled_lcd_color) {
            _colorWell4.color = _colorWell3.color;
        }
        _brightnessSlider.doubleValue = theme.brightness_bias / (0x40000000 / 128.0) + 128;
        _hueSlider.doubleValue = theme.hue_bias / (0x80000000 / 360.0);
        _hueStrengthSlider.doubleValue = theme.hue_bias_strength / (0x80000000 / 256.0);
        
        NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
        NSDictionary *themes = [defaults dictionaryForKey:@"GBThemes"];
        NSString *baseName = openPanel.URL.lastPathComponent.stringByDeletingPathExtension;
        NSString *newName = baseName;
        i = 2;
        while (themes[newName]) {
            newName = [NSString stringWithFormat:@"%@ %d", baseName, i++];
        }
        [defaults setObject:newName forKey:@"GBCurrentTheme"];
        [self savePalette:sender];
        [self.themesList reloadData];
        [self awakeFromNib];
    }
}

- (IBAction)done:(NSButton *)sender
{
    [sender.window.sheetParent endSheet:sender.window];
}

- (instancetype)init
{
    static id singleton = nil;
    if (singleton) return singleton;
    return (singleton = [super init]);
}
@end

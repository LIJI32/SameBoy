#import "GBPaletteEditor.h"
#import "GBColorWell.h"
#import "GBSlider.h"
#import "GBPalettePicker.h"

static double blend(double from, double to, double position)
{
    return from * (1 - position) + to * position;
}

@implementation GBPaletteEditor
{
    bool _displayingManual;
    NSString *_paletteName;
    bool _isCurrent;
    
    UITableViewCell *_nameCell;
    UITextField *_nameField;
    
    UITableViewCell *_colorsCell;
    UIColorWell *_colorWells[5];
    
    UITableViewCell *_disabledLCDCell;
    UISwitch *_disabledLCDSwitch;
    
    UITableViewCell *_manualCell;
    UISwitch *_manualSwitch;

    UITableViewCell *_brightnessCell;
    GBSlider *_brightnessSlider;
    
    UITableViewCell *_hueCell;
    GBSlider *_hueSlider;
    
    UITableViewCell *_hueStrengthCell;
    UISlider *_hueStrengthSlider;
}


- (instancetype)initForPalette:(NSString *)name
{
    self = [self initWithStyle:UITableViewStyleInsetGrouped];
    _paletteName = name;
    _isCurrent = [[[NSUserDefaults standardUserDefaults] stringForKey:@"GBCurrentTheme"] isEqual:name];
    return self;
}

- (UITableViewCell *)sliderCellWithSlider:(UISlider *)slider
{
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    CGRect rect = cell.contentView.bounds;
    rect.size.width -= 24;
    rect.size.height -= 24;
    rect.origin.x += 12;
    rect.origin.y += 12;
    slider.frame = rect;
    slider.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [cell.contentView addSubview:slider];
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    [slider addTarget:self action:@selector(updateAutoColors) forControlEvents:UIControlEventValueChanged];
    return cell;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSDictionary *theme = [defaults dictionaryForKey:@"GBThemes"][_paletteName];
    
    {
        _nameCell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
        CGRect frame = _nameCell.contentView.bounds;
        frame.size.width -= - 32;
        frame.origin.x += 16;
        _nameField = [[UITextField alloc] initWithFrame:frame];
        _nameField.font = _nameCell.textLabel.font;
        _nameField.text = _paletteName;
        _nameField.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        [_nameCell.contentView addSubview:_nameField];
        _nameCell.selectionStyle = UITableViewCellSelectionStyleNone;
    }
    
    {
        static const unsigned wellSize = 36;
        _colorsCell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
        CGRect frame = _nameCell.contentView.bounds;
        UIView *view = [[UIView alloc] initWithFrame:CGRectMake((frame.size.width - wellSize * 5) / 2,
                                                                (frame.size.height - wellSize) / 2,
                                                                wellSize * 5,
                                                                wellSize)];
        NSArray *titles = @[
            @"Darkest Color",
            @"Dark Midtone",
            @"Light Midtone",
            @"Lightest Color",
            @"Display Off Color",
        ];
        for (unsigned i = 0; i < 5; i++) {
            _colorWells[i] = [[GBColorWell alloc] initWithFrame:CGRectMake(i * wellSize, 0, wellSize, wellSize)];
            _colorWells[i].supportsAlpha = false;
            _colorWells[i].title = titles[i];
            _colorWells[i].selectedColor = [UIColor colorWithRed:(([theme[@"Colors"][i] unsignedIntValue] >> 0) & 0xFF) / 255.0
                                                           green:(([theme[@"Colors"][i] unsignedIntValue] >> 8) & 0xFF) / 255.0
                                                            blue:(([theme[@"Colors"][i] unsignedIntValue] >> 16) & 0xFF) / 255.0
                                                           alpha:1.0];
            [_colorWells[i] addTarget:self action:@selector(updateToggles) forControlEvents:UIControlEventValueChanged];

            [view addSubview:_colorWells[i]];
        }
        view.autoresizingMask =
            UIViewAutoresizingFlexibleLeftMargin |
            UIViewAutoresizingFlexibleRightMargin |
            UIViewAutoresizingFlexibleTopMargin |
            UIViewAutoresizingFlexibleBottomMargin;
        [_colorsCell.contentView addSubview:view];
        _colorsCell.selectionStyle = UITableViewCellSelectionStyleNone;
    }
    
    {
        _disabledLCDCell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
        _disabledLCDSwitch = [[UISwitch alloc] init];
        _disabledLCDCell.accessoryView = _disabledLCDSwitch;
        if ([theme[@"DisabledLCDColor"] boolValue]) {
            _disabledLCDSwitch.on = true;
        }
                
        [_disabledLCDSwitch addTarget:self action:@selector(updateToggles) forControlEvents:UIControlEventValueChanged];
        _disabledLCDCell.selectionStyle = UITableViewCellSelectionStyleNone;
        _disabledLCDCell.textLabel.text = @"Distinct Disabled LCD Color";
    }
    
    {
        _manualCell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
        _manualSwitch = [[UISwitch alloc] init];
        _manualCell.accessoryView = _manualSwitch;
        if ([theme[@"Manual"] boolValue]) {
            _manualSwitch.on = true;
        }
        
        [_manualSwitch addTarget:self action:@selector(updateToggles) forControlEvents:UIControlEventValueChanged];
        _manualCell.selectionStyle = UITableViewCellSelectionStyleNone;
        _manualCell.textLabel.text = @"Manual Mode";
    }
    
    {
        _brightnessSlider = [[GBSlider alloc] init];
        _brightnessSlider.minimumValue = -1;
        _brightnessSlider.maximumValue = 1;
        _brightnessSlider.continuous = true;
        _brightnessSlider.style = GBSliderStyleTicks;
        _brightnessSlider.value = [theme[@"BrightnessBias"] doubleValue];
        _brightnessCell = [self sliderCellWithSlider:_brightnessSlider];
    }
    
    {
        _hueSlider = [[GBSlider alloc] init];
        _hueSlider.minimumValue = 0;
        _hueSlider.maximumValue = 360;
        _hueSlider.continuous = true;
        _hueSlider.style = GBSliderStyleHue;
        _hueSlider.value = [theme[@"HueBias"] doubleValue] * 360;
        _hueCell = [self sliderCellWithSlider:_hueSlider];
    }
    
    {
        _hueStrengthSlider = [[UISlider alloc] init];
        _hueStrengthSlider.minimumValue = 0;
        _hueStrengthSlider.maximumValue = 1;
        _hueStrengthSlider.continuous = true;
        _hueStrengthSlider.value = [theme[@"HueBiasStrength"] doubleValue];
        _hueStrengthCell = [self sliderCellWithSlider:_hueStrengthSlider];
    }
    
    [self updateToggles];
    self.title = [NSString stringWithFormat:@"Editing %@", _paletteName];
}

- (UIColor *)autoColorAtPositon:(double)position
{

    UIColor *first = _colorWells[0].selectedColor;
    UIColor *second = _colorWells[4].selectedColor;
    
    CGFloat firstRed, firstGreen, firstBlue;
    CGFloat secondRed, secondGreen, secondBlue;
    [first getRed:&firstRed green:&firstGreen blue:&firstBlue alpha:NULL];
    [second getRed:&secondRed green:&secondGreen blue:&secondBlue alpha:NULL];

    
    double brightness = 1 / pow(4, _brightnessSlider.value);
    position = pow(position, brightness);
    UIColor *hue = _hueSlider.thumbTintColor;
    double bias = _hueStrengthSlider.value;
    
    CGFloat red, green, blue;
    [hue getRed:&red green:&green blue:&blue alpha:NULL];
    red = 1 / pow(4, (red * 2 - 1) * bias);
    green = 1 / pow(4, (green * 2 - 1) * bias);
    blue = 1 / pow(4, (blue * 2 - 1) * bias);
    UIColor *ret = [UIColor colorWithRed:blend(firstRed, secondRed, pow(position, red))
                                   green:blend(firstGreen, secondGreen, pow(position, green))
                                    blue:blend(firstBlue, secondBlue, pow(position, blue))
                                   alpha:1.0];
    return ret;
}

- (void)updateAutoColors
{
    if (_disabledLCDSwitch.on) {
        _colorWells[1].selectedColor = [self autoColorAtPositon:8 / 25.0];
        _colorWells[2].selectedColor = [self autoColorAtPositon:16 / 25.0];
        _colorWells[3].selectedColor = [self autoColorAtPositon:24 / 25.0];
    }
    else {
        _colorWells[1].selectedColor = [self autoColorAtPositon:1 / 3.0];
        _colorWells[2].selectedColor = [self autoColorAtPositon:2 / 3.0];
        _colorWells[3].selectedColor = _colorWells[4].selectedColor;
    }
    [self save];
}
- (void)updateToggles
{
    if (_manualSwitch.on) {
        _colorWells[1].enabled = true;
        _colorWells[2].enabled = true;
        _colorWells[3].enabled = true;
        if (!(_colorWells[4].enabled = _disabledLCDSwitch.on)) {
            _colorWells[4].selectedColor = _colorWells[3].selectedColor;
        }
        if (!_displayingManual) {
            [self.tableView deleteSections:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(2, 3)]
                          withRowAnimation:UITableViewRowAnimationFade];
        }
        [self save];
    }
    else {
        _colorWells[1].enabled = false;
        _colorWells[2].enabled = false;
        _colorWells[3].enabled = false;
        _colorWells[4].enabled = true;
        if (_displayingManual) {
            [self.tableView insertSections:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(2, 3)]
                          withRowAnimation:UITableViewRowAnimationFade];
        }
        [self updateAutoColors];
    }
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
    _displayingManual = _manualSwitch.on;
    return  _displayingManual? 2 : 5;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    switch (section) {
        case 0: return 1; // Name
        case 1: return 3; // Colors
        case 2: return 1; // Brightness Bias
        case 3: return 2; // Hue Bias
        default: return 0;
    };
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section
{
    switch (section) {
        case 0: return @"Palette Name";
        case 2: return @"Brightness Bias";
        case 3: return @"Hue Bias";
    }
    return nil;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    switch (indexPath.section) {
        case 0: return _nameCell;
        case 1: {
            switch (indexPath.row) {
                case 0: return _colorsCell;
                case 1: return _disabledLCDCell;
                case 2: return _manualCell;
            }
            return nil;
        }
        case 2: return _brightnessCell;
        case 3: return indexPath.row == 0? _hueCell : _hueStrengthCell;
    }
    return [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
}

- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (indexPath.section == 2) return 63;
    return [super tableView:tableView heightForRowAtIndexPath:indexPath];
}

- (NSIndexPath *)tableView:(UITableView *)tableView willSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
    return nil;
}

- (NSNumber *)encodeWell:(unsigned)index
{
    CGFloat r, g, b;
    [_colorWells[index].selectedColor getRed:&r green:&g blue:&b alpha:NULL];
    return @((((unsigned)round(r * 255) <<  0) |
              ((unsigned)round(g * 255) <<  8) |
              ((unsigned)round(b * 255) << 16) |
              0xFF000000));
}

- (void)save
{
    NSMutableDictionary *themes = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"GBThemes"].mutableCopy;
    [themes removeObjectForKey:_paletteName];
    if (![_paletteName isEqual:_nameField.text]) {
        _paletteName = [GBPalettePicker makeUnique:_nameField.text];
    }
    
    themes[_paletteName] = @{
        @"BrightnessBias": @(_brightnessSlider.value),
        @"Colors": @[[self encodeWell:0],
                     [self encodeWell:1],
                     [self encodeWell:2],
                     [self encodeWell:3],
                     [self encodeWell:4]],
        @"DisabledLCDColor": _disabledLCDSwitch.on? @YES : @NO,
        @"HueBias": @(_hueSlider.value / 360.0),
        @"HueBiasStrength": @(_hueStrengthSlider.value),
        @"Manual": _manualSwitch.on? @YES : @NO,
    };
    
    [[NSUserDefaults standardUserDefaults] setObject:themes forKey:@"GBThemes"];
    if (_isCurrent) {
        [[NSUserDefaults standardUserDefaults] setObject:@"" forKey:@"GBCurrentTheme"]; // Force a reload
        [[NSUserDefaults standardUserDefaults] setObject:_paletteName forKey:@"GBCurrentTheme"];
    }
}
- (void)viewWillDisappear:(BOOL)animated
{
    [super viewWillDisappear:animated];
    [self save];
}

@end

#import "GBPalettePicker.h"
#import "GBPaletteEditor.h"
#import <CoreServices/CoreServices.h>

/* TODO: Unify with Cocoa? */
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


@interface GBPalettePicker () <UIDocumentPickerDelegate>
@end

@implementation GBPalettePicker
{
    NSArray <NSString *>* _cacheNames;
    NSIndexPath *_renamingPath;
    NSMutableSet *_tempFiles;
}

+ (NSString *)makeUnique:(NSString *)name
{
    NSArray *builtins = @[
        @"Greyscale",
        @"Lime (Game Boy)",
        @"Olive (Pocket)",
        @"Teal (Light)",
    ];
    
    NSDictionary *dict = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"GBThemes"];
    if (dict[name] || [builtins containsObject:name]) {
        unsigned i = 2;
        while (true) {
            NSString *attempt = [NSString stringWithFormat:@"%@ %u", name, i];
            if (!dict[attempt] && ![builtins containsObject:attempt]) {
                return attempt;
            }
            i++;
        }
    }
    return name;
}

+ (const GB_palette_t *)paletteForTheme:(NSString *)theme
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    if ([theme isEqualToString:@"Greyscale"]) {
        return &GB_PALETTE_GREY;
    }
    if ([theme isEqualToString:@"Lime (Game Boy)"]) {
        return &GB_PALETTE_DMG;
    }
    if ([theme isEqualToString:@"Olive (Pocket)"]) {
        return &GB_PALETTE_MGB;
    }
    if ([theme isEqualToString:@"Teal (Light)"]) {
        return &GB_PALETTE_GBL;
    }
    static GB_palette_t customPalette;
    NSArray *colors = [defaults dictionaryForKey:@"GBThemes"][theme][@"Colors"];
    if (colors.count != 5) return &GB_PALETTE_DMG;
    unsigned i = 0;
    for (NSNumber *color in colors) {
        uint32_t c = [color unsignedIntValue];
        customPalette.colors[i++] = (struct GB_color_s) {c, c >> 8, c >> 16};
    }
    return &customPalette;
}

+ (UIColor *)colorFromGBColor:(const struct GB_color_s *)color
{
    return [UIColor colorWithRed:color->r / 255.0
                           green:color->g / 255.0
                            blue:color->b / 255.0
                           alpha:1.0];
}

+ (UIImage *)previewImageForTheme:(NSString *)theme
{
    const GB_palette_t *palette = [self paletteForTheme:theme];
    UIGraphicsBeginImageContextWithOptions((CGSize){29, 29}, false, [UIScreen mainScreen].scale);
    UIBezierPath *path = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(0, 0, 29, 29) cornerRadius:7];
    [[self colorFromGBColor:&palette->colors[4]] set];
    [path fill];
    
    path = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(4, 4, 9, 9) cornerRadius:2];
    [[self colorFromGBColor:&palette->colors[0]] set];
    [path fill];
    
    path = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(16, 4, 9, 9) cornerRadius:2];
    [[self colorFromGBColor:&palette->colors[1]] set];
    [path fill];
    
    path = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(4, 16, 9, 9) cornerRadius:2];
    [[self colorFromGBColor:&palette->colors[2]] set];
    [path fill];
    
    path = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(16, 16, 9, 9) cornerRadius:2];
    [[self colorFromGBColor:&palette->colors[3]] set];
    [path fill];
    
    UIImage *ret = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    
    return ret;
}

- (instancetype)initWithStyle:(UITableViewStyle)style
{
    self = [super initWithStyle:style];
    self.navigationItem.rightBarButtonItem = self.editButtonItem;
    _tempFiles = [NSMutableSet set];
    self.tableView.allowsSelectionDuringEditing = true;
    return self;
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
    return 3;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    if (section == 0) return 4;
    if (section == 2) return 3;
    _cacheNames = [[[NSUserDefaults standardUserDefaults] dictionaryForKey:@"GBThemes"].allKeys sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)];
    return _cacheNames.count;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil];
    
    NSString *name = nil;
    if (indexPath.section == 2) {
        switch (indexPath.row) {
            case 0:
                cell.textLabel.text = @"New Palette";
                break;
            case 1:
                cell.textLabel.text = @"Import Palette";
                break;
            case 2:
                cell.textLabel.text = @"Restore Defaults";
                cell.textLabel.textColor = [UIColor systemRedColor];
                break;
        }
        return cell;
    }
    else if (indexPath.section == 0) {
        name = @[
            @"Greyscale",
            @"Lime (Game Boy)",
            @"Olive (Pocket)",
            @"Teal (Light)",
        ][indexPath.row];
    }
    else {
        name = _cacheNames[indexPath.row];
    }
    
    cell.textLabel.text = name;
    if ([[[NSUserDefaults standardUserDefaults] stringForKey:@"GBCurrentTheme"] isEqual:name]) {
        cell.accessoryType = UITableViewCellAccessoryCheckmark;
    }

    cell.imageView.image = [self.class previewImageForTheme:name];
    return cell;

}


- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentAtURL:(NSURL *)url
{
    [url startAccessingSecurityScopedResource];
    if ([self.class importPalette:url.path]) {
        [self.tableView reloadData];
    }
    else {
        UIAlertController *alertController = [UIAlertController alertControllerWithTitle:@"Import Failed"
                                                                                 message:@"The imported palette file is invalid."
                                                                          preferredStyle:UIAlertControllerStyleAlert];
        [alertController addAction:[UIAlertAction actionWithTitle:@"Close" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:alertController animated:true completion:nil];
        return;
    }
    [url stopAccessingSecurityScopedResource];
}

- (NSIndexPath *)tableView:(UITableView *)tableView willSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (self.editing) {
        if (indexPath.section != 1) return nil;
        if (@available(iOS 14.0, *)) {
            [self.navigationController pushViewController:[[GBPaletteEditor alloc] initForPalette:[self.tableView cellForRowAtIndexPath:indexPath].textLabel.text]
                                                 animated:true];
        }
        return nil;
    }
    if (indexPath.section == 2) {
        switch (indexPath.row) {
            case 0: {
                if (@available(iOS 14.0, *)) {
                    NSString *name = [self.class makeUnique:@"Untitled Palette"];
                    NSMutableDictionary *dict = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"GBThemes"].mutableCopy;
                    srandom(time(NULL));
                    dict[name] = @{
                        @"BrightnessBias": @((random() & 0xFFF) / (double)0xFFF * 2 - 1),
                        @"Colors": @[@((random() & 0x3f3f3f) | 0xFF000000), @0, @0, @0, @((random() | 0xffc0c0c0))],
                        @"DisabledLCDColor": @YES,
                        @"HueBias": @((random() & 0xFFF) / (double)0xFFF),
                        @"HueBiasStrength": @((random() & 0xFFF) / (double)0xFFF),
                        @"Manual": @NO,
                    };
                    [[NSUserDefaults standardUserDefaults] setObject:dict forKey:@"GBThemes"];
                    [self.navigationController pushViewController:[[GBPaletteEditor alloc] initForPalette:name]
                                                         animated:true];
                }
                else {
                    UIAlertController *alertController = [UIAlertController alertControllerWithTitle:@"Palette Editor Unavailable"
                                                                                             message:@"The palette editor requires iOS 14 or newer."
                                                                                      preferredStyle:UIAlertControllerStyleAlert];
                    [alertController addAction:[UIAlertAction actionWithTitle:@"Close" style:UIAlertActionStyleDefault handler:nil]];
                    [self presentViewController:alertController animated:true completion:nil];
                }
                break;
            }
            case 1: {
                [self setEditing:false animated:true];
                NSString *sbpUTI = (__bridge_transfer NSString *)UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, (__bridge CFStringRef)@"sbp", NULL);
                
                
                UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[sbpUTI]
                                                                                                                inMode:UIDocumentPickerModeImport];
                if (@available(iOS 13.0, *)) {
                    picker.shouldShowFileExtensions = true;
                }
                picker.delegate = self;
                [self presentViewController:picker animated:true completion:nil];
                break;
            }
            case 2: {
                UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Restore default palettes?"
                                                                               message: @"The defaults palettes will be restored, changes will be reverted, and created or imported palettes will be deleted. This change cannot be undone."
                                                                        preferredStyle:UIAlertControllerStyleAlert];
                [alert addAction:[UIAlertAction actionWithTitle:@"Restore"
                                                          style:UIAlertActionStyleDestructive
                                                        handler:^(UIAlertAction *action) {
                    [[NSUserDefaults standardUserDefaults] removeObjectForKey:@"GBCurrentTheme"];
                    [[NSUserDefaults standardUserDefaults] removeObjectForKey:@"GBThemes"];
                    [self.tableView reloadData];
                }]];
                [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                                          style:UIAlertActionStyleCancel
                                                        handler:nil]];
                [self presentViewController:alert animated:true completion:nil];
                break;
            }
        }
        return nil;
    }
    [[NSUserDefaults standardUserDefaults] setObject:[self.tableView cellForRowAtIndexPath:indexPath].textLabel.text
                                              forKey:@"GBCurrentTheme"];
    [self.tableView reloadData];
    return nil;
}


- (NSString *)title
{
    return @"Monochrome Palette";
}

- (void)renameRow:(NSIndexPath *)indexPath
{
    if (indexPath.section != 1) return;
    
    UITableViewCell *cell = [self.tableView cellForRowAtIndexPath:indexPath];
    CGRect frame = cell.textLabel.frame;
    frame.size.width = cell.textLabel.superview.frame.size.width - 8 - frame.origin.x;
    UITextField *field = [[UITextField alloc] initWithFrame:frame];
    field.font = cell.textLabel.font;
    field.text = cell.textLabel.text;
    cell.textLabel.textColor = [UIColor clearColor];
    [[cell.textLabel superview] addSubview:field];
    [field becomeFirstResponder];
    [field selectAll:nil];
    _renamingPath = indexPath;
    [field addTarget:self action:@selector(doneRename:) forControlEvents:UIControlEventEditingDidEnd | UIControlEventEditingDidEndOnExit];
}

- (void)doneRename:(UITextField *)sender
{
    if (!_renamingPath) return;
    NSString *newName = sender.text;
    NSString *oldName = [self.tableView cellForRowAtIndexPath:_renamingPath].textLabel.text;
    
    _renamingPath = nil;
    if ([newName isEqualToString:oldName]) {
        [self.tableView reloadData];
        return;
    }
    
    NSMutableDictionary *dict = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"GBThemes"].mutableCopy;
    newName = [self.class makeUnique:newName];
    
    dict[newName] = dict[oldName];
    [dict removeObjectForKey:oldName];
    [[NSUserDefaults standardUserDefaults] setObject:dict forKey:@"GBThemes"];
    if ([[[NSUserDefaults standardUserDefaults] stringForKey:@"GBCurrentTheme"] isEqual:oldName]) {
        [[NSUserDefaults standardUserDefaults] setObject:newName forKey:@"GBCurrentTheme"];
    }
    [self.tableView reloadData];
    _renamingPath = nil;
}


- (void)tableView:(UITableView *)tableView commitEditingStyle:(UITableViewCellEditingStyle)editingStyle forRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (indexPath.section != 1) return;
    
    if (editingStyle != UITableViewCellEditingStyleDelete) return;
    NSString *rom = [self.tableView cellForRowAtIndexPath:indexPath].textLabel.text;
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Delete “%@”?", rom]
                                                                   message: @"This change cannot be undone."
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Delete"
                                              style:UIAlertActionStyleDestructive
                                            handler:^(UIAlertAction *action) {
        NSMutableDictionary *dict = [[NSUserDefaults standardUserDefaults] dictionaryForKey:@"GBThemes"].mutableCopy;
        NSString *name = _cacheNames[indexPath.row];
        [dict removeObjectForKey:name];
        [[NSUserDefaults standardUserDefaults] setObject:dict forKey:@"GBThemes"];
        [self.tableView deleteRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationAutomatic];

        if ([[[NSUserDefaults standardUserDefaults] stringForKey:@"GBCurrentTheme"] isEqual:name]) {
            [[NSUserDefaults standardUserDefaults] removeObjectForKey:@"GBCurrentTheme"];
            [self.tableView reloadRowsAtIndexPaths:@[[NSIndexPath indexPathForRow:1 inSection:0]] withRowAnimation:UITableViewRowAnimationFade];
        }
    }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                              style:UIAlertActionStyleCancel
                                            handler:nil]];
    [self presentViewController:alert animated:true completion:nil];
}

- (BOOL)tableView:(UITableView *)tableView canEditRowAtIndexPath:(NSIndexPath *)indexPath
{
    return indexPath.section == 1;
}

- (NSString *)exportTheme:(NSString *)name
{
    theme_t theme = {0,};
    theme.magic = MAGIC;
    
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSDictionary *themeDict = [defaults dictionaryForKey:@"GBThemes"][name];
    NSArray *colors = themeDict[@"Colors"];
    if (colors.count <= 5) {
        unsigned i = 0;
        for (NSNumber *color in colors) {
            uint32_t c = [color unsignedIntValue];
            theme.colors[i++] = (struct GB_color_s){
                (c & 0xFF),
                ((c >> 8) & 0xFF),
                ((c >> 16) & 0xFF)};
        }
    }
    
    theme.manual = [themeDict[@"Manual"] boolValue];
    theme.disabled_lcd_color = [themeDict[@"DisabledLCDColor"] boolValue];

    theme.brightness_bias = ([themeDict[@"BrightnessBias"] doubleValue] * 0x40000000);
    theme.hue_bias = round([themeDict[@"HueBias"] doubleValue] * 0x80000000);
    theme.hue_bias_strength = round([themeDict[@"HueBiasStrength"] doubleValue] * 0x80000000);
    
    size_t size = sizeof(theme);
    if (theme.manual) {
        size = theme.disabled_lcd_color? 5 + 5 * sizeof(theme.colors[0]) : 5 + 4 * sizeof(theme.colors[0]);
    }
    
    NSString *path = [[NSTemporaryDirectory() stringByAppendingPathComponent:name] stringByAppendingPathExtension:@"sbp"];
    [[NSData dataWithBytes:&theme length:size] writeToFile:path atomically:false];
    [_tempFiles addObject:path];
    return path;
}

+ (bool)importPalette:(NSString *)path
{
    NSData *data = [NSData dataWithContentsOfFile:path];
    theme_t theme = {0,};
    memcpy(&theme, data.bytes, MIN(sizeof(theme), data.length));
    if (theme.magic != MAGIC) {
        return false;
    }
    
    NSMutableDictionary *themeDict = [NSMutableDictionary dictionary];
    themeDict[@"Manual"] = theme.manual? @YES : @NO;
    themeDict[@"DisabledLCDColor"] = theme.disabled_lcd_color? @YES : @NO;
    
#define COLOR_TO_INT(color) ((color.r << 0) | (color.g << 8) | (color.b << 16) | 0xFF000000)
    themeDict[@"Colors"] = @[
        @(COLOR_TO_INT(theme.colors[0])),
        @(COLOR_TO_INT(theme.colors[1])),
        @(COLOR_TO_INT(theme.colors[2])),
        @(COLOR_TO_INT(theme.colors[3])),
        @(COLOR_TO_INT(theme.colors[theme.disabled_lcd_color? 4 : 3])),
    ];

    
    themeDict[@"BrightnessBias"] = @(theme.brightness_bias / (double)0x40000000);
    themeDict[@"HueBias"] = @(theme.hue_bias / (double)0x80000000);
    themeDict[@"HueBiasStrength"] = @(theme.hue_bias_strength / (double)0x80000000);
        
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSMutableDictionary *themes = [defaults dictionaryForKey:@"GBThemes"].mutableCopy;
    NSString *baseName = path.lastPathComponent.stringByDeletingPathExtension;
    NSString *newName = [self.class makeUnique:baseName];
    themes[newName] = themeDict;
    [defaults setObject:themes forKey:@"GBThemes"];
    [defaults setObject:newName forKey:@"GBCurrentTheme"];
    return true;
}

- (UIContextMenuConfiguration *)tableView:(UITableView *)tableView
contextMenuConfigurationForRowAtIndexPath:(NSIndexPath *)indexPath
                                    point:(CGPoint)point API_AVAILABLE(ios(13.0))
{
    if (indexPath.section != 1) return nil;
    
    return [UIContextMenuConfiguration configurationWithIdentifier:nil
                                                   previewProvider:nil
                                                    actionProvider:^UIMenu *(NSArray<UIMenuElement *> *suggestedActions) {
        UIAction *deleteAction = [UIAction actionWithTitle:@"Delete"
                                                     image:[UIImage systemImageNamed:@"trash"]
                                                identifier:nil
                                                   handler:^(UIAction *action) {
            [self tableView:tableView
         commitEditingStyle:UITableViewCellEditingStyleDelete
          forRowAtIndexPath:indexPath];
        }];
        
        deleteAction.attributes = UIMenuElementAttributesDestructive;
        return [UIMenu menuWithTitle:nil children:@[
            [UIAction actionWithTitle:@"Edit"
                                image:[UIImage systemImageNamed:@"paintbrush"]
                           identifier:nil
                              handler:^(__kindof UIAction *action) {
                if (@available(iOS 14.0, *)) {
                    [self.navigationController pushViewController:[[GBPaletteEditor alloc] initForPalette:[self.tableView cellForRowAtIndexPath:indexPath].textLabel.text]
                                                         animated:true];
                }
                else {
                    UIAlertController *alertController = [UIAlertController alertControllerWithTitle:@"Palette Editor Unavailable"
                                                                                             message:@"The palette editor requires iOS 14 or newer."
                                                                                      preferredStyle:UIAlertControllerStyleAlert];
                    [alertController addAction:[UIAlertAction actionWithTitle:@"Close" style:UIAlertActionStyleDefault handler:nil]];
                    [self presentViewController:alertController animated:true completion:nil];
                    return;
                }
            }],
            [UIAction actionWithTitle:@"Share"
                                image:[UIImage systemImageNamed:@"square.and.arrow.up"]
                           identifier:nil
                              handler:^(__kindof UIAction *action) {
                [self setEditing:false animated:true];
                UITableViewCell *cell = [self.tableView cellForRowAtIndexPath:indexPath];
                NSString *file = [self exportTheme:cell.textLabel.text];
                NSURL *url = [NSURL fileURLWithPath:file];
                UIActivityViewController *controller = [[UIActivityViewController alloc] initWithActivityItems:@[url]
                                                                                         applicationActivities:nil];
                
                if ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad) {
                    controller.popoverPresentationController.sourceView = cell.contentView;
                }
                
                [self presentViewController:controller
                                   animated:true
                                 completion:nil];
            }],
            [UIAction actionWithTitle:@"Rename"
                                image:[UIImage systemImageNamed:@"pencil"]
                           identifier:nil
                              handler:^(__kindof UIAction *action) {
                [self renameRow:indexPath];
            }],
            deleteAction,
        ]];
    }];
}

- (void)dealloc
{
    for (NSString *file in _tempFiles) {
        [[NSFileManager defaultManager] removeItemAtPath:file error:nil];
    }
}

- (void)viewWillAppear:(BOOL)animated
{
    [self.tableView reloadData];
}

@end

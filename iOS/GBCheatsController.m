#import "GBCheatsController.h"
#import "GBROMManager.h"
#import "UIToolbar+disableCompact.h"
#import <CoreServices/CoreServices.h>
#import <objc/runtime.h>

@interface GBCheatsController()<UIDocumentPickerDelegate>
@end

@implementation GBCheatsController
{
    GB_gameboy_t *_gb;
    NSIndexPath *_renamingPath;
    __weak UITextField *_editingField;
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
    return 2;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    if (section == 0) return 1;
    size_t count;
    GB_get_cheats(_gb, &count);
    self.toolbarItems.firstObject.enabled = count;
    return count;
}


- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil];

    UISwitch *button = [[UISwitch alloc] init];
    cell.accessoryView = button;
    const GB_cheat_t *cheat = NULL;
    if (indexPath.section == 0) {
        button.on = GB_cheats_enabled(_gb);
        cell.textLabel.text = @"Enable Cheats";
    }
    else {
        cheat = GB_get_cheats(_gb, NULL)[indexPath.row];
        button.on = cheat->enabled;
        cell.textLabel.text = @(cheat->description) ?: @"Unnamed Cheat";
        button.enabled = GB_cheats_enabled(_gb);
    }
    
    id block = ^(){
        if (!cheat) {
            GB_set_cheats_enabled(_gb, button.on);
            [self.tableView reloadSections:[NSIndexSet indexSetWithIndex:1] withRowAnimation:UITableViewRowAnimationNone];
        }
        else {
            GB_update_cheat(_gb, cheat, cheat->description,
                            cheat->address, cheat->bank,
                            cheat->value, cheat->old_value, cheat->use_old_value,
                            button.on);
        }
    };
    objc_setAssociatedObject(cell, "RetainedBlock", block, OBJC_ASSOCIATION_RETAIN);
    
    [button addTarget:block action:@selector(invoke) forControlEvents:UIControlEventValueChanged];
    cell.selectionStyle = UITableViewCellSelectionStyleNone;

    return cell;
}

- (void)addCheat
{
    [self setEditing:false animated:true];
    UIAlertController *alertController = [UIAlertController alertControllerWithTitle: @"Add Cheat"
                                                                              message: @"Add a GameShark or Game Genie cheat code"
                                                                       preferredStyle:UIAlertControllerStyleAlert];
    [alertController addTextFieldWithConfigurationHandler:^(UITextField *textField) {
        textField.placeholder = @"Description";
        textField.clearButtonMode = UITextFieldViewModeWhileEditing;
    }];
    [alertController addTextFieldWithConfigurationHandler:^(UITextField *textField) {
        textField.placeholder = @"Cheat Code";
        textField.clearButtonMode = UITextFieldViewModeWhileEditing;
        textField.keyboardType = UIKeyboardTypeASCIICapable;
    }];
    [alertController addAction:[UIAlertAction actionWithTitle:@"Add" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        size_t index = [self tableView:self.tableView numberOfRowsInSection:1];
        NSString *name = alertController.textFields[0].text;
        if (GB_import_cheat(_gb, alertController.textFields[1].text.UTF8String, name.length? name.UTF8String : "Unnamed Cheat", true)) {
            [self.tableView insertRowsAtIndexPaths:@[[NSIndexPath indexPathForRow:index inSection:1]] withRowAnimation:UITableViewRowAnimationAutomatic];
        }
        else {
            alertController.title = @"Invalid cheat code entered";
            [self presentViewController:alertController animated:YES completion:nil];
        }
    }]];
    [alertController addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:alertController animated:YES completion:nil];
}

+ (UIBarButtonItem *)buttonWithLabel:(NSString *)label
                       imageWithName:(NSString *)imageName
                              target:(id)target
                              action:(SEL)action
{
    if (@available(iOS 13.0, *)) {
        UIImage *image = [UIImage systemImageNamed:imageName
                                 withConfiguration:[UIImageSymbolConfiguration configurationWithScale:UIImageSymbolScaleLarge]];
        UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
        [button setImage:image forState:UIControlStateNormal];
        button.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
        [button addTarget:target action:action forControlEvents:UIControlEventTouchUpInside];
        if (label) {
            [button setTitle:label forState:UIControlStateNormal];
            [button setTitleColor:button.tintColor forState:UIControlStateNormal];
            button.titleEdgeInsets = UIEdgeInsetsMake(0, 4, 0, 0);
            button.contentEdgeInsets = UIEdgeInsetsMake(0, 12, 0, 0);
        }
        [button sizeToFit];
        CGRect frame = button.frame;
        frame.size.width = ceil(frame.size.width + (label? 4 : 0));
        frame.size.height = 28;
        button.frame = frame;
        return [[UIBarButtonItem alloc] initWithCustomView:button];
    }
    return [[UIBarButtonItem alloc] initWithTitle:label style:UIBarButtonItemStylePlain target:target action:action];
}

- (void)importCheats
{
    [self setEditing:false animated:true];
    NSString *chtUTI = (__bridge_transfer NSString *)UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, (__bridge CFStringRef)@"cht", NULL);
    
        
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[chtUTI]
                                                                                                    inMode:UIDocumentPickerModeImport];
    if (@available(iOS 13.0, *)) {
        picker.shouldShowFileExtensions = true;
    }
    picker.delegate = self;
    [self presentViewController:picker animated:true completion:nil];

    return;

}

- (void)exportCheats
{
    [self setEditing:false animated:true];
    NSString *cheatsFile = [[GBROMManager sharedManager] cheatsFile];
    GB_save_cheats(_gb, cheatsFile.UTF8String);
    NSURL *url = [NSURL fileURLWithPath:cheatsFile];
    UIActivityViewController *controller = [[UIActivityViewController alloc] initWithActivityItems:@[url]
                                                                             applicationActivities:nil];
    
    if ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad) {
        controller.popoverPresentationController.barButtonItem = self.toolbarItems.firstObject;
    }

    [self presentViewController:controller
                       animated:true
                     completion:nil];

}

- (instancetype)initWithGameBoy:(GB_gameboy_t *)gb
{
    UITableViewStyle style = UITableViewStyleGrouped;
    if (@available(iOS 13.0, *)) {
        style = UITableViewStyleInsetGrouped;
    }
    self = [super initWithStyle:style];
    self.tableView.allowsSelectionDuringEditing = true;
    self.navigationItem.rightBarButtonItem = self.editButtonItem;
    
    bool hasSFSymbols = false;
    if (@available(iOS 13.0, *)) {
        hasSFSymbols = true;
    }

    self.toolbarItems = @[
        hasSFSymbols?
            [self.class buttonWithLabel:nil
                          imageWithName:@"square.and.arrow.up"
                                 target:self
                                 action:@selector(exportCheats)] :
            [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemAction
                                                          target:self
                                                          action:@selector(exportCheats)],
        [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace
                                                      target:nil
                                                      action:NULL],
        [self.class buttonWithLabel:@"Import"
                      imageWithName:@"square.and.arrow.down"
                             target:self
                             action:@selector(importCheats)],
        [self.class buttonWithLabel:@"Add"
                      imageWithName:@"plus"
                             target:self
                             action:@selector(addCheat)],
        
    ];
    
    _gb = gb;
    return self;
}

- (NSString *)title
{
    return @"Cheats";
}

- (BOOL)tableView:(UITableView *)tableView canEditRowAtIndexPath:(NSIndexPath *)indexPath
{
    return indexPath.section == 1;
}

- (void)tableView:(UITableView *)tableView commitEditingStyle:(UITableViewCellEditingStyle)editingStyle forRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (indexPath.section != 1) return;
    if (editingStyle != UITableViewCellEditingStyleDelete) return;
    
    const GB_cheat_t *cheat = GB_get_cheats(_gb, NULL)[indexPath.row];
    GB_remove_cheat(_gb, cheat);
    [self.tableView deleteRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationAutomatic];
}

- (void)viewWillAppear:(BOOL)animated
{
    [self.navigationController setToolbarHidden:false animated:false];
    self.navigationController.toolbar.disableCompactLayout = true;
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentAtURL:(NSURL *)url
{
    [url startAccessingSecurityScopedResource];
    NSString *tempDir = NSTemporaryDirectory();
    NSString *newPath = [tempDir stringByAppendingPathComponent:@"import.cht"];
    [[NSFileManager defaultManager] copyItemAtPath:url.path toPath:newPath error:nil];
    [url stopAccessingSecurityScopedResource];
    unsigned count = [self tableView:self.tableView numberOfRowsInSection:1];
    
    void (^load)(bool) = ^(bool replace) {
        if (GB_load_cheats(_gb, newPath.UTF8String, replace)) {
            [[NSFileManager defaultManager] removeItemAtPath:newPath error:nil];
            UIAlertController *alertController = [UIAlertController alertControllerWithTitle:@"Import Failed"
                                                                                     message:@"The imported cheats file is invalid."
                                                                              preferredStyle:UIAlertControllerStyleAlert];
            [alertController addAction:[UIAlertAction actionWithTitle:@"Close" style:UIAlertActionStyleDefault handler:nil]];
            [self presentViewController:alertController animated:true completion:nil];
            return;
        }
        
        [[NSFileManager defaultManager] removeItemAtPath:newPath error:nil];
        unsigned newCount = [self tableView:self.tableView numberOfRowsInSection:1];
        if (!replace) {
            NSMutableArray *paths = [NSMutableArray arrayWithCapacity:newCount - count];
            for (unsigned i = count; i < newCount; i++) {
                [paths addObject:[NSIndexPath indexPathForRow:i inSection:1]];
            }
            if (paths.count) {
                [self.tableView insertRowsAtIndexPaths:paths withRowAnimation:UITableViewRowAnimationAutomatic];
            }
        }
        else {
            NSMutableArray *paths = [NSMutableArray arrayWithCapacity:abs((signed)newCount - (signed)count)];
            for (unsigned i = MIN(newCount, count); i < count || i < newCount; i++) {
                [paths addObject:[NSIndexPath indexPathForRow:i inSection:1]];
            }
            if (newCount > count) {
                [self.tableView insertRowsAtIndexPaths:paths withRowAnimation:UITableViewRowAnimationAutomatic];
            }
            else {
                [self.tableView deleteRowsAtIndexPaths:paths withRowAnimation:UITableViewRowAnimationAutomatic];
            }
            
            paths = [NSMutableArray arrayWithCapacity:MIN(newCount, count)];
            for (unsigned i = 0; i < count && i < newCount; i++) {
                [paths addObject:[NSIndexPath indexPathForRow:i inSection:1]];
            }
            [self.tableView reloadRowsAtIndexPaths:paths withRowAnimation:UITableViewRowAnimationRight];
        }
    };
    
    if (count) {
        UIAlertController *alertController = [UIAlertController alertControllerWithTitle:@"Replace Existing Cheats?"
                                                                                 message:@"Append the newly imported cheats or replace the existing ones?"
                                                                          preferredStyle:UIAlertControllerStyleAlert];
        [alertController addAction:[UIAlertAction actionWithTitle:@"Append" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
            load(false);
        }]];
        [alertController addAction:[UIAlertAction actionWithTitle:@"Replace" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *action) {
            load(true);
        }]];
        [self presentViewController:alertController animated:true completion:nil];
    }
    else {
        load(true);
    }
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (indexPath.section == 0) return;
    if (!self.editing) return;
    
    UITableViewCell *cell = [self.tableView cellForRowAtIndexPath:indexPath];
    CGRect frame = cell.textLabel.frame;
    frame.size.width = cell.textLabel.superview.frame.size.width - 8 - frame.origin.x;
    UITextField *field = [[UITextField alloc] initWithFrame:frame];
    field.font = cell.textLabel.font;
    field.text = cell.textLabel.text;
    cell.textLabel.text = @"";
    [[cell.textLabel superview] addSubview:field];
    [field becomeFirstResponder];
    [field selectAll:nil];
    _renamingPath = indexPath;
    [field addTarget:self action:@selector(doneRename:) forControlEvents:UIControlEventEditingDidEnd | UIControlEventEditingDidEndOnExit];
    _editingField = field;
}

- (void)doneRename:(UITextField *)sender
{
    if (!_renamingPath) return;
    const GB_cheat_t *cheat = GB_get_cheats(_gb, NULL)[_renamingPath.row];
    GB_update_cheat(_gb, cheat, sender.text.length? sender.text.UTF8String : "Unnamed Cheat",
                    cheat->address, cheat->bank,
                    cheat->value, cheat->old_value, cheat->use_old_value,
                    cheat->enabled);
    [self.tableView reloadRowsAtIndexPaths:@[_renamingPath] withRowAnimation:UITableViewRowAnimationNone];
    _renamingPath = nil;
}

- (void)setEditing:(BOOL)editing animated:(BOOL)animated
{
    [super setEditing:editing animated:animated];
    if (!editing && _editingField) {
        [self doneRename:_editingField];
    }
}

- (void)viewWillDisappear:(BOOL)animated
{
    [super viewWillDisappear:animated];
    NSString *cheatsFile = [[GBROMManager sharedManager] cheatsFile];
    [[NSFileManager defaultManager] removeItemAtPath:cheatsFile error:nil];
    GB_save_cheats(_gb, cheatsFile.UTF8String);
}

@end

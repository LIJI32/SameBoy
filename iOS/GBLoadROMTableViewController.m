#import "GBLoadROMTableViewController.h"
#import "GBROMManager.h"
#import "GBViewController.h"
#import <objc/runtime.h>

@interface GBLoadROMTableViewController() <UIDocumentPickerDelegate>
@end

@implementation GBLoadROMTableViewController
{
    NSIndexPath *_renamingPath;
}

- (instancetype)init
{
    self = [super initWithStyle:UITableViewStyleGrouped];
    self.navigationItem.rightBarButtonItem = self.editButtonItem;
    return self;
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
    return 2;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    if (section == 1) return 1;
    return [GBROMManager sharedManager].allROMs.count;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (indexPath.section == 1) {
        UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
        cell.textLabel.text = @"Import ROM files";
        return cell;
    }
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    NSString *rom = [GBROMManager sharedManager].allROMs[[indexPath indexAtPosition:1]];
    cell.textLabel.text = rom;
    cell.accessoryType = [rom isEqualToString:[GBROMManager sharedManager].currentROM]? UITableViewCellAccessoryCheckmark : UITableViewCellAccessoryNone;
    
    NSString *pngPath = [[[GBROMManager sharedManager] autosaveStateFileForROM:rom] stringByAppendingPathExtension:@"png"];
    UIGraphicsBeginImageContextWithOptions((CGSize){60, 60}, false, self.view.window.screen.scale);
    UIBezierPath *mask = [UIBezierPath bezierPathWithRoundedRect:CGRectMake(0, 3, 60, 54) cornerRadius:4];
    [mask addClip];
    UIImage *image = [UIImage imageWithContentsOfFile:pngPath];
    [image drawInRect:mask.bounds];
    if (@available(iOS 13.0, *)) {
        [[UIColor tertiaryLabelColor] set];
    }
    else {
        [[UIColor colorWithWhite:0 alpha:0.5] set];
    }
    [mask stroke];
    cell.imageView.image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    
    return cell;
}

- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (indexPath.section == 1) return [super tableView:tableView heightForRowAtIndexPath:indexPath];

    return 60;
}

- (NSString *)title
{
    return @"ROM Library";
}

- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section
{
    if (section == 0) return nil;

    return @"You can also import ROM files by opening them in SameBoy using the Files app or a web browser, or by sending them over with AirDrop.";
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (indexPath.section == 1) {
        UIViewController *parent = self.presentingViewController;
        [self.presentingViewController dismissViewControllerAnimated:true completion:^{
            UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[@"com.github.liji32.sameboy.gb",
                                                                                                                     @"com.github.liji32.sameboy.gbc",
                                                                                                                     @"com.github.liji32.sameboy.isx",
                                                                                                                     @"public.gbrom"]
                                                                                                            inMode:UIDocumentPickerModeImport];
            picker.allowsMultipleSelection = true;
            if (@available(iOS 13.0, *)) {
                picker.shouldShowFileExtensions = true;
            }
            picker.delegate = self;
            objc_setAssociatedObject(picker, @selector(delegate), self, OBJC_ASSOCIATION_RETAIN);
            [parent presentViewController:picker animated:true completion:nil];
        }];
        return;
    }
    [GBROMManager sharedManager].currentROM = [GBROMManager sharedManager].allROMs[[indexPath indexAtPosition:1]];
    [self.presentingViewController dismissViewControllerAnimated:true completion:^{
        [[NSNotificationCenter defaultCenter] postNotificationName:@"GBROMChanged" object:nil];
    }];
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray <NSURL *>*)urls
{
    if (urls.count == 1) {
        NSURL *url = urls.firstObject;
        NSString *potentialROM = [[url.path stringByDeletingLastPathComponent] lastPathComponent];
        if ([[[GBROMManager sharedManager] romFileForROM:potentialROM].stringByStandardizingPath isEqualToString:url.path.stringByStandardizingPath]) {
            [GBROMManager sharedManager].currentROM = potentialROM;
        }
        else {
            [url startAccessingSecurityScopedResource];
            [GBROMManager sharedManager].currentROM =
            [[GBROMManager sharedManager] importROM:url.path
                                       keepOriginal:true];
            [url stopAccessingSecurityScopedResource];
        }
        [[NSNotificationCenter defaultCenter] postNotificationName:@"GBROMChanged" object:nil];
    }
    else {
        for (NSURL *url in urls) {
            NSString *potentialROM = [[url.path stringByDeletingLastPathComponent] lastPathComponent];
            if ([[[GBROMManager sharedManager] romFileForROM:potentialROM].stringByStandardizingPath isEqualToString:url.path.stringByStandardizingPath]) {
                // That's an already imported ROM
                continue;
            }
            [url startAccessingSecurityScopedResource];
            [[GBROMManager sharedManager] importROM:url.path
                                       keepOriginal:true];
            [url stopAccessingSecurityScopedResource];
        }
        [(GBViewController *)[UIApplication sharedApplication].keyWindow.rootViewController openLibrary];
    }
}

- (UIModalPresentationStyle)modalPresentationStyle
{
    return UIModalPresentationOverFullScreen;
}

- (void)tableView:(UITableView *)tableView commitEditingStyle:(UITableViewCellEditingStyle)editingStyle forRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (indexPath.section == 1) return;

    if (editingStyle != UITableViewCellEditingStyleDelete) return;
    NSString *rom = [GBROMManager sharedManager].allROMs[[indexPath indexAtPosition:1]];
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Delete ROM “%@”?", rom]
                                                                   message: @"Save data for this ROM will also be deleted."
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert  addAction:[UIAlertAction actionWithTitle:@"Delete"
                                               style:UIAlertActionStyleDestructive
                                             handler:^(UIAlertAction *action) {
        [[GBROMManager sharedManager] deleteROM:rom];
        [self.tableView deleteRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationAutomatic];
        if ([[GBROMManager sharedManager].currentROM isEqualToString:rom]) {
            [GBROMManager sharedManager].currentROM = nil;
            [[NSNotificationCenter defaultCenter] postNotificationName:@"GBROMChanged" object:nil];
        }
    }]];
    [alert  addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                               style:UIAlertActionStyleCancel
                                             handler:nil]];
    [self presentViewController:alert animated:true completion:nil];
}

- (void)renameRow:(NSIndexPath *)indexPath
{
    if (indexPath.section == 1) return;
    
    UITableViewCell *cell = [self.tableView cellForRowAtIndexPath:indexPath];
    UITextField *field = [[UITextField alloc] initWithFrame:cell.textLabel.frame];
    field.font = cell.textLabel.font;
    field.text = cell.textLabel.text;
    cell.textLabel.text = @"";
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
    NSString *oldName = [GBROMManager sharedManager].allROMs[[_renamingPath indexAtPosition:1]];
    _renamingPath = nil;
    if ([newName isEqualToString:oldName]) {
        [self.tableView reloadData];
        return;
    }
    if ([newName containsString:@"/"]) {
        [self.tableView reloadData];
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"You can't use a name that contains “/”. Please choose another name."
                                                                       message:nil
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert  addAction:[UIAlertAction actionWithTitle:@"OK"
                                                   style:UIAlertActionStyleCancel
                                                 handler:nil]];
        [self presentViewController:alert animated:true completion:nil];
        return;
    }
    [[GBROMManager sharedManager] renameROM:oldName toName:newName];
    [self.tableView reloadData];
    _renamingPath = nil;
}

- (BOOL)tableView:(UITableView *)tableView canEditRowAtIndexPath:(NSIndexPath *)indexPath
{
    return indexPath.section == 0;
}

// Leave these ROM management to iOS 13.0 and up for now
- (UIContextMenuConfiguration *)tableView:(UITableView *)tableView
contextMenuConfigurationForRowAtIndexPath:(NSIndexPath *)indexPath
                                    point:(CGPoint)point API_AVAILABLE(ios(13.0))
{
    if (indexPath.section == 1) return nil;
    
    return [UIContextMenuConfiguration configurationWithIdentifier:nil
                                                   previewProvider:nil
                                                    actionProvider:^UIMenu *(NSArray<UIMenuElement *> *suggestedActions) {
        return [UIMenu menuWithTitle:nil children:@[
            [UIAction actionWithTitle:@"Rename"
                                image:[UIImage systemImageNamed:@"pencil"]
                           identifier:nil
                              handler:^(__kindof UIAction *action) {
                [self renameRow:indexPath];
            }],
            [UIAction actionWithTitle:@"Duplicate"
                                image:[UIImage systemImageNamed:@"plus.square.on.square"]
                           identifier:nil
                              handler:^(__kindof UIAction *action) {
                [[GBROMManager sharedManager] duplicateROM:[GBROMManager sharedManager].allROMs[[indexPath indexAtPosition:1]]];
                [self.tableView reloadData];
            }],
        ]];
    }];
}

@end

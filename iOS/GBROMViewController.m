#import "GBROMViewController.h"
#import "GBROMManager.h"
#import "GBViewController.h"
#import "GBLibraryViewController.h"
#import <CoreServices/CoreServices.h>
#import <objc/runtime.h>
#ifdef APPSTORE
#import "GBWatchManager.h"
#import "GBSubscriptionManager.h"
#import "GBSubscriptionViewController.h"
#endif

@implementation GBROMViewController
{
    NSIndexPath *_renamingPath;
    NSArray *_roms;
#ifdef APPSTORE
    bool _watchMode;
#endif
}

- (instancetype)init
{
    self = [super initWithStyle:UITableViewStyleGrouped];
    self.navigationItem.rightBarButtonItem = self.editButtonItem;
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(deselectRow)
                                                 name:UIApplicationDidBecomeActiveNotification
                                               object:nil];
    
    [[NSNotificationCenter defaultCenter] addObserver:self.tableView
                                             selector:@selector(reloadData)
                                                 name:UIApplicationDidBecomeActiveNotification
                                               object:nil];
    
    return self;
}

#ifdef APPSTORE
- (instancetype)initForWatch
{
    _watchMode = true;
    self = [self init];
    return self;
}
#endif

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
#ifdef APPSTORE
    if (_watchMode) {
        return 1;
    }
#endif
    return 2;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    if (section == 1) return 2;
    return (_roms = [GBROMManager sharedManager].allROMs).count;
}

- (UITableViewCell *)cellForROM:(NSString *)rom
{
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    cell.textLabel.text = rom.lastPathComponent;
    bool isCurrentROM = [rom isEqualToString:[GBROMManager sharedManager].currentROM];
#ifdef APPSTORE
    bool isWatchROM = [[GBROMManager sharedManager] watchUUIDForROM:rom generateIfMissing:false];
    bool checkmark = _watchMode? isWatchROM : isCurrentROM;
#else
    bool checkmark = isCurrentROM;
#endif
    cell.accessoryType = checkmark? UITableViewCellAccessoryCheckmark : UITableViewCellAccessoryNone;
    
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
    
#ifdef APPSTORE
    if (_watchMode) {
        if (isCurrentROM) {
            cell.accessoryView = [[UIImageView alloc] initWithImage:self.tabBarController.viewControllers.firstObject.tabBarItem.image];
        }
    }
    else if (isWatchROM) {
        cell.accessoryView = [[UIImageView alloc] initWithImage:[UIImage systemImageNamed:@"applewatch"] ?: [UIImage systemImageNamed:@"clock"]];
    }
#endif
    
    return cell;

}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (indexPath.section == 1) {
        UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
        switch (indexPath.item) {
            case 0: cell.textLabel.text = @"Import ROM files"; break;
            case 1: cell.textLabel.text = @"Show Library in Files"; break;
        }
        return cell;
    }
    return [self cellForROM:_roms[[indexPath indexAtPosition:1]]];
}

- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (indexPath.section == 1) return [super tableView:tableView heightForRowAtIndexPath:indexPath];

    return 60;
}

- (NSString *)title
{
#ifdef APPSTORE
    if (_watchMode) {
        return @"Apple Watch";
    }
#endif
    return @"Local Library";
}

- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section
{
    if (section == 0) return nil;

    return @"You can also import ROM files by opening them in SameBoy using the Files app or a web browser, or by sending them over with AirDrop.";
}

- (void)romSelectedAtIndex:(unsigned)index
{
    NSString *rom = _roms[index];
#ifdef APPSTORE
    if (_watchMode) {
        if ([[GBROMManager sharedManager] watchUUIDForROM:[GBROMManager sharedManager].allROMs[index] generateIfMissing:false]) {
            [self deselectRow];
            return;
        }
        [self moveToWatch:index];
        return;
    }
    
    if ([[GBROMManager sharedManager] watchUUIDForROM:[GBROMManager sharedManager].allROMs[index] generateIfMissing:false]) {
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"“%@” needs to be moved from Apple Watch.", rom]
                                                                       message:[NSString stringWithFormat:@"“%@” needs to be moved from Apple Watch before being played on this iPhone.", rom]
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"Move from Apple Watch"
                                                  style:UIAlertActionStyleDefault
                                                handler:^(UIAlertAction *action) {
            [self moveFromWatch:rom completion:^(bool success) {
                if (success) {
                    [self romSelectedAtIndex:index];
                }
            }];
        }]];
        [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                                  style:UIAlertActionStyleCancel
                                                handler:^(UIAlertAction *action) {
            [self deselectRow];
        }]];
        [self presentViewController:alert animated:true completion:nil];
        return;
    }
#endif
    [GBROMManager sharedManager].currentROM = rom;
    [self.presentingViewController dismissViewControllerAnimated:true completion:nil];
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (indexPath.section == 1) {
        switch (indexPath.item) {
            case 0: {
                UIViewController *parent = self.presentingViewController;
                NSString *gbUTI = (__bridge_transfer NSString *)UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, (__bridge CFStringRef)@"gb", NULL);
                NSString *gbcUTI = (__bridge_transfer NSString *)UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, (__bridge CFStringRef)@"gbc", NULL);
                NSString *isxUTI = (__bridge_transfer NSString *)UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, (__bridge CFStringRef)@"isx", NULL);
                NSString *zipUTI = (__bridge_transfer NSString *)UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, (__bridge CFStringRef)@"zip", NULL);

                NSMutableSet *extensions = [NSMutableSet set];
                [extensions addObjectsFromArray:(__bridge_transfer NSArray *)UTTypeCopyAllTagsWithClass((__bridge CFStringRef)gbUTI, kUTTagClassFilenameExtension)];
                [extensions addObjectsFromArray:(__bridge_transfer NSArray *)UTTypeCopyAllTagsWithClass((__bridge CFStringRef)gbcUTI, kUTTagClassFilenameExtension)];
                [extensions addObjectsFromArray:(__bridge_transfer NSArray *)UTTypeCopyAllTagsWithClass((__bridge CFStringRef)isxUTI, kUTTagClassFilenameExtension)];
                
                if (extensions.count != 3) {
                    if (![[NSUserDefaults standardUserDefaults] boolForKey:@"GBShownUTIWarning"]) {
                        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"File Association Conflict"
                                                                                       message:@"Due to a limitation in iOS, the file picker will allow you to select files not supported by SameBoy. SameBoy will only import GB, GBC and ISX files.\n\nIf you have a multi-system emulator installed, updating it could fix this problem."
                                                                                preferredStyle:UIAlertControllerStyleAlert];
                        [alert addAction:[UIAlertAction actionWithTitle:@"Close"
                                                                  style:UIAlertActionStyleCancel
                                                                handler:^(UIAlertAction *action) {
                            [[NSUserDefaults standardUserDefaults] setBool:true forKey:@"GBShownUTIWarning"];
                            [self tableView:tableView didSelectRowAtIndexPath:indexPath];
                        }]];
                        [self presentViewController:alert animated:true completion:nil];
                        return;
                    }
                }
                                
                [self.presentingViewController dismissViewControllerAnimated:true completion:^{
                    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[@"com.github.liji32.sameboy.gb",
                                                                                                                             @"com.github.liji32.sameboy.gbc",
                                                                                                                             @"com.github.liji32.sameboy.isx",
                                                                                                                             gbUTI ?: @"",
                                                                                                                             gbcUTI ?: @"",
                                                                                                                             isxUTI ?: @"",
                                                                                                                             zipUTI ?: @""]
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
            case 1: {
                NSURL *url = [NSURL URLWithString:[NSString stringWithFormat:@"shareddocuments://%@",
                                                   [self.rootPath stringByAddingPercentEncodingWithAllowedCharacters:[NSCharacterSet URLPathAllowedCharacterSet]]]];
                [[UIApplication sharedApplication] openURL:url
                                                   options:nil
                                         completionHandler:nil];
                return;
            }
        }
    }
    [self romSelectedAtIndex:indexPath.row];
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray <NSURL *>*)urls
{
    [(GBViewController *)[UIApplication sharedApplication].delegate handleOpenURLs:urls
#ifdef APPSTORE
                                                                     importToCloud:false
#endif
                                                                       openInPlace:false];
}

- (UIModalPresentationStyle)modalPresentationStyle
{
    return UIModalPresentationOverFullScreen;
}

- (void)deleteROMAtIndex:(unsigned)index
{
    NSString *rom = _roms[index];
    
#ifdef APPSTORE
    if ([[GBROMManager sharedManager] watchUUIDForROM:rom generateIfMissing:false]) {
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"“%@” needs to be moved from Apple Watch.", rom]
                                                                       message:[NSString stringWithFormat:@"“%@” needs to be moved from Apple Watch before being deleted.", rom]
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"Move from Apple Watch"
                                                  style:UIAlertActionStyleDefault
                                                handler:^(UIAlertAction *action) {
            [self moveFromWatch:rom completion:^(bool success) {
                if (success) {
                    [self deleteROMAtIndex:index];
                }
            }];
        }]];
        [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                                  style:UIAlertActionStyleCancel
                                                handler:^(UIAlertAction *action) {
            [self deselectRow];
        }]];
        [self presentViewController:alert animated:true completion:nil];
        return;
    }
#endif

    [[GBROMManager sharedManager] deleteROM:rom];
    [self.tableView deleteRowsAtIndexPaths:@[[NSIndexPath indexPathForRow:index inSection:0]] withRowAnimation:UITableViewRowAnimationAutomatic];
    if ([[GBROMManager sharedManager].currentROM isEqualToString:rom]) {
        [GBROMManager sharedManager].currentROM = nil;
    }
}

- (void)tableView:(UITableView *)tableView commitEditingStyle:(UITableViewCellEditingStyle)editingStyle forRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (indexPath.section == 1) return;

    if (editingStyle != UITableViewCellEditingStyleDelete) return;
    NSString *rom = [self.tableView cellForRowAtIndexPath:indexPath].textLabel.text;
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Delete “%@”?", rom]
                                                                   message: @"Save data for this ROM will also be deleted."
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Delete"
                                              style:UIAlertActionStyleDestructive
                                            handler:^(UIAlertAction *action) {
        [self deleteROMAtIndex:indexPath.row];
    }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                              style:UIAlertActionStyleCancel
                                            handler:nil]];
    [self presentViewController:alert animated:true completion:nil];
}

- (void)renameRow:(NSIndexPath *)indexPath
{
    if (indexPath.section == 1) return;
    
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

- (void)renameROM:(NSString *)oldName toName:(NSString *)newName
{
    [[GBROMManager sharedManager] renameROM:oldName toName:newName];
    [self.tableView reloadData];
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
    
    if ([newName containsString:@"/"]) {
        [self.tableView reloadData];
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"You can't use a name that contains “/”. Please choose another name."
                                                                       message:nil
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                                  style:UIAlertActionStyleCancel
                                                handler:nil]];
        [self presentViewController:alert animated:true completion:nil];
        return;
    }
    [self renameROM:oldName toName:newName];
    _renamingPath = nil;
}

- (BOOL)tableView:(UITableView *)tableView canEditRowAtIndexPath:(NSIndexPath *)indexPath
{
    return indexPath.section == 0;
}

- (void)duplicateROMAtIndex:(unsigned)index
{
    [[GBROMManager sharedManager] duplicateROM:_roms[index]];
    [self.tableView reloadData];
}

#ifdef APPSTORE
- (void)transitionToSibling:(unsigned)index
{
    [(GBLibraryViewController *)self.tabBarController setEnableAnimations:true];
    self.tabBarController.selectedIndex = index;
    [(GBLibraryViewController *)self.tabBarController setEnableAnimations:false];
}

- (UIAction *)transferActionForROMIndex:(unsigned)index API_AVAILABLE(ios(13.0))
{
    return [UIAction actionWithTitle:@"Move to iCloud"
                        image:[UIImage systemImageNamed:@"icloud"]
                   identifier:nil
                      handler:^(__kindof UIAction *action) {
        [[GBROMManager sharedManager] moveROMToCloud:[GBROMManager sharedManager].allROMs[index]
                                           completion:^(NSString *error) {
            if (error) {
                UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Move to iCloud Failed"
                                                                               message:error
                                                                        preferredStyle:UIAlertControllerStyleAlert];
                [alert addAction:[UIAlertAction actionWithTitle:@"Close"
                                                          style:UIAlertActionStyleCancel
                                                        handler:nil]];
                [self presentViewController:alert animated:true completion:nil];
                return;
            }
            [self.tableView deleteRowsAtIndexPaths:@[[NSIndexPath indexPathForRow:index inSection:0]]
                                  withRowAnimation:UITableViewRowAnimationAutomatic];
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(NSEC_PER_SEC / 2)), dispatch_get_main_queue(), ^{
                [self transitionToSibling:1];
            });
        }];
        [self.tableView reloadData];
    }];
}

- (void)moveToWatch:(unsigned)index
{
    if (GBSubscriptionManager.defaultManager.watchState == GBSubscriptionInactive) {
        UINavigationController *navController = [[UINavigationController alloc] initWithRootViewController:[[GBSubscriptionViewController alloc] init]];
        UIBarButtonItem *close = [[UIBarButtonItem alloc] initWithTitle:@"Close"
                                                                  style:UIBarButtonItemStylePlain
                                                                 target:self
                                                                 action:@selector(dismissViewController)];
        [navController.visibleViewController.navigationItem setLeftBarButtonItem:close];
        
        [self presentViewController:navController animated:true completion:nil];
        return;
    }
    
    if (![GBWatchManager sharedManager].isReachable) {
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Launch SameBoy on your Apple Watch"
                                                                       message:@"Launch SameBoy on the Apple Watch you wish to move this ROM to."
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                                  style:UIAlertActionStyleCancel
                                                handler:^(UIAlertAction *action) {
            [[GBWatchManager sharedManager] cancelRunWhenReachable];
            [self deselectRow];
        }]];
        [self presentViewController:alert animated:true completion:nil];
        [[GBWatchManager sharedManager] runWhenReachable:^{
            [alert dismissViewControllerAnimated:true completion:nil];
            [self moveToWatch:index];
        }];
        return;
    }
    self.view.window.userInteractionEnabled = false;
    void (^doError)(NSString *error) = ^void(NSString *error) {
        dispatch_async(dispatch_get_main_queue(), ^{
            self.view.window.userInteractionEnabled = true;
            [self.tableView reloadData];
            UIAlertController *alert = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Failed to move “%@” to Watch", [GBROMManager sharedManager].allROMs[index]]
                                                                           message:error
                                                                    preferredStyle:UIAlertControllerStyleAlert];
            [alert addAction:[UIAlertAction actionWithTitle:@"Close"
                                                      style:UIAlertActionStyleCancel
                                                    handler:nil]];
            [self presentViewController:alert animated:true completion:nil];
        });
    };
    UIActivityIndicatorView *spinner = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
    UITableViewCell *cell = [self.tableView cellForRowAtIndexPath:[NSIndexPath indexPathForRow:index inSection:0]];
    cell.accessoryView = spinner;
    [spinner startAnimating];


    [[GBWatchManager sharedManager] getUUID:^(NSString *error, NSUUID *uuid) {
        if (error) {
            doError(error);
            return;
        }
        if (uuid) {
            NSString *currentROM = [[GBROMManager sharedManager] watchUUIDMap][uuid];
            if (currentROM) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    cell.accessoryView = nil;
                    self.view.window.userInteractionEnabled = true;
                    [self moveFromWatch:currentROM completion:^(bool success) {
                        if (success) {
                            [self moveToWatch:index];
                        }
                    }];
                });
                return;
            }
        }
        
        [[GBWatchManager sharedManager] updateBootROMs:^(NSString *error) {
            if (error) {
                doError(error);
                return;
            }
            [[GBWatchManager sharedManager] loadROM:[GBROMManager sharedManager].allROMs[index] completion:^(NSString *error) {
                if (error) {
                    doError(error);
                    return;
                }
                dispatch_async(dispatch_get_main_queue(), ^{
                    self.view.window.userInteractionEnabled = true;
                    [self.tableView reloadData];
                });
            }];
        }];
    }];
}

- (void)moveFromWatch:(NSString *)rom completion:(void (^)(bool))completion
{
    void (^doErrorTryForce)(NSString *error) = ^void(NSString *error) {
        self.view.window.userInteractionEnabled = true;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.tableView reloadData];
            UIAlertController *alert = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Failed to move “%@” from Apple Watch", rom]
                                                                           message:[NSString stringWithFormat:@"%@\nContinue anyway? Apple Watch progress on this ROM might become lost.", error]
                                                                    preferredStyle:UIAlertControllerStyleAlert];
            [alert addAction:[UIAlertAction actionWithTitle:@"Continue"
                                                      style:UIAlertActionStyleDestructive
                                                    handler:^(UIAlertAction *action) {
                [[GBROMManager sharedManager] invalidateWatchUUIDForROM:rom];
                [self.tableView reloadData];
                if (completion) completion(true);
            }]];
            [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                                      style:UIAlertActionStyleCancel
                                                    handler:nil]];
            [self presentViewController:alert animated:true completion:^{
                if (completion) completion(false);
                [self deselectRow];
            }];
        });
    };
    
    if (![GBWatchManager sharedManager].isReachable) {
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Launch SameBoy on your Apple Watch"
                                                                       message:@"Launch SameBoy on the Apple Watch that currently holds this ROM."
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                                  style:UIAlertActionStyleCancel
                                                handler:^(UIAlertAction *action) {
            [[GBWatchManager sharedManager] cancelRunWhenReachable];
            doErrorTryForce(@"The Apple Watch currently holding this ROM is not reachable.");
        }]];
        [self presentViewController:alert animated:true completion:nil];
        [[GBWatchManager sharedManager] runWhenReachable:^{
            [alert dismissViewControllerAnimated:true completion:nil];
            [self moveFromWatch:rom completion:completion];
        }];
        return;
    }
    
    self.view.window.userInteractionEnabled = false;
    UIActivityIndicatorView *spinner = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
    UITableViewCell *cell = [self.tableView cellForRowAtIndexPath:[NSIndexPath indexPathForRow:[[GBROMManager sharedManager].allROMs indexOfObject:rom]
                                                                                     inSection:0]];
    cell.accessoryView = spinner;
    [spinner startAnimating];

    
    NSUUID *targetUUID = [[GBROMManager sharedManager] watchUUIDForROM:rom generateIfMissing:false];
    [[GBWatchManager sharedManager] getSaveState:^(NSString *error, NSData *saveState, NSData *png, NSUUID *currentUUID) {
        if (error) {
            doErrorTryForce(error);
            return;
        }
        if (![targetUUID isEqual:currentUUID]) {
            doErrorTryForce(@"This ROM is not loaded on the currently active Apple Watch.");
            return;
        }
        
        [[GBWatchManager sharedManager] closeROM:^(NSString *error) {
            if (error) {
                doErrorTryForce(error);
                return;
            }
            NSString *path = [[GBROMManager sharedManager] autosaveStateFileForROM:rom];
            if (saveState) {
                [saveState writeToFile:path atomically:false];
            }
            if (png) {
                [png writeToFile:[path stringByAppendingPathExtension:@"png"] atomically:false];
            }
            [[GBROMManager sharedManager] invalidateWatchUUIDForROM:rom];
            dispatch_async(dispatch_get_main_queue(), ^{
                [self.tableView reloadData];
                self.view.window.userInteractionEnabled = true;
                if (completion) completion(true);
            });
        }];
    }];
}

- (void)moveFromWatch:(unsigned)index
{
    [self moveFromWatch:[GBROMManager sharedManager].allROMs[index] completion:nil];
}

#endif

// Leave these ROM management to iOS 13.0 and up for now
- (UIContextMenuConfiguration *)tableView:(UITableView *)tableView
contextMenuConfigurationForRowAtIndexPath:(NSIndexPath *)indexPath
                                    point:(CGPoint)point API_AVAILABLE(ios(13.0))
{
    if (indexPath.section == 1) return nil;
    
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
        NSMutableArray *items = @[
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
                [self duplicateROMAtIndex:indexPath.row];
            }],
        ].mutableCopy;
#ifdef APPSTORE
        if (self.class == [GBROMViewController class] && [GBWatchManager sharedManager].isPaired && !_watchMode) {
            if ([[GBROMManager sharedManager] watchUUIDForROM:[GBROMManager sharedManager].allROMs[indexPath.row]
                                            generateIfMissing:false]) {
                [items addObject:[UIAction actionWithTitle:@"Move from Apple Watch"
                                                     image:[UIImage systemImageNamed:@"applewatch"] ?: [UIImage systemImageNamed:@"arrow.up.doc"]
                                                identifier:nil
                                                   handler:^(__kindof UIAction *action) {
                    [self moveFromWatch:indexPath.row];
                }]];
            }
            else {
                [items addObject:[UIAction actionWithTitle:@"Move to Apple Watch"
                                                     image:[UIImage systemImageNamed:@"applewatch"] ?: [UIImage systemImageNamed:@"arrow.down.doc"]
                                                identifier:nil
                                                   handler:^(__kindof UIAction *action) {
                    [self moveToWatch:indexPath.row];
                }]];
                [items addObject:[self transferActionForROMIndex:indexPath.row]];
            }
        }
        else if (!_watchMode) {
            [items addObject:[self transferActionForROMIndex:indexPath.row]];
        }
#endif
        [items addObject:deleteAction];
        return [UIMenu menuWithTitle:nil children:items];
    }];
}

- (void)deselectRow
{
    if (self.tableView.indexPathForSelectedRow) {
        [self.tableView deselectRowAtIndexPath:self.tableView.indexPathForSelectedRow animated:true];
    }
}

- (void)viewWillAppear:(BOOL)animated
{
    [self.tableView reloadData];
    [super viewWillAppear:animated];
}

- (NSString *)rootPath
{
    return [GBROMManager sharedManager].localRoot;
}

- (void)dismissViewController
{
    [self dismissViewControllerAnimated:true completion:nil];
}
@end

#ifdef APPSTORE
#import "GBCloudROMViewController.h"
#import "GBViewController.h"
#import "GBROMManager.h"

@implementation GBCloudROMViewController
{
    NSArray<NSString *> *_roms;
    bool _loadingRequested;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
    if (section != 0) {
        return [super tableView:tableView numberOfRowsInSection:section];
    }
    return _roms? _roms.count : 1;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    if (indexPath.section != 0) {
        return [super tableView:tableView cellForRowAtIndexPath:indexPath];
    }
    if (!_roms) {
        UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:0];
        UIActivityIndicatorView *spinner = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
        cell.bounds = spinner.bounds;
        [cell addSubview:spinner];
        [spinner startAnimating];
        spinner.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        return cell;
    }
    NSString *rom = _roms[indexPath.row];
    UITableViewCell *cell = [self cellForROM:rom];
    
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSString *pngPath = [[[GBROMManager sharedManager] autosaveStateFileForROM:rom] stringByAppendingPathExtension:@"png"];
        
        
        NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
        NSError *error;
        [coordinator coordinateReadingItemAtURL:[NSURL fileURLWithPath:pngPath]
                                        options:0
                                          error:&error
                                     byAccessor:^(NSURL *newURL) {
            if (!error) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    cell.imageView.image = [self cellForROM:rom].imageView.image;
                });
            }
        }];
    });

    return cell;
}

- (void)romSelectedAtIndex:(unsigned)index
{
    NSString *rom = _roms[index];
    self.view.window.userInteractionEnabled = false;
    UITableViewCell *cell = [self.tableView cellForRowAtIndexPath:[NSIndexPath indexPathForRow:index inSection:0]];
    UIActivityIndicatorView *spinner = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
    cell.accessoryView = spinner;
    [spinner startAnimating];
    
    [[GBROMManager sharedManager] syncROM:rom completion:^(NSString *error) {
        cell.accessoryView = nil;
        self.view.window.userInteractionEnabled = true;
        [self.tableView deselectRowAtIndexPath:self.tableView.indexPathForSelectedRow animated:true];
        
        if (error) {
            UIAlertController *alert = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Could not download “%@”", rom.lastPathComponent]
                                                                           message:error
                                                                    preferredStyle:UIAlertControllerStyleAlert];
            [alert addAction:[UIAlertAction actionWithTitle:@"Close"
                                                      style:UIAlertActionStyleCancel
                                                    handler:nil]];
            [self presentViewController:alert animated:true completion:nil];

        }
        else {
            [GBROMManager sharedManager].currentROM = _roms[index];
            [self.presentingViewController dismissViewControllerAnimated:true completion:nil];
        }
    } queue:[NSOperationQueue mainQueue]];
}

- (UIAction *)transferActionForROMIndex:(unsigned)index API_AVAILABLE(ios(13.0))
{
    return [UIAction actionWithTitle:@"Move to Local Library"
                               image:self.tabBarController.viewControllers[0].tabBarItem.image
                          identifier:nil
                             handler:^(__kindof UIAction *action) {
        NSString *rom = _roms[index];
        NSMutableArray *roms;
        _roms = roms = _roms.mutableCopy;
        [roms removeObjectAtIndex:index];
        [self.tableView deleteRowsAtIndexPaths:@[[NSIndexPath indexPathForRow:index inSection:0]]
                              withRowAnimation:UITableViewRowAnimationAutomatic];
        self.view.window.userInteractionEnabled = false;
        [[GBROMManager sharedManager] moveROMFromCloud:rom
                                            completion:^(NSString *error) {
            self.view.window.userInteractionEnabled = true;
            if (error) {
                [roms insertObject:rom atIndex:index];
                [self.tableView reloadData];
                UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Move to Local Library Failed"
                                                                               message:error
                                                                        preferredStyle:UIAlertControllerStyleAlert];
                [alert addAction:[UIAlertAction actionWithTitle:@"Close"
                                                          style:UIAlertActionStyleCancel
                                                        handler:nil]];
                [self presentViewController:alert animated:true completion:nil];
                return;
            }
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(NSEC_PER_SEC / 2)), dispatch_get_main_queue(), ^{
                [self transitionToSibling:0];
            });
        }];
        UINavigationController *sibling = (UINavigationController *)self.tabBarController.viewControllers[0];
        [((GBROMViewController *)(sibling.viewControllers[0])).tableView reloadData];
    }];
}

- (void)renameROM:(NSString *)oldName toName:(NSString *)newName
{
    self.view.window.userInteractionEnabled = false;
    [[GBROMManager sharedManager] renameCloudROM:[@"icloud/" stringByAppendingString:oldName]
                                              to:newName
                                      completion:^(NSString *error) {
        self.view.window.userInteractionEnabled = true;
        if (error) {
            UIAlertController *alert = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Could not rename ”%@”", oldName]
                                                                           message:error
                                                                    preferredStyle:UIAlertControllerStyleAlert];
            [alert addAction:[UIAlertAction actionWithTitle:@"Close"
                                                      style:UIAlertActionStyleCancel
                                                    handler:nil]];
            [self presentViewController:alert animated:true completion:nil];
            [self.tableView reloadData];
        }
        else {
            _loadingRequested = false;
            [self viewWillAppear:false];
        }
    }];
    [self.tableView reloadData];
}

- (void)duplicateROMAtIndex:(unsigned)index
{
    self.view.window.userInteractionEnabled = false;
    [[GBROMManager sharedManager] duplicateCloudROM:_roms[index]
                                         completion:^(NSString *error) {
        self.view.window.userInteractionEnabled = true;
        if (error) {
            UIAlertController *alert = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Could not duplicate ”%@”", _roms[index].lastPathComponent]
                                                                           message:error
                                                                    preferredStyle:UIAlertControllerStyleAlert];
            [alert addAction:[UIAlertAction actionWithTitle:@"Close"
                                                      style:UIAlertActionStyleCancel
                                                    handler:nil]];
            [self presentViewController:alert animated:true completion:nil];
        }
        else {
            _loadingRequested = false;
            [self viewWillAppear:false];
        }
    }];
}

- (void)deleteROMAtIndex:(unsigned)index
{
    self.view.window.userInteractionEnabled = false;
    NSString *rom = _roms[index];
    NSMutableArray *roms;
    _roms = roms = _roms.mutableCopy;
    [roms removeObjectAtIndex:index];
    [self.tableView deleteRowsAtIndexPaths:@[[NSIndexPath indexPathForRow:index inSection:0]]
                          withRowAnimation:UITableViewRowAnimationAutomatic];
    [[GBROMManager sharedManager] deleteCloudROM:rom
                                      completion:^(NSString *error) {
        self.view.window.userInteractionEnabled = true;
        if (error) {
            [roms insertObject:rom atIndex:index];
            [self.tableView reloadData];
            UIAlertController *alert = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Could not delete ”%@”", _roms[index].lastPathComponent]
                                                                           message:error
                                                                    preferredStyle:UIAlertControllerStyleAlert];
            [alert addAction:[UIAlertAction actionWithTitle:@"Close"
                                                      style:UIAlertActionStyleCancel
                                                    handler:nil]];
            [self presentViewController:alert animated:true completion:nil];
        }
    }];
}

- (NSString *)title
{
    return @"iCloud Library";
}

- (void)viewWillAppear:(BOOL)animated
{
    [super viewWillAppear:animated];
    if (_loadingRequested) return;
    _loadingRequested = true;
    [[GBROMManager sharedManager] obtainCloudROMList:^(NSString *error, NSArray<NSString *> *list) {
        if (error) {
            UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Could not access iCloud Library"
                                                                           message:error
                                                                    preferredStyle:UIAlertControllerStyleAlert];
            [alert addAction:[UIAlertAction actionWithTitle:@"Close"
                                                      style:UIAlertActionStyleCancel
                                                    handler:^(UIAlertAction *action) {
                [self transitionToSibling:0];
            }]];
            [self presentViewController:alert animated:true completion:nil];
        }
        else {
            _roms = list;
            [self.tableView reloadData];
        }
    }];
    
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [[GBROMManager sharedManager] obtainCloudROMList:^(NSString *error, NSArray<NSString *> *list) {
            if (![list isEqualToArray:_roms]) {
                _roms = list;
                [self.tableView reloadData];
            }
        }];
    });
}

- (void)viewDidDisappear:(BOOL)animated
{
    [super viewDidDisappear:animated];
    _loadingRequested = false;
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray <NSURL *>*)urls
{
    [(GBViewController *)[UIApplication sharedApplication].delegate handleOpenURLs:urls
                                                                     importToCloud:true
                                                                       openInPlace:false];
}

- (NSString *)rootPath
{
    return [GBROMManager sharedManager].cloudRoot.path;
}


@end
#endif

#import "GBROMManager.h"
#import <copyfile.h>

@implementation GBROMManager
{
    NSString *_romFile;
    NSMutableDictionary<NSString *,NSString *> *_cloudNameToFile;
    bool _doneInitializing;
#ifdef APPSTORE
    NSOperationQueue *_iCloudQueue;
    NSCondition *_iCloudCondition;
    enum {
        UNLOCKED,
        LOCKING,
        LOCKED,
        UNLOCKING,
    } _lockState;
    unsigned _lockRecursion;
    NSLock *_lockLock;
#endif
}

+ (instancetype)sharedManager
{
    static dispatch_once_t onceToken;
    static GBROMManager *manager;
    dispatch_once(&onceToken, ^{
        manager = [[self alloc] init];
    });
    return manager;
}

- (instancetype)init
{
    self = [super init];
    if (!self) return nil;
    self.currentROM = [[NSUserDefaults standardUserDefaults] stringForKey:@"GBLastROM"];
#ifdef APPSTORE
    _iCloudQueue = [[NSOperationQueue alloc] init];
    _iCloudCondition = [[NSCondition alloc] init];
    _iCloudQueue.maxConcurrentOperationCount = 1;
    _lockLock = [[NSLock alloc] init];
#endif
    _doneInitializing = true;
    return self;
}

- (void)setCurrentROM:(NSString *)currentROM
{
    _romFile = nil;
    _currentROM = currentROM;
    bool foundROM = self.romFile;
    
    if (currentROM && !foundROM) {
        _currentROM = nil;
    }
    
    [[NSUserDefaults standardUserDefaults] setObject:_currentROM forKey:@"GBLastROM"];
    if (_doneInitializing) {
        [[NSNotificationCenter defaultCenter] postNotificationName:@"GBROMChanged" object:nil];
    }
}

- (NSString *)romFileForDirectory:(NSString *)romDirectory
{
    for (NSString *filename in [NSFileManager.defaultManager contentsOfDirectoryAtPath:romDirectory
                                                                                 error:nil]) {
        if ([@[@"gb", @"gbc", @"isx"] containsObject:filename.pathExtension.lowercaseString]) {
            return [romDirectory stringByAppendingPathComponent:filename];
        }
    }
    
    return nil;
}

- (NSString *)romDirectoryForROM:(NSString *)romFile
{
#ifdef APPSTORE
    if ([romFile hasPrefix:@"icloud/"]) {
        NSString *name = romFile.lastPathComponent;
        NSString *root = self.cloudRoot.path;
        return [root stringByAppendingPathComponent:name];
    }
#endif

    return [self.localRoot stringByAppendingPathComponent:romFile];
}

- (NSString *)romFile
{
    if (_romFile) return _romFile;
    if (!_currentROM) return nil;
    return _romFile = [self romFileForDirectory:[self romDirectoryForROM:_currentROM]];
}

- (NSString *)romFileForROM:(NSString *)rom
{
    if ([rom isEqualToString:@"Inbox"]) return nil;
    if ([rom isEqualToString:@"Boot ROMs"]) return nil;
    if (rom == _currentROM) {
        return self.romFile;
    }
    
#ifdef APPSTORE
    if ([rom hasPrefix:@"icloud/"]) {
        NSString *name = rom.lastPathComponent;
        if (_cloudNameToFile[name]) {
            return _cloudNameToFile[name];
        }
    }
#endif
    
    return [self romFileForDirectory:[self romDirectoryForROM:rom]];
}

- (NSString *)auxilaryFileForROM:(NSString *)rom withExtension:(NSString *)extension
{
    return [[[self romFileForROM:rom] stringByDeletingPathExtension] stringByAppendingPathExtension:extension];
}

- (NSString *)batterySaveFileForROM:(NSString *)rom
{
    return [self auxilaryFileForROM:rom withExtension:@"sav"];
}

- (NSString *)batterySaveFile
{
    return [self batterySaveFileForROM:_currentROM];
}

- (NSString *)autosaveStateFileForROM:(NSString *)rom
{
    return [self auxilaryFileForROM:rom withExtension:@"auto"];
}

- (NSString *)autosaveStateFile
{
    return [self autosaveStateFileForROM:_currentROM];
}

- (NSString *)stateFile:(unsigned)index forROM:(NSString *)rom
{
    return [self auxilaryFileForROM:rom withExtension:[NSString stringWithFormat:@"s%u", index]];
}

- (NSString *)stateFile:(unsigned)index
{
    return [self stateFile:index forROM:_currentROM];
}


- (NSString *)cheatsFileForROM:(NSString *)rom
{
    return [self auxilaryFileForROM:rom withExtension:@"cht"];
}

- (NSString *)cheatsFile
{
    return [self cheatsFileForROM:_currentROM];
}

- (NSArray<NSString *> *)allROMs
{
    NSMutableArray<NSString *> *ret = [NSMutableArray array];
    NSString *root = self.localRoot;
    for (NSString *romDirectory in [NSFileManager.defaultManager contentsOfDirectoryAtPath:root
                                                                                     error:nil]) {
        if ([romDirectory hasPrefix:@"."] || [romDirectory isEqualToString:@"Inbox"]) continue;
        if ([self romFileForDirectory:[root stringByAppendingPathComponent:romDirectory]]) {
            [ret addObject:romDirectory];
        }
    }
    return [ret sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)];
}

- (NSString *)makeNameUnique:(NSString *)name
{
#ifdef APPSTORE
    if ([name hasPrefix:@"icloud/"]) {
        assert(_cloudNameToFile);
        name = name.lastPathComponent;
        if (!_cloudNameToFile[name]) return [@"icloud/" stringByAppendingString:name];
        unsigned i = 2;
        while (true) {
            NSString *attempt = [name stringByAppendingFormat:@" %u", i];
            if (_cloudNameToFile[attempt]) {
                i++;
                continue;
            }
            return [@"icloud/" stringByAppendingString:attempt];
        }
    }
#endif
    NSString *root = self.localRoot;
    if (![[NSFileManager defaultManager] fileExistsAtPath:[root stringByAppendingPathComponent:name]]) return name;
    
    unsigned i = 2;
    while (true) {
        NSString *attempt = [name stringByAppendingFormat:@" %u", i];
        if ([[NSFileManager defaultManager] fileExistsAtPath:[root stringByAppendingPathComponent:attempt]]) {
            i++;
            continue;
        }
        return attempt;
    }
}

- (NSString *)importROM:(NSString *)romFile keepOriginal:(bool)keep
{
    NSRegularExpression *regex = [NSRegularExpression regularExpressionWithPattern:@"\\([^)]+\\)|\\[[^\\]]+\\]" options:0 error:nil];
    NSString *friendlyName = [[romFile lastPathComponent] stringByDeletingPathExtension];
    friendlyName = [regex stringByReplacingMatchesInString:friendlyName options:0 range:NSMakeRange(0, [friendlyName length]) withTemplate:@""];
    friendlyName = [friendlyName stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    friendlyName = [self makeNameUnique:friendlyName];

    return [self importROM:romFile withName:friendlyName keepOriginal:keep];
}

- (NSString *)importROM:(NSString *)romFile withName:(NSString *)friendlyName keepOriginal:(bool)keep
{
    NSString *root = self.localRoot;
    NSString *romFolder = [root stringByAppendingPathComponent:friendlyName];
    [[NSFileManager defaultManager] createDirectoryAtPath:romFolder
                              withIntermediateDirectories:false
                                               attributes:nil
                                                    error:nil];
    
    NSString *newROMPath = [romFolder stringByAppendingPathComponent:romFile.lastPathComponent];
        
    if (keep) {
        if (copyfile(romFile.fileSystemRepresentation,
                     newROMPath.fileSystemRepresentation,
                     NULL,
                     COPYFILE_CLONE)) {
            [[NSFileManager defaultManager] removeItemAtPath:romFolder error:nil];
            return nil;
        }
    }
    else {
        if (![[NSFileManager defaultManager] moveItemAtPath:romFile
                                                     toPath:newROMPath
                                                      error:nil]) {
            [[NSFileManager defaultManager] removeItemAtPath:romFolder error:nil];
            return nil;
        }
        
    }
    
    return friendlyName;
}

- (NSString *)renameROM:(NSString *)rom toName:(NSString *)newName
{
    newName = [self makeNameUnique:newName];
    if ([rom isEqualToString:_currentROM]) {
        self.currentROM = newName;
    }
    NSString *root = self.localRoot;

    [[NSFileManager defaultManager] moveItemAtPath:[root stringByAppendingPathComponent:rom]
                                            toPath:[root stringByAppendingPathComponent:newName] error:nil];
    return newName;
}

- (NSString *)duplicateROM:(NSString *)rom
{
    NSString *newName = [self makeNameUnique:rom];
    return [self importROM:[self romFileForROM:rom]
                  withName:newName
              keepOriginal:true];
}

- (void)deleteROM:(NSString *)rom
{
    NSString *root = self.localRoot;
    NSString *romDirectory = [root stringByAppendingPathComponent:rom];
    [[NSFileManager defaultManager] removeItemAtPath:romDirectory error:nil];
}

#ifdef APPSTORE
- (void)deleteCloudROM:(NSString *)rom completion:(void (^)(NSString *error))completion
{
    if ([rom hasPrefix:@"icloud/"]) {
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            NSURL *iCloudRoot = self.cloudRoot;
            if (!iCloudRoot) return;
            
            NSFileAccessIntent *intent = [NSFileAccessIntent writingIntentWithURL:[iCloudRoot URLByAppendingPathComponent:rom.lastPathComponent]
                                                                          options:NSFileCoordinatorWritingForDeleting];
            
            
            NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
            
            [coordinator coordinateAccessWithIntents:@[intent]
                                               queue:[NSOperationQueue mainQueue]
                                          byAccessor:^(NSError *error) {
                if (!error) {
                    [[NSFileManager defaultManager] removeItemAtURL:intent.URL error:&error];
                }
                completion(error.localizedDescription);
            }];
        });
       return;
    }
    [self deleteROM:rom];
    completion(nil);
}

- (void)renameCloudROM:(NSString *)oldName to:(NSString *)newName completion:(void (^)(NSString *error))completion
{
    if (![oldName hasPrefix:@"icloud/"]) {
        [self renameROM:oldName toName:newName];
        completion(nil);
        return;
    }
    newName = [self makeNameUnique:[@"icloud/" stringByAppendingString:newName]];
    [self syncROM:oldName completion:^(NSString *error) {
        if (error) {
            completion(error);
            return;
        }
        [self syncROM:newName completion:^(NSString *error) {
            if (error) {
                completion(error);
                return;
            }
            NSError *fileError = nil;
            [[NSFileManager defaultManager] moveItemAtPath:[self romDirectoryForROM:oldName]
                                                    toPath:[self romDirectoryForROM:newName]
                                                     error:&fileError];
            if (!fileError && [_currentROM isEqual:oldName]) {
                self.currentROM = newName;
            }
            completion(fileError.localizedDescription);
        } queue:[NSOperationQueue mainQueue]];
    } queue:[NSOperationQueue mainQueue]];
}

- (void)duplicateCloudROM:(NSString *)oldName completion:(void (^)(NSString *error))completion
{
    if (![oldName hasPrefix:@"icloud/"]) {
        [self duplicateROM:oldName];
        completion(nil);
        return;
    }
    NSString *newName = [self makeNameUnique:oldName];
    [self syncROM:oldName completion:^(NSString *error) {
        if (error) {
            completion(error);
            return;
        }
        [self syncROM:newName completion:^(NSString *error) {
            if (error) {
                completion(error);
                return;
            }
            NSError *fileError = nil;
            [[NSFileManager defaultManager] copyItemAtPath:[self romDirectoryForROM:oldName]
                                                    toPath:[self romDirectoryForROM:newName]
                                                     error:&fileError];
            completion(fileError.localizedDescription);
        } queue:[NSOperationQueue mainQueue]];
    } queue:[NSOperationQueue mainQueue]];
}

- (void)deleteROM:(NSString *)name completion:(void (^)(NSString *error))completion
{
    if (![name hasPrefix:@"icloud/"]) {
        [self deleteROM:name];
        completion(nil);
        return;
    }
    [self syncROM:name completion:^(NSString *error) {
        if (error) {
            completion(error);
            return;
        }
        NSError *fileError = nil;
        [[NSFileManager defaultManager] removeItemAtPath:[self romDirectoryForROM:name]
                                                 error:&fileError];
        [_cloudNameToFile removeObjectForKey:name];
        completion(fileError.localizedDescription);
    } queue:[NSOperationQueue mainQueue]];
}

- (void)obtainCloudROMList:(void (^)(NSString *, NSArray<NSString *> *))completion
{
    NSURL *iCloudRoot = self.cloudRoot;
    if (!iCloudRoot) {
        completion(@"iCloud Drive is not available.", nil);
        return;
    }
    NSMetadataQuery *query = [[NSMetadataQuery alloc] init];
    query.searchScopes = @[NSMetadataQueryUbiquitousDocumentsScope];
    query.predicate = [NSPredicate predicateWithFormat:@"%K BEGINSWITH %@", NSMetadataItemPathKey, iCloudRoot.path];
    
    __block id observer = [[NSNotificationCenter defaultCenter] addObserverForName:NSMetadataQueryDidFinishGatheringNotification
                                                                            object:query
                                                                             queue:nil
                                                                        usingBlock:^(NSNotification *note) {
        NSMutableSet *set = [NSMutableSet set];
        _cloudNameToFile = [NSMutableDictionary dictionary];
        for (NSMetadataItem *item in query.results) {
            NSString *path = [item valueForAttribute:NSMetadataItemPathKey];
            if (![@[@"gb", @"gbc", @"isx"] containsObject:path.pathExtension.lowercaseString]) {
                continue;
            }
            NSString *name = [path stringByDeletingLastPathComponent].lastPathComponent;
            _cloudNameToFile[name] = path;
            [set addObject:[@"icloud/" stringByAppendingString:name]];
        }
        [query stopQuery];
        [[NSNotificationCenter defaultCenter] removeObserver:observer];
        completion(nil, [set.allObjects sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)]);
    }];
    [query startQuery];
}

- (void)syncROM:(NSString *)rom completion:(void (^)(NSString *error))completion queue:(NSOperationQueue *)queue
{
    if (![rom hasPrefix:@"icloud/"]) {
        if (queue == [NSOperationQueue currentQueue]) {
            completion(nil);
        }
        else {
            [queue addOperationWithBlock:^{
                completion(nil);
            }];
        }
        return;
    }
    NSURL *iCloudRoot = self.cloudRoot;
    if (!iCloudRoot) {
        completion(@"iCloud Drive is not available.");
        return;
    }
    NSURL *url = [iCloudRoot URLByAppendingPathComponent:rom.lastPathComponent];
    NSMetadataQuery *query = [[NSMetadataQuery alloc] init];
    query.operationQueue = _iCloudQueue;
    query.searchScopes = @[NSMetadataQueryUbiquitousDocumentsScope];
    query.predicate = [NSPredicate predicateWithFormat:@"%K BEGINSWITH %@", NSMetadataItemPathKey, [url.path stringByAppendingString:@"/"]];
    
    __block id observer = [[NSNotificationCenter defaultCenter] addObserverForName:NSMetadataQueryDidFinishGatheringNotification
                                                                            object:nil
                                                                             queue:queue
                                                                        usingBlock:^(NSNotification *note) {
        NSMutableArray *intents = @[[NSFileAccessIntent writingIntentWithURL:url
                                                                     options:0]].mutableCopy;
        NSArray <NSString *> *allowedExtensions = @[@"gb", @"gbc", @"isx", @"auto", @"sav", @"cht", @"png",
                                                    @"s0", @"s1", @"s2", @"s3", @"s4", @"s5", @"s6", @"s7", @"s8", @"s9"];

        for (NSMetadataItem *item in query.results) {
            NSURL *itemURL = [item valueForAttribute:NSMetadataItemURLKey];
            
            if ([item valueForAttribute:NSMetadataUbiquitousItemDownloadingStatusKey] == NSMetadataUbiquitousItemDownloadingStatusDownloaded) { // Not up to date
                [[NSFileManager defaultManager] evictUbiquitousItemAtURL:itemURL error:nil];
            }
            if (![allowedExtensions containsObject:itemURL.pathExtension.lowercaseString]) {
                continue;
            }
            NSFileAccessIntent *intent = [NSFileAccessIntent writingIntentWithURL:itemURL
                                                                          options:0];
            [intents addObject:intent];
        }
        [query stopQuery];
        [[NSNotificationCenter defaultCenter] removeObserver:observer];
        if (intents.count == 0) {
            completion(nil);
            return;
        }
        NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
        
        [coordinator coordinateAccessWithIntents:intents
                                           queue:queue
                                      byAccessor:^(NSError *error) {
            for (NSFileAccessIntent *intent in intents) {
                if (![intent.URL.path.stringByResolvingSymlinksInPath hasPrefix:url.path.stringByResolvingSymlinksInPath]) {
                    completion(@"This ROM may be in use by another device.");
                    return;
                }
            }
            completion(nil);
        }];
    }];
    [query startQuery];
}

- (void)moveROMToCloud:(NSString *)rom completion:(void (^)(NSString *error))completion
{
    NSURL *iCloudRoot = self.cloudRoot;
    
    if (!iCloudRoot) {
        completion(@"iCloud Drive is not available.");
        return;
    }
    
    [self obtainCloudROMList:^(NSString *error, NSArray<NSString *> *list) {
        if (error) {
            completion(error);
            return;
        }
        
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            NSError *error = nil;
            
            NSString *uniqueROM = [self makeNameUnique:[@"icloud/" stringByAppendingString:rom]].lastPathComponent;
            
            [[NSFileManager defaultManager] setUbiquitous:true
                                                itemAtURL:[NSURL fileURLWithPath:[self romDirectoryForROM:rom]]
                                           destinationURL:[iCloudRoot URLByAppendingPathComponent:uniqueROM]
                                                    error:&error];
            dispatch_async(dispatch_get_main_queue(), ^{
                if (!error) {
                    if ([rom isEqualToString:_currentROM]) {
                        self.currentROM = [@"icloud/" stringByAppendingString:uniqueROM];
                    }
                }
                completion([error localizedDescription]);
            });
        });
    }];
}

- (void)moveROMFromCloud:(NSString *)rom completion:(void (^)(NSString *))completion
{
    [self syncROM:rom completion:^(NSString *error) {
        if (error) {
            completion(error);
            return;
        }
        NSString *localName = [self makeNameUnique:rom.lastPathComponent];
        [[NSFileManager defaultManager] moveItemAtPath:[self romDirectoryForROM:rom]
                                                toPath:[self.localRoot stringByAppendingPathComponent:localName]
                                                 error:nil];
        if (!error && [_currentROM isEqual:rom]) {
            self.currentROM = localName;
        }
        completion(nil);
    } queue:[NSOperationQueue mainQueue]];
}

- (NSString *)lockCloudROM
{
    if (![_currentROM hasPrefix:@"icloud/"]) return nil;
    [_lockLock lock];
    if (_lockRecursion++) return nil;
            
    assert(_lockState == UNLOCKED);
    _lockState = LOCKING;
    
    __block NSString *ret = nil;
    
    [self syncROM:_currentROM completion:^(NSString *error) {
        ret = error;
        [_iCloudCondition lock];
        _lockState = LOCKED;
        [_iCloudCondition signal];
        while (_lockState != UNLOCKING) {
            [_iCloudCondition wait];
        }
        _lockState = UNLOCKED;
        [_iCloudCondition signal];
        [_iCloudCondition unlock];
    } queue:_iCloudQueue];
    
    [_iCloudCondition lock];
    while (_lockState != LOCKED) {
        [_iCloudCondition wait];
    }
    [_iCloudCondition unlock];
    [_lockLock unlock];
    return ret;
}

- (void)unlockCloudROM
{
    if (![_currentROM  hasPrefix:@"icloud/"] && _lockState != LOCKED) return;
    [_lockLock lock];
    if (--_lockRecursion) return;
    assert(_lockState == LOCKED);
    
    [_iCloudCondition lock];
    _lockState = UNLOCKING;
    [_iCloudCondition signal];
    while (_lockState != UNLOCKED) {
        [_iCloudCondition wait];
    }
    [_iCloudCondition unlock];
    [_lockLock unlock];
}

- (NSURL *)cloudRoot
{
    return [[[NSFileManager defaultManager] URLForUbiquityContainerIdentifier:nil] URLByAppendingPathComponent:@"Documents"];
}

- (void)importCloudROM:(NSString *)romFile keepOriginal:(bool)keep completion:(void (^)(NSString *romName, NSString *error))completion
{
    NSRegularExpression *regex = [NSRegularExpression regularExpressionWithPattern:@"\\([^)]+\\)|\\[[^\\]]+\\]" options:0 error:nil];
    NSString *friendlyName = [[romFile lastPathComponent] stringByDeletingPathExtension];
    friendlyName = [regex stringByReplacingMatchesInString:friendlyName options:0 range:NSMakeRange(0, [friendlyName length]) withTemplate:@""];
    friendlyName = [friendlyName stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    friendlyName = [@"icloud/" stringByAppendingString:friendlyName];
    friendlyName = [self makeNameUnique:friendlyName];
    
    [self syncROM:friendlyName
       completion:^(NSString *error) {
        if (error) {
            completion(friendlyName, error);
            return;
        }
        NSString *romFolder = [self romDirectoryForROM:friendlyName];
        [[NSFileManager defaultManager] createDirectoryAtPath:romFolder
                                  withIntermediateDirectories:false
                                                   attributes:nil
                                                        error:nil];
        
        NSString *newROMPath = [romFolder stringByAppendingPathComponent:romFile.lastPathComponent];
        
        if (keep) {
            if (copyfile(romFile.fileSystemRepresentation,
                         newROMPath.fileSystemRepresentation,
                         NULL,
                         COPYFILE_CLONE)) {
                [[NSFileManager defaultManager] removeItemAtPath:romFolder error:nil];
                completion(friendlyName, @"Failed to copy the ROM file.");
                return;
            }
        }
        else {
            if (![[NSFileManager defaultManager] moveItemAtPath:romFile
                                                         toPath:newROMPath
                                                          error:nil]) {
                [[NSFileManager defaultManager] removeItemAtPath:romFolder error:nil];
                completion(friendlyName, @"Failed to move the ROM file.");
            }
        }
        completion(friendlyName, nil);
    }
            queue:[NSOperationQueue mainQueue]];
}

#endif

- (NSString *)localRoot
{
    return  NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true).firstObject;
}
@end

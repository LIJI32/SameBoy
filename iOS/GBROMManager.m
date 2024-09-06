#import "GBROMManager.h"
#import <copyfile.h>

@implementation GBROMManager
{
    NSString *_romFile;
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
    return self;
}

- (void)setCurrentROM:(NSString *)currentROM
{
    _romFile = nil;
    _currentROM = currentROM;
    if (currentROM && !self.romFile) {
        _currentROM = nil;
    }
    
    [[NSUserDefaults standardUserDefaults] setObject:_currentROM forKey:@"GBLastROM"];
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

- (NSString *)romFile
{
    if (_romFile) return _romFile;
    if (!_currentROM) return nil;
    NSString *root = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true).firstObject;
    NSString *romDirectory = [root stringByAppendingPathComponent:_currentROM];
    return _romFile = [self romFileForDirectory:romDirectory];
}

- (NSString *)romFileForROM:(NSString *)rom
{
    if ([rom isEqualToString:@"Inbox"]) return nil;
    if ([rom isEqualToString:@"Boot ROMs"]) return nil;
    if (rom == _currentROM) {
        return self.romFile;
    }
    NSString *root = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true).firstObject;
    NSString *romDirectory = [root stringByAppendingPathComponent:rom];
    return [self romFileForDirectory:romDirectory];
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
    NSString *root = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true).firstObject;
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
    NSString *root = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true).firstObject;
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
    
    NSString *root = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true).firstObject;
    if ([[NSFileManager defaultManager] fileExistsAtPath:[root stringByAppendingPathComponent:friendlyName]]) {
        unsigned i = 2;
        while (true) {
            NSString *attempt = [friendlyName stringByAppendingFormat:@" %u", i];
            if ([[NSFileManager defaultManager] fileExistsAtPath:[root stringByAppendingPathComponent:attempt]]) {
                i++;
                continue;
            }
            friendlyName = attempt;
            break;
        }
    }

    return [self importROM:romFile withName:friendlyName keepOriginal:keep];
}

- (NSString *)importROM:(NSString *)romFile withName:(NSString *)friendlyName keepOriginal:(bool)keep
{
    NSString *root = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true).firstObject;
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
        _currentROM = newName;
    }
    NSString *root = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true).firstObject;

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
    NSString *root = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true).firstObject;
    NSString *romDirectory = [root stringByAppendingPathComponent:rom];
    [[NSFileManager defaultManager] removeItemAtPath:romDirectory error:nil];
}

@end

#ifdef APPSTORE
#import <WatchConnectivity/WatchConnectivity.h>
#import "GBROMManager.h"
#import "GBWatchManager.h"
#import <Core/gb.h>

@implementation GBWatchManager
{
    void (^_reachableBlock)(void);
}

+ (void)load
{
    [self sharedManager];
}

+ (instancetype)sharedManager
{
    static GBWatchManager *singleton = nil;;
    if (singleton) return singleton;
    
    if (![WCSession isSupported]) return nil;
    WCSession *session = [WCSession defaultSession];
    singleton = [[self alloc] init];
    session.delegate = singleton;
    [session activateSession];
    
    return singleton;
}

- (bool)isPaired
{
    return [WCSession defaultSession].isPaired;
}

- (bool)isReachable
{
    return [WCSession defaultSession].isReachable;
}

- (void)loadROM:(NSString *)rom completion:(void (^)(NSString *error))completion
{
    GBROMManager *romManager = [GBROMManager sharedManager];
    NSUUID *uuid = [romManager watchUUIDForROM:rom generateIfMissing:true];
    NSData *romData = [NSData dataWithContentsOfFile:[romManager romFileForROM:rom]];
    bool isISX = [[romManager romFileForROM:rom].pathExtension.lowercaseString isEqual:@"isx"];
    
    NSMutableDictionary *command = [NSMutableDictionary dictionary];
    command[@"cmd"] = @"load";
    command[@"uuid"] = uuid.UUIDString;
    command[@"rom"] = romData;
    command[@"isx"] = isISX? @YES : @NO;
    
    NSString *statePath = [romManager autosaveStateFileForROM:rom];
    NSData *saveState = [NSData dataWithContentsOfFile:statePath];
    if (saveState) {
        command[@"state"] = saveState;
        NSData *png = [NSData dataWithContentsOfFile:[statePath stringByAppendingPathExtension:@"png"]];
        if (png) {
            command[@"png"] = png;
        }
    }
    else {
        GB_gameboy_t *temp = NULL;
        const uint8_t *romBytes = romData.bytes;
        GB_model_t model = [[NSUserDefaults standardUserDefaults] integerForKey:@"GBDMGModel"];
        if (isISX) {
            temp = GB_alloc();
            GB_init(temp, GB_MODEL_DMG_B);
            GB_load_isx(temp, [romManager romFileForROM:rom].UTF8String);
            romBytes = GB_get_direct_access(temp, GB_DIRECT_ACCESS_ROM, NULL, NULL);
        }
        if (isISX || romData.length > 0x150) {
            if (romBytes[0x143] & 0x80) {
                model = [[NSUserDefaults standardUserDefaults] integerForKey:@"GBCGBModel"];
            }
            else if ((romBytes[0x146]  == 3)) {
                model = [[NSUserDefaults standardUserDefaults] integerForKey:@"GBSGBModel"];
            }
        }
        if (temp) {
            GB_dealloc(temp);
        }
        command[@"model"] = @(model);
    }
    [self sendMessage:command
         replyHandler:^(NSDictionary<NSString *,id> *replyMessage) {
        if (!replyMessage[@"error"] && [rom isEqual:romManager.currentROM]) {
            romManager.currentROM = nil;
        }
        else if (replyMessage[@"error"]) {
            [romManager invalidateWatchUUIDForROM:rom];
        }
        if (completion) completion(replyMessage[@"error"]);
    }
         errorHandler:^(NSString *error) {
        [romManager invalidateWatchUUIDForROM:rom];
        if (completion) completion(error);
    }];
}

- (void)closeROM:(void (^)(NSString *error))completion
{
    [self sendMessage:@{
        @"cmd": @"close"
    }
         replyHandler:^(NSDictionary<NSString *,id> *replyMessage) {
        if (completion) completion(replyMessage[@"error"]);
    }
         errorHandler:^(NSString *error) {
        if (completion) completion(error);
    }];
}

- (void)getSaveState:(void (^)(NSString *error, NSData *saveState, NSData *png, NSUUID *uuid))completion
{
    [self sendMessage:@{
        @"cmd": @"getState"
    }
         replyHandler:^(NSDictionary<NSString *,id> *replyMessage) {
        if (completion) completion(replyMessage[@"error"],
                                   replyMessage[@"state"],
                                   replyMessage[@"png"],
                                   [[NSUUID alloc] initWithUUIDString:replyMessage[@"uuid"]]);
    }
         errorHandler:^(NSString *error) {
        if (completion) completion(error, nil, nil, nil);
    }];
}

- (void)getUUID:(void (^)(NSString *error, NSUUID *uuid))completion
{
    [self sendMessage:@{
        @"cmd": @"getUUID"
    }
         replyHandler:^(NSDictionary<NSString *,id> *replyMessage) {
        if (completion) completion(replyMessage[@"error"],
                                   [[NSUUID alloc] initWithUUIDString:replyMessage[@"uuid"]]);
    }
         errorHandler:^(NSString *error) {
        if (completion) completion(error, nil);
    }];
}

- (void)updateBootROMs:(void (^)(NSString *error))completion
{
    if (![[NSUserDefaults standardUserDefaults] boolForKey:@"GBCustomBootROMs"]) {
        [self sendMessage:@{
            @"cmd": @"updateBootROMs"
        }
             replyHandler:^(NSDictionary<NSString *,id> *replyMessage) {
            if (completion) completion(replyMessage[@"error"]);
        }
             errorHandler:^(NSString *error) {
            if (completion) completion(error);
        }];
        return;
    }
    NSString *path = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true)[0];
    path = [path stringByAppendingPathComponent:@"Boot ROMs"];
    NSMutableDictionary *command = [NSMutableDictionary dictionary];
    command[@"cmd"] = @"updateBootROMs";
    for (NSString *rom in @[@"dmg0_boot.bin",
                            @"dmg_boot.bin",
                            @"mgb_boot.bin",
                            @"sgb_boot.bin",
                            @"sgb2_boot.bin",
                            @"cgb0_boot.bin",
                            @"cgb_boot.bin",
                            @"cgbE_boot.bin",
                            @"agb0_boot.bin",
                            @"agb_boot.bin"]) {
        NSString *romPath = [path stringByAppendingPathComponent:rom];
        if ([[NSFileManager defaultManager] fileExistsAtPath:romPath]) {
            command[rom] = [NSData dataWithContentsOfFile:rom];
        }
    }
    [self sendMessage:command
         replyHandler:^(NSDictionary<NSString *,id> *replyMessage) {
        if (completion) completion(replyMessage[@"error"]);
    }
         errorHandler:^(NSString *error) {
        if (completion) completion(error);
    }];
}

- (NSDictionary *)settingsDict
{
    NSMutableDictionary *ret = [[NSUserDefaults standardUserDefaults] dictionaryRepresentation].mutableCopy;
    for (NSString *key in ret.allKeys) {
        if (![key hasPrefix:@"GB"]) {
            [ret removeObjectForKey:key];
        }
    }
    return ret;
}

- (void)updateSettings:(void (^)(NSString *error))completion
{
    [self sendMessage:@{
        @"cmd": @"updateSettings",
        @"settings": [self settingsDict]
    }
         replyHandler:^(NSDictionary<NSString *,id> *replyMessage) {
        if (completion) completion(replyMessage[@"error"]);
    }
         errorHandler:^(NSString *error) {
        if (completion) completion(error);
    }];
}

- (void)session:(WCSession *)session didReceiveMessage:(NSDictionary<NSString *,id> *)message
   replyHandler:(void (^)(NSDictionary<NSString *,id> *))replyHandler
{
    // The redundant sizeof forces the compiler to validate the selector exists
    #define SelectorString(x) (sizeof(@selector(x))? @#x : nil)
    static const NSDictionary *commands = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        commands = @{
            @"updateState": SelectorString(updateState:),
            @"validateUUID": SelectorString(validateUUID:),
            @"getSettings": SelectorString(getSettings:),
        };
    });
    
    NSString *selector = commands[message[@"cmd"]];
    if (!selector) {
        replyHandler(@{@"error": @"Failed to communicate with the paired device. This watchOS app might not be up-to-date with its iOS counterpart."});
    }
    
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    replyHandler((NSDictionary *)[self performSelector:NSSelectorFromString(selector) withObject:message]);
#pragma clang diagnostic pop
}

- (NSDictionary<NSString *,id> *)updateState:(NSDictionary<NSString *,id> *)command
{
    NSUUID *uuid = [[NSUUID alloc] initWithUUIDString:command[@"uuid"]];
    GBROMManager *romManager = [GBROMManager sharedManager];
    NSString *rom = [romManager watchUUIDMap][uuid];
    if (!rom) {
        return @{@"error": @"The ROM currently loaded on this Apple Watch has been removed from its paired iPhone"};
    }
    
    [command[@"state"] writeToFile:[romManager autosaveStateFileForROM:rom]
                           options:0
                             error:false];
    [command[@"png"] writeToFile:[[romManager autosaveStateFileForROM:rom] stringByAppendingPathExtension:@"png"]
                         options:0
                           error:false];

    return @{};
}

- (NSDictionary<NSString *,id> *)getSettings:(NSDictionary<NSString *,id> *)command
{
    return @{@"settings": [self settingsDict]};
}

- (NSDictionary<NSString *,id> *)validateUUID:(NSDictionary<NSString *,id> *)command
{
    NSUUID *uuid = [[NSUUID alloc] initWithUUIDString:command[@"uuid"]];
    GBROMManager *romManager = [GBROMManager sharedManager];
    NSString *rom = [romManager watchUUIDMap][uuid];
    if (!rom) {
        return @{@"error": @"The ROM currently loaded on this Apple Watch has been removed from its paired iPhone"};
    }
    
    return @{};
}

- (void)sessionReachabilityDidChange:(WCSession *)session
{
    if (session.isReachable && _reachableBlock) {
        dispatch_async(dispatch_get_main_queue(), ^{
            _reachableBlock();
            _reachableBlock = nil;
        });
    }
}

- (void)runWhenReachable:(void (^)(void))completion
{
    if (self.isReachable) {
        completion();
        return;
    }
    _reachableBlock = completion;
}

- (void)cancelRunWhenReachable
{
    _reachableBlock = nil;
}
@end
#endif

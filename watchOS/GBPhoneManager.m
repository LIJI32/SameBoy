#ifdef APPSTORE
#import <WatchConnectivity/WatchConnectivity.h>
#import <WatchKit/WatchKit.h>
#import <sys/stat.h>
#import "GBPhoneManager.h"
#import "GBInterfaceController.h"
#import <Core/gb.h>

@interface GBPhoneManager() <WCSessionDelegate>
@end

@implementation GBPhoneManager
{
    bool _disableCommands;
}
+ (void)load
{
    [self sharedManager];
}

+ (instancetype)sharedManager
{
    static GBPhoneManager *singleton = nil;;
    if (singleton) return singleton;
    
    if (![WCSession isSupported]) return nil;
    WCSession *session = [WCSession defaultSession];
    singleton = [[self alloc] init];
    session.delegate = singleton;
    [session activateSession];
    
    return singleton;
}

- (NSString *)saveStatePath
{
    NSString *path = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true)[0];
    return [path stringByAppendingPathComponent:@"state"];
}

- (NSString *)pngPath
{
    NSString *path = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true)[0];
    return [path stringByAppendingPathComponent:@"png"];
}

- (NSString *)romPath
{
    NSString *path = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true)[0];
    return [path stringByAppendingPathComponent:@"rom"];
}

- (NSString *)metadataPath
{
    NSString *path = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true)[0];
    return [path stringByAppendingPathComponent:@"metadata"];
}

- (void)updateSaveState:(void (^)(NSString *error))completion
{
    if (_disableCommands) {
        if (completion) completion(nil);
        return;
    }
    
    NSString *uuid = [NSDictionary dictionaryWithContentsOfFile:self.metadataPath][@"uuid"];
    [self sendMessage:@{
        @"cmd": @"updateState",
        @"uuid": uuid,
        @"state": [NSData dataWithContentsOfFile:self.saveStatePath],
        @"png": [NSData dataWithContentsOfFile:self.pngPath],
    }
         replyHandler:^(NSDictionary<NSString *,id> *replyMessage) {
        if (completion) completion(replyMessage[@"error"]);
    }
         errorHandler:^(NSString *error) {
        if (completion) completion(error);
    }];
}

- (void)validateUUID:(void (^)(bool valid))completion
{
    if (_disableCommands) {
        completion(true);
        return;
    }
    
    NSString *uuid = [NSDictionary dictionaryWithContentsOfFile:self.metadataPath][@"uuid"];
    if (!uuid) {
        completion(false);
        return;
    }
    [self sendMessage:@{
        @"cmd": @"validateUUID",
        @"uuid": uuid,
    }
         replyHandler:^(NSDictionary<NSString *,id> *replyMessage) {
        if (completion) completion(!replyMessage[@"error"]);
    }
         errorHandler:^(NSString *error) {
        if (completion) completion(true); // Assume the UUID is still valid if the phone can't be accessed
    }];
}

- (void)refreshSettings:(void (^)(void))completion
{
    [self sendMessage:@{
        @"cmd": @"getSettings"
    }
         replyHandler:^(NSDictionary<NSString *,id> *replyMessage) {
        [[NSUserDefaults standardUserDefaults] setValuesForKeysWithDictionary:replyMessage[@"settings"]];
        if (completion) completion();
    }
         errorHandler:^(NSString *error) {
        if (completion) completion();
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
            @"load": SelectorString(loadROM:),
            @"close": SelectorString(closeROM:),
            @"getState": SelectorString(getState:),
            @"getUUID": SelectorString(getUUID:),
            @"updateBootROMs": SelectorString(updateBootROMs:),
            @"updateSettings": SelectorString(updateSettings:),
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

- (NSDictionary<NSString *,id> *)loadROM:(NSDictionary<NSString *,id> *)command
{
    NSString *uuid = command[@"uuid"];
    unlink(self.saveStatePath.UTF8String);
    unlink(self.pngPath.UTF8String);
    
    [command[@"rom"] writeToFile:self.romPath
                         options:0
                           error:nil];
    
    if (command[@"state"]) {
        [command[@"state"] writeToFile:self.saveStatePath
                               options:0
                                 error:nil];
        [command[@"png"] writeToFile:self.pngPath
                             options:0
                               error:nil];
        [@{
            @"uuid": uuid,
            @"isx": command[@"isx"],
        } writeToFile:self.metadataPath atomically:false];
    }
    else {
        [@{
            @"uuid": uuid,
            @"isx": command[@"isx"],
            @"model": command[@"model"]
        } writeToFile:self.metadataPath atomically:false];
    }
    
    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSNotificationCenter defaultCenter] postNotificationName:@"GBROMChanged" object:nil];
    });
    return @{};
}

- (NSDictionary<NSString *,id> *)closeROM:(NSDictionary<NSString *,id> *)command
{
    dispatch_sync(dispatch_get_main_queue(), ^{
        _disableCommands = true;
        [(GBInterfaceController *)[[WKExtension sharedExtension] rootInterfaceController] stop];
        unlink(self.saveStatePath.UTF8String);
        unlink(self.pngPath.UTF8String);
        unlink(self.romPath.UTF8String);
        unlink(self.metadataPath.UTF8String);
        [[NSNotificationCenter defaultCenter] postNotificationName:@"GBROMChanged" object:nil];
        _disableCommands = false;
    });

    return @{};
}

- (NSDictionary<NSString *,id> *)getState:(NSDictionary<NSString *,id> *)command
{
    NSMutableDictionary *ret = [NSMutableDictionary dictionary];
    dispatch_sync(dispatch_get_main_queue(), ^{
        _disableCommands = true;
        GBInterfaceController *controller = (GBInterfaceController *)[[WKExtension sharedExtension] rootInterfaceController];
        bool running = controller.isRunning;
        if (running) {
            [controller stop];
        }
        NSString *uuid = [NSDictionary dictionaryWithContentsOfFile:self.metadataPath][@"uuid"];
        if (uuid) {
            ret[@"uuid"] = uuid;
            NSData *state = [NSData dataWithContentsOfFile:self.saveStatePath];
            if (state) {
                ret[@"state"] = state;
            }
            NSData *png = [NSData dataWithContentsOfFile:self.pngPath];
            if (state) {
                ret[@"png"] = png;
            }
        }
        if (running) {
            [controller start];
        }
        _disableCommands = false;
    });
    
    return ret;
}

- (NSDictionary<NSString *,id> *)getUUID:(NSDictionary<NSString *,id> *)command
{
    NSString *uuid = [NSDictionary dictionaryWithContentsOfFile:self.metadataPath][@"uuid"];
    if (uuid) {
        return @{@"uuid": uuid};
    }
    
    return @{};
}

- (NSDictionary<NSString *,id> *)updateBootROMs:(NSDictionary<NSString *,id> *)command
{
    NSString *path = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, true)[0];
    path = [path stringByAppendingPathComponent:@"Boot ROMs"];
    [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
    mkdir(path.UTF8String, 0755);
    for (NSString *rom in command) {
        if ([rom hasSuffix:@".bin"]) {
            [command[rom] writeToFile:[path stringByAppendingPathComponent:rom] atomically:false];
        }
    }
    
    return @{};
}

- (NSDictionary<NSString *,id> *)updateSettings:(NSDictionary<NSString *,id> *)command
{
    [[NSUserDefaults standardUserDefaults] setValuesForKeysWithDictionary:command[@"settings"]];
    return @{};
}

@end
#endif

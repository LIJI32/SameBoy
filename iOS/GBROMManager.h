#import <Foundation/Foundation.h>

@interface GBROMManager : NSObject
+ (instancetype) sharedManager;

@property (readonly) NSArray<NSString *> *allROMs;
@property (nonatomic) NSString *currentROM;

@property (readonly) NSString *romFile;
@property (readonly) NSString *batterySaveFile;
@property (readonly) NSString *autosaveStateFile;
@property (readonly) NSString *cheatsFile;

@property (readonly) NSString *localRoot;
- (NSString *)stateFile:(unsigned)index;

- (NSString *)romFileForROM:(NSString *)rom;
- (NSString *)batterySaveFileForROM:(NSString *)rom;
- (NSString *)autosaveStateFileForROM:(NSString *)rom;
- (NSString *)stateFile:(unsigned)index forROM:(NSString *)rom;
- (NSString *)importROM:(NSString *)romFile keepOriginal:(bool)keep;
- (NSString *)importROM:(NSString *)romFile withName:(NSString *)friendlyName keepOriginal:(bool)keep;
- (NSString *)renameROM:(NSString *)rom toName:(NSString *)newName;
- (NSString *)duplicateROM:(NSString *)rom;
- (void)deleteROM:(NSString *)rom;

#ifdef APPSTORE
- (void)obtainCloudROMList:(void (^)(NSString *error, NSArray<NSString *> *list))completion;
- (void)syncROM:(NSString *)rom completion:(void (^)(NSString *error))completion queue:(NSOperationQueue *)queue;
- (void)renameCloudROM:(NSString *)oldName to:(NSString *)newName completion:(void (^)(NSString *error))completion;
- (void)duplicateCloudROM:(NSString *)name completion:(void (^)(NSString *error))completion;
- (void)deleteCloudROM:(NSString *)name completion:(void (^)(NSString *error))completion;
- (void)moveROMToCloud:(NSString *)rom completion:(void (^)(NSString *error))completion;
- (void)moveROMFromCloud:(NSString *)rom completion:(void (^)(NSString *error))completion;
- (void)importCloudROM:(NSString *)romFile keepOriginal:(bool)keep completion:(void (^)(NSString *romName, NSString *error))completion;
- (NSString *)lockCloudROM;
- (void)unlockCloudROM;
@property (readonly) NSURL *cloudRoot;
#endif
@end

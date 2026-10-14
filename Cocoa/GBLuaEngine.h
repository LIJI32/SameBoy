#import <Foundation/Foundation.h>
#import <Core/gb.h>

/* A BizHawk-compatible Lua scripting environment. All methods that touch the
   Lua state must be called either from the emulation thread, or while the
   emulator is not running (e.g. via -[Document performAtomicBlock:]). */

@class GBLuaEngine;

@protocol GBLuaEngineHost <NSObject>
@property (readonly) GBLuaEngine *luaEngine;
@end

@interface GBLuaEngine : NSObject
- (instancetype)initWithGameBoy:(GB_gameboy_t *)gb;
@property (copy) void (^logHandler)(NSString *text);
@property (copy) void (^clearHandler)(void);
@property (copy) void (^messageHandler)(NSString *text);
@property (copy) void (^stateHandler)(void);
@property (nonatomic, copy) NSString *romName;
@property (readonly) NSString *scriptPath;
@property (readonly, getter=isRunning) bool running;
- (bool)startScript:(NSString *)path;
- (void)stopScript;
- (void)frameEnded;
- (void)executedAddress:(uint16_t)address opcode:(uint8_t)opcode;
@end

#import "GBLuaEngine.h"
#import <Lua/lua_engine.h>

/* A thin Objective-C wrapper around the portable engine in Lua/lua_engine.c */

@implementation GBLuaEngine
{
    GB_lua_engine_t *_engine;
}

static void log_callback(void *user_data, const char *text, size_t length)
{
    GBLuaEngine *self = (__bridge GBLuaEngine *)user_data;
    if (!self.logHandler) return;
    NSString *string = [[NSString alloc] initWithBytes:text length:length encoding:NSUTF8StringEncoding] ?:
                       [[NSString alloc] initWithBytes:text length:length encoding:NSISOLatin1StringEncoding];
    self.logHandler(string);
}

static void clear_callback(void *user_data)
{
    GBLuaEngine *self = (__bridge GBLuaEngine *)user_data;
    if (self.clearHandler) self.clearHandler();
}

static void message_callback(void *user_data, const char *text)
{
    GBLuaEngine *self = (__bridge GBLuaEngine *)user_data;
    if (self.messageHandler) self.messageHandler([NSString stringWithUTF8String:text] ?: @"");
}

static void state_changed_callback(void *user_data)
{
    GBLuaEngine *self = (__bridge GBLuaEngine *)user_data;
    if (self.stateHandler) self.stateHandler();
}

static void execution_callback(GB_gameboy_t *gb, uint16_t address, uint8_t opcode)
{
    id<GBLuaEngineHost> host = (__bridge id<GBLuaEngineHost>)GB_get_user_data(gb);
    [host.luaEngine executedAddress:address opcode:opcode];
}

- (instancetype)initWithGameBoy:(GB_gameboy_t *)gb
{
    self = [super init];
    if (self) {
        static const GB_lua_engine_callbacks_t callbacks = {
            .log = log_callback,
            .clear = clear_callback,
            .message = message_callback,
            .state_changed = state_changed_callback,
            .execution_callback = execution_callback,
        };
        _engine = GB_lua_engine_create(gb, &callbacks, (__bridge void *)self);
    }
    return self;
}

- (void)dealloc
{
    GB_lua_engine_free(_engine);
}

- (void)setRomName:(NSString *)romName
{
    _romName = [romName copy];
    GB_lua_engine_set_rom_name(_engine, romName.UTF8String);
}

- (NSString *)scriptPath
{
    const char *path = GB_lua_engine_get_script_path(_engine);
    return path? [NSString stringWithUTF8String:path] : nil;
}

- (bool)isRunning
{
    return GB_lua_engine_is_running(_engine);
}

- (bool)startScript:(NSString *)path
{
    return GB_lua_engine_start_script(_engine, path.fileSystemRepresentation);
}

- (void)stopScript
{
    GB_lua_engine_stop_script(_engine);
}

- (void)frameEnded
{
    GB_lua_engine_frame_ended(_engine);
}

- (void)executedAddress:(uint16_t)address opcode:(uint8_t)opcode
{
    GB_lua_engine_executed_address(_engine, address, opcode);
}

@end

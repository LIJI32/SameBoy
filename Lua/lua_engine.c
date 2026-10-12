#include "lua_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#ifndef _WIN32
#include <strings.h>
#endif
#include <Lua/lua/lua.h>
#include <Lua/lua/lauxlib.h>
#include <Lua/lua/lualib.h>

/* Implements the subset of BizHawk's Lua API that is commonly used by tools
   such as Archipelago's connector_bizhawk_generic.lua, on top of SameBoy. */

int luaopen_socket_core(lua_State *L);

typedef enum {
    DOMAIN_WRAM,
    DOMAIN_ROM,
    DOMAIN_VRAM,
    DOMAIN_OAM,
    DOMAIN_HRAM,
    DOMAIN_SYSTEM_BUS,
    DOMAIN_CART_RAM,
    DOMAIN_COUNT,
} domain_t;

/* Same names and order as BizHawk's Gambatte core */
static const char *const domain_names[DOMAIN_COUNT] = {
    [DOMAIN_WRAM] = "WRAM",
    [DOMAIN_ROM] = "ROM",
    [DOMAIN_VRAM] = "VRAM",
    [DOMAIN_OAM] = "OAM",
    [DOMAIN_HRAM] = "HRAM",
    [DOMAIN_SYSTEM_BUS] = "System Bus",
    [DOMAIN_CART_RAM] = "CartRAM",
};

static const GB_direct_access_t domain_access[DOMAIN_COUNT] = {
    [DOMAIN_WRAM] = GB_DIRECT_ACCESS_RAM,
    [DOMAIN_ROM] = GB_DIRECT_ACCESS_ROM,
    [DOMAIN_VRAM] = GB_DIRECT_ACCESS_VRAM,
    [DOMAIN_OAM] = GB_DIRECT_ACCESS_OAM,
    [DOMAIN_HRAM] = GB_DIRECT_ACCESS_HRAM,
    [DOMAIN_CART_RAM] = GB_DIRECT_ACCESS_CART_RAM,
};

typedef enum {
    CALLBACK_FRAME_END,
    CALLBACK_FRAME_START,
    CALLBACK_EXECUTE,
    CALLBACK_EXIT,
    CALLBACK_OTHER, // Registered but never fired (e.g. onloadstate)
} callback_kind_t;

typedef struct {
    callback_kind_t kind;
    int ref;
    uint16_t address;
    bool active;
    char name[64];
    char guid[40];
} callback_t;

#define SHA1_DIGEST_LENGTH 20

typedef struct {
    GB_gameboy_t *gb;
    GB_lua_engine_t *engine;
    lua_State *L;
    lua_State *main_thread;
    int main_thread_ref;
    bool main_finished;
    bool main_waiting;
    bool stop_requested;
    unsigned depth;
    domain_t current_domain;
    uint64_t frame_count;
    char rom_hash[SHA1_DIGEST_LENGTH * 2 + 1];
    callback_t *callbacks;
    size_t callback_count;
    size_t callback_capacity;
    unsigned execute_callback_count;
    uint8_t execute_map[0x10000 / 8];
} lua_context_t;

struct GB_lua_engine_s {
    GB_gameboy_t *gb;
    GB_lua_engine_callbacks_t callbacks;
    void *user_data;
    lua_context_t *ctx;
    char *script_path;
    char *rom_name;
};

static void engine_log(GB_lua_engine_t *engine, const char *text, size_t length)
{
    if (engine->callbacks.log) {
        engine->callbacks.log(engine->user_data, text, length);
    }
}

static void engine_logf(GB_lua_engine_t *engine, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void engine_logf(GB_lua_engine_t *engine, const char *fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    int length = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (length < 0) return;
    if (length >= sizeof(buffer)) {
        length = sizeof(buffer) - 1;
    }
    engine_log(engine, buffer, length);
}

static const char *path_basename(const char *path)
{
    const char *name = path;
    for (const char *c = path; *c; c++) {
        if (*c == '/'
#ifdef _WIN32
            || *c == '\\'
#endif
            ) {
            name = c + 1;
        }
    }
    return name;
}

static lua_context_t *get_context(lua_State *L)
{
    return *(lua_context_t **)lua_getextraspace(L);
}

/* SHA-1 */

/* gameinfo.getromhash returns the SHA-1 of the ROM, as BizHawk does */
static void sha1(const uint8_t *data, size_t length, uint8_t digest[SHA1_DIGEST_LENGTH])
{
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t bit_length = (uint64_t)length * 8;
    size_t padded_length = ((length + 8) / 64 + 1) * 64;

    for (size_t offset = 0; offset < padded_length; offset += 64) {
        uint8_t block[64];
        for (unsigned i = 0; i < 64; i++) {
            size_t index = offset + i;
            if (index < length) {
                block[i] = data[index];
            }
            else if (index == length) {
                block[i] = 0x80;
            }
            else if (index >= padded_length - 8) {
                block[i] = bit_length >> (8 * (padded_length - 1 - index));
            }
            else {
                block[i] = 0;
            }
        }

        uint32_t w[80];
        for (unsigned i = 0; i < 16; i++) {
            w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
                   (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
        }
        for (unsigned i = 16; i < 80; i++) {
            uint32_t value = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = (value << 1) | (value >> 31);
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (unsigned i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            }
            else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            }
            else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            }
            else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    for (unsigned i = 0; i < SHA1_DIGEST_LENGTH; i++) {
        digest[i] = h[i / 4] >> (24 - 8 * (i % 4));
    }
}

/* Memory domains */

static size_t domain_size(lua_context_t *ctx, domain_t domain)
{
    if (domain == DOMAIN_SYSTEM_BUS) return 0x10000;
    size_t size = 0;
    uint16_t bank;
    GB_get_direct_access(ctx->gb, domain_access[domain], &size, &bank);
    return size;
}

static domain_t domain_for_name(lua_context_t *ctx, const char *name)
{
    for (domain_t i = 0; i < DOMAIN_COUNT; i++) {
        if (strcasecmp(name, domain_names[i]) == 0 && domain_size(ctx, i)) {
            return i;
        }
    }
    return DOMAIN_COUNT;
}

/* Memory functions are closures. The first upvalue is a fixed domain (for
   mainmemory.*) or -1 (for memory.*, where the domain is an optional argument
   at domain_arg). */
static domain_t check_domain(lua_State *L, int domain_arg)
{
    lua_context_t *ctx = get_context(L);
    lua_Integer fixed = lua_tointeger(L, lua_upvalueindex(1));
    if (fixed >= 0) return (domain_t)fixed;
    if (lua_isnoneornil(L, domain_arg)) return ctx->current_domain;
    const char *name = luaL_checkstring(L, domain_arg);
    domain_t domain = domain_for_name(ctx, name);
    if (domain == DOMAIN_COUNT) {
        luaL_error(L, "unknown memory domain \"%s\"", name);
    }
    return domain;
}

static uint8_t read_byte(lua_State *L, domain_t domain, lua_Integer address)
{
    lua_context_t *ctx = get_context(L);
    if (domain == DOMAIN_SYSTEM_BUS) {
        if (address < 0 || address > 0xFFFF) {
            luaL_error(L, "address 0x%X out of range for domain \"%s\"", (unsigned)address, domain_names[domain]);
        }
        return GB_safe_read_memory(ctx->gb, address);
    }
    size_t size = 0;
    uint16_t bank;
    uint8_t *data = GB_get_direct_access(ctx->gb, domain_access[domain], &size, &bank);
    if (!data || address < 0 || address >= size) {
        luaL_error(L, "address 0x%X out of range for domain \"%s\"", (unsigned)address, domain_names[domain]);
    }
    return data[address];
}

static void write_byte(lua_State *L, domain_t domain, lua_Integer address, uint8_t value)
{
    lua_context_t *ctx = get_context(L);
    if (domain == DOMAIN_SYSTEM_BUS) {
        if (address < 0 || address > 0xFFFF) {
            luaL_error(L, "address 0x%X out of range for domain \"%s\"", (unsigned)address, domain_names[domain]);
        }
        GB_write_memory(ctx->gb, address, value);
        return;
    }
    size_t size = 0;
    uint16_t bank;
    uint8_t *data = GB_get_direct_access(ctx->gb, domain_access[domain], &size, &bank);
    if (!data || address < 0 || address >= size) {
        luaL_error(L, "address 0x%X out of range for domain \"%s\"", (unsigned)address, domain_names[domain]);
    }
    data[address] = value;
}

/* Upvalues: 1 = fixed domain, 2 = size in bytes, 3 = big endian, 4 = signed */
static int mem_read_int(lua_State *L)
{
    lua_Integer address = luaL_checkinteger(L, 1);
    domain_t domain = check_domain(L, 2);
    unsigned size = (unsigned)lua_tointeger(L, lua_upvalueindex(2));
    bool big_endian = lua_toboolean(L, lua_upvalueindex(3));
    bool is_signed = lua_toboolean(L, lua_upvalueindex(4));

    uint32_t value = 0;
    for (unsigned i = 0; i < size; i++) {
        uint8_t byte = read_byte(L, domain, address + i);
        if (big_endian) {
            value = (value << 8) | byte;
        }
        else {
            value |= (uint32_t)byte << (8 * i);
        }
    }
    if (is_signed && size < 4 && (value & (1 << (size * 8 - 1)))) {
        lua_pushinteger(L, (lua_Integer)value - ((lua_Integer)1 << (size * 8)));
    }
    else if (is_signed && size == 4) {
        lua_pushinteger(L, (int32_t)value);
    }
    else {
        lua_pushinteger(L, value);
    }
    return 1;
}

static int mem_write_int(lua_State *L)
{
    lua_Integer address = luaL_checkinteger(L, 1);
    uint32_t value = (uint32_t)luaL_checkinteger(L, 2);
    domain_t domain = check_domain(L, 3);
    unsigned size = (unsigned)lua_tointeger(L, lua_upvalueindex(2));
    bool big_endian = lua_toboolean(L, lua_upvalueindex(3));

    for (unsigned i = 0; i < size; i++) {
        unsigned shift = big_endian? 8 * (size - 1 - i) : 8 * i;
        write_byte(L, domain, address + i, value >> shift);
    }
    return 0;
}

static int mem_read_float(lua_State *L)
{
    lua_Integer address = luaL_checkinteger(L, 1);
    bool big_endian = lua_toboolean(L, 2);
    domain_t domain = check_domain(L, 3);
    uint32_t bits = 0;
    for (unsigned i = 0; i < 4; i++) {
        uint8_t byte = read_byte(L, domain, address + i);
        bits |= (uint32_t)byte << (big_endian? 8 * (3 - i) : 8 * i);
    }
    float value;
    memcpy(&value, &bits, sizeof(value));
    lua_pushnumber(L, value);
    return 1;
}

static int mem_write_float(lua_State *L)
{
    lua_Integer address = luaL_checkinteger(L, 1);
    float value = luaL_checknumber(L, 2);
    bool big_endian = lua_toboolean(L, 3);
    domain_t domain = check_domain(L, 4);
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    for (unsigned i = 0; i < 4; i++) {
        write_byte(L, domain, address + i, bits >> (big_endian? 8 * (3 - i) : 8 * i));
    }
    return 0;
}

static int mem_read_bytes_as_array(lua_State *L)
{
    lua_Integer address = luaL_checkinteger(L, 1);
    lua_Integer length = luaL_checkinteger(L, 2);
    domain_t domain = check_domain(L, 3);
    lua_createtable(L, length > 0? (int)length : 0, 0);
    for (lua_Integer i = 0; i < length; i++) {
        lua_pushinteger(L, read_byte(L, domain, address + i));
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int mem_read_bytes_as_dict(lua_State *L)
{
    lua_Integer address = luaL_checkinteger(L, 1);
    lua_Integer length = luaL_checkinteger(L, 2);
    domain_t domain = check_domain(L, 3);
    lua_createtable(L, 0, length > 0? (int)length : 0);
    for (lua_Integer i = 0; i < length; i++) {
        lua_pushinteger(L, read_byte(L, domain, address + i));
        lua_rawseti(L, -2, address + i);
    }
    return 1;
}

static int mem_write_bytes_as_array(lua_State *L)
{
    lua_Integer address = luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    domain_t domain = check_domain(L, 3);
    lua_Integer length = luaL_len(L, 2);
    for (lua_Integer i = 0; i < length; i++) {
        lua_rawgeti(L, 2, i + 1);
        uint8_t value = (uint8_t)luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        write_byte(L, domain, address + i, value);
    }
    return 0;
}

static int mem_write_bytes_as_dict(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    domain_t domain = check_domain(L, 2);
    lua_pushnil(L);
    while (lua_next(L, 1)) {
        lua_Integer address = luaL_checkinteger(L, -2);
        uint8_t value = (uint8_t)luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        write_byte(L, domain, address, value);
    }
    return 0;
}

static int mem_get_domain_list(lua_State *L)
{
    lua_context_t *ctx = get_context(L);
    lua_newtable(L);
    unsigned index = 1;
    for (domain_t i = 0; i < DOMAIN_COUNT; i++) {
        if (!domain_size(ctx, i)) continue;
        lua_pushstring(L, domain_names[i]);
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

static int mem_get_domain_size(lua_State *L)
{
    lua_pushinteger(L, domain_size(get_context(L), check_domain(L, 1)));
    return 1;
}

static int mem_get_current_domain(lua_State *L)
{
    lua_Integer fixed = lua_tointeger(L, lua_upvalueindex(1));
    lua_pushstring(L, domain_names[fixed >= 0? fixed : get_context(L)->current_domain]);
    return 1;
}

static int mem_get_current_domain_size(lua_State *L)
{
    lua_context_t *ctx = get_context(L);
    lua_Integer fixed = lua_tointeger(L, lua_upvalueindex(1));
    lua_pushinteger(L, domain_size(ctx, fixed >= 0? (domain_t)fixed : ctx->current_domain));
    return 1;
}

static int mem_use_domain(lua_State *L)
{
    lua_context_t *ctx = get_context(L);
    domain_t domain = domain_for_name(ctx, luaL_checkstring(L, 1));
    if (domain == DOMAIN_COUNT) {
        lua_pushboolean(L, false);
        return 1;
    }
    ctx->current_domain = domain;
    lua_pushboolean(L, true);
    return 1;
}

static void push_memory_function(lua_State *L, lua_CFunction function, lua_Integer fixed_domain,
                                 unsigned size, bool big_endian, bool is_signed, const char *name)
{
    lua_pushinteger(L, fixed_domain);
    lua_pushinteger(L, size);
    lua_pushboolean(L, big_endian);
    lua_pushboolean(L, is_signed);
    lua_pushcclosure(L, function, 4);
    lua_setfield(L, -2, name);
}

static void push_memory_library(lua_State *L, lua_Integer fixed_domain)
{
    lua_newtable(L);

    static const struct {const char *name; unsigned size; bool big_endian; bool is_signed;} ints[] = {
        {"u8",     1, false, false}, {"s8",     1, false, true},
        {"u16_le", 2, false, false}, {"s16_le", 2, false, true},
        {"u16_be", 2, true,  false}, {"s16_be", 2, true,  true},
        {"u24_le", 3, false, false}, {"s24_le", 3, false, true},
        {"u24_be", 3, true,  false}, {"s24_be", 3, true,  true},
        {"u32_le", 4, false, false}, {"s32_le", 4, false, true},
        {"u32_be", 4, true,  false}, {"s32_be", 4, true,  true},
    };
    for (unsigned i = 0; i < sizeof(ints) / sizeof(ints[0]); i++) {
        char name[16];
        snprintf(name, sizeof(name), "read_%s", ints[i].name);
        push_memory_function(L, mem_read_int, fixed_domain, ints[i].size, ints[i].big_endian, ints[i].is_signed, name);
        snprintf(name, sizeof(name), "write_%s", ints[i].name);
        push_memory_function(L, mem_write_int, fixed_domain, ints[i].size, ints[i].big_endian, ints[i].is_signed, name);
    }
    push_memory_function(L, mem_read_int, fixed_domain, 1, false, false, "readbyte");
    push_memory_function(L, mem_write_int, fixed_domain, 1, false, false, "writebyte");
    push_memory_function(L, mem_read_float, fixed_domain, 4, false, false, "readfloat");
    push_memory_function(L, mem_write_float, fixed_domain, 4, false, false, "writefloat");
    push_memory_function(L, mem_read_bytes_as_array, fixed_domain, 1, false, false, "read_bytes_as_array");
    push_memory_function(L, mem_read_bytes_as_dict, fixed_domain, 1, false, false, "read_bytes_as_dict");
    push_memory_function(L, mem_write_bytes_as_array, fixed_domain, 1, false, false, "write_bytes_as_array");
    push_memory_function(L, mem_write_bytes_as_dict, fixed_domain, 1, false, false, "write_bytes_as_dict");
    push_memory_function(L, mem_get_current_domain, fixed_domain, 0, false, false,
                         fixed_domain >= 0? "getname" : "getcurrentmemorydomain");
    push_memory_function(L, mem_get_current_domain_size, fixed_domain, 0, false, false, "getcurrentmemorydomainsize");
    if (fixed_domain < 0) {
        push_memory_function(L, mem_get_domain_list, fixed_domain, 0, false, false, "getmemorydomainlist");
        push_memory_function(L, mem_get_domain_size, fixed_domain, 0, false, false, "getmemorydomainsize");
        push_memory_function(L, mem_use_domain, fixed_domain, 0, false, false, "usememorydomain");
    }
}

/* Callbacks */

static void update_execute_map(lua_context_t *ctx)
{
    memset(ctx->execute_map, 0, sizeof(ctx->execute_map));
    unsigned count = 0;
    for (size_t i = 0; i < ctx->callback_count; i++) {
        callback_t *callback = &ctx->callbacks[i];
        if (callback->active && callback->kind == CALLBACK_EXECUTE) {
            ctx->execute_map[callback->address >> 3] |= 1 << (callback->address & 7);
            count++;
        }
    }
    ctx->execute_callback_count = count;
    GB_set_execution_callback(ctx->gb, count? ctx->engine->callbacks.execution_callback : NULL);
}

/* Inactive callbacks are only removed while no callbacks are being iterated */
static void compact_callbacks(lua_context_t *ctx)
{
    size_t j = 0;
    for (size_t i = 0; i < ctx->callback_count; i++) {
        if (ctx->callbacks[i].active) {
            ctx->callbacks[j++] = ctx->callbacks[i];
        }
    }
    ctx->callback_count = j;
}

/* Callback IDs only need to be unique, so a simple PRNG is enough */
static uint64_t guid_random(void)
{
    static uint64_t state = 0;
    if (!state) {
        state = (uint64_t)time(NULL) ^ (uint64_t)(uintptr_t)&state ^ 0x9E3779B97F4A7C15;
    }
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

static void make_guid(char *out, size_t size)
{
    uint8_t bytes[16];
    uint64_t high = guid_random(), low = guid_random();
    for (unsigned i = 0; i < 8; i++) {
        bytes[i] = high >> (8 * i);
        bytes[i + 8] = low >> (8 * i);
    }
    bytes[6] = (bytes[6] & 0x0F) | 0x40; // Version 4
    bytes[8] = (bytes[8] & 0x3F) | 0x80; // RFC 4122 variant
    snprintf(out, size, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
             bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
}

static int register_callback(lua_State *L, callback_kind_t kind, int name_arg, uint16_t address)
{
    lua_context_t *ctx = get_context(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);
    const char *name = luaL_optstring(L, name_arg, "");

    if (ctx->callback_count == ctx->callback_capacity) {
        ctx->callback_capacity = ctx->callback_capacity? ctx->callback_capacity * 2 : 16;
        ctx->callbacks = realloc(ctx->callbacks, sizeof(*ctx->callbacks) * ctx->callback_capacity);
    }
    callback_t *callback = &ctx->callbacks[ctx->callback_count++];
    memset(callback, 0, sizeof(*callback));
    callback->kind = kind;
    callback->address = address;
    callback->active = true;
    snprintf(callback->name, sizeof(callback->name), "%s", name);
    make_guid(callback->guid, sizeof(callback->guid));
    lua_pushvalue(L, 1);
    callback->ref = luaL_ref(L, LUA_REGISTRYINDEX);

    if (kind == CALLBACK_EXECUTE) {
        update_execute_map(ctx);
    }

    lua_pushstring(L, callback->guid);
    return 1;
}

static int event_onframeend(lua_State *L)
{
    return register_callback(L, CALLBACK_FRAME_END, 2, 0);
}

static int event_onframestart(lua_State *L)
{
    return register_callback(L, CALLBACK_FRAME_START, 2, 0);
}

static int event_onexit(lua_State *L)
{
    return register_callback(L, CALLBACK_EXIT, 2, 0);
}

static int event_unsupported(lua_State *L)
{
    return register_callback(L, CALLBACK_OTHER, 2, 0);
}

static int event_onmemoryexecute(lua_State *L)
{
    lua_Integer address = luaL_checkinteger(L, 2);
    if (!lua_isnoneornil(L, 4)) {
        const char *domain = luaL_checkstring(L, 4);
        if (strcasecmp(domain, "System Bus") != 0) {
            return luaL_error(L, "onmemoryexecute is only supported for the \"System Bus\" domain");
        }
    }
    if (address < 0 || address > 0xFFFF) {
        return luaL_error(L, "address 0x%X out of range", (unsigned)address);
    }
    return register_callback(L, CALLBACK_EXECUTE, 3, address);
}

static int event_unsupported_memory(lua_State *L)
{
    lua_context_t *ctx = get_context(L);
    engine_logf(ctx->engine, "Warning: memory read/write callbacks are not supported by SameBoy, the callback will never be called.\n");
    return register_callback(L, CALLBACK_OTHER, 3, 0);
}

static int unregister_matching(lua_State *L, bool by_name)
{
    lua_context_t *ctx = get_context(L);
    const char *key = luaL_checkstring(L, 1);
    bool found = false;
    bool execute_changed = false;
    for (size_t i = 0; i < ctx->callback_count; i++) {
        callback_t *callback = &ctx->callbacks[i];
        if (!callback->active) continue;
        if (strcmp(by_name? callback->name : callback->guid, key) != 0) continue;
        callback->active = false;
        luaL_unref(L, LUA_REGISTRYINDEX, callback->ref);
        execute_changed |= callback->kind == CALLBACK_EXECUTE;
        found = true;
    }
    if (execute_changed) {
        update_execute_map(ctx);
    }
    lua_pushboolean(L, found);
    return 1;
}

static int event_unregisterbyid(lua_State *L)
{
    return unregister_matching(L, false);
}

static int event_unregisterbyname(lua_State *L)
{
    return unregister_matching(L, true);
}

static int traceback_handler(lua_State *L)
{
    const char *message = lua_tostring(L, 1);
    if (!message) {
        message = luaL_tolstring(L, 1, NULL);
    }
    luaL_traceback(L, L, message, 1);
    return 1;
}

static void report_error(lua_context_t *ctx, const char *message)
{
    GB_lua_engine_t *engine = ctx->engine;
    if (!message) {
        message = "(unknown error)";
    }
    if (engine->callbacks.error) {
        engine->callbacks.error(engine->user_data, message);
        return;
    }
    engine_log(engine, "\nERROR: ", 8);
    engine_log(engine, message, strlen(message));
    engine_log(engine, "\n", 1);
}

/* Calls every active callback of the given kind (or, for execute callbacks,
   at the given address). Returns false if a callback raised an error. */
static bool dispatch_callbacks(lua_context_t *ctx, callback_kind_t kind, uint16_t address, uint8_t opcode)
{
    lua_State *L = ctx->L;
    size_t count = ctx->callback_count;
    bool ok = true;
    ctx->depth++;
    for (size_t i = 0; i < count && i < ctx->callback_count; i++) {
        if (!ctx->callbacks[i].active || ctx->callbacks[i].kind != kind) continue;
        if (kind == CALLBACK_EXECUTE && ctx->callbacks[i].address != address) continue;

        lua_pushcfunction(L, traceback_handler);
        lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->callbacks[i].ref);
        int nargs = 0;
        if (kind == CALLBACK_EXECUTE) {
            lua_pushinteger(L, address);
            lua_pushinteger(L, opcode);
            lua_pushinteger(L, 0);
            nargs = 3;
        }
        if (lua_pcall(L, nargs, 0, -2 - nargs) != LUA_OK) {
            report_error(ctx, lua_tostring(L, -1));
            lua_pop(L, 2);
            ok = false;
            ctx->stop_requested = true;
            break;
        }
        lua_pop(L, 1);
    }
    ctx->depth--;
    if (!ctx->depth) {
        compact_callbacks(ctx);
    }
    return ok;
}

/* Base functions */

static void push_joined_arguments(lua_State *L, const char *separator)
{
    luaL_Buffer buffer;
    int count = lua_gettop(L);
    luaL_buffinit(L, &buffer);
    for (int i = 1; i <= count; i++) {
        if (i > 1) {
            luaL_addstring(&buffer, separator);
        }
        luaL_tolstring(L, i, NULL);
        luaL_addvalue(&buffer);
    }
    luaL_pushresult(&buffer);
}

static void log_top_string(lua_State *L, bool newline)
{
    if (newline) {
        lua_pushliteral(L, "\n");
        lua_concat(L, 2);
    }
    size_t length;
    const char *string = lua_tolstring(L, -1, &length);
    engine_log(get_context(L)->engine, string, length);
}

static int base_print(lua_State *L)
{
    push_joined_arguments(L, " ");
    log_top_string(L, true);
    return 0;
}

static int console_write(lua_State *L)
{
    push_joined_arguments(L, " ");
    log_top_string(L, false);
    return 0;
}

static int console_clear(lua_State *L)
{
    GB_lua_engine_t *engine = get_context(L)->engine;
    if (engine->callbacks.clear) {
        engine->callbacks.clear(engine->user_data);
    }
    return 0;
}

static int gui_addmessage(lua_State *L)
{
    push_joined_arguments(L, " ");
    GB_lua_engine_t *engine = get_context(L)->engine;
    if (engine->callbacks.message) {
        engine->callbacks.message(engine->user_data, lua_tostring(L, -1));
    }
    return 0;
}

static int emu_frameadvance(lua_State *L)
{
    lua_context_t *ctx = get_context(L);
    if (!lua_isyieldable(L)) {
        return luaL_error(L, "emu.frameadvance() and emu.yield() can't be called from a callback");
    }
    if (L == ctx->main_thread) {
        ctx->main_waiting = true;
    }
    return lua_yield(L, 0);
}

static int emu_framecount(lua_State *L)
{
    lua_pushinteger(L, get_context(L)->frame_count);
    return 1;
}

static int emu_getsystemid(lua_State *L)
{
    lua_context_t *ctx = get_context(L);
    if (GB_is_sgb(ctx->gb)) {
        lua_pushstring(L, "SGB");
        return 1;
    }
    size_t size = 0;
    uint16_t bank;
    uint8_t *rom = GB_get_direct_access(ctx->gb, GB_DIRECT_ACCESS_ROM, &size, &bank);
    bool cgb_game = rom && size > 0x143 && (rom[0x143] & 0x80);
    lua_pushstring(L, (GB_is_cgb(ctx->gb) && cgb_game)? "GBC" : "GB");
    return 1;
}

static const char *const register_names_8[] = {"A", "F", "B", "C", "D", "E", "H", "L"};
static const char *const register_names_16[] = {"AF", "BC", "DE", "HL", "SP", "PC"};

static int emu_getregister(lua_State *L)
{
    lua_context_t *ctx = get_context(L);
    const char *name = luaL_checkstring(L, 1);
    GB_registers_t *registers = GB_get_registers(ctx->gb);
    for (unsigned i = 0; i < sizeof(register_names_16) / sizeof(register_names_16[0]); i++) {
        if (strcasecmp(name, register_names_16[i]) == 0) {
            lua_pushinteger(L, registers->registers[i]);
            return 1;
        }
    }
    for (unsigned i = 0; i < sizeof(register_names_8) / sizeof(register_names_8[0]); i++) {
        if (strcasecmp(name, register_names_8[i]) == 0) {
            uint16_t pair = registers->registers[i / 2];
            lua_pushinteger(L, (i & 1)? (pair & 0xFF) : (pair >> 8));
            return 1;
        }
    }
    return luaL_error(L, "unknown register \"%s\"", name);
}

static int emu_getregisters(lua_State *L)
{
    lua_context_t *ctx = get_context(L);
    GB_registers_t *registers = GB_get_registers(ctx->gb);
    lua_newtable(L);
    for (unsigned i = 0; i < sizeof(register_names_16) / sizeof(register_names_16[0]); i++) {
        lua_pushinteger(L, registers->registers[i]);
        lua_setfield(L, -2, register_names_16[i]);
    }
    for (unsigned i = 0; i < sizeof(register_names_8) / sizeof(register_names_8[0]); i++) {
        uint16_t pair = registers->registers[i / 2];
        lua_pushinteger(L, (i & 1)? (pair & 0xFF) : (pair >> 8));
        lua_setfield(L, -2, register_names_8[i]);
    }
    return 1;
}

static int emu_setregister(lua_State *L)
{
    lua_context_t *ctx = get_context(L);
    const char *name = luaL_checkstring(L, 1);
    lua_Integer value = luaL_checkinteger(L, 2);
    GB_registers_t *registers = GB_get_registers(ctx->gb);
    for (unsigned i = 0; i < sizeof(register_names_16) / sizeof(register_names_16[0]); i++) {
        if (strcasecmp(name, register_names_16[i]) == 0) {
            registers->registers[i] = value;
            return 0;
        }
    }
    for (unsigned i = 0; i < sizeof(register_names_8) / sizeof(register_names_8[0]); i++) {
        if (strcasecmp(name, register_names_8[i]) == 0) {
            uint16_t *pair = &registers->registers[i / 2];
            if (i & 1) {
                *pair = (*pair & 0xFF00) | (value & 0xFF);
            }
            else {
                *pair = (*pair & 0x00FF) | ((value & 0xFF) << 8);
            }
            return 0;
        }
    }
    return luaL_error(L, "unknown register \"%s\"", name);
}

static int gameinfo_getromhash(lua_State *L)
{
    lua_pushstring(L, get_context(L)->rom_hash);
    return 1;
}

static int gameinfo_getromname(lua_State *L)
{
    lua_context_t *ctx = get_context(L);
    const char *name = ctx->engine->rom_name;
    if (name && name[0]) {
        lua_pushstring(L, name);
        return 1;
    }
    char title[17];
    GB_get_rom_title(ctx->gb, title);
    lua_pushstring(L, title);
    return 1;
}

static int socket_loader(lua_State *L)
{
    /* Archipelago's socket.lua wrapper exposes LuaSocket's core as socket.socket,
       while plain LuaSocket exposes it directly. Support both. */
    luaL_requiref(L, "socket.core", luaopen_socket_core, false);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "socket");
    return 1;
}

/* Pure Lua parts of the API, mostly BizHawk-specific helpers and stubs */
static const char prelude[] =
"local bit = {}\n"
"_G.bit = bit\n"
"function bit.band(...) local r = 0xFFFFFFFF for _, v in ipairs({...}) do r = r & v end return r & 0xFFFFFFFF end\n"
"function bit.bor(...) local r = 0 for _, v in ipairs({...}) do r = r | v end return r & 0xFFFFFFFF end\n"
"function bit.bxor(...) local r = 0 for _, v in ipairs({...}) do r = r ~ v end return r & 0xFFFFFFFF end\n"
"function bit.bnot(a) return ~a & 0xFFFFFFFF end\n"
"function bit.lshift(a, n) return (a << n) & 0xFFFFFFFF end\n"
"function bit.rshift(a, n) return (a & 0xFFFFFFFF) >> n end\n"
"function bit.arshift(a, n) a = a & 0xFFFFFFFF if a & 0x80000000 == 0 then return a >> n end return ((a >> n) | ~(0xFFFFFFFF >> n)) & 0xFFFFFFFF end\n"
"function bit.rol(a, n) a = a & 0xFFFFFFFF n = n & 31 return ((a << n) | (a >> (32 - n))) & 0xFFFFFFFF end\n"
"function bit.ror(a, n) a = a & 0xFFFFFFFF n = n & 31 return ((a >> n) | (a << (32 - n))) & 0xFFFFFFFF end\n"
"function bit.check(a, n) return (a >> n) & 1 == 1 end\n"
"function bit.set(a, n) return a | (1 << n) end\n"
"function bit.clear(a, n) return a & ~(1 << n) end\n"
"function bit.byteswap_16(a) return ((a & 0xFF) << 8) | ((a >> 8) & 0xFF) end\n"
"function bit.byteswap_32(a) return ((a & 0xFF) << 24) | ((a & 0xFF00) << 8) | ((a >> 8) & 0xFF00) | ((a >> 24) & 0xFF) end\n"
"local function enumerable(t)\n"
"  local keys = {}\n"
"  for k in pairs(t) do keys[#keys + 1] = k end\n"
"  return {GetEnumerator = function()\n"
"    local i = 0\n"
"    local e = {}\n"
"    function e:MoveNext() i = i + 1 e.Current = keys[i] return keys[i] ~= nil end\n"
"    return e\n"
"  end}\n"
"end\n"
"local cores = {GB = 'Gambatte', GBC = 'Gambatte', SGB = 'Gambatte'}\n"
"setmetatable(cores, {__index = {Keys = enumerable({GB = true, GBC = true, SGB = true})}})\n"
"function client.getconfig() return {PreferredCores = cores} end\n"
"function client.getversion() return '2.11' end\n"
"function client.emulatorname() return 'SameBoy' end\n"
"function client.ispaused() return false end\n"
"function client.pause() end\n"
"function client.unpause() end\n"
"function client.screenwidth() return 160 end\n"
"function client.screenheight() return 144 end\n"
"function emu.getluacore() return 'Lua+LuaInterface' end\n"
"function emu.limitframerate() end\n"
"function emu.displayvsync() end\n"
"function emu.minimizeframeskip() end\n"
"function emu.setrenderplanes() end\n"
"function emu.lagcount() return 0 end\n"
"function emu.islagged() return false end\n"
"function emu.getboardname() return '' end\n"
"function gameinfo.getstatus() return 'Unknown' end\n"
"function gameinfo.indatabase() return false end\n"
"function gameinfo.getboardtype() return '' end\n"
"function gameinfo.getoptions() return {} end\n"
"for _, name in ipairs({'text', 'pixelText', 'drawText', 'drawString', 'drawRectangle', 'drawBox', 'drawLine',\n"
"                       'drawPixel', 'drawEllipse', 'drawPolygon', 'drawImage', 'drawImageRegion', 'drawIcon',\n"
"                       'drawBezier', 'drawPie', 'drawArc', 'drawAxis', 'clearGraphics', 'cleartext',\n"
"                       'clearImageCache', 'defaultForeground', 'defaultBackground', 'defaultTextBackground',\n"
"                       'defaultPixelFont', 'use_surface', 'DrawNew', 'DrawFinish'}) do\n"
"  gui[name] = function() end\n"
"end\n"
"joypad = {get = function() return {} end, getimmediate = function() return {} end, set = function() end,\n"
"          setanalog = function() end, getwithmovie = function() return {} end}\n"
"savestate = {}\n"
"function savestate.save() print('Warning: savestate.save is not supported by SameBoy') end\n"
"function savestate.load() print('Warning: savestate.load is not supported by SameBoy') end\n"
"function savestate.saveslot() print('Warning: savestate.saveslot is not supported by SameBoy') end\n"
"function savestate.loadslot() print('Warning: savestate.loadslot is not supported by SameBoy') end\n"
"console.writeline = print\n";


/* Engine */

GB_lua_engine_t *GB_lua_engine_create(GB_gameboy_t *gb, const GB_lua_engine_callbacks_t *callbacks, void *user_data)
{
    GB_lua_engine_t *engine = calloc(1, sizeof(*engine));
    engine->gb = gb;
    if (callbacks) {
        engine->callbacks = *callbacks;
    }
    engine->user_data = user_data;
    return engine;
}

void GB_lua_engine_free(GB_lua_engine_t *engine)
{
    if (!engine) return;
    GB_lua_engine_stop_script(engine);
    free(engine->script_path);
    free(engine->rom_name);
    free(engine);
}

void GB_lua_engine_set_rom_name(GB_lua_engine_t *engine, const char *name)
{
    free(engine->rom_name);
    engine->rom_name = name? strdup(name) : NULL;
}

const char *GB_lua_engine_get_script_path(GB_lua_engine_t *engine)
{
    return engine->script_path;
}

bool GB_lua_engine_is_running(GB_lua_engine_t *engine)
{
    return engine->ctx != NULL;
}

static void compute_rom_hash(lua_context_t *ctx)
{
    size_t size = 0;
    uint16_t bank;
    uint8_t *rom = GB_get_direct_access(ctx->gb, GB_DIRECT_ACCESS_ROM, &size, &bank);
    uint8_t digest[SHA1_DIGEST_LENGTH];
    sha1(rom, rom? size : 0, digest);
    for (unsigned i = 0; i < SHA1_DIGEST_LENGTH; i++) {
        snprintf(ctx->rom_hash + i * 2, 3, "%02X", digest[i]);
    }
}

static void register_functions(lua_State *L, const char *library, const luaL_Reg *functions)
{
    lua_getglobal(L, library);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, library);
    }
    luaL_setfuncs(L, functions, 0);
    lua_pop(L, 1);
}

static void set_up_libraries(lua_context_t *ctx, const char *script_directory)
{
    lua_State *L = ctx->L;
    luaL_openlibs(L);

    lua_pushcfunction(L, base_print);
    lua_setglobal(L, "print");

    register_functions(L, "console", (const luaL_Reg[]){
        {"log", base_print},
        {"write", console_write},
        {"clear", console_clear},
        {NULL, NULL}
    });

    register_functions(L, "emu", (const luaL_Reg[]){
        {"frameadvance", emu_frameadvance},
        {"yield", emu_frameadvance},
        {"framecount", emu_framecount},
        {"getsystemid", emu_getsystemid},
        {"getregister", emu_getregister},
        {"getregisters", emu_getregisters},
        {"setregister", emu_setregister},
        {NULL, NULL}
    });

    register_functions(L, "gameinfo", (const luaL_Reg[]){
        {"getromhash", gameinfo_getromhash},
        {"getromname", gameinfo_getromname},
        {NULL, NULL}
    });

    register_functions(L, "event", (const luaL_Reg[]){
        {"onframeend", event_onframeend},
        {"onframestart", event_onframestart},
        {"onexit", event_onexit},
        {"onmemoryexecute", event_onmemoryexecute},
        {"onmemoryread", event_unsupported_memory},
        {"onmemorywrite", event_unsupported_memory},
        {"onloadstate", event_unsupported},
        {"onsavestate", event_unsupported},
        {"oninputpoll", event_unsupported},
        {"onconsoleclose", event_unsupported},
        {"unregisterbyid", event_unregisterbyid},
        {"unregisterbyname", event_unregisterbyname},
        {NULL, NULL}
    });

    register_functions(L, "gui", (const luaL_Reg[]){
        {"addmessage", gui_addmessage},
        {NULL, NULL}
    });

    register_functions(L, "client", (const luaL_Reg[]){
        {NULL, NULL}
    });

    push_memory_library(L, -1);
    lua_setglobal(L, "memory");
    push_memory_library(L, DOMAIN_WRAM);
    lua_setglobal(L, "mainmemory");

    /* Modules: LuaSocket is built in, other modules are looked up next to the script */
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "preload");
    lua_pushcfunction(L, luaopen_socket_core);
    lua_setfield(L, -2, "socket.core");
    lua_pushcfunction(L, socket_loader);
    lua_setfield(L, -2, "socket");
    lua_pop(L, 1);

    lua_getfield(L, -1, "path");
    lua_pushfstring(L, "%s/?.lua;%s/?/init.lua;%s", script_directory, script_directory, lua_tostring(L, -1));
    lua_setfield(L, -3, "path");
    lua_pop(L, 2);

    if (luaL_dostring(L, prelude) != LUA_OK) {
        report_error(ctx, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

static void notify_state_changed(GB_lua_engine_t *engine)
{
    if (engine->callbacks.state_changed) {
        engine->callbacks.state_changed(engine->user_data);
    }
}

static void resume_main_thread(GB_lua_engine_t *engine)
{
    lua_context_t *ctx = engine->ctx;
    if (!ctx || ctx->main_finished) return;
    ctx->main_waiting = false;
    ctx->depth++;
    int results = 0;
    int status = lua_resume(ctx->main_thread, ctx->L, 0, &results);
    ctx->depth--;
    if (status == LUA_YIELD) {
        lua_pop(ctx->main_thread, results);
        ctx->main_waiting = true;
        return;
    }
    ctx->main_finished = true;
    if (status != LUA_OK) {
        lua_State *thread = ctx->main_thread;
        const char *message = lua_tostring(thread, -1);
        luaL_traceback(ctx->L, thread, message, 0);
        report_error(ctx, lua_tostring(ctx->L, -1));
        lua_pop(ctx->L, 1);
        ctx->stop_requested = true;
        return;
    }

    bool has_callbacks = false;
    for (size_t i = 0; i < ctx->callback_count; i++) {
        if (ctx->callbacks[i].active && ctx->callbacks[i].kind != CALLBACK_EXIT) {
            has_callbacks = true;
            break;
        }
    }
    if (!has_callbacks) {
        engine_logf(engine, "Script finished\n");
        ctx->stop_requested = true;
    }
}

static void handle_pending_stop(GB_lua_engine_t *engine)
{
    if (engine->ctx && engine->ctx->stop_requested && !engine->ctx->depth) {
        GB_lua_engine_stop_script(engine);
    }
}

bool GB_lua_engine_start_script(GB_lua_engine_t *engine, const char *path)
{
    GB_lua_engine_stop_script(engine);

    char *new_path = strdup(path);
    free(engine->script_path);
    engine->script_path = new_path;

    lua_context_t *ctx = engine->ctx = calloc(1, sizeof(*ctx));
    ctx->gb = engine->gb;
    ctx->engine = engine;
    ctx->current_domain = DOMAIN_WRAM;
    ctx->L = luaL_newstate();
    *(lua_context_t **)lua_getextraspace(ctx->L) = ctx;
    compute_rom_hash(ctx);

    const char *name = path_basename(path);
    char *directory = strdup(path);
    if (name == path) {
        strcpy(directory, ".");
    }
    else {
        directory[name - path - 1] = 0;
    }
    set_up_libraries(ctx, directory);
    free(directory);

    lua_State *L = ctx->L;
    ctx->main_thread = lua_newthread(L);
    ctx->main_thread_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    engine_logf(engine, "Starting %s\n", name);
    if (luaL_loadfile(ctx->main_thread, path) != LUA_OK) {
        report_error(ctx, lua_tostring(ctx->main_thread, -1));
        GB_lua_engine_stop_script(engine);
        return false;
    }
    notify_state_changed(engine);
    resume_main_thread(engine);
    handle_pending_stop(engine);
    return engine->ctx != NULL;
}

void GB_lua_engine_stop_script(GB_lua_engine_t *engine)
{
    lua_context_t *ctx = engine->ctx;
    if (!ctx) return;
    if (ctx->depth) {
        ctx->stop_requested = true;
        return;
    }
    dispatch_callbacks(ctx, CALLBACK_EXIT, 0, 0);
    GB_set_execution_callback(engine->gb, NULL);
    lua_close(ctx->L);
    free(ctx->callbacks);
    free(ctx);
    engine->ctx = NULL;
    engine_logf(engine, "Stopped %s\n", engine->script_path? path_basename(engine->script_path) : "script");
    notify_state_changed(engine);
}

void GB_lua_engine_frame_ended(GB_lua_engine_t *engine)
{
    lua_context_t *ctx = engine->ctx;
    if (!ctx) return;
    ctx->frame_count++;
    dispatch_callbacks(ctx, CALLBACK_FRAME_END, 0, 0);
    if (!ctx->stop_requested && ctx->main_waiting) {
        resume_main_thread(engine);
    }
    if (!ctx->stop_requested) {
        dispatch_callbacks(ctx, CALLBACK_FRAME_START, 0, 0);
    }
    handle_pending_stop(engine);
}

void GB_lua_engine_executed_address(GB_lua_engine_t *engine, uint16_t address, uint8_t opcode)
{
    lua_context_t *ctx = engine->ctx;
    if (!ctx || !(ctx->execute_map[address >> 3] & (1 << (address & 7)))) return;
    dispatch_callbacks(ctx, CALLBACK_EXECUTE, address, opcode);
    handle_pending_stop(engine);
}

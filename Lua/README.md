# Lua scripting

SameBoy can run Lua scripts written for BizHawk's Game Boy cores:

- Cocoa: Lua > Open Lua Script…, with output in the Lua Console window.
- SDL (Linux, Windows, etc.): Lua Scripting in the menu, or `sameboy --lua <script> <rom>`.
  Output goes to the terminal or debugger console; on Windows, a console window opens
  when a script starts.

The engine is portable C, shared by both frontends (`lua_engine.c`, with a thin
Objective-C wrapper in `Cocoa/GBLuaEngine.m`). It is built on two Git submodules,
used unmodified:

- `lua/` – [Lua](https://github.com/lua/lua) 5.4.7 (tag `v5.4.7`), Copyright © 1994–2024 Lua.org, PUC-Rio, MIT license.
- `luasocket/` – [LuaSocket](https://github.com/lunarmodules/luasocket) 3.1.0 (tag `v3.1.0`), Copyright © 1999–2013 Diego Nehab, MIT license.

Lua scripting is optional. It's built automatically when both submodules are
present (run `git submodule update --init` if these directories are empty), and
can be turned off with `make DISABLE_LUA=1`. When disabled, `GB_DISABLE_LUA` is
defined, `lua_engine.h` provides no-op stubs, and the frontends hide their Lua
menus. The Makefile builds only the library sources it needs, choosing
LuaSocket's `usocket.c` or `wsocket.c` depending on the platform.

`require("socket")` and `require("socket.core")` resolve to the built-in
LuaSocket, so scripts that ship BizHawk's Windows/Linux socket binaries (such
as Archipelago's `socket.lua` wrapper) work without them. Other modules are
loaded from the script's own directory.

## Supported API

- `print`, `console.log/write/writeline/clear`
- `emu.frameadvance/yield/framecount/getsystemid/getregister(s)/setregister`
- `memory.*` and `mainmemory.*`: `readbyte`/`writebyte`, `read_`/`write_` `u8 s8 u16 s16 u24 s24 u32 s32` (`_le`/`_be`),
  `readfloat`/`writefloat`, `read_bytes_as_array/dict`, `write_bytes_as_array/dict`,
  `getmemorydomainlist/size`, `getcurrentmemorydomain(size)`, `usememorydomain`
- Memory domains (same as BizHawk's Gambatte core): `WRAM`, `ROM`, `VRAM`, `OAM`, `HRAM`, `System Bus`, `CartRAM`
- `event.onframeend/onframestart/onexit/onmemoryexecute` (System Bus), `event.unregisterbyid/byname`
- `gui.addmessage` (shown on the OSD), `gameinfo.getromhash` (SHA-1, as BizHawk), `gameinfo.getromname`
- `client.getversion` (reports `2.11`), `client.getconfig().PreferredCores`
- `bit.*` (BizHawk's bit library)

Not supported (stubs that do nothing): `gui` drawing functions, `joypad`,
`savestate`, and `event.onmemoryread/onmemorywrite` (prints a warning).

## Licenses

Lua: Copyright © 1994–2024 Lua.org, PUC-Rio.
LuaSocket: Copyright © 1999–2013 Diego Nehab.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

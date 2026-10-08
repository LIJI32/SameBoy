# SameBoy Android — M8: Link Cable (Design)

Status: **proposed** · Date: 2026-07-01 · Branch: `android-frontend`
Builds on M1–M7. Parent: `plans/2026-07-01-android-parity-roadmap.md` (M8).

## 1. Goal & scope

Connect two SameBoy instances over the network so their emulated Game Boys trade serial
data (Pokémon trade, link battles, etc.), with graceful connect/disconnect.

**This slice: TCP over the local network.** The roadmap flags M8 as the largest subsystem
and explicitly allows sub-slicing; **Bluetooth RFCOMM is out of scope** here (a later slice)
and the transport is abstracted so it can be added without touching the clocking bridge.

Reused Core APIs (unmodified), all in `gb.h`/`memory.h`:
- Master (internal clock): `GB_set_serial_transfer_bit_start_callback`,
  `GB_set_serial_transfer_bit_end_callback`.
- Slave (external clock): `GB_serial_set_data_bit`, `GB_serial_get_data_bit`.
- `GB_disconnect_serial`; `GB_safe_read_memory(gb, 0xFF01/0xFF02)` to read SB/SC.

## 2. The clocking model (the hard part)

The Game Boy link is a **master-clocked byte exchange**. Exactly one side owns the clock
per transfer (the game that writes `SC` bit0=1). Core drives it **bit by bit**, interleaved
`start(b7), end(r7), start(b6), end(r6), …`. A network cannot answer `r7` in the ~122 µs of
one bit period, so we exchange at the **byte** level and block once per byte:

**Master path** (our Core has internal clock; `bit_start`/`bit_end` fire on the emu thread):
- The Core fires `bit_start` 8× and `bit_end` 8× per byte, interleaved. At the **first**
  `bit_start` of a byte (tracked by a bit counter == 0), `SB` still holds the whole outgoing
  byte → read it with `GB_safe_read_memory(gb, 0xFF01)`.
- `exchange(out_byte) → in_byte`: **send** `out_byte`, then **blocking-recv** the peer byte
  (timeout → 0xFF, so a vanished peer never hangs the emu thread).
- Each of the 8 `bit_end` returns yields `(in_byte >> (7 - i)) & 1`, i = 0..7. Core does
  `SB = (SB<<1) | bit`, so after 8 bits `SB == in_byte` and the serial interrupt fires —
  exactly as hardware.

**Slave path** (our Core has external clock, `SC=0x80`; no callbacks fire):
- Polled on the emu thread once per frame (from `sb_emu_run_frame`, after `GB_run_frame`,
  like the M7 camera drain). If armed — `(GB_safe_read_memory(gb,0xFF02) & 0x81) == 0x80` —
  do a **non-blocking** `recv`. If the master's byte arrived: read the slave's outgoing byte
  (`GB_safe_read_memory(gb,0xFF01)`), **send** it back, then clock the Core with the master
  byte via `GB_serial_set_data_bit(gb, bit)` ×8 (MSB-first) — the 8th sets the interrupt and
  leaves `SB == master_byte`.

**Ordering:** master `send`→ slave `recv` → slave `send` → master `recv`. One master per
byte ⇒ no deadlock. The master blocks per **byte** (not per bit); the slave never blocks
(per-frame non-blocking poll). Throughput is bounded by the slave's per-frame poll (~16 ms/
byte) — correct but not fast; a faster slave pump is a documented later optimization.

**Threading:** connect/listen/accept block → run on a **native helper thread**, not the emu
thread. It sets an atomic status (`IDLE/LISTENING/CONNECTING/CONNECTED/ERROR`) + the socket
fd. The emu thread touches the fd only while `CONNECTED`. Disconnect shuts the socket down
(unblocks any recv) and closes it.

## 3. Native design

New files `Android/jni/link.c` + `link.h`.

### Transport abstraction (lets tests bypass sockets; Bluetooth later)
```c
typedef struct sb_transport sb_transport;
struct sb_transport {
    /* send one byte; returns true on success. */
    bool (*send)(sb_transport *t, uint8_t byte);
    /* recv one byte into *out; block up to timeout_ms (0 = non-blocking poll).
       returns true if a byte was read, false on timeout/closed. */
    bool (*recv)(sb_transport *t, uint8_t *out, int timeout_ms);
    void (*close)(sb_transport *t);
};
sb_transport *sb_transport_tcp_listen(int port);        /* blocks for one peer; NULL on error */
sb_transport *sb_transport_tcp_connect(const char *host, int port);  /* blocks; NULL on error */
sb_transport *sb_transport_loopback_pair(sb_transport **other);      /* test: two ends of a queue */
```
TCP: `AF_INET` stream socket, `TCP_NODELAY`, `SO_RCVTIMEO` per-recv via `setsockopt` or
`poll()`; one length-1 byte per `send`/`recv`. Loopback: two `sb_transport`s over a shared
mutex+cond byte FIFO (for host tests, no sockets).

### `sb_link` (owns the transport + bridges Core)
```c
typedef struct sb_link sb_link;
sb_link *sb_link_create(sb_transport *t);   /* takes ownership of t */
void     sb_link_destroy(sb_link *s);       /* closes transport */
/* Register on a GB as master callbacks + remember gb for the slave poll. */
void     sb_link_attach(sb_link *s, GB_gameboy_t *gb);
void     sb_link_detach(sb_link *s, GB_gameboy_t *gb);   /* GB_disconnect_serial */
/* Slave poll: call each frame on the emu thread; no-op unless externally clocked + byte ready. */
void     sb_link_slave_poll(sb_link *s, GB_gameboy_t *gb);
```
- `bit_start_cb`: if `bits == 0` → `out = GB_safe_read_memory(gb,0xFF01)`; `in =
  exchange(out)`; store `in`, reset `bits`. (Bridge holds `sb_link` via `GB_get_user_data`?
  No — user_data is the `sb_emulator`. The link stores its own `gb→link` association: keep a
  single `sb_link *` pointer in `sb_emulator` and route callbacks through small emulator.c
  trampolines that fetch it.)
- `bit_end_cb`: `bit = (in >> (7 - bits)) & 1; bits = (bits+1) & 7; return bit;`
- `exchange(out)`: `t->send(out)`; if `!t->recv(&in, LINK_TIMEOUT_MS) → in = 0xFF`.
- `slave_poll`: `if ((GB_safe_read_memory(gb,0xFF02)&0x81)!=0x80) return;` non-blocking
  `t->recv(&m,0)`; if got it: `out=GB_safe_read_memory(gb,0xFF01); t->send(out);` then
  `for i in 0..7: GB_serial_set_data_bit(gb,(m>>(7-i))&1);`.

### `emulator.c` glue
- Field `sb_link *link;` in `struct sb_emulator` (NULL when unlinked).
- `void sb_emu_link_set(sb_emulator *e, sb_link *link)` — detach+destroy any old, attach new
  (must be called parked: registers callbacks).
- `void sb_emu_link_clear(sb_emulator *e)` — detach+destroy (parked).
- `sb_emu_run_frame` already exists — add `if (e->link) sb_link_slave_poll(e->link, &e->gb);`
  after `GB_run_frame` (emu thread).
- Master callback trampolines (`static`): `link_bit_start_cb`/`link_bit_end_cb` fetch
  `GB_get_user_data(gb)->link` and forward.

## 4. Session layer (`session.c`/`.h`)

Connect/listen block → **helper thread**; the session exposes status + wires the link into
the emulator (parked) once connected.
```c
enum { SB_LINK_IDLE, SB_LINK_LISTENING, SB_LINK_CONNECTING, SB_LINK_CONNECTED, SB_LINK_ERROR };
void sb_session_link_listen(sb_session *s, int port);          /* spawn acceptor thread */
void sb_session_link_connect(sb_session *s, const char *host, int port); /* spawn connector */
void sb_session_link_disconnect(sb_session *s);                /* tear down, parked */
int  sb_session_link_status(sb_session *s);                    /* atomic read */
```
- The helper thread builds the transport (`tcp_listen`/`tcp_connect`), and on success:
  parks the session, `sb_emu_link_set(emu, sb_link_create(t))`, unparks, sets status
  CONNECTED. On failure → ERROR.
- `disconnect`: park, `sb_emu_link_clear`, unpark, status IDLE; also signal/join the helper
  thread and shut the socket so a blocked accept/recv returns.
- All NULL-tolerant.

## 5. JNI + `NativeBridge.java`
```
void nativeLinkListen(long ctx, int port)
void nativeLinkConnect(long ctx, String host, int port)
void nativeLinkDisconnect(long ctx)
int  nativeLinkStatus(long ctx)     /* 0 IDLE 1 LISTENING 2 CONNECTING 3 CONNECTED 4 ERROR */
```

## 6. Java UI

Add **"Link cable"** to the in-game accessory menu (`GameMenuDialog` → alongside Printer).
Selecting it opens a small dialog/`LinkActivity`:
- **Host**: `nativeLinkListen(ctx, 1989)`; show this device's Wi-Fi IP (`WifiManager`/
  `NetworkInterface`) + port so the peer can join; status line polls `nativeLinkStatus`.
- **Join**: an IP:port field → `nativeLinkConnect(ctx, host, 1989)`; status line.
- **Disconnect**: `nativeLinkDisconnect`.
- Status text reflects IDLE/LISTENING/CONNECTING/CONNECTED/ERROR (a `Handler` poll, ~500 ms).

Manifest: `<uses-permission android:name="android.permission.INTERNET"/>` (+ `ACCESS_
NETWORK_STATE`/`ACCESS_WIFI_STATE` only if used to show the local IP).

## 7. Testing

**Host tests** (`Android/jni/test`, no device, no sockets):
- `test_link.c`: two `sb_emulator`s, each loaded with a **hand-written serial ROM** (master:
  `SB=0xA5; SC=0x81`, spin on SC bit7; slave: `SB=0x3C; SC=0x80`, spin), bridged by a
  `sb_transport_loopback_pair`. Run each core on its **own thread** (like `test_session`,
  `alarm()` deadlock-net); after the transfer completes assert **master SB == 0x3C** and
  **slave SB == 0xA5**, and that both took the serial interrupt (SC bit7 cleared). This
  proves the byte-exchange clocking end-to-end.
- Loopback transport unit check: `send`/`recv` FIFO ordering + non-blocking timeout returns
  false when empty.
- TCP smoke (host only, `127.0.0.1`): `tcp_listen` on a thread + `tcp_connect`, exchange a
  couple of bytes both directions, `close` unblocks a pending recv. (Guarded/skippable if
  the CI host forbids sockets — but the Linux build host allows loopback.)

**On-device (Waydroid, x86_64):**
- Two app instances can't easily run on one Waydroid; instead verify the **transport +
  status FSM** end-to-end using the device as one peer and the **host as the other** over
  `adb` port-forward (or a second Waydroid if available): Host in the app → `nativeLinkStatus`
  goes LISTENING; a host-side `nc`/tiny client connects → CONNECTED; disconnect → IDLE. And
  the reverse (app Joins a host-side listener). Confirms the socket path, status FSM, and
  graceful teardown on-device.
- A **real game trade** (two Pokémon instances) needs the ROM + two synced instances — out
  of reach here; **documented** like M7's Camera-ROM / M6's rumble-motor gap. The clocking
  itself is proven by the two-core host test.

## 8. Acceptance

1. Host test: two cores over the loopback transport swap a byte (master SB→0x3C, slave
   SB→0xA5) with both interrupts — clocking bridge correct.
2. Loopback + localhost-TCP transport unit checks green.
3. On-device: Host→LISTENING→(peer connects)→CONNECTED→disconnect→IDLE, and Join path;
   graceful teardown, no hang/crash on peer loss (timeout → 0xFF).
4. Clean 4-ABI `assembleDebug`; INTERNET permission present; new JNI symbols.
5. `Core/` unmodified; no new Gradle dependency.
6. Real two-game trade documented as needing a link ROM + two synced instances (out of
   slice); clocking verified by the host test.

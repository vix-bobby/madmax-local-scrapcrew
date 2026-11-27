# The `winhttp.dll` proxy

Windows loads a `winhttp.dll` found in the game folder instead of the system's one, so the
game's imports bind to the proxy without any hook or any wait for the executable to unpack.

**Not yet tried in game.** Everything below comes from the test program (`test/harness.c`).

## How it works

- `gen_exports.py` reads the system DLL's export table and writes `src/winhttp.def` and
  `src/exports.h`, keeping names and ordinals. Run it again to follow another Windows version.
- Nothing happens in `DllMain`. The first call loads `winhttp.dll` from the system folder.
- The functions the game imports are wrapped in C (`src/trace.c`). Every other export is a small
  stub that jumps to the real function, resolved on first use.
- Status callbacks are replaced by trampolines that log, then call the original.
  `WinHttpSetStatusCallback` still returns the caller's own previous callback.
- The DLL pins itself, since WinHTTP keeps pointers to those trampolines.

## Other callers

(v) Every module in the process that asks for `winhttp.dll` by name gets the proxy. This
includes WinHTTP itself: on an asynchronous session, sending a request opened further sessions,
without agent name and with their own status callback, that came through the proxy's exports.
Anything that changes behaviour must therefore be limited to the game's two hosts.

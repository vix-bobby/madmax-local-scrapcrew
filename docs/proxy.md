# The `winhttp.dll` proxy

Windows loads a `winhttp.dll` found in the game folder instead of the system's one, so the
game's imports bind to the proxy without any hook or any wait for the executable to unpack.

Tried in game on Windows: the game's WinHTTP calls go through the proxy, which is loaded next
to the system DLL, and a launch with a save load showed no crash or added delay (v). The rest
comes from the test program (`test/harness.c`) unless said otherwise.

## How it works

- `gen_exports.py` reads the system DLL's export table and writes `src/winhttp.def` and
  `src/exports.h`, keeping names and ordinals. Run it again to follow another Windows version.
- Nothing happens in `DllMain`. The first call loads `winhttp.dll` from the system folder.
- The functions the game imports are wrapped in C (`src/trace.c`). Every other export is a small
  stub that jumps to the real function, resolved on first use.
- Status callbacks are replaced by trampolines that log, then call the original.
  `WinHttpSetStatusCallback` still returns the caller's own previous callback.
- The DLL pins itself, since WinHTTP keeps pointers to those trampolines.

## Handle values

(v) Seen in game: WinHTTP reuses a handle value within milliseconds of its closing, also for
another host's request. Anything remembered about a handle must be dropped before the real
`WinHttpCloseHandle` runs.

## Other callers

(v) Every module in the process that asks for `winhttp.dll` by name gets the proxy. This
includes WinHTTP itself: on an asynchronous session, sending a request opened further sessions,
without agent name and with their own status callback, that came through the proxy's exports.
The same was seen in game. Anything that changes behaviour must therefore be limited to the
game's two hosts.

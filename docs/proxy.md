# The `winhttp.dll` proxy

Windows loads a `winhttp.dll` found in the game folder instead of the system's one, so the
game's imports bind to the proxy without any hook or any wait for the executable to unpack.

Tried in game on Windows, with both builds (v): the game's WinHTTP calls go through the proxy,
which is loaded next to the system DLL; the calls to its two hosts are answered by the listener,
with no crash, added delay, or firewall or antivirus prompt seen. On a Steam Deck, see
[Proton](#proton). What follows comes from the test programs (`test/`) unless said otherwise.

## How it works

- `gen_exports.py` reads the system DLL's export table and writes `src/winhttp.def` and
  `src/exports.h`, keeping names and ordinals. Run it again to follow another Windows version.
- Nothing happens in `DllMain`. The first call loads `winhttp.dll` from the system folder.
- The functions the game imports are wrapped in C (`src/trace.c`). Every other export is a small
  stub that jumps to the real function, resolved on first use.
- There are two builds. The normal one logs startup, the listener's requests and errors. The
  trace build (`-DMOD_TRACE`) also logs every wrapped call, and replaces status callbacks by
  trampolines that log, then call the original; `WinHttpSetStatusCallback` still returns the
  caller's own previous callback.
- The DLL pins itself, since its threads and, in the trace build, WinHTTP keep pointers into
  it.

## Answering the two hosts

- `WinHttpConnect` to `blackjack.hydra.agoragames.com` or `cls.psn.turbine.com` is turned into a
  connection to the listener: `127.0.0.1`, on a port Windows picks (`src/server.c`). The listener
  starts at the first such connection. If it cannot start, the call is passed on unchanged.
- Requests on such a connection lose `WINHTTP_FLAG_SECURE`, so there is no TLS and no
  certificate. Real WinHTTP still produces every status callback.
- They are also set to use no proxy. (v) With a named proxy configured, requests to `127.0.0.1`
  reached the listener even without that setting; (?) other proxy configurations.
- (v) `WINHTTP_OPTION_SECURITY_FLAGS`, which the game sets on SOAP requests, is accepted on a
  request without TLS.
- Redirection changes the `Host` header, so the listener tells the hosts apart by path.
- The replies are in `src/responder.c`; unknown requests get status 404.
- Any program on the same PC can reach the listener while the game runs.

## Proton

Tried in game on a Steam Deck with Proton 11.0-3, with the launch option
`WINEDLLOVERRIDES="winhttp=n,b"` (v): the calls to the two hosts are answered by the listener,
Scrap Crew rewards arrive and their challenges progress.

What follows was read in the source of Valve's Wine, branch `proton_10.0` (v). Proton replaces
Windows' WinHTTP with Wine's own.

### Why the launch option

- Without an override, Proton loads its own `winhttp.dll`, not the one in the game folder.
- The override applies by file name, so it also covers the system copy that the proxy loads. That
  copy is Wine's builtin: `n,b` allows it, `n` alone refuses it.
- `winhttp` is not among the known DLLs that `loader/wine.inf.in` registers, which would be
  loaded from the system folder whatever the override.
- `WINEDLLOVERRIDES` takes priority over the overrides stored in the prefix.

### Known limitation: a configured proxy

This is about an HTTP proxy server configured for the network, not about the DLL. The DLL asks
for no HTTP proxy on each redirected request (`WINHTTP_OPTION_PROXY`). Wine accepts that setting
and ignores it. The game's sessions ask for the default HTTP proxy, which Wine takes from the
prefix's registry or from the `http_proxy` variable, and then uses for every host not on a bypass
list, `127.0.0.1` included.

(i) With an HTTP proxy configured there, the game's calls would go to it instead of the listener,
and no reward would arrive. Without one, which is the case that was tried, nothing differs. (?)
Not tried with an HTTP proxy. A possible remedy, not implemented, is to turn it off on the game's
two sessions before they connect.

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

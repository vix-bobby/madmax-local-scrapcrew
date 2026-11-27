# The `winhttp.dll` proxy

Windows loads a `winhttp.dll` found in the game folder instead of the system's one, so the
game's imports bind to the proxy without any hook or any wait for the executable to unpack.

Tried in game on Windows, with the detailed log (v): the game's WinHTTP calls go through the proxy, which is loaded
next to the system DLL; the calls to its two hosts are answered by the listener, with no crash,
added delay, or firewall or antivirus prompt seen. What follows comes from the test programs
(`test/`) unless said otherwise.

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

Not tried in game. (v) Read in the source of Valve's Wine, branch `proton_10.0`:

- A `winhttp.dll` in the game folder is used only with the override `winhttp=n,b`. The override
  applies by file name, so it also covers the system copy the proxy loads: that one is Wine's
  builtin, which `n` alone refuses.
- `winhttp` is not among the known DLLs that `loader/wine.inf.in` registers, which would be
  loaded from the system folder whatever the override.
- `WINEDLLOVERRIDES` takes priority over the prefix's registry overrides.
- Wine exports `DllRegisterServer` and `DllUnregisterServer`, which the Windows export list used
  here lacks. (i) Nothing loads the copy in the game folder to call them.
- `WINHTTP_OPTION_PROXY` on a request is accepted and ignored. A session with the default proxy
  setting takes its proxy from the registry or the `http_proxy` variable and sends every host
  through it, `127.0.0.1` included unless bypassed. (i) So the mod does not work with a proxy
  configured in the prefix or in `http_proxy`.
- The security flags the game sets are accepted on a request without TLS, and the listener's
  socket option `SO_EXCLUSIVEADDRUSE` is supported.

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

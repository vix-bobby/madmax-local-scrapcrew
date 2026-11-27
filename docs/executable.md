# The executable

`MadMax.exe`, 64-bit, SHA-256 `cd719a721db92575aa32ff01e07a590f47dd34b702b1bcd98ec698a07358bb0d`.
Addresses below hold for this file only.

## Layout

- (v) DllCharacteristics `0x8120`: no dynamic base; the image loads at `0x140000000`. The base
  relocation directory is empty.
- (v) Imports include `WINHTTP.dll`, `WININET.dll`, `DINPUT8.dll` (`DirectInput8Create`),
  `d3d9.dll` (`D3DPERF_SetOptions`) and `VERSION.dll`.
- (v) WinHTTP imports: `CheckPlatform`, `Open`, `Connect`, `OpenRequest`, `AddRequestHeaders`,
  `SetOption`, `SetTimeouts`, `SetStatusCallback`, `SendRequest`, `ReceiveResponse`,
  `QueryHeaders`, `QueryDataAvailable`, `ReadData`, `CloseHandle`.
- (v) At run time their import slots, RVA `0x32c31e0` to `0x32c3248` in section `.xpdata`, hold
  the addresses of system library functions.

## Packed code

- (v) The large sections `.xpdata` (about 69 MB) and `.xtext` carry no execute flag in the file,
  and the start of `.xpdata` has an entropy of 7.8 bits per byte. Only the small `.trace` and
  `.xcode` sections are executable, and the entry point is in `.xcode`.
- Tried: disassembling the file as shipped (Ghidra) yields nonsense. This is a dead end.
- (v) In a memory dump taken at the main menu the code is ordinary x64: function prologues are
  there and the classes' virtual methods disassemble. A dump taken after loading a save gave the
  same code at every address examined.
- (v) Some constants stay obfuscated in the dump: a small number is built from two large ones
  (for example `mov ecx, 0x90c3a20f` then `lea ecx, [rcx+0x6f3c5e01]` gives 16), so searching
  for it as an immediate fails.
- (?) Which protection product this is.
- Tried in game: attaching the x64dbg debugger froze the game. Reading memory from another
  process worked. Changed code bytes stayed in place for over an hour and across a save load
  without a crash.

## Class names

(v) The compiler's type names are plain data in the file: scanning for `.?AV...@@`, `.?AU...@@`
and `.?AW...@@` finds about 3,500 classes, structures and unions, with no disassembly. In the
memory dump, they lead to the virtual tables.

## Online classes

- (v) Hydra work is split into `CHydraAction_*` classes (`Setup`, `UpdateMOTD`,
  `ScrapAwardedTimestampGet` and `Set`, and four `OpusWars...`) and `CHydraTask_*` classes
  (account, profile, leaderboard, news and terms requests, among others).
- (v) The pointer at `0x1417f4ef8` leads to an `NNetwork::COnlineManager`; its field `+0xde8`
  points to the `CAgoraService`, the Hydra client.
- (v) Before it builds a Scrap Crew request, the game calls `0x142361ae0`, which is true only if
  the status object at `CAgoraService+0x30` has a nonzero first byte and the value 1 at offset 4.
- Tried in game: that value stayed 0 against the dead server and became 1 by itself once the
  calls were answered. Writing 1 into it by hand crashed the game, in another function that
  reads the same object (`0x142396de0`).
- (v) The fields `+0x40` and `+0x44` of a request object belong to a task base class shared by
  many request types.

## Functions callable from game scripts

- (v) Near `0x141e71830` the game registers native functions by name for its
  [game scripts](file-formats.md#game-scripts): `GetScrapCrewReward`, `GetTimeStamp`,
  `GetTimeOffline`, `Timer`, `TimeLock`, `SleepFrame` and many more, each through a call to
  `0x14300daf0`.
- (v) At run time they form a table of 16-byte entries: the hash of the name, a 32-bit tag and
  the function address. A script node's `Class` is that hash, and its `Function` pointer, zero
  on disk, points at the entry's address field. Such functions therefore have no direct caller.

## WinHTTP status callbacks

(v) Functions the game registers: `0x142fb19b0` on Hydra requests, `0x142f9b980` on SOAP
requests, and `0x1429aaab0` (mask `0x007e0000`) on the session without agent name.

## Scrap Crew reward

- (v) The game script `scrap_crew.gsrc` has a node bound by name to `GetScrapCrewReward`, the
  function at `0x141ea2930`.
- (v) That function calls `0x141d9b520`, which computes the elapsed whole minutes as
  `([manager+0x1370] - [manager+0x1374]) / 60` once the profile request has completed. If
  `[manager+0x1374]` is zero it uses 300 minutes instead. (i) That is the case of a reward
  never given, which the server reports as `"0"`.
- (v) Only if more than 10 minutes have elapsed does it call `0x141d83d10`, which builds the
  request that records a new reward time.
- (v) After a real exchange, `[manager+0x1374]` held the number the server sent and
  `[manager+0x1370]` held `0x7FFFFFFF`. The manager pointer is stored at `0x1417f5fd8`.
- (?) When the game asks again during play.
- Tried in game: writing those fields directly changes the computed minutes, but no scrap grant
  was seen that way. A save load resets them.

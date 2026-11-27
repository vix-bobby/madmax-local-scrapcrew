# The executable

`MadMax.exe`, 64-bit, SHA-256 `cd719a721db92575aa32ff01e07a590f47dd34b702b1bcd98ec698a07358bb0d`.
Addresses below hold for this file only.

## Layout

- (v) DllCharacteristics `0x8120`: no dynamic base; the image loads at `0x140000000`.
- (v) The code is packed on disk and readable in a memory dump taken at the main menu. (?) Which
  protection product this is.
- (v) Imports include `WINHTTP.dll`, `WININET.dll`, `DINPUT8.dll` (`DirectInput8Create`),
  `d3d9.dll` (`D3DPERF_SetOptions`) and `VERSION.dll`.
- (v) WinHTTP imports: `CheckPlatform`, `Open`, `Connect`, `OpenRequest`, `AddRequestHeaders`,
  `SetOption`, `SetTimeouts`, `SetStatusCallback`, `SendRequest`, `ReceiveResponse`,
  `QueryHeaders`, `QueryDataAvailable`, `ReadData`, `CloseHandle`.
- (v) At run time their import slots, RVA `0x32c31e0` to `0x32c3248` in section `.xpdata`, hold
  the addresses of system library functions.

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
- (i) The amount grows with the elapsed time: see [network.md](network.md#the-amount-granted).
- (?) When the game asks again during play.
- Tried in game: writing those fields directly changes the computed minutes, but no scrap grant
  was seen that way. A save load resets them.

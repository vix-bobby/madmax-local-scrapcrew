# Mad Max (2015): local Scrap Crew

A drop-in `winhttp.dll` that aims to restore Scrap Crew income in Mad Max (2015, PC) now that
the publisher's online services are gone. It adds files to the game folder and changes none.

## Status

**Not usable yet: it does not restore Scrap Crew income.** The current DLL passes every call to
the system's WinHTTP and logs what the game sends through it.

## Build

Needs `x86_64-w64-mingw32-gcc`. The tests also need WSL, to run a Windows program.

```sh
./build.sh          # build/winhttp.dll
./build.sh test     # build and test
```

## Install and remove

The game folder is the one holding `MadMax.exe`. If it already has a `winhttp.dll`, that file
belongs to another mod: do not overwrite it.

- Install: copy `build/winhttp.dll` into the game folder.
- Remove: delete `winhttp.dll` and `madmax_scrapcrew.log` from the game folder.

The DLL writes `madmax_scrapcrew.log` next to itself, or into `%TEMP%` if that folder is
read-only. The log is overwritten at each launch. It holds host names, request paths and headers,
but no request or response bodies, query strings, cookies or authorization headers.

Windows only so far. Steam Deck and Proton have not been tried.

## Credits

[madmax-2015-offline](https://github.com/3ek/madmax-2015-offline) by its contributors
(GPL-3.0-or-later) is a local server emulator that restores the same feature, and the reason
this mod is possible. [What this project takes from it](docs/credits.md).

## Documentation

[docs/](docs/README.md): how the game's online calls and the DLL work.

## Licence

[GPL-3.0-or-later](LICENSE).

# Mad Max (2015): local Scrap Crew

A drop-in `winhttp.dll` that aims to restore Scrap Crew income in Mad Max (2015, PC) now that
the publisher's online services are gone. It adds files to the game folder and changes none.
It needs no server, certificate or network change.

## Status

**Experimental.** In a first test on Windows, loading a save granted Scrap Crew resources. Later
rewards and longer play have not been tried.

![Scrap Crew rewards shown in game](media/ingame-scrapcrew-rewards.png)

## What it does

The game asks two online hosts about the Scrap Crew reward. The DLL answers those requests
itself, inside the game process, through a listener reachable only from your own PC. The time of
the last reward is measured by your PC's clock and kept in a file in the game folder.

The game therefore no longer contacts those two hosts. Without the mod it still sends your
SteamID and a Steam ticket to `cls.psn.turbine.com`. Other connections are passed to Windows
unchanged.

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
- Remove: delete `winhttp.dll`, `madmax_scrapcrew.log` and `madmax_scrapcrew.dat` from the game
  folder.

The DLL writes two files next to itself, or into `%TEMP%` if that folder is read-only:

- `madmax_scrapcrew.dat`, kept between launches: your SteamID, Steam display name, installed DLC
  names and the time of the last reward. Deleting it makes the next reward count as the first.
- `madmax_scrapcrew.log`, overwritten at each launch: host names, request paths and headers, but
  no request or response bodies, query strings, cookies or authorization headers.

Windows only so far. Steam Deck and Proton have not been tried.

## Credits

[madmax-2015-offline](https://github.com/3ek/madmax-2015-offline) by its contributors
(GPL-3.0-or-later) is a local server emulator that restores the same feature, and the reason
this mod is possible. [What this project takes from it](docs/credits.md).

## Documentation

[docs/](docs/README.md): how the game's online calls and the DLL work.

## Licence

[GPL-3.0-or-later](LICENSE).

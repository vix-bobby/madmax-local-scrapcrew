# Mad Max (2015): local Scrap Crew

A drop-in `winhttp.dll` that aims to restore Scrap Crew income in Mad Max (2015, PC) now that
the publisher's online services are gone. It adds files to the game folder and changes none.
It needs no server, certificate or network change.

## Status

**Experimental.** On Windows, loading a save has granted Scrap Crew resources, the first time and
again some 16 minutes later. Longer play and Steam Deck have not been tried.

![Scrap Crew rewards shown in game](media/ingame-scrapcrew-rewards.png)

## What it does

The game asks two online hosts about the Scrap Crew reward. The DLL answers those requests
itself, inside the game process, through a listener reachable only from your own PC. The time of
the last reward is measured by your PC's clock and kept in a file in the game folder.

The game therefore no longer contacts those two hosts. Without the mod it still sends your
SteamID and a Steam ticket to `cls.psn.turbine.com`. Other connections are passed to Windows
unchanged.

## How much scrap

The amounts are the game's own. The mod only tells the game how long ago the last reward was,
as your PC's clock measures it; the game's unchanged code decides what that is worth.

## Build

Needs `x86_64-w64-mingw32-gcc`. The tests also need WSL, to run a Windows program.

```sh
./build.sh          # build/winhttp.dll
./build.sh trace    # also build/trace/winhttp.dll, with a detailed log
./build.sh test     # build both and test
```

## Install

The game folder is the one holding `MadMax.exe` (in Steam: right-click the game, Manage, Browse
local files). If it already has a `winhttp.dll`, that file belongs to another mod: do not
overwrite it.

### Windows

Copy `winhttp.dll` into the game folder.

### Steam Deck and Linux (Proton)

**Not yet tried.** These steps follow from how Steam and Proton load files.

The mod works only with the Windows version of the game run through Proton. Mad Max also has a
native Linux version, which cannot load it.

1. Back up your saves, especially if you have been playing the native Linux version: whether its
   saves carry over to the Windows version has not been checked.
2. In the game's Properties, under Compatibility, force the use of a Proton version (Proton 10).
   Let Steam download the Windows files, and check that the game folder now holds `MadMax.exe`.
3. Copy `winhttp.dll` into the game folder (on the Deck, in Desktop Mode).
4. In Properties, set the launch options to exactly:

   ```
   WINEDLLOVERRIDES="winhttp=n,b" %command%
   ```

   Without this, Proton ignores the file. Do not shorten it to `winhttp=n`: the mod needs
   Proton's own `winhttp.dll` too.
5. Start the game and load a save. A `madmax_scrapcrew.log` file in the game folder shows that
   the mod was loaded.

If you set up [madmax-2015-offline](https://github.com/3ek/madmax-2015-offline) before, remove
its launch option and run its uninstall scripts: this mod needs none of it.

## Remove

Delete `winhttp.dll`, `madmax_scrapcrew.log` and `madmax_scrapcrew.dat` from the game folder.
With Proton, also clear the launch options.

## Files the mod writes

Both go next to the DLL, or into `%TEMP%` if that folder is read-only.

- `madmax_scrapcrew.dat`, kept between launches: your SteamID, Steam display name, installed DLC
  names and the time of the last reward. Deleting it makes the next reward count as the first.
  Each computer has its own, so each gives a first reward and then counts on its own. To keep a
  single count when playing on two computers, copy the file from the one used last to the other,
  with the game closed.
- `madmax_scrapcrew.log`, overwritten at each launch: startup, one line per online call (path,
  sizes and result), reward times and errors.

The trace build also logs every WinHTTP call the game makes, with host names, request paths and
headers. Neither log holds request or response bodies, query strings, cookies or authorization
headers.

## Credits

[madmax-2015-offline](https://github.com/3ek/madmax-2015-offline) by its contributors
(GPL-3.0-or-later) is a local server emulator that restores the same feature, and the reason
this mod is possible. [What this project takes from it](docs/credits.md).

## Documentation

[docs/](docs/README.md): how the game's online calls and the DLL work.

## Licence

[GPL-3.0-or-later](LICENSE).

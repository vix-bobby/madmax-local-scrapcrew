# Documentation

| File | Topic |
|------|-------|
| [executable.md](executable.md) | The game executable: packing, class names, online code, the Scrap Crew reward |
| [file-formats.md](file-formats.md) | Archives, name hashes, ADF types, game scripts, save files |
| [network.md](network.md) | The online calls, their wire format and the reward timestamp |
| [proxy.md](proxy.md) | The `winhttp.dll` proxy, the listener and Proton |
| [credits.md](credits.md) | What comes from other projects |

## How sure a claim is

- **(v)** seen in the disassembly or in data, including captured traffic and test output
- **(i)** inferred
- **(?)** not established

Each file says what was and was not tried in game.

## Other tools in the field

| Tool | What it is for | Tried |
|------|----------------|-------|
| [madmax-2015-offline](https://github.com/3ek/madmax-2015-offline) | Local server emulator; needs a certificate, a host redirection and a running server | Yes, in game, commit `fee927e`: Scrap Crew resources were granted |
| [Scrap Crew Challenge Workaround](https://www.nexusmods.com/madmax/mods/100) | Data override in `dropzone/` that points the Scrap Crew challenges at another counter; restores no income | Analysed only, not installed |
| [Mad-Manager](https://github.com/y0xOFF/Mad-Manager) | Mod manager using the `dropzone/` folder | No |
| [Gibbed.MadMax](https://github.com/gibbed/Gibbed.MadMax) | Unpacking the game's archives and converting its files | Yes: unpacking, property and ADF conversion (the latter after changes, see [file-formats.md](file-formats.md)) |
| [Ghidra](https://github.com/NationalSecurityAgency/ghidra) | Disassembly | Yes: useless on the executable as shipped (packed) |
| [mitmproxy](https://mitmproxy.org/) | Reading HTTPS traffic | Yes, through the system's WinHTTP proxy setting |
| [Process Monitor](https://learn.microsoft.com/sysinternals/downloads/procmon) | Watching file and network activity | Yes: showed the online call at launch and no network activity on a save load against the dead servers |
| [DebugView](https://learn.microsoft.com/sysinternals/downloads/debugview) | Reading debug output | Yes: nothing seen from the game |
| [x64dbg](https://x64dbg.com/) | Debugging | Yes: attaching it froze the game |
| [Just Cause 3 tools fork](https://github.com/tim42/gibbed-justcause3-tools-fork) | Gibbed-based tools for a later game on the same engine | Partly: its ADF primitive types were reused |
| [MadMax_Savegame_Editor_Project](https://github.com/Rick-Thompson/MadMax_Savegame_Editor_Project) | Save editing | Partly: its way of locating the scrap amount did not work |

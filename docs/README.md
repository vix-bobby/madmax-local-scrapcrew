# Documentation

| File | Topic |
|------|-------|
| [executable.md](executable.md) | The game executable and the code behind the Scrap Crew reward |
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
| [Gibbed.MadMax](https://github.com/gibbed/Gibbed.MadMax) | Unpacking the game's archives | Yes, to extract game scripts |
| [MadMax_Savegame_Editor_Project](https://github.com/Rick-Thompson/MadMax_Savegame_Editor_Project) | Save editing | Partly: its way of locating the scrap amount did not work |

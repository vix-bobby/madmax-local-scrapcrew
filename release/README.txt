Mad Max (2015): local Scrap Crew
================================

Brings back the Scrap Crew income that stopped when the game's online services were shut down.
No server, certificate or network change. No game file is replaced.

REQUIREMENTS
- Mad Max (2015) on Steam, on Windows or on a Steam Deck / Linux with Proton.
  Tried on Windows and on a Steam Deck (Proton 11.0-3). Other stores and other Linux systems
  have not been tried.
- The Windows version of the game. The native Linux version cannot load the mod.

INSTALL - WINDOWS
1. Open the game folder, the one holding MadMax.exe. In Steam: right-click Mad Max, Manage,
   Browse local files.
2. If there is already a winhttp.dll there, it belongs to another mod: do not overwrite it.
   The two cannot be used together.
3. Copy this mod's winhttp.dll into the folder.
4. Start the game as usual.

INSTALL - STEAM DECK AND LINUX
1. Open the game folder (on the Deck, in Desktop Mode). If it holds no MadMax.exe, open the
   game's Properties, force the use of a Proton version under Compatibility, and let Steam
   download the Windows files.
2. Copy winhttp.dll into the folder, next to MadMax.exe (do not overwrite another mod's).
3. In the game's Properties, set the launch options to:

       WINEDLLOVERRIDES="winhttp=n,b" %command%

   Without this, Proton ignores the file. Do not shorten it to winhttp=n. If you already have
   launch options, put the WINEDLLOVERRIDES=... part in front of them and keep a single
   %command%.
4. Start the game.

If you set up the madmax-2015-offline server before, remove its launch option and run its
uninstall scripts: this mod needs none of it.

UNINSTALL
1. Delete these from the game folder: winhttp.dll, madmax_scrapcrew.log and
   madmax_scrapcrew.dat (and madmax_scrapcrew.dat.new, if there is one).
2. On Steam Deck / Linux, remove the WINEDLLOVERRIDES=... part from the launch options.
Scrap already received stays in your save. Steam's "verify integrity of game files" does not
remove the mod.

IN GAME
- Start the game and load a save: scrap arrives if more than 10 minutes have passed since the
  last reward. The first time with the mod, it arrives right away.
- The challenges "A Penny Saved" and "Dividend" progress again.
- The online features in the main menu show as locked. This is expected.
- The amounts are the game's own: the mod only tells the game how long ago the last reward was,
  measured by your computer's clock, and the game decides how much scrap that is worth.

PRIVACY
The mod answers the game's two online hosts itself, on your computer, so the game no longer
contacts them. Without the mod, the game still sends your SteamID and a Steam ticket to
cls.psn.turbine.com. Other connections are left unchanged.
The mod writes two files in the game folder (or in %TEMP% if it cannot write there):
- madmax_scrapcrew.dat: your SteamID, Steam name, installed DLC names and the time of the last
  reward. Do not share it. Deleting it makes the next reward count as the first.
- madmax_scrapcrew.log: what the mod did at the last launch, without request contents.
Each computer has its own .dat file. To keep a single count when playing on two computers, copy
it from the one used last to the other, with the game closed.

IF SOMETHING GOES WRONG
- No madmax_scrapcrew.log after starting the game, neither in the game folder nor in %TEMP%:
  the mod was not loaded. Check that winhttp.dll is next to MadMax.exe and, with Proton, check
  the launch options.
- With Proton, a proxy set in http_proxy or in the game's Proton prefix is expected to keep the
  mod from working (not tried).
- The game does not start: delete winhttp.dll.
- To report a problem, attach madmax_scrapcrew.log.
- As with any DLL placed in a game folder, an antivirus may flag it.

CREDITS
Built thanks to madmax-2015-offline, a server emulator by its contributors:
https://github.com/3ek/madmax-2015-offline
This mod's knowledge of the game's online calls and of the reward rule comes from it, and the
mod's replies follow it, rewritten in C.

LICENCE AND SOURCE
GPL-3.0-or-later: see the LICENSE file. Source code: <SOURCE URL>

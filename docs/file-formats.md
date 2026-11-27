# Game files

Marks as in the [index](README.md). Offsets are in bytes.

## Archives

- (v) The data is in `.tab`/`.arc` pairs under `archives_win64`, with DLC in `dlc_win64` and
  updates in `patch_win64`. [Gibbed.MadMax](https://github.com/gibbed/Gibbed.MadMax) unpacks
  them; `.ee` files are small archives (SARC) with their own unpacker.
- (v) Property files (`.epe` and others) are self-describing and convert to XML and back with
  Gibbed's property converter.
- (?) Loose files in a `dropzone` folder next to the executable are said to override archived
  ones (Mad-Manager and the Nexus mod listed in the [index](README.md) use it). Not tried here,
  neither for plain files nor for files inside an `.ee`.

## Name hashes

(v) Names are stored as 32-bit hashes: Bob Jenkins' "lookup3" with seed 0 over the ASCII text,
`HashJenkins` in Gibbed's `StringHelpers.cs`. Ten of the eleven primitive type hashes in
Gibbed's `TypeHashes.cs` are reproduced by hashing the text in its comments.

| Hash | Text | Hash | Text |
|------|------|------|------|
| `0xB2B563B5` | `int` | `0x9BFF4266` | `Value` |
| `0xE9F00A0C` | `uint32` | `0x3A4CA003` | `Name` |
| `0x71AF5FAB` | `uint64` | `0x119E0BB8` | `Time` |
| `0x4E0A188B` | `float` | `0xCBD5DB5E` | `Start` |
| `0xA8B28B19` | `bool` | `0x96FDCB24` | `Timer` |
| `0x83FFF3B4` | `Debug` | `0xF0DAAB70` | `SleepFrame` |
| `0xD4E251CB` | `GetScrapCrewReward` | `0x94407F4C` | `GraphScript` |

## ADF files and their types

- (v) Many files are ADF containers (magic `ADF `). Some, the game scripts among them, carry no
  type definitions, and no type library was found in the archives.
- (v) The types are in the executable: its section `.data1` (shown as `.data1_a` by some tools)
  holds 41 complete ADF files back to back, each with the name of its source `.adf` file. Their
  type definitions read like those of an on-disk ADF file, but each member is a 32-byte record
  with a 32-bit offset. Of the 41, 39 were read completely with that layout; enumerations need
  different handling.
- (v) Gibbed's ADF converter needed two changes to read such data: accept files with embedded
  type definitions, and handle the missing primitive types (its Just Cause 3 fork has them).

## Game scripts

(v) A `.gsrc` file is an ADF instance of type `GSGraph` (type hash `0x63B4A6F9`, defined in
`graphadf.adf`): an array of nodes and one data record. 462 such files were extracted from the
game's archives.

Array fields are two 64-bit numbers, offset and count. The node array's offset and the offsets
inside nested records are relative to the start of the instance.

| Record | Size | Fields |
|--------|------|--------|
| `GSNode` | 64 | `Class` (u32) at 0, `Function` (pointer) at 8, a `GSDataSet` inline at 16 |
| `GSDataSet` | 48 | `Name` (u32) at 0, array of `GSData` at 8, array of `GSDataSet` at 24 |
| `GSData` | 32 | `Name` (u32) at 0, `Type` (u32) at 4, array of bytes at 8, `Reference` (u8) at 24 |

- (v) `Class` is the hash of a native function's name (see
  [executable.md](executable.md#functions-callable-from-game-scripts)); `Function` is zero in
  every file and filled in when the game loads the script.
- (v) The graph's own data record points to a shared pool. A node's 4-byte value is a position
  in that pool: text starts there, or an 8-byte slot holding a number.
- (v) The pool also holds 16-byte records, a name hash followed by 8 bytes. (i) Nodes refer to them
  as variables.
- (?) How the connections between nodes are stored, so the order in which nodes run.
- (?) The meaning of `Reference`.

## The Scrap Crew scripts

- (v) Of the three stronghold crews, only the Scrap Crew has a script of its own,
  `graphs/open_world/scrap_crew.gsrc`: there is no Cleanup Crew or Survey Crew script.
- (v) The entity files of the three crews' projects refer to the same two graphs,
  `economy_resource_check_event_sender` and `opportunity_vision_gating`.
- (v) The script's strings include `LOADED SAVE GAME`, `Scrap stored`,
  `global scrap crew detected` followed by `PINKEYE`, `GUTGASH` or `JEET`, and the path of
  `scrap_crew_reward.gsrc`.
- (v) `camp_scrap_reward.gsrc` and `give_player_scrap.gsrc` are separate scripts; (i) they handle
  scrap found in the open world, not the crews.
- Tried in game after the shutdown, without a replacement server: Cleanup and Survey Crews kept
  working, while no Scrap Crew scrap arrived, on a save load or over time.

## Save files

- (v) Steam saves are in `userdata/<account>/234140/remote/`: `GameSave01.sav` to
  `GameSave05.sav` and `Settings.sav`. On Windows the game also keeps copies in
  `Documents/WB Games/Mad Max/Backup Saves/`.
- (v) The content is obfuscated with a repeating 32-byte XOR key. The key and the layout are
  documented by
  [MadMax_Savegame_Editor_Project](https://github.com/Rick-Thompson/MadMax_Savegame_Editor_Project);
  with it, saves of format version 6 showed the magic `0x33116F7F` and a sensible header.
- (v) For version 6 that project's checksum does not match, and its search for the scrap amount
  found nothing in long playthroughs.
- This mod does not read or write save files.

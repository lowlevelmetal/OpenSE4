# Spec 08: Saved games (.gam): format, import and export

Status: reference spec for importing the original's saved games into OpenSE4 and exporting
OpenSE4 games back to them. Written on 2026-10-04 from an analysis of the original
executable (rules marked **(confirmed: binary)**) and from saves made and loaded in the
running original (marked **(observed)**). Anything not settled either way is marked
**(inferred)** and listed in §11. Field names are ours; the original's names for them are
not used.

Sources checked:

- Two saves found on this machine: a small five-empire simultaneous test game left in the
  install's save folder by an earlier session, and a large third-party 20-empire
  turn-based game (version 1.95, about 650 KB). Both decode completely: the parser
  described here consumes every byte of both, ending exactly at the end of the file
  **(observed)**. Neither file, nor anything taken from it, is in this repository; §8
  gives counts only.
- Sixteen saves of our own made in a scratch copy of the original under Wine: a Quick
  Start game saved fresh and then one action apart, diffed field by field after decoding,
  and a simultaneous game made through Game Setup (§9).
- Files written by our own encoder and loaded by the original (§9).

Notation: integers are little-endian. `byte` is an unsigned 8-bit value, `word` an
unsigned 16-bit value, `int` a signed 32-bit value. "1-based" means the first record of a
data file or list is number 1.

---

## 1. The files of a saved game

### 1.1 The game file

- A saved game is one file `<name>.gam` in the save folder (by default `SaveGame\` in the
  installation; the game setup can name another folder). Loading needs only this file
  **(confirmed: binary)**.
- The Save Game window takes the name typed by the player. Autosaves are named
  `AutoSav<d>.gam`, where `<d>` is the last digit of the date counter (§3.1), so they
  cycle through at most ten files (spec 01 §2.2) **(confirmed: binary)**.
- In a turn-based game on different machines, ending a human turn saves the game as
  `<game name>_<player number>_<date counter>.gam` (the game name is the one chosen at
  setup, §3.2) and marks it as such a hand-over file (§3.1) **(confirmed: binary)**.
- In a simultaneous game every turn processing (the host's work) saves the game as
  `<game name>.gam` in the game's save folder; that is the file the players load next.
  Its current player is the empire count and its turn code is new **(observed)**.
- The original also keeps copies of the current game in its `temp\` folder,
  `<game>_LastTurn.gam` (the state at the start of the turn, with current player 0,
  **observed**) and `<game>_CurrTurn.gam`, for the movement replay. They have the same
  format and are not needed to load a game **(confirmed: binary)**.

### 1.2 Companion files

The game keeps per-player history files (statistics, history lines) and the combat replays
in the installation's `history\` folder while a game is played. **Saving** copies every
file of that folder next to the game file under the name `<name>_<file name>` (at most
5,000 files); **loading** first empties the folder, then copies back each
`<name>_plr_*.txt` file found next to the game file, without the `<name>_` prefix
**(confirmed: binary; observed: a save after one turn produced
`<name>_plr_1_stats.txt`)**. Combat replay files are copied both ways in turn-based games
when the Settings.txt switch for creating replays is on. Separately, the turn processing
of a simultaneous game writes `<game>_Log.trn` and `<game>_Combat.cmb` next to its host
save, named after the game **(observed, in both simultaneous games; a Quick Start
turn-based game wrote neither)**.

| File next to the game | Holds | Needed to load |
|---|---|---|
| `<name>_plr_<n>_stats.txt` | Player n's statistics, one text line per turn: player number, date counter, then about ten right-aligned numeric columns (score parts, production, research and the like). Feeds the Scores and Comparisons graphs. | No. Missing files only leave the graphs empty (inferred). |
| `<name>_plr_<n>_events.txt` | Player n's dated history lines (the History window). | No (same). |
| `<game>_Log.trn` | The movement log of the last turn (binary), for the movement replay; written by the turn processing, named after the game. | No. Without it the replay reports that it is unavailable **(confirmed: binary)**. |
| `<name>_Combat.cmb`, `<game>_Combat.cmb` | The combat replays of the last turn (binary). | No. Only the Combat Replay button needs it. |

Import should read the game file only. Export may write the game file only; an empty
`history\` simply means no graphs before the export date. Converting the statistics lines
of OpenSE4's `Empire::history` into `_plr_<n>_stats.txt` is possible but optional
(open question 8 in §11.1).

### 1.3 Other files in the same container

The same container (§2) and the same section writers are used by four other files. They
are not needed for import or export of a game but an importer can share the decoder.

| File | Content after the version string |
|---|---|
| Empire file (`Empires\*.emp`) | One empire record in its "empire file" form, optionally with its designs. |
| Map file (Save Map) | The systems and the stellar objects in their "map" form (no per-empire knowledge, no vehicles), plus the starting points (spec 01 §12). |
| Game setup file | The game options (§3.2) and the victory block (§3.3). |
| Player changes file (`.plr`, simultaneous games on different machines) | Date counter, random codes and the game's two codes (§3.2), then one player's systems view, its empire record, its designs and its changed objects, each in "player" form (spec 05 §9.2). |

---

## 2. The container

### 2.1 Overview

A `.gam` file is a stream of Delphi-style tagged values (the format the original's
runtime library uses for component streaming), most of them obfuscated with a key stream
derived from six numbers at the start of the file. There is no compression and no
checksum. The order of values is fixed by the save code; nothing is named in the file,
and every list carries its own count, except the racial-trait flags, whose count comes
from the data set (§3.6.5) **(confirmed: binary)**.

The file is, in order:

1. the key header (§2.2),
2. the version string, obfuscated (§2.5),
3. the plain-text summary (§2.6),
4. the obfuscated body (§3), up to the end of the file.

### 2.2 The key header

Six integers, each in the compact integer encoding of the stream format: a tag byte 2
followed by a signed byte, or tag 3 followed by a signed 16-bit value, or tag 4 followed by
a signed 32-bit value. The writer uses the smallest form that holds the value
**(confirmed: binary)**.

| # | Meaning | Values written by the original |
|---|---|---|
| K1 | Selector | 1..10 |
| K2 | First seed | 1..10000, random |
| K3 | Unused | 1..10000, random |
| K4 | Unused | 1..10000, random |
| K5 | Mask | 1..10000, random |
| K6 | Second seed | The table value for K1, below |

Second seed by selector **(confirmed: binary)**:

| K1 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | other |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Seed | 6857 | 4444 | 23234 | 48223 | 3827198 | 392737 | 94846 | 9956 | 11211 | 232 | 777456 | 3234 |

The loader reads K6 and ignores it: it takes the second seed from the table by K1
**(confirmed: binary)**. A file whose K6 is wrong loads normally **(observed, §9)**.
Decoders should do the same; writers should still write the table value.

The original makes new random keys for every file it writes, so two saves of an unchanged
game differ byte for byte but decode to the same values **(observed)**.

### 2.3 The key table

The keys come from a table of 256 rows by 1,000 columns of bytes, of which only rows 1,
57 and 123 are used (rows and columns 1-based). It is filled row by row, and within a row
column by column, from a lagged sum **(confirmed: binary)**:

```
a := K2;  b := second seed (table of §2.2)
for row 1..256, for column 1..1000:
    s := a + b
    if s > 1_000_000_000: s := s - 1_000_000_000      (done twice)
    v := s xor K5                                      (32-bit)
    cell[row][column] := v and 255
    a := b;  b := v
```

The original computes `s` in double precision and truncates it, but every value is a
whole number below 2^32, so integer arithmetic gives the same result. Generating rows 1
to 123 (123,000 steps) is enough.

Call the three used rows `R1`, `R57` and `R123`.

### 2.4 The key stream

One position `p`, a column number, runs through the whole file. It starts at 1 after the
key header and is never reset **(confirmed: binary)**. Two kinds of key are drawn from it:

- **Number key** (for bytes, words, ints, booleans and sets): `k := R1[p] × R57[p] ×
  R123[p]` (at most 16,581,375), then `p := p + R1[p]`, and if `p > 1000` then
  `p := p − 1000`. When `R1[p]` is 0, `p` stays where it is.
- **Character key** (for strings, one per character): first `p := p + 1`, with 1001
  becoming 1, then the key is `R1[p]`.

Test vector (our own keys K = 3, 777, 11, 22, 4321, 48223): `R1` begins 137, 9, 115, 157,
241; `R57` begins 243, 133, 153, 255, 121; `R123` begins 243, 69, 217, 255, 57. The version
string "1.95" takes the character keys 9, 115, 157, 241 and is written as the bytes
`06 04 38 5D A4 C4`; `p` is then 5 and the next number key is 241 × 121 × 57 = 1,662,177.

### 2.5 Value encodings

Every value of the body is one of these **(confirmed: binary)**:

| Type | Written as | Key use |
|---|---|---|
| byte | 1 raw byte: value xor (k and 255) | 1 number key |
| word | 2 raw bytes: value xor (k and 65535) | 1 number key |
| int | 4 raw bytes: value xor k | 1 number key |
| bool | One tag byte, 9 for "true" and 8 for "false", of the stored bit `value xor E`, where E is 1 when k is even and 0 when k is odd | 1 number key |
| string | Tag 6, a length byte and the characters (length below 256), or tag 12, a 4-byte length and the characters; each character xor its character key. Latin-1 text. | 1 character key per character; none for the length; none for an empty string |
| float | Tag 5 and an 80-bit x87 extended value, not obfuscated | none |
| set | A word n (encoded as a word above), then ceil(n / 32) ints (each encoded as an int above); member i is bit (i mod 32) of int (i div 32). n is the set's current capacity, which may exceed the highest member. | 1 + ceil(n / 32) number keys |
| text | Raw characters, no tag, not obfuscated (summary only) | none |

Reading rules of the original that matter for writers **(confirmed: binary)**:

- A boolean reads as "true" only for tag 9; any other byte counts as "false" and is not
  an error.
- A string with any tag other than 6 or 12 is an error that ends the load.
- A float may also be given as a compact integer (tags 2, 3, 4); the reader converts it.
- Reading past the end of the file is an error that ends the load.

Floats appear only in a few places (planet conditions; the movement-day accumulators and
the experience of ships, fighters and drones, §3.8; fleet experience, §3.6.12) and hold
64-bit doubles widened to 80 bits.

### 2.6 The plain-text summary

Right after the version string comes a fixed-width text block for tools and dialogs. Each
field is right-aligned in its width and padded with spaces on the left; a longer value is
cut to its first characters **(confirmed: binary)**:

| Width | Content |
|---|---|
| 6 | Version, "  1.95" |
| 6 | Date counter (§3.1), e.g. " 24003" |
| 3 | Number of empires |
| 13 | `Turn Based   ` or `Simultaneous ` |
| 20 | `Same Machine        ` or `Different Machines  ` |
| 3 | Number of empires not under computer control |
| per empire, 129 | 3 empire number; 40 empire name and type; 40 leader title and name; 40 e-mail address (§3.6.1); 6 `Alive ` or `Dead  ` |

The loader skips the block (it reads the empire count from it to know how many rows to
skip) and uses nothing else from it **(confirmed: binary)**. An exporter must write it
with the correct widths and empire count.

### 2.7 Section order of the body

| § | Section | Count |
|---|---|---|
| 3.1 | Prologue: flags, empire count, dates, per-empire names | one |
| 3.2 | Game options | one |
| 3.3 | Victory conditions | one |
| 3.4 | Timed events, then the current player, the game seed and scenario fields | byte count, then one |
| 3.5 | Systems, then the two starting-point lists | byte count, word counts |
| 3.6 | Empires, in player-number order | the empire count of §3.1 |
| 3.7 | Designs | word count |
| 3.8 | Space objects | word count |
| 3.9 | Units launched this turn | word count |

All counts are written before their items. The counts and indices conventions are
described with each section.

---

## 3. The body, section by section

Conventions for every section **(confirmed: binary)**:

- **Players** are numbers 1..20 (`EmpireId` = number − 1); 0 means none.
- **Systems** are 1-based positions in the saved system list (§3.5); 0 means none.
- **Sectors** are one byte, `y × 13 + x` for the 13 × 13 grid of a system (0..168, the
  centre is 84) **(observed: a waypoint set at (8, 3) is stored as 47)**.
- **Objects** are referenced by their 1-based position in the space-object list (§3.8).
- **Designs** are referenced by their 1-based position in the design list (§3.7).
- **Data-file records** are 1-based positions in their file, counting records in file
  order (§4).
- **Dates** are the date counter: game year × 10, so 24000 is 2400.0 and every turn adds 1.
  OpenSE4's `GameState::turn` is the counter − 24000 **(observed)**.
- Counts are written immediately before their items; a list of up to 255 items still
  uses a word count unless stated. Where the original writes a word count but only reads
  its low byte (colony facilities, construction queues), keep the count below 256.
- "Unused" fields are written and read but nothing in the game uses them; export the
  stated value.

The OpenSE4 column names the counterpart in `src/game/state.hpp` and `galaxy.hpp`;
"none" means OpenSE4 has no such state, with what to do on import. §6 lists the reverse
direction.

### 3.1 Prologue

Everything here is **(confirmed: binary)**. The loader reads and discards the fields
marked "ignored".

| Type | Field | Meaning | OpenSE4 |
|---|---|---|---|
| bool | Unused | Always false; ignored | export 0 |
| bool | Unused | Always false; ignored | export 0 |
| bool | Autosave | The file was written by the autosave. On load it opens the next-player screen as for a hand-over file. | export 0 |
| byte | Empire count | Ignored except to count the strings below | `empires.size()` |
| byte | Current player | Ignored (see §3.4) | |
| int | Date counter | Ignored | |
| string | Version | Ignored | "1.95" |
| bool | Hand-over file | Written by the turn-based, different-machines hand-over (§1.1); on load it opens the next-player screen, and hides the main window first in a different-machines game | export 0 |
| N × (string, string, string) | Per empire: full name, leader, race folder | Ignored copies: "name type" and "title leader name" joined by a space and trimmed, and the race folder (§3.6.1) | write the same values |
| byte | Empire count | **The count used** for the empire records | `empires.size()` |
| int | Date counter | **The game date** | `turn` = value − 24000 |
| int | Turn counter | 367 in a new game, +1 per turn **(observed)**; nothing reads it. Present from version 0.52. | none; export 367 + `turn` |
| string | Version | Ignored | "1.95" |

### 3.2 Game options

The game setup's choices, in file order. "Code" values are 1-based positions of the
setup dialog's list items; OpenSE4's 0-based setting is code − 1. Defaults are those of
a new options record. Everything **(confirmed: binary)**.

| Type | Meaning (default) | OpenSE4 `GameOptions` |
|---|---|---|
| word | Quadrant type: QuadrantTypes.txt position (1); used only to generate | `quadrantType` (the record's name) |
| byte | Quadrant size, code: Small, Medium, Large (2) | `quadrantSize` |
| bool | All warp points connected (on) | `allWarpPointsConnected` |
| bool | No warp points | `noWarpPoints` |
| bool | Warp points anywhere in the system | `warpPointsAnywhere` |
| bool | All systems seen by all players | `allSystemsSeen` |
| bool | Omnipresent view | `omnipresent` |
| bool | Finite resources | `finiteResources` |
| bool | All player planets the same size (on) | `allPlanetsSameSize` |
| byte | Event frequency, code: None, Low, Medium, High (2) | `eventFrequency` |
| byte | Maximum event severity, code: Low .. Catastrophic (4) | `maxEventSeverity` |
| byte | Technology cost, code: Low, Medium, High (2) | `techCost` |
| word n, n × string | Allowed tech areas: the names of the areas that can be removed and are checked, alphabetical. An area is allowed when it cannot be removed or its name is listed (compared without regard to case). A Quick Start game lists every area, the fixed ones too (85 names with the stock data); a game made through Game Setup lists only the removable ones (57) **(observed)**; both read the same. | `techAreasAllowed` (by name) |
| byte | Starting resources, code: 5000, 20000, 100000 (2) | `startingResources` |
| byte | Home planet value, code: Bad, Average, Good (2) | `homePlanetValue` |
| byte | Starting planets, code: 1, 3, 5, 10 (1) | `startingPlanets` |
| bool | Players may start in the same system | `sameSystemAllowed` |
| bool | Evenly distributed (on) | `evenlyDistributed` |
| byte | Score display, code: own, allied, all (2) | `scoreDisplay` |
| byte | Tech level for new players, code: Low, Medium, High (1) | `startTechLevel` |
| byte | Racial points, code: 0, 2000, 3000, 5000 (2) | `racialPoints` |
| bool ×2 | Add random computer empires, random neutral empires (setup only) | none; export on, on |
| byte | Number of computer players, code: Low, Medium, High (setup only) | none; export 2 |
| byte | Computer difficulty, code: Low, Medium, High (2) | `aiDifficulty` |
| byte | Computer bonus, code: None .. High (1) | `aiBonus` |
| string | Game master password, plain text | none (server setting); export empty |
| word | Maximum units in space per player (2000) | `maxUnitsPerPlayer` |
| word | Maximum ships per player (200) | `maxShipsPerPlayer` |
| bool | Cheat codes allowed | none; export off |
| bool | Team mode | `teamMode` |
| bool | No tactical combat | `noTacticalCombat` |
| bool ×2 | Unused | export off |
| bool | Players see the complete tech tree | `completeTechTree` |
| bool | Allow gifts and tribute (on) | `allowGifts` |
| bool | Allow technology gifts and trades (on) | `allowTechTrades` |
| bool | Allow surrender (on) | `allowSurrender` |
| bool | Allow intelligence projects (on) | `allowIntel` |
| bool | No ruins | `noRuins` |
| bool | Only breathable atmospheres | `onlyBreathable` |
| bool | Only the home planet type | `onlyHomeType` |
| bool | Players can save the map | `playersCanSaveMap` |
| byte | Play style, code: Same Machine (hotseat), Different Machines (1) | none; export 1 |
| string | Game name (the multiplayer file stem) | none; export empty |
| string | Save folder chosen at setup (empty: the default folder) | none; export empty |
| byte | Connection, code: manual file moving, TCP/IP host, TCP/IP player (1) | none; export 1 |
| byte | Autosave every 0 (none), 1, 2, 3, 5 or 10 turns | `autosaveTurns` |
| bool | Turn-based movement (on) | `simultaneous` = off |
| bool | Simultaneous movement | `simultaneous` |
| int | Combat replay counter | none; export 0 |
| int | Game code: random 1..999,999,999 | none; export 0 |
| int | Host turn code: new at each host save; a player changes file must carry the same game and turn codes | none; export 0 |
| int | Byte sum of the host's program, never checked | none; export 0 |
| int ×7 | Data-set checksums: components, facilities, vehicle sizes, planet sizes, tech areas, mounts, racial traits (§3.2.1). Recomputed when a host processes a turn. Compared when a player (not the game master) signs in to a simultaneous game, hotseat included: a mismatch refuses the sign-in with "Invalid Data Files" **(observed)**. Never checked on a normal load. | none; **export the values computed from the data set (§3.2.1)** |

The game code, the turn code and the seven checksums were already set in a new
simultaneous game saved right after its creation, and all 0 in a Quick Start
(turn-based) game even after two turns **(observed)**.

Setup choices checked by saving a new game made with them **(observed)**: quadrant type
Spiral Arm (the fourth listed) is 4, Small is 1, finite resources on, event frequency
High is 4, maximum severity Medium is 2, number of computer players Low is 1, difficulty
High is 3, bonus Low is 2, autosave every 3 turns is 3, the game name typed is stored,
and simultaneous movement sets the simultaneous flag and clears the turn-based one.

None of these 32-bit values is a random-number state **(confirmed: binary)**.

#### 3.2.1 Data-set checksums

Each of the seven is a sum over the records of one data file, in file order, of the
record's terms below, as 32-bit integers **(confirmed: binary; the formulas give exactly
the seven values found in every save made with the stock data, checked by recomputing
them from the installed files)**. Common rules:

- **p** is the record's 1-based position in its file.
- A text field counts as its length in characters, the value as read (surrounding spaces
  removed).
- A yes/no field counts 1 for True, 0 for False.
- **Cost** counts 2 × minerals + 4 × organics + 6 × radioactives.
- **Requirements** count, for each tech requirement, 4 × the required area's TechArea.txt
  position + 7 × the required level.
- **Abilities** count, for each ability, its ability id (Appendix A) + the length of its
  description + value 1 + value 2. For the abilities Cloak Level and Sensor Level, value 1
  is a sight type name read as EM Active 1, EM Passive 2, Psychic 3, Gravitic 4, Temporal 5;
  every other value is a number.
- Named choices count as their code in the original's list for that field: Physical Type
  1 Planet, 2 Asteroids, 3 Storm, 4 Star, 5 Warp Point, 6 Destroyed Star, 7 Comet; Stellar
  Size 1 Tiny .. 5 Huge; a hull's Vehicle Type 1 Ship, 2 Base, 3 Fighter, 4 Satellite,
  5 Mine, 6 Troop, 7 Drone, 8 Weapon Platform; a component's Vehicle Type 1 Ship, 2 Base,
  3 Fighter, 4 Satellite, 5 Mine, 6 Troop, 7 Drone, 8 WeapPlatform, 9 Ship\Base,
  10 Ftr\Trp, 11 Ship\Base\Sat\WeapPlat\Drone, 12 Ship\Base\Sat\Drone, 13 Ship\Base\Sat,
  14 Ship\Base\Drone, 15 All (the whole text); component Restrictions 1 None,
  2 One Per Vehicle .. 11 Ten Per Vehicle; facility Restrictions 1 None, 2 One Per Planet;
  Weapon Type (and a mount's Weapon Type Requirement) 0 None, 1 Direct Fire, 2 Seeking,
  3 Point-Defense, 4 Warhead, 5 Any; Weapon Damage Type 1..32 and
  Trait Type 1..33 as listed in Appendix B (the damage list differs from the order the
  data files' own notes give); Weapon Display Type 1 None, 2 Beam, 3 Torp,
  4 Seeker; a trait's General Type 1 Advantage, 2 Disadvantage, 3 Neither. Unknown
  names count 0.

| Checksum | Terms per record |
|---|---|
| Components | p + Pic Num + 3 × Tonnage Space Taken + 5 × Tonnage Structure + cost + Vehicle Type + Supply Amount Used + Restrictions + Family + Roman Numeral + Custom Group + requirements + abilities + text of Name, Description and General Group + Weapon Type; and, for a weapon (type not 0): the damages at range (only the numbers followed by a space in the line, at most 20; a last number with nothing after it is not stored) + Damage Type + Reload Rate + Display Type + Weapon Display + Weapon Modifier + text of Weapon Sound + Weapon Family, plus for a Seeking weapon Seeker Speed + Seeker Dmg Res |
| Facilities | p + text of Name, Description and Facility Group + Facility Family + Roman Numeral + Restrictions + Pic Num + cost + requirements + abilities |
| Vehicle sizes | p + text of Name, Short Name, Description, Code, Primary and Alternate Bitmap Name + Vehicle Type + cost + Tonnage + Engines Per Move + requirements + abilities + the yes/no Must Have Bridge, Can Have Aux Con and Uses Engines + Min Life Support + Min Crew Quarters + Max Engines + Pct Fighter Bays + Pct Colony Mods + Pct Cargo |
| Planet sizes | p + Physical Type + Stellar Size + Max Facilities + Max Population + Max Cargo Spaces + Max Facilities Domed + Max Cargo Spaces Domed + Special Ability ID (Max Population Domed is not counted) |
| Tech areas | p + text of Name, Group and Description + Maximum Level + Level Cost + Start Level + Raise Level + Racial Area + Unique Area + requirements |
| Mounts (CompEnhancement.txt) | p + Cost Percent + Tonnage Percent + Tonnage Structure Percent + Damage Percent + Supply Percent + Range Modifier + Weapon To Hit Modifier + Vehicle Size Minimum + Weapon Type Requirement (Shield Percent and Vehicle Size Maximum are not counted) |
| Racial traits | p + Pic Num + General Type + Cost + Trait Type + Value 1 + Value 2 + the positions (in RacialTraits.txt, 0 for None) of Required Trait 1..3 and Restricted Trait 1..3 |

A game made through Game Setup carries them from its creation; a Quick Start game still
carried 0 after two turn-based turns, and a host's turn processing fills them in
**(observed)**. The game code and turn code need not be
set for a sign-in: a file with both at 0 and correct checksums signs in **(observed)**.

### 3.3 Victory conditions

Years are stored in turns (tenths of a year). **(confirmed: binary)**

| Type | Meaning | OpenSE4 |
|---|---|---|
| bool, int | Score condition on; score | `victory.score`, `scoreValue` |
| bool, int | Years condition on; turns | `victory.years`, `yearsValue` = turns / 10 |
| bool, int | Percent of the second empire on; percent | `percentOfSecond`, `percentOfSecondValue` |
| bool, word | Share of technology on; percent | `techPercent`, `techPercentValue` |
| bool, int | Peace condition on; turns | `peace`, `peaceYears` = turns / 10 |
| int | Consecutive peaceful turns so far | `GameState::peacefulTurns` |
| bool, int | Delay before conditions count on; turns | `delay`, `delayYears` = turns / 10 |
| bool | Game completed (a condition was met; no further turns) | `GameState::gameOver` |

OpenSE4 keeps whole years, so a year count that is not a whole number is rounded on
import (lossy).

### 3.4 Timed events and other globals

**Timed events** (events scheduled for a later turn): `byte n`, then n records. A record
whose event is 0 is a free slot; fired events are zeroed, not removed **(confirmed:
binary)**.

| Type | Meaning | OpenSE4 `PendingEvent` |
|---|---|---|
| word | Event: Events.txt position (0 = free slot) | `eventType` = value − 1 |
| int | Date at which it strikes | `fireTurn` = value − 24000 |
| byte | System | `system` |
| byte | Sector of the target object | (none) |
| word | Target: an object for ship, planet, star and warp point events, a player for empire events, a system for system events | `object` / `vehicle` / `empire` by kind |
| byte | Player concerned | `empire` |

Then **(confirmed: binary)**:

| Type | Meaning | OpenSE4 |
|---|---|---|
| byte | System shown in the main window | none (client) |
| byte | Sector selected in it **(observed: follows the map cursor)** | none |
| byte | **Current player** | `playerTurn.empire` in a turn-based game |
| int | **Game random seed**, 0..9998: drawn for a new game; the random generator is reseeded from it on every load and it never changes in play | `seed` (OpenSE4's generator state is lost; reseed) |
| bool | Scenario game (from 0.40) | none; export off |
| bool | The scenario is the tutorial (from 0.49) | none; export off |
| string | Scenario file stem (`<stem>_Settings.txt`) | none; export empty |
| word | Last scenario text page shown (1) | none; export 1 |

### 3.5 Systems

`byte n` (so at most 255 systems), then n records, then the two starting-point lists.
**(confirmed: binary)**

| Type | Field | Meaning | OpenSE4 `StarSystem` |
|---|---|---|---|
| string | Name | | `name` |
| byte | System number | Its position + 1 | `id` |
| byte, byte | Galaxy position | x 1..67, y 1..46 | `position` = (x − 1, y − 1) |
| string | Type description | The SystemTypes.txt description, copied | used to find `type` |
| byte | Physical type | 1 Normal, 2 Nebulae, 3 Black Hole | `physicalType` |
| bool | Empires can start here | Copied from the type | (from `type`) |
| bool | Mask background objects | Copied from the type | (from `type`) |
| bool | Non-tiled centre picture | Copied from the type | (from `type`) |
| string | Background bitmap | Copied from the type | (from `type`) |
| bool | Notes or claims changed this turn | Only for player changes files | none; export off |
| ability list | System abilities | §3.8.1 | `abilities` |
| set | Explored, per empire | Bit p − 1 for player p; usually 20 bits; missing bits are clear | `Knowledge::explored` |
| set | Claimed, per empire | The home system is claimed at setup. The set's capacity varies (after the first turn it was the empire count in every system of our game) and bits beyond it are clear **(observed)** | `Empire::claimedSystems` |
| 20 × string | Notes, per player 1..20 | The Notes window | `Knowledge::notes` |

The system type itself is not stored: generation and stellar manipulation copy the
type's attributes into the system. An importer finds `type` by matching the description,
bitmap and physical type against SystemTypes.txt, falling back to the first type with the
same physical type (inferred rule; the original never needs it).

Then (from version 1.33):

| Type | Meaning | OpenSE4 |
|---|---|---|
| word n, n × (word, word, byte) | Specific starting points: system, sector, player (a later point of the same player wins) | `startingPoints` with `player` = value − 1 |
| word n, n × (word, word) | Common starting points not yet taken: system, sector | `startingPoints` with `kCommonStart` |

The loader drops exact duplicates in both lists. Warp connections are not stored here:
each warp point object holds its destination (§3.8.3).

### 3.6 Empires

One record per empire, in player-number order, as many as the prologue's empire count
(§3.1). The original allows 20 **(confirmed: binary)**. Each record is, in order: the
identity and race fields, the research, intelligence, politics and racial-trait blocks,
three name lists, the empire options, waypoints, repair priorities, tagged mine fields,
systems to avoid, the ministers block, the destroyed flag, the log, the fleets and the
combat strategies. Everything below is **(confirmed: binary)** unless marked.

#### 3.6.1 Identity, race and treasury

| Type | Field | Meaning | OpenSE4 |
|---|---|---|---|
| string | Leader name | | `Empire::leaderName` |
| string | Leader title | | `leaderTitle` |
| string | Empire name | e.g. the race's name | `name` |
| string | Empire type | e.g. "Confederation" | `empireType` |
| byte | Player number | Must equal the record's position + 1 | `id` = number − 1 |
| bool | Computer controlled | | `kind`: Computer (or Neutral with the neutral flag below), else Human |
| bool | "Use race minister style" box | Only the setup dialog reads it | `useRaceMinisterStyle` |
| string | Race folder | The race picked at setup, as typed (often lower case) | `Race::style` (look the folder up without regard to case) |
| string | Art folder in use | Equals the race folder unless that race's main picture is missing; then the original puts a random unused installed race here on every load. The race's AI files are read from this folder too. | none; on import prefer the race folder; export the race folder |
| string | Emblem override folder | Set only for empires born from a revolt, to give them their own emblem | none (cosmetic); export empty |
| string | Ship-name file | File name in the design-names folder | `Race::designNameFile` |
| byte | Home system | | `homeSystem` |
| byte | Home sector | | `homeSector` |
| byte | Native atmosphere | 1 None, 2 Methane, 3 Oxygen, 4 Hydrogen, 5 Carbon Dioxide | `Race::atmosphere` (by name) |
| byte | Native surface | 1 Rock, 2 Ice, 3 Gas Giant | `Race::nativeSurface` |
| string ×3 | Biology, society, history texts | | `Race::biology`, `society`, `history` |
| string | Demeanor | Name | `Race::demeanor` |
| string | Happiness type | Name, matched exactly against Happiness.txt (last match wins; unknown names take the first record) | `Race::happinessModel` (look up by name) |
| int | Experience | The race-age points (combat, tonnage and facilities built; capped at 500,000,000). Nothing in play reads it. | `experience` |
| word | Default formation for new fleets | Formations.txt position; always 1 | none; export 1 |
| word | Default strategy for new fleets | Position in the empire's strategy list; always 1 | none; export 1 |
| word | Planet combat strategy | Position in the strategy list, 0 none; always 1 | none; export 1 |
| bool | Neutral empire | Its art comes from the neutral races' folder | `neutral` / `kind` Neutral |
| byte | Computer difficulty | 1 Low, 2 Medium, 3 High | `aiDifficulty` = value − 1 (export 2 for −1) |
| byte | Unused | | export 100 |
| word | Unused | | export 0 |
| word | Unused | | export 4 |
| byte ×2 | Unused copies of the starting maintenance and reproduction percentages from Settings.txt | The game recomputes both | export the Settings.txt values |
| int ×3 | Stored minerals, organics, radioactives | At most 2,000,000,000; the storage cap is not stored, it is recomputed **(observed: the treasury shown in Empire Status)** | `stockpile` |
| int | Research points for the next research step | | `researchPool` |
| int | Intelligence points for the next intelligence step | | `intelPool` |
| bool ×3 | Can colonize Rock, Ice, Gas Giant | Derived from the native surface and the colonization components researched | none; derive |
| bool ×5 | Breathes None, Methane, Oxygen, Hydrogen, Carbon Dioxide | Only the native atmosphere is ever set | none; derive |
| string | Password | Plain text; the game compares it trimmed and in lower case | `passwordHash`: hash the trimmed, lower-cased text. Export: empty (OpenSE4 cannot recover the text; see §7.4) |
| string | E-mail address | Also the third column of the summary (§2.6) | `email` |

#### 3.6.2 Research

| Type | Field | Meaning | OpenSE4 |
|---|---|---|---|
| bool | Repeat projects | **(observed)** | `repeatResearch` |
| bool | Divide points evenly | **(observed: on in a new game)** | `researchEvenly` |
| word n, n × word | Unique areas unlocked | The tech areas' "Unique Area" values granted by ruins (values, not positions) | `uniqueAreasUnlocked` |
| word n, n × word | Tech levels | Level of tech area 1..n in TechArea.txt order; n is the number of tech areas in the data set when saved | `techLevels` |
| word n, then n × (word, byte, int) | Research queue, at most 12 | Tech area (TechArea.txt position); a share weight, always 2; points spent toward the next level **(observed, also by loading projects written by our encoder)** | `research` (area − 1, progress); export weight 2 |

Spent points exist only for queued areas.

#### 3.6.3 Intelligence

| Type | Field | Meaning | OpenSE4 |
|---|---|---|---|
| word n, then n × (word, byte, int, int) | Intelligence projects | Project (IntelProjects.txt position); target empire (player, 0 none); points spent; specific target, 60000 = "any". The specific target depends on the project's kind: an object position for ships and planets, a tech-area position for technology, a player number for political projects that name a third empire, a system for system projects. | `intel`: `project` = value − 1, `target`, `progress`, and `targetPlanet` / `targetVehicle` / `targetTech` / `thirdEmpire` by kind |
| bool | Repeat projects | | `repeatIntel` |
| bool | Divide points evenly | | `intelEvenly` |

#### 3.6.4 Politics

Twenty entries, one per player number 1..20 (the empire's own entry stays "no contact"):

| Type | Field | Meaning | OpenSE4 |
|---|---|---|---|
| byte | Treaty | 1 War, 2 Non-Intercourse, 3 no contact, 4 None, 5 Non-Aggression, 6 Subjugation, 7 Protectorate, 8 Trade, 9 Trade and Research, 10 Military, 11 Partnership | `Relation::contact` = (value ≠ 3); `Relation::treaty` by name (no contact → None) |
| bool | Dominant | This empire is the master in a Subjugation or Protectorate | `Relation::dominant` |
| word | Trade counter | Reset to 0 when a treaty changes to or from one below Trade; +1 per turn otherwise, at most 60000 | `Relation::tradeTurns` |

Not stored: when the treaty was signed, when the last war was. Derive `treatyTurn` as
the import date; derive `lastWarTurn` from the ministers' "turns since war" (§3.6.9).

#### 3.6.5 Racial traits and characteristics

| Type | Field | Meaning | OpenSE4 |
|---|---|---|---|
| word | Culture | Cultures.txt position, 0 none | `Race::culture` = value − 1 |
| T × bool | Trait flags | One per RacialTraits.txt record, in file order. **T is not stored**: it is the number of traits in the data set that reads the file (14 in the stock data). A data set with another trait count misreads everything after this point. | `Race::traits` (positions of the set flags, − 1) |
| 15 × int | Characteristics in percent | In the order of OpenSE4's `Characteristic` enumeration (Mining is the tenth); 100 is normal; not clamped on load | `Race::characteristics` |

`racialPointsSpent` is not stored; derive it from the traits and characteristics.

#### 3.6.6 Name lists

| Type | Field | Meaning | OpenSE4 |
|---|---|---|---|
| byte n, n × string | Design types | At most 250, no duplicates | `designTypes` |
| byte n, n × string | Colony types | | `colonyTypes` |
| byte n, n × (string, queue) | Queue templates | Named construction queues kept for reuse (the queue record of §3.8.7) | none in the game state; keep in the client's templates or drop |

#### 3.6.7 Empire options

A block of switches and window memories (the Empire Options window and what windows
remember, spec 06 §1.9). Fields in file order; "tab" values are 1-based in the original
and 0-based in OpenSE4.

| Type | Meaning | OpenSE4 `InterfaceOptions` |
|---|---|---|
| byte ×2 | Unused (20) | — |
| bool | Show log at start of turn **(observed)** | `showLogAtTurnStart` |
| byte | Pause after a warp or attack, tenths of a second (not in the window; 0) | none; export 0 |
| bool | Confirm end turn | `confirmEndTurn` |
| bool | Unused (0) | — |
| bool | Confirm scrap | `confirmScrap` |
| bool | Confirm stellar manipulation | `confirmStellarManipulation` |
| bool | Confirm deleting a research project | `confirmDeleteResearch` |
| bool | Confirm deleting an intelligence project | `confirmDeleteIntel` |
| bool | Confirm deleting the first queue item | `confirmDeleteFirstQueueItem` |
| bool | Note similar system-wide abilities | `noteSimilarAbilities` |
| bool | Next/Previous skip ships under construction **(observed)** | `skipUnderConstruction` |
| bool | Ships avoid tagged mine fields | `Empire::avoidTaggedMinefields` |
| bool | Ships avoid restricted systems | `Empire::avoidRestrictedSystems` |
| bool | Next/Previous skip damaged ships | `skipDamaged` |
| bool | Next/Previous stop once per location | `stopOncePerLocation` |
| bool | Next/Previous skip ships in fleets | `skipInFleets` |
| bool | Clear orders on meeting an enemy | `Empire::clearOrdersOnEncounter`: Any if the next flag is set, else Enemy if this one, else Never |
| bool | Clear orders on meeting any other empire | (see previous row) |
| bool | Unused (0) | — |
| bool | Warp point names | `warpPointNames` |
| bool | Planet names | `planetNames` |
| bool | Facility marker group 1 | `facilityMarkers` bit 0 |
| bool | Colonizable markers | `colonizableMarkers` |
| bool | Coordinate location | `coordinateLocation` |
| bool ×11 | Facility marker groups 2..12 | `facilityMarkers` bits 1..11 |
| bool | Galaxy grid lines | `galaxyGridLines` |
| bool | Galaxy warp lines | `galaxyWarpLines` |
| bool | Latest construction items only | `latestConstructionOnly` |
| bool | Latest components only | `latestComponentsOnly` |
| bool | Designs window: statistics/strategy view | none |
| bool | Designs window: hide obsolete | none |
| bool | Choose the colony type on colonization | `Empire::chooseColonyType` |
| byte, byte | System and sector shown when the player's turn last ended (turn-based) | none; export the home system and sector |
| bool | Claim colonized systems automatically | `autoClaimColonized` |
| byte ×4 | Tabs of the Colonies, Planets, Ships\Units and Construction Queues windows | `planetsTab`, `shipsTab`, `queuesTab` (value − 1); Colonies: none |
| byte | Set Construction Queue tab **(observed: changes when the tab is visited)** | none |
| byte | Designs window tab | none |
| byte | Politics window tab | none |
| byte | Log filter: 1 All, 2..8 Construction..Misc | `logFilter` = value − 1 |
| byte ×2 | Cargo and units transfer tabs (3) | none |
| bool ×3 | Ships\Units window: show ships, units, fleets | `shipsShown` bits 0..2 |
| bool ×4 | Construction Queues window: ships, planets, ship yards, planet yards | `queuesShown` bits 0..3 |
| bool | Designer condensed view | `designCondensed` |
| bool | Designer to-hit modifiers | `designToHit` |
| bool ×2 | Galaxy window: show names, show distances | none |
| bool | Planets window: hide systems to avoid | `planetsNoSysToAvoid` |
| bool | Combat simulator: no obsolete designs | `simulatorNoObsolete` |
| bool | System grid | `systemGrid` |
| bool ×4 | Combat replay: fast, animate, grid, viewing rectangle | `replayFast`, `replayAnimate`, `replayGrid`, `replayViewRect` |
| 5 × (byte ×4) | Sort-key history: slot k of the Colonies, Planets, Ships\Units and Construction Queues windows | `coloniesSort[k]`, `planetsSort[k]`, `shipsSort[k]`, `queuesSort[k]` (column numbering: §11.1 question 4) |

The log position and scroll are not stored; import 0.

#### 3.6.8 Waypoints, repair priorities, mine fields, systems to avoid

| Type | Field | Meaning | OpenSE4 |
|---|---|---|---|
| 10 × (string, byte, byte) | Waypoints 1..10 | Name, system (0 = not set), sector **(observed)** | `waypoints` (`set` = system ≠ 0) |
| word n, n × string | Repair priorities | Component group names, first filled from RepairPriorities.txt | `repairPriorities` |
| word n, n × (byte, byte) | Tagged mine fields | System, sector | `taggedMinefields` |
| byte n, n × byte | Systems to avoid | Systems | `systemsToAvoid` |

#### 3.6.9 Computer player memory and ministers

| Type | Field | Meaning | OpenSE4 |
|---|---|---|---|
| byte | Computer player state | 1..10 | `aiState` = value − 1 |
| byte | Staging system | | `aiMemory.staging` |
| byte | Secured system | | `aiMemory.secured` |
| byte | Incursion system | The system of the last incursion damage | none; drop |
| word | Turns in the current state | | `aiTurnsInState` |
| word | After-attack timer | | `aiMemory.afterAttack` |
| word n, n × byte | Systems to defend | | `aiMemory.defend` |
| word n, n × byte | Attack targets, at most 3, may repeat | | `aiMemory.targets` |
| string | Minister style folder | Empty: the race's own AI files | `ministerStyle` |
| 25 × bool | Minister switches | In the order of OpenSE4's `Minister` enumeration (the fifth is Research, **observed**); a new human empire has all off (**observed** in a Quick Start and a Game Setup game) | `ministers` (bit k − 1); `ministerAll` = all 25 on (inferred) |
| bool | New vehicles get individual ministers | | `ministersForNewVehicles` |
| bool | Computer makes no changes in simultaneous games | | `aiMinimalChanges` |
| 20 × (byte, word) | Per player 1..20: anger (default 50); turns since war (default 999, 0 while at war) | | `Relation::anger`, `Relation::turnsSinceWar` |
| byte | Always 0 | | export 0 |
| byte ×7 | Unused | 0, 0, 0, 1, 1, 1, 0 in 204 of the 206 empires of the sample saves, 0, 0, 0, 1, 0, 1, 0 in the other two **(observed)**. These are the values a new ministers record gets; the fifth is cleared when the computer player first notices an enemy star destroyer, and nothing reads any of them **(confirmed: binary)** | export 0, 0, 0, 1, 1, 1, 0 |
| word | Last index used in the ship-name file | | none; derive from the vehicle names, or 0 |
| bool ×8 | Enemy design capability flags | Recomputed every turn; the seventh marks a star destroyer | none; export 0 |
| word | Drone name counter | | none; export the number of drone groups |

#### 3.6.10 Destroyed flag and network name

| Type | Field | Meaning | OpenSE4 |
|---|---|---|---|
| bool | Empire destroyed | Shown as "Dead" in the summary | `alive` = not this |
| string | Network player name | The player name of a network game, used to match reconnecting players | none; export empty |

#### 3.6.11 The log

The log is the empire's message list for about one turn: at the end of each turn the
original keeps only entries dated from one turn ago up to two turns ahead. It is working
state, not only display: replies to diplomatic messages find the original proposal
here, and the computer players' anger counts unread entries.

`word n`, then n entries:

| Type | Field | Meaning | OpenSE4 `LogEntry` |
|---|---|---|---|
| byte | Owner | The player whose log it is | (the list it is in); imported entries keep the byte in `classic.owner` |
| byte | System | 0 none | `location.system` |
| byte | Sector | | `location.sector` |
| int | Date | | `turn` = date − 24000 |
| string | Title | The row text | `title` |
| string | Text | CR LF line breaks | `text` |
| byte | Go-to target | 0 none, 1 location, 2 construction queues, 3 research, 4 intelligence, 5 empire options, 6 designs, 7 empires | `target` (map by meaning) |
| word | Picture key | Meaning depends on the entry kind (below) | `picture` (derive); kept in `classic.pictureKey` |
| bool | Event-style notice | Set on events, mine encounters, stellar changes and riots; no reader found | `classic.eventNotice` (imported entries) |
| byte | Other empire involved | Player, 0 none; the computer players' anger reads it | `classic.otherEmpire` (imported entries) |
| byte | Entry kind | See below | `classic.kind` (imported entries; 0 for OpenSE4's own); derive `picture`, `message` |
| byte | Category | 1 Construction, 2 Research, 3 Intelligence, 4 Events, 5 Politics, 6 Combat, 7 Misc | `category` = value − 1 |
| int | Date first read | 0 unread. The Log window lists only the entries whose read date is 0 or the current date, and stamps the unread ones with the current date; entries read on an earlier turn are hidden **(confirmed: binary; observed: a file whose entries had their own dates as read dates showed an empty Log, the same file with 0 showed them)** | `classic.dateRead` (imported entries, §12); **export 0** for OpenSE4's own |
| byte | Event kind | On event entries | `classic.eventKind` (imported entries) |
| word | Tech area | On new-tech-level entries | `classic.techArea` (imported entries) |
| bool, record | Diplomatic message follows | Present on message entries (kind 19) | `message` → a delivered `DiplomaticMessage` |
| bool, record | Battle details follow | Present on combat entries (kinds 9..14) | `classic.battleNumber`, `classic.battle` (imported entries; see below) |

Entry kinds found **(confirmed: binary; the split of 15..18 is inferred)**: 1 natural
damage to a vehicle, 2 colonization result, 4 vehicle built (picture key = design), 5
facility built (key = facility), 6 ship limit reached, 7 tech level reached, 9 / 10 / 11
battle won / lost / drawn by the player whose turn it was, 12 / 13 / 14 the same for the
other empires, 15..18 intelligence outcomes, 19 diplomatic message, 20 mine field, 21
empire destroyed, 22 event, 23 no storage for a completed item, 24 maintenance or supply
problem, 25, 26 and 52 planet value or conditions changed, 29 / 30 / 31 / 37 new
component / facility / hull / intelligence project (key = data position), 32 / 33 gift
given / received, 34 / 35 contact made / lost, 36 order failed or warning (key =
object), 40 scrap or retrofit result (key = design), 42 damage while moving, 43 treaty
lost, 47 atmosphere changed, 48 / 49 population happy / angry, 51 new stellar
construction, 57 counter-intelligence success, 58 unit launch or recovery warning, 59
password reset. For entries OpenSE4 makes itself, export kind 36 with picture key 0 and
no other empire: that kind shows a generic picture and does not count for anger.

**Diplomatic message record:**

| Type | Field | Meaning |
|---|---|---|
| byte | Message type | 1..38, in the order of spec 05 §7.3's message types: 1 General; 2..5 Propose, Accept, Refuse, Counter Treaty; 6 Break Treaty; 7 Declare War; 8..11 Propose, Accept, Refuse, Counter Trade; 12..14 Give, Accept, Refuse Gift; 15..17 Offer, Accept, Refuse Tribute; 18..34 the seventeen demands and requests (18 want a gift, 19 want a tribute, 20 demand surrender, 21 remove ships, 22 remove colonies, 23 leave planet, 24 stop hostile actions, 25 break a treaty, 26 declare war on, 27 make peace with, 28 support us against, 29 attack an empire in a system, 30 attack a planet, 31 stop espionage, 32 stop sabotage, 33 stop attacks in a system, 34 the generic demand); 35 Surrender; 36 Grant Independence; 37, 38 Accept, Refuse Demand **(confirmed: binary: the original's own name list for message types has exactly this order; observed: in the sample saves 20 is a computer player's surrender demand and 38 a refusal of a request about a third empire)**. Map to `MessageType` by name; OpenSE4 has no Accept/Refuse Tribute and no generic demand. |
| byte | Sender | Player |
| byte | Recipient | Player |
| byte | Tone | 1 pleading, 2 neutral, 3 demanding (`tone` = value − 1) |
| byte | Treaty | Codes of §3.6.4 |
| byte | Third empire | Player |
| word | System | |
| word | Planet | Object |
| word n, n × item | Offered | |
| word n, n × item | Requested | |

Package item: string (display text), byte kind, word value, byte quantity. The text is a
guard, not only a label: whenever an item is shown or carried out, the original compares
it with the current name of the item's subject (the system for kinds 1 and 7, the object's
name for kinds 2, 5 and 6, the tech area's name for kind 4) and treats a mismatch as "Item
Unavailable"; an empty text is not checked, and kinds 3, 8, 9 and the placeholders are
never checked **(confirmed: binary)**. Export must write the subject's current name (as
OpenSE4 does) or nothing; any other wording disables the item. Kinds: 1
system (value = system), 2 planet (object), 3 resource (value = resource 1..5, quantity
= amount in thousands), 4 technology (tech area), 5 ship (object), 6 units (an object: the guard
above compares the object's name, confirmed: binary; the carrier, inferred), 7 star chart (system), 8 treaty (treaty code), 9 comm channel
(player), 10..15 "any" placeholders for planet, technology, ship, units, star chart,
comm channel (quantity = how many, at most 250). OpenSE4's `PackageItem` has no units
kind and no placeholders.

Queued, undelivered messages are not in the game file at all (only in player changes
files); a message exists in a `.gam` only as a log entry.

**Battle details record:** word battle number (its position in the combat replay file),
then for each player 1..20: byte player number, word n, bool took part, n × (string
ship name, string hull code, word hull position or 0) for the forces at the start, word
m, m × (string name, byte damage percent) for the survivors. OpenSE4 has no direct
counterpart: it keeps an imported record as it is (`LogEntry::classic`) to write it back.
An importer could also build a minimal `CombatRecord` per battle (participants, location,
date, outcome per empire from the entry kinds), which is what the anger rules read
(inferred).

#### 3.6.12 Fleets

`word n`, then n fleet records, then a word:

| Type | Field | Meaning | OpenSE4 `Fleet` |
|---|---|---|---|
| word | Fleet number | Its position in this list (1-based); the stored value is ignored on load | map (owner, number) → `id` |
| byte | Owner | Player; read and ignored on load | `owner` |
| string | Name | **(observed)** | `name` |
| byte, byte | System, sector | **(observed)** | `location` |
| float | Experience | 0..50.0 in steps of 0.1 (with rounding noise) | `experience` = whole part, `experienceTenths` = round(fraction × 10) |
| word | Formation | Formations.txt position, 0 none | `formation` = value − 1 |
| word | Strategy | Position in the owner's strategy list | `strategy` = value − 1 |
| bool | Minister control | | `minister` |
| word | Chosen leader | Object position, 0 none | `leader` |
| word (after the list) | Fleets ever created | Only for the next default fleet name ("Alpha Fleet"...) **(observed: 1 after the first fleet)** | none; export the list length |

- A slot whose name is empty and whose system and sector are 0 is a free slot: skip it on
  import, keep it on export so that fleet numbers do not move.
- Members are not listed here. A vehicle belongs to the fleet when its fleet number (§3.8)
  names it; in a saved game some members stand away from the fleet's location.
- After all objects are loaded the original disbands every fleet with no member in the
  fleet's own sector (its slot is cleared and every vehicle naming it is released) and
  moves each remaining fleet to its first member. An importer should do the same.

#### 3.6.13 Combat strategies

`word n`, then n strategies. For computer-controlled empires the original discards these
after loading and reloads the race's AI strategy file, so they matter only for humans.
The fields map to the keys of the strategy data files (spec 04):

| Type | Field |
|---|---|
| word | Position (reassigned on load) |
| string | Name |
| byte, byte | Primary and secondary movement strategy: 1 Don't Get Hurt, 2 Drop Troops (if carrying), 3 Maximum Weapons Range, 4 Optimal Weapons Range, 5 Short Weapons Range, 6 Point Blank, 7 Board Enemy Ships, 8 Ram |
| bool | Use type priority first |
| byte ×4 | Targeting priorities 1..4, each 1..12: Nearest, Farthest, Largest, Smallest, Most Damaged, Least Damaged, Fastest, Slowest, Strongest, Weakest, Has Weapons, Does Not Have Weapons |
| 14 × (byte, bool) | Per target category: type priority rank, don't fire on |
| word | Fighters launch group amount |
| word | Drones per target (default 3) |
| 14 × bool | Break formation per target category (only the first 11 are shown or read from files; the last three stay on, on, off) |
| byte ×4 | Damage percent per ship, planet, fighter group, satellite group |
| bool | Damage until all weapons gone |

Target categories in file order: Bases, Ships, Carriers, Colony Ships, Fighters,
Satellites, Mines, Planets, Transports, Bases (no weapons), Ships (no weapons), Seekers on
us, Seekers on others, Drones. OpenSE4: `Empire::strategies`, keyed by the data-file key
names.

### 3.7 Designs

One list for the whole game: `word n`, then n records. A design's id is its position + 1;
vehicles and queues refer to it by that number, and the stored id is overwritten with the
position on load. A slot whose owner is 0 is a free slot left by the periodic clean-up of
unused designs (its other fields may keep old values); the original reuses the first
free slot for the next design. Everything **(confirmed: binary)**.

| Type | Field | Meaning | OpenSE4 `Design` |
|---|---|---|---|
| word | Id | Position + 1 | `id` |
| byte | Owner | Player; 0 = free slot | `owner`, and the owner's `Empire::designs` |
| word | Hull | VehicleSize.txt position | `hull` = value − 1 |
| string | Design type | e.g. "Attack Ship" | `designType` |
| string | Computer template | The design-creation template's name; empty for hand-made designs | `templateName` |
| string | Name | | `name` |
| int | Date created | | `createdTurn` = value − 24000 |
| bool | Obsolete | | `obsolete` |
| bool | Built at least once | Set by a completed build or a retrofit to it; cleared when re-saved in the designer | `everBuilt`; `retrofitted` = this and `built` = 0 |
| byte | Speed | Movement points, cached | none; **export the computed value** |
| int ×3 | Cost | Minerals, organics, radioactives, cached: hull plus components with their mount adjustments | none; **export the computed value** |
| word n, n × (word, byte) | Parts | Component (Components.txt position); mount (CompEnhancement.txt position, 0 = none) | `entries`: component − 1, mount − 1 (0 → −1) |
| word | Strategy | Position in the owner's strategy list | `strategy` = value − 1 |
| word | Computer type code | 1..39 from a fixed list (§3.7.1), taken from the design type name when it is one of the 39, else worked out from the abilities. The computer players use it to retire older designs of the same code. Free slots keep 0 or an old value. | none; export by design type name (open question Q5) |
| 20 × int | Last seen, per player 1..20 | Date the player last saw the design, 0 never; forgotten after 50 turns. The owner's own entry holds the date of the design's last battle. | other players: their `Knowledge::seenDesigns`; owner: `AiMemory::designsFought` (inferred) |
| ability list | Extra abilities | Always empty in every save seen | none; export count 0 |
| int ×4 | Statistics | Built, lost, scrapped (retrofits and gifts count as scrapped for the old design), enemy tonnage destroyed | `built`, `lost`, `scrapped`, `enemyTonnageDestroyed` |
| bool | Changed this turn | Only for player changes files | none; export off |

The original never recomputes the cached speed and cost on load **(confirmed: binary)**.
OpenSE4's designer Movement (spec 03 §4.4) and design cost give the stored speed and cost
of every owned design of the 24 sample saves, 1,673 designs **(observed)**.

#### 3.7.1 Design type codes

Every code with its name **(confirmed: binary: the original's own name list for these
codes; observed: each seen in the sample saves with the design type name it is taken
from)**:

| Code | Name | Code | Name | Code | Name |
|---|---|---|---|---|---|
| 1 | Attack Ship | 14 | Boarding Ship | 27 | Troop |
| 2 | Defense Ship | 15 | Open Warp Point | 28 | Fighter |
| 3 | Attack Base | 16 | Close Warp Point | 29 | Create Black Hole |
| 4 | Defense Base | 17 | Create Planet | 30 | Destroy Black Hole |
| 5 | Base Space Yard | 18 | Destroy Planet | 31 | Create Nebulae |
| 6 | Population Transport | 19 | Create Star | 32 | Destroy Nebulae |
| 7 | Troop Transport | 20 | Destroy Star | 33 | Satellite Layer |
| 8 | Carrier | 21 | Create Storm | 34 | Kamikaze Attack Ship |
| 9 | Colony (Rock) | 22 | Destroy Storm | 35 | Recon Satellite |
| 10 | Colony (Ice) | 23 | Space Yard Ship | 36 | Cargo Transport |
| 11 | Colony (Gas) | 24 | Mine | 37 | Anti-Planet Drone |
| 12 | Mine Layer | 25 | Satellite | 38 | Anti-Ship Drone |
| 13 | Mine Sweeper | 26 | Weapon Platform | 39 | Drone Carrier |

Designs whose type name is not one of the 39 had the codes spec 05 §7.5's automatic typing
gives (a scout or a missile boat 1, a cargo design 6, a stellar manipulation design 15, a
weapon platform under another name 26) **(observed, eight such names)**.
The stored cost of a stock Quick Start design matches the data with components counted
from 1 and the first hull being "Escort" (confirmed against the data).

### 3.8 Space objects

One list of every object on the map: `word n`, then for each object a class byte, then
the object's record, which begins with the same class byte again. Classes: 1 star, 2 warp
point, 3 storm, 4 planet (asteroid fields too), 5 ship or base, 6 comet, 7 mine field, 8
satellite group, 9 fighter group, 10 drone group. Everything **(confirmed: binary)**
unless marked.

- **Ids.** An object's id is its position + 1; every reference to an object (orders,
  fleet leaders, messages, events, the closing list) uses it. The stored id is overwritten
  with the position on load. OpenSE4: `slot` = id − 1.
- **Free slots.** A removed object is blanked in place and saved as a blank of its class;
  a new object of any class takes the first blank slot. Blank when: star — no name and
  sector type 0; warp point — sector type 0 and destination system 0; storm, comet —
  sector type 0; planet — no name, sector type 0, conditions 0; ship — owner, design or
  system 0; unit groups — owner 0. Import skips blanks but keeps the slot numbers of the
  rest; export writes a blank for every free slot (a storm with all fields 0 is the
  smallest, inferred safe).
- **Order on the map.** Each system's own object list is not saved; after loading, and
  after every move, warp or creation, it is rebuilt from this list in slot order.
- **Location** is a system byte (0 = nowhere) and a sector byte (§3).

#### 3.8.1 Common parts

| Type | Field | Meaning |
|---|---|---|
| byte | Class | |
| word | Id | Position + 1 |
| byte, byte | System, sector | |

**Ability list** (stellar objects, systems, designs; ships always write an empty one):
`byte n`, then n × (word ability id, string description, int value 1, int value 2). The id
is the original's built-in ability number (Appendix A), not a data-file position, so it
survives a changed data set. The description is copied from the data when the ability is
rolled. OpenSE4: `ruleset::Ability` by name (Appendix A), values as given.

**Sector type**: stars, warp points, storms, planets and comets carry a word, the
SectType.txt position of their record. Everything else about the object (size, surface,
atmosphere, star colour, age and luminosity, picture) comes from that record **(confirmed:
binary)**. OpenSE4: `SpaceObject::sectorType` = value − 1, with size, surface and
atmosphere taken from the record. When a planet's atmosphere changes, the original
switches it to a random SectType record with the same size, surface and the new
atmosphere.

#### 3.8.2 Star, storm, comet

Common parts, ability list, sector type; then a star alone has a string, its name. Storms
and comets have no name of their own. OpenSE4: `SpaceObject` of kind Star (or Destroyed
Star by its record), Storm, Comet.

#### 3.8.3 Warp point

Common parts, ability list, sector type, then byte destination system, byte destination
sector. The far end is found by location, not by id. Its name is derived ("Warp Point",
and its destination once explored). OpenSE4: `destination` = the warp point at that
location. Links usually pair up, but not always: the large sample has two warp points
whose destination sector holds no warp point **(observed)**; import them as one-way links
(or with no destination) and note the loss (§7.6).

#### 3.8.4 Planet

| Type | Field | Meaning | OpenSE4 |
|---|---|---|---|
| | Common parts, ability list, sector type | Planet or asteroid record | `SpaceObject` |
| bool | Changed this turn | Only for player changes files **(observed: set when its queue changed)** | none; export off |
| string | Name | **(observed: an edited name shows in the original)** | `name` |
| float | Conditions | 0..1.5 as a double (1.0 for a new planet) | `conditions`, converted exactly |
| int ×3 | Value | Minerals, organics, radioactives: percent, or the remaining stock with finite resources | `value` |
| bool, record | Colony follows | | `colonies[slot]` |

#### 3.8.5 Colony

A colony has no name of its own.

| Type | Field | Meaning | OpenSE4 `Colony` |
|---|---|---|---|
| byte | Owner | | `owner` |
| string | Colony type | By name | `colonyType` |
| population list | Population | §3.8.7 | `population` |
| byte | Anger | Whole percent 0..100 (25 for a new colony) | `anger` |
| byte | Plague level | | `plagueLevel` |
| bool | Cloaked | | `cloaked` |
| byte | Atmosphere counter | Turns with an unbreathable atmosphere (at most 200) | `atmosphereTurns` |
| cargo | Stored cargo | §3.8.7 | `cargo` |
| word n, n × (word, byte, byte) | Facilities | Facility.txt position; how many; how many were destroyed and not yet removed. Each destruction (sabotage, natural events, hazard damage) adds to it and is then removed by subtracting it from the count, but the byte is never reset, so a nonzero value stays behind and is subtracted again at the colony's next removal pass **(confirmed: binary)**; 30 entries of the large sample hold such leftovers, and no entry whose count is 0 holds one **(observed)**. One entry per facility kind; at most 255 entries. | `facilities`: count copies of position − 1; the destroyed count: `destroyedFacilities` (§11.2, §12) |
| queue | Construction queue | §3.8.7 | `queue` |
| unit list | Landed enemy troops | §3.8.7 | `landedTroops` |
| byte | Invading player | 0 none | `invader` |
| word | Militia left to raise | 0 when nobody invades | `militia` (OpenSE4 uses −1 for none) |
| order list | Colony orders | §3.8.8 | `orders` |
| bool | Capital | | `homeworld` |
| bool | Minister control | | `minister` |

Not stored: the founding turn (import 0), the cloak and sensor levels (recomputed on
load). A colony with no population is legitimate: abandoning a planet that keeps its
facilities leaves the record and the owner.

#### 3.8.6 Ship or base

A base is a ship whose design has a base hull.

| Type | Field | Meaning | OpenSE4 `Vehicle` |
|---|---|---|---|
| | Common parts | | `slot`, `location` |
| float | Day accumulator | Simultaneous movement; zeroed at each movement phase | none; export 0 |
| ability list | Always empty | | export count 0 |
| word | Design | Design id | `design` |
| byte | Owner | | `owner` |
| byte | Heading | 0 N, 1 E, 2 S, 3 W, 4 NE, 5 NW, 6 SE, 7 SW | `heading`: OpenSE4's 0..7 clockwise from north are the codes 0, 4, 1, 6, 2, 7, 3, 5 |
| byte | Movement left | **(observed)** | `movement` |
| byte | Maximum movement | Cached; not recomputed on load | none; export the computed value |
| int | Supply | 60000 marks unlimited supply **(observed: −60 per move)** | `supply` |
| byte | Status | 0 normal, 2 under construction, 3 mothballed | `status` |
| float | Experience | 0..50, combat adds 0.1 | `experience` + `experienceTenths` |
| bool | Changed this turn | **(observed: set by an order)** | none; export off |
| bool | Cloaked | | `status` Cloaked |
| string | Name | | `name` |
| word | Fleet | Position in the owner's fleet list, 0 none **(observed)** | `fleet` |
| bool | Minister control | | `minister` |
| order list | Orders | §3.8.8 **(observed)** | `orders`, `repeatOrders` |
| cargo | Cargo | §3.8.7 | `cargo` |
| set | Destroyed parts | Bit i − 1 set when the design's part i is destroyed; the set's size is the design's part count (every ship of the samples) | `damage`: destroyed parts at full structure, the rest 0 |
| bool, queue | Own construction queue | Ships and bases with a space yard | `queue` |

Not stored: partial damage of a part (only destroyed or intact), the place it came from,
the turn it was built, the movement-blocked turn, arrival stamps.

#### 3.8.7 Shared records

- **Population list**: `word n`, then n × (byte player whose race it is, int millions).
  On load equal players merge (at most 60,000) and entries of 0 or less are dropped.
  OpenSE4: `PopulationGroup` with `race` = player − 1.
- **Unit list**: `word n`, then n × (word design id, word count, word killed in the current
  battle, 0 between turns). Design ids from 65000 up are militia of race (id − 65000),
  made during ground combat (whether they reach a save is open, question 9). OpenSE4:
  `UnitStack`.
- **Cargo**: population list, then a bool "units follow" and, when set, a unit list. The
  flag says whether the cargo holds a unit list at all: the list is made when units are
  first added and freed only by two tidying paths when it is empty, so the flag may be set
  over an empty list (144 of about 1,500 cargo records of the sample saves, **observed**).
  Every reader tests the flag first, so a clear flag and an empty list read the same
  **(confirmed: binary)**.
  OpenSE4: `Cargo`.
- **Construction queue**: word item count, bool on hold, bool emergency, bool repeat,
  byte rally waypoint (1..10, 0 none), byte emergency or slow-mode turns counter, three
  ints spent on the top item (minerals, organics, radioactives), then the items, each a
  byte kind (1 facility, 2 vehicle or unit, 3 facility upgrade), a word (facility
  position or design id) and a word count **(observed for kind 2)**. At most 255 items.
  OpenSE4: `ConstructionQueue`; the counter goes to `emergencyTurns` while in emergency,
  else to `slowTurns`; `autoWaypoint` = waypoint − 1; only the top item's `spent` exists.
  Ships added one by one are separate items; units added "× 5" are one item with count 5
  **(observed)**.

#### 3.8.8 Orders

`word n`, bool repeat, word current order (1-based), then n orders, each: byte kind, byte
system, byte sector, byte extra, word target, string target name (shown in windows).
OpenSE4 keeps the current order first: on import rotate the list so that the current order
comes first; export the current order as 1.

| Kind | Order | Parameters | OpenSE4 `OrderKind` |
|---|---|---|---|
| 1 | Move To | system, sector **(observed)** | MoveTo |
| 2 | Move To Waypoint | target = waypoint 1..10 | MoveToWaypoint (`amount` = waypoint − 1) |
| 3 | Warp | target = warp point | Warp |
| 4 | Colonize | target = planet | Colonize |
| 6 | Load cargo | extra = cargo kind: 1 population, 2 troops, 3 fighters, 4 mines, 5 satellites, 6 drones, 7 weapon platforms (loads all of that kind) | LoadCargo (by kind, not by design: lossy both ways) |
| 7 | Drop cargo | extra = kind, target = planet (0 here) | DropCargo |
| 8 | Attack | none: it attacks where the group stands (the turn-based Attack of spec 03 §8) | Attack without a target |
| 9 | Scrap | | Scrap |
| 10, 12 | Seek a location | system, sector. Nothing in this version creates kind 10; kind 12 comes only from the ministers and computer players **(confirmed: binary)** | 10: MoveTo (inferred); 12: Seek |
| 11, 13 | Seek a target | target = object (its system, sector and name when given). Kind 11 is the player's Attack on a target in a simultaneous game, or by drones in any game: the pursuit of spec 03 §8, kept until the target is gone. Kind 13 comes only from the ministers and computer players and lasts one movement phase **(confirmed: binary)** | 11: Attack with the target (the pursuit); 13: Seek |
| 15 | Sweep mines | | SweepMines |
| 17 | Open warp point | target = the system to link to | StellarManipulation |
| 18, 20, 22, 24 | Close warp point, destroy storm, destroy planet, destroy star | target = the object | StellarManipulation |
| 19, 21, 23, 61, 63 | Create storm, create planet, create star, destroy nebulae, destroy black hole | act where the ship is | StellarManipulation |
| 60, 62 | Create nebulae, create black hole | target = an object (a star for nebulae) | StellarManipulation |
| 66 | Stellar construction | target = object, a planet-size parameter (inferred: in extra) | StellarManipulation (CreateConstructedPlanet) |
| 25 | Sentry | | Sentry |
| 34, 35 | Launch, recover units | extra = unit kind in the cargo-kind numbering of Load: 2 troops, 3 fighters, 4 mines, 5 satellites, 6 drones, 7 weapon platforms; 0 every kind **(confirmed: binary)**. Not the closing list's vehicle types (§3.9), which number mines and satellites the other way round | LaunchUnits, RecoverUnits (by kind) |
| 42..46 | Analyze, mothball, unmothball, self-destruct, fire on | | Analyze, Mothball, Unmothball, SelfDestruct, FireOn |
| 47 | Join fleet | target = fleet number | JoinFleet |
| 52 | Retrofit | target = design | Retrofit |
| 58 | Use component | target = part position (1-based) | UseComponent (`amount` = position − 1) |
| 59 | Use facility | target = position in the grouped facility list (1-based) | UseFacility |
| 64 | Abandon planet | target = 1: scrap the facilities first | none |
| 65 | Convert resources | extra = from (1..3), system = to (1..3), target = amount (at most 65,000) | ConvertResources |

What the saves hold **(observed)**: a Move To or a Seek of a location gives the system and
sector; a Colonize gives the planet with its system and sector and an empty target name; a
Seek of a target gives it with its place and name; a Load gives only the cargo kind, a Drop
the kind and the planet, without system or sector. No list of the samples had a current
order other than 1.

Kinds 12 and 13 last one movement phase in simultaneous games. Explore, Resupply, Repair,
Cloak and Decloak are never stored: the original turns the first three into Move To
orders when given, and cloaking is immediate.

#### 3.8.9 Mine field, satellite group

Common parts, then byte owner, bool changed this turn, unit list (one stack per design),
order list, bool minister control, bool cloaked. No supply, no movement. OpenSE4: a
`Vehicle` whose `design` is the first stack, `count` the total, `mixed` the stacks when
there are two or more.

#### 3.8.10 Fighter group, drone group

Common parts, then float day accumulator, byte movement left, byte maximum movement, int
supply, byte heading, byte owner, bool changed this turn; a drone group then has its name
(string); then word fleet, bool minister control, unit list, order list. The maximum
movement is recomputed on load (the slowest design, 1 without supply) and supply is
clamped to it; the cloaked state of fighters and drones is not saved. OpenSE4: `Vehicle`
as for mine fields, with `movement`, `supply`, `heading`, `fleet`.

### 3.9 Closing list: units launched this turn

`word n`, then n × (word launcher object, byte unit kind: 3 fighters, 4 satellites, 5
mines, 7 drones, word count). It is cleared at each turn processing, so in a turn-based
game it covers every player turn of the round **(confirmed: binary)**. Read from version
1.17. OpenSE4: `PlayerTurn::launched` (kind to the launcher's ability kind); export it
empty between turns.

---

## 4. References to the data set, and mods

The save names the installed data set nowhere: the mod folder is chosen for the whole
installation in `Path.txt`, not per game **(confirmed: binary)**. Most references to
data-file records are positions, so a save is only meaningful with the data set it was
made with **(confirmed: binary)**.

| Kind of reference | Items |
|---|---|
| 1-based record position in the data file | TechArea.txt (levels, research queue, log, messages), RacialTraits.txt (one flag per record, **count implied**), Cultures.txt, IntelProjects.txt, Formations.txt, VehicleSize.txt (hulls), Components.txt, CompEnhancement.txt (mounts), Facility.txt (colony facilities, queue items, log keys), SectType.txt (every stellar object), Events.txt (timed events), QuadrantTypes.txt (options) |
| Name | Happiness type, design types, colony types (lists and colonies), repair priorities, allowed tech areas, race and art folders, ship-name file, minister style folder, computer design templates |
| Copied text | System type attributes (§3.5), ability descriptions |
| Built-in numbers | Ability ids (Appendix A), order kinds, cargo and unit kinds, treaty codes, message types, log entry kinds, headings, statuses, setup codes |
| Positions in the save | Players, systems, objects, designs, fleets (per empire), strategies (per empire) |

What a different data set does to a save **(confirmed: binary unless marked)**:

- **Racial traits**: a different number of records shifts the rest of the file; the load
  fails or reads garbage.
- **Tech areas**: the count is stored, so the file reads, but levels attach to whatever
  area now has that position.
- **Everything else by position**: read without bounds checks. Inserting or removing
  records silently changes what the save means; a position past the end gives garbage or
  a crash (inferred).
- **Names**: robust to reordering; unknown happiness types fall back to the first record.

For OpenSE4 this means:

- OpenSE4's ruleset keeps every record of each file in file order, so an original
  position p is OpenSE4 index p − 1 when both load the same files (OpenSE4's loaders push
  each record in order; check with the counts in the save: the tech-level count must
  equal the number of tech areas, 85 in the stock data).
- **Import** must use the data set the game was played with (normally the installed
  one), and should reject a save whose stored counts disagree with the ruleset (tech
  areas) or whose positions fall outside it.
- **Export** must write positions for the data set the original will load. An OpenSE4
  game made with another ruleset cannot be exported meaningfully.
- Many saves carry seven data-set checksums in the options (§3.2). Three saves made with
  the stock data (two simultaneous, one turn-based) carry the same seven values, and a
  Quick Start hotseat game still had all seven at 0 after two turns **(observed)**. When
  set they identify the data set: an importer can recompute them from its ruleset's files
  (§3.2.1) and warn on a mismatch, and an exporter must write them for simultaneous games.

## 5. Loading in the original

### 5.1 What the loader checks

Nothing beyond the stream itself **(confirmed: binary)**: no magic number, no checksum,
no upper version bound (the beta-authorization and demo checks of this build always
pass). The stored check value K6 is not compared (§2.2). A load fails only when the
stream is broken: a string with a wrong tag, reading past the end, or a range error in
the runtime. The player then sees an "Unable to load game" message, and the previous
game is gone (every empire slot is freed before reading).

### 5.2 Version gates

The loader reads one stream for every release since the early betas: each field added
later is read only when the stored version string is not older than the release that
added it. The comparison is a plain string comparison, so "1.95" is newer than "0.47"
and "1.17". Both sample saves and every save we made are "1.95", which has every field;
this spec describes that version. An importer should accept "1.95" and refuse anything
else unless it implements the gates; the main ones are listed here so that refusing is a
choice (all **confirmed: binary**):

| Section | Fields and the version that added them |
|---|---|
| Prologue | Plain-text summary 0.47; turn counter 0.52 |
| Options | allowed tech areas fully stored 1.08 (before: all allowed); same-size planets 1.08; gifts and trades 0.29; surrender 1.39; intelligence 1.13; ruins and atmosphere limits 1.25; save map 1.31; save folder 1.10; connection 1.43; replay counter 1.12; codes and sums 1.72 / 1.42; units limit scaled ×50 before 1.22 |
| Globals, systems | Scenario fields 0.40 / 0.49; physical type 0.30; start flag 1.17; centre picture 0.29; changed flag 1.03; notes for 20 players 0.46; starting points 1.33; closing list 1.17 |
| Empire | neutral 0.25; emblem folder 0.30; happiness type 0.53; password and e-mail position 0.64; repair priorities 0.26; network name 1.46; research flags 0.27–0.42; intelligence flag 0.35; politics for 20 players 0.46; design types extended 1.51; options 0.26–1.78; ministers 0.26–1.51; computer memory 0.21–0.65 |
| Lists | message item text 1.22; battle details for 20 players 0.39, damage 0.61; fleet experience as a float 1.46, minister 0.28, leader 1.09, counter 1.05; strategy fields 0.64–1.81 |
| Designs | template name 1.62; mounts 0.26; type code 0.33; 20 sightings 0.39; scrapped 1.14; changed flag 1.03 |
| Objects | ship supply as int 0.24, experience as a float 1.46, changed flag 1.03, cloak 0.27, minister 0.28; colony population list 0.46, cloak 0.27, atmosphere counter 0.42, militia 0.63, orders 0.30, capital 0.45, minister 0.28; population amounts as int 0.65; queue counter 0.28; order names 1.51; drone names 1.51 |

### 5.3 What the original recomputes or changes after loading

**(confirmed: binary)**

- The random generator is reseeded from the stored game seed (§3.4), on every load.
- Object ids, design ids, fleet numbers and strategy positions are set from list
  positions; stored ids are ignored.
- Each system's object list is rebuilt from the object list in slot order.
- Fleets with no member in their own sector are disbanded; the others move to their
  first member.
- The empires' construction-queue lists are rebuilt.
- Computer-controlled empires lose their saved strategies and get the race's AI
  strategy file again.
- Pictures and colours are reloaded: the empire colour is not saved but taken from the
  emblem picture; a missing race picture makes the original pick another installed race
  for the art folder.
- Colony and ship sensor and cloak levels are recomputed; a colony that can no longer
  cloak decloaks; a ship whose space yard no longer works loses its queue.
- Fighter and drone maximum movement is recomputed and their supply clamped.
- System names are first drawn at random from SystemNames.txt and then replaced by the
  saved names (harmless thanks to the reseed).
- Not recomputed and trusted as stored: design speed and cost, ship maximum movement,
  every data position.
- The `history\` folder is emptied and refilled from the companion files (§1.2); a
  scenario's files are reloaded for a scenario game; a turn-based game reloads its
  combat replay file when replays are on.
- Then the game resumes: a simultaneous game asks the players to sign in, a TCP/IP host
  asks whether to go on hosting, and a hand-over file or an autosave opens the
  next-player screen.

---

## 6. OpenSE4's state: where each field comes from and where it goes

The section tables of §3 map each original field to OpenSE4. This section goes the other
way, for every field of `GameState` and the structs it holds (`src/game/state.hpp`,
`galaxy.hpp`; the field list of `serialize_io.hpp`). "Derive" means compute it from
other imported state; "default" means the struct's default. Fields that hold only
memory between turns and have no source are a loss on import (§7.6).

### 6.1 GameState

| Field | Import from | Export to |
|---|---|---|
| `turn` | Prologue date − 24000 | Date counter in the summary, the prologue (three copies) and the turn counter (367 + turn) |
| `seed` | Game random seed (§3.4) | `seed` mod 9999 |
| `rng` | Reseed from `seed`, as the original does | (not written) |
| `options` | §3.2, §3.3 | §3.2, §3.3 |
| `galaxy.width`, `height` | 67 × 46, the original's fixed quadrant grid | (not written) |
| `galaxy.quadrantType` | Options quadrant type (record name) | Its QuadrantTypes.txt position |
| `galaxy.systems` | §3.5 | §3.5 |
| `galaxy.objects` | Classes 1, 2, 3, 4, 6 of §3.8 (blanks skipped) | Same, in slot order, blanks for free slots |
| `colonies` | Planet colony records | Planet colony records |
| `empires` | §3.6 | §3.6 |
| `designs` | §3.7, free slots as obsolete placeholders owned by nobody's list | §3.7 in id order; a placeholder as a free slot (owner 0) |
| `vehicles` | Classes 5, 7, 8, 9, 10 | Same, at their slots |
| `fleets` | Every empire's fleet list, renumbered into global ids (keep a map from (owner, number)) | Each owner's list, a fleet's number being its position there; free slots where numbers would otherwise move |
| `messages` | Message records of log entries dated the current turn, as delivered and unanswered | Delivered, unanswered messages as log entries of the recipient (kind 19 with the message record); undelivered ones cannot be written (lossy) |
| `pendingEvents` | Timed events (§3.4), free slots skipped | Timed events (five or more slots block later scheduling in the original, §11.2) |
| `pendingMood` | Not stored (the original keeps it in memory too): empty | — |
| `combats` | Not in the file; optionally minimal records from the log's battle details | Battle details of the log entries (§3.6.11) |
| `nextVehicleId`, `nextFleetId`, `nextMessageId`, `arrivals` | Derive from the imported lists | — |
| `peacefulTurns` | Victory block counter | Same |
| `gameOver` | Victory "game completed" | Same |
| `winner` | Not stored: derive from the victory rules, else invalid | — |
| `playerTurn.empire` | Current player (turn-based games) | Current player (simultaneous games: the empire count, as in the simultaneous sample, observed) |
| `playerTurn.started` | Not stored: true (the original saves within a player's turn, inferred) | Not stored: a human current player's turn that has not started is started on a copy first (§12) |
| `playerTurn.moves`, `questions` | Not stored: empty | — |
| `playerTurn.launched` | Closing list (§3.9) | Closing list |
| `startingPoints` | Starting-point lists (§3.5) | Same |
| `leftFacilities` | Always empty | — |

### 6.2 Galaxy

| Field | Import from | Export to |
|---|---|---|
| `StarSystem.id`, `name`, `position`, `physicalType`, `abilities` | System record | System record |
| `StarSystem.type` | Match on description, bitmap and physical type (§3.5) | Copy the type's attributes into the record |
| `StarSystem.objects` | Derive: the system's objects in slot order | (not written) |
| `SpaceObject.id`, `slot` | Position | Position |
| `SpaceObject.kind` | Class, and for planets and stars the SectType record's physical type (Planet / Asteroids, Star / Destroyed Star) | Class |
| `system`, `sector`, `name`, `abilities` | Common parts, name, ability list | Same |
| `sectorType` | Sector type − 1 | + 1 |
| `size`, `surface`, `atmosphere`, `starAge`, `starColor`, `starLuminosity` | Derive from the SectType record | (implied by the sector type) |
| `conditions`, `value` | Planet record | Planet record |
| `destination` | The warp point at the stored destination (none for a one-way link) | The destination's system and sector |

### 6.3 Empire

| Field | Import from | Export to |
|---|---|---|
| `id`, `name`, `empireType`, `leaderTitle`, `leaderName`, `email` | §3.6.1 | §3.6.1, the summary and the prologue copies |
| `race` (style, design names, biology, society, history, demeanor, happiness model, surface, atmosphere) | §3.6.1 | §3.6.1 |
| `race.characteristics`, `traits`, `culture` | §3.6.5 | §3.6.5 |
| `color` | Not stored: the original takes it from the emblem picture; derive the same way or default | — |
| `kind`, `neutral` | Computer flag and neutral flag | Same |
| `alive` | Destroyed flag | Same, and "Dead" in the summary |
| `passwordHash` | Hash of the trimmed, lower-cased password | Empty password (§7.4) |
| `racialPointsSpent` | Derive | — |
| `stockpile`, `researchPool`, `intelPool` | §3.6.1 | §3.6.1 |
| `economy` | Derive at the next turn processing | — |
| `techLevels`, `research`, `researchEvenly`, `repeatResearch`, `uniqueAreasUnlocked` | §3.6.2 | §3.6.2 |
| `intel`, `intelEvenly`, `repeatIntel` | §3.6.3 | §3.6.3 |
| `relations` | §3.6.4 and the ministers block (anger, turns since war); the rest default | Same |
| `knowledge.explored`, `notes` | System records | System records |
| `knowledge.present`, `visibleVehicles` | Recompute | — |
| `knowledge.knownWarpLink` | Not stored: derive (known when the empire has explored the warp point's system, inferred) | — |
| `knowledge.lastSeen` | Not stored: the import date for explored systems (inferred) | — |
| `knowledge.seenDesigns` | Designs' last-seen dates of this player | Same |
| `homeSystem`, `homeSector` | §3.6.1 | §3.6.1 |
| `claimedSystems` | Systems' claimed sets | Same |
| `systemsToAvoid`, `taggedMinefields`, `waypoints`, `repairPriorities` | §3.6.8 | §3.6.8 |
| `designTypes`, `colonyTypes` | §3.6.6 | §3.6.6; queue templates: an empty list |
| `strategies` | §3.6.13 | §3.6.13 |
| `designs` | Designs owned by the empire | (owner field of each design) |
| `log` | §3.6.11 | §3.6.11 |
| `historyEvents`, `history` | Optional: companion files (§1.2, §11.1 question 8) | Optional: companion files |
| `experience` | §3.6.1 | §3.6.1 |
| `aiState`, `aiTurnsInState`, `aiMemory.targets`, `staging`, `secured`, `defend`, `afterAttack` | §3.6.9 | §3.6.9 |
| `aiMemory.avoid`, `attackSystems`, `metMinefield` | Not stored: default | — |
| `aiMemory.designsFought` | The owner's own last-seen entry of each design (inferred) | Same |
| `aiMinimalChanges`, `aiDifficulty`, `ministerStyle`, `useRaceMinisterStyle`, `ministersForNewVehicles`, `ministers`, `ministerAll` | §3.6.1, §3.6.9 | Same |
| `politicsMark` | Not stored: default | — |
| `clearOrdersOnEncounter`, `avoidTaggedMinefields`, `avoidRestrictedSystems`, `chooseColonyType`, `interfaceOptions` | §3.6.7 | §3.6.7 |
| `colonyTypeChoices` | Not stored: empty | — |

### 6.4 Colonies, designs, vehicles, fleets

| Field | Import from | Export to |
|---|---|---|
| `Colony` (all fields but those below) | §3.8.5 | §3.8.5 |
| `Colony.foundedTurn` | Not stored: 0 | — |
| `Colony.cloakLevels`, `sensorLevels` | Recompute from the facilities, as the original does on load | — |
| `Colony.militia` | Militia word, −1 when no invader | 0 when none |
| `Design` (all fields) | §3.7 | §3.7, with computed speed, cost and type code |
| `Vehicle.id` | New ids in slot order | — |
| `Vehicle.slot` | Position | Position |
| `Vehicle.owner`, `design`, `name`, `location`, `supply`, `movement`, `orders`, `repeatOrders`, `cargo`, `status`, `minister`, `queue`, `heading` | §3.8.6, §3.8.9, §3.8.10 | Same |
| `Vehicle.count`, `mixed` | Unit groups' unit list | Unit list |
| `Vehicle.fleet` | Fleet number of the owner, mapped | Position in the owner's fleet list |
| `Vehicle.damage` | Full structure for destroyed parts, 0 for the rest | Destroyed set: parts whose damage reaches their structure (partial damage lost) |
| `Vehicle.experience`, `experienceTenths` | Float experience | Float experience |
| `Vehicle.builtTurn`, `immobileUntil`, `cameFrom`, `cameFromTurn` | Not stored: default | — |
| `Vehicle.arrival` | Not stored: number in slot order (the original orders each system by slot) | — |
| `Fleet` | §3.6.12 | §3.6.12 |

---

## 7. Export: writing a file the original loads and plays

### 7.1 Procedure

1. Check that OpenSE4's ruleset is the data set the original will load (same record
   counts per file, in particular 14 racial traits in the stock data; §4). Refuse
   otherwise.
2. Check the limits: at most 20 empires, 255 systems, 65,535 objects and designs, 255
   facility kinds per colony, 255 queue items, systems and sectors in byte range. Text
   must be Latin-1; replace other characters.
3. Draw keys as the original does (§2.2): K1 in 1..10, K2..K5 in 1..10000, K6 the table
   value for K1.
4. Write the version string, the summary (§2.6) and the body (§3) in order, with every
   count before its items.
5. Optionally write the companion statistics files (§1.2, §11.1 question 8).

### 7.2 Values the original trusts and does not recompute

Write them correctly or the game misbehaves silently **(confirmed: binary)**:

- design speed and cost (§3.7) and the design type code (§11.1 question 5);
- ship maximum movement (§3.8.6);
- ids that equal positions: object ids, design ids, fleet numbers, player numbers,
  system numbers;
- every data-file position (§4);
- a ship's destroyed-part set sized to its design's part count;
- the racial-trait flags, exactly one per RacialTraits.txt record.

### 7.3 Fields OpenSE4 does not hold: what to write

| Where | Write |
|---|---|
| Summary | As §2.6, from the same values as the body |
| Prologue | Unused flags off; autosave and hand-over flags off; copies equal to the real values; turn counter 367 + `turn` |
| Options | Setup-only fields at their defaults (random empires on, number of computer players 2); game master password, game name and save folder empty; play style 1 (same machine) and connection 1; replay counter, codes and program sum 0; **the seven checksums computed from the data set (§3.2.1)**, without which no player can sign in to a simultaneous game **(observed)** |
| Globals | Main-window system and sector: the current player's home system and sector; scenario fields off, empty, 1 |
| Systems | "Changed" off; type attributes copied from the system's SystemTypes.txt record |
| Empires | Art folder = race folder; emblem folder, network name and password empty; unused fields as stated in §3.6; default formation, fleet strategy and planet strategy 1; capability flags off; name counters from the vehicles; window memories without counterparts at their defaults (tabs 1, transfer tabs 3); the system and sector shown at turn end: home system and sector |
| Log | OpenSE4's entries as kind 36, picture key 0, no other empire, **read date 0** (§3.6.11); imported entries with the kind, picture key, other empire, event kind, tech area and read date they came with (§12); diplomatic entries as kind 19 with the message record; combat entries as kinds 9..14 with their battle details |
| Designs | Speed, cost and type code computed; last-seen dates from `seenDesigns`; changed flag off; empty ability list |
| Objects | Day accumulators 0; changed flags off; killed units 0; militia 0 without an invader; blanks for free slots |

### 7.4 Passwords

OpenSE4 stores only a hash, and the original needs the plain text. Export writes an
empty password for every empire (anyone can then play it) and tells the user; the player
sets a new one in the original (Change Password). Import keeps the original's password
as OpenSE4's hash, so the same password works in OpenSE4.

### 7.5 What the original does to an exported file on load

Expect these changes after loading (§5.3): fleets without a member in their own sector
are disbanded; computer empires get their AI strategy files back (strategy positions
used by their fleets and designs may then point elsewhere); empire colours come from the
emblem pictures; the random generator restarts from the stored seed, so the original's
next turn does not continue OpenSE4's random sequence.

### 7.6 What is lost

| Direction | Lost or approximated |
|---|---|
| Both | Partial damage of a ship's parts (only destroyed or intact is stored); load and drop orders by cargo kind in the original versus by design in OpenSE4; the random sequence (reseeded from the seed); undelivered diplomatic messages; window memories that only one side has |
| Import | The original's AI memory not covered by §3.6.9 (OpenSE4's `Relation` memory fields, `politicsMark`, `aiMemory.avoid`, `attackSystems`, `metMinefield` start at their defaults); `treatyTurn`, `lastWarTurn`, `foundedTurn`, `builtTurn` and the per-system last-seen turns are unknown; combat records exist only as log summaries; one-way warp links become links with no far end (OpenSE4 expects pairs); the queue templates; the original's game-master password |
| Export | Fractional victory years cannot come from OpenSE4 (whole years only, no loss); OpenSE4's history and statistics unless the companion files are written; `arrival`, `cameFrom`, `immobileUntil` (blocked movement after sabotage or events is forgotten); OpenSE4's own log targets beyond the original's seven; passwords (§7.4) |

Estimate: everything needed to rebuild a playable game is stored in the `.gam` and
mapped above, so import can be faithful for the board, empires, designs, vehicles,
queues, orders and diplomacy state; the computer players restart with partly empty
memories. Export can produce a file the original loads and plays (proved for the
container and an edited field, §9); its fidelity rests on computing the cached design
and movement values exactly as the original does and on an identical data set.

---

## 8. Verification aids

Tests an implementer can write without the original:

1. **Key stream**: reproduce the test vector of §2.4.
2. **Round trip of the container**: decode a file, encode it again with new keys, decode
   again: every value must be equal (our own tool does this for every save we made).
   The re-encoded file differs only in the key header length and the obfuscated bytes.
3. **Parse to the end**: decoding must consume the file exactly; a remainder or an
   overrun means a wrong count or type. Run it on every `.gam` in the installed
   SaveGame folder as an opt-in test under `OPENSE4_CLASSIC_DATA` (no saves are fixtures).
4. **Invariants** that held in every save examined (the two found ones, our sixteen and
   three files of our encoder):
   - player number = position + 1; system number = position + 1; design id = position +
     1; object id = position + 1; fleet number = position + 1;
   - every ship's design id is within the design list; every ship's fleet number is
     within its owner's fleet list;
   - every ship's destroyed-part set has as many bits as its design has parts;
   - every empire's tech-level count equals the number of tech areas (85 stock);
   - the racial-trait count is 14 with the stock data;
   - warp points pair up (destination of the destination is the warp point itself),
     except one-way links (two in the large sample, none elsewhere);
   - the summary's empire count equals the prologue's.
5. **Import–export–import**: an imported game exported and imported again must give the
   same OpenSE4 state for every field that §6 maps both ways.

Expected counts (only counts; the files are not in the repository):

| Save | Size (bytes) | Version, date | Empires | Systems | Designs | Objects by class | Other |
|---|---|---|---|---|---|---|---|
| Small test game (install's SaveGame) | 72,921 | 1.95, 24003 | 5 | 56 | 56 | 54 stars, 136 warp points, 38 storms, 550 planets, 4 ships | 5 colonies, 8 log entries, 54 strategies, no fleets, no blanks |
| Large third-party game | 646,255 | 1.95, 25214 | 20 | 81 | 1,421 (593 free) | 68 stars, 168 warp points, 52 storms, 788 planets, 940 ships, 48 mine fields, 50 satellite groups, 14 fighter groups, 271 drone groups | blanks: 25 ships, 12 fighter groups, 61 drone groups; 404 colonies; 324 log entries; 121 fleet slots; 219 strategies; 3 timed-event slots; 2 one-way warp points |
| Our Quick Start (Terran) | about 83,160 (varies with the keys) | 1.95, 24000 | 9 | 76 | 11 | 68 stars, 202 warp points, 49 storms, 617 planets | 9 colonies, no log entries, 99 strategies |

---

## 9. Checked in the running original

All **(observed)**, in a scratch copy of the original under Wine, 2026-10-04:

- A Quick Start saved twice without any action between: the files differ byte for byte,
  the decoded values are identical.
- Saves one action apart, decoded and compared: adding a research project, switching
  Repeat Projects, queueing a ship, queueing several ships and a batch of units, setting
  a waypoint, switching a minister on, switching two empire options, ending a turn, a
  Move To order (the ship moved at once in the turn-based game and kept the order),
  creating a fleet with that ship (its orders were cleared). Each changed exactly the
  fields this spec gives for it.
- Files written by our own encoder were loaded: a re-encoded save with new keys; the same
  with the homeworld renamed (the new name and the construction queue appeared); and a
  file with a correct selector but a wrong K6 (it loaded); and a save with two research
  projects inserted (a count changed and items added): the Research window showed both,
  the first with its progress bar a quarter full for 5,000 of 20,000 points.
- The large third-party save loaded in the original. Its date, the current player's
  empire, leader and treasury, the system shown, the display switches of the empire
  options (planet and warp point names, grids, markers) and one planet's sector, size,
  surface, atmosphere (through its sector type) and three values all matched the decoded
  fields.
- A new game made through Game Setup with non-default choices (§3.2) was saved and its
  option codes read back.
- The Load Game window lists `.gam` files by name and file date only.

### 9.1 Files exported by OpenSE4 (2026-10-04, second round)

All **(observed)**, in a fresh scratch copy of the original under Wine. Each file was also
decoded with an independent decoder: every one reads to its last byte and meets §8's
invariants. "Played" means End Turn ran and the computer players took their turns with no
error message.

| File | Kind | Loaded | What was looked at | Played |
|---|---|---|---|---|
| A game made in OpenSE4, turn-based, 5 empires, 30 turns | Export | Yes | Empires (none met yet, as in the file), Colonies (7 colonies, values, moods), Ships\Units (2 ships, a satellite group, 2 fleets), Construction Queues (a facility item that completed on the next turn) | 3 turns; saved by the original and decoded again: a blank storm slot written by OpenSE4 was reused by the original for a new ship |
| The same game, simultaneous | Export | Yes, but no player could sign in: "Invalid Data Files", because the seven data-set checksums were 0 (§3.2.1). The game master could, and processing a turn as host filled them in | Ships\Units and fleets | 3 turns through the host; the same file with the checksums written signs the player in directly |
| Our original turn-based save with a fleet and a waypoint, through OpenSE4 and back | Round trip | Yes | Waypoint, queue, the fleet with its leader, formation, strategy and supply as in the original's file | 2 turns; the fleet kept |
| Our original simultaneous save at creation, through OpenSE4 and back | Round trip | Yes; sign-in refused as above (the original file had the checksums, the export did not) | Finite-resource values | 2 turns through the host |
| Our original simultaneous host save after one turn, through OpenSE4 and back | Round trip | Yes; sign-in refused as above | | 2 turns through the host |
| Our original turn-based save after two turns, through OpenSE4 and back | Round trip | Yes | The Log was empty although the file holds three entries: their read date was their own date (§3.6.11); with 0 they showed, the tech-level entry without its picture because its kind became 36. Designs window: costs, movement, supply and parts as in the original | 2 turns |
| The large third-party game, imported, played 5 turns in OpenSE4 and exported | Export | Yes | The shown system and its objects | 2 full turns with battles (answered Strategic) and four colonizations from exported Colonize orders (each asked for a colony type, as the empire's option says); saved by the original and decoded again |
| An OpenSE4 client export with its statistics file beside it (the name contains a space and a dot) | Export | Yes | The original copied the statistics file into its history folder and the Comparisons window plotted its three lines | |

What also showed:

- The current player's ships in the turn-based export had 0 movement left on loading, one
  of them idle at home with full supply; the original does not refill movement on load
  (it resumes inside the current player's turn), so they could not move until the next
  turn. Export of a turn-based game should write the movement available in the current
  player's turn (its start-of-turn refill done) (inferred cause: OpenSE4 refills at its own
  start-of-turn step, which had not run).
- A simultaneous game exported with an empty game name plays, but the host then saves it
  as a file named only `.gam` (and `_Log.trn` and so on) in the save folder.

### 9.2 The corrected exports (2026-10-04, third round)

All **(observed)**, in a fresh scratch copy of the original under Wine, with exports made
after the fixes of §9.1. Every file decodes to its last byte with an independent decoder,
meets §8's invariants and carries the seven data-set checksums of the stock data.

| File | Result |
|---|---|
| The OpenSE4 simultaneous game; our two original simultaneous saves through OpenSE4 | Players sign in directly, no "Invalid Data Files"; the Log lists the entries; a turn ended by the signed-in player is processed |
| A simultaneous game with a pursuit and a launch | Signs in; the Log lists 28 entries, among them a declaration of war with the sender's portrait. Over two turns the pursuing ship (kind 11 on an enemy ship 12 sectors away in the same system) moved 6 sectors toward its target, then followed it when it moved off, keeping the order. The homeworld's Launch order of kind 5 released exactly its 3 satellites, which joined the satellite group already there, left its 5 weapon platforms in storage, and completed |
| The OpenSE4 turn-based game, made after the player's turn started; and one made before it | Both load and the Log lists the entries. In the second, the current player's ships have their full movement (6/6) and keep their Move To orders; after End Turn the orders ran at the next turn start (one ship reached home and its order completed, the other moved on) |
| Our original turn-based saves through OpenSE4 | The Log lists the three entries, the tech-level entry with its picture again (its kind kept) |
| The large third-party game, imported, played 5 turns in OpenSE4 and exported | Loads, the Log lists OpenSE4's entries (battle summaries, order notices, intelligence reports); two full turns played with battles, colonizations and a "Launch Warning" from an exported satellite launch that hit the original's limit of 100 satellites per location (the order was read as a satellite launch, as intended); saved by the original and decoded again |

What also showed: an export made before the current player's turn has started gives the
ships their movement but not the continuing of their orders, which the original carries
out only at a turn start; the ships keep their orders and act a turn later unless the
player moves them (observed for the two ships above; that End Turn does not run them is
inferred from where they stood after the next turn start). Since 2026-10-05 such an export
carries out OpenSE4's start of that player's turn before writing (§12).

---

## 10. Cross-references

- Spec 01 §12: maps and scenarios; the original's map files use this container (§1.3).
- Spec 02 §1.10: empire files use this container and the empire record of §3.6.
- Spec 05 §9.2: player changes files and the multiplayer flow; their content is the
  "player" form of these sections.
- docs/PARITY_GAPS.md: import and export are implemented since 2026-10-04 (§12).
- docs/SETUP.md "Games of the original": what players see of import and export.

---

## 11. Open questions and side findings

### 11.1 Open questions

1. What the log's "event-style notice" flag is for (no reader found).
2. Which of the log entry kinds 15..18 is which intelligence outcome.
3. The value of a package item of kind 6 (units): an object, since the item check compares
   its name (confirmed: binary); that it is the carrier is inferred.
4. Which column numbers the sort-key slots store, per window, and whether they match
   OpenSE4's column identities.
5. The full rule that gives a design its computer type code when its type name is not
   one of the 39 standard ones.
6. **Answered:** the formulas are in §3.2.1 (confirmed: binary; they reproduce the stock
   values). They are set when a game is made through Game Setup and whenever a host
   processes a turn, and they are needed by every simultaneous game, hotseat included
   (observed).
7. The exact parameters of the Stellar Construction order.
8. The formats of the statistics and events companion files, for exporting OpenSE4's
   history (the statistics file is plain text, one line per turn).
9. Whether militia units (design ids from 65000) can remain in a saved colony.
10. Whether status 2 (under construction) ever reaches a save.
11. Whether fighters and drones can cloak at all (the flag is not saved for them).
12. Why the turn counter starts at 367 (nothing reads it).
13. The best rule for finding a system's type on import (§3.5).
14. `playerTurn.started` and the turn-based turn state on import: a loaded turn-based
    game opened directly on the current player's main window (observed, with the large
    sample). **Answered (observed)** for a file without the autosave and hand-over flags
    (§3.1): loading starts no turn. The current player's vehicles keep the movement the
    file gives them (§9.1) and continue their orders only at a later turn start (§9.2); the
    other start-of-turn steps (the ministers, the destruction check) are taken not to run
    either (inferred). Import therefore counts the current player's turn as started, and
    export writes a human current player's turn as started (§12, question 22).

15. **Answered (observed):** with an empty game name a simultaneous game plays, but each
    host turn saves it as a file named only `.gam` (with `_Log.trn`, `_Combat.cmb` and
    `_plr_1_stats.txt`). Writing a name, as OpenSE4 does (the exported file's), is right; the
    host then overwrites that file each turn, as for the original's own games.
16. **Answered (confirmed: binary):** the kind byte of Launch and Recover orders uses the
    cargo-kind numbering of Load and Drop (3 fighters, 4 mines, 5 satellites, 6 drones,
    2 troops, 7 weapon platforms, 0 every kind), not the closing list's vehicle types:
    OpenSE4's 4 and 5 swap mines and satellites, and its 7 for drones reads as weapon
    platforms (docs/PARITY_GAPS.md).
17. **Answered (confirmed: binary):** kind 11 is the player's Attack on a target in a
    simultaneous game or by drones (the pursuit of spec 03 §8, kept until the target is
    gone), so it maps to OpenSE4's Attack with a target, not to Seek; kind 10 is executed but
    nothing in this version creates it; 12 and 13 come only from ministers and computer
    players and last one movement phase. OpenSE4's pursuing Attack must be written as 11,
    not as 8, whose stored form ignores the target and attacks where the group stands.
18. **Answered (confirmed: binary):** the text is checked against the subject's current name
    for kinds 1, 2, 4, 5, 6 and 7 and a mismatch makes the item unavailable; an empty text
    is not checked (§3.6.11). OpenSE4's texts (the subject's name, and free wording only for
    unchecked kinds) pass.
19. **Answered (confirmed: binary):** the flag only says whether a unit list exists; readers
    test it first, so writing it clear for an empty list changes nothing (§3.8.7).
20. **Answered for the statistics file (observed):** the original copies an OpenSE4-written
    `<name>_plr_<n>_stats.txt` into its history folder on loading, even with a space and a
    dot in the name, and plots its lines in the Comparisons window. The events file was not
    tested (no export carried one).

21. An imported log entry's read date: OpenSE4 writes it back as the file had it (§12), so
    entries the computer players had counted stay counted and entries read on an earlier
    turn stay out of the Log window. **Reviewed (confirmed: binary):** the read date has
    exactly these two uses. Opening the Log window lists the entries whose read date is 0
    or the current date and stamps the unread ones with the current date; a computer
    player's political step counts only entries with read date 0 (by the other empire
    involved, the kind and the event kind, and notes unread diplomatic messages by sender),
    then stamps every unread entry the same way. Keeping the imported value therefore leaves
    the original exactly where the imported file left it; writing 0 instead would make the
    computer players count those entries a second time. The original prunes
    each log to entries dated from one turn ago up to two turns ahead, so an imported entry
    read on an earlier turn would be hidden in the original as well. Whether a human player
    would like the imported game's last messages shown again stays a choice; 0 for human
    empires only would be harmless to the computer players' counting (inferred).

22. **The start of a turn on export (inferred).** OpenSE4 carries out a human current
    player's turn start on a copy of the game before writing (§12). Not settled:
    - What the original does with a file whose current player is a computer. Its own files
      are saved inside a human's turn, since the computer players' turns run within the End
      Turn before it (spec 05 §9.1; inferred for Save Game). OpenSE4 writes such a player's
      vehicles with the movement of their turn's start only, as before 2026-10-05. Playing
      the computer players' turns on the copy up to the next human, as OpenSE4's own resume
      does, would give a file like the original's own.
    - Between two game turns the file names the first living human, and the empires before
      it do not play that game turn in the original. Playing them on the copy first would
      avoid that.
    - The original asks the player during the turn start: Tactical or Strategic for a
      battle, and whether to enter a sector with enemies. The export fights the battle
      strategically and leaves the group before the sector with its orders, unasked; in the
      original it goes on when the order is given again (which asks) or at its next turn
      start.
    - The autosave and hand-over flags (§3.1) open the next-player screen on loading, and a
      loaded hand-over file starts the turn of the empire it names (spec 05 §9.1). Writing
      one of them might let the original carry out the start itself, but how a game on one
      machine (play style 1) treats them has not been tried.

### 11.2 Side findings for other specs (confirmed: binary, found while reading the loader)

- A colony's facility entries keep a count of destroyed facilities that the removal pass
  subtracts but never resets; every later removal pass at that colony (after sabotage, a
  natural event or hazard damage) subtracts the leftover again, so the colony loses those facilities
  a second time. OpenSE4 keeps the counts as the original does since 2026-10-04 (§12).

- Each system's object list is rebuilt in slot order after every load, move, warp and
  creation. Spec 05 §7.5 ("Placement") and `Vehicle::arrival` assume arrival order; this
  should be checked again.
- The timed-event scheduler refuses to schedule when its list already has five or more
  slots, free or not, and the slots are never removed: once five timed events have been
  pending together, no timed event is ever scheduled again (spec 05 §4).
- The random generator is reseeded from the game's stored seed on every load.
- The trade counter of every pair goes up by one per turn whatever the treaty, and is
  reset only when a treaty changes to or from one below Trade (compare spec 05 §3.3).
- The victory peace counter and the game-completed flag are part of the saved game.
- A new human empire starts with all 25 minister switches off **(observed)**; OpenSE4's
  `Empire::ministers` starts with the individual areas on.

---

## 12. Our engine

Implemented on 2026-10-04 from this spec.

- **Where.** `src/game/classic_save.hpp` holds the typed model of a file (every field of §3
  in file order, with the spec's meanings and numbering) and the API;
  `classic_save_codec.cpp` the container (§2) and one routine per section that both reads
  and writes it, so that a decoded file encodes back to the same values;
  `classic_save_import.cpp` and `classic_save_export.cpp` the mappings of §6 and §7;
  `classic_save_tables.cpp` Appendix A and §3.7.1. The client imports on Load Game and
  `--load` (the file's first bytes tell the formats apart) and exports from Save Game's
  *Save for SE IV*; `opense4-convert` converts on the command line (docs/BUILDING.md).
- **Text** is Latin-1 in the file and UTF-8 in OpenSE4; a character Latin-1 lacks is
  written as "?". **Floats** are kept as their 80-bit pattern and converted with the
  emulated x87 arithmetic (`xmath.hpp`): conditions exactly, experience in tenths.
- **Import** runs OpenSE4's own load-time steps afterwards: sight, the colonies' cloak and
  sensor levels, the income report. It refuses a save whose tech-area count differs from
  the data set's, whose racial-trait count does (the file then decodes only with another
  count, which the message names) or that names a record past the end of a data file.
- **Choices where this spec leaves room:**
  - A system's type: the first type with the same description and background bitmap,
    else the first of the same physical type (§11.1 Q13).
  - Free design slots become placeholders no empire lists (owner 0, obsolete), keeping
    their old fields when the data set still has them; a design no empire lists is
    written as a free slot unless something still uses it.
  - Free object slots are not kept by OpenSE4: export writes each empty slot as a blank
    storm and stops at the last object, so trailing blanks of an imported file are gone.
    Free fleet slots go too, and fleet numbers close up (every reference follows).
  - Free timed-event slots are dropped on import, so an imported game schedules events
    again (§11.2).
  - Fleets with no member in their own sector are disbanded on import, as the original
    does on loading; their members keep their orders.
  - Load, Drop, Launch and Recover orders: by kind in the file, by design in OpenSE4. An
    imported order takes the first design of that kind in the vehicle's or colony's cargo
    (a Drop or Launch), else the owner's newest design of that kind; an exported one is
    written by its design's kind and as much as fits. Launch and Recover use the
    cargo-kind numbering both ways (§11.1 Q16); a kind byte of 0 (every kind) becomes one
    order per kind: a launch for each unit kind the launcher carries, a recovery for
    fighters and for satellites.
  - Attack: OpenSE4's Attack on a target (a vehicle or an object) is written as kind 11
    with the target, its place and its name, and one without a target as kind 8, which
    names nothing; kind 11 is read as an Attack on its target, kind 8 as an Attack where
    the group stands, kind 10 as a Move To and kinds 12 and 13 as OpenSE4's one-phase
    Seek (§11.1 Q17).
  - An imported Colonize order counts as given (its colonists were loaded then); a Use
    Facility position is turned between the grouped list and OpenSE4's own; Abandon Planet
    orders are dropped (OpenSE4 abandons at once).
  - Log entries: OpenSE4's are written unread (read date 0, §3.6.11) as kind 36 with
    picture key 0, unanswered messages as kind 19 with their record (a delivered message
    whose entry the log no longer holds gets one). Imported entries keep title, text,
    place, category and go-to, and a picture from their kind and key where the kind names
    one (vehicle and facility built, item developed, message); and they keep what
    OpenSE4's entries do not hold (`LogEntry::classic`: the kind, the owner, system and
    sector bytes as written, the read date, the picture key, the other empire, the
    event-style notice, the event kind, the tech area and a combat entry's battle details),
    which the export writes back. An imported entry's read date is kept rather than set to
    0 (an OpenSE4 choice, question 21): it is the mark of what a computer player's political
    step has counted (spec 05 §7.3), so the original neither counts those entries again nor
    lists those read on an earlier turn, as before the import. A kept kind 19 whose message
    is gone is written as kind 36.
  - Turn-based games: on import the current player's turn counts as started (§11.1 Q14);
    the closing list (§3.9) is the turn's launch budget. The file's current player is the
    empire whose turn is in progress, or between two game turns the first living human.
    When that player is a living human whose turn has not started (OpenSE4 between two
    players' turns or two game turns), the export carries out OpenSE4's start of that turn
    on a copy of the game and writes the copy (`startHumanTurn`): the destruction check,
    the start-of-turn step with the player's ministers, the movement refill and every
    group's orders continued (spec 05 §8 "Turn-based game"), because the original resumes
    inside the turn and never starts it on loading (§9.1, §9.2, Q14). The game itself does
    not change, and the copy goes on with the game's own random sequence, so the same game
    always gives the same file. Where that start would stop for the player, it goes on
    without asking (inferred, Q22): a battle is fought strategically, and a group about to
    enter a sector with enemies stops before it with its orders kept and the question
    unasked, as the file holds no open question. The conversion report says so, and also
    counts the questions still open in a turn that had started, which are lost the same
    way. Between two game turns the empires before that human do not play the game turn in
    the exported file, and a computer player whose turn has not started gets only the
    movement its start gives (`movement::refilledMovement`: each vehicle's maximum, a fleet
    at its location the slowest member's, nothing while held in place), as before (both
    inferred, Q22). The turn-based export of §9.1 was made after the player's turn had
    started: its ships had spent their movement carrying out their Move To orders at the
    start of that turn (one reached home and resupplied there), so its 0 movement left was
    OpenSE4's state as well, and is still written as it is.
  - The data-set checksums (§3.2.1) are computed from the data files the game was played
    with (`Rules::data().dataDir`, read as written: `classic_save_checksums.cpp`) and
    written in every export; import computes them too and notes which files differ from a
    save that carries them.
  - Facility entries' destroyed counts: OpenSE4's engine keeps them per colony
    (`Colony::destroyedFacilities`) and follows §11.2 for the removal passes it has, those
    after sabotage and events (`Planet - Facility Damage`); a kind whose last facility goes
    loses its count (inferred). Battles remove what they destroyed once, as spec 04 §19.1
    chose, and do not touch the counts; OpenSE4 has no hazard damage to colonies. Import
    and export carry the counts as they are, so the colony's next removal pass takes them
    off in either game; a count on an entry with no facility left is dropped on import
    (none was seen).
  - Passwords: imported as OpenSE4's check of the trimmed lower-case password; none is
    exported (§7.4). The game master password is dropped.
  - The windows' sort keys are not carried in either direction (§11.1 Q4).
  - Race folders are written as the installed folder is spelt.
- **Checked in the running original** on 2026-10-04 with seven exports (§9.1): all load
  and play; simultaneous ones need the data-set checksums (§3.2.1) for players to sign in.
  The fixes of that check (the checksums, the read date, Attack kinds 11 and 8, the
  Launch and Recover kind byte, the kept log fields, the movement of an export made before
  the player's turn) followed the same day, and on 2026-10-05 the whole start of that
  turn (§9.2). The checksums computed from the installed data set are the seven values of
  every sample save that carries them (five saves).
- **Checked** on the 24 saves this spec was checked with (§8): every one decodes to the end,
  meets §8's invariants and counts, imports into a state that `validateState` accepts and
  that plays on without desync, and is written again with only the differences listed
  above, the window memories and other fields OpenSE4 does not hold (§7.3), and the float
  noise of experience (tenths are kept). OpenSE4 games of both turn styles come back from
  an export and import with every field the format carries, and a second round trip
  changes nothing. The large sample, imported and played on for five turns, saves and
  loads in OpenSE4's format and exports again.

---

## Appendix A. Ability ids

The numbers the save uses for ability types (§3.8.1), with the ability names of the data
files (spec 03 §3.3) **(confirmed: binary; ids 3, 6..10, 108..110, 122 and 123 also
observed against the stored descriptions)**:

0 none; 1 Random; 2 Warp Point - Unstable; 3 Warp Point - Turbulence; 4 Warp Point -
Periodic; 5 Warp Point - Ability Required; 6 Star - Unstable; 7 Sector - Sight
Obscuration; 8 Sector - Sensor Interference; 9 Sector - Shield Disruption; 10 Sector -
Damage; 11 Sector - Ability Required; 12 Resource Generation - Minerals; 13 Resource
Generation - Organics; 14 Resource Generation - Radioactives; 15 Point Generation -
Research; 16 Point Generation - Intelligence; 17 Spaceport; 18 Palace; 19 Supply
Generation; 20 Planet - Change Minerals Value; 21 Planet - Change Organics Value; 22
Planet - Change Radioactives Value; 23 Planet - Change Conditions; 24 Planet - Change
Population Happiness; 25 Planet - Change Ground Defense; 26 Planet - Shield Generation; 27
Shield Generation; 28 Phased Shield Generation; 29 Component Repair; 30 Cargo Storage; 31
Drop Troops; 32 Launch/Recover Fighters; 33 Lay Mines; 34 Multiplex Tracking; 35 Combat To
Hit Offense Plus; 36 Combat To Hit Defense Plus; 37 Mine Sweeping; 38 Medical Bay; 39
Resupply Pod; 40 Movement Bonus; 41 Emissive Armor; 42 Shield Regeneration; 43 Master
Computer; 44 Cloak Level; 45 Sensor Level; 46 Emergency Resupply; 47 Emergency Energy; 48
Long Range Scanner; 49 Open Warp Point Distance; 50 Create Planet Size; 51 Destroy Planet
Size; 52 Boarding Attack; 53 Boarding Defense; 54 Standard Ship Movement; 55 Ship Bridge;
56 Ship Auxiliary Control; 57 Ship Life Support; 58 Ship Crew Quarters; 59 Scanner Jammer;
60 Quantum Reactor; 61 Supply Storage; 62 Space Yard; 63 Maximum Population; 64 Resource
Storage - Mineral; 65 Resource Storage - Organics; 66 Resource Storage - Radioactives; 67
Resource Gen Modifier Planet - Minerals; 68 Resource Gen Modifier Planet - Organics; 69
Resource Gen Modifier Planet - Radioactives; 70 Resource Gen Modifier System - Minerals; 71
Resource Gen Modifier System - Organics; 72 Resource Gen Modifier System - Radioactives; 73
Planet Point Generation Modifier - Research; 74 Planet Point Generation Modifier -
Intelligence; 75 System Point Generation Modifier - Research; 76 System Point Generation
Modifier - Intelligence; 77 Combat Modifier - System; 78 Damage Modifier - System; 79
Planet Value Change - System; 80 Planet Conditions Change - System; 81 Change Bad Event
Chance - System; 82 Change Bad Intelligence Chance - System; 83 Change Population
Happiness - System; 84 Ship Training; 85 Fleet Training; 86 Modify Reproduction - System;
87 Change Population - System; 88 Plague Prevention - System; 89 Resource Conversion; 90
Resource Reclamation; 91 Close Warp Point; 92 Destroy Star; 93 Create Star; 94 Destroy
Storm; 95 Create Storm; 96 Self-Destruct; 97 Colonize Planet - Rock; 98 Colonize Planet -
Ice; 99 Colonize Planet - Gas; 100 Point-Defense; 101 Armor; 102 Launch/Recover
Satellites; 103 Remote Resource Generation - Minerals; 104 Remote Resource Generation -
Organics; 105 Remote Resource Generation - Radioactives; 106 Armor Regeneration; 107
Shield Generation From Damage; 108 System - Movement Towards Center; 109 System - Movement
Random; 110 System - Destructive Center; 111 Destroy Nebulae; 112 Create Nebulae; 113
Destroy Black Hole; 114 Create Black Hole; 115 Stop Planet Destroyer; 116 Stop Star
Destroyer; 117 Stop Nebulae Creator; 118 Stop Black Hole Creator; 119 Stop Open Warp
Point; 120 Stop Close Warp Point; 121 Component Destroyed On Use; 122 Ancient Ruins; 123
Ancient Ruins Unique; 124 Combat Best Experience; 125 Combat Movement; 126 Solar Supply
Generation; 127 Extra Movement Generation; 128 Planet - Change Atmosphere; 129 Weapons
Always Hit; 130 Create Constructed Planet; 131 Constructed Planet Requirements; 132
Modified Maintenance Cost; 133 Ship Training - System; 134 Fleet Training - System; 135
Long Range Scanner - System; 136 Solar Resource Generation - Minerals; 137 Solar Resource
Generation - Organics; 138 Solar Resource Generation - Radioactives; 139 Reduced
Maintenance Cost - System; 140 Shield Modifier - System; 141 Combat To Hit Offense Minus;
142 Combat To Hit Defense Minus; 143 Launch Drones; 144..163 AI Tag 01..20; 164 Generate
Points Minerals; 165 Generate Points Organics; 166 Generate Points Radioactives; 167
Generate Points Research; 168 Generate Points Intelligence.

## Appendix B. Code lists used by the data-set checksums

**(confirmed: binary)**

Weapon Damage Type: 1 Normal; 2 Shields Only; 3 Skips Normal Shields; 4 Only Engines;
5 Only Weapons; 6 to 10 Plague Level 1 to 5; 11 Only Planet Population; 12 Only Planet
Conditions; 13 Only Resupply Depots; 14 Only Spaceports; 15 Pushes Target; 16 Pulls
Target; 17 Random Target Movement; 18 Only Shield Generators; 19 Only Boarding Parties;
20 Only Security Stations; 21 Only Planet Destroyers; 22 Skips Armor; 23 Skips Shields And
Armor; 24 Quad Damage To Shields; 25 Increase Reload Time; 26 Disrupt Reload Time; 27 Crew
Conversion; 28 Skips All Shields; 29 Only Master Computers; 30 Double Damage To Shields;
31 Half Damage To Shields; 32 Quarter Damage To Shields.

Trait Type: 1 Reproduction; 2 Mineral Production; 3 Organics Production; 4 Radioactives
Production; 5 Research Production; 6 Intelligence Production; 7 SY Rate; 8 Maintenance
Cost; 9 Supply Cost; 10 No Plagues; 11 Luck; 12 No Spaceports; 13 Population Happiness;
14 Vehicle Speed; 15 Galaxy Seen; 16 Planet Storage Space; 17 Planetary SY Rate; 18 Troops
Bonus; 19 Fighter Bonus; 20 Ship Bonus; 21 Mineral Storage; 22 Organics Storage;
23 Radioactives Storage; 24 Production; 25 Trade; 26 Space Combat; 27 Ground Combat;
28 Repair; 29 Tech Area; 30 Tollerance (spelt so in the data); 31 Ship Attack; 32 Ship
Defense; 33 Population Emotionless.

# Setting up the game data

OpenSE4 is only an engine. It plays with the data files, art, sounds and
computer-player files of **your own copy** of Space Empires IV Deluxe. Nothing from
the original game ships with OpenSE4, and nothing is copied out of your installation:
OpenSE4 reads the files in place.

Without a copy there is nothing to play. If OpenSE4 cannot find one, it says where it
looked and exits.

## 1. Get the game

Space Empires IV Deluxe is sold on Steam (app 1610). Any complete install works.
OpenSE4 needs the game directory: the one that contains `Data/`, `Pictures/`,
`Sounds/`, `Music/`, `Ai/` and `Dsgnname/`. On Steam, that is the `se4/` folder inside
`steamapps/common/Space Empires IV Deluxe/`.

### Windows

Install the game through Steam as usual.

### Linux and macOS

Steam lists the game as Windows-only. You still need to download the files, but
OpenSE4 never runs the original executable. Pick one method:

- **Steam client:** open the game's *Properties → Compatibility*, tick *Force the use of
  a specific Steam Play compatibility tool*, then install. This only makes Steam
  download the Windows files.
- **SteamCMD:**

  ```sh
  steamcmd +@sSteamCmdForcePlatformType windows +force_install_dir ~/games/se4 \
           +login <your-account> +app_update 1610 validate +quit
  ```

- **Copy an install from a Windows machine.** A copy of the game directory is enough.

On ARM Linux (a Raspberry Pi, a Rockchip board, an ARM laptop) neither Steam nor SteamCMD
runs: download the files with one of the methods above on another computer and copy the
game directory over.

## 2. Point OpenSE4 at it

With no options, OpenSE4 looks in every Steam library it can find:

| Platform | Steam roots searched |
|---|---|
| Linux | `~/.local/share/Steam`, `~/.steam/steam`, Flatpak Steam (`~/.var/app/com.valvesoftware.Steam/...`) |
| Windows | the Steam folder the registry names (any drive), `%ProgramFiles(x86)%\Steam`, `C:\Program Files\Steam` |
| macOS | `~/Library/Application Support/Steam` |

For each root it also reads `steamapps/libraryfolders.vdf`, so libraries on other
drives are found. It then looks for `steamapps/common/Space Empires IV Deluxe`. Folder
and file names are matched in any case on every platform, as Windows does, so a copy
whose folders are spelled `DATA` or `data` works on Linux too.

If your copy is anywhere else, pass it explicitly. Any of these works: the game
directory, its `se4/` subdirectory, or the `Data/` directory itself.

```sh
opense4 --classic-dir="/mnt/games/SteamLibrary/steamapps/common/Space Empires IV Deluxe"
```

## 3. Check the data

```sh
opense4-datacheck                       # auto-detect
opense4-datacheck /path/to/se4/Data     # or an explicit data directory
```

The checker loads every data file and reports anything it does not understand, with
the file, line and record. A stock install reports no errors.

Run this first when you use a mod: it catches typos and fields that OpenSE4 does not
support yet.

## 4. Play

```sh
opense4                                   # auto-detects the install
opense4 --quick-start --quadrant="Spiral Arm" --systems=80   # an exact count; Game Setup rolls it from the Quadrant Size
opense4 --renderer=opengl                 # if Vulkan misbehaves on your machine
```

`opense4 --help` lists every option. For multiplayer, see
[MULTIPLAYER.md](MULTIPLAYER.md).

## Sound and music

OpenSE4 plays the game's own sound effects and music from `Sounds/` and `Music/`, and
uses the playlists defined in the game's settings file. If the install includes the
remastered sounds in `Sounds/New/`, those are used by default.

You can control sound in Game Menu → Options (kept on this computer):
- sound effects can be switched off (Ctrl+S too), and the classic sound set chosen
  instead of the remastered one;
- music can be switched off ("Music Off") or set to one of five volumes;
- the effects volume is in Options → Settings → Sound.

While the game is in the background (another window has the focus, or the game is
minimized or hidden), its sound effects and music fade out. The music pauses where it is
and goes on from there when you come back; sound effects meanwhile are not played. To
keep hearing the game in the background, turn off "Mute when the game is in the
background" in Options → Settings → Sound (on by default).

`--no-audio` starts without sound.

### Where the music comes from

- **The playlists.** The game's `Data/Settings.txt` has three: intro, background and
  combat (`Num Intro Songs`, `Intro Song 1 Filename`, and so on for `Background` and
  `Combat`). Each entry names a file in the game's `Music/` folder. The stock install
  names its own MP3s, `Space Empires IV - Track 01.mp3` to `Track 15.mp3` (MPEG-2 layer III,
  22.05 kHz stereo). An entry without a file name (`Intro Song 1 := 9`) gives a CD track
  number, and the file is then the `Track` MP3 numbered one lower (`Track 08.mp3`).
- **No music at all** when `Settings.txt` sets `Allow CD Music` to `FALSE`.
- **Names.** Folder and file names are found in any letter case on every platform
  (`music/space empires iv - track 08.MP3` is fine on Linux too). The music is always read
  from the game folder's own `Music/`, also when `Path.txt` names a mod folder.
- **Formats.** Music is MP3 (MPEG-1, 2 or 2.5 layer III, any rate, mono or stereo), sound
  effects are WAV (any PCM or float format). OpenSE4 mixes at the rate of your sound
  device (on Windows usually 48 kHz), converting each file once. Music is decoded on a
  thread of its own, so it keeps playing while a turn is processed.
- **When it plays** (as the original): the intro track when the intro screen opens, which
  New Game and Quick Start leave playing into the game; a background track after Resume
  Game, Load Game, a tutorial or a scenario, and a new one every fifth turn; a combat
  track when Tactical Combat or a Combat Replay opens; a background track when a replay
  closes. Each track is picked at random from its list and loops until the next change.
  Music turned on with nothing playing starts the intro list in the menus and the
  background list in a game. A change fades the old track out over a quarter second.

### No sound, no music, or clicks: what the log says

Every run writes `opense4.log` in your user data folder (see "Where OpenSE4 keeps its own
files" below). Its audio lines tell what happened, in this order:

| Log line | Meaning |
|---|---|
| `Music: Data/Settings.txt names 1 intro, 8 background and 6 combat tracks` | The playlists were read. |
| `Music: none: the game's Data/Settings.txt sets Allow CD Music to FALSE` | The game's settings allow no music. Set it to `TRUE` in a copy of the game folder (see "Mods"). |
| `Music: Music/..., named by Settings.txt, is not in the installed game (...)` | A track the playlists name is missing; it is never played. On Steam, "Verify integrity of game files" restores it. |
| `Audio: <device> through <driver>: 48000 Hz, 2 channels, SDL_AUDIO_F32LE, 1024 frames a buffer; mixing at 48000 Hz` | The sound device that opened, and its format. |
| `Audio: no sound or music: ...` | No sound device could be opened; the reason follows. Check the system's sound output (on Linux, that PipeWire or PulseAudio runs). |
| `Audio settings: sound effects on at 80 % (...), music on at step 5 of 5, muted in the background` | This computer's settings at the start, and again whenever they change. "music off" means Music Off is lit in Game Menu → Options; "not muted in the background", that "Mute when the game is in the background" is off. |
| `Audio: muted: the game is in the background`, `Audio: unmuted: ...` | The game's window went into the background and the sound faded out, or it came back. |
| `Music: playing Music/Space Empires IV - Track 08.mp3 (22050 Hz, 2 channels, 48 s, looped)` | A track started. |
| `Music: cannot play Music/...: <why>` | The file could not be read, or holds no MP3 audio. It is not tried again in that run. |
| `Sound: cannot play .../Sounds/New/....wav: <why>` | A sound effect could not be read or decoded. |
| `Not in the installed game: Sounds/...` | A sound the game wanted is in neither sound folder. |
| `Audio: the music ran dry N times so far ...` | The music's decoding fell behind (the computer was very busy); the music faded out and back in. |

With `--verbose`, the log also has a line for each sound effect played (`Sound: button`)
and each time a track starts again from its beginning.

If you hear clicks or crackling while the log shows nothing wrong, try another format for
the device: OpenSE4 asks for the one named by the environment variables
`SDL_AUDIO_FREQUENCY` (`44100` or `48000`), `SDL_AUDIO_FORMAT` (`F32` or `S16`) and
`SDL_AUDIO_CHANNELS` (`2`), and the log's `Audio:` line shows what it got.
`SDL_AUDIO_DRIVER` picks the system's sound interface: `wasapi` or `directsound` on
Windows, `pipewire`, `pulseaudio` or `alsa` on Linux. On Windows, sound "enhancements" and
exclusive mode in the device's properties are worth switching off as well.

`SDL_AUDIO_DRIVER=disk` writes the mix to a file instead of the speakers
(`SDL_AUDIO_DISK_OUTPUT_FILE`, raw samples in the format the log's `Audio:` line gives):
that is how `tools/check_audio.py` checks OpenSE4's sound without a sound card
(docs/BUILDING.md "Tests").

## Games of the original

OpenSE4 reads the saved games of the original (version 1.95, the last release) and writes
games back in its format, so a game can move between the two. The format is described in
[spec/08-saved-games.md](spec/08-saved-games.md).

- **Load an original game.** Load Game lists `.gam` files; both games use that extension,
  and OpenSE4 tells them apart by their first bytes. Use Change Directory to open the
  original's `SaveGame` folder (or any folder you copied a save to), or start with
  `opense4 --load=PATH/GAME.gam`. The game is converted when it loads; a note lists what
  came across only approximately, and `opense4.log` has every detail. Nothing is written
  next to the original's file: Save Game then saves it in OpenSE4's own format, in
  OpenSE4's saves folder. The players' `<game>_plr_*` History files next to the original's
  save are read as with OpenSE4's own saves.
- **Save a game for the original.** In Save Game, *Save for SE IV* writes the game as a
  saved game of the original. It goes to the `Space Empires IV` folder inside OpenSE4's
  saves folder unless you type another; OpenSE4 never writes into the installation by
  itself. The players' History files go beside it, as the original's own saves keep them.
  To play it there, copy the `.gam` file (and the files beside it named after it) into the
  original's `SaveGame` folder, or open that folder with the original's Change Directory.
  OpenSE4's own saves of the game are not changed. Network and play-by-e-mail games cannot
  be saved this way: a player's copy holds only what that player knows. The file carries
  the data set's checksums, which the original compares when a player signs in to a
  simultaneous game, so it must be played with the same data files there.
- **On the command line,** `opense4-convert` converts in both directions and describes a
  saved game of the original (see docs/BUILDING.md, "Tools"):

  ```sh
  opense4-convert --info GAME.gam                        # what an original save holds
  opense4-convert GAME.gam converted.gam --to=opense4    # an original save as an OpenSE4 save
  opense4-convert mine.gam ForSE4.gam --to=original      # an OpenSE4 save for the original
  opense4-convert --compare A.gam B.gam                  # the fields two original saves differ in
  ```

- **The data set must be the same.** A saved game of the original names data-file records
  by their position, so it only means something with the data set it was played with:
  the installed game, or the same mod. A save made with another number of racial traits
  or tech areas, or one that names a record the data set lacks, is refused with a message
  that says so. Saves of versions other than 1.95 are refused too.

What carries over, both ways: the galaxy (systems, stars, planets with their conditions
and values, storms, warp points, comets, stellar abilities), every empire (race,
traits and characteristics, treasury, technology, research and intelligence projects,
treaties and trade, the computer players' anger and attack plans, ministers, name lists,
waypoints, systems to avoid, mine field tags, combat strategies, Empire Options, the log),
colonies (population, mood, facilities, cargo, construction queues, invasions in
progress, orders), designs (with the original's cached speed, cost and type code
computed again), ships, bases, fleets and unit groups (damage, supply, movement, cargo,
experience, orders, space yard queues), timed events, unanswered diplomatic messages, the
victory conditions and every game option, and whose turn it is in a turn-based game.

What does not, or only approximately:

| From the original into OpenSE4 | From OpenSE4 into the original |
|---|---|
| Load, drop, launch and recover orders act on all units of a kind in the original; in OpenSE4 they name one design of that kind | They act on every unit of the design's kind |
| The computer players start with partly empty memories (OpenSE4 keeps more about each empire than the original's file holds) | OpenSE4's additional memory is not written |
| The random numbers start again from the game's seed, as in the original after every load | The same: the original's next turn differs from OpenSE4's |
| Combat log entries show their text but not their battle details (they are kept and written back to the original); when treaties were signed, colonies founded and ships built is not stored | OpenSE4's own log entries are written as plain entries, without pictures or battle details, and OpenSE4's combat records are not written; entries that came from the original keep theirs |
| A warp point that leads to a sector without a warp point (the original allows one-way links) leads nowhere | Ships keep only destroyed or intact parts: partial damage is lost |
| Saved construction queue templates, the game master password and the window sort orders are dropped | Passwords are not written (OpenSE4 keeps only a check of them): every empire is open in the original until you set new ones there with Change Password |
| Passwords carry over; type them in lower case | Messages not yet delivered, and Explore, Resupply, Repair, Cloak and Decloak orders still in a list, are not written |
| Ships under construction in the original arrive finished | Vehicles held in place by sabotage or an event can move again |

## Mods

OpenSE4 layers mods over your installed game and never changes the game's own files.
Put a mod (a folder or a `.zip`) in the `Mods` folder of OpenSE4's user folder (see
below) and list its id in `classic_settings.toml` there:

```toml
[mods]
enabled = ["example.common-lib", "example.better-carriers"]
```

or name it for one run with `--mod=PATH` (a folder or `.zip`, or the id of a mod in the
mods folder; repeat for several, in load order). `--no-mods` plays without the mods the
settings list, `--mods-dir=DIR` looks for ids in another folder. The log names every mod
loaded, and a mod with an error stops the game with a message that names the mod, its
file, the line and the record.

Classic mods for the original (a folder of replacement data files and pictures, without
a `mod.toml`) load the same way: `--mod=path/to/the/mod`. Applying one to a **copy** of
the game directory and passing that copy with `--classic-dir` works too, as before; run
`opense4-datacheck` on the copy first.

Everyone in a network or e-mail game needs the same mods, apart from mods with only
pictures and sounds; the lobby refuses a player whose mods differ and says which. A saved
game remembers its mods. Making mods: [docs/sdk/packages-and-data.md](sdk/packages-and-data.md)
and the `opense4-sdk` tool.

## Where OpenSE4 keeps its own files

Saves (`saves/`, including the autosaves `AutoSav0` to `AutoSav9`, named after the
last digit of the turn count, the players' history files copied beside each save, and the
games saved for the original in `saves/Space Empires IV/`),
the players' statistics, history and log files of the game being played (`History/`),
maps (`maps/`,
see [MAPS.md](MAPS.md)), empire files (`empires/`), settings and logs go in your user
data directory. OpenSE4 never writes to the game directory.

| Platform | Location |
|---|---|
| Linux | `~/.local/share/OpenSE4/` |
| Windows | `%APPDATA%\OpenSE4\` |
| macOS | `~/Library/Application Support/OpenSE4/` |

The environment variable `OPENSE4_USER_DIR` names another folder for all of this (the
tests use it, and it serves a portable install). Every run of the game writes its log to
`opense4.log` there: on Windows, where the game has no console, that file is where its
messages are, including any file of your install it could not find ("Not in the installed
game: ..."). The run before keeps its log as `opense4.previous.log`.

If OpenSE4 crashes, it adds a short crash report to the end of `opense4.log` (the version,
what went wrong, where in the program, and the last lines of the log) and a message box
says where the file is. Please attach that file to your bug report; if you started the
game again first, the report is in `opense4.previous.log`.

## Troubleshooting

| Symptom | Fix |
|---|---|
| "No copy of the game was found" | Pass `--classic-dir` (step 2). Check that the directory contains `Data/Components.txt`. |
| Black window or crash at startup | Try `--renderer=opengl`. With the Vulkan SDK installed, `--validation` shows driver errors. |
| The game crashed | Send `opense4.log` (or `opense4.previous.log` after a restart) from the folder above with your report: its end holds the crash report. |
| Text looks wrong in names | The data files are Latin-1 and are converted to UTF-8 on load. Report any file that still looks wrong. |
| A mod fails to load | Run `opense4-datacheck` on its data directory. The errors show the file and line. |
| "the game was saved with a data set of N racial traits" or "another data set" when loading a game of the original | The game was played with a mod or another version of the data files: start OpenSE4 with `--classic-dir` on a copy of the game with that mod (see "Mods"). |
| No movement line after Move To | The line is the per-computer option "Display Ship Movement Lines" (Game Menu → Options, or Ctrl+L), off on a fresh install as in the original. Starting a new simultaneous game switches it on; joining one does not. It shows for the ship, base, unit group or fleet whose report is open. |
| No music, or no sound | See "No sound, no music, or clicks: what the log says" above: `opense4.log` names the device, the settings, each track and every file that cannot be played. Music Off in Game Menu → Options and `Allow CD Music` in the game's `Settings.txt` both silence the music, and the game is silent while it is in the background (see "Sound and music"). |
| Clicks, pops or crackling | The same section: the log shows whether the music ran dry; another device format (`SDL_AUDIO_FREQUENCY`, `SDL_AUDIO_FORMAT`) or sound interface (`SDL_AUDIO_DRIVER`) can be tried. |
| Something looks or behaves differently on another computer | Compare the per-computer settings first (`classic_settings.toml` and `settings.toml` in the folder above), then `opense4.log`. The game itself plays the same on every platform. |

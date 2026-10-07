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

Run it on a classic mod's complete data set too (a copy of the game folder with the mod's
files in it): it catches typos and fields that OpenSE4 does not read. A mod made for
OpenSE4 (a folder with a `mod.toml`) is checked over your installed game with
`opense4-sdk check <mod>` instead ([docs/sdk/README.md](sdk/README.md)).

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
below), then choose it in the **Mods** window: the title screen's `Mods` button (top
right), or the `Mods` button in the corner of Game Setup and Quick Start. The window lists
every mod of the folder, and the mods that come with OpenSE4, with what each holds and
whether it changes the game; switch mods on and off and put them in order, and `Done`
reads the game's data again with them. They apply to the next game you start or load, and
the settings keep them. A choice that cannot load (a required mod that is off, a patch
that does not fit your game) is refused with the reason.

**Hegemon comes with OpenSE4.** It is a computer player of its own, stronger than the
classic AI, in the `mods` folder beside the programs (in the Windows install,
`C:\Program Files\OpenSE4\mods`). The classic game stays the default: Hegemon is listed in
the Mods window as coming with OpenSE4, and off. Switch it on there (`Enable`, then
`Done`), and Game Setup and Quick Start offer it under `Computer Players` (see "Computer
players" below). Players of the same OpenSE4 release have the same Hegemon, so a network
or e-mail game with it needs nothing but every player switching it on.

The settings file keeps the choice as ids in `classic_settings.toml`:

```toml
[mods]
enabled = ["example.common-lib", "example.better-carriers"]
```

For one run, name mods with `--mod=PATH` (a folder or `.zip`, or the id of a mod in the
mods folder or of one that comes with OpenSE4, such as `--mod=opense4.hegemon`; repeat for
several, in load order). `--no-mods` plays without the mods the settings list,
`--mods-dir=DIR` looks for ids in another folder in place of your mods folder, and
`--no-bundled-mods` leaves out the mods that come with OpenSE4. Ids are looked for in
your mods folder first: a mod there with the id of one that comes with OpenSE4 replaces
it (docs/sdk/packages-and-data.md "Where mods are found"). The log names every mod
loaded. A mod given with `--mod` that has an error stops the start with a message that
names the mod, its file, the line and the record; the settings' mods that no longer load
are left out, and the title screen and the Mods window say why.

Classic mods for the original (a folder of replacement data files and pictures, without
a `mod.toml`) load the same way: `--mod=path/to/the/mod`. Applying one to a **copy** of
the game directory and passing that copy with `--classic-dir` works too, as before; run
`opense4-datacheck` on the copy first.

Everyone in a network or e-mail game needs the same mods, apart from mods with only
pictures and sounds; the lobby lists the host's mods, and refuses a player whose mods
differ, naming each difference. A saved game remembers its mods: loading one played with
other mods says which, and loads it with its own when your mods folder (or OpenSE4's own
mods) has them. Mods may
bring pictures as PNG (also larger than the original's, for sharper screens) and sounds
and music as OGG Vorbis. Making mods: the modder's guide, [docs/sdk/README.md](sdk/README.md),
and the `opense4-sdk` tool (beside the programs; a release also has the guide and the
example mods in its `sdk` folder).

### Computer players

A mod may bring computer players of its own, written in Python
([docs/sdk/python-api.md](sdk/python-api.md)). When the mods you play with offer some, the
setup screens let you choose who plays the computer empires; without such mods they are
exactly the original's.

| Where | What |
|---|---|
| Game Setup, Players | `Computer Players` (under `Move Down`) opens a window listing the classic AI and each mod's players, with the mod and a description. The one you light plays every computer empire that has no player of its own, the random ones included; the line under the button names it. |
| Empire Setup, General | For a computer-controlled empire, `Computer Player` and its ▽ button: the game's choice (the Players page's), the classic AI, or one of the mods' players, for this empire alone. |
| Game Setup, Game Settings | `Computer players see everything`: their view is the whole game instead of what their empire knows (off by default, so that they play fair). `Computer Player Limits` sets how much they may do for one request of the game: bytecodes for a turn's planning, bytecodes for any other request, and the memory each keeps. A player that goes over fails that request, and the classic AI answers it. |
| Quick Start | `Computer Players` in the left column: who plays the quick game's computer empires, and whether they see everything. |
| Multiplayer | The host form chooses who plays the computer empires and whether they see everything; in the lobby the host changes each computer empire's player and the option, and sets their limits (`Computer Player Limits`, beside the option), and joining players see the players and the option. The players run on the host's computer. |
| Command line | `--ai=MOD:PLAYER` plays every computer empire of a `--quick-start` game with that player. |

In a game, when such a player fails (an error in its Python, or a request over its limits),
the classic AI answers in its place and the game goes on. The host's main window says so
over the bottom of the system view, with `Details` (the error and the traceback of each
failure) and `Dismiss`; the log file (`opense4.log`) has them too.

Settings → Modding switches on the **AI notes view**, which shows the notes these players
write about what they think on the system and galaxy views and in the reports;
`Ctrl+Shift+N` switches it in a game. It shows the notes of every player your computer
runs, whatever your empire knows, so it is a view for making computer players rather than
for playing against them.

### Mods' options

A mod whose rules declare game options ([docs/sdk/rules.md](sdk/rules.md) "Game options")
adds them to the setup screens; without such mods the screens are the original's.

| Where | What |
|---|---|
| Game Setup, Game Settings | `Mod Options` opens a window of every option of the game's mods, under each mod's name: a switch (a lamp) or a whole number in its range (under the pointer), with the mod's description. `Restore Defaults` sets them back. The settings are kept while Game Setup stays open, as its others are. |
| Quick Start | A line above the Mods line says the options' values; `Mod Options` under it opens the same window. |
| Multiplayer | The lobby's line under the mods says the values; the host changes them with `Mod Options`, and players who join see them with `See Mod Options`. |
| Setup files | `[options.mod."<mod id>"]` in a dedicated server's setup file ([MULTIPLAYER.md](MULTIPLAYER.md) "Setup files"). |

The options are part of the game, saved with it: every player of a game has the same.

### Scenarios of mods

A rules mod may hold scenarios: a game's setup with objectives of its own
([docs/sdk/rules.md](sdk/rules.md) "Scenarios"). The title screen's `Scenario` button opens
the Learn window; while the mods in use hold scenarios it has a `Scenarios` tab listing them,
each with its mod, its summary, its empires and its objectives. `Start Game` begins the one
chosen, played by its first human empire. A scenario for computer players only says so and
cannot start there (a dedicated server's setup file can start it:
`scenario = "<mod id>:<name>"`).

### What else mods add to the windows

Mods can also add to the game's windows ([docs/sdk/interface.md](sdk/interface.md)): their
orders in the order strip's Mod Orders, panels in the reports (the report's `MOD` button),
columns in the list windows (a `Mods` tab, or `Mod Columns`), pages in the Empires window,
keys on the Settings' Controls page, and their names in other languages (Settings → Modding,
"Language of the mods' text"). These change nothing in a game, so players of a network game
may have different ones.

## Where OpenSE4 keeps its own files

OpenSE4 keeps everything of yours in one folder, its **user folder**: settings, saved games,
logs, your mods and the history of the game you play. It never writes in the game's own
folder. OpenSE4's saved games are not in the original's format, so they never go into the
original's `SaveGame` folder either: Load Game only reads that folder (with *Change
Directory*), and Save Game's *Save for SE IV* writes a game for the original wherever you
choose (see "Games of the original").

### Which folder

The first of these rules that applies decides, for the game, the dedicated server
(`opense4-server`) and `opense4-sdk` alike:

1. The environment variable `OPENSE4_USER_DIR`, when it is set and not empty: the folder it
   names. The tests use it for a scratch folder of their own; it also gives one run another
   folder.
2. A **portable copy**: when a file named `portable.txt` lies beside the program, the folder
   `userdata` beside it (see "A portable copy" below).
3. Otherwise the folder the system keeps for a user's application data:

| Platform | User folder |
|---|---|
| Linux (and other Unix systems) | `$XDG_DATA_HOME/OpenSE4/`, which is `~/.local/share/OpenSE4/` unless you set `XDG_DATA_HOME` |
| Windows | `%APPDATA%\OpenSE4\`, which is `C:\Users\<your name>\AppData\Roaming\OpenSE4\` |
| macOS | `~/Library/Application Support/OpenSE4/` |

"Beside the program" means the folder that holds `opense4` (`opense4.exe` on Windows), or for
a macOS application bundle (`OpenSE4.app`) the folder that holds the bundle. Settings → Files
shows the folder in use and which rule chose it, and opens it (`Open Folder`); `opense4 --help`
prints it too. A system that names no folder at all (no `HOME`, no `APPDATA`) gets `userdata`
in the working folder.

### What it holds

| File or folder | What it is |
|---|---|
| `settings.toml` | The Settings window's Graphics (with the title bar), Controls and Files pages: this computer's display, keys and saves folder. |
| `classic_settings.toml` | The Options window and the Settings window's Sound and Modding pages, the mods you play with, the last game you saved (for Resume Game) and your progress in the lessons. |
| `saves/` | Your saved games (`.gam`), the autosaves `AutoSav0` to `AutoSav9` (named after the last digit of the turn count), and the players' history files copied beside each save. `saves/Space Empires IV/` is where *Save for SE IV* suggests writing games for the original. Settings → Files can put saved games in another folder (below). |
| `History/` | The players' statistics, history and log files of the game being played. |
| `maps/`, `empires/` | Maps ([MAPS.md](MAPS.md)) and empire files you saved. |
| `Mods/` | Your mods (see "Mods"). |
| `ModCache/` | The `.zip` mods, unpacked once; made again when it is missing. |
| `pbem/` | Play-by-e-mail game and turn files ([MULTIPLAYER.md](MULTIPLAYER.md)). |
| `lessons/` | Where you left a tutorial or training game, to go on from there. |
| `host_key.txt`, `known_hosts.txt` | This computer's key as a network host, and the hosts it trusts ([MULTIPLAYER.md](MULTIPLAYER.md)). |
| `queue_types.txt`, `tech_levels.txt`, `tech_areas.txt` | Construction queue templates, and the Research window's exports. |
| `python/` | The Python package `opense4-sdk` writes for external computer players ([docs/sdk](sdk/)). |
| `opense4.log`, `opense4.previous.log` | The log of this run and of the run before. |

### The saves folder

Settings → Files → *Saves folder* puts your saved games somewhere else: Save Game, the
autosaves, Load Game and Resume Game then use that folder. A folder written without a drive
or root (`My Saves`, `../Saved Games`) lies in the user folder and goes with it; one written
in full (`D:\Saved Games\OpenSE4`, `/home/ann/Games/se4-saves`) stays where it is. `Use`
refuses a folder in the game's own folder and one OpenSE4 cannot write in; `Default` brings
back `saves/`. The setting is kept in `settings.toml`.

### A portable copy

A portable copy keeps its user folder beside the program, so that saved games and settings
go wherever its folder goes: a copy on a USB stick, or several copies side by side. Settings →
Files → *Keep saves and settings in OpenSE4's folder* makes the copy you play portable, and
unticking it makes it use your user folder again. Each time, OpenSE4 first asks whether to
copy your files across:

- **Copy and Switch** copies every file of the folder used until now into the new one, except
  that folder's logs and its `ModCache`, then switches. A file the new folder already has is
  kept as it is.
- **Switch Only** switches without copying.

Nothing is ever deleted: the old folder keeps its files, and switching back finds them there.
The switch takes effect at once, and the settings in use are written to the new folder. The
log of the run stays in the old folder until OpenSE4 starts again, and mods are read from the
new `Mods` folder the next time the data is read (the Mods window's `Done`, or the next
start).

What the tick does is make or remove the file `portable.txt` beside the program (its text says
what it is for), so you can also do it by hand: create an empty text file named `portable.txt`
there, or delete it. In Windows Explorer with file name extensions hidden, name the new text
document `portable`.

OpenSE4 refuses to become portable when it cannot write in its folder, and says why. That is
the case for a copy installed with the Windows installer, which goes into Program Files,
where only administrators may write, and for a copy installed by a package manager into a
system folder. For a portable copy, download the zip file (Windows) or the tarball (Linux) of
the release instead and unpack it into a folder of your own; or keep only your saved games
elsewhere with the saves folder. The release packages carry `PORTABLE-README.txt`, which says
the same. A copy is never portable unless you make it so.

| Mode | Where the files go |
|---|---|
| Installed (Windows installer) | `%APPDATA%\OpenSE4\`; the uninstaller leaves it alone |
| Zip or tarball, as unpacked | The user folder of the table above |
| Zip or tarball made portable | `userdata\` beside `opense4.exe` / `userdata/` beside `opense4` |
| `OPENSE4_USER_DIR` set | The folder it names, portable or not |

Every run of the game writes its log to `opense4.log` in the user folder: on Windows, where
the game has no console, that file is where its messages are, including any file of your
install it could not find ("Not in the installed game: ..."). The run before keeps its log as
`opense4.previous.log`.

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
| A mod fails to load | The title screen and the Mods window say why. For a mod made for OpenSE4, `opense4-sdk check <mod>` reports every problem with the mod, file, line and record; for a classic mod, run `opense4-datacheck` on the game copy that holds it. |
| "the game was saved with a data set of N racial traits" or "another data set" when loading a game of the original | The game was played with a mod or another version of the data files: start OpenSE4 with `--classic-dir` on a copy of the game with that mod (see "Mods"). |
| No movement line after Move To | The line is the per-computer option "Display Ship Movement Lines" (Game Menu → Options, or Ctrl+L), off on a fresh install as in the original. Starting a new simultaneous game switches it on; joining one does not. It shows for the ship, base, unit group or fleet whose report is open. |
| No music, or no sound | See "No sound, no music, or clicks: what the log says" above: `opense4.log` names the device, the settings, each track and every file that cannot be played. Music Off in Game Menu → Options and `Allow CD Music` in the game's `Settings.txt` both silence the music, and the game is silent while it is in the background (see "Sound and music"). |
| Clicks, pops or crackling | The same section: the log shows whether the music ran dry; another device format (`SDL_AUDIO_FREQUENCY`, `SDL_AUDIO_FORMAT`) or sound interface (`SDL_AUDIO_DRIVER`) can be tried. |
| The window has no title bar | Settings → Graphics → "Hide the window's title bar" is on. Drag the game's top row (the main window's status bar, the top of the title screens) to move the window and its edges to resize it; the status bar's minimize button minimizes it, `Alt+Enter` switches to fullscreen and back, and unticking the setting brings the title bar back. |
| The title bar stays although "Hide the window's title bar" is on | The desktop decides about title bars itself: some Wayland desktops and window managers keep theirs. The Graphics page says so under the setting when the game can tell, and `opense4.log` has a line about it. The game's top row moves the window all the same. |
| "OpenSE4 cannot keep its files in its own folder" | OpenSE4's folder cannot be written, as for a copy installed into Program Files. Use the zip file or tarball for a portable copy, or choose a saves folder (see "A portable copy"). |
| Saved games or settings seem gone after moving OpenSE4 | A portable copy keeps them in `userdata` beside the program: move that folder with it. A copy that is not portable keeps them in your user folder, wherever the program is. Settings → Files shows the folder in use. |
| Something looks or behaves differently on another computer | Compare the per-computer settings first (`classic_settings.toml` and `settings.toml` in the folder above), then `opense4.log`. The game itself plays the same on every platform. |

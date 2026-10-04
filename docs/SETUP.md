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
| `Audio settings: sound effects on at 80 % (...), music on at step 5 of 5` | This computer's settings at the start, and again whenever they change. "music off" means Music Off is lit in Game Menu → Options. |
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

## Mods

Classic mods are replacement data files, and sometimes replacement art. To play one,
apply it to a **copy** of the game directory and pass that copy with
`--classic-dir`. OpenSE4 reads whatever that directory contains. Run
`opense4-datacheck` on the copy first.

## Where OpenSE4 keeps its own files

Saves (`saves/`, including the autosaves `AutoSav0` to `AutoSav9`, named after the
last digit of the turn count, and the players' history files copied beside each save),
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
| No movement line after Move To | The line is the per-computer option "Display Ship Movement Lines" (Game Menu → Options, or Ctrl+L), off on a fresh install as in the original. Starting a new simultaneous game switches it on; joining one does not. It shows for the ship, base, unit group or fleet whose report is open. |
| No music, or no sound | See "No sound, no music, or clicks: what the log says" above: `opense4.log` names the device, the settings, each track and every file that cannot be played. Music Off in Game Menu → Options and `Allow CD Music` in the game's `Settings.txt` both silence the music. |
| Clicks, pops or crackling | The same section: the log shows whether the music ran dry; another device format (`SDL_AUDIO_FREQUENCY`, `SDL_AUDIO_FORMAT`) or sound interface (`SDL_AUDIO_DRIVER`) can be tried. |
| Something looks or behaves differently on another computer | Compare the per-computer settings first (`classic_settings.toml` and `settings.toml` in the folder above), then `opense4.log`. The game itself plays the same on every platform. |

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

## 2. Point OpenSE4 at it

With no options, OpenSE4 looks in every Steam library it can find:

| Platform | Steam roots searched |
|---|---|
| Linux | `~/.local/share/Steam`, `~/.steam/steam`, Flatpak Steam (`~/.var/app/com.valvesoftware.Steam/...`) |
| Windows | `%ProgramFiles(x86)%\Steam`, `C:\Program Files\Steam` |
| macOS | `~/Library/Application Support/Steam` |

For each root it also reads `steamapps/libraryfolders.vdf`, so libraries on other
drives are found. It then looks for `steamapps/common/Space Empires IV Deluxe`.

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
- sound effects can be switched off, and the classic sound set chosen instead of the
  remastered one;
- music can be switched off or set to one of five volumes;
- the effects volume is in Options → Settings → Sound.

`--no-audio` starts without sound.

## Mods

Classic mods are replacement data files, and sometimes replacement art. To play one,
apply it to a **copy** of the game directory and pass that copy with
`--classic-dir`. OpenSE4 reads whatever that directory contains. Run
`opense4-datacheck` on the copy first.

## Where OpenSE4 keeps its own files

Saves (`saves/`, including the autosaves `Autosave 0` to `Autosave 9`, named after the
last digit of the turn count), maps (`maps/`,
see [MAPS.md](MAPS.md)), empire files (`empires/`), settings and logs go in your user
data directory. OpenSE4 never writes to the game directory.

| Platform | Location |
|---|---|
| Linux | `~/.local/share/OpenSE4/` |
| Windows | `%APPDATA%\OpenSE4\` |
| macOS | `~/Library/Application Support/OpenSE4/` |

## Troubleshooting

| Symptom | Fix |
|---|---|
| "No copy of the game was found" | Pass `--classic-dir` (step 2). Check that the directory contains `Data/Components.txt`. |
| Black window or crash at startup | Try `--renderer=opengl`. With the Vulkan SDK installed, `--validation` shows driver errors. |
| Text looks wrong in names | The data files are Latin-1 and are converted to UTF-8 on load. Report any file that still looks wrong. |
| A mod fails to load | Run `opense4-datacheck` on its data directory. The errors show the file and line. |

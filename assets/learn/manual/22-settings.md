---
windows: settings, options, empire-options
---
# Settings

OpenSE4 has three sets of settings:

- the **Settings** window: graphics, controls and sound, for every game on this computer;
- the **Options** window: animation, sound, music and autosave, also kept on this computer;
- the **Empire Options** window: how the game behaves for your empire, saved with the game.

## The Settings window

Open [Settings](window:settings) from the title screen, with `Ctrl+,` in the main window, or with
the `Settings` button of the [Options](window:options) window. It has three tabs. Your choices are
saved for every game.

### Graphics

| Setting | Choices |
|---|---|
| Window mode | Windowed (the default, 1600 by 900), borderless fullscreen or exclusive fullscreen. `Alt+Enter` switches between window and fullscreen at any time. |
| Window size, Resolution | The size of the window, or the screen mode for exclusive fullscreen. Press `Apply display` to use them. |
| Vertical sync, Frame rate limit | Smooth drawing (on by default). The frame rate limit can be set only while vertical sync is off. |
| Show frame rate | A frame counter in the corner. |
| Widescreen layout | **Extended** uses the whole width of a wide screen; **Classic 4:3** keeps the original proportions with bars at the sides. |
| Sharp pixels, Scale by whole multiples only | Crisp, unsmoothed classic art. |
| Text size | Larger or smaller text, from 0.75 to 1.5 times. |
| Renderer | Automatic (Vulkan, else OpenGL), Vulkan or OpenGL. This takes effect the next time you start OpenSE4. |

> If the screen stays black or OpenSE4 crashes at start, choose the OpenGL renderer here, or start OpenSE4 with `--renderer=opengl`.

### Controls

- **Right-click a sector to move the selected ship there**: on by default. Switch it off if you prefer right-click to only select.
- **Double-click time**: how fast two clicks must be to count as a double-click (0.30 seconds at first).
- **Keys**: every key you can change, grouped into windows, orders, the movement log, selection and display. Each action can have a key and an alternative. Click a key, then press the new one; `Esc` cancels and `Backspace` clears it. A key already used by another action is taken from it, and you are told. `Restore default keys` brings back the keys listed in [Hotkeys](hotkeys).

### Sound

**Sound effects** and **Music** switch each on or off, with a volume for each (music in steps of
20 %). **Classic sound effects** plays the original sound set instead of the remastered one, if
your copy has both. These are the same choices as in the Options window.

## The Options window

**Options** in the Game Menu (`F2`) opens the [Options](window:options) window, headed *Options In
Use*. Its choices are kept on this computer, except the autosave choice, which belongs to the game.

| Heading | Option | At first |
|---|---|---|
| Animation | Animate ship movement in the system window | on |
| Animation | Animate ship movement in combat | on |
| Sound | Sound On | on |
| Sound | Classic Sound Effects | off |
| Music | Music Off, or a volume from 20 % to 100 %. With music off, or not allowed by your copy's settings (below), the window opens on Music Off and keeps music off until you pick a volume. | 100 % |
| Tactical Combat | Fast Tactical Combat | off |
| System Display | Display Ship Movement Lines: the route of the selected ship, as a dashed line. `Ctrl+L` switches it too. | off |
| Autosave For This Game | None, or every 1, 2, 3, 5 or 10 turns (local and hotseat games only) | None, unless chosen at setup |

`Settings` opens the Settings window, and `Close` closes this one.

### Switches in your copy's Settings.txt

A few lines of the game's own `Data/Settings.txt` change how these windows behave:

- `Allow CD Music`: with `FALSE` no music plays at all. The Options window then opens on Music
  Off, and the Combat Options window's *Music On* stays dark.
- `Allow Export of Weapon And Component Data`: with `TRUE` the Weapons Report (Help) gets an
  `Export` button. It writes four text tables, weapons, components, weapon families and
  component families, into your saves folder and tells you where each one went.
- `System Ship Movement Delay Milliseconds`: above 0, a ship moving in the system window, and
  in the movement log replay, waits this many milliseconds after every square.

## Empire options

[Empire Options](window:empire-options) opens from the **Empire Options** button of
[Empire Status](window:empire-status) (`F11`). These options belong to your empire and are saved
with the game, so each player of a hotseat game has their own. A new empire starts with the
settings shown.

| Heading | Option | At first |
|---|---|---|
| General Options | Show the log at the start of each turn | on |
| General Options | Confirm ending the turn | on |
| General Options | Confirm scrapping | on |
| General Options | Confirm stellar manipulation | on |
| General Options | Confirm deleting a research project, an intelligence project, or the first item of a construction queue | on |
| General Options | Pick the colony type when a colony is founded (turn-based games) | on |
| General Options | Note when a similar system-wide ability already exists (when you queue a facility) | on |
| `Next / Previous` | Skip damaged ships, Stop only once per location, Skip ships in fleets: what the ship selection keys and buttons pass over | off |
| `Next / Previous` | Skip ships under construction (not used yet) | off |
| Ship Movement | Avoid minefields: routes avoid the sectors you tagged as minefields | on |
| Ship Movement | Avoid restricted systems: routes avoid your [systems to avoid](galaxy#claims-and-systems-to-avoid) | on |
| Ship Orders | Clear orders on entering a system with an enemy | on |
| Ship Orders | Clear orders on entering a system with any other empire | off |
| System Display | Warp point names: label warp points with their destination | on |
| System Display | Planet names | off |
| System Display | Colonizable planets: the green and red stars | on |
| System Display | System grid | off |
| System Display | Coordinate location: the coordinates of the sector under the pointer | on |
| System Display | Markers: letters over your colonies for facilities of each kind (resupply depots, spaceports, space yards and so on) | off |
| Galaxy Display | Show grid lines, Show warp lines | on |
| Latest Items | Only the latest items for construction, Only the latest components for designs | off |
| Politics | Claim every system we colonize | on |

> If your ships keep stopping after every warp jump, check the Ship Orders options: they clear orders whenever an enemy is in the arrival system.

## Command line

OpenSE4 also understands options on its command line, for example:

| Option | What it does |
|---|---|
| `--classic-dir=FOLDER` | Use the game copy in that folder. |
| `--renderer=opengl` | Use the OpenGL renderer instead of Vulkan (`vulkan` and `auto` work too). |
| `--fullscreen`, `--size=1920x1080` | Start in borderless fullscreen, or with a window of that size. |
| `--no-audio` | Start without sound or music. |
| `--quick-start[=RACE]` | Skip the title screen and start a quick game, with `--seed`, `--systems`, `--empires` and `--turn-style` to set it up. |
| `--tutorial=LESSON`, `--training=GAME`, `--manual` | Start a lesson or a training game, or open this manual. |
| `--pbem=GAME.gam` | Play your turn of a play-by-e-mail game (see [Multiplayer](multiplayer#play-by-e-mail)). |

`opense4 --help` lists them all.

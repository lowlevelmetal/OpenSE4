---
windows: settings, empire-options
---
# Settings

OpenSE4 has two sets of settings: the **Settings** window for graphics, controls and sound, and
the **Empire Options** window for how the game behaves while you play. This chapter describes
both.

## The Settings window

Open [Settings](window:settings) from the title screen, from the Game Menu, or with `Ctrl+,`
during a game. It has three tabs. Your choices are saved for every game.

### Graphics

| Setting | Choices |
|---|---|
| Window mode | Windowed, borderless fullscreen or exclusive fullscreen. `Alt+Enter` switches between window and fullscreen at any time. |
| Window size, Resolution | The size of the window, or the screen mode for exclusive fullscreen. Press `Apply display` to use them. |
| Vertical sync, Frame rate limit | Smooth drawing, or a cap on frames per second. |
| Show frame rate | A frame counter in the corner. |
| Widescreen layout | **Extended** uses the whole width of a wide screen; **Classic 4:3** keeps the original proportions with bars at the sides. |
| Sharp pixels, Scale by whole multiples only | Crisp, unsmoothed classic art. |
| Text size | Larger or smaller text, from 0.75 to 1.5 times. |
| Renderer | Automatic (Vulkan, else OpenGL), Vulkan or OpenGL. This takes effect the next time you start OpenSE4. |

> If the screen stays black or OpenSE4 crashes at start, choose the OpenGL renderer here, or start OpenSE4 with `--renderer=opengl`.

### Controls

- **Right-click a sector to move the selected ship there**: on by default. Switch it off if you prefer right-click to only select.
- **Double-click time**: how fast two clicks must be to count as a double-click.
- **Keys**: every rebindable key, grouped into windows, orders, selection and display. Each action can have a key and an alternative. Click a key, then press the new one; `Esc` cancels and `Backspace` clears it. A key already in use is moved to the new action, and you are told. `Restore default keys` brings back the classic keys listed in [Hotkeys](hotkeys).

### Sound

Switch sound effects and music on or off, and set their volumes. If your copy of the game has the
remastered sounds, OpenSE4 uses them; switch them off here to hear the classic set.

## Empire options

[Empire Options](window:empire-options) opens from the **Empire Options** button of
[Empire Status](window:empire-status) (`F11`), or **Options** in the Game Menu. Some options are
kept on this computer for every game; the ship and colony options belong to your empire in this
game.

| Group | Option | What it does |
|---|---|---|
| General | Open the log when a turn starts | Opens the Log at the start of a turn when there is news. On by default. |
| General | Ask before ending the turn | Asks for confirmation when you end the turn with `F12` or the End Turn button. Off by default. |
| `Next \ Previous` | Skip damaged ships | The ship selection keys and buttons skip damaged ships. |
| `Next \ Previous` | Stop only once per location | The selection keys stop once in each sector, not at every ship. |
| Ship Movement | Never route through the systems to avoid | Routes avoid your [systems to avoid](galaxy#claims-and-systems-to-avoid). On by default. |
| Ship Orders | Clear orders on warping into a system with enemies | Stops ships that jump into danger. On by default. |
| Ship Orders | Clear orders on warping into a system with any other empire | Stops ships that meet anyone at all. |
| Colonies | Choose the colony type when a colony is founded | In turn-based games, asks you for the type of each new colony. On by default. |
| Autosave | Autosave (this game) | Save every 1, 2, 3, 5 or 10 turns, or never (local and hotseat games). |
| System Display | Show where known warp points lead | Labels warp points with their destination. |
| System Display | Show movement lines | Draws your selected ship's route. `Ctrl+L` switches it too. |
| System Display | Show waypoint markers | Draws your waypoints in the system panel. |
| System Display | Animate ship movement | Ships glide from sector to sector instead of jumping. |
| Sound | Effects, music, volumes, remastered sounds | The same as in the Settings window. |

`Defaults` resets every option.

Other switches in the window are not used yet in this version of OpenSE4.

> If your ships keep stopping after every warp jump in a simultaneous game, check the Ship Orders options: they clear orders whenever an enemy is in the arrival system.

## Command line

OpenSE4 also understands options on its command line, for example:

| Option | What it does |
|---|---|
| `--classic-dir=FOLDER` | Use the game copy in that folder. |
| `--renderer=opengl` | Use the OpenGL renderer instead of Vulkan. |
| `--fullscreen`, `--size=1920x1080` | Start in fullscreen, or with a window of that size. |
| `--no-audio` | Start without sound or music. |
| `--quick-start` | Skip the title screen and start a quick game. |

`opense4 --help` lists them all.

# Hotkeys

These are the default keys. Most of them can be changed in the Controls tab of the
[Settings](settings#controls) window, and the Help window's [Hotkeys](help:hotkeys) tab always
shows the keys as they are bound now. The keys of the main window work while no other window is
open and no text field has the keyboard. An order key works only while its order button is lit;
pointing at an order button shows its name and key at the top of the system panel.

## Windows

| Window or action | Key |
|---|---|
| Help | `F1` |
| Game Menu | `F2` |
| Designs | `F3` |
| Planets | `F4` |
| Colonies | `F5` |
| `Ships\Units` | `F6` |
| Construction Queues | `F7` |
| Research | `F8` |
| Empires | `F9` |
| Log | `F10` |
| Empire Status | `F11` |
| End Turn | `F12` or `Enter` |
| Settings | `Ctrl+,` |
| The manual page for the window in front | `Shift+F1` |
| Show or hide the lesson or training panel | `Ctrl+H` |

## Orders

| Order | Key |
|---|---|
| Move To | `M` |
| Move To Waypoint, from a list | `Ctrl+W` |
| Move To Waypoint 0 to 9 | `Ctrl+0` to `Ctrl+9` |
| Set waypoint 0 to 9 at the selected sector | `Alt+0` to `Alt+9` |
| Warp | `W` |
| Attack | `A` |
| Colonize | `C` |
| Explore | `E` |
| Resupply At Nearest | `S` |
| Repair At Nearest | `R` |
| Sentry | `Y` |
| Set Patrol (then `Enter` or right-click to finish) | `P` |
| Repeat Orders on or off | `K` |
| Clear Orders | `Delete` or `Backspace` |
| Fleet Transfer | `F` |
| Change Formation \ Strategy (fleets) | `H` |
| Construction queue | `Q` |
| Cargo Transfer | `T` |
| Launch \ Recover Units (turn-based games) | `U` |
| Launch Units Remotely, Recover Units Remotely | `I`, `O` |
| Load Cargo, Drop Cargo | `L`, `D` |
| Stellar Manipulation | `B` |
| Scrap \ Analyze \ Mothball | `G` |
| View Orders | `V` |
| Change Name | `N` |
| Cloak, Decloak | `Z`, `X` |
| Use Component | `Ctrl+Z` |
| Sweep Mines | `Ctrl+M` |
| Tag or untag the selected sector as a minefield | `Ctrl+T`, `Ctrl+R` |
| Abandon Planet | `Ctrl+A` |
| Scrap Facilities | `Ctrl+K` |
| Toggle Minister Control | `Ctrl+Y` |

The `Ctrl` and `Alt` number keys cannot be changed.

## Movement log

In simultaneous games the main window can replay the last turn's movement. With *animate ship
movement in the system window* on, each ship's every step in the shown system is played on its
own, one after another, the ships starting the way they faced when the turn began. While a day's
moves are being played, these keys are ignored.

| Action | Key |
|---|---|
| Play the movement log | `Ctrl+P` |
| Play it for every ship | `Ctrl+U` |
| Step it one day | `Ctrl+I` |
| Rewind it: the ships go back where they started | `Ctrl+O` |

## Selecting

| Key | Action |
|---|---|
| `Space` | Next ship without orders (turn-based games: next ship with movement left) |
| `Ctrl+N`, `Ctrl+B` | Next, previous ship |
| `Ctrl+F`, `Ctrl+D` | Next, previous fleet |
| `Ctrl+C`, `Ctrl+X` | Next, previous colony |
| `Shift+click` in the list of a sector | Tag a ship (a ship in a fleet tags the fleet), so that one order goes to every tagged ship |
| `Shift+A`, `Shift+C` | Tag all your ships in the selected sector, clear the tags |
| `Esc` | Cancel picking a place; otherwise clear the selection |

## Display

| Key | Action |
|---|---|
| `Ctrl+L` | Show or hide the selected ship's movement line |
| `Ctrl+S` | Sound effects on or off |
| `Alt+Enter` | Switch between window and fullscreen |
| `Ctrl+Shift+N` | Show or hide the notes of the computer players of mods (see [Settings](settings#modding)) |

Mods may add keys of their own for their orders, panels and pages; the Controls tab lists them
under each mod's name, and a mod's key never takes one of these (see [Settings](settings#controls)).

## Mouse in the main window

| Action | Result |
|---|---|
| Left-click a sector | Select it: its report, or a list of what is there |
| Right-click a sector | Move the selected ship there, when it can move (can be switched off); otherwise select |
| Right-click while picking a place | Choose it (and finish a patrol) |
| Left-click the galaxy panel | Show that system |
| Right-click the galaxy panel | Open the Galaxy Map |

## Dialogs

| Key | Action |
|---|---|
| `Y` | Yes, in every Yes/No question |
| `N`, `Esc`, `Enter` | No, in every Yes/No question, such as *End the turn now?* |
| `T`, `S` | Tactical or Strategic, when a battle asks how to fight it |
| `Esc` | Close a window or cancel |
| `Enter` | Close a window whose bottom button is `Close`; continue from a notice |
| `Delete` | Remove the selected item from a construction queue, with the pointer over the queue |

## Tactical combat

| Key | Action |
|---|---|
| `Space` or `Ctrl+N` | Next piece that can move |
| `Ctrl+B` | Previous piece that can move |
| `Ctrl+F`, `Ctrl+D` | Next, previous piece that can fire |
| `Shift+A`, `Shift+C` | Tick all, untick all weapons of the selected piece |
| `L` | Launch units from the selected piece |
| `T` | Drop troops at once on the adjacent enemy colony |
| `R` | Ram (then click the target) |
| `C` | Capture (then click the target) |
| `E` | End your phase |
| `Alt+1` to `Alt+9` | Make the selected piece leader of a group, then choose a formation |
| `Ctrl+1` to `Ctrl+9` | Make the selected piece a member of a group |
| `Alt+0` or `Ctrl+0` | Clear the selected piece's group |
| Arrow keys | Scroll the map |
| `Esc` | Cancel aiming |
| `Space` while pieces move | Skip the animation |

The piece keys work once the battle has begun.

## Combat replay

| Key | Action |
|---|---|
| `Space` | Next: play the next combat turn |
| `Esc` | Close the replay |

## The manual

| Key | Action |
|---|---|
| `Alt+Left`, `Alt+Right` | Back and forward through the pages you have read |

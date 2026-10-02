---
windows: tactical-combat, tactical-orders, tactical-options, tactical-launch, combat-piece-report, strategic-combat, combat-replay, combat-replay-options, combat-simulator
---
# Battle windows

This chapter shows you how to fight a battle by hand, how to watch one, how to replay it, and
how to test your designs in the Combat Simulator. The rules behind all of them are in
[Combat](combat).

## How battles reach you

In a turn-based game on this computer (alone or hotseat), every battle with a human side stops
the game as it breaks out, before the first combat turn, and asks how to fight it. The question
is the **Strategic Combat** window itself, with two buttons: `Tactical` (`T`) fights it in the
Tactical Combat window, `Strategic` (`S`) lets the strategies fight it right there while you
watch. One answer covers every human side in that battle. When the battle breaks out during a
computer player's turn, a notice naming the system comes first; `Begin` goes on. Nothing else
happens until the battle is over and its window closed: the battle reports, the Log entries and
the rest of the turn come after.

- In a game set up with **Strategic combat only**, the Strategic Combat window opens the same way, with `Begin` and `Close` instead of the question.
- In simultaneous games you read about battles in the Log, and watch them with `Combat Replay`; when the game's settings ask for it, every battle on this computer is shown in the Strategic Combat window as it breaks out, battles between computer players included, without a notice first.
- In network and play-by-e-mail games the host fights every battle at once; you watch the battles you were in (by e-mail, those your own orders started) afterwards.

## Tactical combat

When you choose **Tactical**, the **Tactical Combat** window fills the screen. You cannot close it
or save until the battle is over.

Press **Begin** at the bottom right to start the battle; until then the status bar says so and
the other controls are dim. `Begin` then becomes `End Turn`.

**The status bar** at the top shows the battle's sector, the combat turn, whose phase it is (or
*paused (Auto)*) and how many pieces each side has left, with a star on the side whose phase it
is. The `< Move >` and `< Fire >` selectors step through your pieces that can still move or fire.

**The map** shows the battlefield. The mouse wheel zooms, dragging with the middle button pans,
and the arrow keys scroll. The small overview map shows the whole battlefield with a dotted
rectangle for the part you see; click or drag on it to move the view.

In your phase:

- **Left-click** one of your pieces to select it.
- **Left-click an empty square** to move the selected piece there. The pointer shows the path.
- **Left-click an enemy** to fire every ticked weapon of the selected piece at it. The pointer shows whether you can fire, and why not.
- **Right-click** any piece, or click the name of the selected piece, for its **Combat Piece Report**: movement, shields, damage, supplies, the number of targets it can engage, its combat group and formation.
- Click the map, or press `Space`, to skip an animation.

**The current piece panel** shows the selected piece's shields, damage, movement and supplies,
and its **weapon list**. Click a weapon to tick or untick it; `Shift+A` ticks all and `Shift+C`
unticks all. Hover over a weapon to see its chance to hit and its damage against the target. The
target panel shows the enemy under the pointer, the last one you fired at or the nearest one:
its shields, its damage and its distance in squares.

The buttons:

| Button | What it does |
|---|---|
| `Options` | The **Combat Options** window (below). |
| `Orders` | The special orders below. |
| `Auto: Off`, `Auto: On` | Let every side follow its strategies. Play then pauses after the last player's phase of each combat turn; `End Turn` goes on. Press again to take control back. |
| `End Turn` | End your phase (`E`). A phase in which none of your pieces can act ends by itself. |

The **Tactical Combat Orders** window holds:

- `Launch Units`: launch fighters, satellites or drones from the selected piece, 1, 5, 10 or all of a kind at a time, within its launch rate for the turn. `L` opens it directly.
- `Launch Fighters in Groups`: launch fighters in groups of 5 to 50.
- `Drop Troops` (`T`): land the selected ship's troops at once on an adjacent colony of another empire (see [Ground combat and capture](ground-combat-and-capture)).
- `Ram Ship` (`R`) and `Capture Ship` (`C`): close the window, then click the target. Capturing needs an adjacent enemy ship with its shields down and boarding parties on your ship.
- `Set Group Leader`, `Set Group Member`, `Clear Group Assignment`, `Clear All Group Assignments`: combat groups 1 to 9. A leader picks a formation, and its members follow it. `Alt+1` to `Alt+9` and `Ctrl+1` to `Ctrl+9` do the same from the keyboard; `Alt+0` or `Ctrl+0` clears the selected piece's group.
- `Resolve Combat`: let every side's strategies fight the rest of the battle. It asks first.

Your drones always act by themselves, at the start of your phase. Pieces you leave idle do not
fire by themselves.

The **Combat Options** window, headed *Options In Use*, holds the animation of ship movement,
sound and music, *Fast Tactical Combat*, and display switches: group identifiers, the viewing
rectangle on the map, centring the map on the current ship, the chance to hit of each weapon when
you point at an enemy, and the grid. In the simulator it also has `Stop Combat`.

> Look at the chance to hit before you click: a 5 % shot wastes supplies and a reload.

When the battle is over, **Combat Complete** appears; after `OK` the window closes by itself.

## Strategic combat

The **Strategic Combat** window shows a battle fought by the strategies. Press `Begin`: the battle
is then fought before your eyes, as fast as it can be shown: the small map moves after each side's
moves, and the forces list and the combat turn in the title strip change after each combat turn.

- The **Combat Forces** list shows, for each empire, its pieces by hull with how many are left (**Current**) and how many were lost (**Lost**, in red), and its planets.
- The small map shows every piece as a coloured square. Point at one to see its name and owner.
- `Close` stays dim until the battle is over.

When troops land during the battle, the Ground Combat window opens and the battle waits for it.

## Replays

Select a battle in the [Log](window:log) and press `Combat Replay` to play it back in the
**Combat Replay** window. It looks like the tactical window, with a list of what happens in each
combat turn: moves, shots, hits, misses, launches and losses, and a summary at the end.

- `Next` (or `Space`) plays the next combat turn.
- `Options` opens the replay's options: animation, *Fast Tactical Combat*, the viewing rectangle, the grid, and `Stop Replay`.
- `Esc` closes the replay.

Point at a piece to see its design and owner.

## The Combat Simulator

The [Combat Simulator](window:combat-simulator) fights a test battle that changes nothing in your
game: no losses, no experience. Open it with `Simulator` in the [Designs](window:designs) window.

The battle is fought between ten imaginary races, **Race 1** to **Race 10**. You command Race 1;
the computer plays the others (change this with `Computer Control`).

1. In **Owner For Item** at the bottom right, choose the race that new items join.
2. Click an entry in the **Items** list at the top right to add it: your designs, the enemy designs you have seen (with their empire's name), and the planets of your home system. Each click adds one ship, or one unit to that race's group of its kind. Right-click an item for its report.
3. The **Combat Vehicles** list on the left shows everything in the battle, one row per ship, unit group or planet, with its race. Click a row to remove it.
4. `Fleets For Plr` puts a race's ships into one fleet, with a formation and a strategy. `Change Cargo` loads fighters, troops or platforms into a ship or planet. `No Obsolete` hides your obsolete designs; `Strategies` opens the strategy editor.
5. Choose **Tactical** or **Strategic** and press `Begin`.

With **Tactical**, the simulator closes while you fight in the Tactical Combat window and comes
back afterwards with the same setup. With **Strategic**, every race follows its strategies and the
Strategic Combat window opens over the simulator. `Cancel` closes the simulator and forgets the
setup.

Each design starts the test battle new, fully supplied and without experience. A race whose first
item is an enemy design gets that enemy's race and technology. The races start on different edges
of the map, and races with planets or bases start in the middle.

> Before you build twenty ships of a new design, pit five of them against the enemy designs you have seen. The simulator is free.

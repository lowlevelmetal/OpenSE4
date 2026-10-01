---
windows: tactical-combat, tactical-orders, tactical-options, strategic-combat, combat-replay, combat-simulator
---
# Battle windows

This chapter shows you how to fight a battle by hand, how to watch one, how to replay it, and
how to test your designs in the Combat Simulator. The rules behind all of them are in
[Combat](combat).

## Tactical combat

When you choose **Tactical**, the **Tactical Combat** window fills the
screen. You cannot close it or save until the battle is over.

**The status bar** at the top shows the battle's sector, the combat turn, whose phase it is and
how many pieces each side has left. The `< Move >` and `< Fire >` selectors step through your
pieces that can still move or fire.

**The map** shows the battlefield. The mouse wheel zooms, dragging with the middle button pans,
and the arrow keys scroll. The small overview map shows the whole battlefield; click or drag on
it to move the view.

In your phase:

- **Left-click** one of your pieces to select it.
- **Left-click an empty square** to move the selected piece there. The pointer shows the path.
- **Left-click an enemy** to fire every ticked weapon of the selected piece at it. The pointer is a green crosshair when you can fire, and red, with the reason, when you cannot.
- **Right-click** any piece for a short report: its owner, design, movement, shields, damage, supplies and targets.

**The current piece panel** shows the selected piece's shields, damage, movement and supplies,
and its **weapon list**. Click a weapon to tick or untick it; `Shift+A` ticks all and `Shift+C`
unticks all. Hover over a weapon to see its chance to hit and damage against the current target.
The target panel shows the enemy under the pointer, or the last one you fired at, with your
expected damage and chance to hit.

The buttons:

| Button | What it does |
|---|---|
| `Options` | Display options: animation and its speed, the grid, the selected piece's reach, piece names, and ending your phase when no enemy is left. |
| `Orders` | The special orders below (`L`). |
| `Auto` | Let every side follow its strategies from the next phase on. Press again to take control back. |
| `End Turn` | End your phase (`E`). It reads `End Battle` once no enemy is left. |

The **Tactical Combat Orders** window holds:

- **Launch Units**: launch fighters, satellites or drones from the selected piece, 1, 5, 10 or all of a kind at a time, within its launch rate for the turn.
- **Launch Fighters in Groups**: launch fighters in groups of the size you pick.
- **Drop Troops** (`T`), **Ram Ship** (`R`) and **Capture Ship** (`C`): close the window, then click the target. Troops land on an adjacent enemy colony, and capturing needs an adjacent enemy ship with its shields down (see [Ground combat and capture](ground-combat-and-capture)).
- **Combat groups**: make the selected piece the leader of a numbered group with a formation, or a member of one, or clear its group. Members follow their leader. `Alt+0` to `Alt+9` and `Ctrl+0` to `Ctrl+9` do the same from the keyboard.
- `Auto This Phase`: let your strategies play this one phase.
- `Resolve Combat`: let every side's strategies fight the rest of the battle. It asks first.

Your drones always act by themselves, at the start of your phase. Pieces you leave idle do not
fire by themselves.

> Fire your long-range weapons first, then move in. And always look at the hit chance before you click: a 5 % shot wastes supplies and a reload.

When the battle ends, the result panel sums it up. `Replay` plays it back; `Done` closes it.

## Strategic combat

The **Strategic Combat** window shows a battle your ships fought by their
strategies. The battle has already been decided when the window opens: it plays it back.

- The small map shows every piece as a coloured square. Hover over one to see its owner.
- The **Forces** list shows, for each side, how many of each hull size are left and how many were lost.
- `Begin` starts the playback (then `Pause`, `Continue` and `Watch Again`), and `Skip to End` jumps to the result.
- When troops land, the playback pauses and opens the Ground Combat window; `Ground Combat` opens it again later.

In a turn-based game you see the battles your own orders started and those you chose to fight
strategically, one after another. In simultaneous games you normally read about battles in the
Log instead.

## Replays

`Combat Replay` in the [Log](window:log) plays back any battle of the last turn in the
**Combat Replay** window.

| Control | What it does |
|---|---|
| `Play`, `Pause` (`Space`) | Play or pause the battle. |
| `Next Round`, `Previous Round` (arrow keys) | Step one combat turn. |
| `Next Event` | Step one shot or move. |
| `Rewind` (`Home`) | Go back to the start. |
| `Speed` | Change the playback speed. |
| `Previous Battle`, `Next Battle` | Switch between the turn's battles. |
| `Ground Combat` | Show a ground combat fought in this battle. |

A list beside the map tells what happens in each round: moves, shots, hits, misses, launches and
losses.

## The Combat Simulator

The [Combat Simulator](window:combat-simulator) fights a test battle that changes nothing in your
game: no losses, no experience. Open it with `Simulator` in the [Designs](window:designs) window.

1. Pick a **side** with the tabs at the top. Side 1 is yours; `Add Side` adds more, up to ten. All sides are enemies of each other.
2. **Click** designs in the list on the left to add them to that side: your designs, enemy designs you have seen, and the planets of your home system. Click a design again to add one more. **Right-click** an item in the battle list to remove it.
3. Select an item to set its count and strategy. `Fleets for Plr` puts a ship into its side's fleet, with a formation and a strategy. `Change Cargo` loads fighters, troops or platforms.
4. Choose **Tactical** or **Strategic**, and with `Computer Control` which sides the computer plays. `No Obsolete` hides obsolete designs; `Strategies` opens the strategy editor.
5. Press `Begin`.

Each design starts the test battle new, fully supplied and without experience. A side that starts
with an enemy design gets that enemy's race and technology.

> Before you build twenty ships of a new design, pit five of them against the enemy designs you have seen. The simulator is free.

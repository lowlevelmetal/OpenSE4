---
windows: research, tech-tree
---
# Research

Research unlocks everything you build: new hulls, components, facilities, weapon mounts and
intelligence projects. This chapter explains where research points come from, how projects are
paid for and how to use the Research and Tech Tree windows.

## Research points

Your colonies produce **research points** with research facilities, just as they produce
minerals with mines (see [Economy](economy#how-a-colony-produces)). The race's Intelligence
characteristic, its culture, the colony's mood and population and research-boosting facilities
all change the output. Planet value does not. Treaties of Trade and Research Alliance or better
add a share of your partner's research (see [Diplomacy](diplomacy#trade)).

Research points work on a one-turn delay:

1. At the end of a turn, your colonies produce points into your research pool.
2. During the next turn, the Research window shows that pool as **Research Points Available**.
3. At the end of that turn, the whole pool is spent on your projects and the pool is emptied.

Points that no project can use are lost. They are never saved for later.

> Keep the queue full. An empty queue throws away a whole turn of research.

At the start of a game your pool also holds the Starting Resources amount, so the first turn's
research goes a long way.

## Technology areas and levels

Technology is divided into **areas**, such as a weapon line, a hull family or a kind of
facility. Each area has levels, from 1 up to its maximum. Each level of an area may unlock
something new, and some areas only appear once you reach a level in another area.

Some areas are special:

- **Racial areas** are open only to races with the matching racial trait.
- **Unique areas** appear only when you colonize a planet with certain ancient ruins.
- An area that the game setup removed can never be researched.

Your race's starting levels depend on the **Technology Level for New Player** setup option:
Low keeps every area at its starting level, Medium raises them to a middle level, and High
starts every area at its maximum. You always start knowing how to colonize your home planet
type.

## What a level costs

Each area has a base cost, and each level costs more than the one before. The **Technology
Cost** setup option decides how fast costs climb. For an area with a base cost of 5,000:

| Level | Low | Medium (default) | High |
|---|---|---|---|
| 1 | 5,000 | 5,000 | 5,000 |
| 2 | 10,000 | 10,000 | 20,000 |
| 3 | 15,000 | 22,500 | 45,000 |
| 5 | 25,000 | 62,500 | 125,000 |
| 10 | 50,000 | 250,000 | 500,000 |

With Low costs, level L costs L times the base. With Medium, it costs half of L squared times
the base (but never less than L times the base). With High, it costs L squared times the base.

## The research queue

You can run up to **12 projects** at once. A project is always the next level of one area, and
an area can be in the queue only once. At the end of each turn the pool is shared out in one of
two ways:

- **Divide Pts Evenly** (on for a new empire): every project gets an equal share, whether it needs it or not.
- **In queue order** (with Divide Pts Evenly off): the first project takes what it needs to finish its level, then the next, and so on until the points run out. With 10,000 points and three projects that each need 4,000, the first two finish and the third gets 2,000.

A project finishes when its progress reaches the cost of the level. The area gains exactly one
level and the project leaves the queue; points beyond the cost are lost. A project can finish
at most one level per turn.

With **Repeat Projects** on, a finished area goes back to the end of the queue for its next
level, until it reaches its maximum.

Removing a project throws away its progress. Reordering keeps it: progress moves with the
project.

> Switch Divide Pts Evenly off and put the one thing you need most at the top. Dividing evenly slows every project down, and only pays when several areas matter equally.

## The Research window

Open [Research](window:research) with `F8` or the flask button.

- The title strip shows the **Research Points Available** this turn.
- The list fills the top of the window: every area you can research now, grouped by kind, with your current level and the cost of the next level. Hover over an area to see its description and what its next level unlocks. **Click an area** to add it to the end of the queue.
- Below the list, your current projects appear four at a time, each with an estimate of when it will finish and a progress bar. The page buttons `Projects 1-4`, `Projects 5-8` and `Projects 9-12` switch between them. **Click a project** to cancel it; the game asks first (switch this off with the Empire Option *Confirm deleting a research project*), because its progress is lost.
- `Repeat Projects` and `Divide Pts Evenly` switch the two options above.
- `Reorder Projects` (with at least two projects) opens a window where you move projects up, down, to the top or to the bottom.
- `Tech Tree` opens the [Tech Tree](window:tech-tree).

The estimate uses this turn's points and your current production for the turns after. It reads
*This turn*, a number of years (0.1 years is one turn) or *Never*.

## The Tech Tree window

The [Tech Tree](window:tech-tree) shows every area allowed in the game, not just the ones you
can research now.

- **Tech Areas** lists each area with your level, what it requires and what it leads to. Green areas are complete, white ones can be researched now and grey ones are locked. Click an area to see its levels.
- **Tech Levels** shows one area level by level: the cost of each level and what it unlocks (components, facilities, hulls, intelligence projects and other areas).
- **Export** saves the current view as a text file in your OpenSE4 user data folder.

## Other ways to gain technology

- **Ancient ruins**: colonizing a planet with ruins grants random levels, or a unique area (see [Planets and colonies](planets-and-colonies#ancient-ruins)).
- **Gifts and trades**: another empire can give you technology in a package. You get its level in that area, if that is higher than yours. The game setup can forbid technology gifts.
- **Theft**: an intelligence project can steal one level of an area in which the target is ahead of you (see [Intelligence](intelligence)).
- **Surrender**: when an empire surrenders to you, you gain one level in every area where it was ahead of you.

## Research advice

- Watch the **Cost** column. At the start of a game some levels cost a few turns of research and others a hundred turns or more. Mix a few cheap, useful levels with one long-term goal, such as colonizing another planet type.
- Research grows with your economy: more colonies and research facilities mean more points, and every later level gets there sooner.
- Do not neglect weapons and armor for long. Computer players will notice a weak neighbour.
- Upgraded facilities do not appear by themselves: after you research a better facility, use **Upgrade Facilities** in the [Construction Queues](window:queues) window.
- After you research better components, use **Upgrade** in the [Designs](window:designs) window to make new designs with the newest parts (see [Ship design](ship-design)).

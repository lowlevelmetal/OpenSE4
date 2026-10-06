# Tech areas

A tech area is one line of research: an empire raises it level by level with research
points, and the levels unlock what other tables require. The areas live in
`TechArea.txt`; data patches change them through the table `tech_areas`, and each record
is named by its `Name`. Nearly every other table points at tech areas by name: hulls,
components, facilities, weapon mounts and intelligence projects become available when an
empire's levels meet their requirements, and other areas appear in the research list when
theirs are met.

## The fields

### Name and display

| Field | What it does | Values |
|---|---|---|
| `Name` | Names the area. Other records refer to it by this name, in any letter case. | Text, required. Two areas with the same name are an error. |
| `Group` | The heading the Research window files the area under. Areas with the same group text are listed together, groups in the order their first area appears in the file. It changes no rule. | Text, required. |
| `Description` | Shown in the Research window and the Help window. | Text, optional. |

### Levels and cost

| Field | What it does | Values |
|---|---|---|
| `Maximum Level` | The highest level the area can reach. Research, gifts, theft and ruins never take it past this. | Whole number, required. 0 makes an area that can never be researched. |
| `Level Cost` | The base cost in research points from which every level's cost grows (see "The cost of a level" below). | Whole number of research points, required. Keep it above 0: at 0 or below every level costs nothing and a queued area gains a level each turn. |
| `Start Level` | The level every empire starts with under the Low "Technology Level for New Player" setting (the default). | Whole number, optional, default 0. |
| `Raise Level` | The level the area starts at under the Medium setting, when it is higher than `Start Level`. Under High every area starts at its maximum and this field is not used. | Whole number, optional, default 0. |

### Who can research it

| Field | What it does | Values |
|---|---|---|
| `Racial Area` | When above 0, only a race with a racial trait of `Trait Type` `Tech Area` and `Value 1` equal to this number can see the area. Several areas may share a number: one trait opens them all. | Whole number, optional, default 0 (any race). |
| `Unique Area` | When above 0, the area stays hidden until the empire colonizes a planet with the `Ancient Ruins Unique` ability whose value is this number (see "Racial and unique areas"). | Whole number, optional, default 0 (no ruins needed). |
| `Can Be Removed` | Whether Game Setup's "Technology Areas Allowed" list may leave the area out of a game. An area with `False` is always allowed and its box cannot be cleared. | `True` or `False`, optional, default `True`. |

### Requirements

| Field | What it does | Values |
|---|---|---|
| `Number of Tech Req` | How many requirements follow. | Whole number, optional, default 0. |
| `Tech Area Req N` | An area the empire must have... | The name of a tech area; an unknown name is an error. |
| `Tech Level Req N` | ...at this level or higher. | Whole number. |

The requirements form the list `requirements` in a patch; an entry's fields are
`Tech Area Req` and `Tech Level Req`. All of them must hold (they are joined with AND)
before the area appears in the research list. An area may require areas that come later
in the file.

OpenSE4 reads every field above and uses each one; there are no fields it ignores in this
file. The SDK's rules view gives scripts the same values as its `tech` records.

## How the fields work together

### Research points

Each turn, an empire's colonies produce research points from facilities with `Point
Generation - Research`. The output is raised or lowered by the planet and system modifiers
(`Planet Point Generation Modifier - Research`, `System Point Generation Modifier -
Research`), the colony's mood and population, and the race's research effect: the
`Intelligence` characteristic, the culture's `Research` percentage and `Research
Production` traits (see [races.md](races.md)). `Generate Points Research` adds a flat
amount, and Trade and Research Alliances or better bring a share of a partner's research
(spec 02 §5, spec 05 §1.1, §3.3).

The points go into the empire's **research pool**. At the empire's research step, at the
end of its turn, the whole pool is spent on the queue and then set to 0: points the queue
cannot use are lost. The new turn's production is added afterwards, so the points shown
during a turn were made at the end of the turn before. When a game is created, the pool
starts at the Starting Resources amount plus one turn of the empire's research
production. The pool never holds more than 2,000,000,000.

### The cost of a level

Let LC be the area's `Level Cost` and L the level being researched (the current level
plus 1). The game's "Technology Cost" setting chooses the curve:

| Technology Cost | Cost of level L |
|---|---|
| Low | LC × L |
| Medium (the default) | the larger of LC × L and ⌊LC × L² / 2⌋ |
| High | LC × L² |

No level costs more than 2,000,000,000. Under Medium the first two levels cost the same as
under Low (LC and 2 × LC), and from level 2 on the cost is ⌊LC × L² / 2⌋. For an invented
area with `Level Cost` 3,000:

| Level | 1 | 2 | 3 | 5 | 10 | All of 1 to 5 |
|---|---|---|---|---|---|---|
| Low | 3,000 | 6,000 | 9,000 | 15,000 | 30,000 | 45,000 |
| Medium | 3,000 | 6,000 | 13,500 | 37,500 | 150,000 | 84,000 |
| High | 3,000 | 12,000 | 27,000 | 75,000 | 300,000 | 165,000 |

So `Level Cost` sets the price of the first level, and `Maximum Level` decides how steep
the last ones are: under Medium and High, doubling the maximum makes the top level four
times as expensive.

### Spending the points

The research queue holds up to 12 areas, each at most once; a project always works on its
area's next level. Once a turn (spec 05 §1.4):

1. Areas at their maximum leave the queue.
2. Every project's share is worked out from the pool before any progress changes.
   - **Divide Pts Evenly** on: each project gets the pool divided by the number of
     projects, rounded (halves to even). Shares are not limited to what a project needs.
   - Off: down the queue, each project takes what it still needs for its next level, or
     what is left of the pool if that is less.
3. A project whose progress reaches its level's cost completes: the area gains exactly one
   level and the project leaves the queue. Progress beyond the cost is lost, and no area
   gains more than one level a turn.
4. With **Repeat Projects** on, a completed area below its maximum goes back to the end of
   the queue with no progress.

Example: a pool of 10,000 against three projects that each need 4,000 completes two of
them and puts 2,000 into the third when funded in order; divided evenly, each gets 3,333
and none completes this turn.

Removing a project from the queue throws its progress away; reordering keeps it.

### When an area can be researched

An area is in an empire's research list when all of these hold:

- it is allowed in this game ("Technology Areas Allowed");
- its racial and unique checks pass (`Racial Area`, `Unique Area`);
- the empire meets all its requirements;
- its level is below `Maximum Level`.

Levels gained in other ways (below) check only the first two, so an area can rise without
its requirements being met.

### Starting levels

When a game is created, OpenSE4 gives each empire its starting levels from the
"Technology Level for New Player" setting:

| Setting | An area starts at |
|---|---|
| Low (the default) | `Start Level` |
| Medium | the higher of `Start Level` and `Raise Level` |
| High | `Maximum Level` |

Only areas the empire can see get a starting level: allowed in the game, passing the
racial and unique checks, and with their requirements met by the starting levels of the
other areas. OpenSE4 repeats the pass until nothing changes, so a chain of areas whose
start levels meet each other's requirements all start as written. A starting level is
never above `Maximum Level`.

Then every empire is made able to colonize its home planet type: OpenSE4 takes a
component with the `Colonize Planet - X` ability for the race's planet type and raises the
areas it requires to the levels it requires. Whatever the start levels say, every race
therefore starts with the levels that colony module needs.

### Racial and unique areas

- **Racial areas.** An area with `Racial Area` n is invisible to every race that lacks a
  trait of type `Tech Area` with `Value 1` = n ([races.md](races.md)). Such a race never
  gets the area's start level, cannot research it, receive it as a gift or trade, or
  steal it, and the items that need it stay out of reach.
- **Unique areas.** An area with `Unique Area` n appears when the empire colonizes a planet
  that has `Ancient Ruins Unique` with value n and no plain `Ancient Ruins`. Then every
  area with that number gains one level at once (requirements are not checked) and stays
  open to the empire. Planets get these abilities from the stellar ability types
  ([galaxy.md](galaxy.md)); the "No Ruins" game option removes them. Without a stellar
  ability type that can roll `Ancient Ruins Unique` with value n, the area can never
  appear.
- Theft (`Research - Steal`, [intel.md](intel.md)) never takes a racial or unique area.

### Other ways to gain a level

All of these give a level only to an area that is allowed and passes the racial and unique
checks, and never past its maximum:

- **Ancient ruins.** Colonizing a planet with `Ancient Ruins` value N gives N levels: each
  time, a random area that is researchable now (all four conditions above) gains one level.
- **Gifts and trades.** A technology item gives the receiver the giver's level when it is
  higher, unless the game forbids technology gifts and trades.
- **Theft.** `Research - Steal` gives one level ([intel.md](intel.md)).
- **Analysis.** Analysing a captured ship or base raises each area that its parts and hull
  require above the empire's level by one level for each distinct higher level required.

### How other tables require tech areas

Hulls (`vehicle_sizes`), components, facilities, weapon mounts (`weapon_mounts`) and
intelligence projects carry the same requirement block as tech areas: `Number of Tech
Req`, `Tech Area Req N`, `Tech Level Req N`, the patch list `requirements`. An item is
available when the empire's level in every listed area is at least the listed level. An
item with no requirements is available from the start. When an empire gains a level, its
log names the new level and every component, facility, hull, intelligence project and
tech area the level made available.

Two things follow:

- A component upgrade picks the last component of its family, in file order, that the
  empire can use, so the requirement levels along a family should rise in file order
  ([components.md](components.md)).
- An intelligence project's requirement levels, added together, are what a completed
  counter-intelligence project compares with its own level ([intel.md](intel.md)).

The computer players' research tables name areas by `Tech Area Name`
([ai-tables.md](ai-tables.md)).

### What levels are worth elsewhere

- **Score.** Each level (capped at its area's maximum) adds 200 points to the empire's
  score, and an empire that has researched every area it can see gets a bonus.
- **Victory.** The tech victory condition compares an empire's levels with the sum of the
  maximum levels of the areas it can see (allowed, passing its racial and unique checks).
  Adding areas with high maximums makes that condition harder to reach.
- **Starting facilities.** A starting planet gets research facilities only while the empire
  has something left to research.

## Changing them with patches

### A new area and something that needs it

This adds an invented area with one requirement, then makes a component of your data set
need it. Medium starts give the area at level 1. The fields left out take their defaults:
start level 0, no racial or unique number, and the area may be left out of a game.

```toml
# data/lantern.toml
[[tech_areas.add]]
name = "Lantern Physics"
after = "<a tech area of your data set>"
[tech_areas.add.set]
"Group" = "<a group of your data set>"
"Description" = "Folding starlight into thrust."
"Maximum Level" = 6
"Level Cost" = 4000
"Raise Level" = 1
[tech_areas.add.add]
requirements = [{ "Tech Area Req" = "<a tech area of your data set>", "Tech Level Req" = 2 }]

[[components.change]]
name = "<a component of your data set>"
add = { requirements = [{ "Tech Area Req" = "Lantern Physics", "Tech Level Req" = 1 }] }
```

Use the group text of an existing area so the new area sits with its neighbours in the
Research window, or a new text for a heading of its own.

### Cheaper early levels, a longer line

Lower the first levels' price and add levels at the top. Under Medium the new tenth level
of this area costs 50 times its `Level Cost`, so a long line also becomes a slow one.

```toml
[[tech_areas.change]]
name = "<a tech area of your data set>"
set = { "Level Cost" = 2500, "Maximum Level" = 10 }
```

Remember the items that need the area: levels above the old maximum unlock nothing until
you give some item a requirement at those levels.

### An area for one race only

A racial area needs a number and a trait that carries it ([races.md](races.md)):

```toml
[[tech_areas.add]]
name = "Choir Resonance"
copy_from = "Lantern Physics"
set = { "Racial Area" = 41, "Raise Level" = 0 }

[[racial_traits.add]]
name = "Choir Minds"
[racial_traits.add.set]
"Description" = "Thinks in chords; opens the Choir Resonance research."
"General Type" = "Advantage"
"Cost" = 900
"Trait Type" = "Tech Area"
[racial_traits.add.add]
values = [{ "Value" = 41 }]
```

`copy_from` brings along the copied record's requirements too. Pick a racial number that
no area of the data set uses yet, or the trait opens those areas as well.

### Removing an area

```toml
[[tech_areas.remove]]
name = "<a tech area of your data set>"
cascade = true
```

Without `cascade`, every record that still requires the area is an error that names it.
With `cascade = true`, OpenSE4 removes every tech area, hull, component, facility, weapon
mount and intelligence project that requires the removed area, and every row of the
computer players' research tables that names it; then, in turn, everything that required
the areas removed that way. Check what is left in the files `opense4-sdk dump` writes
before you rely on it: removing an early area can take a large part of the data set with
it, including the colony modules the starting rules need.

## Things to watch

`opense4-sdk check` applies your patches to your installed game and reports these:

- `unknown tech area '<name>'`: a requirement (of any table) names an area that does not
  exist. Areas are matched in any letter case, but the spelling must be exact.
- `missing field 'Group'` (or `Maximum Level`, `Level Cost`): a new area written without
  `copy_from` needs these three fields. `'Level Cost' should be a whole number`: numbers
  must be whole numbers.
- `duplicate tech area` or `already has '<name>'`: the name is taken. Change the existing
  area instead of adding it.
- An area's requirement above the required area's maximum can never be met, so the area
  never appears. `check` does not show this one: run `opense4-sdk dump` and then
  `opense4-datacheck dump` on the folder it wrote, which warns `requires <area> level N
  but its maximum is M`.
- `removing tech_areas '<name>' leaves a reference to it in ...`: remove with `cascade`, or
  change the referring records first.
- A numbered field past the end of its list (`Tech Area Req 3` when there are two): add
  entries with `add = { requirements = [...] }` instead.

Mistakes the check does not catch:

- **Circular requirements.** Two areas that require each other never appear.
- **Racial and unique numbers with nothing to open them.** A racial area whose number no
  `Tech Area` trait carries, or a unique area whose number no `Ancient Ruins Unique` can
  roll, is never seen.
- **Start levels that skip requirements.** OpenSE4 gives an area its start level only when
  its requirements are met by the other areas' start levels. The original game gives start
  levels without that check (spec 05 §1.2), so data that relies on it starts differently
  here; give the required areas start levels too.
- **High start and the colony area.** Under High, the original sets the home planet type's
  colonization area back to level 1 (spec 05 §1.2); OpenSE4 leaves it at its maximum.
- **Level costs too low for the economy.** Research output grows during a game; compare
  your `Level Cost` and maximum with the cost table above and with the costs of the areas
  next to it in your data set. The `research_forecast` query of the SDK
  ([view.md](../../view.md)) shows the cost and turns of each level in a running game.

## More detail

- Research, tech areas, level cost, the queue and other sources of levels: spec 05 §1
  ([../../../spec/05-research-intel-diplomacy-ai-multiplayer.md](../../../spec/05-research-intel-diplomacy-ai-multiplayer.md)).
- How research output is computed: spec 02 §5
  ([../../../spec/02-empires-and-economy.md](../../../spec/02-empires-and-economy.md)).
- Ancient ruins and analysis: spec 03 §3.3 and §15
  ([../../../spec/03-vehicles-and-abilities.md](../../../spec/03-vehicles-and-abilities.md)).
- The setup options (Technology Cost, Technology Level for New Player, Technology Areas
  Allowed): spec 01 §2.2
  ([../../../spec/01-galaxy-and-setup.md](../../../spec/01-galaxy-and-setup.md)).
- Patches, the lists and removal: [../../packages-and-data.md](../../packages-and-data.md).
- Scripts read tech areas from the rules view as `tech` records (with `requirement`
  entries), and an empire's research as `research_state` and `research_entry`
  ([../../view.md](../../view.md) "The rules view").

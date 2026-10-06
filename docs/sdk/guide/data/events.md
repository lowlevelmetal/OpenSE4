# Random events

Events.txt holds the random events. Each record is one possible event: an effect type, a
severity, an amount, who hears about it, the messages they get, a picture, and an optional
delay before it strikes. The patch table is `events`, and its records are named by their
`Type`. A data set may have several records of one type (a good and a bad value change, a
small and a large disaster), so a name often stands for more than one record: see "Changing
them with patches".

Events share their effect types with intelligence projects ([intel.md](intel.md)): both
name an effect by `Type` and give it an `Effect Amount`. An event has no acting empire, so
the types that need one (thefts, espionage reports, political operations) do nothing as
events. The table under "What each type does" says what happens for each type in OpenSE4's
rules.

## The fields

| Field | What it does | Values |
|---|---|---|
| `Type` | The effect. A record of a type OpenSE4 does not know loads, as in the original, and never fires; the load warns about it. | One of the type names in "What each type does". Required. |
| `Severity` | Game Setup's *Maximum Event Severity* allows records up to this level (see "How the game chooses an event"). High and Catastrophic planet and star events also spare every empire's home planet location. | `Low`, `Medium`, `High` or `Catastrophic`; a blank reads as `Low`. Anything else is an error. |
| `Effect Amount` | The effect's parameter. Its meaning and unit depend on the type. | A whole number, positive or negative. Default 0. The load warns when it is 0 or less for a type that then does nothing (`Ship - Damage`, `Ship - Lose Movement`, `Ship - Lose Supply`, `Planet - Cargo Damage`, `Planet - Facility Damage`). |
| `Message To` | Who is told. `None`: nobody. `Owner`: the empire hit (the owner of the ship or colony). `Sector`, `System`: the empire hit and every empire with a ship, base, unit group or colony in the target's sector or system. `All`: every living empire. | One of those five; a blank reads as `Owner`. Anything else is an error. |
| `Num Messages`, `Message Title N`, `Message N` | The messages sent when the effect strikes: list `messages`. One entry is drawn at random each time, and title N goes with text N. | Text with tokens (below). A negative count is an error. |
| `Picture` | The picture shown with the messages, `Pictures/Events/<Picture>.bmp` (128×128). Write the base name without ".bmp"; a mod may bring a PNG of the same name. | A base name. |
| `Time Till Completion` | 0: the event strikes at once. N above 0: the start message goes out now, and the effect with its message follows exactly N turns later. | Turns (10 turns are one game year). Default 0; a negative number is an error. |
| `Num Start Messages`, `Start Message Title N`, `Start Message N` | The messages sent when a timed event begins: list `start_messages`. A timed event without them begins silently. | Text with tokens (below). |

The two lists are patched as entries ([packages-and-data.md](../../packages-and-data.md#lists-in-a-record)):

| List | An entry's fields | Named, for `remove`, by |
|---|---|---|
| `messages` | `Message Title`, `Message` | `Message Title` (an added entry needs one) |
| `start_messages` | `Start Message Title`, `Start Message` | `Start Message Title` (an added entry needs one) |

Game Setup's Events tab lists every record by its first message title (or its type when it
has none) with its severity, so players see what a mod's events are.

### Tokens in messages

Titles and texts may hold these placeholders, written in square brackets as shown and
matched in any letter case:

| Token | Becomes |
|---|---|
| `[%SystemName]`, `[%SectorName]` | The target's system, and its sector as "(x, y)". |
| `[%SourceEmpireName]`, `[%SourceEmperorName]` | The empire hit and its leader. An event has no acting empire, so the empire hit stands in for both source and target; `[%TargetEmpireName]` and `[%TargetEmperorName]` give the same. |
| `[%VehicleName]`, `[%VehicleSize]`, `[%DesignName]` | The ship hit, its hull and its design. |
| `[%PlanetName]`, `[%StarName]`, `[%WarpPointName]` | The planet, star or warp point hit. |
| `[%FacilityName]` | A facility destroyed by `Planet - Facility Damage`. |
| `[%TechName]`, `[%TreatyName]` | Not set by any type that fires as an event. |
| `[%ActualAmount]` | What the effect actually did, as a positive number (the column in "What each type does"). Not available in start messages. |

A token with nothing to put there is removed; text in brackets that is not a token stays as
written.

## What each type does

The **target** is drawn from the whole galaxy, never from one empire. Ship events draw among
every ship, base and unit group in space, of every empire. Planet events draw among colonies
only: an uncolonized planet or asteroid field is never hit. Unless the table says otherwise,
the amount and its unit are as given (spec 05 §2.3, §4).

### Ships

| Type | What happens | `Effect Amount` | `[%ActualAmount]` |
|---|---|---|---|
| `Ship - Damage` | The ship takes Amount damage: armour components first, then intact components at random. A unit group loses whole units. A ship with every component destroyed is lost. | Damage points. | Damage done. |
| `Ship - Lose Movement` | The ship loses Amount of its movement left this turn. Events happen after all movement, and movement is refilled before ships move again, so in practice **this does nothing**. | Movement points. | Points taken. |
| `Ship - Lose Supply` | The ship loses Amount supply, never below 0. A ship with unlimited supply is not affected. | Supply points. | Supply taken. |
| `Ship - Experience Change` | Adds Amount to the ship's experience, never below 0. | Signed. | The change. |
| `Ship - Cargo Damage` | If the ship carries anything, its whole cargo (population and units) is destroyed. | Not used. | The amount, made positive. |
| `Ship - Moved` | The ship is moved to a random sector of a random system (its own system may come up), its orders are cleared, and the system is explored for its owner. It leaves its fleet, and the rest of the fleet is disbanded. | Not used. | 1. |
| `Ship - Orders Change` | Unless the ship is mothballed, it leaves its fleet and its orders become one move to a random sector of a random system. | Not used. | 1. |
| `Ship - Rebel` | Needs an acting empire: as an event, nothing happens. | — | — |

### Planets

| Type | What happens | `Effect Amount` | `[%ActualAmount]` |
|---|---|---|---|
| `Planet - Conditions Change` | Conditions become conditions + Amount / 10, kept within 0 and 1.5 (spec 02 §2: the bands from Deadly to Optimal). | Tenths of the conditions scale: 2 is +0.2. | OpenSE4 shows the change in hundredths (20 for +0.2). |
| `Planet - Value Change` | Each of the three resource values changes by Amount. In a Finite Resources game the change is Amount × 1,000 of stock when that lies strictly between −500,000 and +500,000, else Amount. A result below 0 becomes 0; any other is kept within the Settings keys `Minimum/Maximum Planet Percent Value` (`... Resource Value` in a finite game). | Percentage points (normal game). | The average change of the three. |
| `Planet - Population Change` | With s = trunc(\|Amount\| / 5), the change is Amount + d, d uniform from −s to +s. A loss is taken from the population groups in order, first group first; a colony left with nobody dies out. A gain goes only to an existing group of the owner's race, up to the planet's maximum. | Millions. | Millions changed. |
| `Planet - Population Anger Change` | The colony's anger changes by Amount, within 0 and 100 (at most 80 on a capital). A race with `Population Emotionless` is not affected. | Whole percent of anger: positive makes it angrier. | The change. |
| `Planet - Population Riot` | Sets the colony rioting: its anger goes to its highest, 100. A capital's anger stops at 80, so a capital does not riot. A race with `Population Emotionless` is not affected. | Not used. | The rise in anger. |
| `Planet - Population Rebel` | The colony breaks away and founds a new computer-controlled empire, a copy of its former owner, if fewer than 20 empires exist (destroyed ones count). A homeworld can rebel. | Not used. | 1. |
| `Planet - Cargo Damage` | Nothing when Amount is 0 or less or the cargo is empty. Otherwise Amount ÷ the Settings key `Damage Points To Kill One Population` million people in the planet's cargo die; if fewer are aboard, all die and the rest of the damage hits the units in the cargo. | Damage points. | People killed plus units killed. |
| `Planet - Facility Damage` | min(Amount, the number of facilities) facilities are destroyed one by one, each kind chosen in proportion to how many the planet had. | Facilities. | Facilities destroyed. |
| `Planet - Plague` | Starts a plague on the colony, or raises its level (never lowers it), unless the owner's race has `No Plagues` or the owner has a `Plague Prevention - System` of at least that level in the system. Each plague level kills more people every turn (spec 02 §3). | OpenSE4 reads Amount as the plague level, at least 1; the specs do not confirm this reading. | The level. |
| `Planet - Plague Cured` | Ends the colony's plague. Nothing happens to a colony without one. | Not used. | The level cured. |
| `Planet - Destroyed` | As the Destroy Planet manipulation: the planet becomes an asteroid field of the same size that keeps its name, values and conditions, and the colony is lost. | Not used. | 1. |

### Stars and warp points

| Type | What happens | `Effect Amount` |
|---|---|---|
| `Star - Destroyed` | Any star of the galaxy. As the Destroy Star manipulation: every planet and asteroid field of the system becomes an asteroid field (their colonies are lost), and every other object but the warp points is destroyed, ships and bases included. | Not used. |
| `Warp Point - Closed` | Any warp point of the galaxy. Both ends of the link disappear. | Not used. |

### Types that never fire

These types have no list of targets as events, so a record of one of them never strikes;
when it is drawn, that turn has no event (spec 05 §4):

- the three creation types: `Warp Point - Opened`, `Star - Created` and `Planet - Created`;
- the types that act on an empire's stockpile or queues: `Intel - Delete Project`,
  `Research - Delete Project` and `Points - Change`;
- the espionage and theft types of intelligence projects (`System - Info`,
  `Planet - Info`, `Points - Steal`, `Research - Steal` and the others), and
  `Intelligence Defense`;
- any name OpenSE4 does not know, such as a misspelt type.

The political types, `Politics - Disrupt Trade` and the four other `Politics - ...` types
(`Fake Messages`, `Intercept Messages`, `Prevent Messages`, `Treaty Info`), draw an empire
as their target but have no third empire to act on, so they achieve nothing.

An effect that does not apply (a riot on an emotionless race, cargo damage on an empty hold)
sends no message.

## How the fields work together

### How the game chooses an event

The event step runs once per game turn, after every empire's end-of-turn processing and the
victory check (spec 05 §8). It deals with the hazards of the galaxy (spec 01 §7), then
strikes the timed events that are due, then rolls for a new event:

1. **No new events before 2402.0.** The first 19 turns have none.
2. **One roll for the whole galaxy.** With Game Setup's *Event Frequency* at Low, Medium or
   High, a new event happens when R[1,100] is at most the Settings key `Event Percent Chance
   Low`, `... Medium` or `... High` (OpenSE4 uses 5, 10 and 25 when the keys are missing).
   *None* never rolls. So at 10 % there is at most one new event in ten turns on average,
   whatever the number of empires.
3. **One record.** Let N be the number of records whose `Severity` is at or below the
   *Maximum Event Severity*. The record is drawn uniformly among **the first N records of
   the file**, whatever their own severity. This is a quirk of the original that OpenSE4
   keeps: the severity setting only counts records. With the records sorted from Low to
   Catastrophic, the first N are exactly the allowed ones; with another order, a lower
   setting can draw a High record near the top and never a Low one near the bottom. When
   every severity is allowed, it makes no difference.
4. **A target.** Candidates of the record's kind (above) are drawn at random, up to 1,000
   times; a candidate that fails a check is set aside:
   - it no longer exists;
   - for a High or Catastrophic planet or star event: it lies exactly at some empire's home
     planet location (the system and sector recorded when the game began, or when a rebel
     empire was founded; it never moves, and destroyed empires' locations count). So
     homeworlds are safe from those, but stars, which do not sit in a home sector, are not;
   - **luck**: for an owned target, R[1,100] must be below 100 + the owner's `Luck` trait
     values ([races.md](races.md)), so a race without the trait is passed over 1 % of the
     time and one with −50 about half the time. For a star, every empire with a colony in
     its system rolls this way, except that an empire whose total is exactly 100 (no
     `Luck`) does not roll there. Luck counts for good events as well as bad;
   - `Change Bad Event Chance - System`: a positive value V other than 100 among the
     abilities of the target's system and its objects (not facilities) sets the candidate
     aside when R[1,100] is at most V. The original reads it only from objects that belong
     to no empire (spec 05 §2.4).

   With no candidate left, there is no event that turn.
5. **Strike or wait.** With `Time Till Completion` 0, the effect applies at once and a
   message goes out. With N above 0, the start message goes out now and the event strikes
   exactly N turns later, on whoever then owns the target; if the target is gone by then,
   the event is dropped without a word.

### What sets how often an event happens

Putting the steps together, on any turn after the start:

- the chance of a new event is the frequency's percentage;
- given an event, each of the first N records is equally likely, 1 in N;
- a record whose type never fires still takes its share and makes that turn eventless, and
  a record whose target is hard to find (a planet event late in a game with few colonies,
  a ship event before anyone has ships) fails more often.

So to make one kind of event more common, add more records of it; to make all the others
more common, remove records that never fire. A data set with 40 records of which 10 can
never fire, played at 10 % with every severity allowed, gives each working record about
0.1 × 1/40 = 0.25 % a turn, before luck and missing targets; without the 10 dead records,
0.1 × 1/30, about 0.33 %.

### Messages

When a message goes out, one entry of the list is drawn at random and its tokens filled in.
It goes to the empires `Message To` names, into their log (the events category) with the
picture, and into their history. A star or a warp point has no owner, so `Owner` tells
nobody about a star or warp point event: use `System` or `All` for those.

OpenSE4 choices (spec 05 §4): `Sector` and `System` also tell the owner of the target; a
record with no messages, or with a blank title or text, still sends one, titled with its
type and naming what was hit. The original sends nothing for a record without messages
(spec 01 §10), so give every record messages, or `Message To` = `None` for a silent event.

## Changing them with patches

### Changing existing records

```toml
# data/events.toml
# Every record of this type in your Events.txt tells the whole system.
[[events.change]]
name = "Ship - Damage"
all = true
set = { "Message To" = "System" }

# One record, chosen by its position in your Events.txt.
[[events.change]]
index = 7
set = { "Effect Amount" = -15 }

# Types that never fire take a share of the draws: remove them.
[[events.remove]]
name = "Points - Change"
all = true
```

Without `all = true`, a name that several records share is an error
(`3 records of Events.txt match ...`); with it, the operation applies to each. To pick one
record of a type, use `index`, or `match` on more fields
(`match = { "Type" = "Planet - Value Change", "Effect Amount" = "-10" }`). Nothing refers
to event records, so removing one needs no `cascade`. Removing or moving records changes
which are "the first N", so keep the file in severity order.

### A new event

`add` refuses a name the file already has, and the name of an event record is its `Type`.
To add a record of a type your file already has, give the new record any unused name and
set its `Type` in `set`: the name only has to be new while the record is added.

```toml
# data/lantern-bloom.toml
[[events.add]]
name = "Lantern Bloom"                  # any unused name: Type below replaces it
before = "<the Type of your file's first record above Low>"
set = { "Type" = "Planet - Conditions Change", "Severity" = "Low", "Effect Amount" = 2, "Message To" = "Owner", "Picture" = "LanternBloom", "Time Till Completion" = 0 }
add.messages = [
  { "Message Title" = "Lantern Bloom", "Message" = "Glowing spores settle on [%PlanetName]. Its conditions improve." },
  { "Message Title" = "Lantern Bloom", "Message" = "A drift of light passes over [%PlanetName] in the [%SystemName] system." },
]
```

- `before` names a type: the record goes before the first record of that type. Naming the
  type of the first Medium record keeps the new Low record among the Low ones, so the
  severity setting still works as players expect.
- `Effect Amount` 2 raises conditions by 0.2, on a scale from 0 to 1.5; it cannot go above
  1.5.
- The picture goes in the mod's `assets/Pictures/Events/LanternBloom.bmp` (or `.png`),
  128×128.

### A timed catastrophe

```toml
# data/sable-collapse.toml
[[events.add]]
name = "Sable Collapse"
set = { "Type" = "Star - Destroyed", "Severity" = "Catastrophic", "Message To" = "System", "Picture" = "SableCollapse", "Time Till Completion" = 15 }
add.start_messages = [
  { "Start Message Title" = "Unstable Star", "Start Message" = "[%StarName] in the [%SystemName] system has begun to swell. It may not last long." },
]
add.messages = [
  { "Message Title" = "Star Destroyed", "Message" = "[%StarName] has exploded and swept the [%SystemName] system clean." },
]
```

With no `before` or `after`, the record goes at the end, where a Catastrophic record
belongs. Players who see the start message have 15 turns (a year and a half) to leave the
system. `System` reaches the empires with something there when each message goes out; a
star has no owner, so `Owner` would tell nobody. The `[%ActualAmount]` token is not
available in the start message.

### The chance itself

The chances per frequency are Settings keys ([settings.md](settings.md)):

```toml
[[settings.change]]
set = { "Event Percent Chance Low" = 3, "Event Percent Chance High" = 40 }
```

## Things to watch

- **Typos in `Type` are not reported.** `opense4-sdk check` accepts any text there; a
  misspelt type loads, never fires, and still takes its share of the draws. Copy type names
  from the tables above. A misspelt `Severity` counts as Low, and a misspelt `Message To` as
  `Owner`.
- **Selecting by name.** `name = "<a type>"` matches every record of that type: use
  `all = true`, `index` or `match`. `add` with the name of a type the file has is an error
  (`Events.txt already has '...'`): use another name and set `Type`, as above.
- **List entries need their title field.** An added `messages` entry needs `Message Title`,
  a `start_messages` entry `Start Message Title` (`an entry of messages needs 'Message
  Title'`). A string in `remove = { messages = [...] }` removes every entry with that title.
- **Keep the file in severity order**, Low first, Catastrophic last, or *Maximum Event
  Severity* will not filter as players expect.
- **`Ship - Lose Movement` has no effect** as an event, and the political types and the
  types listed under "Types that never fire" do nothing.
- **`Owner` and unowned targets.** Star and warp point events need `System` or `All` to
  reach anyone.
- **Amounts are whole numbers** (`'Effect Amount' should be a whole number, not '...'`), and
  their sign matters: a negative `Planet - Population Change` kills, a positive
  `Planet - Population Anger Change` angers.
- **Luck counts for good events too**: a race whose `Luck` makes it dodge disasters also
  misses windfalls.
- **Many timed events.** OpenSE4 keeps any number of timed events waiting. The original
  keeps five places for them and, once five have been waiting together, schedules no timed
  event again ([PARITY_GAPS.md](../../../PARITY_GAPS.md)), so a mod that relies on many long
  timers plays differently there.
- **The mod changes the game.** Events.txt is part of the data set's identity: every player
  of a network game needs the same mod.

## More detail

- [Spec 01](../../../spec/01-galaxy-and-setup.md) §2.2 (the Events tab of Game Setup),
  §10 (Events.txt), §9 (the stellar manipulations that `Planet - Destroyed`,
  `Star - Destroyed` and `Warp Point - Closed` repeat), §7 (the hazards of the same step).
- [Spec 05](../../../spec/05-research-intel-diplomacy-ai-multiplayer.md) §4 (random events:
  the roll, the record, the target, timed events), §2.3 (the effect of each type), §2.4
  (`Change Bad Intelligence Chance - System`, which its event counterpart follows), §8 (the
  turn order).
- [Spec 02](../../../spec/02-empires-and-economy.md) §2 (conditions and their bands), §3
  (population and plague levels), §4 (anger and riots).
- [packages-and-data.md](../../packages-and-data.md): "Data patches" and "Lists in a
  record".
- The rules view ([view.md](../../view.md#the-rules-view)) has no table of events. Computer
  players see the game's choices in [`game_options`](../../view.md#game_options)
  (`event_frequency`, `max_event_severity`) and the events that reached them in their log
  (the [`log_category`](../../view.md#log_category) `events`).
- Sibling chapters: [intel.md](intel.md) (the same effect types as intelligence projects),
  [races.md](races.md) (`Luck`, `Population Emotionless`, `No Plagues`),
  [abilities.md](abilities.md) (`Plague Prevention - System`,
  `Change Bad Event Chance - System`), [settings.md](settings.md) (the event chances,
  `Damage Points To Kill One Population`, the planet value limits),
  [galaxy.md](galaxy.md) (conditions and values of planets).

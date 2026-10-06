# Intelligence projects

An intelligence project is an operation an empire buys with intelligence points: sabotage
against another empire's ships and planets, espionage that brings back reports or
technology, political mischief between two other empires, or counter-intelligence that
protects the empire's own affairs. The projects live in `IntelProjects.txt`; data patches
change them through the table `intel_projects`, and each record is named by its `Name`.
Each project names one effect with its `Type`, scales it with `Effect Amount`, and carries
the messages both sides read when it runs.

## The fields

### Name and display

| Field | What it does | Values |
|---|---|---|
| `Name` | Names the project; also the title of the log entries it sends its owner. | Text, required. |
| `Description` | Shown in the Intelligence window and the Help window. | Text, optional. |
| `Group` | The heading the Intelligence window lists the project under; projects with the same text are listed together. No rule uses it. | Text, optional. |

### What it does

| Field | What it does | Values |
|---|---|---|
| `Cost` | The intelligence points the project must gather before it runs. | Whole number, required. |
| `Type` | The effect it has when it runs (see "Project types"). | A type name, required. A name OpenSE4 does not know loads, but the project fails every time it runs. |
| `Effect Amount` | How strong the effect is. What it means depends on the type. | Signed whole number, optional, default 0. |

### Messages and pictures

| Field | What it does | Values |
|---|---|---|
| `Num Source Messages`, `Source Message N` | Texts for the empire that ran the project when it succeeds; one is chosen at random. Patch list `source_messages`, entry field `Source Message`. | Text with tokens (below). |
| `Num Target Messages`, `Target Message Title N`, `Target Message N` | A title and a text for the victim; one pair is chosen at random. Patch list `target_messages`, entry fields `Target Message Title`, `Target Message`. | Text with tokens. |
| `Source Picture`, `Target Picture` | The pictures shown with the owner's and the victim's log entries: a file of `Pictures/Events/`, named without `.bmp`. | A picture name, optional. |

### Requirements

`Number of Tech Req`, `Tech Area Req N`, `Tech Level Req N`: the patch list
`requirements`, entry fields `Tech Area Req` and `Tech Level Req`. An empire may queue the
project only when its level in every listed area is at least the listed level
([techs.md](techs.md)). The levels also matter to counter-intelligence: their **sum** is
the project's level, which a finished defence project compares with its own.

OpenSE4 reads every field above. The SDK's rules view gives scripts `name`, `description`,
`group`, `cost`, `type`, `effect_amount` and `requirements` as its `intel_project`
records.

### Project types

"The target" is the empire the project is aimed at. A ship type acts on one of its ships
or bases, a planet type on one of its colonies; the player names one or leaves it to the
agents ("Any", below). Unless the table says so, `Effect Amount` is not used.

| `Type` | Effect Amount | What happens |
|---|---|---|
| `Ship - Damage` | Damage points | The ship takes that much damage in the usual order, armour first. |
| `Ship - Lose Movement` | Movement points | The ship loses that many movement points of the current turn, never below 0. They are refilled before the ship moves again, so this has no lasting effect. |
| `Ship - Lose Supply` | Supply points | The ship loses that much supply, never below 0. Nothing happens to a ship with unlimited supply. |
| `Ship - Rebel` | | The ship changes sides and joins the empire that ran the project. |
| `Ship - Experience Change` | Experience, signed | Added to the ship's crew experience, never below 0. |
| `Ship - Cargo Damage` | | The ship's whole cargo, people and units, is destroyed. |
| `Ship - Orders Change` | | The ship leaves its fleet, loses its orders and is sent to a random sector of a random system. Mothballed ships are immune. |
| `Ship - Concentrations` | | A report of how many ships the target has in each system; in OpenSE4 those systems also become explored for the thief. |
| `Ship - Construction Info` | | A report of the target's colonies that are building vehicles, the busiest first, and what they build. |
| `Ship - Locations` | | A report listing the target's ships and where they are. OpenSE4 only: the original has no such type (spec 05 §2.2). |
| `Ship Designs - Steal`, `Unit Designs - Steal` | | The thief learns one design of the target: the newest ship or base design (or unit design) that has been built at least once, that the thief does not know and that its owner can still build. It is known, not copied into the thief's designs. With none, the project fails. |
| `Planet - Conditions Change` | Tenths of the conditions scale, signed | The colony's conditions change by Amount / 10 on the scale from 0 to 1.5 (spec 02 §2), kept within it. |
| `Planet - Value Change` | Percentage points, signed | Each of the planet's three resource values changes by Amount; in a finite-resource game by Amount × 1,000 resources when that lies strictly between −500,000 and 500,000. A result below 0 becomes 0; any other result is kept within Settings' minimum and maximum planet value. |
| `Planet - Population Change` | Millions, signed | The population changes by Amount plus a random part of up to a fifth of Amount either way. A loss comes from the first population groups; a gain goes only to the owner's own people, up to the planet's maximum. |
| `Planet - Population Anger Change` | Whole percent of anger, signed | The colony's anger changes by Amount (positive is angrier), within 0 and 100 (80 on a capital). No effect on a `Population Emotionless` race. |
| `Planet - Population Rebel` | | One time in four the colony breaks away as a new computer-controlled empire; otherwise one time in four it joins the project's owner; otherwise nothing happens. Once 20 empires exist (destroyed ones count), nothing happens at all. Homeworlds can rebel too. |
| `Planet - Cargo Damage` | Damage points | Strikes the people in the colony's cargo first, then its units. Nothing when Amount is 0 or less. |
| `Planet - Facility Damage` | Facilities | Destroys that many facilities of the colony (at most all of them), chosen at random in proportion to how many of each kind it has. |
| `Planet - Info` | | A full report of the colony, its cargo included; in OpenSE4 its system also becomes explored for the thief. |
| `Planet - Locations` | | A list of the target's colonies; in OpenSE4 their systems also become explored for the thief. |
| `Points - Change` | Resources, signed | The target's stored minerals, organics and radioactives each change by Amount, never below 0. |
| `Points - Steal` | Resources | Up to Amount of each of the three moves from the target's stores to the thief's. |
| `Research - Steal` | | The thief gains one level in a chosen tech area where the target's level is higher, and which is neither racial nor unique. |
| `Research - Delete Project` | | One random entry of the target's research queue is removed, with its progress. |
| `Intel - Delete Project` | | One random entry of the target's intelligence queue is removed, with its progress; it may be a defence project. |
| `Politics - Disrupt Trade` | | If the target and a third empire trade under a treaty, the count that sets their trade percentage restarts at 0, so trade climbs again from 1 %. Without such trade, the project fails. |
| `Politics - Intercept Messages` | | A report of the latest diplomatic message between the target and a third empire from the last two turns; with none, the project fails. |
| `Politics - Fake Messages` | | A declaration of war goes from the target to a third empire in the target's name, and it takes effect: the two are at war. |
| `Politics - Prevent Messages` | | The diplomatic messages of the last two turns between the target and a third empire are deleted, so they are never answered. |
| `Politics - Treaty Info` | | A report of the treaty between the target and a third empire. |
| `System - Info` | | The highest-numbered system the target has explored and the thief has not becomes explored for the thief; with none, the project fails. |
| `Empire - Info` | | A report of the target's stores, production, planets, ships, technology levels and score. |
| `Tech Level - Info` | | A report of the target's level in every tech area. |
| `Intelligence Defense` | The defence's level | Counter-intelligence; it takes no target (see "Counter-intelligence"). |

The types of random events (`Planet - Plague`, `Star - Destroyed` and the others,
[events.md](events.md)) are accepted too, since events and intelligence projects share
their effect handlers. OpenSE4 then aims them at the target empire's colonies or ships,
or at the systems where it has colonies. Our specs do not say how the original treats
such a project, so test one before you rely on it. The original also knows the types
`Log Reports - Steal`, `Combat Logs - Steal`, `Storm`, `Nebulae` and the black hole
types (spec 05 §2.2); OpenSE4 does not, and a project of those types fails.

## How the fields work together

### Intelligence points

Intelligence points are made like research points: facilities with `Point Generation -
Intelligence`, scaled by `Planet Point Generation Modifier - Intelligence` and `System
Point Generation Modifier - Intelligence`, the colony's mood and population, and the
race's intelligence effect (the `Cunning` characteristic, the culture's `Intelligence`
and `Intelligence Production` traits, [races.md](races.md)). `Generate Points
Intelligence` adds a flat amount, and a Partnership brings a share of the partner's points.

The points go into the empire's intelligence pool, which starts the game at 0. At the
empire's intelligence step, at the end of its turn, the pool is spent on the queue and set
to 0: points the queue cannot use are lost. With the game option "Allow Intelligence
Projects" off, nothing can be queued and the step does nothing.

### The queue and the turn

An empire queues up to 12 projects. Each one that is not a defence names a target empire,
which must still be alive and in contact with the owner when the project runs. Once a
turn (spec 05 §2.1):

1. An empire in contact with no living empire loses its whole queue. Projects aimed at an
   empire that has been destroyed are removed.
2. Every project's share comes from the pool, as for research ([techs.md](techs.md)):
   with **Divide Evenly** each gets the pool divided by the number of projects, rounded;
   otherwise, down the queue, each takes what it still needs to reach its `Cost`, or what
   is left. Even shares are not limited to what a project needs, so a project's progress
   can pass its `Cost`.
3. Every project whose progress has reached its `Cost` runs, in queue order. There is no
   chance of failure: an attack is stopped only by the target's counter-intelligence. An
   attack whose target or effect is not valid any more fails, and its owner is told why.
4. A project that ran, whatever the outcome, leaves the queue; with **Repeat** on, it
   stays in its place and starts again from 0.

So `Cost` alone decides how often a project can run: an empire making 2,000 points a turn
can run a 6,000-point project every three turns, or put the points into several projects
at once.

### Choosing the target

A project may name its target (a ship, a planet, a tech area for `Research - Steal`, a
third empire for the political types) or leave it as "Any". For "Any" the agents draw at
random, up to 1,000 times, among the candidates of the right kind: the target's ships or
bases, its colonies, the tech areas, the systems, or, for the political types, the living
empires other than the owner and the target that the owner has met. Only two things
reject a candidate:

- `Change Bad Intelligence Chance - System` with a value V above 0 other than 100, on
  something in that system that belongs to no empire (a star, an uncolonized planet, a
  warp point): a roll of 1–100 at most V rejects the candidate. Facilities never count
  ([abilities.md](abilities.md)).
- For `Research - Steal`, OpenSE4 keeps, as the original does, only areas where the thief
  is already **ahead** of the target, so "Any" theft always fails. Name the area to steal.

"Any" gives no other advantage. With no candidate left, the project fails.

### Counter-intelligence

A project of type `Intelligence Defense` is the empire's guard. Its `Effect Amount` is its
level, and its strength is trunc(Amount × progress × modifier / 100), where progress is
the points it holds and the modifier is the Settings key `Intelligence Defense Modifier
Percent` (100 when the key is missing). When an attack is about to run against an
empire T:

1. The attack's strength A is its progress (at least its `Cost`).
2. T's defence projects are taken from the **bottom** of T's queue upwards. Each adds its
   strength to a running total D (at most 1,000,000,000) and loses its progress.
3. As soon as D reaches A the attack is defeated and the walk stops. The defence that
   tipped the balance keeps the part of its progress it did not need,
   trunc((D − A) / Amount / (modifier / 100)), but never more than it had.
4. If all of T's defences together stay below A, the attack goes ahead, and they have
   still lost their progress.

A defeated attack is used up like any other: the defender learns who attacked, and the
attacker that its project was stopped.

Example with invented numbers: an attack of `Cost` 4,000 runs with 4,000 progress. The
defender's queue ends with a level-2 defence holding 1,500 and, above it, a level-1
defence holding 2,500. The bottom one gives 3,000 and is emptied; the next brings D to
5,500, which defeats the attack, and keeps 1,500 of its 2,500 points.

**A defence that completes** (its progress reaches its `Cost`) runs too: it looks through
the other living empires in random order and deletes the first project it finds aimed at
its owner whose level (the sum of its requirement levels) is at most the defence's
`Effect Amount`. Only one project goes; if none qualifies, it achieves nothing. Then it
leaves the queue, unless Repeat is on.

So `Effect Amount` does two things for a defence: it multiplies the strength each point
buys, and it decides which enemy projects a finished defence can delete. Attack projects
that need high or many requirement levels are safe from low-level defences.

**Timing.** Empires take their intelligence steps one after another, in empire order. A
defender numbered before the attacker has already added this turn's points to its
defences when the attack runs; one numbered after has not.

### Messages and tokens

- **Success.** The owner gets one of the source messages; the victim gets one of the
  target messages under its title. One time in five the victim's message adds a line
  naming the owner as the suspect. Espionage findings come to the owner in a separate log
  entry named after the project with "Report".
- **Failure.** The owner gets an entry that says why; the victim learns nothing.
- **Defeat.** For a defence project, the roles turn round: the defender reads the defence
  project's source message and the attacker its target message. The same holds when a
  finished defence deletes a project.
- Without messages in the record, OpenSE4 sends short texts of its own.

Messages may hold tokens that are replaced when they are sent: `[%SystemName]`,
`[%SectorName]`, `[%SourceEmpireName]`, `[%SourceEmperorName]`, `[%TargetEmpireName]`,
`[%TargetEmperorName]`, `[%OtherEmpireName]`, `[%OtherEmperorName]` (the third empire),
`[%VehicleName]`, `[%VehicleSize]`, `[%PlanetName]`, `[%DesignName]`, `[%TechName]`,
`[%TreatyName]`, `[%FacilityName]`, `[%StarName]`, `[%WarpPointName]` and `[%ActualAmount]`
(how much the effect did, without its sign). Tokens are read in any letter case; one with
nothing to say is removed, and an unknown token is left as written. Events use the same
tokens ([events.md](events.md)).

### Computer players and diplomacy

- A computer player picks as its target the empire in contact with it, below
  Non-Aggression, that it is angriest at. It then queues projects drawn at random from all
  those it has the technology for, defences included, so a project you add is used by
  computer players as soon as they can.
- It drops projects aimed at empires it now holds Non-Aggression or better with.
- When the victim's message names the suspect, a computer-controlled victim grows angrier
  with that empire (`Intelligence Against Us`, [ai-tables.md](ai-tables.md)).
- An empire that accepts a demand to stop espionage or sabotage removes every project it
  has aimed at the empire that made the demand.

## Changing them with patches

### A new sabotage project

An invented project written out in full, with its own picture
(`assets/Pictures/Events/MirefieldBlight.png`, 128×128):

```toml
# data/mirefield.toml
[[intel_projects.add]]
name = "Mirefield Blight"
after = "<an intelligence project of your data set>"
[intel_projects.add.set]
"Description" = "Agents seed a slow rot in a colony's fields."
"Group" = "<a group of your data set>"
"Cost" = 6000
"Type" = "Planet - Conditions Change"
"Effect Amount" = -2
"Source Picture" = "MirefieldBlight"
"Target Picture" = "MirefieldBlight"
[intel_projects.add.add]
source_messages = [{ "Source Message" = "Our agents have spoiled the fields of [%PlanetName] in the [%SystemName] system." }]
target_messages = [{ "Target Message Title" = "Blight on [%PlanetName]", "Target Message" = "A rot spreads through the fields of [%PlanetName]. Conditions there have worsened." }]
requirements = [{ "Tech Area Req" = "<a tech area of your data set>", "Tech Level Req" = 3 }]
```

`Effect Amount` −2 lowers the conditions by 0.2 on the 0 to 1.5 scale. Its level, for
counter-intelligence, is 3.

### Tuning an existing project

```toml
[[intel_projects.change]]
name = "<an intelligence project of your data set>"
set = { "Cost" = 9000, "Effect Amount" = 3 }
remove = { source_messages = [1] }
add = { target_messages = [{ "Target Message Title" = "Saboteurs at [%PlanetName]", "Target Message" = "Agents of the [%SourceEmpireName] were seen near [%PlanetName]." }] }
```

A message that names the source in every case gives the attack away even when the
one-in-five suspect line does not; that only changes what the player reads, not the
computer players' anger, which follows the suspect line.

### A stronger defence

```toml
[[intel_projects.add]]
name = "Lantern Watch"
copy_from = "<an Intelligence Defense project of your data set>"
set = { "Description" = "A standing guard of loyal agents.", "Cost" = 12000, "Effect Amount" = 4 }
add = { requirements = [{ "Tech Area Req" = "<a tech area of your data set>", "Tech Level Req" = 5 }] }
```

Every point this defence holds counts four times, and when it completes it can delete an
enemy project whose requirement levels add up to 4 or less. The copy keeps the requirements
of the record it was copied from, and the new one is added to them.

### Removing a project

```toml
[[intel_projects.remove]]
name = "<an intelligence project of your data set>"
```

No other table names intelligence projects, so a removal needs no `cascade`.

## Things to watch

`opense4-sdk check` reports:

- `unknown tech area '<name>'` in a requirement, and `missing field 'Cost'` or
  `missing field 'Type'` for a new project without `copy_from`.
- `'Cost' should be a whole number` and the same for `Effect Amount`.
- `an entry of target_messages needs 'Target Message Title'`: an entry of a list must have
  its first field. Use `""` for a message without a title of its own; the log then shows
  a plain "Intelligence Report" title.
- A numbered message past the end of its list (`Source Message 3` when there are two):
  add entries with `add = { source_messages = [...] }` instead.

Not checked:

- **The type's spelling.** A `Type` OpenSE4 does not know loads without a message, and the
  project fails each time it runs. Copy the type names from the table above; letter case
  and spacing do not matter.
- **A `Cost` of 0 or less** makes the project run every turn it is queued, for nothing.
- **Defence levels.** A defence with `Effect Amount` 0 adds no strength and deletes only
  projects without requirements.
- **"Any" theft.** `Research - Steal` aimed at "Any" always fails (above); computer players
  queue it that way, so for them a theft project is wasted points.
- **No lasting movement loss.** `Ship - Lose Movement` takes points that are refilled
  before the ship moves again; use `Ship - Orders Change` or `Ship - Damage` to slow an
  enemy.
- **Missing pictures.** A picture name with no file in `Pictures/Events/` shows no
  picture.

## More detail

- Intelligence points, the queue, targets, every effect type and counter-intelligence:
  spec 05 §2
  ([../../../spec/05-research-intel-diplomacy-ai-multiplayer.md](../../../spec/05-research-intel-diplomacy-ai-multiplayer.md)).
  How the points are produced: spec 02 §5
  ([../../../spec/02-empires-and-economy.md](../../../spec/02-empires-and-economy.md)).
- The computer players' intelligence and their anger: spec 05 §7.3 and §7.5.
- Patches, lists and removal: [../../packages-and-data.md](../../packages-and-data.md).
- Scripts read projects from the rules view as `intel_project` records, and an empire's
  intelligence as `intel_state` and `intel_entry` ([../../view.md](../../view.md) "The
  rules view").

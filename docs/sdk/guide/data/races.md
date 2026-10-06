# Races, racial traits, cultures and happiness

A race is put together from several places. Its **race files** in a race style folder
(`Pictures/Races/<Style>/`) give its names, texts, culture, happiness type, home planet
and three ready-made builds of characteristics and traits, plus its computer players'
tables. Its fifteen **characteristics** are percentages whose costs `Settings.txt` sets.
Three data tables hold the rest, each record named by its `Name`:

| Data file | Patch table | What a record is |
|---|---|---|
| `RacialTraits.txt` | `racial_traits` | An advanced trait a race may buy with racial points. |
| `Cultures.txt` | `cultures` | A set of ten percentages; every race has one culture. |
| `Happiness.txt` | `happiness` | A happiness type: how a race's colonies grow angry or calm. |

The race files are patched through the computer players' table `ai.general` with
`files = "race:<Style>"` ([ai-tables.md](ai-tables.md)), or replaced and added as game
files ([../../packages-and-data.md](../../packages-and-data.md) "Game files").

## The fields

### The race files

A race style is a folder under `Pictures/Races/` (races players can choose) or
`Pictures/RaceNeutral/` (neutral empires). OpenSE4 lists a race for every such folder that
holds a file whose name ends in `_AI_General.txt`, folders in name order, the playable
races before the neutral ones. A folder without that file is not a race.

| File in the folder | What it is |
|---|---|
| `<Style>_AI_General.txt` | The race preset (below). |
| `<Style>_AI_Settings.txt` | The computer players' settings for the race. Its `Personality Group` also steers which races random computer players get ([ai-tables.md](ai-tables.md)). |
| `<Style>_AI_<Table>.txt` | Any other of the computer players' twelve tables (`Anger`, `Politics`, `Research`, `DesignCreation` and so on). A table the folder lacks comes from `Ai/Default_AI_<Table>.txt`. |
| `<Style>_Race_Portrait`, `<Style>_Main`, `<Style>_Pop_Portrait`, `<Style>_Pop_Mini` | The race's portrait (128×128), its flags and empire colour swatch (100×20), and its population pictures (36×36, 20×20). |
| `<Style>_Mini_<Bitmap>`, `<Style>_Portrait_<Bitmap>`, `<Style>_Shields`, `<Style>_BigExplosion` | Its pictures of ships and unit groups, named after each hull's bitmap names ([hulls.md](hulls.md)) and the group icons, and its combat animations. |

A picture the folder lacks is looked for in `Pictures/RaceGeneric/` as `Generic_...`
([../../packages-and-data.md](../../packages-and-data.md) "Pictures, sounds, music and
fonts").

### The race preset: `<Style>_AI_General.txt`

The file is a classic data file (a header, `*BEGIN*`, `Key := Value` lines, `*END*`, read
as Latin-1 text). OpenSE4 reads its first record:

| Field | What it does | Values |
|---|---|---|
| `Name` | The race's name. | Text; the folder name when it is empty. |
| `Description` | The short text beside the race's portrait in Quick Start. | Text. |
| `Empire Name`, `Empire Type` | The empire's default name and government, shown together as "Name Type". | Text. |
| `Emperor Name`, `Emperor Title` | The leader's default name and title. | Text. |
| `Biological Description`, `Society Description`, `General History Description` | The three texts of Empire Setup's Description page. | Text. |
| `Demeanor` | Shown with the race. No rule uses it. | Text. |
| `Culture` | The race's culture: the `Name` of a record of `Cultures.txt`. | Text. |
| `Happiness Type` | Its happiness type: the `Name` of a record of `Happiness.txt`. | Text. |
| `Planet Type` | The surface of its home planet, and the type it can colonize from the start. | `Rock`, `Ice` or `Gas Giant`; `Rock` when empty. |
| `Atmosphere` | The gas it breathes, which is also its home planet's. Planets with other air hold only domed colonies. | `Oxygen`, `Methane`, `Hydrogen` or `Carbon Dioxide`; `Oxygen` when empty. |
| `Design Name File` | A file of `Dsgnname/` with the names its computer players give their designs ([settings.md](settings.md)). | A file name, such as `Corvane.txt`. |
| `Race Opt T Num Characteristics`, `Race Opt T Characteristic N Type`, `Race Opt T Characteristic N Amount` | Build T's characteristics (T is 1, 2 or 3). Patch lists `characteristics_1` to `characteristics_3`. | A characteristic's name (below) and its percentage. |
| `Race Opt T Num Advanced Traits`, `Race Opt T Adv Trait N` | Build T's traits. Patch lists `advanced_traits_1` to `advanced_traits_3`. | Trait names. |

**The three builds.** Each `Race Opt` build is a complete set of characteristics and
traits; a characteristic it does not list stays at 100. They are meant for the three
racial point settings above None: build 1 for 2,000 points, 2 for 3,000 and 3 for 5,000.

- A random computer player uses build 1, 2 or 3 for a game of 2,000, 3,000 or 5,000 racial
  points, and none (every characteristic at 100, no traits) for 0. It applies the
  characteristics in order, each only while the points spent so far are below the budget,
  and puts back to 100 one that takes the total over. Then it adds each trait whose cost
  still fits, skipping those that do not.
- Empire Setup's Preset Build list offers each build with its cost; Quick Start gives the
  player build 1.
- The culture, happiness type, planet type and atmosphere come with every build.

### Racial characteristics

Every race has fifteen characteristics, whole percentages where 100 is average. Each one
feeds one racial effect with d = value − 100 (see "Racial effects" below):

| Characteristic | What d changes |
|---|---|
| `Physical Strength` | Ground combat strength, +d % (with the culture's `Ground Combat`). |
| `Intelligence` | Research output, +d %. |
| `Cunning` | Intelligence output, +d %. It does nothing else to intelligence operations. |
| `Environmental Resistance` | Population growth on every planet: (d + `Tollerance` traits) / 5 percentage points a year, truncated towards zero. |
| `Reproduction` | Population growth, +d percentage points a year on top of Settings' `Starting Percent Reproduction`. |
| `Happiness` | Calm: every colony's anger falls by E / 5 tenths of a percent each turn, truncated towards zero, E being the happiness effect. A negative E makes colonies angrier. |
| `Aggressiveness` | +d to the to-hit chance of its pieces' shots in space combat. |
| `Defensiveness` | −d to the to-hit chance of shots at its ships, bases and unit groups. |
| `Political Savvy` | Income from trade treaties, +d %. |
| `Mining Aptitude`, `Farming Aptitude`, `Refining Aptitude` | Mineral, organics and radioactives output, +d %. |
| `Construction Aptitude` | Space yard rate, +d %. |
| `Repair Aptitude` | Repair points, +d %. |
| `Maintenance Aptitude` | The maintenance rate falls by d percentage points (never below 5 %). |

`Settings.txt` sets the range and the price of each, by its name
([settings.md](settings.md)); the spelling `Threshhold` is the file's own:

| Settings key | What it does |
|---|---|
| `Characteristic <Name> Min Pct`, `Characteristic <Name> Max Pct` | The range the Empire Setup window's buttons allow. Nothing else checks them: a value from a race file outside the range is costed and used as written. |
| `Characteristic <Name> Pct Cost` | c: racial points per percent within the threshold. |
| `Characteristic <Name> Threshold` | T: how far from 100 the plain price applies. |
| `Characteristic <Name> Threshhold Pct Cost Pos` | P: racial points per percent beyond +T. |
| `Characteristic <Name> Threshhold Pct Cost Neg` | N: racial points given back per percent beyond −T. |

### `RacialTraits.txt`

| Field | What it does | Values |
|---|---|---|
| `Name` | Names the trait. Race files and other traits refer to it by this name. | Text, required. |
| `Description` | Shown on Empire Setup's Advanced Traits page and in reports. | Text, optional. |
| `Pic Num` | Read, but OpenSE4 shows no picture for a trait. | Whole number, optional. |
| `General Type` | A label. No rule uses it. | `Advantage`, `Disadvantage` or `Neither`; any other value is an error. Optional. |
| `Cost` | Its price in racial points. A negative cost gives points back, and nothing limits it. | Whole number, required. |
| `Trait Type` | What the trait does (below). | A trait type name, required. |
| `Value 1`, `Value 2`, ... | The trait's values. Every trait type uses `Value 1` only; further values are kept but unused. Patch list `values`, entry field `Value`. | Whole numbers, numbered from 1. |
| `Required Trait 1`, ... | Traits a race must also have to take this one. Patch list `required_traits`, entry field `Required Trait`. | Trait names, or `None` (skipped). |
| `Restricted Trait 1`, ... | Traits a race with this one cannot have. Patch list `restricted_traits`, entry field `Restricted Trait`. | Trait names, or `None` (skipped). |

The three numbered lists have no count field: OpenSE4 reads each from 1 until a number is
missing.

**Trait types.** All values are `Value 1`; the effects add to the same racial effects as
the characteristics and the culture (see "Racial effects"):

| `Trait Type` | What `Value 1` does |
|---|---|
| `Reproduction` | + V percentage points of population growth a year. |
| `Mineral Production`, `Organics Production`, `Radioactives Production` | + V % output of that resource. |
| `Production` | + V % output of all three resources. |
| `Research Production`, `Intelligence Production` | + V % research or intelligence output. |
| `SY Rate` | + V % space yard rate, on planets and ships. |
| `Planetary SY Rate` | + V % space yard rate on planets only. |
| `Maintenance Cost` | The maintenance rate falls by V percentage points. |
| `Population Happiness` | Adds V to the happiness effect (calmer colonies). |
| `Tollerance` (spelled so) | Adds V to the environmental resistance effect (growth). |
| `Trade` | + V % income from trade treaties. |
| `Repair` | + V % repair points. |
| `Supply Cost` | Supply used by moving ships changes by V % (negative saves supply). |
| `Vehicle Speed` | + V movement points for every vehicle that can move. |
| `Luck` | A random event strikes the race's colonies, ships or empire only when a roll of 1–100 is below 100 + V (with no trait, 99 times in 100). A negative value protects from events, good and bad alike; at −100 or below the roll is skipped and every event strikes. |
| `Planet Storage Space` | + V % to its colonies' maximum population, facility slots and cargo space. |
| `Mineral Storage`, `Organics Storage`, `Radioactives Storage` | + V % to its colonies' storage of that resource. |
| `No Plagues` | A plague is cured the next time it would strike, with no loss. |
| `No Spaceports` | Its systems deliver their output without a spaceport, and its starting planets get none. |
| `Galaxy Seen` | In OpenSE4 the empire has every system explored and knows every warp point link. |
| `Population Emotionless` | Its colonies have no mood (see "How mood changes"). |
| `Tech Area` | Opens the tech areas whose `Racial Area` equals V ([techs.md](techs.md)). |
| `Space Combat`, `Ground Combat`, `Ship Attack`, `Ship Defense` | Meant to add to combat like the culture and characteristics (spec 04 §7, §13). OpenSE4 does not apply these trait types yet. |
| `Troops Bonus`, `Fighter Bonus`, `Ship Bonus` | Accepted, but no rule in OpenSE4 uses them, and our specs do not describe their effect. |

Values add up when a race has several traits of one type.

### `Cultures.txt`

Every field after `Name` and `Description` is a signed whole-number percentage, and all ten
are required. A positive value always helps the race.

| Field | What it does |
|---|---|
| `Name` | Names the culture. Race files refer to it by this name. |
| `Description` | Shown on Empire Setup's Culture page. Optional. |
| `Production` | + % output of minerals, organics and radioactives. |
| `Research` | + % research output. |
| `Intelligence` | + % intelligence output. |
| `Trade` | + % income from trade treaties. |
| `Space Combat` | Added to the to-hit chance of its pieces' shots and taken off the chance of shots at its ships, bases and unit groups, in percentage points. |
| `Ground Combat` | + % ground combat strength, attacking and defending. |
| `Happiness` | Adds to the happiness effect: every colony is calmer by E / 5 tenths each turn, truncated. |
| `Maintenance` | The maintenance rate falls by this many percentage points. |
| `SY Rate` | + % space yard rate. |
| `Repair` | + % repair points. |

### `Happiness.txt`

A happiness type says how much each kind of event changes a colony's anger. Anger is a
whole percent from 0 to 100; the values in this file are in **tenths of a percent**, and a
positive value makes colonies angrier.

| Field | What it does | Values |
|---|---|---|
| `Name` | Names the type. Race files refer to it by this name. | Text, required. |
| `Description` | Shown when choosing the type. | Text, optional. |
| `Max Positive Anger Change` | The most a colony's anger can rise in one turn. | Tenths, required. Divided by 10 and truncated: 35 allows 3 %. |
| `Max Negative Anger Change` | The most it can fall in one turn. | Tenths, required, written as a negative number (OpenSE4 uses its size either way). |
| Every other field | A trigger: its name says what happened, its value how much anger it adds. | A whole number of tenths; anything else is an error. |

The triggers OpenSE4 fires, by how far they reach:

| Reach | Triggers |
|---|---|
| Every colony of the empire | `Homeworld Lost`, `Any Planet Lost`, `Any Planet Colonized`, `Any Our Planet Captured`, `Any Enemy Planet Captured`, `Any Ship Lost`, `Any Ship Constructed`, and on a new treaty `New Treaty War`, `New Treaty Non Intercourse`, `New Treaty None`, `New Treaty Non Aggression`, `New Treaty Subjugated (Sub)`, `New Treaty Protectorate (Sub)`, `New Treaty Subjugated (Dom)`, `New Treaty Protectorate (Dom)`, `New Treaty Trade`, `New Treaty Trade and Research`, `New Treaty Military Alliance`, `New Treaty Partnership` |
| Every colony in the system | `Battle in System - Win`, `Battle in System - Loss`, `Battle in System - Stalemate`, `Ship Lost in System`, `Enemy Ship in System`, `Our Ship in System` |
| The colony itself | `Battle in Sector - Win`, `Battle in Sector - Loss`, `Battle in Sector - Stalemate`, `Enemy Ship in Sector`, `Our Ship in Sector`, `Enemy Troops on Planet`, `Our Troops on Planet`, `1M Population Killed`, `Planet Plagued`, `Ship Constructed`, `Facility Constructed` |
| Drift | `Natural Decrease`, `Natural Decrease for Other Races` |

Every starting planet counts as a homeworld for `Homeworld Lost`. `Any Ship Lost` and
`Ship Lost in System` count ships destroyed by damage (in battle, by mines, by events),
not ships abandoned for unpaid maintenance. A field with any other name loads, but
nothing triggers it.

## How the fields work together

### Racial effects

Each effect is one number in percentage points: (the characteristic − 100), plus the
culture's field, plus `Value 1` of every trait of the matching type.

| Effect | Characteristic | Culture field | Trait type |
|---|---|---|---|
| Mineral, organics, radioactives output | `Mining Aptitude`, `Farming Aptitude`, `Refining Aptitude` | `Production` | `Mineral Production`, `Organics Production`, `Radioactives Production`; `Production` for all three |
| Research | `Intelligence` | `Research` | `Research Production` |
| Intelligence points | `Cunning` | `Intelligence` | `Intelligence Production` |
| Space yard rate | `Construction Aptitude` | `SY Rate` | `SY Rate` (planets also `Planetary SY Rate`) |
| Maintenance | `Maintenance Aptitude` | `Maintenance` | `Maintenance Cost` |
| Happiness | `Happiness` | `Happiness` | `Population Happiness` |
| Trade | `Political Savvy` | `Trade` | `Trade` |
| Repair | `Repair Aptitude` | `Repair` | `Repair` |
| Ground combat | `Physical Strength` | `Ground Combat` | (not applied yet) |
| Space combat | `Aggressiveness`, `Defensiveness` | `Space Combat` | (not applied yet) |
| Reproduction | `Reproduction` | — | `Reproduction` |
| Environmental resistance | `Environmental Resistance` | — | `Tollerance` |

How each effect is used:

- **Output.** A colony's output of a resource is its facilities' base, scaled by planet
  value and planet modifiers, then multiplied by (100 + effect + (mood % − 100) +
  (population % − 100)) %, at least 0, truncated (spec 02 §5.1). The effect therefore adds
  to the mood and population terms; it does not multiply them.
- **Space yards.** The rate of a planet with a space yard and of a ship's yard is scaled by
  (100 + effect) %; a planet without a space yard gets no racial bonus (spec 02 §6.2).
- **Maintenance.** The rate is `Starting Percent Maint Cost` − effect, at least 5 %.
- **Happiness.** Every colony is calmer by effect / 5 tenths each turn, truncated towards
  zero: an effect of +20 gives 4 tenths, one of −12 makes colonies 2 tenths angrier.
- **Growth.** The yearly growth rate of a colony is max(0, `Starting Percent Reproduction`
  + reproduction effect), plus 5, 2, 0, −2 or −5 for a Jubilant, Happy, Indifferent,
  Unhappy or Angry colony, plus the conditions term, plus the environmental resistance
  effect / 5 (truncated), plus the best `Modify Reproduction - System` in the system; then
  kept between 0 and 100 (spec 02 §3).
- **Combat.** Attack (offence) adds the culture's `Space Combat` and Aggressiveness − 100;
  defence adds `Space Combat` and Defensiveness − 100, as percentage points of to-hit
  chance. Ground combat strength grows by (culture `Ground Combat` + Physical Strength −
  100) %.

### Racial points

A race's cost is the sum of its characteristics' costs and its traits' costs. With
d = value − 100 and the Settings values c, T, P and N above:

| d | Cost |
|---|---|
| −T ≤ d ≤ T (or T below 1) | c × d (negative d gives points back) |
| d > T | c × T + P × (d − T) |
| d < −T | −c × T − N × (−d − T) |

With invented settings c = 20, T = 10, P = 60 and N = 5: a value of 105 costs 100 points,
125 costs 20 × 10 + 60 × 15 = 1,100, and 80 gives back 20 × 10 + 5 × 10 = 250.

A game offers 0, 2,000 (the default), 3,000 or 5,000 racial points. Empire Setup refuses a
race whose total is above the game's points. Price your builds with your data set's
settings so that build 1 costs at most 2,000, build 2 at most 3,000 and build 3 at most
5,000; the Preset Build list shows what each costs.

### Required and restricted traits

In Empire Setup a trait can be added only when the race has every trait in its required
list and none in its restricted list, and no trait the race has lists it as restricted (the
restriction works both ways). Removing a trait also removes the traits that required it.
The builds of a race file and the random computer races take their traits as written,
without these checks, so keep them consistent.

### How mood changes

Once a turn, at the empire's end-of-turn processing, OpenSE4 works out each colony's
change in tenths (spec 02 §4). v[x] is the race's happiness type's value for trigger x.

1. **Empire-wide part**, the same for every colony: count × v for each empire-wide event
   since the last update (planets lost, colonized and captured, ships lost and built, new
   treaties), minus the happiness effect / 5 (truncated).
2. **Drift.** If the owner's race is less than half of the colony's population, add
   v[`Natural Decrease for Other Races`]. Otherwise an Angry, Unhappy or Rioting colony
   adds v[`Natural Decrease`], a Happy or Jubilant one subtracts it, and an Indifferent one
   does neither. With a negative value, colonies drift towards Indifferent from both sides.
3. **Events in the colony's sector and system:** battles there (a battle in the colony's
   own sector counts as both a sector and a system battle), ships lost in the system,
   people killed, plague, ships and facilities built at the colony.
4. **Ships present**, counted one by one: ships and bases that are not cloaked or
   mothballed. Those of the owner and of empires at Non-Aggression or better are "ours",
   the rest "enemy". If any enemy ship is in the colony's sector, add v[`Enemy Ship in
   Sector`] for each one there; otherwise v[`Enemy Ship in System`] for each one in the
   system. The same for ours.
5. **Troops.** v[`Enemy Troops on Planet`] once while another empire's landed troops still
   fight for the planet; v[`Our Troops on Planet`] for each troop unit in the colony's
   cargo.
6. **Plague.** v[`Planet Plagued`] every turn while the colony is plagued.

The total is divided by 10 and truncated to whole percent, then the colony's
`Planet - Change Population Happiness` abilities are added. The change is limited by the
two maximum fields, and anger is kept between 0 and 100 (at most 80 on a capital). Later
in the turn, the best `Change Population Happiness - System` in the system lowers the anger
of each of the owner's colonies there, outside those limits.

Anger gives the mood:

| Anger | 90 or more | 60–89 | 45–59 | 30–44 | 15–29 | below 15 |
|---|---|---|---|---|---|---|
| Mood | Rioting | Angry | Unhappy | Indifferent | Happy | Jubilant |

The Settings `Mood <band> Modifier` of the colony's mood enters its output percentage
(above), and the mood shifts growth. A rioting colony produces nothing, builds nothing
and does not grow. A new colony starts at anger 25. A race with `Population Emotionless`
skips all of this: its colonies keep their anger until something else changes it, which
then sets it to 35 (Indifferent); they never riot, and their growth has no mood term.

Example with invented values: a type with `Natural Decrease` −15, `Our Ship in System` −4
and `Max Negative Anger Change` −30, for a race with no happiness effect. An Angry colony
with three of its own ships elsewhere in its system, and nothing else happening, changes
by −15 − 12 = −27 tenths, so its anger falls by 2 % this turn; the 7 tenths left over are
lost.

## Changing them with patches

### A culture and a happiness type

A culture written out in full (all ten percentages are required), and a happiness type
copied from one of your data set with a few triggers changed. A patch can set only trigger
names that a `Happiness.txt` of the install or of a mod already uses.

```toml
# data/corvane-ways.toml
[[cultures.add]]
name = "Tidewright"
[cultures.add.set]
"Description" = "Builders first, soldiers last."
"Production" = 5
"Research" = 0
"Intelligence" = -5
"Trade" = 5
"Space Combat" = -5
"Ground Combat" = -10
"Happiness" = 10
"Maintenance" = 0
"SY Rate" = 15
"Repair" = 5

[[happiness.add]]
name = "Stoneheart"
copy_from = "<a happiness type of your data set>"
set = { "Description" = "Slow to anger, slow to forgive.", "Max Positive Anger Change" = 20, "Max Negative Anger Change" = -20, "New Treaty War" = 0 }
```

### Traits that depend on each other

Two invented traits: the second requires the first and excludes a trait of your data set.

```toml
[[racial_traits.add]]
name = "Deep Roots"
[racial_traits.add.set]
"Description" = "Colonies hold far more people."
"General Type" = "Advantage"
"Cost" = 800
"Trait Type" = "Planet Storage Space"
[racial_traits.add.add]
values = [{ "Value" = 20 }]

[[racial_traits.add]]
name = "Hollow Worlds"
copy_from = "Deep Roots"
set = { "Description" = "Builds cities inside its planets.", "Cost" = 1200, "Trait Type" = "Mineral Storage" }
add = { required_traits = [{ "Required Trait" = "Deep Roots" }], restricted_traits = [{ "Restricted Trait" = "<a trait of your data set>" }] }

[[racial_traits.change]]
name = "<a trait of your data set>"
remove = { values = [1] }
add = { values = [{ "Value" = 15 }] }
```

The copy keeps `Deep Roots`' value list, so `Hollow Worlds` adds 20 % storage. The last
operation replaces the value of an existing trait.

### A new race

A new race is a folder of game files under `data/` and its pictures under `assets/`:

```text
corvane/
  mod.toml
  data/corvane-ways.toml                              # the culture, happiness type and traits above
  data/Pictures/Races/Corvane/Corvane_AI_General.txt  # the race preset
  data/Pictures/Races/Corvane/Corvane_AI_Settings.txt # its computer players' settings and Personality Group
  data/Dsgnname/Corvane.txt                           # its design names, one per line
  assets/Pictures/Races/Corvane/Corvane_Race_Portrait.png
  assets/Pictures/Races/Corvane/Corvane_Main.png
  assets/Pictures/Races/Corvane/Corvane_Pop_Portrait.png
  assets/Pictures/Races/Corvane/Corvane_Pop_Mini.png
```

```text
Race preset of the Corvane, written for the corvane mod.
*BEGIN*
Name                                := Corvane
Description                         := Patient builders from a cold world of ice.
Empire Name                         := Corvane
Empire Type                         := Concord
Emperor Name                        := Ilsa Varrow
Emperor Title                       := First Speaker
Biological Description              := Tall and slow, with skin like frost on slate.
Society Description                 := Ruled by a council of the oldest builders.
General History Description         := They waited out three ice ages before leaving home.
Demeanor                            := Watchful
Culture                             := Tidewright
Happiness Type                      := Stoneheart
Planet Type                         := Ice
Atmosphere                          := Methane
Design Name File                    := Corvane.txt
Race Opt 1 Num Characteristics      := 2
Race Opt 1 Characteristic 1 Type    := Construction Aptitude
Race Opt 1 Characteristic 1 Amount  := 115
Race Opt 1 Characteristic 2 Type    := Reproduction
Race Opt 1 Characteristic 2 Amount  := 90
Race Opt 1 Num Advanced Traits      := 1
Race Opt 1 Adv Trait 1              := Deep Roots
Race Opt 2 Num Characteristics      := 2
Race Opt 2 Characteristic 1 Type    := Construction Aptitude
Race Opt 2 Characteristic 1 Amount  := 120
Race Opt 2 Characteristic 2 Type    := Mining Aptitude
Race Opt 2 Characteristic 2 Amount  := 110
Race Opt 2 Num Advanced Traits      := 1
Race Opt 2 Adv Trait 1              := Deep Roots
Race Opt 3 Num Characteristics      := 2
Race Opt 3 Characteristic 1 Type    := Construction Aptitude
Race Opt 3 Characteristic 1 Amount  := 125
Race Opt 3 Characteristic 2 Type    := Mining Aptitude
Race Opt 3 Characteristic 2 Amount  := 115
Race Opt 3 Num Advanced Traits      := 2
Race Opt 3 Adv Trait 1              := Deep Roots
Race Opt 3 Adv Trait 2              := Hollow Worlds
*END*
```

Check each build's cost in the Preset Build list against 2,000, 3,000 and 5,000 points and
adjust. The race then appears in Empire Setup and among the races random computer players
draw from. Its ships use `RaceGeneric` pictures until you add `Corvane_Mini_<Bitmap>` and
`Corvane_Portrait_<Bitmap>` for the hulls' bitmap names. Quick Start offers only the
styles listed in `Settings.txt` (when it names no playable race, it offers them all): add yours
by raising the count by one and naming the style in the new slot.

```toml
[[settings.change]]
set = { "Number of Quick Start Styles" = 9, "Quick Start Style 9" = "Corvane" }  # one more than your data set has
```

To change a race that is already there, patch its preset:

```toml
[[ai.general.change]]
files = "race:<a race style of your data set>"
set = { "Happiness Type" = "Stoneheart" }
add = { advanced_traits_2 = [{ "Race Opt 2 Adv Trait" = "Deep Roots" }] }
```

### Removing a trait

```toml
[[racial_traits.remove]]
name = "<a trait of your data set>"
cascade = true
```

With `cascade = true` the trait also leaves the required and restricted lists of other
traits and the builds of every race file. A culture or happiness type that a race file
names cannot be removed that way: change the race's `Culture` or `Happiness Type` first.

## Things to watch

`opense4-sdk check` reports:

- `unknown required trait '<name>'`, `unknown restricted trait '<name>'`: a list names a
  trait that does not exist.
- `'General Type' must be Advantage, Disadvantage or Neither`.
- `missing field 'SY Rate'` (or any other percentage): a culture needs all ten.
- `'<field>' should be a whole number`: every trigger of a happiness type is a number.
- `has no field '<name>' (no file of the table uses it; check the spelling)`: a happiness
  patch set a trigger name that no `Happiness.txt` has. Only the names in the table above
  have an effect.
- `removing racial_traits '<name>' leaves a reference to it in ...`, and for cultures and
  happiness types `cascade cannot change that: change that record first`.
- Game files under `assets/` are ignored, with a warning: race files belong in `data/`.

Not checked, and silent:

- **A culture or happiness type a race file names that does not exist.** The race gets the
  first record of `Cultures.txt` or `Happiness.txt` instead. Copy the names exactly.
- **Unknown trait or characteristic names in a build** are skipped, and an unknown
  `Trait Type` loads and does nothing.
- **An empty build.** A build with no characteristics and no traits is dropped, and the
  later builds move up: an empty build 1 makes build 2 the one used for 2,000 points.
- **A race in no personality group.** Random computer players first pick the personality
  group furthest below its share (Settings' `Random Player Personality Groups` and
  `Random Player Personality Group N Percent`) and draw from its races. A race whose
  `<Style>_AI_Settings.txt` gives no `Personality Group` among those groups is drawn only
  when no group qualifies or a group's races are all in use, so give a new race a group
  ([ai-tables.md](ai-tables.md)).
- **Small happiness values.** A colony's total is truncated to whole percent each turn, so
  changes that add up to less than 10 tenths do nothing. A `Max ... Anger Change` below 10
  allows no change at all in that direction.
- **Construction triggers.** `Ship Constructed`, `Facility Constructed` and `Any Ship
  Constructed` are logged after that turn's mood update and are not kept in saved games,
  so they count only when the game is not saved and loaded before the next update. Play by
  e-mail and local or hotseat simultaneous games never count them; a turn-based game on
  one machine and a network host do (spec 02 §4).
- **New treaties always count.** The original has a game option that turns the `New
  Treaty` triggers off; OpenSE4 has no such option.
- **Combat trait types.** `Space Combat`, `Ground Combat`, `Ship Attack`, `Ship Defense`,
  `Troops Bonus`, `Fighter Bonus` and `Ship Bonus` traits change nothing in OpenSE4 today;
  use the culture's fields and the characteristics for combat races.

## More detail

- Racial traits, cultures, happiness types and race presets: spec 02 §1.6–§1.10; population
  and growth §3; happiness §4; output §5; maintenance §7; characteristics, costs and racial
  effects §8; Empire Setup §9
  ([../../../spec/02-empires-and-economy.md](../../../spec/02-empires-and-economy.md)).
- The setup options and racial points: spec 01 §2.2
  ([../../../spec/01-galaxy-and-setup.md](../../../spec/01-galaxy-and-setup.md)).
- Random computer races, the computer players' files and trade: spec 05 §7.1, §7.2, §3.3
  ([../../../spec/05-research-intel-diplomacy-ai-multiplayer.md](../../../spec/05-research-intel-diplomacy-ai-multiplayer.md)).
- Combat modifiers: spec 04 §7 and §13
  ([../../../spec/04-combat.md](../../../spec/04-combat.md)).
- Game files, patches and removal: [../../packages-and-data.md](../../packages-and-data.md).
- Scripts read these from the rules view as `race_preset` (with `race_tier` and
  `race_characteristic`), `racial_trait`, `culture` and `happiness_model` (with
  `happiness_trigger`) records, and an empire's race as `race` and `characteristics`
  ([../../view.md](../../view.md) "The rules view").

# Spec 02: Empires and economy

**Scope.** This spec covers the economy side of the game:
- empires and races;
- colonies and population;
- resources and income;
- facilities and construction queues;
- maintenance and happiness;
- racial characteristics, traits and cultures;
- empire setup, ministers, and the planet management screens.

It describes the behaviour of SE4 Deluxe (v1.95) so the engine can reproduce it. It does
not copy SE4 content. The engine reads the user's own `Data/*.txt` files at runtime (§1).

**Provenance tags**
- **[D]** stated in a data file or its header.
- **[M]** stated in the manual.
- **[H]** stated in the game's release notes (`History.txt`, `DataFileHistory.txt`). These
  are newer than the manual and win when the two conflict.
- **[I]** inferred or proposed by us. Check it in the running game (§13).
- **(confirmed: binary)** checked in the executable. This wins over every other source.

**Units**
- Population is counted in millions (M).
- Cargo is counted in kT.
- Happiness is stored per colony as *anger*, a whole percent from 0 to 100. Happiness.txt
  values are in tenths of a percent; they are summed in tenths each turn and then
  converted (§4) (confirmed: binary).
- One turn is 0.1 year, and the game starts at 2400.0. Data text that says "per year"
  means per 10 turns.

**Arithmetic** (confirmed: binary)
- Amounts are integers. A percentage `p` is applied as `x × (p / 100)` in floating point,
  and each rule below says whether the result is **rounded** (to nearest, ties to even) or
  **truncated** (towards zero). Order matters; follow each rule's steps exactly.
- The original computes `p / 100` and the product in the x87's extended precision, so a
  product that is mathematically whole can come out a hair below it and truncate one lower
  (for example 100 × 53 % gives 52, 90 × 130 % gives 116). An exact engine reproduces
  this: round `p / 100` to a 64-bit mantissa, multiply, round again to 64 bits, then
  truncate or round.
- The precision is fixed (confirmed: binary). At start-up, in every new thread and after
  every handled exception the game sets the x87 to 64-bit-mantissa precision with
  round-to-nearest. The only other changes are temporary and keep that precision: a
  truncation switches to round-towards-zero and back, and converting text to a number
  masks the exceptions and restores them. The game loads no Direct3D, which is what
  usually switches a program to single precision. It only loads DirectSound and
  DirectShow, for sound.
- Plain integer divisions truncate towards zero.

---

## 1. Data files

### 1.1 Common format [D]

- Each file starts with a free-text header, followed by records between `*BEGIN*` and
  `*END*`. Some files repeat `*BEGIN*` or wrap it in `=====` lines.
- Fields are `Key := Value` lines with CRLF line endings. Keys are padded with spaces or
  tabs, so trim both the key and the value.
- A record starts at `Name`. Events start at `Type`, and Settings is a single record.
- Numbered fields (`Ability 3 Type`, …) follow a count field. Read exactly that many and
  ignore a trailing `… Type := None`.
- Booleans may be spelled `TRUE`, `True` or `true`.
- Files in a mod directory override single files (`Path.txt`) [H].
- Newer Settings keys have documented defaults when they are missing [H]. The loader
  needs per-key defaults.

### 1.2 Settings.txt economy keys [D]

This is one flat record. The stock value is shown in parentheses. `X` stands for the three
resources: Minerals, Organics and Radioactives.

**Resources and storage**
- `Minimum Empire X Generation` (200): **not a floor.** When an empire's colonies deliver
  exactly 0 of that resource in a turn, it receives this amount instead; any positive
  delivery is kept as is (§5.6) (confirmed: binary).
- `Minimum Empire Point Storage` (50000): base storage cap for each resource, to which
  `Resource Storage` facilities add (§5.6) (confirmed: binary).
- `Scrap Facility/Unit/Ship Percent Returned` (30): scrap refunds; a higher `Resource
  Reclamation` in the sector replaces it (§6.6) (confirmed: binary).
- `UnMothball Ship Percent Cost` (20): cost to reactivate a mothballed ship.
- `Upgrade Facility Cost Percent` (50): upgrade pricing, per facility (§6.6) (confirmed:
  binary).

**Trade and tariffs**
- `Maximum Trade Percentage` (20): trade cap.
- `Treaty Subjugated/Protectorate Resource Percentage` (40/20): tariff rates.

**Planet value ranges**
- `Planet Value Low/High Percent` gives the random % value of ordinary planets.
  `… Resources` gives the same range as absolute amounts, for finite-resource games.
- `Asteroids Value …` does the same for asteroid fields.
- `Plr Planet Value Low/Medium/High Percent|Resources` sets the homeworld value for each
  "Home Planet Value" setup choice.
- `Maximum/Minimum Planet Percent Value` (250/0) and `… Resource Value` clamp values after
  every change; a value that would go negative becomes 0 (confirmed: binary).

**Maintenance and population**
- `Empire Starting Percent Maint Cost` (25): base maintenance, in % of cost per turn (§7)
  (confirmed: binary).
- `Maintenance Cost Amt Per Dead` (20000): sets how many vehicles unpaid maintenance
  destroys: `unpaid ÷ this + 1` (§7) (confirmed: binary).
- `Empire Starting Percent Reproduction` (10): base population growth, in % per year (§3)
  (confirmed: binary).
- `Reproduction Check Frequency` (1): growth happens only on turns whose number is a
  multiple of N. The amount grown is **not** scaled by N (§3) (confirmed: binary).
- `Population Mass` (5): kT of cargo per 1M of population [H].
- `Damage Points To Kill One Population` (10).
- `Defending Units Per Population` (20), `Population Defender Attack Strength` (10) and
  `Population Defender Hit Points` (30): militia in ground combat.
- `Automatic Colonization Population` (0): the population a colony is created with when a
  planet is colonized (confirmed: binary).
- `Maximum Population For Abandon Planet Order` (50).
- `Population Required to Operate One Facility` (50): **never used** by the game; it is
  read from the file and ignored (confirmed: binary).
- `Planet Value Percent Loss After Owner Death` (10): when a colony's population dies out,
  each of the planet's three values drops by this many points (normal game) or by this
  percentage of the stock, truncated (finite game) (confirmed: binary).

**Construction**
- `Empire Base Planet X Usage Rate` (2000): queue rate of a colony that has no space yard
  (§6.2) (confirmed: binary).
- `Maximum Emergency Build Turns` (10) (§6.4) (confirmed: binary).
- `Construction Queue Emergency Build Rate Percent` (150) and `… Slow Build Rate Percent`
  (25). These replace the manual's "double" and "half" [H] (confirmed: binary).

**Tables**
- `Number Of Population Modifiers` gives the row count. Each row has `Pop Modifier N
  Population Amount`, `… Production Modifier Percent` and `… SY Rate Modifier Percent`
  (§5.2) (confirmed: binary).
- `Characteristic <Name> Max Pct|Min Pct|Pct Cost|Threshold|Threshhold Pct Cost Pos|Neg`
  (§8.1). Match the misspelling `Threshhold` exactly.
- `Mood Riot|Angry|Unhappy|Indifferent|Happy|Jubilant Modifier` (0/80/90/100/110/120): the
  production % for each mood band (confirmed: binary).

**Other**
- `Home System Percentage Value With No Spaceport` (25): the share of its home system's
  output an empire still receives there without a spaceport (§5.5) (confirmed: binary).
- `Remote Mining Decreases Asteroid Value` (True): each turn a remote miner works an
  object, that resource's value drops by 1 point (normal games only) (confirmed: binary).
- `Number of Quick Start Styles` and `Quick Start Style N`.

### 1.3 PlanetSize.txt [D]

There is one record for each physical class and size.
- `Physical Type` is `Planet` or `Asteroids`.
- `Stellar Size` runs from Tiny to Huge.
- `Max Facilities`, `Max Population` (M) and `Max Cargo Spaces` (kT) are the normal
  capacities. The three `… Domed` fields hold the domed capacities.
- `Constructed` is True for artificial worlds. For those, `Special Ability ID` is the
  target of `Create Constructed Planet` Val1.

Capacities roughly double with each size step. Domed capacities are about a fifth of the
normal facility slots and a tenth of the normal population and cargo. Constructed worlds
are the exception and keep half. Asteroid rows also have capacities, but the game never
uses them: an asteroid field can never be colonized (§2) (confirmed: binary).

### 1.4 Facility.txt [D]

**Fields**
- `Name`, `Description`, `Pic Num`, `Restrictions` (unused).
- `Facility Group`: a UI category.
- `Facility Family`: 1–64000 [H]. All levels of one facility share it.
- `Roman Numeral`: the level. 0 means no level.
- `Cost Minerals/Organics/Radioactives`.
- `Number of Tech Req`, then N pairs of `Tech Area Req n` and `Tech Level Req n`. All pairs
  must be met.
- `Number of Abilities`, then N sets of `Ability n Type/Descr/Val 1/Val 2`.

**Rules**
- A facility fills one slot and can never be moved [M].
- Its abilities work while it is functional.
- The description tells players whether its effect stacks. Code must not parse that text;
  it applies the stacking class from §1.5 instead.

### 1.5 Ability types used by facilities and planets [D]

**Scopes and stacking** (confirmed: binary)
- *Colony*: the colony's own facilities. A facility built N times counts N times. Facility
  abilities do not depend on population; only output does (§5.1).
- *Sys*: every object the owner has in that system: its colonies' facilities **and** the
  abilities of its ships and units there.
- *Sector*: the same, limited to one sector.
- **Sum** adds every value. **Best** takes the largest value and ignores negative values:
  with no positive value the result is 0. So a negative "best" modifier never applies.

| Ability type | Val1 / Val2 meaning (confirmed: binary unless marked) | Stacking |
|---|---|---|
| `Resource Generation - Minerals/Organics/Radioactives` | Base output per turn, §5.1. | Sum per colony |
| `Point Generation - Research/Intelligence` | Base points per turn. Planet value is not applied. | Sum per colony |
| `Resource Gen Modifier Planet - X` | +% applied to that colony's output as its own multiplication (§5.1). | Best per colony |
| `Resource Gen Modifier System - X` | +% applied to the empire's total for the whole system (§5.1). | Best over the owner's colonies in sys |
| `Planet/System Point Generation Modifier - X` | The same two, for research or intelligence. | As above |
| `Solar Resource Generation - X` | Val1 × stars in the system, added after the colony's own modifiers and not limited by a finite stock; the system modifier still applies, and a rioting, blockaded or empty colony makes none (§5.1). | Sum per colony |
| `Generate Points <Minerals…Intelligence>` | Flat points per turn from every object the empire owns (planets and their facilities, ships). No modifiers and no spaceport rule [H]. | Sum |
| `Spaceport` | Delivers the system's output (§5.5). | Any facility on any of the owner's colonies in the system |
| `Palace` | Defined, but no stock item uses it, and no game rule reads it: it does **not** mark the homeworld (§2). Only the Facility Construction minister looks at it, counting it among the facilities a system needs only once (confirmed: binary). | — |
| `Resource Storage - Mineral/Organics/Radioactives` | Adds to the empire's storage cap (§5.6). | Sum |
| `Cargo Storage` | Extra cargo kT on the planet, before the storage trait (§2). | Sum (planet and facilities) |
| `Space Yard` | Val1 is the resource (1 = minerals, 2 = organics, 3 = radioactives), Val2 is the rate. A yard has three entries. Enables building ships. | Sum of Val2 per resource; one yard facility per planet [H] |
| `Component Repair` | Components repaired per turn. | — |
| `Supply Generation` | Unlimited resupply in the sector. | — |
| `Planet - Change Minerals/Organics/Radioactives Value` | Every 10th turn (turn number divisible by 10): normal game, the value gains Val1 points; finite game, the stock gains Val1 % of itself, truncated. Then clamped. | Sum per colony |
| `Planet - Change Conditions` | Every 10th turn, if the sum is positive: conditions × (1 + Val1/100). A negative sum does nothing. | Sum per colony |
| `Planet Value Change - System` | Every 10th turn, each of the owner's colonies in sys: normal game +Val1 points; finite game stock × (100 + Val1)/100, truncated. | Best per sys |
| `Planet Conditions Change - System` | Every 10th turn, each of the owner's colonies in sys: conditions × (100 + Val1)/100. | Best per sys |
| `Planet - Change Atmosphere` | Once the planet has spent **more than** Val1 turns with an atmosphere other than the one the majority race breathes, it changes to that atmosphere (§2). | Best per colony |
| `Planet - Change Population Happiness` | Added to the colony's anger change, in whole percent, each turn: **positive makes it angrier** (§4). | Sum per colony |
| `Change Population Happiness - System` | Each turn every colony of the owner in sys loses Val1 points of anger (§4). | Best per sys |
| `Modify Reproduction - System` | +% per year to reproduction (§3). | Best per sys |
| `Change Population - System` | +M per turn to each colony in sys, split across its races in proportion to their size and rounded, up to the colony's maximum [H]. Negative values do nothing. | Best per sys |
| `Plague Prevention - System` | Each turn cures every colony of the owner in sys whose plague level is at most Val1 (§3). | Best per sys |
| `Resource Conversion` | % of material lost when converting (§5.6). | Best on the converting planet or ship itself |
| `Resource Reclamation` | % of cost refunded when anything is scrapped in the sector; used when higher than the Settings %. | Best per sector |
| `Reduced Maintenance Cost - System` | % cut to maintenance of the owner's vehicles in sys (§7). Negative values never raise it. | Best per sys |
| `Modified Maintenance Cost` (hull or component) | Maintenance × (100 + Val1)/100, so −50 halves it (§7). | Sum per design |
| `Change Bad Event/Intelligence Chance - System` | ±% chance (not checked here). | Best per sys |

Combat, sensor and shield facility abilities are covered in other specs.

### 1.6 RacialTraits.txt [D]

**Fields**
- `Name`, `Description`, `Pic Num`.
- `General Type`: `Advantage`, `Disadvantage` or `Neither`; any other value is a data-file
  error, and a missing key is fine. All stock traits are `Advantage`. The game never uses
  the value otherwise (confirmed: binary).
- `Cost`: in racial points, a signed number. A negative cost is allowed and gives points
  back: the setup's points left, its final check and the computer's random trait picks all
  add the costs with their sign, and nothing clamps them (confirmed: binary).
- `Trait Type` and `Value 1`/`Value 2`.
- `Required Trait 1..3` and `Restricted Trait 1..3`: trait names or `None`. These are
  prerequisites and exclusions.

**`Trait Type` values** (confirmed: binary). The engine accepts 33 type names; stock data
uses only some of them. The meaning of Val1 follows each type.
- Flags: `No Plagues` (§3), `No Spaceports` (§5.5), `Galaxy Seen`, `Population
  Emotionless` (§4), `Tech Area` (unlocks the TechArea records whose `Racial Area` equals
  Val1).
- `Supply Cost`, `Luck`, `Vehicle Speed`, `Troops Bonus`, `Fighter Bonus`, `Ship Bonus`,
  `Ship Attack`, `Ship Defense`, `Space Combat`, `Ground Combat`, `Repair`, `Trade`:
  covered in other specs.
- **Racial effects.** These add Val1 percentage points to the same term as a
  characteristic or culture field (§8.2): `Reproduction`, `Mineral Production`,
  `Organics Production`, `Radioactives Production`, `Production` (all three resources),
  `Research Production`, `Intelligence Production`, `SY Rate`, `Maintenance Cost`,
  `Population Happiness`, `Tollerance` (spelled so; environmental resistance).
- `Planet Storage Space`: +Val1 % to a colony's maximum population, facility slots and
  cargo space, truncated (§2). It does not raise resource storage.
- `Mineral Storage`, `Organics Storage`, `Radioactives Storage`: +Val1 % to the colony's
  `Resource Storage` of that resource (§5.6).
- `Planetary SY Rate`: % bonus to *planet* space yards only (§6.2).

Two examples: an advantage costing 1000 points that removes the spaceport requirement,
and a 1500-point advantage that unlocks a racial tech tree.

### 1.7 Cultures.txt [D]

**Fields:** `Name`, `Description`, then ten signed percentages:
- `Production` (all three resources), `Research`, `Intelligence`, `Trade`;
- `Space Combat`, `Ground Combat`, `Happiness`, `Maintenance`, `SY Rate`, `Repair`.

A positive value always helps the empire. For `Maintenance` it lowers costs; for
`Happiness` it makes populations happier (confirmed: binary). Each field adds to the same
term as the matching characteristic and trait type (§8.2).

Examples: one warlike culture gives about +10 % to combat, with small penalties to
economy and research. A neutral culture has every field at 0.

### 1.8 Happiness.txt [D]

**Mood bands.** The game maps a colony's anger (a whole percent) to a mood with these
limits (confirmed: binary). They differ from the file header, which puts rioting at 75 %;
the game's limits win.

| Anger % | Mood |
|---|---|
| ≥ 90 | Rioting |
| 60–89 | Angry |
| 45–59 | Unhappy |
| 30–44 | Indifferent |
| 15–29 | Happy |
| < 15 | Jubilant |

The header also lists output multipliers. They differ from the Settings `Mood …` keys, and
the game uses the Settings keys (confirmed: binary).

**Records.** Each record is one racial *happiness type*, chosen at setup. Every value is a
change to anger in tenths of a percent: positive means angrier.
- `Max Positive/Negative Anger Change`: the clamp on one planet's change per turn, in
  tenths; the game divides it by 10 (truncating) and clamps the whole-percent change (§4)
  (confirmed: binary).
- Empire-wide events:
  - `Homeworld Lost`, `Any Planet Lost`, `Any Planet Colonized`;
  - `Any Our Planet Captured`, `Any Enemy Planet Captured`;
  - `Any Ship Lost`, `Any Ship Constructed`;
  - `New Treaty <War | Non Intercourse | None | Non Aggression | Subjugated (Sub) |
    Protectorate (Sub) | Subjugated (Dom) | Protectorate (Dom) | Trade | Trade and
    Research | Military Alliance | Partnership>`.
- System events: `Battle in System - Win/Loss/Stalemate`, `Enemy Ship in System`,
  `Our Ship in System`, `Ship Lost in System`.
- Location events:
  - `Battle in Sector - Win/Loss/Stalemate`, `Enemy/Our Ship in Sector`;
  - `Enemy/Our Troops on Planet`, `1M Population Killed`, `Planet Plagued`;
  - `Ship Constructed`, `Facility Constructed`.
- Drift: `Natural Decrease` and `Natural Decrease for Other Races` (§4 gives when each
  applies and with which sign).

How the game uses each record value is in §4 (confirmed: binary). In short:
- The five planet events at the top are empire-wide. So are the `New Treaty …` values,
  unless a game option switches treaty effects off.
- `Any Ship Lost` is counted from every "ship lost in system" event and `Any Ship
  Constructed` from every ship built; both are empire-wide.
- Battles are logged per sector: the colonies of that system get the `… in System` value,
  and a colony in that very sector also gets the `… in Sector` value.
- `Ship Lost in System` hits every colony of the system. `1M Population Killed`, `Ship
  Constructed`, `Facility Constructed` and `Planet Plagued` hit the colony in that sector.
- Ship presence, troops and plague are recomputed every turn from the map (§4).

Examples: a peaceful type gets 10.0 % angrier when war is declared, while a bloodthirsty
type gets 10.0 % calmer.

### 1.9 Name lists and small files [D]

- **Name lists.** `EmpireNames.txt`, `EmpireTypes.txt`, `EmperorNames.txt`,
  `EmperorTitles.txt` and `Demeanors.txt` have no header and one entry per line. They fill
  the setup combo boxes and AI names; the player may type custom text instead. Demeanor
  is flavour only.
- **Design names.** `Dsgnname/*.TXT` holds design-name lists, one name per line. Treat the
  files as Latin-1.
- **Colony types.** `DefaultColonyTypes.txt` holds records with only a `Name`. They are
  each player's starting colony types (§10).
- **Repair order.** `RepairPriorities.txt` has no header and one component group per line.
  It sets the default repair order, which each empire can reorder in the Repair
  Priorities window.

### 1.10 Empire files and race presets

**`Empires/*.emp`** files are binary and look scrambled. Do not decode them: clean-room
rules forbid it and we do not need them. Our engine defines its own TOML empire format.

A saved empire holds:
- everything in §9;
- experience, from which the race age is derived (§9);
- its strategies, designs and minister settings [H].

When an empire is loaded, it is checked against the current game's rules, such as the
racial point budget [M]. Designs that are not valid in the game are dropped, and the rest
are recomputed [H].

**Race presets.** Stock race presets are readable text in
`Pictures/Races/<Race>/<Race>_AI_General.txt`, with these keys:
- `Empire Name/Type` and `Emperor Name/Title`;
- the three description texts;
- `Demeanor`, `Culture`, `Happiness Type`, `Planet Type`, `Atmosphere`,
  `Design Name File`;
- three tiers, `Race Opt 1..3`. Each tier has `Num Characteristics`, then pairs of
  `Characteristic n Type`/`Amount`, then `Num Advanced Traits` and `Adv Trait n`.

Priced with §8.1, most tiers add up to exactly 2000, 3000 and 5000 points; five tiers
price higher because of the threshold rule (confirmed: binary).
Trim values, since some have trailing spaces.

---

## 2. Planets and colonies

**Planet attributes**
- Name: defaults to "<system> <roman numeral>", and the owner can rename it.
- Type: Rock, Ice or Gas Giant.
- Size.
- Atmosphere: None, Methane, Oxygen, Hydrogen or CO₂. A Gas Giant never has None.
- Conditions: a real number from 0 to 1.5, shown as a band (see **Conditions** below)
  (confirmed: binary).
- Three resource values, a description, and special flags such as ruins.

**Colony attributes**
- Owner and colony type.
- Population per race.
- Anger: one whole percent (0–100) for the whole colony, not per race (confirmed: binary).
- A *capital* flag (see **Colonization**).
- Facilities (each type with a count) and cargo.
- Queue state and the minister flag.
- Plague level, an atmosphere-change counter, and orders (in simultaneous games).

**Colonization** [M/H]
- A ship with `Colonize Planet - <Type>` settles a planet of that type. The ship is broken
  up, and its cargo and population land on the planet.
- A colony with zero population is created, but it cannot build anything. If an empire's
  last colony has no population, the empire dies.
- Game options can limit colonization to breathable atmospheres, or to the home planet
  type. Otherwise tech decides which types are allowed. The colonization tech levels at
  game start depend on the home planet type [D].
- An asteroid field can never be colonized (confirmed: binary). Colonize orders refuse
  it, and the Planets window, the colony-ship target list, the computer players and
  starting-planet placement all skip it. A planet that is destroyed becomes a new
  asteroid field without a colony.
- A new colony starts at anger 25 (Happy). Recolonizing an abandoned planet does not
  inherit the old anger (confirmed: binary).
- The colony starts with `Automatic Colonization Population` of the colonizer's race
  (confirmed: binary); the ship's cargo and population are then added to it [M].
- Every planet an empire starts the game with (the homeworld and any extra starting
  planets, §9), and a planet that rebels and founds a new empire, are **capitals**: their
  anger can never rise above 80, so a capital never riots (confirmed: binary). Ordinary
  colonies are not capitals.
- **No single homeworld marker** (confirmed: binary). Three separate things play that
  part:
  - the capital flag above;
  - the colony type, which decides `Homeworld Lost` (see below);
  - the empire's recorded **home system**, which the no-spaceport rule uses (§5.5). It is
    set when the game is created (for a rebel empire, when it is founded: its capital's
    system) and never moves afterwards, even if the homeworld is lost or captured.

  The `Palace` ability plays no part (§1.5).
- When a colony's population dies out (for example to plague), the colony is removed.
  Each planet value then drops by `Planet Value Percent Loss After Owner Death` (§1.2),
  and the owner gets `Homeworld Lost` if the colony's type is `Homeworld` (the first of
  the game's built-in colony types, §10), else `Any Planet Lost` (confirmed: binary).

**Domes.** A colony is domed if any race on it cannot breathe the atmosphere. A domed
colony uses the `… Domed` capacities [M/D]. Edge cases:
- If a non-breathing race arrives (by transfer or capture), capacities shrink. Nothing
  is removed for that (confirmed: binary):
  - Facilities above the new slot count stay, but no new ones can be built (§6.5).
  - Population above the new maximum stays. The colony has no room left, so it neither
    grows nor takes in population until it drops below the maximum.
  - Cargo above the new capacity stays until the planet next takes damage (in combat, for
    example) or loses population to plague. Then cargo is removed until it fits: first any
    population held as cargo, 1M at a time, then units one at a time from the first stack
    in cargo. A colony whose cargo capacity shrinks for another reason (a lost `Cargo
    Storage` facility, for example) is treated the same way.
- Atmosphere converters (confirmed: binary): each turn the colony has a converter (best
  `Planet - Change Atmosphere` Val1 above 0) and the planet's atmosphere differs from the
  one breathed by the majority race (the owner's race when it has no population), a
  counter goes up by 1, to at most 200. When it exceeds that Val1, the counter resets and
  the planet takes that atmosphere, which can remove the dome. So a converter with Val1 =
  N needs N + 1 turns. On other turns (no converter, or the atmosphere already right) the
  counter keeps its value; it is not reset, so the count resumes later.

**Capacities** (confirmed: binary)
- Facility slots and maximum population come from PlanetSize, using the normal or domed
  column.
- Cargo is PlanetSize cargo plus the planet's own and its facilities' `Cargo Storage`.
- The `Planet Storage Space` trait multiplies all three by (100 + Val1)/100, truncated.

**Value**
- **Normal game:** each resource has a percentage that multiplies output and never runs
  out.
  - The starting value is random within the Settings range.
  - Homeworlds use the setup choice instead.
  - Changes are clamped to the Min/Max percent keys.
- **Finite Resources game:** the value is the absolute stock left.
  - Production draws it down, and a resource at 0 produces nothing.
  - Solar output ignores the stock [H].
- Value and conditions changes from facilities run every 10th turn (§1.5) (confirmed:
  binary).

**Conditions** (confirmed: binary)
- Conditions are a real number from 0 to 1.5. The bands are:

  | Conditions | Band | Reproduction |
  |---|---|---|
  | < 0.3 | Deadly | −20 |
  | 0.3 to < 0.5 | Harsh | −5 |
  | 0.5 to < 1.0 | Unpleasant | −2 |
  | 1.0 to < 1.3 | Mild | 0 |
  | 1.3 to < 1.5 | Good | +2 |
  | 1.5 | Optimal | +5 |

- Facilities change conditions by multiplication (§1.5). Conditions never go above 1.5
  (the owner is told when they reach it), and a change that would leave exactly 0 gives
  0.1.
- Conditions affect **reproduction only** (§3). They do not change anger.
- How the stored double meets the band edges is our choice for now (inferred, §13 Q51).
- Events can also change conditions (see the events spec).

**Blockade** (confirmed: binary)
- A colony is blockaded while its sector holds a ship (bases included) or planet that
  belongs to an empire whose treaty with the owner is below Non-Aggression (war,
  non-intercourse, no treaty, or not yet met), that the owner can currently see, and that
  is not mothballed. Cloaked ships the owner cannot see therefore do not blockade.
- Fighters, satellites, mines and drones never blockade, and neither do troops on the
  planet.
- A blockaded colony produces nothing that turn: no resources, research, intelligence or
  solar output. The output is lost.

---

## 3. Population

Population is stored per race as an ordered list: a race that arrives is appended at the
end, and population added to a race already there joins its entry, which holds at most
60,000M (confirmed: binary). The planet's maximum caps the total across all races for
growth and for arrivals; population already above it is not removed (§2) (confirmed:
binary).

**Growth rate** (confirmed: binary). One rate, in % per year, for the whole colony, taken
from the owner's race:
- Empire part: `max(0, Starting Percent Reproduction + E_repro)`, where `E_repro` is the
  racial effect of §8.2 (Reproduction − 100, plus `Reproduction` traits). The release
  notes call the characteristic a "true percent" [H].
- Mood: Angry −5, Unhappy −2, Indifferent 0, Happy +2, Jubilant +5. Rioting adds nothing
  (the colony does not grow anyway). An Emotionless race gets no mood term.
- Conditions: the band value from §2 (−20 to +5).
- Environmental resistance: `trunc(E_env / 5)`, where `E_env` is Environmental Resistance
  − 100 plus `Tollerance` traits. It applies on every planet, whatever the band.
- Plus the best `Modify Reproduction - System` in the system.
- The rate is 0 if the colony is rioting, has no population, or is plagued. Otherwise it
  is clamped to 0–100.
- Check: a Happy homeworld with Unpleasant conditions shows the base 10 %, and a Happy one
  with Good conditions shows 14 % (07-observations).

**Growth step** (confirmed: binary)
- Growth happens only on turns whose number is a multiple of `Reproduction Check
  Frequency`; the amount is not scaled by it.
- The free room is `max population − population`. For each race in list order:
  `g = round(P_race × ((R / 100) / 10))`, computed in floating point and rounded to
  nearest; if `R > 0` and `g = 0`, then `g = 1`; `g` is capped at the room left, added to
  the race, and taken off the room. So earlier races fill the room first.
- Population never shrinks by growth: the rate is never negative.

**Replicants** (confirmed: binary): the best `Change Population - System` Val1 `P` in the
system is added each turn to every colony of the owner there. Negative values do nothing.
- `T` is the colony's population before anything is added.
- For each race in list order, its share is `round(P × q)`, rounded half to even, where
  `q = P_race / T` is computed first and stored as a 64-bit double. The product itself is
  in extended precision.
- The share is then capped by the room left at that moment (maximum − current total;
  nothing once the colony is full) and added to the race. Earlier races therefore fill the
  room first. A colony without population has no races and gets nothing.

**Moving and removing population** [M/H]
- Each 1M of population takes `Population Mass` kT of cargo.
- You cannot load away a colony's last 1M.
- Abandon Planet is allowed only at or below the Settings maximum. The player chooses
  whether to scrap the facilities (with a refund) or leave them for a later owner.

**Death and defence**
- Bombardment kills 1M for every `Damage Points To Kill One Population` points of damage.
  - Facilities can be lost as the population falls [M].
  - Each 1M killed fires the matching anger event.
- In ground combat, each `Defending Units Per Population` M of population fields one
  militia unit with the attack and HP from Settings.
  - Physical Strength and culture Ground Combat modify it.
  - Survivors of a conquered population become the captor's [M].

**Plague** has levels (confirmed: binary unless marked).
- A plague is started by events or intelligence operations. Setting a plague only ever
  raises the level, and it logs `Planet Plagued`. The level never grows on its own.
- Each turn a plagued colony loses a random whole number of millions between `B` and
  `B + B div 5`, where `B` depends on the level: 1 → 10, 2 → 50, 3 → 100, 4 → 150,
  5 → 300, 6 or more → 500. The loss is taken from the races in their stored order
  (the first race first). If the loss is at least the whole population, everyone dies
  and the colony is removed (§2).
- A plagued colony does not grow, and each turn it adds `Planet Plagued` to its anger
  change (§4).
- The `No Plagues` trait cures the plague the next time it would strike, with no loss.
- Each turn, the best `Plague Prevention - System` cures every colony of the owner in
  that system whose level is at most its Val1.
- A Medical Bay cures plagues on your own planets and on allies' planets (Military
  Alliance or better) [H].

---

## 4. Happiness

All of this section is (confirmed: binary) unless marked. `v[name]` is the value of that
entry in the empire's happiness type (§1.8), in tenths of a percent.

**The empire-wide part `S`** (tenths), computed once per empire per turn from the events
logged since the last update (each event carries a count):
- `count × v[…]` for `Homeworld Lost`, `Any Planet Lost`, `Any Planet Colonized`, `Any Our
  Planet Captured` and `Any Enemy Planet Captured`;
- `count × v[New Treaty …]` for new treaties, unless a game option turns treaty effects off;
- `count × v[Any Ship Lost]` for each "ship lost in system" event, and `count × v[Any Ship
  Constructed]` for each "ship constructed" event;
- minus `trunc(E_happy / 5)`, where `E_happy` is the Happiness characteristic − 100 plus
  culture `Happiness` plus `Population Happiness` traits (§8.2). A happier race thus
  calms by a fixed amount every turn; it does not scale other changes.

**Each colony** then adds, in tenths:
1. **Drift.** If the owner's race is less than half of the population (whole-number half),
   add `v[Natural Decrease for Other Races]`. Otherwise it depends on the current mood:
   Rioting, Angry or Unhappy add `v[Natural Decrease]`; Happy or Jubilant **subtract** it;
   Indifferent adds nothing. With the usual negative value, moods drift towards
   Indifferent from both sides.
2. **Local events** in the colony's own sector: a battle adds `count × v[Battle in Sector
   - Win/Loss/Stalemate]`; `1M Population Killed`, `Ship Constructed`, `Facility
   Constructed` and `Planet Plagued` events add `count × v[…]`.
3. **System events** anywhere in its system: battles add `count × v[Battle in System - …]`
   and ship losses `count × v[Ship Lost in System]`. A battle in the colony's own sector
   therefore counts twice (sector and system values).
4. **Ships present.** Count the ships and bases in the system that are not destroyed,
   not cloaked and not mothballed; fighters, satellites, mines and drones never count,
   and neither does a ship without an owner. Ships of empires below Non-Aggression with
   the owner are *enemy*; the owner's own ships and those of empires at Non-Aggression or
   better are *ours*.
   - Enemy: if any are in the colony's sector, add `v[Enemy Ship in Sector] × that
     number`; else add `v[Enemy Ship in System] × the number in the system`.
   - Ours: the same with `v[Our Ship in Sector]` and `v[Our Ship in System]`.
5. **Troops.**
   - `v[Enemy Troops on Planet]`, once, while troops that another empire landed in a space
     battle are still on the planet with their ground combat unfinished (spec 04 §13).
     The treaty with that empire does not matter here.
   - `v[Our Troops on Planet] × n`, truncated, where `n` is the number of troop units in
     the colony's cargo, whoever owns them.
   - The landed troops are kept apart from the cargo. At the colony owner's end-of-turn
     processing, the fight resumes if their empire is still below Non-Aggression with the
     owner; otherwise the landed troops move into the colony's cargo.
6. **Plague.** `v[Planet Plagued]` while the colony is plagued.

**Applying it**
- `change = trunc(total / 10)` in whole percent, plus the sum of `Planet - Change
  Population Happiness` Val1 on the colony (positive values make it angrier).
- A negative change is limited to `trunc(Max Negative Anger Change / 10)`, a positive one
  to `trunc(Max Positive Anger Change / 10)`.
- `anger = clamp(anger + change, 0, 100)`, then at most 80 for a capital (§2).
- Changes smaller than 10 tenths in total are lost each turn, because of the truncation.
- A colony that is rioting after the update logs a riot message to its owner.

**Later the same turn**, after construction, the best `Change Population Happiness -
System` Val1 in each system is subtracted from the anger of every colony of the owner
there (then clamped to 0–100 and the capital limit). This is outside the clamp above.

**Conditions** do not affect anger.

**Rules from the release notes** [H], consistent with the above
- Mothballed ships never affect happiness.
- Cloaked ships do not trigger unhappiness.
- Our own troops on our own planet reduce anger.

**Effects**
- Output is multiplied by the band's `Mood … Modifier` (§5.1).
- Reproduction shifts by −5 to +5 (§3).
- **Riot:**
  - The colony produces nothing (§5.1), its queue gets no construction rate (§6.2), and it
    does not grow (§3).
  - Troops on the planet or ships in orbit calm it (steps 4–5).
- **Rebellion** does **not** come from rioting: nothing counts riot turns. A planet
  rebels only through the random event `Planet - Population Rebel` or an intelligence
  operation that incites it. Then, if there are fewer than 20 players, the planet founds a
  new computer empire and becomes its capital (details in the events and intelligence
  specs). The random event `Planet - Population Riot` sets anger to 100 (80 for a capital).
- **Emotionless** trait: the empire skips the whole update above, riot events skip its
  planets, and any other change to its colonies' anger (such as a system happiness
  facility) sets the anger to 35 (Indifferent) instead. A new colony keeps its starting 25
  (Happy, so +10 % output with stock Settings) until such a change. It never riots, and
  its growth gets no mood term. The UI shows its mood as "Emotionless".

---

## 5. Production and income

### 5.1 Planet output (per resource r) (confirmed: binary)

`r` is minerals, organics, radioactives, research or intelligence. For one colony:
1. `base` = sum of the colony's `Resource Generation - r` Val1 (research and intelligence:
   `Point Generation - r`). Every facility counts; population size does not matter
   (`Population Required to Operate One Facility` is never used).
2. **Value** (the three resources, normal game only): `base = round(base × value_r/100)`.
3. **Planet modifier:** if the best `Resource Gen Modifier Planet - r` (research and
   intelligence: `Planet Point Generation Modifier - r`) is `m > 0`,
   `base = round(base × (100 + m)/100)`. This is its own multiplication; it does not add
   to the terms of step 4.
4. **Race, mood and population:** `pct = 100 + E_r + (mood % − 100) + (population % −
   100)`, at least 0, and `base = trunc(base × pct/100)`.
   - `E_r` is the racial effect of §8.2: Mining, Farming or Refining for the three
     resources, each plus culture `Production` and the `… Production` traits; Intelligence
     (plus culture `Research`) for research; Cunning (plus culture `Intelligence`) for
     intelligence points.
   - `mood %` is the Settings `Mood … Modifier` of the colony's mood band.
   - `population %` is the production column of §5.2.
5. **Solar:** add `stars × sum of Solar Resource Generation - r Val1` (the three resources
   only), where `stars` counts the stars in the system.
6. **Nothing is produced** (steps 1–5 give 0) if the colony is rioting, has no
   population, or is blockaded (§2).
7. **Finite game:** for the three resources, value is not applied in step 2; instead the
   result of step 4 is capped by the stock left, and that amount is drawn from the stock.
   Solar output is not capped and draws nothing.

The **system modifier** works on the empire's total for a system, not per colony (§5.5).

Check against the Quick Start homeworld (07-observations): organics 800 at 98 % value →
784; Happy (+10) and a 2000M population (+30) → `trunc(784 × 140 %)` = 1097. Research
2500 × (100 + 20 − 2 + 10 + 30) % = 3950.

`Generate Points` abilities are added at the empire level, without modifiers (§5.4).

### 5.2 Population brackets

- Use the first Settings row, in file order, whose `Population Amount` is at least the
  colony's total population. That row gives both the production % and the SY rate %. If
  no row is large enough, both are 100 % (confirmed: binary).
- The stock table runs from 100 % (up to 19M), gaining about 1 % per row. Rows are 20M
  wide at first and 200M wide later, and the table tops out at 200 % above about 10,000M
  [D/H].
- The row count comes from Settings. It is not limited to 20 [H].

### 5.3 Finite resources

- Each turn's capped output (§5.1 step 7) is subtracted from the stock (confirmed:
  binary).
- Remote mining (confirmed: binary): in each sector, only the first of the empire's ships
  with a remote-mining component (in object order) works. It mines every minable object in
  the sector: normal game `round(Val1 × value/100)` per resource, and the value drops by
  1 point per turn when `Remote Mining Decreases Asteroid Value` is on; finite game
  `min(Val1, stock)`, drawn from the stock. Looking at the Empire Status window does not
  change values [H].

### 5.4 Other income

- **Remote mining:** comes from ships (see the ships spec).
- **Trade** needs a Trade Alliance or better [M].
  - Resources are traded at Trade Alliance, research at Trade & Research, intelligence at
    Partnership.
  - Trade starts at 1 % of the partner's output and grows 1 % per turn, up to `Maximum
    Trade Percentage`.
  - The amount is scaled by one factor: 100 + (Political Savvy − 100) + the race's
    `Trade` trait values + the culture's `Trade` value, in percent (confirmed: binary,
    spec 05 §3.3).
  - Switching between treaties of Trade Alliance or better keeps the current percentage
    [H]. Breaking the treaty resets it.
- **Tariffs** (confirmed: binary): a subjugated empire pays 40 %, a protectorate 20 %,
  of each kind of income it takes in this turn (`round(income × % / 100)`, never more than
  that income). The master's treasury receives the three resources at once; the research
  and intelligence taken are lost. The master's storage cap applies at its end of turn
  [H].
- **Computer player bonus** (confirmed: binary): for a computer-controlled empire, each
  kind of income (the three resources, research and intelligence), after tariffs, is
  multiplied by 1, 2, 3 or 5 for the setup choice None, Low, Medium or High, and rounded.
  Its construction rates are also multiplied (§6.2). There is no bonus to the planet
  percentages of §5.1.
- **Generate Points** abilities of every object the empire owns are summed and added to
  income without modifiers (confirmed: binary).
- Research and intelligence points go to their own specs. They are not capped by resource
  storage (confirmed: binary).

### 5.5 Delivery (confirmed: binary)

Output is gathered per system. For each system where the empire has colonies, and for
each of the five kinds:
1. `T` = the sum of its colonies' output there (§5.1).
2. **System modifier:** if the best `Resource Gen Modifier System - r` (research and
   intelligence: `System Point Generation Modifier - r`) over its colonies there is
   `s > 0`, then `T = round(T × (100 + s)/100)`. Solar output is included in `T`.
3. **Spaceport rule:** if none of the empire's colonies in the system has a `Spaceport`
   facility and the empire lacks the `No Spaceports` trait, the system delivers nothing,
   except the empire's home system, which delivers `trunc(T × Home System Percentage
   Value With No Spaceport / 100)`. The rule covers research and intelligence too. The
   home system is the one recorded for the empire (§2); it stays the same after the
   homeworld is lost, so the empire's colonies there keep the 25 %, and colonies anywhere
   else never get it.
4. Otherwise the whole `T` is delivered.

Rioting and blockaded colonies already produce 0 (§5.1). Undelivered output is lost, not
banked. The UI shows it in parentheses and adds a "No Spaceport" icon.

### 5.6 Treasury and conversion (confirmed: binary unless marked)

- **Income floor:** after summing all systems, a resource whose delivered total is exactly
  0 is set to `Minimum Empire X Generation`. A positive total is kept even if smaller.
  Every living empire gets it, including one that holds no colony and survives on its
  ships alone.
- **Storage:** minerals, organics and radioactives are each capped at `Minimum Empire Point
  Storage` plus, for every colony, `round(its Resource Storage - X × (100 + X Storage
  trait)/100)`. Colonies need no population for this. The cap is applied once, late in the
  empire's turn, after construction (§12); the excess is lost. Research and intelligence
  are never capped. Every treasury is also limited to 2,000,000,000.
- Empire Status shows these sections [M]:
  - **Income:** Colonies, Trade, Tariffs, Remote Mining, Total.
  - **Expenses:** Tariffs, Maintenance, Construction Queue Usage, Total.
  - **Net per turn**, which may be negative.
  - **Treasury:** the current amount and the maximum.
- **Convert Resources** order: the player spends X of one resource (at most what is
  held, and at most 65535 per order) and at once receives `trunc(X × (100 − L)/100)` of
  another, where `L` is the best `Resource Conversion` on the planet or ship that gives
  the order. Other converters in the system do not count.

---

## 6. Construction queues

### 6.1 Ownership

**Who has a queue**
- Every colony has a queue, but a colony with zero population cannot build.
- A ship has a queue only if it carries a Space Yard component.
- Building ships needs a space yard, either a facility or a component. A planet can have
  at most one space yard facility [H].

**Blocking rules** [H]
- A cloaked planet or ship cannot build. Cloaking a ship clears its queue.
- Scrapping a planet's space yard removes the ships from its queue. In the game
  (confirmed: binary), at the start of each queue's turn, every item the queue can no
  longer build is removed: ships once the queue has no yard, units or facilities in a
  queue that cannot build that kind, and upgrades with nothing left to upgrade (§6.6).

### 6.2 Rate (per resource r) (confirmed: binary)

**Colony queue**
- A rioting colony, or one with no population, has rate 0.
- *No space yard facility:* `base = Empire Base Planet r Usage Rate`, and the only
  modifier is `m = population SY % − 100` (§5.2). Construction aptitude, culture and
  traits do **not** apply.
- *With a space yard:* `base` = the sum of the yard's Val2 for `r`, and `m = E_SY +
  E_planetSY + (population SY % − 100)`, where `E_SY` is Construction Aptitude − 100 plus
  culture `SY Rate` plus `SY Rate` traits, and `E_planetSY` is the `Planetary SY Rate`
  traits.
- If `m ≠ 0`: `rate = trunc(base × max(0, 100 + m)/100)`; else `rate = base`.

**Ship queue**
- `rate = trunc(Σ yard Val2 for r × (100 + E_SY)/100)`. Population and `Planetary SY
  Rate` do not apply. A mothballed ship has rate 0.

**Both**
- A computer-controlled empire's rate is then multiplied by 1, 1.5, 2 or 3 for the bonus
  None, Low, Medium or High, truncated. Negative rates become 0.
- Mode: in emergency mode `rate = trunc(rate × Emergency % / 100)`; in slow mode
  `rate = trunc(rate × Slow % / 100)`.
- Check: the Quick Start homeworld has a yard and 2000M people: 2000 × 130 % = 2600.

### 6.3 Processing (confirmed: binary)

**Order.** Each empire first handles the queues whose top item is a facility with, in this
order, `Spaceport`, `Resource Generation - Minerals`, `… - Organics`, `… - Radioactives`
and `Supply Generation`; then all other queues in the empire's queue order.

**Each queue, each turn**
- Nothing happens if it is empty or on hold.
- `use_r = min(rate_r, max(0, cost_r − progress_r))` for the **top item only**.
- **All or nothing:** if the treasury holds at least `use_r` of every resource, the
  treasury pays `use` and the item's progress grows by the full `rate` (not by `use`).
  Otherwise nothing is paid, no progress is made, and the owner gets a "Lack of
  Resources" message for that queue.
- When the top item's progress covers its cost, its progress is cleared and it is built
  (§6.5). Any overshoot is lost.
  - If the completion builds nothing (no free facility slot, the ship limit, or a unit
    without room), the item stays at the top of the queue and must be paid for again.
  - Otherwise it leaves the queue, unless Repeat Build is on and the item can still be
    built. Upgrades never repeat.
- So at most one item finishes per queue per turn, and unused rate never flows to the
  next item.
- The UI estimates the build time as `max_r ceil(cost_r / rate_r)`; for example, 6000 at
  a rate of 2000 takes 3 turns.

**Editing the queue** [H]
- Deleting the item in progress discards its progress.
- Reordering resets its timer.
- Removing the top item asks for confirmation.

### 6.4 Modes

**Emergency build** (confirmed: binary unless marked). Each queue keeps one turn counter.
At the end of each queue's turn (whether or not it built anything, and also for cloaked
queues that cannot build):
- in emergency mode: if the counter has reached `Maximum Emergency Build Turns`,
  emergency mode ends; otherwise the counter goes up by 1;
- not in emergency mode: if the counter is above 0, the queue is in **slow mode** and the
  counter goes down by 1.

So emergency runs at 150 % for `Maximum Emergency Build Turns` + 1 turns if left on (the
rate is chosen before the counter moves), then slow mode at 25 % lasts as many turns as
the counter shows. Switching emergency off early leaves the counter as it is, so slow mode
lasts as long as the emergency did [H]. Emergency mode can be switched on only while the
counter is 0; during slow mode the control does nothing. So an emergency always starts
from 0, and switching it off before a turn has passed costs no slow turns. Only the player
switches it on, in the Set Construction Queue window: computer players and ministers never
use emergency build (confirmed: binary).
- Slow mode cannot be avoided by clearing the queue.
- It cannot be avoided by handing over the planet.
- It cannot be avoided by mothballing and unmothballing the ship.
- It cannot be avoided by cloaking.

**Other controls**
- **Repeat Build** rebuilds the top item forever, as long as it is still valid.
- **On Hold** freezes the queue and spends nothing.
- **Move To** sends newly built ships to a waypoint.
- **Queue types** are named templates:
  - Fill appends a template's items, checking each against current tech.
  - Add Type saves the current queue as a new template.
- **Multi-Add** adds ships or units to several selected queues at once.
- **Only Latest** filters the item list to the newest level of each family.

### 6.5 Items

An item has a count; its cost is the unit cost times the count, and it completes all at
once (confirmed: binary).

**Ships**
- A ship appears at the queue's location when finished. It is invisible while being built.
- Each ship built logs a `Ship Constructed` event in that sector (§4) (confirmed: binary).
- If the empire is at its ship limit, nothing is built and the owner gets "Maximum Ships
  Reached"; the progress was already cleared, and the item stays, so it must be paid for
  again (confirmed: binary). The limit is checked once per item, so an item of several
  ships can go past it.

**Facilities**
- Only as many facilities can be queued as there are free slots, counting ones already
  queued. The game warns when this is exceeded.
- Adding a system-wide facility that is already present in the system shows a notice [H].
- On completion (confirmed: binary):
  - If the colony has fewer facilities than slots, the item's whole count is added, even
    if that goes past the slots.
  - Otherwise nothing is built, no message is sent, and the item stays at the top with
    its progress lost.
  - A second space yard is not checked for again at this point.
- A finished facility fires `Facility Constructed`, once per facility (confirmed: binary).

**Units** (confirmed: binary unless marked)
- The player picks how many to build.
- Each unit is placed on its own: in the builder's cargo if it fits, else in the cargo of
  another planet or ship of the empire **in the same sector**, taken in the game's object
  order (planets and ships mixed).
- A unit that finds no room is not built, and a "No Storage Available" message is sent
  for it.
- The item leaves the queue only if its last unit found room. All units of an item are
  the same size, so in practice: if any unit found no room, the item stays at the top with
  its full count and its progress cleared, and the units already placed are kept.
- Units have no cap at completion (confirmed: binary); the units-per-player limit is
  checked when units are launched (spec 03).

### 6.6 Upgrades and scrapping

**Upgrades** (confirmed: binary)
- An upgrade item stores a target facility and a count. Both are fixed when the item is
  queued. The count is the number of facilities of the target's family on the colony
  whose level (`Roman Numeral`) is below the target's. The player cannot choose a
  smaller count. With no such facility, the Upgrades tab says there is nothing to
  upgrade.
- A queue refuses a second upgrade to the same target. There is no tech check at that
  point.
- Cost per resource: `trunc(current cost of the target × Upgrade % / 100) × stored
  count`.
- At the start of each queue's turn, an upgrade item is removed once the colony has no
  lower-level facility of that family left (§6.1). Its count is never lowered.
- On completion, the colony's facilities are taken in their stored order (the order in
  which each facility type was first added). Every lower level of the family is
  converted to the target until the count is used up, in that order and not lowest level
  first. If fewer are left than the count, only those change, and the full price was
  still paid.
- The "Upgrade Facilities" button does this for every colony and every facility type on
  it. The target is the empire's highest researched level of that family. Facility items
  already in the queues are also moved to that newest level, keeping their counts.

**Scrapping** (confirmed: binary)
- Facilities can be scrapped on one planet, or one type everywhere.
- The refund % is the larger of the Settings `Scrap … Percent Returned` and the best
  `Resource Reclamation` among the empire's planets and ships in that sector.
- A facility refunds `round(cost × % / 100)` of each resource, paid into the treasury at
  once. The storage cap applies later in the turn (§5.6).

---

## 7. Maintenance [M/D/H]

**Rate** (confirmed: binary)
- `maint% = max(5, Starting Percent Maint Cost − E_maint)`, where `E_maint` is
  Maintenance Aptitude − 100 plus culture `Maintenance` plus `Maintenance Cost` traits.
- The aptitude is a "true percent" (range 80–120) [H].
- The manual's 30 % figure is out of date.

**Per ship and resource** (confirmed: binary)
1. `a = trunc(cost_r × maint% / 100)`, where `cost_r` is the design's cost.
2. `b = trunc(a × (100 + D) / 100)`, where `D` is the sum of `Modified Maintenance Cost`
   over the hull and components. Stock bases carry −50 there, which is how bases pay half
   [H].
3. `c = trunc(b × (100 − R) / 100)`, where `R` is the best `Reduced Maintenance Cost -
   System` among the empire's planets and ships in the ship's system (0 if none).
4. A mothballed ship pays 0.
- Fighters, satellites, mines and drones pay **no** maintenance, in space or in cargo.
  Planets pay none.

**Mothballing**
- A mothballed ship costs nothing, has no abilities, adds nothing to score, and does not
  blockade.
- Mothballing, unmothballing, scrapping and analysing all need a space yard at the
  location.
- Unmothballing costs `UnMothball Ship Percent Cost` of the design's cost.

**Payment** (confirmed: binary)
- Maintenance is paid after the turn's income and trade, before construction (§12).
- For each resource, if the treasury holds less than the total charge, that treasury goes
  to 0 and the difference is added to `unpaid`, which sums all three resources.
- If anything is unpaid, `unpaid div Maintenance Cost Amt Per Dead + 1` vehicles are
  destroyed (so 1 to 19999 unpaid destroys one, 20000 destroys two), but never more than
  there are candidates.
- Candidates: the empire's ships that are out of supply (supply 0) and pay maintenance.
  If there are none, every ship and unit group of the empire is a candidate.
- Each victim is picked at random among the remaining candidates and destroyed whole (a
  unit group loses all its units). The owner gets a "Ship Abandoned" or "Unit Group
  Abandoned" message. The loss fires no happiness event: `Ship Lost in System` (and so
  `Any Ship Lost`) is logged only for a ship destroyed by damage.

---

## 8. Racial characteristics, traits, cultures

### 8.1 Characteristics and costs

There are 15 characteristics, all integer percents defaulting to 100 [D]:
- Physical Strength, Intelligence, Cunning, Environmental Resistance, Reproduction;
- Happiness, Aggressiveness, Defensiveness, Political Savvy;
- Mining, Farming, Refining, Construction, Repair and Maintenance Aptitude.

`Min/Max Pct` limit a characteristic only in the race window of Empire Setup, whose
buttons keep the value in range. Nothing else reads the limits (confirmed: binary): the
cost below and every racial effect use the stored value as it is, so a value from a race
preset or an empire file outside the range is costed and applied as it stands.
With `d = v − 100`, `c = Pct Cost`, `T = Threshold`,
`P = Threshhold Pct Cost Pos` and `N = Threshhold Pct Cost Neg` (confirmed: binary):
- If `|d| ≤ T`, or `T < 1`: the characteristic costs `c × d` (negative `d` is a refund).
- If `d > T`: it costs `c × T + P × (d − T)`.
- If `d < −T`: it costs `−c × T − N × (−d − T)`, a refund.

So beyond the threshold each point costs `P` (or refunds `N`) racial points outright; they
are not percentages of `c`. With the stock values (`c` = 25, `P` = 100, `N` = 10) a
Mining Aptitude of 130 costs 25 × 20 + 100 × 10 = 1500.
- Our earlier reading (`c × P / 100` per point) made every stock race preset add up to
  exactly 2000, 3000 or 5000. With the game's formula, five preset tiers come out higher
  (3750 or 5750), so those presets exceed their tier as the game counts them.
- **Racial points spent** = the sum of the characteristic costs plus the trait costs. The
  setup refuses an empire whose total exceeds the game's racial point setting (confirmed:
  binary).

**Effects.** Each characteristic feeds one racial effect of §8.2 with `v − 100` (confirmed:
binary; the uses of each effect are in the sections named):
- Physical Strength: ground combat.
- Intelligence: research (§5.1).
- Cunning: intelligence points (§5.1).
- Environmental Resistance: its effect (§8.2) divided by 5, truncated, is added to
  reproduction on every planet (§3). It does not change anger or the conditions penalty.
- Reproduction: added to the growth rate (§3).
- Happiness: its effect (§8.2) divided by 5, truncated, gives tenths of calming per turn
  for every colony (§4).
- Aggressiveness and Defensiveness: to-hit modifiers.
- Political Savvy: trade.
- Mining, Farming and Refining: output of that resource (§5.1).
- Construction: space yard rate (§6.2).
- Repair: repair rate.
- Maintenance: subtracted from the maintenance rate (§7).

Racial combat bonuses also apply to fighter groups [H].

### 8.2 Traits and cultures

- An empire has one culture and any set of traits that meets the Required and Restricted
  rules.
- Engine effects are keyed on `Trait Type`, never on the trait's name.
- **Racial effects** (confirmed: binary). Each effect is one number, in percentage points:
  `(characteristic − 100)` + the matching culture field + the sum of Val1 of the traits of
  the matching type. The pairs are:

  | Effect | Characteristic | Culture field | Trait type |
  |---|---|---|---|
  | Reproduction | Reproduction | — | `Reproduction` |
  | Mineral / Organic / Radioactive output | Mining / Farming / Refining | — | `Mineral/Organics/Radioactives Production` |
  | All three resources | — | `Production` | `Production` |
  | Research | Intelligence | `Research` | `Research Production` |
  | Intelligence points | Cunning | `Intelligence` | `Intelligence Production` |
  | Space yard rate | Construction | `SY Rate` | `SY Rate` |
  | Maintenance | Maintenance | `Maintenance` | `Maintenance Cost` |
  | Happiness | Happiness | `Happiness` | `Population Happiness` |
  | Environmental resistance | Environmental Resistance | — | `Tollerance` |
  | Trade | Political Savvy | `Trade` | `Trade` |
  | Ground combat | Physical Strength | `Ground Combat` | `Ground Combat` |
  | Repair | Repair | `Repair` | `Repair` |
  | Space combat | — | `Space Combat` | `Space Combat` |

  Aggressiveness and Defensiveness have their own effects (`Ship Attack`, `Ship Defense`
  trait types add to them).

---

## 9. Empire and game setup [M]

**Empire Setup tabs**
- **General:**
  - Empire Name and Empire Type, shown together as "Name Type";
  - Emperor Title and Emperor Name;
  - password (asked each turn) and email;
  - race and ship style, design-name file, minister style;
  - Computer Controlled;
  - Use Race Minister Style, which disables the minister style choice;
  - experience and Race Age, both read-only ("Newborn" when new).
- **Environment:** the atmosphere breathed, which is also the homeworld's, and the home
  planet type.
- **Culture:** with a window comparing the modifiers.
- **Characteristics** and **Advanced Traits:** both show the points left.
- **Description:** biology, society and history texts, plus demeanor and happiness type.

Create Empire is refused while the point balance is negative. A password-protected empire
also needs its password before it can be edited [H].

**Experience and race age** (confirmed: binary)
- Experience is a whole number kept with the empire. It starts at 0 and is saved in games
  and in the empire file, so it builds up over several games while the player keeps
  saving the same empire.
- It grows during play (the total never goes above 500,000,000):
  - when a ship or unit group is destroyed in combat, the empire that destroyed it gains
    its tonnage div 10: the hull's tonnage for a ship, the units' total for a group, and
    nothing for a planet. Battles in the combat simulator give nothing;
  - each finished facility item adds its count;
  - each ship built adds its hull's tonnage div 10; units add nothing.
- Race age is not stored; it is a label read off the experience: up to 5,000 Newborn,
  10,000 Infantile, 50,000 Young, 200,000 Moderate, 1,000,000 Old, 10,000,000 Ancient,
  100,000,000 God-like, 400,000,000 Stellar Ancients, and above that First Ones.
- Neither has any gameplay effect. They are only shown: on the Empire Setup General tab,
  and in the Race Report (the points for one's own empire only, the age for every race).

**Player settings** (choices and defaults confirmed: binary; details in spec 01 §2.2)
- Starting resources: 5,000, 20,000 (the default) or 100,000.
- Home Planet Value: Bad, Average (the default) or Good. It selects the
  `Plr Planet Value Low/Medium/High` keys and the homeworld size Small, Medium or Large.
- Number of starting planets: 1 (the default), 3, 5 or 10. A neutral empire always gets
  one. They are placed in nearby systems (spec 01 §3.6).
- *All player planets the same size* (a quadrant option, on by default): every homeworld
  has the size set by Home Planet Value [H].
- Placement: whether players may share a system, and whether they are spread evenly.
- Starting tech level: Low (the default), Medium or High.
- Racial points: 0, 2,000 (the default), 3,000 or 5,000.
- Score visibility: own score, own plus allies (the default), or everyone's.
- Random computer players, either normal or neutral. Neutral empires cannot use warp
  points, so they stay in their home system (spec 01 §8).

**Relevant game options**
- Finite resources.
- Colonize only breathable planets.
- Colonize only the home planet type.
- Event frequency and severity.
- Allow intelligence.

Quick Start picks from the Settings style list and hands out premade designs [H].

**Homeworld and starting planets** (confirmed: binary unless marked)

How the planets are chosen is in spec 01 §3.6. Then **every** starting planet, the
homeworld and the extra ones alike, is set up the same way:
- Its system is explored for the owner.
- Any `Ancient Ruins` or `Ancient Ruins Unique` ability is removed.
- **Values.** In a normal game, each of the three values is the `Plr Planet Value
  {Low,Medium,High} Percent` of the Home Planet Value setting plus R[1,10] − 5, drawn
  separately per resource, so it lies within −4 to +5 of the setting (the observed 100 / 98
  / 102 % fits). In a finite game each value is exactly the matching `... Resources` key.
- Conditions are not changed: they stay as generated (spec 01 §5.6).
- **Colony.** Any previous owner is removed. The planet becomes a colony of the empire's
  race at its **maximum population**. Every starting planet, the extra ones included, is a
  capital (anger at most 80, §2) with colony type `Homeworld`, so losing any of them logs
  `Homeworld Lost`.
- **Facilities.** For each role the empire's best known facility is used (the facility
  whose ability value, or tech level, is highest). They are added in this order while the
  planet has free facility slots:
  1. a spaceport, unless the race has the `No Spaceports` trait;
  2. a space yard;
  3. a supply generator (resupply depot);
  4. one mineral, one radioactives and one organics producer, in that order;
  5. one research facility, unless the empire already knows every technology;
  6. then, until the slots are full, alternately a mineral producer and a research
     facility, starting with minerals (only minerals once every technology is known).

  A Medium planet (15 slots) thus gets a spaceport, a yard, a depot, 5 mineral, 1 organic,
  1 radioactive and 5 research facilities, as observed (07-observations, Calibration).
- A homeworld's colony type is fixed [H].

**Starting stockpile** (confirmed: binary). After the planets are set up, each of the
empire's pools of minerals, organics, radioactives and research is set to the Starting
Resources amount **plus one turn of that empire's production** of that kind: its colonies'
output delivered as in §5.1 and §5.5, with the rule for `Minimum Empire X Generation`
(§5.6). Remote mining, `Generate Points`, trade, tariffs and the computer player's bonus
are not included, so Starting Resources and that turn are never multiplied by the bonus.
Nothing is drawn from finite stocks for it. The intelligence pool starts at 0.
This matches the observed 20000 plus one turn of production (07-observations).

**Starting technology** (confirmed: binary): Low keeps the tech areas' start levels,
Medium raises each allowed area to a middle level from the tech area data, and High sets
every allowed area to its maximum (spec 05). The colonization technology of the home
planet type is always at least level 1.

---

## 10. Ministers and colony types [M/H]

There are 25 ministers. The turn order in which they act and the rules each one follows
are in spec 05 §7.1 and §7.5 (confirmed: binary).

**Global ministers**
- Design and Ship Construction.
- Expenses.
- Production Output. The manual describes emergency builds and keeping planets happy.
- Research, Intelligence and Politics.
- Repair, Resupply, Scrap and Retrofit.

A global minister takes full control of its area and can undo the player's choices. The
Expenses and Production Output ministers do nothing in v1.95: their turn steps are empty
(confirmed: binary).

**Individual ministers**
- Facility Construction.
- Transports, Carriers and Colonization.
- Attack, Defense, Exploration and Patrol.
- Mines/Satellites/Drones, Fleets, Stellar Manipulation and Ship Cloaking.
- Space Yard Ships and Troops.

In a human empire these act only on vehicles, fleets and planets whose own minister flag
is on (confirmed: binary).

**Minister options**
- "Automatically use Individual Ministers for newly built vehicles" turns the flag on for
  vehicles and launched units as they appear (confirmed: binary). The manual says it
  also covers new colonies.
- The option that forbids AI changes in a simultaneous game: when the host covers this
  player's missed turn, the stand-in changes nothing (confirmed: binary).
- Bulk buttons (confirmed: binary):
  - Select All and Select None tick or clear every minister.
  - Indiv. Ministers On and Off set the flag on all of the player's ships, planets and
    fleets.
  - Complete AI On and Off do both, and also set the new-vehicles option.

**Minister style**
- The style is a personality set taken from `Ai/<Aggressive|Defensive|Neutral>/`.
- With "Use Race Minister Style", the race's own AI files are used instead. These files
  are covered in the AI spec.
- OpenSE4 choice: the Ministers window also offers the style and the race-style switch,
  so a player can change them during a game. All minister settings belong to the empire
  and travel with its orders.
- In the game's Empire Setup (§9) (confirmed: binary):
  - A new empire starts with an empty style, and "Use Race Minister Style" and "Computer
    Controlled" unticked.
  - The style picker lists every folder under `Ai\`. Picking nothing, or cancelling,
    leaves the field as it was, so once a style is chosen it cannot be emptied again.
  - The empire can be created with the style left empty, which means the race's own AI
    files.
  - Ticking "Use Race Minister Style" disables the field and stores an empty style.
  - At run time an empty style loads the race's own files, and a named style loads that
    folder's. A missing file falls back to a default under `Ai\`.
  - Random computer players and rebel empires always get an empty style.
- OpenSE4's Empire Setup offers the style folders plus "the race's own" (no style), which
  a new empire starts with, and "Use Race Minister Style" starts off, as in the game. The
  empire starts the game with both, whether it is played by a human or marked Computer
  Controlled.

**Colony types**
- A colony type is a label chosen at colonization or later. The defaults come from
  `DefaultColonyTypes.txt`.
- The Facility Construction minister and the AI know nine fixed types (confirmed:
  binary): Homeworld (also "Imperial Center"), Mining Colony, Farming Colony, Refining
  Colony, Resupply Base, Research Compound, Intelligence Compound, Construction Yard and
  Military Installation. A label outside that list is built like Homeworld.
- How the computer chooses a type, and how ministers use it to pick facilities, is in
  spec 05 §7.5.
- The Population Transport minister (confirmed: binary):
  - A transport more than half full delivers to the least-populated own planet that is
    below its maximum, in a safe system and able to host the carried race.
  - An emptier transport loads at the nearest safe planet with at least 1000M of a race
    that such a planet can take.
  - The manual's 500M limit does not exist.

---

## 11. Planet management screens [M]

- **Colonies:** stats, a mini-map, and a sortable list. Left-click jumps to a colony;
  right-click opens its report.
  - Tabs: General, Value, Production, Facilities, Cargo, Construction, Status, Races and
    Orders (simultaneous games only).
  - Buttons: Scrap Facil Types (empire-wide) and Set Colony Type.
- **Planets:** every planet seen.
  - Filters: All, Colonizable, All/Enemy/Ally Colonies, Colonizable & empty, Colonizable
    & breathable, Ship en route, Asteroids, Special, and "No Sys To Avoid".
  - Send Colony Ship dispatches the nearest idle colony ship that can colonize the target.
- **Planet Report:**
  - Detail: type and size, atmosphere, conditions, value, colony type,
    population/max with dome and race icons, reproduction, mood, outputs, current build
    and time left.
  - Facil: facility grid with levels.
  - Cargo.
  - Ability: *derived* planet modifiers only.
  - Unowned planets use a reduced "Empty" view.
- **Construction Queues:** list columns Rate, Usage, Planet Value, Facilities and Cargo.
  - Filters: Ships, Planets, Ship SY, Planet SY.
  - Buttons: Multi-Add, Scrap Facilities, Upgrade Facilities.
- **Set Construction Queue:** tabs Ships (needs a yard), Facilities (planets only), Units
  and Upgrades.
  - The hover report shows cost and build time. For the top item, the real remaining time
    is shown in parentheses.
- **Select Facilities** is the scrap checklist, with the refund for each facility.
- **Facility Report** shows the cost and the abilities.
- **Empire Status:** the balance sheet, plus Options, Ministers, Systems To Avoid,
  Waypoints, Strategies, Repair Priorities, Change Email and Change Password.
- **Race Report:**
  - Detail: treaty, trade %, mood, age, culture, demeanor, atmosphere and planet type.
    For our own empire it also shows experience.
  - Descr.
  - Race: characteristics and traits.
  - Tech: inferred from designs we have seen.

---

## 12. Economy phase order (confirmed: binary)

The economy runs inside each empire's end-of-turn processing, empire by empire in empire
order, after movement and space combat. The full turn order, including the steps outside
the economy, is in spec 05 §8. The economy-relevant steps of one empire, in order:

1. Intelligence, then research, each spending its pool (filled at the previous turn's
   steps 2 and 3) and then emptying it (spec 05 §1.4, §2.1).
2. Income: each of the five kinds (minerals, organics, radioactives, research,
   intelligence) is the empire's production (§5) plus remote mining (resources only) and
   other sources; tariffs paid to a master are taken off (spec 05 §3.3); a computer player's
   bonus multiplies what is left; the result is added to the treasury and point pools
   (each capped at 2,000,000,000).
3. Treaties: trade income from partners and the trade counters (spec 05 §3.3).
4. Maintenance, with the shortfall penalty (§7).
5. Planets: upkeep, cargo and storage, population growth on the turns set by `Reproduction
   Check Frequency` (§3), planet changes and plague damage.
6. Happiness (§4), skipped for a race with `Population Emotionless`.
7. Construction and its completion events (§6).
8. Repair and supply (spec 03).
9. Storage cap on minerals, organics and radioactives; research and intelligence pools are
   never capped.
10. System-wide abilities that act each turn (§1.5): `Change Population Happiness -
    System`, `Change Population - System`, `Plague Prevention - System`, training, and
    every 10th turn the system value and condition changes.

Random events come after every empire's processing and after the victory check, once per
turn for the whole galaxy (spec 05 §4). Each step iterates in stable ID order.

---

## 13. Open questions to verify in the running game

Answers marked (confirmed: binary) come from the executable; the rule itself is in the
section named.

1. **Modifier combination.** *Answered* (confirmed: binary, §5.1, §5.5): population,
   mood and racial effects add; value, the planet modifier and the system modifier are
   separate multiplications, each rounded; the system modifier works on the system total.
2. **Mood multipliers.** *Answered: Settings wins* (Happy = 110 %, 07-observations;
   confirmed: binary).
3. **Facility staffing.** *Answered:* `Population Required to Operate One Facility` is
   never used (confirmed: binary).
4. **Queue rate.** *Answered* (confirmed: binary, §6.2): a planet without a yard uses the
   Settings base rate (2000) with only the population SY %; with a yard, population SY,
   Construction Aptitude, culture SY Rate and the SY Rate and Planetary SY Rate traits all
   add; ship yards get the aptitude, culture and SY Rate trait but not population or
   Planetary SY Rate.
5. **Multiple completions.** *Answered* (confirmed: binary, §6.3): at most one item per
   queue per turn. A queue that cannot pay its full turn pays nothing. Queues whose top
   item is a spaceport, a resource generator or a supply facility go first.
6. **Build modes.** *Answered:* emergency 150 %, slow 25 %, both from Settings
   (confirmed: binary, §6.4).
7. **Growth.** *Answered* (confirmed: binary, §3): rounded per race, at least 1M while the
   rate is positive, applied only on the turns set by `Reproduction Check Frequency` and
   never scaled by it; conditions give −20 to +5 points, and Environmental Resistance adds
   `(v − 100)/5` points everywhere.
8. **Conditions.** *Answered* (confirmed: binary, §2): six bands (Deadly, Harsh,
   Unpleasant, Mild, Good, Optimal) on a 0–1.5 scale, with edges 0.3, 0.5, 1.0, 1.3 and 1.5.
   They add no anger.
9. **Anger details.** *Answered* (confirmed: binary, §4): presence counts per ship; a
   happiness facility's Val1 is whole percent; a new colony starts at 25; anger is stored
   per colony.
10. **Rebellion.** *Answered* (confirmed: binary, §4): rioting never leads to rebellion by
    itself; only the `Planet - Population Rebel` event or an intelligence operation makes
    a planet rebel.
11. **Income floor.** *Answered* (confirmed: binary, §5.6): it is not a floor. A resource
    whose delivered colony output is exactly 0 is replaced by the Settings amount, before
    trade, tariffs and maintenance.
12. **Home system.** *Answered* (confirmed: binary, §5.5): yes, the empire's home system
    delivers 25 % (truncated) without a spaceport, research and intelligence included.
    The recorded home system never moves: it is set when the game is created (or when a
    rebel empire is founded) and stays after the homeworld is lost (§2).
13. **Storage timing.** *Answered* (confirmed: binary, §5.6, §12): the cap is applied once
    per turn after construction; income, scrap refunds and tariffs received are not capped
    when they arrive.
14. **Maintenance.** *Answered* (confirmed: binary, §7): the shortfall is summed over the
    resources; victims are random, preferring ships out of supply; units pay nothing;
    `Modified Maintenance Cost` works as `100 + Val1`; culture Maintenance and the
    aptitude are both subtracted from the base rate.
15. **Storage trait.** *Answered* (confirmed: binary, §2): `Planet Storage Space` raises
    maximum population, facility slots and cargo. Resource storage has its own traits.
16. **Finite games.** *Answered* (confirmed: binary, §5.1, §1.2): value % does not apply;
    output is capped by the stock and drawn from it. `Planet Value Percent Loss After
    Owner Death` lowers the values when a colony dies out.
17. **Asteroids.** *Answered* (confirmed: binary, §2): never. Every colonization path
    refuses an asteroid field, so the asteroid rows of PlanetSize are never used.
18. **Upgrades.** *Answered* (confirmed: binary, §6.6): an upgrade item stores its target
    and a count fixed when queued, and is priced per facility. The count is always every
    lower-level facility of the family on the colony, so the player cannot choose fewer.
    On completion, facilities are converted in their stored order until the count is
    used up.
19. **Unit overflow.** *Answered* (confirmed: binary, §6.5): only into the empire's other
    planets and ships in the same sector.
20. **Plague.** *Answered* (confirmed: binary, §3): 10/50/100/150/300/500M (plus up to a
    fifth more) per turn by level; the level does not progress on its own.
21. **Homeworld start.** *Answered* (confirmed: binary): see §9. Home Planet Value picks
    the homeworld size (Small, Medium or Large) and the value keys, each value within −4 to
    +5 of the setting. Every starting planet gets full population and facilities in a fixed
    order, and the resupply depot is the third facility placed. The stockpile is Starting
    Resources plus one turn of production. Every starting planet is a capital (anger
    capped at 80) of colony type `Homeworld`.
22. **Setup ranges.** *Answered* (confirmed: binary): racial points 0, 2,000 (the
    default), 3,000 or 5,000; starting resources 5,000, 20,000 (the default) or 100,000
    (§9).
23. **Experience and race age.** *Answered* (confirmed: binary, §9): none. Experience
    grows from kills and construction and carries across games in the empire file, and
    the race age is a label derived from it. Both are only shown.

Items 24 onward were the engine's guesses. They are settled by the executable, and
`src/game/economy*.cpp` follows the sections named. Where the engine still differs from
the answer, [PARITY_GAPS.md](../PARITY_GAPS.md) lists it (economy section).

24. **Mood and reproduction.** *Settled* (confirmed: binary, §3): Angry −5, Unhappy −2,
    Indifferent 0, Happy +2, Jubilant +5, and no growth at all while rioting. The rate is
    clamped to 0–100, so a race never shrinks by growth.
25. **Conditions bands.** *Settled* (confirmed: binary, §2): the 0–1.5 scale and bands
    above; reproduction −20/−5/−2/0/+2/+5; no anger; Environmental Resistance adds a flat
    `(v − 100)/5`.
26. **Mixed races.** *Settled* (confirmed: binary, §3, §4): anger is per colony; the
    owner's race sets growth and output; drift uses `… for Other Races` only when the
    owner's race is under half the population, else `Natural Decrease` pulls towards
    Indifferent.
27. **Presence.** *Settled* (confirmed: binary, §4): counted per ship (ships and bases
    only), with allies at Non-Aggression or better counted as ours; troops per unit.
28. **Happiness facilities.** *Settled* (confirmed: binary, §1.5, §4): the planet version
    is summed and added (positive angers) inside the clamp; the system version is the best
    and subtracts that many points after the update.
29. **Queue leftovers and repeat.** *Settled* (confirmed: binary, §6.3): no leftovers,
    one completion per turn, all-or-nothing payment.
30. **Unit overflow.** *Settled* (confirmed: binary, §6.5): same sector only, one unit at a
    time; a unit without room is dropped with a message.
31. **Planet drift cadence.** *Settled* (confirmed: binary, §1.5): every 10th turn;
    planet facility changes are summed, system ones take the best; in finite games a value
    change is a percentage of the stock.
32. **Remote mining.** *Settled* (confirmed: binary, §5.3): the first miner in each
    sector, all minable objects there, `round(Val1 × value %)`, and 1 point of value per
    turn.
33. **Overcrowding.** *Settled* (confirmed: binary, §2): nothing removes population above
    the maximum, whether a dome went up or not. The colony just has no room to grow or take
    in people until it falls below.
34. **Rebellion.** *Settled* (confirmed: binary, §4): no riot counter; see answer 10.
35. **Plague.** *Settled* (confirmed: binary, §3): see answer 20.
36. **Maintenance victims.** *Settled* (confirmed: binary, §7): `unpaid div amount + 1`
    whole vehicles or unit groups, preferring ships out of supply.
37. **Opening pools.** *Settled* (confirmed: binary, §9, spec 05 §1.1): each pool starts
    at Starting Resources plus one turn of the empire's production (colony output as
    delivered, with the minimum-generation rule), and the intelligence pool at 0. Neither
    part is multiplied by the computer bonus, and remote mining, `Generate Points`, trade
    and tariffs are left out. OpenSE4 matches (`research::openingPools`).
38. **No room at completion.** *Settled* (confirmed: binary, §6.3, §6.5): the facility item
    stays at the top with its progress cleared, as a ship item does at the ship limit, and
    no message is sent. With at least one free slot the whole count is added, even past the
    slots.
39. **Units without room.** *Settled* (confirmed: binary, §6.5): each unit without room is
    lost with its own message. The item stays at the top, with its full count, unless the
    last unit was placed; the units already placed are kept.
40. **Upgrade count.** *Settled* (confirmed: binary, §6.6): the target and the count are
    stored when the item is queued, and both price and completion use them.
41. **Atmosphere counter.** *Settled* (confirmed: binary, §2): the counter moves only on
    turns with a converter and the wrong atmosphere. It is not reset on other turns and
    resumes where it stopped.
42. **Replicant shares.** *Settled* (confirmed: binary, §3): the share is
    `round(P × q)`, ties to even, with `q = P_race / T` first stored as a 64-bit double.
    That differs from exact rational rounding only when the exact product ends in one
    half.
43. **Minimum income.** *Settled* (confirmed: binary, §5.6): every living empire gets it,
    with or without colonies.
44. **Abandoned ships.** *Settled* (confirmed: binary): no happiness event at all. Only a
    ship destroyed by damage logs `Ship Lost in System`, which also feeds `Any Ship Lost`
    (§4).
45. **Troops and strangers.** *Settled* (confirmed: binary, §4): `Enemy Troops on Planet`
    counts once while another empire's landed troops still contest the planet;
    `Our Troops on Planet` counts every troop unit in the colony's cargo, whoever owns it;
    ships without an owner never count.
46. **Conditions in the engine.** *Settled* (confirmed: binary, §2): the original keeps
    conditions as a real number (a 64-bit double) and multiplies it without rounding. The
    engine keeps that double too (`src/game/conditions.hpp`); for the band edges see 51.
47. **Bonus above High.** *Closed:* the original offers only None, Low, Medium and High,
    so no value above High can occur. Treating an out-of-range value as High is our own
    input handling (an OpenSE4 extension); the engine's choice stands.
48. **Turn number.** *Settled* (confirmed: binary): both tests use the game date in tenths
    of a year (2400.0 = 24000). "Every 10th turn" is `date mod 10 = 0`, and growth happens
    when `date mod Reproduction Check Frequency = 0`. In a simultaneous game the date has
    already advanced when an empire's turn is processed (the first processed turn is
    24001). In a turn-based game the date advances after the last player, so the first
    round is processed at 24000 and gets the every-10th-turn effects. Testing the turn
    number (the date minus 24000) instead would differ for frequencies that do not divide
    24000, such as 7 or 9.
49. **Cargo and facilities over capacity.** *Settled* (confirmed: binary, §2): facilities
    above the slots and population above the maximum are never removed. Cargo above the
    capacity stays until the planet next takes damage or loses population to plague;
    then cargo population, then units from the first stack, are removed until it fits.
    See 54 for the details the engine chose.
50. **Minister style at setup.** *Settled* (confirmed: binary, §10): the style starts empty
    (the race's own files) and can be left empty; "Use Race Minister Style" starts
    unticked. Our setup matches.

Items 51 onward are the engine's own choices where the rules above are silent. Each is
marked (inferred) in `src/game` and waits for an answer from the executable or the
running game.

51. **Band edges.** *Open* (inferred, §2): the stored double is compared with each edge
    (0.3, 0.5, 1.0, 1.3, 1.5) as an x87 constant, as a Delphi literal would be. So a
    double just below an edge is in the band below it: the double nearest 0.3 (an
    asteroid field's 0.6 / 2, or 0.5 lowered by an event of −0.2) is Deadly, while the
    double nearest 1.3 lies above 1.3 and is Good. If the original compares with double
    constants, such values fall in the band above.
52. **Where built units go.** *Open* (inferred, §6.5): after the builder, the engine
    tries the empire's other planets in the sector in object order, then its ships and
    bases in vehicle order. The original takes "the game's object order, planets and ships
    mixed"; the engine has no order that mixes the two. Which order is it: creation order,
    or the order of the system's object list, where a ship that arrives is appended?
53. **Removing items a queue cannot build.** *Open* (inferred, §6.1): the engine removes
    them at the start of every queue's turn, also when the queue is on hold, cloaked or
    without population, or its colony is rioting. The processing order of §6.3 is decided
    by each queue's top item before the removal.
54. **Trimming cargo.** *Open* (inferred, §2): population held as cargo is removed from
    the first group in list order. "Takes damage" is a hit in space combat that gets past
    the planet's shields, of any damage type; the engine trims nowhere else (events,
    intelligence sabotage). Plague trims after its loss when the colony survives.
55. **Experience from kills.** *Open* (inferred, §9): a destroyed unit group gives the
    tonnage of every unit it had in the battle (killed ones and units that joined it
    included), the value that credits enemy tonnage (spec 04 §15); units lost by a group
    that survives give nothing, nor do mines outside a battle, units stored on planets or
    ground combat.
56. **Upgrade target at queue time.** *OpenSE4 choice* (§6.6): the original's queue does
    no tech check when an upgrade is queued, and its Upgrades tab offers only researched
    targets. OpenSE4's command check also refuses an unresearched target, since commands
    can come from any client. No difference in play is expected.

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

**Units**
- Population is counted in millions (M).
- Cargo is counted in kT.
- Happiness is stored as *anger*, in tenths of a percent (0–1000).
- One turn is 0.1 year, and the game starts at 2400.0. Data text that says "per year"
  means per 10 turns.

Turn resolution uses integer math: multiply, then divide, then truncate. Rounding points
that are not yet verified are listed in §13.

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
- `Minimum Empire X Generation` (200): floor on per-turn income for each resource (§13).
- `Minimum Empire Point Storage` (50000): base storage cap for each resource.
- `Scrap Facility/Unit/Ship Percent Returned` (30): scrap refunds.
- `UnMothball Ship Percent Cost` (20): cost to reactivate a mothballed ship.
- `Upgrade Facility Cost Percent` (50): upgrade pricing (§6.6).

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
  they change.

**Maintenance and population**
- `Empire Starting Percent Maint Cost` (25): base maintenance, in % of cost per turn.
- `Maintenance Cost Amt Per Dead` (20000): unpaid maintenance that destroys one vehicle.
- `Empire Starting Percent Reproduction` (10): base population growth, in % per year.
- `Reproduction Check Frequency` (1): growth runs every N turns [H].
- `Population Mass` (5): kT of cargo per 1M of population [H].
- `Damage Points To Kill One Population` (10).
- `Defending Units Per Population` (20), `Population Defender Attack Strength` (10) and
  `Population Defender Hit Points` (30): militia in ground combat.
- `Automatic Colonization Population` (0): population added to a new colony [I].
- `Maximum Population For Abandon Planet Order` (50).
- `Population Required to Operate One Facility` (50): see §5.1 and §13.
- `Planet Value Percent Loss After Owner Death` (10): see §13.

**Construction**
- `Empire Base Planet X Usage Rate` (2000): queue rate when there is no space yard [I].
- `Maximum Emergency Build Turns` (10).
- `Construction Queue Emergency Build Rate Percent` (150) and `… Slow Build Rate Percent`
  (25). These replace the manual's "double" and "half" [H].

**Tables**
- `Number Of Population Modifiers` gives the row count. Each row has `Pop Modifier N
  Population Amount`, `… Production Modifier Percent` and `… SY Rate Modifier Percent`
  (§5.2).
- `Characteristic <Name> Max Pct|Min Pct|Pct Cost|Threshold|Threshhold Pct Cost Pos|Neg`
  (§8.1). Match the misspelling `Threshhold` exactly.
- `Mood Riot|Angry|Unhappy|Indifferent|Happy|Jubilant Modifier` (0/80/90/100/110/120): the
  production % for each mood band.

**Other**
- `Home System Percentage Value With No Spaceport` (25): see §5.6.
- `Remote Mining Decreases Asteroid Value` (True).
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
are the exception and keep half. Asteroid rows also have capacities, although the manual
says asteroids cannot be colonized (§13).

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

"Sys" means every planet the owner has in that system. Where the data says "only 1
effective per planet/system", take the single **highest** value in that scope [I].

| Ability type | Val1 / Val2 meaning | Stacking |
|---|---|---|
| `Resource Generation - Minerals/Organics/Radioactives` | Base output per turn. Scaled by §5.1. | Additive |
| `Point Generation - Research/Intelligence` | Base points per turn. Planet value is not applied. | Additive |
| `Resource Gen Modifier Planet - X`, `… System - X` | ±% to that resource's output. | Best per planet or sys |
| `Planet/System Point Generation Modifier - X` | The same, for research or intelligence. | Best per scope |
| `Solar Resource Generation - X` | Output × the number of stars in the system. No race, mood, population or value modifiers, and no depletion [H]. | Additive [I] |
| `Generate Points <Minerals…Intelligence>` | Flat points per turn from any object. No modifiers, and no planet is depleted [H]. | Additive |
| `Spaceport` | Delivers the system's output (§5.6). | Flag |
| `Palace` | Defined, but no stock item uses it. Treat it as the homeworld marker [I]. | — |
| `Resource Storage - Mineral/Organics/Radioactives` | Adds to the empire's storage cap. | Additive |
| `Cargo Storage` | Extra cargo kT on the planet. | Additive |
| `Space Yard` | Val1 is the resource (1 = minerals, 2 = organics, 3 = radioactives), Val2 is the rate. A yard has three entries. Enables building ships. | One yard per planet [H] |
| `Component Repair` | Components repaired per turn. | — |
| `Supply Generation` | Unlimited resupply in the sector. | — |
| `Planet - Change Minerals/Organics/Radioactives Value`, `Planet - Change Conditions` | % change. The header says per turn; the facility text says per year. | [I] |
| `Planet Value Change - System`, `Planet Conditions Change - System` | The same, for every planet in sys, per year. | Best per sys |
| `Planet - Change Atmosphere` | After this many turns, the atmosphere becomes the one the majority race breathes. | — |
| `Planet - Change Population Happiness`, `Change Population Happiness - System` | Happiness improves by this % each turn. | Best per sys |
| `Modify Reproduction - System` | +% to reproduction. | Best per sys |
| `Change Population - System` | +M per turn to each colony in sys, split across its races in proportion to their size [H]. | Best per sys |
| `Plague Prevention - System` | Blocks plagues up to this level. | Best per sys |
| `Resource Conversion` | % of material lost when converting. | Best per sys |
| `Resource Reclamation` | % of cost refunded when anything is scrapped in the sector. Replaces the default [I]. | Best per planet |
| `Reduced Maintenance Cost - System` | % cut to maintenance in sys. A negative value raises it. | Best per sys |
| `Modified Maintenance Cost` (component) | % change on a design. The header's example contradicts its own sign convention (§13). | Per design |
| `Change Bad Event/Intelligence Chance - System` | ±% chance. | Best per sys |

Combat, sensor and shield facility abilities are covered in other specs.

### 1.6 RacialTraits.txt [D]

**Fields**
- `Name`, `Description`, `Pic Num`.
- `General Type`: all stock traits are `Advantage`. The engine should also allow
  disadvantages, meaning a negative cost [I].
- `Cost`: in racial points.
- `Trait Type` and `Value 1`/`Value 2`.
- `Required Trait 1..3` and `Restricted Trait 1..3`: trait names or `None`. These are
  prerequisites and exclusions.

**`Trait Type` values** (the meaning of Val1 follows each type)
- `Supply Cost`: % change to supply use.
- `No Plagues`.
- `Luck`: % change to the chance of bad events.
- `No Spaceports`.
- `Vehicle Speed`: extra movement points.
- `Galaxy Seen`.
- `Planet Storage Space`: % more planet capacity (§13).
- `Planetary SY Rate`: % bonus to *planet* space yards only.
- `Tech Area`: unlocks the TechArea records whose `Racial Area` equals Val1.
- `Population Emotionless`.

Two examples: an advantage costing 1000 points that removes the spaceport requirement,
and a 1500-point advantage that unlocks a racial tech tree.

### 1.7 Cultures.txt [D]

**Fields:** `Name`, `Description`, then ten signed percentages:
- `Production` (all three resources), `Research`, `Intelligence`, `Trade`;
- `Space Combat`, `Ground Combat`, `Happiness`, `Maintenance`, `SY Rate`, `Repair`.

A positive value always helps the empire. For `Maintenance` it lowers costs; for
`Happiness` it makes populations happier.

Examples: one warlike culture gives about +10 % to combat, with small penalties to
economy and research. A neutral culture has every field at 0.

### 1.8 Happiness.txt [D]

**Mood bands.** The header maps anger (in tenths of a percent) to a mood:

| Anger | Mood |
|---|---|
| ≥ 750 | Rioting |
| 600–749 | Angry |
| 450–599 | Unhappy |
| 300–449 | Indifferent |
| 150–299 | Happy |
| < 150 | Jubilant |

The header also lists output multipliers. They differ from the Settings `Mood …` keys, and
Settings should win (§13).

**Records.** Each record is one racial *happiness type*, chosen at setup. Every value is a
change to anger in tenths of a percent: positive means angrier.
- `Max Positive/Negative Anger Change`: the clamp on one planet's total change per turn.
  The units are tenths [H].
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
- Drift: `Natural Decrease` applies to the owner's race. `Natural Decrease for Other
  Races` applies to foreign races.

Examples: a peaceful type gets 10.0 % angrier when war is declared, while a bloodthirsty
type gets 10.0 % calmer. Both drift 2.0 % calmer each turn.

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
- experience and race age;
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

Priced with §8.1, the tiers of most races add up to exactly 2000, 3000 and 5000 points.
Trim values, since some have trailing spaces.

---

## 2. Planets and colonies

**Planet attributes**
- Name: defaults to "<system> <roman numeral>", and the owner can rename it.
- Type: Rock, Ice or Gas Giant.
- Size.
- Atmosphere: None, Methane, Oxygen, Hydrogen or CO₂. A Gas Giant never has None.
- Conditions: a percentage shown as a band from Deadly to Pleasant [M/I].
- Three resource values, a description, and special flags such as ruins.

**Colony attributes**
- Owner and colony type.
- Population per race.
- Anger (see §13 for whether it is per planet or per race).
- Facilities and cargo.
- Queue state and the minister flag.
- Plague level, and orders (in simultaneous games).

**Colonization** [M/H]
- A ship with `Colonize Planet - <Type>` settles a planet of that type. The ship is broken
  up, and its cargo and population land on the planet.
- A colony with zero population is created, but it cannot build anything. If an empire's
  last colony has no population, the empire dies.
- Game options can limit colonization to breathable atmospheres, or to the home planet
  type. Otherwise tech decides which types are allowed. The colonization tech levels at
  game start depend on the home planet type [D].
- A new colony starts in the Happy band. Recolonizing an abandoned planet does not
  inherit the old anger.

**Domes.** A colony is domed if any race on it cannot breathe the atmosphere. A domed
colony uses the `… Domed` capacities [M/D]. Edge cases:
- If a non-breathing race arrives (by transfer or capture), capacities shrink.
  - Existing facilities stay, but no new ones can be built.
  - What happens to surplus population and cargo is open (§13).
- When an atmosphere converter's countdown ends, it sets the atmosphere the majority race
  breathes. This can remove the dome.

**Capacities**
- Facility slots and maximum population come from PlanetSize, using the normal or domed
  column.
- Cargo is PlanetSize cargo plus `Cargo Storage`, possibly plus the storage trait (§13).

**Value**
- **Normal game:** each resource has a percentage that multiplies output and never runs
  out.
  - The starting value is random within the Settings range.
  - Homeworlds use the setup choice instead.
  - Changes are clamped to the Min/Max percent keys.
- **Finite Resources game:** the value is the absolute stock left.
  - Production draws it down, and a resource at 0 produces nothing.
  - Solar output ignores the stock [H].

**Conditions** [M]
- Worse conditions raise anger and slow reproduction. Environmental Resistance softens
  both effects.
- Climate facilities and events change conditions. For example, one stock event lowers
  them by a few points.

**Blockade** [M/H]
- A planet is blockaded while:
  - enemy ships are in its sector; mothballed ships do not count, and cloaked ships most
    likely do not either [I];
  - or enemy troops are on the planet.
- A blockaded planet delivers nothing, and that output is lost.

---

## 3. Population

Population is stored per race. The planet's maximum caps the total across all races.
Growth, drift and replicant additions apply to each race separately.

**Growth** [H/D; the form is I]
- Empire rate: `R_emp = Starting Percent Reproduction + (Reproduction char − 100)`.
  - The release notes say this characteristic is a "true percent" added to the base [H].
  - Its range is 91–130, which gives 1–40 % per year.
- Planet rate: `R = R_emp + best Modify Reproduction in sys + moodRepro − condPenalty`.
  - `moodRepro` runs from −5 when rioting to +5 when jubilant [H].
  - `condPenalty` is unknown (§13).
- Every `freq` turns, a race with population P gains `P × R × freq / 1000` M (a year is 10
  turns). The result is capped at the planet maximum. Fractions below 1M need rules (§13).
- Replicant facilities add Val1 M per turn, split proportionally across the races present
  [H].

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

**Plague** has levels.
- A Medical Lab blocks plagues up to its level in its system.
- A Medical Bay cures plagues on your own planets and on allies' planets (Military
  Alliance or better) [H].
- The `No Plagues` trait grants immunity.
- The population loss from plague is unknown (§13).

---

## 4. Happiness

Each turn, for each colony [D/I]:
```
Δ = Σ event deltas (queued this turn, by scope)
  + Σ presence deltas (ships in system/sector, troops on planet)
  + NaturalDecrease (owner race) | NaturalDecreaseOtherRaces (foreign races)
  + conditions term (worse → angrier, reduced by Environmental Resistance)   [M/I]
  − best "Change Population Happiness" facility effect in sys/planet          [I: units]
calming part of Δ scaled by Happiness characteristic and culture Happiness   [M/I]
Δ = clamp(Δ, MaxNegativeAngerChange, MaxPositiveAngerChange)
anger = clamp(anger + Δ, 0, 1000)
```

**Event scopes** (§1.8)
- Empire-wide events hit every colony.
- System events hit the empire's colonies in that system.
- Location events hit only the colony in that sector.

**Rules from the release notes** [H]
- Mothballed ships never affect happiness.
- Cloaked ships do not trigger unhappiness.
- Our own troops on our own planet reduce anger.

**Effects**
- Output is multiplied by the band's `Mood … Modifier`.
- Reproduction shifts by −5 to +5.
- **Riot** [D/M]:
  - The planet produces no points and builds nothing.
  - Troops on the planet or ships in orbit calm it.
- **Rebellion** [D/H]: a planet that riots for long enough may rebel.
  - It changes owner, possibly to a newly founded AI empire. The new empire starts with a
    clean log and no contacts.
  - The trigger and the odds are unknown (§13).
- **Emotionless** trait: anger never changes, and neither intelligence operations nor
  events can cause riots [H].

---

## 5. Production and income

### 5.1 Planet output (per resource r) [form I; parts D/M/H]
```
if rioting: 0
base   = Σ facility Resource Generation - r Val1   (research/intel: Point Generation)
mod    = (popProd − 100) + (moodMod − 100) + (aptitude_r − 100) + culture_r
       + bestPlanetMod_r + bestSystemMod_r
mod    = max(mod, −100)                         // no negative totals [H]
out    = base × value%_r / 100 × (100 + mod) / 100   // value% omitted for research/intel
```

**Terms**
- `aptitude_r` is Mining, Farming or Refining for the three resources. Research uses
  Intelligence, and intelligence points use Cunning.
- `culture_r` is culture Production, Research or Intelligence.

**Open points**
- Whether the terms add or multiply is unconfirmed. We default to adding, because the
  Planet Report's Ability tab lists the population, mood and racial terms as separate
  percentage abilities.
- `Population Required to Operate One Facility` may limit active facilities to
  `floor(pop / 50)` (§13).

Solar output (`Val1 × stars`) and `Generate Points` are added afterwards, without
modifiers [H].

### 5.2 Population brackets

- Use the first Settings row whose `Population Amount` is at least the colony's total
  population. That row gives both the production % and the SY rate %.
- The stock table runs from 100 % (up to 19M), gaining about 1 % per row. Rows are 20M
  wide at first and 200M wide later, and the table tops out at 200 % above about 10,000M
  [D/H].
- The row count comes from Settings. It is not limited to 20 [H].

### 5.3 Finite resources

- Subtract each turn's `out` from the planet's stock, with a floor of 0 (the formula is
  in §13).
- Remote mining lowers asteroid value when the setting is on. Looking at the Empire
  Status window must not change it (a bug fixed in the release notes) [H].

### 5.4 Other income

- **Remote mining:** comes from ships (see the ships spec).
- **Trade** needs a Trade Alliance or better [M].
  - Resources are traded at Trade Alliance, research at Trade & Research, intelligence at
    Partnership.
  - Trade starts at 1 % of the partner's output and grows 1 % per turn, up to `Maximum
    Trade Percentage`.
  - Political Savvy and culture Trade modify it [I].
  - Switching between treaties of Trade Alliance or better keeps the current percentage
    [H]. Breaking the treaty resets it.
- **Tariffs:** a subjugated empire pays 40 % of its resources, a protectorate 20 %.
  - A tariff can never exceed what the payer holds.
  - What the receiver gets is capped by its storage [H].
- Research and intelligence points go to their own specs. They are not stored against
  resource caps [I].

### 5.5 Delivery [M/H]

A planet's output reaches the treasury only if all of these hold:
- the empire has a `Spaceport` in that system, or has the `No Spaceports` trait;
- the planet is not blockaded;
- the planet is not rioting.

Undelivered output is lost, not banked. The UI shows it in parentheses and adds a "No
Spaceport" icon. `Home System Percentage Value With No Spaceport` hints that the home
system delivers 25 % even without a spaceport (§13).

### 5.6 Treasury and conversion

- Minerals, organics and radioactives are each stored separately. The cap for each is
  `Minimum Empire Point Storage + Σ Resource Storage` of that resource. The excess is lost
  [M]; the timing is in §13.
- `Minimum Empire X Generation` sets a floor on income (§13).
- Empire Status shows these sections:
  - **Income:** Colonies, Trade, Tariffs, Remote Mining, Total.
  - **Expenses:** Tariffs, Maintenance, Construction Queue Usage, Total.
  - **Net per turn**, which may be negative.
  - **Treasury:** the current amount and the maximum.
- **Convert Resources** order (needs `Resource Conversion` on the planet): the player
  spends X of one resource and receives `X × (100 − Val1) / 100` of another. Only one
  converter counts per system, and conversion is instant [M/H].

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
- Scrapping a planet's space yard removes the ships from its queue.

### 6.2 Rate (per resource r)

```
baseRate = SpaceYard Val2 for r  |  Empire Base Planet r Usage Rate (planets without yard) [I]
rate = baseRate × (popSY + (ConstructionApt − 100) + cultureSYRate
                   + (planet ? PlanetarySYRate trait : 0)) / 100          [additive: I]
rate = rate × {100 normal | 150 emergency | 25 slow} / 100               [D/H]
```

### 6.3 Processing

**Each turn** [M/I]
- Skip a queue that is on hold, or whose planet is rioting.
- Otherwise the top item receives `min(rate_r, remaining_r, treasury_r)` of each
  resource, and that progress is kept.
- The item completes when every remaining cost reaches 0. The UI estimates the build time
  as `max_r ceil(cost_r / rate_r)`; for example, 6000 at a rate of 2000 takes 3 turns.

**Shortages** [H]
- A queue that is short of resources logs a message.
- Which queue gets paid first is unknown, and so is whether leftover rate flows to the
  next item (§13).

**Editing the queue** [H]
- Deleting the item in progress discards its progress.
- Reordering resets its timer.
- Removing the top item asks for confirmation.

### 6.4 Modes

**Emergency build** runs at 150 % for up to 10 turns in a row. When it is switched off,
or the cap is reached, the queue runs in **slow mode** at 25 % for as many turns as the
emergency lasted, with a minimum of 1 [H].
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

**Ships**
- A ship appears at the queue's location when finished. It is invisible while being built.

**Facilities**
- Only as many facilities can be queued as there are free slots, counting ones already
  queued. The game warns when this is exceeded.
- Adding a system-wide facility that is already present in the system shows a notice [H].
- A finished facility fires `Facility Constructed`.

**Units**
- The player picks how many to build.
- Units go to the builder's cargo, or overflow into other cargo the empire owns.
- If there is no room, the unit is not built and a log message is sent [M].
- Player caps on ship and unit counts apply.

### 6.6 Upgrades and scrapping

**Upgrades**
- An upgrade item targets one facility family on one planet. Its cost is
  `Upgrade % × cost of the newest level × number of older facilities` [D/M]. On
  completion, all of those facilities become the newest level.
- Whether a single facility can be targeted is open; a v1.91 fix hints at this (§13).
- The "Upgrade Facilities" button queues every possible upgrade across the empire.

**Scrapping**
- Facilities can be scrapped on one planet, or one type everywhere.
- The refund is `Scrap Facility Percent Returned`, or the best `Resource Reclamation` % in
  the sector. It is paid immediately [I].

---

## 7. Maintenance [M/D/H]

**Rate**
- `maint% = Starting Percent Maint Cost − (Maintenance Aptitude − 100) − culture
  Maintenance`, with a floor of 5 % [H].
- The aptitude is a "true percent" (range 80–120) [H].
- The manual's 30 % figure is out of date.

**Per vehicle and resource**
- The charge is `cost_r × maint% / 100 × (100 − best sys reduction − design mod) / 100`.
- Bases pay half [H].
- Units are charged too: unpaid maintenance scuttles units [H].

**Mothballing**
- A mothballed ship costs nothing, has no abilities, adds nothing to score, and does not
  blockade.
- Mothballing, unmothballing, scrapping and analysing all need a space yard at the
  location.
- Unmothballing costs `UnMothball Ship Percent Cost` of the design's cost.

**Payment**
- Maintenance is paid before anything else [M].
- On a shortfall, `ceil(unpaid / Maintenance Cost Amt Per Dead)` vehicles are destroyed.
- How the victims are chosen, and whether shortfalls are summed across resources, is
  open (§13).

---

## 8. Racial characteristics, traits, cultures

### 8.1 Characteristics and costs

There are 15 characteristics, all integer percents defaulting to 100 [D]:
- Physical Strength, Intelligence, Cunning, Environmental Resistance, Reproduction;
- Happiness, Aggressiveness, Defensiveness, Political Savvy;
- Mining, Farming, Refining, Construction, Repair and Maintenance Aptitude.

Each is clamped to its `Min/Max Pct`. With `d = v − 100`, `c = Pct Cost` and
`T = Threshold`:
```
d > 0: cost   = c·min(d,T)  + c·Pos/100·max(0, d−T)
d < 0: refund = c·min(−d,T) + c·Neg/100·max(0, −d−T)
```
- **[I]** This reading of the two `Threshhold Pct Cost` keys, as percentages of `c`,
  matches every stock race preset exactly. Other readings do not.
- **Racial points spent** = Σ costs − Σ refunds + Σ trait costs. An empire cannot be
  created if this exceeds the game's racial point setting [M].

**Effects** [M]. Unless noted, `(v − 100)` is used as a percentage modifier.
- Physical Strength: ground combat.
- Intelligence: research.
- Cunning: intelligence points.
- Environmental Resistance: softens the effect of bad conditions on anger and growth.
- Reproduction: added to the growth rate (§3).
- Happiness: how fast anger falls.
- Aggressiveness and Defensiveness: to-hit modifiers.
- Political Savvy: trade.
- Mining, Farming and Refining: output of that resource.
- Construction: space yard rate.
- Repair: repair rate.
- Maintenance: subtracted from the maintenance rate (§7).

Racial combat bonuses also apply to fighter groups [H].

### 8.2 Traits and cultures

- An empire has one culture and any set of traits that meets the Required and Restricted
  rules.
- Engine effects are keyed on `Trait Type`, never on the trait's name.
- Culture fields are added as percentage points to the matching term in §§4–7 [I].

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

**Player settings**
- Starting resources.
- Home Planet Value: Low, Medium or High (see the `Plr Planet Value` keys).
- Number of starting planets. They are placed in nearby systems.
- An option to give everyone a homeworld of the same size [H].
- Placement: whether players may share a system, or are spread evenly.
- Starting tech level: Low, Medium or High.
- Racial points.
- Score visibility.
- Random computer players, either normal or neutral. Neutral empires stay in their home
  system.

**Relevant game options**
- Finite resources.
- Colonize only breathable planets.
- Colonize only the home planet type.
- Event frequency and severity.
- Allow intelligence.

Quick Start picks from the Settings style list and hands out premade designs [H].

**Homeworld**
- A homeworld's colony type is fixed [H].
- Its starting population, facilities and stock are unknown (§13).

---

## 10. Ministers and colony types [M/H]

**Global ministers**
- Design and Ship Construction.
- Expenses.
- Production Output: emergency build, keeping planets happy.
- Research, Intelligence and Politics.
- Repair, Resupply, Scrap and Retrofit.

A global minister takes full control of its area and can undo the player's choices.

**Individual ministers**
- Facility Construction.
- Transports, Carriers and Colonization.
- Attack, Defense, Exploration and Patrol.
- Mines/Satellites, Fleets, Stellar Manipulation and Ship Cloaking.
- Space Yard Ships and Troops.

These act only on objects whose minister flag is on.

**Minister options**
- "Automatically use Individual Ministers for newly built vehicles" also covers new
  colonies.
- A separate option stops the AI from making changes when it covers a missed turn in a
  simultaneous game.
- Bulk buttons: all or none, individual on or off, and full AI on or off.

**Minister style**
- The style is a personality set taken from `Ai/<Aggressive|Defensive|Neutral>/`.
- With "Use Race Minister Style", the race's own AI files are used instead. These files
  are covered in the AI spec.

**Colony types**
- A colony type is a label chosen at colonization or later. The defaults come from
  `DefaultColonyTypes.txt`.
- Ministers and the AI use the colony type to choose which facilities to build.
- The Population Transport minister moves people from planets over 1000M to planets under
  500M that are not full.

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

## 12. Proposed economy phase order [I]

1. Planet changes: atmosphere timers, value and condition drift, plague. Yearly effects
   run when `turn % 10 == 0`.
2. Happiness update, using the events queued so far this turn.
3. Output of each planet, including finite-resource depletion.
4. Delivery to the treasury.
5. Other income: trade, tariffs received, remote mining, solar, and Generate Points.
6. Apply the income floor.
7. Maintenance, then the shortfall penalty.
8. Tariffs paid.
9. Construction and completion events.
10. Storage cap.
11. Population growth (when `turn % freq == 0`), then replicants.
12. Emergency and slow mode counters.

Each step iterates in stable ID order.

---

## 13. Open questions to verify in the running game

1. **Modifier combination.** Are the population, mood, aptitude, culture and facility
   percentages added (the §5.1 default) or multiplied? Is rounding done per planet or per
   facility?
2. **Mood multipliers.** Settings gives 80–120; the Happiness.txt header gives 60–140.
   Does Settings win?
3. **Facility staffing.** Is `Population Required to Operate One Facility` enforced? For
   example, does a 10M colony with 3 miners produce anything?
4. **Queue rate.**
   - Does a planet with no yard use 2000 per resource?
   - Do population SY, Construction Aptitude, culture SY Rate and Hardy Industrialists add
     or multiply?
   - Does aptitude affect ship-mounted yards?
5. **Multiple completions.** Can one queue finish several items per turn? Which queue is
   paid first when resources are short?
6. **Build modes.** Confirm emergency = 150 % and slow = 25 %. The manual says 200 % and
   50 %.
7. **Growth.**
   - What is the exact formula and rounding? How is growth below 1M/turn handled?
   - How much do conditions slow growth, and how much does Environmental Resistance
     offset it?
   - Is growth applied every turn or yearly?
8. **Conditions.** What are the band names and % edges, and how much anger does each band
   add?
9. **Anger details.**
   - Do presence deltas apply per ship or unit, or once?
   - Is a facility's "1 %" happiness change 10 tenths?
   - What exact anger does a new colony start with?
   - Is anger stored per planet or per race?
10. **Rebellion.** How long must a planet riot before it can rebel, and with what chance?
    Who gets the planet?
11. **Income floor.** Does `Minimum Empire X Generation` apply to gross or net income? Does
    it apply to an empire with no planets?
12. **Home system.** Does the home system deliver 25 % of its output without a spaceport?
13. **Storage timing.** Is the cap applied before or after spending? Are scrap refunds
    capped?
14. **Maintenance.**
    - Is the shortfall computed per resource or summed?
    - How are the scuttled vehicles chosen?
    - Do units stored in cargo pay?
    - What is the sign convention of `Modified Maintenance Cost`?
    - How does culture Maintenance combine with the aptitude?
15. **Storage trait.** Does `Planet Storage Space` (+20 %) apply to cargo, resource storage,
    or both?
16. **Finite games.**
    - How much stock is used per turn, and does value % still apply?
    - What does `Planet Value Percent Loss After Owner Death` do?
17. **Asteroids.** Can asteroids ever be colonized, given that PlanetSize has asteroid
    rows?
18. **Upgrades.** Can a single facility be upgraded, or only the whole family on a planet?
19. **Unit overflow.** Where can overflowing units go: the same sector, the same system, or
    anywhere?
20. **Plague.** How much population does each plague level kill per turn, and how does a
    plague progress?
21. **Homeworld start.** What population, facilities, stock and value does a homeworld
    start with for each setup choice?
22. **Setup ranges.** What are the actual racial-point options (likely 2000, 3000 and
    5000) and starting-resource options?
23. **Experience and race age.** What gameplay effect, if any, do they have?

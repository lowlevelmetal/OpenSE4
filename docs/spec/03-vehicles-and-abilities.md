# Spec 03: Vehicles, Designs, Abilities, Movement and Logistics

This is a clean-room implementation spec for the SE4 vehicle model: hulls, components, the design
rules, the ability system, movement, supply, orders, fleets, formations, cargo, unit launch and
recovery, repair, retrofit, scrapping and ship reports. Sources are the SE4 Deluxe manual and the
headers and field structure of the stock `Data/*.txt` files. All wording here is original. Field
names, ability identifiers, enum values and UI labels are quoted verbatim because modded data and
saved content depend on them.

Conventions:

- **MUST / SHOULD** are normative. Items tagged **[VERIFY]** are inferences, or places where the
  sources contradict each other. They are collected in §19.
- A component is either **working** or **destroyed**; the original keeps no partial damage per
  component (§5). Destroyed components provide no abilities.
- Counts and sums are integers. Most percentages in the original are applied in floating point:
  it first computes `pct / 100` in x87 extended precision (64-bit mantissa, rounding to nearest
  with ties to even; the game sets this at start-up, in every thread it starts and after every
  exception it handles, and never changes it otherwise, and the setting was measured in the
  running game, §6.3), multiplies the value
  by that, and then converts to an integer. Each rule says which conversion is used:
  **round** (to nearest, halves to even) or **truncate** (toward zero). Because `pct / 100` is
  inexact for most `pct`, a truncated result can be one less than the exact `value × pct / 100`
  when that is a whole number (for example 300 × 21 % gives 62, not 63). An implementation MUST
  reproduce this with an extended-precision (or exactly emulated) multiply (confirmed: binary).
  Where a rule gives plain integer arithmetic, `/` is integer division truncating toward zero.
- `Settings[...]` means a key in `Settings.txt`. The stock value follows in parentheses.

---

## 1. Vehicle classes

Every vehicle is an instance of a **design**. A design is a **vehicle size** (hull) plus an ordered
list of components. The hull's `Vehicle Type` decides the class:

| Class | Moves itself | Warp | Carries cargo | How it enters space | Recoverable | Fuel exhausted |
|---|---|---|---|---|---|---|
| Ship | yes | yes | yes | built at a space yard | n/a | at most 1 MP/turn, crippled (§6.1, §7) |
| Base | no (0 MP) | no | yes | built at a space yard | n/a | never: a base's supply is unlimited (§7) (confirmed: binary) |
| Fighter | yes | no (confirmed: binary) | no | launched from a ship bay or a planet | yes | not destroyed: the group moves at most 1 MP (confirmed: binary) |
| Satellite | no | no | no | deployed from a ship bay or a planet | yes | never: satellites have no supply at all (confirmed: binary) |
| Mine | no | no | no | laid by a mine layer or a planet | no | never: mines have no supply at all (confirmed: binary) |
| Drone | yes (by orders, §12) | yes (confirmed: binary) | no: unit groups in space hold no cargo (confirmed: binary) | launched from a launcher or a planet | no | destroyed (confirmed: binary) |
| Troop | no | no | no | dropped on a planet during combat only | n/a | n/a |
| Weapon Platform | no | no | no | moved to a planet as cargo, or built there | n/a | n/a |

Everything except Ship and Base is a **unit**. Units are built into cargo: the builder's own cargo
if a unit fits there, otherwise the first other object of the same empire in the builder's
**sector** (in the system's object order) that can hold cargo and has room for it: a colonized
planet's cargo or a ship's or base's cargo. Nothing further away is used. If no space exists,
the unit is not built and a log message is produced (confirmed: binary). Units can be moved between ships and planets as cargo.
Troops and weapon platforms are never placed in open space.

Units that are in space are held as **groups**: one per (owner, unit kind, sector), mixing
designs; every drone is its own group (§12) (confirmed: binary).
Per-player caps are `Settings[Maximum Mines Per Player Per Sector]` (100) and
`Settings[Maximum Satellites Per Player Per Sector]` (100). Global counts are capped by the
game's ships-per-player and units-per-player options, whose defaults are
`Default Number Of Ships Per Player` (200) and `Default Number Of Units Per Player` (1000); the
units cap counts only units in space and is checked at launch (§12) (confirmed: binary).

---

## 2. Data files in scope

### 2.1 Common record syntax

- Each file has a free-text header, then `*BEGIN*`. Records are blocks of `Key := Value` lines,
  separated by blank lines, up to `*END*` (or EOF).
- Files use CRLF line endings. Keys are right-padded with spaces, and `Settings.txt` also uses
  tabs. Split on the first `:=` and trim both sides, including trailing spaces inside values
  (for example `Vehicle Type := Satellite ` has a trailing space).
- Indexed groups are introduced by a count: `Number of Abilities := N`, followed by
  `Ability i Type`, `Ability i Descr`, `Ability i Val 1` and `Ability i Val 2` for i = 1..N.
  Tech requirements use the same pattern (`Number of Tech Req`, `Tech Area Req i`,
  `Tech Level Req i`).
- Ability values are not always numeric. `Cloak Level` and `Sensor Level` store a sight-type name
  in `Val 1`. `Val` fields can also be empty. Store values as strings and parse per ability type.
  The original turns the sight-type name into its index 1–5 and reads every other value as an
  integer, with an empty value meaning 0 (confirmed: binary).
- At most 20 abilities are read per record: a larger `Number of Abilities` is treated as 20. An
  ability type name that matches no known type is reported as a data error (confirmed: binary).
- `Descr` strings may contain substitution tokens such as `[%ShieldPointsGenerated]`, which the UI
  fills with the computed value (after mount modifiers).
- The loader must tolerate fields that the header does not document: `Weapon Family`,
  `Weapon Seeker Speed` and `Weapon Seeker Dmg Res`. It must also accept header spelling drift:
  the optional field is spelled `Vechicle List Type Override`, and the records use
  `Weapon Damage At Rng`.

### 2.2 VehicleSize.txt (hulls)

| Field | Meaning |
|---|---|
| `Name`, `Short Name`, `Description`, `Code` | Identity. `Code` is a two-letter abbreviation. |
| `Primary Bitmap Name`, `Alternate Bitmap Name` | Art keys. The race style prefixes them with portrait or mini variants. |
| `Vehicle Type` | `Ship`, `Base`, `Fighter`, `Satellite`, `Mine`, `Troop`, `Weapon Platform` or `Drone`. |
| `Tonnage` | Component space available, in kT. |
| `Cost Minerals/Organics/Radioactives` | Hull cost, which is part of the design cost. |
| `Engines Per Move` | Divisor: the summed `Standard Ship Movement` values are divided by it (integer division) to give base movement. 0 means the hull cannot move (§6.1) (confirmed: binary). |
| `Number of Tech Req`, `Tech Area Req i`, `Tech Level Req i` | Research needed before the hull can be designed. |
| `Number of Abilities` + ability block | Abilities the hull itself grants (§3). |
| `Requirement Must Have Bridge` | True/False. The design needs exactly one `Ship Bridge` component (§4.2). |
| `Requirement Can Have Aux Con` | True/False. When True, the design may have at most one `Ship Auxiliary Control`. When False the count is not checked at all (§4.2). |
| `Requirement Min Life Support` | Minimum count of `Ship Life Support` components. |
| `Requirement Min Crew Quarters` | Minimum count of `Ship Crew Quarters` components. |
| `Requirement Uses Engines` | Whether the hull uses engines. False means no engine may be fitted. |
| `Requirement Max Engines` | Maximum number of engine components. 0 means no limit (when `Uses Engines` is True). |
| `Requirement Pct Fighter Bays` | Minimum % of hull tonnage that must be fighter-bay components. |
| `Requirement Pct Colony Mods` | Minimum % of hull tonnage that must be colony modules. |
| `Requirement Pct Cargo` | Minimum % of hull tonnage that must be cargo components. |

The header also describes unit-only flags, `Launched from Ship` and `Launched from Planet`. No
stock record defines them, and the original never reads them (confirmed: binary). The engine MUST
ignore them.

Vehicle classes are numbered in this order, which some rules below rely on: Ship, Base, Fighter,
Satellite, Mine, Troop, Drone, Weapon Platform (confirmed: binary).

Illustrative stock shapes: small warships get a positive `Combat To Hit Defense Plus` from the
hull. Carriers, colony ships and transports use the 50 % requirements. Bases have
`Engines Per Move 0`, `Max Engines 0`, a negative defense modifier and
`Modified Maintenance Cost -50`. Mine hulls need no bridge and carry hull-level `Cloak Level` in
all five sight types. Drone hulls grant `Extra Movement Generation` with `Val 2 = 999`.

### 2.3 Components.txt

| Field | Meaning |
|---|---|
| `Name`, `Description`, `Pic Num` | Identity and art index. |
| `Tonnage Space Taken` | kT of hull space consumed. |
| `Tonnage Structure` | Damage points the component absorbs before it is destroyed. This can differ from its size (armor is higher, flimsy parts are lower). |
| `Cost Minerals/Organics/Radioactives` | Component cost. |
| `Vehicle Type` | Allowed hull classes, as one token string: `Ship`, `Base`, `Fighter`, `Satellite`, `Mine`, `Troop`, `Drone`, `WeapPlatform`, `Ship\Base`, `Ship\Base\Sat`, `Ship\Base\Sat\Drone`, `Ship\Base\Sat\WeapPlat\Drone`, `Ftr\Trp`, `Ship\Base\Drone` or `All`. Split on `\`. Map `Sat` to Satellite, `Ftr` to Fighter, `Trp` to Troop, and `WeapPlat`/`WeapPlatform` to Weapon Platform. |
| `Vechicle List Type Override` (+ `Vehicle List Type Description`) | Optional. Replaces `Vehicle Type`. The original lower-cases the text and enables each class whose keyword occurs anywhere in it: `ship`, `base`, `fighter`, `satellite`, `mine`, `troop`, `drone`, `weapplatform` (confirmed: binary). |
| `Supply Amount Used` | Supply consumed each time the component is activated. For an engine this is per move, for a weapon per shot, for a one-shot device per use (§7). |
| `Restrictions` | `None`, or `One Per Vehicle` through `Ten Per Vehicle`: the maximum count of this component **family** on one design (§4.2 rule 10). |
| `General Group` | Category used for UI filtering and **repair priority** (§13). Stock groups are Weapons, Sensors, Engines, Stellar Manipulation, Shields, Armor, Unit Launch, Vehicle Control, Remote Mining, Supply, Construction, Cargo, Miscellaneous, Colonizing and Religious. |
| `Family` | Integer lineage id. All numeral versions of one part share it, and different lines may too (all stock large engines share one). It drives "Only Latest", which keeps the last of each run of neighbouring same-family entries in file order (spec 02 §6.4), and design Upgrade, which takes the family's last researched entry in file order (§4.1). Neither looks at the numeral. |
| `Roman Numeral` | 0–20. The version within the family (0 means no numeral). |
| `Custom Group` | Integer tag that `Constructed Planet Requirements` looks up. |
| Tech requirement block | As for hulls. It gates whether the component is visible in the designer. |
| Ability block | §3. |
| `Weapon Type` | `None`, `Direct Fire`, `Seeking`, `Point-Defense` or `Warhead`. If it is not None, the weapon fields follow (`Weapon Target`, `Weapon Damage At Rng`, `Weapon Damage Type`, `Weapon Reload Rate`, display and sound fields, `Weapon Modifier`, and for seekers the speed and damage resistance). The combat spec defines their semantics. |

### 2.4 CompEnhancement.txt (weapon mounts)

Each record is one mount:

- `Long Name` and `Short Name` are display names. `Code` is the letter drawn on the component icon.
- `Cost Percent`, `Tonnage Percent`, `Tonnage Structure Percent`, `Damage Percent`,
  `Supply Percent` and the optional `Shield Percent` are multipliers applied to the matching
  component value (100 means unchanged). A missing `Shield Percent` means 100 (confirmed: binary).
- `Range Modifier` is a signed shift of the damage table. With modifier R, the mounted weapon does
  at range r what the base weapon does at range r−R, so the maximum range grows by R.
  `Weapon To Hit Modifier` is a signed to-hit adjustment.
- `Vehicle Size Minimum` and the optional `Vehicle Size Maximum` bound the hull `Tonnage`
  (inclusive). A missing or 0 maximum means no upper bound (confirmed: binary).
- `Comp Family Requirement` (optional) is a comma-separated list of allowed `Family` ids. An
  empty list allows every family.
- `Weapon Type Requirement` is one of `None`, `Direct Fire`, `Seeking`, `Point-Defense`,
  `Warhead` or `Any`. `None` matches only non-weapons, a specific type matches only that weapon
  type, and `Any` matches **every weapon type but not non-weapons** (confirmed: binary).
- `Vehicle Type` is text. The mount is offered on a hull when the hull's class name (as spelled in
  §2.2) occurs in that text with matching case, or when the text is `Any` in any letter case. The
  original applies this test only when it
  lists mounts to choose from, not in the design validity check (confirmed: binary).
- An optional tech requirement block gates availability.

Stock mounts all require `Direct Fire`. They come in sets for Ship, Base, Satellite and Weapon
Platform hulls. The larger mounts need larger hulls. Base, satellite and platform mounts also add
range and to-hit.

### 2.5 Abilities.txt

This is a catalog, not a record file. Each entry is an ability type name followed by prose for
Value1 and Value2. The engine MUST treat the name as the functional key. §3.3 lists every type.

### 2.6 Formations.txt

The header holds ASCII diagrams of a 19×19 grid. Members 1–9 are shown as digits and 10+ as
letters. The authoritative data is in the records:

- `Name`, `Description`
- `Leader Position Xpos` and `Leader Position Ypos`: grid cell of the leader (1..19).
- `Leader Design Type`: `Any` or a design-type name the leader must have.
- `Number of Positions`, then `Position i Xpos`, `Position i Ypos` and `Position i Type`
  (`Any` or a design-type name) for i = 1..N.

Stock formations have 36–100 positions, all `Any`, with no duplicate cells.

### 2.7 RepairPriorities.txt

This file has no header or records. It is a plain list of `General Group` names, one per line,
giving the **default** repair order (stock order starts with Vehicle Control, then Shields,
Engines, Weapons and Armor).

### 2.8 DefaultDesignTypes.txt

Records with only `Name` hold the initial per-empire list of design types (at most 255), such as
attack ship, colony, transport and carrier roles. Design types are labels only and have no rules
effect, except that formation slots and AI logic can refer to them.

### 2.9 Settings.txt keys owned by this spec

| Key | Stock | Use |
|---|---|---|
| `Scrap Ship Percent Returned` / `Scrap Unit Percent Returned` | 30 / 30 | §15 |
| `UnMothball Ship Percent Cost` | 20 | §15 |
| `Retrofit Cost Percent For Comps` / `... For Comp Removal` | 120 / 30 | §14 |
| `Retrofit Max Percent Difference in Cost` | 50 | §14 |
| `No Retrofit Adding Of Spaceyards` / `No Retrofit Adding Of Colony Module` | True / True | §14 |
| `Empire Starting Percent Maint Cost` | 25 | §16 |
| `Maintenance Cost Amt Per Dead` | 20000 | §16 |
| `Supply Amount for Low Supply Warning` | 1000 | §7 |
| `Fighter Supply Usage Per Turn` / `Drone Supply Usage Per Turn` | 5 / 200 | §7 |
| `Fighters Can Be Hit By Mines` / `Drones Can Be Hit By Mines` | True / True | §12 |
| `Combat Fighter/Mine/Satellite Group Amount` | 20 each | units per combat piece |
| `Maximum Mines/Satellites Per Player Per Sector` | 100 / 100 | §1 |
| `Default Number Of Ships/Units Per Player` | 200 / 1000 | §1 |
| `Bases Can Join Fleets` | False | §9 |
| `Population Mass` | 5 | cargo kT per 1M population (confirmed: binary) |
| `Maximum Population For Abandon Planet Order` | 50 | planet order gate |
| `System Ship Movement Delay Milliseconds` | 0 | presentation only: the pause after each animated step of a move in the system window, which the original reads as seconds, not milliseconds (confirmed: binary; spec 06 §2.4); our client reads it the same way |

---

## 3. The ability system

### 3.1 Sources and activation

A ship's or base's own ability list is built from these sources, in this order (confirmed:
binary):

1. the hull's abilities (always active, never damaged);
2. an extra per-design list and an extra per-vehicle list (inferred: both empty in normal play);
3. the abilities of every component that is not destroyed. Of a component's ability values, a
   mount scales only `Shield Generation` and `Phased Shield Generation`, through
   `Shield Percent` (§4.3); its other multipliers act on size, structure, cost, supply and weapon
   stats. Each component ability remembers its component's `Family`, which one aggregation rule
   uses (§3.2).

Two further sources are applied by the specific rules that need them, not merged into that list
(confirmed: binary):

4. racial traits (for example `Vehicle Speed` in §6.1 and `Supply Cost` in §7);
5. the location: facility and system abilities with a "- System" or sector scope, and stellar
   abilities of storms, warp points and system types.

Most abilities are automatic. Some only act through an order:

| Order | Abilities that need it |
|---|---|
| Colonize | the colonize abilities |
| Use Component | `Emergency Energy`, `Emergency Resupply` (nothing else can be used through it) |
| Use Facility | none: the order exists for facilities with `Emergency Energy` or `Emergency Resupply`, but it has no effect (§8) |
| Stellar Manipulation | create/destroy/open/close abilities |
| Cloak | `Cloak Level` of a ship or base, from hull and components alike (§8) |
| Sweep Mines | runs the sector's mine encounter again: sweeping, then the remaining mines strike (automatic sweeping also happens on entry, §12) |
| Scrap window (a Self-Destruct order) | `Self-Destruct` (ships and bases only; §15) |

A **mothballed** vehicle has no abilities at all (confirmed: binary).

The Ship Report "Ability" tab lists only hull and racial abilities, each entry as its `Descr` line
with nothing summed. Component abilities appear on the component reports (confirmed: binary;
observed in spec 07 session 6; the exact lines are in spec 06 §1.4).

### 3.2 Aggregation

The original reads each ability through one fixed aggregation mode per ability type, applied to
whichever list the rule looks at (a vehicle's list from §3.1, a planet's facilities, a system...).
The modes are (confirmed: binary):

| Mode | Result |
|---|---|
| **Sum** | Σ of the chosen value (V1, or V2 where a rule says so), clamped to at most 2,000,000,000. |
| **Largest** | The largest value, starting from 0: with no entry, or only negative entries, the result is 0. |
| **Smallest** | The smallest V1, or 0 when there is no entry. (The original starts from 99,999 and turns a final 99,999 into 0, so an entry of exactly 99,999 also reads as 0.) |
| **Count** | The number of entries of that type. |
| **Present** | Whether at least one entry exists. |
| **Per sight type** | For a sight type t, the largest V2 among entries whose V1 is t, starting from 0. |
| **First per id** | Group entries by V2 (compared as a 16-bit value). Each group contributes the V1 of the **first** entry met in list order; later entries of the same group are ignored, even when larger. The groups' values are summed. |
| **Per family** | Entries from the hull (and other non-component sources) add in full. Component entries are grouped by the component's `Family`; each family contributes only its largest value; the family values are summed. Clamped to at most 2,000,000,000. |

Modes per ability (confirmed: binary):

- **Sum:** `Standard Ship Movement`, `Supply Storage`, `Cargo Storage`, `Shield Generation`,
  `Phased Shield Generation`, `Shield Regeneration`, `Armor Regeneration`,
  `Shield Generation From Damage`, `Mine Sweeping`, `Boarding Attack`, `Boarding Defense`,
  `Emergency Energy`, `Emergency Resupply`, `Component Repair`, `Solar Supply Generation`, the four
  launch abilities (`Launch/Recover Fighters`, `Launch/Recover Satellites`, `Lay Mines`,
  `Launch Drones`; V1 and V2 are summed separately), `Sector - Sensor Interference`,
  `Sector - Shield Disruption`, `Sector - Damage`, `Warp Point - Turbulence`, the three
  `System - ...` movement and centre abilities, `Planet - Shield Generation`,
  `Planet - Change Conditions`, `Planet - Change Ground Defense`,
  `Planet - Change Population Happiness`. `Modified Maintenance Cost` is read as 100 + the sum.
- **Largest:** `Emissive Armor`, `Multiplex Tracking`, `Combat Movement`, `Long Range Scanner`,
  `Medical Bay`, `Open Warp Point Distance`, `Create Planet Size`, `Destroy Planet Size`,
  `Create Constructed Planet`, `Sector - Sight Obscuration`, `Resource Reclamation`,
  `Resource Conversion`, `Ship Training`, `Fleet Training`, `Ship Training - System`,
  `Fleet Training - System` (for the training abilities V1 and V2 are each taken as their own
  largest value, so the rate and the cap can come from different sources),
  `Combat Modifier - System`, `Damage Modifier - System`, `Shield Modifier - System`,
  `Planet Value Change - System`, `Planet Conditions Change - System`,
  `Change Bad Intelligence Chance - System`, `Change Population Happiness - System`,
  `Modify Reproduction - System`, `Change Population - System`, `Plague Prevention - System`,
  `Ancient Ruins`, `Ancient Ruins Unique`, `Planet - Change Atmosphere`. Because the mode starts at
  0, a negative value of these abilities never takes effect.
- **Smallest:** `Movement Bonus` (§6.1). `Reduced Maintenance Cost - System` is read as
  100 − the smallest value.
- **Count:** `Ship Life Support`, `Ship Crew Quarters`.
- **Present:** `Ship Bridge`, `Ship Auxiliary Control`, `Master Computer`, `Quantum Reactor`,
  `Armor`, `Scanner Jammer`, `Space Yard`, `Self-Destruct`, `Combat Best Experience`,
  `Weapons Always Hit`, `Component Destroyed On Use`, `Long Range Scanner - System`, the
  colonize abilities, the stellar-manipulation abilities and the `Stop ...` abilities.
- **Per sight type:** `Cloak Level`, `Sensor Level`.
- **First per id:** `Extra Movement Generation` (V1 grouped by V2).
- **Per family:** `Combat To Hit Offense Plus`, `Combat To Hit Defense Plus`,
  `Combat To Hit Offense Minus`, `Combat To Hit Defense Minus`. So two to-hit components of the
  same family do not stack, but components of different families do, and the hull's value always
  adds. A vehicle's offense (defense) modifier from abilities is Plus − Minus.

Design rules (§4.2) do not use these modes. They count **components**: a component counts once
for an ability however many entries of it it has, and a rule based on size adds the component's
mounted size (confirmed: binary).

### 3.3 Ability type reference (all identifiers)

"V1" and "V2" mean `Val 1` and `Val 2`. A dash means the value is unused. The source column says
where the stock data uses the ability: C = component, H = hull, F = facility, S = stellar or system
type, none = defined but unused.

| Identifier | Src | V1 | V2 | Effect |
|---|---|---|---|---|
| `Warp Point - Turbulence` | S | damage | – | On each transit, 50 % chance that every vehicle of the moving group takes the total V1 as damage; the order then fails (§6.2) (confirmed: binary). |
| `Star - Unstable` | S | % per year | – | Chance that the star explodes. |
| `Sector - Sight Obscuration` | S | level | – | Raises obscuration in all sight types to V1 for objects there (a storm covers its sector, a nebula its system). Units do not benefit. |
| `Sector - Sensor Interference` | S | penalty | – | Subtracted from to-hit rolls in combat there. |
| `Sector - Shield Disruption` | S | points | – | Shield points removed from each combatant during combat there. |
| `Sector - Damage` | S | damage | – | A group stepping into the sector has a 50 % chance to take the total V1 (storms there plus the system) and stop; vehicles staying there, and groups arriving through a warp point, take nothing (§6.2) (confirmed: binary). |
| `Resource Generation - Minerals` / `Resource Generation - Organics` / `Resource Generation - Radioactives` | F | amount/turn | – | Base planetary production, before planet value and population modifiers. |
| `Point Generation - Research` / `Point Generation - Intelligence` | F | points/turn | – | Research or intelligence output. |
| `Spaceport` | F | – | – | The system's planets deliver their output to the empire. |
| `Palace` | none | – | – | Capital marker. |
| `Supply Generation` | F | – | – | Resupply depot on a colonized planet. Refills vehicles in the sector to full (§7). |
| `Planet - Change Minerals Value` / `Planet - Change Organics Value` / `Planet - Change Radioactives Value` | F | %/turn | – | Drifts the host planet's resource value. |
| `Planet - Change Conditions` | F | %/turn | – | Drifts the host planet's conditions. |
| `Planet - Change Population Happiness` | none | %/turn | – | Improves the host planet's mood. |
| `Planet - Change Ground Defense` | none | % | – | Modifies ground combat on the host planet. |
| `Planet - Shield Generation` | F | points | – | Planetary shields in combat. |
| `Shield Generation` | C | points | – | Normal shields raised at the start of each combat. Scaled by the mount's `Shield Percent`. |
| `Phased Shield Generation` | C | points | – | Phased shields. They block both phased and normal fire. |
| `Component Repair` | C, F | components/turn | – | Repair throughput at this location (§13). |
| `Cargo Storage` | C, F | kT (vehicle) or unit space (planet) | – | Adds cargo capacity. |
| `Drop Troops` | none | troops | – | Troops droppable onto a planet at once. |
| `Launch/Recover Fighters` | C | per combat turn | per game turn | Fighter launch rate. Also allows recovery. |
| `Lay Mines` | C | per combat turn | per game turn | Mine-laying rate. |
| `Multiplex Tracking` | C | targets | – | Number of targets per combat turn (the default is 1). Largest value on the vehicle (§3.2). |
| `Combat To Hit Offense Plus` | C, H | % | – | Improves own chance to hit. Per-family aggregation (§3.2). |
| `Combat To Hit Defense Plus` | C, H | % | – | Makes the vehicle harder to hit. Per-family aggregation (§3.2). |
| `Mine Sweeping` | C | mines | – | Enemy mines removed per sweep (§12). |
| `Medical Bay` | C | plague level | – | Automatically, after each step and at turn processing, cures a plague of level ≤ V1 (largest on the vehicle) on a planet in the same sector owned by the vehicle's owner or an empire with Military Alliance or better (confirmed: binary). |
| `Movement Bonus` | C | MP | – | Engine bonus: the smallest value on the vehicle applies (§6.1). |
| `Emissive Armor` | C | points | – | Each hit of the ordinary damage types (normal, shield-skipping and the shield-multiplier types) is reduced by the vehicle's largest V1; a hit of V1 or less does nothing (confirmed: binary; details in the combat spec). |
| `Shield Regeneration` | C | points/combat turn | – | Restores shields during combat. |
| `Master Computer` | C | – | – | Replaces bridge, life support and crew quarters (§4.2, §6.1). |
| `Cloak Level` | C, H | sight type name | level | Obscuration in that sight type: `EM Active`, `EM Passive`, `Psychic`, `Gravitic` or `Temporal`. For ships and bases the level (hull and components alike) applies only while the vehicle is cloaked; otherwise its level is 1. Cloaking needs a level of 2 or more (§8) (confirmed: binary). |
| `Sensor Level` | C | sight type name | level | Detection in that sight type. The empire sees a system at its best level per type (see the sight spec). |
| `Emergency Resupply` | C | supply | – | On use, adds V1 supply, capped at the maximum (§8) (confirmed: binary). |
| `Emergency Energy` | C | MP | – | On use, adds V1 movement this turn (§8) (confirmed: binary). |
| `Long Range Scanner` | C | sectors | – | Lets the empire inspect another empire's ship, base or unit group in the **same system** when some own object there is within its own largest V1 (in sectors) of the target, unless the target has a `Scanner Jammer`. Inspection happens when a player displays the target's details (map selection or report): the details and cargo are shown, and the design (for a unit group, the designs of its units) is recorded as seen by that empire on that date (§4.1) (confirmed: binary). |
| `Open Warp Point Distance` | C | distance | – | Opens a warp point to a chosen system within range. The range is in galaxy-map squares, straight-line distance rounded to nearest (§8) (confirmed: binary). |
| `Create Planet Size` | C | size index | – | Turns asteroids into a planet no larger than V1. |
| `Destroy Planet Size` | C | size index | – | Destroys a planet no larger than V1, leaving asteroids. |
| `Boarding Attack` | C | strength | – | Offensive strength in ship capture. |
| `Boarding Defense` | C | strength | – | Defensive strength against capture. |
| `Standard Ship Movement` | C | MP | – | Marks an **engine** and gives its base movement. |
| `Ship Bridge` | C | – | – | Control component. Fighter, troop, satellite, drone and platform cores also carry this type. |
| `Ship Auxiliary Control` | C | – | – | Backup bridge: keeps control when the bridge is lost (§6.1) (confirmed: binary). |
| `Ship Life Support` | C | – | – | Counted against `Requirement Min Life Support`. |
| `Ship Crew Quarters` | C | – | – | Counted against `Requirement Min Crew Quarters`. |
| `Scanner Jammer` | C | – | – | Enemy long-range scanners cannot inspect this vehicle. |
| `Quantum Reactor` | C | – | – | Unlimited supply: the vehicle's supply is held at the "unlimited" value (§7). |
| `Supply Storage` | C | supply | – | Adds maximum supply. Engines carry it too. |
| `Space Yard` | C, F | resource (1 = minerals, 2 = organics, 3 = radioactives) | rate/turn | Gives a construction queue. One entry per resource. At most one Space Yard component per design (§4.2). |
| `Resource Storage - Mineral` (singular) / `Resource Storage - Organics` / `Resource Storage - Radioactives` | F | amount | – | Raises the empire's storage cap. |
| `Resource Gen Modifier Planet - Minerals` / `Resource Gen Modifier Planet - Organics` / `Resource Gen Modifier Planet - Radioactives` | F | ±% | – | Planet-wide output modifier. |
| `Resource Gen Modifier System - Minerals` / `Resource Gen Modifier System - Organics` / `Resource Gen Modifier System - Radioactives` | F | ±% | – | System-wide output modifier. |
| `Planet Point Generation Modifier - Research` / `Planet Point Generation Modifier - Intelligence` | F | ±% | – | Planet-wide output modifier. |
| `System Point Generation Modifier - Research` / `System Point Generation Modifier - Intelligence` | F | ±% | – | System-wide output modifier. |
| `Combat Modifier - System` | F | ±% | – | Owner's combat modifier in the system. |
| `Damage Modifier - System` | F | ±% | – | Owner's damage modifier in the system. |
| `Planet Value Change - System` | F | ±%/year | – | Planet value drift across the system. |
| `Planet Conditions Change - System` | F | ±%/year | – | Planet conditions drift across the system. |
| `Change Bad Event Chance - System` | F | ±% | – | Adjusts the chance of bad events. |
| `Change Bad Intelligence Chance - System` | F | ±% | – | Adjusts the chance of hostile intelligence events. |
| `Change Population Happiness - System` | F | ±%/turn | – | Mood drift across the system. |
| `Ship Training` | F | +exp/turn | cap | Each own object is a separate source for its own sector, taken in object order: a colonized planet through its facilities (no population needed), a ship, base or unit group through its abilities, each with its own largest V1 and largest V2. From a source with V1 > 0, every own ship or base in that sector that is not mothballed and has experience below V2 gains V1, or exactly V2 − its experience if V1 would pass V2 (experience is fractional). Ship experience never exceeds 50. V2 = 0 means no training (confirmed: binary; details and timing in §9). |
| `Fleet Training` | F | +exp/turn | cap | The same sources train every own fleet located in the source's sector whose experience is below V2: it gains V1, or V2 − truncate(its experience) if V1 would pass V2, so a fleet at 9.5 with V1 = 1 and V2 = 10 reaches 10.5. Fleet experience is capped at 50 (confirmed: binary). |
| `Modify Reproduction - System` | F | ±% | – | Population growth modifier. |
| `Change Population - System` | F | M/turn | – | Population added each turn in the system. |
| `Plague Prevention - System` | F | level | – | Plagues up to V1 are prevented in the system. |
| `Resource Conversion` | F | % loss | – | Enables the Convert Resources order with this loss: the largest value among the colony's facilities, so the highest loss counts (spec 02 §5.6) (confirmed: binary). |
| `Resource Reclamation` | F | % | – | Scrap return rate for scrapping in this sector (§15). |
| `Close Warp Point` | C | – | – | Stellar manipulation. |
| `Destroy Star` / `Create Star` | C | – | – | Stellar manipulation. Destroying a star wipes out the system, including the user. |
| `Destroy Storm` / `Create Storm` | C | – | – | Stellar manipulation. Created storms are capped by the `Created Storm ...` settings. |
| `Self-Destruct` | C | – | – | Enables self-destruct for a ship or base (from any source in its ability list, the hull included; a mothballed vehicle has none). It also fires automatically when the vehicle is about to be captured. Satellite groups, minefields and drone groups can always self-destruct without it; fighter groups never can (confirmed: binary). |
| `Colonize Planet - Rock` / `Colonize Planet - Ice` / `Colonize Planet - Gas` | C | – | – | Can colonize a planet of that type (§8). |
| `Point-Defense` | C | – | – | Marks a point-defense weapon. |
| `Armor` | C | – | – | The component absorbs damage before non-armor components. |
| `Launch/Recover Satellites` | C | per combat turn | per game turn | Satellite deploy and recover rate. |
| `Remote Resource Generation - Minerals` / `Remote Resource Generation - Organics` / `Remote Resource Generation - Radioactives` | C | amount/turn | – | Extracts from asteroids or uncolonized planets at the location. Only one extractor per location per turn. Depletes the source if `Remote Mining Decreases Asteroid Value`. |
| `Armor Regeneration` | C | points/combat turn | – | Restores armor structure during combat. |
| `Shield Generation From Damage` | C | points/hit | – | Converts part of each hit into shield points. |
| `System - Movement Towards Center` | S | squares/turn | – | Pulls ships toward the system centre. |
| `System - Movement Random` | S | squares/turn | – | Moves ships randomly. |
| `System - Destructive Center` | S | damage | – | Damages ships in the centre sector. |
| `Destroy Nebulae` / `Create Nebulae` | C | – | – | Stellar manipulation. Creating a nebula consumes a star and wipes out the system. |
| `Destroy Black Hole` / `Create Black Hole` | C | – | – | As above, for black holes. |
| `Stop Planet Destroyer` / `Stop Star Destroyer` / `Stop Nebulae Creator` / `Stop Black Hole Creator` / `Stop Open Warp Point` / `Stop Close Warp Point` | F | – | – | Blocks that manipulation in the whole system, whoever owns the blocking object; `Stop Planet Destroyer` protects only its sector (§8) (confirmed: binary). |
| `Component Destroyed On Use` | C | – | – | The component becomes destroyed after its use ability fires. |
| `Ancient Ruins` | S | count | – | When the planet is colonized, N = the planet's largest V1. If N > 0: N times, one research area is drawn at random (up to 1000 draws) among areas whose requirements the colonizing empire meets, that it may research and that are below their maximum level, and it gains one level; then this ability is removed from the planet. `Ancient Ruins Unique` is then not applied and stays on the planet (confirmed: binary). |
| `Ancient Ruins Unique` | S | tech id | – | When the planet is colonized and it has no `Ancient Ruins` above 0: the unique area V1 is added to the colonizing empire's unique areas, every research area with that unique id that is not at its maximum level gains one level (requirements are not checked), and this ability is removed from the planet (confirmed: binary). |
| `Combat Best Experience` | C | – | – | In combat, uses the highest experience among the owner's combatants. |
| `Combat Movement` | C | MP | – | Extra movement in combat only. Largest value on the vehicle (§3.2). |
| `Solar Supply Generation` | C | supply/star | – | At the end of each turn adds V1 × (stars in the current system), capped at the maximum (§7) (confirmed: binary). |
| `Extra Movement Generation` | C, H | MP | stacking id | Movement bonus, first entry per stacking id (§3.2, §6.1). |
| `Planet - Change Atmosphere` | F | turns | – | After V1 turns, the atmosphere becomes the one most of the population breathes. |
| `Weapons Always Hit` | C | – | – | The vehicle's direct-fire weapons never miss. |
| `Create Constructed Planet` | C | PlanetSize special id | – | Builds a ringworld or sphereworld. |
| `Constructed Planet Requirements` | C | custom group | kT | V2 kT of components with `Custom Group` = V1 must be present at the location. |
| `Modified Maintenance Cost` | H | ±% | – | Changes this vehicle's maintenance: −50 halves it. |
| `Ship Training - System` / `Fleet Training - System` | F | +exp/turn | cap | System-wide, one source per system rather than per object: for each system the empire has explored, V1 and V2 are each the largest over all the empire's objects in that system taken together. Every own ship or base there that is not mothballed and has experience below V2 gains V1, or V2 − truncate(its experience) if V1 would pass V2; fleets located in the system likewise. This runs after all the sector sources of the turn (confirmed: binary). |
| `Long Range Scanner - System` | F | – | – | While any object of the empire in a system has it (planet facilities count without population), the empire can inspect every ship and base of other empires in that system, as with `Long Range Scanner` but at any distance; a `Scanner Jammer` still prevents it, and unit groups are not covered (confirmed: binary). |
| `Solar Resource Generation - Minerals` / `Solar Resource Generation - Organics` / `Solar Resource Generation - Radioactives` | F | amount/star | – | Output per star in the system. |
| `Reduced Maintenance Cost - System` | F | % reduction | – | The owner's ships in the system pay less. The sign is **opposite** to `Modified Maintenance Cost`: +10 means 90 %. |
| `Shield Modifier - System` | F | points | – | Added to the maximum shields of the owner's ships in combat in the system. |
| `Combat To Hit Offense Minus` | none | % | – | Lowers own chance to hit. |
| `Combat To Hit Defense Minus` | H | % | – | Makes the vehicle easier to hit (large hulls). |
| `AI Tag 01` through `AI Tag 20` (two-digit suffix) | none | – | – | Opaque markers for AI logic. No engine rules effect. |
| `Generate Points Minerals` / `Generate Points Organics` / `Generate Points Radioactives` / `Generate Points Research` / `Generate Points Intelligence` (no dash) | none | amount/turn | – | Flat income from a component or facility, not drawn from a planet. |
| `Launch Drones` | C | per combat turn | per game turn | **Missing from Abilities.txt** but used by drone launchers. Launch only, no recovery. |
| `None` | F | – | – | Placeholder in some facility records. Ignore it. |
| `Random`, `Warp Point - Unstable`, `Warp Point - Periodic`, `Warp Point - Ability Required`, `Sector - Ability Required`, `Resupply Pod`, `Maximum Population` | none | – | – | Recognised type names that stock data does not use. No game rule reads any of them: they have no effect. `Resupply Pod` and `Maximum Population` only appear in the computer player's ranking of items by an ability's value (confirmed: binary). The loader MUST accept them. |

---

## 4. Designs

### 4.1 Identity and lifecycle

- Fields: name, design type, hull, ordered component list (each entry is a component plus an
  optional mount), creation date, obsolete flag, prototype flag, default combat strategy, and
  statistics (number constructed, in service, lost, and enemy tonnage destroyed).
- **Name** MUST be non-empty and differ from the name of every design in the game, other empires'
  designs included. The comparison is exact and case-sensitive (confirmed: binary). Name
  suggestions come from the empire's name list. The original checks the name only in the design
  window and when a computer player picks a name. Designs that arrive in a player's turn file
  or come from an empire file (premade starting designs) are only checked for validity (§4.2),
  not for their name, so two designs can end up sharing a name. A computer player takes the
  first name of its design-name list, after the last one it used, that is not in use; after
  that it adds the numerals II to XV to a name; without a list it uses a generic numbered
  name (confirmed: binary).
- **Prototype:** a design is a prototype until a construction queue completes a vehicle or unit
  of it, or a ship is retrofitted to it; from then on it is marked as built for good. The Edit
  button is enabled only for one of the player's own designs that is still a prototype (never on
  the enemy design tabs or with nothing selected), and the Edit action also refuses a design that
  is in one of the empire's construction queues. Editing changes the design **in place**: the
  same design, which may keep its name. After a design is built, changes go through **Copy**,
  which clones it with an empty name, or **Upgrade**; both are enabled for any own design, built
  or not (confirmed: binary).
- **Upgrade** builds a candidate design from a copy of the selected one and replaces each
  component entry on its own: the new component is the **last one in Components.txt order** that
  has the entry's `Family` and whose tech requirements the design's owner meets (every required
  area at or above its level). With no such component the entry is kept as it is. Roman numerals
  and names play no part, family 0 is an ordinary family, and neither the hull's vehicle type
  nor the mount is checked; each entry keeps its mount. The stock large engines (Ion,
  Contra-Terrene, Jacketed-Photon and Quantum, I to III each) share one family in that order, so
  once Contra-Terrene Engine I is researched every large engine of the design becomes
  Contra-Terrene Engine I, Ion Engine III included; the other stock families that hold more
  than one line (shields, torpedoes, beams, the small engines and armours) move to the newest
  line researched the same way. Where a family's entries are neighbours in the file, as in the
  stock data, this is the component Only Latest shows in the designer (spec 02 §6.4). The
  candidate opens in the designer even when nothing changed, and it keeps the design's name, so
  Create Design refuses it as a name already in use until the player gives another. When
  accepted, it is added as a **new** design under its own name; the original is untouched
  (confirmed: binary; observed, spec 07 session 7: with every large engine researched, an Ion
  Engine I became Quantum Engine III). Facility upgrades choose their target differently, by
  the highest numeral (spec 02 §6.6). Since 2026-10-05 our client follows (§19 Q81); in
  place of the design's own name the designer proposes the next free one ("Lancer II"), an
  OpenSE4 convenience.
- Whenever the designer accepts a design (Create, Copy, Edit or Upgrade), the result starts as a
  prototype that is not obsolete, with no sightings by other empires (§4.1 Obsolete) and empty
  statistics (confirmed: binary).
- **Obsolete** is a flag. Obsolete designs can be hidden in lists. Designs cannot be deleted
  directly. Every design keeps, for each empire, the turn that empire last saw it. Sightings are
  recorded by battles (the designs of ships and bases, and of the units in their or a colony's
  cargo, but not of unit groups in space), mine encounters (the mines' designs), long-range
  inspection (§3.3), intelligence, messages and gifts, planet transfers and treaties. In its
  end-of-turn processing each empire forgets sightings more than 50 turns old. Every 10th turn,
  after all empires' end-of-turn processing, an obsolete design is removed once all of these
  hold: no vehicle or unit of that design exists (in space, in cargo or on a planet), it is in
  none of the owner's construction queues, and no other living empire saw it less than 50 turns
  ago, so a sighting exactly 50 turns old no longer protects it (confirmed: binary).
- Unit designs and ship/base designs are listed separately. **Enemy designs** become known through
  combat, long-range scanning or intelligence. They show without design type or prototype status.
- The combat strategy of a fleet overrides a design's default strategy.

### 4.2 Validity rules

The designer shows every failed rule as a warning. A design with any warning cannot be created.
Let `T` be the hull `Tonnage`. `count(X)` is the number of **components** that have ability X (a
component counts once, however many entries of X it has), and `size(X)` is the sum of those
components' mounted sizes (§4.3). The original checks, in this order (confirmed: binary):

1. The design has a hull. If not, this is the only warning.
2. Tech: the hull's, every component's and every mount's tech requirements are met by the
   designing empire.
3. Σ mounted size of all components ≤ T.
4. At most one component has `Space Yard`.
5. Mounts: every mounted entry's mount accepts T (§2.4 size bounds). If any does not, the warning
   is "mount not allowed on this size". Otherwise, if any mount's tech is not met, the warning is
   "mount beyond our technology". At most one of these two warnings is shown.
6. `Restrictions`: for each component whose restriction is N per vehicle, the number of **other**
   components on the design with the same `Family` (whatever their numeral, mount or own
   restriction) must be below N. In other words the whole family may appear at most N times. Only
   the first violation found is reported.
7. Hull requirements. If `count(Master Computer)` ≥ 1, skip a–c:
   a. `Requirement Must Have Bridge` = True requires `count(Ship Bridge)` to be **exactly 1**. The
      message calls the part a cockpit on fighter and troop hulls, and a computer core on
      satellite, drone and weapon-platform hulls.
   b. `Requirement Can Have Aux Con` = True requires `count(Ship Auxiliary Control)` ≤ 1. When it
      is False nothing is checked.
   c. `count(Ship Life Support)` ≥ `Min Life Support`, and `count(Ship Crew Quarters)` ≥
      `Min Crew Quarters` (each checked only when the minimum is above 0).
8. Engines (not affected by a Master Computer). An engine is a component with
   `Standard Ship Movement`. If `Requirement Uses Engines` is False, `count(engines)` must be 0.
   If it is True and `Requirement Max Engines` > 0, `count(engines)` ≤ `Max Engines`; a maximum
   of 0 means no limit. No minimum number of engines is required.
9. Percentage rules, each checked only when its percentage p is above 0. The requirement is
   `truncate(T × p %)`, computed in floating point (see Conventions):
   - fighter bays: `size(Launch/Recover Fighters)` ≥ requirement;
   - colony modules: `size(Colonize Planet - Rock)` + `size(Colonize Planet - Ice)` +
     `size(Colonize Planet - Gas)` ≥ requirement. A component with two colonize abilities is
     counted once in each term;
   - cargo: `size(Cargo Storage)` ≥ requirement. Bays and colony modules count only if they
     themselves carry `Cargo Storage`.

Not part of the check (confirmed: binary):

- Whether each component may be fitted to the hull's class (§2.3). The designer only offers
  compatible components, so the engine MUST enforce it when a design is created, but it produces
  no warning in the original.
- A mount's `Vehicle Type`, `Weapon Type Requirement` and family list. The first only limits which
  mounts are offered. The other two decide whether the mount changes the component (§4.3).

### 4.3 Mounts

A mount is chosen per component entry, and one design may mix mounts. Validity is covered by
§4.2 rules 2 and 5 and the offer rule in §2.4.

A mount **applies** to a component when the component's `Weapon Type` meets the mount's
`Weapon Type Requirement` (§2.4) and the component's `Family` is on the mount's family list (or
the list is empty). A mount that does not apply leaves every value of that component unchanged
(confirmed: binary).

When it applies, each value is computed in floating point (see Conventions) and **rounded** to
the nearest integer, halves to even (confirmed: binary):

| Value | Mounted formula |
|---|---|
| size | round(size × `Tonnage Percent` %) |
| structure | round(structure × `Tonnage Structure Percent` %) |
| cost | round(each resource × `Cost Percent` %) |
| supply per use | round(`Supply Amount Used` × `Supply Percent` %) |
| shields | round(the component's summed `Shield Generation` (or `Phased Shield Generation`) × `Shield Percent` %) |
| damage at range r | round(D(i) × `Damage Percent` %), capped at 50,000, where D(i) is the base damage-table entry at index i = r − `Range Modifier`, clamped to 1..20 |
| to-hit | + `Weapon To Hit Modifier` (combat spec) |

For an unmounted weapon, D(r) is used for 1 ≤ r ≤ 20 and the damage is 0 outside that range. With
a mount, ranges closer than 1 + `Range Modifier` use the range-1 damage (confirmed: binary).

How mounts interact with retrofit is covered in §14.

### 4.4 Cost and displayed stats

- Design cost for each resource = hull cost + Σ mounted component cost (§4.3) (confirmed:
  binary). This cost drives build time, maintenance, scrap value, unmothball cost and retrofit
  limits.
- Designer and report stats: Space Used / T, Total Cost, Movement, Shields (normal + phased),
  Cargo Space (Σ `Cargo Storage`) and Supply Capacity (Σ `Supply Storage`).
- The designer's Movement uses the hull and all components: `(Σ Standard Ship Movement) /
  Engines Per Move` (0 when the divisor is 0), plus the bonus B of §6.1 when 0 < B < 100. It
  leaves out the racial bonus and every damage, supply and control penalty (confirmed: binary).

---

## 5. Vehicle instance state

Each vehicle stores:

- id, owner, name, design, and location (system, sector);
- per component, whether it is destroyed. The original keeps only this flag per component, not
  partial damage (confirmed: binary); how a hit's damage is spread over components is in the
  combat spec;
- supply;
- movement points remaining and movement points at the start of the turn;
- an order list and a repeat flag;
- fleet id;
- cargo (population amount and a count per unit design);
- crew experience;
- status (Normal, Under Construction or Mothballed) and a cloaked flag;
- minister-control flag;
- a construction queue, if the vehicle has a `Space Yard`;
- for drones, the target.

A vehicle is **destroyed** when all of its components are destroyed. The displayed damage is
Σ structure of destroyed components over Σ structure.

The status values are Normal, Under Construction and Mothballed. Cloaking is a separate flag, not a
status (confirmed: binary).

---

## 6. Movement

### 6.1 Movement points per turn

A ship's maximum MP is recomputed whenever something that affects it changes (a component is
destroyed or repaired, supply changes, a turn starts). The MP left this turn are then lowered to
the new maximum if they exceed it, so engines lost mid-turn cut the remaining movement at once.
Unspent MP do not carry over. The maximum is found in these steps (confirmed: binary), where L
is the ship's ability list (§3.1: hull plus components not destroyed; empty when mothballed):

1. E = Sum of `Standard Ship Movement` V1 over L (the original keeps only the low 8 bits of E,
   which never matters with stock data).
2. Base = E ÷ `Engines Per Move` with integer division, or 0 when `Engines Per Move` is 0.
3. B = (Smallest `Movement Bonus` V1 over L, 0 when there is none) + (First-per-id
   `Extra Movement Generation` over L).
4. If Base > 0 and B < 100, add B to Base.
5. If Base > 0, add the owner's racial `Vehicle Speed` total.
6. If the result is above 0, apply in this order:
   - supply 0: the result becomes exactly 1;
   - mothballed: the result becomes 0;
   - unless L has a `Master Computer`: for each of (a) no `Ship Bridge` and no
     `Ship Auxiliary Control` in L, (b) Count of `Ship Crew Quarters` in L is 0, (c) Count of
     `Ship Life Support` in L is 0, halve the result with integer division and raise it to 1 if
     it fell below 1.
7. That is the maximum. The MP left this turn become the smaller of the MP left and the maximum.

Consequences worth stating:

- **Movement Bonus is a minimum over the whole list**, including engines, hull and any other
  component with the ability. An engine without `Movement Bonus` does not count as 0; it is simply
  not part of the minimum. Stock ship engines all carry the ability (0 for standard engines), so
  mixing a +2 engine with a standard engine gives +0, and mixing +3 with +2 gives +2. Destroyed
  engines are not in the list and do not count.
- B of 100 or more is ignored entirely. A negative B is added too. The original keeps the result
  in 8 bits, so a sum below 0 wraps around to a huge value; the engine SHOULD clamp at 0 instead
  and data should avoid negative bonuses.
- `Extra Movement Generation` and the racial bonus only apply when the engines already give at
  least 1 MP. A ship with no working engines never moves.
- Out of supply the maximum is **exactly 1**, even for a fast ship, as long as it had at least 1
  MP from its engines. A `Quantum Reactor` keeps supply at the unlimited value (§7), so it never
  triggers this.
- Losing control **halves** the maximum (rounding down, never below 1) once for each missing item:
  bridge-or-auxiliary-control, crew quarters, life support. The test is "none left", not "fewer
  than the hull minimum", and it ignores whether the hull requires the item. A working
  `Master Computer` prevents all three halvings. Auxiliary control stands in for a lost bridge.
- `Emergency Energy` adds movement to the current turn only (§8).
- Units (fighters, drones) in space use their own movement rules (§12).

### 6.2 Grid, paths and warp points

All rules in this section are (confirmed: binary) unless marked otherwise.

- A system is a 13 × 13 grid of sectors, numbered y × 13 + x. The distance between two sectors is
  max(|dx|, |dy|). One step moves to an adjacent sector, diagonals included, and costs 1 MP and one
  step's supply (§7).
- A **warp** moves a vehicle from a warp point's sector to the linked warp point in the other
  system. It costs 1 MP and one step's supply. Ships (and bases, which cannot move) and drone
  groups can warp; fighter groups cannot: a group containing one fails a Warp order (the list is
  cleared), and a Move To into another system travels to the warp point and fails there. Each
  transit through a warp point with `Warp Point - Turbulence` has a
  50 % chance that every vehicle in the moving group takes the total turbulence V1 as damage. The
  transit still happens, but the order then fails (§8).
- **Routes between systems** are a shortest-path search whose cost is the in-system distance to
  each warp point plus 1 per jump. Only the first 10 warp points of a system are considered.
- **Known space:** a human player's route may only pass through explored systems, and the
  destination system must be explored, so a Move To into an unexplored system fails and needs an
  explicit Warp order. Computer players have no such limit but avoid warp points watched by
  dangerous enemies.
- **Systems To Avoid:** with the empire's avoid option on, listed systems are never crossed,
  except the current and destination systems. If no route remains, the Move To fails; there is no
  fallback through avoided systems. The window's map filters (Presence, Ally Claimed, Enemy
  Claimed) are read-only.
- **Tagged minefields** (a per-empire set of sectors, added or removed by order) are avoided only
  when the empire's option to route around them is on. Then a warp link whose warp-point sector on
  either side is tagged is not used, whether or not it is the start or the destination, and
  in-system steps avoid tagged sectors (below). A group whose first member has the design type
  named `Mine Sweeper` may still use such links; for a fleet the first member is the first one at
  the fleet's location in object order, not necessarily the chosen leader. The exemption never
  applies to in-system steps.
- These two options (avoid tagged minefields, avoid restricted systems) are the empire's **Ship
  Movement** options in the Empire Options window (spec 06). Both are on for a new empire.
  Computer players copy them from their `AI_Settings` each turn (spec 05 §7.5).
- **In-system steps are greedy.** The target square is the destination, or the sector of the next
  warp point on the route. The first candidate is one square toward it, diagonal when both
  coordinates differ. Unless that candidate is the target, it is tested; a square is *bad* when it
  is a tagged minefield (option on), when objects on it have `Sector - Damage` (the system's own
  value is not counted here), or when it holds an object the mover can see whose owner is hostile
  to it (treaty below Non-Aggression): a ship, base, unit group (a mine only if seen) or colony.
  A bad candidate is replaced by a random one, always drawn from the current square: when the
  first step was diagonal, the straight step along x or along y (1 in 2 each); when it was
  straight, the forward square or either square beside it (1 in 3 each, so the forward square can
  come up again). Coordinates are clamped to 0..12, so a draw off the grid becomes the edge square.
  Every replacement is tested, even when it is the target. After the 10th bad test the group stays
  where it is and the order fails.
- **Around a destructive centre.** In a system whose total `System - Destructive Center` is above 0,
  every in-system step of a moving order (and the route estimates and path display) is chosen by
  a cost map instead of the greedy rule above (confirmed: binary). Let R be the system's total
  `System - Movement Towards Center` (0 makes no zone). The target square costs 1. Entering a
  square from a neighbour costs 1 + (30 − round(√(dx² + dy²))) + (1000 if max(|dx|, |dy|) ≤ R),
  where (dx, dy) is that square's offset from the centre square (6, 6) and the rounding is to
  nearest; each square keeps its cheapest cost, and only squares whose cost is at most 500 spread
  further. The group then steps to the cheapest of the nine squares around it, its own included;
  on a tie the square with the lowest sector number wins. So
  groups keep out of the zone within R of the centre and prefer squares far from it. When no
  square around the group was reached (the path would cost more than 500, or every square next to
  the target lies in the zone), the greedy step is used after all. The cost map ignores tagged
  minefields, storms and hostile objects. Stellar drift toward the centre does not use it.
- **Entering a sector with enemies:** only in turn-based games, only for in-system steps (a warp
  jump never asks), and only while a human is the current player (orders carried over and run at
  the start of that human's turn included). The question comes when the next sector holds any
  object the group's owner can see of an empire hostile to it (mines and colonies included if
  seen); declining stops the move and fails the order. The approach steps of an Attack ask like
  any other step. No question is asked for a group made only of drones, or when every member is
  cloaked. Computer players' groups let their AI decide, and in simultaneous games groups simply
  enter.
- A ship whose space yard has items in its construction queue cannot move or warp; the order fails.
- Fighters stay in their system. Satellites, mines, troops and platforms never move by themselves.
  Drones move only by orders: an Attack order makes them pursue their target (§8, §12).

**Hazards met while moving:**

- Stepping into a sector with `Sector - Damage` (the storm objects there plus the system's own
  value) has a 50 % chance of dealing that damage to each vehicle of the moving group. If it does,
  movement stops and the order fails. Vehicles that stay in a storm take nothing, and a warp
  arrival makes no storm roll.
- Storm, turbulence and destructive-centre damage (and the `Ship - Damage` event, spec 01 §10) use the
  combat damage routine for Normal damage, with no shields, no emissive or crystalline armor, no
  damage modifier and no carried-over pool: on a ship, the components are destroyed in the random,
  structure-weighted order with all armor first, until the damage cannot cover the next one
  (spec 04 §9.1a), and what is left is lost; on a unit group, whole units die as in spec 04 §9.4
  (up to 20 random draws, a unit's shields counting as hit points), and the leftover is lost. A
  vehicle still under construction that is hit becomes Normal with 0 supply (then refilled if it
  is at a depot). All of this is (confirmed: binary).
- Hitting a minefield stops the move and fails the order.
- In turn-based games only, entering a sector with enemies starts combat at once and fails the
  order.

**Stellar drift and centre damage**, applied at the end of the turn after the economy phase, with
no MP or supply spent:

- `System - Movement Towards Center` moves every ship, base and unit group V1 greedy steps toward
  the centre square (6, 6).
- `System - Movement Random` picks one random target square per turn, shared by every system and
  drawn only from sector numbers 0..144 (the original never picks the last row and a half); objects
  move V1 steps toward it. The square is drawn every turn, first thing in the events step, even
  when no system has the ability, so it always takes one number from the random sequence.
- `System - Destructive Center` deals V1 damage to everything on the centre square.

### 6.3 Turn modes

**Turn-based.** Orders execute as soon as they are issued, spending MP. At the start of each turn,
every vehicle regains MP and first continues its existing order list.

**Simultaneous** (this project's mode). Players only queue orders; the host resolves all movement
over a **30-day** month (confirmed: binary):

1. At the start, every vehicle's MP is reset to its maximum, and the MP of every fleet member at
   the fleet's location to the lowest maximum among those members. Every day counter is set
   to 0.
2. On each day, before anyone acts, every ship, base and unit group whose MP is above 0 adds
   speed ÷ 30 to its counter, where speed is its current MP. For a fleet member, wherever it
   is, speed is the lowest current MP among the fleet's members at the fleet's location, and 0
   when none is there (§9) (confirmed: binary). The arithmetic is given below.
3. On a day, a ship, base, fighter group or drone group acts when its counter is at least 1, and
   its counter then loses 1. A vehicle with 0 maximum MP that has orders acts on day 1 only, and
   so do colonized planets, minefields and satellite groups that have orders.
4. An action sets the acting object's MP to exactly 1, whatever MP it had, when its maximum is
   at least 1 (otherwise it keeps 0), and runs its order list (§8); the other members of its
   group keep their own MP (confirmed: binary). It executes the head order, and
   every order that completes chains into the next within the same action, up to 21 executions;
   the action ends at the first order that waits (a move once its 1 MP is used, a Sentry...) or
   fails. So a Move To that arrives, a Load Cargo and a Drop Cargo can all happen in one action,
   and an object that acts on day 1 only runs all of its orders that need no movement then.
   MP are not spent in this mode: after the action the vehicle's MP is put back to its value
   before the action, unless its maximum fell below that value during the action; then it keeps
   what the action left (0 after a step) and gains nothing on the following days. It still acts
   while its counter holds a whole unit (speeds above 30, Emergency Energy), each time with 1 MP
   again, so it can take one more step per such action, and then stops for the rest of the turn
   (confirmed: binary). A maximum lowered between actions (by combat damage) caps the MP, which
   only slows the daily gain.
5. Within a day, objects act in the order of their slots in the game's object list. That list
   holds every object: stars, planets, asteroid fields, storms, warp points, ships, bases and
   unit groups. A removed object of any kind leaves its slot empty in place, and a new object of
   any kind takes the lowest empty slot, whatever kind of object left it, else a new slot at the
   end; so the order is creation order only until something has been removed, and from then on
   vehicles and planets mix (confirmed: binary). A fleet acts when the first of its members in
   that order that is due and has orders acts; the members it carries along do not act again
   that day, but those that were due still lose 1 from their counters. Ad-hoc groups are formed
   at each execution (§8).
6. After each day, every sector where an object acted that day is checked: the sector a
   vehicle stands in after its action, whether or not its list held an order (a vehicle with
   movement acts on its counter's days, step 3, so an idle ship marks its sector on those
   days; a vehicle carried along by a group member does not act itself), and the sector of a
   colony that acted with orders (confirmed: binary; observed 2026-10-03: in game 10 a
   little under half of the ships' daily actions were made with an empty list, spec 07 "The
   computer players' second round under a debugger"). OpenSE4 follows this since
   2026-10-03 (`Mover::run`, `movement.cpp`); it used to skip a vehicle with no orders, so
   only an action that carried out an order marked a sector. In 120 scratch games with idle
   vehicles marking theirs, battles went from 11.1 to 13.2 per empire and 25 turns of turns
   51–100, nearly all of the new ones drawn without a shot, and decided battles and losses
   did not change. When an empire
   with an uncloaked vehicle there sees an object, not a minefield, of an empire it is
   hostile to (spec 04 §2; a colony never counts as the side that sees), a combat is fought,
   unless the location already had a battle this turn and both of these hold for the latest
   one: no piece recorded as surviving it was below full structure (damage from before that
   battle counts), and every object owned by a player now in the sector (ships, bases, unit
   groups including minefields, and colonies) is on its owner's list of that battle's
   survivors, matched by name. So a minefield (never a combat piece) or any newcomer forces
   a new battle. Combat is always resolved automatically.
7. Also after each day, pursuit orders (Attack, §8) whose target is gone are removed.
8. Combat does **not** clear orders or stop movement. Its only effect on orders is that a Sentry
   order at the head of a participant's list is removed, even with Repeat on.

**The day counter's arithmetic.** The counter is a 64-bit floating-point number (a double). Each
day the original divides speed by 30 and adds the stored counter in x87 extended precision, each
of the two operations rounded to a 64-bit significand (to nearest, ties to even), and stores the
sum back as a double, which rounds it again to a 53-bit significand (to nearest, ties to even).
The test is counter ≥ 1, and acting subtracts exactly 1. Emergency Energy's addition (§8) is done
the same way. Because 1/30 is not exact in binary, the stored sum often lands just below a whole
number, so many speeds lose their day-30 step and some steps come a day later than in exact
arithmetic, where speed M acts on day d when floor(d·M/30) > floor((d−1)·M/30) (for M = 5 that is
days 6, 12, 18, 24 and 30, as the manual says; the original moves on days 7, 13, 19 and 25). The
game sets the x87 to 64-bit precision at start-up, in every thread and after every exception it
handles, and never lowers it (confirmed: binary). In the running game (under Wine), turn
processing runs at that precision, and a speed-6 ship's stored counter matched this arithmetic bit
for bit over a whole turn, while double or single precision would differ from day 3 and day 1
(observed). An implementation MUST reproduce this arithmetic exactly, for example with an
emulated extended-precision divide and add followed by rounding to a double. It gives these
acting days:

| Speed (MP) | Days the vehicle acts |
|---|---|
| 1 | never |
| 2 | 16 |
| 3 | 11, 21 |
| 4 | 8, 16, 23 |
| 5 | 7, 13, 19, 25 |
| 6 | 6, 11, 16, 21, 26 |
| 7 | 5, 9, 13, 18, 22, 26 |
| 9 | 4, 7, 11, 14, 17, 21, 24, 27 |
| 11 | 3, 6, 9, 11, 14, 17, 20, 22, 25, 28 |
| 16 | every day except 1, 3, 5, 7, 9, 11, 13, 15, 18, 20, 22, 24, 26, 28, 30 |
| 17 | every day except 1, 3, 5, 7, 10, 12, 14, 17, 19, 21, 24, 26, 28, 30 |
| 18 | every day except 1, 3, 5, 8, 10, 13, 15, 18, 20, 23, 25, 28, 30 |
| 19 | every day except 1, 3, 6, 9, 11, 14, 17, 20, 22, 25, 28, 30 |
| 20 | every day except 1, 3, 6, 9, 12, 15, 18, 21, 24, 27, 30 |
| 21 | every day except 1, 4, 7, 10, 14, 17, 20, 24, 27, 30 |
| 22 | every day except 1, 4, 8, 12, 15, 19, 23, 27, 30 |
| 23 | every day except 1, 5, 9, 13, 18, 22, 26, 30 |
| 25 | every day except 1, 6, 12, 18, 24, 30 |
| 27 | every day except 1, 10, 20, 30 |
| 29 | every day except 1, 30 |
| 8, 10, 12, 13, 14, 15, 24, 26, 28, 30 | as in exact arithmetic |
| above 30 | every day (at most one action a day; the counter just grows) |

Each speed in the upper part of the table makes one action fewer than its MP. A speed-1 vehicle
never moves in a simultaneous game unless it gets extra actions (Emergency Energy).

The Cargo Transfer and Launch/Recover windows do not exist in this mode. Their effects happen
through the Load, Drop, Launch-remote and Recover-remote orders during resolution. A movement log
records every step for replay, per system and per ship.

### 6.4 Interruption

All rules in this section are (confirmed: binary) unless marked otherwise.

- An order whose destination cannot be reached fails, and a failed order clears the whole list
  (§8). The lists a failure clears are the acting vehicle's, or, when it is a fleet member, those
  of its fleet's members at the fleet's location, or in a turn-based game those of the vehicles
  the player selected together; a computer player's ad-hoc companions keep theirs (§8, §19 Q75,
  Q77).
- Movement stops and the list is cleared by: storm damage on entry, turbulence damage on a warp, a
  minefield, a refused entry into an enemy sector, and (turn-based games only) combat on entry.
  Each of these makes the step fail, and the failure clears the lists named above, never a
  companion's (§19 Q77). A
  pursuit (the Seek form of Attack, §8) is the exception: when its step meets storm damage,
  turbulence, mines or a battle, it only stops moving for this run of its list; its order and the
  list are kept (spec 04 §2, §19.2 Q76) (confirmed: binary).
- Being in combat in a simultaneous game neither stops movement nor clears orders.
- Each empire has two **Ship Orders** options: clear orders on encountering an enemy empire (on
  for a new empire) and on encountering any empire (off); computer players take them from their
  AI settings. They are checked only at the end of a warp transit that met no other trouble
  (turbulence damage, mines, turn-based combat), whether the jump belongs to a Warp order, a Move
  To or a pursuit; never on in-system steps, and not for groups made only of drones. They count
  the objects of other empires in the arrival system that the group's owner can see (colonies
  and unit groups included, only if seen); "enemy" means a treaty below Non-Aggression. When one
  applies, the order list of every member of the acting group is cleared and its Repeat switched
  off: fleet copies, a computer player's ad-hoc companions, companion drone groups and, in a
  turn-based game, the vehicles selected together (§19 Q77). The jump itself does not fail: a
  Warp order just ends; in a turn-based game a Move To in progress keeps stepping toward its
  destination in the same run while MP last, although the lists are already empty, so nothing is
  left of it for a later turn if MP run out first; in a simultaneous game the action ends
  there.
- If a vehicle's maximum MP drops mid-turn, its remaining MP are capped at once (§6.1). In a
  simultaneous game a drop during the vehicle's own action (supply running out on its step, for
  example) stops it once its counter has no whole unit left (§6.3 step 4), and a drop between
  its actions slows it (§6.3).
- A cloaked vehicle that the enemy cannot detect passes through enemy-held sectors without combat.
  Detection triggers combat as usual. Attacking does not decloak a vehicle: only drones decloak,
  when their group attacks at the target of an Attack pursuit (§8), and vehicles under the Ship
  Cloaking minister lower their cloaks for an Attack order and raise them again afterwards
  (spec 05). A battle decloaks every piece for as long as it lasts (spec 04 §2) (confirmed:
  binary).
- Treaties decide whether a meeting leads to combat: an empire whose treaty with another is
  Non-Aggression or better never fights it, so their vehicles pass through each other's sectors;
  below that (War, Non-Intercourse, None, or not yet met) the battle checks of spec 04 §2 apply.
  Each empire judges by its own side of the treaty (spec 04 §2, spec 05 §3.2) (confirmed: binary).

---

## 7. Supply

All rules in this section are (confirmed: binary) unless marked otherwise.

**Capacity.** Maximum supply is the Sum of `Supply Storage` over the vehicle's ability list (hull
and working components). It shrinks when storage is destroyed, and current supply is clamped to it.

**Unlimited supply.** Bases, vehicles under construction and vehicles with a working
`Quantum Reactor` have unlimited supply: the original holds their supply at a marker value
(60,000), never charges them, and shows "Endless". A vehicle that loses its reactor is set back to
a normal supply value on its next move.

**Consumption:**

| Activity | Supply spent |
|---|---|
| Each step, each warp, each Attack step and each Sweep Mines order | S = the sum of the mounted `Supply Amount Used` of every working component that has `Standard Ship Movement` > 0, a `Movement Bonus` > 0 or `Extra Movement Generation` > 0. If the owner's racial `Supply Cost` total c is not 0, S becomes round(S × (100 + c) %) (to nearest, halves to even, in floating point). |
| Each weapon shot | the weapon's mounted `Supply Amount Used` (combat spec). |
| Each end of turn while cloaked | the sum of the mounted `Supply Amount Used` of the working components with `Cloak Level`, without racial scaling. |
| Unit groups each turn | §12. Units also pay the per-step cost when they move. |
| Stellar manipulation, Use Component | §8. |

- A step that costs more than the remaining supply still happens and leaves supply at 0. The
  depot check (below) runs before the cost is taken.
- At 0 supply the maximum MP becomes 1 (§6.1), a cloaked vehicle decloaks, and weapons that use
  supply cannot fire (combat spec).

**Replenishment:**

- **Resupply depot:** a vehicle is refilled to maximum when its sector holds a colonized planet
  with a `Supply Generation` facility that belongs to the vehicle's owner, or to an empire with
  which the owner has a Military Alliance or Partnership (the owner's side of the treaty). No
  population is needed. The check runs after every step, after every daily action, at the end of
  the turn and on unmothballing, so passing through a depot refills. It costs no MP.
- **Fleet pooling:** at the end of each turn only, among the members in the fleet's sector: divide
  the members' total supply by the member count (integer division; the remainder is lost), give
  each member the smaller of that share and its capacity, then hand out what did not fit, in member
  order, to members that still have room. A member with unlimited supply adds its marker value to
  the total, so it effectively refills the fleet.
- `Solar Supply Generation` adds V1 × (number of stars in the system) at the end of the turn, capped
  at the maximum, in the empire's training step (below), so after depot refills, fleet pooling and
  the loss of drones out of supply. Fighter and drone groups with it gain too.

**End-of-turn order.** In each empire's end-of-turn processing (spec 05 §8), after maintenance
(§16) and repair (§13), the supply step runs (confirmed: binary):

1. Upkeep, for every object of the empire in object order: a cloaked ship or base pays its cloak
   parts' supply; a fighter group pays its per-unit upkeep (or its cloak cost instead while
   cloaked); a drone group pays its upkeep plus its cloak cost while cloaked (§12). A vehicle whose
   supply reaches 0 here decloaks at once.
2. The depot check (and `Medical Bay`) for every object of the empire.
3. Fleet pooling for every fleet of the empire.
4. Drone groups at 0 supply are destroyed.

The storage cap of spec 02 follows, then the training step, which applies the training abilities
(§3.3) and solar collectors object by object and then the system-wide abilities. A later
per-object step of the end-of-turn processing only records each vehicle's current sector as the
sector it came from; it involves no supply. In simultaneous games all of this comes after the
turn's movement and combat; in turn-based games each empire's end-of-turn processing runs when
that player ends the turn, after its own moves.
- `Emergency Resupply` adds V1 when used, capped at the maximum (§8).
- Drones can never be resupplied; fighters only at a depot planet (§12).

**Status icons.** "Low Supplies" shows when supply is below
`Supply Amount for Low Supply Warning` (1000), an absolute amount (for fighter groups one tenth of
it). "No Supplies" shows at 0.

---

## 8. Orders

Every ship, base, planet and fleet has an **ordered list** of orders. The list rules are
(confirmed: binary):

- Execution always starts at the head of the list. Each executed order ends as **done**,
  **waiting** or **failed**:
  - done: the order is removed. With **Repeat Orders** on, it stays in the list and execution
    moves on to the next order, wrapping round to the first;
  - waiting: execution stops for this pass and the order stays where it is;
  - failed: the **whole list is cleared** and Repeat is switched off.
- Done orders chain: the next order runs in the same pass, up to 21 order executions per vehicle
  per pass. Orders that need no MP can therefore complete several times in one turn.
- Clear Orders also switches Repeat off. Switching Repeat on restarts at the first order.
- Repeat with a single order that completes without effect makes the vehicle idle. This is legal.
- New orders are **appended**. Giving orders to a vehicle that already has some is a common player
  mistake, so the UI should show the queue.
- **Fleets:** a fleet has no list of its own; its orders are copies in the lists of its members
  at the fleet's location. Orders given to the fleet or to any member are appended to each of
  those lists, and every change to the list applies to each of them. The fleet acts through the
  first member in object order that is due and has orders: the order at that member's own
  list position is carried out by the members at the fleet's location (a member elsewhere is not
  one of them, even when it is the one acting), and completing it removes, or with Repeat
  passes, that position in each of their lists; a failure clears their lists. The acting
  member's own list changes only when it is at the location: an away member keeps its order
  and runs it again (§19 Q73). Mothballed members count like any other (§19 Q74). Orders,
  Clear Orders and Repeat Orders given to an away member go to the members at the location and
  leave its own list alone; the original has no other way to edit a list (§19 Q76). A
  vehicle's list is cleared when it joins or leaves a fleet (§9, §19 Q65) (confirmed: binary).
- **Ad-hoc groups:** the executing group is rebuilt every time an order is executed. For a
  computer player, every own vehicle in the sector whose head order is identical joins: ships,
  bases and unit groups, fleet members, mothballed and cloaked ones included, each alone (not its
  whole fleet). Carrying out the order changes only the acting vehicle's list (or its fleet's
  lists at the location); the companions' lists are left as they are, and they carry the order
  out again on their own next action (§19 Q75). A failure, a minefield or another hazard of
  §6.4 included, leaves them alone too. The one exception is the Ship Orders options at the end
  of a warp, which clear every group member's list, companions included (§6.4, §19 Q77). For a human player only a drone
  group outside fleets gathers the other drone groups outside fleets in its sector with an
  identical head order; human ships never form ad-hoc groups. In turn-based games the vehicles the
  player selected together act as the group (below). A group moves only while every member has
  MP left (confirmed: binary).
- **Tagged vehicles** (confirmed: binary; observed, spec 07 session 7). An order given while
  vehicles are tagged in the report panel's list (spec 06 §2.5) is appended to the own list of
  every tagged vehicle, in the order they were tagged. Tagging a fleet member tags every member
  at the fleet's location, so each of them gets the order in its own list. All tagged vehicles
  stand in the list's sector, since every click on a sector clears the tags.
  - **Simultaneous game.** Nothing else happens. During the turn each vehicle (or its fleet)
    carries its own list out alone, as every human player's ship does: an Attack is a pursuit
    for each of them, each moves on its own day schedule (§6.3), and those that step into the
    target's sector on the same day are in the battle of that day's check; one arriving on a
    later day starts another battle there, which the earlier arrivals still in the sector join
    (spec 04 §2).
  - **Turn-based game.** The orders run at once, as one group. The acting vehicle is the first
    one tagged, and its list runs as usual (from its first order, completed orders chaining, up
    to 21 executions). The group that carries out each of its orders is every tagged vehicle,
    whatever its fleet and whatever its own list holds: nothing is compared with the acting
    vehicle's order. Each order completed removes the entry at the acting vehicle's list
    position from every tagged vehicle's list, and a failure clears every tagged vehicle's
    list. The group steps with all its members together and only while every member has
    movement left, so the tagged vehicles arrive together, are asked once about entering a
    sector with enemies (§6.2), and a battle on entry, or the Attack order at the end, takes
    them all into one battle (spec 04 §2). If any of them has no movement left when the
    order is given, the whole group waits where it is.
  - The tags are cleared once the order is given and carried out (spec 06 §2.5). Whatever is
    left of the orders when movement runs out, because the target is further away than the
    slowest tagged vehicle can go this turn, is carried out at the start of the next turn by
    each vehicle (or fleet) alone, one after another in object order and each at its own speed
    (spec 05 §8): the first to reach a sector with enemies is asked and fights alone, and the
    battle clears its list; the others follow one at a time. Only a fleet keeps vehicles
    together from one turn to the next.

  The engine differs (§19 Q82): the client gives the order with one command per tagged
  vehicle or fleet, and a turn-based game carries out each command before the next one is
  applied, so the first tagged ship moves and fights alone before the others have the order,
  and each of them then does the same. Its turn-based group also takes only vehicles outside
  fleets whose first order equals the acting vehicle's, and an acting fleet member takes only
  its fleet.

Several orders are expanded into simpler ones at the moment they are given; they never exist as
stored orders (confirmed: binary). "Composite" orders below become Move To (when the target
sector is elsewhere) plus the action.

| Order (hotkey) | Parameters | Behaviour |
|---|---|---|
| Move To (M) | sector, in any known system | Paths and warps as needed (§6.2). Done on arrival; waits when out of MP; fails on an invalid destination or a blocked path. |
| Move To Waypoint (Ctrl 0–9) | waypoint | The waypoint is looked up when the order runs; if it has been deleted the order fails (§8.1). |
| Set Waypoint (Alt 0–9) | sector | Empire-level. Defines a waypoint (§8.1). |
| Attack (A) | target | In turn-based games (except for drones) it becomes Move To the sector the target is in when the order is given (added even when the group is already there) plus an Attack. The stored Attack names no target and no place: it attacks wherever the group stands when it runs, which costs 1 MP and one move's supply and starts combat at once; with no MP left it is removed doing nothing. It decloaks nobody (§6.4). In simultaneous games, and for drones, it becomes a pursuit: on each action the group moves toward the target's current sector, warping as needed. Once there, a group holding a drone whose own first order pursues an object in that sector attacks: its cloaked drones decloak, then 1 MP and one move's supply (the battle itself comes from the day's combat check, §6.3, or at once in a turn-based game). A group with no such drone just waits there, spending neither MP nor supply. The order stays either way. The pursuit is done when the target no longer exists (or its slot holds another object), belongs to the attacker's owner, or is a planet without a colony; there is no visibility test. In a battle, a drone group's target is the object its first order pursues (§12). A drone given a warp point as target gets Move To plus Warp instead (confirmed: binary). |
| Warp (W) | warp point | Composite. Needed for links to unexplored systems. The transit needs MP (else it waits), a warp point in the sector and every group member able to warp (else it fails), and no member with a space yard whose queue is not empty (else it fails). It costs 1 MP and one move's supply (§7). On each transit there is a 50 % chance that every member takes damage equal to the warp point's total `Warp Point - Turbulence`; taking that damage, or meeting mines or enemies on arrival, fails the order (confirmed: binary). |
| Resupply (S) | — | Expanded at once into Move To the nearest depot: a colonized planet with `Supply Generation` owned by the empire or by an empire with Military Alliance or better, in an explored system, skipping sectors that hold a visible, armed, non-mothballed hostile, nearest by travel distance (the first one found wins a tie). The Move To is added even when the depot is in the current sector, where it completes at once; with no reachable depot nothing is added (confirmed: binary). |
| Repair (R) | — | Expanded at once into Move To the nearest own object with `Component Repair`, preferring immobile sources (planets, bases); repair ships are used only if there is no immobile source. As for Resupply, the Move To is added even for the current sector, and nothing is added without a reachable source (confirmed: binary). |
| Explore (E) | — | Expanded at once into Move To plus Warp for the warp point, nearest by travel distance, that lies in a system the empire has explored and leads to one it has not (nothing else about the link is checked), skipping every warp point that some own vehicle has a Warp order for anywhere in its list, the ordered vehicle's own earlier orders and fleet copies included. With no candidate nothing is added (confirmed: binary). |
| Colonize (C) | planet | Composite. At the current location it first adds Load Cargo (population), but only if the ship carries no population; that Load Cargo completes and chains into the next order within the same action (§6.3). See the rules below the table. |
| Sentry (Y) | — | Stays at the head at no MP cost. It ends when the owner can see, anywhere in the same **system**, an object (ship, base, unit group or colony) of an empire whose treaty with it is below Non-Aggression (War, Non-Intercourse, None or no contact), or when any group member's supply is below `Supply Amount for Low Supply Warning` (for fighter groups one tenth of it). The Sentry then counts as done: without Repeat it is removed and the following orders run; with Repeat on it stays in the list and execution moves past it. The list is not cleared (confirmed: binary). |
| Set Patrol (P) | list of points | Each click appends a Move To. Clicking the ship's own location ends it and turns Repeat on; if only one order results, the list is cleared instead (confirmed: binary). |
| Repeat Orders (K) | toggle | See the list rules above. |
| Clear Orders (Del) | — | Empties the list and switches Repeat off. |
| View Orders (V) | — | UI only. |
| Load Cargo (L) | cargo type, sector | Composite. Loads as much of the type as fits, from own planets in the sector first, then own vehicles; in a group each member loads for itself. Always done, even when nothing loads (confirmed: binary). |
| Drop Cargo (D) | cargo type, sector | Composite. Drops the type into an own colony or vehicle there that has room. Can fail (confirmed: binary). |
| Launch Units Remotely (I) | unit type, sector | Composite. Launches as many units of the type as allowed (§12). Always done (confirmed: binary). |
| Recover Units Remotely (O) | unit type, sector | Composite. Names a unit kind (fighters or satellites), not a design. For each own group of that kind in the sector, in object order, it recovers the units of every design as far as cargo room allows, and moves on to the next group only while the previous one gave at least one unit. Always done (confirmed: binary). |
| Cargo Transfer (T) | — | Instant window, turn-based games only (§11). |
| Launch/Recover Units (U) | — | Instant window, turn-based games only (§12). |
| Jettison Cargo (J) | window | Not an order: destroys the cargo chosen in a window at once, in both turn styles, at no MP or supply cost. See the rules below the table (confirmed: binary). |
| Fleet Transfer (F) | — | Window (§9). |
| Change Formation\Strategy (H) | — | Fleet only. |
| Cloak (Z) / Decloak (X) | — | Take effect immediately; they are not queued. Cloaking needs a working part whose `Cloak Level` gives level 2 or more in some sight type, and supply above 0. While cloaked the vehicle pays, every turn, the total mounted `Supply Amount Used` of its working cloak parts (a group of fighters pays it per unit), and it decloaks by itself at 0 supply or when it can no longer cloak. A cloaked vehicle cannot colonize, manipulate stellar objects, be mothballed, scrapped, analyzed or retrofitted (confirmed: binary). Colonies cloak by other rules: no supply, no upkeep, and their queue is kept (spec 01 §6.9). |
| Sweep Mines (Ctrl M) | — | Runs the whole mine encounter of the current sector again (sweeping, then the remaining hostile mines strike the group, §12), then, if the group survived, costs 1 MP (if any is left) and one move's supply. Always done (confirmed: binary). |
| Add / Remove Tagged Minefield (Ctrl T / Ctrl R) | sector | Empire-level. |
| Use Component (Ctrl Z) | component position | Given through a picker; in a turn-based game it first clears the list. Only the acting group's first member uses the part (rules below the table). A part with `Component Destroyed On Use` is destroyed first. `Emergency Energy`, only when the vehicle's maximum MP is above 0: in turn-based games it adds V1 to this turn's remaining MP, without a cap; in simultaneous games it adds V1 to the day counter (§6.3), giving up to V1 extra actions at one per day. `Emergency Resupply` adds V1 supply, capped at the maximum, with no effect on unlimited supply or mothballed vehicles. No supply is charged for the use (confirmed: binary). |
| Self-Destruct | — | A separate order. A ship or base needs `Self-Destruct` in its ability list; satellite groups, minefields and drone groups need nothing; fighter groups cannot self-destruct (§15) (confirmed: binary). |
| Stellar Manipulation (B) | action | See below. |
| Scrap / Analyze / Mothball (G) | — | §15. |
| Change Name (N) | text | — |
| Toggle Minister Control | — | Puts the vehicle under AI control, limited by the minister categories the empire enables. |
| Build Queue (Q) | — | Only if the vehicle has a `Space Yard`. |

**Colonize** (confirmed: binary):

- **Choosing the planet.** The player presses Colonize and clicks a sector of the system panel.
  The candidates are every planet in that sector, colonized or not, of any surface type and
  whether or not the ship can colonize it; asteroid fields are not candidates, and a system the
  player has not explored has none. No candidate: nothing is given and the pick ends without a
  word. One: it is the target. Several (a planet and its moons, or two planets): a small window
  asks which one (spec 06 §2.9); Cancel or Esc gives nothing. The orders then appended name that
  planet: Load Cargo (population) at the current location if the ship carries none, Move To the
  sector, Colonize. Nothing about the planet is checked when the order is given; the checks below
  run when the Colonize does, so a wrong pick shows up only on arrival (spec 06 §7 Q100). The
  pick applies no sight test: planets that a storm, a nebula or a colony's cloak hides from the
  player, which the system panel does not draw (spec 01 §6.9), are candidates like any other and
  are listed by name; a sector that looks empty but holds one hidden planet gives the order at
  once, with no window (§19 Q80).
- It needs the ship in the planet's sector with movement left; without movement it waits.
- It fails if the planet is missing, not colonizable or already colonized, if any group member is
  cloaked, or if no member has the `Colonize Planet - *` ability matching the planet type. The game
  options that limit colonization to the empire's own planet type or to breathable atmospheres
  apply.
- The colonizer is the last suitable member in group order. The colony starts with
  `Automatic Colonization Population`, and then every kind of cargo on the ship is dropped under
  the Drop Cargo rules, so only what fits arrives; the rest is lost with the ship, which is
  consumed. Then the planet's ancient ruins take effect (§3.3).
- The colony type: when the empire's option to choose it on colonization is on (it is on for a
  new empire) and the colonizing empire is a human player in a turn-based game, the player picks
  it in a dialog; otherwise it is chosen automatically, as a computer player chooses it
  (spec 05 §7.5). The dialog interrupts the order: the colony is made when it closes, with the
  type picked, or with no type at all (an empty name) when the player cancels (confirmed:
  binary).
- When two ships target the same planet, the first one processed wins; the other's order fails.
- A colony with 0 population is legal but cannot build facilities.
- **The reasons, in the order tested:** the named planet is gone, is not seen by the ship's owner
  (below), or is not in the ship's sector ("no planet here to colonize"); it is an asteroid field
  ("cannot be colonized"); no movement left (the order waits, with no message); it already has a
  colony, of any empire, the ship's own included ("already a colony"); a group member is cloaked;
  no member can colonize it (no matching ability, or a game option excludes it: "unable to
  colonize" and the surface type's name). A Colonize aimed at an existing colony never adds the
  ship's population to it; Drop Cargo does that. Every reason but the wait fails the order, which
  clears the whole list (§8), later orders and a second Colonize included.
- **"Seen" is the detection rule** of spec 01 §6.3 for the ship's owner, the same test that
  decides whether a ship is seen, applied to the planet whether or not it has a colony (§19 Q80):
  the owner has explored the system, and in some sight type its sensor level in that system
  reaches the planet's obscuration. The sensor level is the best over every sensor source the
  owner has in the system at that moment, the colonizing ship itself included (every ship not
  mothballed has at least EM Active 1), its other ships, bases, unit groups (not mines) and
  colonies, raised by its Partnership partners' levels. The planet's obscuration is 1 in every
  type, or the colony's cloak levels while a colony there is cloaked, raised to the obscuration of
  its sector (the largest `Sector - Sight Obscuration` among the storms, ships and planets there)
  and of its system (the system type's, at least 1). Sight is worked out again after every move
  step and warp, so the test sees the ship that has just arrived. With the Omnipresent view the
  test is the one of spec 01 §6.5 instead (no explored test, EM Active at least 1), which gives
  the same result for a ship standing at the planet. Consequences:
  - A planet in clear space is always seen by the ship that reaches it.
  - A planet in a storm or a nebula whose obscuration is above every sensor level the owner has
    there fails, colonized or not: with the stock data a nebula system or a storm with the
    obscuration ability (level 3 in every type) hides its planets from a colony ship with base
    sensors, unless a sensor of level 3 or more of the owner or a partner is in the system.
  - A cloaked colony the owner does not detect fails with the same "no planet here" reason;
    once detected it fails as "already a colony". There is no other difference between a
    cloaked colony and an obscured empty planet, and nothing is remembered between tries: the
    original keeps no memory of planets (spec 01 §6.9). An unexplored system cannot arise here,
    since the ship's arrival explores it.
  - The test is made each time the Colonize is carried out, which is when it heads the list:
    on arrival (the Move To completes and the Colonize runs in the same action), and again at
    each later try while it waits for movement (in a simultaneous game, each acting day). The
    sight test comes before the movement test, so an unseen planet fails at once, even with no
    movement left.
- **Where the player reads the reason.** In a turn-based game, when the player whose turn it is
  is human, a message box titled "Colonize" gives the reason at once, and nothing goes to the
  log. In a simultaneous game the ship's owner, computer players included, gets one log entry
  titled "Unable to Colonize", worded as the Colonization Minister's report naming the system
  and the reason (spec 06 §4.1 gives its picture). A computer player's failure in a turn-based
  game gives no message.

**Stellar manipulation** (confirmed: binary):

- It needs movement left but does not spend it (Construct needs none). It is refused while the
  vehicle is cloaked and, for every action except Destroy Planet, while a visible hostile is
  present in the target sector (spec 01 §9 defines "hostile" there).
- The first working part with the ability must have a mounted `Supply Amount Used` no greater than
  the current supply. That amount is paid on success, without the racial supply modifier, and the
  part is destroyed if it has `Component Destroyed On Use`.
- A refusal for lack of movement makes the order wait in simultaneous games; any other refusal
  fails it.
- Open Warp Point is also refused when the two systems are already linked, when either system has
  10 warp points, when the target is the same system, or when any object of any empire in either
  system has `Stop Open Warp Point`. The distance is the straight-line distance between the two
  systems' galaxy-map positions, in map squares, rounded to nearest; it must not exceed the best
  `Open Warp Point Distance`.
- The `Stop ...` abilities protect the whole system, counting objects of every empire, except
  `Stop Planet Destroyer`, which protects only the target's sector.
- The other preconditions and every result (what Destroy Star leaves, created nebulae, black
  holes and storms, the far end of an opened warp point...) are in spec 01 §9.

**Jettison Cargo** (confirmed: binary):

- **Not an order.** Jettison acts at once through its window, in both turn styles. It costs no
  MP and no supply, adds nothing to the order list, and does not touch Repeat, the vehicle's
  fleet or any group. It is lit for an own ship or base that is not mothballed and for an own
  colony, when the cargo holds anything (spec 06 §2.8). It is never lit for a fleet (the player
  selects the member instead), a unit group or a list of several objects. Being cloaked or in a
  fleet makes no difference. The computer players and the ministers never jettison.
- **What can be dropped:** anything in the holder's cargo: population, one entry per race, and
  units of every kind (troops, fighters, satellites, mines, drones, weapon platforms), one entry
  per stack. For a colony this means its stored cargo. The colony's own population is not cargo
  and cannot be jettisoned; Abandon Planet is the way to empty a colony.
- **The window** ("Jettison Cargo") has a list "Cargo Present" on the left and a list "Cargo To
  Be Jettisoned" on the right, each line an entry with its amount. Four buttons, of which exactly
  one is down, set how much a click moves: Move One (1), Move Five (5), Move Ten (10) and Move
  All (the whole line). Move All is down when the window opens. A click on a line on the left
  moves that much of it to the right, or what the line holds if that is less. If that entry is
  already on the right the amounts add up, otherwise a new line is added at the end. A line
  emptied this way disappears. A click on a line on the right moves it back in the same way. OK
  carries out the right list. Cancel empties it, so nothing is dropped.
- **Where it goes:** nowhere. Jettisoned population and units are destroyed. Nothing is placed in
  space or on a planet, no unit group is made, and nobody can pick it up. Each jettisoned unit
  counts as lost in its design's statistics (Number Lost, spec 04 §15). There is no log entry
  and no other consequence: no happiness change, no score or treaty effect.
- **Faults of the original window.**
  - The amount shown on the left for each population line is the total population aboard, all
    races together, not that race's own amount. Moving at least the race's own amount removes
    its whole entry.
  - The right list is carried out line by line in the order its lines were first added. Each
    line names its entry by its position in the cargo list, population entries first, then the
    unit stacks. After each line, every later line whose position is higher is moved one
    position down, as if the entry just handled had been removed. When a line takes only part
    of an entry, a later line for an entry further down the cargo list therefore hits the
    entry above its own. Example: a ship carries 10 fighters of design A and then 5 of design
    B. The player moves 5 of A and then all of B. The game takes 5 from A, then takes 5 more
    from A, which empties it, and leaves B alone; design A records 10 lost.

  OpenSE4 choice: jettison exactly what the player moved, and show each race's own population.
  These two faults are not reproduced.
- **The engine follows this since 2026-10-01:** an immediate command (`cmd::JettisonCargo`)
  names the holder (an own ship or base that is not mothballed, or an own colony) and the
  amounts per population race and per unit stack; it removes them from the cargo, adds the
  jettisoned units to their designs' lost counters and logs nothing. It refuses amounts that are
  not aboard. The window is as above, with the OpenSE4 choice.

**Use Component and Use Facility** (confirmed: binary):

- **Lit:** Use Component (Ctrl Z) for an own ship or base that is not mothballed and has
  `Emergency Resupply` or `Emergency Energy`. It is never lit for a fleet, a unit group or a
  colony. Use Facility (Ctrl J) for an own colony whose planet or facilities have either ability
  (spec 06 §2.8). These two abilities are the only ones the orders can use. Stellar
  manipulation, self-destruct and cloaking have their own orders and are never reached through
  them. The computer players and the ministers never give either order.
- **Picker.** "Select Component" lists each component position of the selected vehicle whose
  part is not destroyed and has `Emergency Resupply` or `Emergency Energy`, every position
  separately. "Select Facility" lists each facility position of the colony whose facility has
  either ability. Closing the picker without a choice gives no order.
- **Giving the order.** The order records the chosen position, not the part. In a turn-based
  game the object's order list is cleared first, by the same clearing as Clear Orders (so Repeat
  is switched off too), then the order is added. For a fleet member,
  the lists of all the members at the fleet's location are cleared (§9). Then the vehicle's list
  runs at once, so Use Component takes effect as it is given. In a simultaneous game the order is
  appended like any other, for a fleet member to each member at the fleet's location. It runs
  when it reaches the head of the list during movement. It needs no MP, so it completes and the
  next order runs in the same action.
- **Use Component, when it runs.** Only the first member of the acting group uses it. For a
  vehicle outside a fleet that is the vehicle itself. For a fleet member it is the first fleet
  member at the fleet's location in the sector's object order, which need not be the vehicle the
  player picked. That member's part at the recorded position is used, whatever part sits there
  now. A part with `Component Destroyed On Use` is destroyed first, then the energy and supply
  rules of the table apply. A part with neither ability gives nothing, though a
  destroyed-on-use part is still destroyed. Nothing checks that the part is still intact or that
  the vehicle is not mothballed. No supply is charged and nothing is logged. The order always
  completes and never fails.
- **Use Facility, when it runs, does nothing.** The game looks up the facility at the recorded
  position, and the order completes with no effect, cost or message, whether or not that
  facility still exists. So `Emergency Resupply` and `Emergency Energy` on facilities are inert;
  no stock facility has them. Colony orders run on day 1 of a simultaneous movement phase
  (§6.3). In a turn-based game the immediate run after an order covers vehicle lists only. The
  Use Facility order therefore stays in the colony's list until the colony's orders next run:
  at the start of the owner's next turn, or when the player gives a colony order that runs the
  list at once (Convert Resources, spec 02 §5.6; Abandon Planet; Launch or Recover Units
  Remotely). Its only visible effects are that, in a turn-based game, giving it clears the colony's
  other orders, and that it shows in the colony's order list until it runs.
- **The engine follows this since 2026-10-01**, with the pickers above. The clearing in a
  turn-based game is the Clear Orders clearing, so it also switches Repeat off (confirmed:
  binary, §19 Q77).

**Convert Resources** is a colony order with economic effects; its rules are in spec 02 §5.6.
Scrap Facilities and Abandon Planet are also in spec 02. The movement-log replay orders are UI
only.

### 8.1 Waypoints

- A waypoint is an empire-owned, named sector reference. Each empire has exactly 10, slots 0–9,
  addressed by the hotkeys (confirmed: binary).
- A waypoint supports rename, delete and re-set. The window lists the ships en route to it and the
  space yards whose **automatic Move To** targets it.
- A construction queue (planet or ship) can have an automatic Move To waypoint. Every newly built
  vehicle then receives a plain Move To the waypoint's sector, fixed at build time; nothing is
  added if the waypoint is unset (confirmed: binary).
- Deleting a waypoint clears the slot. A pending Move To Waypoint that refers to it fails when it
  is reached, which clears that vehicle's list. Move To orders already given by yard auto-move are
  unaffected, since they hold the sector (confirmed: binary).

---

## 9. Fleets

All rules in this section are (confirmed: binary) unless marked otherwise.

- An empire can have any number of named fleets. The name gets a default and is editable. A new
  fleet starts with the first formation and the first strategy. A fleet is deleted when its last
  member at its location leaves (below).
- A fleet has one location. Its members are the owner's vehicles in that sector that carry the
  fleet's tag. The location is the fleet's own record: it is set where the fleet is formed and
  follows any member that moves, whatever moved it (a group step, a warp, stellar drift, an
  event), so it is the sector of the member that moved last.
- When a vehicle leaves a fleet, or is removed from the game, and no member is left at the
  fleet's location, the fleet is disbanded: every vehicle still carrying its tag leaves it,
  normally losing its orders. The `Ship - Moved` event moves the ship first and then takes it out
  of its fleet, so the fleet's location goes with it and the whole fleet is disbanded.
- **Who can join:** ships always; bases only when `Bases Can Join Fleets` is True; fighter groups
  yes; drones, satellites and mines never. Fleet Transfer offers only own vehicles in the same
  sector that are not already in a fleet, mothballed ones included. "Add All" and "Remove All"
  are available. Joining or leaving a fleet clears the vehicle's own orders (below). A vehicle
  in a fleet cannot be mothballed, scrapped, analyzed or retrofitted, because the Scrap window
  does not list it (§15, §19 Q74).
- **Leader:** the player-chosen leader when one is set, otherwise the first member in the sector's
  object order. If the chosen leader leaves or is destroyed, the choice is cleared and the first
  member leads again. The leader only matters for the formation.
- **Movement:** the fleet's movement is the minimum of its members' MP left (and of their maximum
  MP for display), over the members at its location. During turn processing every member's MP
  there is set to that minimum, so the fleet moves at its slowest member's speed; a member with
  0 maximum MP freezes the fleet. In a simultaneous game every member, wherever it is, gains
  day credit at that speed (§6.3). Members at the fleet's location execute the fleet's orders
  as one group; the orders are copies held in each member's own list, and members elsewhere are
  not part of the group (§8).
- **Orders on joining and leaving:** joining a fleet (by Fleet Transfer or the Join Fleet order)
  clears the vehicle's list, and so does leaving it by Fleet Transfer. The joining vehicle does
  not get the orders the fleet already has (§8, §19 Q65).
- **Supply display:** the Fleet Report sums the members' current supply, maximum supply and
  per-move cost, leaving out fighter groups and members with unlimited supply, and shows "Endless"
  when every member has unlimited supply. Supply is pooled among the members in the fleet's
  sector at the end of each turn (§7).
- **Strategy:** in combat a member (ship or fighter group) uses the fleet's strategy while it is
  in the fleet's combat group, and its own design's default otherwise (for a unit group, its first
  design's). Only armed members (at least one weapon component, not mothballed) join the combat
  group; unarmed members are placed at random and use their design strategy. How members leave
  the group and when it dissolves is in §10 and spec 04 §4 and §16 (confirmed: binary).
- **Experience:** a fleet has its own fractional experience value, capped at 50 (a gain past 50
  sets it to 50). It is not reduced when members join or leave; it is lost only when the fleet is
  deleted. The training abilities raise it (§3.3). Whenever a member ship gains experience from a
  kill in combat or from a ram (spec 04), there is a 1-in-4 chance that its fleet gains 0.1
  (confirmed: binary). The fleet's experience adds to its members' to-hit (spec 04).

---

## 10. Formations

All rules in this section are (confirmed: binary) unless marked otherwise.

- The fleet's formation takes effect only in **combat**.
- The fleet's armed members (see §9) form its combat group. The fleet leader anchors it and takes
  the leader cell, even when it is unarmed; if the leader is not in the battle, the first armed
  member in piece order anchors instead. The other armed members get positions 1, 2, 3… in the
  order their pieces enter combat. Nothing breaks formation at placement. Members numbered beyond
  the formation's position count get no formation place.
- A position's `Type` and the `Leader Design Type` are never used for placement; they are for
  display only.
- A member's target cell is the leader's cell plus the offset (dx, dy) = its position − the leader
  position, rotated by the leader's facing, one of 8 directions:

  | Facing | Offset used |
  |---|---|
  | 0 | (dx, dy) |
  | 1 | (−dy, dx) |
  | 2 | (−dx, −dy) |
  | 3 | (dy, −dx) |
  | 4 | (dx − dy, dx + dy) |
  | 5 | (dx + dy, dy − dx) |
  | 6 | (−dx − dy, dx − dy) |
  | 7 | (dy − dx, −dx − dy) |

  The diagonal facings are not rescaled, so they stretch the pattern by about 1.41. The result is
  clamped to the combat map (x 0..71, y 0..62).
- At most 100 positions are read per formation record.
- Members leave the formation only during automated moves (every piece in strategic combat): a
  piece leaves when its strategy in effect is Don't Get Hurt, Drop Troops, Board or Ram, or when
  the strategy's `Break Formation` flag covers its category (spec 04 §16.1). The whole group
  dissolves when the leader is destroyed or removed from the group, when the leader of an automated
  side survives a hit (even one its shields absorb completely) and has no movement left this combat
  turn once its movement is capped at its new maximum, or when the leader's turn to act comes in an
  automated phase and every square on the map around its footprint is already occupied. A
  computer-played piece has no movement left once it has acted, so any hit on the leader after its
  action in that combat turn is enough, as is any hit on a leader that cannot move. The surrounded
  test runs only then, before the leader plans and moves, and the leader then does not move; there
  is no such test after a move (spec 04 §16.1). The computer players' sides are automated; so is
  every side in strategic combat, in a battle fought without a window, in tactical combat while the
  Auto button is down, and after Resolve Combat (§19 Q60). When the leader itself leaves formation,
  only its own marks are cleared: the members stay in the group, keep the fleet strategy, but have
  no leader to follow and move on their own. In tactical combat the player's combat group orders
  also change groups (spec 04 §5).
- Fleet Transfer and Change Formation\Strategy select the formation. The Formation Report draws the
  grid.

---

## 11. Cargo

All rules in this section are (confirmed: binary).

- **Holders:** only ships, bases and colonized planets hold cargo. Unit groups in space (fighters,
  satellites, mines, drones) hold none.
- **Vehicle capacity:** the Sum of `Cargo Storage` over the vehicle's ability list (hull and
  working components). Bays, colony modules and mine layers count only if they carry
  `Cargo Storage` themselves. Free space = max(0, capacity − used).
- **Planet capacity:** the planet size's cargo spaces (the domed value if domed) + `Cargo Storage`
  of the planet's own abilities + `Cargo Storage` of its facilities, then × (100 + the racial
  storage trait) %, truncated. An uncolonized planet gets neither the facility part nor the racial
  factor.
- **Footprint:**

  | Item | Space used |
  |---|---|
  | Population | `Population Mass` (5) kT per 1M |
  | A unit | its hull `Tonnage` |

  Room for population = free space ÷ `Population Mass`, rounded down.
- **One pool:** any unit kind fits in any cargo space. Bay abilities only allow launch and
  recovery.
- **Damage:** when capacity drops below the amount used, cargo is removed until it fits:
  population first, 1M at a time from the first population entry, then units one at a time from
  the first unit stack. Cargo in transit is inert: it does not fight or grow, and troops cannot
  defend.
- **Transfer:** only between the player's own ships, bases and colonized planets in the same
  sector that have capacity above 0; never with allies. It costs nothing and uses no MP. It moves
  1, 5, 10, 100 or all per click and only what fits. Nothing can be loaded from a planet
  quarantined by plague. Taking population from a planet always leaves at least 1M; emptying a
  colony needs the Abandon Planet order.
  - **The window** (Cargo Transfer, order T, lit for every own ship, base and colony in both
    turn styles, spec 06 §2.8). The left list ("Cargo From") holds the selected ship, base or
    colony, and with a fleet member its fleet-mates at that place; the right list ("Cargo To")
    every other qualifying holder in the sector, other own colonies included (every planet size
    has cargo spaces, so every own colony there takes part). Each holder's row is followed by its
    cargo lines: for a colony one line per race of its population, then its stored cargo. A list
    keeps its selected holder when refilled, else selects its first. A click on a cargo line
    moves it from that line's holder to the holder selected in the other list.
  - **Population between two colonies** goes straight from population to population, not into
    cargo: only the clicked race moves, at most what that race has there, capped by the target's
    free population room (its maximum population, spec 02, less its current population, never
    below 0); cargo space plays no part. The source keeps at least 1M of its total: when the
    amount would take all of it, one less than the total moves. Population from a colony to a
    ship goes into the ship's cargo, capped by its room for population; population carried as
    cargo, on a ship or stored by a colony, moved to a colony goes into its population, capped by
    the same free population room. Units need free cargo space in the target.
  - The Population window ("Races On Planet") is read-only.
- **Captured or gifted ships** keep their cargo.

---

## 12. Units: launch, recovery and life

All rules in this section are (confirmed: binary) unless marked otherwise.

- **Launchable kinds:** fighters, satellites, mines and drones. Troops and weapon platforms are
  never launched (troops drop in ground combat; platforms go to planets as cargo or are built
  there).
- **Who can launch:** a vehicle with the matching ability (`Launch/Recover Fighters`,
  `Launch/Recover Satellites`, `Lay Mines`, `Launch Drones`), or any colonized planet, which needs
  no ability.
- **Per game turn** (outside combat): each launcher has a budget per unit kind equal to the Sum of
  `Val 2` over its working launchers (no fallback to `Val 1`); a colonized planet's budget is
  1000. The counts reset every turn.
- **Per combat turn:** a ship launches up to the Sum of `Val 1`; a planet up to 100 of each kind it
  carries. The per-turn budget and the units-in-space cap do not apply in combat.
- **Recovery:** only fighters and satellites can be recovered. A ship or base needs the matching
  ability (`Launch/Recover Fighters` or `Launch/Recover Satellites`); a colonized planet needs
  none. There is no per-turn limit; free cargo space is the only limit. In turn-based games a
  fighter group can be recovered only while its MP left equal its maximum.
- **Caps:**
  - Mines and satellites per sector: if the owner's mines (satellites) in the sector are already at
    `Maximum Mines Per Player Per Sector` (`Maximum Satellites Per Player Per Sector`), the launch
    is refused; otherwise it is reduced to the remaining room.
  - Units in space: before each launched stack, the empire's number of units in space must be below
    the game's units-per-player option (default `Default Number Of Units Per Player`). The launch is
    not reduced to fit, so one launch can go past the cap. Units in cargo and under construction do
    not count.
  - Ships: building a ship or base is refused while the empire has at least the game's
    ships-per-player option (default `Default Number Of Ships Per Player`).
- **Grouping:** units in space are held in groups; different designs share a group. Launching
  outside combat adds the units to the **last** group of the same kind and owner in the sector's
  object order, whatever its designs, fleet, orders or cloak, and refills that group's supply to
  its new maximum; with no such group a new one is made, which starts full. Every drone is
  launched as its own group. Groups never merge otherwise: a group moving into a sector where
  another of its kind waits, the end of the turn and combat all leave them apart. In combat each
  launch action (one launcher in one combat turn) makes a new group holding everything it
  launched, and groups that no carrier recovers afterwards stay as they are.
- **Movement of a new group:** it starts with 0 MP. In a turn-based game the launch then sets its
  MP to its maximum, so it can move, and be recovered, in the same turn; in a simultaneous game it
  stays at 0 and the group does nothing more that turn. Units added to an existing group do not
  change its MP. (In combat launched units move at once, spec 04 §5.)
- **Group abilities** list each unit's design abilities once per unit, so summed abilities scale
  with the number of units. A group's cloak and sensor levels are, per sight type, the best level
  of any of its units (a cloak level at least 1, an `EM Active` sensor level at least 1); a unit
  group's cloak level applies whether or not the group is flagged as cloaked.
- **Group supply:**
  - Maximum = the sum over units of each unit's `Supply Storage`.
  - Upkeep each turn: fighters pay count × `Fighter Supply Usage Per Turn`, or, while cloaked,
    count × the cloak parts' `Supply Amount Used` instead; drones pay `Drone Supply Usage Per Turn`
    plus cloak supply when cloaked. No racial scaling applies to upkeep.
  - Per-move cost = Σ over designs of (the design's per-move cost × count), then the racial
    `Supply Cost` percentage, rounded.
  - Group MP = the lowest design speed in the group, or 1 at zero supply.
- **Resupply:** a fighter group is refilled when it shares a sector with a colonized planet that has
  a `Supply Generation` facility, owned by the same empire or by one whose treaty allows depot
  use. Drones can never be resupplied. Satellites and mines have no supply at all (maximum 0, no
  upkeep) and never run out.
- **Out of supply:** a fighter group drops to 1 MP and is **not** destroyed. A drone group is
  destroyed when its supply reaches 0.
- The low-supply warning for fighter groups uses `Supply Amount for Low Supply Warning` ÷ 10.

| Unit | Notes |
|---|---|
| Fighter | Moves like a ship inside its system; cannot use warp points (§6.2). |
| Satellite | Stationary. Fires on enemies in its sector. |
| Mine | Hidden: hull cloak level 5 in all sight types (stock data). Strikes hostile groups that enter the sector (below). Its owner sees it, as every owner sees its own objects. |
| Drone | A launch takes no target and adds no order. A drone group follows orders like any vehicle: an Attack order makes it pursue its target, through warp points too, and ram it in combat (§8, spec 04 §10.7). When a battle starts, a drone group's target is the object named by the first order in its list, if that order is an Attack pursuit and the object (of any kind) is in the battle; otherwise its target is chosen as for a computer player's piece (confirmed: binary). Out of supply it is destroyed, after each step or warp and at the end of the turn. |

- **Mine encounter.** When a group enters a sector (by a step or a warp arrival), and when it runs
  a Sweep Mines order, the minefields there act, unless some vehicle of the group belongs to the
  mine owner or to an empire the mine owner rates at Non-Aggression or better (the mine owner's
  side of the treaty); so mines strike at War, Non-Intercourse, None and no contact. Sweeping
  comes first: if the group has a sweeper and the sector is one of its owner's tagged minefields,
  its cloaked members decloak; then the total `Mine Sweeping` of the group's uncloaked members
  removes that many hostile mines, minefield by minefield in sector order and design by design in
  the order the designs joined. Then every remaining hostile mine strikes a random vehicle of the
  group, cloaked ones included, using its own design's warheads, and is used up, as spec 04 §10.6
  describes (fighter and drone groups are skipped unless `Fighters Can Be Hit By Mines` /
  `Drones Can Be Hit By Mines` allow them). If mines struck, the move stops and the order fails.
- **After combat:** each carrier recovers the fighter and satellite groups it launched itself in
  that battle, ships first and then planets, in piece order, with every design of each group,
  as far as its free cargo allows. It needs to still have a working bay of that kind (a colonized
  planet needs none); the turn-based full-movement test does not apply. Groups launched by
  others, mines and drones stay in space.
- **Self-destruct:** satellite groups, minefields and drone groups can self-destruct at any time
  with no ability; fighter groups never can. Drone groups and minefields cannot be scrapped
  (§15).

---

## 13. Repair

All rules in this section are (confirmed: binary) unless marked otherwise.

- **When:** once per turn, in each empire's end-of-turn processing, after that empire's
  maintenance has been paid (§16) and before its supply step (§7). In simultaneous games this is
  after the whole turn's movement and combat; in turn-based games the empire's end-of-turn
  processing runs when that player ends the turn, after its own moves.
- **Where and how much:** repair is pooled per (empire, sector). The pool is the Sum of
  `Component Repair` V1 over all of that empire's own objects in the sector: ships, bases, unit
  groups (their abilities) and colonized planets (their own abilities and their facilities). A
  colony needs no population or anything else to count. Allies' sources do not count. Repair is
  free.
- **Points:** repair points = truncate(pool × (100 + R) %), where R is the empire's repair
  modifier: racial trait values, plus (Repair Aptitude − 100), plus the culture's repair value,
  added together before the one truncation.
- **Who:** the pool is shared by the empire's damaged ships, bases and unit groups in the sector,
  taken in order of creation. Each point restores one destroyed component to working order.
- **Selection:** for each vehicle, the empire's repair priority groups (§2.7, player-editable) are
  taken in list order. Within a group, the vehicle repeatedly repairs the first destroyed component
  of that `General Group`, in design order, until none is left or the points run out. After all
  listed groups, any remaining destroyed components are repaired in design order.
- A component whose technology the owner has not researched (for example a captured alien part)
  cannot be repaired.
- Components with `Emergency Energy` or `Emergency Resupply` can be repaired only when an own space
  yard is in the sector. Other used-up one-shot components are repaired like any other.
- The status icons "Under Repair" and "Can Repair" follow from this.

---

## 14. Retrofit

All rules in this section are (confirmed: binary).

Retrofit is an order that is carried out during turn processing. When it runs, these checks are
made in order and the first failure cancels it:

1. The retrofit cost (below), summed over the three resources, is 0: the designs are identical.
2. No own space yard is in the sector. A planet facility counts, and so does a ship's `Space Yard`
   component while that ship is not cloaked.
3. The target design uses a different hull.
4. The vehicle carries any cargo.
5. The empire's stock of any resource is below that resource's retrofit cost.
6. `No Retrofit Adding Of Spaceyards` is set, the current design has no `Space Yard` and the
   target has one. The same test applies to the colonize abilities under
   `No Retrofit Adding Of Colony Module`.
7. The target design's total cost (all three resources added) exceeds the current design's total
   × (100 + `Retrofit Max Percent Difference in Cost`) %, compared in floating point. Only an
   increase is limited; a cheaper target is always allowed.

Cost, per resource:

- Walk the target design's components in order. Pair each with the first not-yet-paired entry of
  the current design that has the same component **and the same mount**.
- Each unpaired target component adds truncate(its mounted cost × `Retrofit Cost Percent For
  Comps` %).
- Each current component left unpaired adds truncate(its mounted cost × `Retrofit Cost Percent For
  Comp Removal` %).
- The hull costs nothing.

Effect:

- The vehicle's design changes at once.
- Paired components keep their working or destroyed state. Every unpaired target component starts
  **destroyed** and must be repaired (§13).
- Movement and supply are recomputed and clamped to the new maxima.
- The cost is taken from stock only if at least one component was added, so a retrofit that only
  removes components is free.
- Damage and mothball status are not checked.

---

## 15. Scrap window actions

Each action applies to the selected own vehicles in one sector. The window lists only own
vehicles in the sector that are in no fleet and not cloaked (§19 Q74). Scrap, Analyze, Mothball
and Retrofit need an own space yard in the sector and a vehicle that is not cloaked. All rules
in this section are (confirmed: binary).

| Action | Requires | Effect |
|---|---|---|
| Scrap | a space yard, not cloaked | Destroys the vehicles. A ship or base returns, per resource, round(design cost × P %), where P is the larger of `Scrap Ship Percent Returned` and the best `Resource Reclamation` among the owner's objects in the sector. A fighter or satellite group returns round(design cost × `Scrap Unit Percent Returned` %) per unit, times the number of units. Drone groups and minefields cannot be scrapped. Damage does not lower the value. Cargo on board is lost without refund. |
| Analyze | a ship or base (never a unit group), a space yard, not cloaked | Destroys the vehicle, returns no resources, and raises the owner's technology from the vehicle's parts. Rules under **Analyze** below. |
| Mothball | a space yard, not cloaked, status Normal, no cargo | Status becomes Mothballed: no abilities, 0 movement, 0 supply, no maintenance. |
| Unmothball | status Mothballed | Costs round(design cost × `UnMothball Ship Percent Cost` %) per resource, and every resource must be in stock. The vehicle returns to Normal. Bases and vehicles with a working `Quantum Reactor` come back with full (unlimited) supply; others are refilled only if they are at a resupply depot and otherwise stay at 0. |
| Retrofit | a space yard | §14. |
| Self-destruct | a ship or base: `Self-Destruct` in its ability list (the hull counts; a mothballed vehicle has none); satellite groups, minefields and drone groups: nothing; fighter groups: never allowed | The whole object is destroyed. No yard is needed and nothing is refunded. |
| Fire On | another own armed vehicle in the sector | The vehicle is removed outright, with no battle. Rules under **Fire On** below. |

"round" is to nearest with halves to even, computed in floating point (see Conventions).

**Carrying out the actions** (confirmed: binary). Scrap, Analyze, Mothball, Unmothball, Retrofit,
Self-Destruct and Fire On are given to every selected vehicle, one after another in list order.
A button is lit only when every selected vehicle passes its test (the window is spec 06 §1.3).
- **Turn-based game.** Each vehicle's action is carried out at once, in that order. The test is
  made again for each vehicle just before its action, so the action on one vehicle can change
  the outcome for the next (see Fire On). The vehicle's order list is not touched. Analyze and
  Fire On neither need nor spend movement, and a vehicle that fails their test is left alone,
  with no message.
- **Simultaneous game.** Nothing happens while the player gives orders. Each selected vehicle's
  order list is cleared (Repeat goes off) and the action becomes its only order, shown in the
  order list as "Scrap / Analyze / Mothball" (Scrap), "Deconstruct & Analyze", "Mothball",
  "Unmothball", "Retrofit", "Self-Destruct" or "Fire On And Destroy". Orders given afterwards are
  added behind it. The order is carried out when the vehicle first acts during the turn's
  movement (§6.3): a vehicle with a maximum of 0 movement (a mothballed one, for instance) acts on
  day 1, any other on its first action, and a vehicle that never acts (speed 1) never carries
  it out. The test is made again then: if it passes, the action happens and the order is done;
  if it fails, the order fails and the whole list is cleared (§8), with no message from Analyze
  or Fire On. Analyze and Fire On act on the acting vehicle only, even when it heads an
  ad-hoc group (§8), so each vehicle carries out its own order.
- No computer player and no minister ever gives an Analyze or Fire On order; only the player's
  Scrap window does.

**Analyze** (confirmed: binary).
- **Which vehicles.** Ships and bases only: the Analyze button stays dim while a fighter,
  satellite, drone group or minefield is selected. The vehicle needs an own working space yard in
  its sector (an uncloaked own ship with `Space Yard`, the vehicle itself included, or an own
  uncloaked colony with a `Space Yard` facility; spec 01 §6.9) and must not be cloaked. Its design,
  its origin (built, captured, received as a gift), its damage, its cargo and a mothballed status
  do not matter. Fleet members are not listed, so they cannot be analyzed.
- **What the vehicle can teach.** For the vehicle V of empire E, build a list P of pairs
  (area A, level L), once, before anything changes:
  1. for each component of V's design, in design order, skipping destroyed ones, take each of the
     component's tech requirements (`Tech Area Req N`, `Tech Level Req N`) in order;
  2. then each tech requirement of V's hull (vehicle size);
  3. a pair is appended when E's current level in A is below L and the same pair (same A and
     same L) is not yet in P.

  Mounts, the units or population in cargo, and the design's other data are not read. Two
  components that need the same area at the same level give one pair; at different levels they
  give two.
- **Research potential.** The Scrap window shows a word for the number of pairs in P: 0 "None",
  1 "Minor", 2 "Moderate", 3 "Sizable", 4 or more "Major". The value is per vehicle and is not
  summed: the window shows that of the last selected vehicle in list order, and "None" when any
  selected vehicle cannot be analyzed or nothing is selected.
- **What Analyze grants.** For each pair (A, L) of P in order, E gains exactly one level in A,
  whatever L is, provided A is allowed in this game and passes its racial and unique checks for
  E (the same test as every other level gain, spec 05 §1.2). L itself is not used. So the levels
  gained in A are the number of distinct required levels of A above E's level, and A never rises
  above the highest level of A that V's parts require. Examples: E has level 2 in an area; the
  parts need it at 3, 5 and again 5, and the hull at 4: P holds three pairs and E ends at level 5.
  If a single part needs level 6, each analysis gives one level: 3, then 4 with a second vehicle
  of that design, and so on until 6.
- **No research points.** No points are added to the pool or to any project, and no cost is
  checked. The area's `Maximum Level` and its own area requirements are not checked: an area can
  gain levels before it becomes researchable, and appears with them once its requirements are
  met. A queued project for A keeps its progress, which now counts toward A's new next level; a
  project whose area has reached its maximum leaves the queue at the next research step (spec 05
  §1.4).
- **Messages.** Each level gained writes the same Research entries as a level completed by
  research (spec 05 §1.4), in this order: "New Tech Level" with the area's new level, then one
  entry per component, facility and vehicle size that became available ("... discovered") and
  per intelligence project ("... developed"), then "New Tech Area Discovered" for each area whose
  requirements are now met. "All Projects Completed" is never written. There is no entry about
  the vehicle itself, so a vehicle with no potential disappears without any log entry.
- **The vehicle** is removed like a scrapped one: no refund, cargo lost, sight recalculated. Its
  design's "Number Scrapped" statistic rises by 1. A captured ship keeps its original design
  (spec 04 §12), so this counts on the design record of the empire that built it.
- **Designs.** Analyze adds no design to any empire and changes no design list or "seen designs"
  record; what is learned is technology levels only.

**Fire On** (confirmed: binary).
- **Test.** In the target's sector there must be at least one other vehicle (ship, base or unit
  group) of the same owner that counts as **armed**:
  - a ship or base that is not mothballed and whose design has at least one component of Weapon
    Type `Direct Fire` or `Seeking`. Destroyed components count; `Point-Defense` and `Warhead`
    components do not;
  - a fighter group whose fighters' designs have at least one component of any Weapon Type other
    than `None` (`Point-Defense` and `Warhead` count here);
  - satellite groups, minefields, drone groups and planets never count, whatever they carry.

  The armed vehicle may be cloaked, in a fleet, out of supply or out of movement, and may itself
  be selected. Nothing else is tested: no treaty, range, sight, supply or yard, and the target
  may be any listed vehicle, unit groups and minefields included.
- **The last armed vehicle.** Because the test needs another armed vehicle, the last armed
  vehicle in a sector can never be fired on. When several armed vehicles are selected the
  button is lit (each sees the others), but the one carried out last finds no armed companion
  left: in a turn-based game it stays, and in a simultaneous game its order fails.
- **Effect.** The target is removed at once, as by scrapping. Nobody fires: there is no battle, no
  shot, no damage roll, no movement or supply spent, no experience and no kill statistic. Cargo
  aboard is lost. The system's sight is recalculated.
- **Statistics.** A ship or base adds 1 to its design's "Number Lost". A fighter group, satellite
  group or minefield adds each unit stack's living units to that unit design's "Number Lost".
  A drone group records nothing.
- **Log.** The owner gets one Construction entry, with Goto to the sector, from the demolition
  minister: titled "Vehicle Destroyed" for a ship, base or drone group (with the design's
  picture for a ship or base), or "Group Destroyed" for a fighter group, satellite group or
  minefield (with that unit kind's picture). The text names the vehicle, its system and its
  sector and says that our own ships fired on it and destroyed it.

**The engine and client follow this section since 2026-10-01** (`src/game/scrap.*`):
- Each action is a command on one vehicle (`cmd::Scrap`, `cmd::Analyze`, `cmd::Mothball` for
  Mothball and Unmothball, `cmd::Retrofit`, `cmd::SelfDestruct`, `cmd::FireOn`), tested by
  `scrapActionProblem` when it is given. A turn-based game carries it out at once and leaves
  the order list alone; a simultaneous game clears the list, switches Repeat off and leaves
  the action as the only order (order kinds `Scrap`, `Analyze`, `Mothball`, `Unmothball`,
  `Retrofit`, `SelfDestruct`, `FireOn`), which movement carries out at the vehicle's first
  action, testing it again (a failure clears the list, silently for Analyze and Fire On).
  The window's orders cannot be given through `cmd::SetOrders`; a list keeps those it holds.
- Analyze grants one level per pair of `analyzePairs` (`research::analyzeLevel`, which
  checks only `canGainLevel`), and the level entries of research (`research::grantLevel`
  now writes "... Developed" for intelligence projects and "New Tech Area Discovered" too).
  Designs keep "Number Scrapped" (`Design::scrapped`), raised by Scrap, Analyze and
  Self-Destruct (spec 04 §15); Fire On raises "Number Lost" and writes one Construction
  entry.
- The client's Scrap window shows the research potential word of the last selected vehicle
  in list order, Can Self-Destruct and Can Be Fired On only when every selected vehicle can,
  and lights each button only when every selected vehicle qualifies (Retrofit: one design,
  a yard, no cloak), giving the actions in list order (`scrapWindowState` in
  `src/client/classic/screens/ships_logic.cpp`).

OpenSE4's own choices, (inferred):
- Computer players' and ministers' Scrap and Retrofit go through the same commands, so in a
  simultaneous game they too become the vehicle's only order. How the original's ministers
  carry them out is open.
- Scrap and Self-Destruct count each living unit of a unit group in "Number Scrapped".
- A self-destructed vehicle still gets its "destroyed" Misc entry, but raises no "ship lost"
  mood (it is not lost).
- The Fire On entry has no picture (our log keeps pictures only for events), and Unmothball's
  button needs the cost in stock as well as the Mothballed status.

---

## 16. Maintenance (vehicle side)

All rules in this section are (confirmed: binary).

- Only ships and bases pay. Units, in space or in cargo, and mothballed vehicles pay nothing.
- The empire's percentage is max(5, `Empire Starting Percent Maint Cost` − M), where M is the
  empire's maintenance modifier (racial trait values, plus Maintenance Aptitude − 100, plus the
  culture's maintenance value). It does not change during a game otherwise.
- Per vehicle and resource, three successive truncations:
  1. m = truncate(design cost × percentage %);
  2. m = truncate(m × H %), where H = 100 + the Sum of `Modified Maintenance Cost` over the
     vehicle's ability list (hull and working components);
  3. m = truncate(m × F %). F starts at 100; for each object of the owner in the vehicle's system,
     compute 100 − that object's Smallest `Reduced Maintenance Cost - System` (0 if it has none),
     and F is the smallest of these values. (The original uses 100.0 instead of 1.0 as the factor
     for a vehicle whose system index is invalid; this only affects vehicles outside any system and
     SHOULD NOT be copied.)
- Payment: each resource pays what is in stock. The shortage is the sum of the unpaid amounts over
  the three resources.
- If there is any shortage, min(candidates, shortage ÷ `Maintenance Cost Amt Per Dead` + 1)
  vehicles are destroyed (integer division, so a shortage of 1 already destroys one). Each is picked
  uniformly at random. Candidates are the empire's ships and unit groups that have 0 supply and a
  nonzero upkeep; if there are none, any ship or unit group, mothballed ones included. A unit group
  is destroyed whole.

---

## 17. Reports and lists (data the UI needs)

- **Ship Report, Detail tab:** owner flag, picture, status icons, name, class, hull name
  (tonnage), movement remaining/max, damage taken/max, supply/max, crew experience with its bonus
  %, design type, fleet, maintenance cost. Details are hidden for foreign ships.
- **Other Ship Report tabs:**
  - Comps: components with destroyed ones marked, and "damaged X of Y".
  - Cargo: items with counts, and space used/max.
  - Ability: hull and racial abilities.
- **Ships window:** filters Ships, Units (groups in space) and Fleets, with these tabs:

  | Tab | Columns |
  |---|---|
  | General | picture, name, size, type, move, damage, supplies |
  | Orders | class, orders |
  | Cargo | space, max, list |
  | Fleet | experience, fleet |
  | Maintenance | cost per resource |

  The window also shows total maintenance.
- **Fleet Report:** name, movement, supply pool, fleet experience, formation, strategy, member
  count, and the member list with the leader marked. Clicking a member makes it the leader; a
  right-click opens its Ship Report as a popup. In the report panel's sector list the player's
  own fleet is one row that opens this report (spec 06 §2.5) (confirmed: binary).
- **Component and hull reports:** every field from §2.2 and §2.3, the allowed vehicle types, and
  ability descriptions.
- **Status icons:** Space Yard, Repeat Orders, Sentry, Low Supplies, No Supplies, Mothballed,
  Cloaked, Minister, Can Repair, Constructing, Under Repair, Damaged, and one "Has X in cargo"
  icon per cargo kind (fighters, satellites, mines, drones, population, troops, weapon platforms).
- **Turn-based cycling:** "next ship" cycles through ships that still have MP. In simultaneous
  mode it cycles through ships that have no orders.

---

## 18. Cross-spec hooks

- Combat uses: shields (normal and phased), armor ordering, to-hit abilities, `Multiplex Tracking`,
  `Combat Movement`, the zero-supply penalties, unit launch rates, ram and warhead rules, boarding,
  and formation placement.
- Sight uses: `Cloak Level` and `Sensor Level` semantics and the long-range scanner.
- The economy uses: `Space Yard` rates, construction placement of new vehicles and units, and
  maintenance.

---

## 19. Open questions to verify in the running game

1. **`Engines Per Move`:** is it really a divisor (engines per MP)? All stock mobile hulls use 1
   and immobile hulls use 0. **Answer:** yes. The summed `Standard Ship Movement` values are divided
   by it with integer division; 0 means no movement (§6.1) (confirmed: binary).
2. **Mixed engine bonus:** does mixing +3 and +2 engines give +2 or +0? Do destroyed engines count
   toward the "all engines" test? **Answer:** +2. The bonus is the smallest `Movement Bonus` among
   all working entries on the ship; components without the ability are ignored (with none at
   all the bonus is 0), and destroyed engines are not counted (§6.1) (confirmed: binary).
3. **Zero supply or lost control:** is MP exactly 1 even when the ship has no working engines?
   Does "no bridge/LS/CQ" mean zero undamaged components, or fewer than the hull minimum? Does
   Auxiliary Control stand in for a lost bridge? **Answer:** no working engines means 0 MP; the
   zero-supply rule only turns a positive maximum into exactly 1. Lost control means none left
   (not "below the hull minimum"), is tested even when the hull has no minimum, and halves the
   maximum once per missing item (never below 1) rather than setting it to 1. Auxiliary control
   stands in for the bridge (§6.1) (confirmed: binary).
4. **Master Computer:** does it lift the design requirements for bridge, life support and crew
   quarters, or only the in-flight penalty? **Answer:** both. A design with a Master Computer
   skips the bridge, auxiliary-control, life-support and crew-quarters rules (not the engine
   rules), and a working one prevents the in-flight halvings (§4.2, §6.1) (confirmed: binary).
5. **Warping:** does a warp cost 1 MP and engine supply, or is it free on arrival at the warp
   point? Is diagonal movement allowed at 1 MP? **Answer:** a warp costs 1 MP and one step's
   supply; diagonal steps cost 1 MP like straight ones (§6.2) (confirmed: binary).
6. **Supply per step:** is engine supply charged per sector moved or once per turn of movement? Do
   fighters and drones pay for movement on top of their per-turn cost? Is a cloak's
   `Supply Amount Used` charged per turn while cloaked? **Answer:** per sector moved and per warp
   (also per Attack step and Sweep Mines order). Units pay per step as well as their per-turn
   upkeep. The cloak is charged at every end of turn while cloaked (§7, §12) (confirmed: binary).
7. **Fleet supply pooling:** how is the pool redistributed? Does pooling work across sectors?
   **Answer:** equal shares capped by capacity, overflow handed out in member order, remainder of
   the division lost; only among members in the fleet's sector, only at end of turn (§7)
   (confirmed: binary).
8. **Simultaneous day schedule:** the exact rounding for MP that do not divide 30, and processing
   order within a day. Does a combat interruption clear orders in simultaneous mode?
   **Answer:** a per-vehicle counter gains speed / 30 each day and the vehicle acts when it reaches
   1; combat does not clear orders (§6.3) (confirmed: binary). The counter is a double updated in
   x87 extended precision; the game never lowers that precision (confirmed: binary), and turn
   processing was measured at it in the running game, where a speed-6 ship's counter matched the
   arithmetic bit for bit (observed). So many speeds lose their day-30 step and some steps come a
   day later: speed 1 never moves, speed 5 moves on days 7, 13, 19 and 25, and only speeds 8, 10,
   12–15, 24, 26, 28, 30 and above keep the exact schedule. §6.3 gives the arithmetic and the full
   table. Within a day objects act in object-slot order, and one action runs orders until one
   waits (done orders chain) (§6.3) (confirmed: binary).
9. **Systems To Avoid** when no avoid-free route exists: refuse the order or go through?
   **Answer:** the Move To fails and the list is cleared; there is no fallback (§6.2)
   (confirmed: binary).
10. **Launch rate:** is the per-game-turn launch rate (V2) enforced in the Launch/Recover window?
    Do planets need any ability to launch? Can fighters sit in plain cargo bays and satellites in
    fighter bays? **Answer:** yes for launches (window and orders), not for recovery, which is
    limited only by cargo space. Planets need no ability (1000 per game turn, 100 per combat turn
    per kind carried). Cargo is one pool, so both are allowed (§11, §12) (confirmed: binary).
11. **Population cargo footprint:** is `Population Mass` = kT per 1M? Which cargo items are lost
    first when cargo space is destroyed? **Answer:** yes, kT per 1M. Population goes first, 1M at a
    time, then units one at a time from the first stack (§11) (confirmed: binary).
12. **Percentage design rules:** exactly which components count toward `Pct Cargo`,
    `Pct Fighter Bays` and `Pct Colony Mods`? Is there a one-bridge maximum? Is at least one engine
    mandatory on `Uses Engines` hulls? **Answer:** components with `Cargo Storage`,
    `Launch/Recover Fighters` and the colonize abilities respectively, by mounted size, against
    truncate(T × p %). A hull that requires a bridge needs exactly one; aux control is limited to
    one when the hull allows it. No minimum engine count exists (§4.2) (confirmed: binary).
13. **Hull plus component to-hit:** do the bonuses add, or does only the best one count? Do hull
    `Defense Minus` and component `Defense Plus` net out? **Answer:** the hull's value always adds;
    components add the best value of each component family, summed across families; the result is
    Plus − Minus, so they net out (§3.2) (confirmed: binary).
14. **Repair:** is throughput pooled per sector or per source? Are allied yards usable? Is repair
    free? Is it applied before or after movement? Can consumed one-shot components be repaired?
    **Answer:** pooled per (empire, sector) over the empire's own sources only; free; done in the
    end-of-turn phase after maintenance (after movement is inferred). Used-up components can be
    repaired, but emergency-energy and emergency-resupply parts only with an own yard present, and
    parts of unknown technology never (§13) (confirmed: binary).
15. **Retrofit:** which direction and which resource total does the 50 % cost-difference cap use?
    Is the cost charged at once? Does the retrofit take effect instantly in simultaneous games?
    Does it need the ship to be undamaged or unmothballed? **Answer:** only an increase is
    limited, against the old design's three-resource total × (100 + pct) %. The cost is charged
    when the retrofit order runs during the turn, and only if a component is added; the change is
    immediate at that point. Damage and mothball status are not checked; cargo forbids it (§14)
    (confirmed: binary).
16. **Maintenance:** 25 % or 30 %? Does the percentage change over the game? Do units in cargo or
    space pay? How is the vehicle lost to unpaid maintenance chosen? **Answer:** the base is the
    `Empire Starting Percent Maint Cost` setting (stock 25), lowered by racial and culture modifiers
    to no less than 5, and otherwise fixed. Units never pay. Victims are picked at random, first
    among vehicles out of supply (§16) (confirmed: binary).
17. **Cloak semantics:** a cloaking device with level 2 claims to block level-1 scans, while a mine
    with level 5 claims to block level-5 scans. Confirm "sensor level ≥ obscuration level detects".
    **Answer:** confirmed: a sensor level at least equal to the obscuration detects. Per sight type
    the obscuration is the larger of the object's cloak level (1 when not cloaked) and the
    sector's obscuration (units do not get the sector's); detection in any one sight type is
    enough, and own objects are always seen (see the sight spec) (confirmed: binary).
18. **Emissive Armor V1:** is it a per-hit damage threshold (as the component text says) or extra
    armor points (as the catalog says)? **Answer:** neither exactly: each hit of the ordinary
    damage types is reduced by V1, so a hit of V1 or less does nothing (§3.3) (confirmed: binary).
19. **Scrapping:** does it return cargo value? Is scrap value reduced for damaged ships? Do
    recyclers of allies apply? **Answer:** no, no, and no: cargo is lost, damage does not matter,
    and only the owner's own `Resource Reclamation` counts (§15) (confirmed: binary).
20. **Obsolete designs:** the auto-deletion interval. Is design-name uniqueness per empire or
    global? **Answer:** an obsolete design is purged once it has no vehicles, is in no queue and no
    other living empire has seen it for 50 turns. Names are unique across the whole game,
    case-sensitively (§4.1) (confirmed: binary).
21. **Waypoints:** maximum count, and the effect of deleting a waypoint that pending orders or yard
    auto-moves reference. **Answer:** 10 per empire. A pending Move To Waypoint fails when reached
    and clears the list; yard auto-moves were already turned into plain Move To orders at build
    time (§8.1) (confirmed: binary).
22. **Fleet membership:** can units (fighters, drones) join fleets? Which member becomes leader
    automatically? Is fleet experience lost on any membership change? **Answer:** fighter groups
    can join; drones, satellites and mines cannot. Without a chosen leader, the first member in the
    sector's object order leads. Experience is lost only when the fleet is deleted (§9)
    (confirmed: binary).
23. **Formations:** do slot `Type` restrictions and formation orientation matter in practice? What
    happens with more members than positions? **Answer:** `Type` is ignored. Formations rotate
    with the leader's 8-way facing, diagonals stretched. Extra members get no formation place
    (§10) (confirmed: binary).
24. **Stellar-manipulation "Stop ..." facilities:** do they cover the system or the whole empire?
    What are the units of `Open Warp Point Distance`? **Answer:** the whole system, counting
    objects of every empire; `Stop Planet Destroyer` covers only its sector. Distance is in
    galaxy-map squares, straight line, rounded (§8) (confirmed: binary).
25. **Fleet orders:** the engine lets members in the leader's sector follow the fleet's orders
    instead of their own; members elsewhere act alone. Is that how split fleets behave?
    **Answer:** yes. The fleet's orders are copies in each member's list; the members in the
    fleet's sector execute them as one group, and members elsewhere are not part of it (§8, §9)
    (confirmed: binary).
26. **Combat interruption:** the engine stops every vehicle that fought for the rest of the turn
    and clears its orders (and its fleet's); bystanders at peace with everyone, and ships that only
    met mines, keep moving. Is that right? **Answer:** no. In the original, combat neither stops
    movement nor clears orders; only a Sentry order at the head of the list is removed. Orders are
    cleared by storm damage, turbulence damage, minefields and refused entry (§6.3, §6.4)
    (confirmed: binary).
27. **Launch budgets:** the engine uses `Val 2` per game turn (`Val 1` when a record leaves
    `Val 2` at 0) with separate budgets for launching and recovering. Launched units start with
    full supply and move from the next turn; drones only join groups with the same target.
    **Answer:** the original uses `Val 2` only (no fallback), budgets launches only (recovery is
    limited by cargo space), adds launched units to the last group of their kind in the sector
    whatever its designs, and never merges drones. New groups start full and launching refills
    the group joined. A new group starts with 0 MP: a turn-based launch then sets its MP to the
    maximum, so it moves (and can be recovered) the same turn, while in a simultaneous game it
    first moves the next turn (§12) (confirmed: binary).
28. **Hazard damage outside combat:** storms, destructive centres and turbulence hit armor first,
    then other components, with no shields. **Answer:** storm damage happens only to a group
    stepping into the storm sector (not on a warp arrival), with a 50 % chance, and stops it;
    turbulence is 50 % per transit; destructive centres hit everything on the centre square at
    the end of the turn. All of them, and the `Ship - Damage` event, use the combat damage routine
    for Normal damage without shields, special armor, modifiers or pool: a ship loses components in
    the random, structure-weighted order with armor first until the damage cannot cover the next
    one, and the rest is lost; a unit group loses whole units by up to 20 random draws, a unit's
    shields counting as hit points, and the rest is lost (§6.2) (confirmed: binary).
29. **Cargo lost to damage:** the engine drops the last unit stack first, then population.
    **Answer:** wrong way round: the original drops population first (1M at a time), then units
    from the first stack (§11) (confirmed: binary).
30. **Cloak cost:** the engine charges the cloak's `Supply Amount Used` once, when cloaking.
    **Answer:** wrong: cloaking is free and immediate, and the cloak parts' supply is charged at
    every end of turn while cloaked; the vehicle decloaks at 0 supply (§7, §8) (confirmed: binary).
31. **Explore with nothing left:** the order completes. Should it wait for a target instead?
    **Answer:** Explore is not a stored order: when given, it becomes Move To plus Warp for one
    chosen warp point, and with no candidate nothing is added (§8) (confirmed: binary).
32. **Resupply and repair sources:** the engine requires a populated colony for depot and
    facility repair, repairs for free, and lets allies' depots refuel from Military Alliance up.
    **Answer:** depots need no population; allies' depots refuel from Military Alliance up; repair
    is free and uses own sources only. Facility repair needs no population either: every own
    colonized planet, ship, base and unit group in the sector adds its `Component Repair` (§7,
    §13) (confirmed: binary).
33. **Training:** the engine uses the single best source (sector or system facility) and treats a
    `Val 2` of 0 as no cap. **Answer:** sector sources stack: every own object in the sector is a
    separate source with its own largest V1 and V2, and a V2 of 0 gives no training. Fleet
    training works the same way on fleets located in the sector, but a gain that would pass the cap
    is V2 − truncate(experience). The system-wide abilities give one source per system (the largest
    V1 and V2 over all the empire's objects there), train ships, bases and fleets in the whole
    system with the truncated-experience cap rule, and run after all sector sources. Ship
    experience is capped at 50 (§3.3, §9) (confirmed: binary).
34. **Colonization:** population or units beyond the new colony's capacity are lost, the colony
    type is the first of the empire's colony types, ruins are used up, and asteroids cannot be
    colonized. When two ships reach a planet in the same turn the lower vehicle id wins.
    **Answer:** cargo beyond capacity is lost; the first ship processed (object-slot order) wins
    and the other's order fails; the colony also receives `Automatic Colonization Population`.
    The colony type is picked by the player in a dialog when the empire's option for it is on (the
    default) and a human colonizes in a turn-based game, and otherwise chosen automatically as a
    computer player would. `Ancient Ruins` are used up (removed) once applied; `Ancient Ruins
    Unique` applies only when there are no plain ruins and is then used up too (§3.3, §8)
    (confirmed: binary).
35. **Stellar manipulation:** the engine requires movement left but does not spend it.
    **Answer:** movement is required but not spent (Construct needs none). The original also
    refuses while cloaked or, except for Destroy Planet, with a visible hostile in the target
    sector, needs the part's supply, and for Open Warp Point refuses existing links, systems with
    10 warp points and blocked systems (§8) (confirmed: binary). Every created or removed object is
    in spec 01 §9 (confirmed: binary): Destroy Star sends a shockwave that turns planets into
    asteroid fields and destroys everything else but warp points, and **no destroyed star
    remains**; Create Nebulae and Create Black Hole do the same and then give the system fixed
    abilities; Destroy Nebulae and Destroy Black Hole make it a normal system with no system
    abilities; a created storm gets one random effect whose value equals its `Created Storm ...`
    setting; an opened link is two-way, its far end on the target system's edge facing the origin.
36. **End-of-turn supply order:** the engine runs, per empire, unit and cloak upkeep, then depot
    refills, fleet pooling and solar collectors, then the limits (a drone at 0 is lost, a cloak at
    0 supply drops). **Answer:** upkeep for every own object (cloak cost, fighter and drone
    upkeep), with a vehicle that reaches 0 decloaking at once; then depot checks; then fleet
    pooling; then drones at 0 are destroyed. Solar collectors come later, in the training step
    after the storage cap. The units' upkeep is part of this step; the later per-object step only
    records the sector each vehicle came from (§7) (confirmed: binary).
37. **Storms on a warp arrival:** does a group arriving through a warp point roll for storm damage?
    **Answer:** no; only in-system steps into the sector roll (§6.2) (confirmed: binary).
38. **Emergency Energy in simultaneous games:** **Answer:** it adds V1 to the day counter only. MP
    left are untouched, but every action runs with 1 MP anyway, so the extra actions can be steps
    (§6.3, §8) (confirmed: binary).
39. **Acting order within a day:** **Answer:** one list in object-slot order (a new object reuses
    the first slot freed by a destroyed one). Colonized planets with orders, minefields, satellite
    groups and vehicles with 0 maximum MP act on day 1 only, where their slots put them. A fleet
    acts at the place of the first member due that day, not its leader, and there are no separate
    member orders: each member's list holds copies of the fleet's orders (§6.3) (confirmed: binary).
40. **Sentry "present":** **Answer:** an object the owner can see, anywhere in the system, of an
    empire whose treaty with it is below Non-Aggression: ships, bases, unit groups (mines only if
    seen) and colonies (only if seen) (§8) (confirmed: binary).
41. **Recovery into a ship:** **Answer:** yes, a ship or base needs the matching ability for the
    kind (`Launch/Recover Fighters` or `Launch/Recover Satellites`); planets need none (§12)
    (confirmed: binary).
42. **Mounted damage far out:** **Answer:** a weapon may fire whenever its damage at the actual
    distance is above 0; there is no other range cap, so a mounted weapon whose range-20 entry is
    above 0 reaches the whole combat map. The "maximum range" used by range strategies,
    point-defense and the reports is the largest range from 1 to 20 with damage, so at most 20
    (confirmed: binary). No stock weapon does damage at range 20.
43. **Groups of several designs:** **Answer** (§12) (confirmed: binary):
    - launched units join the **last** group of their kind and owner in the sector (object
      order), whatever its designs, orders, fleet or cloak, and only that group is refilled;
      groups never merge in any other way (moving, end of turn, combat);
    - Recover Units takes a unit kind, not a design: it recovers every design of each own group
      of that kind in the sector, group by group;
    - a mixed minefield loses mines to sweepers design by design in the order the designs joined,
      strikes in the same order, and each mine uses its own design's warheads;
    - a group's cloak and sensor levels are the best of its units', and its cloak applies whether
      or not it is flagged as cloaked;
    - the Combat Simulator puts the units of one side and kind into one group (each drone alone);
      in a battle each launch action makes its own group, and groups that no carrier recovers
      stay separate;
    - battles record the designs of ships and bases (and of units in cargo) as seen, but not
      those of unit groups in space; mine encounters record the mines' designs.
    The engine keeps unrecovered battle groups separate too (spec 04 §19 Q56).
44. **Training sources:** **Answer:** every own object in object order, colonized planets without
    any population requirement (§3.3, question 33) (confirmed: binary).
45. **Obsolete design purge:** **Answer:** each design keeps, per empire, the turn it was last seen.
    Each empire forgets sightings more than 50 turns old in its end-of-turn processing, and every
    10th turn a design seen by another living empire less than 50 turns ago is kept, so a
    sighting exactly 50 turns old no longer protects it (§4.1) (confirmed: binary).
46. **Attack on a planet:** **Answer:** the pursuit ends when the target no longer exists, belongs
    to the attacker's owner, or is a planet without a colony; there is no visibility test for any
    target (§8) (confirmed: binary).
47. **Repeat battles within a turn:** **Answer:** only the latest battle at that location this
    turn counts. A new battle is fought if any of its surviving pieces was below full structure;
    otherwise it is skipped only when every object owned by a player in the sector (ships,
    bases, unit groups including minefields, colonies) is on its owner's list of that battle's
    survivors, matched by name, with no exception for cloaked or peaceful objects (§6.3)
    (confirmed: binary).
48. **Drift target:** **Answer:** it is drawn every turn, whether or not any system drifts (§6.2)
    (confirmed: binary).
49. **Sentry with Repeat on:** **Answer:** an ending Sentry counts as done, so with Repeat on it
    stays in the list and execution moves past it; the removal of a head Sentry after combat
    ignores Repeat (§6.3, §8) (confirmed: binary).
50. **Names taken meanwhile:** **Answer:** the original never checks names of designs that arrive
    in a turn file or come from an empire file, so a duplicate name is simply kept; computer
    players pick names from their list, then add II to XV, then use a numbered name (§4.1)
    (confirmed: binary). The engine's renaming (first free Roman numeral, empire name in front of
    premade designs) is an OpenSE4 extension that keeps names unique; engine choice stands.
51. **Ad-hoc groups:** **Answer:** the executing group is rebuilt at every execution. A computer
    player's group takes every own vehicle in the sector with an identical head order (ships,
    bases, unit groups, fleet members and cloaked ones included); a human player's ships never
    group this way, only drone groups outside fleets do; in turn-based games the vehicles selected
    together form the group. Load and Drop carry no amount: each member loads or drops for itself
    (§8) (confirmed: binary).
52. **Greedy steps:** **Answer:** the target is the next warp point on the route or the
    destination; replacements are drawn from the current square (1 in 2 straight steps after a
    diagonal, 1 in 3 among the forward square and its two side neighbours after a straight step,
    so the forward square can repeat); coordinates are clamped to the grid, so an off-grid draw
    becomes the edge square; every replacement is tested even when it is the target; bad squares
    are tagged minefields (option on, no Mine Sweeper exemption inside a system), squares whose
    objects have `Sector - Damage` (the system value is not counted), and squares with a visible
    hostile object, colonies included, mines only if seen (§6.2) (confirmed: binary).
53. **Clearing orders on meeting empires:** **Answer:** checked only at the end of a warp transit
    that met no other trouble (Warp orders and the jumps of a Move To or a pursuit), not for
    drone-only groups, counting objects of that empire in the arrival system that the owner can
    see; every member's list is cleared but the jump itself does not fail (§6.4) (confirmed:
    binary). New empires have "clear on meeting an enemy" on.
54. **Orders as given:** **Answer:** Resupply and Repair add their Move To even for the current
    sector (it completes at once), and nothing without a reachable target, which the engine's
    choice matches in effect. Explore skips warp points named by a Warp order anywhere in any own
    vehicle's list, the vehicle's own earlier orders and fleet copies included, and checks nothing
    about the link beyond explored/unexplored; the engine matches. The Load Cargo that Colonize
    adds does not take an action of its own: done orders chain within one action (§6.3, §8)
    (confirmed: binary). Not expanding orders that are sent again unchanged is an OpenSE4
    extension; engine choice stands.
55. **The Attack Sector question (turn-based, §6.2):** **Answer:** asked only on in-system steps
    and only while a human is the current player, orders carried over to the start of that turn
    included; any object of a hostile empire in the next sector that the owner can see counts,
    mines and colonies included if seen; never for drone-only groups or groups whose members are
    all cloaked; warp jumps never ask; the approach steps of an Attack ask like any step (§6.2)
    (confirmed: binary).
56. **Fighter recovery in turn-based games (§12):** **Answer:** recovery is refused while the
    group's MP left are below its maximum (§12) (confirmed: binary), which the engine's reading
    matches.
57. **Damage between upkeep steps:** **Answer:** every destroyed component, in combat or outside
    it, triggers an immediate recompute that clamps supply and trims cargo to the new capacity.
    `Ship - Lose Supply` does nothing to a vehicle with unlimited supply; otherwise it takes the
    smaller of the event amount and the current supply (§7, §11) (confirmed: binary).
58. **Ship Movement options (§6.2):** **Answer:** a new empire has both on (avoid tagged
    minefields, avoid restricted systems), and "clear orders on meeting an enemy" on too. The
    exempt design type is the one named `Mine Sweeper`, tested on the group's first member (for a
    fleet the first member at its location in object order), and only for warp links. A tagged
    warp-point sector blocks a link even at the start or the destination (§6.2) (confirmed:
    binary).
59. **Body notes settled in this pass** (confirmed: binary): units that do not fit in the
    builder's cargo go to other own cargo in the same sector only (§1); `Long Range Scanner -
    System` lets the empire inspect every foreign ship and base in the system, and inspection takes
    effect when a player displays the target's details (§3.3); mine sweeping comes before the
    mines strike (§12); fighters cannot use warp points (§6.2); drones get no target at launch and
    pursue targets only through Attack orders (§12); a fleet's strategy applies to its armed
    members while they are in its combat group (§9); when members leave formation and when the
    group dissolves, the 0.1 fleet experience gain (a kill or a ram, 1 in 4) and the anchoring when
    the leader is absent or leaves formation (§9, §10); mine hostility, cloaked victims, the
    fighter and drone settings, mines used up per strike, owners seeing their mines (§12);
    automatic recovery after combat of the groups a carrier launched in that battle (§12);
    self-destruct of unit groups (§12, §15); the seven recognised but unused ability types have no
    effect (§3.3); repair and the rest of the end-of-turn processing come after the turn's
    movement and combat in simultaneous games (§7, §13).

The last three body notes are settled too (confirmed: binary): the path cost around a destructive
centre (§6.2), how prototype status gates Edit and what Upgrade makes (§4.1), and which treaties
let vehicles pass without combat (§6.4). Nothing in this spec's body is left unverified.

The questions below came up while the engine was brought in line with the settled rules. Each
gave the engine's choice, marked (inferred); all of them are answered now.

60. **Formations of a player's side in tactical combat (§10):** **Answer:** yes, while that side
    is automated, and the engine picks the automated sides correctly. At the start of a battle
    the computer players' sides are automated. Strategic combat, and every battle fought without
    a window, automates every side. In tactical combat, pressing the window's Auto button
    automates every side and releasing it hands the human sides back to their players; Resolve
    Combat automates every side for the rest of the battle (confirmed: binary). The trigger is
    wider than the engine's, though. After every hit that a group's leader (its anchor, §10)
    survives, even one its shields absorb completely, its movement left for this combat turn is
    lowered to its new maximum; if that leaves 0 and its side is automated, the whole group
    dissolves. A leader that has already used up its movement this combat turn is therefore
    enough (confirmed: binary). The other trigger, a surrounded leader, is tested once, when the
    leader's turn to act comes in an automated phase, before it plans and moves; there is no
    test after the move, and a surrounded leader does not move (spec 04 §16.1) (confirmed:
    binary). The engine follows both since 2026-09-30.
61. **Fleet members away from the fleet's sector (§6.3 step 2):** **Answer:** no. Every fleet
    member's daily gain uses the fleet's figure, wherever the member is: the lowest MP left among
    the fleet's members at the fleet's location. A member elsewhere is not counted in it. With no
    member at the location the figure is 0, so the members gain nothing. The member's own MP only
    decides whether it gains at all (it must be above 0). At the start of the turn only the
    members at the location get the fleet's lowest maximum (confirmed: binary). A member
    elsewhere never moves by its own orders either: when it is the one to act, its order is
    carried out by the members at the fleet's location, without it (§8). Members rarely end up
    apart, because the fleet's location follows any member that moves and a fleet left with no
    member at its location is disbanded at once (§9). The engine does the same (question 73
    records its choice for the list of a member elsewhere that acts).
62. **Object slots (§6.3 step 5):** **Answer:** the original keeps one list of every object:
    stars, planets, asteroid fields, storms, warp points, ships, bases and unit groups alike. A
    removed object of any kind leaves its slot empty where it is, and a new object of any kind
    takes the lowest empty slot, whatever kind of object left it, else a new slot at the end
    (confirmed: binary). The generated galaxy comes first (the homeworlds and starting planets
    made at setup included), but afterwards kinds mix: a new ship can take the slot of a planet
    replaced by stellar manipulation, of a closed warp point or of a destroyed storm, and a
    planet, storm, star or warp point made by stellar manipulation can take a destroyed ship's
    slot or a new slot after every ship. Colonized planets then act on day 1 in their slot's
    place among the vehicles. The engine does the same (question 72 records its choice for
    objects that stellar manipulation changes in place).
63. **A stopped vehicle's later actions (§6.3 step 4):** **Answer:** every action sets the acting
    vehicle's MP to exactly 1 when its maximum is at least 1, whatever MP it had, so such a
    vehicle moves again, one step per remaining action. After the action its MP goes back to
    the 0 it had, so its counter gains nothing and it stops once the counter falls below 1. Only
    a vehicle whose maximum is 0 acts with 0 MP, so that its moves wait. Only the acting vehicle
    gets the 1 MP; another group member with 0 MP still holds the group back (confirmed:
    binary). The engine does the same.
64. **Which check comes first:** **Answer:** as the engine does it. Each time a Move To runs it
    fails at once only when the destination is not a valid system and sector. Then it is done
    when the group is already at the destination, and it waits when the group is empty or any
    member has no MP left. Only after that does the step look for a route, and fail when there
    is none (or when a member's space yard is building). So with no MP left it waits, and it
    fails on the next action that has MP (confirmed: binary).
65. **Fleet members' own lists:** **Answer:** the original has no fleet list. Each vehicle has
    exactly one list, and a fleet's orders are the lists of its members at the fleet's location
    (confirmed: binary):
    - Orders given to a fleet, or to any vehicle in a fleet, are appended to the list of every
      member at the fleet's location. Clear Orders and Repeat Orders change all of those lists
      the same way.
    - A vehicle's list is cleared when it joins a fleet, by Fleet Transfer or by the Join Fleet
      order, and when it leaves one by Fleet Transfer or because the fleet is disbanded (§9). A
      vehicle that joins does not receive the orders the fleet already has, only those given
      after it joined.
    - The fleet acts when its first member in object order that is due that day and has a
      non-empty list acts. The order at that member's current position is carried out by the
      members at the fleet's location (§8). Completing it removes the order at that same
      position from each of their lists; with Repeat on, each list moves on to its next order.
      A failure, a refused sector entry or the Ship Orders options clear all of their lists.
    - So the lists act as one shared list while they hold the same orders. A member that joined
      after orders were given holds only the later ones. If it is the one to act, its own next
      order is carried out for the fleet, and completing it removes the order at that position
      from every other member's list, whichever order that is.

    The engine does the same; questions 73–76 record its choices where this leaves something
    open.
66. **The Launch/Recover window in simultaneous games:** **Answer:** OpenSE4 choice; the
    original has nothing to compare it with. It offers the window only in turn-based games; in
    a simultaneous game the command does nothing (confirmed: binary). Units are then launched
    and recovered only by the Launch Units Remotely and Recover Units Remotely orders, and that
    Recover order names a unit kind, never a group or a design (§8). The engine's window and
    its Recover orders for one design of one group remain an OpenSE4 extension.
67. **The Colonize dialog (§8):** **Answer:** the original asks at once and keeps nothing
    pending. The Colonize order stops in the middle of its execution for a dialog, and the
    colony is made after the dialog closes, with the type picked. Closing it without a pick
    (Cancel) leaves the colony with no type (an empty name). The dialog comes only in a
    turn-based game, when the player whose turn it is is human and the colonizing empire's
    option is on; otherwise the type is chosen automatically (spec 05 §7.5) (confirmed:
    binary). The engine cannot stop an order for a dialog, so its pending choice, with the
    automatic type standing until the player answers, is an OpenSE4 choice; the result is the
    same once the player picks. The engine's dialog has no Cancel.
68. **A drone's Attack target in battle (§8, §12, spec 04 §10.7):** **Answer:** yes in effect,
    but the original reads the target from the order instead of storing it. When a battle
    starts, each drone group's target is the object named by the first order in the group's
    list, if that order is an Attack pursuit and the object is a piece in the battle, whatever
    its kind (ship, base, planet or unit group). Otherwise, and for drones launched during the
    battle (they have no orders), the target is chosen as for a computer player's piece (spec 04
    §10.7) (confirmed: binary). So a pursuing drone caught in a battle on its way attacks its own
    target only if that target is there. The engine follows this since 2026-09-30: combat reads the target from the order, and
    nothing is stored.
69. **Turn-based Attack by a group that is not all drones (§8):** **Answer:** no member decloaks.
    The Attack spends 1 MP and one move's supply per member and runs the battle check at once; a
    battle that starts decloaks every piece for that battle (spec 04 §2). The only exception is
    the Ship Cloaking minister: vehicles under it (all of a computer player's, and a human's
    vehicles under minister control while that minister is on) lower their cloaks for the Attack
    and raise them again afterwards if they can (spec 05). In the pursuit form only drones
    decloak, and only when the group attacks at its target (confirmed: binary). The engine
    does the same. §6.4 said otherwise and is corrected. Also in the pursuit form, only a group
    holding a drone that pursues an object in that sector attacks at all (§8). A group of other
    vehicles that has reached its target's sector just waits there and spends neither MP nor
    supply (confirmed: binary). The engine does the same here too.
70. **Colonies "seen" (§6.2, §6.4, §8):** **Answer:** "seen" is the detection rule of spec 01
    §6.3 applied to the colony's planet, the same test as for a ship. Its owner always sees
    it. Any other empire sees it only when it has explored the system and its sensor level
    there, in some sight type, reaches the planet's obscuration: 1, or the colony's cloak levels
    while the colony is cloaked, raised by the sector's and the system's obscuration (spec 01
    §6.2). With no sensor source in the system (and no partner's sensors there and no
    omnipresent view, spec 01 §6.1, §6.5), no colony is seen, even one shown on the map from
    memory (confirmed: binary). The engine does the same. The battle check remains combat's own
    (spec 04 §19 Q74).
71. **The turn-based Attack as one order (§8):** **Answer:** the original stores two orders: a
    Move To the sector the target was in when the order was given (added even when the group is
    already there, where it completes at once) and an Attack. The stored Attack names no target
    and checks no place: it attacks wherever the group stands when it runs (confirmed: binary).
    The engine's single order does the same as that pair: the move, its waits and failures,
    an arrival with no MP left (the Attack is then removed doing nothing), and Repeat. The single
    order is an OpenSE4 choice. An Attack with no sector recorded attacks where the group
    stands, as the original's does.

The questions below came up while the engine was brought in line with the answers to 60–71.
Each gave the engine's choice, marked (inferred) in the code; all are now settled from the
binary.

72. **Objects replaced in place (§6.3 step 5, spec 01 §9):** Create Planet turns an asteroid
    field into a planet, and Destroy Planet and the shockwave turn a planet or asteroid field
    into an asteroid field. The engine changes the object where it stands, so it keeps its slot
    in the object list. Does the original remove the old object and make a new one, which
    would take the lowest free slot (perhaps another one) and leave the old slot free?
    **Answer:** yes. The original never changes such an object in place: it adds the new
    object first and then removes the old one (confirmed: binary).
    - Create Planet adds the planet while the asteroid field still holds its slot. The planet
      takes the lowest empty slot (Q62), else a new slot at the end, and never the field's own
      slot. Then the field is removed and its slot left empty. When the data has no planet
      type of the field's stellar size, no planet is made, but the field is still removed,
      the supply and the part are still paid, and the result is still reported.
    - Destroy Planet does the reverse. A new asteroid field, with the planet's name and
      values, is added first, then the planet is removed with its colony.
    - The shockwave (Destroy Star, Create Nebulae, Create Black Hole) is one pass over all the
      system's objects in slot order, vehicles and colonies included. Each planet or asteroid
      field gets a new asteroid field in the lowest slot empty at that moment, which may be
      one emptied earlier in the same pass; then the old object is removed. Every other object
      except warp points is removed in the same pass: ships, bases, unit groups, storms,
      comets. The star goes after the pass.
    - Anything that named the old object finds it gone. A Colonize, an Attack, or another
      ship's Create Planet aimed at it fails, and a pursuit of it ends (§8).

    The engine follows this since 2026-10-01: the replacement is a new object, added while the
    old one still holds its slot, and the old one is removed (its record stays, so ids remain
    stable); the shockwave is one pass in slot order over vehicles and stellar objects, a
    destroyed vehicle leaving the game at once; Create Planet with no planet type of the
    field's size removes the field, pays and reports.
73. **A fleet member away from the fleet's location that acts (§8, Q61):** its order is carried
    out by the members at the location. The engine also moves that member's own list on (the
    order leaves it, or goes to its back under Repeat), so the member does not carry out the
    same order again on its next action. Does the original leave that member's list as it is?
    **Answer:** yes. Every change that carrying out an order makes goes only to the lists of the
    members at the fleet's location (confirmed: binary):
    - a completed order is removed, at the acting member's list position, from their lists
      only; with Repeat on, only their lists move on; a failure clears only their lists;
    - the away member's own list keeps the order and its position.

    Completed orders chain (§8), so the same order runs again at once. Each time it completes
    it removes the order at that position from the location members' lists again, even after
    their lists are empty. This goes on until the 21-execution limit, or until the order waits
    or fails. Example: an away member whose Move To the group has
    already reached wipes the location members' lists, one order per execution, and does so
    again on every later action.

    The engine follows this since 2026-10-01: only the lists of the members at the fleet's
    location change, and the chained run repeats the away actor's unchanged head order, up to
    21 times.
74. **Mothballed fleet members (§9):** the engine leaves them out of the group that carries out
    the fleet's orders and gives them no copies of orders given to the fleet; they still count
    for the fleet's speed, which their 0 maximum holds at 0. Does the original give them copies
    and make them part of the group?
    **Answer:** yes. Nothing in fleet handling tests the vehicle's status (confirmed: binary):
    - A mothballed member at the location gets copies of appended orders and is affected by
      Clear Orders and Repeat Orders.
    - It is in the group that carries out the fleet's orders, and every list change of §8
      applies to its list.
    - Its maximum movement is 0, so the fleet's speed is 0. Every member's movement at the
      location is set to 0 (§9), and movement orders wait.
    - It can be the acting member. In a simultaneous game a vehicle with maximum 0 that has
      orders acts on day 1 (§6.3), while the other members gain no day credit at speed 0. So
      the fleet acts once a turn, through it, and orders that need no movement are carried out.
      In a turn-based game it acts when it is the first member in object order with orders.
    - A mothballed ship can join a fleet: Fleet Transfer and Join Fleet do not test status.
    - Conversely, a vehicle in a fleet cannot be mothballed, scrapped, analyzed or retrofitted.
      The Scrap window lists only own vehicles in the sector that are in no fleet and not
      cloaked (§15).

    The engine follows this since 2026-10-01: the fleet group is every member at the fleet's
    location, mothballed ones included, and a fleet frozen at speed 0 makes its movement
    orders wait; the mothball (and unmothball), scrap and retrofit commands refuse a vehicle in
    a fleet, and the Scrap window lists only vehicles in no fleet that are not cloaked.
75. **Fleet members in a computer player's ad-hoc group (§8, Q51):** when a member's head order
    is identical to the acting group's, the engine takes in every member of its fleet at the
    fleet's location. Does the original take only that member?
    **Answer:** yes, only that vehicle (confirmed: binary).
    - **Who joins:** every own vehicle in the acting vehicle's own sector whose first order is
      identical to the order being carried out, fleet members, cloaked and mothballed vehicles
      included. Each joins alone. Fleet-mates join only when their own first orders match too,
      which identical fleet copies normally do.
    - **Whose lists change:** only the acting vehicle's list, or, when the actor is a fleet
      member, the lists of its fleet's members at the fleet's location. A companion's list is
      never touched: the order stays at its head, and a failure does not clear it. The
      companions are held from acting again that day. On their next action they carry the
      order out themselves; a Move To already reached then completes at once.
    - Drone groups gathering other drone groups (human or computer) follow the same rule.
    - A companion that is a fleet member and moves with the group takes its fleet's location
      with it (§9).
    - The only exception is the turn-based selection: when the player selected vehicles
      together, every selected vehicle's list is changed.

    The engine follows this since 2026-10-01: each companion joins alone, and only the actor's
    list (or its fleet's members' at the location), or the turn-based selection, changes. It
    reads "never touched" for every list change, so a minefield that stops the group or a
    Ship Orders option met after a warp clears only those lists too. Q77 settled both: the
    minefield is right, the Ship Orders options clear every member's list, companions
    included.
76. **Changes to a fleet's orders other than adding (§8, Q65):** the engine appends added orders
    to every copy, as the original does; for any other change (Clear Orders, an order taken back
    or put in front) it makes every copy the new list. Orders given to a member away from the
    fleet's location also go to that member's own list. Does the original apply such changes
    position by position to each list, and leave the away member's list alone?
    **Answer:** the original has no other such changes, and it leaves the away member alone
    (confirmed: binary).
    - The View Orders window is read-only. There is no way to take an order back, put one in
      front or reorder the list.
    - Besides appending, the only changes to a list are:
      - Clear Orders, which empties it and switches Repeat off;
      - Repeat Orders, which sets the flag and restarts at the first order;
      - Sentry given to a vehicle whose first order is a Sentry, which clears the list;
      - Use Component and Use Facility in a turn-based game, which clear the list before the
        order is added (§8).
    - For a fleet each of these is applied list by list to the members at the fleet's location.
      The computer players do the same for their fleets.
    - An order, Clear Orders or Repeat Orders given to a member away from the fleet's location
      goes to the members at the location; the addressed member's own list is left alone.

    The engine follows this since 2026-10-01: an order, Clear or Repeat given to an away member
    reaches only the members at the location, list by list. OpenSE4 choice: taking an order
    back or putting one in front makes every list at the location the new list.

The questions below came up while the engine was brought in line with the answers to 72–76 and
with the Jettison, Use Component and Use Facility rules of §8. Each gives the engine's choice,
marked (inferred) in the code.

77. **Smaller choices of 2026-10-01.**
    - In a turn-based game Use Component and Use Facility clear the list before they are added
      (§8); the engine also switches Repeat off then, as Clear Orders does. Does the clearing
      leave Repeat as it was?
    - A computer player's ad-hoc companions keep their lists when the group's order fails
      (Q75). The engine applies this to every clearing a step can cause: a minefield that stops
      the group and the Ship Orders options after a warp clear only the actor's (or its
      fleet's) lists, though §6.4 says every group member's list is cleared. Which lists do
      those two clear when the group holds companions?

    **Answer** (confirmed: binary; §6.4, §8):
    - **Repeat.** In a turn-based game the clearing done before Use Component or Use
      Facility is the Clear Orders clearing: each list is emptied, Repeat is switched off and
      the list restarts at its first order. It reaches the same lists as Clear Orders: the
      vehicle's, its fleet's members' at the fleet's location, every vehicle of the turn-based
      selection, and for Use Facility the colony's. The engine matches.
    - **A minefield** clears nothing by itself: the strike makes the step fail, and the failure
      clears only the lists a failure clears (the actor's, or its fleet's members' at the
      location, or the turn-based selection). Companions keep theirs. Storm damage,
      turbulence, a refused entry and turn-based combat on entry behave the same way. The
      engine matches (`movement.cpp`, the hazard's `clearListsOf(e.holders)`).
    - **The Ship Orders options** at the end of a warp clear the list of every member of the
      acting group, each with Repeat switched off: the fleet's members at the location, a
      computer player's ad-hoc companions, companion drone groups (the check is skipped when
      every member is a drone) and the turn-based selection. In a turn-based game a Move To
      then goes on stepping toward its destination in the same run while movement lasts, but
      the lists are already empty, so when movement runs out first nothing is left of the
      order for a later turn. In a simultaneous game the action ends there.

    Since 2026-10-01 the engine follows this for the Ship Orders options too: `encounter()`
    (`movement.cpp`), used by the Warp order as well, empties the list of every member of the
    group (companions included) with Repeat off, and in a turn-based game the Move To goes on
    stepping in the current run only (`Mover::carried_`), so nothing of it is left for a later
    turn.
78. **The Scrap window's actions: the engine's choices (§15, 2026-10-01).**
    - The computer players' and ministers' Scrap and Retrofit go through the window's
      commands, so in a simultaneous game they become the vehicle's only order and are
      carried out at its first action. Does the original's Scrap or Retrofit minister give
      them as orders, or carry them out at once?
    - "Number Scrapped" counts each living unit of a scrapped or self-destructed unit group.
      Does the original count units, or one per group?
    - A self-destructing vehicle writes "<name> destroyed" in the Misc log and raises no
      "ship lost" mood. What does the original write, and does it count the ship as lost for
      happiness?
    - Unmothball is lit only when the cost is in stock (the order's test). Is the button lit
      for a mothballed vehicle the empire cannot afford?

**Players' reports on v0.8.1 (2026-10-04).** The Colonize picker is spec 06 §7 Q100 (with the
rules in §8); this one is ours:

79. **Population between colonies in one sector.** "You can't move population between colonies
    in the same sector directly anymore." Does the original move population directly between two
    own colonies in the same sector? **Answer:** yes, through Cargo Transfer opened with either
    colony (or a ship there) selected: the other colony is in the right-hand list, and a click on
    a race's line moves people from population to population, capped by the target's free
    population room, the source keeping at least 1M, at no cost and in both turn styles (§11)
    (confirmed: binary).
    Since 2026-10-04 our engine follows: `cmd::TransferCargo` from one own colony to another in
    the same sector moves the clicked race from population to population under the rules of §11
    (`commands.cpp`), and the window puts the selected holder (and its fleet-mates there) on the
    left and the others on the right (`cargo_transfer.cpp`).

**Implementing the players' reports on 2026-10-04** left this question of ours:

80. **A planet hidden from its colonizer.** §8 names, among a Colonize's reasons to fail, a
    planet "not seen by the ship's owner". Ours counts as unseen only a planet whose colony the
    owner cannot see (a cloaked colony, spec 01 §6.9), not one that a storm or a nebula hides
    by the "seeing the planet" test: our computer players aim their colony ships at every
    planet of the systems they have explored, so with that test their ships would fail at such
    a planet turn after turn. Also ours, for the computer players' Colonize, which travels
    itself (spec 05 §7.5, open question 23): it gives up on its way as soon as no group member
    could settle the planet any more; a player's Colonize follows its Move To and is checked
    only in the planet's sector (inferred). Which planets does the original's "seen" test
    reject while a ship stands in their sector, and does its computer player aim only at
    planets it sees?

    **Answer** (confirmed: binary, 2026-10-04):
    - **The test** is the detection rule of spec 01 §6.3 for the ship's owner, applied to the
      planet whether or not it has a colony (§8 "Seen"): the system explored, and in some sight
      type the owner's sensor level in the system, at that moment and with the colonizing ship
      counted, at least the planet's obscuration. Storms, nebulae and a planet's own rolled
      obscuration count, as does a cloaked colony's cloak. It is not the "seeing the planet"
      test of the windows (which has no presence and no explored test), and nothing about the
      planet is remembered. It runs each time the Colonize is carried out (on arrival, and on
      every later try while it waits for movement), before the movement test, and an unseen
      planet gives the same "no planet here to colonize" failure as a planet that is gone.
    - **The player's pick** applies no sight test: every planet of the clicked sector of an
      explored system is a candidate, hidden ones included (§8, spec 06 §2.9).
    - **The computer players** apply no sight test either: their targets are the planets of
      every system they have explored, by the real colony state (spec 05 §7.5 "Colonization",
      §7 Q78). Their colony ships get Load Cargo, Move To and Colonize as ordinary orders,
      nothing checks the target on the way, and at a planet they cannot see the Colonize fails
      on arrival, every time it is tried. The minister then finds the ship idle and, the
      planet being a target again, often gives it the same planet: the original's computer
      players can keep failing at a hidden planet turn after turn, as ours did with the full
      test.

    Since 2026-10-04 our engine follows:
    - `colonize()` (`movement.cpp`) applies the detection rule for the group's owner to the
      planet, colonized or not (`sight::canSeeColony`), with the sensors of the moment, the
      colonizing group's own counted, before the movement test, so a planet hidden by a storm,
      a nebula, its own obscuration or an undetected colony's cloak fails at once with "There is
      no planet here to colonize.", and a detected cloaked colony as "already a colony".
      `movement::colonizeProblem` applies the same test.
    - Every order given, the computer players' included, is Load Cargo, Move To and Colonize
      (`cmd::SetOrders` expands the minister's single Colonize, spec 05 §7.5), so their ships
      already checked nothing on the way; the give-up branch of `colonize()` applied only to a
      list set without that expansion (tools, tests). That branch now checks nothing either: a
      Colonize away from its planet travels there as the Move To it stands for and is carried
      out in the arriving action.
    - The computer players' targets keep no sight test, so their colony ships loop at a hidden
      planet as the original's do (spec 05 §7 Q78); OpenSE4 adds no rule to avoid it.
    - The client's Pick Object takes the Colonize candidates from every planet of the clicked
      sector of an explored system, hidden ones included (`colonizeCandidates`,
      `client/classic/sector_view.hpp`); one alone gives the order at once (spec 06 §2.9).

**Players' reports on v0.9.0 (2026-10-05).** Two reports turned on rules of this spec; the
fleet's row in the sector list is spec 06 §7 Q110.

81. **Upgrade and the engine families.** "When clicking Upgrade on the Designs page, it has the
    same issue [as Only Latest] with engines, using Ion Engine III instead of Contra-Terra
    Engine I, even if a previous design had Contra-Terra Engines." Ours replaces each component
    with the highest numeral its empire has of the component's family, leaves family 0 alone and
    changes nothing when that numeral is not higher. Which component does the original's Upgrade
    take?

    **Answer** (confirmed: binary): the last component in Components.txt order that has the same
    `Family` and whose tech requirements the design's owner meets, for every entry and every
    family, family 0 included; with none the entry stays; numerals, names, the vehicle type and
    the mount play no part, and each entry keeps its mount (§4.1). The stock large engines are
    one family in the order Ion, Contra-Terrene, Jacketed-Photon, Quantum, so with
    Contra-Terrene Engine I researched an upgraded design gets it in place of Ion Engine III,
    whatever the design held before. The candidate always opens in the designer, also when
    nothing changed, under the design's own name, which Create Design refuses as in use until it
    is changed (observed, spec 07 session 7: an Ion Engine I upgraded to Quantum Engine III with
    every large engine researched). Facilities are different: an upgrade's target is the highest
    numeral above the facility's own that the empire has researched, the first in Facility.txt
    order on a tie (spec 02 §6.6), so the highest-numeral rule is right for facility upgrades
    and wrong only for designs.

    Since 2026-10-05 the engine follows: `upgradeEntries`
    (`client/classic/screens/design_tools.cpp`) replaces each entry with
    `Rules::componentUpgradeTarget`, the family's last researched component in data-file
    order, for every family, and keeps the entry when none is researched; `isLatestComponent`
    asks the same question. Upgrade opens the designer every time (`designs.cpp`), with no
    note. Proposing the next free name (`nextVersionName`) where the original keeps the old
    one stays an OpenSE4 convenience. `Rules::latestFacilityOfFamily` (facility upgrades, the
    Upgrade Facilities button, the computer's upgrades) keeps the highest numeral, as the
    original does. Nothing else used the old numeral rule for components: the computer
    players' test whether a design can be improved (a researched successor of a higher
    numeral, spec 05 §7.5) is their own rule and is unchanged.
82. **Tagged ships ordered to attack.** "Multiple ships that are not in a fleet but are all
    selected and ordered to attack together do not attack together, only one at a time." In
    the report's turn-based game each tagged ship was asked on its own whether to enter the
    enemy's sector, and fought alone. What does the original do with an order given to tagged
    vehicles?

    **Answer** (confirmed: binary; observed, spec 07 session 7, without an enemy): §8 "Tagged
    vehicles". In a turn-based game the order goes into every tagged vehicle's list and runs at
    once with all of them as one group, acting through the first one tagged: they step together
    while every one has movement left, the entry question comes once for the group, and they
    fight one battle together. What is left when movement runs out is carried out at the next
    turn's start by each vehicle alone, at its own speed and one after another, so ships whose
    attack takes more than one turn's movement do arrive and fight one at a time in the original
    as well, unless they are in a fleet. In a simultaneous game every tagged vehicle pursues on
    its own: those that enter the target's sector on the same day fight in one battle, later
    arrivals in another.

    The engine differs in turn-based games:
    - `MainWindow::giveOrder` (`client/classic/main_window.cpp`) issues one `cmd::SetOrders`
      per tagged vehicle or fleet, and `applyLive` carries out each command before the next
      is applied (`applyEach` and `settle` in `game/turn_based.cpp`, a network host's
      `HostSession::runLive` likewise), so the first tagged ship runs its new orders alone,
      meets the enemy alone and fights alone, before the second ship has its order.
    - `Mover::build` (`game/movement.cpp`) joins the vehicles listed together only when they
      are in no fleet, stand where the actor stands and hold the actor's first order, and an
      acting fleet member takes its fleet alone; the original takes every tagged vehicle.
    - The entry question and its answer (`EntryQuestion`, `cmd::EnterSector`) name one vehicle
      or fleet, so an answered question moves that one on alone.

    Simultaneous games match: each tagged vehicle gets its own pursuit and acts alone.

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
  it first computes `pct / 100` in x87 extended precision (64-bit mantissa, the default precision
  setting; not measured at run time), multiplies the value
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
| Drone | yes (automatic) | yes (confirmed: binary) | no: unit groups in space hold no cargo (confirmed: binary) | launched from a launcher or a planet | no | destroyed (confirmed: binary) |
| Troop | no | no | no | dropped on a planet during combat only | n/a | n/a |
| Weapon Platform | no | no | no | moved to a planet as cargo, or built there | n/a | n/a |

Everything except Ship and Base is a **unit**. Units are built into cargo (the builder's cargo
first, then any other cargo space the empire owns [VERIFY scope]). If no space exists, the unit is
not built and a log message is produced. Units can be moved between ships and planets as cargo.
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
| `Family` | Integer lineage id. All numeral versions of one part share it. It drives "Only Latest" and design Upgrade. |
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
| `System Ship Movement Delay Milliseconds` | 0 | presentation only |

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
| Use Component | `Emergency Energy`, `Emergency Resupply` |
| Stellar Manipulation | create/destroy/open/close abilities |
| Cloak | `Cloak Level` of a ship or base, from hull and components alike (§8) |
| Sweep Mines | an explicit sweep (automatic sweeping also happens on entry) |
| Scrap window (a Self-Destruct order) | `Self-Destruct` |

A **mothballed** vehicle has no abilities at all (confirmed: binary).

The Ship Report "Ability" tab lists only hull and racial abilities. Component abilities appear on
the component reports.

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
| `Sector - Damage` | S | damage | – | A group stepping into the sector has a 50 % chance to take the total V1 (storms there plus the system) and stop; vehicles staying there take nothing (§6.2) (confirmed: binary). |
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
| `Long Range Scanner` | C | sectors | – | Can inspect enemy vehicles up to V1 sectors away, which also reveals their designs. |
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
| `Ship Training` | F | +exp/turn | cap | Each own object in the sector (planet facilities or vehicle abilities) is a separate source with its own largest V1 and V2. From each source in turn, a non-mothballed ship below V2 gains the smaller of V1 and (V2 − its experience). V2 = 0 means no training (confirmed: binary). |
| `Fleet Training` | F | +exp/turn | cap | Same for fleet experience (same shape; less fully traced). |
| `Modify Reproduction - System` | F | ±% | – | Population growth modifier. |
| `Change Population - System` | F | M/turn | – | Population added each turn in the system. |
| `Plague Prevention - System` | F | level | – | Plagues up to V1 are prevented in the system. |
| `Resource Conversion` | F | % loss | – | Enables the Convert Resources order with this loss. |
| `Resource Reclamation` | F | % | – | Scrap return rate for scrapping in this sector (§15). |
| `Close Warp Point` | C | – | – | Stellar manipulation. |
| `Destroy Star` / `Create Star` | C | – | – | Stellar manipulation. Destroying a star wipes out the system, including the user. |
| `Destroy Storm` / `Create Storm` | C | – | – | Stellar manipulation. Created storms are capped by the `Created Storm ...` settings. |
| `Self-Destruct` | C | – | – | Enables self-destruct. It also fires automatically when the vehicle is about to be captured. |
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
| `Ancient Ruins` | S | count | – | Number of random tech areas granted when the planet is colonized. |
| `Ancient Ruins Unique` | S | tech id | – | Grants that unique tech area when the planet is colonized. |
| `Combat Best Experience` | C | – | – | In combat, uses the highest experience among the owner's combatants. |
| `Combat Movement` | C | MP | – | Extra movement in combat only. Largest value on the vehicle (§3.2). |
| `Solar Supply Generation` | C | supply/star | – | At the end of each turn adds V1 × (stars in the current system), capped at the maximum (§7) (confirmed: binary). |
| `Extra Movement Generation` | C, H | MP | stacking id | Movement bonus, first entry per stacking id (§3.2, §6.1). |
| `Planet - Change Atmosphere` | F | turns | – | After V1 turns, the atmosphere becomes the one most of the population breathes. |
| `Weapons Always Hit` | C | – | – | The vehicle's direct-fire weapons never miss. |
| `Create Constructed Planet` | C | PlanetSize special id | – | Builds a ringworld or sphereworld. |
| `Constructed Planet Requirements` | C | custom group | kT | V2 kT of components with `Custom Group` = V1 must be present at the location. |
| `Modified Maintenance Cost` | H | ±% | – | Changes this vehicle's maintenance: −50 halves it. |
| `Ship Training - System` / `Fleet Training - System` | F | +exp/turn | cap | As `Ship Training` / `Fleet Training`, but system-wide. |
| `Long Range Scanner - System` | F | – | – | Long-range scanning across the whole system [VERIFY]. |
| `Solar Resource Generation - Minerals` / `Solar Resource Generation - Organics` / `Solar Resource Generation - Radioactives` | F | amount/star | – | Output per star in the system. |
| `Reduced Maintenance Cost - System` | F | % reduction | – | The owner's ships in the system pay less. The sign is **opposite** to `Modified Maintenance Cost`: +10 means 90 %. |
| `Shield Modifier - System` | F | points | – | Added to the maximum shields of the owner's ships in combat in the system. |
| `Combat To Hit Offense Minus` | none | % | – | Lowers own chance to hit. |
| `Combat To Hit Defense Minus` | H | % | – | Makes the vehicle easier to hit (large hulls). |
| `AI Tag 01` through `AI Tag 20` (two-digit suffix) | none | – | – | Opaque markers for AI logic. No engine rules effect. |
| `Generate Points Minerals` / `Generate Points Organics` / `Generate Points Radioactives` / `Generate Points Research` / `Generate Points Intelligence` (no dash) | none | amount/turn | – | Flat income from a component or facility, not drawn from a planet. |
| `Launch Drones` | C | per combat turn | per game turn | **Missing from Abilities.txt** but used by drone launchers. Launch only, no recovery. |
| `None` | F | – | – | Placeholder in some facility records. Ignore it. |
| `Random`, `Warp Point - Unstable`, `Warp Point - Periodic`, `Warp Point - Ability Required`, `Sector - Ability Required`, `Resupply Pod`, `Maximum Population` | none | – | – | Recognised type names that stock data does not use (confirmed: binary). Their effects were not traced; the loader MUST accept them. |

---

## 4. Designs

### 4.1 Identity and lifecycle

- Fields: name, design type, hull, ordered component list (each entry is a component plus an
  optional mount), creation date, obsolete flag, prototype flag, default combat strategy, and
  statistics (number constructed, in service, lost, and enemy tonnage destroyed).
- **Name** MUST be non-empty and differ from the name of every design in the game, other empires'
  designs included. The comparison is exact and case-sensitive (confirmed: binary). Name
  suggestions come from the empire's name list.
- **Prototype:** a design is a prototype until the first vehicle of it is built. Only prototypes
  can be edited. After that, changes go through **Copy**, which clones the design with an empty
  name. The original's Edit action refuses a design that is currently in one of the empire's
  construction queues (confirmed: binary); how the prototype state gates it otherwise was not
  traced.
- **Upgrade** builds a candidate design that replaces every component with the highest-numeral
  known component of the same `Family`, keeping the mount. It is then saved under a new name.
- **Obsolete** is a flag. Obsolete designs can be hidden in lists. Designs cannot be deleted
  directly. An obsolete design is removed automatically once all of these hold: no vehicle or unit
  of that design exists, it is in none of the owner's construction queues, and no other living
  empire has seen it in the last 50 turns (confirmed: binary).
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
  system. It costs 1 MP and one step's supply. Ships (and bases, which cannot move) and drones can
  warp; fighters cannot. Each transit through a warp point with `Warp Point - Turbulence` has a
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
  when the empire's option to route around them is on. Then a warp link whose sector on either side
  is tagged is not used, and in-system steps avoid tagged sectors. A group whose leading ship has a
  particular design type (inferred: the mine-sweeper type) is exempt.
- **In-system steps are greedy:** one square toward the target, diagonal first. The step is
  re-chosen when the next square is not the destination and is a tagged minefield (option on), has
  `Sector - Damage`, or holds a visible hostile object. The re-choice picks at random among the
  other steps that still approach the target (1 of 2 when moving diagonally, 1 of 3 when moving
  straight). After 10 failed tries the vehicle does not move and the order fails.
- In a system with `System - Destructive Center`, paths avoid the squares near the centre within
  the `System - Movement Towards Center` radius (the exact cost formula was not decoded).
- **Entering a sector with enemy ships:** in simultaneous games human ships simply enter; in
  turn-based games the human is asked, and declining stops the move and fails the order. Computer
  players decide for themselves. Groups made only of drones always enter.
- A ship whose space yard has items in its construction queue cannot move or warp; the order fails.
- Fighters stay in their system. Satellites, mines, troops and platforms never move by themselves.
  Drones move toward their target (§12).

**Hazards met while moving:**

- Stepping into a sector with `Sector - Damage` (the storm objects there plus the system's own
  value) has a 50 % chance of dealing that damage to each vehicle of the moving group, with no
  shields. If it does, movement stops and the order fails. Vehicles that stay in a storm take
  nothing.
- Hitting a minefield stops the move and fails the order.
- In turn-based games only, entering a sector with enemies starts combat at once and fails the
  order.

**Stellar drift and centre damage**, applied at the end of the turn after the economy phase, with
no MP or supply spent:

- `System - Movement Towards Center` moves every ship, base and unit group V1 greedy steps toward
  the centre square (6, 6).
- `System - Movement Random` picks one random target square per turn, shared by every system and
  drawn only from sector numbers 0..144 (the original never picks the last row and a half); objects
  move V1 steps toward it.
- `System - Destructive Center` deals V1 damage to everything on the centre square.

### 6.3 Turn modes

**Turn-based.** Orders execute as soon as they are issued, spending MP. At the start of each turn,
every vehicle regains MP and first continues its existing order list.

**Simultaneous** (this project's mode). Players only queue orders; the host resolves all movement
over a **30-day** month (confirmed: binary):

1. At the start, every vehicle's MP is reset to its maximum, and every fleet member's MP to the
   lowest maximum among the members in the fleet's sector. Each vehicle's day counter is set to 0.
2. On each day, before anyone acts, every vehicle adds speed / 30 to its counter, where speed is its
   current MP (for fleet members, the fleet's lowest maximum).
3. A vehicle acts on a day when its counter is at least 1, and after acting subtracts 1. A vehicle
   with 0 maximum MP that has orders acts once, on day 1.
4. Each action carries out exactly **one** order execution with 1 MP available. Orders that need no
   movement (load, drop, launch, use component...) also use up that day's action, so the next order
   starts on the vehicle's next acting day.
5. Within a day, vehicles act in the order the objects were created.
6. After each day, every sector where something acted is checked. If mutually hostile forces that
   can see each other are there, a combat is fought, unless every object now present already
   fought there this turn. Combat is always resolved automatically.
7. Combat does **not** clear orders or stop movement. Its only effect on orders is that a Sentry
   order at the head of a participant's list is removed.

In exact arithmetic, steps 2–3 give a vehicle with speed M a step on day d when
floor(d·M/30) > floor((d−1)·M/30): for M = 5, days 6, 12, 18, 24 and 30, as the manual says. The
original accumulates the counter in floating point (the quotient and sum in the x87 registers,
stored as a 64-bit double each day). Depending on the x87 precision in effect at run time, many
speeds then lose their last step: with extended or double precision a speed-1 ship would never
move and speed 5 would move on days 7, 13, 19 and 25; with single precision only speeds 13, 17,
19, 21, 23, 25 and 27 lose day 30. This needs checking in the running game (§19). The engine
SHOULD use the exact formula until then.

The Cargo Transfer and Launch/Recover windows do not exist in this mode. Their effects happen
through the Load, Drop, Launch-remote and Recover-remote orders during resolution. A movement log
records every step for replay, per system and per ship.

### 6.4 Interruption

All rules in this section are (confirmed: binary) unless marked otherwise.

- An order whose destination cannot be reached fails, and a failed order clears the whole list
  (§8).
- Movement stops and the list is cleared by: storm damage on entry, turbulence damage on a warp,
  a minefield, a refused entry into an enemy sector, and (turn-based games only) combat on entry.
- Being in combat in a simultaneous game neither stops movement nor clears orders.
- Each empire has two options, off by default: clear orders on encountering an enemy empire, and
  on encountering any empire (computer players take them from their AI settings). When one of them
  applies after a warp into a system where such an empire has objects, the Warp order fails and
  the list is cleared (confirmed: binary). Whether in-system steps check these options was not
  traced.
- If a vehicle's maximum MP drops mid-turn, its remaining MP are capped at once (§6.1). If supply
  runs out during its own step, it makes no further steps this turn (the maximum becomes 1).
- A cloaked vehicle that the enemy cannot detect passes through enemy-held sectors without combat.
  Detection triggers combat as usual. A cloaked vehicle decloaks when it attacks (§8).
- Treaties can allow passing through another empire's sectors without combat (see the diplomacy
  spec; not checked here).

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
- `Solar Supply Generation` adds V1 × (number of stars in the system) at the end of the turn, after
  depot refills and fleet pooling, capped at the maximum.
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
- **Fleets:** the members at the fleet's location act as one group on the fleet's head order, and
  every change to the list applies to each of those members' lists.
- **Ad-hoc groups:** ships in the same sector whose head orders are identical execute them as one
  group, so they move together at the pace of the slowest.

Several orders are expanded into simpler ones at the moment they are given; they never exist as
stored orders (confirmed: binary). "Composite" orders below become Move To (when the target
sector is elsewhere) plus the action.

| Order (hotkey) | Parameters | Behaviour |
|---|---|---|
| Move To (M) | sector, in any known system | Paths and warps as needed (§6.2). Done on arrival; waits when out of MP; fails on an invalid destination or a blocked path. |
| Move To Waypoint (Ctrl 0–9) | waypoint | The waypoint is looked up when the order runs; if it has been deleted the order fails (§8.1). |
| Set Waypoint (Alt 0–9) | sector | Empire-level. Defines a waypoint (§8.1). |
| Attack (A) | target | Turn-based: starts combat with enemies at the ship's current location. For a ship target it becomes a pursuit: the ship follows the target and on reaching it decloaks if needed and fights. The order stays until the target is destroyed or no longer seen, then it is done (confirmed: binary). |
| Warp (W) | warp point | Composite. Needed for links to unexplored systems. The transit needs MP (else it waits), a warp point in the sector and every group member able to warp (else it fails), and no member with a space yard whose queue is not empty (else it fails). It costs 1 MP and one move's supply (§7). On each transit there is a 50 % chance that every member takes damage equal to the warp point's total `Warp Point - Turbulence`; taking that damage, or meeting mines or enemies on arrival, fails the order (confirmed: binary). |
| Resupply (S) | — | Expanded at once into Move To the nearest depot: a colonized planet with `Supply Generation` owned by the empire or by an empire with Military Alliance or better, in an explored system, skipping sectors that hold a visible, armed, non-mothballed hostile (confirmed: binary). |
| Repair (R) | — | Expanded at once into Move To the nearest own object with `Component Repair`, preferring immobile sources (planets, bases); repair ships are used only if there is no immobile source (confirmed: binary). |
| Explore (E) | — | Expanded at once into Move To plus Warp for the warp point, nearest by travel distance, that lies in a system the empire has explored and leads to one it has not, skipping warp points another own ship is already bound for. With no candidate nothing is added (confirmed: binary). |
| Colonize (C) | planet | Composite. At the current location it first adds Load Cargo (population), but only if the ship carries no population. See the rules below the table. |
| Sentry (Y) | — | Stays at the head at no MP cost. It ends when an empire the owner is hostile to (War, Non-Intercourse, None or no contact) is present in the same **system**, or when any group member's supply is below `Supply Amount for Low Supply Warning`. Then only the Sentry order is removed and the following orders run; the list is not cleared (confirmed: binary). |
| Set Patrol (P) | list of points | Each click appends a Move To. Clicking the ship's own location ends it and turns Repeat on; if only one order results, the list is cleared instead (confirmed: binary). |
| Repeat Orders (K) | toggle | See the list rules above. |
| Clear Orders (Del) | — | Empties the list and switches Repeat off. |
| View Orders (V) | — | UI only. |
| Load Cargo (L) | cargo type, sector | Composite. Loads as much of the type as fits, from own planets in the sector first, then own vehicles. Always done, even when nothing loads (confirmed: binary). |
| Drop Cargo (D) | cargo type, sector | Composite. Drops the type into an own colony or vehicle there that has room. Can fail (confirmed: binary). |
| Launch Units Remotely (I) | unit type, sector | Composite. Launches as many units of the type as allowed (§12). Always done (confirmed: binary). |
| Recover Units Remotely (O) | unit type, sector | Composite. Recovers as many units as there is room for. Always done (confirmed: binary). |
| Cargo Transfer (T) | — | Instant window, turn-based games only (§11). |
| Launch/Recover Units (U) | — | Instant window, turn-based games only (§12). |
| Jettison Cargo (J) | — | Destroys the selected or all cargo. No MP. |
| Fleet Transfer (F) | — | Window (§9). |
| Change Formation\Strategy (H) | — | Fleet only. |
| Cloak (Z) / Decloak (X) | — | Take effect immediately; they are not queued. Cloaking needs a working part whose `Cloak Level` gives level 2 or more in some sight type, and supply above 0. While cloaked the vehicle pays, every turn, the total mounted `Supply Amount Used` of its working cloak parts (a group of fighters pays it per unit), and it decloaks by itself at 0 supply or when it can no longer cloak. A cloaked vehicle cannot colonize, manipulate stellar objects, be mothballed, scrapped, analyzed or retrofitted (confirmed: binary). |
| Sweep Mines (Ctrl M) | — | Runs the minefield encounter of the current sector again (§12), then costs 1 MP (if any is left) and one move's supply. Always done (confirmed: binary). |
| Add / Remove Tagged Minefield (Ctrl T / Ctrl R) | sector | Empire-level. |
| Use Component | component | A part with `Component Destroyed On Use` is destroyed first. `Emergency Energy`, only when the vehicle's maximum MP is above 0: in turn-based games it adds V1 to this turn's remaining MP, without a cap; in simultaneous games it adds V1 to the day counter (§6.3), giving up to V1 extra actions at one per day. `Emergency Resupply` adds V1 supply, capped at the maximum, with no effect on unlimited supply or mothballed vehicles. No supply is charged for the use (confirmed: binary). |
| Self-Destruct | — | A separate order; needs a working `Self-Destruct` ability (§15). |
| Stellar Manipulation (B) | action | See below. |
| Scrap / Analyze / Mothball (G) | — | §15. |
| Change Name (N) | text | — |
| Toggle Minister Control | — | Puts the vehicle under AI control, limited by the minister categories the empire enables. |
| Build Queue (Q) | — | Only if the vehicle has a `Space Yard`. |

**Colonize** (confirmed: binary):

- It needs the ship in the planet's sector with movement left; without movement it waits.
- It fails if the planet is missing, not colonizable or already colonized, if any group member is
  cloaked, or if no member has the `Colonize Planet - *` ability matching the planet type. The game
  options that limit colonization to the empire's own planet type or to breathable atmospheres
  apply.
- The colonizer is the last suitable member in group order. The colony starts with
  `Automatic Colonization Population`, and then every kind of cargo on the ship is dropped under
  the Drop Cargo rules, so only what fits arrives; the rest is lost with the ship, which is
  consumed.
- When two ships target the same planet, the first one processed wins; the other's order fails.
- A colony with 0 population is legal but cannot build facilities.

**Stellar manipulation** (confirmed: binary):

- It needs movement left but does not spend it. It is refused while the vehicle is cloaked and
  while a visible hostile is present in the target sector.
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
- Other preconditions per action: asteroids to create a planet, a planet to destroy one, a star to
  destroy or nebularise, a warp point to close, and so on.

Planet-only orders (Scrap Facilities, Abandon Planet, Use Facility, Convert Resources) belong to
other specs. The movement-log replay orders are UI only.

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
  member leaves.
- A fleet has one location. Its members are the owner's vehicles in that sector that carry the
  fleet's tag.
- **Who can join:** ships always; bases only when `Bases Can Join Fleets` is True; fighter groups
  yes; drones, satellites and mines never. Fleet Transfer offers only own vehicles in the same
  sector that are not already in a fleet. "Add All" and "Remove All" are available. Joining or
  leaving a fleet can clear the vehicle's own orders.
- **Leader:** the player-chosen leader when one is set, otherwise the first member in the sector's
  object order. If the chosen leader leaves or is destroyed, the choice is cleared and the first
  member leads again. The leader only matters for the formation.
- **Movement:** the fleet's movement is the minimum of its members' MP left (and of their maximum
  MP for display). During turn processing every member's MP is set to that minimum, so the fleet
  moves at its slowest member's speed; a member with 0 maximum MP freezes the fleet. Members at the
  fleet's location execute the fleet's orders as one group; the orders are copies held in each
  member's own list, and members elsewhere are not part of the group (§8).
- **Supply display:** the Fleet Report sums the members' current supply, maximum supply and
  per-move cost, leaving out fighter groups and members with unlimited supply, and shows "Endless"
  when every member has unlimited supply. Supply is pooled among the members in the fleet's
  sector at the end of each turn (§7).
- **Strategy** (from the manual; not checked in the binary): the fleet strategy overrides each
  member's design default. By default the whole fleet forms one combat group in its formation. A
  strategy can list vehicle types that break formation.
- **Experience:** a fleet has its own fractional experience value, capped at 50 (a gain past 50
  sets it to 50). It is not reduced when members join or leave; it is lost only when the fleet is
  deleted. `Fleet Training` raises it. One further gain path adds 0.1 with a 1-in-4 chance; its
  trigger was not traced.

---

## 10. Formations

All rules in this section are (confirmed: binary) unless marked otherwise.

- The fleet's formation takes effect only in **combat**.
- The leader takes the leader cell. The other members get positions 1, 2, 3… in the order their
  pieces enter combat, skipping members whose strategy tells them to break formation. Members
  numbered beyond the formation's position count get no formation place. If the leader itself
  breaks formation, the fleet is anchored differently (only partly traced).
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
- The formation breaks when the leader is destroyed, or when it is cleared by a combat group order
  (from the manual; not checked in the binary).
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
- **Recovery:** only fighters and satellites can be recovered. There is no per-turn limit; free
  cargo space is the only limit. In turn-based games a fighter group can be recovered only while
  it has its full movement points.
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
- **Grouping:** units in space are held in groups, one per (owner, unit kind, sector); different
  designs share a group. Every drone is launched as its own group and drones never merge.
  Launching into a group refills the whole group's supply to its new maximum; new groups start
  full.
- **Group abilities** list each unit's design abilities once per unit, so summed abilities scale
  with the number of units.
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
| Fighter | Moves like a ship inside the system. Whether it can use warp points was not determined. |
| Satellite | Stationary. Fires on enemies in its sector. |
| Mine | Hidden: hull cloak level 5 in all sight types (stock data). Triggers when a hostile vehicle enters the sector. |
| Drone | Needs a ship or planet target at launch and moves toward it automatically to ram. How drones pick and follow targets was not traced. |

The remaining points come from the manual and were not checked in the binary:

- **Mines:** "hostile" means any empire with which there is no treaty, or war. Mines hit cloaked
  vehicles as well. They hit fighters and drones when `Fighters/Drones Can Be Hit By Mines` is
  true. A mine explosion damages the mine as well. The owner always sees its own mines.
- **Sweeping:** a vehicle with `Mine Sweeping` removes up to V1 enemy mines when it enters a
  sector, before detonation [VERIFY order], and again on each Sweep Mines order.
- **In combat:** after combat, survivors are recovered automatically into own carriers in the
  sector that have space. Units that do not fit stay in space.
- **Self-destruct** works on unit groups as on ships. Drone groups and minefields cannot be
  scrapped (§15).

---

## 13. Repair

All rules in this section are (confirmed: binary) unless marked otherwise.

- **When:** once per turn, in each empire's end-of-turn processing, right after that empire's
  maintenance has been paid (§16). That this comes after the turn's movement and combat is
  (inferred) from the phase order.
- **Where and how much:** repair is pooled per (empire, sector). The pool is the Sum of
  `Component Repair` V1 over all of that empire's own objects in the sector: ships, bases and
  planet facilities. Allies' sources do not count. Repair is free.
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
- Whether a planet's facilities need population to repair was not determined.

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

Each action applies to the selected own vehicles in one sector. Scrap, Analyze, Mothball and
Retrofit need an own space yard in the sector and a vehicle that is not cloaked. All rules in this
section are (confirmed: binary).

| Action | Requires | Effect |
|---|---|---|
| Scrap | a space yard, not cloaked | Destroys the vehicles. A ship or base returns, per resource, round(design cost × P %), where P is the larger of `Scrap Ship Percent Returned` and the best `Resource Reclamation` among the owner's objects in the sector. A fighter or satellite group returns round(design cost × `Scrap Unit Percent Returned` %) per unit, times the number of units. Drone groups and minefields cannot be scrapped. Damage does not lower the value. Cargo on board is lost without refund. |
| Analyze | a space yard, not cloaked | Destroys the vehicles, returns no resources, and grants research from technology the empire lacks (see the research spec). The UI gives a qualitative "research potential". |
| Mothball | a space yard, not cloaked, status Normal, no cargo | Status becomes Mothballed: no abilities, 0 movement, 0 supply, no maintenance. |
| Unmothball | status Mothballed | Costs round(design cost × `UnMothball Ship Percent Cost` %) per resource, and every resource must be in stock. The vehicle returns to Normal. Bases and vehicles with a working `Quantum Reactor` come back with full (unlimited) supply; others are refilled only if they are at a resupply depot and otherwise stay at 0. |
| Retrofit | a space yard | §14. |
| Self-destruct | a working `Self-Destruct` ability | The vehicle is destroyed. No yard is needed and nothing is refunded. |
| Fire On | another own object in the sector with at least one weapon | The selected vehicles are destroyed. The last armed object cannot be destroyed this way. |

"round" is to nearest with halves to even, computed in floating point (see Conventions).

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
  count, and the member list with the leader marked. Clicking a member makes it the leader.
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
   1; processing within a day is in object creation order; combat does not clear orders (§6.3)
   (confirmed: binary). Still open: the counter is kept in floating point, and whether some speeds
   lose their last step (a speed-1 ship never moving, for example) depends on the x87 precision
   at run time. Observe a speed-1 and a speed-5 ship in a simultaneous game.
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
    limited by cargo space), groups units per (owner, unit kind, sector) across designs, and never
    merges drones. New groups start full and launching refills the whole group. When launched units
    first move was not determined (§12) (confirmed: binary).
28. **Hazard damage outside combat:** storms, destructive centres and turbulence hit armor first,
    then other components, with no shields. A unit group takes it once for all its members.
    **Answer (partial):** storm damage happens only to a group stepping into the storm sector, with
    a 50 % chance, and stops it; turbulence is 50 % per transit; neither uses shields. Destructive
    centres hit everything on the centre square at the end of the turn (§6.2) (confirmed: binary).
    How the damage is spread over components was not checked here.
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
    is free and uses own sources only. Whether facility repair needs population was not determined
    (§7, §13) (confirmed: binary).
33. **Training:** the engine uses the single best source (sector or system facility) and treats a
    `Val 2` of 0 as no cap. **Answer:** sources stack: every own object in the sector is a
    separate source with its own largest V1 and V2, and a V2 of 0 gives no training (§3.3)
    (confirmed: binary for sector-level ship training; the system-wide and fleet paths have the
    same shape but were less fully traced).
34. **Colonization:** population or units beyond the new colony's capacity are lost, the colony
    type is the first of the empire's colony types, ruins are used up, and asteroids cannot be
    colonized. When two ships reach a planet in the same turn the lower vehicle id wins.
    **Answer (partial):** cargo beyond capacity is lost; the first ship processed (object creation
    order) wins and the other's order fails; the colony also receives
    `Automatic Colonization Population` (§8) (confirmed: binary). Colony type and ruins were not
    determined.
35. **Stellar manipulation:** the engine requires movement left but does not spend it. Destroy
    Star leaves a Destroyed Star and removes every other body but warp points. Created nebulae
    and black holes take the first system type of that physical type; removing them takes the
    first normal type without abilities. Created storms get one random effect up to the
    `Created Storm Maximum ...` caps. Opened links are two-way, and their far end goes on the
    free edge sector facing the origin. **Answer (partial):** movement is required but not spent
    (confirmed: binary). The original also refuses while cloaked or with a visible hostile in the
    target sector, needs the part's supply, and for Open Warp Point refuses existing links, systems
    with 10 warp points and blocked systems (§8) (confirmed: binary). The created-object details
    were not checked.
36. **End-of-turn supply order:** the engine runs, per empire, unit and cloak upkeep, then depot
    refills, fleet pooling and solar collectors, then the limits (a drone at 0 is lost, a cloak at
    0 supply drops). The text above fixes only that solar collectors come after depots and
    pooling. Is the rest in that order, and is the upkeep of units part of this step or of the
    per-object upkeep that follows? (inferred)
37. **Storms on a warp arrival:** does a group that arrives through a warp point in a sector with
    `Sector - Damage` roll for storm damage as if it had stepped in? The engine says yes.
    (inferred)
38. **Emergency Energy in simultaneous games:** it adds V1 to the day counter. Does it also add
    V1 to the movement points left, so the extra actions can be steps? The engine adds both.
    (inferred)
39. **Acting order within a day:** vehicles act in object creation order. Where do planets with
    launch or recover orders and fleets fit in? The engine puts planets first (they were created
    before any vehicle), a fleet at its leader's place, and a fleet member's own in-place orders
    right after its fleet. (inferred)
40. **Sentry "present":** the engine ends a Sentry when a hostile empire has a colony in the
    system or a vehicle there that the owner can see. Do unseen (cloaked) vehicles count, and do
    colonies? (inferred)
41. **Recovery into a ship:** does a ship need the matching launch ability (bay) to recover units,
    as the engine requires? Planets need none. (inferred)
42. **Mounted damage far out:** with a mount the table index is clamped to 1..20, so a weapon
    whose range-20 damage is above 0 would reach any distance. The engine's maximum range stops
    at the last damaging range plus the range modifier. How far does the original let such a
    weapon fire? (inferred)
43. **Groups of several designs:** the engine keeps one record per design inside an (owner, unit
    kind, sector) group. They share the per-sector caps and launch refills, but move and fight as
    separate records. (inferred representation)
44. **Training sources:** the engine takes the owner's populated colonies first, then vehicles, as
    sources, and facilities need population. What order and population rule does the original
    use? (inferred)
45. **Obsolete design purge:** "seen in the last 50 turns" is read as "still in the empire's list
    of seen designs"; the list itself is kept by the intelligence rules. (inferred)
46. **Attack on a planet:** the engine ends it when the colony is gone or has become the
    attacker's. When does the original end it? (inferred)
47. **Repeat battles within a turn:** "every object present already fought here" — the engine
    counts vehicles (not mines) and colonies whose owner is hostile to, or faced by, another owner
    present, and takes a cloaked vehicle that did not fight as unseen. (inferred)
48. **Drift target:** the engine draws the shared random target only in turns when some system
    drifts. Does the original draw it every turn? This changes only the random sequence.
    (inferred)
49. **Sentry with Repeat on:** when a Sentry ends, the engine removes it even with Repeat on,
    instead of keeping it at the end of the list. (inferred)
50. **Names taken meanwhile:** a design created with a name that another design took since
    the order was given (two players in one turn, a computer player's name list) gets the
    first free Roman numeral ("Scout II") instead of being refused. Premade starting
    designs keep their plain name when it is free, else get the empire's name in front.
    (inferred)

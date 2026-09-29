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
- "Undamaged" means a component whose accumulated damage is below its structure. Destroyed
  components provide no abilities.
- All arithmetic is integer. Percentages are applied as `value * pct / 100`, truncated toward zero
  unless noted **[VERIFY rounding]**.
- `Settings[...]` means a key in `Settings.txt`. The stock value follows in parentheses.

---

## 1. Vehicle classes

Every vehicle is an instance of a **design**. A design is a **vehicle size** (hull) plus an ordered
list of components. The hull's `Vehicle Type` decides the class:

| Class | Moves itself | Warp | Carries cargo | How it enters space | Recoverable | Fuel exhausted |
|---|---|---|---|---|---|---|
| Ship | yes | yes | yes | built at a space yard | n/a | 1 MP/turn, crippled (§7) |
| Base | no (0 MP) | no | yes | built at a space yard | n/a | as a ship |
| Fighter | yes | normally no [VERIFY] | no | launched from a ship bay or a planet | yes | destroyed |
| Satellite | no | no | no | deployed from a ship bay or a planet | yes | goes dormant |
| Mine | no | no | no | laid by a mine layer or a planet | no | non-supply parts keep working |
| Drone | yes (automatic) | may [VERIFY] | yes (only unit allowed) | launched from a launcher or a planet | no | destroyed |
| Troop | no | no | no | dropped on a planet during combat only | n/a | n/a |
| Weapon Platform | no | no | no | moved to a planet as cargo, or built there | n/a | n/a |

Everything except Ship and Base is a **unit**. Units are built into cargo (the builder's cargo
first, then any other cargo space the empire owns [VERIFY scope]). If no space exists, the unit is
not built and a log message is produced. Units can be moved between ships and planets as cargo.
Troops and weapon platforms are never placed in open space.

Units that are in space are held as **groups**: one per (owner, unit design, sector). Launching a
unit into a sector that already has a group of the same design adds it to that group.
Per-player caps are `Settings[Maximum Mines Per Player Per Sector]` (100) and
`Settings[Maximum Satellites Per Player Per Sector]` (100). Global counts are capped by
`Default Number Of Ships Per Player` (200) and `Default Number Of Units Per Player` (1000)
[VERIFY: probably overridable in game setup].

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
| `Engines Per Move` | Engines required per movement point. 0 means the hull cannot move (§6.1). |
| `Number of Tech Req`, `Tech Area Req i`, `Tech Level Req i` | Research needed before the hull can be designed. |
| `Number of Abilities` + ability block | Abilities the hull itself grants (§3). |
| `Requirement Must Have Bridge` | True/False. The design needs at least one `Ship Bridge`. |
| `Requirement Can Have Aux Con` | True/False. Whether `Ship Auxiliary Control` components are allowed. |
| `Requirement Min Life Support` | Minimum count of `Ship Life Support` components. |
| `Requirement Min Crew Quarters` | Minimum count of `Ship Crew Quarters` components. |
| `Requirement Uses Engines` | Whether the hull uses engines. False for immobile hulls. |
| `Requirement Max Engines` | Maximum number of engine components. |
| `Requirement Pct Fighter Bays` | Minimum % of hull tonnage that must be fighter-bay components. |
| `Requirement Pct Colony Mods` | Minimum % of hull tonnage that must be colony modules. |
| `Requirement Pct Cargo` | Minimum % of hull tonnage that must be cargo components. |

The header also describes unit-only flags, `Launched from Ship` and `Launched from Planet`. No
stock record defines them, so both MUST default to true for launchable unit types.

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
| `Vechicle List Type Override` (+ `Vehicle List Type Description`) | Optional. A comma-separated list that replaces `Vehicle Type`. |
| `Supply Amount Used` | Supply consumed each time the component is activated. For an engine this is per move, for a weapon per shot, for a one-shot device per use (§7). |
| `Restrictions` | `None`, or `One Per Vehicle` through `Ten Per Vehicle`: the maximum count of this component on one design. |
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
  component value (100 means unchanged).
- `Range Modifier` is a signed shift of the damage table. With modifier R, the mounted weapon does
  at range r what the base weapon does at range r−R, so the maximum range grows by R.
  `Weapon To Hit Modifier` is a signed to-hit adjustment.
- `Vehicle Size Minimum` and the optional `Vehicle Size Maximum` bound the hull `Tonnage`.
- `Comp Family Requirement` (optional) is a comma-separated list of allowed `Family` ids.
  `Weapon Type Requirement` is `Any`, `None` (non-weapons only) or a specific weapon type.
  `Vehicle Type` is `Any` or one hull class.
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
| `Population Mass` | 5 | cargo kT per 1M population [VERIFY] |
| `Maximum Population For Abandon Planet Order` | 50 | planet order gate |
| `System Ship Movement Delay Milliseconds` | 0 | presentation only |

---

## 3. The ability system

### 3.1 Sources and activation

A vehicle's abilities come from four sources:

1. undamaged components (of a component's ability values, a mount scales only shield generation,
   through `Shield Percent`; its other multipliers act on size, structure, cost, supply and weapon
   stats);
2. the hull (always active and never damaged);
3. racial traits (for example `Vehicle Speed` +1 MP and `Supply Cost` −25 %);
4. its location: facility and system abilities with a "- System" or sector scope, and stellar
   abilities of storms, warp points and system types.

Most abilities are automatic. Some only act through an order:

| Order | Abilities that need it |
|---|---|
| Colonize | the colonize abilities |
| Use Component | `Emergency Energy`, `Emergency Resupply` |
| Stellar Manipulation | create/destroy/open/close abilities |
| Cloak | component-provided `Cloak Level` (hull-provided cloak is always on) |
| Sweep Mines | an explicit sweep (automatic sweeping also happens on entry) |
| Scrap window | `Self-Destruct` |

A **mothballed** vehicle has no abilities at all.

The Ship Report "Ability" tab lists only hull and racial abilities. Component abilities appear on
the component reports.

### 3.2 Aggregation

Unless the table in §3.3 says otherwise, numeric abilities **sum** over all sources on the vehicle.
These exceptions apply:

- **Best single component only:** `Combat To Hit Offense Plus`, `Combat To Hit Defense Plus`,
  `Multiplex Tracking` and `Combat Movement`. The stock descriptions state this. Whether the
  hull's value adds to the best component value is [VERIFY]. The recommendation is hull + best
  component.
- **Per identifier:** `Extra Movement Generation` takes the maximum `Val 1` for each distinct
  `Val 2`, then sums across identifiers.
- **Per sight type:** `Cloak Level` and `Sensor Level` take the maximum level for each sight-type
  name.
- **Engine bonus:** see §6.1 for `Movement Bonus`.
- **Facilities marked "only 1 per planet effective"** (`Resource Reclamation`, `Ship Training`,
  `Fleet Training`): take the maximum per planet. Across planets in the same sector, use the
  maximum [VERIFY].
- **Flags** (no values) are present or absent. For `Ship Bridge`, `Ship Life Support`,
  `Ship Crew Quarters` and `Ship Auxiliary Control`, the **count** of undamaged components
  matters.

### 3.3 Ability type reference (all identifiers)

"V1" and "V2" mean `Val 1` and `Val 2`. A dash means the value is unused. The source column says
where the stock data uses the ability: C = component, H = hull, F = facility, S = stellar or system
type, none = defined but unused.

| Identifier | Src | V1 | V2 | Effect |
|---|---|---|---|---|
| `Warp Point - Turbulence` | S | damage | – | Every object that transits the warp point takes V1 normal-type damage. |
| `Star - Unstable` | S | % per year | – | Chance that the star explodes. |
| `Sector - Sight Obscuration` | S | level | – | Raises obscuration in all sight types to V1 for objects there (a storm covers its sector, a nebula its system). Units do not benefit. |
| `Sector - Sensor Interference` | S | penalty | – | Subtracted from to-hit rolls in combat there. |
| `Sector - Shield Disruption` | S | points | – | Shield points removed from each combatant during combat there. |
| `Sector - Damage` | S | damage/turn | – | Damages every object in the sector each turn. |
| `Resource Generation - Minerals` / `Resource Generation - Organics` / `Resource Generation - Radioactives` | F | amount/turn | – | Base planetary production, before planet value and population modifiers. |
| `Point Generation - Research` / `Point Generation - Intelligence` | F | points/turn | – | Research or intelligence output. |
| `Spaceport` | F | – | – | The system's planets deliver their output to the empire. |
| `Palace` | none | – | – | Capital marker. |
| `Supply Generation` | F | – | – | Resupply depot. Refills vehicles in the sector to full (§7). |
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
| `Multiplex Tracking` | C | targets | – | Number of targets per combat turn (the default is 1). Best component only. |
| `Combat To Hit Offense Plus` | C, H | % | – | Improves own chance to hit. |
| `Combat To Hit Defense Plus` | C, H | % | – | Makes the vehicle harder to hit. |
| `Mine Sweeping` | C | mines | – | Enemy mines removed per sweep (§12). |
| `Medical Bay` | C | plague level | – | Cures plagues up to V1 on a planet at the same location [VERIFY target]. |
| `Movement Bonus` | C | MP | – | Engine-family bonus (§6.1). |
| `Emissive Armor` | C | threshold | – | A hit of V1 damage or less on this armor is ignored. The catalog wording differs [VERIFY]. |
| `Shield Regeneration` | C | points/combat turn | – | Restores shields during combat. |
| `Master Computer` | C | – | – | Replaces bridge, life support and crew quarters (§4.2, §6.1). |
| `Cloak Level` | C, H | sight type name | level | Obscuration in that sight type: `EM Active`, `EM Passive`, `Psychic`, `Gravitic` or `Temporal`. From a component it applies only while cloaked. From a hull it always applies. |
| `Sensor Level` | C | sight type name | level | Detection in that sight type. The empire sees a system at its best level per type (see the sight spec). |
| `Emergency Resupply` | C | supply | – | On use, adds V1 supply (§7). |
| `Emergency Energy` | C | MP | – | On use, adds V1 movement points this turn. |
| `Long Range Scanner` | C | sectors | – | Can inspect enemy vehicles up to V1 sectors away, which also reveals their designs. |
| `Open Warp Point Distance` | C | distance | – | Opens a warp point to a chosen system within range. The stock text maps V1 = 10 to 100 light years [VERIFY units]. |
| `Create Planet Size` | C | size index | – | Turns asteroids into a planet no larger than V1. |
| `Destroy Planet Size` | C | size index | – | Destroys a planet no larger than V1, leaving asteroids. |
| `Boarding Attack` | C | strength | – | Offensive strength in ship capture. |
| `Boarding Defense` | C | strength | – | Defensive strength against capture. |
| `Standard Ship Movement` | C | MP | – | Marks an **engine** and gives its base movement. |
| `Ship Bridge` | C | – | – | Control component. Fighter, troop, satellite, drone and platform cores also carry this type. |
| `Ship Auxiliary Control` | C | – | – | Backup bridge that keeps control when the bridge is lost [VERIFY]. |
| `Ship Life Support` | C | – | – | Counted against `Requirement Min Life Support`. |
| `Ship Crew Quarters` | C | – | – | Counted against `Requirement Min Crew Quarters`. |
| `Scanner Jammer` | C | – | – | Enemy long-range scanners cannot inspect this vehicle. |
| `Quantum Reactor` | C | – | – | Unlimited supply: the vehicle never runs down. |
| `Supply Storage` | C | supply | – | Adds maximum supply. Engines carry it too. |
| `Space Yard` | C, F | resource (1 = minerals, 2 = organics, 3 = radioactives) | rate/turn | Gives a construction queue. One entry per resource. |
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
| `Ship Training` | F | +exp/turn | cap | Ships in the sector gain V1 experience per turn, up to V2. |
| `Fleet Training` | F | +exp/turn | cap | Same for fleet experience. |
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
| `Stop Planet Destroyer` / `Stop Star Destroyer` / `Stop Nebulae Creator` / `Stop Black Hole Creator` / `Stop Open Warp Point` / `Stop Close Warp Point` | F | – | – | Blocks that manipulation in the facility's system [VERIFY scope]. |
| `Component Destroyed On Use` | C | – | – | The component becomes destroyed after its use ability fires. |
| `Ancient Ruins` | S | count | – | Number of random tech areas granted when the planet is colonized. |
| `Ancient Ruins Unique` | S | tech id | – | Grants that unique tech area when the planet is colonized. |
| `Combat Best Experience` | C | – | – | In combat, uses the highest experience among the owner's combatants. |
| `Combat Movement` | C | MP | – | Extra movement in combat only. Best component only. |
| `Solar Supply Generation` | C | supply/star | – | Each turn adds V1 × (stars in the current system) supply. |
| `Extra Movement Generation` | C, H | MP | stacking id | Flat movement bonus (§3.2, §6.1). |
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

---

## 4. Designs

### 4.1 Identity and lifecycle

- Fields: name, design type, hull, ordered component list (each entry is a component plus an
  optional mount), creation date, obsolete flag, prototype flag, default combat strategy, and
  statistics (number constructed, in service, lost, and enemy tonnage destroyed).
- **Name** MUST be non-empty and unique. The manual says both "unique among your designs" and "no
  two designs in the game" [VERIFY scope]. Name suggestions come from the empire's name list.
- **Prototype:** a design is a prototype until the first vehicle of it is built. Only prototypes
  can be edited. After that, changes go through **Copy**, which clones the design with an empty
  name.
- **Upgrade** builds a candidate design that replaces every component with the highest-numeral
  known component of the same `Family`, keeping the mount. It is then saved under a new name.
- **Obsolete** is a flag. Obsolete designs can be hidden in lists and are deleted automatically
  after some time [VERIFY interval]. Designs cannot be deleted directly.
- Unit designs and ship/base designs are listed separately. **Enemy designs** become known through
  combat, long-range scanning or intelligence. They show without design type or prototype status.
- The combat strategy of a fleet overrides a design's default strategy.

### 4.2 Validity rules

The designer shows every failed rule as a warning. A design with any warning cannot be created.
Let `T` be the hull `Tonnage`, and `count(X)` the number of components with ability X.

1. The hull's and every component's tech requirements are met. Every mount's tech requirements
   are met.
2. Each component's allowed-vehicle set (§2.3) contains the hull's `Vehicle Type`.
3. Σ `Tonnage Space Taken` (after mounts) ≤ T.
4. `Requirement Must Have Bridge` requires `count(Ship Bridge)` ≥ 1.
5. `Requirement Can Have Aux Con = False` requires `count(Ship Auxiliary Control)` = 0.
6. `count(Ship Life Support)` ≥ `Min Life Support`, and `count(Ship Crew Quarters)` ≥
   `Min Crew Quarters`.
7. A `Master Computer` satisfies rules 4 and 6 [VERIFY].
8. `count(Standard Ship Movement)` ≤ `Requirement Max Engines`. When `Requirement Uses Engines`
   is False, the maximum is 0. Whether a ship needs at least one engine is [VERIFY]. The
   recommendation is not to require one, since movement is then simply 0.
9. Percentage rules: Σ size of qualifying components ≥ T × pct / 100. The qualifying abilities
   are `Launch/Recover Fighters` for fighter bays, any `Colonize Planet - *` for colony modules,
   and `Cargo Storage` for cargo [VERIFY: whether bays and colony modules also count as cargo].
10. For each component with a `Restrictions` of N per vehicle, the count of that component is at
    most N. Compare by component name; whether different numerals of a family share the limit is
    [VERIFY].
11. Mount eligibility (§4.3).

### 4.3 Mounts

A mount is chosen per component entry, and one design may mix mounts. A mount is valid when all of
these hold:

- the hull tonnage is within the mount's minimum and maximum;
- the mount's `Vehicle Type` is `Any` or equals the hull class;
- the component's `Weapon Type` meets the mount's `Weapon Type Requirement`;
- the component's `Family` is on the mount's family list, if one exists;
- the mount's tech requirements are met.

A mounted component has these values:

| Value | Mounted formula |
|---|---|
| size | size × `Tonnage Percent`/100 |
| structure | structure × `Tonnage Structure Percent`/100 |
| cost | each resource × `Cost Percent`/100 |
| supply per use | × `Supply Percent`/100 |
| damage at every range | × `Damage Percent`/100 |
| shields | × `Shield Percent`/100 |
| range and to-hit | shifted by `Range Modifier` and `Weapon To Hit Modifier` |

A mounted component counts as a **different part** from the unmounted one for retrofit and
restrictions [VERIFY].

### 4.4 Cost and displayed stats

- Design cost for each resource = hull cost + Σ mounted component cost. This cost drives build
  time, maintenance, scrap value, unmothball cost and retrofit limits.
- Designer and report stats: Space Used / T, Total Cost, Movement (§6.1, with all components
  undamaged and full supply), Shields (normal + phased), Cargo Space (Σ `Cargo Storage`) and
  Supply Capacity (Σ `Supply Storage`).

---

## 5. Vehicle instance state

Each vehicle stores:

- id, owner, name, design, and location (system, sector);
- per-component damage (0..structure). A component with damage ≥ structure is destroyed;
- supply;
- movement points remaining and movement points at the start of the turn;
- an order list and a repeat flag;
- fleet id;
- cargo (population amount and a count per unit design);
- crew experience;
- status: Normal, Mothballed or Cloaked;
- minister-control flag;
- a construction queue, if the vehicle has a `Space Yard`;
- for drones, the target.

A vehicle is **destroyed** when all of its components are destroyed. The displayed damage is
Σ damage over Σ structure.

---

## 6. Movement

### 6.1 Movement points per turn

MP are recomputed at the start of each turn. Unspent MP do not carry over.

```
if status == Mothballed or hull.EnginesPerMove == 0: MP = 0
E      = Σ V1(Standard Ship Movement) over undamaged components
base   = E / hull.EnginesPerMove                      // integer division
bonus  = (E > 0) ? min over undamaged engines of V1(Movement Bonus) : 0
         // an engine without the ability counts as 0
extra  = Σ over distinct V2 of max V1(Extra Movement Generation)   // hull + undamaged comps
racial = empire "Vehicle Speed" trait value
MP     = base + bonus + extra + racial
if supply == 0 and no undamaged Quantum Reactor: MP = min(MP, 1)       // [VERIFY]: "only 1"
if !hasControl(): MP = min(MP, 1)                                      // [VERIFY]
```

- `hasControl()` is true when the vehicle has an undamaged `Master Computer`. Otherwise it
  requires all three of these:
  - if the hull requires a bridge, at least one undamaged `Ship Bridge` or
    `Ship Auxiliary Control`;
  - if the hull minimum for life support is above zero, at least one undamaged `Ship Life Support`;
  - if the hull minimum for crew quarters is above zero, at least one undamaged
    `Ship Crew Quarters`.

  The threshold may instead be "below the hull minimum" [VERIFY].
- The `min` in the bonus rule implements the rule that the family bonus applies only when every
  engine is of the bonus family. For example, 6 engines that each give +1 produce 7 MP. Mixing in
  any engine without a bonus gives +0. Whether mixing two bonus families gives the lower bonus or
  nothing is [VERIFY].
- `Emergency Energy` adds MP to the current turn only.
- Engines destroyed mid-turn: cap the remaining MP at the new maximum [VERIFY].

### 6.2 Grid, paths and warp points

- One step moves the vehicle to an adjacent sector, diagonals included [VERIFY], and costs 1 MP
  and the engines' supply (§7).
- A **warp** moves the vehicle from a warp-point sector to the linked warp point in the other
  system. It costs MP (the manual lists Warp among the orders that use movement), assumed 1 MP
  [VERIFY]. `Warp Point - Turbulence` damage applies to each object that passes through.
- **Pathfinding** for Move To only uses warp points whose destination the empire knows (it has
  travelled them). To go through an unexplored link, the player issues an explicit **Warp** order.
- **Systems To Avoid:** a per-empire set of systems. Long routes MUST avoid them when an
  avoid-free route exists. What happens when none exists is contradictory in the manual (either
  refuse the order, or route through anyway) [VERIFY]. The window also offers read-only map
  filters (Presence, Ally Claimed, Enemy Claimed).
- **Tagged minefields:** a per-empire set of sectors, added or removed by order. Automatic
  routing never enters them. An explicit order whose destination is that sector does.
- Fighters cannot use warp points, and satellites, mines, troops and platforms never move. Drones
  move toward their target automatically.
- Stellar effects applied each turn: movement toward the centre, random drift, and damage at the
  centre (§3.3).

### 6.3 Turn modes

**Turn-based.** Orders execute as soon as they are issued, spending MP. At the start of each turn,
every vehicle regains MP and first continues its existing order list.

**Simultaneous** (this project's mode):

- Players only queue orders. The host resolves all movement in a **30-day** month.
- A vehicle with M MP gets `floor(d·M/30) − floor((d−1)·M/30)` steps on day d. For M = 5 that is
  one step on each of days 6, 12, 18, 24 and 30, which matches the manual's example [VERIFY
  rounding]. Processing order within a day MUST be deterministic, for example by vehicle id.
- Combat is checked on days 5, 10, 15, 20, 25 and 30 at every sector holding mutually hostile,
  mutually visible forces. That allows up to 6 combats per sector per turn. Combat is always
  resolved automatically.
- The Cargo Transfer and Launch/Recover windows do not exist in this mode. Their effects happen
  through the Load, Drop, Launch-remote and Recover-remote orders during resolution.
- A movement log records every step for replay, per system and per ship.

### 6.4 Interruption

- An order whose destination cannot be reached is removed.
- If a vehicle is **blocked** by enemy ships or **enters combat**, its entire order list is cleared.
  Sentry has its own trigger (§8).
- A cloaked vehicle that the enemy cannot detect passes through enemy-held sectors without combat.
  Detection triggers combat as usual. A cloaked vehicle must decloak before it can attack.
- Treaties can allow passing through another empire's sectors without combat (see the diplomacy
  spec).

---

## 7. Supply

**Capacity.** Maximum supply is Σ `Supply Storage` over undamaged components. It shrinks when
storage is destroyed, and current supply is clamped to it.

**Consumption:**

| Activity | Supply spent |
|---|---|
| Each step, and each warp [VERIFY] | Σ `Supply Amount Used` over undamaged engines (components with `Standard Ship Movement`) |
| Each weapon shot | the weapon's `Supply Amount Used` × the mount's `Supply Percent` |
| Other activated components (cloak, shield regenerator, remote miner, stellar device) | their `Supply Amount Used` per activation. Whether "per activation" means per turn while active is [VERIFY]. |
| Fighters and drones in space | `Fighter Supply Usage Per Turn` (5) and `Drone Supply Usage Per Turn` (200) each turn, even when stationary. Whether movement also costs them is [VERIFY]. |

- The racial `Supply Cost` trait scales every consumption by (100 + V1)%.
- Supply never goes below 0. A move that costs more than the remaining supply is still allowed
  and leaves supply at 0 [VERIFY].

**Replenishment:**

- **Resupply depot:** a vehicle that is in or **passes through** a sector with a planet that
  carries `Supply Generation`, owned by the empire or an ally whose treaty grants depot use, is
  refilled to maximum. This costs no MP and is instant.
- `Solar Supply Generation` adds V1 × (stars in the system) each turn, capped at the maximum.
- `Emergency Resupply` adds V1 when used, capped at the maximum [VERIFY cap].
- `Quantum Reactor` makes supply effectively infinite. Treat it as always full.
- **Fleet pooling:** a fleet's supply is a shared pool. The pool maximum is the sum of member
  maximums, and every member draws from the pool, so no member reaches 0 while the pool has
  supply. The Fleet Report shows pool/max. Individual values are redistributed in proportion to
  capacity [VERIFY].
- Drones can never be resupplied. Satellites need an outside source.

**Out of supply (supply = 0):**

- 1 MP per turn;
- in combat, half shields and 1 combat MP;
- weapons that use supply cannot fire;
- a fighter or drone is destroyed; a satellite goes dormant; a mine keeps working with its
  non-supply components.

**Status icons.** "Low Supplies" shows when 0 < supply < `Supply Amount for Low Supply Warning`
(1000) [VERIFY: whether the threshold is absolute]. "No Supplies" shows at 0.

---

## 8. Orders

Every ship, base, planet and fleet has an **ordered list** of orders.

- Execution always starts at the head of the list. A completed order is removed. With **Repeat
  Orders** on, it moves to the tail instead, so the list cycles.
- Repeat with a single order that completes without effect makes the vehicle idle. This is legal.
- New orders are **appended**. Giving orders to a vehicle that already has some is a common player
  mistake, so the UI should show the queue.
- Orders that need no MP can run any number of times in one turn.
- Giving an order to a fleet gives it to all members. A shift-selected ad-hoc group gets the same
  orders per ship without forming a fleet.

"Composite" orders in the table below expand to Move To plus an action.

| Order (hotkey) | Parameters | Behaviour |
|---|---|---|
| Move To (M) | sector, in any known system | Paths and warps as needed (§6.2). Completes on arrival. |
| Move To Waypoint (Ctrl 0–9) | waypoint | Move To the waypoint's sector. |
| Set Waypoint (Alt 0–9) | sector | Empire-level. Defines a waypoint (§8.1). |
| Attack (A) | target | Turn-based: starts combat with enemies at the ship's current location. Simultaneous: pursues the target, re-pathing every step. |
| Warp (W) | warp point | Moves there and transits. Needed for links to unexplored systems. |
| Resupply (S) | — | Move To the nearest resupply depot the empire may use (own or allied). |
| Repair (R) | — | Move To the nearest own location with `Component Repair`. |
| Explore (E) | — | Move To the nearest unexplored system, skipping systems that other own ships are already heading for. |
| Colonize (C) | planet | Composite. At the current location, loads population from an own planet if possible. Then Move To the planet, then colonize. Needs a `Colonize Planet - <type>` matching the planet type and an uncolonized planet. The ship is consumed. All its cargo, including population, goes to the new colony. A colony with 0 population is legal but cannot build facilities. `Automatic Colonization Population` (0) adds free population [VERIFY]. |
| Sentry (Y) | — | Uses no MP and stays in place. When an enemy vehicle is in the same **system** at the start of an order step, the whole list is cleared. |
| Set Patrol (P) | list of points | Appends Move To for each point (the last point should be the current location) and turns Repeat on. |
| Repeat Orders (K) | toggle | See the list rules above. |
| Clear Orders (Del) | — | Empties the list. |
| View Orders (V) | — | UI only. |
| Load Cargo (L) | cargo type, sector | Composite. Loads as much of the type as fits, from an own planet or base there. Fails (removed) if there is no source. |
| Drop Cargo (D) | cargo type, sector | Composite. Drops all of the type into an own colony or base there that has room. |
| Launch Units Remotely (I) | unit type, sector | Composite. Launches as many units of the type as allowed (§12). |
| Recover Units Remotely (O) | unit type, sector | Composite. Recovers as many units as there is room for. |
| Cargo Transfer (T) | — | Instant window, turn-based games only (§11). |
| Launch/Recover Units (U) | — | Instant window, turn-based games only (§12). |
| Jettison Cargo (J) | — | Destroys the selected or all cargo. No MP. |
| Fleet Transfer (F) | — | Window (§9). |
| Change Formation\Strategy (H) | — | Fleet only. |
| Cloak (Z) / Decloak (X) | — | Needs an undamaged component with `Cloak Level`. While cloaked, many orders are unavailable (at least attacking) [VERIFY list]. |
| Sweep Mines (Ctrl M) | — | An extra sweep of the current sector (§12). |
| Add / Remove Tagged Minefield (Ctrl T / Ctrl R) | sector | Empire-level. |
| Use Component | component | One-shot use (Emergency Energy or Emergency Resupply). The component is destroyed if it has `Component Destroyed On Use`. |
| Stellar Manipulation (B) | action | Uses a manipulation component, which is destroyed after use. Preconditions per action: asteroids to create a planet, a planet to destroy one, a star to destroy or nebularise, a warp point to close, and so on. |
| Scrap / Analyze / Mothball (G) | — | §15. |
| Change Name (N) | text | — |
| Toggle Minister Control | — | Puts the vehicle under AI control, limited by the minister categories the empire enables. |
| Build Queue (Q) | — | Only if the vehicle has a `Space Yard`. |

Planet-only orders (Scrap Facilities, Abandon Planet, Use Facility, Convert Resources) belong to
other specs. The movement-log replay orders are UI only.

### 8.1 Waypoints

- A waypoint is an empire-owned, named sector reference. The hotkeys address slots 0–9, and the
  Waypoints window lists all waypoints [VERIFY maximum].
- A waypoint supports rename, delete and re-set. The window lists the ships en route to it and the
  space yards whose **automatic Move To** targets it.
- A construction queue (planet or ship) can have an automatic Move To waypoint. Every newly built
  vehicle then receives a Move To order to that waypoint.
- What deleting a waypoint does to existing orders is [VERIFY]. The recommendation is to keep them
  as Move To the stored sector.

---

## 9. Fleets

- An empire can have any number of named fleets. The name gets a default and is editable. A fleet
  is deleted when it becomes empty.
- Vehicles join a fleet through Fleet Transfer, and only when they are in the **same sector** as
  the fleet. "Add All" and "Remove All" are available. Bases cannot join
  (`Bases Can Join Fleets` = False). Whether units can join is [VERIFY].
- **Leader:** one member is the leader. The player can pick it. If the leader leaves or is
  destroyed, a new leader is chosen automatically [VERIFY rule, for example the first remaining
  member]. The leader only matters for the formation.
- **Movement cohesion:** members move together. A member with MP left MUST NOT leave a sector that
  holds a fleet-mate with 0 MP. In effect the fleet moves at its slowest member's speed. Members
  with different orders can split up, and the fleet then has no cohesion effect until they are
  together again. The Fleet Report shows fleet movement as available/total, meaning the minimum
  across members.
- **Supply:** pooled (§7).
- **Strategy:** the fleet strategy overrides each member's design default. By default the whole
  fleet forms one combat group in its formation. A strategy can list vehicle types that break
  formation.
- **Experience:** a fleet has its own experience bonus, which `Fleet Training` raises. It is lost
  when the fleet is broken up [VERIFY: whether "broken up" means disbanded or any member change].

---

## 10. Formations

- The fleet's formation takes effect in **combat** (tactical and strategic). When the leader moves,
  each member moves toward the cell at `leader + (Position_i − LeaderPosition)`.
- Members are assigned positions in fleet order: the first non-leader member gets position 1, and
  so on. A position whose `Type` is a design-type name only accepts members of that type [VERIFY].
  If there are more members than positions, the extra members have no slot [VERIFY].
- The grid is drawn with the leader facing up, toward lower y. Whether formations rotate with
  approach direction is [VERIFY].
- The formation breaks when the leader is destroyed, or when it is cleared by a combat group order.
- Fleet Transfer and Change Formation\Strategy select the formation. The Formation Report draws the
  grid.

---

## 11. Cargo

- **Capacity:** vehicle capacity is Σ `Cargo Storage` over undamaged components, including bays,
  colony modules and mine layers, which all carry some. Planet capacity comes from planet size,
  cargo facilities and the `Planet Storage Space` trait.
- **Contents and footprint:**

  | Item | Space used |
  |---|---|
  | Population | `Population Mass` (5) kT per 1M [VERIFY] |
  | A unit | its hull `Tonnage` |

  Only ships, bases, drones and planets hold cargo. Whether a bay type limits which units it holds
  (for example fighters only in fighter bays) is [VERIFY]. The recommendation is one generic pool.
- **Damage:** if capacity drops below the amount used, cargo is destroyed until it fits [VERIFY
  which items go first]. Cargo in transit is inert: it does not fight or grow, and troops cannot
  defend.
- **Transfer:** only between an own vehicle or planet and another own (or allied [VERIFY]) holder
  in the same sector. It costs nothing and uses no MP. It moves 1, 5, 10 or all units per click and
  only moves what fits. Population on a colony can go down to 1M but never to 0. Emptying a colony
  needs the Abandon Planet order.
- **Captured or gifted ships** keep their cargo.

---

## 12. Units: launch, recovery and life

Launch and recover abilities give a rate per combat turn (V1) and per game turn (V2). A vehicle's
rate is the sum over its undamaged launchers.

The Launch/Recover window says any number can be moved without cost. That contradicts the
per-game-turn rate [VERIFY]. The recommendation is to enforce V2 per turn for ship launches and
leave planet launches unlimited.

| Unit | From ship | From planet | Recover | Notes |
|---|---|---|---|---|
| Fighter | `Launch/Recover Fighters` | yes | into a ship with the ability, or an own planet, in the same sector | Moves like a ship inside the system and cannot warp. Consumes 5 supply per turn in space and can refuel at depots. Destroyed at 0 supply. |
| Satellite | `Launch/Recover Satellites` | yes | same | Stationary. Fires on enemies in its sector. Dormant at 0 supply. |
| Mine | `Lay Mines` | yes | never (self-destruct only) | Hidden: hull cloak level 5 in all sight types. Triggers when a hostile vehicle enters the sector. |
| Drone | `Launch Drones` | yes | never | Needs a ship or planet target at launch. Moves toward it automatically and rams. The target is fixed unless lost, then it can be re-targeted. Consumes 200 supply per turn in space. |
| Troop | – | – | – | Dropped on an enemy planet during combat (see the combat spec). |
| Weapon Platform | – | – | – | Transferred to a planet as cargo, or built there. |

- **Mines:** "hostile" means any empire with which there is no treaty, or war. Mines hit cloaked
  vehicles as well. They hit fighters and drones when `Fighters/Drones Can Be Hit By Mines` is
  true. A mine explosion damages the mine as well. The owner always sees its own mines.
- **Sweeping:** a vehicle with `Mine Sweeping` removes up to V1 enemy mines when it enters a
  sector, before detonation [VERIFY order], and again on each Sweep Mines order.
- **In combat:** units launch at up to V1 per combat turn, in groups of up to 20 per combat piece.
  After combat, survivors are recovered automatically into own carriers in the sector that have
  space. Units that do not fit stay in space.
- **Self-destruct and fire-on** work on unit groups as they do on ships (§15).

---

## 13. Repair

- **Where:** every sector that holds an own planet or vehicle with `Component Repair`, which
  includes space yard facilities and components and repair bays. Allied sources are [VERIFY].
  Repair is automatic, has no resource cost [VERIFY], and happens during turn processing.
- **Throughput:** Σ V1 of the repair sources in the sector, in components per turn, scaled by the
  empire's Repair Aptitude characteristic (50–150 %). Whether each source serves only its own
  vehicle or all vehicles in the sector share one pool is [VERIFY]. The recommendation is a sector
  pool shared by all own damaged vehicles, in ascending vehicle id order.
- **Selection:** damaged components are sorted by the position of their `General Group` in the
  empire's **repair priority list**, which the player edits starting from `RepairPriorities.txt`.
  Groups missing from the list come last. Ties are broken by position on the design. Each repair
  restores one component to full structure.
- The status icons "Under Repair" and "Can Repair" follow from this.
- Components used up by `Component Destroyed On Use`, stellar manipulation, or boarding parties
  after a capture attempt are destroyed components. Whether a yard can repair them is [VERIFY];
  the recommendation is that it can.

---

## 14. Retrofit

Preconditions:

- the ship is at a sector with an own space yard (planet facility or `Space Yard` component);
- the target design uses the **same hull** as the ship. When several ships are selected, they must
  all share one hull;
- if `No Retrofit Adding Of Spaceyards`, the target may not have more `Space Yard` components than
  the current design. The same applies to `Colonize Planet - *` components under
  `No Retrofit Adding Of Colony Module`;
- |cost(new) − cost(old)| ≤ `Retrofit Max Percent Difference in Cost` (50) % of cost(old)
  [VERIFY: which direction, and which resource total].

Cost, computed per resource:

```
Added   = multiset(new components) − multiset(old components)   // component + mount identity
Removed = multiset(old) − multiset(new)
cost    = Σ cost(Added)   × Retrofit Cost Percent For Comps (120) / 100
        + Σ cost(Removed) × Retrofit Cost Percent For Comp Removal (30) / 100
```

Effect:

- The retrofit is immediate [VERIFY timing in simultaneous mode] and the ship's class becomes the
  new design.
- Components in both designs keep their current damage. Every added component starts
  **destroyed** and must be repaired (§13).
- Supply and cargo are clamped to the new capacities.

---

## 15. Scrap window actions

Each action applies to the selected own vehicles in one sector.

| Action | Requires | Effect |
|---|---|---|
| Scrap | a space yard in the sector | Destroys the vehicles. Returns design cost × P %, per resource. P is `Scrap Ship Percent Returned` (30), or `Scrap Unit Percent Returned` for units, or the best `Resource Reclamation` in the sector if that is higher. The fate of the cargo on board is [VERIFY]. |
| Analyze | a space yard | Destroys the vehicles and grants research from technology the empire lacks (see the research spec). The UI gives a qualitative "research potential". |
| Mothball | a space yard, status Normal | Status becomes Mothballed: no abilities, no movement, no shields, no weapons, no maintenance. |
| Unmothball | a space yard, status Mothballed | Pays design cost × `UnMothball Ship Percent Cost` (20) % and returns the vehicle to Normal. |
| Retrofit | a space yard | §14. |
| Self-destruct | an undamaged `Self-Destruct` component | The vehicle is destroyed. No yard is needed. |
| Fire On | another own vehicle in the sector with undamaged weapons | The selected vehicles are destroyed. The last armed vehicle cannot be destroyed this way, so at least one always remains. |

---

## 16. Maintenance (vehicle side)

- Per vehicle per turn: design cost × base % × hull `Modified Maintenance Cost` factor × system
  `Reduced Maintenance Cost - System` factor × the Maintenance Aptitude characteristic.
  - The base % is `Empire Starting Percent Maint Cost` (25). The manual says both 25 % and 30 %,
    and "Starting" suggests the value can change during a game [VERIFY].
  - The hull factor is (100 + V1)%. The system factor is (100 − V1)%.
- Mothballed vehicles pay nothing. Whether units pay is [VERIFY].
- Maintenance is paid before any other spending. Each `Maintenance Cost Amt Per Dead` (20000) of
  unpaid maintenance destroys one random vehicle [VERIFY rounding and resource basis].

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
   and immobile hulls use 0.
2. **Mixed engine bonus:** does mixing +3 and +2 engines give +2 or +0? Do destroyed engines count
   toward the "all engines" test?
3. **Zero supply or lost control:** is MP exactly 1 even when the ship has no working engines?
   Does "no bridge/LS/CQ" mean zero undamaged components, or fewer than the hull minimum? Does
   Auxiliary Control stand in for a lost bridge?
4. **Master Computer:** does it lift the design requirements for bridge, life support and crew
   quarters, or only the in-flight penalty?
5. **Warping:** does a warp cost 1 MP and engine supply, or is it free on arrival at the warp
   point? Is diagonal movement allowed at 1 MP?
6. **Supply per step:** is engine supply charged per sector moved or once per turn of movement? Do
   fighters and drones pay for movement on top of their per-turn cost? Is a cloak's
   `Supply Amount Used` charged per turn while cloaked?
7. **Fleet supply pooling:** how is the pool redistributed? Does pooling work across sectors?
8. **Simultaneous day schedule:** the exact rounding for MP that do not divide 30, and processing
   order within a day. Does a combat interruption clear orders in simultaneous mode?
9. **Systems To Avoid** when no avoid-free route exists: refuse the order or go through?
10. **Launch rate:** is the per-game-turn launch rate (V2) enforced in the Launch/Recover window?
    Do planets need any ability to launch? Can fighters sit in plain cargo bays and satellites in
    fighter bays?
11. **Population cargo footprint:** is `Population Mass` = kT per 1M? Which cargo items are lost
    first when cargo space is destroyed?
12. **Percentage design rules:** exactly which components count toward `Pct Cargo`,
    `Pct Fighter Bays` and `Pct Colony Mods`? Is there a one-bridge maximum? Is at least one engine
    mandatory on `Uses Engines` hulls?
13. **Hull plus component to-hit:** do the bonuses add, or does only the best one count? Do hull
    `Defense Minus` and component `Defense Plus` net out?
14. **Repair:** is throughput pooled per sector or per source? Are allied yards usable? Is repair
    free? Is it applied before or after movement? Can consumed one-shot components be repaired?
15. **Retrofit:** which direction and which resource total does the 50 % cost-difference cap use?
    Is the cost charged at once? Does the retrofit take effect instantly in simultaneous games?
    Does it need the ship to be undamaged or unmothballed?
16. **Maintenance:** 25 % or 30 %? Does the percentage change over the game? Do units in cargo or
    space pay? How is the vehicle lost to unpaid maintenance chosen?
17. **Cloak semantics:** a cloaking device with level 2 claims to block level-1 scans, while a mine
    with level 5 claims to block level-5 scans. Confirm "sensor level ≥ obscuration level detects".
18. **Emissive Armor V1:** is it a per-hit damage threshold (as the component text says) or extra
    armor points (as the catalog says)?
19. **Scrapping:** does it return cargo value? Is scrap value reduced for damaged ships? Do
    recyclers of allies apply?
20. **Obsolete designs:** the auto-deletion interval. Is design-name uniqueness per empire or
    global?
21. **Waypoints:** maximum count, and the effect of deleting a waypoint that pending orders or yard
    auto-moves reference.
22. **Fleet membership:** can units (fighters, drones) join fleets? Which member becomes leader
    automatically? Is fleet experience lost on any membership change?
23. **Formations:** do slot `Type` restrictions and formation orientation matter in practice? What
    happens with more members than positions?
24. **Stellar-manipulation "Stop ..." facilities:** do they cover the system or the whole empire?
    What are the units of `Open Warp Point Distance`?

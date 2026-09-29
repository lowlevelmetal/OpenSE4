# Spec 04: Combat (space, planetary, ground, capture, tooling)

Status: reference spec for engine work. It was written clean-room from the SE4 Deluxe
manual (HTML and PDF), the self-documenting data files, and the version history that
ships with the game (`History.txt`, `DataFileHistory.txt`). Everything is paraphrased.
The version history records rule changes made after the manual was written, so **where
it contradicts the manual, the history wins** and we cite it as "(history 1.xx)". Where
all sources are silent we give a recommended behaviour marked **(inferred)**. Section 19
lists what must be checked in the running game.

Conventions:

- Every tunable below belongs in data (a `combat` table in `rules.toml`) with the SE4
  default shown. Settings.txt names are given verbatim so a loader can map them.
- Percentages are integer math: `x * pct / 100`, rounding toward zero **(inferred)**.
- All randomness uses `GameState::rng`. A "roll" is a uniform integer in 1..100.
- "Intact" means a component that has not been destroyed. Destroyed components provide no
  abilities, no weapons and no movement.

---

## 1. Vocabulary

| Term | Meaning |
|---|---|
| Battle | One combat at one sector (one grid location of one system map). |
| Combat map | A square grid on which the battle is fought. Size is not documented (see §19). |
| Piece | One occupant of the combat map: a ship, base, planet, satellite group, fighter group, drone or seeker. |
| Unit group | One piece that holds several units of one kind (fighters or satellites). It moves and is targeted as one piece. |
| Combat turn | One round of the battle, numbered 1..`Number Of Space Combat Turns` (30). |
| Phase | The part of a combat turn in which one empire moves and fires. |
| Combat group | A leader piece and member pieces that follow it in a formation. |
| Distance | Squares between two pieces. Use Chebyshev distance (a diagonal step costs 1), the same metric as the system map **(inferred)**. For a planet, measure to its nearest square **(inferred)**. |

## 2. When combat happens

**Turn-based games.**

- Combat starts when a vehicle enters a sector that holds a visible piece of an empire it
  is hostile to. The Attack order starts combat against enemies already in the ship's
  own sector. A Sentry order waits until an enemy enters the system, then clears itself.
- Hostility follows treaties. War and Non-Intercourse fight. Non-Aggression and anything
  better never fight and may share sectors. For the treaty level "None" (met, no treaty),
  use the mine rule: mines treat "no relationship or worse" as enemies, so None fights
  **(inferred)**.
- Undetected cloaked pieces neither trigger combat nor can be attacked. If the other side
  can see them, combat starts as normal. Once a battle starts, every piece in it is
  decloaked until it ends (history 1.28).
- Satellites and colonies are participants, so moving into a sector with only an
  enemy's satellites or planet also triggers a battle **(inferred from the unit rules)**.
- Mines are not combat pieces. They act when a vehicle enters their sector (§10.6).
- A ship that fights loses its queued orders.

**Simultaneous games.** Tactical combat is never offered, and the computer resolves every
battle. The manual's older rule (combat only on every 5th day) was replaced (history 1.15,
1.42). Movement now triggers combat, at most once per sector per movement phase, and only
in sectors where a vehicle executed orders while hostile pieces were present. Defenders
move first (history 1.58). Results arrive as log entries. The Settings flag `Simultaneous
Games Show Strategic Combat` decides whether the strategic view is shown.

**Game option "No Tactical Combat".** It removes the tactical choice, so every battle is
strategic.

**More than two empires.** Every empire with pieces in the sector takes part. Treaties are
enforced during the battle: no piece may fire on an empire it is not hostile to. An
empire whose two allies fight each other sits the battle out. When several enemies are
present, the AI engages the nearer one first.

## 3. Battle setup

1. Each human participant chooses **Tactical** or **Strategic** resolution. The rules are
   identical, and only control differs.
2. Build the pieces: every ship, base and colonized planet, every satellite and fighter
   group already in space, and every drone. A planet occupies 2×2 squares (history
   1.60). Stars, asteroids, storms, nebula effects and warp points in the sector appear
   on the map. Whether they block movement is unknown (§19). A system type may set a
   centre picture for the map background (SystemTypes `Non-Tiled Center Pic`).
3. Place the pieces. Each empire starts in its own region of the map. Each fleet starts
   in its formation as one combat group, and large groups start spread out (history
   1.43, 1.44, 1.46). In the simulator, a side that owns a base or planet starts at the
   map centre (history 1.87). The exact geometry is unknown (§19).
4. Form combat groups. Each fleet becomes one combat group: the fleet leader leads it
   and the fleet's formation applies.
5. Set every piece's shields to their maximum (§9.2). Shields always start a battle full.
6. Set every weapon's reload counter to 0, meaning ready **(inferred)**. The only
   exception is a ship captured earlier in this battle (§12).
7. Assign each piece a strategy. While a ship is in its fleet's combat group, it uses the
   fleet strategy. Once it leaves the group (broken formation, group dissolved), it
   uses its design's default strategy (history 1.84, the latest ruling).

## 4. Combat turn sequence

The battle ends when the turn limit is reached (Settings `Number Of Space Combat Turns`
= 30), or when no two hostile sides both still have pieces on the map. A side with no
weapons left does not end the battle **(inferred)**.

Each combat turn runs as follows. Steps 1 and 5 are an **(inferred)** canonical order.

1. **Turn start.** Restore movement points. Lower each weapon's reload counter by 1
   (never below 0). Apply shield regeneration and armor regeneration (§9.3). Reset
   per-turn launch allowances and target budgets.
2. **Empire phases.** Empires act one after another. The order is random, but defenders
   always come before attackers (history 1.47). Who counts as the defender is not
   documented. Recommended: the owner of a planet in the sector, otherwise the empire
   that was in the sector first **(inferred)**.
3. **Inside a phase.** Each of the empire's pieces may move and fire in any order and
   interleaving. A piece spends up to its movement points, and each ready weapon fires at
   most once. Human players act by hand. AI players, and humans with **Auto** on, act
   through their strategies. **End Turn** passes to the next empire. **Resolve Combat**
   is the same as Auto for the rest of the battle, without waiting for End Turn.
4. **Reactions.** Point-defense may fire at any moment, including during other
   empires' phases (§10.2).
5. **Seekers.** Each seeker in flight moves up to its speed toward its target. The
   timing is not documented (§19). Recommended: the seekers of an empire move at the
   end of that empire's phase **(inferred)**.
6. **Turn end.** Advance the turn counter and check the end conditions.

## 5. Movement in combat

- **Movement points (MP)** are the same as the vehicle's system-map movement: total
  `Standard Ship Movement` from intact engines, divided by the hull's `Engines Per Move`,
  plus the engine `Movement Bonus` when every engine has the same bonus type, plus
  racial bonuses (see the ships spec). Add `Combat Movement` (afterburners on fighters,
  best single component only) in combat.
- **MP overrides.** A vehicle gets only 1 MP if it has zero supplies, or if it has lost
  its Bridge (with no intact Auxiliary Control), its Life Support or its Crew Quarters,
  unless a Master Computer covers them. Bases, satellites and planets have 0 MP. A
  fighter group moves at its slowest member's speed (history 1.24). Fighters with zero
  supplies get 1 MP (history 1.65).
- Each step to one of the 8 neighbouring squares costs 1 MP **(inferred)**. Unused MP is
  lost at turn end.
- **Occupancy.** A square holds one piece. Seekers are the exception and may overlap
  anything. Drones may also end in their target's square: history 1.59 fixed "drones not
  attacking when in the same square as the target".
- **Launched units** (fighters, drones) get their full MP in the turn they launch (history
  1.55, 1.71).
- **Combat groups.** When the leader moves, each member moves toward its formation slot
  relative to the new leader position, spending its own MP **(inferred)**. The group
  dissolves when the leader is destroyed, removed from the group, or blocked in its
  movement (history 1.03). In tactical mode, groups are set with Set Group Leader, Set
  Group Member, Clear Group Assignment and Clear All Group Assignments. Hotkeys give 10
  groups (0–9). Members of an AI side leave formation according to the strategy's
  `Break Formation` flags (§16) or when the leader is surrounded (history 1.21).
- **Forced movement** from push, pull and teleport weapons is described in §9.5.

## 6. Firing

A weapon may fire at a target only if all of the following hold:

1. The component is intact, its Weapon Type is `Direct Fire` or `Seeking` (point-defense
   fires on its own, §10.2), and its reload counter is 0.
2. The owner has supplies. A vehicle with zero supplies cannot fire at all (history 1.65).
   Each firing uses the component's `Supply Amount Used`, scaled by the mount's
   `Supply Percent`. A Quantum Reactor means unlimited supply. The advanced trait
   "Advanced Power Conservation" cuts use by 25%.
3. The target is hostile and visible, and its category is in the weapon's target set
   (§18.1).
4. The weapon's effective damage at the current distance is greater than 0 (§8). A seeker
   may be launched only if the target is within its maximum travel distance
   **(inferred)**.
5. The piece still has target budget. Each piece may engage at most `T` distinct targets
   per combat turn:
   - A ship or base uses its best single `Multiplex Tracking` value, or 1 if it has none.
     Multiplex components do not stack.
   - A planet may engage up to 10 targets.
   - A satellite group may engage as many targets as it has satellites, but no more
     than its multiplex level (history 1.04).
   - A fighter group always engages exactly one target (history 1.04).

In the tactical window, the player toggles which of the ready weapons are selected.
Clicking an enemy fires every selected weapon that can reach it. Weapons fire in the order
they appear in the design (history 1.82 describes a bug that only makes sense with design
order). After firing, set the reload counter to `Weapon Reload Rate`: 1 fires every turn,
2 every other turn, and 30 once per battle. Push, pull and teleport weapons spread over as
many different enemies as possible rather than piling onto one (history 1.73).

## 7. Chance to hit

Direct-fire and point-defense shots roll to hit. Seekers, drones, rams and warheads never
roll. A seeker hits when it reaches its target, unless it is shot down first.

```
chance = clamp( Base − PerSquare × d + Offense − Defense − Interference , 1, 99 )
hit if roll(1..100) ≤ chance
```

- `Base` = Settings `Combat Base To Hit Value` (100). `PerSquare` = Settings `Combat To
  Hit Modifier Per Square Distance` (10). `d` is the distance.
- Clamp after every modifier, including the weapon's own modifier (history 1.56).
- Modifiers from different sources add together rather than taking the largest (history
  1.33). Within one component ability, only the best component counts: each such
  component says "only 1 per ship effective".
- **Weapons Always Hit** (Religious Talisman): the vehicle's direct-fire weapons skip the
  roll and always hit.

**Offense** is the sum of:

| Source | Notes |
|---|---|
| Weapon `Weapon Modifier` | Per weapon. For example, point-defense cannons are +70 and some heavy beams +10 to +30. |
| Mount `Weapon To Hit Modifier` | 0 for ship mounts and the satellite mount. Base and weapon-platform mounts give +40 to +80. |
| Best intact `Combat To Hit Offense Plus` component | Combat sensors (+25/+45/+65). Fighter sensors are separate small parts. |
| Hull `Combat To Hit Offense Plus/Minus` | Fighter hulls +50. |
| Crew experience and fleet experience | Both apply to offense and defense. Fleet experience never applies to unit groups (history 1.33). |
| Culture `Space Combat`, Aggressiveness characteristic | Race-level. How they convert to points is open (§19). Unit groups get racial modifiers (history 1.53). |
| `Combat Modifier - System` | From the empire's facilities in this system. Only one facility per system counts, so use the best. |
| Settings `Planet Combat Offense Modifier` (+30) | Only when a planet fires. |

**Defense** is the sum of:

| Source | Notes |
|---|---|
| Best intact `Combat To Hit Defense Plus` component | ECM (+20/+40/+60), stealth and scattering armor. Whether they stack with each other is open (§19). |
| Hull `Combat To Hit Defense Plus/Minus` | Small warships +40 down to +10. Large bases −20 to −60. Fighters +60 to +80. Drones +50. |
| Experience, culture, Defensiveness characteristic, `Combat Modifier - System` | As for offense **(inferred for the system bonus)**. |
| Settings `Seeker Combat Defense Modifier` (+40) | When the target is a seeker. |
| Settings `Planet Combat Defense Modifier` (−200) | When the target is a planet. In practice planets are hit 99% of the time. |

**Interference** is the `Sensor Interference` value of a sector or system ability at the
battle location.

Worked example: a destroyer (hull +20 defense) with ECM I (+20) at 5 squares, fired on by
a ship with Combat Sensors I (+25) and no other modifiers: 100 − 50 + 25 − 40 = 35%.

## 8. Damage at range, mounts, damage modifiers

- **Encoding.** `Weapon Damage At Rng` holds 20 space-separated integers. Entry `k`
  (1-based) is the damage when the target is `k` squares away, so entry 1 means adjacent.
  An entry of 0 means the weapon cannot reach at that distance. Tables need not be
  monotonic: some stay flat and some fall off. Maximum range is the last non-zero entry.
  Examples: a basic cannon lists "20 20 0 …", which is 20 damage at 1–2 squares. A
  falloff missile lists 70 down to 45 over 1–6 squares.
- **Seekers** index the table by the squares the seeker has travelled since launch, not
  by the launcher's distance at firing.
- **Warheads** use entry 1 as their detonation damage, and the other entries are 0.
- **Special damage types** reinterpret the numbers (§9.5).
- **Mounts** (CompEnhancement.txt, §18.2). `Damage Percent` scales every entry.
  `Range Modifier m` shifts the table outward: `eff(r) = table[max(1, r − m)]` for
  `r ≤ 20 + m`. The data text says that at range `m` the weapon does its "range 0"
  damage. We read that as clamped to entry 1 (§19).
- **Damage Modifier - System** (for example, shrines): add that percentage to damage dealt
  by the empire's pieces in the system **(inferred application)**.
- **Cap.** No single weapon deals more than 50000 per shot (history 1.14).
- **Unit groups** fire one combined shot per distinct weapon type. Its damage is the sum
  over all members carrying that weapon. Whether each member rolls separately or the
  group rolls once is open (§19). Emissive armor is still tested per member weapon
  (history 1.76).

## 9. Damage application

### 9.1 Pipeline for a Normal hit on a ship, base or unit

1. **Shields** absorb first, taking `min(shields, damage)`.
2. **Armor.** The remainder goes to intact components with the `Armor` ability, which are
   damaged before anything else.
3. **Internals.** Once every armor component is destroyed, the remainder goes to the
   other intact components.

Components keep accumulated damage. A component is destroyed when its damage reaches
its `Tonnage Structure` (scaled by the mount's `Tonnage Structure Percent`). Damage
beyond that carries over to the next component in the same layer, then to the next
layer. A partly damaged component works at full effect until it is destroyed. Within a
layer, pick the next component at random from the intact ones. Whether the pick is
uniform or weighted by size is open (§19). A ship's hit points are the sum of all its
components' structure. The ship is destroyed when every component is destroyed, which
means total damage has reached total structure (history 1.33 fixed ships that survived
with nothing left).

Stock SE4 is **not leaky**. Nothing passes the shields until they reach 0, and nothing
reaches internals while any armor is intact. "Leaky" shields and armor exist only in
community mods. The damage types in §9.5 are the only built-in bypasses.

### 9.2 Shields

- **Maximum at battle start** = total `Shield Generation` of intact generators, plus total
  `Phased Shield Generation`, each scaled by the mount's `Shield Percent` (default 100).
  Add `Shield Modifier - System` from the empire's facilities in the system, and subtract
  `Shield Disruption` from the sector or system (storms) **(inferred: subtracted from the
  starting value)**.
- **Zero supplies** means zero shields (history 1.65). This replaces the manual's "half".
- **Two kinds.** Phased shields stop normal and phased damage. Normal shields do not stop
  damage of type `Skips Normal Shields`. Keep two pools. Recommended: normal damage
  drains normal shields first, then phased; phased damage drains only phased shields
  **(inferred)**.
- **No natural recharge in battle.** Only `Shield Regeneration` (+V per combat turn,
  capped at the maximum, summed over regenerators **(inferred)**) and crystalline armor
  add shields.
- When a generator is destroyed, the maximum drops. Recommended: also cap current shields
  at the new maximum, since shield-generator-only weapons would otherwise do nothing in
  that battle **(inferred)**.
- Shields refill at the start of the next battle.

### 9.3 Armor specials

- **Emissive** (`Emissive Armor` V). A hit whose damage is V or less does nothing to the
  armor or hull. A larger hit does full damage **(inferred reading of the description)**.
  Several emissive parts use the largest V **(inferred)**.
- **Organic** (`Armor Regeneration` V) restores V structure per combat turn. It applies
  only after damage has been taken, never before the first hit (history 1.80). All
  regenerating armor is fully restored when the battle ends (history 1.79).
- **Crystalline** (`Shield Generation From Damage` V). From each hit that reaches the
  hull, up to V points become shield points instead of hull damage **(inferred: capped at
  the maximum shields)**.
- **Stealth and scattering armor** add a defense bonus (non-stacking, §7), plus cloaking
  or scanner-jamming effects covered in the sight spec.

### 9.4 Destroyed and crippled

- **Destroyed:** every component destroyed; a self-destruct; the loser of a ram; the
  self-destruct triggered by boarding (§12).
- **Crippled** (the vehicle survives but is impaired): no command (Bridge lost with no
  Auxiliary Control, or Life Support or Crew Quarters lost, with no Master Computer)
  gives 1 MP. Lost engines reduce MP. Lost weapons cannot fire. Lost shield generators
  cut shields. Lost cargo components destroy the cargo they held, including troops and
  stored fighters. Which items are lost when capacity only partly drops is open.
- Members of a unit group are tracked one by one. The group dies with its last member
  **(inferred)**.

### 9.5 Damage types (`Weapon Damage Type`)

| Type | Effect |
|---|---|
| `Normal` | §9.1. |
| `Shields Only` | Drains shields only. Harmless once shields are 0. |
| `Quad/Double/Half/Quarter Damage To Shields` | Against shields, damage counts ×4, ×2, ×½ or ×¼. Leftover raw damage continues as Normal. Shields drained = `min(S, dmg×f)`, raw damage used = drained ÷ f, rounded up **(inferred)**. |
| `Skips Normal Shields` | Phased weapons. Ignores normal shields, stopped by phased shields, then Normal. |
| `Skips All Shields` | Ignores both shield pools, then armor, then internals. |
| `Skips Armor` | Shields first, then only non-armor components. |
| `Skips Shields And Armor` | Straight to non-armor components. |
| `Only Engines` | Shields still absorb (history 1.70 overrides the manual). The remainder hits only engine components and ignores armor. |
| `Only Weapons` | Hits only weapon components and ignores shields and armor. Harmless once no weapons are left. |
| `Only Shield Generators`, `Only Master Computers` | Same pattern, restricted to those components. |
| `Only Boarding Parties`, `Only Security Stations`, `Only Planet Destroyers` | Same pattern. Declared in the data but unused by stock components **(inferred semantics)**. |
| `Increase Reload Time` | Adds the table value, in turns, to every weapon's reload counter on the target. No effect on a vehicle with a Master Computer. |
| `Disrupt Reload Time` | Same, but also works against Master Computers (history 1.13). |
| `Crew Conversion` | Chance that the target changes owner to the attacker. The table value is the percent chance **(inferred)**. Works on ships only (history 1.80). Always fails if the target carries any Master Computer, destroyed or not (history 1.81). On failure nothing happens. |
| `Pushes Target` / `Pulls Target` | Moves the target the table value in squares directly away from or toward the firer. No damage **(inferred: stops at blocked squares or the map edge)**. |
| `Random Target Movement` | Moves the target to a random free square of the map. No damage. The meaning of the table value is open (§19). |
| `Plague Level 1..5` | Planets only. Gives the planet a plague of that level. No damage. |
| `Only Planet Population` | Kills population (§11) and nothing else. |
| `Only Planet Conditions` | Worsens planet conditions. The rate per point is open. |
| `Only Resupply Depots`, `Only Spaceports` | Destroys that facility on the target planet. |

Drones apply special types too (history 1.86). A mine does not detonate against a target
its damage type cannot affect (history 1.70).

## 10. Special weapons and units

### 10.1 Seekers

- Firing a `Seeking` weapon uses supply and reload as usual, and puts a seeker piece next
  to the launcher, locked on one target.
- The seeker moves up to `Weapon Seeker Speed` squares per combat turn toward its target.
  It can pass through and share any square.
- **Impact.** On reaching the target, it deals `table[squares travelled]` with the
  weapon's damage type. No roll.
- **Expiry.** The seeker is removed when the next step would take it past its last
  non-zero entry, when its target dies or leaves (history 1.18), or when its target
  becomes the seeker owner's property, for example a captured planet (history 1.04).
- **Durability.** A seeker has `Weapon Seeker Dmg Res` hit points and gets +40 defense
  (§7). In stock data, only point-defense weapons list seekers as targets.
- Kills by a seeker credit experience to the ship that launched it (history 1.87).
- Example: a speed-5 missile with 60 damage out to 8 squares hits targets up to 8 squares
  away, arriving in at most two turns.
- **AI allocation.** Count the damage of seekers already in flight against a target. Once
  that reaches a set fraction of the target's remaining hit points, aim new seekers at
  the next target (history 1.82, 1.86).

### 10.2 Point-defense

- A `Point-Defense` weapon type, typically aimed at fighters, satellites, seekers and
  drones, with a large weapon modifier.
- It is never ordered to fire. It fires by itself whenever a valid hostile target comes
  within range, whether because the target moved (including moving drones, history 1.75)
  or because the point-defense carrier moved. Firing main weapons that turn does not stop
  it (history 1.17).
- It rolls to hit like direct fire. Recommended: each point-defense weapon fires once per
  reload cycle and is not limited by the multiplex budget **(inferred)**.

### 10.3 Ramming and warheads

- **Ram Ship order:** the rammer must have MP left, and the target must be adjacent.
  Strategies with Ram movement (Kamikaze, Drone Attack) do this automatically.
- **Damage to the target** = the rammer's remaining structure × Settings `Ram Ship Target
  Modifier Percent` (100), plus the entry-1 damage of every intact warhead whose target
  set includes the target (history 1.58).
- **Damage to the rammer** = the target's remaining structure × Settings `Ram Ship Source
  Modifier Percent` (60) **(inferred mapping of the two settings)**.
- Both are applied as Normal hits through §9. Warheads are used up **(inferred)**.
  Usually the smaller vessel dies. Kills by ramming give experience and count in design
  statistics (history 1.22).
- Warheads are also how mines and kamikaze fighters do damage.

### 10.4 Fighters

- **Launch.** Carriers and planets launch from cargo. Each intact bay adds its
  `Launch/Recover Fighters` Value1 to the launches allowed per combat turn (Value2 is
  the limit per game turn outside combat). The per-player unit cap does not apply in
  combat (history 1.46).
- **Grouping.** Launched fighters form groups. The size comes from the strategy's
  `Fighters Launch Group Amount` (stock 10), or the tactical "Launch Fighters in Groups"
  choice. Settings `Combat Fighter Group Amount` (20) is probably the most fighters one
  group can hold (§19).
- A group engages one target per turn. Fighter hulls give +50 offense and +60 to +80
  defense.
- Supplies are per fighter and never pooled with the fleet (history 1.68). With zero
  supplies a fighter gets 1 MP, no shields and no fire.
- **After combat** fighters land automatically on any friendly carrier or planet with
  room. Those without room stay in space.

### 10.5 Satellites and weapon platforms

- Satellites are stationary pieces in groups (Settings `Combat Satellite Group Amount`
  20). Target budget is covered in §6. They are launched with `Launch/Recover Satellites`
  (Value1 per combat turn). A player may have at most `Maximum Satellites Per Player Per
  Sector` (100) in one sector.
- Weapon platforms are never pieces. They are the planet's guns (§11). Their mounts add
  range and to-hit.

### 10.6 Mines

- Mines are invisible and never appear on the combat map (history 1.04). When a hostile
  vehicle enters the sector, the mines detonate against it, cloaked vehicles included.
- Each mine strikes with its warhead and is used up. Damage builds up on the victim across
  several mines (history 1.70, 1.78). History 1.77 fixed mine damage piling up without
  limit, so cap the total at the victim's remaining hit points **(inferred)**. No mine strikes a target its damage type or target set cannot
  affect.
- Fighters and drones are hit only if Settings `Fighters Can Be Hit By Mines` and `Drones
  Can Be Hit By Mines` allow it.
- On entry, intact sweepers first remove up to their total `Mine Sweeping` in mines.
- A player may have at most `Maximum Mines Per Player Per Sector` (100) in one sector.
  The role of `Combat Mine Group Amount` (20) is open.

### 10.7 Drones

- Drones are launched by `Launch Drones` components (Value1 per combat turn) and have full
  MP immediately.
- A drone picks its own targets among ships, planets and satellites, never fighters,
  seekers, mines or other drones (history 1.53, 1.65). If its target is absent or gone,
  it picks temporary targets until the battle ends (history 1.58).
- A drone attacks by ramming (§10.3). Only warheads matching the target's category do
  damage.
- Drone hulls give +50 defense. A strategy option sets how many drones go after one
  target (history 1.81). The name of that field in the data is unknown.

## 11. Planets in combat

- A colony owned by a participant is a 2×2 piece.
- **Offense.** Its weapons are those of the weapon platforms in its cargo, with their
  mounts. It engages up to 10 targets per turn, gets +30 offense, and measures range
  from the planet (history 1.72). It can launch fighters, satellites and drones from
  cargo. The AI launches enough seekers to be sure of a kill (history 1.82).
- **Defense.** It gets −200 defense. Its shields are the total `Planet - Shield
  Generation` of its facilities. Whether shield parts on its platforms add to this is
  open.
- **Damage order for a Normal hit:**
  1. Shields.
  2. Weapon platforms in cargo, until all are destroyed (history 1.84).
  3. Other units in cargo, chosen at random.
  4. Population, at 1M killed per Settings `Damage Points To Kill One Population` (10).
  As population falls, facilities may be destroyed. The rule is open (§19). The planet's
  health bar counts its units plus its population (history 1.68).
- A planet whose population reaches 0 loses its colony **(inferred)**.
- If a friendly transport carrying troops is present with an explicit capture order or
  strategy, the AI holds fire on that planet (history 1.43) **(inferred general rule)**.
- **Drop Troops.** A ship carrying troops drops them onto an **adjacent** enemy planet,
  which starts ground combat (§13). An AI ship with a Drop Troops or Capture Planet
  strategy heads for the planet it was ordered to take, otherwise for the most populous
  enemy planet (history 1.59).

## 12. Boarding, capture, conversion, self-destruct

- **Preconditions** for Capture Ship: the attacker has at least one intact component with
  `Boarding Attack`, the target is adjacent (history 1.73), and the target's shields are 0.
- **Strength.** Offense is the total `Boarding Attack` of the attacker's intact boarding
  parties. Defense is the total `Boarding Defense` of the target's security stations plus
  the total `Boarding Attack` of the target's own boarding parties (their description
  says they defend as well as attack).
- **Resolution.** The capture succeeds if offense exceeds defense. Whether there is also a
  roll is open. The descriptions quote marine and turret counts at 4× the ability
  values, which suggests a hidden per-point multiplier (§19).
- **Every attempt** destroys all of the attacker's boarding party components, whatever
  the result. On failure, the marines die and nothing else changes.
- **On success** the ship changes owner at once and becomes the capturer's piece. It
  loses its experience (history 1.15). Every weapon's reload counter gets Settings
  `Captured Ship Additional Reload Combat Turns` (10) added. The log records it as
  "Taken".
- **Self-destruct.** If the target has a Self-Destruct Device and a capture would
  succeed, it detonates. This destroys the target and the boarding ship.
- A ship taken over by `Crew Conversion` changes owner the same way. Applying the reload
  penalty and experience loss to it is **(inferred)**. Drones aimed at a newly
  converted ship pick new targets (history 1.73). The new owner's seekers already aimed
  at it self-destruct (§10.1).

## 13. Ground combat

- **Triggers:** troops dropped during a battle, or hostile troops on a planet at the end
  of a game turn.
- **Sides.** The attacker has its landed troop units. The defender has its troop units in
  the planet's cargo plus militia raised from the population. Satellites, mines,
  fighters and weapon platforms never fight on the ground (history 1.24, 1.40).
- **Militia.** Settings `Defending Units Per Population` (20), `Population Defender Attack
  Strength` (10) and `Population Defender Hit Points` (30) define it. Each militia unit
  deals 10 and has 30 hit points. Recommended: one militia unit per 20M population,
  rounded down, with at least 1 while any population remains **(inferred)**. The other
  reading, 20 units per 1M, gives absurd numbers on large worlds. Confirm in play (§19).
- **Length.** Up to Settings `Number Of Ground Combat Turns` rounds per game turn. The
  data says 10 and the manual 20: use the data value. If both sides survive, the fight
  resumes next game turn and continues until one side is eliminated. It stops at once on
  peace or surrender (history 1.03, 1.20).
- **Round (inferred).** Each surviving unit fires its weapons at a random enemy unit.
  Troops pick enemy troops before anything else (history 1.24). Use each weapon's
  entry-1 damage × Settings `Ground Combat Damage Modifier Percent` (30), adjusted by
  culture `Ground Combat`, the Physical Strength characteristic, and, for the defender,
  the planet's `Planet - Change Ground Defense` percent. The version history mentions
  per-side hit chances, so a to-hit roll exists, but its formula is unknown. Troops
  absorb damage through shields, armor and components as in §9 (history 1.62). Militia
  have flat hit points.
- **Collateral.** Facilities may be destroyed during the fight. The rate is open.
- **Victory.** When the defenders are gone, the attacker takes the planet with its
  surviving facilities, stored units and population. Unrest is covered in the happiness
  spec. When the attackers are gone, the fight ends.
- **Reinforcing.** Either side may drop more troops at any time. A third empire may not
  land on a planet already contested by two other empires.

## 14. Retreat and disengagement

Stock SE4 has no retreat order and no way to leave the map. None is mentioned in the
manual, the data or the history. The only ways to disengage are:

- a `Don't Get Hurt` strategy, which keeps the piece out of enemy range;
- surviving until the turn limit, then moving away on the system map next game turn;
- cloaking, which does not help inside a battle, because every piece is decloaked.

Whether a battle restarts next game turn when hostiles still share the sector without
moving is open (§19).

## 15. After a battle

- Recover launched units automatically (§10.4).
- Restore all regenerating armor. Shields refill at the next battle's start. All other
  damage stays until repaired (repair spec).
- **Experience.** Crews, including Master Computers, and fleets gain experience from
  their actions and kills. Kills of fighters and mines are worth less (history 1.46).
  Seeker and ram kills count. A ship with a Neural Combat Net (`Combat Best Experience`)
  fights at the best experience level among its empire's ships in the battle. The
  experience scale is open (§19). A fleet's bonus is lost if the fleet breaks up.
- **Design statistics:** kills and enemy tonnage destroyed.
- **Log.** One battle report per participant, listing losses and ships "Taken". If
  Settings `Create Combat Replay` is on, also record a replay stream (§17).
- Each participant learns the designs it fought.
- Happiness events (battle won or lost in a system, ships lost) are covered in the
  happiness spec.

## 16. Strategic combat and strategies

Strategic combat uses exactly the tactical rules, with every piece driven by its
strategy. Strategies are records defined in DefaultStrategies.txt and edited per empire.
The stock set contains Optimal Firing Range, Don't Get Hurt, Capture Enemy Ships,
Capture Planet, Fighter Attack, Kamikaze, Maximum Weapons Range, Short Weapons Range,
Point Blank and Drone Attack.

| Field | Semantics |
|---|---|
| `Primary Movement Strategy`, `Secondary Movement Strategy` | One of: Don't Get Hurt (stay where no enemy can fire on you); Drop Troops (close on a planet and land troops); Maximum Weapons Range (sit at your longest range from the target); Optimal Weapons Range (the square giving the best ratio of damage dealt to damage taken); Short Weapons Range (1–3 squares, least exposure); Point Blank (as close as possible); Board Enemy Ships (close to adjacent and capture); Ram. Use the secondary strategy when the primary is impossible, for example Drop Troops with no troops or no planet. |
| `Targeting Priority 1..4` | Sort keys for choosing a target, applied in order: Nearest, Farthest, Largest, Smallest, Most Damaged, Least Damaged, Fastest, Slowest, Strongest, Weakest, Has Weapons, Does Not Have Weapons. The first key filters or sorts, and later keys break ties. |
| `Use Type Priority First`, `Type Priority <Cat>` | Rank per category (1 = engage first) over 14 categories: Planets, Fighters, Seekers(On Us), Seekers(On Others), Mines, Carriers, Colony Ships, Transports, Bases(No Weapons), Ships(No Weapons), Bases, Ships, Satellites, Drones. The flag decides whether category rank is applied before the targeting keys. The UI also offers turning type priority off. |
| `Dont Fire On <Cat>` | Never engage that category. |
| `Fighters Launch Group Amount` | Fighter group size at launch. |
| `Break Formation <Cat>` | Whether own pieces of that category leave the formation in combat. |
| `Damage Percent Per Ship / Planet / Fighter Group / Satellite Group` | Switch to a fresh target once the current one has taken this share of its hit points. Come back to finish damaged targets when no fresh ones are left. |
| `Damage Until All Weapons Gone` | If true, keep firing on a target until it has no working weapons, then apply the percentage rule. |

The AI should fire before moving away from a target, dodge enemy seekers, and close one
extra square on planets (history 1.60).

## 17. Combat tooling

- **Combat simulator.** A mock battle in which nothing is really lost. Choose from your
  designs, enemy designs you have seen, and sample planets from your home system. Assign
  each item to one of several virtual empires, which count as separate empires. You may
  edit cargo (fighters on carriers, platforms on planets), form fleets, edit strategies,
  and choose which virtual empires the computer controls. Run it tactically or
  strategically. Minefields cannot be added. Obsolete designs can be hidden. It must
  never change real game state (history 1.46).
- **Combat replay.** A view-only playback from the log, advanced one combat turn at a
  time with Next. It records only piece movement, including forced moves, and weapon
  firings. It holds no damage or reload state. Hovering a piece shows its design, not
  its damaged state. In simultaneous games only the latest turn's replay is available,
  and the host sends it to the players.
- **Weapons report.** Lists available weapons with size, reload and damage at ranges
  1–10 and 11–20. Filters: All, Direct Fire, Seeking, Point-Defense, Warhead, Only
  Latest. A mount selector shows the numbers as they would be with that mount. Seeker
  ranges are travel distances.
- **Weapon mounts window.** In the design screen, picking a mount applies it to every
  weapon added afterwards. Only mounts valid for the hull are listed (§18.2). A mounted
  component shows the mount's code letter.

## 18. Data reference

### 18.1 Components.txt weapon fields

| Field | Semantics |
|---|---|
| `Weapon Type` | `None`, `Direct Fire`, `Seeking`, `Point-Defense`, `Warhead`. If not None, the fields below are present. |
| `Weapon Target` | Fixed target-set strings joined by `\`: `Ships` (ships and bases **(inferred)**), `Planets`, `Ftr`/`Fighters`, `Sat`/`Satellites`, `Seekers`, `Drones`. Parse into a bit set. |
| `Weapon List Target Override`, `Weapon List Target Description` | Optional. A comma list (Ships, Planets, Seekers, Fighters, Satellites, Drones) that replaces `Weapon Target`, plus UI text. |
| `Weapon Damage At Rng` | 20 integers (§8). The key's capitalisation varies ("at"/"At"), so match without regard to case. |
| `Weapon Damage Type` | One of the §9.5 identifiers. |
| `Weapon Reload Rate` | Turns between shots. 0 for warheads. |
| `Weapon Display Type`, `Weapon Display`, `Weapon Sound` | Presentation only: Beam, Torp or Seeker, a sprite index, and a sound file. |
| `Weapon Modifier` | To-hit bonus (§7). |
| `Weapon Seeker Speed`, `Weapon Seeker Dmg Res` | Seekers only: squares per turn and hit points. The header calls the speed field `Weapon Speed`, but stock records use the seeker names. Accept both. |
| `Weapon Family` | Undocumented integer grouping weapon lines, separate from `Family`. Parse and keep, with no rules attached. |
| Shared fields | `Supply Amount Used` (per shot), `Tonnage Structure` (hit points), `Vehicle Type` / `Vechicle List Type Override` (the misspelling is in the data; accept it) for which vehicles may carry the part, and `Restrictions` (max per vehicle). |

### 18.2 CompEnhancement.txt (weapon mounts)

Each record has: `Long Name`, `Short Name`, `Description`, `Code` (letter badge); percent
modifiers `Cost Percent`, `Tonnage Percent`, `Tonnage Structure Percent`, `Damage
Percent`, `Supply Percent`, and optional `Shield Percent` (default 100); `Range Modifier`
(± squares, §8); `Weapon To Hit Modifier` (± points, §7); and eligibility filters:
`Vehicle Size Minimum`, optional `Vehicle Size Maximum`, optional `Comp Family
Requirement` (comma list of component `Family` values), `Weapon Type Requirement`
(`Any`; `None` meaning non-weapons only; or a single weapon type), `Vehicle Type` (a
single type or `Any`), and optional tech requirements. The stock pattern: ship mounts
multiply damage ×2, ×3 or ×5 at hull minimums of 400, 800 and 1200 kT with no range
change. Base and weapon-platform mounts also add range and to-hit. The satellite mount adds range only. All stock
mounts are direct-fire only.

### 18.3 Settings.txt entries used by combat

| Key | Default | Use |
|---|---|---|
| `Number Of Space Combat Turns` | 30 | Turn limit (§4) |
| `Number Of Ground Combat Turns` | 10 | Rounds per game turn (§13) |
| `Combat Base To Hit Value` | 100 | §7 |
| `Combat To Hit Modifier Per Square Distance` | 10 | §7 |
| `Seeker Combat Defense Modifier` | 40 | §7 (default if missing: 40) |
| `Planet Combat Offense Modifier` | 30 | §7 (default 30) |
| `Planet Combat Defense Modifier` | −200 | §7 (default −200) |
| `Ram Ship Source Modifier Percent` / `Ram Ship Target Modifier Percent` | 60 / 100 | §10.3 |
| `Captured Ship Additional Reload Combat Turns` | 10 | §12 |
| `Combat Fighter/Mine/Satellite Group Amount` | 20 each | §10 (meaning open) |
| `Maximum Mines/Satellites Per Player Per Sector` | 100 each | §10.5, §10.6 |
| `Fighters Can Be Hit By Mines`, `Drones Can Be Hit By Mines` | true | §10.6 |
| `Fighter Supply Usage Per Turn`, `Drone Supply Usage Per Turn` | 5, 200 | Idle supply use of launched units |
| `Defending Units Per Population`, `Population Defender Attack Strength`, `Population Defender Hit Points` | 20, 10, 30 | §13 |
| `Ground Combat Damage Modifier Percent` | 30 | §13 |
| `Damage Points To Kill One Population` | 10 | §11 |
| `Create Combat Replay` | true | §17 |
| `Simultaneous Games Show Strategic Combat` | false | §2 |

### 18.4 Abilities used by combat

Summed over intact components unless marked "best", which means the largest single
value (the component text says only one per ship counts):

- **Hit chance:** `Combat To Hit Offense Plus/Minus`, `Combat To Hit Defense Plus/Minus`
  (best within components; hulls add separately), `Weapons Always Hit`, `Combat
  Modifier - System` (best per system).
- **Targets and movement:** `Multiplex Tracking` (best), `Combat Movement` (best),
  `Point-Defense` (marker on point-defense weapons).
- **Shields and armor:** `Shield Generation`, `Phased Shield Generation`, `Planet - Shield
  Generation`, `Shield Regeneration`, `Shield Modifier - System`, `Armor` (marks the
  armor layer), `Emissive Armor`, `Armor Regeneration`, `Shield Generation From Damage`.
- **Damage and environment:** `Damage Modifier - System`; the sector or system abilities
  `Sensor Interference`, `Shield Disruption` and `Damage` (the last is per game turn,
  outside combat).
- **Launching:** `Launch/Recover Fighters`, `Launch/Recover Satellites`, `Launch Drones`,
  `Lay Mines` (Value1 per combat turn, Value2 per game turn), `Mine Sweeping`, `Drop
  Troops` (declared, unused in stock data).
- **Capture and command:** `Boarding Attack`, `Boarding Defense`, `Self-Destruct`,
  `Master Computer`, `Ship Bridge`, `Ship Auxiliary Control`, `Ship Life Support`, `Ship
  Crew Quarters`.
- **Experience and ground:** `Combat Best Experience`, `Ship Training`, `Fleet Training`
  (Value1 per turn, Value2 as a cap), `Planet - Change Ground Defense`.

### 18.5 Formations.txt, VehicleSize.txt, Cultures.txt

- **Formations.** The header holds only ASCII diagrams. Each record has `Name`,
  `Description`, `Leader Position Xpos/Ypos` and `Leader Design Type`, then `Number of
  Positions` (up to 100, history 1.68) followed by `Position N Xpos/Ypos/Type` on a
  19×19 template. Slots are filled in order by the leader's followers. A slot's offset
  is its position minus the leader's position. `Type` restricts which design types may
  take the slot, and is `Any` in all stock data.
- **VehicleSize.** The hull's to-hit abilities (§7) and `Engines Per Move`.
- **Cultures.** `Space Combat` and `Ground Combat` are signed percentages.

## 19. Open questions to verify in the running game

1. **Grid.** The combat map size, and how starting regions are laid out for 2, 3 or more
   empires. Do stars, asteroids and warp points block squares? Is distance Chebyshev?
2. **Distance to planets.** Nearest or centre square of the 2×2 footprint?
3. **Turn order.** Who is the "defender" who moves first? When do seekers move? When do
   reload countdown and regeneration happen?
4. **Hit roll.** Is it `roll ≤ chance`? How do culture `Space Combat`,
   Aggressiveness/Defensiveness and experience convert into points? Does `Combat
   Modifier - System` add to defense too? Do ECM and stealth armor (same ability) stack?
5. **Mount range shift.** Confirm `table[max(1, r−m)]` by comparing a platform mount's
   weapons report.
6. **Damage order.** Random pick of the next internal component: uniform, or weighted by
   size? Is armor picked at random or in design order? What happens to Skips Armor
   damage when only armor is left?
7. **Shields.** Which pool, normal or phased, drains first under normal damage? Is
   current shield capped when a generator dies? Is Shield Disruption applied once or
   per turn?
8. **Emissive.** Threshold negation or subtraction per hit? Does it act before or after
   shields? Do several parts stack?
9. **Crystalline.** Can converted shield points exceed the maximum? Organic armor:
   regenerate per component or as a pool?
10. **Unit groups.** One to-hit roll per group shot, or one per member? Does excess
    damage carry from one fighter to the next? What do the `Combat * Group Amount`
    settings actually limit?
11. **Point-defense.** Shots per turn, whether it uses the multiplex budget, and whether
    it may fire at pieces other than its listed targets.
12. **Ramming.** Confirm which setting scales which ship's damage. Do shields absorb ram
    damage? Is the rammer always destroyed, and are its warheads used up?
13. **Boarding.** Is success a strict compare or a roll? Explain the 4× gap between the
    ability values and the marine and turret counts in the descriptions. Do crew,
    security or Master Computers affect defense? Must a self-destruct device be intact?
14. **Crew Conversion.** Is the table value a percent chance? Does a converted ship get
    the captured-reload penalty?
15. **Random Target Movement and push/pull.** What does the table value mean, and how do
    map edges and blocked squares behave?
16. **Planets.** How facilities are lost as population dies. Is a planet at 0 population
    uncolonized? Do shields block the special planet damage types (plague, population,
    facility-only)? Do weapon platform shield parts add to planet shields? What is a
    planet's launch rate?
17. **Ground combat.** The militia formula, the hit chance, how damage is chosen
    (entry 1?), and how often facilities suffer collateral loss.
18. **Repeat battles.** Does combat trigger again next game turn when hostiles remain in
    the sector without anyone moving (turn-based)? How does that interact with 30-turn
    stalemates?
19. **Experience.** The level names, thresholds and bonus per level for crews and
    fleets, and how kills convert to experience points.
20. **Cargo loss.** Which stored items (troops, fighters, population) are lost first when
    cargo components are destroyed?
21. **Treaty "None".** Do empires with no treaty fight on contact?

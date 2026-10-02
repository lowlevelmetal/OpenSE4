# Spec 05: Research, intelligence, diplomacy, events, scores, AI and multiplayer

This is a clean-room behavioral spec of Space Empires IV Deluxe (v1.95), written for engine
programmers. Each rule is tagged with its source:

- **[M]**: the HTML or PDF manual.
- **[D]**: data-file headers and records.
- **[H]**: the shipped version history (`History.txt`, `DataFileHistory.txt`).
- **[T]**: numbers shown in the tutorial scenario text.
- **[I]**: our inference, which must be checked in the running game (see the last
  section).
- **(confirmed: binary)**: checked against the original executable and restated here in our
  own words (docs/CLEANROOM.md).

Field keys and enum values are functional identifiers. OpenSE4 content should use its own
names and numbers.

## 0. Conventions

- **Time**: one turn is a month, or 0.1 year. The game starts in 2400.0 [M]. Timed data
  fields are counted in turns. The game keeps its date as a count of turns, and every "years"
  value in the setup (victory conditions) is stored as years × 10 turns (confirmed: binary).
- **Data format**: data files hold `Key := Value` records between `*BEGIN*` and `*END*`.
  Lists are written as a count key followed by keys numbered from 1. Keys that are absent
  take a default, usually 0 or "none" [H].
- **Mods**: `Path.txt` names a mod folder, and each file falls back to the base install
  when the mod lacks it [H].
- **Math** (confirmed: binary): quantities are integers, but many percentages are applied in
  floating point and converted back. In this spec, round(x) means the nearest integer with
  ties to the even neighbour, and trunc(x) means dropping the fraction (toward zero). Each
  formula says which one applies.
- **Random numbers** (confirmed: binary): "a roll of 1–N" is a uniform integer from 1 to N
  inclusive.

## 1. Research

### 1.1 Research points (RP)

- **Planet output** (the formula belongs to spec 02 §5):
  - Base: the Value1 of each facility with `Point Generation - Research` (stock: a few
    hundred per center) [D].
  - Scaled by the population-production table (`Pop Modifier N` in `Settings.txt`), the
    mood modifier (`Mood … Modifier`, where riot = 0), the `Planet/System Point Generation
    Modifier - Research` (±%), the race's `Intelligence` characteristic and the culture's
    `Research` % [M][D]. The `Intelligence` characteristic changes research output, and
    `Cunning` changes intelligence output (confirmed: binary).
  - Worked example [T]: the tutorial homeworld has one base-500 center and yields 780 RP.
    That equals 500 × 130 % × 120 %, a population modifier times the jubilant mood.
- **Spaceport rule**: a system without a Spaceport contributes nothing, unless the empire
  has `Natural Merchants` [M][D]. See spec 02 for the exact rule.
- **Flat empire sources**: the `Generate Points Research` ability, which no planet pays
  for [D][H], and treaty trade (§3.3).
- **The pool** (confirmed: binary): each empire has a research pool. At the empire's
  research step (§8) the whole pool is spent on the queue and then set to 0: points the
  projects cannot absorb are lost. Later in the same end-of-turn processing the empire's new
  production, trade and tariff adjustments are added to the pool. So the points shown as
  available during a turn were produced at the end of the previous turn, and they are spent
  at the end of the current one.
- **Opening pool** (confirmed: binary): when the game is created, each empire's minerals,
  organics, radioactives and research pools start at the Starting Resources amount (Low
  5,000, Medium 20,000, High 100,000) plus one turn of the empire's own production of that
  resource. The intelligence pool starts at 0. This explains the tutorial's opening 20.6k RP
  against 780 RP per turn [T].

### 1.2 `TechArea.txt`

| Field | Semantics |
|---|---|
| `Name` | Key that other files reference. |
| `Group` | Free-text UI group. The stock groups are Applied Science, Theoretical Science and Weapon Technology. |
| `Description` | Tooltip. |
| `Maximum Level` | Level cap (1–12 in stock data). |
| `Level Cost` | Base RP per level (§1.3). |
| `Start Level` | Start level under the "Low" tech-start option. The three colonization areas are overridden: the one matching the home planet type starts at 1 [D][M]. |
| `Raise Level` | Start level under the "Medium" option, as max(Start, Raise). The "High" option starts every area at its maximum. |
| `Racial Area` | n > 0: the area is visible only to races that have a trait with `Trait Type := Tech Area` and `Value 1 := n`. The stock values are Psychic, Religious, Temporal, Crystallurgy and Organic. |
| `Unique Area` | n > 0: the area is visible only after the empire gains an `Ancient Ruins Unique` ability with Value1 = n, by colonizing that ruin planet. |
| `Can Be Removed` | Whether setup may exclude the area ("Technology Areas Allowed"). Items that depend on an excluded area can never be obtained. |
| `Number of Tech Req`, `Tech Area Req N`, `Tech Level Req N` | All must hold (AND) before the area is visible. |

**Start levels at game creation** (confirmed: binary), in this order:

1. Every allowed area that is still at level 0 and passes the racial and unique checks gets
   its `Start Level`.
2. "Medium" tech start: every allowed area is raised to its `Raise Level` if it is below it.
   "High": every allowed area the race can see is set to its `Maximum Level`.
3. The colonization area of the home planet's type (the first tech requirement of the
   component that colonizes that planet type) is then set to exactly level 1. This happens
   even under "High", which leaves that one area at level 1 (a quirk of the original).

An area is **researchable** when all of these hold [D]:

- it is allowed in this game;
- its racial and unique checks pass;
- all its requirements are met;
- its level is below the maximum.

Gaining a level (from any source) only checks the first two conditions (confirmed: binary).

Example: an armor area appears once Chemistry reaches level 1.

**Ancient ruins** (confirmed: binary): colonizing a planet with `Ancient Ruins` (Value1 = N)
gives N advances. For each advance the game rolls an area uniformly among all tech areas,
up to 1,000 times, until it finds one that is researchable (above); that area gains exactly
one level. The ability is then removed from the planet. `Ancient Ruins Unique` makes its
unique area visible to the colonizing empire and is removed too.

### 1.3 Level cost

Let LC be the area's `Level Cost` and L the level being researched (current level + 1).
The "Technology Cost" setup option has three settings (confirmed: binary):

| Setting | Cost of level L |
|---|---|
| Low | LC × L |
| Medium (the default) | max(LC × L, trunc(LC × L² / 2)) |
| High | LC × L² |

Every cost is capped at 2,000,000,000. Levels 1 and 2 cost the same under Low and Medium
(LC and 2 × LC), which matches the tutorial's data points [T]. With LC = 5,000:

| L | 2 | 3 | 5 | 10 |
|---|---|---|---|---|
| Low | 10,000 | 15,000 | 25,000 | 50,000 |
| Medium | 10,000 | 22,500 | 62,500 | 250,000 |
| High | 20,000 | 45,000 | 125,000 | 500,000 |

### 1.4 Queue and allocation

- **Queue** [M]: up to 12 projects. Each project is an area; it always works on the area's
  next level. An area can be queued only once: adding an area that is already in the queue
  does nothing (confirmed: binary). The ETA is shown as ceil(remaining / RP applied) turns,
  displayed in years [T].
- **Research step** (confirmed: binary), once per turn for each empire (§8):
  1. Projects whose area has reached its maximum are removed.
  2. Every project's share is computed from the pool before any progress changes:
     - **Divide Pts Evenly** on: every project gets round(pool / N), where N is the number
       of projects. The shares are not capped by what a project needs, and the rounding can
       make them add up to slightly more than the pool.
     - Divide off (in list order): walking down the queue, each project takes the smaller
       of what it still needs (cost of its next level − progress, at least 0) and what is
       left of the pool; the points it gets are round of that amount. Example: 10k RP
       against three projects of 4k each completes two and puts 2k into the third [M].
  3. Each project adds its share to its progress. A project whose progress reaches the cost
     of its area's next level completes: the area gains exactly one level, and the project
     leaves the queue with all its progress, so any excess is lost. At most one level per
     project per turn.
  4. **Repeat Projects**: a completed area that is still below its maximum is added again
     at the end of the queue with no progress.
  5. The pool is set to 0.
- **Removal**: removing a project throws away its progress. Progress belongs to the queue
  entry and moves with it when the queue is reordered (confirmed: binary).
- **Reorder** [M]: a separate window moves projects up, down, to the top or to the bottom.
- **Completion messages** [T]: finishing a level logs "New Tech Level", then "<item>
  Discovered" for each newly available item. An empty queue logs "All Projects Completed".
- **OpenSE4 choice** [I]: the ETA simulates the queue with the pool for this turn and the
  current production for the turns after, so projects waiting behind others in in-order
  mode get a real estimate.

### 1.5 Unlocking and other tech sources

- **Unlocking**: components, facilities, vehicle sizes, component enhancements (mounts)
  and intel projects all carry the same `Number of Tech Req` / `Tech Area Req N` / `Tech
  Level Req N` block (AND). An item with zero requirements is available from the start
  [D].
- **When to recompute**: after every tech change, re-evaluate what is available and log
  the new items. Tech changes come from research, gifts, trades, theft, ruins, surrender,
  Analyze and the Medium/High start.
- **Other sources**:
  - A Technology item in a gift, tribute or trade package gives the giver's level. It
    helps only when the receiver's level is lower. Setup options "Allow Gifts/Tributes" and
    "Allow Technology Gifts/Tributes/Trades" can forbid it [M].
  - Subjugation does **not** pass technology to the master in the original, despite the
    treaty description; the master only sees the subject's designs (§3.2) (confirmed:
    binary).
  - `Research - Steal` intel (§2.3).
  - **Analyze** (the Scrap window, spec 03 §15) (confirmed: binary). Analyzing an own ship or
    base at an own space yard destroys it and raises the owner's levels from the tech
    requirements of its intact components and its hull. Each distinct (area, level)
    requirement above the owner's current level in that area gives exactly one level in that
    area, so an area rises by the number of distinct higher levels its parts require and never
    above the highest of them. No research points are involved: nothing goes to the pool or to
    the queue, and the level is gained even when the area is not yet researchable (only the
    "allowed in this game" and racial or unique checks of §1.2 apply). The log entries are those
    of a level completed by research (§1.4) without "All Projects Completed". Any own ship or
    base may be analyzed, but one the empire built needs no technology it lacks, so Analyze pays
    only for a captured ship or one received from another empire. The Scrap window's
    "Research Potential" word (None, Minor, Moderate, Sizable, Major for 0, 1, 2, 3, 4+
    requirements) previews it. The engine has no Analyze yet (spec 03 §15).

## 2. Intelligence

### 2.1 Points and lifecycle

- **Intelligence points (IP)**: generated like RP. Sources are `Point Generation -
  Intelligence`, IP modifiers, `Generate Points Intelligence`, the culture `Intelligence`
  %, and Partnership trade.
- **Cunning** changes intelligence point generation; it has no other effect on operations
  (confirmed: binary).
- **The pool** (confirmed: binary): it works like the research pool (§1.1). It starts the
  game at 0, is spent at the empire's intelligence step and is then set to 0.
- **"Allow Intelligence Projects"** off: the intelligence step is skipped entirely [M]
  (confirmed: binary).
- **Queue**: the same as research, with 12 slots, Divide Evenly or in-order funding, Repeat
  and Reorder [M]. The shares use the same rules as §1.4 step 2, where "what a project still
  needs" is `Cost` − progress (confirmed: binary).
- **Adding a project**: pick a target empire we are **in contact with** (§3.1), friend or
  foe; defense projects take no target. Then pick a specific target according to the type:
  a known planet or ship, or a third empire for political operations, or "Any" [M].
- **Intelligence step** (confirmed: binary), once per turn for each empire in empire order
  (§8):
  1. An empire in contact with no living empire loses its whole queue. Projects aimed at
     an empire that has been destroyed are removed.
  2. Shares are computed and added to progress.
  3. Every project whose progress has reached its `Cost` runs, in queue order. There is
     **no random success roll**: an attack aimed at a living empire is only tested against
     that empire's counter-intelligence (§2.4). If it is not defeated, its effect applies
     (§2.3); an invalid or missing target makes it fail with a message.
  4. Every project that ran, whether it succeeded, failed or was defeated, leaves the queue,
     unless Repeat is on: then its progress restarts at 0 in place. Defense projects follow
     the same rule.
  5. The pool is set to 0.
- **Target "Any"** (confirmed: binary): the operatives pick the target themselves: up to
  1,000 random draws among the candidates of the right kind (the target empire's ships, its
  planets, the tech areas, third empires, or systems), and a candidate that fails the checks
  is removed from the draw. "Any" gives no bonus; the manual's claim that it succeeds more
  often is not borne out by the binary. For a political operation the candidates are the
  living empires the source has contact with, other than the source and the target, in
  empire order; no further check applies, so the first draw stands. With no candidate the
  operation fails and the source is told that no valid target was found. A third empire
  named in the order is only checked to exist; the handler checks the rest (confirmed:
  binary).
- **Messages**:
  - The source is sent a source message.
  - The victim gets a titled target message, prefixed with an "Intelligence Minister"
    label. On a successful operation, a roll of 1–5 equal to 1 (a 20 % chance) adds a line
    naming the source as the suspect (confirmed: binary).

### 2.2 `IntelProjects.txt`

- **Display**: `Name`, `Description`, `Group`. The stock groups are Ship Sabotage, Planet
  Sabotage, General Sabotage, General Espionage, Political Disruption and Defense. The
  "stop espionage" and "stop sabotage" demands imply that each project belongs to one of
  those two classes.
- **Mechanics**: `Cost` (IP), `Type` (effect handler) and `Effect Amount` (a signed
  parameter whose meaning depends on the handler).
- **Messages**: `Num Source Messages`/`Source Message N`, `Num Target Messages`/`Target
  Message Title N`/`Target Message N`, plus `Source Picture` and `Target Picture`.
- **Requirements**: the tech block. The stock data uses only Applied Intelligence, levels
  1–4.
- **Tokens**: messages substitute system, sector, source/target/other emperor and empire,
  vehicle name and size, planet, design, tech, treaty and facility. Events and AI speech use
  the same token scheme, so implement one formatter.
- **Type values** (confirmed: binary): the effect handler recognises the types named in
  §2.3 and §4 and also `Log Reports - Steal`, `Combat Logs - Steal`, `Storm`, `Nebulae` and
  `Black Hole - Created/Destroyed`. There is no `Ship - Locations` type.

### 2.3 Effect handlers

Amounts are the record's `Effect Amount`. The handlers are shared by intel projects and
events (§4); "source" is the spying empire and is absent for events. Unless marked
otherwise, the readings below are (confirmed: binary). Every handler that acts on an object
first checks that the object exists, is not destroyed and, when a target empire is given,
belongs to it; otherwise nothing happens. An event whose effect does not apply sends no
message at all.

| Category | `Type` → effect on success |
|---|---|
| Ship sabotage | `Ship - Damage`: Amount damage points through the standard damage routine. `Ship - Lose Movement`: the ship loses Amount of its remaining movement points for the current turn (never below 0). Movement points are refilled when simultaneous movement starts, and in a turn-based game at the start of each player's turn before its vehicles move (§8). Intelligence runs in the attacker's end-of-turn processing and events after every player, so the loss is always refilled before the ship moves again and has no effect. `Ship - Lose Supply`: removes min(Amount, the ship's supply). `Ship - Rebel`: the ship joins the source. `Ship - Experience Change`: adds the signed Amount. `Ship - Cargo Damage`: if the ship carries anything, its whole cargo (every person and unit) is destroyed; Amount is not used, and the message shows its absolute value. `Ship - Orders Change`: mothballed ships and ships without an order list are immune; the ship leaves its fleet, its orders are cleared, and it gets one Move To a random sector of a random system of the quadrant (its own system included). |
| Ship espionage | `Ship - Concentrations`: a report. `Ship - Construction Info`: the largest queue concentrations. `Ship Designs - Steal`, `Unit Designs - Steal`: the thief learns exactly one design of the target, the newest one of the right class (ship or base, or a unit type) that has been built at least once, or that a ship was retrofitted to (a mark kept with the design, cleared only when the design is saved again in the designer; confirmed: binary), that the thief does not know yet and that its owner can still build. It is dated as seen this turn (§8 "Design knowledge"); it does not join the thief's own designs, and nothing is copied. A specific target in the order is ignored; with no such design the operation fails. |
| Planet sabotage | `Planet - Conditions Change`: any planet, colonized or not; the conditions become conditions + Amount / 10 on spec 02's 0–1.5 scale, kept within 0 and 1.5 (Amount is in tenths). The planet sends no notice of its own, but the record's messages go out as for any other effect: as a project, the source and the victim get theirs (with the 1-in-5 suspect line); as an event, the recipients its `Message To` names (confirmed: binary). `Planet - Value Change`: each of the three resource values changes by Amount; in finite-resource games by Amount × 1,000 when that is strictly between −500,000 and +500,000, otherwise by Amount. A result below 0 becomes 0; any other result is clamped into `Minimum/Maximum Planet Percent/Resource Value`, so a value already outside is pulled in. `Planet - Population Change`: with s = trunc(\|Amount\| / 5), the change is Amount + d, d uniform from −s to +s. A loss is taken from the population groups in list order, first group first; a gain goes only to an existing group of the owner's race, capped at the planet's maximum population (the domed maximum when domed); no group is created. The message shows the absolute change. `Planet - Population Anger Change`: needs a colony whose owner's race is not `Population Emotionless`; anger changes by Amount in whole percent, within 0 and 100 (at most 80 on a capital). `Planet - Population Rebel`: see below. `Planet - Cargo Damage`: nothing when Amount ≤ 0 or the cargo is empty; otherwise Amount damage points strike the population in the cargo first, killing Amount ÷ `Damage Points To Kill One Population` million (truncated) from the first group on; if fewer people are aboard, all die and the damage they absorbed (people × that setting) is subtracted, and the rest hits the units in the cargo as a hull-damaging hit on a unit group (spec 04 §9.4). `Planet - Facility Damage`: n = min(Amount, the number of facilities), nothing when n ≤ 0; n facilities are destroyed one at a time, each draw picking a facility kind with a chance in proportion to how many of that kind the planet had at the start (a kind with none left is drawn again); the message shows n. |
| Planet espionage | `Planet - Info`: a full report, including cargo [H]. `Planet - Locations`. |
| Points | `Points - Change`: the target's minerals, organics and radioactives each change by Amount, never below 0. `Points - Steal`: for each of the three, min(Amount, the target's stock) moves to the source. Research and intelligence points are not touched. |
| Projects | `Research - Steal`: the chosen area must be one where the target's level is above ours and that is neither racial nor unique; we gain exactly one level. `Research - Delete Project`, `Intel - Delete Project`: one random entry of the target's queue is removed with its progress; the latter can remove a defense project. |
| Politics | `Politics - Disrupt Trade`: if trade between the target and the third empire is running, its counter restarts at 0 (§3.3). `Politics - Intercept Messages`: reports the latest political message between the target and the third empire from the last two turns; with none, the operation fails. `Politics - Fake Messages`: a declaration of war is sent in the target's name to the third empire, and it takes effect: the two are at War. `Politics - Prevent Messages`: deletes the political messages of the last two turns between the two from both logs, so they are never answered; nothing is blocked afterwards. `Politics - Treaty Info`: the treaty between the two. |
| Info | `System - Info`: the highest-numbered system that the target has explored and the thief has not becomes explored for the thief; with none, the operation fails. `Empire - Info`, `Tech Level - Info`: reports. |
| Defense | `Intelligence Defense`, with level = Amount (1–3). See §2.4. |

- **`Planet - Population Rebel`** (confirmed: binary):
  - As an event (no source): the colony breaks away and becomes a new independent empire,
    if fewer than 20 empires exist. Destroyed empires count, since their numbers are never
    reused.
  - As an intel project: a roll of 1–4 equal to 1 makes the colony a new independent
    empire; otherwise a second roll of 1–4 equal to 1 makes it join the source; otherwise
    nothing happens. That is 25 %, 18.75 % and 56.25 %. When the 20-empire limit blocks
    the break-away, nothing happens (the "join the source" roll is not made).
  - **The new empire** starts as a copy of the former owner (confirmed: binary): race,
    traits, characteristics, culture, research levels and queue, intelligence queue,
    construction queues, options, experience, and its ministers' state: the minister
    style, the anger toward each empire, the turns since war with each, the AI state and
    turns in it, and the AI's target, staging, secured and defended systems and its
    attack timer. It gets no designs, and the lists of accepted demands start empty. Then:
    - its name is the system's name. When that is empty or any other empire (destroyed
      ones included) has the name, a random line of the empire-names file is drawn, again
      until the name is unique. It gets a new leader name and the pictures of a random
      neutral race that no empire uses for its race or pictures; when all are in use, a
      random one of all the neutral races;
    - it is computer controlled with all ministers on, not neutral; its difficulty is the
      highest held by any computer-controlled empire, or Medium when there is none (§7.1);
    - its home is the planet's system and sector (§4). The planet's atmosphere becomes the
      gas its race breathes and the planet's physical type its native type, and its lists
      of allowed atmospheres and planet types are reset to just those two; it has explored
      only that system (every system under the option that shows all systems);
    - the colony's whole population, of every race, becomes one group of its own people
      with the same total; the colony becomes a Homeworld and a capital, with anger 25;
      facilities and cargo stay;
    - each of its five stocks starts at 4 × its production of that kind;
    - all its treaties start at "no contact" (anger kept, as above) and its log is
      emptied: it meets only the empires that detect it and that a warp path links to it
      (§3.1), at None, not at War.
  - A homeworld can rebel: neither a named target nor an "Any" pick excludes it.
- **`Research - Steal` with target "Any"** (confirmed: binary): the original's automatic
  pick keeps only areas where the thief is already *ahead* of the target, so the steal that
  follows always fails. A faithful engine reproduces this; an option may correct it.
- **"Any" picks** (confirmed: binary) apply only two candidate checks: the inverted
  `Research - Steal` level test above, and `Change Bad Intelligence Chance - System`
  (§2.4). The handler's own checks come afterwards, on the chosen target.

### 2.4 Counter-intelligence

**Defense strength and blocking** (confirmed: binary). When an attack runs (§2.1 step 3)
against a living empire T:

1. A = the attack's accumulated progress (at least its `Cost`).
2. T's `Intelligence Defense` projects are taken **from the bottom of T's queue upwards**.
   Each adds trunc(Amount × progress × (`Intelligence Defense Modifier Percent` / 100)) to
   a running total D (capped at 1,000,000,000), and its progress drops to 0. Amount ×
   progress is a whole-number product, multiplied by the fraction in floating point.
3. As soon as D ≥ A the attack is **defeated** and the walk stops. The defense project that
   tipped the balance keeps trunc((D − A) / Amount / (modifier / 100)) progress, but never
   more than it had (when Amount or the modifier is 0 or less it keeps D − A, capped the
   same way).
4. If all of T's defenses together stay below A, the attack goes ahead, and all those
   defenses have still lost their progress.
5. A defeated attack is used up like any other. The attacker is told its project was
   defeated; the defender is told who attacked.

**A defense project that completes** (confirmed: binary): when its progress reaches its
`Cost`, it runs like any project. It goes through the other living empires in random order
and deletes the first project, in queue order, that it finds aimed at its owner and whose
technology requirement is at most the defense's level (Amount); the requirement is the
**sum** of the levels in the project's tech block (confirmed: binary). Only one project is deleted; if none qualifies
the defense achieves nothing. It then leaves the queue unless Repeat is on.

**Timing** (confirmed: binary): the intelligence steps run empire by empire in empire order
during end-of-turn processing (§8). A defender with a lower empire number has already added
this turn's points to its defenses when a higher-numbered attacker strikes; a defender with
a higher number has not.

**`Change Bad Intelligence Chance - System`** (confirmed: binary): the original consults
it only while choosing an "Any" target, and reads it only from the abilities of objects in
the system that belong to no empire (stars, warp points, uncolonized planets and the
like): colony facilities never count, and a system-type ability counts only when an object
carries it. It keeps only a positive value V. With V = 100 it has no effect; otherwise a roll of
1–100 at most V rejects that candidate. The stock facilities carry negative values, so the
ability never changes anything in the stock game.

**AI reaction**: a detected attack adds `Intelligence Against Us` anger (§7.3).

## 3. Diplomacy

### 3.1 Contact

- **First contact** (confirmed: binary): the check runs in one system at a time, and only
  at these moments:
  - once in every system when the game is created, after the empires are placed;
  - in the destination system when a group arrives through a warp point (Warp, Move To or
    pursuit) and some member is still there after the passage;
  - in an object's system whenever a ship, unit group or colony decloaks, whatever the
    cause: the Decloak order (a fleet's reaches every member that can cloak), a
    minister's or computer player's decloak before an order, the start of a battle, a ship
    that can no longer cloak or runs out of supply, a fighter or drone group out of
    supply, a colony's automatic decloak at a recalculation (spec 01 §6.9);
  - in an event's system once the event is logged (sight is updated first);
  - in every system after a surrender;
  - in the system of a planet or vehicle handed over in a package, after that system
    becomes explored for the receiver;
  - in a planet's system when an intelligence project's `Planet - Population Rebel`
    makes its colony a new empire (§2.3; as an event, the event's own check covers it).

  It does not run on moves within a system, colonizing, construction, launches or
  recoveries, the end of a battle, the host's reading of turn or game files, or in any
  per-turn pass; so ships of two empires that meet inside a system make no contact until
  one of these moments comes there. Each time, every pair of living empires that have not
  met and that **each detect the other** in that system make contact, but only when a warp
  path links them: a colony of the other empire must lie in
  a system reached from the systems holding our colonies by following warp points (every
  one, known or not, in its own direction). Their treaty goes from "no contact" to None and
  each logs "First Contact". So an empire without colonies makes no contact.

  Since 2026-10-01 the engine follows this (`diplomacy::firstContactIn`, and
  `firstContactEverywhere` at game creation and after a surrender): a warp arrival
  (`movement.cpp`, after the passage), every decloak (the Decloak order, the Ship Cloaking
  minister's and a computer colony's decloak before an order, a drone's at its pursuit's
  target, a sweeper group's in a tagged minefield, a cloak lost after an action or at the
  supply step, the start of a battle that decloaks a piece, a colony's Decloak), a logged
  event, a package's planet or vehicle and a rebellion project. Detection is worked out
  from the current positions and sensors. A timed event's start message counts as the
  event being logged (inferred, question 57). The colony's automatic decloak at a
  recalculation is spec 01 §14 Q44 (`diplomacy::recalculateColony`, the check in the
  colony's system). In a simultaneous game a colony's Decloak makes only the acting
  empire's side of a contact (`firstContactIn` with its `onlySide`, spec 01 §14 Q44); a
  pair where one side has met the other is completed by the next check in a system where
  both detect each other.
- **What contact enables** [M]: messages, treaties, trades and intel.
- **Losing contact** (confirmed: binary): once per game turn, after the design cleanup and
  before the victory check, in both turn styles (§8), each living empire A checks every
  empire B it has contact with. It follows warp points outward from the systems holding
  A's colonies, as for first contact. When no colony of B lies in a reached system, A's
  treaty with B returns to "no contact", A's intelligence projects against B are removed,
  and A logs "contact lost" (a history line, §3.4). A's anger toward B is unchanged. B's
  side changes when B is checked. So an empire that loses its last colony loses contact
  with everyone at the next check, even while its ships keep it alive. Contact also ends
  when an empire is destroyed (§6), which logs only the destruction.
- **Buying contact** [M]: a package item **Comm Channels** grants contact with a third
  empire.

### 3.2 Treaties

Treaties are ordered from worst to best (confirmed: binary): War, Non-Intercourse, no
contact, None, Non-Aggression, Subjugation, Protectorate, Trade Alliance, Trade and
Research Alliance, Military Alliance, Partnership. Benefits accumulate from Non-Aggression
up the trade chain [M].

| Treaty | Effect |
|---|---|
| War | Hostile. |
| Non-Intercourse | Cold. The parties agree to keep apart. Counts as a "bad" treaty for happiness. Ships fight, as under every treaty below Non-Aggression (spec 04 §2, confirmed: binary). |
| None | Met, no treaty. Ships still fight, because Non-Aggression is the first level that stops combat. |
| Non-Aggression | No combat. Ships may share sectors. |
| Subjugation | The subject pays `Treaty Subjugated Resource Percentage` of its income each turn and may hold no other treaty (§3.3). The master learns every design of the subject. No trade. |
| Protectorate | The protected side pays `Treaty Protectorate Resource Percentage`. Protection is honor-based. No trade. |
| Trade Alliance | Resource trade (§3.3). |
| Trade and Research Alliance | Adds RP trade. |
| Military Alliance | Adds resupply at the ally's depots. RP trade continues. |
| Partnership | Adds IP trade and shared sight. Each turn the partner's explored systems become explored for us (logged "New System Maps Available"), and we learn the designs the partner has seen that we have not. |

- **Who is the master** (confirmed: binary): in a Subjugation or Protectorate, the empire
  that **accepts** the proposal becomes the subordinate and pays; the proposer becomes the
  master.
- **Accepting Subjugation** (confirmed: binary) drops every treaty better than None between
  the new subject and the third empires it is in contact with to None; both sides of each
  lost treaty are told. War and Non-Intercourse stay. Accepting a Protectorate breaks
  nothing.
- **Consistency check** (confirmed: binary): at each empire's treaty step (§8), if the two
  sides of a pair record different treaties, both are reset to None.
- **Break Treaty** sets both sides to None; **Declare War** sets both to War, whatever the
  current treaty (confirmed: binary).
- **Combat** (confirmed: binary, spec 04 §2): two empires fight when their treaty is below
  Non-Aggression (War, Non-Intercourse, None or no contact). Non-Aggression and every better
  treaty, Subjugation and Protectorate included, never fight. Allies never fire on each other,
  not even when two of our allies fight. Both allies defend a shared location [M].
- **Happiness**: Trade and better are "good" treaties that please the empire. War,
  Non-Intercourse, Subjugation and Protectorate displease it [M]. Accepting a treaty raises
  a happiness event for both sides (confirmed: binary).
- **Treaty grid**: treaties between third parties are visible only for empires we are
  allied with [M], or through `Politics - Treaty Info`.
- **OpenSE4 choices** [I]:
  - A Treaty package item makes the receiver the master unless it names the giver.
  - A subject cannot sign new treaties with third empires.
  - Break Treaty does not end a war.

### 3.3 Trade and tariffs

- **Trade counter** (confirmed: binary): every ordered pair of empires keeps a counter of
  turns. At each empire's treaty step it grows by 1 for every other living empire, whatever
  the treaty. Setting a treaty resets it to 0, unless both the old and the new treaty are
  Trade Alliance or better.
- **Trade percentage** (confirmed: binary): with a treaty of Trade Alliance or better,
  tradePct = min(counter, `Maximum Trade Percentage`) (stock 20). On the turn a treaty is
  signed the counter is 0, so trade starts at 1 % on the next turn and rises by 1 % a turn.
- **Income** (confirmed: binary): at our treaty step, from each partner X,
  `income = trunc(round(base_X × tradePct / 100) × F / 100)`, created from nothing; X
  receives the same from us at its own step. base_X is X's production of that kind for the
  turn, the same figure X's own income uses (before tariffs and maintenance). F = 100 +
  (`Political Savvy` − 100) + the sum of Value 1 of the race's traits whose Trait Type is
  `Trade` (the stock data has none) + the culture's `Trade` value. Minerals, organics and radioactives are traded from Trade Alliance up, RP from
  Trade and Research Alliance up (including Military Alliance), and IP only in a
  Partnership. The income joins the pools after our own production, so traded RP and IP are
  spent next turn.
- **Tariffs** (confirmed: binary): at the subordinate's income step, each of its five
  incomes (minerals, organics, radioactives, research, intelligence) is cut by
  round(income × pct / 100), never more than the income, where pct is the treaty's
  `Settings.txt` percentage. The income taxed is the whole non-trade income of that kind:
  colony production (after the income floor, spec 02 §5.6), remote mining and `Generate
  Points` income. Trade income arrives later (§8 step 6) and is never taxed. The master receives the minerals, organics and radioactives at
  once; the research and intelligence part is simply lost. Tariffs are taken before the
  computer-player bonus (§8).

### 3.4 Messages [M]

- **Rules**:
  - An empire may send one message per recipient per turn. It arrives in the recipient's
    log.
  - A message has a type, a tone (Pleading, Neutral or Demanding) and editable template
    text.
  - An AI in a simultaneous game replies two turns after the send [H].
- **When a message takes effect** (confirmed: binary): in a turn-based game a message is
  processed the moment it is sent, so a declaration of war or an acceptance changes the
  treaty at once. In a simultaneous game each player's messages are processed at the start
  of the host's turn processing, player by player, before the computer players act and
  before movement (§8).
- **Binding types**:
  - Propose, Accept, Refuse and Counter Treaty. A treaty takes effect only on acceptance.
  - **Break Treaty**: immediate, and the treaty falls to None.
  - **Declare War**: immediate, whatever the current treaty.
  - **Propose, Accept, Refuse and Counter Trade**: the asked-for side may hold "Any"
    placeholders. A trade with placeholders cannot be accepted; the recipient must counter
    with real items. Items lost before acceptance show as "unavailable" [H].
  - **Gift, Tribute**, with Accept and Refuse. Items move only on acceptance.
  - **Surrender**: the sender's whole empire passes to the recipient at once, and ground
    combat stops [H]. (confirmed: binary) It takes effect only when the game option Allow
    Surrender is on (§7.4); otherwise the message does nothing. Every object of the sender
    passes to the recipient, and every system where such an object lies becomes explored
    for the recipient; no other map knowledge passes. The recipient receives the sender's
    minerals, organics and radioactives, which drop to 0; research and intelligence points
    do not pass. Designs are learned as in §8 "Design knowledge". Technology: the
    recipient gains **exactly one level** in every area where its level is lower than the
    sender's, that is allowed in the game, that passes the racial and unique checks for
    both empires and whose requirements the sender meets. The living third empires in
    contact with either side are told. Nothing else happens then: the sender, now owning
    nothing, is destroyed at its next destruction check (§6), which resets its treaties to
    no contact and removes the intel projects aimed at it.
  - **Grant independence**: marks one of the sender's planets, which the sender then
    evacuates and abandons; the recipient may then colonize it.
- **Non-binding demands and requests**, each taking a system, planet or empire where
  relevant: want a gift, want a tribute, demand surrender, remove ships from a system,
  remove colonies from a system, leave a planet, stop hostilities against an empire, break
  a treaty with an empire, declare war on an empire, make peace with an empire, support us
  against an empire, attack an empire in a system, attack a planet, stop espionage, stop
  sabotage, stop attacks in a system. The recipient replies Accept or Refuse. Only the AI
  acts on them (§7.4).
- **Package items** (each logged on its own when carried out, spec 06 §7 Q70; confirmed:
  binary):
  - Systems: our border claim on the system passes to the receiver.
  - Planets: transfers ownership.
  - Resources: in steps of 1000.
  - Technologies.
  - Ships.
  - Loaded transports: the ship and its cargo.
  - Star Charts: the system becomes explored for the recipient.
  - A Treaty: becomes active on acceptance.
  - Comm Channels: only when the receiver has no contact with the named empire; the two
    are then set to treaty None, without a first-contact check or entry.
- **Borders**: each empire publicly claims systems, and claims may overlap. The AI uses
  claims to compute anger.
- **History window** (confirmed: binary): each human player's history is a file of dated
  lines, each naming another empire (or none), kept by player number in a history folder of
  the installation (not with the game) and extended once a turn at the start of that
  player's end-of-turn processing (§8 step 2). The window shows the
  whole file; nothing limits its length. Each turn the file gets the entries dated the turn
  before:
  - from the political messages sent or received by the player: an accepted treaty ("<treaty>
    established"), a broken treaty and a declaration of war. The original also has a line for
    a surrender, but its test can never pass, so a surrender is never recorded;
  - from the player's own log: an empire destroyed, first contact, and contact lost (§3.1).
    A destruction writes only its own line, never a contact-lost line. The log entry for a
    treaty enacted by a package is of the same kind as a first-contact entry, so it also
    writes a contact line naming the other party (spec 06 §7 Q70).

  Nothing else is recorded: colonies, battles, events, research and intelligence stay in the
  log. Computer players have no history.
- **Players' text files** (confirmed: binary): a human player's three files (statistics
  §5, history, log copy) live in a history folder of the installation (or of the mod),
  named after the player number. Saving a game copies them next to the save, prefixed with
  the game's name; loading a game empties the folder and copies that game's files back. A
  TCP/IP host sends each player its own history and statistics files with the turn.
  - History: appended, and opened only when there is at least one line. Each line is the
    entry's date as a whole number of tenths of a year (24001 for 2400.1) right-aligned in
    8 characters, the other empire's number in 5 (0 for none), two flags always 0 in 5
    each, a space, then the text.
  - Log copy: written only when `Create Log Text Files for Players` is true (it is off when
    the key is missing, and the stock file turns it off), and only when the log is not
    empty. The file is rewritten each turn, not appended: two header lines (the column
    titles, then a rule), then one line per entry of the whole log as it stands: the date
    in the form 2400.1 left-aligned in 9 characters, the title padded to 40, then the text
    with its line breaks turned into spaces.
- **Log categories**: Construction, Research, Intelligence, Events, Politics, Combat, Misc.
- **Log lifetime** (confirmed: binary): at the end of each empire's turn processing its log
  keeps only the entries of the last turn; the history window keeps the long record.
- **OpenSE4 choices** [I]:
  - Replies are checked against the message they answer: an acceptance only works for a
    proposal the recipient really sent. Accepting a counter-proposal (an Accept Demand
    reply) applies the counter.
  - Messages are kept 10 turns after sending, then dropped, together with any message
    whose sender or recipient is gone.
  - The history record (an extension, open question 30): each empire keeps its own dated
    list, whose lines concern itself, another empire or nobody (the General list), each with
    an optional map position. It is kept for the whole game, saved with it, and shown only to
    its owner. It records more than the original's file (open question 30 lists it).

## 4. Random events

- **Setup options** [M][D]:
  - **Event Frequency**: None, Low, Medium or High. Low, Medium and High use `Event Percent
    Chance Low/Medium/High` in `Settings.txt` (stock 5, 10 and 25 %).
  - **Maximum Event Severity**: Low, Medium, High or Catastrophic.
- **The event step** (confirmed: binary) runs once per game turn, after every empire's
  end-of-turn processing and after the victory check (§8):
  1. Ships in hazardous sectors take their damage (spec 01 §7).
  2. Timed events that are due strike (below).
  3. **One roll for the whole galaxy**: a new event happens when a roll of 1–100 is at most
     the chance of the chosen frequency. There are no new events until the date (already
     advanced for this turn) reaches 2402.0, so the first 19 turns have none.
- **Choosing the record** (confirmed: binary): let N be the number of `Events.txt` records
  whose `Severity` is at most the Maximum Event Severity. The record is drawn uniformly
  among the **first N records of the file**, whatever their severity. This is a bug in the
  original: the severity filter only sets N. When every severity is allowed it makes no
  difference.
- **Choosing the target** (confirmed: binary): the record's `Type` decides the kind of
  target, taken from the whole galaxy: ship types → every owned ship, base and unit group
  in space; planet types (including `Planet - Destroyed`, `Planet - Plague` and `Plague
  Cured`) → every colony (uncolonized planets and asteroid fields are never drawn);
  political types (`Politics - Disrupt Trade`, `Intercept Messages`, `Fake Messages`,
  `Treaty Info`, `Prevent Messages`) → every empire number, destroyed ones included, but
  they run with no third empire and so achieve nothing (the stock data has none);
  `Star - Destroyed` → stars; `Warp Point - Closed` → warp points. Every other type (for
  example `Points - Change`, `Research - Delete Project`, `Intel - Delete Project`,
  `System - Info`, `Planet - Created`, the storm, nebula and black hole types, `Warp Point
  - Opened`) has no target list, so such a record never fires and that turn has no event. The game draws candidates at random, up to 1,000 times;
  a candidate that fails a check is removed from the draw:
  - it no longer exists;
  - for a High or Catastrophic planet or star event: it sits exactly at an empire's home
    planet location (system and sector), so homeworlds are safe from those, but stars,
    which sit elsewhere, practically never are. Each empire records that location when
    the game is created (its home planet) or when a rebel empire is founded (the rebel
    planet), and it never moves: not when the planet is lost, captured or abandoned, nor
    when the empire is destroyed. Every empire's recorded location counts, destroyed
    empires included, whoever owns the planet now; other starting planets and other
    capitals are not protected (confirmed: binary);
  - **luck**: for an owned target, a roll of 1–100 must be below 100 + the owner's `Luck`
    trait values (a total of 0 or less skips the roll). A normal race keeps 99 %, a `Luck`
    −50 race 49 %. An empire target rolls with its own Luck. For star events every empire
    with a colony in the system (vehicles do not count) makes this roll (a total of exactly
    100 skips it). Luck applies to every event, good or bad;
  - `Change Bad Event Chance - System` behaves exactly like its intel counterpart (§2.4):
    it never matters in the stock game.
  If no candidate passes, there is no event.
- **Applying** (confirmed: binary): with `Time Till Completion` 0 the effect (§2.3, with no
  source) applies at once and a random message from the record is sent. With N > 0 the
  event is scheduled and strikes in the event step exactly N turns later, if its target
  object still exists; otherwise it is dropped silently.
- **Event-only handlers** (confirmed: binary):
  - `Ship - Moved`: the ship is moved to a random system of the quadrant (its own
    included), at a random sector, and its orders are cleared. Amount is not used.
    - **The draws** (confirmed: binary): first the system, a roll of 1 to the number of
      systems, in the game's system order; then the sector, one single roll s from 0 to 168
      inclusive. Both use the game's ordinary between-two-bounds draw (the R[a,b] of spec 01),
      which takes exactly one number from the shared random sequence. The sector is s mod 13
      for x and s div 13 for y, the game's sector numbering y × 13 + x (spec 01 §4.1), so
      every one of the 169 sectors is equally likely, whatever it holds. The engine does the
      same.
    - **The fleet** (confirmed: binary): the ship is moved first and only then taken out of
      its fleet. A fleet's location follows any member that moves (spec 03 §9), so the
      location goes with the ship, and once the ship has left, no member is at the fleet's
      location any more. The fleet is therefore disbanded: every other member leaves it,
      normally losing its orders, and the fleet itself is deleted. The engine does the same.
  - `Planet - Destroyed`, `Star - Destroyed`, `Warp Point - Closed`: the same result as the
    stellar manipulation of that name (spec 01 §9).
  - `Planet - Population Riot`: sets the planet rioting unless the race has `Population
    Emotionless`.
  - `Planet - Population Rebel`: see §2.3.
- **`Events.txt` fields**:
  - `Type`: shares the handlers with intel (§2.3).
  - `Severity`: Low, Medium, High or Catastrophic.
  - `Effect Amount`: the handler's parameter. Messages can show the realized value with
    `[%ActualAmount]`.
  - `Message To`: None, Owner, Sector, System or All.
  - `Num Messages`, `Message Title N`, `Message N`, `Picture`: the log message.
  - `Time Till Completion`: 0 means immediate, otherwise turns until it strikes. Stock
    catastrophes take 10 to 30 turns.
- **Other modifiers** [D][H]: `Mechanoids` blocks plague. `Emotionless` blocks riots and
  anger caused by events (confirmed: binary for riots and anger).
- **OpenSE4 choice** [I]: Sector and System messages also go to the owner. A record with a
  blank text names what was hit.

## 5. Scores and history

- **Score window columns** (confirmed: binary):
  - Score and Rank (ranked across all empires).
  - Resources: this turn's production of minerals + organics + radioactives. Research and
    Intelligence: this turn's production of each.
  - Tech Levels: the sum of the levels of all areas.
  - Systems colonized, Planets, Population.
  - Units: units on planets, unit groups in space and units carried by ships.
  - Ships and Bases: counts, excluding mothballed vehicles.
- **Score formula** (confirmed: binary):

  Score = 10 × (sum of the hull `Tonnage` of every ship and base, mothballed ones excluded)
  + (minerals + organics + radioactives + research + intelligence produced this turn)
  + 200 × (sum of tech levels, each capped at its area's maximum)
  + 50,000 if the empire has researched everything (its capped level sum is at least the
  sum of the maximum levels of the areas that are allowed in the game and pass its racial
  and unique checks).

  Systems, planets, population and units do not count. "Produced" is the same per-turn
  production figure the empire's income uses (spec 02), before trade, tariffs and
  maintenance.
- **Visibility** (confirmed: binary): the "Score Display" setup option has three settings:
  own empire only; own empire plus the empires we hold Non-Aggression or better with; all
  empires. Once the game is over every score is visible. Destroyed empires are not shown.
- **Comparisons window**: plots the same statistics per turn, with gaps for periods when
  there was no treaty and the data was unknown. SE4 stores this per player in a statistics
  file, and the History window's data in an events file [M]. Both files, and a text copy of
  the log, are written for human players at the start of their end-of-turn processing
  (confirmed: binary). Each turn the statistics file gets one row for every empire whose
  score that player may see under the "Score Display" rule above, with the date and the
  score columns (confirmed: binary). So a row exists only for the turns the empire was
  visible, which makes the gaps. Both files are plain text in fixed-width columns, one
  record per line, appended each turn (confirmed: binary): a statistics line holds the
  empire's number, the date and the Score window's columns (score, resources, research,
  intelligence, tech levels, systems, planets, population, units, ships, bases); a history
  line holds the date, the other empire's number (0 for none), two flags the game always
  writes as 0, and the text (§3.4). In a statistics line every number is right-aligned
  with no separator: the empire number in 5 characters, the date as a whole number of
  tenths of a year (24001 for 2400.1) in 8, and each of the eleven columns in 12. The date
  is the one the empire's end-of-turn processing sees: advanced in a simultaneous game,
  unadvanced in a turn-based one (§8). The player's own empire always has a row.

## 6. Victory

The setup may combine any of these conditions. With none selected, nothing ends the game
automatically (confirmed: binary); the manual describes this as playing until one empire is
left [M]. The check runs once per game turn, after every empire's end-of-turn processing and
before the random event (§8) (confirmed: binary).

**Meeting a condition ends the game; the original does not name a winner** (confirmed:
binary). It announces that this is the last turn and points the players to the Scores
window, where the ranking shows the result.

| Condition | Test (confirmed: binary) | Initial value |
|---|---|---|
| Score reaches X | Some living empire has score ≥ X. | 100,000 |
| X years elapsed | The date has reached 2400.0 + X years. | 10 years |
| X % of second place | Some living empire has score ≥ (X / 100) × the score of every other living empire, compared in floating point. With one living empire the test passes. | 150 % |
| X % of tech | Some living empire's capped tech level sum (§5, all areas) is ≥ the sum of the maximum levels of the areas that are allowed in the game and pass its racial and unique checks × X / 100, compared in floating point. | 10 % |
| Peace for X years | A counter rises by 1 each turn and restarts at 0 whenever any two living empires hold a treaty worse than Non-Aggression, which includes None and "no contact". It moves only while this condition is on and the qualifier date has passed. The game ends when it reaches X years. | 2 years |
| After X years | A qualifier: while the date is before 2400.0 + X years, nothing is checked (and the peace counter does not move). | 5 years |

- **Quirk** (confirmed: binary): when "X % of second place" is enabled, its test replaces
  the results of the Score and Years tests of the same turn: those two can then end the game
  only if the "% of second place" test also passes. The tech and peace tests still apply.
- **Last empire standing** (confirmed: binary): the original has no automatic victory for
  it. In a turn-based game, when every other empire has been destroyed, the player is told
  and may keep playing. A game created with a single empire (no computer players) does this at
  once, before its first turn: the Victory picture with a line naming the empire and its
  leader, then the "All Players Eliminated" box asking whether to continue (observed, spec
  07 session 3).
- **Destruction** (confirmed: binary): an empire is destroyed when it has no populated
  planet and no ship or base (units do not count). The check runs once per turn for each
  empire: in simultaneous games right after its end-of-turn processing, in turn-based games
  when its turn comes up. Every empire in contact with it is told, all treaties with it
  return to "no contact", intel projects aimed at it are removed, and its remaining objects
  (empty colonies, units) are removed.
- **OpenSE4 choices** [I]: the game-over screen names the best score as the winner, with
  ties going to the lower empire number, while the game-over message names none. Neutral
  empires neither win nor count toward the last empire standing. When one empire is left
  in any game it is told so and plays on.

## 7. Computer player

### 7.1 Control, selection, difficulty

- **Who plays AI** (confirmed: binary): an empire is AI-controlled when it carries the
  computer-controlled mark. The mark is set by "Computer Controlled" at setup, for random
  computer and neutral players and for rebels, and during play by the Player Computer Control
  window (spec 06 §1.2.1) or the TCP/IP host's toggle (§9.4). The window also switches all 25
  ministers and every individual minister flag on (to computer) or off (to human); the TCP/IP
  host's toggle changes only the mark. A computer player is an empire with the mark and all 25
  ministers on; its ministers act on every object, whatever the individual flags.
  A human empire with all 25 ministers and every individual flag on (Ministers, Complete AI
  On) but without the mark played far worse than the computer players in three observed
  games: it stayed in its home system, its research stayed flat and it was destroyed or
  nearly so by turn 100 (observed, spec 07 session 3; why was not traced).
- **Missing orders** (confirmed: binary): in a game played on different machines (not
  Hotseat), a player whose orders are missing when the host processes the turn is played by
  the computer for that turn (§9.2). The host switches all of that empire's ministers on
  and restores the player's own minister settings after the turn. If that player ticked
  the minister option that forbids AI changes in a simultaneous game, the stand-in does
  its bookkeeping (state, scores) but changes nothing.
- **The stand-in is keyed on the file, not the mark** (confirmed: binary). Every empire
  without a valid orders file, computer players included, gets all ministers on and the
  "standing in" state for that turn, unless the game is hotseat. Quirk: an empire that is now
  computer-controlled but still has "AI should not make changes during a simultaneous game" on,
  left over from when a human played it, does only bookkeeping every turn on the host of a game
  on different machines; in hotseat it plays normally. The engine differs: `liveControl` in
  `src/game/turn_based.cpp` plays such an empire normally.
- **Ministers** hand single areas to the AI [M]. There are 25, in this order (confirmed:
  binary):
  - Global (1–11): Design, Ship Construction, Expenses, Production Output, Research,
    Intelligence, Politics, Repair, Resupply, Scrap, Retrofit.
  - Individual (12–25): Facility Construction, Transports, Carriers, Colonization, Attack,
    Defense, Exploration, Patrol, Mines/Satellites/Drones, Fleets, Stellar Manipulation,
    Ship Cloaking, Space Yard Ships, Troops. In a human empire they act only on vehicles
    and planets whose own minister flag is on. In a computer empire they act on
    everything (confirmed: binary).
  - The Expenses and Production Output ministers do nothing: their turn steps are empty
    (confirmed: binary). Ship Cloaking has no turn step of its own. Ships under it raise
    and lower their cloaks as their movement, repair and yard orders need (confirmed:
    binary). Colonies under it are decloaked before their Launch Units Remotely, Recover
    Units Remotely, Use Facility, Convert Resources and Abandon Planet orders and cloaked
    again afterwards when they can (not after Abandon Planet); for a computer player this
    is how its colonies come to be cloaked (spec 01 §6.9, confirmed: binary).
  - "Minister Style" selects a personality folder (§7.2), and "Use Race Minister Style"
    selects the race's own files instead. The style also applies to an empire marked
    "Computer Controlled" in the player setup, unless "Use Race Minister Style" is ticked.
    Random computer players always use their race's files (confirmed: binary). AI ministers
    never surrender [H].
- **Minister order** each turn (confirmed: binary):
  1. While orders are given, before movement: the AI state update (§7.2), then Politics,
     Troops, Transports, Colonization, Space Yard Ships, Carriers,
     Mines/Satellites/Drones, Fleets, Defense, Attack, Exploration, Patrol, Resupply,
     Repair, Scrap, Retrofit and Stellar Manipulation.
  2. At the start of the empire's economy step, before income: Design, Research,
     Intelligence, Facility Construction, Ship Construction, then Facility Construction a
     second time. The first Facility Construction pass is skipped on every fifth turn.
- **Random AIs** (confirmed: binary):
  - "Random Computer Players" and "Random Neutral Players" each add players. One
    Low/Medium/High "Number of Computer Players" level serves both.
  - Each kind draws a count uniformly in [`Minimum … Player <L> Setting`, `Maximum … Player
    <L> Setting`], with the `Computer` or `Neutral` keys. Computer players are added
    first, and the game never exceeds 20 empires.
  - A game set up in the multiplayer lobby uses fixed ranges instead: Low 1–3, Medium 3–7,
    High 6–10, with at most 5 neutral players.
- **Random race choice** (confirmed: binary):
  - Races already used by an empire in the game are never drawn.
  - A computer player first picks a personality group. For each group N from 1 to `Random
    Player Personality Groups` (at most 10), its share is round(100 × E_N / E), half to
    even. E_N counts the empires already in the game whose race's `AI_Settings` has
    `Personality Group := N`, and E counts all empires including the new one. Among the groups whose share is
    below `Random Player Personality Group N Percent`, the one with the smallest share
    wins; ties go to the lowest N. So groups fill up toward their target percentages; no
    dice are rolled.
  - The race is drawn uniformly from the unused races of that group. With no qualifying
    group, or no unused race in it, it is drawn uniformly from all unused races. Neutral
    players always draw uniformly from the unused neutral races.
  - So with the stock settings (four groups at 30, 30, 30 and 10 %) and the Terran (group 3)
    as the only other empire, four computer players always come from groups 1, 2, 4 and 1,
    in that order: one of the two group-4 races in every game. Observed (2026-10-02, spec 07
    "Resources, ships and colony losses under a debugger"): five games of the original drew
    their four races in exactly that group order. The engine matches. The races' output
    effects differ a lot (from +25 to −5 % on minerals), so the draw alone moves an empire's
    resource output by up to a quarter from game to game.
- **Random race build** (confirmed: binary):
  - A random computer player's race uses the `AI_General` "Race Opt" set of the game's
    racial-point level: Race Opt 1 for Low (2000 points), 2 for Medium (3000) and 3 for
    High (5000). With None (0 points) no set is used.
  - Characteristics are applied in the listed order, each only while the points spent so
    far are still below the budget. A characteristic that pushes the total over the budget
    goes back to 100.
  - Advanced traits are then taken in order: each one whose cost fits the remaining points
    is added, and one that does not fit is skipped, so later, cheaper traits can still be
    added.
  - Culture, planet type and atmosphere come from `AI_General`. Every pair is allowed
    except a Gas Giant with no atmosphere; the data set is not consulted. While the pair is
    not allowed, the atmosphere is redrawn uniformly among the five (None, Methane, Oxygen,
    Hydrogen, Carbon Dioxide) and the type among the three (Rock, Ice, Gas Giant), with no
    limit on tries.
- **Neutral empires** stay in their home system, never use warp points and cannot be
  played by humans [M]. Neutral is a per-empire flag. Its AI ignores every system except
  its home and claims only its colony systems, so its ships never leave (confirmed:
  binary).
- **Information**: the AI gets no information a human would lack [M].
- **Difficulty** (confirmed: binary): "Computer Player Difficulty" has three levels, Low,
  Medium and High, and defaults to Medium. The level is stored per empire:
  - random computer players get the chosen level;
  - empires marked "Computer Controlled" in the player setup always play at Medium, and
    so do a human empire's ministers;
  - an empire founded by a planet's revolt takes the highest level among the computer
    empires, or Medium when there is none.

  Its effects are listed where they apply (§7.2–7.5).
- **Computer Player Bonus** (confirmed: binary): None (the default), Low, Medium or High.
  It never applies to human empires. For a computer empire:
  - each turn's income of minerals, organics, radioactives, research points and
    intelligence points is multiplied by 1, 2, 3 or 5 and rounded (half to even). This
    happens after tribute to other empires is paid, and before storage limits apply;
  - every construction queue rate, for planets and ships alike, is multiplied by 1, 1.5, 2
    or 3 and truncated. This comes after the racial, culture and population modifiers.

  The planets' production percentages are not changed.
- **Team Mode**: every AI is locked in Partnership with the other AIs against the humans
  [M][H]. In the politics rules (confirmed: binary), computer empires always propose and
  accept Partnership among themselves, always want war with human empires, and refuse
  everything a human empire offers (§7.4). A human empire's Politics minister treats the
  sides the same way.

### 7.2 File lookup

There are twelve tables, each in a file named `<prefix>_AI_<Name>.txt`: Anger,
Construction_Facilities, Construction_Vehicles, DesignCreation, Fleets, General,
Planet_Types, Politics, Research, Settings, Speech and Strategies. One rule finds every
table (confirmed: binary):

1. An empire with a minister style reads `Ai/<Style>/<Style>_AI_<Name>.txt`. Any other
   empire reads its race's file, `<race folder>/<Race>_AI_<Name>.txt`. The race folder is
   `Pictures/Races/<Race>`, or `Pictures/RaceNeutral/<Race>` for a neutral race.
2. If that file does not exist, it reads `Ai/Default_AI_<Name>.txt`.

There is no fallback from a style folder to the race folder. A race or style folder may
replace any table, including those the stock install ships only as `Default_` files.
Mod folders apply as for other data files.

What the stock install ships:

| Folder | Files |
|---|---|
| Player race folder | Anger, DesignCreation, Fleets, General, Politics, Research, Settings |
| Neutral race folder | Anger, Fleets, General, Settings |
| `Ai/` | `Default_` versions of all twelve tables. `Strategies` is reloaded when a game is loaded [H]. |
| `Ai/Aggressive`, `Ai/Defensive`, `Ai/Neutral` (minister styles) | Anger, General, Politics, Settings, Speech |

Most tables filter their rows by **AI state** [D]. Everything below is (confirmed:
binary).

| # | State | Reached in play |
|---|---|---|
| 1 | Exploration | yes, the starting state |
| 2 | Infrastructure | yes |
| 3 | Prepare for Attack | yes |
| 4 | Attack | yes |
| 5 | Secure Holdings After Attack | yes |
| 6 | Incursion | never |
| 7 | Prepare for Defense | never |
| 8 | Defend (Short Term) | yes |
| 9 | Defend (Long Term) | never |
| 10 | Not Connected | yes |

The tests that would lead to Incursion and Prepare for Defense always fail, and nothing
leads to Defend (Long Term). Table rows that only list those states never apply.

**Memory.** A new computer player starts in Exploration. Between turns the AI keeps:
- its state and the turns spent in it (reset to 0 on every change);
- up to 3 target systems;
- a staging system and a system to secure;
- a list of systems to defend;
- an after-attack timer. The timer is set to 1 when an attack ends, grows by 1 at the end
  of each turn while it is not 0, and is reset to 0 by every state except Infrastructure.

The update runs once per turn, before the ministers act (§7.1), for computer empires and
for humans whose ministers are active.

**Hostile.** The military AI treats an empire as hostile when the treaty with it is below
Non-Aggression: War, Non-Intercourse, None, or not yet met.

**Territory** (confirmed: binary). The territory is the empire's set of claimed systems,
the same set the Borders display shows (spec 01 §6.8). At the start of the game an empire
claims its home system only. A computer empire's Politics minister rewrites the set each
turn, first thing in its run:
1. every claim is cleared;
2. every system holding one of the empire's colonies is claimed;
3. unless the empire is neutral, every other system one warp jump from such a system (over
   every link of the map) is claimed too, except the home system of a computer-controlled
   empire and the systems the empire has agreed to avoid (§7.4). These two exclusions apply
   only to the systems added in this step: a system holding one of our colonies is always
   claimed.

A neutral empire therefore claims only its colony systems, and it ignores every system
except its home. A System item of an accepted trade (§7.4) moves the claim on that system
from the giver to the receiver at once; it lasts until the receiver's next rewrite
(confirmed: binary). Observed under a debugger (2026-10-02, spec 07 "Pace observed under a
debugger"): in 1,463 of 1,485 start-of-turn updates of three 100-turn games the claimed
systems were exactly those the rule above gives from the colonies and warp links of the
turn before; each of the other 22 held one system more for that turn only, as a traded
system would. The Politics minister runs after the state update and after the lists
below are built (§7.1), so each turn's lists and transitions use the territory claimed
during the previous turn: a new colony's system and its neighbours count only from the
second start-of-turn update after the colony is founded. A human empire keeps the claims its
player makes unless its Politics minister acts. OpenSE4 follows this since 2026-10-02: the
claims are rewritten (`ai::claimTerritory`) after the state update and the political step,
first thing before the Politics minister decides, so a trade it accepts then moves a claim
until the receiver's next rewrite; the ministers after Politics read the territory only
through the lists (question 60), and the engine hands them the claims the state update used
(`planOrdersAfterPolitics`). It used to claim after the empire's start-of-turn ministers,
which wiped a system received in the empire's own Politics run at once.

**Jumps** (confirmed: binary): every AI jump count uses every warp link of the map, known
or not, and an unreachable system counts as 999 jumps. That covers the territory's "one
warp jump", the jumps from home that order the lists, the staging system, the 4-jump test,
Secure Holdings' neighbours, the reachability of the Not Connected test, the nearest fleet
or colony ship, recruiting and drone distances.

**Strength.** Each turn the AI adds up a strength per system and per empire. It counts
every owned object, including those it cannot see:
- each object adds its rating + 1;
- a ship's rating (confirmed: binary): W is the sum, over its undamaged weapon parts, of
  each weapon's best damage at any range; B is its `Boarding Attack` ability. If W + B > 0,
  the rating is W + B + (`Shield Generation` + `Phased Shield Generation` + `Planet -
  Shield Generation`) / 2, kept as a fraction; otherwise 0. Every fighter stack in its
  cargo then adds count × the fighter design's summed best weapon damage, even when W + B
  is 0;
- a unit group's rating is its number of units;
- planets rate 0, so each planet adds 1.

A system's hostile strength is the total over the hostile empires we have met.

**Lists built each turn.** Each hostile object or planet enters a list only if the AI
notices it. A Low-difficulty AI notices each one with a 90 % chance per turn; Medium and
High always notice.
- **Enemy in territory** (confirmed: binary, question 54):
  - every ship and unit group, armed or not, other than a mine field, that an empire below
    Non-Aggression with us owns (an empire we have not met included), that is in a
    territory system, and that we detect at the moment of the update by the rule of spec 01
    §6.3 (we have explored the system and our sensors there reach the object's
    obscuration). An object seen on an earlier turn but not now does not count;
  - every colony with population of such an empire on a planet (not an asteroid field) of
    a territory system we have explored, whether or not we see it now and whether or not
    we have met its owner.
- **Enemy nearby**: hostile mine fields in our territory that we detect, by the same rule.
  That is all it contains. Stock mines can never be detected (spec 01 §6.3), so with the
  stock data this list stays empty, and only the frontier test below lets a computer
  player move from Exploration or Defend (Short Term) to Infrastructure.
- **Attack candidates**: planets of other empires in systems we have explored, each one
  noticed as above and kept only when we could settle a planet of its kind (the
  colonization test of the colonization targets below: we have the colonization technology
  for its surface type, and the game options "only breathable" and "only home planet type"
  allow it) or when its owner is below None with us: at War, at Non-Intercourse or not yet
  met (confirmed: binary). So a colony of an empire at None or better whose surface we
  cannot settle yet (for most races at the start, every planet but their own surface type)
  is no candidate: it is never a target, never makes a Prepare for Attack, and adds
  nothing to the anger's attack locations. Only a kept candidate also adds its owner's
  rating + 1 a second time to the strength of that system. They are ordered by fewest jumps
  from our home, then highest anger toward the owner, then highest value. The value
  (confirmed: binary) is the ratings (without the + 1) of every owned object in the
  planet's sector that is not ours, whoever owns it, plus the planet's defence: its
  colony's `Planet - Shield Generation` total / 5, plus its population in millions div 100,
  plus, for every unit stack in its cargo, count × that unit design's summed best weapon
  damage. OpenSE4 follows this since 2026-10-02 (`assess`, `ai.cpp`; the settle test is
  `canSettle`, and the second count goes to the hostile strength of the system when we have
  met the owner); it used to keep every noticed planet of another empire, so its computers
  found attack candidates, and left Infrastructure for Prepare for Attack, more often.
- **Exploration frontier** (confirmed: binary): the warp points of explored systems whose
  far system we have not explored. Whether we know where the link leads plays no part: a
  warp point into a system we have explored by another route is never on the frontier,
  even when none of our ships has crossed it. A point is free when none of our ships is
  already headed for it. "Unexplored space borders our territory" means that some frontier
  point lies in a territory system. The same list feeds the explorers, the Not Connected
  test and the Open Warp Point gate (§7.5). OpenSE4 follows this since 2026-10-01; it used
  to count every warp point whose link was unknown as well, which changed the computer
  players' pace far less than expected (question 53).
- **Defend list** (confirmed: binary): the enemy-in-territory entries are kept one per
  (system, sector, owner). An entry's threat is the sum of rating + 1 over the owner's
  noticed vehicles in that sector; a noticed populated colony adds the ratings (without the
  + 1) of every object in its sector that is not ours, its own planet included at rating 0,
  so a colony with nothing else in its sector adds 0 (confirmed: binary; observed
  2026-10-02: 94 % of the colony entries of two games had the value 0). The engine differs:
  `assess` (`ai.cpp`) adds 1 for the colony itself (without it, 17 of our 24 games played
  out identically, scratch run). The entries are ordered by:
  1. fewest jumps from home;
  2. our colonies' maximum population in that sector (spec 02 §2, domed capacity and the
     storage trait included), highest first;
  3. entries in a sector that holds a planet (not an asteroid field) before the others,
     the weaker threat first among them;
  4. then the threat itself: strongest first for the defend list and for the fleets; for
     the Defense minister's own choice weakest first, or strongest first when the ship is
     in a fleet.

  The defend list is the distinct systems of these entries in this order, at most `Maximum
  Systems to Defend at a Time`.

**Whose lists the economy step reads** (confirmed: binary; observed 2026-10-02). There is
one set of these lists for the whole game. An empire's start-of-turn step (§7.1 group 1)
always builds its own, replacing the set it finds, and leaves it in place. Its
economy-step ministers (§7.1 group 2) build a set only when none is left, and remove the
set when they finish. So the first empire, in player order, whose economy-step ministers
run uses the set left by the last start-of-turn step of the turn, which belongs to the last
empire in player order whose start-of-turn ministers ran; every later empire builds its
own. Of these lists the economy step reads only the colonization targets: the colony-ship
type that `Colonizer` entries build (§7.5) and the neutral empire's colony-ship test. Under
a debugger, in a game of five computer players the economy step of player 1 found the set
of player 5 on every turn, and players 2–5 built their own. OpenSE4 follows this since
2026-10-02 (`TurnContext::aiColonyTargets`): the colonization targets of the lists built by
an empire's start-of-turn ministers stay in place, and the next economy step whose ministers
run plans with them and removes them. Whether the reading empire can settle each target is
its own test, and the neutral empire's colony-ship test, which no rule of this spec
describes, is not modelled (question 64). The set lives within one processing of the turn:
a human's turn-based ministers, whose start and end of turn can come in different calls,
build their own at the end.

**Tests used by the transitions**

- **Not Connected test**, all three must hold:
  1. the AI knows at most 10 uncolonized planets it could settle and has not yet targeted;
  2. no free frontier point is left;
  3. at most 60 % of all systems other than home can be reached from home by warp, over
     every link (so in a galaxy where every system is linked the test never holds).
- **Attack gap**: the after-attack timer is 0, or above `Turns to Wait until next attack`
  (1 in Team Mode).
- **Contact**: we have met at least one living empire.

**Transitions**, checked in the order listed:

- **Exploration**
  1. An enemy is in territory → Defend (Short Term).
  2. The Not Connected test holds → Not Connected.
  3. We have contact, and either an enemy is nearby or no system of our territory borders
     unexplored space → Infrastructure.
  4. Otherwise stay. An AI that has met nobody never leaves Exploration.
- **Infrastructure**
  1. An enemy is in territory → Defend (Short Term).
  2. The attack gap allows it and an attack candidate has a hostile owner → Prepare for
     Attack.
     - The targets are the systems of the first such candidates, up to 3.
     - The staging system is the system of our territory, other than the first target,
       with the fewest jumps to the first target.
  3. The Not Connected test holds → Not Connected.
- **Prepare for Attack**
  1. No targets or no staging system → Infrastructure.
  2. An enemy is in territory → Defend (Short Term).
  3. Drop every target whose hostile strength exceeds 5 × our strength in the staging
     system. If none is left → Infrastructure.
  4. Once 5 full turns have been spent in this state (from the 6th turn; the counter is 0
     on the first): add up, for each target, our strength in every system within 4 jumps
     of it, the target included, so a system near two targets counts twice. Unless the sum
     exceeds 3 × the targets' total hostile strength → Infrastructure.
  5. No hostile-owned candidate planet is left in the target systems → Infrastructure.
  6. Our strength in the staging system exceeds the first target's hostile strength →
     Attack.
  7. More than 20 turns in this state → Infrastructure.
- **Attack**
  1. Steps 1–4 as in Prepare for Attack. Losing every target at step 3 also starts the
     after-attack timer.
  2. Every target has zero hostile strength → Secure Holdings, securing the first target.
  3. Step 5 as in Prepare for Attack.
  4. More than 20 turns in this state → Infrastructure, and the after-attack timer
     starts.
- **Secure Holdings**
  1. An enemy is in territory → Defend (Short Term).
  2. Hostile strength returns to the secured system → Attack, with that system as both
     target and staging system.
  3. Our strength there exceeds the larger of 10 and the strongest hostile strength in
     any system one jump away → Infrastructure, and the after-attack timer starts.
- **Defend (Short Term)**: stay while any enemy is in territory. Then go to
  Infrastructure if an enemy is nearby or no unexplored space borders our territory,
  otherwise to Exploration. The state has no timer of its own (question 53): the turns
  spent in it play no part, and the first update whose enemy-in-territory list is empty
  ends it. Since the update runs once per turn, before the empire's ships move, a spell
  lasts as many turns as there are consecutive start-of-turn updates that find a detected
  hostile ship or unit group, or a populated hostile colony, in the territory.
- **Not Connected**: back to Infrastructure as soon as the Not Connected test fails.

### 7.3 Anger (`AI_Anger`)

Everything in this section is (confirmed: binary).

**Storage**
- Each empire keeps an anger value toward every other player: a whole number from 0 to
  100, starting at 50.
- Every single change is clamped to 0–100 at once, so the order of the terms below
  matters.

**Mood.** The Empires window shows another empire's anger toward the viewer as a word:

| Anger | Mood |
|---|---|
| below 10 | Brotherly |
| 10–19 | Amiable |
| 20–29 | Receptive |
| 30–39 | Warm |
| 40–59 | Moderate |
| 60–69 | Cool |
| 70–79 | Displeased |
| 80–89 | Angry |
| 90–100 | Murderous |

**When.** Anger changes in the empire's political step, which runs only while its Politics
minister is on: always for computer players, and for humans whose Politics minister is on.
- The step does nothing when a host covers a missed turn for a player who forbade AI
  changes (§7.1).
- It updates anger only toward living empires we have contact with.
- The anger table comes from the `_AI_Anger` file found by §7.2.
- **What it counts**: the entries of the empire's own log that no earlier political step of
  this empire has counted, among those dated this turn or the turn before (it reads the log
  from the newest entry back). Afterwards every entry of the log is marked as counted. In a
  simultaneous game that is the previous turn's battles and reports plus the messages
  delivered at the start of this turn's processing (§8 step 2). In a turn-based game it is
  everything logged since the empire's previous political step: the rest of its own last
  turn, the turns of the players after it, and the turns of the players before it in the
  current game turn.
  - The mark is the same as the Log window's "read" mark (confirmed: binary). When a human
    opens the Log window, every entry is marked, so those entries are never counted; in a
    simultaneous game the marks reach the host with the orders. The step stops reading
    after the first entry dated before the turn before, and counts that entry too when it
    is unmarked.
  - An empire whose step does not run (its Politics minister off, or a missed turn whose
    player forbade AI changes) marks nothing; its next step counts what is still unmarked
    in that window.
  - Battles are counted from the combat entries of its own log (one per battle, listing the
    participants), messages from the message entries of its own log.

**Territory.** The political step first recomputes the claimed territory (the same
territory §7.2 uses):
- every system holding one of our colonies;
- unless we are neutral, every system one warp jump from such a system;
- except the home system of any computer empire, and systems we agreed to leave by
  accepting a "remove ships", "remove colonies" or "leave planet" demand (§7.4).

Human players claim systems by hand; their home system starts claimed. The step runs
for every empire whose Politics minister is on, so a human who turns that minister on
gets the same claims each turn (spec 06 §7 Q47, confirmed: binary).

**Per-turn update** toward empire X, in this order:

1. **Combat.** For each combat this turn involving X, while X's treaty with us is below
   Non-Aggression, apply one `Combat …` value:
   - we are *Attacking* when we are the "current player" at the moment the battle is fought,
     otherwise *Defending*; where it happens does not matter (confirmed: binary). In a
     turn-based game the current player is the one whose turn it is, so a battle in our own
     turn is Attacking and one in anyone else's turn Defending. In a simultaneous game the
     battles are fought after each day's moves, and the current player is still the
     highest player number of the game (destroyed empires included), left over from the
     start-of-turn loop: that empire counts every battle it fights as Attacking, every other
     empire as Defending;
   - *Won*: we had units there, some survived, and no other side has survivors;
   - *Lost*: we had units, none survived, and another side has survivors;
   - anything else is a *Stalemate*.

   Ground combat changes nothing.
2. **Stellar manipulation.** Each report this turn that X destroyed a planet, destroyed a
   star (making a nebula is reported the same way) or created a black hole adds 2 ×
   `Combat Defending Lost`. The report is an entry of our own log, counted like the
   others: when the manipulation happens, it is logged, naming the empire responsible. For
   Destroy Planet it goes to every empire that, once the planet is gone, still has a ship,
   base, colony, or fighter, satellite or drone group (not mine fields) in that system; the
   destroyed colony does not count. For Destroy Star, Create Nebulae and Create Black Hole it
   goes to every empire that lost a colony or any object of its own in the system to the
   shockwave, mine fields included, the culprit too (a report naming oneself never counts)
   (confirmed: binary, question 50).
3. **Intelligence.** Each successful project by X against us adds `Intelligence Against
   Us`, but only when we learn who did it. After a success, the target is told the culprit
   with a 1 in 5 chance. Blocked attempts and counter-intelligence projects add nothing.
4. **Messages.** At most one message from X counts per turn: the earliest received this
   turn. It adds the `Receive <type>` value of its message type. There are 38 types, in
   file order:
   - general message;
   - treaty: propose, accept, refuse, counter, break treaty, declare war;
   - trade: propose, accept, refuse, counter;
   - gift and tribute: give gift, accept gift, refuse gift, offer tribute, accept
     tribute, refuse tribute;
   - requests: want a gift, want a tribute, demand surrender, remove ships from a system,
     remove colonies from a system, leave planet, stop hostile actions against an empire,
     break a treaty with an empire, declare war on an empire, make peace with an empire,
     support us against an empire, attack an empire in a system, attack a planet, stop
     espionage, stop sabotage, stop attacks in a system, and the generic
     demand/request/warn;
   - surrender, grant independence to a colony, accept demand, refuse demand.
5. **Decay.** Add `Regular Decrease`.
6. **Mega Evil Empire.** Add `Mega Evil Empire` if X is the MEE as this empire sees it
   (§7.6).
7. **Team Mode.** −20 toward an empire on our side, +20 toward one on the other side.
8. **Promise.** −20 for a promise about X, once. Accepting a "stop hostile actions
   against an empire" demand records a promise about the empire the demand names,
   whoever sent it (only half the time, §7.4). Each accepted demand adds a promise, and
   each turn uses up one promise about X, so two promises about X give −20 on two turns.
   All promises are forgotten every 10 turns (confirmed: binary).
9. **Attack locations.** Count N:
   - +1 for each colony of X, in a system we have explored, on a planet we could colonize.
     That means we have the colonization technology for its planet type, and the
     "breathable atmosphere only" and "home planet type only" game options allow it;
   - +3 for each system of our territory holding a colony of X;
   - +10 for each system outside our territory, holding a colony of X, within 2 jumps of
     X's territory. This term counts only while we are not at war with any empire whose
     score is above a quarter of ours.

   Then adjust N:
   - If X is a friend (Non-Aggression or better), N = 0 unless `Get Angry Over Allied
     Colonizable Planets` is on. Otherwise N = 0 unless `Get Angry Over Enemy Colonizable
     Planets` is on.
   - N = N × pct / 100, truncated toward zero. pct is `Percentage of Allied Planets to
     consider as Attack Locations for Anger` for a friend, and the Enemy key otherwise.
   - Add `Per Attack Location` × N.
10. **Objects in our territory.** Count S, but only for X below Non-Aggression:
    - X's visible ships and fighter, satellite and drone groups in our territory (mine
      fields excluded);
    - plus X's populated colonies there.

    Add `Per No Treaty Ship` × S when the treaty is None, or `Per Enemy Ship` × S for War
    and Non-Intercourse. `Per Ally Ship` is never used.
11. **Floor.** Anger = max(anger, `Minimum Anger`).

**Difficulty.** At Low difficulty each hostile object or colony is left out of steps 9 and
10 with a 10 % chance per turn (§7.2). There is no other difficulty effect.

**Other changes**
- Declaring war on X sets anger toward X to 100 (§7.4).
- When an empire is eliminated, every survivor resets its treaty with it to no contact and
  its anger toward it to 0.
- Nothing else changes anger: not contact, treaties or gifts (beyond their `Receive`
  values), or surrender.
- Personalities only change the table's numbers [D].

### 7.4 Politics (`AI_Politics`)

Everything in this section is (confirmed: binary) unless marked otherwise. Politics uses
neither the difficulty level nor any `AI_Settings` value, and neutral empires take part
like the others.

**Terms**

- **Treaty order**, numbered 1–11: War, Non-Intercourse, (no contact), None,
  Non-Aggression, Subjugation, Protectorate, Trade Alliance, Trade and Research Alliance,
  Military Alliance, Partnership. "Better" means higher in this list.
- **Friend**: Non-Aggression or better. **Enemy**: None or worse.
- **P** (score ratio) = their score / our score × 100, truncated toward zero, or 0 when
  our score is 0 or less. Every fractional result in this section is truncated.
- **Wars**: the empires we are at war with whose score is greater than our score / 4
  (integer division). The empire being judged counts if we are at war with it.
- **First 50 turns**: the first 50 turns of the game.
- **Turns since war** with an empire: +1 each turn, 0 while at war with it; it starts at
  999.
- **Treaty age**: turns since the treaty with that empire last changed. A change from one
  treaty at Trade Alliance or better to another does not reset it.
- **Team Mode**: a *team mate* is on the same side (both computer or both human), a *team
  enemy* on the other side.
- **Stronger** means P ≥ the rule's `Stronger Player` percent. **Weaker** means P < its
  `Weaker Player` percent (strict). Both can hold at once, and then both amounts apply.
- **MEE**: the Mega Evil Empire as this AI sees it (§7.6).

**Tone**: Demanding if P ≤ `Score Percent For Demanding Tone`, else Pleading if P ≥ `Score
Percent To Pleading Tone`, else Neutral. Demands the AI starts on its own always use the
Neutral tone.

**Thresholds.** Each rule turns the table into a threshold T for one other empire X.

- **Accept Treaty**, for an offered treaty t, in this order:
  1. T = `Accept Treaty Base Anger Level`.
  2. If t is Trade Alliance or better, add `Anger Modifier Per Higher Treaty Level` × (the
     number of steps t is above Trade Alliance). The current treaty does not matter.
  3. Raise T to at least `Minimum Anger Chance`. It is a floor, not a probability.
  4. Add `Anger Modifier Per Other Wars` × wars.
  5. For Trade and Research Alliance, Military Alliance and Partnership only: if the
     treaty age is below `Minimum Time From Last Treaty`, set T to −1. The later steps
     still apply.
  6. Add `First 50 Turns Modifier` during the first 50 turns.
  7. Add the stronger and weaker amounts.
  8. If there is an MEE: T = −1 when X is the MEE, otherwise add 60.
  9. Subjugation needs P ≥ `Score Percent To Accept Subjugation Treaty`, and Protectorate
     needs P ≥ its own percent. Otherwise T = −1.
  10. Team enemy: T = −1. Team mate: T = 101.
- **Propose Treaty**:
  - T = base + per other wars × wars + first-50-turns modifier + the stronger and weaker
    amounts.
  - MEE: T = −1 toward the MEE, otherwise add 70.
  - Team rules as for Accept.
- **Break Treaty**:
  - T = base + per other wars × wars.
  - Stronger and weaker use Break's own percents but add the Declare War amounts. This is
    a quirk of the original; the stock files give both rules the same amounts.
  - MEE: subtract 20 toward the MEE, otherwise add 60.
  - Team rules as for Accept. There is no first-50-turns key.
- **Declare War**:
  - T = base + per other wars × wars, plus the stronger amount (skipped when X is the MEE)
    and the weaker amount.
  - MEE: subtract 40 toward the MEE, otherwise add 60.
  - Team rules as for Accept. There is no first-50-turns key.

Accepting needs T > anger. Proposing needs anger ≤ T. Breaking and declaring war need
anger ≥ T.

**Each turn**, for every empire X in contact, in player order:

- With a 50 % chance the AI first checks "wants war" and then "wants to break". If either
  holds, it takes the initiative (below) instead of answering X.
- Otherwise it answers X: only the newest political message from X in its answer window
  (below), whatever its type. Older ones are never answered.
- If it sent X nothing, it takes the initiative. In simultaneous games it does not while a
  political message from X of any type, dated after the date − 2, waits. This test also
  follows an initiative of the 50 % branch that sent nothing, so the initiative can run
  twice in one turn (confirmed: binary).
- Each check of "wants war" or "wants to break" rolls again, and the initiative checks
  them again. The first check that finds a war or break queued by an accepted demand uses
  it up, so a queued war can end without a declaration when a later roll fails
  (confirmed: binary).

**Answer window** (confirmed: binary). Every delivered message becomes a political entry in
the recipient's log, dated at delivery. In a simultaneous game the messages from the
players' orders (§8 step 2) carry the date before it advances, and those a computer player
sends during its start-of-turn step carry the advanced date. The AI keeps no "answered"
mark; it looks only at the single newest political message from X in a window:
- Turn-based: messages dated after a cut-off, which is the date − 1 when X's player number
  is lower than ours and the date − 2 otherwise. That is exactly what X sent since our
  previous turn.
- Simultaneous: only messages dated exactly the date the ministers see − 2. So a message
  from a player's orders is answered at the next turn's processing, and a computer
  player's message two processings later.

If that newest message gets no reply (a General message, a generic demand, request or
warning, or a reply whose speech pool is empty), nothing from X is answered that turn. The
windows never overlap, so no message is answered twice.

**Wants war with X**
- Never when already at war with X or X is a team mate. Always when X is a team enemy.
- Otherwise, when the war list (§7.4 "Demand lists") holds X: one entry for X is used up,
  and the answer is yes (confirmed: binary).
- Otherwise a 50 % roll must pass (no roll in Team Mode), then war when anger ≥ T.

**Wants to break** the treaty with X: only when the treaty is Non-Aggression or better. It
follows the same pattern as war, with the Break Treaty threshold and the break list.

**Initiative**, the first that applies:

1. Declare war. Anger toward X becomes 100. Against the MEE, the message text is a random
   `Mega Evil Declarations` line.
2. Break the treaty.
3. Propose a treaty.
4. Otherwise, with a 25 % chance, a demand or request (below).

**Propose Treaty**

- The chance is `Propose Treaty Percent Chance Per Turn`, tripled while an MEE exists and
  X is not the MEE. There is no roll in Team Mode.
- Never while the AI is someone's subject.
- Blocked while turns since war < `Turns Since Last War Before Friendly Treaty`. This
  gate applies only if the AI or X is human, and is lifted in Team Mode or while an MEE
  exists.
- Needs anger ≤ T. When anger > 70, there is a further 50 % chance to drop the proposal.
- A team mate, or an accepted "make peace" request, forces a proposal. If no threshold was
  computed in that case, T = 50. The request counts when X is neither a team mate nor a
  team enemy, the AI is not a subject and the peace list (§7.4 "Demand lists") holds X:
  one entry for X is then used up, even when no treaty qualifies and nothing is sent
  (confirmed: binary).
- **Which treaty**:
  - Walk the `Propose Treaty Type N` entries in file order.
  - An entry qualifies if its treaty is at most `Highest Allowed Treaty`, better than the
    current treaty, and anger ≤ T − its `Anger Level Below Computed`.
  - The *last* qualifying entry is sent. With the stock best-first lists the AI climbs one
    step at a time.
  - A team mate is always offered Partnership. Nothing is sent when no entry qualifies.
- Proposals are made even while at war.

**Answering a treaty proposal or counter-proposal**

- No answer at all while the AI is a subject.
- Team mate: yes. Team enemy: no.
- No when the treaty is above `Highest Allowed Treaty`.
- No when turns since war < `Turns Since Last War Before Friendly Treaty`, whether human
  or not.
- No when X is the MEE.
- Otherwise yes exactly when T > anger. There is no randomness.
- After a refusal, with a 25 % chance, the AI counter-proposes the treaty one step lower
  (never below Non-Aggression), if that one would pass the same test.

**Trades**

- Refused when the AI cannot give what is asked (items it does not have or know), with
  one of three stock replies.
- Refused when the request contains "any …" items. The AI may then counter: it replaces
  each with a random concrete item and, with a 50 % chance, offers that if the value test
  passes.
- Otherwise accepted when value received ≥ value given × pct / 100 (truncated), where
  pct is `Accept Trade From Friend …` or `… Enemy …`. Every treaty item in either package
  must also pass the accept rule.

**Item values**, for the receiving side:

| Item | Value |
|---|---|
| Resources | 1 per 1000 units |
| Technology | (receiver's level + 1) × the area's level cost, only if the giver is ahead and the receiver can research it; else 0 |
| Planet | trunc(worth / 100000), with the planet's worth below |
| Ship | 100 × its scrap value (the three resources added up; spec 03 §15: `Scrap Ship Percent Returned`, raised by `Resource Reclamation` in its sector). A ship without weapon parts counts each resource divided by 4, truncated. A mothballed ship, or one without an owner, a design or a position, counts 0 |
| Unit | 100 × the units' scrap value at `Scrap Ship Percent Returned` (the ship percentage, not raised by reclamation) |
| Star chart | 20000 if the receiver lacks it |
| Treaty | its number in the treaty order × 100000 |
| Communication channel | 50000 |
| "Any …" item | 0 |
| System | 100,000 × the number of planets in it (asteroid fields excluded, colonized or not) whose type and atmosphere the receiver could colonize (it has the colonization technology, and the breathable-atmosphere and home-type options allow it), but only when the giver claims the system; otherwise 0 |

A planet's **worth** (confirmed: binary) is the sum of its three resource values × 1000 (× 1
in finite-resource games), plus, when it is colonized, 100 × its population in millions,
1,000,000 per facility, 10,000 per unit of cargo (each million people and each unit) and
1,000,000,000 for a capital. Items that are no longer valid (lost since the offer) count 0.

**Gifts and tributes received** are accepted when:
- anger ≤ `Max Anger Level for Accept a Gift` (or `… Tribute`);
- every treaty item inside passes the accept rule.

Team mates are always accepted, and team enemies always refused.

**Requests for a gift or tribute**

- Accepted when P ≥ `Score Percent To Accept …` and the friend or enemy `Will Accept …`
  flag allows it.
- Anger must also be ≤ `Gift/Tribute to Friend/Enemy Max Anger`. Team enemies are
  refused.
- The AI then builds a package worth V = `… Value Base to Friend/Enemy`, plus (P × `…
  Per Percentage Greater Score` − 100) when P > 100.
- It adds the requested items in order, a random concrete item for each "any" item, until
  the package reaches V. A concrete requested item goes in even when the AI cannot hand
  it over; it just adds no value (confirmed: binary).
- A package that ends up empty (V is 0 or less, or every requested item is an "any" item
  with nothing to offer) counts as a refusal (confirmed: binary).
- The game option that allows gifts and tributes is never read by the computer player: it
  only limits which message types a human can pick. With it off, an accepted request is
  still answered with the gift or tribute, and its items move when it is accepted
  (confirmed: binary).
- The AI never gives or asks for gifts or tributes on its own.

**Other demands and requests**

- Accepted when P ≥ the matching `Score Percent To Accept …` and its flag allows it.
  Team rules apply.
- A surrender demand is considered only when Allow Surrender is on and the empire is
  computer-controlled. A human's ministers never surrender. Allow Surrender (confirmed:
  binary) is the seventh check box of the Game Settings tab, on by default; with it off a
  Surrender message does nothing at all.
- An accepted demand is carried out only with a 50 % chance, decided before the reply is
  sent. It is carried out even when no Accept Demand reply is sent because its pool is
  empty, as in the stock files (confirmed: binary):
  - remove ships or colonies, or leave a planet: the system is marked to avoid. Colonies
    are never abandoned;
  - declare war on, support against, break with or make peace with a third empire: one
    entry naming that empire is added to the matching demand list (below);
  - attack an empire in a system, or attack a planet: that system becomes an attack target;
  - stop espionage or sabotage: intelligence projects against the requester are cancelled;
  - stop hostile actions against an empire: a promise about the empire the demand names,
    not the requester (§7.3 term 8);
  - stop attacks in a system: nothing happens.
- **Demand lists** (confirmed: binary). Each computer empire keeps a war list (fed by
  "declare war on" and "support against"), a break list ("break with"), a peace list
  ("make peace with") and the promises of §7.3 term 8. Each is a list of empire numbers
  that keeps duplicates: every carried-out demand adds one entry, for the empire the
  demand names, so two accepted demands about the same empire give two entries. Each
  check that finds an entry for X uses up one entry: "wants war with X" and "wants to
  break with X" above, the treaty proposal to X, and for a promise the anger update toward
  X (§7.3). A check that stops earlier (already at war, a treaty below Non-Aggression for
  a break, a team mate or team enemy, the AI a subject for a proposal) leaves the entries
  alone.
- These lists, and the systems marked to avoid or to attack, are emptied in the
  start-of-turn step of every turn whose date (the one the ministers see, §7.5 "The
  date") is a multiple of 10, before the Politics minister acts. An entry therefore lasts
  until the next such date: from 1 turn (added on a date ending in 9) to 10 turns (added
  on a date that is a multiple of 10) (confirmed: binary).
- **Which messages get an answer** (confirmed: binary). Each turn the AI answers at most the
  newest political message from X in its answer window (above):
  - a treaty proposal or counter-proposal: Accept, Counter or Refuse Treaty; a trade:
    Accept, Counter or Refuse Trade; a gift or a tribute: Accept or Refuse;
  - a request for a gift or tribute: the gift or tribute itself when accepted and its
    package holds at least one item; otherwise (refused, or accepted with an empty
    package) a General message from the `Response … Want a gift/tribute` pool;
  - a surrender demand (only with Allow Surrender): Surrender, otherwise a General message
    from `Response … Demand your surrender`. With the option off it gets no answer;
  - the other 13 demands: Accept or Refuse Demand (`Response … YES/NO …`);
  - an acknowledgement (accept or refuse treaty, break treaty, declare war, accept or refuse
    trade, gift or tribute, surrender, grant independence, accept or refuse demand): a
    General message from the `Response …` pool of its type. Accept and Refuse Demand have no
    such pool, so they get nothing;
  - a General message or the generic demand/request/warn: no answer.

  So two computer players never chatter back and forth: a General message ends each
  exchange.

**Demands the AI starts.** The first of steps 1–5 that applies is chosen, and only then is
the chosen request's `Will Send To Friend/Enemy …` flag tested. When the flag forbids it,
nothing from steps 1–5 is sent that turn; only steps 6 and 7 remain. A step's roll is made
only when the earlier steps found nothing, and a step whose roll succeeds but that finds no
candidate sends nothing, so the next step is tried (confirmed: binary; question 52).

1. Stop attacks, espionage or sabotage, after the AI logged such acts by X.
2. Remove ships (20 %), then, as a separate 20 % roll, remove colonies, from a system where
   the AI has a colony; each candidate system also needs a 1-in-3 roll.
3. At Trade Alliance or better with X, 20 %: break with Z, the lowest-numbered empire other
   than the AI and X that X holds at Trade Alliance or better by its own treaty and that
   the AI holds at War or Non-Intercourse (None does not count). If the AI is Military
   Alliance or better with X and at War with Z, it asks X to declare war on Z instead.
4. At Military Alliance or better, 20 %: make peace with the lowest-numbered empire other
   than the AI and X that X is at War with and that the AI holds at Military Alliance or
   better.
5. At Military Alliance or better, 20 %: attack an empire in a system. Both come from the
   AI's newest battle report, dated this turn or the turn before, of a battle it lost
   while defending (`Combat Defending Lost`, §7.3 term 1): the battle's system, and the
   highest-numbered empire other than the AI that had pieces when the battle began,
   whatever its treaty, contact or survival. Nothing is asked when that empire is X. In a
   simultaneous game the empire with the game's highest player number counts every battle
   as Attacking (§7.3 term 1), so it never sends this request.
6. At war, 33 %, after more than one combat report in the last two turns: demand
   surrender. No flag is needed.
7. At Trade Alliance or better, 10 %: plain chatter.

`Score Percent to Send …` has no effect: its test can never fail.

### 7.5 Other AI tables

In this section round() rounds half to even and trunc() cuts toward zero (confirmed:
binary).

- **Matching rows to the AI state** (confirmed: binary). This applies to `AI_Research`,
  `AI_Construction_Vehicles`, `AI_Construction_Facilities` and `AI_Planet_Types`.
  - A row applies when the name of the current state occurs anywhere in the row's `AI
    State` text. The test is case-sensitive.
  - Where several tables match, the last one in the file wins, and there is no fallback
    row.
  - Because "Attack" also occurs in "Prepare for Attack" and in "Secure Holdings After
    Attack", the stock vehicle file gives the Attack state the queue listed for "Secure
    Holdings After Attack, Incursion".
- **`AI_Research`** (Research minister; confirmed: binary). Rows are (state set, `Tech
  Area Name`, `Tech Area Level`, `Tech Area Min Percent`). When a key is absent, level and
  percent are 0.
  - *Setup each run*:
    - The empire is switched to funding projects in queue order: each project takes what
      it needs to finish its level and the rest flows to the next.
    - "Keep researching" is turned off.
    - Queued areas already at their maximum are dropped.
  - *When*: new projects are chosen only if a research event (new level, new area, all
    projects done) was logged last turn, or fewer than 4 projects are queued. Never when
    the research pool (the points waiting to be spent) is 0, or with a full queue (12).
  - *Mine sweeping*: every fifth turn, if the empire has ever run into a mine field and
    its research file has rows, it queues the area of the **first** tech requirement of
    the first component in the list with Mine Sweeping. This happens only if that area can
    be researched now and is below the required level.
  - *Rows*: projects are added one at a time. Each time, the first row in file order is
    taken whose state matches, whose area can be researched now (prerequisites, game
    options, racial and ruins unlocks) and is below both its maximum and the row's `Tech
    Area Level` (9999 = to the maximum), and whose area is not already queued.
  - *Stop* when the queue is full, no row qualifies, or the share total reaches 100.
    - The share total adds, for each queued project, the `Tech Area Min Percent` of the
      first matching row for that area whose level is still above the area's level.
      Projects with no such row add 0.
    - The test comes before each addition. So rows of 25 or 33 give 4 projects, rows of
      50 give 2, and a row of 100 gives 1.
    - The percentages do not change how points are split.
  - If the queue is still empty, one researchable area below its maximum is queued,
    chosen uniformly at random.
- **Intelligence** (Intelligence minister; confirmed: binary). Nothing happens when
  intelligence projects are not allowed in the game.
  - Queued projects aimed at an empire we now hold Non-Aggression or better with are
    removed.
  - The target is the empire in contact, not eliminated and below Non-Aggression with us,
    that we are angriest at. Anger must be above 0; ties go to the lower player number.
  - With a target and intelligence points above 0, the minister makes up to 10 tries.
    Each try queues against the target a project chosen uniformly at random among those
    whose technology requirement is met.
  - It goes on while fewer than 12 projects are queued and the summed cost of this turn's
    additions does not exceed the empire's intelligence points. This is checked before
    each addition.
- **Difficulty** changes nothing in research, intelligence or design (confirmed: binary).
- **Budget and maintenance caps** (confirmed: binary), shared by the construction ministers:
  - *Net income*: one turn of production (times the computer-player income factor,
    §7.1), plus income received from other empires (trade and tariffs), minus vehicle
    maintenance and facility upkeep. The stockpile is not counted, and tariffs the empire
    pays are not subtracted.
  - *Revenue*: production × income factor + income from other empires.
  - With M = `Maximum Maintenance Percent of Revenue`, the empire is over the soft cap
    when, for any one of minerals, organics or radioactives, that resource's maintenance
    > its revenue × M / 100. It is over the hard cap when the same holds with M + 20.
  - The maintenance in that test (confirmed: binary) is the sum, over the empire's ships
    and bases, of each one's maintenance by spec 02 §7, leaving out every vehicle whose hull
    has `Requirement Pct Colony Mods` above 0 (in the stock data the Colony Ship hull):
    colony ships never count toward either cap. It is worked out from the vehicles of the
    moment when the start-of-turn ministers and again when the economy-step ministers run.
    The vehicles' maintenance actually paid (spec 02 §7) includes the colony ships. Observed
    under a debugger (2026-10-02): at turn 30 an empire with three attack ships and a colony
    ship compared the maintenance of the three attack ships alone; in two 100-turn games the
    empires were over the soft cap in 9 and 11 % of turns 26–100, all after turn 50. The
    engine differs: `Planner::overCap` (`ai.cpp`) compares the maintenance last paid
    (`Empire::economy.maintenance`), colony ships included; our computers are over the soft
    cap in 20 % of turns 26–100 (11 % with colony ships left out, scratch run), where they
    build no warships and scrap a ship a turn (spec 07 "Battles, bases and the first turns
    under a debugger").
- **What spends the stockpile** (confirmed: binary): the computer player spends its
  resources only through construction queues and retrofits. It never uses Emergency
  Build or Repeat Build (only the Construction Queue window sets them) and never converts
  resources. Nothing reacts to a full storage: the only AI rule that reads the stockpile is
  the colony-type pre-rule below (minerals at most 2000).
- **The date** (confirmed: binary): the ministers see the date of the moment they run,
  which is the advanced one in a simultaneous game (§8 step 3) and the unadvanced one in a
  turn-based game. Every "every N turns", "first 50 turns", fleet-delay and early
  size-cap rule reads that date.
- **`AI_Construction_Vehicles`** (Ship Construction minister, once per turn; confirmed:
  binary). The file holds one queue per state, with entries (`Type`, `Planet Per Item`,
  `Must Have At Least`).
  - *Budget*: one turn of net income, less `Percentage of Resources To Reserve For Unit
    Construction` %. That key comes from an optional `_AI_Construction_Units` file that
    the stock install does not ship, so the reserve is 0 (see the reserve quirk of the units
    file below).
  - *Clean-up*: queued items whose design is obsolete are removed, except a first item
    already paid into. Unit items too large for the planet's free cargo space are removed
    too.
  - *Loop*, until a resource of the budget is at or below 0, nothing qualifies, or 1000
    items have been placed:
    1. Scan the table from the top for the first entry whose design type is
       under-supplied.
       - Count = existing vehicles of that type (units counted one by one) plus items in
         all queues. The three colony-ship types share one count.
       - Under-supplied: count < `Must Have At Least`, or `Planet Per Item` > 0 and count
         < colonies × 10 / `Planet Per Item`, compared exactly (not rounded).
       - Over the hard cap, only "Open Warp Point" designs qualify. Over the soft cap, only
         colony ships and "Open Warp Point" designs qualify.
    2. **The design** (confirmed: binary): the AI first looks, for every entry, among its
       own designs that it can build for one made from the `AI_DesignCreation` template
       whose `Name` equals the entry's `Type` text, ignoring case. The newest by creation
       turn wins (the first listed on a tie).
    3. Only when there is none does an entry that names a design type take the newest
       design of that type the empire can build, hand-made ones included. Any other text
       builds nothing. The count and the maintenance-cap gate of step 1 use the chosen
       design's own type. Hand-made and premade designs carry no template name, so text
       never matches them, and neither lookup looks at the obsolete mark. In the stock files
       every template is named after its design type and every entry is a design type or
       `Colonizer`, so the two lookups agree.
    4. **Colonizer** entries build the colony-ship type of the first colonization target
       (in the order below) that the empire can settle and that no queued colony ship of
       that type already covers. When every target is covered, the race's native surface
       type is built. Nothing is built while the empire knows no target it can settle.
    5. **Placement** (confirmed: binary). One queue is chosen, and the item is placed
       there or nowhere.
       - **Defense Base** items (that design type only). The routine was meant to spread
         them over the yard colonies, fewest bases first, but it mixes up two lists, and
         the queue it returns does not depend on the counts. The empire's queue list holds
         every queue the empire owns, system by system in system order and, within a
         system, in the order of the system's own object list (the order the objects were
         placed in or entered the system, so the planets come before the ships that
         arrived or were built later) (confirmed: binary; observed 2026-10-02: one
         empire's list ran through eight planets of its home system in planet order, then a
         Base Space Yard built there, then a planet of its second system). Let K be the
         number of those queues that have a working space yard: a colony that is not
         cloaked and has a Space Yard facility, or a ship that is not cloaked and has a
         working Space Yard part (none while mothballed or with that part destroyed)
         (confirmed: binary). When some yard colony (not a yard ship) counts at most two
         bases, the base goes to the K-th queue of the whole list, whatever that queue is.
         The count is the empire's bases in that colony's sector plus every base queued
         anywhere in the empire; a "base" is any vehicle or queued item whose design uses
         a base hull (Defense Bases, Base Space Yards and any other base), and a queued item
         counts as many as its count builds (confirmed: binary). When every yard colony
         counts three or more, the base goes to a queue of the whole list drawn at random.
         The backlog test below is made on that queue alone. A queue without a working
         space yard loses the base at the empire's next construction step (spec 02 §6.1),
         so the base is built only when that queue happens to have a yard. With only the
         homeworld's yard, K is 1, and the base goes to the first queue of the list, a
         colony in the empire's lowest-numbered colony system, which is the homeworld only
         when no other colony lies in a lower-numbered system or before it in the
         homeworld's system. Observed under a debugger (spec 07 "Pace observed under a
         debugger"): all 119 Defense Base placements of three 100-turn games followed this
         rule, every one made in Infrastructure, and 9 of them reached a queue with a yard
         (an empire whose list held only its homeworld, or whose K-th queue happened to be a
         yard colony); in two more games 55 placements did, 16 of them to a queue with a
         yard (spec 07 "Battles, bases and the first turns under a debugger"). As the
         commands refuse a base at a colony without a yard, the engine counts such a base as
         queued and spends its share of the budget without queueing it, which is what the
         original's construction step leaves (`ai_economy.cpp` `placeDefenseBase`). Since
         2026-10-02 `workingYard` passes over a cloaked yard ship, and `queueList` follows
         the system's list: every vehicle carries a stamp of when it was placed in or
         entered its system (`Vehicle::arrival`, saved with the game), and the planets come
         first (question 60; a planet made during play too, inferred, question 64).
       - Mines, satellites, weapon platforms and fighters go to the first queue in this
         order: the smallest backlog in turns; then the most free cargo space; then the
         fewest units of that kind already in its cargo; then the larger planet; then the
         higher resource production (minerals, organics and radioactives); then the
         highest rate. Any colony's queue can take them. When that first queue lacks the
         cargo space for the batch, nothing is placed. The "units of that kind" are in
         fact every unit stack in the colony's cargo, whatever its kind: the key is the
         total number of units it holds (confirmed: binary, question 60). The engine
         follows this since 2026-10-02; it used to count only the units of the item's
         vehicle type.
       - Any other item goes to the queue with the smallest backlog in turns, then the
         highest rate. Ships need a space yard; the other units can use any colony's
         queue.
       - A queue's **backlog** is the sum, over its items, of each item's turns: the
         largest, over the resources the queue has a positive rate for, of (what the item
         still costs ÷ that rate) rounded up. The first item counts only what is still
         unpaid. A queue whose rates are all 0 counts 9,999.
       - The item is placed only if that backlog is under 5 turns, so a queue holds at
         most four items that each take some turns.
       - Units come in batches of as many as the queue finishes in one turn. Recon
         satellites are the exception.
    6. The budget is reduced by what the item takes from that queue this turn: its cost,
       capped at the queue's rate.
    7. After a placement the scan restarts from the top. After a failure (no queue under
       5 turns) it continues below that entry. It stops at the end of the list; there is
       no wrap-around.
  - Difficulty does not affect construction. Nothing outside the table is ever built, so
    there are no extra scouts.
- **Units file** (confirmed: binary): the game also looks for an `_AI_Construction_Units`
  file through the §7.2 rule, and reads it again every time. The stock install has none.
  - *Format*: the first record holds `Percentage of Resources To Reserve For Unit
    Construction`. Each later record holds `Colony Type`, `Num Queue Entries`, `Entry N
    Type` (an AI design type name) and `Entry N Maximum in kT` (values above 65,000 read as
    65,000). There is no `AI State` key: the rows apply in every state.
  - *When*: after the vehicle list, every turn, the Ship Construction minister takes a
    fresh budget: a full turn of net income, with no reserve taken off and nothing of what
    the vehicle list used. It goes through the empire's queues in order. A colony qualifies
    when its queue is empty, it has no free facility slot, and all three resources of the
    budget are above 0.
  - *Row*: the last record whose `Colony Type` text contains the colony's type name,
    ignoring case. A colony without a type gets nothing.
  - *What is built*: the first entry that names an AI design type exactly, whose units in
    the colony's cargo take less than `Maximum in kT`, whose type has a newest valid
    design, and whose design tonnage is below the free cargo space. One batch is queued: as
    many units as the queue finishes in one turn, at least one (recon satellites one at a
    time). The budget is reduced by the cost capped at the rate. At most one item per
    colony.
  - *Reserve quirk* (confirmed: binary): the reserve is one value shared by all empires.
    Each empire's start-of-turn AI step resets it to 0. Only the units step loads it,
    after the vehicle list, and that step runs for every empire whose Ship Construction
    minister acts: it sets the reserve to 0, then to the file's percentage when a units
    file is found (race folder, style folder or Default). An empire whose ministers do not
    act (the minister off, or a player with missing orders who forbade AI changes, §7.1)
    leaves it unchanged. So the reserve an empire's vehicle list applies is the value left
    by the nearest empire before it in the same round whose units step ran, which is 0
    when that empire has no units file (0 for the first). In turn-based games, and with the
    stock install, it is always 0.
- **`AI_Construction_Facilities`** (Facility Construction minister; confirmed: binary).
  Rows are (state set, `Construction Queue Type` = Homeworld or a colony type, ordered
  `Facility N Ability` + `Amount`).
  - *Timing*: it runs before Ship Construction and again after it, except on every fifth
    turn, when only the second pass runs (§7.1).
  - *Which colonies*: those whose queue is completely empty and that have a free facility
    slot, visited system by system in system order and in the game's object order within
    a system. Each gets at most one facility per pass.
  - *Colony type*: a colony without one is given a type first (§7.5 colony types below).
    An unknown type uses the Homeworld rows.
  - The facility for an ability is the researched facility that provides it best: the
    highest `Value 1` for the amount-type abilities (those the Design minister scores by
    Amount 1, §7.5 `AI_DesignCreation`), otherwise the highest sum of its tech-requirement
    levels; a tie goes to the later facility in the file.
  - The facility queued is for the first entry of the matching row that a researched
    facility provides and that no rule below blocks:
    - `Amount` is 0, or the colony already has `Amount` facilities with that ability;
    - Spaceport: the system has one already, or the race needs none;
    - Supply Generation: the system has one already;
    - Space Yard: the planet has one already;
    - research abilities (`Point Generation - Research` and the planet and system research
      modifiers): all research is done, or the empire's research production has reached
      `Maximum Research Point Generation`;
    - intelligence abilities (`Point Generation - Intelligence`, the planet and system
      intelligence modifiers and `Change Bad Intelligence Chance - System`): intelligence
      projects are off, or the empire's intelligence production has reached `Maximum
      Intelligence Point Generation`. The `Generate Points` abilities are on neither
      list;
    - Change Atmosphere: the planet is unpopulated, or its atmosphere already suits its
      majority race;
    - a one-per-system ability that the empire's colonies in the system already have,
      built or queued. The fixed list of such abilities: Palace, the three `Resource Gen
      Modifier System` and two `System Point Generation Modifier` abilities, `Combat
      Modifier - System`, `Damage Modifier - System`, `Shield Modifier - System`, `Planet
      Value Change - System`, `Planet Conditions Change - System`, `Change Bad Event Chance -
      System`, `Change Bad Intelligence Chance - System`, `Change Population Happiness -
      System`, `Change Population - System`, `Modify Reproduction - System`, `Plague
      Prevention - System`, `Ship Training`, `Fleet Training`, `Ship Training - System`,
      `Fleet Training - System`, `Resource Conversion`, `Long Range Scanner - System`,
      `Reduced Maintenance Cost - System` and the five `Stop …` abilities (Star Destroyer,
      Nebulae Creator, Black Hole Creator, Open Warp Point, Close Warp Point) (confirmed:
      binary);
    - with finite resources: `Resource Generation` or `Resource Gen Modifier Planet` for a
      resource whose value on this planet is 0. The system modifiers and the planet-value
      abilities of that resource are not blocked.
  - *Nothing to build* (confirmed: binary, question 55): when no entry of the row passes,
    the colony gets nothing in that pass. It keeps its type, no other row or table is
    tried, and no other type is given, so such a colony stays as it is until a rule stops
    blocking one of its entries (for an Intelligence Compound, the first researched
    intelligence facility). The engine matches.
  - *Upgrades*: every fifth turn, obsolete facilities are upgraded to the newest
    researched version, planet by planet in queue-owner order, while what was queued so far
    is at most half of one turn's net income (division toward zero) in all three
    resources, checked before each planet; so a zero budget still upgrades the first
    planet. A planet's queue need not be empty. Every queued facility of an older version
    switches to the newest one. This runs at the start of the pass, before any colony gets
    its facility (confirmed: binary), so a planet that receives an upgrade no longer has an
    empty queue and gets no new facility that turn.
- **Colony types** (confirmed: binary). The list is fixed: Homeworld (also "Imperial
  Center"), Mining Colony, Farming Colony, Refining Colony, Resupply Base, Research
  Compound, Intelligence Compound, Construction Yard, Military Installation.
  - *At colonization* (for every empire, unless the player is asked), the default is
    Mining Colony. Two pre-rules come first:
    1. Mining Colony if the stock of minerals is at most 2000 and the planet's mineral
       value is above 80.
    2. Otherwise compute, per resource, deficit = vehicle maintenance + facility upkeep −
       income from other empires − production. The first match wins: Farming Colony if
       the organics deficit exceeds 2000 and the organics value is at least 50; Mining
       Colony for minerals likewise; Refining Colony for radioactives likewise.
  - Otherwise the type is the `Planet Type` of the first `AI_Planet_Types` row (file
    order, state matched as above) that passes all its tests:
    - Research Compound rows fail when all research is done or the empire's research
      production has reached the AI_Settings cap. Intelligence Compound rows fail when
      its intelligence production has reached its cap or intelligence projects are off.
      Neither test looks at whether the empire has a facility of that kind (confirmed:
      binary, question 55), so an Intelligence Compound is chosen before any
      intelligence facility is researched;
    - `Maximum Total in Empire` > 0 and the empire already has that many of the type;
    - `Max Per System` > 0 and the system already has that many;
    - `Percent Of Colonies` > 0 and colonies of the type > colonies × percent / 100,
      compared exactly without rounding and counted before the new colony;
    - the planet's size is below `Minimum Planet Size for Type`, a `PlanetSize.txt` entry
      named in the row;
    - the resource values: only thresholds above 100 count. Without finite resources the
      planet's value must be at least the threshold. With finite resources it must be at
      least mid × threshold / 100, compared exactly. Here mid = Low + (High − Low) / 2
      (integer halving) from `Planet Value Low Resources` and `Planet Value High
      Resources`.

    A 0 in a limit field means no limit. If no row passes, the colony stays a Mining
    Colony; the last row gets no special treatment.
  - *Later*: when the facility minister meets a colony without a type, it assigns
    Homeworld in the home system. Elsewhere it assigns Mining, Farming or Refining by the
    strictly highest resource value. On a tie it assigns nothing and uses the Homeworld
    rows.
- **Colonization** (Colonization minister and the target list; confirmed: binary).
  - *Targets*: uncolonized planets in systems the empire knows, plus zero-population
    colonies of hostile empires. A neutral empire looks only at its home system. Left out:
    - planets in a system where an empire at Non-Aggression or better with us has a colony
      and we have none (only colonies count here);
    - planets one of our colony ships is already ordered to settle;
    - at Low difficulty, each target with a 10 % chance per turn.
  - *Order*:
    1. danger, lowest first: 5 per non-friendly empire present in the target system, plus
       1 for each non-friendly empire present in each system a warp point of the target
       system leads to. So one such empire beyond two warp points adds 2, and two such
       empires beyond one warp point add 2. Non-friendly means below Non-Aggression, not
       yet met included; present means owning any object there, seen or not (confirmed:
       binary);
    2. warp jumps from home, fewest first;
    3. planets with ancient ruins first;
    4. breathable atmosphere first;
    5. larger planets first;
    6. higher planet value first.
  - *Ships*: idle colony ships are those with no orders, or whose orders are Move To then
    Colonize and whose target has become one of our colonies. A ship whose target turned
    into an asteroid field, was taken by another empire or is gone keeps flying; its
    Colonize order fails on arrival, the failure clears its orders, and it is planned
    again later. Each target, in order, gets the nearest idle colony ship (by jumps) whose
    design can settle it. The minister clears its orders; if the ship has cargo space and
    carries no people, it first gives a Load Cargo (population) order where the ship is
    (usually the yard that built it); then Move To the planet's sector, then Colonize. So
    an AI colony starts with the people the ship carried, dropped under the Drop Cargo
    rules (spec 03 §8). OpenSE4 gives the single Colonize order of spec 03 §8, which loads
    colonists where the ship is and has the same effect.
- **Logistics ministers** (confirmed: binary unless marked).
  - *Transports*: an idle population transport that carries people and whose used cargo
    space (people and units) is more than half its capacity delivers; otherwise it runs the
    load step (confirmed: binary). A Load Cargo (population) order takes the planet's races in the order
    of its population list until the hold is full, always leaving 1M.
    - The destination is the own colony with the lowest population among those below
      their maximum (the domed maximum when domed), in a safe system, not already another
      transport's drop target, and where either every carried race breathes the planet's
      atmosphere, or the planet is already domed and already hosts one of the carried
      races. A delivery therefore never domes a colony.
    - Otherwise the transport loads at the nearest safe own colony with at least 1000M whose
      own atmosphere is "wanted" and whose whole population breathes one same atmosphere.
      An atmosphere is wanted when it is the atmosphere of an under-populated safe colony
      or, for a domed one, an atmosphere all its races breathe. The test reads the source
      planet's atmosphere, which looks like a slip for the race's.
    - A failed delivery never falls back to loading (confirmed: binary). After the load
      step, the delivery is planned in the same turn only when the load source is in the
      transport's own sector: the Load order is then followed by the delivery orders. When
      the load step finds nothing, or sends the transport to a source elsewhere, no
      delivery is tried. The delivery picks its destination by the races aboard, so a
      transport with nothing aboard never delivers.
    - The orders depend on where the target is: in another system, only a move to the
      target's sector (the transport finishes the job when it is planned again after
      arrival); in the same system, a move and then Load or Drop; in the same sector, Load
      or Drop at once. Only a Drop order reserves its destination against other
      transports. A transport left without orders gets the resupply orders below.
    - Idle ships with a Medical Bay get no order: the routine picks a planet and then drops
      it.
  - *Retrofit*: none while over the soft cap, in the Attack, Incursion or Defend (Short
    Term) states, or while 3 ships are already being retrofitted.
    - Eligible ships are idle, have empty cargo, are in a system without enemies, and have
      a design older than 20 turns.
    - Such a ship goes to a yard and retrofits to the newest valid design with the same
      hull and design type, up to 3 ships.
  - *Scrap*: while over the soft cap, one ship per turn (confirmed: binary). The
    candidates are the empire's vehicles that are not of a colony-ship design type
    (Colony (Rock), (Ice) or (Gas)): every one that can move (maximum movement above 0, not
    mothballed), wherever it is, and every one that cannot move (bases) only where the
    empire has a space yard in its sector. The candidate whose design is oldest (creation
    date; the first in the empire's vehicle list on a tie) is ordered, when it can move and
    stands elsewhere, to Move To the nearest sector where it can be scrapped, and then to
    Scrap; it leaves the empire's vehicle list for that turn. Every 10 turns, useless
    facilities are scrapped (extraction on a 0-value planet with finite resources,
    atmosphere changers no longer needed). Observed under a debugger (2026-10-02): every
    ship that vanished outside a battle (16 in a 100-turn game: attack ships, mine sweepers,
    a carrier, layers) did so on a turn its empire was over the soft cap, and no base was
    scrapped: an old attack ship far from a yard is chosen before a base at a yard. The
    engine differs: `planScrap` (`ai_military.cpp`) takes only ships already at a yard and
    scraps the oldest of them at once, so bases, which always sit at yards, go first; our
    computers scrapped 0.7 bases per empire in 100 turns (of the 1.0 they built). A scratch
    run with the original's candidates kept 0.42 bases per empire at turn 100 instead of
    0.24 (the original: 0.4–0.6 in five games, then 1.2, and 0.0 in a game with no turn in
    Infrastructure).
  - *Repair* (confirmed: binary). A vehicle needs repair only when at least one of its
    parts is destroyed, and then:
    - an Attack or Defense Ship: when its strength rating (§7.2) is 0, or its destroyed
      parts exceed round(0.3 × its part count), or it lacks a part it needs to operate, or
      it is in a fleet and its maximum movement is below 6 and below the fleet's fastest
      member's minus 1;
    - a Population Transport, Mine Layer, Mine Sweeper, Boarding Ship, the warp-point and
      stellar-manipulation types, a Space Yard Ship or a cargo transport: always;
    - any other type: when its destroyed parts exceed round(0.25 × its part count) or it
      lacks a part it needs to operate.

    A vehicle "lacks a part it needs to operate" when it has no working (not destroyed)
    source of `Ship Bridge`, none of `Ship Auxiliary Control` and none of `Master
    Computer`, the hull's own abilities included (confirmed: binary). Engines, movement,
    life support and crew quarters are not checked: the game tests the last two, but that
    test can never fail. A mothballed vehicle has no working abilities, so it always lacks
    one. The fleet-fitness test (§7.5 "Fleets") uses the same rule.

    Every turn, such a vehicle loses all its orders (Colonize included) and leaves its
    fleet, before any destination is looked for. If it can move, it seeks the nearest of the
    empire's space yards by travel distance (an uncloaked colony with a Space Yard facility,
    or an uncloaked ship with a working yard) and waits when it is already there or a yard ship
    is within that ship's own speed. With no yard it is left without orders. Details
    (confirmed: binary):
    - Mothballed vehicles are not skipped. One with a destroyed part loses its orders and
      leaves its fleet every turn, but its maximum movement is 0, so it gets no
      destination.
    - Yards are visited by system number, then in the game's object order within the
      system, and on equal travel distance the first one found wins. A yard at a travel
      distance above 9,998 is never chosen.
    - The vehicle itself counts as a yard when it has a working yard and a queue: it is
      then its own nearest yard and stays where it is.
  - *Resupply* (confirmed: binary). Each system's supply distance is 13 × (1 + the warp
    jumps, over all links, to the nearest system holding one of our colonies with `Supply
    Generation`), or 999,999 when there is none. A fleet is on unlimited supply when none
    of its ships lacks unlimited supply, a fleet without ships included. A fleet not on
    unlimited supply is sent when its total supply ÷ its total supply cost per move is
    below the distance of the system it is in; both totals count only the members without
    unlimited supply (confirmed: binary); the fleet takes the orders its leader would get. A ship outside
    fleets is sent on the same test or when its supply is 0; colony ships and Destroy Star
    ships are exempt. The orders: the list is cleared, then a Move To the nearest own or
    allied depot, avoiding sectors with a visible armed hostile (the Resupply order of spec
    03 §8); with no depot, a move to our nearest colony, or home when we have none.
  - *Space Yard Ships* (confirmed: binary): own yard ships with normal status, movement
    left and no orders. One without a working yard part gets the resupply orders above.
    Otherwise it seeks the nearest own vehicle that has a destroyed part, a maximum movement
    of at most 2 and no own yard in its sector (in practice mostly bases, and mothballed
    vehicles), and waits when already there. With no such vehicle it gets the resupply
    orders. Details (confirmed: binary):
    - "Own yard in its sector" is any of our objects there with a working space yard: an
      uncloaked colony with a Space Yard facility, or an uncloaked ship with a working yard,
      the yard ship itself included. So a vehicle the yard ship has reached is no longer a
      target, and the yard ship goes on to the next one or gets the resupply orders. It waits
      beside a vehicle only while it is cloaked.
    - "Movement left" is the movement the yard ship still has when the start-of-turn
      ministers act. Movement is refilled later (spec 03 §6.3, §8 here), so this is what
      its last movement left.
    - Fleet membership is not checked.
  - *Stellar Manipulation* (confirmed: binary): idle own ships of the matching design type
    that are not in a fleet. "Nearest" means nearest to our home system.
    - Open Warp Point: while a free exploration frontier point is left (§7.2), the ship's
      range counts as 0, so nothing is chosen and the ship gets the resupply orders
      (confirmed: binary). Otherwise each explored system with fewer than 10 warp points
      gets one random target: an unexplored system within the ship's range, with fewer
      than 10 warp points and not yet linked to it. The source with the fewest of our
      colonies (then the fewest jumps from home) is chosen. The ship moves to a random
      empty edge sector there and opens the point. The edge sector is drawn as a random
      side (1 in 4 each), then a random position 0–12 along it, so each corner is twice
      as likely as any other edge sector. A sector holding any object, vehicles and unit
      groups included, is drawn again. Only the first 99 draws can succeed (the 100th is
      discarded even when empty); when none does, or nothing is chosen, the ship gets the
      resupply orders.
    - Close Warp Point: a random warp point that leads from an explored system with one of
      our colonies into a system where we have no presence but see a hostile empire.
      Otherwise no order. (Confirmed: binary.) "Presence" is a ship, base, colony, or
      fighter, satellite or drone group of ours; mine fields do not count. We "see" a
      hostile empire (treaty below Non-Aggression, no contact included) when any object
      it owns there (ship, base, unit group or colony) passes the detection test that
      Sentry uses (spec 03 §8): the system is explored by us and our sensors there reach
      the object's cloak level. A colony counts unless a cloaking facility hides it.
    - Create Planet: the uncolonized asteroid field nearest home by jumps, in an explored
      system with a star, that no other ship of ours is already heading for; ties are
      random.
    - Destroy Planet: the nearest planet colonized by a hostile empire, within the ship's
      size limit, with no armed hostile strength in its sector and no `Stop Planet
      Destroyer`, and whose colony is not marked cloaked, even when it is seen (spec 01
      §6.9, confirmed: binary).
    - Create Star: the nearest explored system without a star whose centre sector holds no
      visible, armed, non-mothballed hostile. Nebulae, black holes and constructed planets
      are not checked, so the order can then fail by spec 01 §9.
    - Destroy Star, Create Black Hole, Create Nebulae: the nearest star in an explored
      system where we have no colony and a living hostile empire has one.
    - Create Storm: never used.
    - Destroy Storm: the nearest storm whose sector holds no visible armed hostile.
    - Destroy Black Hole, Destroy Nebulae: the explored system of that kind nearest by
      jumps. The order is given at once when the ship is inside. Otherwise the ship gets a
      Seek toward sector 36 of that system (x 10, y 2) (confirmed: binary). That Seek lasts
      one turn: in a turn-based game it moves the ship as far as it can and is then done;
      in a simultaneous game it pursues the sector for the whole movement phase and is
      removed after it. So the minister plans the ship again every turn. With no such
      system known, the order it gives points at no system, moves nothing and is gone the
      next turn, the same as no order.
  - *Carriers and troops*: empty carriers, drone carriers and troop transports reload at
    the nearest colony holding fighters, drones or troops.
- **Design types of other designs** (confirmed: binary). A human's design is typed when it
  is saved: its type label is matched exactly against the 39 built-in names, and failing
  that it is typed automatically. Every turn, before the Design minister, each of the
  empire's designs that still has no type is typed automatically too. The first test that
  matches wins:
  1. `Colonize Planet - Rock`, `- Ice` or `- Gas`: the colony ship of that surface.
  2. A base hull: Base Space Yard with a Space Yard, else Defense Base.
  3. Launch/Recover Fighters: Carrier; Launch/Recover Satellites: Satellite Layer; Launch
     Drones: Drone Carrier.
  4. Lay Mines: Mine Layer; Mine Sweeping: Mine Sweeper; Boarding Attack: Boarding Ship;
     Space Yard: Space Yard Ship.
  5. Each stellar-manipulation ability: its own type (Open and Close Warp Point, Create and
     Destroy Planet, Star, Storm, Black Hole and Nebulae).
  6. By hull type: a satellite is a Recon Satellite with a sensor, else a Satellite; a
     drone is an Anti-Planet Drone when a warhead can target planets, else an Anti-Ship
     Drone; fighters, mines, troops and weapon platforms take their own type.
  7. Otherwise a Population Transport when it has cargo space, else an Attack Ship, armed
     or not. So a premade scout is an Attack Ship.
- **`AI_DesignCreation`** (Design minister; confirmed: binary). The file holds one
  template per AI design type. Its fields are vehicle type, default strategy, tonnage
  window, must-have abilities, minimum and desired speed, five majority and five secondary
  weapon-family picks, and "spaces per one" densities. A missing key is 0, so a template
  without `Size Maximum Tonnage` never finds a hull.
  - *When*: on turns after a research event was logged, when the empire has no designs,
    and every tenth turn. Every template is processed in file order, and each yields at
    most one new design per run.
  - *Best part per ability*: only researched components that fit the vehicle type count.
    - A component that also has Space Yard or Cloak counts only for that ability.
    - The score is Amount 1 for amount-type abilities (shields, cargo, supply, movement,
      bays, mines, troops and the like), Amount 2 for Cloak Level, and otherwise the sum
      of the component's tech-requirement levels. Ties go to the later component.
  - *Weapons*: for a "Weapon" majority or secondary, take the family picks 1–5 in order.
    Use the first family with any researched weapon, and in it the weapon with the highest
    tech-requirement sum. With no such family the template gets no weapon.
  - *Candidate hulls*: researched hulls of the template's vehicle type that meet all of:
    - tonnage within `Size Minimum Tonnage`–`Size Maximum Tonnage`;
    - `Requirement Max Engines` ≥ `Minimum Speed`;
    - the pairing rules: fighter-bay hulls only with a Launch/Recover Fighters majority,
      and such templates must use them; colony-module hulls pair with Colonize
      majorities and cargo hulls with Cargo Storage majorities in the same way;
    - the early size caps of `AI_Settings`.
  - *A new design is needed* when the designer has no non-obsolete design of this template
    yet, or its latest one can be improved:
    - one of its components has a researched successor with a higher Roman numeral;
    - the chosen majority or secondary weapon outranks the design's best of that family;
    - a larger candidate hull exists;
    - it carries an engine other than the current best;
    - the template wants shields, shields exist, and the design has none;
    - Quantum Reactor is a misc ability, one exists, and the design has none.

    Nothing is designed when the latest design was made this turn, or when a must-have
    ability has no available part.
  - *Building*:
    1. **Hull**: the candidate with the largest tonnage (the first listed on a tie). With
       no candidate, nothing is built.
    2. **Control**: a Master Computer if one exists, which replaces the bridge, life
       support and crew quarters. Otherwise a bridge if the hull needs one, plus the
       hull's minimum life support and crew quarters.
    3. **Engines**: as many as `Minimum Speed`, if the hull uses engines.
    4. **Hull parts**: on a fighter-bay or cargo hull, (trunc(tonnage × pct / 100) ÷ part
       size) + 1 of that part. On a colony hull, exactly one module of the design's
       surface type.
    5. **Must-haves**: one each ("Weapon" means the majority weapon).
    6. **Speed** is counted in engines (total `Standard Ship Movement` Amount 1), not
       movement points. Engines are added until the count reaches `Minimum Speed`. Then
       more are added while it is below `Desired Speed` and the hull's Max Engines, and
       space remains. Each loop stops after 100 tries.
    7. **Densities**: a "Spaces Per One" value N adds floor(hull tonnage / N) copies, at
       least 1; 0 or less adds none. Each copy is added only if it fits.
       - A component's per-vehicle limit caps the copies added in one step.
       - A Space Yard adds one copy per step, and none once the design has two.
       - So 10000 means "one" on any hull up to 10000 kT.
       - The steps, in order: shields (phased if available), armor, majority, secondary,
         then each misc ability.
       - To Hit Offense Plus is skipped with Weapons Always Hit, and Emergency Resupply and
         Supply Storage are skipped with a Quantum Reactor.
    8. **Fill**:
       - the majority again at density 10 (only if its own density is positive);
       - shields at density 10;
       - armor at density 10;
       - any space left goes to the highest-numbered pure armor part.
    9. **Mounts**: each part gets the last mount in the list that the empire has
       researched, the part accepts, whose size range fits the hull, and that lists the
       vehicle type.
    10. **Strategy**: the template's `Default Strategy` if the empire has it ("Ram" falls
        back to "Kamikaze"). Otherwise the first strategy whose primary movement suits the
        type:
        - "Don't Get Hurt" for colony, warp, stellar, space-yard, mine, transport and
          carrier types;
        - "Drop Troops" for troop ships;
        - "Board Enemy Ships" for boarding ships;
        - "Ram" for drones;
        - "Optimal Weapons Range" for all others.
    11. **Name** (confirmed: binary): the counter is the number of designs this empire's
        Design minister has made (saved with the game; hand-made and premade designs do
        not count). The name is the first line of the race's design-name file whose
        position is beyond the counter and that no design of any empire uses. Then come
        rounds with "II" up to "XV", the position count carrying on across the rounds;
        with nothing left the name is empty, and the design is still made under the empty
        name: nothing checks it (confirmed: binary). With no file the name is "Design
        <counter + 1>", unchecked.
    12. **Obsolete**: every older design of the same player and design type becomes
        obsolete, including the player's own hand-made ones.
  - There is no scout design type, so the AI never designs or builds scouts.
- **`AI_Fleets`** (Fleets minister; confirmed: binary).
  - *Wanted fleets* n: take the first division, in order, whose `Max Amount of Ships` is
    at least our vehicle count (unit groups included). A division whose ship limit is 0
    or less compares `Max Amount of Planets` with our planet count instead. With no match,
    n = 0.
  - n is also 0 until `Fleets Dont Use For Num Turns` turns after the game start.
  - *Forming*: with fewer fleets than wanted, at most one new fleet is formed per turn.
    It forms around the newest idle, fit ship not in a fleet, with `Fleets Default
    Formation` and `Fleets Default Strategy`. Only a ship that can move and that an attack
    fleet or a defence fleet could take may lead it, and never a troop transport or a
    boarding ship: an attack ship, a carrier or drone carrier with its units aboard, a
    kamikaze ship, or a defence ship (confirmed: binary). The engine differs: `planFleets`
    (`ai_military.cpp`) forms a fleet around any fit ship, troop transports and empty
    carriers included; 7 % of our fleets were troop transports alone and 6 % carriers alone
    (fleet-turns of turns 51–100), against none and 1 % in one game of the original, and
    such fleets go to fight and draw (spec 07 "Battles, bases and the first turns under a
    debugger").
  - *Disbanding*: fleets beyond n are disbanded, and so is a fleet whose leader is gone
    or unfit.
  - *Roles*: fleet i of n (counting from 1) is an attack fleet when i is odd and (i + 1) /
    2 < n × (100 − `Percentage of Fleets to use for defense`) / 100. All others are
    defence fleets.
  - *Size*: a fleet recruits up to trunc(vehicle count × `Fleets Percentage of Ships For
    Fleets` / 100 / n) members.
    - Recruits are idle ships not in a fleet, within 3 jumps. They join at once if at the
      fleet's spot; otherwise their orders are cleared and they get a Join Fleet order
      (confirmed: binary). That order chases the fleet: on each of the ship's actions it
      steps toward the fleet's position at that moment (wherever the fleet has gone since),
      and it joins as soon as it stands where the fleet stands; it waits when out of
      movement points and fails (clearing the list) only when no route is left or the
      fleet is gone. The ship counts toward the fleet's size from the moment it is ordered.
      OpenSE4 follows this since 2026-10-02: the order is `OrderKind::JoinFleet` (the fleet
      in `amount`), and `planFleets` counts the ships that carry one for a fleet among its
      members. A recruit also waits, rather than fails, when a hazard, a busy yard or a
      blocked way stops its step (inferred from "fails only when").
    - Observed under a debugger (spec 07 "Pace observed under a debugger"): from turn 41
      on, 74–78 % of the original's attack ships were in fleets, against 44–51 % in ours;
      an empire with 46 ships kept six fleets of 7, 7, 6, 6, 2 and 1 attack ships, while our
      fleets, capped by trunc(vehicle count × 80 / 100 / n) with fewer ships, mostly hold
      one or two.
    - Attack fleets take attack ships, loaded carriers and drone carriers, loaded troop
      transports, kamikaze ships and boarding ships.
    - A fleet led by a defence ship takes only defence ships.
  - A ship is unfit in any of these cases:
    - its damaged components outnumber round(0.3 × its component count) for attack and
      defence ships, or round(0.25 × count) for other combat types;
    - it lacks a component it needs to operate;
    - it is an armed type with zero strength;
    - its role is not combat.
  - *Orders* go to idle fleets only:
    - In Defend (Short Term), each enemy in territory, in turn, gets the nearest idle
      defence fleet. In simultaneous-movement games the fleet pursues the enemy ship
      rather than moving to its spot.
    - Otherwise fleets head for the state's goal: the staging system in Prepare for
      Attack, the target with the most candidates in Attack, and the secured system in
      Secure Holdings. In other states they go to the top attack candidate if its owner
      is at War with us.
    - Leftover fleets defend, then patrol (fleets led by a defence ship) or explore.
  - Ships outside fleets get individual attack and defence orders only while the empire
    has no active fleet.
- **How long the ministers' movement orders last** (confirmed: binary). Wherever this
  section says that a minister sends a ship or fleet somewhere, chases an object or makes
  it "move" toward a target, the order it gives is a Seek: toward a sector (the Defense
  minister in turn-based games, the Attack minister, the fleets' goals, exploration,
  patrol, repair, Space Yard Ships) or after an object (the Defense minister and the
  defence fleets in simultaneous games). These are not the player's Move To and Attack
  orders:
  - in a turn-based game a Seek moves the ship as far as it can and is then done;
  - in a simultaneous game it is kept for the whole movement phase, also once the ship has
    arrived, and is removed after the phase. An order queued behind it (an explorer's
    Warp) therefore waits for the next turn's phase;
  - an Attack given when the ship already stands on its target's sector is carried out at
    once and done.

  So at every start of turn the ministers find the computer player's warships idle (or
  with only a Warp left) and plan them again: they are free to be recruited by a fleet,
  to defend against this turn's threats, to explore or to patrol. Only the colonization,
  transport, resupply, Join Fleet and stellar-manipulation orders last until done.
  OpenSE4 follows this since 2026-10-02 with an order of its own, `OrderKind::Seek`,
  which only the ministers give: toward a sector, or after a ship or planet whose current
  sector is the goal. Its steps are a pursuit's (a hazard or a battle on one only stops it
  for that run of the list, spec 03 §6.4), and at its goal it waits without attacking, as
  a group with no drone does at the target of an Attack (spec 03 §8), so the day's battle
  check covers the sector on each of its actions. The movement phase removes every Seek
  after its last day. In a turn-based game a Seek that arrives is done and the next order
  runs with the movement left, and what is left of one when its group's run ends (out of
  movement, stopped by a hazard or a battle) is removed then (inferred: "done once moved",
  question 64). The ministers' Attack is the stored Attack that names no target and no
  place (spec 03 §8): carried out at once where the group stands, at 1 movement point,
  and done, in either kind of game; the Ship Cloaking minister's ships lower their cloaks
  for it and raise them again after the day's battles. Retrofit, the layers, sweepers,
  carriers, troop transports and drones keep their own orders (§7.5 above).
- **Attack and defence** (Attack and Defense ministers; confirmed: binary).
  - *Defence* (confirmed: binary): only in Defend (Short Term). Each defender in turn goes
    to the first entry, in the defend-list systems and the Defense minister's order
    (§7.2), whose assigned strength is at most round(its threat × 1.3) at Low difficulty,
    or round(its threat × 1.5) at Medium and High. The order is Attack when the defender
    is already in that sector, otherwise a move there, or in a simultaneous game a pursuit
    of the entry's latest object. The entry's assigned strength then grows by the
    defender's rating, without the + 1.
  - *Attack*: attackers go to the first candidate in the target system whose assigned
    strength is at most round(its value × k). k = 1.5 when our score exceeds 1.5 × the
    owner's score, else 1.
    - Each candidate is skipped with a 25 % chance. If none qualifies, the first
      candidate that passes a 75 % roll is taken.
    - The order is Attack when the ship is already in that sector, otherwise a move there.
- **Exploration** (confirmed: binary). There is no scout design type.
  - *Explorers*, taken in the game's object order: the empire's ships of design type
    Attack Ship or Attack Base (a base cannot move, so its orders do nothing), Carriers
    that carry fighters and whose cargo is more than half full, and Drone Carriers that
    carry drones and whose cargo is more than half full. Each must be in normal status,
    have fewer than 4 destroyed parts and supply above 0, be outside fleets, and have no
    orders or a first order that is a Seek (spec 03 §8). Nothing is done while no free
    frontier point is left (§7.2).
  - *The point list* starts as the free frontier points in list order (system by system,
    then the system's warp points in order). With A = the number of the empire's Attack
    Ships (all of them, not only the explorers): when no point is free and A exceeds 3 ×
    the number of all frontier points, every frontier point is put in the list. Then,
    when A exceeds 5 × the list's length, each point of the list (as it was) is entered
    once more if A still exceeds 5 × the current length, and once more again if A exceeds
    8 × the current length; the length grows as entries are added.
  - Each explorer takes the point of the list with the smallest travel distance from it
    (movement points over the route; the earlier point on a tie). If that distance is 0,
    the ship is on the warp point and gets no order this turn. Otherwise it gets a Seek
    toward the point's sector, followed by a Warp through the point when its movement
    points at that moment (§7.5 *Space Yard Ships* says which value that is) reach the
    distance. The point then leaves the list.
  - A Seek lasts one movement phase, so an explorer that cannot reach its point this turn
    is planned again on the next one, and it jumps only on a turn it starts within reach
    of its point.
  - OpenSE4 follows this since 2026-10-02 (`planExploration`, `ai_explore.cpp`; the
    travel distance is the route's length in movement points, `findPathToNearest`, and a
    point no route reaches is never taken). Its choices where the text leaves room are in
    question 64: the minister stops at once while no point is free, so the 3× rule never
    applies; an explorer on its point keeps the point in the list; a turn-based explorer
    compares the movement of this turn's run; the fleets that explore (§7.5 `AI_Fleets`,
    leftover fleets) keep the Seek toward the first free point and the Warp.
    The engine used to plan only idle ships, without the supply and half-full cargo tests,
    to count the explorers for the 3×, 5× and 8× tests, to pick by jumps and then sector
    distance, and to give a Move To and a Warp that lasted until done.
- **Patrol** (confirmed: binary): idle warships not in fleets move to our colony with the
  fewest of our ships present, the smallest population breaking ties.
- **Mines, satellites and drones** (confirmed: binary).
  - Sweepers sweep. Mine and satellite layers, both launches below and the orders to idle
    drones work only while the empire has fewer placed units than the game's unit limit.
  - *Layers*: an empty layer goes to the nearest own colony holding mines (satellites) in
    its cargo and loads them. A loaded layer picks among the warp points of our colony
    systems whose far system holds any object of another empire. Each other empire with
    any object there adds its own weight to that warp point, and the weights add up:
    mines 1, or 4 when that empire is hostile and 7 when at war; satellites 1, or 2 when
    hostile (confirmed: binary). The per-sector cap counts only our units of the layer's
    kind (mines for a mine layer, satellites for a satellite layer) in the far system at
    the sector number of the warp point in our system, and both kinds are compared with
    `Maximum Mines Per Player Per Sector`.
  - *Layer fallback* (confirmed: binary): a satellite layer with no candidate, and a mine
    layer with no candidate or on a turn with the star-destroyer flag set (it then ignores
    the candidates), first draws one random quiet colony system: one of our colony systems
    where no other empire has any object. With none, no order. With the flag set, half the
    time it takes a random star of that system (no star: no order this turn); otherwise
    it takes a random warp-point sector of that system (none: no order). The flag is
    evaluated only on turns whose date is a multiple of 20 (it is off on all others): it
    is set when some design, our own included, whose seen date for us is at most 20 turns
    old has `Destroy Star`, `Create Nebulae` or `Create Black Hole`. Our own designs get
    such a date when they fight in a battle (§8 "Design knowledge").
  - The orders are Move To the chosen sector, then Launch Units Remotely.
  - *Satellites*: total = the empire's Satellite and Recon Satellite units everywhere (in
    space and in every planet's and vehicle's cargo); stored = those in any cargo. If total
    > 0 and stored / total × 100 exceeds `Percentage of total satellites to keep as
    planetary cargo`, the excess is trunc((stored / total × 100 − kept %) / 100 × total).
    Only when that excess is positive are own colonies holding satellites taken, in planet
    order: while the excess is at least 1, each gets a Launch Units Remotely (satellites)
    order where it is, which launches all it may (spec 03 §12), and the excess drops by its
    whole satellite cargo. Once the excess is used up, colonies whose cargo holds a Recon
    Satellite still launch.
  - *Drones*: Anti-Ship and Anti-Planet drones are counted the same way against
    `Percentage of total drones to keep as planetary cargo` to give the excess. Each half
    gets excess div 2, so an odd one stays. The ship half is capped at the ship targets ×
    `Number Of Anti-Ship Drones Per Target`, the planet half at the planet targets × `Number
    Of Anti-Planet Drones Per Target`. Colonies holding Anti-Ship drones, in planet order,
    launch all their drones (with no target) until the ship half is used, each launch
    taking all the drones in that cargo off the half; colonies holding Anti-Planet drones
    do the same for the planet half. A colony can launch for both.
  - Anti-ship targets are ships inside our territory of empires at War with us.
    Anti-planet targets are planets of such empires among the attack candidates.
  - Idle drones in space within `Maximum Anti-Ship` (or `Anti-Planet`) `Drone Target
    System Distance` jumps are then sent after the targets, per-target times.
- **`AI_Settings`** (confirmed: binary):
  - `Turns to Wait until next attack` is the attack gap of §7.2;
  - `Maximum Systems to Defend at a Time` caps the defend list;
  - the RP and IP caps and `Maximum Maintenance Percent of Revenue` are used as above;
  - the anger keys are covered in §7.3;
  - the drone and satellite keys are covered above;
  - `Personality Group` is covered in §7.1;
  - `Max Ship Size Tonnage From Start N Amount` and `… N Num Turns` (N = 1–3) are early
    size caps. While fewer than Num Turns turns have passed, hulls above Amount are not
    used. Each pair applies on its own, only when Amount > 0, and to every vehicle type.
  - Defaults when a key is absent: no size caps, attack gap 0, maintenance 80 %, research
    and intelligence caps 300 000, 3 systems to defend, anger over allied planets on and
    over enemy planets off, both anger percentages 5, personality group 0, movement flags
    off, 40 % of satellites and of drones kept as cargo, 3 drones per target of each
    kind, and drone target distances of 5.

  Each turn the four movement flags (minefields, restricted systems, clear orders on
  meeting an enemy, clear orders on meeting anyone) are copied into the computer empire's
  own movement options.
- **`AI_General`**: identity fields (names, descriptions, `Demeanor`, `Culture`,
  `Happiness Type`, planet and atmosphere, design-name file), plus three `Race Opt` build
  variants. A random computer player uses the variant of the game's racial-point level
  (§7.1, confirmed: binary).
- **`AI_Speech`** (confirmed: binary): numbered text pools of at most 20 lines each:
  - `Send <Type>` for each of the 38 message types: the text of every message the AI
    starts or answers with a message of that type (proposals, counter-proposals, answers to
    trades, gifts and tributes, gifts and tributes it grants, declarations, breaks, demands,
    surrender, chatter);
  - `Response Friend/Enemy <Type>` for accept and refuse treaty, break treaty, declare war,
    accept and refuse trade, accept and refuse gift, accept and refuse tribute, want a gift,
    want a tribute, demand surrender, the generic demand/request/warn, surrender and grant
    independence: the text of the General message that answers such a message;
  - `Response Friend/Enemy YES/NO <Demand>` for the 13 demands from "remove ships" to "stop
    attacks in a system": the text of the Accept Demand (YES) or Refuse Demand (NO) reply;
  - miscellaneous friend and enemy chatter: loaded but never used;
  - `Mega Evil Declarations`: the text of a war declaration against the MEE.

  A line is drawn at random from the pool. Friend or Enemy follows the AI's treaty with the
  other empire when it answers: Non-Aggression or better is Friend. A message whose pool is
  empty is not sent at all; a war declaration with an empty pool still sets the anger to
  100 but declares nothing, and an accepted demand is still carried out (§7.4). In the stock files the pools of the generic demand/request/warn
  and of the Accept and Refuse Demand messages are empty.
- **`AI_Strategies`**: the same schema as `DefaultStrategies.txt` (§7.7). Other tables
  refer to it by name.

### 7.6 Mega Evil Empire (MEE)

`Settings.txt` has four keys: `AI Uses Mega Evil Empire`, a threshold in thousands of
score points, and separate score percents for human and computer players [D].

Each computer empire works out the MEE for itself once per turn (confirmed: binary):

1. There is none unless `AI Uses Mega Evil Empire` is true.
2. The candidate is the highest-scoring empire other than the evaluating one, among
   empires not yet eliminated, whose score is strictly greater than threshold × 1000.
   Ties go to the lower player number. Neutral empires are not excluded.
3. The percent is the `Human` key if the candidate is played by a human, else the
   `Computer` key.
4. The candidate is the MEE if its score is at least round(S × percent / 100), half to
   even, for every other empire not yet eliminated, where S is that empire's score. The evaluating empire
   counts too. In effect the MEE must lead the next-best score by that percent.

Scores are the ones from §5. AIs then add the MEE anger each turn and may make an MEE
declaration (§7.3, §7.4).

### 7.7 Player default lists (not used by the AI) [M]

- **`DefaultDesignTypes.txt`**: design-type labels. Ministers use them to find, for
  example, colony ships. The AI tables and ministers work with a fixed set of 39 design
  types built into the game, and none of them is a scout (confirmed: binary).
- **`DefaultColonyTypes.txt`**: colony-type labels. A planet's colony type selects its
  facility-minister queue. Only the nine fixed names of §7.5 mean anything to the
  ministers; any other label is built like Homeworld (confirmed: binary).
- **`DefaultStrategies.txt`**: the starting combat strategies. Fields:
  - `Primary/Secondary Movement Strategy`;
  - `Targeting Priority 1..4`;
  - `Use Type Priority First`;
  - `Type Priority` and `Dont Fire On` per target class;
  - `Fighters Launch Group Amount`;
  - `Break Formation` per class;
  - damage-percent thresholds for ships, planets, fighter groups and satellite groups;
  - `Damage Until All Weapons Gone`.

  Their combat semantics belong to the combat spec.

## 8. Turn processing order (confirmed: binary)

The original resolves a turn in three layers: a few steps for the whole game, a fixed list
of steps run for each empire in turn ("end-of-turn processing"), and a few global steps
after all empires. Empires are always taken in empire-number order, and destroyed empires
are skipped.

**Simultaneous game** (the host's turn processing):

1. **Orders.** Each player's orders file is read, in player order. A player with no file
   (or a stale one) is played by the computer for this turn: all its ministers are switched
   on for the turn and restored afterwards.
2. **Messages.** Each player's outgoing messages are processed, player by player (§3.4).
3. **Date.** The date advances by one turn.
4. **Start of turn**, for each empire: its ministers and, for computer players, the AI act
   (orders, queues, research and intel choices, diplomacy). AI messages take effect as they
   are sent. The Politics minister acts first (§7.1), so the treaties it changes are in
   place before the empire's other ministers give their orders.
5. **Movement and space combat**: 30 movement phases, each followed by combat where it
   applies (§9.3). Colonize orders are carried out like any other order, on one of the
   ship's acting days with movement left (spec 03 §6.3, §8), so a colony can be founded in
   any phase.
6. **End-of-turn processing**, for each empire in order (the list below), each followed by
   the destruction check for that empire (§6). The list of empires is fixed when this step
   starts: an empire founded during it (a rebel colony) is first processed next turn.
7. **Design cleanup** every tenth turn (each new year). Then, every turn, the **contact
   check** (§3.1): empires that no warp path links any more lose contact (confirmed:
   binary).
8. **Victory check** (§6).
9. **Event step** (§4): hazard damage, due timed events, then the new event roll.
10. Per-turn marks (bookkeeping) are cleared, a new random code for the turn is drawn (the
    orders files are checked against it, §9.2), and the game is saved and sent out.

**Turn-based game** (confirmed: binary): each player's movement, combat and diplomacy
happen live during that player's own turn. When a player ends the turn, that empire's
end-of-turn processing runs at once and the turn passes to the next player number. The
number of empires is read again each time, so an empire founded during the game turn (a
rebel colony) plays when its number comes up, in the same game turn. After the last player
the date advances, then the design cleanup (every tenth turn), the contact check, the
victory check and the event step run, as in steps 7–9 above; step 10's per-turn marks are not cleared in
turn-based games. A player's turn starts like this:

1. A human's turn starts with the destruction check (§6).
2. The start-of-turn step: the AI state update and the ministers (all of them for a
   computer player, the Politics minister first) give their orders. Messages take effect
   when sent.
3. The player's vehicles get their movement back, and then every vehicle and fleet of the
   player, in object order, carries out its orders, those just given by the ministers
   included. Each goes on until an order is not finished (no movement left, or it waits),
   fails (the list is cleared) or the list is empty, and completes at most 21 orders.
   The limit counts the orders completed in one run of the list (an order that finishes
   leaves the list, or goes to its back under Repeat); removals outside a run, such as
   combat ending a Sentry, do not count. Each run counts afresh: this one, each
   simultaneous movement phase, and in a turn-based game each order a human gives, which
   runs the list at once (confirmed: binary).
4. A computer player's destruction check comes only now, after its start-of-turn step. Its
   turn then ends at once.

A human then plays, and orders execute as they are given (spec 03 §6.3). The date stays
the same for the whole game turn, so every player's start-of-turn and end-of-turn
processing sees the date before it advances; "every N turns" rules test that date. In a
simultaneous game the same steps see the advanced date.

**One message per recipient per turn** (confirmed: binary) is read from the recipient's
log: a political message from us dated the current turn blocks another. In a turn-based
game the limit therefore lasts until the game turn ends.

**End-of-turn processing of one empire** (in this order):

1. The list of the empire's construction queues is rebuilt (bookkeeping only), and its
   ministers (all of them for a computer player) take their end-of-turn actions (§7.1,
   group 2).
2. For a human player: the statistics, history and log text files are written (§3.4, §5).
3. **Intelligence** (§2.1, §2.4).
4. **Research** (§1.4). Steps 3 and 4 spend the pools filled at the previous turn's step 5
   and 6, then empty them.
5. **Income**: the production of each of the five kinds (spec 02 §5), plus remote mining and
   other sources; tariffs paid to a master (§3.3); the computer-player bonus (below); the
   result is added to the pools (each capped at 2,000,000,000).
6. **Treaties and trade** (§3.2, §3.3): the consistency check, a master's view of its
   subject's designs, trade income from every partner, Partnership maps and designs, and
   the trade counters.
7. **Maintenance** (spec 02 §7), including abandoned ships and unit groups on a shortfall.
8. **Planets**: per planet upkeep, cargo and storage, population growth (every `Reproduction
   Check Frequency` turns), planet changes and plague damage (spec 02).
9. **Happiness** (spec 02 §4), skipped for a race with `Population Emotionless`.
10. **Construction** (spec 02 §6).
11. **Repair** (spec 03).
12. Knowledge of foreign designs seen more than 50 turns ago is forgotten.
13. **Supply**: resupply and supply use; ships out of supply may be destroyed (spec 03).
14. **Storage cap**: minerals, organics and radioactives are cut to the storage capacity.
    Research and intelligence pools are not capped.
15. **System-wide abilities**: planet value and condition changes, population changes,
    ship and fleet training, and similar abilities that act each turn.
16. Each ship, base, fighter group and drone group of the empire records its current sector
    as the sector it comes from (spec 04 §3). Nothing else happens in this step (confirmed:
    binary). In a turn-based game a vehicle that moved during its owner's turn therefore
    counts as an arrival in battles only until its owner's end-of-turn processing.
17. **Ground combat** (confirmed: binary): for each colony **of this empire** on which
    another empire's troops have landed, the ground combat goes on when that empire is below
    Non-Aggression with this one (spec 04 §13). The fight runs in the defending owner's
    processing, not the invader's. If the landed troops belong to the owner itself or to an
    empire now at Non-Aggression or better, they are put into the colony's cargo instead.
    In a turn-based game a fight is shown, after a notice, in the Ground Combat window
    unless both empires are computer-controlled, and the processing waits for it (spec 06
    §1.10.6) (confirmed: binary).
18. The log is pruned to the last turn's entries (§3.4).

**Computer-player bonus** (confirmed: binary): for a computer-controlled empire, each of
the five incomes at step 5 is multiplied after tariffs by 1, 2, 3 or 5 for the four
settings of the "Computer Player Bonus" option (in list order, the first being none), then
rounded.

**Consequences**:

- Points shown during a turn were produced at the end of the previous turn and are spent at
  the end of this one (§1.1). Trade points follow the same delay.
- Intel attacks resolve during the attacker's processing, so empire order decides whether a
  defender's defenses already hold this turn's points (§2.4).
- In simultaneous games messages take effect before movement, research and intelligence;
  in turn-based games they take effect when sent (§3.4).

**Design knowledge and step 12** (confirmed: binary): each design carries, for every
other empire, the date that empire last saw it (none means unknown). Step 12 drops a date
more than 50 turns old: seen at turn T, the design is still known at T + 50 and forgotten
at T + 51. A date is set to the current turn when:

- the empire fights a battle in which a vehicle of that design takes part, or which a
  unit of that design joins in a piece's cargo (every participant learns every piece);
- one of its vehicles strikes a mine field of that design;
- it steals the design (§2.3);
- it receives a ship or a planet in a trade, gift or tribute: the ship's design and the
  designs of the units in the cargo;
- an empire surrenders to it: every design of the surrendering empire, and every design
  the surrendering empire knew whose owner can still build it (has the technology for its
  hull, parts and mounts);
- a human player's screen fills in the report of a foreign vehicle that its long-range
  scanners reach and that carries no Scanner Jammer: in the main screen's report panel or
  in a report window, whatever tab is showing (confirmed: binary). The report of a ship or
  base dates only its own design, never the units in its cargo; the report of a fighter,
  satellite, drone or mine group in space dates its units' design. Each display renews
  the date. Nothing is learned from scanners until a report is shown, and computer
  players never learn this way. In a simultaneous game the player's machine dates with
  the unadvanced date, and only the ship and base reports reach the host, through a
  "viewed this turn" mark sent with the orders; dates from unit-group reports never
  reach it.

Two treaty rules copy dates at step 6 of the receiving empire:

- Partnership: every design the partner knows with a later date than ours takes the
  partner's date.
- Subjugation: the master learns every design of its subject that it does not know and
  that the subject can build, dated with the **design's creation date**, not the current
  turn. Step 12 comes later in the same processing, so a subject's design made more than 50
  turns ago is forgotten again at once, and the master never has it during its turn.

**OpenSE4 mapping**: `processTurn` (`turn.cpp`) follows this order for a simultaneous game,
and `empireEndOfTurn` is one empire's end-of-turn processing; the lines of a human
player's files (step 2) come back in `TurnResult::records`. A turn-based game
(`turn_based.cpp`) starts each player's turn as listed above (a human's destruction
check, the start-of-turn step, the movement refill and every group's orders, a computer
player's destruction check); `applyLive` carries out each order as it is given
(`movement::runLive`), `endPlayerTurn` runs `empireEndOfTurn` and passes the turn on, and
after the last player the date, design cleanup, victory check and event step run. Every step has a stable iteration order and draws its randomness from
`GameState::rng`. Open questions 24 (both styles) and 32 (turn-based games) record how
each detail of this section was settled; where the engine still differs from the rules
above is listed in docs/PARITY_GAPS.md. Network and play-by-e-mail hosts run the same calls
(§9.5).

## 9. Multiplayer

### 9.1 Modes [M]

- **Play Style**: Hotseat or Different Machines.
- **Turn Style**:
  - **Turn-Based**: players act in sequence, and orders execute immediately. On different
    machines the save file passes from player to player. (confirmed: binary) When a human
    ends the turn, the computer players' turns up to the next human run at once, and the
    game is saved for that human; the player is offered to quit. The file is passed on by
    hand. Loading it asks for the password of the empire whose turn it is and starts that
    turn (§8). There is no host, no time limit and no check that a file is the latest one.
    The very first turn of player 1 is played on the machine that created the game.
  - **Simultaneous**: players only give orders, and a host resolves them all at once. It
    also works hotseat, with automatic hosting.
- **Connection Type** (Different Machines + Simultaneous only): manual file moving
  (PBEM), TCP/IP Host or TCP/IP Player.
- **Also set here**: the game file name, a shared save directory, the autosave frequency,
  and a **master password**, which simultaneous games on separate machines require [H].
  Each empire may add its own password.

### 9.2 Simultaneous files and flow [M][H]

- **Files**:
  - `.emp`: a player's empire, sent before the start.
  - `.gam`: the full state, produced by the host. (confirmed: binary) There is one game file
    for all players. It holds every empire's full record, every design and every object,
    including those a player cannot see (cloaked ships and colonies, mines, other empires'
    fleets); nothing is filtered per player. A player's copy hides them only in its windows
    (spec 01 §6.9 "What other players see"), so a player reading the file with another tool
    sees everything. The e-mail host writes this one file; a TCP/IP host sends every player the
    same game file, the same combat-replay and movement-replay files, and that player's own
    history and statistics files.
  - `.plr`: one player's turn changes, deleted after processing.
  - `.trn`: the movement replay.
  - `.cmb`: the combat replays.
  - `<game>_events.txt` and `<game>_stats.txt`: per-player history and statistics [M]. The
    game itself keeps them, and the text copy of the log, in a history folder of the
    installation, one set per player number (confirmed: binary).
- **Flow**:
  1. The host fixes the settings, and the players send their `.emp` files.
  2. The host creates the game and sends out the `.gam`.
  3. Each player logs in and plays. Saving mid-turn is allowed. End Turn writes the
     `.plr`. (confirmed: binary) The `.plr` is written to the game's multiplayer folder
     (the shared folder chosen at setup, or the default save folder when none is set or it
     does not exist), named after the game and the four-digit player number. Saving
     during the turn writes the same `.plr` (the player is told to send it to the host);
     the `.gam` is not changed. The player's side keeps a copy of the turn's `.gam` for the
     movement replay. It never reads a `.plr` back: only the host's turn processing reads
     them. A `.plr` is a snapshot of the player's own part of the game (its empire record, its
     view of the systems, its designs, and those of its objects that got a command or a
     queue change during the turn, each with its state and orders), not a list of commands
     (confirmed: binary; spec 01 §14 Q44).
  4. The host loads the game with the master password and processes the turn:
     - the AI plays any empire whose file is missing, making only minimal changes if that
       empire set the option "AI should not make changes during a Simultaneous game" [M];
     - missing or out-of-date files trigger warnings. (confirmed: binary) Each `.plr`
       carries the game's date, a random code the host draws for each turn and a code for
       the game. A file for another date, another turn or another game is reported, and
       the host may go on with that player played by the computer. The files are deleted
       after processing.
     - (confirmed: binary) A missing orders file is reported ("Player File Error") only for an
       empire that is human-controlled and not destroyed, and never in hotseat or when
       processing without dialogs. An orders file restores the player's own part of the
       empire: its minister settings, its password and e-mail, its options, its vehicles,
       fleets, designs and so on. It never restores the empire's name, its computer-controlled
       mark or its difficulty.
     - (confirmed: binary; spec 01 §14 Q44) The files are read in player order at the start
       of processing (§8 step 1). Reading a file replaces each object it carries that the
       player still owns on the host (others are skipped), recalculating the colonies among
       them (spec 01 §6.9). The player's treaty with each empire is taken only where it
       differs from the host's and is None, "no contact" or War, and the player's log comes
       from the file. Then sight is recalculated in every system; no first-contact check
       runs.
  5. The host sends out the new files.
- **Password reset**: the host may reset players' passwords between turns (Reset Passwords,
  spec 06 §1.9). The new password is shown to the host in a message and takes effect at the
  next processing, after the orders files are read; nothing tells the player (confirmed:
  binary).
- **Player turn**: the player may change queues, research, intel, messages, designs and
  strategies, and give orders to ships and **planets**. Planet orders exist only in this
  mode.
- **Restrictions**:
  - Cargo transfer and launch/recover become orders.
  - Attack becomes a pursuit of a moving target.
  - There is no tactical combat.
- **Headless hosting**: the command-line arguments are the save path, the password, the
  player number (0 = process the turn as host) and an optional mod path, plus a flag that
  suppresses dialogs [H].

### 9.3 Simultaneous movement and combat timing

- **Movement**: a month has 30 phases (days). A ship with speed S moves every 30/S days;
  speed 5 moves on days 6, 12, …, 30 [M]. The exact day schedule, for every speed, is a
  per-vehicle counter that gains S / 30 a day and lets the vehicle act when it reaches 1
  (spec 03 §6.3, confirmed: binary; the counter is kept in floating point at the x87's
  64-bit precision, and spec 03 open question 8 covers what that does to some speeds).
- **Combat timing** [M][H]:
  - The original rule held combat on every 5th day in any sector with hostiles.
  - v1.15 made combat depend on movement.
  - From v1.42, at most one combat happens per sector per phase, and only where some ship
    executed orders that phase. The settled rule (spec 04 §2, spec 03 §6.3 step 6,
    confirmed: binary): after each day, every sector where an object (a vehicle or a
    colony) carried out any order that day, a waiting Sentry included, is checked, and a
    sector that already had a battle that game turn fights again only when a newcomer is
    there or a survivor of that battle was damaged.
- **Combat order**: the defending side moves first in simultaneous combat [H]. The setting
  `Simultaneous Games Show Strategic Combat` controls whether players see these battles.
- **Tie-break**: within a day, vehicles act in the order the objects were created (spec 03
  §6.3, confirmed: binary). Placement and turn order inside a battle are spec 04 §3.

### 9.4 TCP/IP [M][H]

TCP/IP runs the same file flow over the network, with the host as the hub.

**Host states**:

1. Waiting for connections.
2. Sending the setup file.
3. Waiting for empire files.
4. Ready for the first turn.
5. Generating a turn.
6. Sending the turn file.
7. Waiting for order files.
8. Ready for the next turn, then back to state 5.

**Player states**:

1. Ready to connect.
2. Connecting.
3. Waiting for the other players.
4. Waiting for the setup file.
5. Creating an empire.
6. Sending the empire file.
7. Waiting for the turn file.
8. Taking the turn.
9. Sending the order file.

**What the host can do**:

- Process a turn with order files missing; the AI plays those empires.
- Add its own empires, one at a time.
- One button whose meaning follows the host state (confirmed: binary):
  - while waiting for connections or for empire files, it removes the selected connected
    player;
  - when ready for the first turn, it removes the selected empire, and later empires are
    renumbered;
  - while waiting for orders files and when ready for the next turn, it is "Toggle Empire AI
    On/Off": it asks "Change Empire Control", naming the empire and whether it goes to AI or
    human control, and flips only the empire's computer-controlled mark (unlike the Players
    window, ministers and individual flags are untouched). The player column then shows
    "[Computer]", or "[Host]" for an empire handed back (no player is connected to it and the
    host plays it).

  The host waits for orders files only from connected players. An empire handed to the
  computer is played each turn by the stand-in rule (§7.1: all ministers on for the turn,
  restored afterwards). Open: needs observation (or a resource lookup): the button's caption
  in the earlier states.
  OpenSE4 follows this since 2026-10-01: `HostSession::setAiControl` (`src/net/host.cpp`)
  flips the empire's kind (`ai::setComputerMark`), and the in-game host's Empires list
  (network status strip) asks "Change Empire Control" and calls it. A kicked player's empire
  is still played by a stand-in with the mark unchanged (`Slot::aiControl`), and an empire
  that was a computer player from the start cannot be handed to a human (inferred).
  Turn-based TCP/IP is an OpenSE4 extension, so its behaviour there is our choice.
- Play any empire's turn.

**Other behavior**:

- Players reconnect by player name, and the host then resends the current turn.
- There is a broadcast chat.
- Resuming means loading the game as host.
- **Computer players' messages are delivered twice** (confirmed: binary). In a
  simultaneous game every message a computer player sends during the host's start-of-turn
  step (§8 step 4) takes effect at once and is also kept in that empire's outbox. Step 2
  of the next turn processing delivers every empire's outbox, then empties it. The outbox
  is saved only in a player's orders file, never in the game file. An e-mail or
  shared-folder host quits after it processes a turn and loads the game file again for
  the next one, and a Hotseat game reloads the game file after each processing, so their
  outboxes are always empty and each message arrives once. A TCP/IP host keeps the game
  in memory from turn to turn, so at its next step 2 it delivers each computer player's
  messages of the turn before a second time. Each lands in the recipient's log again,
  dated the date before it advances (the same date as the first delivery), and its effect
  is applied again: a treaty accepted, a treaty broken or a declaration of war sets the
  treaties again, and a surrender hands over whatever the surrendered empire still owns.
  A repeated acceptance of a trade, gift or tribute moves items only when the newest
  political message from the other empire in the accepting empire's log, among those
  dated the turn before or later, is an offer of that kind; it is then that newer offer
  that is carried out. The offer accepted the first time is too old to be found. OpenSE4
  does not reproduce this double delivery (question 48).
- The stock ports are UDP 6716 for control and TCP 6720 for files.

### 9.5 Mapping to OpenSE4

- A `.plr` corresponds to the ordered `game::Command` list of one empire for one turn
  (an `EmpireOrders`). Design, strategy and queue edits must also be commands.
- A `.gam` corresponds to a serialized `GameState` plus the per-empire logs and
  statistics.
- The host is authoritative, clients validate only against their own knowledge, and a
  missing empire falls back to the AI. This matches ENGINE.md's rule of sending state
  rather than a seed.
- **Turn-based games on different machines.** The original passes the save file from
  player to player (§9.1), and offers TCP/IP only for simultaneous games. OpenSE4 keeps a
  host in charge of the game in both network and e-mail play (an OpenSE4 extension, open
  question 33):
  - **Network (an OpenSE4 extension).** The host runs `resumeTurnBased`, `applyLive` and
    `endPlayerTurn`. Only the player whose turn it is may send commands; each one is
    carried out on the host at once. The computer players' turns run on the host. A
    player who is away is waited for until the turn time limit, a forced turn or a
    hand-over to the computer. The computer then plays the rest of that turn as it plays
    a missing player in a simultaneous game (§7.1: all ministers on, or bookkeeping only).
  - **E-mail.** The player plays their turn on their copy of the `.gam`, and the `.plr`
    carries that player's commands in the order given, with checksums of the game before
    and after. The host replays them with `applyLive`, runs `endPlayerTurn` and sends the
    new `.gam` on to the next player. A missing `.plr` means the computer plays that turn.
- **What a client receives.** The original sends every player the whole game (§9.2) and hides
  things only on display. OpenSE4's TCP/IP host sends each player a redacted view instead (an
  OpenSE4 choice; its e-mail game file is the full state, like the original's). Either way the
  client must apply spec 01 §6.9 "What other players see" to what it holds, so that the visible
  behaviour matches the original.
- **The player's side of an e-mail game** (both turn styles). The game client opens the
  `.gam`, the player picks their empire and gives its password, which is checked against
  the empire's verifier as the host will check the `.plr`. The turn is played as in a
  local game of that style, and End Turn writes the `.plr` instead of processing the turn.
  A turn in progress can be saved and finished later (step 3 of §9.2). The engine's choices
  are an OpenSE4 extension (open question 36 compares them with the original).

## 10. `Settings.txt` keys in scope [D]

- **Trade and tariffs**: `Maximum Trade Percentage` (the cap on the trade percentage),
  `Treaty Subjugated Resource Percentage` and `Treaty Protectorate Resource Percentage`
  (the tariff rates) (§3.3, confirmed: binary).
- **Events and intelligence**: `Event Percent Chance Low/Medium/High` (the single
  galaxy-wide chance per turn, §4) and `Intelligence Defense Modifier Percent` (a
  multiplier on defense strength, §2.4) (confirmed: binary).
- **AI**: the MEE keys (§7.6); `Random Player Personality Groups` and `… Group N
  Percent`; `Minimum/Maximum Computer|Neutral Player Low|Medium|High Setting`.
- **Multiplayer**: `Simultaneous Games Show Strategic Combat`.
- **Logs**: `Create Log Text Files for Players`, `Create Log Text File for Game`, `Use Old
  Log Political Message Display` (UI only).
- **Race characteristics** relevant here: Intelligence (research output), Cunning
  (intelligence output) and Political Savvy (the trade factor, §3.3) (confirmed: binary).
  Each has min/max % and point-cost keys.

## Open questions to verify in the running game

1. **Tech cost curve**: answered (confirmed: binary). The options are Low, Medium (the
   default) and High: linear, half-quadratic with a linear floor, and quadratic. §1.3 gives
   the formulas and the costs of levels 2, 3, 5 and 10 at `Level Cost` 5000.
2. **Research leftovers**: answered (confirmed: binary). Points not absorbed are lost in
   both modes: the pool is emptied, and Divide Evenly shares are not capped, so a finishing
   project's excess is lost. An area can be queued only once. A project completes at most
   one level per turn (§1.4).
3. **Opening RP/IP pool**: answered (confirmed: binary). Starting Resources (5,000,
   20,000 or 100,000) plus one turn of production; intelligence starts at 0 (§1.1).
4. **RP/IP modifiers**: answered (confirmed: binary). Cunning changes intelligence output and
   nothing else; Intelligence changes research output. How the modifiers stack is spec 02
   §5.1 and §5.5: the racial, mood and population terms add up into one percentage, while
   the planet and system modifiers are separate multiplications.
5. **Intel success**: answered (confirmed: binary). There is no success roll and no "Any"
   bonus; counter-intelligence is a deterministic sum that drains the defenses it uses; the
   victim names the source with a 1-in-5 chance; a defense project does finish, and then
   deletes one hostile project (§2.1, §2.4).
6. **Intel details**: answered (confirmed: binary, §2.3). Research - Steal gives one level
   (and never works with "Any"); a rebel planet becomes independent, joins the source or
   stays, by fixed odds; Lose Supply removes the amount; Disrupt Trade restarts the trade
   counter; Lose Movement takes movement points from the current turn; faked messages are a
   real declaration of war. `Ship - Cargo Damage` destroys the whole cargo and ignores
   Amount; `Planet - Cargo Damage` deals Amount damage points, to the people first, then to
   the units. A stolen blueprint does not join the thief's designs: the thief only learns
   (dates as seen) the newest built design it does not know. OpenSE4 matches.
7. **Score weights**: answered (confirmed: binary). The formula is in §5.
8. **Treaty combat and trade**: answered (confirmed: binary). Subjugation and Protectorate
   allow no trade. Empires fight when their treaty is below Non-Aggression: War,
   Non-Intercourse, None and no contact fight; Subjugation and Protectorate never do (spec
   04 §2, §3.2).
9. **Trade base**: answered (confirmed: binary). The base is the partner's production
   before tariffs and maintenance; Political Savvy, racial Trade traits and the culture's
   Trade value add up into one factor; tariffs also take research and intelligence
   points, which nobody receives (§3.3).
10. **Events**: answered (confirmed: binary). One roll per turn for the whole galaxy;
    targets are drawn from the whole galaxy with luck checks; `Ship - Moved` sends a ship
    to a random system anywhere; a destroyed star or planet has the same result as the
    stellar manipulation; High and Catastrophic planet and star events never pick a home
    planet location (§4). What the stellar manipulations do is spec 01 §9 (confirmed:
    binary).
11. **Victory**: answered (confirmed: binary). The original names no winner: a met
    condition ends the game and the Scores ranking tells the result. The tech percentage
    compares all levels against the allowed areas the race can see. An empire is destroyed
    when it has no populated planet and no ship or base (§6).
12. **AI**: answered (confirmed: binary).
    - Anger range and mood labels: 0–100, starting at 50, with nine labels (§7.3).
    - Acceptance probability: there is none. Acceptance is a fixed threshold test, and
      "Minimum Anger Chance" is a floor on the threshold (§7.4).
    - AI-state transitions: §7.2. Three of the ten states are never entered.
    - MEE baseline: §7.6.
    - Difficulty and Bonus: §7.1, §7.2 and §7.5.
    - Trade item values: §7.4. A system is worth 100,000 per planet in it that the receiver
      could colonize, when the giver claims it; a ship is worth 100 × its scrap value, a
      quarter of that when it carries no weapon.
    - `Spaces Per One`: floor(hull tonnage / N) copies, at least one (§7.5).
    - `AI_Planet_Types` value thresholds: only thresholds above 100 count. They are
      absolute minimums, scaled by the settings' mid value under finite resources (§7.5).
    - Scouts: none. There is no scout design type, and idle attack ships explore (§7.5).
    - Queue commitment: vehicles get one turn of net income, and only queues with under 5
      turns of backlog. Facilities: one per empty colony queue per pass. The AI never sends
      gifts or tributes on its own; its own demands are listed in §7.4.
    - Race folders: they may override every table, including `Construction_*`,
      `Planet_Types`, `Speech` and `Strategies` (§7.2).
    - Formerly open, now settled: the repair and resupply thresholds, where Space Yard
      Ships go and what the Stellar Manipulation minister does (§7.5 logistics); how each
      speech pool is tied to each reply (§7.4 "Which messages get an answer", §7.5
      `AI_Speech`); and which design a non-type `Type` entry of `AI_Construction_Vehicles`
      builds: the AI's own design made from the `AI_DesignCreation` template of that name
      (§7.5). Where OpenSE4 differs is listed in docs/PARITY_GAPS.md.
    - The details OpenSE4 had filled in by its own choice are settled too (confirmed:
      binary), each in the section named:
      - who "started" a combat: the "current player" when the battle is fought (§7.3);
      - the "combat bonus" in a ship's strength rating: `Boarding Attack`; carried fighters
        add their damage and the half shields keep their fraction (§7.2);
      - a planet's defence in an attack candidate's value: shields / 5, population div
        100 and its cargo units' damage, added to the foreign ratings in its sector (§7.2);
      - the colony value at stake that orders the defend list: our colonies' maximum
        population in the sector; entries are kept per sector and owner (§7.2);
      - the threat a defence commitment is measured against: the entry's sector threat
        (§7.2, §7.5);
      - "our strength within 4 jumps of each target": counted per target, so a system near
        two targets counts twice, from the 6th turn in the state (§7.2);
      - jumps for the staging system, the 4-jump test, Secure Holdings' neighbours, the
        territory and the Not Connected test: over every warp link, known or not (§7.2);
      - a planet's worth in trades: §7.4;
      - the design-name counter: the designs the Design minister has made (§7.5);
      - a Race Opt trait that does not fit is skipped (§7.1);
      - designs of no AI type are typed automatically by what they carry (§7.5);
      - the systems an accepted demand marks to avoid are forgotten with the other queues
        every 10 turns, as in OpenSE4;
      - "within 2 jumps of X's territory" counts over every warp link, as in OpenSE4;
      - where mine and satellite layers lay their units: warp points toward other empires
        (§7.5);
      - Allow Surrender is a game option, on by default (§7.4);
      - presence for colonization danger: any object, seen or not, and neighbours counted
        per warp point (§7.5);
      - the speech pools: §7.4 and §7.5.

      Where OpenSE4 still differs is listed in docs/PARITY_GAPS.md.
13. **Contact loss**: answered (confirmed: binary; corrected on 2026-09-30, the earlier
    answer "never lost" missed the check). Contact is lost when no warp path leads from an
    empire's colonies to a colony of the other empire, checked for every pair once per game
    turn, after the design cleanup and before the victory check, in both turn styles. An
    empire that finds no path resets its own treaty to "no contact", drops its intelligence
    projects against the other and logs "contact lost". First contact needs the same path, so an
    empire without colonies neither makes nor keeps contact (§3.1, §8). Destruction also
    ends contact. The engine differs: it never loses contact and makes first contact on
    mutual detection alone. It must add the check of §3.1 to both turn styles, require the
    path for first contact, and record the contact-lost line in the history (§3.4).
14. **Simultaneous timing**: answered (confirmed: binary). Messages are processed before
    movement; research and intelligence run after movement, in each empire's end-of-turn
    processing (§8). The day schedule and the order within a day are spec 03 §6.3 (§9.3).
    The game never changes the x87 precision (64-bit mantissa, round to nearest); what that
    means for the floating-point day counter is spec 03 open question 8.
15. **Tariff base** (§3.3): answered (confirmed: binary). The tariff is cut from the
    subordinate's whole non-trade income of each kind (colony production after the income
    floor, remote mining and `Generate Points` income), at its income step before the
    computer-player bonus; trade income is never taxed. The race's trade bonus is the sum of
    Value 1 of its traits of Trait Type `Trade` (none in the stock data). OpenSE4 matches
    (`economy::nonTradeIncome`).
16. **Rebel colonies** (§2.3): answered (confirmed: binary). The new empire is a copy of
    the former owner (race, traits, technology, queues, options), named after the system,
    computer controlled at the highest computer difficulty, with the whole population as
    its own people, anger 25, stocks of 4 × its production, and no contact with anyone
    until detection makes it (at None). The 20-empire limit counts destroyed empires and
    stops the intelligence variant too, which then does nothing. OpenSE4 matches; its own
    choices for what the text leaves open are question 41.
17. **Intelligence details** (§2.1–§2.4): answered (confirmed: binary). The defense sum
    multiplies Amount × progress as whole numbers by the fraction modifier / 100, then
    truncates, as OpenSE4 does. A finished defense deletes, in the first other living empire
    (in random order) that has one, the first project in queue order aimed at its owner
    whose summed requirement levels are at most Amount, as OpenSE4 does. An
    "Any" pick applies only the steal-level test and the bad-chance test, so a homeworld can
    rebel. `Change Bad Intelligence/Event Chance - System` is read only from objects that
    belong to no empire. `Ship - Orders Change` sends the ship to a random sector of a
    random system, its own included, and `Ship - Moved` may pick the ship's own system.
    `Politics - Intercept Messages` with nothing to report fails. `Planet - Value Change`
    uses Amount × 1,000 only strictly inside ±500,000, sets negative results to 0 and pulls
    every value into the Minimum/Maximum limits. OpenSE4 matches.
18. **Event targets** (§4): answered (confirmed: binary). The political types draw from
    every empire number but act on no third empire, so they achieve nothing; the
    delete-project types and `System - Info` have no target list and never fire. Planet
    events draw only colonies; ship events draw ships, bases and unit groups in space. For
    star events the empires present are those with a colony in the system, and the High and
    Catastrophic protection applies only to the exact home planet location. An empire target
    rolls with its own Luck. OpenSE4 matches (the home planet locations are question 42).
19. **Victory arithmetic** (§6): answered (confirmed: binary). "X % of second place" and
    the tech share are compared in floating point, as OpenSE4 does (in extended precision); with one living empire the second-place test passes. The peace counter
    moves only while the peace condition is on and past the qualifier date, which makes no
    observable difference. A surrender itself clears nothing: the surrendered empire is
    destroyed at its next destruction check, which does the clearing (§3.4).
20. **Event amounts on the spec 02 scales** (§2.3, §4): answered (confirmed: binary).
    `Planet - Conditions Change` is additive, Amount in tenths of the 0–1.5 scale, on any
    planet; `Planet - Population Anger Change` is Amount in whole percent, within 0 and 100
    (80 on a capital). OpenSE4 matches.
21. **How much the AI spends** (§7.2, §7.5): answered (confirmed: binary). The original has
    no rule that spends surplus which OpenSE4 lacks. The computer player spends only through
    construction queues and retrofits; it never uses Emergency Build or Repeat Build, never
    converts resources, and nothing reacts to a full storage (§7.5 "What spends the
    stockpile"). The stockpiling seen in OpenSE4's all-computer games (5 empires, 30
    systems, 120 turns: organics and radioactives at their caps, minerals piling up once an
    empire stops expanding) comes from the same rules: the budget is one turn of net income,
    queues take items only while their backlog is under 5 turns, facilities go only into
    empty colony queues, and every queue is bounded by its rates. OpenSE4 once differed in
    the backlog test, in the date its ministers see, in the upgrade budget and in the
    tariffs subtracted from the budget; it follows the rules above since 2026-09-30. None of
    these was a missing way to spend. The emergent pace can only be compared by observing an
    all-computer game on the stock data: each empire's stock of the three resources at
    turns 40, 80 and 100; its planets, ships, bases and yards at those turns; how many
    items its yard queues hold (never more than four items of several turns each in the
    original); how many turns it spends in Defend (Short Term); whether it scraps ships
    while it builds others; whether it ever spends more in a turn than one turn of net
    income; and whether its colonies are ever founded empty. Since 2026-09-30 OpenSE4
    follows these rules too.
22. **Repair and resupply orders** (§7.5): answered (confirmed: binary). The original
    interrupts orders, Colonize included: a vehicle that needs repair loses all its orders
    and leaves its fleet every turn, even when there is no yard to send it to (it is then
    left without orders). Resupply clears the orders too but spares colony ships and
    Destroy Star ships, and always finds a destination (a depot, else our nearest colony,
    else home). The thresholds are not percentages of damage or supply: §7.5 gives the
    repair tests by design type and the resupply test by supply distance. OpenSE4's
    ministers follow these rules since 2026-09-30.
23. **Colonists on AI colony ships** (§7.5): answered (confirmed: binary). The Colonization
    minister orders Load Cargo (population) where the ship is when it has cargo space and
    carries no people, then Move To and Colonize, so an AI colony starts with the people
    the ship carried. OpenSE4's single Colonize order has the same effect.
24. **Turn order details** (§8): answered (confirmed: binary). Item by item:
    - The turn number: the original advances the date at step 3, and every later step sees
      the new date, the ministers included (§7.5 "The date"). OpenSE4 keeps the old number
      until the end of the turn and hands the advanced date to the steps that use it; its
      ministers read the date they would see (`ai::aiDate`: the advanced date in a
      simultaneous game, the unadvanced one in a turn-based game).
    - Colony ships: the original carries out Colonize orders during the movement phases, on
      the ship's acting days (§8 step 5). OpenSE4 matches.
    - Computer players' messages take effect the moment they are sent, during the Politics
      minister, which acts first; the empire's other ministers already see the new
      treaties. OpenSE4 matches: the Politics minister plans alone, its messages are
      delivered, then the other ministers plan.
    - The political step counts each log entry once, at the first political step after it
      was logged (§7.3 "What it counts"). In a simultaneous game that is the turn processed
      before plus the messages of this turn's step 2, as in OpenSE4; turn-based games differ
      (question 32). What the AI remembers is its log; OpenSE4's record after the event step
      holds the same things.
    - A player whose orders are missing is played with all ministers on (§7.1).
    - Statistics: the original writes, for each human player, one row per empire whose
      score that player may see (§5). OpenSE4 writes those files too, and also keeps every
      empire's row in the save for its windows, which show the visible ones: an OpenSE4
      extension; engine choice stands.
    - Step 1's queue refresh only rebuilds a list (bookkeeping); OpenSE4 needs none. Step
      16 is not an upkeep: it only records each vehicle's current sector as the one it
      comes from (§8). Where unit and cloak upkeep belong is spec 03 open question 36.
    - An empire founded during step 6 is first processed next turn (§8 step 6), as in
      OpenSE4.
    - A subject's tariff that reaches its master after the master's storage cap stands until
      the master's next cap, as in OpenSE4: the tariff is credited at once at the subject's
      income step (§3.3) and the master's cap runs only in its own processing.
25. **How fast the AI researches** (§1.3, §7.5): answered (confirmed: binary). The Research
    and Intelligence ministers match OpenSE4 in every rule that drives research: funding in
    queue order, no repeat, maxed areas dropped, the choosing gate, the first matching row,
    the share-total stop, the random fallback and the intelligence minister. Two small
    differences were fixed on 2026-09-30: the gate tests the research pool rather than this
    turn's production, and mine sweeping queues only the first tech requirement of the first
    Mine Sweeping component, and only when the research file has rows. The pace (5 to 37
    levels by turn 120 in OpenSE4's games, a single project in most turns) follows from the
    stock rows of 100 % and the research points. To compare it, observe an all-computer
    game on the stock data: each empire's tech levels and research points per turn at turns
    40, 80 and 120, and how many projects its queue holds.
26. **Hazard systems** (§7.2, §7.5; spec 01 §7): answered (confirmed: binary). The original
    AI has no hazard test: no AI routine reads `System - Destructive Center`, `System -
    Movement Towards Center` or `Sector - Damage`. Its destinations (attack targets, the
    exploration frontier, patrol, defence, fleets, colonization) ignore black holes and
    storms, and its routes skip only warp points whose sector holds a visible hostile. The
    shared path rules of spec 03 §6.2 apply to every empire. OpenSE4 matches, so the ship
    losses in black-hole systems are faithful.
27. **`_AI_Construction_Units` rows** (§7.5): answered (confirmed: binary). §7.5 "Units
    file" gives the format (a reserve record, then `Colony Type` rows with `Entry N Type` and
    `Entry N Maximum in kT`, no AI state) and when it builds (after the vehicle list, one
    batch per colony with an empty queue and no free facility slot, from a fresh budget).
    OpenSE4 reads it so since 2026-09-30; the stock install has no such file.
28. **Allowed planet type and atmosphere** (§7.1, random race build): answered (confirmed:
    binary). Every pair except a Gas Giant with no atmosphere is allowed; a pair that is not
    is redrawn from the five atmospheres and three types with no limit. OpenSE4 matches
    since 2026-09-30.
29. **Planet launches** (§7.5): answered (confirmed: binary). The kept share is counted for
    the whole empire (units in space and in every cargo); the excess is launched colony by
    colony in planet order, each colony launching its whole stock; drones go without a
    target, and idle drones in space are then sent after targets (§7.5 "Mines, satellites
    and drones"). OpenSE4's ministers match since 2026-09-30; a drone launch without a
    target is spec 03's movement matter (PARITY_GAPS, "Drones").
30. **What the History window records** (§3.4, §5): closed as an OpenSE4 extension; engine
    choice stands. The original keeps a history file only for human players, extended each
    turn with the previous turn's accepted treaties, broken treaties, declarations of war,
    destroyed empires, first contacts and lost contacts (a surrender line exists but is
    never written), each dated and naming the other empire; it has no length limit and no
    map positions (confirmed: binary, §3.4). OpenSE4's record keeps those events (contact
    is lost by the warp-path rule of §3.1, question 13) and more (colonies, ruins,
    captured planets, stellar manipulations, events, the end of the game), in the save, for
    every empire. The files themselves are written for human players as §5 describes
    (OpenSE4's layout and place: question 40).
31. **When a foreign design was "seen"** (§8, §3.2): answered (confirmed: binary). §8
    "Design knowledge and step 12" lists every event that dates a design. OpenSE4 matches:
    scanners teach a design only when a human opens the report of a vehicle they reach
    (`cmd::OpenVehicleReport`, question 43).
32. **Turn-based details** (§8 "Turn-based game", spec 03 §6.3): answered (confirmed:
    binary) except the two extensions at the end:
    - Start of a player's turn: the original runs the start-of-turn step (AI state, the
      Politics minister first, the other ministers) before the vehicles get their movement
      back and carry out their orders, the new ones included; a computer player's
      destruction check comes after its start-of-turn step. OpenSE4 matches.
    - The ministers only give orders; the vehicles carry them out afterwards, in object
      order, with the orders left from the previous turn. A human's orders execute as given.
      This matches OpenSE4.
    - A vehicle goes on until an order is not finished (no movement left, or waiting),
      fails or its list is empty, completing at most 21 orders. OpenSE4 matches (question
      45).
    - Battles start when a group moves into a sector or carries out an order there, never
      for groups that sit (spec 04 §2); combat on entry stops the move and clears the list
      (spec 03 §6.4). As in OpenSE4.
    - Colonize needs movement left and waits without it (spec 03 §8). As in OpenSE4.
    - One message per recipient per turn is read from the recipient's log by date, so it
      lasts the whole game turn (§8). As in OpenSE4.
    - The political step counts every log entry since the empire's previous political step
      (§7.3). OpenSE4 matches, with a mark of what each empire's step counted
      (`Empire::politicsMark`, question 44).
    - The date stays unadvanced for the whole game turn (§8). As in OpenSE4.
    - An empire founded during the game turn plays when its number comes up (§8). As in
      OpenSE4.
    - The sector a vehicle came from is reset in its owner's end-of-turn processing (§8 step
      16). OpenSE4 matches (question 46).
    - OpenSE4 extensions, engine choice stands: `processTurn` for a turn-based game (orders
      given in advance, a missing human played by the computer) and an Attack Sector
      question kept open in the game. The original asks its questions at once, in a window,
      during the human's own turn.
33. **Turn-based games on different machines** (§9.1, §9.5): closed as an OpenSE4
    extension; engine choice stands. The original has no host for turn-based games: the
    save file passes from player to player, the computer players' turns run at once on the
    machine of the human before them, and loading the file asks for the password of the
    empire whose turn it is. A player who does not pass the file on stops the game: there is
    no time limit and no stand-in, and nothing checks that a file is the latest (confirmed:
    binary, §9.1). Only simultaneous games check the orders files (§9.2).
34. **Transports and mixed races** (§7.5): answered (confirmed: binary). The load takes
    every race the planet can spare, in list order, but the destination must let every
    carried race breathe, or already be domed and host one of them, so a delivery never
    domes a colony. OpenSE4 matches since 2026-09-30.
35. **Colony ships whose target is gone** (§7.5): answered (confirmed: binary). The
    original does not re-target them: they keep flying, the Colonize order fails on
    arrival and clears the orders, and the ship is planned again later. OpenSE4 matches.
36. **The player's side of an e-mail game** (§9.2, §9.5): closed as an OpenSE4 extension;
    engine choice stands. In the original (confirmed: binary, §9.2) the `.plr` goes to the
    game's multiplayer folder (the shared folder chosen at setup, else the default save
    folder), named after the game and the player number; saving during the turn writes the
    same `.plr` and leaves the `.gam` unchanged; the player's side never reads a `.plr`
    back; a `.plr` is a snapshot of the player's part of the game checked by date, turn code
    and game code. Turn-based games have no `.plr` at all. OpenSE4's `.plr` is a command
    list with checksums, written next to the `.gam`, and its drafts folder, password rule
    and turn-based files have no counterpart.
37. **Computer-player details the settled rules leave open** (§7.2–§7.5): **Answer:**
    every sub-item is settled from the executable (confirmed: binary), except the last,
    which has no counterpart. The rule is in the section named. Where the engine (each
    choice marked "(inferred)" in `ai*.cpp`) differs, the sub-item says what must change.
    - Which messages count for "the newest unanswered political message" and for "a
      message from X is still waiting": **Answer:** the AI keeps no "answered" mark. It
      looks only at the single newest political message from X in a date window, whatever
      its type, and an older message is never answered (§7.4 "Answer window"). In a
      turn-based game the window holds what X sent since our previous turn; in a
      simultaneous game, the messages dated exactly the ministers' date − 2. "Waiting"
      (simultaneous only) is any political message from X dated after the date − 2. The
      engine differs: it takes the newest unanswered message dated this turn or the turn
      before and tests "waiting" on unanswered answerable messages. It must use the window
      and the waiting test of §7.4, dating each message as the original does (a message
      from a player's orders carries the unadvanced date, one a computer player sends
      during its start-of-turn step the advanced date). It must also run the initiative
      again when the 50 % branch's initiative sent nothing and nothing waits, and roll
      "wants war" and "wants to break" again at every check instead of once per turn.
    - Transports: **Answer:** a transport delivers first only when it carries people and
      its cargo is more than half full. A failed delivery never falls back to loading, and
      a delivery follows a load in the same turn only when the source is in the
      transport's own sector. A transport with nothing aboard never delivers (§7.5
      Transports). The engine differs: it tries the other step once whichever fails,
      always gives a move followed by Load or Drop, and leaves a transport that found
      nothing without orders. It must follow §7.5: a move only when the target is in
      another system, delivery after loading only in the same sector, and the resupply
      orders for a transport left without orders.
    - The reserve quirk of the units file: **Answer:** the reserve is one value shared by
      all empires. Every empire whose Ship Construction minister acts reloads it in its
      units step, to 0 when it has no units file; an empire whose ministers do not act,
      a missing player who forbade AI changes included, leaves it alone (§7.5 "Reserve
      quirk"). The engine differs: it walks back past acting empires without a units file,
      and counts a missing player who forbade AI changes as acting. It must take the
      nearest earlier empire whose units step ran (its percentage, or 0 without a file)
      and treat that missing player as not acting. With the stock install the reserve is
      0 either way.
    - "Lacks a part it needs to operate" (Repair minister, fleet fitness): **Answer:** no
      working source of `Ship Bridge`, of `Ship Auxiliary Control` or of `Master
      Computer`; engines, life support and crew quarters are not checked, and a mothballed
      vehicle always lacks one (§7.5 Repair). The engine differs: it asks for a bridge with
      life support and crew quarters (or a Master Computer) and a working engine on hulls
      that use engines. It must use the bridge, auxiliary control or Master Computer rule,
      in the Repair minister and in fleet fitness.
    - The Repair minister and mothballed vehicles; ties between yards: **Answer:**
      mothballed vehicles are not skipped: they lose their orders and leave their fleet,
      but get no destination. Yards are visited by system number, then in object order,
      and the first one found wins a tie. The vehicle itself counts as a yard when it has
      a working yard and a queue (§7.5 Repair). The engine differs: it skips mothballed
      vehicles, ranks colonies before yard ships on a tie whatever their systems, and
      never counts the vehicle itself. It must follow §7.5.
    - A fleet "on unlimited supply": **Answer:** when none of its ships lacks unlimited
      supply, a fleet without ships included; the supply totals leave out the members with
      unlimited supply (§7.5 Resupply). The engine matches the test but differs in the
      totals: it adds every member's supply and cost per move, and must skip the members
      with unlimited supply.
    - The Space Yard Ship's "no own yard in its sector" and "movement left": **Answer:**
      the test does not ignore the yard ship: when uncloaked it counts as a yard in its
      own sector, so a vehicle it has reached stops being a target and it waits beside one
      only while cloaked. "Movement left" is what the ship has when the ministers act, as
      in OpenSE4. Fleet membership is not checked (§7.5). The engine differs: its yard
      test leaves out the yard ship itself and it skips yard ships in fleets. It must count
      the yard ship when uncloaked, never count a cloaked yard ship, and drop the fleet
      skip.
    - Open Warp Point, Destroy Black Hole and Destroy Nebulae: **Answer:** when no draw
      succeeds, the ship gets the resupply orders; the last draw is not used. It also gets
      them while a free exploration frontier point is left. A draw picks a side, then a
      position along it; a sector with any object, vehicles included, is drawn again; only
      99 draws can succeed. Destroy Black Hole and Destroy Nebulae ships seek sector 36
      (x 10, y 2) of the system, with a Seek that lasts one turn (§7.5). The engine
      differs: it skips the ship while a frontier point is left, draws evenly among the
      edge sectors (so its corners are not twice as likely), counts only galaxy objects as
      filling a sector, uses the last draw after 100 failures, and heads for sector
      (0, 0). It must follow §7.5.
    - Close Warp Point, "seeing" a hostile empire: **Answer:** any object the hostile
      empire owns in the far system (ship, base, unit group or colony) that passes the
      detection test of Sentry; our presence includes fighter, satellite and drone groups
      but not mines (§7.5). The engine differs: its presence test leaves out our fighter,
      satellite and drone groups, and it counts any hostile colony in an explored system,
      even one a cloaking facility hides. It must follow §7.5.
    - Mine and satellite layers: **Answer:** the weights of the empires in the far system
      add up; the per-sector cap counts only our units of the layer's kind; the
      star-destroyer flag is evaluated on dates that are multiples of 20, from designs
      (ours included) seen within 20 turns that have `Destroy Star`, `Create Nebulae` or
      `Create Black Hole`; the fallback draws one quiet colony system first and takes its
      star or one of its warp points (§7.5 "Layers", "Layer fallback"). The engine
      differs: it takes the largest weight, counts every unit group of ours at the sector,
      flags known foreign designs with `Destroy Star` on every turn, and draws the star
      from any colony system. It must follow §7.5.
    - Design names when every name is taken: **Answer:** the name stays empty and the
      design is still made (§7.5 "Name"). OpenSE4 choice: OpenSE4 refuses a design without
      a name, so it uses "Design <counter + 1>" instead. This happens only after every
      round of the whole name file is used. The engine choice stands.
    - An accepted demand whose `Response … YES …` pool is empty: **Answer:** the original
      carries it out. The demand is accepted and carried out first (with its 50 % chance),
      and the reply is sent afterwards, only when its text is not empty (§7.4). The stock
      pools are empty, so this is the normal case. The engine differs: it acts only on a
      sent Accept Demand, so nothing happens. It must record the acceptance without a
      message and carry it out.
    - A request for a gift or tribute that is accepted but for which nothing can be given,
      or with gifts off: **Answer:** an accepted request whose package ends up empty
      counts as refused and gets the General reply from `Response … Want a gift` (or
      `… Want a tribute`). A concrete requested item goes in even when the AI cannot hand
      it over. The game option for gifts and tributes is never read by the computer
      player; it only limits the message types a human can pick (§7.4). The engine
      differs: it refuses when gifts are off, skips items it cannot give, and stays silent
      on an empty package; it also refuses to move a gift's items at acceptance when the
      option is off. It must follow §7.4.
    - Old saves: **Answer:** OpenSE4 choice. The original has no counterpart: it writes
      each side's attacker or defender log entry when the battle is fought, so nothing can
      be missing. A battle recorded before `CombatRecord::currentPlayer` existed counts as
      Defending for everyone; that is save compatibility only, and the engine choice
      stands.
38. **"Any" third empires** (§2.1, §2.3): **Answer:** the candidates are the living
    empires the source has contact with, other than the source and the target, in empire
    order; one is drawn at random and no further check applies, so the first draw stands.
    With no candidate the operation fails and the source is told that no valid target was
    found (confirmed: binary, §2.1). The engine differs: it draws among every empire number
    other than the target and the source, destroyed empires and empires without contact
    included, and leaves the check to the handler. It must draw only among living empires
    in contact with the source and fail at once when there are none.
39. **An operation that tells nobody** (§2.3): **Answer:** `Planet - Conditions Change` is
    an ordinary successful effect. The planet sends no notice of its own about the change,
    but the record's messages go out as for any other effect: as an intelligence project,
    the source gets its source message and the victim its target message, with the usual
    1-in-5 suspect line; as an event, the record's message goes to the recipients its
    `Message To` names (confirmed: binary, §2.3). The engine differs: it treats the effect
    as silent, so the event logs nothing and the victim of the project hears nothing. It
    must send the messages as for any other effect.
40. **The players' files in OpenSE4** (§3.4, §5, §9.2): **Answer:** the formats are settled
    (confirmed: binary, §3.4 "Players' text files", §5). Statistics: empire number in 5
    characters, the date as a whole number of tenths of a year (24001) in 8, then the
    eleven columns in 12 each, right-aligned, with the date the empire's end-of-turn
    processing sees (advanced in a simultaneous game). History: the entry's date in the
    same form in 8, the other empire in 5, two zero flags in 5 each, a space, the text;
    the file is opened only when there is a line to add. A destruction writes only its own
    line; contact-lost lines come from the warp-path rule of §3.1. The log copy is written
    only when `Create Log Text Files for Players` is true (off when the key is missing; the
    stock file turns it off) and the log is not empty; it is rewritten each turn with two
    header lines and one line per entry of the whole log (date as 2400.1 in 9 characters,
    title in 40, text with line breaks turned into spaces). The engine differs in each of
    these: its column widths; its dates, written as 2400.1 rather than as a whole number of
    tenths, and in the statistics always the unadvanced turn; its log copy (its own line
    layout, on when the key is missing, appended, last turn's entries only, no header); and
    the missing contact-lost lines. It must follow §3.4 and §5. Spec 06 §6.1 lists the
    original's file names (`History/plr_<N>_stats.txt`, `_events.txt`, `_log.txt`); our
    client uses them, with `History/` in the user data directory instead of the
    installation (an OpenSE4 choice), copies them next to each save and restores them
    when a game is loaded (spec 06 §6.1, Q12). That
    OpenSE4's network and e-mail hosts write no files is also an OpenSE4 choice; the
    original's TCP/IP host sends each player its history and statistics files.
41. **Rebel empire details** (§2.3): **Answer** (confirmed: binary, §2.3 "The new empire"):
    - the planet's atmosphere becomes the race's breathed gas and its physical type the
      native type, and the allowed atmosphere and planet type lists shrink to those two.
      The engine's reading matches;
    - the pictures come from a random neutral race that no empire uses for its race or
      pictures, else from a random one of all neutral races;
    - the name is the system's; when it is empty or already taken (destroyed empires
      included), empire-names lines are drawn until one is unique;
    - copied from the former owner: research, the intelligence and construction queues,
      options, experience, and the ministers' state (style, anger toward each empire,
      turns since war, AI state and turns in it, target, staging, secured and defended
      systems, attack timer). No designs are given; the log is emptied; treaties reset to
      "no contact"; the accepted-demand lists start empty;
    - "built at least once" for design theft is a mark set when a queue completes a
      vehicle or unit of the design or a ship is retrofitted to it, cleared only when the
      design is saved again in the designer. No statistics reset clears it.

    The engine differs: it resets the experience, the minister style, the AI state and
    memory and the anger toward everyone; it keeps the former owner's pictures when no
    unused neutral race is left; it draws a replacement name only once and not for an
    empty system name; and its design theft reads the built count, which misses designs
    only retrofitted to. It must follow §2.3. Spec 02 §10 is corrected to match: a rebel
    keeps its former owner's minister style.
42. **Home planet locations** (§4): **Answer:** each empire records a home system and
    sector, set when the game is created (its home planet) or when a rebel empire is
    founded (the rebel planet). It never moves: not when the planet is lost, captured or
    abandoned, nor when the empire is destroyed. High and Catastrophic planet and star
    events reject a candidate at any empire's recorded location, destroyed empires
    included, whoever owns the planet now; other starting planets and other capitals are
    not protected (confirmed: binary, §4; spec 02 §2). The engine differs: it records only
    the home system and protects the sectors of the current capitals. It must record the
    home sector with the home system (set at setup and when a rebel empire is founded,
    never changed, and saved) and protect every empire's recorded location.
43. **Opening a report** (§8 "Design knowledge"): **Answer:** the design is dated when the
    report of a foreign vehicle is filled in for display, in the report panel or a report
    window, whatever tab shows; no tab adds anything. A ship's or base's report dates only
    its own design, never its cargo units; a unit group's report in space dates its
    units' design. In a simultaneous game only ship and base reports reach the host, with
    the unadvanced date (confirmed: binary, §8). The engine differs: its report also dates
    the designs of the units in a ship's cargo, and in a simultaneous game a unit group's
    report dates its design on the host. It must date only the vehicle's own design for a
    ship or base, and nothing on the host for a unit-group report in a simultaneous game.
    The date it uses matches.
44. **What a turn-based political step counts** (§7.3): **Answer:** each log entry carries
    a mark. The step counts only the unmarked entries of the empire's own log, from the
    newest back, and stops after the first entry dated before the turn before (counting it
    when unmarked); when it finishes, every entry is marked. The mark is the Log window's
    "read" mark, so entries a human has seen in the Log window are never counted. An
    empire whose step does not run marks nothing, and its next step counts what is still
    unmarked in its window. The stellar-manipulation term reads the counting empire's own
    log: the report is logged to every empire present in the system when it happens,
    naming the empire responsible (confirmed: binary, §7.3). The engine differs in term 2:
    it reads the culprit's log and tests who is present when the step runs. It must log
    the report to each empire present at that moment and count it from the counting
    empire's own log. Its per-empire mark of what a step counted gives the same result as
    the original's marks, except that opening the Log window does not mark anything; an
    implementer may replace it with a mark per log entry, set also when a human reads the
    log.
45. **The 21-order limit** (§8 "Turn-based game"): **Answer:** the limit counts the orders
    completed in one run of a vehicle's or fleet's order list; an unfinished order ends the
    run and a failed one clears the list. Removals outside a run, such as combat ending a
    Sentry, do not count. Each run counts afresh: the start of the turn, each simultaneous
    movement phase, and in a turn-based game each order a human gives, which runs the list
    at once (confirmed: binary, §8). The engine matches.
46. **Step 16** (§8): **Answer:** only ships, bases, fighter groups and drone groups record
    their current sector. Satellites, mines, planets and other objects do nothing, and
    troops are never in space. The step is part of every empire's end-of-turn processing,
    so it runs in both turn styles (confirmed: binary, §8 step 16). The engine matches.
47. **Promises to stop hostile actions** (§7.3 term 8, §7.4): **Answer:** an accepted
    "stop hostile actions against an empire" demand records a promise about the empire the
    demand names, not about the requester. Each promise gives −20 toward that empire once,
    one promise per turn, and all are forgotten every 10 turns (confirmed: binary). The
    computer player never sends this demand itself (§7.4 "Demands the AI starts"), so it
    comes from human players, whose message names the empire. The engine differs: it sets
    a single promise flag on the requester, so the −20 goes to the requester even when the
    demand names another empire, and a second promise before the first is used adds
    nothing. It must record the promise about the named empire and count promises instead
    of setting a flag.
48. **Computer players' messages in a TCP/IP game** (§9.4): **Answer:** in a simultaneous
    game a computer player's messages take effect when sent and are also kept in its
    outbox, which step 2 of the next turn processing delivers. Only a TCP/IP host keeps
    the outbox from one turn to the next (an e-mail or shared-folder host quits after the
    turn and Hotseat reloads the game file), so a TCP/IP host delivers every computer
    player's messages a second time, with the effects listed in §9.4 (confirmed: binary).
    The engine differs for TCP/IP games only: it marks each message delivered and never
    delivers one twice, in any mode, which matches the original's e-mail, shared-folder
    and Hotseat hosts. Reproducing the TCP/IP double delivery would mean delivering the
    computer players' messages of the turn before again at step 2 of a network host's turn
    processing. OpenSE4 choice: OpenSE4 does not reproduce it, because it is a fault of the
    original's TCP/IP host. OpenSE4 delivers each message once in every mode, as the
    original's e-mail, shared-folder and Hotseat hosts do; the engine choice stands.
49. **The other demand lists** (§7.4 "Demand lists", "Wants war with X", "Propose
    Treaty"): **Answer** (confirmed: binary):
    - Added: when a carried-out demand (the 50 % chance of §7.4) is "declare war on" or
      "support against", one entry for the empire it names goes to the war list; "break
      with" adds one to the break list; "make peace with" one to the peace list.
      Duplicates are kept.
    - Used up, one entry at a time: a war entry by a check of "wants war with X" that gets
      past "already at war" and the team tests, which then answers yes; a break entry the
      same way by "wants to break", which first needs a treaty of Non-Aggression or better;
      a peace entry by the treaty-proposal check when X is neither a team mate nor a team
      enemy and the AI is not a subject, which then forces a proposal (nothing is sent when
      no treaty qualifies, but the entry is gone). Since every check rolls again and the
      50 % branch checks war and break before the initiative checks them again (§7.4 "Each
      turn"), a single war entry can be used up by the first check, after which the second
      check rolls normally; a second entry for the same empire makes the second check
      answer yes too.
    - Expire: every list is emptied in the start-of-turn step of each turn whose date (the
      ministers' date) is a multiple of 10, before the Politics minister acts, so an entry
      lasts from 1 to 10 turns.

    The engine differs: it keeps one flag per empire for each list (`Relation::queuedWar`,
    `queuedBreak`, `queuedPeace`), never uses a flag up (it stays set until the 10-turn
    clear, so a queued peace forces a proposal on every turn until then), and decides
    "wants war" and "wants to break" once per turn. It must keep counts (or lists) per
    empire, add one per carried-out demand, use one up at each check described above, and
    clear them all on the dates that are multiples of 10.
50. **When the empires present at a stellar manipulation are taken** (§7.3 term 2): the
    report goes to every empire with a ship, base, colony, or fighter, satellite or drone
    group in the system "at that moment". Destroy Planet removes the colony and Destroy Star
    everything in the system, so it matters whether that moment is before or after the
    result. OpenSE4 takes the empires present as the manipulation is carried out, before
    its result removes anything, so the owner of a destroyed colony and the empires whose
    ships a shockwave destroys are told (inferred). To verify: does the victim of a
    destroyed planet, with nothing else in the system, count it in its anger?
    **Answer:** no. The moment depends on the manipulation (confirmed: binary):
    - Destroy Planet tests presence after its result: the planet (and its colony) is
      already gone. The colony's owner is told, and so counts the report in its anger, only
      if it still has a ship, base, another colony, or a fighter, satellite or drone group
      in that system. Mine fields never count.
    - Destroy Star, Create Nebulae and Create Black Hole report to every empire that lost a
      colony or any object of its own in the system to the shockwave, mine fields included.
      The culprit's own ship is destroyed too, so it is told as well; that report names
      itself and never counts.

    The engine follows this since 2026-10-01: Destroy Planet takes the empires present after
    its result, mine fields aside; the three shockwave manipulations take every empire with a
    colony or an object of its own in the system as the shockwave begins, mine fields
    included, which is every empire that loses one to it.
51. **A "stop hostile actions against an empire" demand that names no empire** (§7.3
    term 8, question 47): the promise is about the empire the demand names. A message
    whose third empire is missing or invalid records no promise in OpenSE4 (inferred).
    To verify: what the original records when the named empire is missing.
    **Answer:** the original cannot send one (confirmed: binary).
    - The sender must pick the empire from a list of the empires it has met that are still
      in the game, other than itself and the recipient. The same picker serves "break a
      treaty with", "declare war on", "make peace with" and "support us against" (and
      "attack an empire in a system"). Cancelling the pick abandons the message.
    - The computer players never send this demand (§7.4).
    - When a computer player carries the demand out (half the time), it records the named
      empire without any check. Promises are used up only for living empires it has met,
      other than itself, so a promise about an invalid empire would never be used; it would
      be forgotten at the next 10-turn clearing.

    The engine has the same effect, since recording nothing is equivalent. Since 2026-10-01
    its send-message command refuses these requests from a human player unless the third
    empire meets the picker's rule above. A computer player's (and a minister's) own
    messages do not go through the picker, and the engine does not check them (question
    52 settles which of them can name an invalid empire).
52. **A computer player's request about a third empire** (question 51, §7.4): the computer
    players send "break a treaty with", "declare war on", "make peace with" and "attack an
    empire in a system" from their own rules, not through the player's picker; the
    "attack an empire" target comes from their list of attack candidates, whose owners they
    may not have met. OpenSE4 checks the third empire only for human players' messages
    (inferred). To verify: can a computer player's request name an empire it has not met,
    or one no longer in the game?
    **Answer:** only "attack an empire in a system" can; the other three always name a
    living empire the sender is in contact with, never itself or the recipient
    (confirmed: binary; §7.4 "Demands the AI starts"). The attack target does not come
    from the attack candidates, as the question assumed.
    - **Break a treaty with, declare war on:** Z is the lowest-numbered empire, other
      than the AI and the recipient X, that X holds at Trade Alliance or better by its own
      treaty and the AI holds at War or Non-Intercourse; the request is "declare war on"
      when the AI is Military Alliance or better with X and at War with Z. Any treaty
      other than "not yet met" means contact, and an empire's destruction resets every
      living empire's treaty with it to "not yet met", so Z is met and alive.
    - **Make peace with:** the lowest-numbered empire, other than the AI and X, that X is
      at War with and that the AI holds at Military Alliance or better; met and alive for
      the same reason.
    - **Attack an empire in a system:** taken from the AI's newest report, dated this turn
      or the turn before, of a battle it lost while defending: the battle's system, and
      the highest-numbered empire other than the AI that had pieces at the battle's start.
      Nothing tests treaty, contact or survival, so that empire may be unmet, destroyed
      since, or an ally. When it is X nothing is asked. In a simultaneous game the empire
      with the game's highest player number never sends it, since it counts every battle
      as Attacking (§7.3 term 1).
    - **No candidate:** the step sends nothing and the next step is tried; its roll is
      spent.
    - **The flag:** the request is chosen before its `Will Send …` flag is tested; a
      forbidden request cancels steps 1–5 for that turn.
    - **Ministers:** a human empire whose Politics minister is on sends these requests by
      the same rules, unchecked.
    - **Recipients:** a computer recipient that carries the request out records the named
      empire without any check (question 51); for "attack an empire in a system" it
      records only the system and never uses the empire. A human recipient reads the
      empire's name in the text, whatever that empire's state.

    Since 2026-10-01 the engine follows this (`ai_diplomacy.cpp`, `chooseRequest()` and
    `demand()`; `commands.cpp`, `SendMessage`): steps 1-5 choose the request first and
    test only its flag, a refusal going straight to steps 6 and 7; step 2 rolls for ships,
    then separately for colonies, with the 1-in-3 roll per candidate system; step 3 takes
    the lowest-numbered Z by both treaties; step 5 reads the newest battle of this game
    turn or the one before that the AI lost while defending, from the battle records the
    game keeps for two turns (`GameState::combats`, which hold the verdict, the current
    player, the participants at set-up and the system). The Politics minister's and the
    computer player's messages carry `cmd::SendMessage::minister` and are not checked;
    only the messages the player writes go through the picker's test. The order in which
    step 2 tries its candidate systems is question 58. The recipient's side matches
    (`CarryOutDemand`, `speechLine`).
53. **Time in each AI state** (§7.2; spec 07 session 3 "Pace after the starting assets"):
    **Answer** (confirmed: binary). The transitions of §7.2 are complete: nothing else
    changes the state, and the only clocks are the turns-in-state counter (used by Prepare
    for Attack and Attack) and the after-attack timer. Defend (Short Term) is entered from
    every state except Not Connected and Defend (Long Term) as soon as the
    enemy-in-territory list (question 54) is not empty. It has no minimum or maximum
    length: the first start-of-turn update that finds the list empty ends it. It then goes
    to Infrastructure when no frontier point lies in the territory, otherwise to
    Exploration (with the stock data mine fields are never detected, so "an enemy is
    nearby" never applies). Which of the two follows depends on the frontier test alone.
    The share of turns in each state and the length of the spells cannot be read from the
    code, and the captures do not record states. The shipped program cannot show them
    either: it contains a debug display of every empire's AI state and an AI log (the
    setting keys `Show AI State` and `Output AI Information To Log` of a `DebugSettings.txt`
    file in the game folder), but the program skips the code that reads that file, so an
    observation session would need a debugger to read each computer empire's state.
    The code does show where the engine differs: its exploration frontier (§7.2
    "Exploration frontier") also holds the warp points whose link is unknown, even when the
    far system is explored. Its territory then borders "unexplored space" much longer than
    the original's, which keeps our computers in Exploration and sends them from Defend
    (Short Term) back to Exploration. Under the original's rule an empire that has met
    another and has explored every system that a warp point of its territory leads to
    goes to Infrastructure instead, the only state whose stock vehicle table builds
    Defense Bases, Base Space Yards and population transports. The engine must use the
    original's frontier (and the territory rules of §7.2). Whether the original also
    spends about half of its turns in Defend (Short Term) stays open. Its
    enemy-in-territory list is built as ours is (question 54), so a difference there would
    have to come from the situations themselves, such as treaties between computer
    players (question 59).

    **Measured after the fix** (spec 07 "Pace after the frontier fix"): with the original's
    frontier and territory our computers' shares barely moved: Infrastructure 7 % → 8 % of
    all empire-turns (9 % with the frontier, territory and danger rules alone), Defend
    (Short Term) 53 % → 52 % (78 % of turns 51–100), and 27 of 60 empires still never reach
    Infrastructure in 100 turns. In the turns with contact and an empty enemy-in-territory
    list, our territory borders a truly unexplored system 64–75 % of the time; the old
    rule's unknown links added only 2–3 points to that. The frontier stays because our
    computers explore slowly: by turns 51–100 they have explored about 12 of 23–37 systems,
    4–5 frontier points lie open of which nearly all are free, and most of their attack
    ships (the only explorers besides loaded carriers) sit in fleets. Whether the original
    explores faster is not known; its statistics files hold the systems with colonies, not
    the systems explored, so this needs the observation of question 59.

    **Observed in the original** (2026-10-02, under a debugger; spec 07 "Pace observed under
    a debugger": three 100-turn games of five computer players against 24 of ours). The
    original also spends most of its later turns in Defend (Short Term): 63 % of turns
    51–100 (49–73 % per game) against our 76 % (56–99 %), and 46 % of all turns against 53 %.
    It is in Infrastructure 16 % of turns 51–100 (2–30 % per game) against our 5 % (0–18 %),
    and 10 of its 15 empires reach Infrastructure against 63 of our 120. The frontier is not
    what differs: our computers explore at least as many systems (9.8–14.6 against 7.9–11.0
    in turns 51–100), and a spell of Defend (Short Term) ends in Infrastructure about as
    often (ours 32 %, the original's 25 %). The difference is how long the enemy-in-territory list stays
    filled: it is not empty in 76 % of our updates of turns 51–100 against 63 %, ours hold
    about 1.7 times as many hostile colonies and ships, and 63 % of our turns in Defend
    (Short Term) fall in spells of 20 turns or more (35 % in the original), mostly with a
    colony of an enemy at war in the territory. The original removes such colonies about
    twice as fast (33 % gone within ten turns against 13 %), with most of its attack ships
    in fleets (74–78 % from turn 41 against 44–51 %) and more of them (13.2 against 8.1 per
    empire at turn 100). The rules found that the engine does not follow yet, in order of
    their effect in scratch runs (spec 07):
    1. the ministers' movement orders last one movement phase (§7.5 "How long the
       ministers' movement orders last"), and recruits chase their fleet (§7.5
       `AI_Fleets`): together Defend (Short Term) 76 % → 70 % of turns 51–100;
    2. attack candidates are kept only when we could settle them or their owner is below
       None (§7.2 "Attack candidates"): Infrastructure 7 % → 9 % of all turns;
    3. the explorers and their point list (§7.5 "Exploration"): within the noise;
    4. the economy step's borrowed lists (§7.2 "Whose lists the economy step reads"), the
       cloaked yard ship and the unit count (question 60): not measured, small.

    With 1 and 2 our computers are in Defend (Short Term) 48 % of all turns and 70 % of
    turns 51–100, and in Infrastructure 11 % and 8 %: about half of the gap in Defend (Short
    Term) and a quarter of the gap in Infrastructure. The rest is questions 61–63.

    **After the rules** (2026-10-02, spec 07 "Pace after the movement rules"; all four,
    with the choices of question 64): on the 24 seeds our computers are in Defend (Short
    Term) 51 % of all turns and 72 % of turns 51–100, in Infrastructure 8 % and 8 %, and
    69–79 % of their attack ships are in fleets (the original's 74–78 %). On 120 seeds the
    shares did not move (50 % and 72 % before and after): the one-turn orders and the
    pursuit take about 2 points off Defend (Short Term), and the explorers, which take ships
    whose first order is a Seek, put them back. The 24 seeds lie 3–4 points above our
    long-run share, so the gap to the original's three games is about 4 points over all
    turns and 9 over turns 51–100, and it sits in the hostile colonies at war that our
    territories keep listing (question 63).
54. **What enters the enemy-in-territory list** (§7.2 "Lists built each turn"): **Answer**
    (confirmed: binary), now in §7.2:
    - ships and unit groups other than mine fields: only those the evaluating empire
      detects at the moment of its start-of-turn update (spec 01 §6.3), wherever they came
      from, and whether or not their owner has been met ("hostile" is below
      Non-Aggression, no contact included). Nothing remembers ships seen before;
    - colonies: every populated colony of such an empire in a territory system the
      evaluating empire has explored, seen or not, met or not;
    - the systems: the claimed territory (every colony system and every system one jump
      away over all links), with the exclusions and the one-turn delay of §7.2 "Territory".

    The engine matches, since 2026-10-01 with the territory's delay and exclusions too
    (§7.2). So the list is not where the engine's extra Defend (Short Term) time comes
    from, and the scratch test of spec 07 that counted a hostile colony only when seen
    departs from the original.
55. **A colony type whose row builds nothing** (§7.5 `AI_Construction_Facilities`, colony
    types): **Answer** (confirmed: binary). The original also leaves such a colony empty:
    when no entry of its row passes, it gets nothing, keeps its type, and no other row,
    table or type is tried (§7.5 "Nothing to build"). An Intelligence Compound therefore
    builds only a spaceport and a supply depot where its system lacks them until the
    empire researches its first intelligence facility. The `AI_Planet_Types` tests of a
    Research Compound or Intelligence Compound row look only at the research or
    intelligence production cap, whether everything is researched and whether
    intelligence projects are allowed, never at whether the empire has a facility of that
    kind (§7.5 colony types). The engine matches. Three of the original's twelve computer
    empires produced intelligence points (up to 1,600–5,800 a turn) from turns 60, 84 and
    94, so they reached their first intelligence facility and these colonies started to
    build within 100 turns; ours rarely do (question 59).
56. **The computer players' research inputs** (spec 07 session 3): **Answer** (from the
    captures; the parts the captures cannot show are question 59). The statistics files
    hold, per empire and turn, score, resources produced (minerals + organics +
    radioactives), research, intelligence, tech levels, systems, planets, population,
    units, ships and bases. Colony types, domed share, facilities and moods are not in
    them, and the screenshots show only the human player's own colonies. Means per
    computer empire, original (12 empires) against ours (spec 07 "Pace after the starting
    assets", 60 empires; resources from the earlier set of spec 07 "Pace of the computer
    players", the only one that recorded them):

    | Turn | Population | Resources produced | Research per colony | Research spent per tech level gained |
    |---|---|---|---|---|
    | 25 | 2,177 / 2,166 | 12.8k / 14.1k | 1,194 / 1,173 | 38k / 42k |
    | 50 | 2,419 / 2,436 | 22.0k / 21.0k | 1,423 / 969 | 42k / 47k |
    | 75 | 2,523 / 2,710 | 26.2k / 25.6k | 1,261 / 949 | 51k / 50k |
    | 100 | 3,096 / 3,095 | 37.8k / 29.1k | 1,310 / 929 | 56k / 52k |

    (Research per colony is the mean over empires of research ÷ planets; research spent per
    level counts the opening 20,000 points.) So the lead is in research alone: the two
    sides have the same population and, to turn 75, the same resource output, and they
    spend research equally well, but from turn 25 the original's colonies yield about 40 %
    more research each. The original's research is also very uneven: at turn 50 four of
    its twelve computer empires had 22,000–25,000 research points with 9–20 colonies and
    the others 4,000–14,000, against a 90th percentile of 13,800 and a maximum of 23,400
    for our 61. Its fast empires gained about 1,000 points a turn on average (up to 1,800
    in a turn) for 10–20 turns with 5–9 colonies (one went from 4,300 at turn 19 to
    16,200 at turn 30 with 5–6 colonies). At about 600 points per new Research Center I
    that is about two new centres a turn, and a colony gets at most one a turn (§7.5
    "Which colonies"), so two or more of their first colonies were Research Compounds with
    ten or more free slots: breathable planets, since a domed one has at most five. Ours,
    at turn 50: 1.9 breathable colonies per empire (0.75 Mining Colony, 0.40 Research
    Compound) holding 4.1 research centres, and 7.9 domed ones holding 7.5. The colony-type rules, the facility rows and their blocks, the
    colonization order and the Research minister all match the binary (§7.5), so which
    rule gives the original its larger Research Compounds is not found yet (question 59).
57. **The first-contact check of a timed event** (§3.1): the check runs in an event's
    system "once the event is logged". A timed event logs twice: its start message when it
    begins and its message when it strikes. OpenSE4 runs the check after each, whenever
    the event has a location, whoever its messages reach (inferred). To verify: does the
    start message of a timed event run the check too, and does an event whose messages
    reach nobody (`Message To` None) run it?
58. **The order of step 2's candidates** (§7.4 "Demands the AI starts"): each system where
    the AI has a colony and sees a ship (or holds a colony) of X is a candidate that needs
    a 1-in-3 roll. OpenSE4 tries the candidate systems in system order and takes the first
    whose roll succeeds; a ship counts when the AI sees it (inferred). To verify: in what
    order does the original try the candidates, which ships of X count (seen ones, every
    one, unit groups too), and does it stop at the first successful roll?
59. **Research per colony after turn 25** (question 56, §7.2, §7.5). With the same
    population and resource output, the original's computer empires get about 40 % more
    research per colony than ours from turn 25 on, and their fastest empires build about
    two Research Centers a turn on two or more breathable Research Compounds. The rules
    for colony types, facility rows, colonization order and research choices match the
    binary, so the cause lies in what those rules are fed. Candidates, to check once the
    engine follows §7.2 (frontier, territory) and §7.5 (Defense Base placement):
    - more Infrastructure turns, through the frontier rule (question 53): the homeworld's
      Infrastructure row asks for two research facilities instead of one, population
      transports move people to breathable colonies, and Defend (Short Term) builds the
      fewest colony ships of all states (`Colonizer` with `Planet Per Item` 100);
    - which planets are settled first and the type they get: in ours the breathable planets
      of the home system are settled first and most become Mining Colonies through the
      first `AI_Planet_Types` row (at most two in the empire), leaving the Research
      Compounds on domed planets of one to five slots;
    - moods: research is scaled by the colony's mood (Happy +10 %, Jubilant +20 %);
    - treaties between computer players: a Non-Aggression treaty or better takes the
      other empire's ships and colonies out of the enemy-in-territory list and out of the
      colonization danger.

    To observe (inferred method): repeat the pace game with the player's empire marked
    computer-controlled in the Player Computer Control window (spec 06 §1.2.1), so that it
    plays as a computer player, and record its colonies every ten turns from its own
    Colonies and Planets windows: type, size, atmosphere (domed or not), facilities, mood
    and research output, and its treaty with each other empire. A debugger session could
    also read each computer empire's AI state every turn (question 53).

    **After the frontier fix** (2026-10-01, spec 07 "Pace after the frontier fix"; seeds
    1–12, five computers, 60 empires): the gap remains (research 10.0k at turn 50 and 13.6k
    at turn 100 against 13.9k and 23.5k; research per colony 997 and 877 against 1,423 and
    1,310). The first candidate above did not materialise: Infrastructure stays at 8 % of
    the empire-turns (question 53). What our computers have, at turn 50 / 100, per empire:
    - colonies 10.8 / 16.5: the homeworld, 1.9 / 2.5 breathable and 7.9 / 13.1 domed. A
      breathable colony has 13.5 facility slots on average, a domed one 2.9 / 2.7;
    - Research Compounds 2.8 / 4.2, of which 0.5 / 0.6 breathable, with 8.2 / 12.3 Research
      Centers between them (about three each) and 4.6k / 6.9k research (about 1,650 each).
      Mining Colonies 2.6 / 4.0 (0.7 / 0.8 breathable), Military Installations 1.2 / 1.8,
      Construction Yards 1.0 / 1.4, Intelligence Compounds 0.8 / 1.5, Farming 0.8 / 1.3,
      Refining 0.7 / 1.3;
    - the homeworld: 3.5k research from five Research Centers, its 15 slots full from turn
      25 on, so all growth in research comes from the other colonies;
    - moods: 2 % / 5 % of the colonies Jubilant, 39 % / 22 % Happy, 57 % / 68 % Indifferent,
      the rest worse;
    - treaties: of the 1.9 / 2.8 empires met, 0.7 / 0.8 at Non-Aggression or better and
      0.9 / 1.6 at war;
    - intelligence: none of the 60 empires produces any at turn 50, 2 at turn 100 (3 of the
      original's 12 did from turns 60–94).

    The original's fast empires needed two breathable Research Compounds with ten or more
    free slots by turn 20–30 (question 56). Ours have half a breathable Research Compound by
    turn 50, so the open questions are how many breathable planets the original's computers
    settle early, and which type those colonies get.

    **Answer** (observed under a debugger, 2026-10-02; spec 07 "Pace observed under a
    debugger"). In games where all five empires are computer players, the set-up of our
    own measurements, the original's research equals ours within the spread between games:
    per empire 11.0k at turn 50 and 13.8k at turn 100 against our 10.3k and 14.1k; research
    per colony 1,041 and 1,038 against 1,074 and 954 (per game 924–1,233 and 849–1,230; ours
    568–1,483 and 473–1,505); 34.6 tech levels at turn 100 against 34.0. Research per colony
    falls as small domed colonies are added, so it follows the map size and the colony count.
    The colonies agree as well: 15.5 per empire at turn 100 against 15.7, with the same mix
    of types (Research Compounds 3.9 against 4.0, Mining Colonies 4.4 against 4.0), 22.7
    Research Centers against 24.2, and one empire of fifteen far ahead at turn 50 (24.9k
    research with 17 colonies; 3 of our 120 above 20k). So no rule is missing here: the lead
    of question 56 came from its set-up (a human empire on every minister beside four
    computers) and its spread (three games, ±2,200 at turn 50 and ±5,200 at turn 100). Of
    the candidates above, the Infrastructure time does differ (question 53), but bringing
    ours closer changed our research by less than the noise in scratch runs; the colony
    choices match; the moods differ (question 61). What the statistics still show apart are
    the resources produced (question 61), the ships (question 62) and the bases (question
    53 and §7.5 "Placement": the same rule, less time in Infrastructure).
60. **Details the territory and queue rules leave open** (§7.2 "Territory", §7.5
    "Placement"). OpenSE4's choices (inferred):
    - the start-of-turn ministers after Politics read the territory only through the lists
      of the state update, never the claimed systems themselves, so the engine claims after
      them and they use the previous turn's claims;
    - within a system the empire's queue list follows the game's object order, colonies
      and ships mixed;
    - for K and for the "yard colony" test, a colony's yard works while the colony is not
      cloaked, and a ship's while its yard component is neither destroyed nor mothballed,
      cloaked or not;
    - a queued base item counts as many bases as its count builds;
    - the "units of that kind" of the unit queue choice are the units of the same vehicle
      type (mines, satellites, weapon platforms or fighters).

    To verify in the executable: which of these the routines test, and whether any start-of-
    turn minister other than Politics reads the claimed systems directly.

    **Answer** (confirmed: binary, 2026-10-02; now in §7.2 and §7.5):
    - Only the start-of-turn analysis that builds the lists, the state update's choice of a
      staging system, the Politics minister itself (claims, anger, trade values and its own
      demands) and the display read the claimed systems. No minister after Politics reads
      them, so the engine's choice holds.
    - A system's queues follow the system's own object list: the order in which objects
      were placed in or entered the system, planets before the ships that came later. The
      engine's object slot order agrees for planets and can differ only between two ships;
      since 2026-10-02 the ships follow their arrival stamps.
    - A colony's yard works while the colony is not cloaked; a ship's while the ship is not
      cloaked and its Space Yard part works. The engine follows this since 2026-10-02.
    - A queued item counts its count, and every design on a base hull counts, Base Space
      Yards included. The engine matches.
    - The unit queue choice counts every unit in the colony's cargo, whatever its kind. The
      engine follows this since 2026-10-02.
61. **Resources produced by the computer players** (spec 07 "Pace observed under a
    debugger"). With the same population (2,887M and 3,198M at turns 75 and 100 against our
    2,789M and 3,106M), the same colony types and about as many facilities, the original's
    computer empires produce 27 % and 19 % more resources than ours (32.9k and 36.3k against
    25.9k and 30.4k), as the statistics of question 56 showed too (37.8k against 29.1k at
    turn 100). Their colonies are happier (3.2–3.3 Jubilant colonies per empire at turns
    75–100 against 0.8–1.5), but the mood modifiers make only a few per cent of that.
    OpenSE4 computes output by spec 02 §5 and moods by spec 02 §4, and no missing rule is
    known. To verify: what the statistics' "resources produced" counts in the original
    (trade treaty income, gifts and tributes, scrapping); which happiness inputs make the
    original's colonies Jubilant (our ships in the system or sector, treaties); and the
    output of a few computer colonies read under a debugger beside the same colonies in ours.

    **Answer** (observed under a debugger, 2026-10-02; spec 07 "Resources, ships and colony
    losses under a debugger", five original games against 24 of ours on 0d71f41). No rule of
    production differs, and the first three games overstated the gap:
    - Every output of every colony at the income step of every fifth turn of two games
      (13,100 values) followed spec 02 §5.1, with the racial effects of our random race
      build. Colonized planets have the same values, and the mood and population
      percentages weighted by generation are the same (1.12–1.14 in both). Resources
      produced per unit of the facilities' generation: 1.05 against our 1.04.
    - "Resources produced" in the statistics is the delivered production of minerals,
      organics and radioactives at the moment the record is made (spec 05 §5): no trade,
      tariffs or bonus (confirmed: binary). The engine matches.
    - The race draw follows §7.1 in both, but its luck matters: three of the five games drew
      the group-4 race with +20 % minerals and two a group-1 race with +20 % on all three
      resources. With each game's own line-up our engine produces 22 % less at turn 75 and
      16 % less at turn 100 (z 2.2 and 1.6 over five games), against 31 % and 25 % with
      random line-ups.
    - The rest is the colonies: 16.6 against 13.9 per empire at turn 75 and 17.0 against
      15.6 at turn 100, more of them per colonized system, and slightly more of their
      facility slots in resource facilities (7–12 % more generation per colony). The
      original builds more colony ships (two games: 6.6, 8.1, 6.7 and 4.6 per empire in turns
      1–25, 26–50, 51–75 and 76–100 against our 5.5, 6.4, 4.8 and 4.2) and spends more of
      turns 1–25 in Exploration, whose vehicle row asks for the most colony ships (question
      65).
    - Happiness: Jubilant colonies per empire at turn 75 were 8.6, 0.4, 0.6, 1.6 and 0.0 in
      the five games against 0.8 (at most 4.2) in ours; the one happy game had most
      neighbours at Non-Aggression or better, and new treaties calm every colony. The terms
      of the update per colony have the same sizes in both (spec 07). One rule differs: the
      event list is not saved, so a game hosted turn by turn never counts construction
      events (spec 02 §4, confirmed: binary); the engine counts them.
62. **The computer players' ships** (spec 07 "Pace observed under a debugger"). From turn
    75 the original's computer empires have more ships: 14.4 and 19.7 at turns 75 and 100
    against our 10.8 and 13.1, attack ships alone 9.1 and 13.2 against 6.9 and 8.1, although
    they spend fewer turns in Defend (Short Term). The Ship Construction minister follows the
    binary (§7.5), so the cause lies in the budget (question 61) or in losses. To verify: how
    many ships each side builds and loses per turn (a debugger log of ships created and
    destroyed in the original, the same from our engine), and how the budget is split among
    the rows of the vehicle table in each state.

    **Answer** (observed under a debugger, 2026-10-02; spec 07 "Resources, ships and colony
    losses under a debugger"): losses, not construction or the budget. Over turns 26–100
    each of the original's computer empires built 21.4 attack ships (15.6–27.6 per game) and
    lost 12.5 (7.6–15.0); ours build 24.2 and lose 17.9 (16.7–18.9 with each original game's
    line-up). Per attack ship and turn in turns 51–100, 1.9 % of the original's are lost
    against 4.2 % of ours (4.4 % before the movement rules), in fleets 2.2 % against 4.0 %.
    Our extra losses fall in battles away from colonies, which ours fight more often (9.9–10.5
    decided and 7–9 drawn per empire and 25 turns, against the original's 6.6–8.2 and
    1.0–1.2 in two games), while a decided battle costs about the same on both sides
    (question 66). Nor do the resources (question 61) limit either side: from turn 50 on
    both keep 43–67k of each resource in stock (the original 45–67k, ours 43–53k).
63. **How fast hostile colonies in the territory go** (spec 07 "Pace observed under a
    debugger"). Of the colonies of an enemy at war listed in a territory, 33 % (21–46 % per
    game) are gone ten turns later in the original against 13 % in ours, and the original
    loses 3.5 colonies per empire in turns 51–100 against our 1.9. It is in Attack 4 % of
    turns 51–100 against our 2 %, with Attack spells of 4.4 turns that end in Infrastructure
    85 % of the time (ours 2.9 turns, 42 %). Larger fleets explain part of it (question 53),
    but with one-turn orders and the fleet pursuit our rate rose only to 16 % in a scratch
    run. To verify: how the original's colonies go (destroyed, depopulated by bombardment,
    captured by troops, or traded away), from the colony records and combat results of a
    debugger session; which minister sends the ships that do it (the Attack minister in
    Attack, the Defense minister and the defence fleets in Defend (Short Term), the attack
    fleets in other states); and whether our ships reach those colonies and fight them.

    **Answer** (observed under a debugger, 2026-10-02; spec 07 "Resources, ships and colony
    losses under a debugger"). By bombardment in battle: no planet was destroyed, ownership
    changed for 1–2 % of them (by capture or by settling an emptied planet), and with five
    games treaties take 13 % of the no-treaty colonies out of the list within ten turns
    against our 10 % (35 % in the first three games). The colonies go when a battle at the
    colony kills their last people; most had 21–100M. With the two new games the rate is 18 %
    of listed colonies at war within ten turns (53 of 291), against our 15 %, not 33 %. The
    difference left is in the outcome of the battles fought at an enemy colony: per empire
    and 25 turns the original won 2.1–3.2 of them, lost 0.8–0.9 and drew 0.2–0.6, against
    our 0.9–1.1, 0.4–0.7 and 1.1–1.4 for about as many battles there, and it lost 2.2–3.6
    colonies per empire and 25 turns against our 1.0–1.4 (question 67).
64. **Details the one-turn orders, the exploration rules and the lists leave open** (§7.5
    "How long the ministers' movement orders last", "Exploration", "Placement", §7.2 "Whose
    lists the economy step reads"). OpenSE4's choices since 2026-10-02 (inferred):
    - a turn-based Seek that arrives is done and the next order (an explorer's Warp) runs
      with the movement left; what is left of one when its group's run ends, because the
      movement is spent or a hazard or a battle stopped it, is removed then;
    - "nothing is done while no free frontier point is left" is taken literally: the
      Exploration minister stops before it builds its point list, so the rule that puts
      every frontier point in an empty list (more than 3 × as many Attack Ships) never
      applies;
    - an explorer that stands on the point it takes gets no order and leaves the point in
      the list for the next explorer ("the point then leaves the list" read as part of the
      orders); an explorer that no route takes to any point gets nothing;
    - a planet made during play (Create Planet) still comes before every ship on its
      system's list for the queue list (the original puts it after the ships already there);
    - the economy step that borrows another empire's colonization targets asks its own
      colonization test of each (the targets' order and danger are the other empire's), and
      the neutral empire's colony-ship test that §7.2 names is not modelled;
    - the leftover fleets that explore (§7.5 `AI_Fleets`) get a Seek toward the first free
      frontier point and the Warp through it, which lasts until done. In scratch runs the
      explorers' rule for them (the Warp only when the fleet's movement reaches the
      distance) kept our computers in Exploration 22 % of turns 51–100 instead of 13 %
      (spec 07 "Pace after the movement rules");
    - in a turn-based game the explorer's movement points "at that moment" are those of
      this turn's run: the points come back only after the ministers (§8 step 3), so what
      the last movement left is usually 0, and no explorer that moved the turn before
      would ever get its Warp.

    To verify in the executable: when a turn-based Seek leaves the list, and whether the
    order behind it runs in the same turn; which empty list stops the Exploration minister
    (no free point, or no frontier point at all); whether an explorer on its point takes
    the point out of the list; how a fleet explores; which movement points a turn-based
    explorer compares with its distance; whose settle test the borrowed colonization targets
    get, and what the neutral empire's colony-ship test is; and where a planet made during
    play goes on its system's list.
65. **The computer players' first 25 turns** (spec 07 "Resources, ships and colony losses
    under a debugger", question 61). The original's computers spend 85 % of turns 1–25 in
    Exploration, 12 % in Defend (Short Term) and 2 % in Infrastructure; ours 75, 15 and 7 %.
    Exploration's vehicle row asks for the most colony ships, and the original builds more of
    them and has 12.2 colonies at turn 50 against our 10.4. In turns 1–25 ours have explored
    5.7 systems against 4.8, with more attack ships (1.63 against 1.17 in turns 1–10, 1.94
    against 1.62 in turns 11–20), and their territory borders unexplored space in 76 % of the
    calm turns with contact against 90 %. The rules for the first designs, the first builds
    and the explorers are confirmed (spec 01 §3.6, §7.5), so the cause is open. To verify:
    the turn of each empire's first and second attack ship and first colony ship, the build
    queue of the homeworld in turns 1–10, and the explorers' targets in turns 1–25, read under
    a debugger beside ours.

    **Answer** (in part; observed under a debugger, 2026-10-02; spec 07 "Battles, bases and
    the first turns under a debugger"). The first attack ship comes at turn 2 in both. In
    four games of the original (20 empires) the second comes at turn 9 (median) against our
    turn 5 and the first colony ship at turn 7 against our 9; at turn 5 the original's
    empires have 1.1 attack ships against our 2.0, and from turn 10 both have about 2. The
    homeworld queues read every turn in two games show one sequence in every empire: turn 1
    queues two attack ships and a weapon platform; on turn 2 the first attack ship is
    finished, a new Attack Ship design makes the turn-1 design obsolete and the second
    queued attack ship leaves the queue; the ministers of turn 2 then queue weapon
    platforms, satellites and a colony ship, and an attack ship of the new design only on
    turn 4 (one empire of ten on turn 3), behind them. Ours queue the new attack ship at
    once and finish it by turn 5, before the colony ship. Why the original queues no attack
    ship on turns 2 and 3 is question 70.
66. **Battles away from colonies** (spec 07 "Resources, ships and colony losses under a
    debugger", question 62). Per empire and 25 turns of turns 51–100, our computer players
    fight 9.9–10.5 battles away from any colony that one side wins and 7–9 that end drawn,
    most with no loss; in the original (two games) 6.6–8.2 and 1.0–1.2. Their attack ships
    are lost at 4.2 % per ship and turn against 1.9 %. A battle starts in a simultaneous
    game wherever an object carried out an order that day and sees a hostile object (spec 04
    §2), the same in both. To verify: per battle in the original, the sector (warp point,
    planet or empty), the participants and their ships by design type and order, and the
    losses, read under a debugger from the battle setup and the verdict, beside the same
    records of ours; and in particular whether the drawn battles of ours are ships passing
    each other on their way.

    **Answer** (observed under a debugger, 2026-10-02; spec 07 "Battles, bases and the first
    turns under a debugger", two more games of the original with every battle recorded at
    its setup and its verdict). The original fights drawn battles of the same kind as ours,
    by the same rules, but how many depends on the game: drawn battles away from colonies
    per empire and 25 turns were 0.0 and 21.5 in the two new games (0.8 and 1.2 in games 4
    and 5), against 1.0–21.8 in our 24 games (median 6). In the second new game five attack
    ships, three of them damaged, sat in one sector with a satellite group and an unarmed
    ship of another empire: 30 combat turns without a hit, fought again on most days of the
    turn because a survivor was below full structure (spec 03 §6.3 step 6), 106 battles in
    one sector over turns 51–100 and up to 29 in one turn. Ours draw the same way: 27 % of
    our battles of turns 51–100 end without a shot, mostly armed ships against troop
    transports or carriers that keep away from them (38 % of those), or against satellite
    groups (15 %; the one traced had only missiles, whose `Weapon Target` leaves satellites
    out), or a ship that is slower or damaged (one movement point) chasing a faster one;
    the drawn ones come back the next day while a survivor is damaged (24 % of our battles
    are a second or later battle in the same sector and turn, the new games 13 % and 61 %).
    So the draws are not a rule difference. What does differ:
    - Decided battles away from colonies: ours win 4.9 and lose 5.1 per empire and 25 turns
      (wins 3.0–10.3 per game), the original wins 2.1–3.4 and loses as many in four games;
      ours lose 6.1 attack ships per empire and 25 turns in battle against 4.2 and 4.9.
    - Fleets: a new fleet's leader must be a ship an attack or defence fleet could take,
      never a troop transport or boarding ship (§7.5 `AI_Fleets`, confirmed: binary). Ours
      form fleets of troop transports alone (7 % of fleet-turns) and of carriers alone (6
      %); the original none and 1 % (one game). In a scratch run with the original's rule,
      our battles fell from 11.3 to 9.7 per empire and 25 turns (standard error 0.9), those
      between armed and unarmed ships from 3.0 to 1.9, and attack ships lost in turns 26–100
      from 17.9 to 16.5.
    - The step toward a square in combat (spec 04 §5, confirmed: binary): ours stop behind a
      large piece where the original slides along it; with the original's rule our drawn
      share fell from 42 to 39 %.
    - Scrapping: ours scrap 1.1 attack ships per empire and 25 turns of turns 51–100, as the
      soft cap and the scrap candidates differ (§7.5, the budget and *Scrap*).
    Who engages whom in the decided battles, and whether ships without orders start
    battles, is question 68.
67. **Battles at an enemy colony** (spec 07 "Resources, ships and colony losses under a
    debugger", question 63). The original's computer players win most battles they fight at
    an enemy colony (2.1–3.2 won, 0.8–0.9 lost, 0.2–0.6 drawn per empire and 25 turns in
    two games), and a win there means the colony is gone (spec 04 §15). Ours fight as many
    there but draw most (0.9–1.1 won, 0.4–0.7 lost, 1.1–1.4 drawn), so the colony survives.
    To verify, per such battle in the original and in ours: the attacking ships, their
    weapons and orders, the colony's population, shields and stored units (weapon
    platforms and satellites take hits before the people, spec 04 §11), the number of
    combat turns fought, and the damage the colony took.

    **Answer** (observed under a debugger, 2026-10-02; spec 07 "Battles, bases and the first
    turns under a debugger"). The battles go the same way once fought; the original fights
    more of those it wins. Per empire and 25 turns of turns 51–100, counting each battle
    once, battles in which armed ships attacked an enemy colony and the colony was gone at
    the end: 2.2 and 1.9 in the two new games, 0.9 in ours; colonies lost (all four
    original games): 1.8–3.6 against our 1.1. The colonies taken had about the same hit
    points (spec 04 §11; median 420 and 425 against our 520) and fell to two armed ships
    (median, both) in 11–12 combat turns (ours 10). Attacks that fail are made on strong
    colonies on both sides: in the second new game 43 such battles, the colonies' median
    15,490 hit points with two unit groups; ours 224 in 24 games (median 2,675), 56 of them
    on one colony and 24 % without a shot. The colony threat of the defend list (§7.2)
    differs (the original adds nothing for the colony itself, ours 1) but changes little:
    without the + 1, 17 of our 24 games played out identically (scratch run). Which colonies
    the ministers send ships against is question 69.
68. **Decided battles away from colonies** (question 66; spec 07 "Battles, bases and the
    first turns under a debugger"). Our computer players win 4.9 battles away from colonies
    per empire and 25 turns of turns 51–100 and lose 5.1 (wins 3.0–10.3 per game), the
    original wins 2.1–3.4 and loses as many in four games, and ours lose more attack ships
    in battle (6.1 against 4.2–4.9). In the original's daily battle check the sector of
    every ship that acts by the movement schedule and has an order list seems to be marked,
    whether or not it has an order (inferred: read in the executable, not traced); ours mark
    a sector only when an order is carried out (spec 04 §2). To verify: a breakpoint on the
    marking, to see which ships mark a sector and whether a ship without orders beside a
    hostile one starts a battle; per decided battle in the original, the first orders of the
    ships on each side (Seek, Attack, Warp, none) and which minister gave them, beside ours.
69. **Which enemy colonies the computer players attack** (question 67). The original ends
    1.9–2.2 battles per empire and 25 turns with an enemy colony gone, ours 0.9, for
    battles that go the same way once fought. To verify: per turn in the original, the
    defenders' and fleets' targets that are enemy colonies (the defend-list entry each
    defender takes, §7.5 *Defence*; the fleets' targets in Defend (Short Term), Attack and
    the other states) and the colonies' strength, beside ours; and whether a fleet or a
    defender is ever sent against a colony whose planet out-rates it.
70. **The second attack ship** (question 65). In the original, after the turn-2 redesign
    makes the first Attack Ship design obsolete, the ministers of turns 2 and 3 queue units
    and a colony ship but no attack ship, and the new design's first attack ship is queued
    on turn 4 (one empire of ten on turn 3); ours queue it on turn 2. To verify: the vehicle
    table's counts for the Attack Ship row on turns 2–4 (ships, queued items, the obsolete
    item removed), the backlog test of the homeworld's queue on those turns, and when the
    queued obsolete item leaves the queue relative to the Ship Construction minister.
71. **The soft cap in turns 26–50.** With colony ships left out of the maintenance (§7.5),
    our computer players are over the soft cap in 4 % of turns 26–50 and 14 % of turns
    51–100 (scratch run); the original in none of turns 26–50 and 14–16 % of turns 51–100
    (two games). To verify: the maintenance and revenue compared at the test in turns 26–50
    in the original and in ours, and which vehicles make the difference.

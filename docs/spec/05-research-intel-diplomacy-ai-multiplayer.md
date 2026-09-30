# Spec 05: Research, intelligence, diplomacy, events, scores, AI and multiplayer

This is a clean-room behavioral spec of Space Empires IV Deluxe (v1.95), written for engine
programmers. Each rule is tagged with its source:

- **[M]**: the HTML or PDF manual.
- **[D]**: data-file headers and records.
- **[H]**: the shipped version history (`History.txt`, `DataFileHistory.txt`).
- **[T]**: numbers shown in the tutorial scenario text.
- **[I]**: our inference, which must be checked in the running game (see the last
  section).

Field keys and enum values are functional identifiers. OpenSE4 content should use its own
names and numbers.

## 0. Conventions

- **Time**: one turn is a month, or 0.1 year. The game starts in 2400.0 [M]. Timed data
  fields are counted in turns.
- **Data format**: data files hold `Key := Value` records between `*BEGIN*` and `*END*`.
  Lists are written as a count key followed by keys numbered from 1. Keys that are absent
  take a default, usually 0 or "none" [H].
- **Mods**: `Path.txt` names a mod folder, and each file falls back to the base install
  when the mod lacks it [H].
- **Math**: turn resolution uses integer percentages throughout.

## 1. Research

### 1.1 Research points (RP)

- **Planet output**:
  - Base: the Value1 of each facility with `Point Generation - Research` (stock: a few
    hundred per center) [D].
  - Scaled by the population-production table (`Pop Modifier N` in `Settings.txt`), the
    mood modifier (`Mood … Modifier`, where riot = 0), the `Planet/System Point Generation
    Modifier - Research` (±%), the race's `Intelligence` characteristic and the culture's
    `Research` % [M][D].
  - Worked example [T][I]: the tutorial homeworld has one base-500 center and yields 780
    RP. That equals 500 × 130 % × 120 %, a population modifier times the jubilant mood.
- **Spaceport rule**: a system without a Spaceport contributes nothing, unless the empire
  has `Natural Merchants` [M][D].
- **Flat empire sources**: the `Generate Points Research` ability, which no planet pays
  for [D][H], and treaty trade (§3.3).
- **Opening pool**: turn 1 brings a large one-off pool. The tutorial shows about 20.6k RP
  against 780 RP/turn afterwards [T]. We assume it scales with the Starting Resources
  setting [I].
- **No carry-over**: RP that the projects cannot absorb in a turn are lost [M].

### 1.2 `TechArea.txt`

| Field | Semantics |
|---|---|
| `Name` | Key that other files reference. |
| `Group` | Free-text UI group. The stock groups are Applied Science, Theoretical Science and Weapon Technology. |
| `Description` | Tooltip. |
| `Maximum Level` | Level cap (1–12 in stock data). |
| `Level Cost` | Base RP per level (§1.4). |
| `Start Level` | Start level under the "Low" tech-start option. The three colonization areas are overridden: the one matching the home planet type starts at 1 [D][M]. |
| `Raise Level` | Start level under the "Medium" option, as max(Start, Raise). The "High" option starts every area at its maximum. |
| `Racial Area` | n > 0: the area is visible only to races that have a trait with `Trait Type := Tech Area` and `Value 1 := n`. The stock values are Psychic, Religious, Temporal, Crystallurgy and Organic. |
| `Unique Area` | n > 0: the area is visible only after the empire gains an `Ancient Ruins Unique` ability with Value1 = n, by colonizing that ruin planet. |
| `Can Be Removed` | Whether setup may exclude the area ("Technology Areas Allowed"). Items that depend on an excluded area can never be obtained. |
| `Number of Tech Req`, `Tech Area Req N`, `Tech Level Req N` | All must hold (AND) before the area is visible. |

An area is **researchable** when all of these hold [D]:

- it is allowed in this game;
- its racial and unique checks pass;
- all its requirements are met;
- its level is below the maximum.

Example: an armor area appears once Chemistry reaches level 1. The `Ancient Ruins`
ability (Value1 = N) grants N random tech advances when the planet is colonized. How big
each advance is remains open.

### 1.3 Level cost

- **Data points** [T][D]: in an area with `Level Cost` 10000, going from level 1 to 2
  finished in one turn on about 20.6k RP. In an area with `Level Cost` 5000, the same step
  showed an ETA of 1.3 years at 780 RP/turn, which means a cost between 9.4k and 10.1k.
- **Model**: both points fit a cost of `LevelCost × L` at the default setting, where L is
  the target level. The data header says the setup factor makes higher levels cost more.
  Implement:

  `Cost(L) = LevelCost × (100 + (L − 1) × g) / 100`

  Here g is the growth percent of the chosen "Technology Cost" option. The default is
  g = 100 [I]. The set of options and their g values is open. A flat multiplier
  (`LevelCost × L × m`) fits the tutorial equally well.

### 1.4 Queue and allocation [M]

- **Queue**: up to 12 projects. Each project is (area, next level). It shows its ETA as
  ceil(remaining / RP applied) turns, displayed in years [T].
- **Divide Pts Evenly** (the default): each project gets floor(RP / N).
- **Divide off**: projects are funded in list order, each up to its remaining cost, until
  RP run out. Example: 10k RP against three projects of 4k each completes two and puts 2k
  into the third.
- **Repeat Projects**: a finished project re-appends the area's next level, until the area
  is maxed.
- **Removal**: removing a project throws away its progress. Progress is stored per project
  [I].
- **Reorder**: a separate window moves projects up, down, to the top or to the bottom.
- **Completion messages**: finishing a level logs "New Tech Level", then "<item>
  Discovered" for each newly available item. An empty queue logs "All Projects Completed"
  [T].
- **OpenSE4 choices** [I]:
  - A project completes at most one level per turn; points it cannot absorb are lost,
    also in Divide Evenly mode.
  - Several entries for one area stand for successive levels: the n-th entry works toward
    current level + n. Entries beyond the maximum are dropped.
  - The ETA simulates the queue with this turn's points, so projects waiting behind others
    in in-order mode get a real estimate.
  - Every level a subject gains (research, gifts, theft) is also granted to a Subjugation
    master. A Protectorate passes nothing.

### 1.5 Unlocking and other tech sources

- **Unlocking**: components, facilities, vehicle sizes, component enhancements (mounts)
  and intel projects all carry the same `Number of Tech Req` / `Tech Area Req N` / `Tech
  Level Req N` block (AND). An item with zero requirements is available from the start
  [D].
- **When to recompute**: after every tech change, re-evaluate what is available and log
  the new items. Tech changes come from research, gifts, trades, theft, ruins, subjugation
  sharing and the Medium/High start.
- **Other sources**:
  - A Technology item in a gift, tribute or trade package gives the giver's level. It
    helps only when the receiver's level is lower. Setup options "Allow Gifts/Tributes" and
    "Allow Technology Gifts/Tributes/Trades" can forbid it [M].
  - Subjugation passes the subject's discoveries to its master (§3.2).
  - `Research - Steal` intel (§2.3).

## 2. Intelligence

### 2.1 Points and lifecycle

- **Intelligence points (IP)**: generated like RP. Sources are `Point Generation -
  Intelligence`, IP modifiers, `Generate Points Intelligence`, the culture `Intelligence`
  %, and Partnership trade.
- **Cunning**: the manual credits the race's `Cunning` characteristic with better intel
  operations. Whether that means generation or success chance is open; the default
  assumption is generation [I].
- **Accumulation**: IP do not accumulate [M]. The option "Allow Intelligence Projects"
  disables every project [M].
- **Queue**: the same as research — 12 slots, Divide Evenly or in-order funding, Repeat,
  Reorder [M].
- **Adding a project**: pick a target empire we are **in contact with** (§3.1), friend or
  foe; defense projects take no target. Then pick a specific target according to the type:
  a known planet or ship, or a third empire for political operations. The choice "Any"
  lets the operatives pick the easiest target and succeeds more often [M].
- **Execution**: when progress ≥ `Cost` the project runs. It then succeeds, fails
  (invalid target), or is defeated by counter-intelligence (§2.4). It leaves the queue
  unless Repeat is on.
- **Messages**:
  - The source is sent a source message.
  - The victim gets a titled target message, prefixed with an "Intelligence Minister"
    label. It may add a suspicion line naming the source (a detection roll [I]).

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

### 2.3 Effect handlers

Amounts are the record's `Effect Amount`. The two stock values quoted below are examples only [D].

| Category | `Type` → effect on success |
|---|---|
| Ship sabotage | `Ship - Damage`: Amount damage points, using the standard damage order [I]. `Ship - Lose Movement`: the ship cannot move this turn [I]. `Ship - Lose Supply`: Amount supply. The stock record's amount is 1000, but its text says "all" (open). `Ship - Rebel`: the ship defects to the source. `Ship - Experience Change`: add a signed Amount, for example −50. `Ship - Cargo Damage`. `Ship - Orders Change`: bogus orders on exactly one ship [H]. |
| Ship espionage | `Ship - Locations`, `Ship - Concentrations`: reports. `Ship - Construction Info`: the largest queue concentrations. `Ship Designs - Steal`, `Unit Designs - Steal`: copy designs. |
| Planet sabotage | `Planet - Conditions Change`: Amount condition steps. `Planet - Value Change`: a permanent value shift. `Planet - Population Change`: kill population (in millions). `Planet - Population Anger Change`: add anger; Emotionless races are immune [H]. `Planet - Population Rebel`: the planet leaves the target's control (new owner open). `Planet - Cargo Damage`. `Planet - Facility Damage`. |
| Planet espionage | `Planet - Info`: a full report, including cargo [H]. `Planet - Locations`. |
| Points | `Points - Change`: the target loses Amount of stored resources. `Points - Steal`: move Amount from the target to the source. |
| Projects | `Research - Steal`: gain a level the target has (which one is open). `Research - Delete Project`, `Intel - Delete Project`: kill one project and its progress. |
| Politics | `Politics - Disrupt Trade`: only when real trade exists [H]; whether the trade % resets or drops is open. `Politics - Intercept Messages`, `Politics - Fake Messages` (appear to come from a third empire), `Politics - Prevent Messages` (between the target and a third empire), `Politics - Treaty Info`. |
| Info | `System - Info` (star charts), `Empire - Info`, `Tech Level - Info`. |
| Defense | `Intelligence Defense`, with level = Amount (1–3). See §2.4. |

### 2.4 Counter-intelligence

**Facts** [D][H]:

- A defense project works **while it is in progress**. Level 1 needs no tech.
- Several defense projects protect better than one.
- Each incoming attack is tested separately.
- When a defense wins, the attack is defeated, both sides are logged, and the defense
  project's progress is **set back**.
- `Intelligence Defense Modifier Percent` in `Settings.txt` and the `Change Bad
  Intelligence Chance - System` ability (±%) both adjust defense.

**Placeholder model [I]**, applied to each attack against empire T in a deterministic
order:

1. Defense strength D = Σ (progress × level) × modifier / 100, over T's defense projects.
2. Attack strength A = the attack's `Cost`, raised a little when the target is "Any".
3. The attack is blocked with probability D / (D + A).
4. On a block, remove A from T's defense progress, in list order.
5. If not blocked, apply the system's bad-intel modifier to the success roll.

**AI reaction**: a detected attack adds `Intelligence Against Us` anger (§7.3).

**OpenSE4 choices** [I]:

- Defense projects never finish. Their progress stops at `Cost`, and a block drains the
  attack strength from it, so the queue refills them.
- Points are allocated for every empire first, then projects run empire by empire, so
  each defense holds this turn's points when attacks arrive.
- The "Any" target raises attack strength by 25 %.
- `Change Bad Intelligence Chance - System` sets the success chance to 100 % plus the
  system abilities plus the best (lowest) facility value of the target's colonies in the
  target's system. Empire-wide projects ignore it.
- The victim learns the source with a 50 % chance.
- A project that fails (no contact, no valid target, a failed roll) is used up like a
  successful one; Repeat restarts it in place.

**Handler readings** [I] (the stock amounts are often a placeholder 1):

- `Ship - Lose Movement`: the ship cannot move for Amount turns (at least one), starting
  next turn (`Vehicle::immobileUntil`).
- `Ship - Lose Supply`: Amount supply, or all of it when Amount ≤ 0.
- `Ship - Cargo Damage`, `Planet - Cargo Damage`: Amount kT of cargo, picked a unit or
  1M at a time; an Amount of 1 or less destroys about half.
- `Ship - Orders Change`: the ship leaves its fleet and gets one Move To a random sector
  of its system.
- `Planet - Value Change`: percentage points on every resource, clamped to the Settings
  range but never pushed back into it; in finite games, a percentage of the stock.
- `Planet - Population Anger Change`: tenths of a percent, like Happiness.txt.
- `Planet - Population Rebel`: the colony joins the source. As "Any", homeworlds are
  never picked. As an event (no source), the colony is lost.
- `Points - Change`, `Points - Steal`: Amount of each resource.
- `Research - Steal`: one level above ours in a random area where the target leads and
  that we can research.
- `Ship Designs - Steal`, `Unit Designs - Steal`: a copy joins our designs (building it
  still needs the technology), preferring designs we have not seen.
- `Politics - Disrupt Trade`: the trade percentage restarts from 0.
- `Politics - Prevent Messages`: for Amount turns, messages between the two are lost;
  war declarations, broken treaties and surrenders still get through
  (`Relation::messagesBlockedUntil`).
- `Politics - Fake Messages`: a forged tribute demand from the target reaches the third
  empire.
- `Politics - Intercept Messages`: a report of their messages from the last 10 turns.
- `System - Info`: prefers a system the target knows and we do not.

## 3. Diplomacy

### 3.1 Contact [M]

- **First contact**: decloaked ships of both empires meet in a system.
- **What contact enables**: messages, treaties, trades and intel.
- **Losing contact**: checked at the end of each turn. If no warp path (explored or not)
  links any of our planets to any of theirs, contact is lost and relations are severed.
  We assume this resets the treaty [I].
- **Buying contact**: a package item **Comm Channels** grants contact with a third
  empire.
- **OpenSE4** [I]: contact begins when one empire sees a vehicle of the other, or has
  presence in a system where the other has a colony. Contact loss is checked only
  between empires that both have colonies and did not see each other this turn, and not
  in games without warp points.

### 3.2 Treaties

Treaties are ordered from worst to best. Benefits accumulate from Non-Aggression up the
trade chain [M].

| Treaty | Effect |
|---|---|
| War | Hostile. |
| Non-Intercourse | Cold. The parties agree to keep apart. Counts as a "bad" treaty for happiness. Combat behavior is open. |
| None | Met, no treaty. Ships still fight, because Non-Aggression is the first level that stops combat. |
| Non-Aggression | No combat. Ships may share sectors. |
| Subjugation | The subject pays `Treaty Subjugated Resource Percentage` each turn and may hold no other treaty; accepting it breaks the subject's other treaties and notifies those empires [H]. The master sees the subject's new designs and receives its tech discoveries. |
| Protectorate | The protected side pays `Treaty Protectorate Resource Percentage`. Protection is honor-based. |
| Trade Alliance | Resource trade (§3.3). |
| Trade and Research Alliance | Adds RP trade. |
| Military Alliance | Adds resupply at the ally's depots. |
| Partnership | Adds IP trade and shared sight (the partner's view and explored systems). Also gives copies of enemy designs the partner scans. |

- **Combat**: allies never fire on each other, not even when two of our allies fight. Both
  allies defend a shared location [M].
- **Happiness**: Trade and better are "good" treaties that please the empire. War,
  Non-Intercourse, Subjugation and Protectorate displease it [M].
- **Treaty grid**: treaties between third parties are visible only for empires we are
  allied with [M], or through `Politics - Treaty Info`.
- **OpenSE4 choices** [I]:
  - In a Subjugation or Protectorate proposal the proposer is the master, unless the
    message names the dominant side in its third-empire field. A Treaty package item
    makes the receiver the master unless it names the giver.
  - Becoming a subject ends every treaty better than None with other empires; War and
    Non-Intercourse stay. A subject cannot sign new treaties with third empires.
  - Break Treaty does not end a war.
  - Partnership shares explored systems, known warp links and scanned designs each turn.

### 3.3 Trade and tariffs

- **Income**: each turn, from each partner at Trade Alliance or better,
  `income_r = partnerGenerated_r × tradePct / 100` [M][D]. It is created from nothing, and
  the partner receives the same benefit from us. Resources are traded from Trade Alliance
  up, RP from Trade & Research Alliance up, and IP only in a Partnership.
- **tradePct**:
  - starts at 1 on the turn after the treaty is signed and rises by 1 each turn;
  - is capped at `Maximum Trade Percentage` (stock value 20);
  - is kept when switching between two treaties at trade level or above [H];
  - drops to 0 when the treaty falls below trade level; a new treaty starts again at 1.
- **Modifiers**: Political Savvy and the culture `Trade` % increase trade income [M]. How
  they stack is open. We assume "generated" means production before maintenance and
  tariffs [I].
- **Tariffs**: the subordinate's resources are cut by the treaty percentage before it
  spends anything, and the master receives that amount [M]. Receipts are capped at the
  receiver's storage [H]. An old bug capped the subject's RP, which hints that tariffs may
  also touch RP (open).
- **OpenSE4 choices** [I]: the trade base is the output of the partner's colonies that
  reaches its treasury; Political Savvy (− 100) and culture Trade add to the percentage
  multiplier; tariffs take resources only.

### 3.4 Messages [M]

- **Rules**:
  - An empire may send one message per recipient per turn. It arrives in the recipient's
    log next turn.
  - A message has a type, a tone (Pleading, Neutral or Demanding) and editable template
    text.
  - An AI in a simultaneous game replies two turns after the send [H].
- **Binding types**:
  - Propose, Accept, Refuse and Counter Treaty. A treaty takes effect only on acceptance.
  - **Break Treaty**: immediate, and the treaty falls to None.
  - **Declare War**: immediate, whatever the current treaty.
  - **Propose, Accept, Refuse and Counter Trade**: the asked-for side may hold "Any"
    placeholders. A trade with placeholders cannot be accepted; the recipient must counter
    with real items. Items lost before acceptance show as "unavailable" [H].
  - **Gift, Tribute**, with Accept and Refuse. Items move only on acceptance.
  - **Surrender**: the sender's whole empire passes to the recipient at once, and ground
    combat stops [H].
  - **Grant independence**: marks one of the sender's planets, which the sender then
    evacuates and abandons; the recipient may then colonize it.
- **Non-binding demands and requests**, each taking a system, planet or empire where
  relevant: want a gift, want a tribute, demand surrender, remove ships from a system,
  remove colonies from a system, leave a planet, stop hostilities against an empire, break
  a treaty with an empire, declare war on an empire, make peace with an empire, support us
  against an empire, attack an empire in a system, attack a planet, stop espionage, stop
  sabotage, stop attacks in a system. The recipient replies Accept or Refuse. Only the AI
  acts on them (§7.4).
- **Package items**:
  - Systems: gives up our border claim.
  - Planets: transfers ownership.
  - Resources: in steps of 1000.
  - Technologies.
  - Ships.
  - Loaded transports: the ship and its cargo.
  - Star Charts: the system becomes explored for the recipient.
  - A Treaty: becomes active on acceptance.
  - Comm Channels.
- **Borders**: each empire publicly claims systems, and claims may overlap. The AI uses
  claims to compute anger.
- **History window**: dated events per empire, plus a "General" list with optional map
  locations.
- **Log categories**: Construction, Research, Intelligence, Events, Politics, Combat, Misc.
- **OpenSE4 choices** [I]:
  - Replies are checked against the message they answer: an acceptance only works for a
    proposal the recipient really sent. Accepting a counter-proposal (an Accept Demand
    reply) applies the counter.
  - Messages are kept 10 turns after sending, then dropped, together with any message
    whose sender or recipient is gone.
  - Surrender also hands over the stockpile, the technology levels and the maps.
    Granting independence removes the colony at once.

## 4. Random events

- **Setup options**:
  - **Event Frequency**: None, Low, Medium or High. Low, Medium and High map to `Event
    Percent Chance Low/Medium/High` in `Settings.txt` (stock 5, 10 and 25 %) [M][D].
  - **Maximum Event Severity**: only records at or below the chosen severity are
    eligible.
  - **Roll scope**: open. The default assumption is one roll per empire per turn [I].
- **Chance modifiers**:
  - The `Lucky` trait (`Luck` −50) halves the chance of bad events.
  - `Change Bad Event Chance - System` (±%) adjusts the chance per system.
  - `Mechanoids` blocks plague.
  - `Emotionless` blocks riots caused by events [D][H].
- **`Events.txt` fields**:
  - `Type`: shares the ship, planet, points and project handlers with intel. It adds
    `Ship - Moved` (Amount sectors [I]), `Planet - Population Riot`, `Planet - Plague` and
    `Plague Cured`, `Planet/Star - Created/Destroyed`, and `Warp Point - Opened/Closed`.
    Types without a stock record never fire unless a mod adds one.
  - `Severity`: Low, Medium, High or Catastrophic.
  - `Effect Amount`: the handler's parameter. Messages can show the realized value with
    `[%ActualAmount]`.
  - `Message To`: None, Owner, Sector, System or All.
  - `Num Messages`, `Message Title N`, `Message N`, `Picture`: the log message.
  - `Time Till Completion`: 0 means immediate. N > 0 sends the `Start Message` lines now
    and applies the effect about N turns later. Stock catastrophes take 10 to 30 turns.
- **Selection [I]**: pick uniformly among the eligible records, then pick a valid target
  in the affected empire.
- **OpenSE4 choices** [I]:
  - For a bad event, Luck and the target system's `Change Bad Event Chance - System`
    (system abilities plus the best facility) give a keep chance of
    (100 + luck) × (100 + modifier) / 100 %.
  - A timed event strikes after exactly `Time Till Completion` turns, and fizzles if its
    target is gone by then.
  - `Ship - Moved` sends the ship to a random system at most Amount warp jumps away (the
    stock text speaks of another system, not of sectors).
  - `Planet - Destroyed` leaves an asteroid field; `Star - Destroyed` leaves a destroyed
    star, turns the system's planets into asteroid fields and destroys every vehicle
    there. Homeworlds, and for stars their systems, are never chosen.
  - `Planet - Population Riot` raises anger to the Rioting band.
  - Sector and System messages also go to the owner. A record with a blank text names
    what was hit.

## 5. Scores and history

- **Score window columns** [M]:
  - Score and Rank (ranked across all empires).
  - Resources, Research and Intelligence per turn.
  - Total tech levels.
  - Systems colonized, Planets, Population.
  - Units, including units in space [H].
  - Ships, excluding mothballed ones [H].
  - Bases.
- **Score formula**: a weighted sum of these statistics with **undocumented weights**. Keep
  the weights in data and calibrate them.
- **Visibility**: the "Score Display" setup option shows self plus allies (the default),
  or all empires.
- **Comparisons window**: plots the same statistics per turn, with gaps for periods when
  there was no treaty and the data was unknown. So record every empire's statistics every
  turn with a per-viewer "known" flag. SE4 stores this per player in `<game>_stats.txt`,
  and the History window's data in `<game>_events.txt`.
- **OpenSE4 weights** [I]: the score is Σ statistic × weight / 1000, with Settings keys
  `Score Weight Resources`, `Research`, `Intelligence`, `Tech Levels`, `Systems`,
  `Planets`, `Population`, `Units`, `Ships` and `Bases` (defaults 100, 100, 100, 100000,
  100000, 200000, 1000, 5000, 50000, 50000). The resource columns are the turn's income,
  population counts in millions.

## 6. Victory

The setup may combine any of these conditions. With none selected, the game runs until one
empire is left [M]. Victory is checked at the end of each turn [I].

| Condition | Test |
|---|---|
| Score reaches X | The first empire with score ≥ X wins. The default X is 50000. |
| X years elapsed | At start + X years, the highest score wins. |
| X % of second place | The leader wins when leaderScore × 100 ≥ X × secondScore. X is at least 100 [H]. |
| X % of tech | Owned levels ÷ Σ max levels ≥ X %. It counts levels, not areas [H]. |
| Peace for X years | No War treaty between any two empires for X consecutive years. The winner rule is open. |
| After X years | A qualifier: suppresses all checks until start + X years. |

**OpenSE4 choices** [I]: an empire is eliminated when it has no populated colony and no
ship or base; its last turn is still recorded, and its empty colonies and units vanish.
Neutral empires neither win nor count toward the last empire standing, which needs at
least two empires at the start. Ties go to the lower empire number, and a peace victory
goes to the best score. The tech percentage leaves out racial and unique areas and areas
not allowed in the game.

## 7. Computer player

### 7.1 Control, selection, difficulty

- **Who plays AI**: an empire is AI-controlled if it is "Computer Controlled" at setup,
  toggled in the Player Computer Control window, or a simultaneous player who submitted
  nothing (§9.2) [M].
- **Human ministers** hand single areas to the AI [M]:
  - Global: Design, Construction, Expenses, Production, Research, Intelligence, Politics,
    Repair, Resupply, Scrap, Retrofit.
  - Per-object: Facilities, Transports, Carriers, Colonization, Attack, Defense,
    Exploration, Patrol, Mines/Satellites, Fleets, Stellar Manipulation, Cloaking, Space
    Yard Ships, Troops.
  - "Minister Style" selects a personality folder (§7.2), or the race's own files. AI
    ministers never surrender [H].
- **Random AIs** [D][H]:
  - The Low/Medium/High "number of computer players" option picks a count in
    [`Minimum`, `Maximum Computer Player <L> Setting`]. Neutral empires use the
    `… Neutral Player …` keys.
  - Each random AI draws a personality group weighted by `Random Player Personality Group
    N Percent`.
  - It then draws a race whose `AI_Settings` has `Personality Group := N`. Group 0 is never
    drawn.
- **Neutral empires** stay in their home system, never use warp points and cannot be
  played by humans [M].
- **Difficulty and bonus**: the AI gets no information a human would lack [M]. "Computer
  Player Bonus" raises AI production and research by an amount that is open. What
  "Difficulty" does is open.
- **Team Mode**: every AI is locked in Partnership with the other AIs against the humans
  [M][H].

### 7.2 File lookup

Files are named `<prefix>_AI_<Name>.txt`. The loader first looks in the race's own folder
and falls back to `Ai/Default_*` [M].

| Folder | Files |
|---|---|
| Player race folder | Anger, DesignCreation, Fleets, General, Politics, Research, Settings |
| Neutral race folder | Anger, Fleets, General, Settings |
| `Ai/` (global only) | Construction_Facilities, Construction_Vehicles, Planet_Types, Speech, Strategies. `Strategies` is effectively global and is reloaded on load [H]. |
| `Ai/Aggressive`, `Ai/Defensive`, `Ai/Neutral` (minister styles) | Anger, General, Politics, Settings, Speech |

Most tables filter their rows by **AI state**. The states are Exploration,
Infrastructure, Prepare for Attack, Attack, Secure Holdings After Attack, Incursion,
Prepare for Defense, Defend (Short Term), Defend (Long Term), and Not Connected (no path to
any other empire) [D].

The transitions between states are hard-coded. `Turns to Wait until next attack` is one
known input [I].

**Placeholder transitions [I]**, evaluated once per turn in phase 12:

1. Armed hostile ships in one of our colony systems: Defend (Short Term), becoming Defend
   (Long Term) after 10 turns. When they are gone, Prepare for Defense for 5 turns.
2. No warp route to any other empire's colony: Not Connected.
3. At war: Prepare for Attack when our warships outweigh the enemy's visible ones (by a
   difficulty-dependent margin), else Prepare for Defense. Prepare for Attack becomes
   Attack after 3 turns; Attack becomes Secure Holdings after 15 turns, or after 5 once we
   are no longer stronger; Secure Holdings waits `Turns to Wait until next attack`, then
   goes back to Prepare for Attack, or to Incursion when weaker. A weaker empire with at
   least 3 warships raids (Incursion).
4. At peace: Exploration during the first 30 turns while unexplored neighbours remain,
   otherwise Infrastructure. Neutral empires never explore.

### 7.3 Anger (`AI_Anger`)

Each AI keeps a scalar anger toward every other empire. The Empires window shows it as the
leader's "mood".

- **Per turn**:
  - `Per Attack Location` × the number of the other empire's planets we covet. Which
    planets count is set by the `AI_Settings` keys `Get Angry Over Allied/Enemy Colonizable
    Planets` and `Percentage of Allied/Enemy Planets to consider as Attack Locations for
    Anger`.
  - `Per No Treaty / Ally / Enemy Ship` × their ships inside our claimed borders.
  - `Regular Decrease` (negative).
  - `Mega Evil Empire` while the other empire is the MEE.
- **Per event**: `Combat Attacking/Defending Won/Lost/Stalemate`, `Intelligence Against
  Us`, and `Receive <MessageType>` for each of about 40 message types. Negative values
  soothe; accepted treaties and gifts are examples.
- **Floor**: anger never drops below `Minimum Anger`. The thresholds imply a 0–100 scale,
  but the upper clamp is open.
- **Personalities** only change these numbers. For example, the aggressive profile has a
  higher floor, slower decay and more anger at ships in its borders [D].

### 7.4 Politics (`AI_Politics`)

**Score ratio**: P = (their score / our score) × 100 [D].

**Tone**: Demanding if P ≤ `Score Percent For Demanding Tone`, Pleading if P ≥ `Score
Percent To Pleading Tone`, otherwise Neutral.

**Treaty thresholds [I]**. Each treaty rule computes a threshold T:

- Start from `<Rule> Base Anger Level`.
- Add `Per Other Wars` × the number of wars we are in.
- Add `First 50 Turns Modifier` during the first 50 turns.
- Add the `Stronger Player Amount` when P ≥ `Stronger Player`, or the `Weaker Player
  Amount` when P ≤ `Weaker Player`.
- For Accept Treaty only, add `Per Higher Treaty Level` for each step the offer is above
  the current treaty.

Friendly rules (Accept, Propose) fire when anger < T. Hostile rules (Break, Declare War)
fire when anger > T.

**Accept Treaty** has extra gates:

- The treaty must not exceed `Highest Allowed Treaty`.
- The last treaty change must be at least `Minimum Time From Last Treaty` turns ago.
- No friendly treaty within `Turns Since Last War Before Friendly Treaty` turns of a war.
- Subjugation and Protectorate need P ≥ their own score percent.
- `Minimum Anger Chance` acts as a probability floor (open).

**Propose Treaty**: with `Propose Treaty Percent Chance Per Turn`, pick the first `Type
N`, ordered best first, for which anger ≤ T − `Type N Anger Level Below Computed`. Never
propose the current treaty [H].

**War intent**: an AI that wants war tends to ignore incoming messages [H].

**Demands**:

- Send one when P ≤ `Score Percent to Send <X>` and the `Will Send To Friend/Enemy <X>`
  flag allows it.
- Accept one when P ≥ `Score Percent To Accept <X>` and `Will Accept From Friend/Enemy
  <X>` allows it.

**Gifts and tributes**:

- Given: value = `Value Base` + `Per Percentage Greater Score` × max(0, P − 100) [I], and
  only when anger ≤ `… Max Anger`.
- Accepted: when anger ≤ `Max Anger Level for Accept a Gift/Tribute`.

**Trades**: accepted when received ≥ `Accept Trade From Friend/Enemy At Percentage…` %
of what is given. Every package item type needs a valuation (open).

### 7.5 Other AI tables

- **`AI_Research`**: an ordered list of rows (state set, `Tech Area Name`, `Tech Area
  Level`, `Tech Area Min Percent`). Each turn, add a row as a project when [D][H]:
  - its state matches;
  - the area is visible;
  - the area is below the row's level (9999 means the maximum);
  - no existing project's minimum share would be broken (100 = alone, 50 = at most one
    other, 33 = two others, 25 = three others).

  The share rule only blocks additions. `AI_Settings` also caps RP and IP generation.
- **`AI_Planet_Types`**: an ordered list of rows (state set, colony `Planet Type`, `Max
  Per System`, `Percent of Colonies`, `Minimum Planet Size for Type`, three
  resource-value ratio thresholds, `Maximum Total in Empire` where 0 means unlimited). A
  new colony takes the first row that fits, and the last row always fits [D].
- **`AI_Construction_Facilities`**: rows are (state set, queue type = Homeworld or a
  colony type, ordered `Facility N Ability` + `Amount`). Rules [D]:
  - skip abilities that no researched facility provides;
  - stop when the planet's facility slots run out;
  - build system-wide abilities only if the system lacks them.
- **`AI_Construction_Vehicles`**: one queue per state, with entries (`Type` = AI design
  type, `Planet Per Item` in tenths, `Must Have At Least`). For each entry in order [D]:
  1. Top up to the Must-Have count (existing plus under construction).
  2. Buy more while planets × 10 / PlanetPerItem > count.
  3. Wrap around to the first entry.

  Each item goes to the fastest available yard.
- **`AI_DesignCreation`**: one template per AI design type. Fields:
  - vehicle type, default strategy, tonnage window, must-have abilities;
  - minimum and desired speed;
  - five majority and five secondary weapon-family picks;
  - "spaces per one" densities for shields, armor, majority and secondary components and
    misc abilities.

  Regenerate a design when a new component or hull is researched, taking the newest hull
  that satisfies the constraints [D][H].
- **`AI_Fleets`**: bands of ship or planet counts → number of fleets; % of ships in
  fleets; turns before fleets are used; default formation and strategy; % of fleets kept
  for defense.
- **`AI_Settings`**: early hull-tonnage caps by turn, maximum maintenance % of revenue,
  number of systems defended at once, movement caution flags, satellite and drone
  stockpile percentages and targeting ranges, and `Personality Group`.
- **`AI_General`**: identity fields (names, descriptions, `Demeanor`, `Culture`,
  `Happiness Type`, planet and atmosphere, design-name file), plus three `Race Opt` build
  variants. We assume the variant is picked by the racial-point budget [I].
- **`AI_Speech`**: numbered text pools:
  - `Send <Type>`;
  - `Response Friend/Enemy <Type>`, with YES and NO variants for demands;
  - miscellaneous friend and enemy chatter;
  - `Mega Evil Declarations`.
- **`AI_Strategies`**: the same schema as `DefaultStrategies.txt` (§7.7). Other tables
  refer to it by name.

### 7.6 Mega Evil Empire (MEE)

`Settings.txt` has four keys: `AI Uses Mega Evil Empire`, a threshold in thousands of
score points, and separate score percents for human and computer players [D].

Interpretation [I]: an empire is the MEE when both of these hold:

- its score is at least threshold × 1000;
- its score is at least that percent of the next-best score.

AIs then add the MEE anger each turn and may make an MEE declaration.

### 7.7 Player default lists (not used by the AI) [M]

- **`DefaultDesignTypes.txt`**: design-type labels. Ministers use them to find, for
  example, colony ships.
- **`DefaultColonyTypes.txt`**: colony-type labels. A planet's colony type selects its
  facility-minister queue.
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

## 8. Turn processing order (proposed [I])

The manual gives no explicit order. The order below fits the documented timings:

- messages arrive next turn;
- trade begins the turn after signing;
- RP and IP are spent in the turn they are shown;
- a completed level appears in the next turn's log;
- contact is evaluated at the end of the turn.

In turn-based mode, a human's movement and combat happen during their own turn.

1. **Orders.** Turn-based: players act in sequence. Simultaneous: merge the `.plr` files,
   and the AI plays any empire that sent none. Then AIs and ministers act.
2. **Diplomacy.** Deliver messages. Apply accepts, breaks and declarations, move packages,
   and apply surrenders.
3. **Movement and space combat.** Simultaneous mode uses §9.3.
4. **Ground combat and capture**, then colonization.
5. **Economy.** Production, trade, tariffs, maintenance, then queue spending.
6. **Research.** Allocate, complete levels, unlock items, repeat.
7. **Intelligence.** Allocate, then execute projects empire by empire in a fixed order,
   including defense tests.
8. **Population.** Growth, mood, riots, rebellion, plague.
9. **Events.** New rolls, and timed-event countdowns.
10. **Upkeep.** Supply and repair.
11. **Contact.** Check contact, then advance trade %.
12. **AI anger.** Decay, borders, MEE.
13. **End of turn.** Statistics, scores, history; victory check; date += 0.1.

Today's `processTurn` covers steps 3–6 and 8. Every step needs a stable iteration order
and draws its randomness from `GameState::rng`.

## 9. Multiplayer

### 9.1 Modes [M]

- **Play Style**: Hotseat or Different Machines.
- **Turn Style**:
  - **Turn-Based**: players act in sequence, and orders execute immediately. On different
    machines the save file passes from player to player.
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
  - `.gam`: the full state, produced by the host.
  - `.plr`: one player's turn changes, deleted after processing.
  - `.trn`: the movement replay.
  - `.cmb`: the combat replays.
  - `<game>_events.txt` and `<game>_stats.txt`: per-player history and statistics.
- **Flow**:
  1. The host fixes the settings, and the players send their `.emp` files.
  2. The host creates the game and sends out the `.gam`.
  3. Each player logs in and plays. Saving mid-turn is allowed. End Turn writes the
     `.plr`.
  4. The host loads the game with the master password and processes the turn:
     - the AI plays any empire whose file is missing, making only minimal changes if that
       empire set the option "AI should not make changes during a Simultaneous game" [M];
     - missing or out-of-date files trigger warnings.
  5. The host sends out the new files.
- **Password reset**: the host may reset a player's password, and the new password is
  delivered by message.
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
  speed 5 moves on days 6, 12, …, 30 [M]. For speeds that do not divide 30, the k-th move
  happens on day ceil(k·30/S) [I].
- **Combat timing** [M][H]:
  - The original rule held combat on every 5th day in any sector with hostiles.
  - v1.15 made combat depend on movement.
  - From v1.42, at most one combat happens per sector per phase, and only where some ship
    executed orders that phase. Implement the v1.42 rule.
- **Combat order**: the defending side moves first in simultaneous combat [H]. The setting
  `Simultaneous Games Show Strategic Combat` controls whether players see these battles.
- **Tie-break**: order moves within a phase deterministically, for example by speed, then
  empire id, then ship id [I].

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
- Remove an empire before the start, or switch it between AI and human control after the
  start.
- Play any empire's turn.

**Other behavior**:

- Players reconnect by player name, and the host then resends the current turn.
- There is a broadcast chat.
- Resuming means loading the game as host.
- The stock ports are UDP 6716 for control and TCP 6720 for files.

### 9.5 Mapping to OpenSE4

- A `.plr` corresponds to the ordered `sim::Command` list of one empire for one turn.
  Design, strategy and queue edits must also be commands.
- A `.gam` corresponds to a serialized `GameState` plus the per-empire logs and
  statistics.
- The host is authoritative, clients validate only against their own knowledge, and a
  missing empire falls back to the AI. This matches DESIGN.md's rule of sending state
  rather than a seed.

## 10. `Settings.txt` keys in scope [D]

- **Trade and tariffs**: `Maximum Trade Percentage`, `Treaty Subjugated Resource
  Percentage`, `Treaty Protectorate Resource Percentage`.
- **Events and intelligence**: `Event Percent Chance Low/Medium/High`, `Intelligence
  Defense Modifier Percent`.
- **AI**: the MEE keys (§7.6); `Random Player Personality Groups` and `… Group N
  Percent`; `Minimum/Maximum Computer|Neutral Player Low|Medium|High Setting`.
- **Multiplayer**: `Simultaneous Games Show Strategic Combat`.
- **Logs**: `Create Log Text Files for Players`, `Create Log Text File for Game`, `Use Old
  Log Political Message Display` (UI only).
- **Race characteristics** relevant here: Intelligence (RP), Cunning (intel) and Political
  Savvy (trade). Each has min/max % and point-cost keys.

## Open questions to verify in the running game

1. **Tech cost curve**: which Technology Cost options exist? What does level L cost
   (L = 2, 3, 5, 10) in an area with `Level Cost` 5000 under each option: linear, a
   multiplier, or geometric?
2. **Research leftovers**: in Divide Evenly mode, does unused share go to other projects
   or is it lost? Can the same area be queued twice, and does a second entry work on the
   next level at the same time? Can a project finish two levels in one turn?
3. **Opening RP/IP pool**: its size, and how it depends on Starting Resources.
4. **RP/IP modifiers**: how population, mood, characteristics, culture and planet/system
   modifiers stack. What exactly does Cunning change?
5. **Intel success**: the base success chance, the "Any" bonus, the counter-intel
   strength formula, how much progress a block removes, and the detection (naming)
   chance. Does a counter-intelligence project ever finish?
6. **Intel details**: which level Research - Steal takes, who gets a rebel planet, whether
   Lose Supply drains the amount or everything, whether Disrupt Trade resets the
   percentage, and how long Lose Movement lasts. What do the cargo-damage amounts mean,
   do stolen blueprints join our design list, and what do faked messages say?
7. **Score weights**: log Score and all columns over several turns, then fit the weights.
8. **Treaty combat and trade**: do None and Non-Intercourse fight on contact? Do
   Subjugation and Protectorate stop combat, and do they allow trade?
9. **Trade base**: gross or net production? How do Political Savvy and culture apply? Do
   tariffs take RP?
10. **Events**: the scope of the roll (per empire, per planet or global) and how targets
    are chosen. How far does `Ship - Moved` send a ship? What does a destroyed star do
    to its system, and can events strike homeworlds?
11. **Victory**: who wins a peace victory, whether tech % counts only allowed areas, and
    how ties resolve. When exactly is an empire eliminated (ships only, empty colonies)?
12. **AI**:
    - the anger range and the mood labels;
    - how anger turns into an acceptance probability;
    - the AI-state transitions;
    - the MEE baseline;
    - what Difficulty and Bonus do;
    - the value of trade items;
    - what the `Spaces Per One` densities of `AI_DesignCreation` count. We read N as one
      component per N kT of hull, rounded up, so 10000 means "one";
    - how the three value thresholds of `AI_Planet_Types` compare. We read them as ratios
      against the resource with the highest threshold;
    - whether the computer builds scouts, which no table names. We keep two while there is
      unexplored space;
    - how much the computer commits to its queues at once, and when it sends gifts,
      tributes and demands on its own;
    - whether race folders may override the global tables (`Construction_*`,
      `Planet_Types`, `Speech`, `Strategies`). We always read those from `Ai/`.
13. **Contact loss**: does losing contact reset the treaty to None? Is trade % kept after
    re-contact?
14. **Simultaneous timing**: the day schedule for speeds that do not divide 30, how
    same-phase ties resolve, and whether messages, research and intel resolve before or
    after movement during host processing.

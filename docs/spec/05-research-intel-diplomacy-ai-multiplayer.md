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
  the new items. Tech changes come from research, gifts, trades, theft, ruins, surrender and
  the Medium/High start.
- **Other sources**:
  - A Technology item in a gift, tribute or trade package gives the giver's level. It
    helps only when the receiver's level is lower. Setup options "Allow Gifts/Tributes" and
    "Allow Technology Gifts/Tributes/Trades" can forbid it [M].
  - Subjugation does **not** pass technology to the master in the original, despite the
    treaty description; the master only sees the subject's designs (§3.2) (confirmed:
    binary).
  - `Research - Steal` intel (§2.3).

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
  often is not borne out by the binary.
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
otherwise, the readings below are (confirmed: binary).

| Category | `Type` → effect on success |
|---|---|
| Ship sabotage | `Ship - Damage`: Amount damage points through the standard damage routine. `Ship - Lose Movement`: the ship loses Amount of its remaining movement points for the current turn (never below 0). Movement points are refilled when simultaneous movement starts, so in a simultaneous game, where intelligence runs after movement, the loss has no effect; in a turn-based game it hurts empires that move later in the turn (inferred). `Ship - Lose Supply`: removes min(Amount, the ship's supply). `Ship - Rebel`: the ship joins the source. `Ship - Experience Change`: adds the signed Amount. `Ship - Cargo Damage` (details open). `Ship - Orders Change`: the ship's orders are cleared and replaced by one order to move to a random system of the quadrant; mothballed ships are immune. |
| Ship espionage | `Ship - Concentrations`: a report. `Ship - Construction Info`: the largest queue concentrations. `Ship Designs - Steal`, `Unit Designs - Steal`: copy designs. |
| Planet sabotage | `Planet - Conditions Change`: shifts the conditions. `Planet - Value Change`: each of the three resource values changes by Amount; in finite-resource games by Amount × 1,000 (when that stays within ±500,000). `Planet - Population Change`: Amount with a random spread (size of the spread open). `Planet - Population Anger Change`: adds Amount anger, unless the owner's race has `Population Emotionless`. `Planet - Population Rebel`: see below. `Planet - Cargo Damage`. `Planet - Facility Damage`. |
| Planet espionage | `Planet - Info`: a full report, including cargo [H]. `Planet - Locations`. |
| Points | `Points - Change`: the target's minerals, organics and radioactives each change by Amount, never below 0. `Points - Steal`: for each of the three, min(Amount, the target's stock) moves to the source. Research and intelligence points are not touched. |
| Projects | `Research - Steal`: the chosen area must be one where the target's level is above ours and that is neither racial nor unique; we gain exactly one level. `Research - Delete Project`, `Intel - Delete Project`: one random entry of the target's queue is removed with its progress; the latter can remove a defense project. |
| Politics | `Politics - Disrupt Trade`: if trade between the target and the third empire is running, its counter restarts at 0 (§3.3). `Politics - Intercept Messages`: reports the latest political message between the target and the third empire from the last two turns. `Politics - Fake Messages`: a declaration of war is sent in the target's name to the third empire, and it takes effect: the two are at War. `Politics - Prevent Messages`: deletes the political messages of the last two turns between the two from both logs, so they are never answered; nothing is blocked afterwards. `Politics - Treaty Info`: the treaty between the two. |
| Info | `System - Info` (star charts), `Empire - Info`, `Tech Level - Info`. |
| Defense | `Intelligence Defense`, with level = Amount (1–3). See §2.4. |

- **`Planet - Population Rebel`** (confirmed: binary):
  - As an event (no source): the colony breaks away and becomes a new independent empire,
    if fewer than 20 empires exist.
  - As an intel project: a roll of 1–4 equal to 1 makes the colony a new independent
    empire; otherwise a second roll of 1–4 equal to 1 makes it join the source; otherwise
    nothing happens. That is 25 %, 18.75 % and 56.25 %.
- **`Research - Steal` with target "Any"** (confirmed: binary): the original's automatic
  pick keeps only areas where the thief is already *ahead* of the target, so the steal that
  follows always fails. A faithful engine reproduces this; an option may correct it.
- **Open readings** [I]: `Ship - Cargo Damage` and `Planet - Cargo Damage` (Amount kT of
  cargo, picked a unit or 1M at a time; an Amount of 1 or less destroys about half);
  `Planet - Population Anger Change` in tenths of a percent, like Happiness.txt;
  `Ship Designs - Steal`, `Unit Designs - Steal` (a copy joins our designs, preferring
  designs we have not seen; building it still needs the technology); `System - Info`
  (prefers a system the target knows and we do not).

### 2.4 Counter-intelligence

**Defense strength and blocking** (confirmed: binary). When an attack runs (§2.1 step 3)
against a living empire T:

1. A = the attack's accumulated progress (at least its `Cost`).
2. T's `Intelligence Defense` projects are taken **from the bottom of T's queue upwards**.
   Each adds trunc(Amount × progress × `Intelligence Defense Modifier Percent` / 100) to a
   running total D (capped at 1,000,000,000), and its progress drops to 0.
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
and deletes the first project it finds that is aimed at its owner and whose technology
requirement is at most the defense's level (Amount) [I: we read the requirement as the
highest level in the project's tech block]. Only one project is deleted; if none qualifies
the defense achieves nothing. It then leaves the queue unless Repeat is on.

**Timing** (confirmed: binary): the intelligence steps run empire by empire in empire order
during end-of-turn processing (§8). A defender with a lower empire number has already added
this turn's points to its defenses when a higher-numbered attacker strikes; a defender with
a higher number has not.

**`Change Bad Intelligence Chance - System`** (confirmed: binary): the original consults
it only while choosing an "Any" target, and reads it only from objects that belong to no
empire, keeping only a positive value V. With V = 100 it has no effect; otherwise a roll of
1–100 at most V rejects that candidate. The stock facilities carry negative values, so the
ability never changes anything in the stock game.

**AI reaction**: a detected attack adds `Intelligence Against Us` anger (§7.3).

## 3. Diplomacy

### 3.1 Contact

- **First contact** (confirmed: binary): whenever sight in a system changes, every pair of
  living empires that have not met and that **each detect the other** in that system make
  contact: their treaty goes from "no contact" to None and each logs "First Contact".
- **What contact enables** [M]: messages, treaties, trades and intel.
- **Contact is never lost** (confirmed: binary). The manual's rule about losing contact
  when no warp path links two empires is not implemented in the original. The only way back
  to "no contact" is the destruction of an empire (§6).
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
| Non-Intercourse | Cold. The parties agree to keep apart. Counts as a "bad" treaty for happiness. Combat behavior is open. |
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
- **Combat**: allies never fire on each other, not even when two of our allies fight. Both
  allies defend a shared location [M].
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
  (`Political Savvy` − 100) + the race's `Trade` trait values + the culture's `Trade`
  value. Minerals, organics and radioactives are traded from Trade Alliance up, RP from
  Trade and Research Alliance up (including Military Alliance), and IP only in a
  Partnership. The income joins the pools after our own production, so traded RP and IP are
  spent next turn.
- **Tariffs** (confirmed: binary): at the subordinate's income step, each of its five
  incomes (minerals, organics, radioactives, research, intelligence) is cut by
  round(income × pct / 100), never more than the income, where pct is the treaty's
  `Settings.txt` percentage. The master receives the minerals, organics and radioactives at
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
    combat stops [H]. The recipient also receives the sender's minerals, organics and
    radioactives, its designs, and technology in the areas where the sender was ahead
    (confirmed: binary; how many levels per area is open).
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
- **Log lifetime** (confirmed: binary): at the end of each empire's turn processing its log
  keeps only the entries of the last turn; the history window keeps the long record.
- **OpenSE4 choices** [I]:
  - Replies are checked against the message they answer: an acceptance only works for a
    proposal the recipient really sent. Accepting a counter-proposal (an Accept Demand
    reply) applies the counter.
  - Messages are kept 10 turns after sending, then dropped, together with any message
    whose sender or recipient is gone.

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
  target, taken from the whole galaxy: ship types → every ship; planet types (including
  `Planet - Destroyed`, `Planet - Plague` and `Plague Cured`) → every planet; political
  types → empires; `Star - Destroyed` → stars; `Warp Point - Closed` → warp points. Every
  other type (for example `Points - Change`, `Planet - Created`, the storm, nebula and
  black hole types, `Warp Point - Opened`) has no target list, so such a record never
  fires and that turn has no event. The game draws candidates at random, up to 1,000 times;
  a candidate that fails a check is removed from the draw:
  - it no longer exists;
  - for a High or Catastrophic planet or star event: it sits at an empire's home planet
    location (so homeworlds are safe from those);
  - **luck**: for an owned target, a roll of 1–100 must be below 100 + the owner's `Luck`
    trait values (a total of 0 or less skips the roll). A normal race keeps 99 %, a `Luck`
    −50 race 49 %. For star events every empire present in the system makes this roll (a
    total of exactly 100 skips it). Luck applies to every event, good or bad;
  - `Change Bad Event Chance - System` behaves exactly like its intel counterpart (§2.4):
    it never matters in the stock game.
  If no candidate passes, there is no event.
- **Applying** (confirmed: binary): with `Time Till Completion` 0 the effect (§2.3, with no
  source) applies at once and a random message from the record is sent. With N > 0 the
  event is scheduled and strikes in the event step exactly N turns later, if its target
  object still exists; otherwise it is dropped silently.
- **Event-only handlers** (confirmed: binary):
  - `Ship - Moved`: the ship is moved to a random system of the quadrant, at a random
    sector, and its orders are cleared. Amount is not used.
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
  there was no treaty and the data was unknown. So record every empire's statistics every
  turn with a per-viewer "known" flag. SE4 stores this per player in `<game>_stats.txt`,
  and the History window's data in `<game>_events.txt` [M]. Both files, and a text copy of
  the log, are written for human players at the start of their end-of-turn processing
  (confirmed: binary).

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
| X % of second place | Some living empire has a score of at least X % of every other living empire's score. | 150 % |
| X % of tech | Some living empire's capped tech level sum (§5, all areas) is at least X % of the sum of the maximum levels of the areas that are allowed in the game and pass its racial and unique checks. | 10 % |
| Peace for X years | A counter rises by 1 each turn and restarts at 0 whenever any two living empires hold a treaty worse than Non-Aggression, which includes None and "no contact". The game ends when it reaches X years. | 2 years |
| After X years | A qualifier: while the date is before 2400.0 + X years, nothing is checked (and the peace counter does not move). | 5 years |

- **Quirk** (confirmed: binary): when "X % of second place" is enabled, its test replaces
  the results of the Score and Years tests of the same turn: those two can then end the game
  only if the "% of second place" test also passes. The tech and peace tests still apply.
- **Last empire standing** (confirmed: binary): the original has no automatic victory for
  it. In a turn-based game, when every other empire has been destroyed, the player is told
  and may keep playing.
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

- **Who plays AI**: an empire is AI-controlled if it is "Computer Controlled" at setup or
  toggled in the Player Computer Control window [M]. A computer player is simply an empire
  with all 25 ministers switched on (confirmed: binary).
- **Missing orders** (confirmed: binary): in a game played on different machines (not
  Hotseat), a player whose orders are missing when the host processes the turn is played by
  the computer for that turn (§9.2). The host switches all of that empire's ministers on
  and restores the player's own minister settings after the turn. If that player ticked
  the minister option that forbids AI changes in a simultaneous game, the stand-in does
  its bookkeeping (state, scores) but changes nothing.
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
    binary).
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
- **Random race build** (confirmed: binary):
  - A random computer player's race uses the `AI_General` "Race Opt" set of the game's
    racial-point level: Race Opt 1 for Low (2000 points), 2 for Medium (3000) and 3 for
    High (5000). With None (0 points) no set is used.
  - Characteristics are applied in the listed order while the points spent stay within the
    budget. A characteristic that pushes the total over the budget goes back to 100.
  - Advanced traits are then added in order while each one's cost fits the remaining
    points.
  - Culture, planet type and atmosphere come from `AI_General`. A planet type and
    atmosphere pair that is not allowed is replaced by random ones until it is.
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

**Territory.** The AI claims its home system, every system with one of its colonies, and
every system one warp jump from a colony system. It does not claim another computer
player's home system, or the systems it has agreed to avoid (§7.4). A neutral empire claims
only its colony systems and ignores every system except its home.

**Strength.** Each turn the AI adds up a strength per system and per empire. It counts
every owned object, including those it cannot see:
- each object adds its rating + 1;
- a ship's rating is the sum, over its undamaged weapons, of each weapon's best damage at
  any range, plus its combat-bonus ability. If that is positive, add half of its shield
  abilities;
- a unit group's rating is its number of units;
- planets rate 0, so each planet adds 1.

A system's hostile strength is the total over the hostile empires we have met.

**Lists built each turn.** Each hostile object or planet enters a list only if the AI
notices it. A Low-difficulty AI notices each one with a 90 % chance per turn; Medium and
High always notice.
- **Enemy in territory**: visible hostile ships and unit groups in our territory, armed or
  not, except mine fields; and populated hostile colonies in our territory.
- **Enemy nearby**: visible hostile mine fields in our territory. That is all it
  contains.
- **Attack candidates**: planets of other empires in systems we have explored. They are
  ordered by fewest jumps from our home, then highest anger toward the owner, then highest
  value (strength at that spot plus the planet's defence).
- **Exploration frontier**: warp points in explored systems that lead to unexplored ones.
  A point is free when none of our ships is already headed for it.
- **Defend list**: the systems of the enemy-in-territory entries, at most `Maximum Systems
  to Defend at a Time`. They are ordered by fewest jumps from home, then most colony value
  of ours at stake, then the weakest threat.

**Tests used by the transitions**

- **Not Connected test**, all three must hold:
  1. the AI knows at most 10 uncolonized planets it could settle and has not yet targeted;
  2. no free frontier point is left;
  3. at most 60 % of all systems can be reached from home by warp.
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
  4. From the 5th turn in this state: add up our strength in every system within 4 jumps
     of each target. Unless it exceeds 3 × the targets' total hostile strength →
     Infrastructure.
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
  otherwise to Exploration.
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

**Territory.** The political step first recomputes the claimed territory (the same
territory §7.2 uses):
- every system holding one of our colonies;
- unless we are neutral, every system one warp jump from such a system;
- except the home system of any computer empire, and systems we agreed to leave by
  accepting a "remove ships", "remove colonies" or "leave planet" demand (§7.4).

Human players claim systems by hand; their home system starts claimed.

**Per-turn update** toward empire X, in this order:

1. **Combat.** For each combat this turn involving X, while X's treaty with us is below
   Non-Aggression, apply one `Combat …` value:
   - we are *Attacking* when our own ship's move started the combat, otherwise
     *Defending*;
   - *Won*: we had units there, some survived, and no other side has survivors;
   - *Lost*: we had units, none survived, and another side has survivors;
   - anything else is a *Stalemate*.

   Ground combat changes nothing.
2. **Stellar manipulation.** Each report this turn that X destroyed a planet, destroyed a
   star (making a nebula is reported the same way) or created a black hole adds 2 ×
   `Combat Defending Lost`. Empires present in that system get the report.
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
8. **Promise.** If we accepted X's "stop hostile actions" demand, −20, once. Accepting
   records the promise only half the time, and such records are forgotten every 10
   turns.
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
- Otherwise it answers X: only the newest unanswered political message from X, at most
  one per turn.
- If it sent X nothing, it takes the initiative. In simultaneous games it does not while a
  message from X is still waiting.

**Wants war with X**
- Never when already at war with X or X is a team mate. Always when X is a team enemy.
- Always when an accepted demand queued a war on X.
- Otherwise a 50 % roll must pass (no roll in Team Mode), then war when anger ≥ T.

**Wants to break** the treaty with X: only when the treaty is Non-Aggression or better. It
follows the same pattern as war, with the Break Treaty threshold and its own queue.

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
  computed in that case, T = 50.
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
| Planet | the planet's worth / 100000, truncated |
| Ship | its build cost × 100, a quarter of that in some cases (open) |
| Unit | its scrap value × 100 |
| Star chart | 20000 if the receiver lacks it |
| Treaty | its number in the treaty order × 100000 |
| Communication channel | 50000 |
| "Any …" item | 0 |
| System | open |

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
  the package reaches V.
- The AI never gives or asks for gifts or tributes on its own.

**Other demands and requests**

- Accepted when P ≥ the matching `Score Percent To Accept …` and its flag allows it.
  Team rules apply.
- A surrender demand is considered only when Allow Surrender is on and the empire is
  computer-controlled. A human's ministers never surrender.
- An accepted demand is carried out only with a 50 % chance:
  - remove ships or colonies, or leave a planet: the system is marked to avoid. Colonies
    are never abandoned;
  - break with, declare war on, support against or make peace with a third empire: queued
    for the matching decision above;
  - attack an empire in a system, or attack a planet: that system becomes an attack target;
  - stop espionage or sabotage: intelligence projects against the requester are cancelled;
  - stop attacks in a system: nothing happens.
- These queues are cleared every 10 turns.
- Acknowledgement messages get a chatter reply from the friend or enemy response pool.

**Demands the AI starts**, the first that applies. Each needs the `Will Send To
Friend/Enemy …` flag.

1. Stop attacks, espionage or sabotage, after the AI logged such acts by X.
2. 20 %: remove ships, or colonies, from a system where the AI has a colony.
3. At Trade Alliance or better with X, 20 %: break with a third empire the AI is hostile
   to. If the AI is Military Alliance or better with X and at war with that empire, it
   asks X to declare war instead.
4. At Military Alliance or better, 20 %: make peace with one of the AI's allies.
5. At Military Alliance or better, 20 %: attack an empire in a system.
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
    projects done) was logged last turn, or fewer than 4 projects are queued. Never with
    0 research points or a full queue (12).
  - *Mine sweeping*: every fifth turn, if the empire has ever run into a mine field, it
    queues the tech area required by the first component in the list with Mine Sweeping.
    This happens only if that area can be researched now and is below the required level.
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
    §7.1), plus income received from other empires, minus vehicle maintenance and
    facility upkeep. The stockpile is not counted.
  - *Revenue*: production × income factor + income from other empires.
  - With M = `Maximum Maintenance Percent of Revenue`, the empire is over the soft cap
    when, for any one of minerals, organics or radioactives, that resource's maintenance
    > its revenue × M / 100. It is over the hard cap when the same holds with M + 20.
- **`AI_Construction_Vehicles`** (Ship Construction minister, once per turn; confirmed:
  binary). The file holds one queue per state, with entries (`Type`, `Planet Per Item`,
  `Must Have At Least`).
  - *Budget*: one turn of net income, less `Percentage of Resources To Reserve For Unit
    Construction` %. That key comes from an optional `_AI_Construction_Units` file that
    the stock install does not ship, so the reserve is 0.
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
    2. The design built is the newest valid design of that type.
    3. A `Type` that is not a design type (and not `Colonizer`) is matched against the
       designs' own text. Which field is matched is open.
    4. **Colonizer** entries build the colony-ship type of the first colonization target
       (in the order below) that the empire can settle and that no queued colony ship of
       that type already covers. When every target is covered, the race's native surface
       type is built. Nothing is built while the empire knows no target it can settle.
    5. **Placement**:
       - Defense bases spread over the yard planets: first planets without one, then
         planets with one, and so on. Higher-value planets come first within a round.
       - Any other item goes to the queue with the smallest backlog in turns, then the
         highest rate. Ships need a space yard; units can use any colony's queue.
       - The item is placed only if that backlog is under 5 turns.
       - Units come in batches of as many as the queue finishes in one turn. Recon
         satellites are the exception.
    6. The budget is reduced by what the item takes from that queue this turn: its cost,
       capped at the queue's rate.
    7. After a placement the scan restarts from the top. After a failure (no queue under
       5 turns) it continues below that entry. It stops at the end of the list; there is
       no wrap-around.
  - Difficulty does not affect construction. Nothing outside the table is ever built, so
    there are no extra scouts.
- **Units file**: the game also looks for an `_AI_Construction_Units` file through the
  §7.2 rule (confirmed: binary). The stock install has none. When one exists, its rows
  (`Colony Type`, entries) fill the cargo of colonies whose queue is empty. The details
  are open.
- **`AI_Construction_Facilities`** (Facility Construction minister; confirmed: binary).
  Rows are (state set, `Construction Queue Type` = Homeworld or a colony type, ordered
  `Facility N Ability` + `Amount`).
  - *Timing*: it runs before Ship Construction and again after it, except on every fifth
    turn, when only the second pass runs (§7.1).
  - *Which colonies*: those whose queue is completely empty and that have a free facility
    slot. Each gets at most one facility per pass.
  - *Colony type*: a colony without one is given a type first (§7.5 colony types below).
    An unknown type uses the Homeworld rows.
  - The facility queued is for the first entry of the matching row that a researched
    facility provides and that no rule below blocks:
    - `Amount` is 0, or the colony already has `Amount` facilities with that ability;
    - Spaceport: the system has one already, or the race needs none;
    - Supply Generation: the system has one already;
    - Space Yard: the planet has one already;
    - research abilities: all research is done, or research points have reached
      `Maximum Research Point Generation`;
    - intelligence abilities: intelligence projects are off, or intelligence points have
      reached `Maximum Intelligence Point Generation`;
    - Change Atmosphere: the planet is unpopulated, or its atmosphere already suits its
      majority race;
    - any other system-wide ability the system already has;
    - with finite resources: resource generation or planet-value modifiers for a resource
      whose value on this planet is 0.
  - *Upgrades*: every fifth turn, obsolete facilities are upgraded to the newest
    researched version. The budget is half of one turn's net income, checked before each
    planet. Queued obsolete facility items also switch to the newest version.
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
    - Research Compound rows fail when all research is done or research points have
      reached the AI_Settings cap. Intelligence Compound rows fail when intelligence
      points have reached their cap or intelligence projects are off;
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
    - planets in a system where an empire at Non-Aggression or better with us is present
      and we are not;
    - planets one of our colony ships is already ordered to settle;
    - at Low difficulty, each target with a 10 % chance per turn.
  - *Order*:
    1. danger, lowest first: 5 per non-friendly empire present in the target system,
       plus 1 per non-friendly empire present in each adjacent system;
    2. warp jumps from home, fewest first;
    3. planets with ancient ruins first;
    4. breathable atmosphere first;
    5. larger planets first;
    6. higher planet value first.
  - *Ships*: idle colony ships are those with no orders, or whose target has become one
    of our colonies. Each target, in order, gets the nearest idle colony ship (by jumps)
    whose design can settle it, with the orders move there, then colonize.
- **Logistics ministers** (confirmed: binary unless marked).
  - *Transports*: an idle population transport more than half full delivers.
    - The destination is the least-populated own planet below its maximum, in a safe
      system, able to host the carried race, and not already another transport's
      destination.
    - Otherwise the transport loads at the nearest safe own planet with at least 1000M of
      a race that some under-populated planet can take.
  - *Retrofit*: none while over the soft cap, in the Attack, Incursion or Defend (Short
    Term) states, or while 3 ships are already being retrofitted.
    - Eligible ships are idle, have empty cargo, are in a system without enemies, and have
      a design older than 20 turns.
    - Such a ship goes to a yard and retrofits to the newest valid design with the same
      hull and design type, up to 3 ships.
  - *Scrap*: while over the soft cap, one non-colony ship per turn, the one of the oldest
    design, where it can be scrapped. Every 10 turns, useless facilities are scrapped
    (extraction on a 0-value planet with finite resources, atmosphere changers no longer
    needed).
  - *Repair and resupply*: damaged ships and ships low on supplies go to the nearest
    repair or supply point. The thresholds are open.
  - *Carriers and troops*: empty carriers, drone carriers and troop transports reload at
    the nearest colony holding fighters, drones or troops.
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
    11. **Name**: the first unused name in the race's design-name file whose position is
        past the number of designs the designer has made so far. After the list runs out
        it reuses names with "II", "III" and so on. With no file: "Design <n>".
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
    Formation` and `Fleets Default Strategy`.
  - *Disbanding*: fleets beyond n are disbanded, and so is a fleet whose leader is gone
    or unfit.
  - *Roles*: fleet i of n (counting from 1) is an attack fleet when i is odd and (i + 1) /
    2 < n × (100 − `Percentage of Fleets to use for defense`) / 100. All others are
    defence fleets.
  - *Size*: a fleet recruits up to trunc(vehicle count × `Fleets Percentage of Ships For
    Fleets` / 100 / n) members.
    - Recruits are idle ships not in a fleet, within 3 jumps. They join at once if at the
      fleet's spot, otherwise they are ordered to join.
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
- **Attack and defence** (Attack and Defense ministers; confirmed: binary).
  - *Defence*: defenders keep being sent to a threat until their total strength exceeds
    round(1.3 × the threat's) at Low difficulty, or round(1.5 × the threat's) at Medium
    and High.
  - *Attack*: attackers go to the first candidate in the target system whose assigned
    strength is at most round(its value × k). k = 1.5 when our score exceeds 1.5 × the
    owner's score, else 1.
    - Each candidate is skipped with a 25 % chance. If none qualifies, the first
      candidate that passes a 75 % roll is taken.
    - The order is Attack when the ship is already in that sector, otherwise a move there.
- **Exploration** (confirmed: binary). There is no scout design type.
  - Explorers are idle attack ships and loaded carriers or drone carriers that are not in
    a fleet and have fewer than 4 damaged components.
  - Each goes to the nearest free frontier warp point and jumps through if it can reach
    it this turn.
  - When explorers outnumber the free targets several times over (thresholds 3×, 5× and
    8×), points already taken are handed out again, so several ships may head for the
    same point.
- **Patrol** (confirmed: binary): idle warships not in fleets move to our colony with the
  fewest of our ships present, the smallest population breaking ties.
- **Mines, satellites and drones** (confirmed: binary).
  - Sweepers sweep. Mine and satellite layers work only while the empire has fewer placed
    units than the game's unit limit.
  - Satellites in planet cargo above `Percentage of total satellites to keep as planetary
    cargo` are launched.
  - Drones above `Percentage of total drones to keep as planetary cargo` are launched,
    half anti-ship and half anti-planet. Each half is capped at the number of targets ×
    `Number Of Anti-Ship Drones Per Target` (or `Anti-Planet`).
  - Anti-ship targets are ships inside our territory of empires at War with us.
    Anti-planet targets are planets of such empires among the attack candidates.
  - Idle drones within `Maximum Anti-Ship` (or `Anti-Planet`) `Drone Target System
    Distance` jumps are sent after them.
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
   are sent.
5. **Movement and space combat**: 30 movement phases, each followed by combat where it
   applies (§9.3).
6. **End-of-turn processing**, for each empire in order (the list below), each followed by
   the destruction check for that empire (§6).
7. **Design cleanup** every tenth turn (each new year).
8. **Victory check** (§6).
9. **Event step** (§4): hazard damage, due timed events, then the new event roll.
10. Per-turn flags are cleared and the game is saved and sent out.

**Turn-based game**: each player's movement, combat and diplomacy happen live during that
player's own turn. When a player ends the turn, that empire's end-of-turn processing runs at
once; then the next player's turn starts with the destruction check and its start-of-turn
step. After the last player the date advances, then the design cleanup (every tenth turn),
the victory check and the event step run, as in steps 7–9 above.

**End-of-turn processing of one empire** (in this order):

1. The empire's construction queues are refreshed, and its ministers (all of them for a
   computer player) take their end-of-turn actions.
2. For a human player: the statistics, history and log text files are written (§5).
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
16. Per-object end-of-turn upkeep of the empire's ships, bases and units.
17. **Ground combat** on planets where the empire has troops against an enemy.
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

**OpenSE4 mapping**: `processTurn` (`turn.cpp`) follows this order for every game, and
`empireEndOfTurn` is one empire's end-of-turn processing. Every step has a stable iteration
order and draws its randomness from `GameState::rng`. The turn-based style is not
implemented. The engine's choices where this section is silent are open question 21.

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
4. **RP/IP modifiers**: partly answered. Cunning changes intelligence output and nothing
   else; Intelligence changes research output (confirmed: binary). How the modifiers stack
   belongs to spec 02 §5.
5. **Intel success**: answered (confirmed: binary). There is no success roll and no "Any"
   bonus; counter-intelligence is a deterministic sum that drains the defenses it uses; the
   victim names the source with a 1-in-5 chance; a defense project does finish, and then
   deletes one hostile project (§2.1, §2.4).
6. **Intel details**: mostly answered (confirmed: binary, §2.3). Research - Steal gives
   one level (and never works with "Any"); a rebel planet becomes independent, joins the
   source or stays, by fixed odds; Lose Supply removes the amount; Disrupt Trade restarts
   the trade counter; Lose Movement takes movement points from the current turn; faked
   messages are a real declaration of war. Still open: what the cargo-damage amounts mean
   and whether stolen blueprints join our design list.
7. **Score weights**: answered (confirmed: binary). The formula is in §5.
8. **Treaty combat and trade**: partly answered. Subjugation and Protectorate allow no
   trade (confirmed: binary). Whether None, Non-Intercourse, Subjugation and Protectorate
   fight is still open (spec 04).
9. **Trade base**: answered (confirmed: binary). The base is the partner's production
   before tariffs and maintenance; Political Savvy, racial Trade traits and the culture's
   Trade value add up into one factor; tariffs also take research and intelligence
   points, which nobody receives (§3.3).
10. **Events**: answered (confirmed: binary). One roll per turn for the whole galaxy;
    targets are drawn from the whole galaxy with luck checks; `Ship - Moved` sends a ship
    to a random system anywhere; a destroyed star or planet has the same result as the
    stellar manipulation; High and Catastrophic planet and star events never pick a home
    planet location (§4). Still open: what exactly the stellar manipulations do (spec 01
    §9).
11. **Victory**: answered (confirmed: binary). The original names no winner: a met
    condition ends the game and the Scores ranking tells the result. The tech percentage
    compares all levels against the allowed areas the race can see. An empire is destroyed
    when it has no populated planet and no ship or base (§6).
12. **AI**: answered (confirmed: binary) except where noted.
    - Anger range and mood labels: 0–100, starting at 50, with nine labels (§7.3).
    - Acceptance probability: there is none. Acceptance is a fixed threshold test, and
      "Minimum Anger Chance" is a floor on the threshold (§7.4).
    - AI-state transitions: §7.2. Three of the ten states are never entered.
    - MEE baseline: §7.6.
    - Difficulty and Bonus: §7.1, §7.2 and §7.5.
    - Trade item values: §7.4. Still open: the value of a system, and which ships count at
      a quarter of their cost.
    - `Spaces Per One`: floor(hull tonnage / N) copies, at least one (§7.5).
    - `AI_Planet_Types` value thresholds: only thresholds above 100 count. They are
      absolute minimums, scaled by the settings' mid value under finite resources (§7.5).
    - Scouts: none. There is no scout design type, and idle attack ships explore (§7.5).
    - Queue commitment: vehicles get one turn of net income, and only queues with under 5
      turns of backlog. Facilities: one per empty colony queue per pass. The AI never sends
      gifts or tributes on its own; its own demands are listed in §7.4.
    - Race folders: they may override every table, including `Construction_*`,
      `Planet_Types`, `Speech` and `Strategies` (§7.2).
    - Still open:
      - the repair and resupply thresholds;
      - where Space Yard Ships go;
      - the Stellar Manipulation minister;
      - how each speech pool is tied to each reply;
      - which design text a non-type `Type` entry in `AI_Construction_Vehicles` matches.
    - Choices OpenSE4 makes where the rules above leave a detail open (inferred, to
      check):
      - who "started" a combat for the anger terms: OpenSE4 does not record it and
        counts a battle outside our territory as attacking, inside it as defending;
      - the "combat bonus" in a ship's strength rating: the Combat To Hit Offense Plus
        amount of its undamaged parts;
      - a planet's defence in an attack candidate's value: the units in its cargo;
      - the "colony value at stake" that orders the defend list: population plus 100 per
        facility;
      - the threat a defence commitment is measured against: the strength of the listed
        enemies in that system;
      - "our strength in every system within 4 jumps of each target" (Prepare for Attack
        and Attack step 4): each system counts once, however many targets it is near;
      - jumps for the staging system, the 4-jump test and Secure Holdings' neighbours are
        counted over the warp links we know;
      - a planet's worth in trades: 100,000 per facility plus 1,000 per million people;
      - "the number of designs the designer has made so far" for design names: all of the
        empire's designs;
      - whether a Race Opt trait that does not fit the remaining points ends the list (we
        stop there) or is skipped;
      - designs whose Design Type is not one of the 39 AI types (premade ships): typed by
        what they carry; an unarmed ship with nothing else to do explores;
      - whether the systems an accepted demand marks to avoid are forgotten with the other
        queues every 10 turns (we forget them);
      - "within 2 jumps of X's territory" (the +10 attack-location term) counts jumps over
        every warp link, known or not;
      - where mine and satellite layers lay their units (where they are);
      - "Allow Surrender" is not a game option in OpenSE4, so surrender demands are refused;
      - which empires are "present" in a system for colonization danger: their colonies
        in systems we explored and their vehicles we can see.
13. **Contact loss**: answered (confirmed: binary). Contact is never lost, except when an
    empire is destroyed (§3.1).
14. **Simultaneous timing**: partly answered (confirmed: binary). Messages are processed
    before movement; research and intelligence run after movement, in each empire's
    end-of-turn processing (§8). Still open: the day schedule for speeds that do not divide
    30 and how same-phase ties resolve.
15. **Tariff base** (§3.3): OpenSE4 cuts the tariff from what the subject's colonies
    deliver (spec 02 §5.5, the trade base), once, at its income step before the
    computer-player bonus. Does it also apply to remote mining and to flat `Generate
    Points` income? And is the race's trade bonus a trait of Type `Trade` (none exists in
    the stock data)? (inferred)
16. **Rebel colonies** (§2.3): OpenSE4 makes the new independent empire a computer player
    with the former owner's race (of the planet's largest population group), technology,
    strategies and name lists, the planet as its capital, in contact and at War with the
    former owner, and named after the planet. The population of that race becomes the new
    empire's own people, and the planet's system is its claimed system. Which of these does
    the original do? Does the
    20-empire limit count destroyed empires, and does it also stop the intelligence
    variant? (inferred)
17. **Intelligence details** (§2.1–§2.4): OpenSE4 reads the defense sum as
    trunc(Amount × progress × modifier / 100) with the percentage applied last; a finished
    defense deletes the first qualifying project in the other empire's queue order; the
    candidate checks of an "Any" pick are the ones a specified target must pass (for
    example, a homeworld is never drawn for `Planet - Population Rebel`); `Change Bad
    Intelligence/Event Chance - System` takes the largest positive value among the
    system's own abilities and the stellar abilities of its objects. `Ship - Orders Change`
    picks a random sector of the random system, and `Ship - Moved` may pick the ship's own
    system. With no message in the last two turns, `Politics - Intercept Messages` reports
    that nothing passed. In finite-resource games `Planet - Value Change` uses the plain
    Amount when Amount × 1,000 is beyond ±500,000, and every change stays within `Minimum/
    Maximum Planet Percent/Resource Value` (a value already outside is not pulled in).
    All (inferred).
18. **Event targets** (§4): which types count as political, and do `Research - Delete
    Project` and `Intel - Delete Project` have a target list? OpenSE4 gives the `Politics -`
    types the empire list and treats the delete-project types as having none, so they never
    fire. "Every planet" includes asteroid fields; an event on a planet without a colony
    changes only the planet itself (value) and otherwise achieves nothing and sends no
    message. "Every ship" includes bases. For star events an empire is present when it has
    a vehicle or a colony in the system, and the High/Catastrophic protection spares every
    star in a system that holds a home planet. The luck roll of an empire target uses that
    empire's Luck. All (inferred).
19. **Victory arithmetic** (§6): OpenSE4 compares "X % of every other score" and the
    tech share exactly in integers; with one living empire the second-place test passes.
    The peace counter moves (after the qualifier's date) whether or not the peace condition
    is on. A surrender clears the surrendered empire's relations and the intel projects
    aimed at it like a destruction. (inferred)
20. **Event amounts on the spec 02 scales** (§2.3, §4): OpenSE4 adds a `Planet -
    Conditions Change` Amount as hundredths of the 0–1.5 conditions scale, within 0 and
    1.5, and reads a `Planet - Population Anger Change` Amount in tenths, changing the
    whole-percent anger by trunc(Amount / 10) within 0 and 100 (80 on a capital). Is the
    conditions change additive, or a percentage of the current value like `Planet
    Conditions Change - System` (spec 02 §1.5)? (inferred)
21. **Turn order details** (§8): where the order above leaves a detail open, OpenSE4:
    - keeps the turn number the orders were given for until the end of the turn and hands
      the advanced date to the steps that use it (bookkeeping only);
    - lets colony ships waiting at their planet found their colonies when the 30 movement
      phases end, then updates sight and first contact; both are updated again after the
      event step;
    - delivers a computer player's (or a minister's) messages right after that empire's
      start-of-turn orders;
    - counts, in each empire's political step at the start of a turn, the battles, reports
      and messages of the turn processed before; what the AI remembers of a turn (battles,
      traced spies, mine fields) is recorded after the event step;
    - covers a player whose orders are missing by switching all of that empire's ministers
      on for the turn, so they plan both groups of §7.1;
    - writes every empire's statistics row at step 2 (the original writes the files for
      human players only);
    - has no queue refresh of its own at step 1, and does step 16's per-object upkeep in
      the supply step (spec 03 open question 36);
    - starts the end-of-turn processing of an empire founded during step 6 (a rebel colony)
      in the next turn;
    - lets a subject's tariff that reaches its master after the master's own storage cap
      stand until the master's next cap (spec 02 §13 item 13).

    Which of these does the original do? (inferred)

# Settings, name lists and default types

This chapter covers the data set's small files: **Settings.txt**, the game's numeric and
switch settings (patch table `settings`, one record); the **name lists** (`empire_names`,
`empire_types`, `emperor_names`, `emperor_titles`, `demeanors`, `system_names`,
`repair_priorities`), which are plain lists of names; **DefaultDesignTypes.txt** and
**DefaultColonyTypes.txt** (patch tables `design_types` and `colony_types`, records named
by `Name`); the **design-name lists** of the `Dsgnname/` folder, which have no patch
table; and what OpenSE4 does about **starting designs**.

## The fields

### Settings.txt

Settings.txt holds one record of `Key := Value` lines. OpenSE4 keeps every line of it and
looks a key up when a rule needs it, in any letter case and with runs of spaces counted
as one. A key that appears twice is a load warning (the later value wins).

- A key that is **missing** takes OpenSE4's built-in default for it.
- A value that should be a whole number or a switch but is not one (`25%`, `1.5`, `yes`)
  is **ignored without a message**: the built-in default is used. Switches read `True` or
  `False` in any case, or `1` and `0`.
- A key OpenSE4 does not know is kept and does nothing.

Below are the keys OpenSE4 reads, grouped by what they govern. `<X>` stands for one of
the three resources as the key spells it.

#### Galaxy, planets and game setup

| Key | What it does | Values |
|---|---|---|
| `Maximum Number Of Systems` | The most systems a quadrant can have. It also sets the Quadrant Size choices: with q = this ÷ 5, Small rolls q to 2q − 1 systems, Medium 2q to 4q − 1, Large 4q to 5q − 1. Extra starting planets look two jumps away instead of one when the quadrant has more than 60 % of it. | Systems; clamped to 1–255 |
| `Planet Value Low Percent`, `Planet Value High Percent` | The range each resource value of a natural planet is rolled in. A planet made by stellar manipulation gets the High value. | Percent, both ends included |
| `Planet Value Low Resources`, `Planet Value High Resources` | The same in finite-resource games, as a stock. | Units of the resource |
| `Asteroids Value Low Percent` and the other three `Asteroids Value` keys | The same for asteroid fields. | As above |
| `Plr Planet Value Low Percent`, `... Medium Percent`, `... High Percent` | A starting planet's values for each Home Planet Value choice (Bad, Average, Good), each with a random spread of −4 to +5. | Percent |
| `Plr Planet Value Low Resources`, `... Medium Resources`, `... High Resources` | The same in finite-resource games, exactly. | Units |
| `Minimum Planet Percent Value`, `Maximum Planet Percent Value` | Planet values are kept within these after every change during play. | Percent |
| `Minimum Planet Resource Value`, `Maximum Planet Resource Value` | The same in finite-resource games. | Units |
| `Remote Mining Decreases Asteroid Value` | `True`: each turn a remote miner works an object, that resource's value drops by 1 point (normal games only). | Switch |
| `Planet Value Percent Loss After Owner Death` | When a colony's population dies out and the colony is removed, each planet value drops by this many points, or in a finite game by this percentage of the stock. | Points or percent |
| `Event Percent Chance Low`, `... Medium`, `... High` | The galaxy-wide chance per turn of a random event for each Event Frequency setting. | Percent per turn |
| `Created Storm Maximum Obscuration Level`, `... Turbulence Damage`, `... Shield Disruption` | A storm made by stellar manipulation gets one of these three effects at random, with exactly this value; a kind whose value is 0 or less is never chosen. | Ability values |
| `Default Number Of Ships Per Player`, `Default Number Of Units Per Player` | The per-player limits Game Settings starts with. | Ships; units |
| `Number of Quick Start Styles`, `Quick Start Style 1` ... | The races Quick Start offers, in order, each by its folder or race name. Neutral races and names that match no race are skipped; with none left, every race that is not neutral is offered. | Count; race names |
| `Minimum Computer Player Low Setting`, `Maximum Computer Player Low Setting` and the same for `Medium`, `High` and `Neutral` | The range the number of random computer (or neutral) players is drawn from, for each Number of Computer Players level. Games set up in the network lobby use fixed ranges instead. | Players |
| `Random Player Personality Groups`, `Random Player Personality Group 1 Percent` ... | How random computer players' races are spread over personality groups: each group (the `Personality Group` of a race's `AI_Settings`) is filled toward its percentage. At most 10 groups are read. | Count; percent |

#### Resources, trade and maintenance

| Key | What it does | Values |
|---|---|---|
| `Minimum Empire <X> Generation` (`Minerals`, `Organics`, `Radioactives`) | Not a floor: when an empire's colonies deliver exactly 0 of that resource in a turn, it receives this amount instead. A game's opening stock counts it the same way. | Units per turn |
| `Minimum Empire Point Storage` | The base storage limit for each resource; `Resource Storage` facilities add to it. | Units |
| `Home System Percentage Value With No Spaceport` | The share of its home system's output an empire still receives there without a spaceport. | Percent; clamped to 0–100 |
| `Mood Riot Modifier`, `Mood Angry Modifier`, `Mood Unhappy Modifier`, `Mood Indifferent Modifier`, `Mood Happy Modifier`, `Mood Jubilant Modifier` | A colony's production for each mood. It scales resources, research and intelligence points. | Percent of normal output |
| `Number Of Population Modifiers` | How many population rows follow. | Rows; OpenSE4 reads up to 10,000 |
| `Pop Modifier <N> Population Amount` | Row N applies to colonies of at most this population. The first row, in order, that is large enough is used; above every row both percentages are 100. | Millions |
| `Pop Modifier <N> Production Modifier Percent` | The production percentage of that row (resources, research and intelligence). | Percent |
| `Pop Modifier <N> SY Rate Modifier Percent` | The construction-rate percentage of that row, for colony queues. | Percent |
| `Maximum Trade Percentage` | Under a Trade Alliance or better, the trade percentage grows by 1 a turn up to this. | Percent |
| `Treaty Subjugated Resource Percentage`, `Treaty Protectorate Resource Percentage` | The tariff a subordinate pays on each of its five incomes. The master receives the resources; the research and intelligence part is lost. | Percent |
| `Empire Starting Percent Maint Cost` | The base maintenance rate, before the race's Maintenance Aptitude, culture and traits; the result is at least 5. | Percent of a vehicle's cost per turn |
| `Maintenance Cost Amt Per Dead` | When maintenance cannot be paid, (unpaid ÷ this) + 1 vehicles are lost. | Units of resource |
| `Scrap Facility Percent Returned`, `Scrap Unit Percent Returned`, `Scrap Ship Percent Returned` | The share of the cost refunded on scrapping; a higher `Resource Reclamation` in the sector replaces it. | Percent |
| `UnMothball Ship Percent Cost` | What bringing a mothballed ship back costs. | Percent of the design's cost |
| `Upgrade Facility Cost Percent` | What upgrading a facility to a newer one costs, per facility. | Percent of the new facility's cost |

#### Construction and retrofit

| Key | What it does | Values |
|---|---|---|
| `Empire Base Planet Mineral Usage Rate`, `... Organic Usage Rate`, `... Radioactive Usage Rate` | The construction rate of a colony queue without a space yard. The population row's SY percentage applies to it; the race's construction modifiers do not. | Units per turn |
| `Construction Queue Emergency Build Rate Percent` | A queue's rate during emergency build. | Percent |
| `Construction Queue Slow Build Rate Percent` | A queue's rate in the slow turns after an emergency. | Percent |
| `Maximum Emergency Build Turns` | Emergency build ends after this many turns + 1; then the queue builds slowly for as many turns as the emergency counted. | Turns |
| `Retrofit Cost Percent For Comps` | Each component a retrofit adds costs this share of its mounted cost. | Percent |
| `Retrofit Cost Percent For Comp Removal` | Each component a retrofit takes out costs this share. | Percent |
| `Retrofit Max Percent Difference in Cost` | A retrofit is refused when the new design costs more than the old one by more than this. | Percent |
| `No Retrofit Adding Of Spaceyards`, `No Retrofit Adding Of Colony Module` | `True`: a retrofit may not add a space yard (or a colonize component) the ship did not have. | Switch |

#### Population and colonies

| Key | What it does | Values |
|---|---|---|
| `Empire Starting Percent Reproduction` | The base growth rate, before the race and its traits. | Percent per year |
| `Reproduction Check Frequency` | Colonies grow only on dates that are a multiple of this. The amount grown is not scaled up to make up for the skipped turns. | Turns (tenths of a year) |
| `Population Mass` | How much cargo space people take. | kT per million people |
| `Automatic Colonization Population` | Added to every new colony, besides the people the ship carried. | Millions |
| `Maximum Population For Abandon Planet Order` | A colony with more people cannot be abandoned. | Millions |
| `Characteristic <Name> Min Pct`, `... Max Pct` | The range of each racial characteristic in Empire Setup's race window. Nothing else reads them. | Percent |
| `Characteristic <Name> Pct Cost`, `... Threshold`, `... Threshhold Pct Cost Pos`, `... Threshhold Pct Cost Neg` | What a characteristic costs in racial points (the misspelt `Threshhold` keys must be written that way). See [races.md](races.md). | Points; percent |

`<Name>` is one of the fifteen characteristics as the race window names them: `Physical
Strength`, `Intelligence`, `Cunning`, `Environmental Resistance`, `Reproduction`,
`Happiness`, `Aggressiveness`, `Defensiveness`, `Political Savvy`, `Mining Aptitude`,
`Farming Aptitude`, `Refining Aptitude`, `Construction Aptitude`, `Repair Aptitude`,
`Maintenance Aptitude`.

#### Combat

| Key | What it does | Values |
|---|---|---|
| `Number Of Space Combat Turns` | The battle stops when its turn counter, which starts at 1, reaches this, so N − 1 combat turns are fought at most. | Combat turns; clamped to 1–250 |
| `Number Of Ground Combat Turns` | The most rounds of one ground combat; an undecided fight goes on next turn. | Rounds; at least 1 |
| `Combat Base To Hit Value` | The base of every to-hit chance. | Percentage points |
| `Combat To Hit Modifier Per Square Distance` | Taken off the chance for every square between the two pieces. | Points per square |
| `Seeker Combat Defense Modifier` | A seeker's whole defense when it is shot at. | Points |
| `Planet Combat Offense Modifier`, `Planet Combat Defense Modifier` | The first is added to every planet's offense; the second is a planet's whole defense. | Points; a negative defense makes planets easier to hit |
| `Ram Ship Source Modifier Percent`, `Ram Ship Target Modifier Percent` | The share of the rammer's and of the target's remaining hit points that a ram deals to the other side. | Percent |
| `Captured Ship Additional Reload Combat Turns` | A captured ship's weapons wait this much longer before they can fire for their new owner. | Combat turns |
| `Maximum Mines Per Player Per Sector`, `Maximum Satellites Per Player Per Sector` | How many mines or satellites one player may have in one sector, also in battle. | Units |
| `Fighters Can Be Hit By Mines`, `Drones Can Be Hit By Mines` | Whether mines strike fighter and drone groups that enter a minefield. | Switch |
| `Defending Units Per Population` | A colony raises one militia unit per this many millions of each population group when it is invaded. | Millions per unit; at least 1 |
| `Population Defender Attack Strength`, `Population Defender Hit Points` | A militia unit's attack and hit points. | Points |
| `Ground Combat Damage Modifier Percent` | Each side's ground damage is multiplied by this before it is applied. | Percent |
| `Damage Points To Kill One Population` | How much damage kills one million people (combat against planets, and events). | Damage points per million |
| `Create Combat Replay` | Whether battles record a replay the Log window can show. | Switch |
| `Simultaneous Games Show Strategic Combat` | Whether players of a simultaneous game see the strategic view of their battles. | Switch |
| `Combat Fighter Group Amount`, `Combat Mine Group Amount`, `Combat Satellite Group Amount` | Read, but no rule uses them, as in the original: group sizes come from the strategies ([combat.md](combat.md)). | Units |

#### Movement and supply

| Key | What it does | Values |
|---|---|---|
| `Supply Amount for Low Supply Warning` | Below this a ship shows the low-supply icon and a Sentry order ends; for fighter and drone groups a tenth of it. | Supply units |
| `Fighter Supply Usage Per Turn`, `Drone Supply Usage Per Turn` | What each fighter or drone in space uses every turn. A drone group dies when its supply runs out; a fighter group slows to 1 move. | Supply per unit per turn |
| `Bases Can Join Fleets` | Whether bases may join fleets. | Switch |
| `System Ship Movement Delay Milliseconds` | Presentation only: a pause after each animated step of a ship on the system map. Despite its name the value is read as seconds, as the original reads it. | Seconds |

#### Research, intelligence and diplomacy

No key of Settings.txt belongs to research alone: research points follow the population
rows and the mood modifiers above, like production. Intelligence has one key; trade and
tariffs are in the resources table above.

| Key | What it does | Values |
|---|---|---|
| `Intelligence Defense Modifier Percent` | Multiplies the strength of every intelligence defense project when it meets an attack. | Percent |

#### Computer players and score

The score formula has no keys. The Mega Evil Empire rule reads the scores:

| Key | What it does | Values |
|---|---|---|
| `AI Uses Mega Evil Empire` | Whether computer players look for a Mega Evil Empire at all. | Switch |
| `AI Mega Evil Empire Threshold Score Thousands` | An empire must score more than this to be one. | Thousands of score points |
| `AI Human Mega Evil Empire Score Percent`, `AI Computer Mega Evil Empire Score Percent` | A human (or computer) empire is the Mega Evil Empire when its score is at least this share of every other empire's. | Percent |

The computer players' own tables are in [ai-tables.md](ai-tables.md).

#### Interface, logs and music

| Key | What it does | Values |
|---|---|---|
| `Allow Export of Weapon And Component Data` | `True`: the Weapons Report gets an Export button that writes the weapons and components as text tables. | Switch |
| `Num Finale Victory Pictures`, `Num Finale Lose Pictures`, `Num Finale Human Dead Pictures` | How many pictures each ending window draws from. | Count; OpenSE4 reads up to 100 |
| `Finale Victory Picture <N>` and the same for `Lose` and `Human Dead` | The pictures, by file name under `Pictures/Game/Finale/`. | File names |
| `Create Log Text Files for Players` | `True`: each human player's log is also written as a text file every turn. | Switch |
| `Allow CD Music` | `False`: no music plays at all, and the Options window shows music as off. | Switch |
| `Num Intro Songs`, `Num Background Songs`, `Num Combat Songs` | How many tracks each playlist has. | Count; OpenSE4 reads up to 64 |
| `Intro Song <N> Filename` and the same for `Background` and `Combat` | Each track, by file name under `Music/`. | File names |
| `Intro Song <N>` and the same for the other lists | The older form, used when the file name key is missing: a CD track number, mapped to the install's numbered music files. | Track numbers |

Each time the game switches music (the intro screen, a loaded game, a battle), it picks one
random track of the list and loops it. A track whose file is missing does not play, and
OpenSE4's program log says so. `Create Log Text File for Game` and `Use Old Log Political
Message Display` are not read by OpenSE4 (the first was never used by the original
either).

`Population Required to Operate One Facility` is read by no rule, in OpenSE4 or the
original.

### The name lists

| Patch table | File | What it is used for |
|---|---|---|
| `empire_names` | EmpireNames.txt | The Empire Name list of Empire Setup. A rebel empire is named after its system, or after a random line of this list when that name is taken. |
| `empire_types` | EmpireTypes.txt | The Empire Type list of Empire Setup. |
| `emperor_names` | EmperorNames.txt | The Emperor Name list of Empire Setup; a rebel empire's leader gets a random line. |
| `emperor_titles` | EmperorTitles.txt | The Emperor Title list of Empire Setup. |
| `demeanors` | Demeanors.txt | The Demeanor list of Empire Setup's race page. Demeanour is flavour only; a new race starts with `Neutral` when the list has it, else with the first line. |
| `system_names` | SystemNames.txt | Names for the systems of a new galaxy: a random line to start from, then the first name not yet used. Blank lines are skipped. With too few names OpenSE4 names the rest "System 1", "System 2" and so on, and warns (the original leaves them unnamed). |
| `repair_priorities` | RepairPriorities.txt | The default repair order (below). |

Each is a plain text file with one name per line and no header. A file with a `*BEGIN*`
section is read too, every value of it being one name. The files are Latin-1. All of them
are optional: a missing file is a load warning and an empty list, and the Empire Setup
fields still take typed names.

### DefaultDesignTypes.txt and DefaultColonyTypes.txt

Both files are required and hold records with a single field, `Name`.

- **Design types.** Each new empire gets the list, in order, as its own design types;
  the ship designer offers them for a design's type. A design type is a label with no
  rules effect of its own. The computer players' ministers work with a fixed set of 39
  design types ([ai-tables.md](ai-tables.md) lists them): a design whose type is spelt
  exactly as one of them counts as that type, and any other design is typed by what it
  carries (a colonize component makes it a colony ship, a base hull a defense base, and so
  on). Formation positions name design types for display only. The original keeps at
  most 255 design types.
- **Colony types.** Each new empire gets the list as its own colony types; a player picks
  a new colony's type from it when the game asks. The facility minister knows nine fixed
  colony types; a colony with any other label is built up like a homeworld. Starting
  planets get the type `Homeworld`, whatever the file says.
- **The first colony type** is special: when a colony whose type matches it (in any case)
  dies out, its owner suffers the `Homeworld Lost` mood event instead of `Any Planet
  Lost`. Keep `Homeworld` as the first record.

### RepairPriorities.txt

A list of component `General Group` names (see [components.md](components.md)). Each
empire gets a copy as its repair order and can change it in the Repair Priorities window,
whose `Default Order` button restores the file's list. When a ship is repaired, its
destroyed components are repaired group by group in this order, each group in design
order, and then every component of a group not on the list, in design order. A name that
no component's `General Group` uses does nothing.

### Design-name lists (Dsgnname/)

The game folder's `Dsgnname/` holds lists of names for designs, one name per line
(Latin-1, blank lines skipped). A mod adds or replaces one as `data/Dsgnname/<file>.txt`;
there is no patch table for them, so a mod always supplies the whole file.

- A race names its list in the `Design Name File` field of its
  `<Race>_AI_General.txt`, by the file's name as it is in the folder, extension
  included (`Sable.txt`), in any case. Empire Setup offers every `.txt` file of the folder
  for a custom race.
- The ship designer suggests names from the empire's list.
- The computer's Design minister names each new design with the first line beyond its
  count of designs made so far that no design of any empire uses, then the list again
  with `II`, `III` and so on up to `XV`. Without a list, or once every name is used,
  OpenSE4 names it `Design <n>`.

### Starting designs

The data set has no table of starting designs. A new game gives no empire any design; a
computer player's Design minister makes its first designs on its first turn. Quick Start
gives the human player the designs of one run of the Design minister, made from the
empire's `AI_DesignCreation` templates ([ai-tables.md](ai-tables.md)), so changing those
templates changes what a Quick Start begins with. An empire file loaded at setup brings
its own designs.

## How the fields work together

- **Production chain.** A colony's output is its facilities' values, scaled by its
  population row's production percentage and its mood modifier (with the race and
  culture), and delivered only where a spaceport allows. A resource delivered at exactly 0
  is replaced by `Minimum Empire <X> Generation`; what is not spent is capped at
  `Minimum Empire Point Storage` plus storage facilities.
- **Construction rate.** A colony queue without a yard builds at the base usage rate
  times its population row's SY percentage; with a yard the yard's values and the race's
  construction modifiers apply too; emergency and slow modes multiply the result.
- **Maintenance.** Each turn a vehicle costs `Empire Starting Percent Maint Cost`
  (adjusted by the race, at least 5 %) of its design's cost; what cannot be paid destroys
  (unpaid ÷ `Maintenance Cost Amt Per Dead`) + 1 vehicles.
- **Retrofit.** The cost is the added components at `Retrofit Cost Percent For Comps` plus
  the removed ones at `... For Comp Removal`; it must stay within `Retrofit Max Percent
  Difference in Cost` of the old design's cost, and the two `No Retrofit Adding` switches
  forbid particular additions.
- **To-hit chance.** chance = `Combat Base To Hit Value` + offense + system bonus +
  weapon modifier − defense − `Combat To Hit Modifier Per Square Distance` × distance −
  interference, kept between 1 and 99. Planets add `Planet Combat Offense Modifier` to their offense and
  defend with `Planet Combat Defense Modifier` alone; seekers defend with `Seeker Combat
  Defense Modifier`.
- **Names and types.** The design types and colony types an empire starts with come from
  the two files, but the computer players read only the fixed names among them; the first
  colony type decides `Homeworld Lost`.

## Changing them with patches

Settings.txt is read as open key and value pairs: a patch may set any key that your data
set's Settings.txt (or a mod's replacement Settings.txt) already uses, with numbers in
place of numbers (`Pop Modifier 31 ...` is accepted when the file has other `Pop Modifier`
rows). A key no file uses is an error, so a typo does not pass.

**Longer battles, cheaper retrofits and a larger population table:**

```toml
# data/settings.toml
[[settings.change]]
set = { "Number Of Space Combat Turns" = 41, "Retrofit Cost Percent For Comps" = 85, "Retrofit Max Percent Difference in Cost" = 70 }

[[settings.change]]
set = { "Number Of Population Modifiers" = 3, "Pop Modifier 1 Population Amount" = 40, "Pop Modifier 1 Production Modifier Percent" = 90, "Pop Modifier 1 SY Rate Modifier Percent" = 95, "Pop Modifier 2 Population Amount" = 400, "Pop Modifier 2 Production Modifier Percent" = 110, "Pop Modifier 2 SY Rate Modifier Percent" = 110, "Pop Modifier 3 Population Amount" = 4000, "Pop Modifier 3 Production Modifier Percent" = 135, "Pop Modifier 3 SY Rate Modifier Percent" = 125 }
```

The second change replaces only the rows it names: rows above 3 that the file already has
stay in the file, but no rule reads them once `Number Of Population Modifiers` is 3.

**A background playlist of the mod's own** (the files go in `assets/Music/`, as MP3s or
as OGG Vorbis files with the same base names, such as `Lantern Drift.ogg`):

```toml
[[settings.change]]
set = { "Num Background Songs" = 2, "Background Song 1 Filename" = "Lantern Drift.mp3", "Background Song 2 Filename" = "Sable Tide.mp3" }
```

The count says how many numbered entries are read: entries above it stay in the file but
are no longer played. To add a track to the list your data set has, give it the next
number and raise the count by one.

**Names, design types and colony types:**

```toml
# data/names.toml
[system_names]
add = ["Lantern", "Sable Reach", "Mirefield", "Kestrel Gap"]
remove = ["<a system name of your data set>"]

[repair_priorities]
remove = ["<a group of your data set>"]
add = ["<the same group>"]          # moves it to the end of the order

[[design_types.add]]
name = "Picket Ship"

[[colony_types.add]]
name = "Listening Post"

[[colony_types.remove]]
name = "<a colony type your mod retires>"
cascade = true
```

A list operation removes first, then adds at the end. Without `cascade`, removing a colony
type that a computer player's facility queue (`Construction Queue Type`) names is an error
that says where; with it, those queues go too. Removing a design type works the same way
for the design templates whose `Design Type` names it.

**A race's own design names**: put the list in `data/Dsgnname/Sable.txt` and point the
race at it:

```toml
[[ai.general.change]]
files = "race:<a race folder of your data set>"
set = { "Design Name File" = "Sable.txt" }
```

## Things to watch

- **Settings changes the game.** Settings.txt is game data, so a mod that patches it, even
  only its music playlists, is a mod that changes the game: every player of a network or
  e-mail game needs it. Tracks under `assets/Music/` alone do not.
- **Silent fallbacks.** A Settings value that is not a whole number is not reported;
  OpenSE4 uses its default. `opense4-sdk check` catches misspelt keys in patches
  (`Settings.txt has no field 'Maximum Number Of Sytems' (no file of the table uses
  it; check the spelling)`), but not in a replacement Settings.txt, whose unknown keys
  are simply never read.
- **Keys your data set lacks.** A patch cannot add a key that no file of the table uses.
  To add one, the mod must supply a whole replacement Settings.txt.
- **Spelling that matters.** `Threshhold`, `Mineral`/`Organic`/`Radioactive` (singular)
  in the construction-rate keys, and `Minerals`/`Organics`/`Radioactives` (plural) in the
  generation keys are written as shown.
- **Ranges the rules clamp.** `Maximum Number Of Systems` 1–255, `Number Of Space Combat
  Turns` 1–250, `Home System Percentage Value With No Spaceport` 0–100; ground combat
  turns, militia hit points and the keys used as divisors (`Defending Units Per
  Population`, `Damage Points To Kill One Population`, `Maintenance Cost Amt Per Dead`,
  `Reproduction Check Frequency`) are raised to at least 1. Keep `Population Mass` above
  0: not every cargo rule guards against 0.
- **Order matters** in the design-type and colony-type files (the first colony type), and
  in RepairPriorities.txt (the order is the repair order).
- **Name list removals** of a name that is not there are errors: `SystemNames.txt has no
  'Nowhere' to remove`. Adding to a list file the data set lacks creates it.
- **Design-name lists** cannot be patched, and a race whose `Design Name File` names a
  file that does not exist simply has no list.

## More detail

- [Spec 01 §2.2, §2.3, §3.6 and §9](../../../spec/01-galaxy-and-setup.md): galaxy size,
  planet values, starting planets, created storms.
- [Spec 02 §1.2, §1.9, §5–§8 and §10](../../../spec/02-empires-and-economy.md): the
  economy keys, name lists, production, queues, maintenance, characteristics, colony types.
- [Spec 03 §2.7–§2.9, §7, §13 and §14](../../../spec/03-vehicles-and-abilities.md): repair
  order, design types, supply, repair and retrofit.
- [Spec 04 §7, §10, §13 and §18.3](../../../spec/04-combat.md): the combat keys.
- [Spec 05 §2.4, §3.3, §7.1, §7.6 and §10](../../../spec/05-research-intel-diplomacy-ai-multiplayer.md):
  intelligence defense, trade and tariffs, random computer players, the Mega Evil Empire.
- [Spec 06 §1.9 and §5](../../../spec/06-ui-and-assets.md): music, finale pictures and the
  other interface keys.
- [packages-and-data.md](../../packages-and-data.md): patches, name lists, game files.
- [view.md](../../view.md) "The rules view": computer players read the default lists as
  `rules_view.design_types`, `colony_types` and `repair_priorities`, and their own
  empire's lists in `my_empire`. Settings.txt and the name lists of Empire Setup are not
  part of the view.

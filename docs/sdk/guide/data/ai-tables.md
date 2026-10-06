# The computer players' tables

The classic computer player is steered by thirteen tables that live outside the data
folder: `<prefix>_AI_<Name>.txt` files in `Ai/` and in the race folders of `Pictures/`.
They decide how quickly a computer empire grows angry, which treaties it seeks, what it
researches, which designs it draws and builds, how it groups its fleets and what it says.
They are data patch tables `ai.anger`, `ai.politics`, `ai.settings`, `ai.general`,
`ai.fleets` and `ai.speech` (one record each, no name needed), `ai.research`,
`ai.planet_types`, `ai.construction_facilities`, `ai.construction_vehicles` and
`ai.construction_units` (rows, chosen with `match` or `index`), and
`ai.design_creation` and `ai.strategies` (records named by `Name`). Every operation on
them also takes `files`, which says which of the table's files it changes.

The same tables steer the ministers of a human empire that hands areas to the computer,
and the classic ministers that a script computer player calls through `ai.builtin`
(below).

## Where the tables live

| Table | The file's name ends in | Patch table | Records | Read by |
|---|---|---|---|---|
| Anger | `_AI_Anger.txt` | `ai.anger` | one | the political step, which changes anger once a turn |
| Politics | `_AI_Politics.txt` | `ai.politics` | one | the Politics minister |
| Settings | `_AI_Settings.txt` | `ai.settings` | one | several ministers; the random race choice |
| General | `_AI_General.txt` | `ai.general` | one | the race list (see [races.md](races.md)) |
| Fleets | `_AI_Fleets.txt` | `ai.fleets` | one | the Fleets minister |
| Research | `_AI_Research.txt` | `ai.research` | rows | the Research minister |
| Planet_Types | `_AI_Planet_Types.txt` | `ai.planet_types` | rows | the colony-type choice |
| Construction_Facilities | `_AI_Construction_Facilities.txt` | `ai.construction_facilities` | rows | the Facility Construction minister |
| Construction_Vehicles | `_AI_Construction_Vehicles.txt` | `ai.construction_vehicles` | rows | the Ship Construction minister |
| Construction_Units | `_AI_Construction_Units.txt` | `ai.construction_units` | rows | the Ship Construction minister, for units on planets |
| DesignCreation | `_AI_DesignCreation.txt` | `ai.design_creation` | named | the Design minister |
| Speech | `_AI_Speech.txt` | `ai.speech` | one | every message the computer writes |
| Strategies | `_AI_Strategies.txt` | `ai.strategies` | named | added to the empire's combat strategies |

The original installs ship no `Construction_Units` file; OpenSE4 reads one when a mod
supplies it.

### How an empire finds its file

One rule finds every table, each table on its own ([spec 05 §7.2](../../../spec/05-research-intel-diplomacy-ai-multiplayer.md)):

1. An empire with a **minister style** reads the style's folder, `Ai/<Style>/`. Any other
   empire reads its **race folder**, `Pictures/Races/<Race>/` (or
   `Pictures/RaceNeutral/<Race>/` for a neutral race). In that folder OpenSE4 takes the
   first file, by name, whose name ends in `_AI_<Name>.txt` in any case; normally that is
   `<Style>_AI_<Name>.txt` or `<Race>_AI_<Name>.txt`.
2. When the folder has no such file, the empire reads `Ai/Default_AI_<Name>.txt`.
3. There is no fallback from a style folder to the race folder.

So a race folder may hold only the tables in which the race differs, and every race
without its own file shares the Default one. A file that is there but holds no records
does not fall back to the Default file: OpenSE4's built-in table is used instead.

**Who has a style.** Empire Setup offers every folder under `Ai/` that holds tables as a
Minister Style, plus the race's own files; "Use Race Minister Style" picks the race's
files whatever the style says. Random computer players always use their race's files, and
a rebel empire keeps its former owner's choice. A script player can see the choice in
`my_empire.ministers.style` and `use_race_style` ([view.md](../../view.md)).

**A mod's own tables** go in `data/`, in the game folder's layout
([packages-and-data.md](../../packages-and-data.md) "Game files"):
`data/Ai/Default_AI_Research.txt` replaces the Default research table,
`data/Ai/Cautious/Cautious_AI_Anger.txt` makes a new style `Cautious`, and
`data/Pictures/Races/Sable/Sable_AI_General.txt` adds a race (its pictures go in
`assets/`). Tables under `assets/` are ignored.

### The AI states

The row tables (Research, Planet_Types, Construction_Facilities, Construction_Vehicles)
filter their rows by the empire's **AI state**, which the computer changes once a turn:

| State | Reached in play |
|---|---|
| `Exploration` | Yes, the starting state |
| `Infrastructure` | Yes: other empires met, nothing urgent |
| `Prepare for Attack` | Yes: a target worth attacking is chosen and forces gather in a staging system |
| `Attack` | Yes |
| `Secure Holdings After Attack` | Yes: holding a system just taken |
| `Incursion` | Never |
| `Prepare for Defense` | Never |
| `Defend (Short Term)` | Yes: an enemy is in our territory |
| `Defend (Long Term)` | Never |
| `Not Connected` | Yes: little left to settle or explore within reach |

A row's `AI State` text applies to every state whose name occurs anywhere in it, case
sensitive: `Exploration, Not Connected` covers both, and a row for `Prepare for Attack`
also covers `Attack`, because the word occurs inside it. Rows that name only states never
reached never apply.

## The fields

All values are whole numbers unless a row says otherwise; a switch is `True` or `False`.
A key the file leaves out takes OpenSE4's built-in default for it (0 in the row tables,
the design templates and AI_Fleets).

### AI_Anger (`ai.anger`)

Each empire holds an anger from 0 to 100 toward every other player, starting at 50. Once
a turn, in its political step (only while its Politics minister is on, which it always is
for a computer player), it changes the anger toward each living empire X it has met, by
the values below in this order. Every change is clamped to 0–100 at once, so the order
matters. Positive values make it angrier.

| Field | What it adds | Values |
|---|---|---|
| `Combat Attacking Won`, `Combat Attacking Lost`, `Combat Attacking Stalemate`, `Combat Defending Won`, `Combat Defending Lost`, `Combat Defending Stalemate` | Once per battle with X this turn, while X is below Non-Aggression with us. We are attacking when the battle is fought in our own turn (for simultaneous games see spec 05 §7.3). A report naming X for destroying a planet or a star, or making a nebula or a black hole, in a system where we have holdings adds 2 × `Combat Defending Lost`. | Anger points |
| `Intelligence Against Us` | Per successful intelligence project of X against us whose author we learn (one in five). | Anger points |
| `Receive <message>` | For the earliest message from X this turn, by its type (list below). | Anger points |
| `Receive Accept Tribute`, `Receive Refuse Tribute` | For an answer to a tribute we offered. | Anger points |
| `Regular Decrease` | Every turn. A negative value calms. | Anger points |
| `Mega Evil Empire` | Every turn X is the Mega Evil Empire ([settings.md](settings.md)). | Anger points |
| `Per Attack Location` | × N, where N counts X's colonies we could settle (+1 each), X's colonies in our territory (+3 per system) and nearby (+10 per system), scaled by the `AI_Settings` anger percentages. | Anger points per location |
| `Per No Treaty Ship` | × the ships, unit groups and colonies of X we see in our territory, when our treaty is None. | Anger points per object |
| `Per Enemy Ship` | The same at War or Non-Intercourse. | Anger points per object |
| `Per Ally Ship` | Read, but no rule uses it. | |
| `Minimum Anger` | The anger never ends the update below this. | 0 to 100 |

`<message>` is one of: `General Message`, `Propose Treaty`, `Accept Treaty`, `Refuse
Treaty`, `Offer Counter Treaty Proposal`, `Break Treaty`, `Declare War`, `Propose Trade`,
`Accept Trade`, `Refuse Trade`, `Offer Counter Trade Proposal`, `Give Gift`, `Offer
Tribute`, `Accept Gift`, `Refuse Gift`, `Surrender`, `Grant independence to colony`,
`Accept Demand/Request`, `Refuse Demand/Request`, or one of the sixteen demands: `Want a
gift`, `Want a tribute`, `Demand your surrender`, `Remove your ships from system`,
`Remove your colonies from system`, `Leave planet`, `Stop hostile actions against
empire`, `Break treaty with empire`, `Declare war on empire`, `Make peace with empire`,
`Support us against another empire`, `Attack empire in system`, `Attack planet`, `Stop
espionage activities`, `Stop sabotage activities`, `Stop attacks in system`.

Besides the table: Team Mode lowers the anger toward team mates by 20 a turn and raises it
toward the other side by 20, a promise about X (made by accepting a demand to stop hostile
acts against X) lowers it by 20 once, declaring war sets it to 100, and the anger toward an
eliminated empire is reset to 0. At Low difficulty each hostile object is left out
of the location and ship counts with a one-in-ten chance.

### AI_Politics (`ai.politics`)

Politics compares the anger toward X with thresholds built from this table. P is X's
score as a percentage of ours. A **friend** is at Non-Aggression or better, an **enemy**
at None or worse. Treaties are named `War`, `Non-Intercourse`, `None`, `Non-Aggression`,
`Subjugation`, `Protectorate`, `Trade Alliance`, `Trade & Research Alliance` (also written
`Trade and Research Alliance`), `Military Alliance`, `Partnership`, in that order from
worst to best.

**The four thresholds.** For each prefix `Accept Treaty`, `Propose Treaty`, `Break
Treaty` and `Declare War` the table has:

| Field (after the prefix) | What it does | Values |
|---|---|---|
| `Base Anger Level` | Where the threshold T starts. | Anger, 0 to 100 |
| `Anger Modifier Per Other Wars` | Added for each empire we are at war with whose score is above a quarter of ours. | Anger points |
| `First 50 Turns Modifier` | Added during the game's first 50 turns (Accept and Propose only). | Anger points |
| `Anger Modifier For Percent Stronger Player`, `... Amount` | When P is at least the percentage, the amount is added. | Percent; anger points |
| `Anger Modifier For Percent Weaker Player`, `... Amount` | When P is below the percentage, the amount is added (both may apply). Break Treaty uses its own percentages with the Declare War amounts, a quirk of the original. | Percent; anger points |

The computer **accepts** a treaty when T > anger, **proposes** one when anger ≤ T, and
**breaks** a treaty or **declares war** when anger ≥ T (each turn it rolls a coin before
these two tests). A Mega Evil Empire and Team Mode change T further (spec 05 §7.4).

| Field | What it does | Values |
|---|---|---|
| `Accept Treaty Anger Modifier Per Higher Treaty Level` | Added to the Accept threshold for each step the offered treaty is above Trade Alliance. | Anger points |
| `Accept Treaty Minimum Anger Chance` | A floor on the Accept threshold (not a probability). | Anger |
| `Accept Treaty Minimum Time From Last Treaty` | For Trade and Research Alliance, Military Alliance and Partnership the Accept threshold is set to −1 until the current treaty is this old (the later terms still add to it, so such offers are usually refused). | Turns |
| `Accept Treaty Score Percent To Accept Subjugation Treaty`, `... Protectorate Treaty` | Those treaties are accepted only when P is at least this. | Percent |
| `Highest Allowed Treaty` | Never proposes or accepts anything better. | A treaty name |
| `Turns Since Last War Before Friendly Treaty` | Refuses treaties until this long after the last war (for proposals only when a human is involved). | Turns |
| `Propose Treaty Percent Chance Per Turn` | The chance of a proposal in a turn; tripled while there is a Mega Evil Empire other than X. | Percent |
| `Propose Treaty Type Count`, `Propose Treaty Type <N>`, `Propose Treaty Type <N> Anger Level Below Computed` | The treaties it may propose, in file order. An entry qualifies when it is better than the current treaty, not above the highest allowed, and anger ≤ T − its level; the **last** qualifying entry is sent. | Count; treaty names; anger points |
| `Score Percent For Demanding Tone`, `Score Percent To Pleading Tone` | Its messages take a demanding tone when P is at most the first, a pleading one when P is at least the second (demands it starts itself are always neutral). | Percent |
| `Max Anger Level for Accept a Gift`, `Max Anger Level for Accept a Tribute` | Gifts and tributes offered are accepted only up to this anger. | Anger |
| `Accept Trade From <side> At Percentage Value or Greater of Received`, where `<side>` is `Friend` or `Enemy` | A trade is accepted when what it receives is worth at least this share of what it gives. | Percent |
| `Will Send To Friend <demand>`, `Will Send To Enemy <demand>` | Whether it may send that demand to a friend or an enemy. | Switch |
| `Score Percent To Accept <demand>` | It gives in to that demand only when P is at least this. | Percent |
| `Will Accept From Friend <demand>`, `Will Accept From Enemy <demand>` | Whether it may give in to that demand from a friend or an enemy at all. | Switch |
| `Gift Value Base to Friend`, `... to Enemy`, `Tribute Value Base to Friend`, `... to Enemy` | What a gift or tribute it agrees to give is worth, in the trade values of spec 05 §7.4 (1 per 1,000 units of a resource). | Trade value |
| `Gift Value to Friend Per Percentage Greater Score` and the other three | Added for a richer asker: P × this − 100, when P is above 100. | Trade value per percent |
| `Gift to Friend Max Anger`, `Gift to Enemy Max Anger`, `Tribute to Friend Max Anger`, `Tribute to Enemy Max Anger` | Requests for a gift or tribute are refused above this anger. | Anger |

`<demand>` is one of the sixteen demands listed under AI_Anger. An accepted demand is
carried out only half the time. Keys named `Score Percent to Send ...` have no effect in
the original and are not read by OpenSE4.

### AI_Settings (`ai.settings`)

| Field | What it does | Values |
|---|---|---|
| `Max Ship Size Tonnage From Start 1 Amount`, `... 1 Num Turns` (and 2, 3) | Early size limits for the Design minister: until that many turns have passed, no hull above the amount is used. Each pair counts only when its amount is above 0. | kT; turns |
| `Turns to Wait until next attack` | The pause after an attack ends before the next Prepare for Attack. | Turns |
| `Maximum Maintenance Percent of Revenue` | M. Over the soft cap (any resource's maintenance above M % of its revenue) the computer builds only colony ships and warp-point openers, retrofits nothing and scraps one ship a turn; over the hard cap (M + 20 %) it builds only warp-point openers. Colony ships do not count toward the maintenance. | Percent |
| `Maximum Research Point Generation`, `Maximum Intelligence Point Generation` | Once its production reaches this, it builds no more research (or intelligence) facilities and makes no more Research (Intelligence) Compounds. | Points per turn |
| `Maximum Systems to Defend at a Time` | How many systems the defend list holds. | Systems |
| `Get Angry Over Allied Colonizable Planets`, `Get Angry Over Enemy Colonizable Planets` | Whether X's colonies count as attack locations for anger when X is a friend (or an enemy). | Switch |
| `Percentage of <side> Planets to consider as Attack Locations for Anger`, where `<side>` is `Allied` or `Enemy` | Scales those attack locations. | Percent |
| `Personality Group` | The race's group for the random race choice (Settings.txt `Random Player Personality Group` keys). Read from the race folder's file. | Group number |
| `Ships don't move through minefields`, `Ships don't move through restricted systems`, `Clear orders on encounter enemy`, `Clear orders on encounter all` | Copied into the computer empire's own movement options every turn. | Switch |
| `Percentage of total satellites to keep as planetary cargo`, `Percentage of total drones to keep as planetary cargo` | Satellites and drones above this share of all it owns are launched from its planets. | Percent |
| `Number Of Anti-Ship Drones Per Target`, `Number Of Anti-Planet Drones Per Target` | How many drones it launches and sends per target. | Drones |
| `Maximum Anti-Ship Drone Target System Distance`, `Maximum Anti-Planet Drone Target System Distance` | Idle drones are sent at targets up to this distance less 2 jumps (the original counts jumps plus two). | Jumps + 2 |

### AI_General (`ai.general`)

A race folder's `<Race>_AI_General.txt` is what makes the folder a race: its names,
descriptions, `Demeanor`, `Culture`, `Happiness Type`, `Planet Type`, `Atmosphere`,
`Design Name File` and three build tiers (`Race Opt 1` to `3`, lists
`characteristics_1` to `_3` and `advanced_traits_1` to `_3`). A random computer player
builds its race from the tier of the game's racial-point level. These fields are covered in
[races.md](races.md). The copies of the table in style folders and the Default file are
read, but no rule of OpenSE4 uses them.

### AI_Fleets (`ai.fleets`)

| Field | What it does | Values |
|---|---|---|
| `Fleets Num Divisions` and, for each division N, `Fleets Div <N> Max Amount of Ships`, `Fleets Div <N> Max Amount of Planets`, `Fleets Div <N> Num Fleets` | How many fleets it wants: the first division whose ship limit is at least its number of vehicles gives `Num Fleets`. A division whose ship limit is 0 or less compares its planet limit with its planet count instead. | Vehicles; planets; fleets |
| `Fleets Dont Use For Num Turns` | No fleets until this many turns after the start. | Turns |
| `Fleets Percentage of Ships For Fleets` | Each of the n fleets recruits up to (vehicles × this ÷ 100 ÷ n) members, from ships within one jump. | Percent |
| `Percentage of Fleets to use for defense` | Fleet i of n is an attack fleet when i is odd and (i + 1) ÷ 2 < n × (100 − this) ÷ 100; the others defend and patrol. | Percent |
| `Fleets Default Formation` | The formation of the fleets it forms. Empty or unknown: the first formation. | A formation `Name` ([combat.md](combat.md)) |
| `Fleets Default Strategy` | Their strategy. Empty or unknown: the empire's first strategy. | A strategy `Name` |

It forms at most one fleet a turn, around its newest fit attack ship, carrier with its
units aboard, kamikaze ship or defence ship, and disbands fleets beyond the number wanted.

### AI_Research (`ai.research`)

| Field | What it does | Values |
|---|---|---|
| `AI State` | The states the row applies in. | State names |
| `Tech Area Name` | The tech area to research. | A tech area `Name` ([techs.md](techs.md)) |
| `Tech Area Level` | Research it up to this level. | Level; 9999 means its maximum |
| `Tech Area Min Percent` | The row's share: the minister stops adding projects once the shares of the queued areas reach 100. | Percent |

When the queue needs projects (a research event last turn, or fewer than 4 queued), the
minister adds, one at a time, the first row in file order whose state matches and whose
area can be researched now, is below the row's level and is not queued yet. Rows of 25
give four projects, rows of 50 two, a row of 100 one. The shares do not split the points:
projects are funded in queue order. When nothing qualifies and the queue is empty, it
picks a researchable area at random. Every fifth turn, after its ships have run into a
minefield, it also queues the technology of the first mine-sweeping component.

### AI_Planet_Types (`ai.planet_types`)

When the computer settles a planet (or a human's colony needs a type chosen automatically),
the colony's type is the `Planet Type` of the first row whose state matches and whose
tests all pass. Two rules come first: a mining colony when minerals run low, or a farming,
mining or refining colony when that resource runs short (spec 05 §7.5). If no row passes,
the colony is a Mining Colony.

| Field | What it does | Values |
|---|---|---|
| `AI State` | The states the row applies in. | State names |
| `Planet Type` | The colony type given. | One of the nine colony types (below) |
| `Max Per System` | Skip when the system already has this many of the type. | Colonies; 0 = no limit |
| `Percent of Colonies` | Skip when the type already makes up more than this share of its colonies. | Percent; 0 = no limit |
| `Maximum Total in Empire` | Skip when the empire already has this many. | Colonies; 0 = no limit |
| `Minimum Planet Size for Type` | Skip planets smaller than this size. | A planet size `Name` ([galaxy.md](galaxy.md)), or empty |
| `Mineral Value`, `Organics Value`, `Radioactives Value` | The planet's value of that resource must be at least this; only values above 100 count. In finite-resource games the value is compared with this percentage of the middle of the `Planet Value Low/High Resources` range. | Percent |

A Research Compound row is skipped when all research is done or research has reached the
`AI_Settings` limit; an Intelligence Compound row when intelligence has reached its limit
or intelligence projects are off.

### AI_Construction_Facilities (`ai.construction_facilities`)

| Field | What it does | Values |
|---|---|---|
| `AI State` | The states the queue applies in. | State names |
| `Construction Queue Type` | The colony type it builds for. | `Homeworld` or one of the other eight colony types |
| Entries (list `entries`): `Facility <N> Ability`, `Facility <N> Amount` | In order, the facility it wants: the researched facility that gives the ability best, until the colony has `Amount` facilities with it. | Ability names ([abilities.md](abilities.md)); count, 0 = never |

Where several queues match, the **last** one in the file is used. The minister visits each
colony whose queue is empty and that has a free facility slot, and queues one facility: the
first entry that a researched facility gives and that no rule blocks (a second spaceport
or supply generator in a system, a second space yard on a planet, research or
intelligence beyond the limits, one-per-system abilities, and resources a finite planet
does not have; spec 05 §7.5). A
colony whose label is none of the nine types is built like `Homeworld`. Every fifth turn
it also upgrades old facilities.

### AI_Construction_Vehicles (`ai.construction_vehicles`)

| Field | What it does | Values |
|---|---|---|
| `AI State` | The states the queue applies in. | State names |
| Entries (list `entries`): `Entry <N> Type` | What to build: an AI design type, a design template's `Name`, or `Colonizer` (a colony ship for the best colonization target it can settle). | Text |
| `Entry <N> Planet Per Item` | Wants one item for every (value ÷ 10) colonies: 10 is one per colony, 25 one per two and a half, 5 two per colony. | Tenths of a colony; 0 = not used |
| `Entry <N> Must Have At Least` | Wants at least this many, whatever the colonies. | Vehicles |

Where several queues match, the **last** one is used. Once a turn the minister spends one
turn of net income: it takes the first entry, from the top, whose type has fewer vehicles
(built and queued) than wanted, finds the newest design made from the template of that name
(else the newest design of that type), and places it in the queue with the least backlog,
if that is under 5 turns (defense bases and units are placed by rules of their own, spec
05 §7.5). After a placement it starts again from the top. Nothing outside
the table is ever built. The maintenance caps of `AI_Settings` limit what qualifies.

### AI_Construction_Units (`ai.construction_units`)

An optional table for units kept on planets. Its first record holds `Percentage of
Resources To Reserve For Unit Construction` (taken off the vehicle list's budget; a quirk
of the original shares this value between empires, so in turn-based games it is always
0). Each further record is a row:

| Field | What it does | Values |
|---|---|---|
| `Colony Type` | The colonies the row serves: the last row whose text contains the colony's type, in any case. | Text |
| Entries (list `entries`): `Entry <N> Type`, `Entry <N> Maximum in kT` | The first entry, an AI design type, whose units in the colony's cargo take less than the maximum gets one batch queued. | AI design type; kT (above 65,000 reads as 65,000) |

There is no `AI State` key: the rows apply in every state. Only colonies with an empty
queue and no free facility slot qualify, one item each.

### AI_DesignCreation (`ai.design_creation`)

One template per kind of design. The Design minister runs when a research event was
logged last turn, when the empire has no designs, and every tenth turn; it goes through
the templates in file order and makes at most one new design from each, when the
template has no current design yet or its last one can be improved (a better component,
weapon or engine, a larger hull, shields or a Quantum Reactor now available).

| Field | What it does | Values |
|---|---|---|
| `Name` | The template's name: entries of `AI_Construction_Vehicles` build designs made from it. The 39 AI design types are the usual names. | Text |
| `Design Type` | The type the designs get. Missing: the `Name`. | A design type |
| `Vehicle Type` | What it designs. | `Ship`, `Base`, `Fighter`, `Satellite`, `Mine`, `Troop`, `Drone` or `Weapon Platform` |
| `Default Strategy` | The designs' combat strategy, by name. Missing or unknown: the first strategy whose primary movement suits the type (Don't Get Hurt for colony, transport, carrier, layer, mine, space-yard, warp and stellar types, Drop Troops for troops and troop transports, Board Enemy Ships for boarding ships, Ram for drones, Optimal Weapons Range for the rest). | A strategy `Name` |
| `Size Minimum Tonnage`, `Size Maximum Tonnage` | The hull's size range; the largest researched hull within it is used. A template without a maximum never finds a hull. | kT |
| Must-haves (list `must_have`): `Must Have Ability <N>` | One part with each ability (`Weapon`: the majority weapon). Nothing is designed while one has no available part. | Ability names, or `Weapon` |
| `Minimum Speed`, `Desired Speed` | Engines are added to reach the minimum, then more while below the desired speed, within the hull's engine limit, while space remains. Hulls whose `Requirement Max Engines` is below the minimum are skipped. | Engines, counted as the engines' total `Standard Ship Movement` |
| `Majority Weapon Family Pick 1` to `5`, `Secondary Weapon Family Pick 1` to `5` | For a `Weapon` majority (or secondary): the first family with a researched weapon gives the weapon, the one with the highest technology. | `Weapon Family` numbers ([components.md](components.md)); 0 = none |
| `Shields Spaces Per One`, `Armor Spaces Per One` | One shield (armor) part for every N kT of hull. | kT; 0 or less = none |
| `Majority Comp Ability`, `Majority Comp Spaces Per One` | The main load: one part with the ability per N kT. | Ability name or `Weapon`; kT |
| `Secondary Comp Ability`, `Secondary Comp Spaces Per One` | A second load the same way. | As above |
| Misc (list `misc_abilities`): `Misc Ability <N> Name`, `Misc Ability <N> Spaces Per One` | Further parts the same way. | As above |

A "spaces per one" value N adds floor(hull tonnage ÷ N) parts, at least one, each only if
it fits: 10000 means "one" on any hull up to 10000 kT. The order is control parts, minimum
engines, the hull's own parts (bays, cargo or a colony module), must-haves, speed, shields,
armor, majority, secondary, misc; then the rest is filled with more majority, shields and
armor. Each part gets the last mount in
CompEnhancement.txt that the empire has researched, that accepts the part and fits the
hull. Every older design of the same type and player becomes obsolete. The design is
named from the race's design-name list ([settings.md](settings.md)).

### AI_Speech (`ai.speech`)

Pools of lines, each `Number of <Pool>` followed by `<Pool> 1`, `<Pool> 2` and so on (the
lines of `Mega Evil Declarations` are numbered in the singular, `Mega Evil Declaration
<N>`). A line is drawn at random from the pool each time. A message whose pool is empty
is not sent at all: a war declaration then declares nothing but still sets the anger to
100, and an accepted demand is still carried out.

| Pool | Used for |
|---|---|
| `Send <message>` | The text of every message of that type it sends: proposals, answers, gifts, demands, declarations |
| `Response Friend <message>`, `Response Enemy <message>` | The General message it answers an acknowledgement with (accepted or refused treaties and trades, gifts and tributes, war, a broken treaty, surrender, independence) and the refusal of a gift, tribute or surrender request |
| `Response Friend YES <demand>`, `Response Friend NO <demand>`, `Response Enemy YES <demand>`, `Response Enemy NO <demand>` | Its acceptance or refusal of the demands from `Remove your ships from system` to `Stop attacks in system` |
| `Mega Evil Declarations` | Its declaration of war on the Mega Evil Empire |

Friend or Enemy follows its treaty with the other empire. The lines may hold
`[%OurEmpireName]`, `[%OurEmperorName]`, `[%OurEmperorTitle]`, the same three with `Target`
(the empire addressed) and `Other` (a third empire), `[%TreatyName]`,
`[%ProposedTreatyName]`, `[%SystemName]` and `[%PlanetName]`.

### AI_Strategies (`ai.strategies`)

Combat strategies with the fields of DefaultStrategies.txt ([combat.md](combat.md)). Each
turn a computer empire adds to its strategy list every strategy of this table that it does
not have by name, so its design templates and fleets can name them.

### The fixed names

The ministers know these names and no others.

- **AI design types (39).** Warships: `Attack Ship`, `Defense Ship`, `Kamikaze Attack
  Ship` and `Boarding Ship`. Carriers: `Carrier` and `Drone Carrier`. Transports: `Troop
  Transport`, `Population Transport` and `Cargo Transport`. Colony ships: `Colony (Rock)`,
  `Colony (Ice)` and `Colony (Gas)`. Work ships: `Space Yard Ship`, `Mine Layer`, `Mine
  Sweeper` and `Satellite Layer`. Bases: `Attack Base`, `Defense Base` and `Base Space
  Yard`. Units: `Fighter`, `Troop` and `Mine`; `Satellite` and `Recon Satellite`; `Weapon
  Platform`; the drones `Anti-Ship Drone` and `Anti-Planet Drone`. Warp: `Open Warp Point`
  and `Close Warp Point`. Stellar manipulation: `Create Planet` and `Destroy Planet`,
  `Create Star` and `Destroy Star`, `Create Storm` and `Destroy Storm`, `Create Black Hole`
  and `Destroy Black Hole`, `Create Nebulae` and `Destroy Nebulae`. There is no scout type:
  the computer explores with attack ships.
- **Colony types (9).** `Homeworld` (which `Imperial Center` also means); the resource
  colonies `Mining Colony`, `Farming Colony` and `Refining Colony`; the point colonies
  `Research Compound` and `Intelligence Compound`; and `Resupply Base`, `Construction
  Yard` and `Military Installation`.

A design whose type label is spelt exactly as an AI design type has that type; any other
design is typed by what it carries ([settings.md](settings.md)).

## How the fields work together

- **Anger drives politics.** AI_Anger moves the anger each turn; AI_Politics turns it into
  decisions. A large `Regular Decrease` (negative) with a high `Declare War Base Anger
  Level` makes a peaceable empire; small combat values and a low `Minimum Anger` let it
  forgive. Anger also orders the attack candidates and picks the target of its spies.
- **The build chain.** AI_Research decides which parts exist; AI_DesignCreation turns them
  into designs, one per template; AI_Construction_Vehicles builds those designs by template
  name; AI_Fleets gathers the warships into fleets with a formation and a strategy from
  combat.md's tables or AI_Strategies. An entry looks for designs made from the template
  of its name first, and takes the newest design of that design type only when there is
  none, so give a new template an entry of its own. An entry naming neither a template nor
  a design type builds nothing.
- **The colony chain.** AI_Planet_Types gives each new colony a type, and
  AI_Construction_Facilities builds that type's facilities. Both work with the nine fixed
  colony types; the empire's own colony-type list does not limit them.
- **Budgets.** One turn of net income is spent on vehicles, and the maintenance cap of
  AI_Settings stops warship building before maintenance eats the revenue.
- **Not in the tables.** The difficulty level (Low notices only nine in ten hostile objects
  and defends with less margin) and the Computer Player Bonus (more income and faster
  queues) are game options, the same for every table.

## Script computer players and these tables

A computer player written in Python ([python-api.md](../../python-api.md)) plays its empire
itself, but it can call the classic AI: `ai.builtin.politics(view)`, `orders(view)` and
`economy(view)` give the commands the classic ministers would give now, and
`ai.builtin.answer()` the classic answer to a question such as a new colony's type. Those
ministers read the empire's own tables, found by the rules above (its minister style or
its race), so a mod that patches the tables also changes what `builtin` suggests. The
`AI_Strategies` join counts as the `design` minister.

For an empire a script plays, the classic AI's own steps do not run
([ai-protocol.md](../../ai-protocol.md) section 9): its AI state never changes from the
one it had (a new empire's is Exploration), and its political step does not run, so
AI_Anger does not move its anger. The state-filtered rows that `builtin` uses are
therefore those of that state. The tables are not part of the rules view; a script player
cannot read them directly. Every other computer player of the game still uses them in
full.

## Changing them with patches

`files` chooses the files: `"default"` (`Ai/Default_AI_*.txt`), `"style:<folder>"`,
`"race:<folder>"`, a list of these, or `"all"` (the default: every file of the table, every
race's and style's included). An operation that reaches no file is an error.

**Calmer computer players everywhere, and one race that holds grudges:**

```toml
# data/ai.toml
[[ai.anger.change]]
set = { "Regular Decrease" = -4, "Minimum Anger" = 5 }

[[ai.anger.change]]
files = "race:<a race folder of your data set>"
set = { "Regular Decrease" = -1, "Combat Defending Lost" = 15 }

[[ai.politics.change]]
files = "default"
set = { "Declare War Base Anger Level" = 92, "Propose Treaty Percent Chance Per Turn" = 14 }
```

Patches apply in order, so the race's own file gets -1 after the first change gave it -4.

**More research into propulsion while exploring**, for the Default table and a style:

```toml
[[ai.research.add]]
files = ["default", "style:<a minister style of your data set>"]
set = { "AI State" = "Exploration, Not Connected", "Tech Area Name" = "<a tech area of your data set>", "Tech Area Level" = 3, "Tech Area Min Percent" = 30 }

[[ai.research.change]]
match = { "Tech Area Name" = "<another tech area>" }
all = true
set = { "Tech Area Level" = 9999 }
```

An added row goes at the end of each file, so it is tried after the rows already there;
use `before` or `after` only with named tables. `all = true` changes every matching row
of a file (without it, several matches in one file are an error).

**A new kind of warship, designed and built:**

```toml
[[ai.design_creation.add]]
name = "Lantern Picket"
files = "default"
set = { "Design Type" = "Defense Ship", "Vehicle Type" = "Ship", "Default Strategy" = "<a strategy of your data set>", "Size Minimum Tonnage" = 100, "Size Maximum Tonnage" = 250, "Minimum Speed" = 3, "Desired Speed" = 5, "Majority Weapon Family Pick 1" = 7, "Majority Comp Ability" = "Weapon", "Majority Comp Spaces Per One" = 60, "Shields Spaces Per One" = 120, "Armor Spaces Per One" = 150 }
add = { must_have = [{ "Must Have Ability" = "Weapon" }], misc_abilities = [{ "Misc Ability Name" = "Supply Storage", "Misc Ability Spaces Per One" = 10000 }] }
# 7 stands for a `Weapon Family` number of your data set's weapons.

[[ai.construction_vehicles.change]]
files = "default"
match = { "AI State" = "<the AI State text of one of your data set's rows>" }
add = { entries = [{ "Entry Type" = "Lantern Picket", "Entry Planet Per Item" = 30, "Entry Must Have At Least" = 1 }] }
```

The design template and the queue must be in the files the empires read: a race or style
with its own DesignCreation or Construction_Vehicles file does not see the Default file's
records. A new entry goes at the end of the queue, after the entries that may use the
whole budget first.

**A new minister style** is a set of files, not a patch: `data/Ai/Cautious/Cautious_AI_Anger.txt`,
`Cautious_AI_Politics.txt` and whichever other tables differ, in the data format. The
tables it lacks come from the Default files, and later patches can reach it with
`files = "style:Cautious"`.

**Another line for a speech pool** (the keys must be ones your data set's speech files
use):

```toml
[[ai.speech.change]]
files = "default"
set = { "Number of Send Give Gift" = 3, "Send Give Gift 3" = "[%OurEmpireName] sends this to [%TargetEmpireName], with no conditions." }
```

## Things to watch

- **The file an empire reads.** A change to the Default file does not reach a race or
  style that has its own file of that table. Check with `files = "all"` (the default) or
  name the race and style files you mean.
- **Errors `opense4-sdk check` reports**: a misspelt key in a table with fixed fields
  (`ai.anger records have no field 'Regular Decrese' (check the spelling)`); a key no file
  of an open table uses (`ai.settings`, `ai.general`, `ai.speech`, `ai.strategies`); `no
  ai.fleets file (<prefix>_AI_Fleets.txt) to patch` when the selected files do not exist
  (always the case for `ai.construction_units` unless a mod adds one); `no ai.research file
  has a record match {...} to change`; several matches without `all = true`.
- **Values are read leniently.** A number that is not a whole number, a state name with
  the wrong letter case, an unknown treaty name or an ability no part has is not an error:
  the row never applies, the key keeps its default, or the entry builds nothing.
- **State text is case-sensitive and matched inside words.** A row for `Attack` alone
  cannot exclude `Prepare for Attack` rows from applying to Attack; where several
  construction queues match, the last one in the file wins.
- **Names refer across tables.** Template names tie AI_DesignCreation to
  AI_Construction_Vehicles; strategy and formation names tie AI_Fleets and the templates
  to combat.md's tables; `Tech Area Name` and `Minimum Planet Size for Type` name records
  of the data folder. Removing those records with `cascade = true` removes the research
  rows, empties the fields or removes the templates and facility queues that name them.
- **Spaces per one** of 0 adds nothing, and a missing `Size Maximum Tonnage` means the
  template never finds a hull.
- **Speech pools** may be longer in OpenSE4, but the original reads at most 20 lines a
  pool; keep to 20 if the data is also meant for the original.
- **Identity.** The AI tables are game data: a mod that changes them changes the game for
  network and e-mail play.

## More detail

- [Spec 05 §7](../../../spec/05-research-intel-diplomacy-ai-multiplayer.md): §7.1 control
  and difficulty, §7.2 file lookup and AI states, §7.3 anger, §7.4 politics, §7.5 the other
  tables and ministers, §7.6 the Mega Evil Empire, §7.7 the default lists.
- [Spec 02 §1.10 and §10](../../../spec/02-empires-and-economy.md): race presets, minister
  styles and colony types.
- [packages-and-data.md](../../packages-and-data.md): game files, the `files` key and the
  lists of the AI tables.
- [python-api.md](../../python-api.md) "The classic AI as a library" and
  [ai-protocol.md](../../ai-protocol.md): `ai.builtin` and what runs for a script-played
  empire.
- [view.md](../../view.md): the rules view has no record for these tables; a player sees
  its own minister style in `my_empire.ministers` and its strategies in
  `my_empire.strategies`.
- [combat.md](combat.md), [settings.md](settings.md) and [races.md](races.md): the tables
  these name.

# The script view

What a script reads: the **view** of the game from one empire, the **rules
view** of the data set, and the **queries** that answer what the view alone
does not. The engine side is `src/sdk/view.hpp`, `src/sdk/rules_view.hpp` and
`src/sdk/queries.hpp`; the commands a script gives back are in
[commands.md](commands.md), which also describes the types both pages share
(`location`, `resources`, `order`, `message`, the enumerations of commands).

Every table on these two pages is checked against what the engine builds: a
field the engine adds without documenting it, or one documented that it does
not make, fails the SDK's tests.

## Conventions

- **Values** are those of `script::Value`: null, true and false, whole
  numbers, text, lists and maps. There are no fractions anywhere: the game
  counts in whole numbers, and so do scripts (conditions are in hundredths,
  shares in percent).
- **Entities are lists**, never maps keyed by id. Each element carries its
  own `id`, the number the game uses for it; a script builds whatever indexes
  it needs.
- **References are ids**: `system id`, `object id`, `empire id`,
  `vehicle id`, `fleet id`, `design id` and `message id` always name
  something the same view lists. A `ref` (`object ref`, `vehicle ref`, ...)
  may name something the view does not list (a vehicle destroyed in a
  battle, a planet the empire cannot see, a message already answered).
- **Rules records are indices** (`component index`, `hull index`,
  `tech index`, ...): positions in the tables of the rules view.
- **Enumerations** are lower_snake_case names.
- **Every element of a kind has every key.** What the empire does not know is
  `null`: a foreign ship's orders, the name of an unexplored system.
- The **types** are written `int`, `bool`, `text`, `list of X`, `X, or null`,
  `map` (a map of any plain values, whose shape the field says), and the names
  of the types below and in commands.md.

## The view

`sdk::buildView(rules, state, empire, options)` builds it; one is built for
each engine call that runs a script, never kept between calls.

- **The empire's own knowledge** (the default) is made from the state as
  `game::redactForEmpire` leaves it: exactly what a human player of that
  empire is sent in a network game. Foreign ships appear only while seen,
  foreign colonies only in explored systems, other empires' treasuries,
  plans and logs not at all.
- **The whole state** (`ViewOptions::whole`, the game option "computer
  players see everything") shows everything: every system's contents, every
  colony, vehicle, fleet, design and message, every empire's treasury.
  `explored` and the empire's own lists stay its own.

Building a view never changes the game, and the same game always gives the
same view.

### `view`

| Field | Type | Meaning |
|---|---|---|
| `api` | int | The SDK's interface version: 1. |
| `empire` | empire id, or null | Whose view this is. |
| `whole` | bool | Built from the whole state. |
| `game` | game | The game: turn, date, options. |
| `my` | my_empire, or null | The empire's own affairs (null for a spectator). |
| `empires` | list of empire | Every empire, as known. |
| `systems` | list of system | Every system; the contents of explored ones. |
| `objects` | list of space_object | The stellar objects of the systems whose contents are shown. |
| `colonies` | list of colony | Our colonies, and the foreign ones we know. |
| `vehicles` | list of vehicle | Our vehicles, and the foreign ones we see. |
| `fleets` | list of fleet | Our fleets. |
| `designs` | list of view_design | Our designs, the foreign ones we know, and every design a listed vehicle or cargo uses. |
| `messages` | list of message | Diplomatic messages to or from us, not yet answered or expired. |
| `log` | list of log_entry | This turn's log. |
| `battles` | list of battle | The battles we fought in the last turn processed. |

### `game`

| Field | Type | Meaning |
|---|---|---|
| `turn` | int | Turns played; the date is 2400 plus a tenth of a year a turn. |
| `date` | text | The date as the game shows it, "2403.7". |
| `year` | int | The year. |
| `turn_style` | turn_style | Simultaneous or turn-based. |
| `player_turn` | empire id, or null | Turn-based games: whose turn it is. |
| `player_turn_started` | bool | Turn-based games: that player's turn has started. |
| `game_over` | bool | The game has ended. |
| `winner` | empire id, or null | The best score when it ended. |
| `peaceful_turns` | int | The peace victory's counter. |
| `options` | game_options | The options that matter for play. |
| `victory` | victory_conditions | The victory conditions. |

### `turn_style`

| Value | Meaning |
|---|---|
| `simultaneous` | Every player gives orders, then one turn processing carries them all out. |
| `turn_based` | Players take turns; orders are carried out as they are given. |

### `game_options`

| Field | Type | Meaning |
|---|---|---|
| `quadrant_type` | text | The quadrant's type. |
| `all_systems_seen` | bool | Every system was explored from the start. |
| `omnipresent` | bool | Every empire sees everywhere. |
| `finite_resources` | bool | Planet values are stocks that run out. |
| `event_frequency` | int | 0 none, 1 low, 2 medium, 3 high. |
| `max_event_severity` | int | 0 low to 3 catastrophic. |
| `tech_cost` | int | 0 low, 1 medium, 2 high. |
| `start_tech_level` | int | 0 low, 1 medium, 2 high. |
| `tech_areas_allowed` | list of tech index, or null | The tech areas the game allows; null for all. |
| `starting_resources` | resources | What each empire started with. |
| `racial_points` | int | Racial points for building a race. |
| `no_tactical_combat` | bool | Every battle is fought by the strategies. |
| `complete_tech_tree` | bool | Players may see the whole tech tree. |
| `allow_gifts` | bool | Gifts allowed. |
| `allow_tech_trades` | bool | Technology trades allowed. |
| `allow_intel` | bool | Intelligence projects allowed. |
| `no_ruins` | bool | No ancient ruins. |
| `only_breathable` | bool | Only planets a race can breathe on may be colonized. |
| `only_home_type` | bool | Only planets of a race's own surface. |
| `team_mode` | bool | Team mode. |
| `allow_surrender` | bool | Surrender is possible. |
| `score_display` | int | Whose scores are shown: 0 own, 1 own and treaty partners, 2 all. |
| `max_ships_per_player` | int | The ship limit. |
| `max_units_per_player` | int | The unit limit. |
| `ai_difficulty` | int | Random computer players' difficulty: 0 low, 1 medium, 2 high. |
| `ai_bonus` | int | The computer players' bonus. |
| `mod_options` | list of mod_option | The values of the options the game's rules mods declare (docs/sdk/rules.md, "Game options"); empty without them. |

### `mod_option`

| Field | Type | Meaning |
|---|---|---|
| `mod` | text | The mod's id. |
| `name` | text | The option's name in the mod. |
| `value` | int | Its value in this game; a switch is 0 or 1. |

### `victory_conditions`

Each condition is a switch and its value.

| Field | Type | Meaning |
|---|---|---|
| `score` | bool | Win at a score... |
| `score_value` | int | ...of this. |
| `years` | bool | The game ends after... |
| `years_value` | int | ...this many years. |
| `percent_of_second` | bool | Win with a score of... |
| `percent_of_second_value` | int | ...this percentage of the second best. |
| `tech_percent` | bool | Win with... |
| `tech_percent_value` | int | ...this percentage of the tech tree researched. |
| `peace` | bool | The game ends when everyone has been at peace for... |
| `peace_years` | int | ...this many years. |
| `delay` | bool | Nothing is checked until... |
| `delay_years` | int | ...this many years have passed. |

## The empire's own affairs

### `my_empire`

| Field | Type | Meaning |
|---|---|---|
| `id` | empire id | Our empire. |
| `stored` | resources | The treasury. |
| `score` | int, or null | Our score at the end of the last turn (null before the first). |
| `economy` | economy_report | Income and spending expected in the coming turn. |
| `maintenance_percent` | int | Our maintenance rate, in percent of each vehicle's cost a turn. |
| `research` | research_state | Research. |
| `intel` | intel_state | Intelligence. |
| `ministers` | ministers | The Ministers window. |
| `settings` | empire_settings | The empire's options. |
| `ship_count` | int | Ships and bases, against the ship limit. |
| `unit_count` | int | Units, against the unit limit. |
| `experience` | int | The empire's experience. |
| `race_age` | text | The race age that experience shows. |
| `home_system` | system id, or null | Where we started. |
| `home_sector` | sector, or null | The home planet's sector. |
| `claimed_systems` | list of system id | Systems we claim. |
| `systems_to_avoid` | list of system id | Systems our routes avoid. |
| `tagged_minefields` | list of location | Sectors tagged as minefields. |
| `waypoints` | list of waypoint_slot | The ten waypoint slots. |
| `notes` | list of system_note | Our notes on systems (only those written). |
| `strategies` | list of own_strategy | Our combat strategies. |
| `design_types` | list of text | Our design types. |
| `colony_types` | list of text | Our colony types. |
| `repair_priorities` | list of text | The order repairs are made in. |
| `colony_type_choices` | list of object id | New colonies waiting for us to choose their type (turn-based games). |
| `questions` | list of entry_question | Moves waiting for an `enter_sector` answer (turn-based games). |
| `ai_difficulty` | int | Our difficulty as a computer player (-1 until first set). |
| `mod_data` | map | What the game's rules mods keep on our own things, for the mods that let computer players see it (`players_see_mod_data`, docs/sdk/rules.md "Mod data"): `{mod id: {"empire": value or null, "colonies": {planet id as text: value}, "vehicles": {vehicle id as text: value}}}`; empty without them. |

### `economy_report`

What the coming turn is expected to bring, as the Empire Status window shows
it: worked out at the end of the last turn and when orders change.

| Field | Type | Meaning |
|---|---|---|
| `colonies` | resources | Delivered by our colonies. |
| `remote_mining` | resources | From remote mining. |
| `other_income` | resources | Generated points, the income floor and the computer bonus. |
| `trade` | resources | From trade. |
| `tariffs_in` | resources | Tariffs our subjects pay us. |
| `tariffs_out` | resources | Tariffs we pay our master. |
| `maintenance` | resources | Maintenance. |
| `construction` | resources | What the queues will spend. |
| `lost_to_storage` | resources | What the storage cap will throw away. |
| `undelivered` | resources | Production lost for want of a spaceport. |
| `storage_capacity` | resources | The storage cap. |
| `research` | int | Research points. |
| `intelligence` | int | Intelligence points. |
| `net` | resources | Colonies, remote mining, other income, trade and tariffs in, less tariffs out, maintenance and construction. |

### `research_state`

| Field | Type | Meaning |
|---|---|---|
| `points` | int | The points this turn's research will spend. |
| `income` | int | The research points a turn brings. |
| `evenly` | bool | Points are shared evenly, not in queue order. |
| `repeat` | bool | Finished areas go back to the end of the queue. |
| `queue` | list of research_entry | The research queue. |
| `levels` | list of int | Our level in every tech area, by tech index. |
| `researchable` | list of tech index | The areas we may queue now. |
| `unique_areas` | list of int | Unique areas granted by ruins. |
| `total_levels` | int | All our levels, each capped at its area's maximum. |
| `tech_percent` | int | That total as a percentage of everything we can research. |

### `research_entry`

| Field | Type | Meaning |
|---|---|---|
| `area` | tech index, or null | The tech area. |
| `progress` | int | Points toward its next level. |
| `level` | int | The level it is working toward. |
| `cost` | int, or null | That level's cost. |
| `eta` | int | Turns until the level is reached at the current rate; -1 never. |

### `intel_state`

| Field | Type | Meaning |
|---|---|---|
| `points` | int | The points this turn's intelligence will spend. |
| `income` | int | The intelligence points a turn brings. |
| `evenly` | bool | Shared evenly, not in queue order. |
| `repeat` | bool | Finished projects start again. |
| `defense` | int | Our counter-intelligence now. |
| `queue` | list of intel_entry | The intelligence queue. |

### `intel_entry`

| Field | Type | Meaning |
|---|---|---|
| `project` | intel project index | The project. |
| `target` | empire id, or null | The empire it is aimed at. |
| `target_planet` | object ref, or null | A planet it aims at. |
| `target_vehicle` | vehicle ref, or null | A vehicle it aims at. |
| `third_empire` | empire id, or null | The other empire of a political operation. |
| `target_tech` | tech index, or null | The area a theft aims at. |
| `progress` | int | Points so far. |
| `cost` | int, or null | The project's cost. |
| `problem` | text | Why it cannot run now; empty when it can. |

### `ministers`

| Field | Type | Meaning |
|---|---|---|
| `areas` | list of minister | The minister areas switched on. |
| `style` | text | The minister style; empty for the race's own. |
| `use_race_style` | bool | The race's own files even with a style. |
| `new_vehicles` | bool | New vehicles start under minister control. |
| `all` | bool | Complete minister control. |

### `empire_settings`

| Field | Type | Meaning |
|---|---|---|
| `clear_orders_on_encounter` | encounter_clear | Whom meeting after a warp clears a group's orders. |
| `avoid_tagged_minefields` | bool | Routes go around tagged minefields. |
| `avoid_restricted_systems` | bool | Routes never cross the systems to avoid. |
| `choose_colony_type` | bool | We choose new colonies' types (turn-based games). |
| `ai_minimal_changes` | bool | The computer player changes as little as it can. |
| `email` | text | Our e-mail address. |
| `interface` | interface_options | The Empire Options switches. |

### `waypoint_slot`

| Field | Type | Meaning |
|---|---|---|
| `slot` | int | 0 to 9. |
| `name` | text | The waypoint's name. |
| `set` | bool | The slot holds a waypoint. |
| `location` | location, or null | Where it is (null when not set). |

### `system_note`

| Field | Type | Meaning |
|---|---|---|
| `system` | system id | The system. |
| `note` | text | Our note. |

### `own_strategy`

| Field | Type | Meaning |
|---|---|---|
| `index` | int | Its position, the `strategy index` designs and fleets use. |
| `name` | text | Its name. |
| `settings` | list of strategy_setting | Its settings. |

### `entry_question`

A turn-based move that stopped before a sector with enemies; answer it with
`enter_sector`.

| Field | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id, or null | The vehicle that stopped... |
| `fleet` | fleet id, or null | ...or the fleet. |
| `location` | location | The sector with enemies. |
| `tagged` | list of vehicle id | A tagged group's vehicles. |

## Empires

### `empire`

Every empire of the game, ours included.

| Field | Type | Meaning |
|---|---|---|
| `id` | empire id | The empire. |
| `name` | text | Its name. |
| `empire_type` | text | Its kind of government. |
| `leader_title` | text | Its leader's title. |
| `leader_name` | text | Its leader's name. |
| `color` | int | Its colour, 0xRRGGBB. |
| `kind` | player_kind | Who plays it. |
| `neutral` | bool | A neutral empire. |
| `alive` | bool | Still in the game. |
| `is_me` | bool | This is our empire. |
| `race` | race | Its race. |
| `relation` | relation, or null | Our standing with it (null for ourselves). |
| `anger_toward_me` | int, or null | A computer player's anger toward us, 0 to 100 (null for human players and ourselves). |
| `treaties` | list of treaty_entry | Its treaties that we may see, with the empires it has met. |
| `score` | int, or null | Its score at the end of the last turn, when we may see it. |
| `stats` | turn_stats, or null | Its statistics of the last turn, when we may see its score. |
| `tech_levels` | list of int, or null | Its tech levels (ours, our partners', or everyone's in a whole view). |
| `stored` | resources, or null | Its treasury (ours, or everyone's in a whole view). |
| `economy` | economy_report, or null | Its expected income (ours, or everyone's in a whole view). |
| `claimed_systems` | list of system id | The systems it claims. |
| `home_system` | system id, or null | Where it started (ours, or everyone's in a whole view). |

### `player_kind`

| Value | Meaning |
|---|---|
| `human` | A human player. |
| `computer` | A computer player. |
| `neutral` | A neutral empire the computer plays. |

### `race`

| Field | Type | Meaning |
|---|---|---|
| `name` | text | The race's name. |
| `style` | text | Its pictures' style. |
| `biology` | text | Its description... |
| `society` | text | ... |
| `history` | text | ... |
| `characteristics` | characteristics | Its characteristics in percent (100 is average). |
| `traits` | list of trait index | Its racial traits. |
| `culture` | culture index | Its culture. |
| `happiness_model` | happiness model index | How its population's mood changes. |
| `native_surface` | text | The surface it lives on: "Rock", "Ice" or "Gas Giant". |
| `atmosphere` | text | The gas it breathes. |
| `demeanor` | text | Its demeanor. |

### `characteristics`

| Field | Type | Meaning |
|---|---|---|
| `physical_strength` | int | Ground combat. |
| `intelligence` | int | Research. |
| `cunning` | int | Intelligence. |
| `environmental_resistance` | int | Tolerance of poor conditions. |
| `reproduction` | int | Population growth. |
| `happiness` | int | Mood. |
| `aggressiveness` | int | Attack in combat. |
| `defensiveness` | int | Defense in combat. |
| `political_savvy` | int | Trade and diplomacy. |
| `mining_aptitude` | int | Minerals. |
| `farming_aptitude` | int | Organics. |
| `refining_aptitude` | int | Radioactives. |
| `construction_aptitude` | int | Building speed. |
| `repair_aptitude` | int | Repairs. |
| `maintenance_aptitude` | int | Maintenance cost. |

### `relation`

| Field | Type | Meaning |
|---|---|---|
| `contact` | bool | We have met. |
| `treaty` | treaty | Our treaty. |
| `dominant` | bool | Subjugation or protectorate: we are the master. |
| `trade_turns` | int | The trade counter (the trade percentage grows with it). |
| `treaty_turn` | int | When the treaty took effect. |
| `last_war_turn` | int | When we were last at war (-1 never). |
| `message_sent_this_turn` | bool | We sent it a message this turn. |
| `anger` | int | Our anger toward it as a computer player, 0 to 100. |

### `treaty_entry`

| Field | Type | Meaning |
|---|---|---|
| `empire` | empire id | The other empire. |
| `treaty` | treaty | The treaty between them. |

### `turn_stats`

The Scores window's statistics of one turn.

| Field | Type | Meaning |
|---|---|---|
| `turn` | int | The turn. |
| `score` | int | The score. |
| `production` | resources | Resources produced. |
| `research` | int | Research produced. |
| `intelligence` | int | Intelligence produced. |
| `tech_levels` | int | Tech levels. |
| `systems` | int | Systems with colonies. |
| `planets` | int | Colonies. |
| `population` | int | Millions of people. |
| `units` | int | Units. |
| `ships` | int | Ships. |
| `bases` | int | Bases. |

## The galaxy

### `system`

Every system of the quadrant. An unexplored system shows only where it is.

| Field | Type | Meaning |
|---|---|---|
| `id` | system id | The system. |
| `name` | text, or null | Its name (null: unexplored). |
| `position` | galaxy_position | Where it is on the quadrant map. |
| `explored` | bool | We have explored it. |
| `present` | bool | We have something there that sees, this turn. |
| `last_seen` | int | The turn we last had something there. |
| `type` | system type index, or null | Its system type. |
| `physical_type` | text, or null | "Normal", "Nebulae", "Black Hole"... |
| `abilities` | list of ability, or null | Abilities of the whole system. |
| `objects` | list of object id, or null | Its stellar objects, in the game's object order. |
| `avoid` | bool | Among our systems to avoid. |
| `claimed` | bool | We claim it. |
| `note` | text | Our note on it. |
| `claimed_by` | list of empire id | Every empire that claims it. |

### `galaxy_position`

| Field | Type | Meaning |
|---|---|---|
| `x` | int | Column of the quadrant map, from 0. |
| `y` | int | Row, from 0 at the top. |

### `space_object`

A star, planet, asteroid field, storm, warp point or comet. Fields that do not
apply to its kind are empty text or null.

| Field | Type | Meaning |
|---|---|---|
| `id` | object id | The object. |
| `kind` | object_kind | What it is. |
| `system` | system id | Its system. |
| `sector` | sector | Its sector there. |
| `name` | text | Its name (a warp point names its destination once we have explored it). |
| `sector_type` | sector type index | Its appearance. |
| `abilities` | list of ability | Its own abilities. |
| `size` | text | A planet's or asteroid field's size. |
| `surface` | text | A planet's surface. |
| `atmosphere` | text | A planet's atmosphere. |
| `conditions` | int, or null | A planet's conditions in hundredths, 0 to 150 (higher is better). |
| `conditions_band` | conditions_band, or null | The band those conditions fall in. |
| `value` | resources, or null | A planet's value of each resource: a percentage, or the stock left in a finite game. |
| `star_age` | text | A star's age. |
| `star_color` | text | A star's colour. |
| `star_luminosity` | text | A star's luminosity. |
| `destination` | object id, or null | A warp point's other end, once we have explored its system. |
| `destination_system` | system id, or null | That end's system. |
| `link_known` | bool, or null | A warp point: our routes may use it (we have been through, or know of it). |
| `colony` | empire id, or null | The owner of the colony on it, as we know it. |

### `object_kind`

| Value | Meaning |
|---|---|
| `star` | A star. |
| `planet` | A planet. |
| `asteroids` | An asteroid field. |
| `storm` | A storm. |
| `warp_point` | A warp point. |
| `destroyed_star` | A destroyed star (still a star for every rule). |
| `comet` | A comet. |

### `conditions_band`

| Value | Meaning |
|---|---|
| `optimal` | The best. |
| `good` | Good. |
| `mild` | Mild. |
| `unpleasant` | Unpleasant. |
| `harsh` | Harsh. |
| `deadly` | The worst. |

### `ability`

One ability entry as the data files write it.

| Field | Type | Meaning |
|---|---|---|
| `name` | text | The ability's name, as in the data ("Supply Storage"). |
| `value1` | int or text | Its first value: a number, or text where the data writes a name. |
| `value2` | int or text | Its second value. |
| `description` | text | The data's description. |

## Colonies

### `colony`

Our colonies show everything; a foreign colony shows only what a player sees
of it (who owns it, its population and mood), the rest null.

| Field | Type | Meaning |
|---|---|---|
| `planet` | object id | Its planet. |
| `owner` | empire id | Its owner. |
| `colony_type` | text | Its type. |
| `population` | list of population_group | Its people, by race. |
| `total_population` | int | Millions in all. |
| `max_population` | int, or null | The most it can hold. |
| `anger` | int | Its anger, 0 (calm) to 100. |
| `mood` | mood | The mood that anger shows. |
| `emotionless` | bool | Its owner's race has no moods. |
| `homeworld` | bool | A capital (every starting planet is one). |
| `founded_turn` | int | When it was founded. |
| `cloaked` | bool | It is cloaked. |
| `plague_level` | int | Its plague. |
| `militia` | int | Militia left to raise in a ground fight; -1 when nobody invades. |
| `invader` | empire id, or null | Whose troops are landed and fighting. |
| `landed_troops` | list of unit_stack | Those troops (only when the invader is us). |
| `facilities` | list of facility index, or null | Its facilities. |
| `facility_slots` | int, or null | How many facilities it can hold. |
| `destroyed_facilities` | list of destroyed_facility, or null | Its counts of destroyed facilities. |
| `cargo` | cargo, or null | What it stores. |
| `cargo_capacity` | int, or null | How much it can store. |
| `cargo_used` | int, or null | How much is used. |
| `queue` | queue, or null | Its construction queue. |
| `orders` | list of order, or null | Its order list. |
| `repeat_orders` | bool, or null | The list repeats. |
| `minister` | bool, or null | Under minister control. |
| `atmosphere_turns` | int, or null | Turns its majority has spent unable to breathe. |
| `cloak_levels` | list of int, or null | Its cloak level in each sight type. |
| `sensor_levels` | list of int, or null | Its sensor level in each sight type. |
| `breathable` | bool, or null | Its majority breathes the atmosphere. |
| `space_yard` | bool, or null | It has a space yard. |
| `output` | colony_output, or null | What it produces this turn. |

### `mood`

| Value | Meaning |
|---|---|
| `jubilant` | Anger below 15. |
| `happy` | 15 to 29. |
| `indifferent` | 30 to 44. |
| `unhappy` | 45 to 59. |
| `angry` | 60 to 89. |
| `rioting` | 90 and above. |

### `destroyed_facility`

| Field | Type | Meaning |
|---|---|---|
| `facility` | facility index | A kind of facility. |
| `count` | int | How many of it were destroyed. |

### `cargo`

| Field | Type | Meaning |
|---|---|---|
| `population` | list of population_group | People carried. |
| `units` | list of unit_stack | Units carried. |

### `colony_output`

| Field | Type | Meaning |
|---|---|---|
| `production` | resources | Resources produced. |
| `research` | int | Research. |
| `intelligence` | int | Intelligence. |
| `supply` | int | Supply it gives as a depot (0 none). |
| `connected` | bool | Its system has a spaceport, so its output reaches us. |
| `blockaded` | bool | Enemies in its sector stop its output. |
| `reproduction_percent` | int | Its growth, in percent a year. |
| `delivery_percent` | int | The share of its output that reaches the treasury. |
| `facilities_operating` | int | Facilities that produce. |
| `mood` | mood | Its mood band. |

### `queue`

A construction queue, with what each item costs and when it is done at the
queue's rate now. The estimate assumes the treasury pays: an item takes
ceil(what is left of each resource ÷ the rate) turns, at least one, once it is
at the top; -1 means never at this rate (or the queue is on hold).

| Field | Type | Meaning |
|---|---|---|
| `on_hold` | bool | Nothing is built. |
| `repeat` | bool | Finished items go on being built. |
| `emergency` | bool | Emergency building. |
| `emergency_turns` | int | Emergency turns used. |
| `slow_turns` | int | Slow turns left after an emergency. |
| `auto_waypoint` | int | The waypoint new vehicles go to; -1 none. |
| `rate` | resources | What it can spend a turn. |
| `items` | list of queue_entry | The items, top first. |

### `queue_entry`

| Field | Type | Meaning |
|---|---|---|
| `kind` | queue_item_kind | What it builds. |
| `design` | design id, or null | The design (vehicles). |
| `facility` | facility index | The facility, or an upgrade's target. |
| `count` | int | How many together. |
| `spent` | resources | Paid so far. |
| `cost` | resources | Its whole cost. |
| `remaining` | resources | What is left to pay. |
| `turns` | int | Turns it takes once at the top; -1 never. |
| `done_in` | int | Turns until it is done, counting the items ahead; -1 never. |

## Vehicles and fleets

### `vehicle`

A ship, base or unit group. Ours show everything; a foreign one shows what a
player sees of it.

| Field | Type | Meaning |
|---|---|---|
| `id` | vehicle id | The vehicle. |
| `owner` | empire id | Its owner. |
| `design` | design id, or null | Its design (a mixed unit group: its first). |
| `name` | text | Its name. |
| `type` | vehicle_type, or null | What kind of vehicle. |
| `location` | location | Where it is. |
| `count` | int | Units in a unit group; 1 for ships and bases. |
| `stacks` | list of unit_stack | Its designs and how many of each. |
| `status` | vehicle_status | Normal, mothballed or cloaked. |
| `built_turn` | int | When it was built. |
| `structure` | int, or null | Its full structure, per unit. |
| `damage` | int | The damage it has taken. |
| `component_damage` | list of int | Damage per component of its design. |
| `experience` | int | Its crew's experience. |
| `supply` | int, or null | Supply on board. |
| `supply_capacity` | int, or null | The most it holds. |
| `unlimited_supply` | bool, or null | It never runs out. |
| `movement` | int, or null | Movement left this turn. |
| `max_movement` | int, or null | Its full movement a turn. |
| `orders` | list of order, or null | Its order list. |
| `repeat_orders` | bool, or null | The list repeats. |
| `fleet` | fleet id, or null | Its fleet. |
| `cargo` | cargo, or null | What it carries. |
| `cargo_capacity` | int, or null | How much it can carry. |
| `cargo_used` | int, or null | How much is used. |
| `minister` | bool, or null | Under minister control. |
| `immobile_until` | int, or null | It cannot move before this turn. |
| `queue` | queue, or null | Its construction queue, when it has a space yard. |

### `vehicle_type`

| Value | Meaning |
|---|---|
| `ship` | A ship. |
| `base` | A base. |
| `fighter` | Fighters. |
| `satellite` | Satellites. |
| `mine` | Mines. |
| `troop` | Troops. |
| `drone` | A drone. |
| `weapon_platform` | Weapon platforms. |

### `vehicle_status`

| Value | Meaning |
|---|---|
| `normal` | In service. |
| `mothballed` | Mothballed. |
| `cloaked` | Cloaked. |

### `fleet`

| Field | Type | Meaning |
|---|---|---|
| `id` | fleet id | The fleet. |
| `owner` | empire id | Its owner. |
| `name` | text | Its name. |
| `members` | list of vehicle id | Its vehicles. |
| `leader` | vehicle id, or null | The member that leads now. |
| `chosen_leader` | vehicle id, or null | The leader chosen with `set_fleet_leader`, if any. |
| `location` | location | Where it is. |
| `formation` | formation index | Its formation. |
| `strategy` | strategy index | Its strategy (a position in its owner's strategies). |
| `experience` | int | Its experience. |
| `minister` | bool | Under minister control. |
| `speed` | int | The lowest full movement among the members with it. |
| `orders` | list of order | Its orders. |
| `repeat_orders` | bool | They repeat. |

## Designs

### `view_design`

Our designs show everything. A foreign design we have seen shows its parts
and figures, not its owner's records; one we have not seen (named by
something else in the view) shows only its hull.

| Field | Type | Meaning |
|---|---|---|
| `id` | design id | The design. |
| `owner` | empire id, or null | Its owner. |
| `known` | bool | We know its parts. |
| `name` | text, or null | Its name. |
| `design_type` | text, or null | Its type. |
| `hull` | hull index | Its hull. |
| `picture` | text | Its own picture's base name, empty for its hull's (how its ships look, so it shows for every design). |
| `entries` | list of design_entry, or null | Its components. |
| `strategy` | strategy index | Its vehicles' combat strategy. |
| `obsolete` | bool | Obsolete. |
| `created_turn` | int | When it was made. |
| `template_name` | text, or null | The computer designer's template it came from. |
| `prototype` | bool, or null | Nothing of it was built yet. |
| `built` | int, or null | Vehicles built. |
| `lost` | int, or null | Vehicles lost. |
| `scrapped` | int, or null | Vehicles scrapped. |
| `enemy_tonnage_destroyed` | int, or null | Tonnage of enemies its vehicles destroyed. |
| `seen_turn` | int, or null | A foreign design: the turn we last saw it. |
| `figures` | design_figures, or null | What the designer shows of it. |

### `design_figures`

What the designer shows of a design; also the result of the
`design_figures` query.

| Field | Type | Meaning |
|---|---|---|
| `vehicle_type` | vehicle_type | What its hull builds. |
| `tonnage_used` | int | Space used. |
| `tonnage_max` | int | The hull's space. |
| `cost` | resources | What one costs. |
| `maintenance` | resources, or null | One's maintenance a turn, for its owner (our designs). |
| `structure` | int | Structure. |
| `movement` | int | Movement a turn. |
| `supply` | int | Supply it holds. |
| `cargo` | int | Cargo space. |
| `shields` | int | Shields. |
| `phased_shields` | int | Phased shields. |
| `engines` | int | Engines. |
| `weapons` | int | Weapons. |
| `max_weapon_range` | int | The longest range its weapons do damage at. |
| `weapon_damage` | int | Its weapons' damage at range 1, added up. |
| `offense_bonus` | int | Its to-hit bonus when firing, in percent. |
| `defense_bonus` | int | Its to-hit bonus against being hit, in percent. |
| `space_yard` | bool | It has a space yard. |
| `colonize` | list of text | The surfaces it can colonize. |
| `valid` | bool | The design follows every design rule. |
| `problems` | list of text | Which rules it breaks, as the designer says it. |

## Messages, the log and battles

The `messages` list holds diplomatic messages in the form
[commands.md](commands.md#message) describes.

### `log_entry`

| Field | Type | Meaning |
|---|---|---|
| `turn` | int | When. |
| `category` | log_category | What about. |
| `title` | text | Its title. |
| `text` | text | Its text. |
| `location` | location, or null | Where it happened. |
| `goto` | log_goto | What the log's Goto opens. |
| `message` | message ref, or null | The diplomatic message it delivered. |

### `log_category`

| Value | Meaning |
|---|---|
| `construction` | Construction. |
| `research` | Research. |
| `intelligence` | Intelligence. |
| `events` | Events. |
| `politics` | Diplomacy. |
| `combat` | Battles. |
| `misc` | Anything else. |

### `log_goto`

| Value | Meaning |
|---|---|
| `none` | Nothing. |
| `location` | The entry's place on the map. |
| `research` | The Research window. |
| `intelligence` | The Intelligence window. |
| `empires` | The Empires window. |
| `construction_queues` | The Construction Queues window. |
| `empire_options` | The Empire Options window. |
| `designs` | The Designs window. |

### `battle`

| Field | Type | Meaning |
|---|---|---|
| `turn` | int | When it was fought. |
| `location` | location | Where. |
| `participants` | list of empire id | The sides. |
| `summary` | list of text | What happened, in words. |
| `pieces` | list of battle_piece | Everything that took part. |

### `battle_piece`

| Field | Type | Meaning |
|---|---|---|
| `kind` | battle_piece_kind | What it was. |
| `owner` | empire id, or null | Its side. |
| `vehicle` | vehicle ref, or null | The vehicle it was. |
| `planet` | object ref, or null | The planet it was. |
| `design` | design ref, or null | Its design. |
| `name` | text | Its name. |
| `count` | int | Units at the start (a group). |
| `damage` | int | Its damage in percent when the battle ended; -1 for a piece the log does not list. |
| `survivor` | empire id, or null | Its owner at the end if it survived; null if it was destroyed. |

### `battle_piece_kind`

| Value | Meaning |
|---|---|
| `vehicle` | A ship or base. |
| `planet` | A planet. |
| `unit_group` | A unit group. |
| `seeker` | A seeking weapon in flight. |
| `obstacle` | Something in the way. |

## The rules view

`sdk::buildRulesView(rules)` gives the data set as scripts need it. The rules
never change during a game, so it is built once per engine call. Each table
is a list whose positions are the indices the view and commands use; every
record also carries its `id`, which equals its position.

### `rules_view`

| Field | Type | Meaning |
|---|---|---|
| `api` | int | The SDK's interface version: 1. |
| `components` | list of component | Components. |
| `facilities` | list of facility | Facilities. |
| `hulls` | list of hull | Hulls (vehicle sizes). |
| `mounts` | list of mount | Weapon mounts. |
| `techs` | list of tech | Tech areas. |
| `racial_traits` | list of racial_trait | Racial traits. |
| `cultures` | list of culture | Cultures. |
| `happiness_models` | list of happiness_model | How moods change. |
| `races` | list of race_preset | The races an install offers. |
| `planet_sizes` | list of planet_size | Planet and asteroid sizes. |
| `system_types` | list of system_type | System types. |
| `sector_types` | list of sector_type | The appearances of stellar objects. |
| `abilities` | list of ability_kind | Every ability the engine knows, with how its values combine. |
| `formations` | list of formation | Fleet formations. |
| `strategies` | list of default_strategy | The default combat strategies. |
| `intel_projects` | list of intel_project | Intelligence projects. |
| `design_types` | list of text | The default design types. |
| `colony_types` | list of text | The default colony types. |
| `repair_priorities` | list of text | The default repair priorities. |

### `requirement`

| Field | Type | Meaning |
|---|---|---|
| `area` | tech index, or null | A tech area. |
| `level` | int | The level needed. |

### `component`

| Field | Type | Meaning |
|---|---|---|
| `id` | component index | Its index. |
| `name` | text | Its name. |
| `description` | text | The data's description. |
| `tonnage` | int | Space it takes. |
| `structure` | int | Its structure. |
| `cost` | resources | What it costs. |
| `vehicle_types` | list of vehicle_type | The vehicles it may go on. |
| `supply_used` | int | Supply it uses. |
| `max_per_vehicle` | int | The most one vehicle may carry; 0 no limit. |
| `group` | text | Its group in the designer. |
| `family` | int | Its family (upgrades replace within a family). |
| `roman_numeral` | int | Its level within the family. |
| `custom_group` | int | The data's custom group. |
| `requirements` | list of requirement | The technology it needs. |
| `abilities` | list of ability | Its abilities. |
| `weapon` | weapon, or null | What it does as a weapon. |

### `weapon`

| Field | Type | Meaning |
|---|---|---|
| `kind` | weapon_kind | How it fires. |
| `targets` | list of text | What it can hit. |
| `damage_at_range` | list of int | Damage by range, unmounted: the first entry at range 1 (the next square), the next at range 2, and so on (the data file's numbers in order). |
| `damage_type` | text | Its damage type. |
| `reload_rate` | int | Turns between shots. |
| `modifier` | int | Its to-hit modifier. |
| `family` | int | Its weapon family. |
| `seeker_speed` | int | A seeker's speed. |
| `seeker_resistance` | int | A seeker's damage resistance. |
| `max_range` | int | The longest range it does damage at, unmounted. |

### `weapon_kind`

| Value | Meaning |
|---|---|
| `none` | Not a weapon. |
| `direct_fire` | Fires directly. |
| `seeking` | Launches a seeker. |
| `warhead` | A warhead. |
| `point_defense` | Shoots down seekers and units. |

### `facility`

| Field | Type | Meaning |
|---|---|---|
| `id` | facility index | Its index. |
| `name` | text | Its name. |
| `description` | text | The data's description. |
| `group` | text | Its group. |
| `family` | int | Its family (upgrades replace within a family). |
| `roman_numeral` | int | Its level within the family. |
| `restriction` | text | A limit on how many may be built. |
| `cost` | resources | What it costs. |
| `requirements` | list of requirement | The technology it needs. |
| `abilities` | list of ability | Its abilities. |

### `hull`

| Field | Type | Meaning |
|---|---|---|
| `id` | hull index | Its index. |
| `name` | text | Its name. |
| `short_name` | text | Its short name. |
| `description` | text | The data's description. |
| `code` | text | Its code. |
| `type` | vehicle_type | What it builds. |
| `tonnage` | int | Its space. |
| `cost` | resources | What it costs. |
| `engines_per_move` | int | Engines per point of movement. |
| `requirements` | list of requirement | The technology it needs. |
| `abilities` | list of ability | Its abilities. |
| `must_have_bridge` | bool | A design needs a bridge. |
| `can_have_aux_control` | bool | A design may have at most one auxiliary control; when false their number is not checked. |
| `min_life_support` | int | Life support needed. |
| `min_crew_quarters` | int | Crew quarters needed. |
| `uses_engines` | bool | It takes engines. |
| `max_engines` | int | The most engines; 0 no limit. |
| `min_percent_fighter_bays` | int | The least share of the hull's space a design gives fighter bays, in percent; 0 none. |
| `min_percent_colony_modules` | int | The least share of the hull's space a design gives colony modules, in percent; 0 none. |
| `min_percent_cargo` | int | The least share of the hull's space a design gives cargo space, in percent; 0 none. |

### `mount`

| Field | Type | Meaning |
|---|---|---|
| `id` | mount index | Its index. |
| `name` | text | Its name. |
| `short_name` | text | Its short name. |
| `description` | text | The data's description. |
| `code` | text | Its code. |
| `cost_percent` | int | The component's cost, in percent. |
| `tonnage_percent` | int | Its space, in percent. |
| `structure_percent` | int | Its structure, in percent. |
| `damage_percent` | int | Its damage, in percent. |
| `supply_percent` | int | Its supply use, in percent. |
| `shield_percent` | int | Its shields, in percent. |
| `range_modifier` | int | Range added. |
| `to_hit_modifier` | int | To-hit added. |
| `minimum_hull_size` | int | The smallest hull it fits. |
| `maximum_hull_size` | int | The largest (0 no limit). |
| `families` | list of int | The component families it applies to (empty: all). |
| `weapon_type_requirement` | text | The weapon type it needs. |
| `vehicle_type` | text | The vehicles it is offered for. |
| `requirements` | list of requirement | The technology it needs. |

### `tech`

| Field | Type | Meaning |
|---|---|---|
| `id` | tech index | Its index. |
| `name` | text | Its name. |
| `group` | text | Its group. |
| `description` | text | The data's description. |
| `max_level` | int | Its highest level. |
| `level_cost` | int | The cost the level costs grow from. |
| `start_level` | int | The level empires start with. |
| `raise_level` | int | The data's raise level. |
| `racial_area` | int | Non-zero: only races with the matching trait. |
| `unique_area` | int | Non-zero: only by special means. |
| `can_be_removed` | bool | The game options may leave it out. |
| `requirements` | list of requirement | What it needs before it can be researched. |

### `racial_trait`

| Field | Type | Meaning |
|---|---|---|
| `id` | trait index | Its index. |
| `name` | text | Its name. |
| `description` | text | The data's description. |
| `general_type` | text | Its general type. |
| `cost` | int | Its cost in racial points. |
| `trait_type` | text | What it changes. |
| `values` | list of text | Its values, as written. |
| `required_traits` | list of text | Traits it needs. |
| `restricted_traits` | list of text | Traits it excludes. |

### `culture`

Each value is a percentage added to that part of the race's life.

| Field | Type | Meaning |
|---|---|---|
| `id` | culture index | Its index. |
| `name` | text | Its name. |
| `description` | text | The data's description. |
| `production` | int | Production. |
| `research` | int | Research. |
| `intelligence` | int | Intelligence. |
| `trade` | int | Trade. |
| `space_combat` | int | Space combat. |
| `ground_combat` | int | Ground combat. |
| `happiness` | int | Happiness. |
| `maintenance` | int | Maintenance. |
| `shipyard_rate` | int | Building speed. |
| `repair` | int | Repairs. |

### `happiness_model`

| Field | Type | Meaning |
|---|---|---|
| `id` | happiness model index | Its index. |
| `name` | text | Its name. |
| `description` | text | The data's description. |
| `max_positive_change` | int | The most a turn's mood can rise. |
| `max_negative_change` | int | The most it can fall. |
| `triggers` | list of happiness_trigger | What changes it. |

### `happiness_trigger`

| Field | Type | Meaning |
|---|---|---|
| `trigger` | text | The event, as the data names it. |
| `change` | int | How much it changes the mood. |

### `race_preset`

A race the install offers, from its race files.

| Field | Type | Meaning |
|---|---|---|
| `name` | text | Its name. |
| `style` | text | Its pictures' folder. |
| `neutral` | bool | A neutral race. |
| `description` | text | Its short description. |
| `empire_name` | text | The empire's default name. |
| `empire_type` | text | Its default government. |
| `emperor_name` | text | Its leader's default name. |
| `emperor_title` | text | Its leader's default title. |
| `demeanor` | text | Its demeanor. |
| `culture` | text | Its culture's name. |
| `happiness_type` | text | Its happiness model's name. |
| `planet_type` | text | Its surface. |
| `atmosphere` | text | The gas it breathes. |
| `personality_group` | int | The computer players' personality group. |
| `tiers` | list of race_tier | Its characteristics and traits at each racial-point level. |

### `race_tier`

| Field | Type | Meaning |
|---|---|---|
| `characteristics` | list of race_characteristic | Characteristics in percent. |
| `traits` | list of text | Traits. |

### `race_characteristic`

| Field | Type | Meaning |
|---|---|---|
| `name` | text | The characteristic, as the data names it. |
| `value` | int | Its value in percent. |

### `planet_size`

| Field | Type | Meaning |
|---|---|---|
| `id` | planet size index | Its index. |
| `name` | text | Its name. |
| `physical_type` | text | "Planet", "Asteroids"... |
| `stellar_size` | text | "Tiny" to "Huge". |
| `max_facilities` | int | Facilities it holds. |
| `max_population` | int | Population it holds. |
| `max_cargo` | int | Cargo it stores. |
| `max_facilities_domed` | int | Facilities under domes. |
| `max_population_domed` | int | Population under domes. |
| `max_cargo_domed` | int | Cargo under domes. |
| `constructed` | bool | A ringworld or sphereworld. |

### `system_type`

| Field | Type | Meaning |
|---|---|---|
| `id` | system type index | Its index. |
| `name` | text | Its name. |
| `description` | text | The data's description. |
| `physical_type` | text | "Normal", "Nebulae", "Black Hole"... |
| `empires_can_start_in` | bool | Empires may start in one. |
| `abilities` | list of ability | Its system-wide abilities. |

### `sector_type`

| Field | Type | Meaning |
|---|---|---|
| `id` | sector type index | Its index. |
| `physical_type` | text | The kind of object. |
| `description` | text | The data's description. |
| `planet_size` | text | A planet's size. |
| `planet_physical_type` | text | A planet's surface. |
| `planet_atmosphere` | text | A planet's atmosphere. |
| `star_size` | text | A star's size. |
| `star_age` | text | A star's age. |
| `star_color` | text | A star's colour. |
| `star_luminosity` | text | A star's luminosity. |
| `storm_size` | text | A storm's size. |
| `warp_point_size` | text | A warp point's size. |
| `unusual` | bool | An unusual object. |

### `ability_kind`

| Field | Type | Meaning |
|---|---|---|
| `name` | text | The ability's name, as the data writes it. |
| `aggregation` | aggregation | How several entries of it combine. |

### `aggregation`

How the entries of one ability on a vehicle, colony or system combine.

| Value | Meaning |
|---|---|
| `sum` | Added up (to two billion at most). |
| `largest` | The largest value counts. |
| `smallest` | The smallest value counts. |
| `count` | How many entries there are. |
| `present` | Whether there is one. |
| `per_sight_type` | For each sight type, the largest level. |
| `first_per_id` | Grouped by the second value; each group's first, added up. |
| `per_family` | Whole-vehicle entries plus each component family's largest. |
| `unspecified` | Rules read it their own way; added up here. |

### `formation`

| Field | Type | Meaning |
|---|---|---|
| `id` | formation index | Its index. |
| `name` | text | Its name. |
| `description` | text | The data's description. |
| `leader` | formation_slot | Where the leader stands. |
| `positions` | list of formation_slot | Where the members stand, in member order. |

### `formation_slot`

| Field | Type | Meaning |
|---|---|---|
| `x` | int | Column offset. |
| `y` | int | Row offset. |
| `design_type` | text | The design type that takes the place. |

### `default_strategy`

| Field | Type | Meaning |
|---|---|---|
| `id` | int | Its position. |
| `name` | text | Its name. |
| `settings` | list of strategy_setting | Its settings. |

### `intel_project`

| Field | Type | Meaning |
|---|---|---|
| `id` | intel project index | Its index. |
| `name` | text | Its name. |
| `description` | text | The data's description. |
| `group` | text | Its group. |
| `cost` | int | Points it needs. |
| `type` | text | What it does. |
| `effect_amount` | int | How much. |
| `requirements` | list of requirement | The technology it needs. |

## Queries

`sdk::Queries` holds the questions a script may ask during one engine call,
each a named function from a map of arguments to a result. They answer with
the engine's own functions, on the same state the view was built from: in an
empire's own view, from what that empire knows. A bad argument is refused
with its path, as commands are (`vehicle: no such vehicle in view`). Asking
never changes the game.

### path

The route between two places, or from a vehicle or fleet, over the sectors
and warp links the empire knows, with its own Ship Movement options, as its
ships route their moves. A whole view routes over everything instead, known
or not, unless `omniscient` is false.

| Argument | Type | Meaning |
|---|---|---|
| `origin` | location, or system id | Where from (a system id stands for its centre). Default: the group's place. |
| `destination` | location, or system id | Where to. |
| `vehicle` | vehicle id | A group to route: one of our vehicles... |
| `fleet` | fleet id | ...or fleets. Its route options apply and `turns` is given. |
| `omniscient` | bool | Route over everything, known or not: a whole view's default, refused in an empire's own view. |

Result: `path_result`.

### `path_result`

| Field | Type | Meaning |
|---|---|---|
| `found` | bool | A route exists. |
| `steps` | list of location | Each sector entered; a warp jump is one step to the far end. |
| `length` | int, or null | Movement points the route takes. |
| `jumps` | int, or null | Warp jumps on the route. |
| `turns` | int, or null | For a group: turns to get there from where it is at its speed; -1 never. |

### movement

The movement and supply range of one of our vehicles or fleets.

| Argument | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id | A vehicle... |
| `fleet` | fleet id | ...or a fleet. |

Result: `vehicle_movement` for a vehicle, `fleet_movement` for a fleet.

### `vehicle_movement`

| Field | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id | The vehicle. |
| `movement` | int | Movement left this turn. |
| `max_movement` | int | Its full movement a turn. |
| `moves_per_turn` | int | Steps that movement makes in a turn (a simultaneous turn's day counter may lose some). |
| `supply` | int | Supply on board. |
| `supply_capacity` | int | The most it holds. |
| `unlimited_supply` | bool | It never runs out. |
| `uses_supply` | bool | It keeps supply at all. |
| `supply_per_move` | int | Supply one step costs. |
| `moves_on_supply` | int, or null | Steps its supply lasts (null: no limit). |
| `turns_on_supply` | int, or null | Turns of full movement its supply lasts. |

### `fleet_movement`

| Field | Type | Meaning |
|---|---|---|
| `fleet` | fleet id | The fleet. |
| `movement` | int | The least movement left among the members with it. |
| `max_movement` | int | Its speed: the lowest full movement among them. |
| `moves_per_turn` | int | Steps that speed makes in a turn. |
| `moves_on_supply` | int, or null | The fewest steps any member's supply lasts. |
| `turns_on_supply` | int, or null | Turns of full movement that lasts. |
| `members` | list of vehicle_movement | Each member with the fleet. |

### design_figures

A design's figures as the designer shows them, and whether it is legal and
why not. Name one of our designs or a known foreign one, or propose a hull and
components: a proposal is checked against our technology, as the designer
checks it.

| Argument | Type | Meaning |
|---|---|---|
| `design` | design id | An existing design... |
| `hull` | hull index | ...or a hull... |
| `entries` | list of design_entry | ...with its components and mounts... |
| `components` | list of component index | ...or just components, unmounted. |

Result: `design_figures`.

### abilities

The abilities of one thing, each read in its own mode. Name exactly one.

| Argument | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id | A vehicle in view (its intact components). |
| `planet` | object id | A planet in view (with a colony: its facilities too). |
| `system` | system id | An explored system. |
| `design` | design id | A known design, every component intact. |
| `component` | component index | A component. |
| `facility` | facility index | A facility. |
| `hull` | hull index | A hull. |

Result: `ability_report`.

### `ability_report`

| Field | Type | Meaning |
|---|---|---|
| `entries` | list of parsed_ability | Every entry, in order. |
| `values` | list of ability_total | Each ability once, its entries combined. |

### `parsed_ability`

| Field | Type | Meaning |
|---|---|---|
| `name` | text | The ability's name. |
| `value1` | int | Its first value as a number (0 for text). |
| `value2` | int | Its second value. |

### `ability_total`

| Field | Type | Meaning |
|---|---|---|
| `name` | text | The ability's name. |
| `aggregation` | aggregation | How its entries combine. |
| `value` | int | The combined value (for per-sight-type abilities, the best level). |
| `per_sight_type` | sight_levels, or null | Per-sight-type abilities: the level in each type. |

### `sight_levels`

| Field | Type | Meaning |
|---|---|---|
| `em_active` | int | Active electromagnetic. |
| `em_passive` | int | Passive electromagnetic. |
| `psychic` | int | Psychic. |
| `gravitic` | int | Gravitic. |
| `temporal` | int | Temporal. |

### queue_forecast

What one of our construction queues will finish and when, or what it would
with other items.

| Argument | Type | Meaning |
|---|---|---|
| `planet` | object id | A colony's queue... |
| `vehicle` | vehicle id | ...or a vehicle's. |
| `items` | list of queue_item | Items to forecast instead of the queue's own. |

Result: `queue` (see [Colonies](#colonies)).

### research_forecast

What reaching a level in one tech area costs, and how long it takes when
nothing else is researched: the first level draws on this turn's points and
the area's progress, the later ones on a turn's research income; one level a
turn at most, the excess lost.

| Argument | Type | Meaning |
|---|---|---|
| `area` | tech index | The tech area. |
| `level` | int | The level wanted (default: the next). |

Result: `research_forecast`.

### `research_forecast`

| Field | Type | Meaning |
|---|---|---|
| `area` | tech index | The area. |
| `current_level` | int | Our level now. |
| `max_level` | int | Its highest level. |
| `researchable` | bool | We may queue it now. |
| `levels` | list of research_level | Each level up to the one wanted (never past the highest). |
| `total_cost` | int | All their costs. |
| `turns` | int | All their turns; -1 never. |

### `research_level`

| Field | Type | Meaning |
|---|---|---|
| `level` | int | The level. |
| `cost` | int | Its cost. |
| `turns` | int | Turns it takes; -1 never. |

### colonize_problem

Why one of our vehicles cannot colonize a planet now, distance aside.

| Argument | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id | The colony ship. |
| `planet` | object id | The planet. |

Result: `problem`.

### queue_item_problem

Why an item cannot go in one of our queues.

| Argument | Type | Meaning |
|---|---|---|
| `planet` | object id | A colony's queue... |
| `vehicle` | vehicle id | ...or a vehicle's. |
| `item` | queue_item | The item. |

Result: `problem`.

### `problem`

| Field | Type | Meaning |
|---|---|---|
| `problem` | text | The reason, as the game gives it; empty when there is none. |

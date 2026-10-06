# Script commands

A script changes the game only by giving commands, exactly as a human player
does through the windows. This page lists every command, every order a
vehicle, fleet or colony can be given, and the orders of a tactical battle,
with their fields and an example of each. The engine side is `src/sdk/codec.hpp`
(`sdk::decodeCommand`, `sdk::encodeCommand` and friends); see
[MODDING_SDK.md](../MODDING_SDK.md) for where commands fit.

The view a script reads, and the types it shares with this page, are in
[view.md](view.md).

## Conventions

- **A command is a map.** Its `kind` names it (`"set_orders"`); its other keys
  are its fields. Field names, kinds and every enumeration value are
  lower_snake_case.
- **Ids are whole numbers**: the ids of the view (`vehicles[].id`,
  `systems[].id`, ...). `null` means "none" wherever an id is optional.
- **Rules records are indices** into the tables of the rules view (a
  component, facility, hull, tech area, ...).
- **Missing fields keep their defaults**: an id is none, a number 0, a flag
  false, a list empty and an enumeration its first value, unless the table
  names another default ("-1 by default", "(the default)"); a field of a type
  keeps the defaults of that type's table. So
  `{"kind": "leave_fleet", "vehicle": 12}` is a whole command.
- **Unknown fields are refused**, so a misspelt field is noticed.
- **No field is a Python keyword**, so a script can read every field as an
  attribute: an order's resources are `from_resource` and `to_resource`, a
  message's empires `from_empire` and `to_empire`.
- **Encoding writes every field**, so a command read back from the engine (a
  journal, a replay, an external bot's echo) always has all its keys.
- **The same tree is JSON** for external bots: a command is a JSON object, ids
  are numbers, `null` is `null`.

The engine checks the *shape* of a command when it decodes it: kinds, field
names, value types and ranges. Whether the command is allowed (the vehicle is
yours, the queue has room, the design is legal) is decided when the game
applies it, with the same checks and the same reasons a human player gets.

### Errors

A command that cannot be decoded is refused with the path to the first bad
value and what is wrong with it:

```text
kind: 'SetOrders' is not a command kind
vehical: unknown field
orders[2].object: expected an object id (or null)
formation: -1 is out of range (0 to 4294967295)
item.spent.minerals: expected a whole number
areas[1]: 'dancing' is not a minister
```

In a list of commands, or a turn's orders, the path starts with the
command's position: `commands[3].orders[0].kind: 'fly' is not an order kind`.

## A turn's orders

### `empire_orders`

One empire's commands for one turn, the form a whole turn travels in (the
classic game's order file).

| Field | Type | Meaning |
|---|---|---|
| `empire` | empire id, or null | Whose orders these are. |
| `turn` | int | The turn they were made for. |
| `commands` | list of command | The commands, applied in this order. |

```json
{"empire": 0, "turn": 12, "commands": [{"kind": "leave_fleet", "vehicle": 31}]}
```

## Vehicles and fleets

### `set_orders`

Replaces the order list of a vehicle, a fleet or a colony (name one of the
three). The orders that start the new list and repeat the current one are
kept as they are; every order after them is expanded the moment it is given,
as in the classic game: Explore becomes a move to the nearest unexplored warp
point and a warp, Resupply and Repair a move to the nearest place for them,
and an order aimed somewhere else gets a Move To in front of it. A colony
takes only Launch Units, Recover Units, Use Facility and Convert Resources.

| Field | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id, or null | The vehicle given the orders. |
| `fleet` | fleet id, or null | Or the fleet. |
| `planet` | object id, or null | Or one of our colonies, by its planet. |
| `orders` | list of order | The new list (see [Orders](#orders)). |
| `repeat` | bool | Repeat the list when it ends (false by default). |

```json
{"kind": "set_orders", "vehicle": 31, "orders": [{"kind": "move_to", "location": {"system": 4, "x": 6, "y": 6}}, {"kind": "sentry"}]}
```

### `order_tagged`

An order given to several vehicles at once, as when the player tags vehicles
in a sector's list: the orders are added to the end of each vehicle's own
list, a fleet member standing for its whole fleet. The vehicles must share a
sector. In a turn-based game they then act at once as one group.

| Field | Type | Meaning |
|---|---|---|
| `vehicles` | list of vehicle id | The tagged vehicles, in the order they were tagged. |
| `orders` | list of order | The orders to add. |
| `repeat` | bool | Switch Repeat on in each list. |

```json
{"kind": "order_tagged", "vehicles": [31, 32], "orders": [{"kind": "attack", "vehicle": 77}]}
```

### `create_fleet`

Forms a new fleet of vehicles that share a sector; the first one leads it.

| Field | Type | Meaning |
|---|---|---|
| `name` | text | The fleet's name. |
| `members` | list of vehicle id | Its vehicles; the first leads. |

```json
{"kind": "create_fleet", "name": "First Strike", "members": [31, 32, 40]}
```

### `join_fleet`

A vehicle joins a fleet in its sector. Its own orders are cleared: it gets
the fleet's later orders only.

| Field | Type | Meaning |
|---|---|---|
| `fleet` | fleet id, or null | The fleet. |
| `vehicle` | vehicle id, or null | The vehicle that joins. |

```json
{"kind": "join_fleet", "fleet": 3, "vehicle": 41}
```

### `leave_fleet`

The vehicle leaves its fleet, and its orders are cleared. A fleet left with
no member at its location is disbanded.

| Field | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id, or null | The vehicle that leaves. |

```json
{"kind": "leave_fleet", "vehicle": 41}
```

### `disband_fleet`

Every member leaves the fleet and loses its orders; the fleet is gone.

| Field | Type | Meaning |
|---|---|---|
| `fleet` | fleet id, or null | The fleet. |

```json
{"kind": "disband_fleet", "fleet": 3}
```

### `set_fleet_options`

Sets a fleet's formation and combat strategy.

| Field | Type | Meaning |
|---|---|---|
| `fleet` | fleet id, or null | The fleet. |
| `formation` | formation index | A formation of the rules view (0 by default). |
| `strategy` | strategy index | A position in our own strategies (`my.strategies`). |

```json
{"kind": "set_fleet_options", "fleet": 3, "formation": 1, "strategy": 0}
```

### `set_fleet_leader`

Makes one of the fleet's members at its location its leader, the vehicle the
formation forms around.

| Field | Type | Meaning |
|---|---|---|
| `fleet` | fleet id, or null | The fleet. |
| `vehicle` | vehicle id, or null | The new leader. |

```json
{"kind": "set_fleet_leader", "fleet": 3, "vehicle": 40}
```

### `set_vehicle_strategy`

Sets the combat strategy every vehicle of one of our designs uses.

| Field | Type | Meaning |
|---|---|---|
| `design` | design id, or null | The design. |
| `strategy` | strategy index | A position in our own strategies. |

```json
{"kind": "set_vehicle_strategy", "design": 12, "strategy": 2}
```

### `rename`

Renames one of our vehicles, fleets, designs or colonies (name one).

| Field | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id, or null | A vehicle. |
| `fleet` | fleet id, or null | Or a fleet. |
| `design` | design id, or null | Or a design. |
| `planet` | object id, or null | Or a colony, by its planet. |
| `name` | text | The new name. |

```json
{"kind": "rename", "fleet": 3, "name": "Home Guard"}
```

### `scrap`

The Scrap window's Scrap for one vehicle, or Scrap Facilities for one
facility of a colony. A vehicle is scrapped at once in a turn-based game; in
a simultaneous game Scrap becomes its only order, carried out when it next
acts. With `move_first` the vehicle first moves to that sector (the
computer player's way of scrapping at a yard). Scrap Facilities acts at once
in both turn styles.

| Field | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id, or null | The vehicle to scrap. |
| `facility_planet` | object id, or null | Or the colony whose facility goes. |
| `facility_slot` | int | That facility's position in the colony's list (-1 by default). |
| `move_first` | location | Where to go before scrapping (no system: stay). |

```json
{"kind": "scrap", "vehicle": 52}
```

### `mothball`

Mothballs a vehicle, or brings it back into service (`mothball` false). Like
Scrap, it acts at once in a turn-based game and as the vehicle's only order
in a simultaneous one.

| Field | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id, or null | The vehicle. |
| `mothball` | bool | True to mothball (the default), false to unmothball. |

```json
{"kind": "mothball", "vehicle": 52, "mothball": false}
```

### `analyze`

The Scrap window's "Deconstruct & Analyze": the vehicle is taken apart and
may teach one technology level.

| Field | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id, or null | The vehicle. |

```json
{"kind": "analyze", "vehicle": 60}
```

### `self_destruct`

The vehicle destroys itself.

| Field | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id, or null | The vehicle. |

```json
{"kind": "self_destruct", "vehicle": 61}
```

### `fire_on`

The Scrap window's "Fire On And Destroy": our armed ships in the sector
destroy one of our own vehicles.

| Field | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id, or null | The vehicle to destroy. |

```json
{"kind": "fire_on", "vehicle": 62}
```

### `retrofit`

Refits a vehicle to another of our designs at one of our space yards. A
Scrap window action like `scrap`.

| Field | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id, or null | The vehicle. |
| `design` | design id, or null | The design to refit it to. |

```json
{"kind": "retrofit", "vehicle": 31, "design": 18}
```

### `set_minister`

Puts a vehicle or colony under its minister (the computer controls it), or
takes it back; `empire_wide` does the same for everything of ours.

| Field | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id, or null | A vehicle. |
| `planet` | object id, or null | Or a colony, by its planet. |
| `empire_wide` | bool | Every vehicle and colony at once. |
| `on` | bool | Minister control on (the default) or off. |

```json
{"kind": "set_minister", "vehicle": 31, "on": true}
```

### `enter_sector`

Turn-based games: the answer to the question asked when a group's move stops
before a sector with enemies in it (`my.questions` in the view). With `enter`
the group carries on into the sector and its battle; otherwise the move stops
and the order list is cleared. A tagged group's question is answered with its
vehicles in `tagged`.

| Field | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id, or null | The vehicle asked. |
| `fleet` | fleet id, or null | Or the fleet. |
| `where` | location | The sector the question is about. |
| `enter` | bool | Enter (the default) or stay out. |
| `tagged` | list of vehicle id | A tagged group's vehicles, in tag order. |

```json
{"kind": "enter_sector", "fleet": 3, "where": {"system": 4, "x": 5, "y": 6}, "enter": true}
```

### `open_vehicle_report`

A human player opened the report of a foreign vehicle: when our scanners
reach it, the designs the report shows become known. A script AI may give it
to learn designs the same way.

| Field | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id, or null | The foreign vehicle. |

```json
{"kind": "open_vehicle_report", "vehicle": 77}
```

## Construction

### `queue_add`

Adds an item to a construction queue: a colony's, or a vehicle's with a
space yard.

| Field | Type | Meaning |
|---|---|---|
| `target` | queue_target | Whose queue. |
| `item` | queue_item | What to build. |
| `position` | int | Where in the queue; -1 (the default) is the end. |

```json
{"kind": "queue_add", "target": {"planet": 7}, "item": {"kind": "vehicle", "design": 12, "count": 2}}
```

### `queue_remove`

Removes the item at `index`.

| Field | Type | Meaning |
|---|---|---|
| `target` | queue_target | Whose queue. |
| `index` | int | The item's position. |

```json
{"kind": "queue_remove", "target": {"planet": 7}, "index": 1}
```

### `queue_move`

Moves an item within the queue.

| Field | Type | Meaning |
|---|---|---|
| `target` | queue_target | Whose queue. |
| `from_index` | int | Its position now. |
| `to_index` | int | Its new position. |

```json
{"kind": "queue_move", "target": {"planet": 7}, "from_index": 3, "to_index": 0}
```

### `queue_set_count`

Changes how many of an item are built together.

| Field | Type | Meaning |
|---|---|---|
| `target` | queue_target | Whose queue. |
| `index` | int | The item's position. |
| `count` | int | The new count (1 by default). |

```json
{"kind": "queue_set_count", "target": {"vehicle": 90}, "index": 0, "count": 5}
```

### `queue_flags`

Sets a queue's switches.

| Field | Type | Meaning |
|---|---|---|
| `target` | queue_target | Whose queue. |
| `on_hold` | bool | Nothing is built while on hold. |
| `repeat` | bool | A finished item goes on being built. |
| `emergency` | bool | Emergency building: faster for a while, then slower. |
| `auto_waypoint` | int | New vehicles move to this waypoint slot; -1 (the default) for none. |

```json
{"kind": "queue_flags", "target": {"planet": 7}, "repeat": true, "auto_waypoint": 2}
```

### `queue_replace_facility`

Switches a queued facility, in place, to another facility of the same family
that we have researched, keeping its count and what was paid into it (the
Upgrade Facilities button).

| Field | Type | Meaning |
|---|---|---|
| `target` | queue_target | Whose queue. |
| `index` | int | The item's position. |
| `facility` | facility index | The facility to build instead. |

```json
{"kind": "queue_replace_facility", "target": {"planet": 7}, "index": 2, "facility": 31}
```

## Colonies

### `set_colony_type`

Sets a colony's type (one of `my.colony_types`); also the answer when the
game asks for a new colony's type.

| Field | Type | Meaning |
|---|---|---|
| `planet` | object id, or null | The colony's planet. |
| `colony_type` | text | The type's name. |

```json
{"kind": "set_colony_type", "planet": 7, "colony_type": "Research"}
```

### `abandon_planet`

Gives up a colony.

| Field | Type | Meaning |
|---|---|---|
| `planet` | object id, or null | The colony's planet. |

```json
{"kind": "abandon_planet", "planet": 19}
```

### `cloak_colony`

Cloaks or decloaks a colony, at once and at no cost. Cloaking needs a cloak
level of 2 or more from the colony's facilities.

| Field | Type | Meaning |
|---|---|---|
| `planet` | object id, or null | The colony's planet. |
| `cloak` | bool | Cloak (the default) or decloak. |

```json
{"kind": "cloak_colony", "planet": 7, "cloak": true}
```

### `transfer_cargo`

Moves cargo at once between two of our holders (vehicles or colonies) in the
same sector: units of one design, or population of one race.

| Field | Type | Meaning |
|---|---|---|
| `from_vehicle` | vehicle id, or null | The holder it leaves... |
| `from_planet` | object id, or null | ...a vehicle or a colony. |
| `to_vehicle` | vehicle id, or null | The holder it goes to... |
| `to_planet` | object id, or null | ...a vehicle or a colony. |
| `unit_design` | design id, or null | The units' design; null to move population. |
| `population_race` | empire id, or null | The race of the population moved (the empire it belongs to). |
| `amount` | int | Units, or millions of population. |

```json
{"kind": "transfer_cargo", "from_planet": 7, "to_vehicle": 33, "population_race": 0, "amount": 50}
```

### `jettison_cargo`

Throws cargo away at once, from one of our ships or bases or from a colony's
stored cargo (never its own population). Each unit thrown away counts as lost.

| Field | Type | Meaning |
|---|---|---|
| `vehicle` | vehicle id, or null | The vehicle... |
| `planet` | object id, or null | ...or the colony. |
| `population` | list of population_group | Millions of each race to throw away. |
| `units` | list of unit_stack | Units of each design to throw away. |

```json
{"kind": "jettison_cargo", "vehicle": 33, "units": [{"design": 21, "count": 4}]}
```

## Designs

### `create_design`

Adds a design. The engine gives it its id and owner and checks it as the
designer does (`design_figures` in [view.md](view.md#queries) tells in
advance whether it is legal and why not).

| Field | Type | Meaning |
|---|---|---|
| `design` | design | The design: name, type, hull, components, strategy and picture matter. |

A design may name a picture of its own (`picture`), such as one a mod adds;
the engine refuses a name with folders in it, and a computer without the
picture shows the hull's.

```json
{"kind": "create_design", "design": {"name": "Lancer", "design_type": "Attack Ship", "hull": 3, "entries": [{"component": 0, "mount": -1}, {"component": 14, "mount": 2}]}}
{"kind": "create_design", "design": {"name": "Escort", "design_type": "Attack Ship", "hull": 3, "picture": "Corvette2", "entries": [{"component": 0, "mount": -1}]}}
```

### `edit_design`

Changes one of our designs in place; refused for a design that was built,
retrofitted to, or is in a queue.

| Field | Type | Meaning |
|---|---|---|
| `design` | design id, or null | The design to change. |
| `new_design` | design | Its new contents. |

```json
{"kind": "edit_design", "design": 12, "new_design": {"name": "Lancer II", "design_type": "Attack Ship", "hull": 3, "entries": [{"component": 0, "mount": -1}]}}
```

### `set_design_obsolete`

Marks a design obsolete, or current again.

| Field | Type | Meaning |
|---|---|---|
| `design` | design id, or null | The design. |
| `obsolete` | bool | Obsolete (the default) or not. |

```json
{"kind": "set_design_obsolete", "design": 12, "obsolete": true}
```

### `delete_design`

Deletes a design that was never built.

| Field | Type | Meaning |
|---|---|---|
| `design` | design id, or null | The design. |

```json
{"kind": "delete_design", "design": 13}
```

## Research and intelligence

### `set_research`

Replaces the research queue (at most 12 projects, one per tech area). A tech
area keeps its progress while it stays in the queue.

| Field | Type | Meaning |
|---|---|---|
| `queue` | list of research_project | The projects, in order. |
| `evenly` | bool | Share points evenly (the default), or fill the projects in order. |
| `repeat` | bool | A finished area goes back to the end of the queue. |

```json
{"kind": "set_research", "queue": [{"area": 4}, {"area": 9}], "evenly": false}
```

### `set_intel`

Replaces the intelligence queue.

| Field | Type | Meaning |
|---|---|---|
| `queue` | list of intel_project_order | The projects, in order. |
| `evenly` | bool | Share points evenly (the default), or in order. |
| `repeat` | bool | A finished project starts again. |

```json
{"kind": "set_intel", "queue": [{"project": 2, "target": 1}]}
```

## Diplomacy

### `send_message`

Sends a diplomatic message. The engine gives it its id, sender and date.
`minister` marks a message the computer player or a Politics minister writes,
whose requests about a third empire are not checked as a player's are.

| Field | Type | Meaning |
|---|---|---|
| `message` | message | The message. |
| `minister` | bool | Written by a minister. |

```json
{"kind": "send_message", "message": {"to_empire": 1, "type": "propose_treaty", "treaty": "non_aggression", "text": "Peace?"}}
```

### `answer_message`

Answers a message delivered to us: accept or refuse, with a text.

| Field | Type | Meaning |
|---|---|---|
| `message` | message id, or null | The message answered. |
| `accept` | bool | Accept (false by default: refuse). |
| `text` | text | The reply's text. |

```json
{"kind": "answer_message", "message": 40, "accept": true, "text": "Agreed."}
```

### `decide_war`

A computer player's decision to go to war without a declaration (its speech
list was empty): its anger toward the empire becomes 100; no treaty changes
and no message is sent.

| Field | Type | Meaning |
|---|---|---|
| `target` | empire id, or null | The empire. |

```json
{"kind": "decide_war", "target": 2}
```

### `carry_out_demand`

The Politics minister carries out a demand or request it accepted (leaving a
system, declaring war on a third empire, stopping espionage, ...).

| Field | Type | Meaning |
|---|---|---|
| `demand` | message id, or null | The demand, delivered to us. |

```json
{"kind": "carry_out_demand", "demand": 41}
```

### `use_demand_entry`

The Politics minister used up one entry of a demand list naming an empire.

| Field | Type | Meaning |
|---|---|---|
| `list` | demand_list | Which list. |
| `about` | empire id, or null | The empire the entry names. |

```json
{"kind": "use_demand_entry", "list": "peace", "about": 2}
```

## Empire settings and lists

### `set_waypoint`

Sets or clears one of the ten waypoints.

| Field | Type | Meaning |
|---|---|---|
| `slot` | int | 0 to 9. |
| `waypoint` | waypoint, or null | The waypoint; null clears the slot. |

```json
{"kind": "set_waypoint", "slot": 2, "waypoint": {"name": "Rally", "location": {"system": 4, "x": 6, "y": 6}, "set": true}}
```

### `set_system_flags`

Marks a system to avoid, or claims it; a null field is left as it is.

| Field | Type | Meaning |
|---|---|---|
| `system` | system id, or null | The system. |
| `avoid` | bool, or null | Among the systems to avoid. |
| `claim` | bool, or null | Among our claimed systems. |

```json
{"kind": "set_system_flags", "system": 8, "claim": true}
```

### `set_system_note`

Writes our note on a system.

| Field | Type | Meaning |
|---|---|---|
| `system` | system id, or null | The system. |
| `note` | text | The note; empty to clear it. |

```json
{"kind": "set_system_note", "system": 8, "note": "Rich asteroid belt"}
```

### `tag_minefield`

Tags a sector as a minefield to route around, or untags it.

| Field | Type | Meaning |
|---|---|---|
| `location` | location | The sector. |
| `tagged` | bool | Tag (the default) or untag. |

```json
{"kind": "tag_minefield", "location": {"system": 8, "x": 0, "y": 6}}
```

### `set_strategy`

Adds, replaces or removes one of our combat strategies.

| Field | Type | Meaning |
|---|---|---|
| `index` | int | The strategy's position; -1 (the default) adds a new one. |
| `strategy` | strategy | Its name and settings. |
| `remove` | bool | Remove the strategy at `index` instead. |

```json
{"kind": "set_strategy", "strategy": {"name": "Careful", "settings": [{"name": "Break Off At", "value": "50"}]}}
```

### `set_repair_priorities`

Replaces the order in which repairs are made.

| Field | Type | Meaning |
|---|---|---|
| `priorities` | list of text | The priorities, first first. |

```json
{"kind": "set_repair_priorities", "priorities": ["Ships", "Bases", "Units"]}
```

### `set_design_types`

Replaces our list of design types.

| Field | Type | Meaning |
|---|---|---|
| `design_types` | list of text | The types. |

```json
{"kind": "set_design_types", "design_types": ["Attack Ship", "Scout", "Colony Ship"]}
```

### `set_colony_types`

Replaces our list of colony types.

| Field | Type | Meaning |
|---|---|---|
| `colony_types` | list of text | The types. |

```json
{"kind": "set_colony_types", "colony_types": ["Mining", "Research"]}
```

### `set_empire_options`

Empire options; a null field is left as it is.

| Field | Type | Meaning |
|---|---|---|
| `ai_minimal_changes` | bool, or null | The computer player changes as little as it can. |
| `password_hash` | text, or null | The empire's password, hashed. |
| `choose_colony_type` | bool, or null | Ask for a new colony's type (turn-based games). |

```json
{"kind": "set_empire_options", "choose_colony_type": false}
```

### `set_email`

Changes the empire's e-mail address.

| Field | Type | Meaning |
|---|---|---|
| `email` | text | The address. |

```json
{"kind": "set_email", "email": "admiral@example.org"}
```

### `set_ministers`

The Ministers window; a null field is left as it is.

| Field | Type | Meaning |
|---|---|---|
| `areas` | list of minister, or null | The minister areas switched on. |
| `style` | text, or null | The minister style: a folder of AI files, empty for the race's own. |
| `use_race_style` | bool, or null | Use the race's own files even with a style. |
| `new_vehicles` | bool, or null | New vehicles start under minister control. |
| `individual` | bool, or null | Minister control of every vehicle, fleet and colony, on or off. |
| `fleets` | bool, or null | Minister control of every fleet, on or off. |
| `complete_ai` | bool, or null | Everything above at once, on or off. |

```json
{"kind": "set_ministers", "areas": ["research", "politics"], "new_vehicles": false}
```

### `set_encounter_options`

The empire's Ship Movement and Ship Orders options; a null field is left as
it is.

| Field | Type | Meaning |
|---|---|---|
| `clear_orders_on_encounter` | encounter_clear, or null | Whom meeting after a warp clears a group's orders. |
| `avoid_tagged_minefields` | bool, or null | Routes go around tagged minefields. |
| `avoid_restricted_systems` | bool, or null | Routes never cross the systems to avoid. |

```json
{"kind": "set_encounter_options", "clear_orders_on_encounter": "any"}
```

### `set_interface_options`

Replaces the Empire Options switches and what the windows remember. Only
`auto_claim_colonized` changes the game; the rest is for the client.

| Field | Type | Meaning |
|---|---|---|
| `options` | interface_options | Every switch. |

```json
{"kind": "set_interface_options", "options": {"auto_claim_colonized": false}}
```

## Mod orders

### `mod_command`

An order a mod declares (`[[rules.orders]]` in its mod.toml, docs/sdk/rules.md
"Orders"). The game checks it against the declaration: its mod is one of the
game's rules mods, it names exactly the target the order applies to (a vehicle,
fleet or colony of ours, another empire, or none for the empire itself), and its
arguments are the declared ones with the declared types and ranges (those left
out take their defaults). Then the mod's own check may refuse it with its
reason, and its effect runs at once, in both turn styles, as a cargo transfer
does. It travels like any other command: over the network, in e-mail games and
in replays.

| Field | Type | Meaning |
|---|---|---|
| `mod` | text | The mod's id. |
| `name` | text | The order's name in the mod. |
| `vehicle` | vehicle id, or null | The vehicle it is given to (orders that apply to a vehicle). |
| `fleet` | fleet id, or null | The fleet (orders that apply to a fleet). |
| `planet` | object id, or null | The colony, by its planet (orders that apply to a colony). |
| `empire` | empire id, or null | The other empire (orders that apply to an empire). |
| `args` | map | Its arguments by name: whole numbers, true or false, text, or ids. |

```json
{"kind": "mod_command", "mod": "example.shields", "name": "overcharge", "vehicle": 31, "args": {"power": 2}}
```

## Orders

An order list belongs to a vehicle, a fleet (each member holds a copy) or a
colony, and is given with `set_orders` or `order_tagged`. Every order has the
same fields; each kind uses some of them.

### `order`

| Field | Type | Meaning |
|---|---|---|
| `kind` | order_kind | What to do. |
| `location` | location | Where: a sector (Move To, Seek, a stellar manipulation's target). |
| `object` | object ref, or null | A stellar object: a warp point, a planet, a manipulation's target. |
| `vehicle` | vehicle ref, or null | A vehicle: an attack's target, a pursuit, the unit group to recover from. |
| `design` | design ref, or null | A design: the units to load, launch or recover, a retrofit's design. |
| `amount` | int | A number whose meaning depends on the kind (see below). |
| `from_resource` | resource | Convert Resources: the resource converted. |
| `to_resource` | resource | Convert Resources: the resource it becomes. |

`from_resource` and `to_resource` are `"minerals"` unless the order converts
resources. An order whose resource is no resource does nothing; it reads as a
number.

### `order_kind`

| Value | Fields used | Meaning | Example |
|---|---|---|---|
| `move_to` | location | Move to the sector. | `{"kind": "move_to", "location": {"system": 4, "x": 3, "y": 9}}` |
| `warp` | object | Go through the warp point. | `{"kind": "warp", "object": 88}` |
| `attack` | vehicle or object, location | Attack a vehicle, or a planet (`object`), where it was seen. | `{"kind": "attack", "vehicle": 77}` |
| `resupply` | | Go to the nearest place that resupplies us. | `{"kind": "resupply"}` |
| `repair` | | Go to the nearest place that repairs us. | `{"kind": "repair"}` |
| `explore` | | Explore the nearest system we have not explored. | `{"kind": "explore"}` |
| `colonize` | object, amount | Colonize the planet. | `{"kind": "colonize", "object": 19}` |
| `sentry` | | Wait, ready to fight. | `{"kind": "sentry"}` |
| `load_cargo` | design, amount | Load units of the design, or population (no design); amount -1 is as much as fits. | `{"kind": "load_cargo", "amount": -1}` |
| `drop_cargo` | design, amount | Drop units or population, as for loading. | `{"kind": "drop_cargo", "design": 21, "amount": 4}` |
| `launch_units` | design, amount | Launch units of the design from cargo. | `{"kind": "launch_units", "design": 21, "amount": 10}` |
| `recover_units` | design, vehicle, amount | Take units back aboard: every group of the design's kind, or only `design` from the group `vehicle`, up to `amount`. | `{"kind": "recover_units", "design": 21, "vehicle": 95, "amount": 10}` |
| `cloak` | | Switch the cloak on. | `{"kind": "cloak"}` |
| `decloak` | | Switch the cloak off. | `{"kind": "decloak"}` |
| `sweep_mines` | | Sweep the mines in the sector. | `{"kind": "sweep_mines"}` |
| `use_component` | amount | Use the component at position `amount` of the design. | `{"kind": "use_component", "amount": 3}` |
| `stellar_manipulation` | amount, object, location | The manipulation numbered `amount` in `stellar_action`, on a target. | `{"kind": "stellar_manipulation", "amount": 6, "location": {"system": 4, "x": 2, "y": 2}}` |
| `move_to_waypoint` | amount | Move to the waypoint in slot `amount`. | `{"kind": "move_to_waypoint", "amount": 2}` |
| `self_destruct` | | Destroy the whole vehicle or group. | `{"kind": "self_destruct"}` |
| `use_facility` | amount | A colony's: use the facility at position `amount`. | `{"kind": "use_facility", "amount": 0}` |
| `convert_resources` | amount, from_resource, to_resource | A colony's: convert `amount` (at most 65,000) of one resource into another. | `{"kind": "convert_resources", "amount": 5000, "from_resource": "organics", "to_resource": "minerals"}` |
| `scrap` | | Scrap the vehicle (the Scrap window's command). | `{"kind": "scrap"}` |
| `analyze` | | Take the vehicle apart to learn from it. | `{"kind": "analyze"}` |
| `mothball` | | Mothball the vehicle. | `{"kind": "mothball"}` |
| `unmothball` | | Bring it back into service. | `{"kind": "unmothball"}` |
| `retrofit` | design | Refit to the design. | `{"kind": "retrofit", "design": 18}` |
| `fire_on` | | Our armed ships destroy this vehicle. | `{"kind": "fire_on"}` |
| `seek` | location, vehicle, object | The computer's ministers' order: head for a sector, follow a vehicle or a planet, for one movement phase. | `{"kind": "seek", "vehicle": 77}` |
| `join_fleet` | amount | The ministers' order: follow the fleet whose id is `amount` and join it. | `{"kind": "join_fleet", "amount": 3}` |

Notes:
- Scrap, Analyze, Mothball, Unmothball, Retrofit and Fire On come from the
  Scrap window's commands (`scrap`, `analyze`, `mothball`, `retrofit`,
  `fire_on`), which put them on the list. A `set_orders` list may keep those
  the vehicle already holds but cannot add new ones, and `order_tagged` takes
  none of them.
- Use Facility and Convert Resources are colonies' orders only.
- Seek and Join Fleet are what the computer player's ministers give; a script
  AI may give them too.
- An expanded Colonize carries 1 in `amount`: its colonists were loaded by the
  Load Cargo in front of it.

### `stellar_action`

The manipulations a `stellar_manipulation` order's `amount` numbers, from 0.

| Value | Meaning |
|---|---|
| `create_planet` | 0: make a planet. |
| `destroy_planet` | 1: break a planet into an asteroid field. |
| `create_star` | 2: make a star. |
| `destroy_star` | 3: destroy a star and everything around it. |
| `open_warp_point` | 4: open a warp point to another system. |
| `close_warp_point` | 5: close a warp point. |
| `create_storm` | 6: make an ion storm. |
| `destroy_storm` | 7: end a storm. |
| `create_nebulae` | 8: fill the system with nebulae. |
| `destroy_nebulae` | 9: clear the nebulae. |
| `create_black_hole` | 10: turn the star into a black hole. |
| `destroy_black_hole` | 11: remove a black hole. |
| `create_constructed_planet` | 12: build a ringworld or sphereworld. |

## Tactical battles

A turn-based game can fight a battle tactically: a side the player drives
gets the orders below during its phases. They are checked by the battle
(`combat::TacticalBattle::check`) and refused with a reason, as the battle
window's commands are. Pieces, weapons and targets are positions in the
battle's piece list and in a piece's weapon list.

### `tactical_order`

| Field | Type | Meaning |
|---|---|---|
| `kind` | tactical_order_kind | What to do. |
| `empire` | empire id, or null | The side giving it: the empire whose phase it is. |
| `piece` | int | The piece acting (-1 by default: none). |
| `target` | int | The target piece (-1 by default: none). |
| `weapon` | int | The piece's weapon; -1 (the default) for every enabled weapon. |
| `instance` | int | Which unit of a stack fires; -1 (the default) for every ready one. |
| `x` | int | Where to move: the square's column (-1 by default). |
| `y` | int | ... and row (-1 by default). |
| `path` | list of square | Or the squares to move along. |
| `design` | design id, or null | The units to launch. |
| `count` | int | How many units to launch. |
| `group` | int | A combat group (0 to 9), a launch session, or a fighter group size. |
| `formation` | int | The formation a group leader takes (a formation index; -1 by default: none). |
| `on` | bool | Weapons on or off; Auto on or off (true by default). |
| `alone` | bool | A group leader moves without its group. |

### `square`

| Field | Type | Meaning |
|---|---|---|
| `x` | int | Column. |
| `y` | int | Row. |

### `tactical_order_kind`

| Value | Fields used | Meaning | Example |
|---|---|---|---|
| `move` | piece, x, y or path, alone | Move toward a square, or along a path of neighbouring squares. | `{"kind": "move", "piece": 2, "x": 10, "y": 4}` |
| `fire` | piece, target, weapon, instance | Fire at a piece. | `{"kind": "fire", "piece": 2, "target": 7, "weapon": -1}` |
| `toggle_weapon` | piece, weapon, on | Switch a weapon (or all) on or off. | `{"kind": "toggle_weapon", "piece": 2, "weapon": 0, "on": false}` |
| `launch` | piece, design, count, group | Launch units. | `{"kind": "launch", "piece": 2, "design": 21, "count": 6, "group": 1}` |
| `launch_fighters` | piece, design, count, group | Launch fighters in groups of `group` (5 to 50). | `{"kind": "launch_fighters", "piece": 2, "design": 22, "count": 20, "group": 10}` |
| `drop_troops` | piece | Land troops on the neighbouring enemy colony. | `{"kind": "drop_troops", "piece": 5}` |
| `ram` | piece, target | Ram a neighbouring piece. | `{"kind": "ram", "piece": 2, "target": 7}` |
| `capture` | piece, target | Board a neighbouring ship. | `{"kind": "capture", "piece": 2, "target": 7}` |
| `set_leader` | piece, group, formation | Lead a combat group in a formation. | `{"kind": "set_leader", "piece": 2, "group": 1, "formation": 0}` |
| `set_member` | piece, group | Join a combat group, taking the next place in its formation. | `{"kind": "set_member", "piece": 3, "group": 1}` |
| `clear_group` | piece | Leave its group. | `{"kind": "clear_group", "piece": 3}` |
| `clear_all_groups` | | Every piece of the side leaves its group. | `{"kind": "clear_all_groups"}` |
| `auto` | piece, on | The piece acts by its strategy now; with piece -1 the battle's Auto switch. | `{"kind": "auto", "piece": -1, "on": true}` |
| `auto_phase` | | The strategies play the rest of this phase. | `{"kind": "auto_phase"}` |
| `end_phase` | | End the side's phase. | `{"kind": "end_phase"}` |
| `resolve_combat` | | Every side follows its strategies to the end of the battle. | `{"kind": "resolve_combat"}` |

## Types the commands carry

### `location`

A sector of a system.

| Field | Type | Meaning |
|---|---|---|
| `system` | system id, or null | The system (null: no place). |
| `x` | int | Column, 0 to 12 left to right (6 by default). |
| `y` | int | Row, 0 to 12 top to bottom; (6, 6) is the centre (6 by default). |

### `sector`

A sector within a system.

| Field | Type | Meaning |
|---|---|---|
| `x` | int | Column, 0 to 12 (6 by default). |
| `y` | int | Row, 0 to 12 (6 by default). |

### `resources`

| Field | Type | Meaning |
|---|---|---|
| `minerals` | int | Minerals. |
| `organics` | int | Organics. |
| `radioactives` | int | Radioactives. |

### `queue_target`

A construction queue: a colony's (by its planet) or a vehicle's with a space
yard. Name one.

| Field | Type | Meaning |
|---|---|---|
| `planet` | object id, or null | The colony's planet. |
| `vehicle` | vehicle id, or null | Or the vehicle. |

### `queue_item`

| Field | Type | Meaning |
|---|---|---|
| `kind` | queue_item_kind | A vehicle (or units), a facility, or a facility upgrade. |
| `design` | design id, or null | The design built (vehicles). |
| `facility` | facility index | The facility built, or the upgrade's target (0 by default). |
| `count` | int | How many are built together (1 by default); an upgrade's count is fixed when it is queued. |
| `spent` | resources | What was paid into it so far (the engine keeps this; leave it out). |

### `queue_item_kind`

| Value | Meaning |
|---|---|
| `vehicle` | A ship, base or units of a design. |
| `facility` | A facility. |
| `upgrade` | The colony's facilities of a family brought up to the target facility. |

### `design`

A design as `create_design` and `edit_design` take it. Only `name`,
`design_type`, `hull`, `entries`, `strategy` and `picture` matter there; the
engine sets the rest.

| Field | Type | Meaning |
|---|---|---|
| `id` | design id, or null | Its id (set by the engine). |
| `owner` | empire id, or null | Its owner (set by the engine). |
| `name` | text | Its name, unique in the game. |
| `design_type` | text | One of `my.design_types`. |
| `hull` | hull index | The hull. |
| `entries` | list of design_entry | The components, in order. |
| `strategy` | strategy index | The combat strategy its vehicles use (a position in our strategies). |
| `obsolete` | bool | Obsolete. |
| `created_turn` | int | When it was made. |
| `template_name` | text | The computer designer's template it came from. |
| `retrofitted` | bool | A ship was refitted to it. |
| `ever_built` | bool | One was ever built. |
| `built` | int | Vehicles built. |
| `lost` | int | Vehicles lost. |
| `enemy_tonnage_destroyed` | int | Tonnage of enemies its vehicles destroyed. |
| `scrapped` | int | Vehicles scrapped. |
| `picture` | text | Its own picture: a base name looked up as a hull's bitmap names are (`Mini_<picture>`, `Portrait_<picture>`), in place of its hull's; empty for the hull's. At most 64 characters, no folders ([packages-and-data.md](packages-and-data.md#a-designs-own-picture)). |

### `design_entry`

| Field | Type | Meaning |
|---|---|---|
| `component` | component index | The component. |
| `mount` | int | A weapon mount of the rules view, or -1 (the default) for none. |

### `research_project`

| Field | Type | Meaning |
|---|---|---|
| `area` | tech index, or null | The tech area. |
| `progress` | int | Points toward its next level (the engine keeps it). |

### `intel_project_order`

| Field | Type | Meaning |
|---|---|---|
| `project` | intel project index | The project. |
| `target` | empire id, or null | The empire it is aimed at (none for defense). |
| `target_planet` | object ref, or null | A planet it aims at; null for any. |
| `target_vehicle` | vehicle ref, or null | A vehicle it aims at. |
| `third_empire` | empire id, or null | The other empire of a political operation. |
| `target_tech` | tech index, or null | The area a theft aims at; null for any. |
| `progress` | int | Points so far (the engine keeps it). |

### `message`

A diplomatic message: what `send_message` sends and the view's `messages`
hold.

| Field | Type | Meaning |
|---|---|---|
| `id` | message id, or null | Its id (set by the engine). |
| `from_empire` | empire id, or null | The sender (set by the engine). |
| `to_empire` | empire id, or null | The recipient. |
| `sent_turn` | int | When it was sent. |
| `type` | message_type | What kind of message. |
| `tone` | int | 0 pleading, 1 neutral (the default), 2 demanding. |
| `text` | text | What it says. |
| `treaty` | treaty | The treaty proposed, accepted or broken (none by default). |
| `offer` | list of package_item | What the sender gives. |
| `request` | list of package_item | What the sender asks for. |
| `third_empire` | empire id, or null | The empire a request is about. |
| `system` | system id, or null | The system a demand or request names. |
| `planet` | object ref, or null | The planet a demand or request names. |
| `in_reply_to` | message ref, or null | The message it answers. |
| `delivered` | bool | It reached the recipient. |
| `answered` | bool | It was answered. |
| `dated` | int | The turn of its entry in the recipient's log. |

### `package_item`

One thing given or asked for in a trade, gift or tribute.

| Field | Type | Meaning |
|---|---|---|
| `kind` | package_item_kind | What it is. |
| `resources` | resources | Resources. |
| `tech` | tech index, or null | A tech area (null: any). |
| `planet` | object ref, or null | A planet. |
| `vehicle` | vehicle ref, or null | A vehicle. |
| `system` | system id, or null | A system. |
| `treaty` | treaty | A treaty (none by default). |
| `empire` | empire id, or null | The empire of a communication channel. |

### `waypoint`

| Field | Type | Meaning |
|---|---|---|
| `name` | text | Its name. |
| `location` | location | Where it is. |
| `set` | bool | The slot holds a waypoint. |

### `strategy`

A combat strategy: its name and its settings, as the classic data writes them.

| Field | Type | Meaning |
|---|---|---|
| `name` | text | Its name. |
| `settings` | list of strategy_setting | Its settings, in order. |

### `strategy_setting`

| Field | Type | Meaning |
|---|---|---|
| `name` | text | The setting's name, as the data files write it. |
| `value` | text | Its value, as written. |

### `population_group`

| Field | Type | Meaning |
|---|---|---|
| `race` | empire id | The empire whose race it is. |
| `millions` | int | Millions of people. |

### `unit_stack`

| Field | Type | Meaning |
|---|---|---|
| `design` | design id, or null | The units' design. |
| `count` | int | How many. |

### `interface_options`

The Empire Options switches and what the windows remember. Only
`auto_claim_colonized` changes the game. Sort keys are five column numbers
plus one, newest first, 0 for none.

| Field | Type | Meaning |
|---|---|---|
| `show_log_at_turn_start` | bool | Open the log when a turn starts (true by default). |
| `confirm_end_turn` | bool | Ask before ending the turn (true by default). |
| `confirm_scrap` | bool | Ask before scrapping (true by default). |
| `confirm_stellar_manipulation` | bool | Ask before a stellar manipulation (true by default). |
| `confirm_delete_research` | bool | Ask before removing research (true by default). |
| `confirm_delete_intel` | bool | Ask before removing intelligence projects (true by default). |
| `confirm_delete_first_queue_item` | bool | Ask before removing the item being built (true by default). |
| `note_similar_abilities` | bool | Note similar abilities in reports (true by default). |
| `skip_under_construction` | bool | Next and Previous skip vehicles being built. |
| `skip_damaged` | bool | ... skip damaged vehicles. |
| `stop_once_per_location` | bool | ... stop once per place. |
| `skip_in_fleets` | bool | ... skip fleet members. |
| `warp_point_names` | bool | System map: warp point names (true by default). |
| `planet_names` | bool | System map: planet names. |
| `colonizable_markers` | bool | System map: colonizable markers (true by default). |
| `system_grid` | bool | System map: the grid. |
| `coordinate_location` | bool | System map: coordinates (true by default). |
| `facility_markers` | int | Facility marker groups shown (bit i: group i). |
| `galaxy_grid_lines` | bool | Galaxy map: grid lines (true by default). |
| `galaxy_warp_lines` | bool | Galaxy map: warp lines (true by default). |
| `latest_construction_only` | bool | Construction lists: only the latest items. |
| `latest_components_only` | bool | Designer: only the latest components. |
| `auto_claim_colonized` | bool | A system we colonize is claimed (true by default). |
| `log_filter` | int | The log's filter (0 all, else a category + 1). |
| `log_position` | int | The selected log entry. |
| `log_scroll` | int | The log's scroll position. |
| `planets_tab` | int | The Planets window's tab. |
| `planets_no_sys_to_avoid` | bool | The Planets window hides systems to avoid. |
| `queues_tab` | int | The Construction Queues window's tab. |
| `queues_shown` | int | Its toggles (bits; 15 by default). |
| `simulator_no_obsolete` | bool | The combat simulator hides obsolete designs. |
| `ships_tab` | int | The Ships window's tab. |
| `ships_shown` | int | Its Show Ships, Units and Fleets toggles (bits; 7 by default). |
| `planets_sort` | list of int | The Planets window's sort keys (five; [0, 0, 0, 0, 0] by default). |
| `colonies_sort` | list of int | The Colonies window's sort keys (five; [0, 0, 0, 0, 0] by default). |
| `ships_sort` | list of int | The Ships window's sort keys (five; [0, 0, 0, 0, 0] by default). |
| `queues_sort` | list of int | The Construction Queues window's sort keys (five; [0, 0, 0, 0, 0] by default). |
| `replay_animate` | bool | Combat replays: animate (true by default). |
| `replay_fast` | bool | Combat replays: fast. |
| `replay_view_rect` | bool | Combat replays: the view rectangle (true by default). |
| `replay_grid` | bool | Combat replays: the grid. |
| `design_to_hit` | bool | The designer shows to-hit modifiers. |
| `design_condensed` | bool | The designer's condensed view. |
| `designs_hide_obsolete` | bool | The Designs window hides obsolete designs. |
| `designs_stats_view` | bool | The Designs window shows statistics. |

## Enumerations the commands use

### `resource`

| Value | Meaning |
|---|---|
| `minerals` | Minerals. |
| `organics` | Organics. |
| `radioactives` | Radioactives. |

### `treaty`

From worst to best.

| Value | Meaning |
|---|---|
| `war` | At war. |
| `non_intercourse` | No dealings. |
| `none` | No treaty. |
| `non_aggression` | Non-aggression: no fighting on contact. |
| `subjugation` | One side is the other's subject. |
| `protectorate` | One side protects the other. |
| `trade_alliance` | Resource trade. |
| `trade_research_alliance` | Resource and research trade. |
| `military_alliance` | Trade, research and resupply at each other's depots. |
| `partnership` | All of the above and shared sight. |

### `message_type`

| Value | Meaning |
|---|---|
| `general` | Words only. |
| `propose_treaty` | Proposes `treaty`. |
| `accept_treaty` | Accepts a proposed treaty. |
| `refuse_treaty` | Refuses it. |
| `counter_treaty` | Answers with another treaty. |
| `break_treaty` | Ends the treaty. |
| `declare_war` | Declares war. |
| `propose_trade` | Proposes a trade of the offered for the requested. |
| `accept_trade` | Accepts a trade. |
| `refuse_trade` | Refuses it. |
| `counter_trade` | Answers with another trade. |
| `gift` | Gives the offered things. |
| `tribute` | Pays tribute. |
| `accept_gift` | Accepts a gift. |
| `refuse_gift` | Refuses it. |
| `surrender` | The sender surrenders everything to the recipient. |
| `grant_independence` | Frees a subject. |
| `demand_gift` | Demands a gift. |
| `demand_tribute` | Demands tribute. |
| `demand_surrender` | Demands surrender. |
| `demand_remove_ships` | Demands that ships leave `system`. |
| `demand_remove_colonies` | Demands that colonies in `system` go. |
| `demand_leave_planet` | Demands that `planet` be left. |
| `request_stop_hostilities` | Asks to stop hostile acts against `third_empire`. |
| `request_break_treaty` | Asks to break with `third_empire`. |
| `request_declare_war` | Asks to declare war on `third_empire`. |
| `request_make_peace` | Asks to make peace with `third_empire`. |
| `request_support` | Asks for support against `third_empire`. |
| `request_attack_empire` | Asks to attack `third_empire` in `system`. |
| `request_attack_planet` | Asks to attack `planet`. |
| `demand_stop_espionage` | Demands an end to espionage. |
| `demand_stop_sabotage` | Demands an end to sabotage. |
| `demand_stop_attacks` | Demands an end to attacks in `system`. |
| `accept_demand` | Accepts a demand or request. |
| `refuse_demand` | Refuses it. |

### `package_item_kind`

| Value | Meaning |
|---|---|
| `resources` | Resources. |
| `technology` | A tech level. |
| `planet` | A planet with its colony. |
| `vehicle` | A vehicle. |
| `star_chart` | Our maps. |
| `treaty` | A treaty. |
| `comm_channel` | Contact with another empire. |
| `system` | A system's charts. |

### `demand_list`

| Value | Meaning |
|---|---|
| `war` | Empires we agreed to make war on. |
| `break` | Empires we agreed to break with. |
| `peace` | Empires we agreed to make peace with. |

### `encounter_clear`

| Value | Meaning |
|---|---|
| `never` | Meeting others never clears orders. |
| `enemy` | Meeting an enemy after a warp clears the group's orders. |
| `any` | Meeting any other empire does. |

### `minister`

The minister areas, in the classic game's order: the first eleven run the
whole empire, the rest its vehicles and colonies one by one.

| Value | Meaning |
|---|---|
| `design` | Designs. |
| `ship_construction` | Building ships. |
| `expenses` | Spending. |
| `production_output` | Production. |
| `research` | Research. |
| `intelligence` | Intelligence. |
| `politics` | Diplomacy. |
| `repair` | Repairs. |
| `resupply` | Resupply. |
| `scrap` | Scrapping. |
| `retrofit` | Refits. |
| `facility_construction` | Building facilities. |
| `transports` | Transports. |
| `carriers` | Carriers. |
| `colonization` | Colony ships. |
| `attack` | Attack ships. |
| `defense` | Defense. |
| `exploration` | Exploration. |
| `patrol` | Patrols. |
| `mines_satellites_drones` | Mines, satellites and drones. |
| `fleets` | Fleets. |
| `stellar_manipulation` | Stellar manipulation. |
| `ship_cloaking` | Cloaking. |
| `space_yard_ships` | Space yard ships. |
| `troops` | Troops. |

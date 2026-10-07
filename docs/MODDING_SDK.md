# Modding SDK: outline

Status: being built (from 2026-10-05). Section 14 records the decisions the
implementation follows; where it differs from the outline, section 14 wins. This fills in
the third goal under "Future goals" in [PARITY_PLAN.md](PARITY_PLAN.md).

The SDK aims to let players:
- **write their own computer players**, with full control: everything a human player can
  do, every decision the built-in AI makes, and memory of their own;
- **add, remove and change units**, from hulls, components and facilities to designs and
  races, together with their pictures and sounds;
- **change the rules and the game around them** far beyond what the original's data files
  allow: new abilities, events, victory conditions, galaxy generators, game options,
  orders and interface panels.

The original's data and AI files can still be modded with a text editor, as they always
could. The SDK adds a package format, data patches, Python scripts and tools on top.

---

## 1. Principles

1. **The classic game stays as it is.** With no mods, OpenSE4 plays exactly as it does now;
   the golden determinism checksums prove it. Mods are chosen per game.
2. **The original stays required.** Mods are layered over the player's own install and
   never change it. OpenSE4 still needs that install, even with a mod that replaces every
   picture and data file. Players buy the original.
3. **The same game on every computer.**
   - Data patches, rules scripts and in-game AIs decide how a turn resolves, so they are
     part of the game's identity: every player in a game has exactly the same ones.
   - Rules scripts resolve the same way on every platform.
   - Every AI decision is recorded with the turn, so a turn can be replayed without
     running the AI again.
4. **Mods use published interfaces.** Mods change the game through:
   - data patches;
   - the player commands;
   - named hooks;
   - an effects API that keeps the game state valid.

   They never touch engine internals. The interface carries a version number (`api = 1`),
   and a mod names the version it needs.
5. **Scripts are untrusted.** Workshop mods come from strangers, so their scripts run in a
   sandbox with no access to files, the network, the clock or other programs. Mods contain
   no native code.
6. **The built-in AI and rules are a library.** A mod can call the built-in Research
   minister and keep everything else, or start from the classic combat rules and change
   one step. Replacing everything is possible, not required.

## 2. Tiers

Each tier works without the ones after it, and each can ship on its own.

| Tier | What a mod can do | Code? | Affects the game's identity? |
|---|---|---|---|
| 0 Assets | Pictures, sounds, music, fonts, text | No | No (cosmetic) |
| 1 Data | Add, change or remove records in the classic data files; the AI's data tables; starting designs | No (optional Python generators) | Yes |
| 2 AI | Python computer players with full control | Yes | Yes, when run inside the game |
| 3 Rules | Python hooks in turn processing; new abilities, events, orders, options, victory conditions, galaxy generators | Yes | Yes |
| 4 Interface | New panels and reports, extra columns, key bindings | Yes | No |

AI comes before rules because the engine already has the right shape for it. A computer
player's planner is a function from the game to a list of commands, and every command
goes through `game::apply`. Rules hooks need more engine work (sections 6 and 7).

## 3. Mod packages

A mod is a folder or a `.zip` with a manifest:

```text
better-carriers/
  mod.toml          the manifest
  data/             data patches (*.toml), replacement classic files (*.txt), generators (*.py)
  assets/           Pictures/, Sounds/, Music/, Fonts/, in the install's own layout
  ai/               computer players (Python)
  scripts/          rules hooks (Python)
  ui/               interface extensions
  text/             strings and translations
  tests/            the mod's own tests, run by `opense4-sdk test`
```

```toml
[mod]
id = "example.better-carriers"      # unique; reverse-domain style
name = "Better Carriers"
version = "1.2.0"
api = 1                             # the SDK interface version it needs
authors = ["..."]
description = "..."

[requires]
"example.common-lib" = ">=1.0"

[load]
after = ["example.common-lib"]      # load order hints; the user can reorder
```

- **Load order:** the install, then each enabled mod in order, then the game's own
  settings. A later mod wins.
- **Identity:** the manifest, plus a hash of every file except `assets/`, `ui/` and
  `text/`, gives the mod's identity. A game's mod set (id, version, hash) is saved with the game, sent in
  the lobby and checked when a player joins, next to today's data-set identity
  (`game::dataSetIdentity`).
- **Classic mods** (a folder of replacement data files and pictures) load as a package
  without a manifest, so existing SE4 mods keep working.

## 4. Assets (tier 0)

**Engine work: a layered file system.** `assets::InstallFiles` indexes one folder, and
only fonts and pointers honour `Path.txt`'s mod folder. It becomes a stack of layers: the
install first, then each mod's `assets/`. Every lookup goes through the stack: pictures,
sounds, music, fonts and pointers. Nothing is ever written into the install.

**What a mod can add or replace:**
- **Ship and unit pictures:** a hull names its pictures (`Primary Bitmap Name`,
  `Alternate Bitmap Name`), and the client looks for `Mini_<name>` and `Portrait_<name>`
  in the race's style folder, then the shared folders. A new hull with new pictures is a
  data record plus two files; a new race style is a folder.
- **Component, facility and planet pictures** (`Pic Num` into the sheets, or a picture
  of its own), event pictures, flags, race portraits.
- **Sounds:** weapon sounds are named by the weapon; the interface sounds get names a
  mod can replace.
- **Music:** playlists in a mod's settings. Today music comes only from the base
  `Music/` folder.

**Beyond the original's formats:**
- PNG with real transparency, next to BMP with black as transparent. stb already decodes
  PNG; callers just stop asking for `.bmp` by name.
- Larger pictures for sharper screens, scaled into the classic layout (for example a
  portrait at twice the size).
- OGG music and sounds, and WAV at any rate.
- A design may name its own picture, not only its hull's.

**Checks:** `opense4-sdk check` reports missing pictures, wrong sizes, unsupported
formats and files no record uses.

## 5. Data (tier 1)

**Patches, not whole files.** Replacing a whole `Components.txt` means two mods can
never be combined. Data patches name records and fields, so several mods apply one after
another:

```toml
# data/carriers.toml
[[components.add]]
name = "Heavy Fighter Bay"
copy_from = "<an existing fighter bay>"   # start from an existing record
set = { "Tonnage Space Taken" = 40, "Supply Amount Used" = 2 }

[[components.change]]
name = "Ion Engine I"
set = { "Supply Amount Used" = 3 }

[[components.remove]]
name = "<a component the mod retires>"

[[vehicle_sizes.add]]                  # a new hull
name = "Escort Carrier"
copy_from = "<an existing hull>"
set = { "Primary Bitmap Name" = "EscortCarrier", "Tonnage" = 350 }
```

- **Field names** are the data files' own, as `opense4-datacheck` already reports them.
  Lists such as abilities and requirements have `add` and `remove` of their own.
- **Every table:** components, facilities, hulls, techs, races and racial traits,
  cultures, planets and systems, quadrants, events, intel projects, formations, combat
  strategies, settings, the name lists, starting designs, and the AI's data tables (anger,
  politics, research, design templates, construction, colony types).
- **Removing a record checks its references.** A removed component still named by a
  design, a tech or an AI table is reported with the file and line of each reference.
  The mod then removes those too, or asks for `cascade = true`.
- **Python generators** (`data/*.py`) can build records programmatically, for example
  twelve levels of a new weapon line. They run once when the game loads, in the sandbox,
  and their output is an ordinary patch: it is hashed into the identity and can be
  printed with `opense4-sdk dump`.
- **Diagnostics** keep today's quality: every error names the mod, file, line and record,
  and unread fields are reported.

**"Units"** here means every vehicle type the game has: ships, bases, fighters, troops,
mines, satellites, drones and weapon platforms, with their hulls, components and designs. New hull sizes,
components and designs are plain data. A new vehicle *type* is not: the eight types are
built into the rules everywhere (question 6).

**Engine work:**
- **Patches:** the ruleset loader applies patches after reading the classic files, and
  records where each value came from.
- **New ability names:** abilities are a closed list today (`OPENSE4_ABILITIES`), and an
  unknown name is a load error. A mod may declare new ability names with how values
  combine (sum, highest, lowest), so data can carry them and scripts can read them
  (tier 3 gives them effects). Unknown names that no mod declares stay an error.
- **Identity:** `dataSetIdentity` also covers the AI's data tables. Today it misses the
  `Ai/` folder and the race AI files, which already decide how computer players behave.
  That gap is worth closing even before the SDK.

## 6. Computer players (tier 2)

### 6.1 What "full control" means

A script AI controls an empire completely:
- **Every command a human has:** all 54 kinds of `cmd::Command`, among them orders, fleets,
  construction queues, designs, research, intelligence, diplomacy, empire settings and
  the Scrap window. Also the AI-only orders such as Seek.
- **Every decision the built-in AI makes**, each as a callback:

| Decision | Today, in the engine | Callback |
|---|---|---|
| Diplomacy before messages are delivered | `ai::planPoliticsOrders` | `politics(view, orders)` |
| Orders: movement, colonizing, attack, defence, exploration, fleets, supply, scrapping, retrofits | `ai::planOrdersAfterPolitics` | `orders(view, orders)` |
| Economy: designs, research, intelligence, construction | `ai::planEconomyStep` (end of turn) | `economy(view, orders)` |
| A new colony's type | `ai::colonyTypeAtColonization` | `colony_type(view, colony)` |
| Entering a sector with enemies | computer groups always enter | `enter_sector(view, question)` |
| Space combat, round by round: movement, targets, launching, boarding, ramming | the combat strategies (`combat::Strategy`) | `battle_round(battle, orders)`, or keep strategies |
| Decloaking for orders and attacks | the Ship Cloaking minister inside movement | `decloak(view, vehicle, reason)` |
| Its own memory and mood | `aiMemory`, `aiState`, anger (`updateAiState`, `politicalStep`) | `self.memory`, owned by the script |

- **Its own memory:** a script AI keeps whatever it likes in `self.memory`. It is saved with
  the game, counts in the checksums and has a size limit. The built-in AI's own
  bookkeeping (state machine, anger, counters, lists) keeps running for an empire a script
  controls, so the built-in ministers it calls on see what they would see for a computer
  empire; a script that makes every decision itself can turn it off (`classic_state =
  false`). What the built-in AI writes into an empire directly (claims, movement options)
  is the script's to give, as commands of the classic answers. Rules that apply to every
  computer player still do, such as difficulty bonuses.
- **Optional parts:** any callback left out falls back to the built-in AI for that decision;
  a script that overrides nothing plays exactly the built-in AI's game.

### 6.2 The API

```python
from opense4 import ai, cmd, order


class Admiral(ai.Player):
    """A computer player that keeps the classic economy and plays its own war."""

    def economy(self, view, orders):
        orders.extend(ai.builtin.economy(view))             # the classic ministers

    def orders(self, view, orders):
        for fleet in view.my.fleets:
            target = self.pick_target(view, fleet)
            if target is not None:
                orders.add(cmd.give(fleet, [order.attack(vehicle=target)]))
        # anything else: the built-in Defense, Exploration, Supply... ministers
        orders.extend(ai.builtin.orders(view, skip=["attack"]))

    def pick_target(self, view, fleet):
        return view.galaxy.nearest(fleet, view.enemy_vehicles)   # the fewest jumps away

    def colony_type(self, view, question):
        return "Research" if "Research" in question.choices else None

    def battle_round(self, battle, orders):
        for piece in battle.my_pieces:
            target = battle.weakest_enemy_in_range(piece)
            if target is not None:
                orders.fire(piece, target)
```

The outline's sketch, completed with its `pick_target`, is a working player of the package
as built: the SDK's tests play it, as they play every complete example of the docs
(`tests/sdk/python/check_doc_snippets.py`). [docs/sdk/python-api.md](sdk/python-api.md)
describes the package and [docs/sdk/ai-protocol.md](sdk/ai-protocol.md) the messages
between it and the engine.

- **The view:**
  - `view` is the empire's own knowledge, made by `game::redactForEmpire`: what a human
    player of that empire would see, as objects with typed fields.
  - Queries: paths and jump counts, supply range, a design's figures, what a component
    does.
  - Forecasts: what a queue will finish, what research costs.
- **A whole-galaxy view:** the original's AI deliberately reads more than it can see, such as
  every empire's strength. A game option "computer players see everything" gives script
  AIs the whole state too. It is off by default and shown in the lobby, so games between
  AIs can be fair.
- **Commands** are checked by `game::apply` as for a human. A refused command comes back
  with the engine's reason, and the AI may try something else in the same call.
- **The built-in AI as a library:** `ai.builtin` exposes the ministers one by one
  (Research, Design, Colonization, Attack and the rest) and the AI's data tables. A mod
  can take their commands, filter them, or change their tables, and doesn't start from
  nothing.
- **Notes:** `self.note(object, text)` attaches notes the client shows on the map and in
  the reports in its AI notes view.

### 6.3 Where the AI runs

| | In the game | External bot |
|---|---|---|
| How | Inside OpenSE4, in the sandbox | A separate Python program that connects to the game |
| For | Mods and the Workshop; playing against it | Research, machine learning, heavy computation |
| Libraries | The standard library (and what the runtime can offer, question 1) | Any (numpy, torch, …) |
| Limits | Time and memory budget per turn; on overrun or error the built-in AI takes over for that turn and the log says why | None; the host's turn timer applies |
| Network games | Runs on the host only | Joins as a player, like `opense4-server bot` today |

- **Same API in both places:** the same `opense4.ai` package runs inside the game and as
  an external bot, so an AI can be developed outside and shipped inside.
- **Decision journal:** every answer an AI gives during a turn (colony types, battle
  rounds, questions) is recorded with the turn's orders. Replays, play by e-mail and
  desync repair use the journal and never run an AI twice. That is why an external bot
  need not be deterministic.

### 6.4 Tools for serious AIs

- **The arena:**

  ```sh
  opense4-sdk arena --ai mine.py --ai builtin --games 200 --turns 150
  ```

  It plays headless games in parallel (the engine has no window and already plays turns
  without one) and reports wins, scores, colonies, battles and research over time, with
  the seeds to replay any game.
- **A step-by-step environment** (`opense4.env`) for training machine-learning players:
  - reset with a seed;
  - each step takes a turn's commands and returns the next view and the score.
- **Replays and logs:** a game played in the arena opens in the client, with the AI's notes
  and its decision journal.
- **Tests:** `opense4-sdk test` runs the mod's own tests against fixed seeds.
- **As built:** [docs/sdk/bots-and-arena.md](sdk/bots-and-arena.md) describes external
  bots, the arena, the training environment, `opense4-sdk test` and `run`.

### 6.5 Engine work

- **A controller per empire:** built-in, script (mod and class), or external. Every place
  listed in 6.1 asks the controller:
  - steps 4 and 6 of the simultaneous turn (`turn.cpp`);
  - `startPlayerTurn` and `computerTurn` (`turn_based.cpp`);
  - `colonyTypeAtColonization`;
  - the cloaking calls in `movement.cpp`.
- **A round hook** in `Battle::act` and `Battle::move` for `battle_round`. Without the
  callback, the strategies decide, as now.
- **State:** an `Empire` field for the script's memory and controller, listed in `io()`,
  in a new save format.
- **The decision journal,** stored with the turn's orders.
- **A local connection for external bots** that skips the network encryption on the same
  computer, plus Python bindings for the existing protocol for remote ones.
- **As built (S3, engine side):** [docs/sdk/ai-protocol.md](sdk/ai-protocol.md) describes
  the controllers, sessions, every call and when it comes, the services and their costs,
  the budgets and failures, the journal (kept with the game in memory, not saved), and
  which of the built-in AI's own steps run for an empire a player plays (its bookkeeping,
  unless `classic_state = false`) and which become commands of its classic answers.
- **As built (S3, client side):** when the game's mods offer players, Game Setup (its
  Players and Game Settings pages), Empire Setup, Quick Start and the network lobby choose
  who plays each computer empire, whether they see everything, and their limits
  ([docs/SETUP.md](SETUP.md) "Computer players", [docs/MULTIPLAYER.md](MULTIPLAYER.md));
  without such mods the screens are the original's. Battles shown in a window fight the
  script sides through their players and carry their answers into the turn
  (ai-protocol.md §8). The AI notes view (Settings → Modding, `Ctrl+Shift+N`) shows the
  players' notes on the maps and in the reports, and the host's main window tells of a
  player that failed, with its traceback ([python-api.md](sdk/python-api.md) "Watching a
  player think", "When a player fails").

## 7. Rules (tier 3)

### 7.1 Hooks

Hooks follow the turn order in `docs/ENGINE.md`. A rules script registers for the ones
it needs:

```python
from opense4 import rules

@rules.on("colony_end_of_turn")
def overcrowding(game, colony, fx):
    if colony.population > colony.planet.max_population * 9 // 10:
        fx.change_happiness(colony, -1)
        fx.log(colony.owner, f"{colony.name} is overcrowded")
```

| Stage | Hooks |
|---|---|
| Setup | `new_game` (options, empires), `generate_galaxy` (replace or adjust), `after_galaxy` |
| Turn start | `turn_start`, `orders_applied` |
| Movement and combat | `movement_day`, `vehicle_entered_sector`, `before_battle`, `after_battle`, `vehicle_destroyed` |
| End of turn | `empire_end_of_turn` around each step: income, maintenance, research, intelligence, construction, population, happiness, repair, supply, ground combat; and `colony_end_of_turn` |
| Events | `colony_founded`, `vehicle_built`, `tech_researched`, `treaty_changed`, `message_sent`, `event_fired` |
| Turn end | `check_victory`, `turn_end` |

### 7.2 Changing the game

- **Through effects:** hooks change the game only through `fx`, the effects API. It is a
  set of engine functions that keep the state valid:
  - resources and population;
  - damage, repair and supply;
  - creating and removing vehicles and facilities;
  - treaties, log entries and events.

  Reading uses the same typed objects as the AI view, but sees the whole state.
- **A mod's own state:** a mod can keep data on the game, an empire, a colony or a vehicle
  (`game.mod_data`). It is saved, checksummed and limited in size.
- **New abilities:** a mod declares an ability name (section 5) and gives it an effect in a
  hook. Components, facilities, hulls and systems can then carry it like any other.
- **New orders:** a mod declares an order with its arguments, its check and its effect. It
  travels as one new command kind (`cmd::ModCommand`), so it works over the network, by
  e-mail and in replays. Script AIs can give it too. Tier 4 gives it a button.
- **New events and intelligence projects:** declared with their chances and an effect
  hook, next to the classic ones.
- **Victory conditions and scenarios:**
  - `check_victory` can end the game.
  - Scenarios combine a setup with objectives written in the lessons' `when` condition
    language (docs/LEARNING.md) and hooks for their actions.
- **Game options:** declared in the manifest with type, range and default. They are shown
  in the setup screen and the lobby, and saved with the game.

### 7.3 Determinism

Rules scripts are part of the rules, so they must resolve every turn identically on every
computer, as the engine itself does. The sandbox enforces it:
- **Random numbers** only from `game.rng`, the engine's generator. Python's `random`
  module is not available.
- **Whole numbers** in everything written back to the game; the effects API refuses
  floats. Calculations inside a script may use floats; question 1 covers what makes them
  repeatable.
- **No clock, files, threads or network.** Hash randomisation is fixed, so set order is
  the same everywhere.
- **Golden tests:** `opense4-sdk test` plays a mod's games twice and on both the native
  and the 32-bit build, and compares checksums.

### 7.4 As built (S4)

[docs/sdk/rules.md](sdk/rules.md) describes the rules tier as built:

- **Hooks**: the table of 7.1, each with its arguments and timing in both turn styles
  (turn-based games: `turn_start` once per game turn, `orders_applied` at the end of each
  player's turn, `movement_day` after each live run), and `empire_end_of_turn` around ten
  steps, narrowed by step and side. Moments run where the engine reaches them; events are
  delivered at the next safe point, so no script runs inside the engine's loops.
- **Reading**: `game` is the whole view (view.md) read a part at a time, with the mod's
  options, the game's random numbers, ability values and queries.
- **Effects**: resources, research and intelligence points, tech levels, population,
  happiness, colony type and plague; damage, repair and supply; vehicles created and
  removed; facilities; treaties; Log entries; mod events; planets, names and warp links;
  the galaxy replaced at generation; options at a new game; the end of the game.
- **Mod data** on the game, empires, colonies and vehicles (save format 9), and what
  players' computers and computer players may see of it.
- **Abilities** declared by mods get their effects from hooks; computer players read
  them in the rules view.
- **Orders** (`cmd::ModCommand`), **events**, **intelligence project types**, **game
  options** and **victory conditions**, declared in mod.toml's `[rules]`; **scenarios**
  in `scenarios/*.toml` with objectives in the lessons' condition language; **data
  generators** in `data/*.py`.
- **Budgets and failures**: per call and per game turn (game options); a failing function
  is skipped for the turn, and three failures turn a mod's rules off for the turn.
- **Not yet**: hooks inside a combat round (question 4 stays open: battles have hooks
  around them only). The buttons for mod orders, the mods' options in the setup screens
  and the lobby, and starting scenarios came with the interface tier (section 8).

## 8. Interface (tier 4)

Later and smaller:
- **Panels and reports:** a mod adds a panel to a report, a column to a list, a page to the
  Empires window, or a button for its own order.
- **Text:** strings and translations.
- **Key bindings.**

The interface is drawn by Dear ImGui. A small declarative layout (a TOML or Python
description of rows, labels, values and buttons) keeps mods working across interface
changes better than raw drawing calls would. Interface code runs on each player's own
computer and never changes the game except through commands.

**As built (S5):** [docs/sdk/interface.md](sdk/interface.md) describes the interface tier:

- `ui/*.toml` declares how the mod's orders show (a picture, questions and choices for
  their arguments, a key), report panels (ship, fleet, planet, colony, system), list columns
  (Ships\Units, Planets, Colonies, Designs), Empires pages and buttons that give orders;
  values come from the player's own view (a field, the mod's data, an ability) or from
  Python in `ui/*.py` (`opense4.ui`), worked out once per game state, in the sandbox, with
  small budgets; a failing one shows an error box in place of its panel.
- Mod orders are given from the order strip's free place (Mod Orders), from panels'
  buttons and with keys, their arguments asked one after another (numbers, text, choices,
  picks on the map), as commands.
- `text/<language>.toml` gives the mod's names in other languages (Settings → Modding chooses
  one; English and the mod's own words are the fallbacks).
- Keys the mods suggest join the Settings' Controls page; a suggestion that another binding
  has is left unbound and said, never taken.
- The setup screens and the lobby set the mods' game options; the Learn window starts the
  mods' scenarios, and a server's setup file names one; the lobby's host sets the computer
  players' limits.
- Without such mods every window is the original's; `ui/` and `text/` are outside a mod's
  identity.

## 9. Multiplayer, saves and the original's saves

- **Saved games** record the mod set and all mod state. A game whose mods are missing
  doesn't load; the error names what is missing. Asset-only mods may be missing; the
  game then shows the original's pictures.
- **Network and e-mail games:**
  - The host's mod set must match every player's, except asset and interface mods.
  - With the Workshop, a joining player is offered the missing mods.
  - Script AIs run on the host.
- **Export to the original** stays possible while every change is something the original
  understands (data within its format). It is refused, with the reason, when a game uses
  new abilities, scripts or mod state.

## 10. Security

- **Sandboxed scripts:** scripts from mods run in a sandbox: no files, no network, no
  clock, no processes, no native modules. There are limits on memory and time per call
  and per turn. Running out ends that script's turn safely.
- **Code:** mods carry no native code (no DLLs or shared libraries).
- **External bots** are ordinary programs the player starts on purpose. They are outside
  the sandbox and are never installed or started by a mod.
- **Package checks:** the client shows what a mod contains (assets, data, AI, rules,
  interface) before enabling it.

## 11. The SDK itself

- **`opense4-sdk`**, one command-line tool:
  - `new` (templates for an asset, data, AI or rules mod);
  - `check` (data, references, assets, scripts, API version);
  - `run` (start the game with the mod);
  - `test`;
  - `arena`;
  - `dump` (the patched data set);
  - `pack`;
  - `publish` (with the Workshop).
- **The `opense4` Python package:** type stubs for editors, and the same API in the
  game and for external bots.
- **Documentation:**
  - a modder's guide to the data files, written in our own words from `docs/spec/`;
  - the API reference, generated from the stubs;
  - tutorials: first picture mod, first new unit, first AI, first rules hook.
- **Example mods:**
  - a new hull with its pictures;
  - a weapon line from a generator;
  - a new ability with its effect;
  - a scenario;
  - "the classic AI with my own research";
  - a complete small AI.
- **A mod manager in the client:** enable, order, inspect, and per game.
- **As built (2026-10-05):**
  - the documentation: the modder's guide, its tutorials and the reference pages, indexed in
    [docs/sdk/README.md](sdk/README.md); the guide to the data files has a chapter per table
    group, written from `docs/spec/` and the loader; the API reference
    ([docs/sdk/reference](sdk/reference/README.md)) is generated from `python/opense4` by
    `tools/gen_sdk_reference.py`, and the SDK's tests fail when it is out of date;
  - the example mods in [mods/examples](../mods/examples/README.md): `new-hull` (a hull with
    its pictures and a component), `balance` (data patches with `cascade`),
    `classic-ai-research` (the classic AI with its own research), `small-ai` (a complete
    small AI), `weapon-line` (a generator), `new-ability` (a declared ability and its hook) and
    `scenario`; each has tests, and CI runs `check` and `test` on each on the test fixtures
    (`tests/sdk/test_sdk_guide.cpp`); `opense4-sdk new --from-example` copies one, and the
    release packages carry the documentation and the examples in `sdk/` beside
    `opense4-sdk`;
  - `new`, `check`, `info`, `dump` and `pack` ([packages-and-data.md](sdk/packages-and-data.md));
  - `test` (the mod's `tests/` in the game's runtime, a short game per computer player, a game
    with the mod's rules on, and each of its scenarios),
    `run`, `arena` (with its JSON and CSV reports, saved games, replays and Elo ratings),
    and `bot` and `python` for external bots ([bots-and-arena.md](sdk/bots-and-arena.md));
  - `publish` waits for the Steam release (section 14.7);
  - external bots connect to the dedicated server, the client and play-by-e-mail hosts over
    TCP with a token (docs/sdk/ai-protocol.md §10), and `opense4.env` is the step-by-step
    training environment (section 6.4).

## 12. Milestones

| | Milestone | Contents |
|---|---|---|
| S0 | Foundations | Layered file system; manifests and mod identity in saves, the lobby and `dataSetIdentity` (with the AI tables); `opense4-sdk check`; the mod manager. No scripting. |
| S1 | Units and assets | Data patches (add, change, remove) for every table; new ability names; PNG, larger pictures, OGG; reference checks; classic mods as packages. |
| S2 | Runtime | A prototype of the Python runtime on every platform (question 1), measured against the built-in AI's workload; the sandbox and its limits. |
| S3 | Computer players | Controllers per empire; the AI API and view; every decision callback; script memory; the built-in AI as a library; the decision journal; external bots; the arena. |
| S4 | Rules | Hooks; the effects API; mod state; abilities with effects; mod orders, events, options, victory conditions; generators and scenarios. |
| S5 | Interface | Panels, columns, buttons for mod orders, text, key bindings. Built: docs/sdk/interface.md. |
| S6 | Workshop | Publishing and subscribing with the Steam goal; mod sets offered on joining. |

Each milestone ends with the golden checksums unchanged for unmodded games, and with
example mods and their tests in CI.

## 13. Open questions

1. **The Python runtime.** The main decision:
   - **Native CPython embedded in the game** is fast and complete. But:
     - current CPython has dropped Windows 7;
     - its Windows builds use a different C runtime from ours;
     - it can't be sandboxed inside our own process.
   - **CPython compiled to WebAssembly,** run by a small WebAssembly runtime inside the
     game:
     - It is sandboxed by construction.
     - It is identical on every platform, floating point and its maths library included.
     - It can be limited by memory and instruction count.

     It is slower, though, and native modules such as numpy are only available if they
     are built for it. Which runtime covers Windows 7 and 32-bit ARM is to be checked.
   - **Recommendation, to be confirmed by the S2 prototype:**
     - WebAssembly for everything that runs inside the game;
     - ordinary Python for external bots.
2. **Full or fair view by default** for script AIs, and whether the lobby can require fair
   AIs.
3. **Budgets:** how much time per turn an in-game AI gets, and who sets it (the host, per
   game).
4. **Rules scripts and the classic combat:**
   - Should battles allow hooks inside a round (damage, to-hit) or only around a battle?
   - Inside-round hooks give the most freedom but run thousands of times a turn.
5. **API stability:** how long an `api` version is supported, and how mods are told about
   a change.
6. **New vehicle types** beyond the eight the rules know (ships, bases, fighters, troops,
   mines, satellites, drones and weapon platforms). It is possible, but it touches combat,
   movement, cargo and every window, so it is left for after S4.
7. **Licensing of mods** on the Workshop, and whether example mods ship with OpenSE4 or
   separately.

## 14. Implementation decisions (2026-10-05)

These settle the open questions the implementation can't wait for, and fix the shared
contracts between its parts.

### 14.1 The runtime: MicroPython inside the game, CPython outside

- **In-game scripts** (data generators, AIs, rules hooks, interface extensions) run on
  **MicroPython**, built from source as part of OpenSE4 on every platform. It is small,
  plain C, and links statically on every build: Windows 7 with msvcrt, 32-bit ARM, MSVC and
  macOS included.
- **Sandboxed by construction:** only the modules we compile in exist. There is no file,
  socket, OS, time, threading or random module. Each interpreter has:
  - a fixed memory heap;
  - a budget counted in executed bytecodes, the same on every computer, so running out is
    deterministic;
  - a stack limit.
- **The language** is Python 3 as MicroPython implements it: classes, generators, closures,
  comprehensions, f-strings, exceptions, big integers, sets and dictionaries. The SDK ships
  small pure-Python versions of what's missing (`typing`, `dataclasses`, parts of
  `itertools`, `functools` and `bisect`) so that AI code also runs unchanged under CPython.
- **External bots** run on any CPython 3.10 or newer, with any library, and use the same
  `opense4` package (section 6.3).
- **Why not CPython compiled to WebAssembly** (the outline's recommendation): a full
  CPython plus a WebAssembly runtime would add about 25 MB to every download, run Python
  inside a second interpreter, and no runtime was known to cover Windows 7 and 32-bit ARM
  together. MicroPython has none of these costs. CPython remains available to anyone who
  needs numpy or machine learning, as an external bot.
- **As built (S2):** [docs/sdk/runtime.md](sdk/runtime.md) describes the runtime: the
  dialect and the modules scripts get, the sandbox, the limits and their defaults (heap,
  bytecode budget, call depth, C stack), the errors, the values that cross, and what is
  the same on every computer.

### 14.2 Values: `script::Value`

Everything that crosses between the engine and scripts is a `script::Value`
(`src/script/value.hpp`): null, bool, a 64-bit whole number, text, a list, or a map that
keeps its insertion order. There is no floating point: what scripts give the game is whole
numbers, as the engine uses.
- **The AI view** and the rules' read access are built as `Value` trees.
- **Commands** are `Value` maps: `{"kind": "set_orders", ...}` (the command names in lower_snake_case, docs/sdk/commands.md).
- **Mod data and AI memory** are `Value` trees too, kept in the game state.
- **Two representations** of the same tree: in-game, the interpreter converts each `Value`
  to and from its own objects (lists, dicts, ints, str). For external bots, the tree is JSON
  text. One schema, documented in `docs/sdk/`, serves both.

### 14.3 Interpreters live for one engine call

- **A fresh interpreter per call:** each engine entry point that runs scripts gets one, then
  discards it. That means a simultaneous turn, a turn-based player's turn start or end, game
  setup, or loading the data. Compiled bytecode is cached per process, so this is cheap.
- **Nothing persists in globals:** a module's globals never outlive the call. Whatever a
  script must remember goes in its AI memory or mod data, which are saved, sent and
  checksummed with the game. A game saved and loaded therefore continues exactly as one
  that wasn't.

### 14.4 Determinism

- **Rules scripts and in-game AIs run on the host,** inside turn processing, and resolve
  identically everywhere:
  - the engine's random numbers only;
  - whole numbers out;
  - bytecode budgets instead of clocks;
  - dictionary and set order that doesn't depend on memory addresses. The runtime is
    configured or patched for this, and tests compare script results across the native,
    32-bit ARM and Windows builds.
- **AI decisions are also journaled** with the turn (section 6.3), so replays never run an
  AI again.
- **Unmodded games don't change:** a game without mods hashes, at save format 8, to the
  same golden checksums as before the SDK. Each part of the work checks this.

### 14.5 Versions

- **Save format 9 and network protocol 7** cover all SDK state (mod sets, mod data, AI
  memory, journals, new commands). The first change that needs a field bumps them; later
  SDK changes add their fields under 9 and 7 without bumping again until the next release.
- **The SDK's own interface** is `api = 1`.

### 14.6 Where things live

| Path | What |
|---|---|
| `src/script/` | `script::Value`, JSON, the MicroPython runtime and its sandbox (library `opense4_script`) |
| `src/mods/` | Mod packages, manifests, mod sets and identity, the layered file system, data patches (library `opense4_mods`) |
| `src/sdk/` | The engine's side of the SDK: the view, the command codec, controllers, hooks, the effects API, the journal (library `opense4_sdk`) |
| `python/opense4/` | The Python package used by scripts in the game and by external bots |
| `tools/sdk.cpp` | `opense4-sdk` |
| `mods/examples/` | Example mods, each with tests |
| `docs/sdk/` | The modder's guide, the API reference and the schemas |
| `tests/sdk/` | The SDK's own tests: C++ unit tests, Python tests run under both runtimes, and example-mod tests |

### 14.7 Not in this round

- **Steam Workshop** publishing waits for the Steam release goal. `opense4-sdk pack`
  already makes the package an upload would use.
- **New vehicle types** (question 6) remain for later.


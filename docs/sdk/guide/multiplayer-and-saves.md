# Multiplayer, saved games and identity

Mods change the game, and a game must be the same for everyone who plays it and every time
it is loaded. This chapter says how OpenSE4 makes sure of that, and what it means for you
when you make, update and share a mod. The reference is
[packages-and-data.md](../packages-and-data.md#multiplayer-and-saved-games).

## Game-affecting or not

| A mod with | Is | Because |
|---|---|---|
| Only pictures, sounds, music, fonts (`assets/`) | **not** game-affecting | Each computer may show the game its own way; the rules are the same |
| Interface extensions (`ui/`, a later tier) | not game-affecting | They change nothing but through commands |
| Data patches, data files, generators (`data/`) | **game-affecting** | They change the rules' numbers |
| Computer players (`ai/`) | game-affecting | Their decisions are part of the turn |
| Rules scripts, scenarios (`scripts/`, `scenarios/`) | game-affecting | They change the rules |

The Mods window, `opense4-sdk info` and `check` say whether a mod changes the game. Every
player of a game needs the same game-affecting mods, in the same order; pictures and sounds
may differ.

## Identity

Each mod has an **identity**: a hash of its manifest and every file outside `assets/` and
`ui/`. Text files are read with LF line ends, so a mod saved on Windows has the same
identity as on Linux. Change a patch, a script, a test or the manifest, and the identity
changes; change a picture, and it does not. `opense4-sdk info` and `check` print it, and
`pack` records it in the archive (`mod.identity`).

A game's **mod set** has an identity too: the ids, versions and identities of its
game-affecting mods, in load order. It is part of the data set's identity, which saved
games, network games and e-mail games compare.

So two copies of "the same" mod are the same only if every one of their game-affecting files
is. Share mods as the `.zip` that `pack` makes, and change `version` whenever you change
anything: players can then tell versions apart in the Mods window and in error messages.

## Saved games

- **A game records its mods** (ids, versions, identities, and whether each changes the
  game) when it is created, in the saved game (format 9).
- **Loading a game whose game-affecting mods differ** from yours is refused, and the message
  names what is missing or different. In the client, the load window lists each difference;
  when your mods folder has the game's mods, **Load with Its Mods** reads the data again with
  them and loads the game.
- Mods of pictures and sounds may differ: the game shows what this computer has.
- **Mod state is saved**: a computer player's memory, a rules mod's data, its options, a
  scenario's objectives met, a design's own picture. All of it counts in the game's
  checksums.
- Saved games of format 8 and older (no mods) load as before.

**Updating a mod during a long game.** A new version is a new identity, so a game started
with the old one will not load with the new one. Keep the old version (its `.zip`) for the
games that use it; Load with Its Mods finds it by id and identity in your mods folder.
Changing only pictures and sounds does not have this problem.

## Network and e-mail games

- **The lobby carries the host's mods** (protocol 7) and lists them. A player whose
  game-affecting mods differ is refused, each difference on a line of its own, with a
  **Mods** button to choose the same ones.
- **Computer players and rules scripts run on the host**, inside its turn processing. The
  other players' computers receive views of the game, never run the mods' scripts for the
  turn, and need the mods for the data (and for their own copy of a simultaneous game's
  orders, such as a mod's orders, which their computer applies to its copy when given).
- **What players see of a mod's data**: each player's copy of the game holds only what that
  player may see. A rules mod's data on a player's own empire, colonies and vehicles is in it
  when the mod says `players_see_mod_data = true`, and none of it otherwise; computer
  players' memories and notes are never sent to others ([rules.md](../rules.md#mod-data)).
- **The dedicated server** takes mods with `--mod=...`, or `mods = [...]` in a setup file,
  and sets the mods' options in `[options.mod."<mod id>"]` and the computer players' limits
  with `ai_planning_budget` and the others ([docs/MULTIPLAYER.md](../../MULTIPLAYER.md)).
- **Play by e-mail**: the host processes each turn with the game's mods, found in its mods
  folder by id and identity. External bots that play there keep their connection between
  runs with `--reconnect` ([bots-and-arena.md](../bots-and-arena.md#where-bots-play)).

## Every computer, the same turn

Game-affecting scripts run the same way everywhere, which is what lets network games,
replays and saved games agree:

- the script runtime counts budgets in bytecodes, not time, has no clock, files, threads or
  randomness of its own, and keeps dict and set order independent of memory addresses
  ([runtime.md](../runtime.md#the-same-on-every-computer));
- rules scripts draw random numbers from the game's generator, computer players from one
  seeded by the request;
- every computer player's answer is recorded in the turn's journal, so a turn replayed or a
  battle shown in a window never asks a player twice; external bots need not be
  deterministic at all.

You keep it that way by giving the game whole numbers, keeping nothing in module globals
that must last, and storing what must last in memory or mod data.

## Saving for the original

The Game Menu's **Save for SE IV** writes a game as a saved game of the original game. With
mods ([packages-and-data.md](../packages-and-data.md#saving-for-the-original)):

- a game whose mods only change data within the original's format (patches and replacement
  files) is written; the original plays it the same only with the same data, which
  `opense4-sdk dump` writes, so the export's notes say to put that data in a copy of the
  original's game folder;
- a game is refused, with the reason, when it uses what the original cannot hold: ability
  names that mods declare, computer players or rules scripts of mods, or designs whose own
  picture the installed game does not have.

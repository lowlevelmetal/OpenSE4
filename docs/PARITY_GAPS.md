# Parity gaps: where the engine differs from the original

On 2026-09-29 every rule in specs 01–05 was checked against the original executable (see
[CLEANROOM.md](CLEANROOM.md)). The specs were corrected, and each checked rule is marked
"(confirmed: binary)". This list compares the classic engine (`src/game`) with those
corrected specs.

**How to fix an item:** read the spec section it names and implement from that text. Do
not work from any listing (CLEANROOM.md, "Code follows the spec"). File and line numbers
are from the check and will drift. Impact: **H** changes most games, **M** changes some
outcomes, **L** is an edge case.

## Cross-cutting

| Item | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Turn-based games (`turn_based.cpp`, `net/host.cpp`, `net/pbem.cpp`) | Played locally, hotseat, over the network and by e-mail. On different machines a host is in charge: over the network (an OpenSE4 extension) it carries out the commands of the player whose turn it is; by e-mail each player sends the commands of their turn (`.plr`), and the host replays them and sends the game on to the next player. A player who is away, out of time or without a `.plr` is played by the computer for that turn (spec 05 open question 33). The game client opens a PBEM `.gam` (Multiplayer, Play by E-mail, or `--pbem`), plays the player's turn in either style and writes the `.plr` at End Turn (spec 05 open question 34). A computer player's (or a minister's) orders of one planning pass are carried out together after the pass, not one at a time as issued. Battles are strategic, so the Tactical or Strategic question never comes up | Spec 05 §9.1: on different machines the save file passes from player to player, and TCP/IP is for simultaneous games only; spec 04 §3 step 1; spec 06 §2.7 | M |

## Economy and population (spec 02)

Every row of this section was implemented on 2026-09-29. The economy's end-of-turn work is
one function per step of spec 02 §12 (`economy.hpp`), run inside each empire's end-of-turn
processing (`turn.cpp`, spec 05 §8); open engine choices are spec 02 §13 items 37–48. Conditions are hundredths of the 0–1.5 scale
everywhere (generation, events, combat, stellar manipulation), and setup's racial point
cost sums `economy::characteristicPointCost`.

## Vehicles, movement and logistics (spec 03)

Design names are unique in the whole game (`uniqueDesignName`), composite orders are
expanded when given (`orders.hpp`), ships with identical head orders move as ad-hoc groups,
in-system steps are greedy with the random re-choice, the empire option to clear
orders on meeting empires is `Empire::clearOrdersOnEncounter`, and the Ship Movement
options (avoid tagged minefields, avoid restricted systems) are
`Empire::avoidTaggedMinefields` and `avoidRestrictedSystems`. The engine's choices where
the spec is silent are spec 03 §19 Q50–Q57.

| Where | Engine now | Original | Impact |
|---|---|---|---|
| Low | Unit groups that mix designs are kept as one record per design: they share the per-sector caps and launch refills, but move, pay supply and fight as separate records (spec 03 §19 Q43). A single record would need a vehicle that holds several designs, which touches combat, movement, supply, cargo, the windows and the save layout | One group per (owner, unit kind, sector), mixing designs: its supply is pooled, its MP is the lowest design speed, and it fights as one group (spec 03 §1, §12) | L |

## Combat (spec 04)

Every row of this section was implemented. Designs record the enemy tonnage their
vehicles destroyed (`Design::enemyTonnageDestroyed`, spec 04 §15; the measure is open
question 47).

## Research, intelligence, diplomacy, events, score (spec 05 §1–§6)

The rules of this section follow the spec. What is left depends on other parts of the engine:

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Statistics and history files | Not written: every empire's statistics rows and history record (`Empire::history`, `Empire::historyEvents`) live in the save, and the windows read them there | Human players' `<game>_stats.txt`, `<game>_events.txt` and a text copy of the log are written at step 2 of their end-of-turn processing (§5, §8); their layout is unknown (spec 06 question 12) | L |

## Galaxy, setup and sight (spec 01, spec 02 §9)

Generation, empire placement, starting planets and stockpile, the setup option lists,
racial point costs, sight and stellar manipulation now follow the specs (`generate.cpp`,
`setup.cpp`, `sight.cpp`, `movement_stellar.cpp`). Maps are saved and loaded in our own
format ([MAPS.md](MAPS.md), `map_file.hpp`) with starting points placed first, and the
autosave choices are applied after each processed turn in local and hotseat games. The
engine's own choices where the spec is silent are listed in spec 01 §14 (Q27 onward). No
gap is left in this section.

## Computer player (spec 05 §7)

The four AI_Settings movement flags become the computer empire's own Ship Movement and
Ship Orders options each turn (`Empire::avoidTaggedMinefields`, `avoidRestrictedSystems`,
`clearOrdersOnEncounter`), and routes follow those options (spec 03 §6.2; the engine's
choices are spec 03 §19 Q57). Empire Setup sets the minister style and "Use Race Minister
Style" (`EmpireSetup::ministerStyle`, `useRaceMinisterStyle`), which the empire keeps
whether a human plays it or it is marked Computer Controlled (spec 02 §13 Q49). No gap is
left in this section.

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
| Turn-based games (`turn_based.cpp`, `net/host.cpp`, `net/pbem.cpp`) | Played on one computer only (local and hotseat): network hosts, `opense4-server pbem` and PBEM turn processing refuse a turn-based game with a message. A computer player's (or a minister's) orders of one planning pass are carried out together after the pass, not one at a time as issued. Battles are strategic, so the Tactical or Strategic question never comes up | Spec 05 §9.1: on different machines the save file passes from player to player; spec 04 §3 step 1; spec 06 §2.7 | M |

## Economy and population (spec 02)

Every row of this section was implemented on 2026-09-29. The economy's end-of-turn work is
one function per step of spec 02 §12 (`economy.hpp`), run inside each empire's end-of-turn
processing (`turn.cpp`, spec 05 §8); open engine choices are spec 02 §13 items 37–48. Conditions are hundredths of the 0–1.5 scale
everywhere (generation, events, combat, stellar manipulation), and setup's racial point
cost sums `economy::characteristicPointCost`.

## Vehicles, movement and logistics (spec 03)

Design names are unique in the whole game (`uniqueDesignName`), composite orders are
expanded when given (`orders.hpp`), ships with identical head orders move as ad-hoc groups,
in-system steps are greedy with the random re-choice, and the empire option to clear
orders on meeting empires is `Empire::clearOrdersOnEncounter`. The engine's choices where
the spec is silent are spec 03 §19 Q50–Q54.

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

| Where | Engine now | Original | Impact |
|---|---|---|---|
| Low | Of the four AI_Settings movement flags only the clear-orders pair is copied (into `Empire::clearOrdersOnEncounter`, which movement reads); empires have no minefield or avoided-system option, and routes always go around both. The empire setup (`EmpireSetup`, `setup.cpp`) has no minister style or "Use Race Minister Style", so every empire starts without a style; only the Ministers window sets one | The four flags become the computer empire's own movement options each turn (§7.5, spec 03 §6.2, §6.4). A style chosen at setup also applies to an empire marked Computer Controlled, unless it uses its race's style (§7.1, spec 02 §9) | L |

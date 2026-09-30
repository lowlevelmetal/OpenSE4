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
| Arithmetic (`ai_diplomacy.cpp`, client `ships_logic.cpp`) | The rules engine applies percentages in extended-precision floating point (`xmath.hpp`; audited on 2026-09-30, and `Resources::percent` now truncates through it). Two places still differ: a computer player's value of a Unit item truncates the three-resource total once, and the Ships window's facility refund preview truncates | The Unit item is worth its scrap value × 100, each resource rounded before summing (spec 05 §7.4, spec 03 §15); a facility refunds round(cost × %) per resource (spec 02 §6.6) | L |
| Turn order (`turn.cpp:39-104`) | Economy, research, intelligence, events and population, in fixed global phases | Spec 05 §8: orders, messages, date, ministers and AI, 30 movement/combat phases, then each empire's end-of-turn steps one empire at a time (intelligence and research first, spending last turn's points), then victory, then one galaxy-wide event roll | M |

## Economy and population (spec 02)

Every row of this section was implemented on 2026-09-29. The economy's end-of-turn work is
one function per step of spec 02 §12 (`economy.hpp`), ready for the turn-order change; open
engine choices are spec 02 §13 items 37–48. Conditions are hundredths of the 0–1.5 scale
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
| `turn.cpp` phases | Research runs before intelligence, both after the economy; the first research step takes its opening pool from the economy's first-turn income (`economy::openingResearchPool`) | Intelligence, then research, first in each empire's end-of-turn processing; the pools are filled when the game is created and by the income step (§1.1, §8). The per-empire steps exist: `intel::intelStep`, `research::researchStep`, `research::addToPools`, `research::openingPools`, `diplomacy::treatyStep`, `score::checkDestruction`, `score::checkVictory`, `events::fireDueEvents`, `events::rollNewEvent` | L |
| `events.cpp` | `Planet - Destroyed` maps Tiny and Huge planets to asteroid sizes | The Destroy Planet result of spec 01 §9 (the hazards now run first in the event step, `movement::runStellarHazards`) | L |
| `events.cpp` (stolen designs) | The copy keeps the victim's enemy tonnage destroyed, and its name is checked only against the thief's designs | A new design starts without statistics and its name differs from every design in the game (spec 04 §15, spec 03 §4.1): use `resetDesignStatistics` and `uniqueDesignName` (`design.hpp`) | L |

## Galaxy, setup and sight (spec 01, spec 02 §9)

Generation, empire placement, starting planets and stockpile, the setup option lists,
racial point costs, sight and stellar manipulation now follow the specs (`generate.cpp`,
`setup.cpp`, `sight.cpp`, `movement_stellar.cpp`). Maps are saved and loaded in our own
format ([MAPS.md](MAPS.md), `map_file.hpp`) with starting points placed first, and the
autosave choices are applied after each processed turn in local and hotseat games. The
engine's own choices where the spec is silent are listed in spec 01 §14 (Q27 onward). What
is left needs code outside those files:

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| client `ships_logic.cpp`, `reports.cpp`, `main_window.cpp` | Button checks follow the old manipulation rules; warp points show their stored name | Mirror spec 01 §9 (one star per system, hostile, cloak and supply checks); name warp points with `sight::warpPointName` | L |

## Computer player (spec 05 §7)

| Where | Engine now | Original | Impact |
|---|---|---|---|
| `turn.cpp:39-104` | The AI plans both minister groups while orders are applied (`ai::planTurn`); anger and the state machine run at the end of the turn (`ai::updateAnger`) | Design, Research, Intelligence and the construction ministers run at the start of each empire's economy step (§7.1, §8). `ai::planOrders` and `ai::planEconomyStep` are the two groups, and `updateAnger`'s parts (`recordAiDecisions`, `updateAiStates`, `politicalStep`, `rememberAiEvents`) are separate, ready for the §8 turn order | M |
| Commands | No command sets `Empire::ministers` or `Empire::ministerStyle`; the Ministers window keeps its switches on this computer | The per-area minister switches and the minister style are part of the player's settings (§7.1) | L |
| Commands | No command launches units from a planet's cargo | Satellites and drones above the kept percentages are launched from planets, drones half anti-ship and half anti-planet (§7.5) | L |
| Low | Acknowledgement chatter replies; the Units file rows; the AI_Settings movement flags (no empire movement options yet); a missed human turn's political step; the Race Opt planet type and atmosphere check | see §7 | L |

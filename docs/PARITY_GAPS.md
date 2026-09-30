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
| Arithmetic | Integer maths with floor division throughout | Percentages are applied in floating point, and each rule states whether its result is rounded (half to even) or truncated (spec 02 §1, spec 03 §2). Because of extended precision, an exact product can come out one lower, e.g. 100 × 53 % gives 52 | M |
| Turn order (`turn.cpp:39-104`) | Economy, research, intelligence, events and population, in fixed global phases | Spec 05 §8: orders, messages, date, ministers and AI, 30 movement/combat phases, then each empire's end-of-turn steps one empire at a time (intelligence and research first, spending last turn's points), then victory, then one galaxy-wide event roll | M |

## Economy and population (spec 02)

Every row of this section was implemented on 2026-09-29. The economy's end-of-turn work is
one function per step of spec 02 §12 (`economy.hpp`), ready for the turn-order change; open
engine choices are spec 02 §13 items 37–48. Conditions are hundredths of the 0–1.5 scale
everywhere (generation, events, combat, stellar manipulation), and setup's racial point
cost sums `economy::characteristicPointCost`.

## Vehicles, movement and logistics (spec 03)

| Where | Engine now | Original | Impact |
|---|---|---|---|
| Low | Design names are unique per empire in `CreateDesign` (`commands.cpp`): starting designs (`setup.cpp`) and computer players' designs (`ai_design.cpp`) reuse names across empires, so a game-wide check would stop computer players from creating designs; `designNameInUse` exists, renaming a design already checks every empire, and the designer avoids every empire's names. Movement makes a stellar manipulation order wait when no capable member has movement left; the manipulation's own checks are in `movement_stellar.cpp` (spec 01 §9). Unit groups that mix designs are kept as one record per design (they share caps and launch refills, but move and fight as separate records). Not implemented: the empire options to clear orders on meeting empires, ad-hoc groups of ships with identical head orders, greedy in-system steps with the random re-choice, and composite orders being expanded when given | see spec 03 | L |

## Combat (spec 04)

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| Low | design statistics lack "enemy tonnage destroyed": it needs a `Design` field that the design commands, redaction and events also reset | see spec 04 §15 | L |

## Research, intelligence, diplomacy, events, score (spec 05 §1–§6)

The rules of this section follow the spec. What is left depends on other parts of the engine:

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| `turn.cpp` phases | Research runs before intelligence, both after the economy; the first research step takes its opening pool from the economy's first-turn income (`economy::openingResearchPool`) | Intelligence, then research, first in each empire's end-of-turn processing; the pools are filled when the game is created and by the income step (§1.1, §8). The per-empire steps exist: `intel::intelStep`, `research::researchStep`, `research::addToPools`, `research::openingPools`, `diplomacy::treatyStep`, `score::checkDestruction`, `score::checkVictory`, `events::fireDueEvents`, `events::rollNewEvent` | L |
| `events.cpp` | `Planet - Destroyed` maps Tiny and Huge planets to asteroid sizes | The Destroy Planet result of spec 01 §9 (the hazards now run first in the event step, `movement::runStellarHazards`) | L |

## Galaxy, setup and sight (spec 01, spec 02 §9)

Generation, empire placement, starting planets and stockpile, the setup option lists,
racial point costs, sight and stellar manipulation now follow the specs (`generate.cpp`,
`setup.cpp`, `sight.cpp`, `movement_stellar.cpp`). The engine's own choices where the spec is
silent are listed in spec 01 §14 (Q27 onward). What is left needs code outside those files:

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| client `ships_logic.cpp`, `reports.cpp`, `main_window.cpp` | Button checks follow the old manipulation rules; warp points show their stored name | Mirror spec 01 §9 (one star per system, hostile, cloak and supply checks); name warp points with `sight::warpPointName` | L |
| Low | map starting points (no map files yet, spec 01 §12); autosave choices | see spec 01 | L |

## Computer player (spec 05 §7)

| Where | Engine now | Original | Impact |
|---|---|---|---|
| `turn.cpp:39-104` | The AI plans both minister groups while orders are applied (`ai::planTurn`); anger and the state machine run at the end of the turn (`ai::updateAnger`) | Design, Research, Intelligence and the construction ministers run at the start of each empire's economy step (§7.1, §8). `ai::planOrders` and `ai::planEconomyStep` are the two groups, and `updateAnger`'s parts (`recordAiDecisions`, `updateAiStates`, `politicalStep`, `rememberAiEvents`) are separate, ready for the §8 turn order | M |
| `turn.cpp` (missed orders) | A human empire whose orders are missing is planned with `ai::planTurn`, but its own minister settings stay in force, so the political step (anger) skips it when its Politics minister is off | The host switches all of that empire's ministers on for the turn and restores them afterwards (§7.1, §8 step 1): `ai::standIn` and `ai::restoreMinisters` do this, the turn pipeline has to call them around the turn | L |
| Low | Of the four AI_Settings movement flags only the clear-orders pair is copied (into `Empire::clearOrdersOnEncounter`), and movement does not read that option yet; empires have no minefield or avoided-system option, and routes always go around both. The empire setup (`EmpireSetup`, `setup.cpp`) has no minister style or "Use Race Minister Style", so every empire starts without a style; only the Ministers window sets one | The four flags become the computer empire's own movement options each turn (§7.5, spec 03 §6.2, §6.4). A style chosen at setup also applies to an empire marked Computer Controlled, unless it uses its race's style (§7.1, spec 02 §9) | L |

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

Every row of this section was implemented on 2026-09-29 except the ones below. The
economy's end-of-turn work is now one function per step of spec 02 §12 (`economy.hpp`),
ready for the turn-order change; open engine choices are spec 02 §13 items 37–48.

| Where | Engine now | Original | Impact |
|---|---|---|---|
| `setup.cpp:122-138` | Characteristic cost c × P/100 beyond the threshold | P per point, N refunded (§8.1). `economy::characteristicPointCost` implements the rule; `racialPointCost` still has to use it (setup is outside the economy's files) | M |
| `generate.cpp:353`, `movement_stellar.cpp:178` | Conditions rolled and changed on a 0–100 scale | The economy reads `SpaceObject::conditions` as hundredths of the 0–1.5 scale (100 = 1.0, 150 = Optimal). Natural planets must roll 50 + 10 × R[0,10] and asteroids half of that (spec 01 §5.6), or most planets count as Harsh or Deadly. The event handlers and combat already use the scale; the AI no longer reads conditions | H |

## Vehicles, movement and logistics (spec 03)

| Where | Engine now | Original | Impact |
|---|---|---|---|
| Low | Design names are unique per empire in `CreateDesign` (`commands.cpp`): starting designs (`setup.cpp`) and computer players' designs (`ai_design.cpp`) reuse names across empires, so a game-wide check would stop computer players from creating designs; `designNameInUse` exists, renaming a design already checks every empire, and the designer avoids every empire's names. Stellar manipulation's cloak and hostile checks and its supply payment without the racial modifier are in `movement_stellar.cpp` (galaxy work); movement makes the order wait when no capable member has movement left. Unit groups that mix designs are kept as one record per design (they share caps and launch refills, but move and fight as separate records). Not implemented: the empire options to clear orders on meeting empires, ad-hoc groups of ships with identical head orders, greedy in-system steps with the random re-choice, and composite orders being expanded when given | see spec 03 | L |

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

| Where | Engine now | Original | Impact |
|---|---|---|---|
| `generate.cpp:430-491` | Nearest-6 links, `Max Warp Points per Sys` as a cap | K nearest considered, hard cap 10, angle tests, connectivity pass (§3.5) | H |
| `generate.cpp:626-722` | Converts any planet, prefers Medium, Euclidean farthest point | Natural planets matching atmosphere, type and home size; jump-distance tiers; created if none fits (§3.6) | H |
| `generate.cpp:96-204, 353, 514-547` | Free size, invented placements, conditions 0–100, each warp end rolls its own ability | Small/Medium/Large counts; 67×46 grid; the five placements; conditions 0.5–1.5; paired ends share one roll (§3) | M |
| `setup.cpp:284-317` | Extra planets at ¼ population, no facilities | Full homeworld setup, matching atmosphere and type, 1–2 jumps away (spec 02 §9) | M |
| `movement_stellar.cpp` | Create Planet, Destroy Star, nebula, black hole, storm and Construct results; no hostile or cloak checks | Results and checks in §9 | M |
| Low | Circle Radius rounding; comets instantiated; naming; homeworld value spread; facility order and first-turn income (`setup.cpp`); ship cloaks always on, units ignore obscuration, mines give presence, partners always visible (`sight.cpp`); blockers | see spec 01 | L |

## Computer player (spec 05 §7)

| Where | Engine now | Original | Impact |
|---|---|---|---|
| `turn.cpp:39-104` | The AI plans both minister groups while orders are applied (`ai::planTurn`); anger and the state machine run at the end of the turn (`ai::updateAnger`) | Design, Research, Intelligence and the construction ministers run at the start of each empire's economy step (§7.1, §8). `ai::planOrders` and `ai::planEconomyStep` are the two groups, and `updateAnger`'s parts (`recordAiDecisions`, `updateAiStates`, `politicalStep`, `rememberAiEvents`) are separate, ready for the §8 turn order | M |
| Commands | No command sets `Empire::ministers` or `Empire::ministerStyle`; the Ministers window keeps its switches on this computer | The per-area minister switches and the minister style are part of the player's settings (§7.1) | L |
| Commands | No command launches units from a planet's cargo | Satellites and drones above the kept percentages are launched from planets, drones half anti-ship and half anti-planet (§7.5) | L |
| Low | Acknowledgement chatter replies; the Units file rows; the AI_Settings movement flags (no empire movement options yet); a missed human turn's political step; the Race Opt planet type and atmosphere check | see §7 | L |

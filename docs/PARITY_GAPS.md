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
| `generate.cpp:353`, `movement_stellar.cpp:178`, `events.cpp:875-877`, `combat_space.cpp:2046-2049`, `ai.cpp:359` | Conditions rolled and changed on a 0–100 scale | The economy reads `SpaceObject::conditions` as hundredths of the 0–1.5 scale (100 = 1.0, 150 = Optimal). Natural planets must roll 50 + 10 × R[0,10] and asteroids half of that (spec 01 §5.6); events, combat and the AI's planet rating must use the same scale, or most planets count as Harsh or Deadly | H |

## Vehicles, movement and logistics (spec 03)

| Where | Engine now | Original | Impact |
|---|---|---|---|
| `movement.cpp:770-791` | Combat clears orders and stops movement | It does neither; only a leading Sentry order is removed (§6.4) | H |
| `movement.cpp:238` | A failed order removes only itself | The whole list is cleared for every fleet member, and Repeat is turned off (§8) | H |
| `movement_upkeep.cpp:219` | Fighters at 0 supply die | They drop to 1 MP; drones die (§12) | H |
| `movement_util.cpp:390-446` | Planets cannot launch or recover units | They can, with no ability needed (§12) | H |
| `design.cpp:243-258,278-281` | Lost control sets MP to 1 | Halve (min 1) once per missing item: bridge or aux, crew quarters, life support (§6.1) | M |
| `design.cpp:190-193`; `combat.cpp:374-383,494-505` | Restrictions per component; to-hit takes the best part | Restrictions per family; to-hit sums the best of each family (§3.2, §4.2) | M |
| `design.cpp:70-73, 285-291` | Mounted values truncated; no unlimited supply | Rounded; bases, ships under construction and Quantum Reactor ships have unlimited supply (§4.3, §7) | M |
| `movement.cpp:384-388, 795-839, 743, 450-456, 861-876` | Turbulence always hits; storms hit every turn; cloak charged once; Sentry clears the list | Turbulence 50 % and fails the order; storms hit 50 % on entry and stop the move; cloak charged each turn; Sentry removes itself and ends on low supply (§6, §7, §8) | M |
| `movement_path.cpp:312` | Route falls back through avoided systems | The order fails (§6.2) | M |
| `movement_util.cpp:423-434,458`, `movement_upkeep.cpp:147-169,197-198` | Groups per design; recovery limit; fighter upkeep per group, race-scaled; best single training source | Groups per owner, kind and sector; limit on launch only; count × setting; training sources stack (§12) | M |
| `commands.cpp:374-387, 778` | Two-sided retrofit cap, always charged; unit cap at build time | Cap limits increases only; charged only when something is added; no cargo; unit cap at launch, units in space only (§12, §14) | M |
| `combat_space.cpp:1424-1428` | Planets launch 20 per combat turn | Up to 100 per kind (§12) | M |
| Low | extra movement max per id (`design.cpp:33-56`); validity details (`:177-184`); unparsed fields (`ruleset.hpp:253-268`); 20-ability cap (`load.cpp:156`); cargo trim order; fleet pooling; actor order; depot population; Use Component; manipulation checks; scrap and unmothball rounding; design-name uniqueness and purge; fleet join rules; repair modifier order; build queue blocks movement; Attack and Sweep Mines cost | see spec 03 | L |

## Combat (spec 04)

| Where | Engine now | Original | Impact |
|---|---|---|---|
| `combat_space.cpp:563` | Full speed in combat | Half the speed, rounded up, plus the best Combat Movement part (§5) | H |
| `combat.cpp:623-685` | Partial damage to uniformly chosen parts | Parts destroyed whole, in random order weighted by structure, armor first; leftover damage goes to a per-ship pool (§9) | H |
| `combat.hpp:103`, `combat_space.cpp:322,434-537` | 32×32 map, 2×2 planets, ring placement | 72×63 map, 4×4 planets and obstacles, a start box by piece count, entry edge or corner (§3) | M |
| `combat_space.cpp:1881` | Fights the full setting's rounds | One fewer (§4) | M |
| `combat_space.cpp:574-589`; `combat.cpp:561-602, 664-673` | System bonus on defense; planet racial defense; two shield pools; emissive and crystalline rules | Offense only; the setting alone for planets; one pool, phased only if all generators are; emissive and crystalline as in §9.2–9.3 | M |
| `combat_space.cpp:1117-1161, 1377-1413, 1821-1822` | Unit groups roll once; seekers move on launch turn; ram settings swapped | Per-member rolls with one combined hit and a unit pool; seekers wait a phase and aim at the centre; ramming formula in §10.3 | M |
| `combat.cpp:784-883` | Mines hit every hostile present, through shields | Only the group that moved in, ignoring shields, not if a friendly vehicle is with it (§10.6) | M |
| `combat_ground.cpp:117-176`, `combat.cpp:950`, `combat_space.cpp:1848-1876` | Militia minimum 1, 90 % hit, per-unit damage; dropped troops wait | Model in §13; dropped troops fight at once, mid-battle | M |
| `combat_space.cpp:1932-1941, 1163-1194, 2033-2038` | Experience scale; planet damage order and facility loss | §15 and §11 | M |
| Low–medium | defender definition and per-turn phase reshuffle; Crew Conversion side effects; Crew Quarters in boarding; drone group size; satellite budget; Weapons Always Hit on PD; rounding of mounts and modifiers; shield-skipping types; supply-less designs; organic armor; push and pull; carrier recovery | see spec 04 | L |

## Research, intelligence, diplomacy, events, score (spec 05 §1–§6)

| Where | Engine now | Original | Impact |
|---|---|---|---|
| `rules.cpp:44-47`, `state.hpp:470` | Linear tech cost | Default Medium: max(LC × L, LC × L² ÷ 2) (§1.3) | H |
| `intel.cpp:57-171`, `intel.hpp:20-22` | Random block and success rolls; defenses never finish; attacker named 50 % | No rolls: a fixed defense sum drains the defenses it counts; defenses finish and delete one project; 20 % naming (§2) | H |
| `events.cpp:1424-1450` | One roll per empire, uniform pick, own targets | One galaxy roll per turn, none before 2402.0, the original's record pick, galaxy-wide targets with a luck roll (§4) | H |
| `score.cpp:49-70` | Invented weights | 10 × hull tonnage + this turn's production + 200 × tech levels + 50,000 when all is known (§5) | H |
| `events.cpp:725-790, 933-947, 1165-1188` | Fake Messages is a tribute demand; Rebel always joins the spy; Lose Movement; Ship Moved; Prevent Messages | A real war declaration; 25 / 18.75 / 56.25 %; current-turn points only; a random system anywhere; deletes two turns of messages (§2.3) | M |
| `diplomacy.cpp:559-602, 651-655, 719-725` | Contact can be lost; one-way sight; tariffs on resources only | Contact is never lost, and needs mutual detection; trade and tariff rounding as in §3.3 | M |
| `research.cpp:47-109, 192-223`, `score.cpp:114-211` | Master gets the subject's tech; queue duplicates; even split capped; peace broken by war only; winners named | No tech sharing; one entry per area; round(pool ÷ N) uncapped; peace needs Non-Aggression or better; the game ends with no winner named (§1, §6) | M |

## Galaxy, setup and sight (spec 01, spec 02 §9)

| Where | Engine now | Original | Impact |
|---|---|---|---|
| `generate.cpp:430-491` | Nearest-6 links, `Max Warp Points per Sys` as a cap | K nearest considered, hard cap 10, angle tests, connectivity pass (§3.5) | H |
| `generate.cpp:626-722` | Converts any planet, prefers Medium, Euclidean farthest point | Natural planets matching atmosphere, type and home size; jump-distance tiers; created if none fits (§3.6) | H |
| `generate.cpp:96-204, 353, 514-547` | Free size, invented placements, conditions 0–100, each warp end rolls its own ability | Small/Medium/Large counts; 67×46 grid; the five placements; conditions 0.5–1.5; paired ends share one roll (§3) | M |
| `setup.cpp:284-317` | Extra planets at ¼ population, no facilities | Full homeworld setup, matching atmosphere and type, 1–2 jumps away (spec 02 §9) | M |
| `movement_stellar.cpp` | Create Planet, Destroy Star, nebula, black hole, storm and Construct results; no hostile or cloak checks | Results and checks in §9 | M |
| Low | Circle Radius rounding; comets instantiated; naming; homeworld value spread; facility order and first-turn income (`setup.cpp`); ship cloaks always on, units ignore obscuration, mines give presence, partners always visible (`sight.cpp`); drift; blockers; one-way handling | see spec 01 | L |

## Computer player (spec 05 §7)

| Where | Engine now | Original | Impact |
|---|---|---|---|
| `state.hpp:72`, `ai_anger.cpp` | Anger starts at 0, clamped 0–200 once | Starts at 50; 0–100, clamped after every term; the 11-step order and the terms in §7.3 | H |
| `ai_anger.cpp:240-306` | Placeholder state machine | The state machine in §7.2 | H |
| `ai_diplomacy.cpp` | Unprompted gifts and tributes; `Minimum Anger Chance` as a probability; no MEE politics | Never unprompted; a floor on a deterministic threshold; MEE terms in §7.4 and §7.6 | H |
| `ai_design.cpp:378-400`, `ai_economy.cpp:285-492` | Tries every hull; budget of stockpile plus 6× income; colony types re-chosen every turn | Largest allowed hull; one turn of net income, queues under 5 turns; 9 fixed colony types chosen at colonization (§7.5) | H |
| Difficulty (`state.hpp:492`, `ai.cpp:39`, …) | 4 levels on every AI, many invented effects | 3 levels on random AIs only; only noticing and commitment ratios change (§7.1) | M |
| `ai_data.cpp` | Style-to-race fallback; some tables global only; whole-name state match, first wins | One lookup rule for all 12 tables; substring match, last wins (§7.2, §7.5) | M |
| Scouts (`ai_design.cpp`, `ai_explore.cpp`, …) | Built and used | No scout type; idle attack ships explore (§7.5) | M |
| Low–medium | personality pick; minimal-changes mode; when ministers run; MEE per AI and strict threshold; mood labels; mines, satellites and drones; defaults | see §7 | L |

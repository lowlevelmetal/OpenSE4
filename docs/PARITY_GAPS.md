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

| Where | Engine now | Original | Impact |
|---|---|---|---|
| `types.hpp:85`, `events.cpp:922,929` | Mood in tenths, riot at 750 | Whole percent 0–100; riot ≥ 90, angry ≥ 60, unhappy ≥ 45, indifferent ≥ 30, happy ≥ 15 (§4) | H |
| `economy_population.cpp:297-384` | Drift always calms; presence counted once per colony; conditions add anger; no capital cap | Drift pulls toward Indifferent; presence per ship, allies count as ours; no conditions term; capitals capped at 80; Emotionless fixed at 35; happiness facilities and characteristic as in §4 | H |
| `economy_population.cpp:167-240` | Rebellion after 10 riot turns | No such rule; rebellion only through an event or an intelligence operation (§4) | H |
| `economy_queue.cpp:331-375` | Partial payment, leftover rate flows on, several completions per turn | A queue pays a whole turn or nothing; at most one completion per turn; leftover lost (§6) | H |
| `ai.cpp:494-497`, `economy.cpp:305`, `economy_queue.cpp:64` | Computer bonus adds 10 points per level | Income × 1/2/3/5 (rounded); construction rate × 1/1.5/2/3 (truncated) (§5, §6, spec 05 §7.1) | H |
| `economy.cpp:141-176,203-212` | 20-point condition bands, resistance scaling, no clamp | Conditions are a 0–1.5 scale with six bands; growth rate formula, clamp and zero-rate cases in §3 | H |
| `economy.cpp:302-323` | One additive modifier sum, floor division, system modifier per colony | Separate rounded steps; the system modifier applies to the system total (§5.1) | M |
| `economy.cpp:539-545` | Income topped up to the minimum | The minimum only replaces a total of exactly 0 (§5) | M |
| `economy.cpp:372-381, 552-576` | Units pay maintenance; ceil victim count from all payers | Units pay nothing; victims = unpaid ÷ amount + 1, out-of-supply ships first, destroyed whole (§7) | M |
| `economy_population.cpp:139-163` | Plague kills 1 % per level | Fixed loss per level, plus up to 20 % at random (§3) | M |
| `economy_population.cpp:83-126` | Floor, times frequency; negative rates shrink; room shared proportionally | Rounded; frequency only gates; no shrinking; earlier races fill free room first (§3) | M |
| `economy_queue.cpp:64-94, 142-149, 195-232` | Yard-less planets get aptitude and culture; queues by id; units overflow anywhere | Population modifier only; spaceport, resource and supply items go first; overflow into the same sector, one unit at a time (§6) | M |
| `economy.cpp:585-617` | Value changes take the best; conditions change additively | Values sum; conditions change multiplicatively, every 10th turn; negative changes ignored (§2) | M |
| `setup.cpp:122-138` | Characteristic cost c × P/100 beyond the threshold | P per point, N refunded (§8.1) | M |
| Low | spaceport needs population, and the blockade counts units (`economy.cpp:180-201`); storage and maintenance traits ignored (`:341-361`); remote mining (`:426-460`); above the last bracket (`:97-112`); upgrade truncation (`economy_queue.cpp:107-126`); emergency lasts max + 1 turns (`:387-402`); atmosphere conversion takes Val1 + 1 turns (`economy_population.cpp:244-263`) | see spec 02 | L |

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
| Low | extra movement max per id (`design.cpp:33-56`); validity details (`:177-184`); unparsed fields (`ruleset.hpp:253-268`); 20-ability cap (`load.cpp:156`); cargo trim order; fleet pooling; actor order; depot population; Use Component; manipulation checks; scrap and unmothball rounding; design-name uniqueness and purge; fleet join rules; repair modifier order; build queue blocks movement; Attack and Sweep Mines cost | see spec 03 | L |

## Combat (spec 04)

| Where | Engine now | Original (spec) | Impact |
|---|---|---|---|
| `movement.cpp` (`moveMembers`, the combat hooks) | Movement does not record where a vehicle came from, nor which group entered a sector | Combat reads `Vehicle::cameFrom`/`cameFromTurn` for attackers and start boxes (§3) and takes the entering group for mines (§10.6, `resolveSpaceCombat(ctx, where, entering)`). Until movement records them, every vehicle counts as already present and mines strike every hostile empire's vehicles there | M |
| Low | design statistics lack "enemy tonnage destroyed": it needs a `Design` field that the design commands, redaction and events also reset; mount `Comp Family Requirement` and `Shield Percent` are not read by the loader (combat applies them once they are) | see spec 04 §8, §15, §18.2 | L |

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

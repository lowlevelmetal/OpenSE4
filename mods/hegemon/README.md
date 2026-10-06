# Hegemon

A computer player for OpenSE4, written in Python with the `opense4` package
(docs/sdk/python-api.md). It makes its own decisions: diplomacy, the orders of every
ship and fleet, the economy (research, designs, every construction queue), battles round
by round and whether its groups enter sectors with enemies. It never calls the classic
AI (`ai.builtin`), and its empire opts out of the classic AI's bookkeeping
(`classic_state = false`). Two questions get the game's classic answer: the label of a
new colony (`colony_type`: the label only steers the classic ministers, which Hegemon
does not run) and lowering a cloak (`decloak`: Hegemon designs no cloaked ships).

```toml
[[ai.players]]
name = "Hegemon"
module = "hegemon"        # ai/hegemon/
class = "Hegemon"
classic_state = false
```

Play against it: `opense4-sdk run mods/hegemon -- --quick-start=Terran`, or
`opense4 --mod=mods/hegemon --ai=opense4.hegemon:Hegemon`. Measure it:
`opense4-sdk arena --mod=mods/hegemon --ai=opense4.hegemon:Hegemon --ai=builtin`
(see "Evaluation" below).

## Design

Each planning call builds a picture of the game from the view (`world.py`: the view's
plain maps indexed once, the warp graph, jump distances) and of the rules
(`knowledge.py`: every facility, component, hull, mount and tech area classified by its
abilities, never by its name, so any data set or mod works). The planners then decide,
in this order:

| Call | Module | What it decides |
|---|---|---|
| `politics` | `diplomacy.py` | treaties, answers, gifts |
| `orders` | `strategy.py` | the phase of the game and the rival to beat |
| | `explore.py`, `logistics.py` | scouts, refuelling |
| | `expansion.py` | colony ships to planets |
| | `military.py`, `invasion.py` | fleets, defence, strikes, troop landings, satellites and mines launched |
| `economy` | `designs.py`, `designer.py` | new designs when technology changes |
| | `research.py` | the research queue |
| | `construction.py`, `economy.py` | what every queue builds |
| `battle_round` | `tactics.py` | each piece's moves, targets and landings |
| `enter_sector` | `player.py` | whether a group steps into a sector with enemies |

Nothing survives a session but `self.memory` (a few kilobytes of plain values): the
phase and target, the opponent model, beliefs about enemy ships, fleet tasks, the design
book, research bookkeeping, exploration and invasion state.

### The strategic layer (`strategy.py`)

Three phases:

- **expand**: settle every planet worth it while nobody threatens us; little military
  (a tenth of income for upkeep), zero-upkeep defences on exposed colonies;
- **arm**: from turn 45, when good planets run out, or as soon as a rival's ships are near
  our colonies or a nearby rival declares war; research turns to weapons, hulls, armour
  and troops; the fleet grows to about half of income in upkeep;
- **war**: once the fleet beats the target rival's known forces three times over (and
  what threatens us at home twice over): strikes and invasions.

**Opponent model.** For each rival: its colonies we have seen and their population, its
armed ships (attack and hit points from the designs we know, fading when unseen), how
many jumps its colonies are from ours, its treaty with us and the battles it fought us.
The target is the rival whose reachable colonies are worth most for the force they take,
nearer first, one at war with us before one at peace; once chosen it stays the target
unless another becomes twice as attractive. **One war at a time:** while a rival whose
colonies are within four jumps of ours is at war with us, the target is one of those, so
no treaty is broken with anyone else.

**Upkeep share.** Maintenance is a quarter of a ship's cost every turn, so the fleet is
paid for by a share of income the phase sets, raised when enemies are near our colonies
and in the last third of a game, when new facilities and research pay back less.
Maintenance is paid resource by resource, so the share is applied to each resource's
income on its own (at most three quarters of it for the fleet, and all maintenance
together at most 85 %).

### The economy (`economy.py`, `construction.py`, `expansion.py`)

**Shadow prices.** Every kind of output (minerals, organics, radioactives, research,
intelligence) has a price: a base, moved toward the output mix the phase wants, then up
when its store is running dry and down when its store is filling up. Every other value
in the economy is in these prices.

**What each colony builds.** Facilities produce regardless of population, so a colony is
worth its slots; each colony builds one item at a time at its own rate. A colony whose
queue is about to run dry gets the item worth most per turn of building it:

- a facility's value is its output, priced, times the planet's value of that resource,
  the race's aptitude and the planet's modifiers, and only if its system delivers (a
  spaceport comes first in every new system);
- an upgrade's value is the gain over what the colony has, for every facility it upgrades;
- yards go where the strategy asks for building capacity, supply depots in every colony
  system more than a jump from one, happiness facilities where colonies are unhappy;
- ships come first at yards, in the strategy's order of priority (colony ships, scouts,
  warships, transports, yard bases), and units (platforms, satellites, mines, troops) at
  the colonies that need them.

Items are added only while the treasury can pay what they take this turn (each queue pays
all or nothing), less a reserve (a third of a turn's maintenance and a twentieth of its
income): maintenance is paid before construction, and income lost in the turn's battles
must not leave it unpaid, or the game abandons ships. What the best facilities of the
idle colonies take this turn, up to 40 % of income, is held back from ships of ordinary
priority, so colony ships cannot starve the development of the colonies they found (a
colony without facilities is worth little). When one resource overflows while another
runs short, a colony with a converter converts.

**Expansion.** A planet is worth its facility slots (under a dome if our race does not
breathe there), its resource values at today's prices, its population room and any ruins;
less in a new system (a spaceport first), less where enemies are or another empire
already lives, less where hostile armed ships are believed to be or where we lost an
unarmed ship in the last 25 turns, and less the further it is; a sector that holds a
hostile colony is left alone. Colony ships are built for the good targets, a few at a
time, and each idle one takes the best target it can reach on the supply it has (a ship
out of supply hardly moves), a tenth as willingly when the way there passes through such
a system; one with no target in reach refuels first. A colony ship on its way through a
system where we just lost an unarmed ship, or that has not moved for four turns, chooses
again. New planet surfaces to settle are researched for the planets they would open.

**Exploration** (`explore.py`). Up to three scouts (two after turn 80) each take the
nearest warp point of an explored system whose far side we have not seen. A scout sets
out only with the supply for the trip there and back to a depot, else it refuels first;
once on its way it turns back only when its supply no longer covers the way home, so it
does not give up at the warp point. A scout stuck for four turns gives up its warp point
for fifteen.

### Research (`research.py`)

Each tech area is worth what its next levels unlock: better facilities for the families
we use (the extra output on every one we have), new kinds of facility, new surfaces to
settle (every known planet of that surface), and for the military better weapons
(damage per ton over the ranges battles are fought at), armour, shields, engines,
to-hit parts, bigger hulls and troops (the troop hull, then a weapon troops can carry).
Items one or two levels away, or needing two
areas, count for less, and an area we cannot research yet passes some of its worth to
the areas it needs. Each level is also worth its score. Values are divided by what the
level costs, discounted by the turns it takes at our income. The queue is funded in
order, never evenly (the default splits points thinly), and the civil and military sides
take turns at the head of the queue to keep the share of points the phase sets; an area
with progress is never dropped.

### Designs (`designer.py`, `designs.py`)

Designs are built from the rules for each role and checked with the engine's
`design_figures` query before they are created:

- **warship**: for each hull, control parts and engines for the fleet speed, then every
  mix of weapons (each with each mount the hull allows), shields, a to-hit part and armour
  in the space left; Lanchester's square law makes a fleet of equal cost worth its
  firepower times its staying power, so the mix with the most
  `offense × hit points / cost²` wins. Offense is damage per combat turn over ranges 1 to
  8 at the battle's to-hit chances; hit points count armour, shields and the to-hit
  defence bonus. Costs are weighed by resource scarcity. Weapons that also hit planets
  are preferred, so warships can strike colonies;
- **colony ships** per surface, **scouts** (the cheapest fast hull, with fuel tanks),
  **yard bases**, **weapon platforms** and **satellites** (the warship search on their
  hulls), **mines** (most warhead damage per cost), **troops** (ground attack times hit
  points per cost squared), **troop transports** (most cargo per cost at fleet speed).

A role is redesigned when technology changes and the new design is at least 8 % better.

### Military operations (`military.py`, `intel.py`, `logistics.py`, `invasion.py`)

**Beliefs.** Every armed foreign ship seen is remembered with its owner, system, attack
and hit points; a belief fades over 12 turns and is dropped when we look at its system
again and it is gone. The threat to a system is what is believed there.

**Doctrine: concentrate, then strike where much stronger.** New warships gather at a
rally point (our colony system nearest the enemy) and form fleets; fleets that meet
merge into the strongest. Each turn each fleet gets one task:

1. refit: repair when most members are damaged, resupply before supply runs out (with a
   margin for the trip to the nearest depot: a ship out of supply barely moves, cannot
   fire and has no shields); cripples leave the fleet so it keeps its speed;
2. the main fleet in war strikes first, unless enemies in our systems need it;
3. defend: the nearest colony system with hostile ships it beats by 30 %;
4. strike: the best sector of enemy colonies (a battle is fought in one sector) within
   three jumps of our space, of the target rival or of a rival at war with us, that it
   beats by a factor of two (five outside war), counting the planets' defences as
   estimated from their population and the date and the hostile ships in the system, and
   that every ship has the supply to reach, fight at and come back from (a fleet short of
   it refuels first); a strike that costs more than half the fleet raises the estimate for
   that planet (it learns) and the fleet withdraws;
5. otherwise join the main fleet (in war) or hold at the rally point.

Our orders are kept when we meet others after a warp (empire option). Groups that
cannot win stay out of sectors with enemies (`enter_sector`); unarmed ones go in only
where nothing armed is, or behind our own warships already there and winning. A sector
where mines struck our ships is tagged as a minefield, so routes go around it.

**Zero-upkeep defence.** Units pay no maintenance: every colony gets a weapon platform,
exposed ones more, and exposed colonies build satellites and (with the technology) mines
and launch them into their sector.

**Invasions.** In the arm and war phases, troop transports wait at a staging colony in
the rally system, where troop units are built and loaded. Strikes prefer a colony the
loaded troops can take (its population then need not be killed): enough troops for its
militia (Lanchester again, with ground combat's militia per 20 million people). Loaded
transports join the main fleet at the rally point, so they are in its battles, where the
tactics land them beside the colony once its guns are silent; empty ones leave the fleet
and go back to reload. A taken colony keeps its facilities.

### Battles (`tactics.py`)

Each phase of our side:

- **targets**: enemies ranked by the damage they deal per hit point it takes to kill them;
  our pieces, strongest first, fire at them, each target taking only as much fire as kills
  it (no overkill); planets spread their weapons over many targets;
- **range**: each mobile piece fights at the distance where its damage, less what the
  target deals back, is best; it moves there, fires, and backs out of the target's reach
  with the movement left (our move orders do not end a piece's turn);
- point-defense is switched off for hand fire (it still reacts on its own); combat groups
  are dissolved so every piece moves on its own; unarmed ships keep away; troop carriers
  land beside enemy colonies; fighters, drones and pieces it has no plan for follow their
  strategies.

### Diplomacy (`diplomacy.py`)

Peace with everyone but the target: non-aggression is offered to every other rival
(the classic AI then never targets us), offers of non-aggression are accepted, demands
are refused, gifts taken. A rival whose anger toward us is high gets a token gift first
(its anger counts the message, not its size). A treaty with the target is broken before
the war starts.

## Tuning

`ai/hegemon/config.py` switches features on and off (the evaluation measures each);
the phases' upkeep shares and research weights are in `strategy.py`, the output mix the
prices aim at in `economy.py` (`TARGET_SHARES`), the margins of strikes and defence in
`military.py`, the late-game surge in `strategy.py` (`LATE_*`). `player.py` has `DEBUG`,
which writes the planners' reasoning to the game's log.

## Tests

- `opense4-sdk test mods/hegemon`: `tests/test_hegemon.py` in the game's runtime (the
  planners on a new game of the installed data set, a whole turn and the next session
  through the harness, designs that fit their hulls, the tactics on made-up battles,
  troop and strength sums) and a short game.
- The engine's tests: `tests/sdk/test_mod_hegemon.cpp` plays Hegemon beside the built-in
  AI on OpenSE4's own fixture data in both turn styles (no failed request, the same game
  every time), and, with `OPENSE4_CLASSIC_DATA`, a free-for-all on the installed data
  set with its costs a turn (`OPENSE4_HEGEMON_TURNS=150` for long games).

## Evaluation

(See the results below.)

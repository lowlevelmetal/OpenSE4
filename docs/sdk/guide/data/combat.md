# Combat strategies and formations

Two small tables decide how ships behave once a battle starts. **DefaultStrategies.txt**
(patch table `strategies`, records named by `Name`) holds the combat strategies: how a
piece moves, what it shoots at first and what it never shoots at. **Formations.txt** (patch
table `formations`, records named by `Name`) holds the fleet formations: where the members
of a fleet stand around their leader. Both matter mostly when the computer moves the
pieces: every side of a strategic battle, the computer players' sides, a battle nobody
watches, and a tactical battle while the Auto button is down or after Resolve Combat. In
a tactical battle a player's side acts only on the player's orders, except its drones,
which the computer always moves; point-defense fires on its own on every side.

## The fields

### DefaultStrategies.txt

Each new empire gets a copy of every record of the file, in file order, as its own
strategy list (an empire file loaded at setup brings its own list instead). Players edit
their copies in the Strategies window, so changing the file changes new games only; a
running game keeps the lists its empires were given.

A piece in battle uses one strategy at a time, looked up again each time it is needed:

| Piece | Strategy it uses |
|---|---|
| A ship or fighter group that belongs to a fleet | The fleet's strategy while it leads or belongs to a combat group; its design's once it has left every group |
| A ship outside fleets | Its design's strategy |
| A unit group | The strategy of its first stack's design (its fleet's under the same condition as a ship) |
| A planet | The first strategy of its empire's list, always |
| Every planet in the combat simulator | The viewer's first strategy |

New fleets also start with the first strategy, so the **first record** of the file is the
default for planets and fleets. Computer players add the records of their `AI_Strategies`
table to their list and name strategies in their design templates and fleet table (see
[ai-tables.md](ai-tables.md)).

Values are matched in any letter case, ignoring spaces and punctuation (`Don't Get Hurt`
and `Dont Get Hurt` are the same). A value OpenSE4 does not recognise is ignored, and the
key keeps OpenSE4's default; nothing reports it (see "Things to watch").

| Field | What it does | Values |
|---|---|---|
| `Name` | Names the strategy in the Strategies window and in the AI tables. Designs and fleets point at a strategy by its position in their empire's list. | Text |
| `Primary Movement Strategy` | How the piece moves when the computer moves it (below). | One of the eight movement strategies |
| `Secondary Movement Strategy` | Used when the primary one is impossible for the piece (below). | One of the eight |
| `Targeting Priority 1` to `Targeting Priority 4` | Sort keys for choosing targets, applied in order: the first sorts, the later ones break ties. | `Nearest`, `Farthest`, `Largest`, `Smallest`, `Most Damaged`, `Least Damaged`, `Fastest`, `Slowest`, `Strongest`, `Weakest`, `Has Weapons`, `Does Not Have Weapons`, or `None` |
| `Use Type Priority First` | `True`: candidates are ranked by their category's `Type Priority` before the targeting keys. `False`: the targeting keys come first and the category rank only breaks their ties (that order of the tie-break is inferred). | `True` or `False` |
| `Type Priority <category>` | The category's rank as a target. | Whole number, 1 = engage first; 0 or less ranks after every positive rank |
| `Dont Fire On <category>` | `True`: pieces of that category are never chosen as targets. | `True` or `False` |
| `Break Formation <category>` | `True`: the side's own pieces of that category leave their formation when the computer moves them. | `True` or `False` |
| `Fighters Launch Group Amount` | How many fighters the computer puts in each group it launches from a carrier. | Fighters per group; 0 puts all of a launch in one group |
| `Damage Percent Per Ship` | While choosing targets, a ship or base that has already lost more than this share of its hit points is passed over, if any other candidate is left. | Percent, 0 to 100; 100 never passes anything over |
| `Damage Percent Per Planet` | The same for planets (hit points lost since the battle began). | Percent, 0 to 100 |
| `Damage Percent Per Fighter Group` | The same for fighter groups. | Percent, 0 to 100 |
| `Damage Percent Per Satellite Group` | The same for satellite groups. | Percent, 0 to 100 |
| `Damage Until All Weapons Gone` | `True`: a damaged target that still has weapons is never passed over by the four filters above. | `True` or `False` |
| `Drones Per Target` | The computer's drone launch batch, and with the number of hostile ships and bases its launch limit (below). OpenSE4 reads it when a record holds it; the original's file has no such key, so a record without it uses OpenSE4's default. | Drones; 0 launches all |

A record that leaves a key out gets OpenSE4's neutral default for it: Optimal Weapons
Range as primary and Point Blank as secondary movement, `Has Weapons` then `Nearest` as
targeting keys, no damage filter, nothing excluded, nothing breaking formation, and an
engagement order of OpenSE4's own for the categories (armed ships, armed bases, planets,
drones, fighters, seekers aimed at us, satellites, carriers, transports, colony ships,
unarmed ships, unarmed bases, seekers aimed at others, mines).

#### The movement strategies

| Value | What the piece does | Impossible when |
|---|---|---|
| `Optimal Weapons Range` | Looks for a square where it can deal damage with no danger; failing that, the best ratio of danger to damage; failing that, the square with the most damage. A ship or base with this primary and no weapon components at all rams instead. | The piece has no weapon other than point-defense and warheads |
| `Short Weapons Range` | The square where it deals the most damage, less danger breaking ties. | As above |
| `Maximum Weapons Range` | Stays as far from its target as its weapons allow, outside the target's reach when it can. | As above |
| `Point Blank` | The square next to its target. | As above |
| `Ram` | Picks the hostile ship or base whose hull comes latest in VehicleSize.txt, the nearest of those (a drone takes its drone target), moves next to it and rams it if it still has movement left. Planets and unit groups are never rammed. | A drone group without a drone target; otherwise never |
| `Board Enemy Ships` | Goes next to a hostile ship or base whose shields are down and whose boarding defense is below its own `Boarding Attack`, then tries the capture. | The ship has no `Boarding Attack` |
| `Drop Troops` | Waits while the target planet still has guns and an armed escort fights; otherwise goes next to the planet and lands its troops. | The ship carries no troops |
| `Don't Get Hurt` | Runs from the mass of enemies while spreading away from its friends, using its full move. It looks only at where pieces stand. | Never |

The text is matched loosely: any value containing "optimal", "short", "maximum", "point
blank", "board" (or "capture") or "don't get hurt", or starting with "drop troops" or
"ram", counts as that strategy. The exact square chosen by each strategy, with its danger
and attack maps, is in [spec 04 §16.1](../../../spec/04-combat.md).

#### The categories

The same fourteen category names follow `Type Priority`, `Dont Fire On` and `Break
Formation`. Spell them as your data set's file does: the parentheses are part of the name
(`Type Priority Seekers(On Us)`).

| Category | The pieces in it |
|---|---|
| `Planets` | Planets with a colony |
| `Fighters`, `Satellites`, `Drones` | Groups of those units |
| `Seekers(On Us)` | Seekers aimed at a piece of the empire choosing |
| `Seekers(On Others)` | Every other seeker |
| `Mines` | Read, but mines never take part in a battle as pieces, so these keys change nothing |
| `Colony Ships` | Ships with a colonize component |
| `Carriers` | Other ships that launch fighters, drones or satellites, or lay mines |
| `Transports` | Other ships without weapons that have cargo space |
| `Ships`, `Ships(No Weapons)` | Every other ship, with or without weapons |
| `Bases`, `Bases(No Weapons)` | Bases, with or without weapons |

### Formations.txt

A formation is a pattern drawn on a 19 × 19 template: one cell for the leader and a
numbered list of cells for the members. A fleet's formation is chosen with Fleet Transfer
or Change Formation\Strategy; a new fleet starts with the **first record** of the file, and
the computer players name theirs in `AI_Fleets` ([ai-tables.md](ai-tables.md)). In the
Tactical Combat window, Set Group Leader also picks a formation for the group.

| Field | What it does | Values |
|---|---|---|
| `Name` | Names the formation in the windows and in `AI_Fleets`. Fleets refer to a formation by its position in the file. | Text |
| `Description` | Shown in the Formation Report. Optional. | Text |
| `Leader Position Xpos`, `Leader Position Ypos` | The leader's cell. Every member's place is measured from it. Required. | Whole numbers; 1 to 19 to stay on the drawn template |
| `Leader Design Type` | The design type meant for the leader. Shown in the report, never used to place pieces. Optional. | `Any` or a design type name |
| `Number of Positions` | How many member cells follow. Required. | Whole number |
| Positions (list `positions`) | Each entry is one member's cell: `Position N Xpos`, `Position N Ypos` (required) and `Position N Type` (the design type meant for it, display only; optional). Member number N takes entry N. | Whole numbers; `Any` or a design type name |

Columns grow to the right and rows grow downwards. The fleet faces up the template: a
cell with a smaller `Ypos` than the leader's is ahead of the leader. The Formation Report
draws columns and rows 1 to 19, with the leader marked; cells outside that square still
work in battle but are not drawn.

## How the fields work together

### Strategies in battle

**The strategy in effect.** The primary movement strategy is used unless it is impossible
for the piece (the table above). Then the secondary is tested the same way, and if that is
impossible too, the piece uses Don't Get Hurt. A secondary `Ram` never falls back. So a
troop strategy should name a secondary that suits the ship once its troops have landed.

**Choosing targets.** A computer piece gives targets to all its weapons at once, when it
plans its move and again each time it fires:

1. The candidates are the hostile pieces of other empires, less the categories its
   strategy marks `Dont Fire On`. When firing, only pieces within reach of a ready weapon
   count.
2. The `Damage Percent` filters pass over damaged candidates (unless `Damage Until All
   Weapons Gone` keeps an armed one). If that leaves nothing, the filters are dropped.
3. The rest are sorted by `Type Priority` and the four targeting keys, as
   `Use Type Priority First` says; the order of the pieces breaks the last ties.
4. The first candidate is the main target, which the range strategies measure their
   distances from. The weapons are then spread over the first few candidates, as many as
   the piece's target budget allows (for a ship or base its best `Multiplex Tracking`, 1
   without it), and a candidate stops taking weapons once the damage given to it is well
   beyond what it can take. Fighter groups give all their weapons one target.

**Holding fire.** While a side has a ship carrying troops whose strategy in effect is Drop
Troops, none of its pieces targets an enemy planet that has no weapon other than
point-defense and warheads.

**Launching.** The computer launches a carrier's fighters in groups of `Fighters Launch
Group Amount`, each group from one cargo stack, within the bays' launch rate. It launches
drones in batches of `Drones Per Target`, and never more than `Drones Per Target` × the
hostile ships and bases in the battle, less its drones already flying. Players launching
by hand choose their own group sizes.

**Formations.** A piece whose strategy in effect is Don't Get Hurt, Drop Troops, Board
Enemy Ships or Ram leaves its formation, as does any piece whose category has `Break
Formation` set. The others head for their formation places (below).

### Formations in battle

- **The combat group.** When a battle is set up, each fleet's armed members (at least one
  weapon component, not mothballed) form one combat group. The fleet leader anchors it,
  armed or not; if the leader is not in the battle, the first armed member anchors it.
  The other armed members get member numbers 1, 2, 3 in the order their pieces enter the
  battle. Unarmed members are placed at random and use their own design's strategy.
- **A member's place** is the leader's square plus (its position − the leader position),
  turned by the leader's facing and kept on the combat map (columns 0 to 71, rows 0 to 62):

  | Leader's facing | Offset used for (dx, dy) |
  |---|---|
  | 0 | (dx, dy): as drawn |
  | 1 | (−dy, dx): a quarter turn clockwise |
  | 2 | (−dx, −dy): a half turn |
  | 3 | (dy, −dx): three quarters |
  | 4 to 7 | the diagonal turns: (dx − dy, dx + dy), (dx + dy, dy − dx), (−dx − dy, dx − dy), (dy − dx, −dx − dy) |

  A fleet arriving from the bottom edge of the map faces 0, one from the left edge 1, one
  from the top edge or through a warp point 2, one from the right edge 3; fleets already in
  the sector get a random facing from 1 to 4. A piece's facing then follows its last step,
  so the pattern turns as the leader moves. The diagonal facings are not rescaled, so they
  stretch the pattern by about 1.4.
- **Members without a place.** A member whose number is larger than `Number of Positions`
  stays in the group but does not follow the leader; it acts on its own. A member keeps
  only its number, never a square: when another piece takes over the lead, or the leader
  picks another formation, every member goes to the position with its number in the new
  pattern.
- **Moving.** When the leader moves, each member moves toward its place around the
  leader's new square with its own movement points. Members leave the formation only
  during the computer's moves, as the strategy says (above).
- **Dissolving.** The whole group dissolves when its leader is destroyed or leaves it.
  On a side the computer moves it also dissolves when every square around the leader is
  taken as the leader is about to act, and when the leader survives a hit with no movement
  left (which is always the case once a computer-moved leader has acted).
- **Design types.** `Leader Design Type` and `Position N Type` never decide who stands
  where: members take positions in number order whatever their designs.

## Changing them with patches

DefaultStrategies.txt is read as open key and value pairs, so a patch may only set keys
that a file of the table already uses: your data set's DefaultStrategies.txt, or a
replacement file a mod supplies. Formations.txt has fixed fields and the `positions` list.

**A new strategy for long-range escorts**, copied from one of your data set's strategies:

```toml
# data/strategies.toml
[[strategies.add]]
name = "Lantern Screen"
copy_from = "<a strategy of your data set>"
set = { "Primary Movement Strategy" = "Maximum Weapons Range", "Secondary Movement Strategy" = "Don't Get Hurt", "Targeting Priority 1" = "Fastest", "Targeting Priority 2" = "Nearest", "Use Type Priority First" = true, "Dont Fire On Planets" = true, "Damage Percent Per Ship" = 60 }
```

It goes at the end of the file, so existing games and designs are unaffected; new empires
get it as their last strategy. Set the category keys (`Type Priority Fighters`, `Break
Formation Carriers` and so on) the same way.

**A tighter fighter doctrine** for an existing strategy:

```toml
[[strategies.change]]
name = "<a strategy of your data set>"
set = { "Fighters Launch Group Amount" = 6, "Damage Until All Weapons Gone" = true }
```

**A new formation written out in full**, and positions added to another:

```toml
# data/formations.toml
[[formations.add]]
name = "Mirefield Wedge"
after = "<a formation of your data set>"

[formations.add.set]
"Description" = "A narrow wedge with the leader at its tip."
"Leader Position Xpos" = 10
"Leader Position Ypos" = 6
"Leader Design Type" = "Any"

[formations.add.add]
positions = [
  { "Position Xpos" = 9,  "Position Ypos" = 7, "Position Type" = "Any" },
  { "Position Xpos" = 11, "Position Ypos" = 7, "Position Type" = "Any" },
  { "Position Xpos" = 8,  "Position Ypos" = 8, "Position Type" = "Any" },
  { "Position Xpos" = 12, "Position Ypos" = 8, "Position Type" = "Any" },
]

[[formations.change]]
name = "<another formation of your data set>"
add = { positions = [{ "Position Xpos" = 10, "Position Ypos" = 14, "Position Type" = "Any" }] }
remove = { positions = [3] }
```

OpenSE4 sets `Number of Positions` and numbers the entries again, so the patch never
writes them. An entry to remove is named by its place in the list (`3`) or by a table of
its fields (`{ "Position Xpos" = 9, "Position Ypos" = 7 }`).

**Removing a formation or a strategy** that the computer players' fleet table names:

```toml
[[formations.remove]]
name = "<a formation your mod retires>"
cascade = true
```

Without `cascade`, every `Fleets Default Formation` (or, for a strategy, `Fleets Default
Strategy` and a design template's `Default Strategy`) that still names it is an error that
says where it is. With `cascade = true` those fields are emptied: the fleet table then
falls back to the first formation or strategy, and a design template to the first
strategy whose primary movement suits its design type.

## Things to watch

- **Order is meaning.** The first strategy is what planets use and what new fleets start
  with; the first formation is what new fleets start with and what the computer players
  fall back to. Put a new record first only if you mean it to become the default.
- **Strategy values are not checked when the data loads.** A misspelt movement strategy
  or targeting key leaves OpenSE4's default in place, silently. Try a changed strategy in
  the combat simulator before you rely on it.
- **Open keys.** `opense4-sdk check` reports a key no file of the table uses: `DefaultStrategies.txt
  has no field 'Dont Fire On Planet' (no file of the table uses it; check the spelling)`.
  Copy the key as your data set's file writes it, with the same spaces and parentheses.
  `Drones Per Target` can only be set by a patch when a mod's replacement
  DefaultStrategies.txt already uses it.
- **Impossible primaries.** A strategy whose primary needs weapons, used by unarmed
  ships, falls to its secondary; an unarmed design with an Optimal strategy and no weapon
  components rams. Give colony ships, transports and carriers a strategy whose primary is
  Don't Get Hurt.
- **`Mines` keys** have no effect; mines never become pieces.
- **Formation fields** that are missing or not whole numbers are load errors that name the
  patch. The original game reads at most 100 positions per formation; OpenSE4 reads them
  all, so keep to 100 if the data is also meant for the original.
- **Removing** a strategy or formation that the AI tables name needs `cascade = true` or
  a change to those tables (above). A later mod that adds the name again leaves nothing
  to refer to a removed record.

## More detail

- [Spec 04 §16](../../../spec/04-combat.md): strategies, target choice, the overkill limit,
  holding fire; §16.1 the movement strategies square by square; §3 battle setup, facings
  and placement; §5 combat groups and the tactical group orders; §10.4 and §10.7 launching
  fighters and drones; §18.5 the formation fields.
- [Spec 03 §2.6, §9 and §10](../../../spec/03-vehicles-and-abilities.md): the formation
  file, fleets, and how formations place and dissolve.
- [Spec 05 §7.5 and §7.7](../../../spec/05-research-intel-diplomacy-ai-multiplayer.md): how
  the computer players choose strategies and formations.
- [packages-and-data.md](../../packages-and-data.md): the patch format, `cascade` and the
  open-key rule.
- [view.md](../../view.md) "The rules view": computer players read the file's records as
  `default_strategy` and `formation` (with `formation_slot`), and their own empire's list
  as `my_empire.strategies` (`own_strategy`). A `view_design` and a `fleet` carry a
  `strategy` index into that list, and a `fleet` a `formation` index.
- [ai-tables.md](ai-tables.md): `AI_Strategies`, `AI_Fleets` and the design templates'
  `Default Strategy`.

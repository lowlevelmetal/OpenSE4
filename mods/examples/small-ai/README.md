# Pioneer: a small computer player of its own

An example mod of the OpenSE4 SDK: a complete computer player written from scratch, to be
read. It makes its own designs, research and construction, explores, colonizes and defends
its systems, and answers diplomacy by a few rules. The tutorial
[A first computer player](../../../docs/sdk/guide/tutorials/computer-player.md) builds a
smaller one and then reads this one.

| File | What it decides |
|---|---|
| `ai/pioneer.py` | The player: the three planning calls, and what it keeps in memory |
| `ai/parts.py` | What the empire can build now: the best component, weapon, hull or facility for a job, by abilities, never by name |
| `ai/designs.py` | Scout, colony ship (one per surface it can settle) and warship designs, checked with the `design_figures` query and created with `apply` |
| `ai/expansion.py` | Scouts explore; colony ships settle the best planets they can reach, asking the engine with `colonize_problem` |
| `ai/defence.py` | Warships intercept enemies in our systems, gather at home into fleets and strike enemy colonies at war |
| `ai/economy.py` | Research toward better parts; ships at yards, facilities on colonies with room, yards as the empire grows |
| `tests/test_pioneer.py` | Its designs fit, its calls give well-formed commands, its diplomacy |

It overrides `politics`, `orders` and `economy`, and leaves a new colony's type, entering
sectors with enemies, decloaking and the battles themselves to the classic AI. It reads
everything by what records do (abilities, vehicle types, figures), so it plays on any data
set.

**How it does.** Eight games of 80 turns on the installed game against the classic AI,
seeds 1 to 8, the players changing seats each game (`opense4-sdk arena --games=8
--turns=80 --seed=1`, a debug build):

| Player | Games | Wins | Mean score | Median score | Colonies | Systems | Tech levels | Ships | Battles won/lost | Failures | Time a turn |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Pioneer | 8 | 3 | 89,725 | 79,096 | 9.1 | 3.5 | 31.5 | 10.5 | 0/15 | 0 | 118 ms |
| builtin | 8 | 5 | 110,754 | 104,673 | 24.2 | 6.2 | 32.5 | 13.0 | 15/0 | 0 |  |

Pioneer is a teaching example, not a match for the classic AI. It researches as well and
builds an economy that wins some games on score, but it settles far fewer planets: it
builds colony ships at few yards, settles only the surfaces its research has opened, and
its domed colonies stay small. It loses its battles, which it leaves to the classic
strategies with designs that are mostly weapons. Each of those is a good place to start
improving it.

```sh
opense4-sdk test mods/examples/small-ai
opense4-sdk run mods/examples/small-ai -- --quick-start=Terran
opense4-sdk new --from-example small-ai my-ai     # a copy to make your own
```

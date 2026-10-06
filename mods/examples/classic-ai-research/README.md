# Classic AI, own research: the Scholar

An example mod of the OpenSE4 SDK: a computer player that keeps the classic AI for
everything but its research. The guide's [Computer players](../../../docs/sdk/guide/computer-players.md)
chapter walks through it.

```text
classic-ai-research/
  mod.toml              declares the player: [[ai.players]] Scholar, module scholar
  ai/scholar.py         the player: economy() keeps the classic economy but its research
  ai/planner.py         the research planner: plain functions of plain values
  tests/test_planner.py the planner on an invented tech tree (runs under CPython too)
  tests/test_scholar.py the player on a new game of your data set
```

**How it plans.** Each turn it ranks every tech area it may research by what the next
three levels unlock (components, facilities, hulls, mounts and tech areas, worth more when
they colonize, produce or move ships, half as much when they only upgrade a family it has)
for what those levels cost, plus five turns of research per level, so that cheap levels do
not always come first. Areas already under way stay at the front, so no points are lost.
It gives the classic economy's commands except its `set_research`, and its own queue.

**How it does.** Eight games of 80 turns on the installed game against the classic AI, seeds
1 to 8, the players changing seats each game (`opense4-sdk arena --games=8 --turns=80
--seed=1`, a debug build):

| Player | Games | Wins | Mean score | Median score | Colonies | Systems | Tech levels | Ships | Battles won/lost | Failures | Time a turn |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Scholar | 8 | 2 | 90,305 | 94,671 | 21.4 | 5.4 | 23.9 | 8.9 | 6/11 | 0 | 110 ms |
| builtin | 8 | 6 | 116,024 | 105,016 | 24.5 | 6.6 | 34.2 | 13.5 | 11/6 | 0 |  |

The fair comparison is a player that overrides nothing: every decision is the classic AI's,
through `ai.builtin`. Such a player trails the built-in AI the same way, because an empire a
player plays does not run the classic AI's own steps between its decisions (its state
machine, anger, territory claims and start-of-turn figures:
[ai-protocol.md](../../../docs/sdk/ai-protocol.md), section 9), and its colonization suffers
most. Against that baseline the Scholar's own research holds its own:

| Player | Games | Wins | Mean score | Median score | Colonies | Systems | Tech levels | Ships | Battles won/lost | Failures | Time a turn |
|---|---|---|---|---|---|---|---|---|---|---|---|
| a player that overrides nothing | 8 | 3 | 91,172 | 87,904 | 18.5 | 5.2 | 32.5 | 10.1 | 9/9 | 0 | 43 ms |
| builtin | 8 | 5 | 117,895 | 105,016 | 25.0 | 6.5 | 34.4 | 13.6 | 9/9 | 0 |  |

The Scholar settles more planets than the classic research minister run through the
library (21.4 colonies to 18.5) and scores about the same, with fewer tech levels: it
spends its points on what colonizes, produces and builds rather than on many cheap levels.
Both are about a fifth behind the built-in AI's score. What closes that gap is choosing the
colony ships' targets: the tutorial's Settler, which does only that, draws level with the
built-in AI ([A first computer player](../../../docs/sdk/guide/tutorials/computer-player.md)).

```sh
opense4-sdk test mods/examples/classic-ai-research
opense4-sdk arena --mod=mods/examples/classic-ai-research --ai=example.classic-ai-research:Scholar --ai=builtin --games=8 --turns=80
```

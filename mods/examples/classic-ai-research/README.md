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

**How it does.** Forty-eight games of 100 turns on the installed game against the classic
AI, seeds 1 to 48, the players changing seats each game (`opense4-sdk arena --games=48
--turns=100 --seed=1`, a debug build):

| Player | Games | Wins | Mean score | Median score | Colonies | Systems | Tech levels | Ships | Battles won/lost | Failures | Time a turn |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Scholar | 48 | 26 | 155,754 | 146,498 | 29.6 | 6.3 | 26.0 | 19.1 | 257/290 | 0 | 118 ms |
| builtin | 48 | 22 | 144,887 | 136,320 | 25.6 | 6.9 | 39.2 | 16.1 | 293/272 | 0 |  |

Everything but its research is the classic AI's, and a player that overrides nothing plays
exactly the classic AI's games (the classic AI's own bookkeeping runs for it:
[ai-protocol.md](../../../docs/sdk/ai-protocol.md), section 9), so what the Scholar gains
or loses is its research's. Measured game by game against the classic AI in the same seat
of the same 48 games (the classic AI playing itself, `--ai=A=builtin --ai=builtin`), the
Scholar won 12 more games in a hundred (95 % interval 3 to 22), scored 18,900 more (6,800
to 31,000), and had 5.1 more colonies (2.4 to 7.9) and 4.5 more ships (2.4 to 6.5), with
10.9 fewer tech levels (9.3 to 12.5): it spends its points on what colonizes, produces and
builds rather than on many cheap levels.

```sh
opense4-sdk test mods/examples/classic-ai-research
opense4-sdk arena --mod=mods/examples/classic-ai-research --ai=example.classic-ai-research:Scholar --ai=builtin --games=48 --turns=100 --seed=1
```

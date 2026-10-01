---
windows: scores, comparisons, victory-conditions
---
# Score and victory

This chapter explains how your score is counted, how a game ends, and the windows that show who
is ahead.

## Your score

Every empire's score is worked out each turn from three things:

| Part | Points |
|---|---|
| Fleet | 10 for every kT of hull size of your ships and bases. Mothballed ships do not count. |
| Production | 1 for every mineral, organic, radioactive, research point and intelligence point you produced this turn. |
| Technology | 200 for every technology level you have, plus 50,000 once you have researched everything open to you. |

Planets, population and units do not count by themselves, but they are what produces the points
above.

> Your score is mostly your production and your fleet. A peaceful empire that builds a strong economy can lead the scores without fighting a single battle.

## Who sees which scores

The **Score Display** option of the game setup decides whose scores you can see:

- only your own;
- your own and those of empires with which you have Non-Aggression or better (the default);
- everyone's.

When the game is over, every score is shown.

## Victory conditions

On the Victory Conditions page of Game Setup you can switch on any of these conditions, each with
its own number. With none switched on, the game never ends by itself.

| Condition | The game ends when |
|---|---|
| Score reaches | Some empire's score reaches the number. |
| Game lasts | The number of years has passed since 2400.0. |
| Score is at least ... of the second-best | Some empire's score is at least that percentage of every other empire's score. |
| Technology researched | Some empire has researched that percentage of all the technology levels open to it. |
| Quadrant at peace for | Every pair of empires has held Non-Aggression or better for that many years in a row. |
| No victory in the first | Nothing is checked before this many years have passed. |

Peace means every pair of living empires: two empires with no treaty, or that have never met,
break the peace.

When the "second-best" condition is on, it also overrides the score and years conditions: those
two can then end the game only if the leader is that far ahead.

The conditions are checked at the end of every turn. When one is met, every empire gets a
**Game Over** entry in its Log, and that turn is the last. No winner is announced: the ranking in
the Scores window is the result. Orders are refused after that.

## Elimination

An empire is **destroyed** when it has no populated planet and no ship or base left. Every empire
in contact with it is told. When you are the last empire left, you are told, and you may play on.

## The Scores window

[Scores](window:scores) (from the [Empires](window:empires) window) shows, for each empire whose
score you can see: its rank, score, resources produced, research, intelligence, technology
levels, systems, planets, population, units, ships and bases. Hover over a column heading for its
full name. Empires whose scores are hidden show only dashes.

## The Comparisons window

[Comparisons](window:comparisons) draws one of the score table's columns as a graph over time. Pick
the column with the buttons on the right, and tick the empires to compare. Empires whose scores
you could not see at some point show gaps.

## The Victory Conditions window

[Victory Conditions](window:victory-conditions) lists each condition of this game, whether it is
on, and how close each empire is to it, as far as you can see. It also shows how much of the
qualifying time is left. Once the game is over, it names the empire with the best score.

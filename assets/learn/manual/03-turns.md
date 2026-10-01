---
windows: log
---
# Turns

The game is played in turns. Each turn is a month of game time, a tenth of a year. This chapter
explains the two turn styles, what happens when a turn ends and how to read the news in the Log.

## Turn styles

A game uses one of two turn styles, chosen on the Mechanics page of Game Setup.

| | Turn-based | Simultaneous |
|---|---|---|
| Who acts | One empire after another. | Everyone at the same time. |
| Orders | Carried out the moment you give them. | Stored, and carried out when the turn is processed. |
| Ship movement | Ships move at once, as far as their movement points allow. | Ships move during the turn's 30 days, all at once. |
| Battles | Fought as soon as ships meet; tactical combat is possible. | Fought automatically during movement. |
| Messages | Take effect when sent. | Delivered at the start of the next turn's processing. |
| Default for | Quick Start and New Game. | Network games. |

## Turn-based play

In a turn-based game, your turn begins with your ships' movement points refilled. Ships that
still have orders from earlier turns carry them on at once, before you get control. Then you play:

- A **Move To** order moves the ship right away, step by step, until it arrives or runs out of movement points. The rest of the move continues at the start of your next turn.
- When a move would take your ships into a sector where they can see enemy forces, you are asked **Attack Sector**: `Attack` enters the sector and starts a battle, `Stay Back` stops the move and cancels the ship's orders.
- When a battle starts, you choose **Tactical** (you command your ships yourself) or **Strategic** (your ships follow their strategies and you watch). See [Combat](combat).
- Messages to other empires, treaties and declarations of war take effect at once.

When you press `End Turn`, your empire's end-of-turn processing runs (below). Then the computer
players and any other humans take their turns. After the last empire, the date moves on, the
game checks the victory conditions, and random events happen.

## Simultaneous play

In a simultaneous game, you give orders during your turn but nothing moves. When every player has
ended the turn, the whole turn is processed at once:

1. Messages are delivered.
2. Computer players and ministers give their orders.
3. Ships move during 30 **days**. A ship with speed N acts on about N days spread over the month, one sector or one order per action. After each day, battles are fought wherever enemies meet.
4. Each empire's end-of-turn processing runs (below).
5. The game checks the victory conditions, and random events happen.

Battles in simultaneous games are always strategic. You read about them in the Log and can watch
them with `Combat Replay`.

> In a simultaneous game a ship with only 1 movement point never moves. That includes any ship out of supplies. Keep your ships supplied.

Because you cannot react during the month, plan ahead: use Sentry, patrols and the options that
clear orders when your ships meet an enemy (see [Settings](settings#empire-options)).

## What happens at the end of a turn

Each empire's end-of-turn processing runs these steps in order:

1. Ministers and computer players finish their planning.
2. **Intelligence** projects get this turn's points and run (see [Intelligence](intelligence)).
3. **Research** projects get this turn's points (see [Research](research)).
4. **Income**: your colonies produce, and the minerals, organics and radioactives go to your treasury; research and intelligence points go to their pools for next turn.
5. **Treaties and trade**: trade income arrives (see [Diplomacy](diplomacy#trade)).
6. **Maintenance** is paid for your ships and bases (see [Economy](economy#maintenance)).
7. **Planets**: populations grow, planet changes take effect, and plagues strike.
8. **Happiness**: each colony's mood changes.
9. **Construction**: each queue builds (see [Construction](construction)).
10. **Repair** and **supply**: damaged ships are repaired, and ships at depots are refilled.
11. **Storage**: minerals, organics and radioactives above your storage capacity are lost.
12. **System effects**: facilities that change planets or train crews over time act.
13. **Ground combat** goes on wherever enemy troops are still fighting on your planets.

After every empire, once a year obsolete designs that nobody uses are removed, the game checks
the [victory conditions](score-and-victory), and the [random events](events-and-stellar-manipulation)
of the turn happen.

One consequence: research and intelligence points that you see during a turn were produced at
the end of the last turn, and they are spent at the end of this one.

## The Log

The [Log](window:log) (`F10`) holds the news of the last turn: things built, technologies
discovered, battles, messages from other empires, events and orders that could not be carried
out. It opens by itself at the start of each turn when there is news (you can switch this off in
[Empire Options](settings#empire-options)).

- The list at the top left shows the entries, each with a coloured dot for its category. Tick `Earlier turns` to see older entries too.
- The map below it marks the system of the selected entry.
- The details on the right show the entry's picture, title, date and text. A battle entry lists the forces on each side and their losses. A message shows its type, tone, text and package.
- The buttons on the right filter by category: **All, Construction, Research, Intelligence, Events, Politics, Combat** and **Misc**.

| Button | What it does |
|---|---|
| `Send Reply` | Answer the selected message (opens [Communicate](window:communicate)). |
| `Combat Replay` | Watch the selected battle again (see [Battle windows](combat-windows#replays)). |
| `Constr. Queues` | Open the [Construction Queues](window:queues). |
| `Goto` | Close the Log and show the entry's location in the main window. |

> Read the Misc entries: they list the orders that could not be carried out, so that you can give new ones before the ships sit idle.

## Hotseat games

When several human players share one computer, each plays their turn in order. Between players a
**Next Player** screen asks the others to look away. If the empire has a password, the player
types it there. Then `Begin Turn` starts that player's turn.

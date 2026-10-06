---
windows: ministers, player-errors
---
# Computer players and ministers

Computer players are your opponents, and ministers are the same computer intelligence working
for you. This chapter explains how computer players are set up and how to hand parts of your own
empire to ministers.

## Computer players

A computer player is an empire whose every decision is made by the computer: research, designs,
construction, colonization, fleets, diplomacy and battles. It knows nothing that a human in its
place would not know.

You add computer players on the Players page of Game Setup, in two ways:

- set an empire in the list to **Computer**;
- switch on **Random computer players**, and choose how many (the range comes from the game data). Random players get races nobody else in the game uses.

**Random neutral players** are small empires that never leave their home system. They cannot use
warp points, but they defend themselves and trade and talk like anyone else.

The same page sets two handicaps:

- **Difficulty** (Low, Medium or High) applies to the random computer players. Empires you set to Computer yourself play at Medium.
- **Bonus** (None, Low, Medium or High) multiplies every computer player's income by 1, 2, 3 or 5, and its construction rates by 1, 1.5, 2 or 3. It never helps humans.

With **Team mode** (Game Settings page), every computer player is allied with the others in a
Partnership against the human players.

## Computer players of mods

A mod can bring computer players of its own, written in Python, which play an empire as fully as
the classic computer player does. When the mods you play with offer some, the setup screens let
you choose who plays each computer empire (see [Getting started](getting-started#computer-players-of-mods)),
and so does the host of a network game in its lobby (see [Multiplayer](multiplayer)). Everything
else stays as above: difficulty, bonus and team mode apply to them too.

- **Seeing everything.** The game option *Computer players see everything* gives them the whole
  game instead of what their empire knows. It is off by default.
- **When one fails**, because of a mistake in its program or because it asks for more than the
  game's limits allow, the classic computer player decides that one thing in its place and the
  game goes on. A notice over the bottom of the system view says who failed, in what and how;
  `Details` opens a list of every failure of the game with what its program reported, and
  `Dismiss` hides the notice until the next one. Only the computer that runs the players (the
  host of a network game) shows it.
- **Their notes.** These players can write notes about what they think. Switch on *Show the
  computer players' notes* in [Settings](settings#modding) (or press `Ctrl+Shift+N` in a game) to
  see them: a list in the system view, the noted sectors framed in yellow, rings around the noted
  systems in the galaxy view, and the notes about a ship, fleet or planet in its report. It shows
  every note, whatever your empire knows, so it is a tool for people who make computer players.

## How computer players behave

Each computer player has a **personality** that sets how quickly it angers, how much it values
treaties and how it builds. Its mood toward you is shown in the [Empires](window:empires) window,
from Brotherly to Murderous. What makes it angry, and how it decides on treaties and war, is
explained in [Diplomacy](diplomacy#how-computer-players-feel-about-you).

> Computer players may gang up on whoever runs away with the score. If you lead by a wide margin, expect your former friends to turn on you.

## Ministers

**Ministers** let the computer run parts of your empire for you. There are 25, in two groups.

**Empire-wide ministers** take over a whole area of your empire and may undo your own choices
there:

| Minister | Takes over |
|---|---|
| Design | Creating and upgrading designs. |
| Ship Construction | Building ships. |
| Research, Intelligence | Your research and intelligence queues. |
| Politics | Diplomacy: answering messages, proposing treaties, declaring war. |
| Repair, Resupply | Sending damaged and empty ships for repair and supplies. |
| Scrap, Retrofit | Scrapping and refitting old ships. |
| Expenses, Production Output | Nothing: these two have no duties in this version of the rules. |

**Individual ministers** act only on the ships, fleets and colonies that you have put under
minister control:

| Minister | Handles |
|---|---|
| Facility Construction | What colonies build, guided by their colony type. |
| Colonization | Colony ships. |
| Transports | Moving population between planets. |
| Carriers, Mines/Satellites/Drones, Troops | Launching and using units. |
| Attack, Defense, Patrol, Exploration | Warships and scouts. |
| Fleets | Forming fleets. |
| Space Yard Ships | Ships with space yards. |
| Stellar Manipulation | Ships with stellar manipulation components. |
| Ship Cloaking | Cloaking ships when their orders call for it. |

To put a single ship or colony under minister control, select it and click the **Minister
Control** order button. Its individual ministers then look after it, as far as you have switched
them on.

## The Ministers window

Open [Ministers](window:ministers) from [Empire Status](window:empire-status) (`F11`).

- Each minister has a lamp: click it to switch that minister on or off.
- `Select All` and `Select None` switch every minister on or off.
- `Indiv. On` and `Indiv. Off` put every ship, fleet and colony you own under minister control, or take them all back.
- `Complete AI On` hands your whole empire to the computer: every minister, every ship and colony, and every new one. `Complete AI Off` takes it all back.
- A switch puts newly built ships and launched units under minister control automatically.
- Another switch is for missed turns in simultaneous multiplayer games (see below).
- The **minister style** chooses the personality your ministers follow: your race's own, or one of the styles in the game data. You can change it at any time.

> Ministers are good at chores. Let them handle repair, resupply and the facilities of small colonies, and keep the big decisions, research and diplomacy, for yourself.

## Missed turns in multiplayer games

In a multiplayer game, when your orders for a turn do not arrive in time, the computer plays that
turn for you with all ministers on. If you would rather it changed nothing, switch on the option
about missed turns in the [Ministers](window:ministers) window: the computer then only keeps the
books for you.

## Handing an empire to the computer

The Game Menu's `Players` button opens the list of every empire. A lit lamp means the computer
plays that empire. Click lamps to change them, then press `OK`; `Cancel` changes nothing. If the
game has a master password, it is asked for first, exactly as it was set.

- Handing an empire **to the computer** switches all of its ministers on, and the minister mark
  of every one of its ships, fleets and colonies. From then on the computer plays its turns.
- Taking it **back** switches all of them off again; your earlier minister settings do not come
  back. A row you clicked twice counts too, so its ministers go off.
- Neither changes the empire's password or minister style.
- In a turn-based game the empire whose turn it is goes on being played by hand until `End
  Turn`. In a local or hotseat game, once no empire is left for a human to play, the game ends:
  the ending window shows and no further turn is played.
- In a network or e-mail game the window changes only your own copy of the game. For your own
  empire, the minister switches also go to the host with your orders, so all your ministers act
  there, but the host still waits for your orders each turn. The host of a network game hands
  empires over with the `Empires` list of its status strip instead.


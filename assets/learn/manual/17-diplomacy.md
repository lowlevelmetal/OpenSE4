---
windows: empires, communicate, treaty-grid, history, race-report
---
# Diplomacy

Other empires are rivals, but not always enemies. Treaties stop wars, open trade and share
maps; messages let you bargain, demand and threaten. This chapter explains contact, treaties,
trade and the windows you use for diplomacy.

## Meeting other empires

You start the game knowing no one. You **make contact** with another empire when your objects
and theirs are in the same system and each side can see the other, as long as a chain of warp
points leads from the systems of your colonies to a colony of theirs. Both empires then get a
**First Contact** entry in the log, and their treaty becomes **None**: met, but with no treaty.
An empire without colonies makes no contact.

The game looks for new contacts only at certain moments, in the system concerned: when ships
arrive there through a warp point, when a ship, unit group or colony there lowers its cloak
(by an order, at the start of a battle, or when its cloak fails), when an event happens there,
and when a planet or ship there changes hands in a trade or surrender. Ships that only move
around inside a system, even next to a stranger's colony, make no contact until one of those
moments comes.

Contact can also be **lost**. Once a turn, each empire follows the warp points outward from its
colonies. When no colony of an empire it has met can be reached that way any more (for example
because a warp point was closed, or because one side lost its last colony), contact with that
empire ends: the treaty returns to "no contact" and the intelligence projects aimed at it are
removed, and your Log reports **Contact Lost**. Contact also ends when an empire is destroyed.
An empire you lost contact with can be met again in the usual way.

You can buy contact with a third empire: **Comm Channels** can be part of a trade package. If you
have no contact with that empire yet, you and it are put in contact (with no treaty) when the
package is carried out; there is no First Contact entry, only a note in each log. The new
contact lasts only if a chain of warp points links you.

While you are not in contact with an empire you cannot send it messages, trade with it or aim
intelligence projects at it, and it is missing from the Empires window.

## Treaties

Treaties are ranked from worst to best. Each pair of empires has one treaty.

| Treaty | What it means |
|---|---|
| War | Open hostility. |
| Non-Intercourse | The two keep apart. Their ships still fight. |
| None | Met, but no treaty. Ships still fight. |
| Non-Aggression | No fighting. Ships may share sectors. |
| Subjugation | One side is the master, the other its subject. The subject pays tribute and may hold no other treaties. |
| Protectorate | One side protects the other, which pays a smaller tribute. |
| Trade Alliance | Trade in minerals, organics and radioactives. |
| Trade and Research Alliance | Trade in research points as well. |
| Military Alliance | Your ships may also resupply at each other's depots. |
| Partnership | Trade in intelligence points as well, shared sight and shared maps. |

The most important line is between None and Non-Aggression. **Below Non-Aggression, ships
fight on sight.** From Non-Aggression up, your ships and theirs never fire on each other, and
they do not blockade each other's planets.

In a Subjugation or Protectorate, the empire that **accepts** the proposal becomes the
subordinate and pays; the empire that proposed it becomes the master. A subject pays 40 % of
its income (stock settings) and a protectorate 20 %. The master receives the minerals,
organics and radioactives; the research and intelligence part is simply lost. A master also
learns its subject's ship designs. Accepting Subjugation ends the subject's friendly treaties
with everyone else.

A **Partnership** shares the most:

- each partner sees what the other's sensors see;
- each turn, the systems your partner has explored become explored for you;
- you learn the ship designs your partner has seen.

New treaties also change your people's mood. Good treaties please most races and wars and
subjugation displease them, but a warlike race may feel the opposite (see
[Planets and colonies](planets-and-colonies#mood)).

## Trade

With a Trade Alliance or better, each partner receives a share of the other's production every
turn, without the other losing anything. Trade starts at 1 % the turn after you sign, and grows
by 1 % each turn up to 20 % (stock settings). What is traded depends on the treaty:

- resources from Trade Alliance up;
- research points from Trade and Research Alliance up;
- intelligence points only in a Partnership.

Your race's Political Savvy, its culture and some traits raise or lower what you receive.
Moving from one trade treaty to another keeps the percentage reached; breaking the treaty starts
it again from 0.

> Trade is free income. Even a Trade Alliance with a distant empire you will never fight is worth signing early, because the percentage takes 20 turns to reach its maximum.

## Messages

You talk to other empires with political messages. You can send each empire **one message per
turn**.

- In a **turn-based** game, a message takes effect the moment you send it: a declaration of war or an accepted treaty changes things at once.
- In a **simultaneous** game, all messages are delivered at the start of the next turn's processing, before any ship moves. Computer players answer two turns after you send.

Each message has a **type**, a **tone** (Pleading, Neutral or Demanding) and a text you can
edit. The types are:

- **General Message**: words only.
- **Propose Treaty**, and the replies Accept, Refuse and Counter. A counter-proposal is a new proposal. A treaty takes effect only when accepted.
- **Break Treaty**: the treaty falls to None at once.
- **Declare War**: war at once, whatever the treaty was.
- **Propose Trade**: a package of things you give and things you ask for. You may ask for **Any** system, planet, technology and so on, and let the other side choose. A trade that still holds an Any item cannot be accepted: the other side must counter with real items.
- **Gift** and **Tribute**: things you give. They change hands only when accepted. A tribute is a gift to a stronger power. The game setup can forbid gifts.
- **Surrender**: your whole empire passes to the recipient at once: your planets, ships, stored minerals, organics and radioactives, and a level in each technology where you were ahead. You are asked to confirm. It works only when the game setup allows surrender.
- **Grant Independence**: you give up one of your colonies, which the recipient may then settle.
- **Demands and requests**: ask for a gift, a tribute or surrender; ask the other side to pull ships or colonies out of a system or leave a planet; to stop hostilities, espionage, sabotage or attacks; to break a treaty with, declare war on, make peace with, support you against or attack a third empire. These bind no one. Computer players weigh them; a human may simply ignore them.

A **package** can hold systems (your claim passes to the receiver), planets, resources in steps
of 1,000, technology, ships (with their cargo), units, star charts (the receiver explores those
systems), a treaty and comm channels. When a package is carried out, each item gets its own
entries in the Log, the receiver's first: Planet, Vehicle, Resources or Technology Received and
Transfered, Starcharts Received, Treaty Enacted and so on, each with its Goto. Things that were
lost before the trade was accepted are simply left out. Technology that teaches the receiver
nothing new still arrives, with a note saying so.

## How computer players feel about you

Every computer player keeps an **anger** value toward each empire, from 0 to 100, starting at 50.
The [Empires](window:empires) window shows it as a mood:

| Anger | Mood |
|---|---|
| 0 to 9 | Brotherly |
| 10 to 19 | Amiable |
| 20 to 29 | Receptive |
| 30 to 39 | Warm |
| 40 to 59 | Moderate |
| 60 to 69 | Cool |
| 70 to 79 | Displeased |
| 80 to 89 | Angry |
| 90 to 100 | Murderous |

What makes them angrier or calmer depends on their personality, but these all count:

- battles against you, won or lost, while you have no Non-Aggression treaty;
- destroying planets or stars, or making black holes, in systems they are in;
- your intelligence attacks on them, when they find out who did it;
- your messages: gifts calm them, demands annoy them;
- your ships and colonies in their territory (the systems they hold and the ones next to them), while you have no Non-Aggression treaty;
- your colonies on planets they would like to settle;
- slowly, the passing of time.

A computer player accepts a treaty only when its anger toward you is low enough, and the better
the treaty, the calmer it must be. It declares war or breaks a treaty when its anger climbs too
high. Declaring war on you sets its anger to 100. Its own strength, yours, and the wars it is
already fighting all shift these limits.

When one empire's score pulls far ahead of everyone else, computer players may treat it as a
**Mega Evil Empire**: they grow angry at it every turn and refuse it treaties. Being the clear
leader has a price.

> Gifts are the fastest way to calm an angry neighbour. A small gift every few turns can keep a dangerous empire friendly while you build up.

## The Empires window

Open [Empires](window:empires) with `F9`. It shows a portrait for each empire you are in contact
with, four at a time, with a column of facts below each.

- **Left-click** a portrait to open [Communicate](window:communicate) with that empire.
- **Right-click** a portrait for its Race Report: its race, characteristics, traits and description, and its technology if it is your partner.
- The tabs choose the column: **Treaty** (treaty, race, player type, since when, last war, the computer's mood with its anger value, messages waiting, a message sent this turn), **Trade** (trade percentage of the maximum, what is shared, its growth, shared sight and resupply) and **Tariff** (who pays whom).

The buttons on the right open more windows:

| Button | What it shows |
|---|---|
| History | A dated record of your empire's and others' main events. |
| Treaty Grid | The treaty between each pair of empires. You see treaties of your allies only; the rest show `??`. |
| Intelligence | Your [intelligence](intelligence) projects. |
| Borders | A check box that switches the view to the systems each empire claims, with filters `Select All`, `Allies`, `Enemies` and `Us`. |
| Victory Conditions | Progress toward the game's [victory conditions](score-and-victory). |
| Scores | The score table (see [Score and victory](score-and-victory#the-scores-window)). |
| Comparisons | Graphs of the score table over time. |
| Our Race | Your own race report. |

## The Communicate window

In [Communicate](window:communicate) you choose the **Message Type**, the **Tone** and, when the
type needs it, a treaty, a third empire, a system or a planet. The third empire comes from the
empires you have met that are still in the game. The text starts with a suitable default; change
it as you like. (Your Politics minister, when it is on, writes its own requests and needs no such
choice.)

For trades, gifts and tributes, `Edit Package` opens the package editor. Choose **We give** or
**We ask for**, pick a tab (systems, planets, resources, technology, ships, units, star charts,
treaty, comm channels) and click items to add them; resources are added in steps of 1,000 with
`Add To Package`. Click an item in the package to remove it. `Clear Package` empties it and `Done`
goes back to the message.

`Send Message` sends it; the button is disabled once you have written to that empire this turn.
Surrender and Declare War ask you to confirm. `Report` shows the empire's race report,
`View Last Offer` their newest offer, and `Start Again` clears the message.

When you open a message you received (with **Send Reply** in the [Log](window:log)), the window
shows what they offer and ask for, with `Accept`, `Refuse`, `Counter` and `Reply`. Counter
starts a counter-proposal from their offer, with the two sides of the package swapped.

## Diplomacy advice

- Sign Non-Aggression with neighbours you are not ready to fight. It costs nothing and stops surprise battles.
- Trade treaties with distant empires pay for themselves.
- A Partnership shares sight and maps both ways. Sign one only with an empire you trust.
- Before you declare war, check the [Treaty Grid](window:treaty-grid) to see who your target is allied with, and whom you may end up fighting.

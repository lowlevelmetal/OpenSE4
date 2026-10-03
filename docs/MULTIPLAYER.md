# Multiplayer

OpenSE4 plays games with several people in two ways:

- **Network games.** One machine hosts. Players connect over TCP, set up their
  empires in a lobby, and send their orders each turn. The host processes the turn
  and sends everyone the new game.
- **Play by e-mail (PBEM).** The host keeps the game file and sends each player a turn
  file holding only that player's view of the game. Each turn, every player sends in
  an orders file by any means (e-mail, a shared folder, a chat upload), and the host
  processes the turn from whatever arrived.

Both play either turn style of the classic game (spec 05 §8, §9):

- **Simultaneous** (the default): players only give orders, the host resolves
  everyone's turn at once, and the computer plays any empire whose orders are
  missing.
- **Turn-based**: players take their turns one after another, and every order is
  carried out as it is given. See [Turn-based games](#turn-based-games).

The host is authoritative in both.

Tactical combat is offered in local and hotseat turn-based games only. Network and PBEM
games, turn-based ones included, resolve every battle strategically: the host plays the
turn-based calls without battle answers, so nobody is asked Tactical or Strategic, and
`no_tactical_combat` in a setup file changes nothing there (docs/PARITY_GAPS.md).

Everything here is implemented by `src/net` (the `opense4_net` library),
`src/game/serialize.*` (the save format) and `src/server` (`opense4-server`).

## Hosting

### From the game

The in-game lobby is built on `net::HostSession` (see [For developers](#for-developers)).
The host is one of the players: it has its own slot, picks its empire, and plays like
everyone else. On top of that it can:

- add and remove computer empires before the start;
- kick a player (in the lobby the slot opens up again; in a running game the computer
  takes over the empire). A kicked name cannot rejoin;
- start the game when everyone is ready, or force the start;
- see who has sent orders for the turn, and process the turn early;
- set a turn time limit;
- hand any human empire to the computer, or back to its player (the `Empires` button of the
  in-game status strip, see [Computer control](#computer-control));
- reset players' passwords in a simultaneous game ([Reset Passwords](#reset-passwords));
- give orders for any empire (to play for an absent friend).

The lobby shows the host's key fingerprint (see [Security](#security)). It is this
computer's identity as a host, kept in `host_key.txt` in OpenSE4's user folder and made
the first time this computer hosts. Players' games remember it.

### Dedicated server

`opense4-server` hosts a game without playing in it:

```sh
opense4-server --players=3 --ai=2 --quadrant-size=1 --name="Friday Game" --password=boss
```

It finds the installed classic data set on its own (or takes `--data=DIR`), opens TCP
port 6720, forwards it on the router with UPnP, and waits. The game starts
automatically once every player slot is taken and every player has marked ready.
From then on the server processes a turn as soon as every human player's orders are
in. It saves after every turn (`--autosave=N` changes that; a turn-based game is also
saved as each player's turn begins) and logs to standard output. Stop it with Ctrl+C: it saves, says goodbye to the players and removes the
port forwarding.

| Option | Meaning |
|---|---|
| `--data=DIR` | the classic game's `Data` directory (default: auto-detect) |
| `--port=N` | TCP port (default 6720; `0` picks a free one) |
| `--bind=ADDR` | listen on one address only (default: every IPv4 interface) |
| `--upnp` / `--no-upnp` | UPnP port forwarding (default: on) |
| `--players=N` | human player slots (default 2) |
| `--ai=N` | computer empires (default 0) |
| `--seed=N`, `--quadrant-size=N`, `--systems=N`, `--quadrant=NAME` | galaxy settings (by default the number of systems is rolled from the quadrant size) |
| `--setup=FILE.toml` | name, seed, options and computer empires from a [setup file](#setup-files) |
| `--turn-based` | a [turn-based game](#turn-based-games) (the setup file's `simultaneous = false` does the same) |
| `--name=NAME` | game name (also the save file's name) |
| `--password=PW` | master password (see below) |
| `--join-password=PW` | a password every player needs to join |
| `--host-key=FILE` | the host's long-term key (default: `host_key.txt` in OpenSE4's user folder, the one the game uses; made on first use). The log names its fingerprint |
| `--turn-timeout=SEC` | process the turn after SEC seconds even if orders are missing (turn-based: end a player's turn after SEC seconds) |
| `--load=GAME.gam` | continue a saved game |
| `--save-dir=DIR`, `--autosave=N` | where and how often to save |
| `--max-turns=N` | stop once the game reaches turn N (for tests) |

**Master password.** A player who connects with the master password becomes an
administrator: from their client they can start the game (also forced), add and
remove computer empires, kick players, set the turn time limit, hand empires to the
computer, reset passwords and process the turn early. The master password is also stored (as a
verifier, see [Security](#security)) in the saved game, and continuing that game
with `--load`, or processing it as a PBEM game, requires it again.

**Continuing a game.** `opense4-server --load=Friday_Game.gam` reopens the saved
game in its current turn, under the name stored in the file. Players reconnect with
the same name and password. If the game has a master password, pass it again with
`--password`. A game saved with a different data set is refused.

`tools/server_smoke.sh` runs complete games: a server with one human slot and one
computer empire, and the server's scripted `bot` client, which plays two turns; a
turn-based game with two bots and a computer empire; and a turn-based PBEM game.

## Joining

**On the same local network:** Multiplayer → Join a Game lists the games that hosts on
the LAN are running, with:

- players and slots;
- whether the game has started;
- whether a join password is needed;
- a warning when the host's game data differs from yours.

Click a game to fill in its address. A host answers these searches on UDP port 6716,
the port the classic game used for its control traffic. The dedicated server turns
this off with `--no-lan-discovery`.

**Over the internet,** or when a game is not listed, enter the host's address and port.
To join, a player needs:

- the host's address and port;
- a player name;
- optionally, a password of their own.

Both routes then check:

- **The host's identity.** The connection is encrypted, and the host shows its key. The
  first time a player joins a host (an address and port), their game remembers that key
  in `known_hosts.txt` in OpenSE4's user folder, and the lobby names its fingerprint:
  compare it with the one the host sees (its lobby, or the server's log; the LAN list
  shows it too). From then on a host with another key is refused, with both
  fingerprints. If the host really has a new key (a new computer, a deleted key file),
  the player chooses **Trust the New Key and Connect**; otherwise someone may be in
  between ([Security](#security)).
- **Same version and data.** The client and the host must speak the same network
  protocol (the same OpenSE4 release) and use the same data set. The data set is
  compared by fingerprint, a hash of every data file plus the loaded tables, so two
  installations of the same game and mods match wherever they live. The host
  refuses mismatches with a message that names both sides. Older and newer releases
  refuse each other the same way, in both directions, and the LAN list marks a host
  of another version.
- **The player password** protects the player's slot and empire. Nobody else can
  take over the empire or send orders for it, in network and in PBEM games.
- **The join password**, if the host set one, is needed by everyone. It is part of the
  connection's keys, so a wrong one is refused before anything else is said. A player
  who gives a join password refuses a host that does not ask for one.

### Reconnecting

A player who drops out (crash, network trouble, closed laptop) reconnects with the
same name and password; the game tries again every few seconds by itself. The host
sends the current turn at once, and nothing of the turn is lost:

- **Simultaneous games.** Orders the host had stored still count. Orders sent just
  before the drop, which may never have arrived, go out again, and so do orders
  given (End Turn) while the connection was down. A later copy replaces an earlier
  one, so sending twice changes nothing.
- **Turn-based games.** The player goes on with their turn where the host has it,
  including the questions still open. Commands and End Turn that got no answer, sent
  before the drop or given while the connection was down, go out again in order. Each
  carries a number the host remembers, so one it had already carried out is answered
  again but not carried out twice.

A new connection with the right password replaces an old one that is still open, so
a half-dead connection never locks a player out. Names are not case-sensitive.

While a player is away, the host keeps waiting for their orders. It stops waiting
when the turn time limit runs out, when the host processes the turn by hand, or when
the empire is handed to the computer.

### When a player's game differs from the host's

Each player holds a copy of the game: the view the host sent. When the player sends
orders or a command, the game says what that copy looks like (its checksum, and a
hash of each part of it: the date and options, the galaxy, the colonies, the empires,
the designs, the vehicles, the fleets, the messages, the events, the battles, the
counters, the random numbers, the player turn and the map). If it is no longer what
the host sent, the copies have drifted apart (a desync), and the host:

- tells the player, naming the turn and the parts that differ, for example
  "Turn 12: your copy of the game differed from the host's (vehicles, empires). The
  host sent its game again.";
- logs the same, with the player's name, in its own log (the server's output, or the
  host's `opense4.log`);
- sends its game again. The player's game replaces its copy and gives the orders of
  the turn so far again on the new copy; an End Turn already given stays given. The
  orders that reached the host still count, checked against the host's game as always.

The player's game shows the message on the network status strip and in the Chat
window's log, and writes it to `opense4.log`. A desync is never silent. It means a bug, for example two platforms
reading the game differently, so a report with both logs helps.

### Computer control

The host's toggle ("Toggle Empire AI On/Off", spec 05 §9.4) flips only the empire's
computer-controlled mark (`HostSession::setAiControl`, `game::ai::setComputerMark`): its
ministers and the minister marks of its ships and colonies stay as they were. A marked empire
is played by the computer every turn, the host waits for no orders from it, and every player
sees it as a computer player from the next turn. Handed back, the host waits for its player's
orders again (or the host plays it). A kicked player's empire is played by the computer for
each turn without changing the mark. Empires that were computer players from the start cannot
be handed to a human.

The Game Menu's `Players` window (spec 06 §1.2.1) is something else: on a player's machine it
changes only that player's copy of the game. For the player's own empire it also gives the
orders that switch all its ministers on (or off), which reach the host with the turn's orders;
the host still counts the empire as a human that must send orders.

### Reset Passwords

The host of a simultaneous game can give players new passwords between turns (spec 06 §1.9):
Game Menu, `Options`, `Reset Passwords`, then pick the empires. Each gets six digits (three
random numbers from 11 to 99, drawn from a source apart from the game's random numbers), shown
to the host only. The new passwords take effect when the next turn is processed, after the
players' orders (which may carry a password change of their own) have been read. They are kept
in memory only: a new `Reset Passwords` discards earlier ones not applied yet, and stopping the
host loses them.

- A dedicated server has no window: an administrator's client offers the same button, and the
  passwords come back to that administrator only, in the chat log (and the server's log).
- A PBEM host passes `--reset-passwords=N,M` to `pbem process`; the new passwords are printed.

### When orders are missing

When a turn is processed without some human empire's orders, the computer plays
that empire for the turn. An empire can set the option "the computer should only
keep things running" (`cmd::SetEmpireOptions::aiMinimalChanges`), and then the
computer makes only minimal changes in its place. In a turn-based game the same
holds for the rest of the turn of a player who is not there (see below).

## Turn-based games

In a turn-based game (spec 05 §8) the empires take their turns in empire order. During
a player's turn every order is carried out at once: ships move and spend their movement
points, a battle is fought the moment a group enters a sector with enemies, and
messages take effect when they are sent. When the player ends the turn, that empire's
end-of-turn processing runs and the next empire's turn begins. After the last empire,
the date advances and the once-per-turn steps (design cleanup, victory check, events)
run.

The classic game offers its network connection for simultaneous games only; there, a
turn-based game on different machines passes the save file from player to player
(spec 05 §9.1). OpenSE4 plays turn-based games over the network too, and by e-mail it
keeps the host in charge of the game file. Choices the spec leaves open are marked
(inferred) below and listed in spec 05's open question 33.

### Over the network

The host runs the game. Choose "Turn-based" as the turn style in the host form, start
`opense4-server` with `--turn-based`, or set `simultaneous = false` in a setup file.

- **Only the player whose turn it is can act.** Everyone sees whose turn it is. The host
  refuses commands and End Turn from anyone else, including a client that ignores the
  turn order.
- **Each command is carried out on the host at once.** The client sends it, the host
  carries it out with the game rules, and the player gets their new view of the game at
  once, then the result: which commands were refused and why.
- **Attack Sector questions.** When a move stops before a sector with enemy forces, the
  player is asked whether to enter it (spec 03 §6.2). The question is part of the game
  state. It survives a reconnect, a saved game and a host restart, and the other players
  never see it (inferred).
- **Other players** get their view of the game each time the turn passes to the next
  player. A player who took part in a battle fought in someone else's turn gets their
  view at once, and can watch the battle (inferred). The others see nothing of the turn
  in progress.
- **End Turn** runs the player's end-of-turn processing. The computer players then take
  their turns on the host, and the next human's turn starts.
- **A player who is away is waited for**, as in a simultaneous game. The host stops
  waiting when:
  - the turn time limit runs out (in a turn-based game it counts for each player's
    turn);
  - the host forces the turn on;
  - the empire is handed to the computer or kicked.

  The computer then plays the rest of that player's turn the way it plays a missing
  player's turn in a simultaneous game: all ministers on for that turn, or only
  bookkeeping if the empire chose minimal changes (inferred). An empire handed to the
  computer is played by the computer every turn until it is handed back. When every
  human is handed to the computer, the host plays one game turn each time the turn is
  forced on or the time limit runs out.
- **Reconnecting** in the middle of a turn resumes the game where it is. A player
  whose turn it is goes on with it, including the questions still open.
- **Saving** is possible in the middle of any player's turn, and `--load` continues the
  game at that player's turn.

### By e-mail

The classic game passes the whole game file from player to player. That would show each
player everyone's secrets, so OpenSE4 keeps the game with the host and has the host
process each player's turn in between; the players exchange only their own views and
orders:

1. `pbem new` with `simultaneous = false` in the setup file creates the game. The
   computer players before the first human take their turns, and the command writes the
   turn file of the player whose turn it is and names the empire to send it to.
2. That player opens their turn file in the game ([Playing your turn](#playing-your-turn))
   and plays their turn on it. Every command is carried out at once on the player's
   copy, as a preview: the copy is the player's own view, so it lacks what the player
   cannot see and has other random numbers, and battles in it may end differently. At
   End Turn the game writes a `.plr` holding every command in the order it was given
   (Attack Sector answers and refused commands included), with the checksum of the turn
   file it was made from. `opense4-server pbem orders` writes a turn without commands.
3. `pbem process` checks the `.plr` like any other: game, turn, empire, the turn file it
   was made from and the password's signature. It also checks that the file is for the
   player whose turn it is. It then carries the commands out one after another on the
   whole game, ends the player's turn (the computer players move), writes the new `.gam`
   and the next player's turn file, and names the empire whose turn is next. Files for
   another player, or made from another turn file, are reported and kept.
4. If no file for the player whose turn it is has arrived, `pbem process` has the
   computer play that turn, as for missing orders (inferred).

The whole game decides: a battle, an enemy the player could not see, or a ship lost in a
battle that ended otherwise can make the host's result differ from the preview, and
commands that no longer fit are refused. The next turn file shows what happened, and
`pbem process` lists the refused commands. `pbem info` says whose turn it is.

## UPnP and port forwarding

Players outside the host's home network can only reach it if the router passes
TCP port 6720 on to the host. With UPnP, the host asks the router to do that itself:

- At start the host looks for a UPnP Internet gateway on the local network for up
  to two seconds. This happens in the background; the lobby is already open.
- If it finds one, it asks for a forwarding of TCP 6720 to this machine, named
  "OpenSE4", with a one-hour lease that it renews every half hour while the game
  runs. Routers that only allow permanent forwardings get one.
- If another machine already uses 6720 on the router, it tries the next eight ports
  (6721 to 6728) as external ports, all leading to 6720 on the host. The log then
  says which address and port players should use. A stale forwarding left behind
  by an earlier run on this same machine is replaced.
- The log line reports the outcome, including the router's public address. If that
  address is itself private (double NAT, carrier-grade NAT), the log warns that
  players outside may still not get through.
- When the game stops, the forwarding is removed.

If no router answers, the router refuses, or UPnP is off (`--no-upnp`, or a build
configured with `-DOPENSE4_ENABLE_UPNP=OFF`), the log says so and names the address
to forward to. Players on the same local network can always join directly.

### Forwarding the port by hand

1. Find the host's local address. The server prints it in its UPnP message. You can
   also use `ip addr` (Linux), `ipconfig` (Windows) or System Settings, Network (macOS).
   It usually looks like `192.168.x.y` or `10.x.y.z`.
2. Give the host a fixed local address in the router's DHCP settings, so the
   forwarding keeps working after a restart.
3. In the router's settings (often called "Port Forwarding", "Virtual Servers" or
   "NAT"), forward **TCP port 6720** (external) to port 6720 on the host's local
   address. UDP is not needed.
4. Players connect to the router's public address, which sites like "what is my IP"
   show. If your provider gives you no public IPv4 address (carrier-grade NAT), port
   forwarding cannot work. Use a VPN or overlay network that puts everyone on one
   virtual LAN instead.

### Firewalls

The host's own firewall must allow incoming TCP on the port:

- **Windows**: allow `opense4` / `opense4-server` when Windows Defender Firewall
  asks, or add an inbound rule for TCP 6720.
- **Linux**: for example `sudo ufw allow 6720/tcp`, or
  `sudo firewall-cmd --add-port=6720/tcp --permanent && sudo firewall-cmd --reload`.
- **macOS**: allow incoming connections when asked (System Settings, Network,
  Firewall).

For LAN discovery, also allow incoming UDP 6716 on the host (for example
`sudo ufw allow 6716/udp`). Without it, players on the LAN can still join by typing
the address.

Players need no incoming ports: they only connect out.

## Play by e-mail

A PBEM game is a `.gam` file that stays with the host, a turn file (`.turn`) for each
player, and the `.plr` orders files the players send in. The `.gam` holds the whole game,
every empire's secrets included: never send it to a player. A turn file holds what one
empire knows of the game, exactly what a network host sends that player
([Security](#security)). One round goes like this:

1. **Create the game** from a [setup file](#setup-files):

   ```sh
   opense4-server pbem new --setup=campaign.toml --out=campaign.gam
   ```

   This also writes the turn files of the players who play first, named
   `<game>_<NN>.turn` (NN is the empire's number), next to the game (or in
   `--turn-files=DIR`), and lists which file goes to which player.

2. **Send each player their own turn file.** Each player opens it in the game, plays
   their turn, and End Turn saves their orders as a `.plr` file
   ([Playing your turn](#playing-your-turn); the game uses
   `net::pbem::writePlayerOrders`). The file is named `<game>_<NN>.plr`. It holds only
   that empire's orders, marked with the game, the turn, the empire and the checksum of
   the turn file they were made from, and signed with the empire's password. It holds
   no password, nor any hash of one: a copy of it lets nobody send orders for that
   empire, or for another turn.

   A player who wants to end the turn without changes can make an empty orders file:
   `opense4-server pbem orders --turn=campaign_02.turn --password=... --out=DIR`.

3. **Collect the `.plr` files** in one directory, then process the turn:

   ```sh
   opense4-server pbem process --game=campaign.gam --orders=inbox --password=boss
   ```

   `--reset-passwords=2,5` gives empires 2 and 5 new passwords once their orders have been
   read; they are printed for the host to pass on.

   For every `.plr` file the host checks the game, the turn, the empire, that it was
   made from the turn file the host would make now from its own game, and the
   password's signature. It reports and skips files that fail a check: another game,
   out of date, made from another turn file, wrong password or changed after signing,
   damaged. If two files are for the same empire, the newer one counts. The computer
   plays human empires that sent nothing. The turn is processed, `campaign.gam` is
   replaced (the previous turn is kept as `campaign.gam.bak`), the new turn files are
   written, and the `.plr` files that were used are deleted (`--keep-orders` keeps
   them). Files that were skipped stay where they are.

4. **Send out the new turn files** and repeat.

`opense4-server pbem info --game=campaign.gam` shows the turn, the empires, their
players and whether a master password is set; given a turn file, it shows that player's
view. `opense4-server pbem turn-files --game=campaign.gam` writes the current turn files
again, for a lost file, or to go on with a game made by OpenSE4 0.6.

**Games of OpenSE4 0.6** go on after `pbem turn-files`. Their password verifiers are of
the old kind, which cannot check a signature: the turn file says so, and the player
chooses a new password with their next turn (the Play by E-mail window asks for it;
`pbem orders --new-password`). That `.plr` carries the old password's hash once, as 0.6
did, and is signed with the new password. The host checks the old one and keeps the new
one's verifier from then on, so the old hash, which anyone reading that mail could see,
is worth nothing afterwards.

### Playing your turn

In the game, choose **Multiplayer**, then **Play by E-mail**:

1. Open the turn file the host sent you. The window lists the `.turn` files in the
   `pbem` folder of your OpenSE4 user data (on Linux `~/.local/share/OpenSE4/pbem`), or
   you type the file's path. The game must have been made with the same data set as
   yours, as the host checks too, and the file's view must match its checksum. A host's
   `.gam` is refused: it is not meant for players.
2. Enter your empire's password. It is checked against the turn file the way the host
   checks your `.plr`, so a wrong password is caught at once. In a turn-based game only
   the player whose turn it is gets a turn file.
3. Choose where the orders file goes: by default next to the turn file.
4. Play the turn. A simultaneous game works like a local one: you give orders and they
   are carried out when the host processes the turn. In a turn-based game every order
   is carried out at once on your copy, as a preview of what the host will carry out on
   the whole game ([By e-mail](#by-e-mail)).
5. **End Turn** writes `<game>_<NN>.plr`, signed with your password, and ends the turn on
   your machine; the status line names the file. Send it to the host.

**Save Game** in the Game Menu keeps the turn so far, unsigned, in the `pbem/drafts`
folder (not with the orders, so it cannot be sent by mistake). Open the same turn file
again later, and the orders given so far come back. End Turn removes the draft. A
password changed during the turn (Empire Status, Change Password) counts from the next
turn; this turn's `.plr` is still signed with the password you opened it with.

From the command line:

```sh
opense4 --pbem=campaign_02.turn --pbem-password=PW [--pbem-orders=DIR]
```

`--pbem-end-turn` ends the turn at once, writes the `.plr`, prints where and quits (for
scripts). `--open=pbem:campaign_02.turn` opens the Play by E-mail window with that file.
The choices the rules leave open are listed in spec 05 open question 36.

Battles in a PBEM game are always fought strategically; the Tactical or Strategic
question is only asked in local and hotseat turn-based games.

**Load Game** (not Play by E-mail) opens a host's PBEM game file or network save as an
ordinary local or hotseat game on the host's machine, for instance to go on without the
other players. The empire passwords keep working as they did in the multiplayer game, and the
game saved from there keeps them that way.

## Setup files

`pbem new`, and the network server's `--setup`, read a TOML file. Every key is
optional, and an unknown key is an error, to catch typos:

```toml
name = "Campaign"              # game name
seed = 1234                    # galaxy seed; random when missing
master_password = "boss"       # or master_password_hash = "<opense4-server hash-password output>"

[options]
quadrant_size = 1              # 0 small, 1 medium (the default), 2 large: the number of systems is rolled
systems = 30                   # optional: exactly this many systems instead (0 = rolled from quadrant_size)
quadrant = "Name From QuadrantTypes"   # default: the data set's first quadrant type
start_tech = 0                 # 0 low, 1 medium, 2 high
starting_resources = [20000, 20000, 20000]
racial_points = 2000
events = 2                     # 0 none .. 3 high
max_event_severity = 2
tech_cost = 1                  # 0 low, 1 medium, 2 high
home_planet_value = 1
starting_planets = 1
max_ships = 200
max_units = 1000
ai_difficulty = 1              # 0 low, 1 medium, 2 high: the level random computer players get
ai_bonus = 0                   # 0 none, 1 low, 2 medium, 3 high
score_display = 1              # 0 own, 1 own and Non-Aggression or better, 2 all
# true/false: all_warp_points_connected, all_planets_same_size, no_warp_points, warp_points_anywhere,
# all_systems_seen, omnipresent, finite_resources, same_system_allowed,
# evenly_distributed, no_tactical_combat, allow_gifts, allow_tech_trades,
# allow_intel, allow_surrender (on by default), no_ruins, only_breathable, only_home_type, team_mode,
# simultaneous (the default; false plays a turn-based game)

[options.victory]              # each key switches that condition on
score = 50000
years = 100
percent_of_second = 200
tech_percent = 75
peace_years = 20
delay_years = 10

[[empire]]
name = "Northern Compact"      # default: from the race
race = "RaceFolder"            # a race preset of the data set; default: presets in turn
tier = 0                       # the preset's build tier, 0 to 2
kind = "human"                 # human, computer or neutral
player = "alice"               # the player's name, shown in logs and used to log in
password = "alices-password"   # or password_hash = "<opense4-server hash-password output>"
color = 0x3070ff
empire_type = ""
leader = ""
leader_title = ""
minister_style = "Aggressive"  # a folder under Ai/ of the install; default: none (the race's own AI files)
use_race_minister_style = false
```

The network server takes the name, seed, options, master password and computer
empires from the file. Human players join through the lobby, so it ignores human
`[[empire]]` entries.

Setup files hold passwords in plain text. To avoid that, a player can run
`opense4-server hash-password PW` and send the host only the output, which goes into
`password_hash`.

## Security

Network games run over encrypted connections, play-by-e-mail players get only their
own view, and passwords prove themselves without ever travelling. This is OpenSE4's own
design: the classic game has no counterpart.

### Threat model

- **Someone who watches the traffic** (a shared Wi-Fi, a provider, a router) sees that
  two machines talk, how much and when, and the program versions in the first two
  messages. They see nothing of the game, the chat, the player names or the
  passwords.
- **Someone who changes, repeats, drops or reorders the traffic** breaks the
  connection. Nothing they inject is accepted; the player's game reconnects
  ([Reconnecting](#reconnecting)).
- **An impostor host** (someone who makes a player connect to them instead of the
  host, a "man in the middle"):
  - fails in a game with a join password unless it knows that password. The password
    is part of the session keys, so without it the player's login cannot even be
    read. A player who gives a join password refuses a host that asks for none.
  - is refused once a player has met the real host: every host has a long-term key,
    which the player's game remembers per address and port and checks at every later
    connection ("trust on first use"). The first connection is the weak spot: compare
    the fingerprint the lobby shows with the host's.
  - learns, when it does get a player to talk to it (an open game, first contact),
    the player's name and verifier, and signatures that are worthless on any other
    connection. It cannot log in as the player with them, only try to guess the
    password offline.
- **Passwords never travel**, not even hashed. A player's game proves it knows the
  password by signing (the session it logs in on, or a PBEM orders file); hosts and
  saved games keep only a verifier, which can check such a signature but not make one.
  A copy of a saved game, a turn file or an orders file holds nothing anyone could log
  in or sign with, and an orders file cannot be changed or used for another turn. Weak
  passwords can still be guessed offline from a verifier or a signature, and anyone who
  can reach the host can try join passwords there, one connection at a time: do not
  reuse important passwords.
- **Not protected:** the host itself (it sees and decides everything), the host's key
  file (whoever copies it can pose as the host: it is readable by its owner only), the
  players' own computers, and the timing and size of the traffic. Anyone who can cut
  the connection can stop the game.

### How it works

- **Cryptography.** [Monocypher](https://monocypher.org) 4.0.2 (BSD 2-clause or CC0,
  pinned by hash): X25519 key agreement, XChaCha20-Poly1305 authenticated encryption,
  BLAKE2b hashing and EdDSA signatures. OpenSE4 implements no primitive itself. Keys
  come from the operating system's random source (`getrandom`, `arc4random_buf`,
  `BCryptGenRandom`), never from the game's random numbers.
- **Handshake** (`net/secure.hpp`), in the Noise pattern NX with the join password's
  key mixed in last (NXpsk2):
  1. The client sends a fresh X25519 key (`ClientHello`, in the clear).
  2. The host answers with a fresh key of its own, its long-term key and whether it
     has a join password (`ServerHello`, in the clear).
  3. Both derive the keys with BLAKE2b from the two messages as sent, the agreement of
     the two fresh keys, the agreement of the client's fresh key with the host's
     long-term key, and the join password's key (or zeros). Changing either message
     changes the keys; only the holder of the long-term key's secret half gets them.
  4. The client checks the host's long-term key against the one it remembers, then
     sends its `Login`, the first encrypted message.
- **Frames.** Every frame after the handshake is sealed: the message type and payload
  are encrypted, and they and the frame's header are authenticated. Each direction has
  its own key and counts its messages; the count is the nonce. A frame that was
  changed, repeated, dropped or moved fails to open and ends the connection. A host
  whose first sealed message from a client does not open (the keys differ: a wrong
  join password) says so in the clear and closes.
- **Logins.** A password hash (SHA-256 of the password with an OpenSE4 prefix, made on
  the player's machine) is the seed of an EdDSA key; the verifier is its public half
  (`pk1:` and 64 hex digits). The login signs a BLAKE2b hash of the session's id, the
  role (player or master) and the player's name; the host checks it with the
  verifier. A new player's login carries the verifier, which the host keeps. The
  master password works the same way.
- **Games of OpenSE4 0.6.** Their verifiers (a second SHA-256) cannot check a
  signature. For such a game the host asks the login for the password hash itself,
  inside the encrypted connection, checks it and replaces the verifier by the new
  kind. That hash is also the seed of the player's signing key, so the player's game
  sends it only to a host whose key the player trusted beforehand (remembered from an
  earlier game, or confirmed in the lobby after comparing fingerprints), or that knows
  the join password; a host met for the first time could be someone claiming an old
  game to collect it. A PBEM empire of the old kind moves to a new password instead
  ([Play by e-mail](#play-by-e-mail)), because its old hash travels in the clear.
- **Orders files** sign a BLAKE2b hash of the game, the turn, the empire, the orders,
  the checksum of the turn file they were made from and the signer's verifier.
- **Host keys** are files of 64 hex digits (`host_key.txt`), made on first use and
  readable by their owner only; the players' remembered keys are lines of
  `<address>:<port> <key>` in `known_hosts.txt`. Delete a line there to meet that
  host anew.

### Other rules

- **The host is authoritative.** It checks every order list against the game rules
  and applies only the sender's own empire's orders. In a turn-based game it takes
  commands only from the player whose turn it is. Clients never change the host's
  game. Garbage, a stale turn or a repeated message never reaches the host's turn
  processing: unreadable orders and orders for another turn or empire are refused, a
  repeat replaces (orders) or is answered again (commands), and a malformed message
  ends that connection only. The host also refuses oversized messages (64 KiB before the login, 16 MiB
  after), malformed messages, unknown message types, anything before a valid
  handshake and anything in the clear after it. Connections that go silent are
  dropped.
- **Each player gets only their own view.** Each turn, the host sends every
  player the game as their empire knows it (`game::redactForEmpire`):
  - Removed:
    - other empires' treasuries, research and intelligence queues, logs, history
      records and plans;
    - ships the player cannot see this turn;
    - colonies in systems the player has not explored;
    - colonies hidden from the player by their cloak (the planet's cloak beats the
      player's sensors there): the colony is left out and its planet taken off its
      system's list, so the player's windows show nothing there, as the original's do
      (spec 01 §6.9). A colony the player merely has no sensors near stays, and the
      windows show its planet as uncolonized, as the original's do;
    - the contents of foreign colonies;
    - the details of designs the player has never seen, or has forgotten (designs not
      seen for 50 turns), and every foreign design's statistics (built, lost, kills,
      enemy tonnage destroyed);
    - other empires' messages;
    - battles the player was not in;
    - in a turn-based game, what the player whose turn it is has done in that turn (the
      moves of its ships, its launches and its open questions), for everyone else;
    - the random-number state.
  - Kept: what diplomacy and the score screens show.
  - Shared: a Partnership gives its partner the maps and tech levels, as in the rules.

  The host's own player sees the same view. In a turn-based game the other players get
  that view when the turn passes on, not after every command. A PBEM player's turn file
  is the same view, made by the same function; the host's `.gam` never leaves the host.
  A turn file also carries the game's header (name, empires' and players' names, the
  game's id and data set) and its own empire's password verifier, so the player's game
  can check the password. The master password's verifier stays with the host.
- **The host itself must be trusted.** It sees and decides everything.
- **Files are checked before use.** Save, turn, orders and `.plr` files start with a
  type tag, a format version and a checksum. Loading rejects wrong types, newer or
  retired versions, truncation, corruption and internal inconsistencies (for example
  a ship whose design does not exist), and always reports what is wrong. Hosts also
  refuse games whose data-set fingerprint differs from their own, and check that
  every hull, component and facility the game uses exists in their data set.

## Future work

- IPv6 hosting.
- Sending only the changes between turns instead of the whole game. A turn-based game
  sends the player's whole view after every command.
- Processing the turn on a background thread, so an in-game host's interface stays
  responsive during long turns.
- Movement and combat replays for players (the classic `.trn` and `.cmb` files).
- Rate limits for chat and for failed password attempts.

## For developers

### Library API (`src/net`)

Both sessions are single-threaded and polled: call `poll(timeoutMs)` every frame (or
in a loop), and handle the `net::Event`s it returns. Neither class is thread-safe.
UPnP runs on a thread of its own inside `net::PortMapper`.

```cpp
net::HostConfig cfg;                         // port, slots, passwords, setup (seed, options), UPnP, limits, hostKey
cfg.hostKey = *net::secure::loadOrCreateHostKey(file);   // the host's identity (none: a new key per session)
cfg.localPlayer = net::LocalPlayer{"Host", net::hashPassword(pw), setup};   // in-game hosting
net::HostSession host(rules, cfg);           // rules must outlive the session
host.start();                                // or host.resume(state, saveInfo)
host.addComputerEmpire(setup); host.kick(slot, "reason"); host.setLocalReady(true);
host.startGame(/*force*/ false);
host.submitOrders(orders);                   // any empire; normally the host's own
host.processTurnNow(); host.setTurnTimeout(120); host.setAiControl(empire, true);   // the mark only
host.resetPasswords({empire}); host.clearPasswordResets();     // simultaneous games, applied at the next turn
host.playCommands(empire, {cmd});            // turn-based: the empire whose turn it is
host.endPlayerTurn(empire); host.activeEmpire(); host.turnBased();
host.save(path);
for (const net::Event& e : host.poll(0)) { /* e.type, e.text, e.player, e.slot, e.empire, e.turn */ }
host.lobby();  host.turnStatus();  host.state();  host.portMapping();

net::ClientConfig cc;                        // host, port, name, passwordHash, dataSet = game::dataSetIdentity(rules), hostKey (a pin)
net::ClientSession client(cc);
client.connect();
client.submitSetup(setup); client.setReady(true);
client.submitOrders(orders); client.chat("hi");
client.play(cmd); client.endTurn();          // turn-based: in our turn (client.myTurn())
client.questions(); client.pendingRequests(); client.activeEmpire();
client.seenHostKey(); client.hostKeyChanged();  // the host's key, and a refusal because it is not the pinned one
client.lobby(); client.turnStatus(); client.state(); client.empire(); client.ordersAccepted();
client.requestAiControl(empire, true); client.requestPasswordReset({empire});   // administrators
```

`net::describe(event)` gives a one-line log text. `net::pbem::*` reads and writes turn
files (`writeTurnFiles`, `readTurnFile`) and `.plr` files (`writePlayerOrders`,
`signOrdersFile`) and processes PBEM turns (`processGameFile`). The player's side in the
client is `client/classic/pbem_play.hpp` (open a turn file, check the empire and
password, write the signed `.plr` or a draft) and `ClassicSession` with
`SessionKind::Pbem`. `game::saveGame`, `loadGame` and
`readSaveInfo` handle `.gam` files.

### Protocol

TCP, one stream per player. Each message is a frame: a u32 little-endian length, a
u8 message type, then the payload, which is encoded with the save-format archive.
After the handshake every frame is sealed: its type byte is `0xf0`, followed by the
message type and payload encrypted with XChaCha20-Poly1305, then the 16-byte tag
([How it works](#how-it-works)).

1. The client sends `ClientHello` in the clear: magic, protocol version, program
   version and its fresh X25519 key. It is laid out like protocol 4's greeting, so an
   OpenSE4 0.6 host reads the version and refuses it with a readable `Reject`. Hosts
   read the magic, version and program version first, whatever follows, and refuse
   other versions the same way, in the clear.
2. The host answers `ServerHello` in the clear: its fresh key, its long-term key,
   whether it has a join password, and whether the game still has OpenSE4 0.6
   verifiers. Both sides then switch to sealed frames.
3. The client sends `Login`: data-set fingerprint, player name, a random id of its
   session object, the player's verifier and signature, and the master password's
   signature if it has one (and the password hash, only when the host asked for it
   for an old verifier and the player trusts the host's key or the session has a join
   password).
4. The host answers `Reject` (with a reason and a readable text) or `Welcome` (game
   name and id, the player's slot, admin rights). Then it sends the `Lobby`, and,
   during a game, the `State` and `TurnStatus`.
5. In the lobby: `SubmitSetup` (the empire setup), `SetReady`, `ChatSend`, and
   `Admin` requests. The host sends `Lobby` after every change.
6. At the start and after every turn, the host sends `State`: the turn, the
   player's empire, a complete `serializeState()` blob with its own checksum, and a
   serial number that grows with every `State` the host sends.
7. Each turn the player sends `SubmitOrders`, a `serializeOrders()` blob of an
   `EmpireOrders` for the current turn. A later submission replaces an earlier one.
   The host answers `OrdersAck` and sends everyone a `TurnStatus` saying who has
   sent orders.
   In a turn-based game (`TurnStatus` says so, and names the empire whose turn it is)
   the player whose turn it is sends `PlayCommands` instead: a request number and an
   `EmpireOrders` blob of commands, which the host carries out one after another. The
   host answers with the player's new `State`, then a `PlayResult` with the same
   request number and the refused commands. `EndTurn` ends the turn. The host answers
   with a `PlayResult`, then sends everyone their `State` and the new `TurnStatus` once
   the turn has passed to the next player. Anything from a player whose turn it is not
   gets a `PlayResult` saying so. Request numbers grow with every request of a client
   session (the `Login`'s id): the host answers a number it has already answered
   again, without carrying anything out twice.
8. `SubmitOrders` and `PlayCommands` also carry the player's copy of the game as it
   is when they are sent: the serial of the `State` it came from, its checksum and
   its part hashes (`game::statePartHashes`). When the serial is the last one the host
   sent that player and the checksum differs, the host sends `Desync` (the turn, the
   parts that differ, a text) and the `State` again, marked as a resync.
9. `Ping`/`Pong` keep idle connections alive (every 5 s). A peer that sends nothing
   for 60 s (host side) or 90 s (client side) is disconnected. `Bye` ends a session
   politely and carries a reason, for example a kick or a shutdown.

`net::kProtocolVersion` must change whenever the messages or the save format
change; the host refuses any other version and compares nothing else, so releases
that share it play together. It was 2 from turn-based games on, 3 in 0.5.0, 4 in
0.6.0, and is 5 from encrypted connections on (after 0.6.1).

### Save format

`game/serialize.hpp` describes the envelope: magic, format version, flags, size,
FNV-1a 64 checksum. `game/serialize_io.hpp` has one `io()` per struct, which lists
the struct's fields once and serves for writing, reading and hashing.
`stateChecksum()` is the hash of the serialized state: the same on every platform,
and usable for desync checks.

**Adding a field** to a struct in `state.hpp`, `commands.hpp` or `setup.hpp` means
adding it to that struct's `io()` line. `tests/test_serialize.cpp` compares each
struct's declared fields with its `io()` list and names any struct that is out of
step. The golden checksum test then prints the new values to paste in. Bump
`kSaveVersion` when older files can no longer be read, and `kProtocolVersion` too.
Readers can branch on `ar.version()` to keep reading older formats.

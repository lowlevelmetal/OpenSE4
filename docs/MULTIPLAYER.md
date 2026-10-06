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
the first time this computer hosts. Players' games remember it. If other users of the
computer can read that file, the lobby's log says so: make it private.

A game hosted from the client gets its galaxy's seed from the system's cryptographic
random source, since the players must not be able to guess it; `--seed=N` on the
command line makes the host use N instead. (Local and hotseat games take the clock's
seed: their players share the computer that holds the whole game.)

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
| `--host-key=FILE` | the host's long-term key (default: `host_key.txt` in OpenSE4's user folder, the one the game uses; made on first use). The log names its fingerprint, and warns when other users of the computer can read the file. The `pbem` commands take it too: they sign the turn files with it, and the orders files are encrypted to it |
| `--no-password-migration` | a game of OpenSE4 0.6 (`--load`): refuse to move a player's password from 0.6's form; such players get a new password by [Reset Passwords](#reset-passwords) only. Without it, every move is logged (`Password migration: ...`) |
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
  the player chooses **Trust the New Key**, sees both fingerprints, and confirms with
  **Yes, and Connect**; otherwise someone may be in between ([Security](#security)).
- **A game saved by OpenSE4 0.6.** A player whose password is still in 0.6's form is
  asked, once, whether to show the host that old form so that it moves to the new one.
  The game asks for two confirmations, and sends it only to a host whose key the player
  trusts ([Security](#security)). The bot's `--old-password` does the same, with
  `--host-key`.
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
  who gives a join password refuses a host that does not ask for one. Making its key
  (and the player's password keys) takes the player's computer a moment when it
  connects; the host makes its own when it starts.

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
Game Menu, `Options`, `Reset Passwords`, then pick the empires. Each gets twelve digits (six
random numbers from 11 to 99, drawn from a source apart from the game's random numbers), shown
to the host only. The original gives six digits; OpenSE4 gives more because its verifiers can
be guessed offline. The new passwords take effect when the next turn is processed, after the
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
empire knows of the game, exactly what a network host sends that player, encrypted so
that only that empire's password opens it, and signed by the host ([Security](#security)).
The `.plr` files are encrypted to the host's key (`host_key.txt` in OpenSE4's user folder,
or `--host-key=FILE`, as for network games): keep that file, or the orders sent to it can
no longer be read, and the players' games, which trust the key that signed a game's
first turn file, refuse turn files signed by another. The `pbem` commands print the
key's play-by-e-mail fingerprint, which the players' Play by E-mail window shows too:
tell the players yours so they can compare. A shared folder is therefore fine: nobody
reads another player's view or orders, and nobody passes a turn file off as the host's.
One round goes like this:

1. **Create the game** from a [setup file](#setup-files):

   ```sh
   opense4-server pbem new --setup=campaign.toml --out=campaign.gam [--host-key=FILE]
   ```

   This also writes the turn files of the players who play first, named
   `<game>_<NN>.turn` (NN is the empire's number), next to the game (or in
   `--turn-files=DIR`), and lists which file goes to which player.

2. **Send each player their own turn file.** Each player opens it in the game, plays
   their turn, and End Turn saves their orders as a `.plr` file
   ([Playing your turn](#playing-your-turn); the game uses
   `net::pbem::writePlayerOrders`). The file is named `<game>_<NN>.plr`. It holds only
   that empire's orders, marked with the game, the turn, the empire and the checksum of
   the turn file they were made from, signed with the empire's password and encrypted to
   the host's key. It holds no password, nor any hash of one: a copy of it lets nobody
   read the orders, or send orders for that empire or for another turn.

   A player who wants to end the turn without changes can make an empty orders file:
   `opense4-server pbem orders --turn=campaign_02.turn --password=... --out=DIR`.

3. **Collect the `.plr` files** in one directory, then process the turn:

   ```sh
   opense4-server pbem process --game=campaign.gam --orders=inbox --password=boss [--host-key=FILE]
   ```

   `--reset-passwords=2,5` gives empires 2 and 5 new passwords once their orders have been
   read; they are printed for the host to pass on. `--no-password-migration` refuses the
   moves of OpenSE4 0.6 passwords described below.

   For every `.plr` file the host checks the game, the turn, the empire, that it was
   made from the turn file the host would make now from its own game, and the
   password's signature. It reports and skips files that fail a check: another game,
   out of date, made from another turn file, wrong password or changed after signing,
   for another host's key, setting a password of the wrong kind, damaged. If two files
   are for the same empire, the one made later counts (by the revision number signed in
   it, not by the file's date). The computer
   plays human empires that sent nothing. The turn is processed, `campaign.gam` is
   replaced (the previous turn is kept as `campaign.gam.bak`), the new turn files are
   written, and the `.plr` files that were used are deleted (`--keep-orders` keeps
   them). Files that were skipped stay where they are.

4. **Send out the new turn files** and repeat.

`opense4-server pbem info --game=campaign.gam` shows the turn, the empires, their
players and whether a master password is set; given a turn file, it shows its header
(the view itself opens with the empire's password only). `opense4-server pbem turn-files --game=campaign.gam` writes the current turn files
again, for a lost file, or to go on with a game made by OpenSE4 0.6.

**Games of OpenSE4 0.6** go on after `pbem turn-files`. Their password verifiers are of
the old kind, which cannot check a signature: the turn file says so, and the player
chooses a new password with their next turn (the Play by E-mail window asks for it;
`pbem orders --new-password`). That turn file is in the clear (an old verifier has no
key to encrypt to), but signed by the host. The `.plr` carries the old password's hash
once, as 0.6 did, and is signed with the new password; the player's game writes that
hash only after the player compared the host key's fingerprint with the one the host
sees and agreed twice (`pbem orders --confirm-old-password`). The host checks the old
one and keeps the new one's verifier from then on, so the old hash, which anyone
reading that mail (or the 0.6 mails) could see, is worth nothing afterwards; its
report lists each such move (`password migration: ...`). If two different moves to a
new password arrive for one empire, the host takes neither and says so: ask the player
which is theirs, or reset the password (`--reset-passwords`). A host that wants no such
moves at all processes with `--no-password-migration` and resets those passwords.

### Playing your turn

In the game, choose **Multiplayer**, then **Play by E-mail**:

1. Open the turn file the host sent you. The window lists the `.turn` files in the
   `pbem` folder of your OpenSE4 user data (on Linux `~/.local/share/OpenSE4/pbem`), or
   you type the file's path. The game must have been made with the same data set as
   yours, as the host checks too, and the file's view must match its checksum. A host's
   `.gam` is refused: it is not meant for players.

   The window shows the fingerprint of the host key that signed the file. The game's
   first turn file on this computer makes that key the trusted one for the game
   (`known_hosts.txt`, like a network host's): compare it with the one the host sees.
   A later turn file signed by another key is refused unless you choose **Trust the
   New Key** and confirm; do that only if the host says it made a new key (a new
   computer, a lost key file) and its fingerprint is the one shown.
2. Enter your empire's password. It opens the turn file's view (nothing else does), so a
   wrong password is caught at once. In a turn-based game only the player whose turn it
   is gets a turn file.
3. Choose where the orders file goes: by default next to the turn file.
4. Play the turn. A simultaneous game works like a local one: you give orders and they
   are carried out when the host processes the turn. In a turn-based game every order
   is carried out at once on your copy, as a preview of what the host will carry out on
   the whole game ([By e-mail](#by-e-mail)).
5. **End Turn** writes `<game>_<NN>.plr`, signed with your password and encrypted to the
   host's key, and ends the turn on your machine; the status line names the file. Send
   it to the host.

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
scripts). It trusts a game's host key on first use like the window, but never a changed
one, and does not move an OpenSE4 0.6 password. `--open=pbem:campaign_02.turn` opens the Play by E-mail window with that file.
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
mods = ["example.better-carriers", "mods/my-tweaks"]   # mods: ids in the mods folder, or paths from this file's folder
seed = 1234                    # galaxy seed; random when missing
game_id = 4711                 # the game's id, which salts its passwords; random when missing
master_password = "boss"       # or master_password_verifier = "<password-verifier output>" (needs game_id)
ai = "example.admiral:Admiral" # who plays the computer empires: a player of one of the game's mods, or "builtin" (the default)

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
ai_sees_everything = false     # script computer players see the whole game, not only what their empire knows
ai_planning_budget = 200000000 # bytecodes a script player may run for a politics, orders or economy call
ai_call_budget = 5000000       # ... for any other call
ai_memory_limit = 1048576      # the most a script player's memory may take, as JSON text
rules_hook_budget = 50000000   # bytecodes one call of a mod's rules function may run (docs/sdk/rules.md)
rules_turn_budget = 2000000000 # ... all of one mod's rules functions in one game turn
mod_data_limit = 1048576       # the most one mod's data on one thing may take, as JSON text
# true/false: all_warp_points_connected, all_planets_same_size, no_warp_points, warp_points_anywhere,
# all_systems_seen, omnipresent, finite_resources, same_system_allowed,
# evenly_distributed, no_tactical_combat, complete_tech_tree, allow_gifts, allow_tech_trades,
# allow_intel, allow_surrender (on by default), no_ruins, only_breathable, only_home_type, team_mode,
# simultaneous (the default; false plays a turn-based game)

[options.mod."example.crowding"]   # the options a game's mod declares ([[rules.options]], docs/sdk/rules.md)
crowding_limit = 85            # a whole number in the option's range, or true/false for a switch

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
password = "alices-password"   # or password_verifier = "<password-verifier output>" (needs game_id)
color = 0x3070ff
empire_type = ""
leader = ""
leader_title = ""
minister_style = "Aggressive"  # a folder under Ai/ of the install; default: none (the race's own AI files)
use_race_minister_style = false
ai = "builtin"                 # computer and neutral empires: who plays this one (default: the file's `ai` for a computer empire)
```

The network server takes the name, seed, game id, options, master password, mods and
computer empires from the file. A computer empire's `ai` names a script computer player
of one of the game's mods (docs/sdk/ai-protocol.md); `--ai=<mod id>:<player>` on the
command line gives one to every computer empire, the file's included, and `--ai=N` adds N
computer empires. The host runs the script players: their memory stays in its game, and
players' copies never hold it. `--mod` on the command line takes the place of the file's
`mods`. Without either, a host continuing a saved game (`--load`, `pbem process`, `pbem
turn-files`) finds the game's own mods by id and identity in the mods folder (`Mods/` in
the user folder, or `--mods-dir`). Human players join through the lobby, so it ignores human
`[[empire]]` entries.

Setup files hold passwords in plain text. To avoid that, the host fixes the game's id
(`game_id`) and tells the players; each runs
`opense4-server password-verifier --game-id=4711 PW` and sends the host only the output,
which goes into `password_verifier`. A verifier holds nothing anyone could log in with,
so it may travel by any means; it belongs to that one game. The `password_hash` and
`master_password_hash` of OpenSE4 0.6 setup files are refused: they made the same key
in every game.

## Security

Network games run over encrypted connections; play-by-e-mail players get only their own
view, readable by them alone, and send orders only the host can read; passwords prove
themselves without ever travelling. This is OpenSE4's own design: the classic game has
no counterpart.

### Threat model

- **Someone who watches the traffic** (a shared Wi-Fi, a provider, a router) sees that
  two machines talk, how much and when, the program versions in the first two
  messages, the host's public key and the game's id. They see nothing of the game, the
  chat, the player names or the passwords.
- **Someone who changes, repeats, drops or reorders the traffic** breaks the
  connection. Nothing they inject is accepted; the player's game reconnects
  ([Reconnecting](#reconnecting)). A refusal sent before the connection is secured is
  shown as "the host said, before the connection was secured", as anyone could have
  sent it.
- **An impostor host** (someone who makes a player connect to them instead of the
  host, a "man in the middle"):
  - fails in a game with a join password unless it knows that password. The password
    is part of the session keys, so without it the player's login cannot even be
    read. A player who gives a join password refuses a host that asks for none. But an
    impostor who wins a player's first connection can try to guess a weak join
    password offline (one Argon2id run per guess, see below): choose one that is not
    easy to guess.
  - is refused once a player has met the real host: every host has a long-term key,
    which the player's game remembers per address and port and checks at every later
    connection ("trust on first use"). The first connection is the weak spot: compare
    the fingerprint the lobby shows with the host's. A changed key is trusted only after
    the player saw both fingerprints and confirmed twice.
  - learns, when it does get a player to talk to it (an open game, first contact),
    the player's name and verifier, and signatures that are worthless on any other
    connection. It cannot log in as the player with them, only try to guess the
    password offline.
- **Passwords never travel**, not even hashed. In each game a password stands for keys
  made from the password itself with Argon2id, salted with the game's id: a player's
  game proves the password by signing (the session it logs in on, or a PBEM orders
  file), and PBEM turn files are encrypted to it. Hosts and saved games keep only a
  verifier, which can check a signature and encrypt, but not sign or decrypt. A copy of
  a saved game, a turn file or an orders file holds nothing anyone could log in or sign
  with, and an orders file cannot be changed or used for another turn. A password can
  still be guessed offline from a verifier or a signature, but each guess costs an
  Argon2id run (128 MiB of memory) and helps with that one game only: do not use weak
  passwords, and do not reuse important ones.
- **Other players of an e-mail game**, even with access to the same shared folder, read
  neither another's turn file (encrypted to that empire's password) nor another's
  orders (encrypted to the host's key). Nor does any player's copy of the game hold the
  galaxy's seed, from which the whole map could be rebuilt (and no host, in the game
  or dedicated, takes a seed anyone could guess, unless one is given). Nor can anyone
  pass a turn file off as the host's: the host signs every turn file, and a player's
  game trusts the key that signed the game's first turn file it opened and refuses
  any other unless the player accepts a new key after comparing fingerprints. The first
  turn file is the weak spot, as the first connection is for network games.
- **Games of OpenSE4 0.6** sent their password hashes in the clear (logins and orders
  files). Nothing new is ever made of such a hash: a player whose empire still has 0.6's
  kind of verifier moves to the new kind once, showing the old hash to the host
  ([How it works](#how-it-works)). Anyone who recorded those 0.6 games knows that hash
  too and could make the move first; if that matters, the host resets the passwords
  instead ([Reset Passwords](#reset-passwords)), and can refuse such moves altogether
  (`--no-password-migration`). Every move is in the host's log.
- **Not protected:** the host itself (it sees and decides everything), the host's key
  file (whoever copies it can pose as the host and read the PBEM orders: it is readable
  by its owner only, and the host is warned when it is not), the players' own computers,
  and the timing and size of the traffic. Anyone who can cut the connection can stop
  the game. A computer without the memory Argon2id needs (128 MiB at once) cannot make
  password keys: it says so, and nothing else happens.

### How it works

- **Cryptography.** [Monocypher](https://monocypher.org) 4.0.2 (BSD 2-clause or CC0,
  pinned by hash): X25519 key agreement, XChaCha20-Poly1305 authenticated encryption,
  BLAKE2b hashing, EdDSA signatures and Argon2id. OpenSE4 implements no primitive
  itself. Keys come from the operating system's random source (`getrandom`,
  `arc4random_buf`, `BCryptGenRandom`), never from the game's random numbers. Secrets
  that are no longer needed (fresh key halves, session keys) are wiped.
- **Password keys** (`net/auth.hpp`). Argon2id of the password (128 MiB, three passes,
  salted with the game's id) is the seed of two key pairs: an EdDSA key that signs, and
  an X25519 key that opens the empire's turn files. The verifier is the Argon2id work
  and their public halves: `pk2:<KiB>:<passes>:` and 128 hex digits, so a verifier
  is checked with the work it was made with even after the default changes. A
  verifier that is malformed, names a work out of bounds (8 KiB to 1 GiB, 1 to 16
  passes) or holds a key of small order is refused, with words saying which. A host
  that keeps a verifier of another work than the one a player's game used says so
  (another OpenSE4 version) rather than "wrong password". Argon2id runs on the
  players' machines (a moment, once per game: the result is kept while the program
  runs, and wiped at exit) and when a host starts (its own player, the master and join
  passwords); a host otherwise only checks signatures.
- **Handshake** (`net/secure.hpp`), in the Noise pattern NX with the join password's
  key mixed in last (NXpsk2):
  1. The client sends a fresh X25519 key (`ClientHello`, in the clear).
  2. The host answers with a fresh key of its own, its long-term key, whether it has a
     join password, and the game's id (`ServerHello`, in the clear).
  3. Both derive the keys with BLAKE2b from the two messages as sent, the agreement of
     the two fresh keys, the agreement of the client's fresh key with the host's
     long-term key, and the join password's key (Argon2id salted with the host's key
     and the game's id; zeros without one). Changing either message changes the keys;
     only the holder of the long-term key's secret half gets them.
  4. The client checks the host's long-term key against the one it remembers, then
     sends its `Login`, the first encrypted message. Until the host's `Welcome` (sealed)
     it reads only frames of 64 KiB or less.
- **Frames.** Every frame after the handshake is sealed: the message type and payload
  are encrypted, and they and the frame's header are authenticated. Each direction has
  its own key and counts its messages; the count is the nonce. A frame that was
  changed, repeated, dropped or moved fails to open and ends the connection. A host
  whose first sealed message from a client does not open (the keys differ: a wrong
  join password) says so in the clear and closes.
- **Logins.** The login signs a BLAKE2b hash of the session's id, the role (player or
  master) and the player's name with the password's key in this game; the host checks
  it with the verifier. A new player's login carries the verifier, which the host keeps.
  The master password works the same way.
- **Password values in orders.** A player's orders may set the empire's password
  (Empire Status, Change Password) only to nothing or to a verifier of the current kind;
  hosts refuse anything else, over the network and by e-mail, so no player can lock an
  empire out or make it look like one of OpenSE4 0.6.
- **Games of OpenSE4 0.6.** Their verifiers (a second SHA-256 of 0.6's password hash)
  cannot check a signature. When such a player logs in, the host refuses once and says
  so (`OldPassword`): that player's game asks whether to show the host the old hash,
  once. It sends it only after the player agreed twice and only to a host whose key the
  player trusted beforehand (remembered from an earlier game, or confirmed after
  comparing fingerprints); a join password vouches for nothing here. The same login
  carries the verifier made from the password itself, which the host keeps from then
  on. A PBEM empire of the old kind moves to a new password with its next turn instead
  ([Play by e-mail](#play-by-e-mail)), because its old hash travels by mail.
- **PBEM files.** A turn file's view is encrypted to the empire's X25519 key with a
  fresh key pair and the host's PBEM box key (XChaCha20-Poly1305 under a BLAKE2b hash
  of both agreements, fresh-to-empire and host-to-empire, and the three public keys,
  as NaCl's `crypto_box` with a fresh key pair besides), bound to the file's readable
  header (game, empire, turn style, the empire's verifier and the host's two PBEM
  keys). The host then signs the whole file with its PBEM signing key. A player's game
  refuses a file whose signature does not match, a file signed by another key than the
  one it trusts for the game (`pbem-game:<id> <key>` in `known_hosts.txt`, set by the
  game's first turn file), and a view in the clear for an empire whose verifier has a
  box key. An empire without a password, or with one of 0.6's kind (moving this turn),
  gets its view in the clear, signed all the same. An orders file is signed over the
  game, the turn, the empire, the orders, the checksum of the turn file it was made
  from, a revision number (the time it was made) and the signer's verifier, then
  encrypted with a fresh key pair to the host's box key named in the turn file. Of two
  files for one empire the higher revision counts; two different moves of a 0.6 empire
  to a new password are both refused, and the host is told.
- **Host keys** are files of 64 hex digits (`host_key.txt`), made on first use without
  ever replacing another, readable by their owner only and, where the folder is made
  for them, in a folder only the owner can enter; a host whose key file others can
  read is warned. The file holds one secret, from which each use gets its own key
  (BLAKE2b keyed with the secret, one label per use): the network handshake's X25519
  key, the PBEM box key and the PBEM signing key, so no key serves two protocols. The
  file's comments show the network and the play-by-e-mail fingerprints. The players'
  remembered keys are lines of `<address>:<port> <key>` (network hosts) and
  `pbem-game:<game id> <key>` (PBEM hosts) in `known_hosts.txt`. Delete a line there to
  meet that host anew.

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
    - the random-number state, and the galaxy's seed (from which the whole map and the
      computer players' choices could be rebuilt; the lobby does not show it either).
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
cfg.hostKey = net::secure::loadOrCreateHostKey(file)->keys.network;   // the host's identity (none: a new key per session)
cfg.localPlayer = net::LocalPlayer{"Host", pw, setup};   // in-game hosting (passwords as typed: they never leave)
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

net::ClientConfig cc;                        // host, port, name, password, dataSet = game::dataSetIdentity(rules), hostKey (a pin)
net::ClientSession client(cc);
client.connect();
client.submitSetup(setup); client.setReady(true);
client.submitOrders(orders); client.chat("hi");
client.play(cmd); client.endTurn();          // turn-based: in our turn (client.myTurn())
client.questions(); client.pendingRequests(); client.activeEmpire();
client.seenHostKey(); client.hostKeyChanged(); client.hostAskedOldPassword();  // after a refusal: why (cc.sendOldPassword: consent)
client.lobby(); client.turnStatus(); client.state(); client.empire(); client.ordersAccepted();
client.requestAiControl(empire, true); client.requestPasswordReset({empire});   // administrators
```

`net::describe(event)` gives a one-line log text. `net::pbem::*` reads and writes turn
files (`writeTurnFiles` with the host's `secure::PbemHostKeys`, `readTurnFile`) and
`.plr` files (`writePlayerOrders`, `signOrdersFile`) and processes PBEM turns
(`processGameFile`); `openTurnForPlayer` is the player's side of a turn file (the
host's signature, the host key trusted for the game, the password, the view), shared by
the client and `opense4-server pbem orders`. The player's side in the client is
`client/classic/pbem_play.hpp` (open a turn file, check the empire and password, write
the signed `.plr` or a draft) and `ClassicSession` with `SessionKind::Pbem`.
Functions that run Argon2id throw `net::PasswordWorkError` when its memory cannot be
had; the sessions, the PBEM functions and the server report it in words. `game::saveGame`, `loadGame` and
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
   whether it has a join password, and the game's id (which salts the passwords). Both
   sides then switch to sealed frames.
3. The client sends `Login`: data-set fingerprint, its mods (id, version, identity and
   whether each changes the game), player name, a random id of its
   session object, the player's verifier in this game (`pk2:<KiB>:<passes>:<keys>`) and
   its signature, and the master password's signature if it has one. The host refuses
   a player whose empire still has an OpenSE4 0.6 verifier with `OldPassword` (or with
   `Password`, when the host takes no such moves); that player's game then asks the
   player, and only with the player's consent and a host key trusted beforehand does
   the next `Login` also carry the password's 0.6 hash.
4. The host answers `Reject` (with a reason and a readable text) or `Welcome` (game
   name and id, the player's slot, admin rights). A player whose game-changing mods
   differ from the host's is refused with `Mods` and a text that names each mod missing,
   in another version or with other files, or not used by the host (mods with only
   pictures, sounds or interface do not count); then a different data set is refused
   with `DataSet`. After the `Welcome` the host sends the `Lobby` (which lists the host's
   mods), and, during a game, the `State` and `TurnStatus`.
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
0.6.0 and 5 from 0.7.0 (encrypted connections) through 0.9.0, whose save format was 7.
Protocol 6 and save format 8 (the log fields of games imported from the original, the
colonies' destroyed facility counts, orders given to tagged vehicles as one group, the
fleet leader command and the Designs window's check boxes kept with the empire) came with
0.10.0, so it does not play with 0.9.0; it still loads 0.9.0's saves. Protocol 7 and save
format 9 come with the modding SDK (docs/MODDING_SDK.md §14.5): the game's mods in the
state and the save header, the player's mods in `Login`, the host's in `Lobby`, and the
`Mods` refusal; later SDK changes add their fields under these numbers until the next
release. The data set's identity of format 9 also covers the game folder's AI tables,
race files and design-name lists and the game-changing mods; a save of format 8 or older
is compared with the identity as its format computed it, so it is not taken for another
data set. Protocol 7 does not play with 0.10.0; format 8 and 7 saves still load.

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

# Multiplayer

OpenSE4 plays simultaneous-turn games with several people in two ways:

- **Network games.** One machine hosts. Players connect over TCP, set up their
  empires in a lobby, and send their orders each turn. The host processes the turn
  and sends everyone the new game.
- **Play by e-mail (PBEM).** The host keeps the game file. Each turn, every player
  sends in an orders file by any means (e-mail, a shared folder, a chat upload), and
  the host processes the turn from whatever arrived.

Both follow the classic game's simultaneous mode (spec 05 §9): players only give
orders, the host resolves everyone's turn at once, and the computer plays any empire
whose orders are missing. The host is authoritative.

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
- hand any human empire to the computer, or back to its player;
- give orders for any empire (to play for an absent friend).

### Dedicated server

`opense4-server` hosts a game without playing in it:

```sh
opense4-server --players=3 --ai=2 --quadrant-size=1 --name="Friday Game" --password=boss
```

It finds the installed classic data set on its own (or takes `--data=DIR`), opens TCP
port 6720, forwards it on the router with UPnP, and waits. The game starts
automatically once every player slot is taken and every player has marked ready.
From then on the server processes a turn as soon as every human player's orders are
in. It saves after every turn (`--autosave=N` changes that) and logs to standard
output. Stop it with Ctrl+C: it saves, says goodbye to the players and removes the
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
| `--name=NAME` | game name (also the save file's name) |
| `--password=PW` | master password (see below) |
| `--join-password=PW` | a password every player needs to join |
| `--turn-timeout=SEC` | process the turn after SEC seconds even if orders are missing |
| `--load=GAME.gam` | continue a saved game |
| `--save-dir=DIR`, `--autosave=N` | where and how often to save |
| `--max-turns=N` | stop once the game reaches turn N (for tests) |

**Master password.** A player who connects with the master password becomes an
administrator: from their client they can start the game (also forced), add and
remove computer empires, kick players, set the turn time limit, hand empires to the
computer and process the turn early. The master password is also stored (as a
verifier, see [Security](#security)) in the saved game, and continuing that game
with `--load`, or processing it as a PBEM game, requires it again.

**Continuing a game.** `opense4-server --load=Friday_Game.gam` reopens the saved
game in its current turn, under the name stored in the file. Players reconnect with
the same name and password. If the game has a master password, pass it again with
`--password`. A game saved with a different data set is refused.

`tools/server_smoke.sh` runs a complete game: a server with one human slot and one
computer empire, and the server's scripted `bot` client, which plays two turns.

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

- **Same version and data.** The client and the host must speak the same network
  protocol (the same OpenSE4 release) and use the same data set. The data set is
  compared by fingerprint, a hash of every data file plus the loaded tables, so two
  installations of the same game and mods match wherever they live. The host
  refuses mismatches with a message that names both sides.
- **The player password** protects the player's slot and empire. Nobody else can
  take over the empire or send orders for it, in network and in PBEM games.
- **The join password**, if the host set one, is needed by everyone.

### Reconnecting

A player who drops out (crash, network trouble, closed laptop) reconnects with the
same name and password. The host sends the current turn at once, and any orders the
player had already sent for it still count. A new connection with the right password
replaces an old one that is still open, so a half-dead connection never locks a
player out. Names are not case-sensitive.

While a player is away, the host keeps waiting for their orders. It stops waiting
when the turn time limit runs out, when the host processes the turn by hand, or when
the empire is handed to the computer.

### When orders are missing

When a turn is processed without some human empire's orders, the computer plays
that empire for the turn. An empire can set the option "the computer should only
keep things running" (`cmd::SetEmpireOptions::aiMinimalChanges`), and then the
computer makes only minimal changes in its place.

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

A PBEM game is a `.gam` file that stays with the host, and `.plr` order files that
the players send in. One round goes like this:

1. **Create the game** from a [setup file](#setup-files):

   ```sh
   opense4-server pbem new --setup=campaign.toml --out=campaign.gam
   ```

2. **Send `campaign.gam` to every player.** Each player opens it, plays their turn,
   and saves their orders as a `.plr` file (the game does this with
   `net::pbem::writePlayerOrders`). The file is named `<game>_<NN>.plr`, where NN is
   the empire's number. It holds only that empire's orders, marked with the game, the
   turn, the empire and the player's password hash.

   A player who wants to end the turn without changes can make an empty orders file:
   `opense4-server pbem orders --game=campaign.gam --empire=2 --password=... --out=DIR`.

3. **Collect the `.plr` files** in one directory, then process the turn:

   ```sh
   opense4-server pbem process --game=campaign.gam --orders=inbox --password=boss
   ```

   For every `.plr` file the host checks the game, the turn, the empire and the
   password. It reports and skips files that fail a check: another game, out of date,
   wrong password, damaged. If two files are for the same empire, the newer one
   counts. The computer plays human empires that sent nothing. The turn is
   processed, `campaign.gam` is replaced (the previous turn is kept as
   `campaign.gam.bak`), and the `.plr` files that were used are deleted
   (`--keep-orders` keeps them). Files that were skipped stay where they are.

4. **Send out the new `campaign.gam`** and repeat.

`opense4-server pbem info --game=campaign.gam` shows the turn, the empires, their
players and whether a master password is set.

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
# allow_intel, no_ruins, only_breathable, only_home_type, team_mode,
# simultaneous

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
```

The network server takes the name, seed, options, master password and computer
empires from the file. Human players join through the lobby, so it ignores human
`[[empire]]` entries.

Setup files hold passwords in plain text. To avoid that, a player can run
`opense4-server hash-password PW` and send the host only the output, which goes into
`password_hash`.

## Security

- **Passwords are hashed.** The game computes a SHA-256 hash of a password on the
  player's own machine (with an OpenSE4-specific prefix, so it differs from a plain
  SHA-256 of the same password), and only that hash is ever sent, over the network or
  in a `.plr` file. Hosts and saved games keep only a verifier, a second hash of that
  hash. So a copy of the game file, which every PBEM player receives, holds nothing
  a player could log in with. Weak passwords can still be guessed offline from a
  verifier, so do not reuse important passwords.
- **The connection is not encrypted.** Someone who can watch the traffic between a
  player and the host can see the game and the player's password hash, and could
  replay it to log in as that player. For games where that matters, play over a VPN.
  Encryption (for example TLS or a Noise handshake) is future work.
- **The host is authoritative.** It checks every order list against the game rules
  and applies only the sender's own empire's orders. Clients never change the host's
  game. The host also refuses oversized messages (64 KiB before the handshake,
  16 MiB after), malformed messages, unknown message types and anything before a
  valid greeting. Connections that go silent are dropped.
- **Each network player gets only their own view.** Each turn, the host sends every
  player the game as their empire knows it (`game::redactForEmpire`):
  - Removed:
    - other empires' treasuries, research and intelligence queues, logs and plans;
    - ships the player cannot see this turn;
    - colonies in systems the player has not explored;
    - the contents of foreign colonies;
    - the details of designs the player has never seen;
    - other empires' messages;
    - battles the player was not in;
    - the random-number state.
  - Kept: what diplomacy and the score screens show.
  - Shared: a Partnership gives its partner the maps and tech levels, as in the rules.

  The host's own player sees the same view. A PBEM `.gam`, however, is the complete
  game, because every player processes it with the same program. Play PBEM with
  people you trust not to peek.
- **The host itself must be trusted.** It sees and decides everything.
- **Files are checked before use.** Save, orders and `.plr` files start with a type
  tag, a format version and a checksum. Loading rejects wrong types, newer or
  retired versions, truncation, corruption and internal inconsistencies (for example
  a ship whose design does not exist), and always reports what is wrong. Hosts also
  refuse games whose data-set fingerprint differs from their own, and check that
  every hull, component and facility the game uses exists in their data set.

## Future work

- Per-player views for PBEM (a `.gam` per empire).
- Encrypted connections.
- IPv6 hosting.
- Sending only the changes between turns instead of the whole game.
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
net::HostConfig cfg;                         // port, slots, passwords, setup (seed, options), UPnP, limits
cfg.localPlayer = net::LocalPlayer{"Host", net::hashPassword(pw), setup};   // in-game hosting
net::HostSession host(rules, cfg);           // rules must outlive the session
host.start();                                // or host.resume(state, saveInfo)
host.addComputerEmpire(setup); host.kick(slot, "reason"); host.setLocalReady(true);
host.startGame(/*force*/ false);
host.submitOrders(orders);                   // any empire; normally the host's own
host.processTurnNow(); host.setTurnTimeout(120); host.setAiControl(empire, true);
host.save(path);
for (const net::Event& e : host.poll(0)) { /* e.type, e.text, e.player, e.slot, e.empire, e.turn */ }
host.lobby();  host.turnStatus();  host.state();  host.portMapping();

net::ClientConfig cc;                        // host, port, name, passwordHash, dataSet = game::dataSetIdentity(rules)
net::ClientSession client(cc);
client.connect();
client.submitSetup(setup); client.setReady(true);
client.submitOrders(orders); client.chat("hi");
client.lobby(); client.turnStatus(); client.state(); client.empire(); client.ordersAccepted();
```

`net::describe(event)` gives a one-line log text. `net::pbem::*` reads and writes
`.plr` files and processes PBEM turns. `game::saveGame`, `loadGame` and
`readSaveInfo` handle `.gam` files.

### Protocol

TCP, one stream per player. Each message is a frame: a u32 little-endian length, a
u8 message type, then the payload, which is encoded with the save-format archive.

1. The client sends `Hello`: magic, protocol version, program version, data-set
   fingerprint, player name, and hashes of the player, join and master passwords.
2. The host answers `Reject` (with a reason and a readable text) or `Welcome` (game
   name and id, the player's slot, admin rights). Then it sends the `Lobby`, and,
   during a game, the `State` and `TurnStatus`.
3. In the lobby: `SubmitSetup` (the empire setup), `SetReady`, `ChatSend`, and
   `Admin` requests. The host sends `Lobby` after every change.
4. At the start and after every turn, the host sends `State`: the turn, the
   player's empire, and a complete `serializeState()` blob with its own checksum.
5. Each turn the player sends `SubmitOrders`, a `serializeOrders()` blob of an
   `EmpireOrders` for the current turn. A later submission replaces an earlier one.
   The host answers `OrdersAck` and sends everyone a `TurnStatus` saying who has
   sent orders.
6. `Ping`/`Pong` keep idle connections alive (every 5 s). A peer that sends nothing
   for 60 s (host side) or 90 s (client side) is disconnected. `Bye` ends a session
   politely and carries a reason, for example a kick or a shutdown.

`net::kProtocolVersion` must change whenever the messages or the save format
change.

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

# Multiplayer

OpenSE4 plays with friends in three ways: on one computer (hotseat), over a network, and by
e-mail. This chapter explains each one.

## Hotseat

In a hotseat game, several people take turns at one computer. Start a **New Game** and, on the
Players page, add an empire for each player and set each one to **Human**. Give each empire a
password in Empire Setup if you do not want others to play your turns.

Between turns, a **Next Player** screen names the next empire and asks the others to look away.
The player types the empire's password, if it has one, and presses `Begin Turn`.

Hotseat works with both turn styles. In a simultaneous game, the turn is processed once the last
player has ended their turn.

## Network games

Choose **Multiplayer** on the title screen.

### Hosting

`Host a Game` opens a form:

- the game's name, and the **port** (6720 by default);
- the number of **human players** and **computer players**, and the quadrant size;
- the **turn style**: simultaneous (the default) or turn-based;
- a **turn time limit** in seconds (0 for none), after which the turn goes on without players who have not finished;
- a **join password**, if only invited players should get in;
- **automatic port forwarding** (UPnP), which asks your router to let players from the internet reach you;
- your own name, password and race.

`Open Lobby` opens the game's lobby. Players join, choose their race and mark themselves
`Ready`. As the host you can add and remove computer players and remove players. `Start Game`
begins when everyone is ready (`Start Anyway` starts regardless). The lobby has a chat box.

### Joining

`Join a Game` lists the games running on your local network. Click one to fill in its address, or
type the host's address and port yourself for a game over the internet. Enter your name and an
optional password, and the join password if the host set one, then press `Connect`.

Your copy of the game data must match the host's, and both of you must run the same OpenSE4
version; the list warns you when the data differs.

To come back to a game after a disconnection, join again with the **same name and password**. The
host sends you the current turn, and orders you already sent still count.

### Playing a network game

- In a **simultaneous** game, give your orders and press `End Turn`. Your orders go to the host, and the status bar shows `Waiting...` until the turn has been processed. The host processes the turn when every player has ended it, or when the time limit runs out.
- In a **turn-based** game, only the player whose turn it is can act, and every order is carried out on the host at once.
- If your orders do not arrive in time, the computer plays your empire for that turn (see [Computer players and ministers](computer-players-and-ministers#missed-turns-in-multiplayer-games)).
- Every battle in a network game is fought strategically.
- The chat button at the bottom left opens the chat.

If players outside your home network cannot connect, your router may not support automatic port
forwarding. Forward TCP port 6720 to the host computer by hand in the router's settings, and allow
the port through the host's firewall. Players on the same network can always join directly.

## Play by e-mail

In a play-by-e-mail game, a host keeps the game file and processes each turn. The players receive
the game file, play their turn on it, and send back a small **orders file**. Any way of passing
files works: e-mail, a shared folder or a chat upload.

To play your turn:

1. Choose **Multiplayer**, then `Play by E-mail`.
2. Open the game file the host sent you. The window lists the game files in the `pbem` folder of your OpenSE4 user folder, or you can type the file's path.
3. Choose your empire and type its password.
4. Choose where to save the orders file (by default, next to the game file) and press `Play Turn`.
5. Play your turn as usual. `Save Game` in the Game Menu saves your turn so far, so you can finish it later.
6. Press `End Turn`. OpenSE4 writes the orders file and tells you its name. Send it to the host.

In a turn-based e-mail game, only the empire whose turn it is can play, and the game file goes
from player to player through the host.

Battles in play-by-e-mail games are always fought strategically.

> A play-by-e-mail game file holds the whole game, including what your opponents are doing. Play this way with people you trust.

## The dedicated server

A separate program, `opense4-server`, hosts a network game without playing in it, and processes
the turns of play-by-e-mail games. It runs without a window, saves after every turn, and forwards
its port automatically. Players join it like any other host. Running it is explained in the
multiplayer guide that comes with OpenSE4.

## Passwords and safety

- Your password is never sent or stored as you typed it: OpenSE4 sends only a scrambled form of it. Still, do not reuse an important password.
- Network connections are not encrypted. For games where that matters, play over a VPN.
- The host sees everything and decides everything. Play with a host you trust.

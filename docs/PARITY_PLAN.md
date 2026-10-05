# Parity plan: a faithful clone of the classic game

**Goal.** The new engine (`src/game`), run on the player's installed classic data set
and art, should play the same as the original: the same rules, numbers and screens.
It must stay clean-room throughout (see [CLEANROOM.md](CLEANROOM.md)).

**Not a goal: replacing the original's content.** OpenSE4 plays on the player's own copy
of Space Empires IV Deluxe and always needs it: we make no data set, art, sound or music
of our own to stand in for it. To play, players buy the original game.

**How parity is established:**

1. **Specs** in `docs/spec/`, written in our own words from the manual, the data-file
   documentation and the version history. Each spec's last section lists its open
   questions; most are now answered (see "Next steps" for what is left).
2. **Observation sessions.** Answer open questions by playing the original through
   `tools/observe`, and record the answers in `docs/spec/07-observations.md`.
3. **Tests.** Rules are unit-tested against our own fixtures, and every milestone gets
   an opt-in test against the installed data set.
4. **Side-by-side checks.** Set up the same situation in both games and compare what
   the screens show: production numbers, research costs, growth, and combat statistics
   over many runs.
5. **The executable** (since 2026-09-29, see [CLEANROOM.md](CLEANROOM.md)). Every rule in
   specs 01–05 has been checked against it and marked "(confirmed: binary)", and the
   engine follows the corrected specs. [PARITY_GAPS.md](PARITY_GAPS.md) lists what still
   differs.

## Milestones

Every rule is implemented from the specs. A guess where the sources are silent is
marked `(inferred)` in the code and kept as an open question in the spec. Those
questions are what the observation sessions still have to settle.

### M0: Foundations (done)
- [x] Reader for the classic data format, the typed ruleset for all 28 data files,
      specs 01–06, the observation log, the observation harness, the clean-room lint,
      and runtime access to the installed art.

### M1: Quadrant (done)
- [x] Quadrant generation from the data files: placement modes, system types, stellar
      abilities, the warp network, planet values and homeworld placement.
- [x] Knowledge and sight: explored, present and last-seen state; sensors against
      cloaking by sight type; scanners; sharing between partners.
- [x] Galaxy panel and the Galaxy Map window: overlays, distances, notes.
- [x] Quadrant size choices and the 67 × 46 galaxy grid (spec 01 §2.2, §3.2).
- [x] Map files: Save Map, Load Map and map starting points, in our own format
      ([MAPS.md](MAPS.md), spec 01 §12).
- [ ] Compare generated quadrants with the original's maps.

### M2: Empires and economy (done; calibration continues)
- [x] Empire setup:
  - race presets from the install;
  - characteristics and point costs;
  - traits, cultures and happiness types;
  - our own empire files;
  - Game Setup (8 pages) and Empire Setup (6 pages).
- [x] Colonies:
  - planet output with every modifier;
  - the spaceport rule, blockades, storage, and the minimum-income floor;
  - maintenance, including destroying vehicles when it goes unpaid.
- [x] Construction queues:
  - rates;
  - emergency and slow builds, repeat and hold;
  - units built into cargo;
  - facilities and upgrades.
- [x] Population: growth, domes, mood from every event, riots, rebellion, plague.
- [x] Calibration: a stock Quick Start homeworld reproduces the original's figures
      exactly (spec 07, Calibration).
- [x] Screens: Planets, Colonies, Construction Queues, Set Construction Queue, Empire
      Status.

### M3: Vehicles and movement
- [x] Design rules, weapon mounts, the Designs and Create Design windows.
- [x] The ability identifiers used by the stock data.
- [x] Movement points and supply.
- [x] Retrofit, scrap and mothball.
- [x] The order, fleet, cargo, unit, scrap and stellar-manipulation windows.
- [x] 30-phase simultaneous movement, every order (expanded when given), ad-hoc groups,
      greedy in-system steps, colonization, supply and repair.
- [x] Unit groups: one per (owner, unit kind, sector), mixing designs, through launch and
      recovery, supply, movement, damage, sight, combat, the windows and the save.

### M4: Research, intelligence, diplomacy (done)
- [x] Tech costs, queue allocation, racial and unique areas, ruins.
- [x] Intelligence projects (every stock project type), counter-intelligence.
- [x] Treaties, messages, packages, trade and tariffs, contact.
- [x] Random and timed events (every stock event type), scores and history, victory
      conditions.
- [x] Screens: Research, Tech Tree, Intelligence, Empires, Communicate, Treaty Grid,
      Scores, Comparisons, History, Race Report, Victory Conditions, Log.

### M5: Combat (done)
- [x] Combat triggers and strategic resolution: strategies, formations, to-hit, every
      damage type, seekers, point defense, fighters, satellites, drones, mines.
- [x] Planetary combat, bombardment, boarding and capture, ground combat.
- [x] Combat replay window.
- [x] Interactive tactical combat: a battle stepped phase by phase with validated
      orders for the player's pieces (`combat::TacticalBattle`), the same rules and
      code as strategic resolution; in turn-based games each human side chooses
      Tactical or Strategic, and the battle fought in the client is applied to the game
      by replaying its orders.
- [x] The combat simulator: mock battles between virtual empires on a sandbox copy of
      the game, fought tactically or strategically.
- [x] The watch-only Strategic Combat window (turn-based battles answered Strategic or
      started by the player's orders, simultaneous battles when the Settings flag asks,
      computer-only simulations) and the Ground Combat window after troops land.

### M6: Computer players (done)
- [x] An AI driven by the install's `Ai/` and race files:
  - states and anger;
  - politics and the Mega Evil Empire;
  - research, construction and designs;
  - fleets, colonization and invasions.
- [x] Minimal mode for absent humans, neutral empires, and ministers.

### M7: Presentation parity (in progress)
- [x] The main window in the classic layout (1024×768, scaled): status bar, command
      buttons, order strip with the original icons, system/report/galaxy panels, and
      the original hotkeys.
- [x] Every window in the inventory, including the combat resolution prompt, Tactical
      Combat with its Orders and Options, the Combat Simulator, the watch-only Strategic
      Combat and the Ground Combat windows.
- [x] Sounds (UI, weapons, explosions; remastered set) and the three music playlists.
- [x] The original's `.fon` fonts and `.cur` pointers, read from the install, and the
      800×600 layout (spec 06 §5.4, §5.8, §2.1.1).
- [x] Screenshot comparisons against the original through the harness: every window
      side by side (spec 07 sessions 3 and 5), and the setup screens, the management
      windows and the battle windows rebuilt in the original's layouts (2026-10-03).

### M8: Persistence and multiplayer (done)
- [x] Save and load: a versioned, checksummed format with validation on load; autosave
      every 1, 2, 3, 5 or 10 turns into ten rotating slots.
- [x] Hotseat, with a password hand-over between players.
- [x] Network games:
  - the in-game lobby, including hosting with UPnP port mapping;
  - reconnect and chat;
  - the computer taking over empires that are missing.
- [x] Dedicated server `opense4-server`, and PBEM (`pbem new|process|turn-files|orders|info`);
      the game client plays a PBEM turn from the player's own turn file and writes the
      signed `.plr` (Multiplayer, Play by E-mail).
- [x] Turn-based games over the network and by e-mail.
- [x] Fog of war in network and PBEM games: each player receives only their empire's view.
- [x] Encrypted network connections with pinned host keys and signed logins; reconnects
      that keep the turn's orders; desyncs detected, named and repaired.
- [x] Tutorials, training games and the manual: OpenSE4's own, in our own words
      ([LEARNING.md](LEARNING.md)). The original's tutorial cannot be played: its starting
      game is a binary `.gam`.

## Next steps

1. **Computer players: enemy colonies inside the territories.** The second debugger round
   (spec 07, 2026-10-03) and its rules leave every pace figure within the original's
   spread except two, which both follow how many enemy colonies lie inside an empire's
   territory: colonies at turn 100 (15.7 against 17.0) and battles at enemy colonies
   (2.6 against 3.0–4.9 per empire and 25 turns). See spec 05 questions 76 and 77; question
   74 (a Scrap order on a fleet member) is not traced yet.
2. **The interface's last open choices:**
   - spec 06 §7 Q99: endings when a human loses during its own turn, intelligence targets,
     changed objects;
   - Q60: Windows' own Small Fonts size, which needs a Windows machine;
   - spec 01 Q46: Ring 8 and Ring 9;
   - the OpenSE4 choices the setup screens listed in [PARITY_GAPS.md](PARITY_GAPS.md): spin
     steps, characteristic words, Compare Culture Modifiers, Login To Game.
3. **Keep the interface covered by input scripts.**
   - `tools/run_input_tests.py` plays 23 scripts through the client's own input, covering
     every tutorial and training game and the mouse-only features. Six of them run in CI on
     fixture data ([BUILDING.md](BUILDING.md) "Input scripts").
   - A new window or lesson gets a script.
4. **Multiplayer.** Protocol 5's security design was reviewed twice ([MULTIPLAYER.md](MULTIPLAYER.md)
   "Security"). Two improvements are left:
   - a password-authenticated key exchange, for the first connection to an open game;
   - Argon2 off the interface thread.

## Future goals

Beyond parity, three larger goals are planned. None of them has started. Each keeps the
rule above: OpenSE4 always plays on the player's own copy of the original.

### A Steam release, with multiplayer through Steam

OpenSE4 on Steam as a free program that finds the player's Space Empires IV Deluxe
through Steam and plays on it.
- **Steam multiplayer:**
  - lobbies, and invites from the friends list;
  - connections through Steam's relay network, so nobody has to open ports.

  Network games over plain TCP, the dedicated server and play by e-mail stay as they are,
  with the same rules and protocol, so Steam and non-Steam players can share a game.
- **Steam integration:** cloud saves, and rich presence (the game, turn and empire shown to
  friends).
- **Builds without Steam stay complete.** Steam support is an optional part, used only when
  Steam is running. The GitHub downloads, the ARM builds and `opense4-server` don't need it.
- **To settle first:**
  - The Steamworks SDK is not free software. Shipping it with OpenSE4, which is under the
    GPL, needs an added permission in OpenSE4's licence, or a separate Steam component.
  - The store page.
  - How the Steam build checks that the original is installed.

### Steam Workshop

Players publish mods from inside the game and subscribe to them.
- **Data-set mods:** the original's mod format, checked with `opense4-datacheck` before
  publishing. A mod is layered over the installed game and never changes it.
- **Script mods:** from the SDK below.
- **Matching mods in multiplayer:** every player in a network or e-mail game must have the
  same mods. The data-set checksums already make sure of this for data files; scripts
  will join them.

### A modding SDK with Python scripts

- **Tools:**
  - a mod template;
  - the data-format reference (the specs in `docs/spec/`, rewritten as a modder's guide);
  - `opense4-datacheck`;
  - a tool that packages a mod for the Workshop.
- **Python scripts:** hooks for the rules, events, the computer players' ministers,
  victory conditions, scenarios and training games.
- **What the engine requires of scripts:**
  - **Same result everywhere:** a turn must resolve the same way on every computer
    (network games compare checksums). Scripts therefore run only inside turn processing,
    take every random number from the game's generator, use whole numbers where the engine
    does, and change the game only through the engine's commands and hooks.
  - **One set of scripts per game:** each script's version and checksum become part of the
    data set's identity, so every player in a game runs the same scripts.
- **To settle first:**
  - Embedding CPython on every platform OpenSE4 supports, Windows 7 and 32-bit ARM
    included.
  - A sandbox: Workshop scripts come from strangers, so they must not reach the player's
    files or the network. Python is hard to sandbox, so this choice comes before any
    scripting API.

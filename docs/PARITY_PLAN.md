# Parity plan: a faithful clone of the classic game

**Goal.** The new engine (`src/game`), run on the player's installed classic data set
and art, should play the same as the original: the same rules, numbers and screens.
It must stay clean-room throughout (see [CLEANROOM.md](CLEANROOM.md)). A second goal
is our own free data set and art in the same format, so the game also runs, and can be
distributed, without the original.

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
   engine follows the corrected specs. [PARITY_GAPS.md](PARITY_GAPS.md) lists the two
   deliberate differences that remain.

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
- [ ] `.fon` fonts, cursors.
- [ ] Screenshot comparisons against the original through the harness.

### M8: Persistence and multiplayer (done)
- [x] Save and load: a versioned, checksummed format with validation on load; autosave
      every 1, 2, 3, 5 or 10 turns into ten rotating slots.
- [x] Hotseat, with a password hand-over between players.
- [x] Network games:
  - the in-game lobby, including hosting with UPnP port mapping;
  - reconnect and chat;
  - the computer taking over empires that are missing.
- [x] Dedicated server `opense4-server`, and PBEM (`pbem new|process|orders|info`); the
      game client plays a PBEM turn and writes the `.plr` (Multiplayer, Play by E-mail).
- [x] Turn-based games over the network and by e-mail.
- [x] Fog of war in network games: each player receives only their empire's view.
- [x] Tutorials, training games and the manual: OpenSE4's own, in our own words
      ([LEARNING.md](LEARNING.md)). The original's tutorial cannot be played: its starting
      game is a binary `.gam`.

### M9: Our own content (not planned)
- [ ] A complete data set in the classic format, with our own art, fonts and sounds, so
      the game would run without the original. Not planned: OpenSE4 is an engine for
      the player's own copy of Space Empires IV Deluxe.

## Next steps

1. **The engine's own choices.** On 2026-10-01 every open question of specs 01–06 was
   settled from the executable and the engine and client follow the answers. Implementing
   them raised new, smaller questions where the spec is silent; each names the engine's
   choice, marked "(inferred)" in the code:
   - spec 03 §19 Q72–Q76 (fleet orders and slots);
   - spec 04 §19.4 Q87–Q89 (Drop Troops details);
   - spec 05 Q50–Q51;
   - spec 06 §7 Q24–Q55 (window and main-window details), and the one part of Q18 (where
     the hull code appears in ship names) that needs observation; Q56, Q60–Q64 and
     Q70–Q72 (list windows, fonts, pointers, the 800×600 layout, the Log); Q73–Q82 (the
     battle flow and the combat windows).

   Settle them from the executable where it can, and implement from the spec text, not
   from any listing.
2. **Side-by-side checks** with the original through `tools/observe`: screenshots of each
   window, and an all-computer game to compare the computer players' pace.
3. The original's `.fon` fonts and `.cur` pointers, and the 800×600 layout.
4. Encrypted connections, and per-player views for PBEM.

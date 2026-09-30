# Parity plan: a faithful clone of the classic game

**Goal.** The new engine (`src/game`), run on the player's installed classic data set
and art, should play the same as the original: the same rules, numbers and screens.
It must stay clean-room throughout (see [CLEANROOM.md](CLEANROOM.md)). A second goal
is our own free data set and art in the same format, so the game also runs, and can be
distributed, without the original.

**How parity is established:**

1. **Specs** in `docs/spec/`, written in our own words from the manual, the data-file
   documentation and the version history. There are about 130 open questions in total,
   in each spec's last section.
2. **Observation sessions.** Answer open questions by playing the original through
   `tools/observe`, and record the answers in `docs/spec/07-observations.md`.
3. **Tests.** Rules are unit-tested against our own fixtures, and every milestone gets
   an opt-in test against the installed data set.
4. **Side-by-side checks.** Set up the same situation in both games and compare what
   the screens show: production numbers, research costs, growth, and combat statistics
   over many runs.

The prototype game (`src/sim`, the default `opense4` mode) stays playable until the
classic engine can replace it.

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
- [ ] Quadrant size choices and galaxy grid dimensions (spec 01 Q2); compare placement
      with the original's maps.

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
- [ ] 30-phase simultaneous movement, every order, pathfinding, colonization, supply
      and repair (being merged).

### M4: Research, intelligence, diplomacy (done)
- [x] Tech costs, queue allocation, racial and unique areas, ruins.
- [x] Intelligence projects (every stock project type), counter-intelligence.
- [x] Treaties, messages, packages, trade and tariffs, contact.
- [x] Random and timed events (every stock event type), scores and history, victory
      conditions.
- [x] Screens: Research, Tech Tree, Intelligence, Empires, Communicate, Treaty Grid,
      Scores, Comparisons, History, Race Report, Victory Conditions, Log.

### M5: Combat (done; tactical mode open)
- [x] Combat triggers and strategic resolution: strategies, formations, to-hit, every
      damage type, seekers, point defense, fighters, satellites, drones, mines.
- [x] Planetary combat, bombardment, boarding and capture, ground combat.
- [x] Combat replay window.
- [ ] Interactive tactical combat and the combat simulator.

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
- [x] Every window in the inventory, except the tactical combat screens and the combat
      simulator.
- [ ] Sounds and music playlists, `.fon` fonts, cursors, the 800×600 layout.
- [ ] Screenshot comparisons against the original through the harness.

### M8: Persistence and multiplayer (done)
- [x] Save and load: a versioned, checksummed format with validation on load.
- [x] Hotseat, with a password hand-over between players.
- [x] Network games:
  - the in-game lobby, including hosting with UPnP port mapping;
  - reconnect and chat;
  - the computer taking over empires that are missing.
- [x] Dedicated server `opense4-server`, and PBEM (`pbem new|process|orders|info`).
- [ ] Per-player fog-of-war in network state (every client currently receives the
      full state).
- [ ] Scenarios and the tutorial script format.

### M9: Our own content
- [ ] A complete original data set in the classic format, with original art, fonts and
      sounds, so the game runs without the original and can be distributed.

## Next steps

1. Observation sessions to settle the open questions, starting with the ones that
   affect play the most:
   - combat to-hit and damage;
   - growth and the mood numbers;
   - AI thresholds.
2. The tactical combat screen.
3. Sound and music.
4. Fog-of-war redaction for network games.

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

### M0: Foundations (done)
- [x] Reader for the classic `Key := Value` data format (`src/datafile`), with
      diagnostics down to file, line and record, plus unread-field tracking.
- [x] Typed ruleset for all 28 data files (`src/ruleset`). The stock data set loads
      with 0 errors.
- [x] Clean-room specs 01–06 and observation log 07.
- [x] Observation harness (`opense4-observe`) and clean-room lint (`tools/cleanroom_check.py`).
- [x] Runtime access to the installed art (`src/assets`: case-insensitive index,
      BMP/JPG, black as transparent).

### M1: Quadrant (in progress)
- [x] Quadrant generation from QuadrantTypes, SystemTypes, SectType,
      StellarAbilityTypes, PlanetSize and Settings. Covers placement modes, weighted
      system types, position specifiers, sector-type constraint matching, the stellar
      ability roll, the warp network (cap, minimum angle, connectivity pass, edge
      placement), planet values and homeworld placement. All six stock quadrant types
      generate with no warnings.
- [x] Classic main-window replica for browsing the result (`opense4 --classic`): system
      panel with original backgrounds and sprites, report panel with portraits, galaxy
      panel.
- [ ] Quadrant size choices and galaxy grid dimensions (spec 01 Q2).
- [ ] Compare each placement algorithm with the original's maps and tune.
- [ ] Planet condition bands, naming rules and multi-star naming (Q14, Q15).
- [ ] Galaxy Map window (780×475): overlays, distances, notes.
- [ ] Knowledge and sight: explored, present and last-seen state; five sight types (spec 01 §6).

### M2: Empires and economy (spec 02)
- [ ] Empire setup: race presets from the race AI text files, characteristics,
      traits, culture, point costs, and our own empire file format.
- [ ] Colonies: population, reproduction, domes, conditions, plague.
- [ ] The facility ability pipeline: resource generation, research, intelligence,
      storage and spaceports.
- [ ] Construction queues: per-resource rates, emergency and slow builds, upgrades, scrapping.
- [ ] Maintenance, the minimum-income floor, happiness and moods, riots.
- [ ] Planet report tabs (Detail, Facil, Cargo, Ability) and the Planets and Colonies screens.

### M3: Vehicles and movement (spec 03)
- [ ] Design rules and the Create Design screen, including weapon mounts.
- [ ] All 160 ability identifiers used by vehicles and facilities.
- [ ] Movement points, supply, warp jumps, and 30-phase simultaneous movement.
- [ ] Every order, including waypoints, fleets and formations.
- [ ] Cargo, units (fighters, troops, mines, satellites, drones), and launch/recover.
- [ ] Repair, retrofit, scrap, mothball.

### M4: Research, intelligence, diplomacy (spec 05)
- [ ] Tech costs (Tech Cost setting), queue and allocation, racial and unique areas, ruins.
- [ ] Intel points and projects, counter-intelligence.
- [ ] Treaties, communications, trade and tariffs, anger.
- [ ] Random events (Events.txt), scores and history, victory conditions.

### M5: Combat (spec 04)
- [ ] Combat triggers, strategic (automatic) resolution, and the damage-type rules.
- [ ] Tactical combat screen and orders, seekers, point defense, fighters, mines.
- [ ] Planetary combat, ground combat, boarding and capture.
- [ ] Combat replay and simulator.

### M6: Computer players (spec 05 §AI)
- [ ] AI driven by the `Ai/` and race AI files: states, anger, politics, research,
      construction, design and fleets.
- [ ] Personalities, neutral empires, the "Mega Evil Empire" rule.

### M7: Presentation parity (spec 06)
- [ ] Every screen in the inventory (~100), with classic layouts at 800×600 and 1024×768.
- [ ] Button sheets, fonts (`.fon`), cursors, sounds, music playlists and event
      pictures, all taken from the install.
- [ ] Hotkeys identical to the original. The prototype's G/F11 clashes are already
      fixed: fullscreen is now Alt+Enter.
- [ ] Screenshot comparisons against the original via the harness.

### M8: Persistence and multiplayer
- [ ] Save and load (our own format), autosave rotation.
- [ ] Hot-seat, simultaneous play-by-email, TCP/IP.
- [ ] Scenarios and the tutorial script format.

### M9: Our own content
- [ ] A complete original data set in the classic format, plus original art, fonts and
      sounds, so the game runs without the original and can be distributed.

## Next steps

1. M1 remainder: knowledge and sight, then the Galaxy Map window.
2. M2 core: colonies, facilities and the resource pipeline. The economy is the
   backbone the other milestones build on.
3. Observation session 2: answer spec 01/02 questions about quadrant size, placement
   look, condition bands, growth numbers and queue rates. Use a Quick Start game and
   several New Game quadrants.

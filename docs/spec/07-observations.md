# Observations of the running original

Black-box observations of the player's installed copy (version 1.95, Steam
"Deluxe" edition, under Proton), made with `tools/observe`. Screenshots stay
local in `reference/se4/` (gitignored). Each entry records what was seen and how.

## Session 1: launch and first screens

**Launcher.** Starting the Steam app opens a small launcher window (736×536)
before the game itself. It has a vertical list of buttons: Play, Readme,
History, Extras, PDF Manual, HTML Manual, Map Editor, two website links, and
Quit. The map editor is a separate executable reached from here.

**Presentation model.** The game switches to fullscreen at the desktop
resolution (observed at 2560×1440). It does not scale its interface: screens
are drawn at a fixed logical size, centered on black.
- The main menu background art fills the screen.
- Dialogs such as empire selection float at their native size in the middle.
- The in-game main window is a fixed **1024×768** frame (measured 1024×766 of
  drawn content) centered on the screen.

For exact parity our client should support the same fixed-frame mode, with
integer or crisp scaling as an option for modern displays.

**Main menu.** The art fills the screen, with the version string at the bottom
left and a load-progress indicator at the bottom right. Two rows of four
buttons run along the bottom edge: Quick Start, New Game, Resume Game (disabled
with no saved game), Load Game, Tutorial, Scenario, Credits, Quit Game.

**Quick Start → "Pick Empire".** A starfield dialog titled "Select Empire" has
a short instruction to click a portrait. The scrollable grid shows two columns
of empire cards: a portrait, the empire name, and a two-to-four-line blurb.
Begin Game and Cancel buttons sit below the grid. The stock empires come from
`Empires/*.emp` plus portraits under `Pictures/Races/<race>/`.

**Main game window** (1024×768 frame, positions approximate):
- **Title bar**, full width and about 30 px tall. It shows the empire logo and
  name, the leader's title and name, "Game Date" as a year with one decimal
  (the first turn is 2400.0; a turn is 0.1 year), and the three stockpiles
  with resource icons, colored blue (minerals), green (organics) and red
  (radioactives). A small square button sits at the far right.
- **Command toolbar**, two rows of square icon buttons below the title bar,
  about 70 px tall.
  - Left block, 2×6: empire-level screens such as log, research, intelligence,
    empires and help.
  - Middle block: order buttons for the selected object, shown dimmed when
    nothing is selected. Scroll arrows flank it, so it pages. A red "NAME"
    button is among them.
  - Right: a 3×2 block of navigation arrows.
- **System map**, the large left pane (about 655×660). It has a starfield
  background and no visible grid lines.
  - The star is drawn at the center as a glowing disc.
  - Planets are drawn as small sprites at their sector positions; warp points
    are blue swirl sprites near the edges.
  - A small red `+` beside some planets marks something: its meaning is not
    yet known.
  - A small white number beside an object ("2") appears to count the objects
    stacked in that sector.
  - The selected object gets yellow corner brackets, and stacks of our ships
    show a small blue marker with a flag.
  - The system name is written at the top left of the pane.
- **Detail panel**, top right (about 290×330).
  - Header: the selected object's picture and name.
  - Planet fields: Type ("Rock - Medium"), Atmosphere, Conditions, and Value as
    three per-resource percentages, plus a one-line description.
  - Colony fields: Colony Type (e.g. Homeworld), Population ("2000M/2000M")
    with a portrait, Reproduction ("10% per year"), Mood, Resource Production
    (three values), Research, Intelligence, Under Construction and Time
    Remaining.
  - Four tabs at the bottom: Detail, Facil, Cargo, Ability.
- **Galaxy minimap**, bottom right (about 290×270). It shows a fine blue grid
  with each system as a small hollow circle, and our home system marked with a
  blue ship-like icon.
- A decorative circuit-pattern strip runs along the right edge.

## Resolved questions

- **The system grid is 13×13** (Spec 01 §14 Q1). In the 1024×768 main window the system
  pane is 650 px wide, starting at x=8, y=110. Object sprites are centered on a 50 px
  lattice:
  - Sector (i, j) is centered at (8 + 25 + 50·i, 110 + 25 + 50·j).
  - The star sits at (333, 435), which is sector (6, 6), the center of a 0..12 grid.
  - Every other object measured lies on the same lattice (x = 83, 133, 283, 383, 433,
    483, 533, 633).
  - Warp points sit in the outermost column and row, sectors 0 and 12.
  - The executable puts the centres at (34 + 50·i, 139 + 50·j), 1 px right of and 4 px
    below these measurements; spec 06 §2.4 has the exact geometry (confirmed: binary).

  This matches the SystemTypes header (0..12). The manual's "196 sectors" is wrong for
  this version.

## Calibration: the Quick Start homeworld

**What was seen.** A Quick Start game (medium Rock homeworld, default
setup) showed this on the homeworld's detail panel on the first turn:
- Population 2000M of 2000M; reproduction 10% per year; mood Happy.
- Resource production: minerals 6000, organics 1097, radioactives 1142.
- Research 3950.
- Planet value 100% / 98% / 102% (minerals / organics / radioactives).
- Conditions "Unpleasant".
- Treasury 20000 of each resource.

**How it fits the data.** Read against the installed data set with a
Terran empire (first preset tier; Merchants culture: research −2, production
0; Mining Aptitude 110, Intelligence 120, other characteristics 100):
- 2000M falls in the population row worth +30 % (the first row whose
  amount is at least the population); Happy is +10 % per the Settings `Mood
  Happy Modifier` (110), not the 120 % of the Happiness.txt header.
- The terms **add** (spec 02 §5.1): organics 800 × 98 % × (100 + 30 + 10) %
  = 1097, radioactives 800 × 102 % × 140 % = 1142, both truncated. Multiplying
  (130 % × 110 %) would give 1121 and 1166.
- Minerals: five 800-point miners with Mining Aptitude +10: 4000 × 100 % ×
  150 % = 6000.
- Research: five 500-point research facilities, value not applied:
  2500 × (100 + 30 + 10 + 20 − 2) % = 3950.
- So the homeworld holds 5 mineral, 1 organic, 1 radioactive and 5 research
  facilities, plus the space yard and spaceport; the 15th of the medium
  planet's 15 slots is taken to be the resupply depot, which the stock
  tutorial also names as a standard homeworld facility. No intelligence
  facility (its technology starts at level 0).
- Reproduction shows 10 %: the Happy bonus and the Unpleasant penalty cancel
  (the planet report says the shown rate includes both).
- Construction rate at the start: 2000 × 130 % = 2600 per resource;
  maintenance rate 25 − 10 (aptitude) − 5 (culture) = 10 %.

**Residual differences.** Our homeworld generator gives every homeworld
exactly the setup value (100 %) and random conditions; the original spreads
the three values by a couple of points and showed Unpleasant conditions. The
opt-in test sets those two observed inputs and then reproduces every number
above exactly (`tests/test_economy.cpp`, `OPENSE4_CLASSIC_DATA`).

**Related note.** The stock tutorial expects a brand-new colony of a few
million people to start producing as soon as mining facilities are built on
it, so facility output is not limited by `Population Required to Operate One
Facility` (spec 02 §13 Q3).

## UI: layout, fonts and palette

**How it was measured.** A Quick Start game at the 1024×768 frame (the game
draws that frame unscaled, centred on a larger desktop). Each window was opened
with its command button and captured on its own; frame pieces were located by
exact pixel matching against the player's own picture files (nothing copied).

**Main window.** The frame is built from the `Game/Screens/1024X768` strips,
all drawn 1:1 with black transparent, at these top-left corners: `Top` (0,0),
`Toptitle` (0,29), `Topsys` (0,106), `Bottom` (0,760), `Left` (0,0), `Right`
(957,0), `Middle` (655,106), `RightFiller` (958,107), `Topgal` (662,470).
- Status bar: large flag (15,8); empire name and type at x 47; leader title
  and name at x 231; "Game Date" label at x 421 and the date at x 501; the
  three stockpiles at x 627, 700 and 771, each an amount in its resource
  colour followed by the resource icon. Text top at y 12.
- Command buttons: `Main.bmp` cells at (13 + 34·col, 36 + 34·row), in this
  order. Top row: Game Menu (icon 0), Designs (4), Planets (2), Colonies (1),
  Ships (3), Construction Queues (9). Bottom row: Research (6), Empires (7),
  Log (8), Empire Status (5), Help (10), End Turn (11).
- Order strip: every order has a fixed place, 20 columns × 2 rows of 34 px
  from (246, 36), and is drawn dim when it does not apply. There is one page at
  1024; the `BigLeftRightArrows` pager halves sit dim at (231,44) and (927,44).
  Columns (top / bottom), with their `Orders.bmp` cell as band:column:
  Move To 0:0 / Warp 0:5; Move To Waypoint 0:1 / Colonize 0:2; Attack 0:4 /
  Fleet Transfer 0:6; Resupply 0:8 / Repair 0:9; Clear Orders 0:10 / Build
  Queue 0:11; Cargo Transfer 2:10 / Launch-Recover Units 2:9; Load Cargo 0:19 /
  Drop Cargo 0:20; Launch Units Remotely 0:15 / Recover Units Remotely 0:16;
  Sentry 0:12 / Explore 0:3; Patrol 0:13 / Repeat Orders 0:14; Stellar
  Manipulation 1:5 / Change Name 2:0; Scrap 0:18 / Formation-Strategy 1:0;
  View Orders 2:1 / Sweep Mines 1:11; Scrap Facilities 1:18 / Jettison Cargo
  1:17; Cloak 1:21 / Decloak 1:22; Use Component 1:12 / Use Facility 1:13;
  Abandon Planet 1:3 / Convert Resources 2:2; an unused cell 2:8 / Minister
  0:22; Play Movement Log 2:4 / Play Log For Ship 2:5; Play Log Stepped 2:6 /
  Rewind Log 2:7. The manual lists 2:8 as "not being used". Other cells of the
  sheet (0:7, 0:17, 0:21, 1:16 and more) are not placed in the strip.
- With the homeworld selected these are lit: Cargo Transfer, Build Queue,
  Launch-Recover Units, Recover Units Remotely, Change Name, Scrap, Scrap
  Facilities, Abandon Planet, Minister.
- Selectors: `Nextprev.bmp` groups (ship, fleet, planet) at (965, 34 + 24·row).
- Report panel (planet, Detail tab): portrait at the top left; the name in the
  button font centred over the right column (centre x 824, text top y 115);
  label lines at x 797 with the value on the next line at x 807, 15 px apart
  (Type, Atmosphere, Conditions, Value; Value shows the three percentages in the
  resource colours with icons). The description follows in the small font at
  y 251, 12 px lines. Then two columns (labels x 671, values x 807, 14 px
  lines): Colony Type, Population (with the population picture), Reproduction,
  Mood; Resource Production, Research, Intelligence (values right-aligned to
  x 833, then an icon); Under Construction, Time Remaining. `TabBtns.bmp`
  tabs (72×30) sit at (667 + 72·i, 439); the selected tab uses state row 2.
- System view: colonies carry the owner's small flag at the planet's top
  right. Uncolonized planets that the empire can colonize carry a small star
  from `General.bmp` (green when the atmosphere is breathable, red otherwise).
  The selection is a picture of small yellow marks around the sprite (session 3: the
  eight marks of `Selection.bmp`). The
  system name is in the button font at (13, 121).
- Galaxy map: a fine grid in dark blue, the quadrant kept in proportion and
  centred; systems are small grey rings; no warp lines are drawn before any
  link is known.

**Windows.** Most windows are 780×475, centred. Planets, Colonies, Ships and
Construction Queues are 780 wide and as tall as the screen less 50 px at the
top and bottom. The Game Menu is 173×320 with no title strip.
- Frame: side pipes from `Dialogs/MainParts.bmp` (top cap, a straight run,
  bottom cap; the right side mirrored), light rails at the top and bottom
  edges, a title strip (10,4)-(770,31), then a content box and a button box.
- Title: white, in the button font, at (18, 8).
- Button column: 180×28 buttons, one slot every 31 px from (585, 35), with
  Close in the 14th slot. Unused slots show as dark empty boxes. Page and
  filter buttons (tabs) have the top-right corner cut off and a green lamp when
  selected. On/off settings have a check box that holds a green lamp when on.
  Action buttons are plain rectangles. Disabled buttons are drawn in dark grey.
  Tall windows fill the column below Close with the circuit filler.
- Text: headings in the button font in silver; body text in Futurist Medium;
  notes and hints in Futurist Small; field labels in blue with white values.
  Numbers have no digit grouping (26120, 500000).

- Research: "Research Points Available" and the amount sit in the title strip;
  the list of areas spans the content width with Current Level and Cost
  columns (completed areas stay listed, dimmed, with "Complete" as the cost);
  below it four project boxes side by side; clicking a project cancels it.
- Empires: "N Known Empires" over a framed strip of four portraits between
  the big page arrows; clicking a portrait opens Communicate.
- Designs: a list of about 240 px on the left with lamps (green for the
  selected design, blue otherwise) under design-type headings; the detail
  shows the portrait, then the name with Size, Design Type and Date Created
  stacked, then Cost, Movement, Shields, Cargo Space and Supply Capacity.
- Intro: the intro picture is stretched to the whole screen. A black band
  along the bottom holds two rows of four buttons across the full width
  (26 px tall, 5 px apart): Quick Start, New Game, Resume Game (not
  available on a first run), Load Game; Tutorial, Scenario, Credits, Quit
  Game. The version is at the left over the picture, the loading state at
  the right.
- Quick Start's Select Empire: a star field; the title and a hint at the left;
  a framed two-column list of 128 px portraits, each with the empire name and
  its short description (the `Description` key of the race's general AI
  file), in alphabetical order down the left column and then the right; Begin
  Game and Cancel below. Only eight empires were listed although `Empires/`
  holds 20 empire files; which rule picks them is an open question.

**Fonts** (`Fonts/*.fon`, Windows 3.x raster fonts, character set 0):
Futurist Medium (16 px cell, ascent 13, internal leading 3), Futurist small
(12, 10, 2), SE4 Block 1 Large/Medium/Small (15/12/9 px, all capitals),
SE4 Text button (17 px, ascent 12, no leading).

**Palette** (sampled): frame lines #4F65A2 with brighter rails #647EC7;
button outlines and captions #617BC2; labels #7D9FFF; headings #C0C0C0;
second lines #A0A0A0; unavailable rows #606060; disabled buttons and empty
slots #2D2D2D; minerals #4665CC, organics #008000, radioactives #FF0000;
background black.

**Calibration notes from the same game.**
- On the first turn the stockpile shows 20000 plus one turn of production
  (26120 / 21108 / 21153 with production 6120 / 1108 / 1153). It looks as if
  the first turn's income is already counted when the game starts.
- The homeworld showed Conditions "Good" and 14% reproduction with mood Happy.
  "Good" is not one of our condition band names (spec 02 §13 Q25), so the band
  names and their reproduction effects need another look.
- Homeworld values were 102% / 99% / 103%, and production scaled by them
  (6120 / 1108 / 1153).

## Session 2: simultaneous movement days (2026-09-30)

Watched with a debugger under Wine during a simultaneous turn: a speed-6 ship's day
counter over the 30 days of one turn.

- The ship acted on days 6, 11, 16, 21 and 26: five steps, not six. The counter
  matched, bit for bit, a counter added in 64-bit-mantissa precision and stored as a
  double each day; a double-precision or single-precision sum would differ from day 3
  or day 1.
- The x87 was at 64-bit precision throughout turn processing, including inside the day
  addition.
- This settles spec 03 Q8; the resulting schedule for every speed is in spec 03 §6.3.

## Session 3: starting assets, map lettering, battle display, computer pace (2026-10-01)

**How.** A copy of the installed game (version 1.95) ran under Wine 11.18 in a prefix of
its own, inside a nested rootful Xwayland display of 1024×768, so the game's 1024×768
frame filled the display 1:1. Input went through XTest on that display only. Stills were
taken with ImageMagick; motion was recorded losslessly with ffmpeg's x11grab at 60 frames
a second and examined frame by frame (which pixels changed, and their bounding box). Our
engine and client were built from commit dc71cad. Captures, videos and the statistics
files are in `reference/observe/2026-10-01/`.

**Waits under Wine.** Every animation frame for which spec 06 §1.10.3 gives a wait of
1 ms or 10 ms lasted 16 ms: a slide moved exactly one pixel per 16.0 ms over more than a
hundred steps (one step more than the 60 Hz capture every 24 frames), and waits of 0.1 s
lasted 6–7 capture frames (0.11 s). So under Wine 11.18 the millisecond tick counter that
the waits read advances in steps of 16 ms, much like the 15.6 ms of Windows, not in steps
of 1 ms (inferred cause: Wine refreshes its tick count every 16 ms). The timings below are
therefore close to what a Windows player sees.

### Starting assets (spec 01 §2.1, §3.6)

Confirmed on screen: no empire gets a ship at creation, and only Quick Start gives the
human designs.

- **Quick Start** (Terran). On the first turn (2400.0) Ships\Units shows Total Ships 0,
  Total Units in Space 0, Total Fleets 0 and a maintenance of 0 0 0; the home system shows
  only the colony's flag at the homeworld. The Designs window holds eleven designs, one
  per design type, all dated 2400.0, marked Prototype and named from the race's
  design-name list:

  | Design type | Hull | Cost (M / O / R) | Move | Cargo | Supply |
  |---|---|---|---|---|---|
  | Attack Ship | Escort (150 kT) | 3250 / 100 / 380 | 6 | 0 | 3000 |
  | Base Space Yard | Space Station (500 kT) | 5000 / 200 / 500 | 0 | 0 | 0 |
  | Cargo Transport | Small Transport (300 kT) | 4850 / 100 / 250 | 5 | 1650 | 2500 |
  | Colony (Rock) | Colony Ship (300 kT) | 4850 / 1100 / 1250 | 5 | 170 | 2500 |
  | Defense Base | Space Station (500 kT) | 6600 / 200 / 720 | 0 | 0 | 0 |
  | Kamikaze Attack Ship | Escort (150 kT) | 3850 / 100 / 500 | 6 | 0 | 3000 |
  | Population Transport | Small Transport (300 kT) | 4850 / 100 / 250 | 5 | 1650 | 2500 |
  | Satellite Layer | Escort (150 kT) | 3050 / 100 / 300 | 6 | 160 | 3000 |
  | Troop Transport | Small Transport (300 kT) | 4850 / 100 / 250 | 5 | 1500 | 3000 |
  | Satellite (unit) | Small Satellite (80 kT) | 500 / 0 / 80 | 0 | 0 | 0 |
  | Weapon Platform (unit) | Small Weapons Platform | not read | | | |

  There is one colony ship, for the home planet's type only, and no scout, minesweeper,
  mine layer, carrier or fighter: their templates need technology the race does not have
  yet (inferred). This fits one Design minister run with starting technology.
- **New Game** with the default settings, the human added with Add Existing from the stock
  Terran empire file: on the first turn Ships\Units shows 0 ships, 0 units and 0 fleets,
  and both design lists (ships and units) are empty.
- **Computer players** cannot be inspected at creation. In the three five-empire games of
  the pace test below, the statistics file shows 0 ships for every empire after the first
  turn; the first ship of any empire appears after the second turn (the third in one
  game), and by turn 10 every empire has 1–3. That fits designs made in the first turn and ships built from them.

Our engine gives every empire four designs (Scout, Colonizer, Escort, Defense Base) and
three ships (two scouts and a colonizer), as the PARITY_GAPS row "Starting assets" says.
Two side effects show in the statistics: our homeworlds lose 4M people on turns 1 and 3
(58 of 60 empires at 1996M after turn 1, inferred: colonists loaded onto the starting
colonizer and the next colony ship), where all 15 original empires stay at 2000M through
turn 6; and our homeworlds keep their mood bonus longer (next paragraph).

**Homeworld mood.** In 13 of the 15 original empires the homeworld's production and
research fall by one mood step (the 10 % Happy term of 07 "Calibration") after turn 4, in
the other two after turn 6. In ours the same drop comes after turns 6–8, or not within 30
turns (60 empires, 12 seeds). Inferred cause: our starting ships sit in the home system.

### A game with only one empire

A New Game with only the human and both random-computer boxes cleared shows, before the
first turn, the Victory picture with a line naming the empire and its leader as winners,
then a box titled "All Players Eliminated" asking whether to continue; Yes goes on with
the game. So the original treats a game with one living empire as won at once, before any
turn is played.

### Victory Conditions page

On a fresh start every condition is unchecked and the page shows: score 5000000; 10.0
years; 300 % of the second place; 50 % of the tech areas; 1.0 year of peace; conditions
taking effect after 5.0 years. Its header says that with none selected the game goes on
until only one empire survives. (Spec 01 §11 lists the values the setup stores; these are
the ones this page shows.)

### Small map lettering under Wine (spec 06 §7 Q60)

Measured 1:1 on the 1024×768 system panel: facility markers on the homeworld (Empire
Options, System Display, with the R/S/Y row on: "YSR") and the white counts "2" of
sectors holding two stellar objects.
- The face is Wine's own `smalle.fon` ("Small Fonts"), which holds one size only: 7 pt at
  96 DPI, an 11 px cell with ascent 9 and internal leading 2. The game's 6 pt request
  therefore gets this 11 px face.
- Capitals and digits are 7 px tall (cell rows 2–8); lower case is 5 px tall with 2 px
  descenders.
- Every glyph has one blank column at its left. Digits: 4 px of ink, advance 5 ("1": 2 in
  3). Capitals: Y and S 5 px of ink, advance 6; R, C and N 6, advance 7; W 7, advance 8;
  M 8, advance 9.
- On screen "YSR" ends at the right edge of the planet's 36×36 square, with the glyph
  bottoms 2 px above the square's bottom (the cell's bottom on the square's bottom), one
  blank column between letters; a count's glyph starts 1 px right of the square's left
  edge.
- Windows' own Small Fonts at 6 pt could not be measured (no Windows machine).

### Strategic Combat as seen (spec 06 §7 Q73)

Combat Simulator, Strategic, three sides of identical escorts with one direct-fire weapon
each, 30 combat turns, recorded from the press of Begin until Close lit:

| Battle | Frames with a new map state | "Combat Turn" values seen | Span |
|---|---|---|---|
| 3 × 3 escorts | 8 | 2, 7, 11, 15, 19, 24, 28, 30 | 0.12 s |
| 3 × 3 escorts, again | 7 | 2, 6, 10, 13, 20, 26, 30 | 0.10 s |
| 3 × 6 escorts | 15 | 2, 4, 6, 8, 9, 11, 12, 14, 16, 18, 20, 23, 25, 28, 30 | 0.23 s |

Every captured frame from the first change to Close showed a new map and a new turn
number: two to five combat turns fall between two 60 Hz frames, no single step of a phase
is ever seen, "Combat Turn 1" never appears, and Close lights in the frame that first
shows 30. Confidence: medium, since the count depends on the processor (a current desktop
here).

### Tactical Combat animation, Fast Tactical Combat off (spec 06 §7 Q77)

Combat Simulator, Tactical, every side under computer control, End Turn pressed every
2.5 s. A game started at High technology supplied test designs: cruisers with two beams
(Anti-Proton Beam), two torpedoes (Quantum Torpedo) or two seekers (Capital Ship Missile);
unarmed cruisers and escorts, some without engines; Space Station bases with armour only
or with a phased shield generator. One tick below is one 16 ms step of Wine's counter.

- **Slide**: 1 px per tick along the longer axis, straight and diagonal alike: a
  one-square step takes 36 ticks, about 0.58 s, and a four-square move about 2.3 s
  (136–138 capture frames). High confidence.
- **Turn**: in place before the step, 5° per tick: 45° in 9–10 capture frames (about
  0.15 s), 90° in 16–17 (0.28 s), 180° in 34 (0.57 s), so 9 ticks per 45°. Medium-high
  confidence (the angles were judged from the sprite before and after).
- **Beam**: stamps 6 px apart, drawn outward about one per tick, then erased in the same
  order about one per tick (now and then 2–4 fall in one capture frame): about 0.17 s for a
  beam over one square, 1.0 s over 5.9 squares, 1.3 s over 7.2 squares, so about 0.19 s
  per square drawn and erased. Medium confidence on the exact rate.
- **Torpedo**: the picture moves 4 px per tick: a flight over 5.4 squares took 42 capture
  frames (0.70 s), about 0.14 s per square. Medium confidence.
- **Hit on structure**: the 8 explosion pictures follow each other 6–7 capture frames
  apart (0.1 s, rounded up to whole ticks), and the wipe comes after the same wait, about
  1.0 s per hit; 36×36 when the target survives.
- **Shield-only hit** (beams at the shielded base): no explosion and no wait. The shot ends
  about 17 px short of the target's centre; while it is drawn and erased the base is
  circled by a cyan shield ring that changes every 8 stamps, and the ring is gone in the
  next frame. No separate shield picture showed in any captured frame (a stamp removed
  within the same tick would not show). Confirmed for beams.
- **Miss**: beam and torpedo misses end 18 px off the target's centre in x and in y (seen
  +18/−18, +18/+17, +18/+20); nothing else is drawn. Confirmed.
- **Pauses after an impact**: after a seeker struck a base that survived, 25 capture frames
  (0.42 s, that is 0.1 s and 0.3 s) passed between the last explosion change and the next
  redraw; after beam hits on survivors 7–8 frames (0.12 s), after the seeker that destroyed
  a base 6 frames. So only a seeker impact on a survivor pauses. One sample of the pause:
  medium confidence.
- **Losses**: all five losses seen used the 72×72 explosion, whose 8 pictures grow to fill
  a 72×72 box centred on the square: two Space Station bases (one to a seeker, one to
  beams), a cruiser and two escorts (beams). The destroying hit played that explosion once,
  with no second animation; no loss used the 36×36 pictures.

### Pace of the computer players (spec 05 §7)

**Original.** Three games: Small quadrant of the first quadrant type, default settings,
simultaneous turns, the human (the stock Terran empire file) and four random computer
empires (neutral empires off; the copy's `Settings.txt` was set to roll exactly four for
Medium), every score visible, and the human given every minister (Ministers, Complete AI
On). End Turn was pressed 100 times, about 1 s a turn. Figures come from `History/plr_1_stats.txt`,
which holds one row per living empire per turn (empire, date, then the Comparisons
columns). The third game ended after turn 92, when the human empire was destroyed, so turn
100 averages the first two games only.

**Ours.** Twelve games (seeds 1–12) of the engine at dc71cad through a scratch program
(not tracked): `createGame` with a Small quadrant and simultaneous turns, Terran plus four
other race presets as Quick Start picks them, all five empires computer-controlled, 100
`processTurn` calls, and the statistics the engine records (`Empire::history`).

The human with every minister on did much worse than any computer player in all three
original games: it stayed in its home system until turn 75 with 2–3.5 colonies and
research flat at 3,700–5,300, and was destroyed or nearly destroyed by turn 100. So
"every minister on" does not play like a computer player in the original (the computer
mark's own effects are in spec 06 §1.2.1), and the table compares the original's computer
empires 2–5 (12 empires) with all five of ours (60). Means per empire, original / ours:

| Turn | Colonies | Systems | Tech levels | Research | Ships | Bases | Units | Score |
|---|---|---|---|---|---|---|---|---|
| 10 | 1.7 / 2.9 | 1.0 / 1.1 | 17.2 / 17.5 | 3,612 / 3,981 | 2.0 / 2.4 | 0.0 / 0.0 | 14 / 14 | 19,664 / 21,965 |
| 25 | 5.3 / 6.8 | 2.2 / 2.3 | 19.2 / 19.5 | 5,935 / 6,411 | 3.0 / 4.2 | 0.3 / 0.2 | 23 / 25 | 30,579 / 34,986 |
| 50 | 11.2 / 12.2 | 3.5 / 3.6 | 24.8 / 23.6 | 13,865 / 11,179 | 7.2 / 7.2 | 0.6 / 0.2 | 38 / 39 | 65,216 / 59,853 |
| 75 | 14.8 / 15.5 | 4.5 / 4.1 | 31.4 / 29.1 | 17,852 / 13,552 | 10.4 / 9.2 | 0.8 / 0.1 | 76 / 71 | 97,235 / 82,565 |
| 100 | 19.6 / 17.4 | 5.6 / 4.6 | 39.4 / 34.6 | 23,502 / 14,503 | 10.7 / 10.3 | 0.9 / 0.1 | 98 / 83 | 125,370 / 95,724 |

(Standard errors of the original's means: colonies ±0.1 at turn 10, ±0.6 at 25, ±1.7 at
50, ±3.6 at 100; research ±2,200 at 50 and ±5,200 at 100. Ours are about a third of
those.)

Clear differences:
- **Early colonies**: the original's computers have 1.7 colonies at turn 10 and 5.3 at turn
  25, ours 2.9 and 6.8: about one colony ahead, from the starting colonizer and designs.
  By turns 50–75 the two are level within the noise.
- **Research later on** (likely, not certain: 1–2 standard errors, and only three
  original games): level until turn 25, then ours falls behind: about 80 % of the
  original's mean research points at turn 50, 76 % at 75 and 62 % at 100 (medians 10.9k
  against 11.1k at 50, but 12.7k against 21.7k at 100), and 4.8 fewer tech levels at turn
  100 (34.6 against 39.4). Score follows (96k against 125k at turn 100).
- **Bases** (about two standard errors): the original's computers keep 0.6–0.9 bases each
  from turn 50 on, ours 0.1–0.2.
- **Ships**: the same from turn 50 on (7 at 50, about 10 at 100); ours are one ahead early
  because of the starting ships.
- Within the noise: systems, units and intelligence (a few original empires had 1,500–5,800
  intelligence points by turn 92–100; ours rarely any).

### Side by side: our client against the original (Quick Start, first turn)

Screens of the original (Terran Quick Start) and of our client (`--quick-start=Terran
--seed=7 --layout=1024x768`, offscreen, OpenGL) at 1024×768. Differences in content that
come from our starting designs and ships are left out. The frame counter in the top-left
corner of our screens is the owner's `show_fps` setting.

- **Main window.** The selected object's marker is `Dialogs/Selection.bmp` (36×36, eight
  small yellow arrowheads at the corners and edge midpoints) drawn over the sector's 36×36
  square with black transparent; it matched the capture pixel for pixel. Ours draws four
  corner lines. Our planet report has a small "List" button at the bottom right of the
  Detail page that the original lacks.
- **Lists.** The original's lists have the up/down arrow buttons and a "Pic" column heading
  where there is a picture; ours have a thin bar and no "Pic" heading.
- **Planets.** Labels differ: the original reads "Number of Planets", "Number of
  Colonizable Planets", then "... which are owned by Enemies / Allies / Non-Aligned",
  "... which are not Colonized", "... which are Breathable", "Colonizing Ships", "...
  which are available"; ours "Planets", "Colonizable Planets", "Owned By ...", "Not
  Colonized", "Not Colonized, Breathable", "Available Colonizing Ships". The value columns
  are three "Value" headings with the resource icons (ours "Min.", "Org.", "Rad."). In a
  row the atmosphere sits on the name's line (ours centred on the two lines); our own
  homeworld's name is yellow (the original's white). "No Sys To Avoid" has a check box in
  the original.
- **Colonies.** The original's summary lists System with Colonies, Number of Colonies,
  Number of Blockaded Colonies, Total Population, Research and Intelligence Points
  Produced, Total Resources Produced and Maximum Resource Storage (with icons, "50kT");
  ours shows "Statistics" and "Output" blocks, the second cut off by the minimap. The
  General tab's columns are Pic, Name (name and planet type on two lines), Atmosphere,
  Conditions, Pop (on two lines), Mood; ours Name ("(home)" added), Type, Colony Type,
  Population, Mood, Facil. The original has no Constr. Queue or Goto button; its Close is
  in the 14th slot.
- **Construction Queues.** The Ships, Planets, Ship SY and Planet SY filters are check
  boxes (box with a lamp at the left); ours are lamp buttons. The Rate values carry
  resource icons in the original.
- **Research.** Completed areas stay in the list, dimmed, with "Complete" as the cost
  (ours leaves them out); under each of the four project boxes the original has a small
  box; Reorder Projects is in the 13th slot just above Close (ours the 9th), and there is
  no Tech Tree button.
- **Designs.** The original groups the list under design-type headings, each design with a
  lamp, picture, name, hull and "Prototype"; the detail shows Size, Design Type and Date
  Created by the picture, then Cost, Movement, Shields, Cargo Space and Supply Capacity,
  and the components as an 8-column grid of icons; a note says obsolete designs are
  deleted automatically. Ours has plain rows with a highlight bar, more figures (Class,
  Space, Structure, Weapons) and the components as a text list. Hide Obsolete and
  Stats\Strategy are check boxes in the original.
- **Create Design.** The original asks the vehicle type first (Ship, Base, Satellite,
  Weapon Platform) and titles the window after it ("Ship Design"); it starts with no size,
  and a warning asks for one. Its layout: picture, Size, Design Type and
  Design Name each with a ▽ button; a figures box (Space Used, Total Cost on three lines,
  Movement, Shields, Cargo Space, Supply Capacity); "Components on Design" as a horizontal
  strip with arrows; "Components Available" as a 3-column grid of tiles (icon, name, kT)
  paged with arrows; Warnings and Component Details boxes; buttons Comp Type, Weap Mount,
  To Hit Modifiers, Condensed View and Only Latest (check boxes), Weapons Report, Create
  Design and Cancel in the 13th and 14th slots. Ours starts on Escort with a suggested name
  and a Suggest button, lists components vertically by group, shows the warnings in red at
  the bottom and puts Cancel lower. The original words each warning as a requirement on
  the part count (for a cruiser: one bridge, at least two life supports, at least two crew
  quarters); ours as a need.
- **Empire Status.** The original has three blocks: Resource Production Per Turn (From Our
  Colonies, From Trade, From Tariffs, From Remote Mining, Total), Resource Expenses Per
  Turn (Tariffs, Maintenance Cost, Construction Queue Usage, Total), Net Resources Per
  Turn, and Resources in Treasury (Current Total, Maximum Resource Storage), with icons on
  the first row. Ours adds a header with the flag and name, Other, Not delivered, Lost to
  full storage, the points, the password and the simultaneous-game option. The original
  has Change Email (12th slot) and Change Password (13th); ours lacks Change Email.
- **Empires.** The original's order: Treaty, Trade, Tariff, (gap), History, Treaty Grid,
  Intelligence (dim with no contact), Borders, Scores, Victory Conditions, Comparisons,
  (gap), Our Race, Close. Ours puts Victory Conditions before Scores and Our Race one slot
  higher, gives Borders a check box, and adds a "Treaties" heading with an explanation.
- **Log.** Ours writes "Nothing to report this turn." in an empty list; the original's list
  stays empty, and its Goto is lit.
- **Ships\Units.** Ours adds a hint paragraph and spaces in the title ("Ships \ Units");
  the Show Ships, Show Units and Show Fleets check boxes are in the original's slots 11–13
  just above Close (ours 7–9).
- **Combat Simulator.** The original's vehicle rows show the side's number in a colour box
  (Flag column) and the name with Cargo and Fleet lines; the items list is alphabetical; the
  owner rows are lamp, name, numbered colour box; the buttons are Tactical, Strategic,
  four gaps, No Obsolete, Strategies, Computer Control, Fleets For Plr, Change Cargo, a gap,
  Begin, Cancel, with hints under both lists. Ours shows the empire's flag picture, lists
  designs in design order and places the buttons higher.
- **Tactical Combat.** The original's title strip holds the title, Location, Turn and
  Empires, and the navigation arrows with a stop button at the right; the map fills the left
  part directly; the right column holds the current piece's report (picture, Size, Move, a
  damage bar, a 6×6 weapon grid), the target's report below it, Options and Orders, an Auto
  check box and End Turn as a 2×2 group, and the overview map at the bottom. Ours (its
  sample battle) titles the window after the simulator, adds a heading line and a side list above the map, draws
  a box around each piece, shows hint texts instead of the reports, and stacks its buttons
  in one column beside a smaller overview.

## Harness notes

- Launch: `steam steam://rungameid/1610`. The first launch created the Proton prefix.
- Input: under GNOME Wayland, XTest (`opense4-observe click`) is silently dropped,
  while `opense4-observe sclick` (synthetic window events) works for Wine windows.
  Coordinates are window-relative in screen pixels. The fullscreen window is
  2560×1440 and the 1024×768 frame is centered at offset (768, 337).
- Capture: `import -window <id> out.png`, then `convert out.png -trim` isolates
  the 1024×768 frame.
- Close: terminate the `Se4.exe` process (quick games are unsaved), or use the
  in-game quit button.

Session 3 (2026-10-01), without Steam and without touching the owner's desktop:
- Copy the install to a scratch folder, make a Wine prefix of its own
  (`WINEDLLOVERRIDES="winemenubuilder.exe=d"` always, so no menu entries are made) and run
  `Se4.exe` from the copy; Play in the launcher starts the game.
- Run the game in a nested rootful Xwayland (`Xwayland :7 -geometry 1024x768`) and point
  `DISPLAY` at it for Wine, input and capture. The game then fills the 1024×768 display.
  XTest input works there (it stays inside the nested server). `opense4-observe list`
  finds nothing, because the nested display has no window manager; use the root window.
- Record with `ffmpeg -f x11grab -framerate 60 -video_size 1024x768 -draw_mouse 0 -i :7`
  and a lossless codec (`libx264rgb -qp 0`), then compare frames.
- The per-player statistics file is written only for human-controlled empires but holds a
  row for every living empire each turn; a simultaneous game needs a Multiplayer Game
  Filename on the Mechanics page before Begin Game.

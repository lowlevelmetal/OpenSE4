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

The engine has followed spec 01 §3.6 since (2026-10-01). A Terran Quick Start now gets the
eleven designs of the table above, with the same hulls, costs, movement, cargo and supply,
and no ship; the side effects are measured again under "Pace after the starting assets".

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

### Pace after the starting assets (OpenSE4, 2026-10-01)

Measured again once the engine followed spec 01 §3.6 (no starting ships or designs; computer
players design in their first turn), with the same scratch method as above (seeds 1–12, Small
quadrant, simultaneous, 100 `processTurn` calls, `Empire::history`). "Five computers" is the
set-up above; "passive player" is closer to the original's games, whose human player hardly
grew: empire 1 is a human empire that gives no orders and gets no stand-in, and the means are
over the four computer empires (48). Means per empire, original / five computers / passive
player:

| Turn | Colonies | Systems | Tech levels | Research | Ships | Bases | Units | Score |
|---|---|---|---|---|---|---|---|---|
| 10 | 1.7 / 1.5 / 1.5 | 1.0 / 1.0 / 1.0 | 17.2 / 17.4 / 17.3 | 3,612 / 3,505 / 3,453 | 2.0 / 2.5 / 2.4 | 0.0 / 0.0 / 0.0 | 14 / 10 / 10 | 19,664 / 19,942 / 19,815 |
| 25 | 5.3 / 5.5 / 5.6 | 2.2 / 1.9 / 2.0 | 19.2 / 19.0 / 18.9 | 5,935 / 6,173 / 5,907 | 3.0 / 3.3 / 3.3 | 0.3 / 0.2 / 0.2 | 23 / 23 / 23 | 30,579 / 30,703 / 29,869 |
| 50 | 11.2 / 10.8 / 10.9 | 3.5 / 3.2 / 3.5 | 24.8 / 22.9 / 23.0 | 13,865 / 9,769 / 9,747 | 7.2 / 6.6 / 6.1 | 0.6 / 0.3 / 0.3 | 38 / 35 / 35 | 65,216 / 54,546 / 52,738 |
| 75 | 14.8 / 14.0 / 15.0 | 4.5 / 3.9 / 4.1 | 31.4 / 28.1 / 28.2 | 17,852 / 12,385 / 13,188 | 10.4 / 9.4 / 8.1 | 0.8 / 0.2 / 0.2 | 76 / 67 / 55 | 97,235 / 80,092 / 75,035 |
| 100 | 19.6 / 15.8 / 17.2 | 5.6 / 4.3 / 4.5 | 39.4 / 33.8 / 34.1 | 23,502 / 13,354 / 13,899 | 10.7 / 10.3 / 9.3 | 0.9 / 0.3 / 0.2 | 98 / 85 / 66 | 125,370 / 93,433 / 90,275 |

(Our standard errors: colonies ±0.1 at turn 10, ±0.6 at 50, ±1.0 at 100; research ±500 at 50
and ±930 at 100; bases ±0.1.)

- **The early game now agrees**: colonies 1.5 against 1.7 at turn 10 and 5.5 against 5.3 at
  turn 25, ships 2.5 against 2.0 at turn 10. Every computer player has its eleven-odd designs
  after the first turn; in each game the first ship of any empire is in the statistics of turn
  2 (7 games) or turn 3 (5 games), and at turn 10 every empire has 1–3 ships, as in the
  original.
- **Homeworld population**: 2000M in the statistics of turns 1–6 for all 60 homeworlds, as for
  the original's 15.
- **Homeworld mood**: the research of the homeworld first falls by one mood step (to 92.6–93.9 %
  of the row before) in the statistics row of turn 4 for 32 of 60 empires, turn 5 for 7, turn 6
  for 19 and turn 8 for 2 (the anger rises by 1 or 2 points a turn from 25 to the Indifferent
  band at 30, depending on the race). That matches the original's "after turn 4" (13 of 15)
  and "after turn 6" (2 of 15) if those count statistics rows the same way, as the 1996M "after
  turn 1" above does; otherwise ours is one turn early.
- **Still behind**: research (70 % of the original's at turn 50, 57 % at 100), tech levels (5.6
  fewer at 100), bases (0.2–0.3 against 0.6–0.9) and, from turn 50, colonies and systems. The
  passive player changes little.

What our computer players do (seeds 1–12, five computers), for the analyst questions of spec
05 (53–56):
- **AI state**, share of all empire-turns 1–100: Exploration 34 %, Infrastructure 5 %, Prepare
  for Attack 2 %, Attack 2 %, Secure Holdings 1 %, Defend (Short Term) 56 %; from turn 50, 73–83 %
  of our computers are in Defend (Short Term) at any moment. What keeps them there is mostly
  other empires' attack ships one jump from a colony system and other empires' colonies there
  (per empire 1.7 ships and 0.8 colonies at turn 50, 4.0 and 3.5 at turn 100). The stock vehicle
  table builds Defense Bases, Base Space Yards and population transports only in
  Infrastructure; most of our empires never build a base.
- **Colonies** at turn 50 / 100: 10.8 / 15.8 per empire, of which Mining 2.6 / 4.0, Research
  Compound 2.7 / 4.0, Military Installation 1.2 / 1.7, Construction Yard 1.0 / 1.4, Farming
  0.8 / 1.3, Intelligence Compound 0.8 / 1.4, Refining 0.7 / 1.2. Of the colonies other than
  the homeworld, 83 % are domed at turn 100 (2.5 breathable ones per empire, 34 slots between
  them; 12.4 domed ones, 33 slots).
- **Research facilities**: 16.6 / 22.6 per empire, all Research Center I (no computer reaches
  the Applied Research rows of its research file, rows 31–38 of 66–73, by turn 100); research
  13.5k at turn 100, of which the homeworld 3.5k, flat from turn 25. Facilities 51 of 64 slots
  at turn 50, 73 of 83 at turn 100: the empty slots are mostly on Intelligence Compounds, which
  get nothing at all while the empire has no intelligence facility (spec 05 Q55).
- **Population** 2,491M at turn 50, 3,214M at turn 100 (the homeworld 2,000M).
- **Research lost** (the excess of finished projects, spec 05 §1.4): 1–6 % of the pool.
- **Sensitivity** (scratch experiments against the spec, not kept; turn 100, five computers):
  counting a hostile colony in the enemy-in-territory list only when it is seen (spec 05 Q54)
  gives 17.4 colonies and 14.0k research; letting a colony whose type has nothing to build use
  the Homeworld rows (Q55) gives 14.6k research; never entering Defend (Short Term) gives 17.2
  colonies, 0.5 bases and 13.4k research; placing Defense Bases whatever the queue backlog
  changes nothing. None of these closes the gap alone.
- **Settled from the executable** (spec 05 questions 53–56, 2026-10-01): the original's
  enemy-in-territory list is the engine's, so the first two experiments above depart from
  the original. What keeps our computer players out of Infrastructure was thought to be their
  exploration frontier, which also counted unknown links into explored systems (spec 05
  §7.2); once fixed it made little difference ("Pace after the frontier fix" below). Set
  against the original's statistics files, ours have the same population and resource
  output but about 40 % less research per colony from turn 25, while the original's fastest
  empires add about two Research Centers a turn (spec 05 question 56). The cause of that
  is spec 05 question 59.

### Pace after the frontier fix (OpenSE4, 2026-10-01)

Measured again once the engine followed the rules settled in spec 05 questions 53–56: the
exploration frontier, the territory claimed one turn late, colonization danger per empire,
the queue choice of Defense Bases and units, facility upgrades first and the facility
minister's lists (commits e1b910b, 4badd2c and a72b94d). The set-up is the five computers of
"Pace of the computer players" (seeds 1–12, Small quadrant, simultaneous, 100 `processTurn`
calls, `Empire::history`), run on the engine just before these changes (500ab8f, which
includes later work than the table above) and after them. Means per empire, original /
before / after:

| Turn | Colonies | Systems | Tech levels | Research | Ships | Bases | Units | Score |
|---|---|---|---|---|---|---|---|---|
| 10 | 1.7 / 1.5 / 1.5 | 1.0 / 1.0 / 1.0 | 17.2 / 17.4 / 17.4 | 3,612 / 3,505 / 3,501 | 2.0 / 2.5 / 2.5 | 0.0 / 0.0 / 0.0 | 14 / 10 / 10 | 19,664 / 19,942 / 19,929 |
| 25 | 5.3 / 5.3 / 5.3 | 2.2 / 1.9 / 1.9 | 19.2 / 19.0 / 18.9 | 5,935 / 6,097 / 6,131 | 3.0 / 3.3 / 3.5 | 0.3 / 0.3 / 0.2 | 23 / 22 / 21 | 30,579 / 31,137 / 31,067 |
| 50 | 11.2 / 10.3 / 10.8 | 3.5 / 3.2 / 3.3 | 24.8 / 22.9 / 23.0 | 13,865 / 9,619 / 10,018 | 7.2 / 7.4 / 6.8 | 0.6 / 0.2 / 0.1 | 38 / 34 / 36 | 65,216 / 56,653 / 54,190 |
| 75 | 14.8 / 14.3 / 14.4 | 4.5 / 4.0 / 3.9 | 31.4 / 28.0 / 28.2 | 17,852 / 12,366 / 12,438 | 10.4 / 10.0 / 11.4 | 0.8 / 0.3 / 0.1 | 76 / 67 / 85 | 97,235 / 83,649 / 89,384 |
| 100 | 19.6 / 16.4 / 16.5 | 5.6 / 4.4 / 4.4 | 39.4 / 33.7 / 33.8 | 23,502 / 14,155 / 13,582 | 10.7 / 10.2 / 11.3 | 0.9 / 0.2 / 0.0 | 98 / 94 / 103 | 125,370 / 94,306 / 99,113 |

(Standard errors after: colonies ±0.1 at turn 10, ±0.6 at 50, ±1.0 at 100; research ±520 at
50 and ±870 at 100; bases ±0.1.)

- **Nothing moved but the bases.** Colonies, research and tech levels are within the noise
  of the run before. Research per colony is 997 at turn 50 and 877 at turn 100 (the
  original's 1,423 and 1,310).
- **AI states**, share of all empire-turns 1–100, before / after: Exploration 34 / 35 %,
  Infrastructure 7 / 8 %, Prepare for Attack 2 / 3 %, Attack 2 / 2 %, Secure Holdings 1 / 1 %,
  Defend (Short Term) 53 / 52 %. Turns 51–100: Infrastructure 4 / 5 %, Defend (Short Term)
  79 / 78 %. With the frontier, territory and danger rules alone (e1b910b) Infrastructure
  was 9 % and Defend (Short Term) 51 %. 27 of the 60 empires never reach Infrastructure in
  100 turns (30 before); the others first do at a median of turn 19.
- **Why the frontier changed so little** (seeds 1–6, every turn): in the turns with contact
  and an empty enemy-in-territory list, the territory borders a system we have not explored
  in 75 % of them in turns 1–25, 71 % in turns 26–50 and 64 % in turns 51–100; the old rule
  (unknown links too) gave 78, 74 and 66 %. Our computers have explored 6, 9 and 12 of the
  23–37 systems in those periods; 4–5 frontier points are open, nearly all free; of their
  1.8, 3.2 and 6.2 attack ships, 0.3, 2.4 and 3.2 are in fleets and 0.5, 0.4 and 1.1 are idle
  outside fleets at the end of the turn.
- **Bases**: the original's Defense Base placement (spec 05 §7.5) sends a base to the K-th
  queue of the empire's list, mostly a colony without a yard, where it is lost; our bases
  fell from 0.2–0.3 to 0.0–0.1 per empire (10 empires of 60 had one at turn 100 before, 1
  after). The original's 0.6–0.9 therefore needs much more time in Infrastructure, or yard
  colonies early in the list, than ours have.
- **Sensitivity** (scratch, not kept): with every system explored from the start (the
  game option that shows every system), no frontier is left, but our computers stop
  exploring and meet nobody for longer (Exploration 93 % of turns 1–25, first
  Infrastructure at a median of turn 42); Defend (Short Term) is still 75 % of turns
  51–100, and research at turn 100 is 12.8k.
- Colonies at turn 50 / 100, the research per Research Compound and the moods are in spec
  05 question 59.

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

Since 2026-10-01 our client follows these observations (spec 06 §1.11); what they leave open
is asked in spec 06 §7 Q89–Q98.

## Session 4: the computer players under a debugger (2026-10-02)

### Pace observed under a debugger

**How.** The copy of session 3 (version 1.95 under Wine 11.18, nested 1024×768 display) ran
under a debugger whose breakpoints only read memory ("Harness notes" below). For each
computer empire the log recorded, at every start-of-turn state update, the AI state before
and after, the turns spent in it, the enemy-in-territory list (system, sector, owner and kind
of each entry), the claimed, explored and colonized systems, the frontier points, the
number of attack candidates, the treaty and the anger toward each other empire; at every
economy step, the research queue and the stock; at every Defense Base placement, the
empire's queue list, which queues have a yard and the queue chosen. Once a turn it listed
every ship (design type, fleet, orders) and every empire's statistics row, and every fifth
turn every colony (type, population, facilities, queue, mood). The logs are in
`reference/re/notes/aiwatch/` (not tracked).

The set-up matches our scratch harness: the Small quadrant of the first quadrant type,
default settings, simultaneous turns, the stock Terran empire file (its race's minister
style) and four random computer empires (the copy's `Settings.txt` rolls exactly four for
Medium), every score visible. The Terran empire was marked computer-controlled before the
first turn, so all five empires are computer players throughout. Three games of 100 turns,
on maps of 21, 26 and 31 systems.

Ours: 24 games (seeds 1–24) of the engine at 6ef2c3e through a scratch program (not
tracked) that writes the same records: a Small quadrant, simultaneous turns, the Terran
preset and four random races with random computer personalities, five computer players,
100 `processTurn` calls. Our maps have 20–37 systems; "small maps" are our 8 games of 20–26
systems.

**AI states.** Share of the empire-turns in Defend (Short Term) and in Infrastructure:

| Games | Systems | Defend 26–50 | Defend 51–100 | Defend, all | Infra. 26–50 | Infra. 51–100 | Infra., all | Empires ever in Infrastructure |
|---|---|---|---|---|---|---|---|---|
| Original, game 1 | 21 | 46 % | 66 % | 49 % | 18 % | 16 % | 13 % | 5 of 5 |
| Original, game 2 | 26 | 30 % | 49 % | 33 % | 19 % | 30 % | 21 % | 3 of 5 |
| Original, game 3 | 31 | 58 % | 73 % | 58 % | 4 % | 2 % | 2 % | 2 of 5 |
| Original, all three | | 45 % | 63 % | 46 % | 14 % | 16 % | 12 % | 10 of 15 |
| Ours, 24 games | 20–37 | 46 % | 76 % | 53 % | 11 % | 5 % | 7 % | 63 of 120 |
| Ours, small maps | 20–26 | 60 % | 78 % | 59 % | 10 % | 5 % | 7 % | 23 of 40 |

Exploration takes 80 % of the original's turns 1–25 (ours 72 %) and 37 % of all its turns
(ours 34 %); Prepare for Attack, Attack and Secure Holdings share the rest. Our games range
from 56 % to 99 % in Defend (Short Term) over turns 51–100 (median about 75 %) and from 0 % to 18 %
in Infrastructure: the original's second game lies outside that range, its first at the
low end and its third in the middle. So the original also spends most of its later turns
in Defend (Short Term), but about 13 points fewer than ours on average, with three times
our share of Infrastructure in turns 51–100; with three games the spread between games is
as large as the difference.

**Statistics.** Means per empire, original (15 empires) / ours (120):

| Turn | Colonies | Research | Research per colony | Resources produced | Tech levels | Ships | Bases | Score |
|---|---|---|---|---|---|---|---|---|
| 25 | 5.3 / 5.1 | 6,557 / 5,470 | 1,337 / 1,120 | 13.9k / 12.3k | 19.1 / 18.9 | 3.4 / 3.5 | 0.0 / 0.2 | 32.1k / 30.4k |
| 50 | 11.3 / 10.4 | 11,045 / 10,263 | 1,041 / 1,074 | 23.6k / 20.7k | 23.9 / 23.1 | 8.3 / 7.6 | 0.5 / 0.2 | 66.9k / 59.1k |
| 75 | 14.7 / 13.8 | 13,823 / 12,825 | 1,007 / 1,029 | 32.9k / 25.9k | 29.1 / 28.6 | 14.4 / 10.8 | 0.6 / 0.1 | 110.4k / 87.8k |
| 100 | 15.5 / 15.7 | 13,833 / 14,118 | 1,038 / 954 | 36.3k / 30.4k | 34.6 / 34.0 | 19.7 / 13.1 | 0.6 / 0.1 | 141.5k / 109.6k |

- **Research agrees** in this set-up. Research per colony at turn 50 / 100 was 1,233 / 1,230,
  968 / 849 and 924 / 1,035 in the three games; ours range from 568 to 1,483 at turn 50 and
  from 473 to 1,505 at turn 100. It falls as small domed colonies are added, so it follows the
  map and the colony count: the original's 21-system game sits among our 20–23-system games,
  its 26-system game among our 28-system ones. One original empire had 24,900 research
  points at turn 50 with 17 colonies, like the fast empires of session 3; 3 of our 120 had
  over 20,000 (at most 27,000). So the 40 % lead seen in session 3 (spec 05 questions 56 and
  59) is not a difference of the rules: those three games had another set-up (a human empire
  playing on every minister) and a spread of ±2,200 at turn 50.
- **Still different**: bases (0.5–0.6 against 0.1–0.2), ships from turn 75 (14.4 / 19.7
  against 10.8 / 13.1, the attack ships alone 9.1 / 13.2 against 6.9 / 8.1) and the resources
  produced (27 % and 19 % more at turns 75 and 100, with the same population: 2,887M /
  3,198M against 2,789M / 3,106M).
- **Colonies alike**: at turn 100 the original's empires hold 15.5 colonies, of which Mining
  4.4, Research Compound 3.9, Military Installation 1.5, Intelligence Compound 1.4, Refining
  1.1 and Construction Yard 1.1 (ours 15.7: 4.0, 4.0, 1.7, 1.4, 1.2, 1.4), and 22.7 Research
  Centers (ours 24.2), 12.1 of them on Research Compounds (ours 14.0) and 5 on the homeworld.
- **Moods differ**: per empire at turns 75 / 100, Jubilant colonies 3.2 / 3.3 (ours 0.8 /
  1.5), Happy 4.9 / 4.9 (5.3 / 5.1), Indifferent 6.1 / 5.8 (7.0 / 8.2), Unhappy or worse
  0.6 / 1.6 (0.7 / 1.0). Weighted by colony count, the mood modifiers of spec 02 come to
  107 % / 106 % of normal output against our 104 % / 104 %: a few per cent, not 20–27 % (spec
  05 question 61).

**What keeps them in Defend (Short Term)** (turns 51–100; original / ours):
- The enemy-in-territory list is not empty in 63 % / 76 % of the state updates. It holds, per
  update, 1.01 / 1.77 colonies of empires at war with the evaluating empire, 0.32 / 0.56
  colonies of empires without a treaty, 0.92 / 1.56 ships of empires at war and 0.14 / 0.40
  ships of empires without a treaty. Of the turns in Defend (Short Term), 46 % / 61 % have a
  colony of an enemy at war in the list, and 42 % / 25 % have only ships.
- Spells: 35 % / 63 % of all the turns in Defend (Short Term) lie in spells of 20 turns or
  more, 43 % / 25 % in spells of 5–19 turns. A spell ends toward Exploration 75 % / 67 % of
  the time and toward Infrastructure 25 % / 32 %.
- Hostile colonies removed: of the colonies of an enemy at war listed in a territory (counted
  every fifth turn), 33 % (37 of 113; 21–46 % per game) were gone ten turns later, against
  13 % of ours (190 of 1,456; median 13 % per game, 3–46 %).
- Treaties: over turns 51–100 neighbouring empires (one's territory holds a colony system of
  the other) were at war in 13 %, 44 % and 59 % of the pair-turns in the three games, ours in
  44 %; at turn 100, of the pairs that have met, 43 % / 49 % are at war and 38 % / 37 % have a
  trade treaty or better. No consistent difference.

**Fleets, explorers and ships.**
- Attack ships in fleets: 81 %, 74 %, 78 % and 77 % of the original's in turns 21–40, 41–60,
  61–80 and 81–100, against 68 %, 51 %, 45 % and 44 % of ours. At turn 90 the empire with the
  most ships in each game kept five or six fleets, two to four of them with four to nine
  attack ships each (one with 46 ships: fleets of 7, 7, 6, 6, 2 and 1 attack ships); empires
  with few ships had fleets of one to three. In a trace of one of our games (turns 20–70)
  every fleet held one ship: the size cap of spec 05 §7.5, trunc(vehicles × 80 / 100 / fleets
  wanted), was 1 with 4–7 vehicles and 3 fleets wanted.
- Attack ships outside fleets at the end of the turn, turns 51–100: idle 0.9–1.4 / 1.3–1.8,
  with orders 0.6–1.3 / 1.7–2.9 per empire. Our ships keep their Move To orders for several
  turns; the original's Seek orders last one movement phase (spec 05 §7.5).
- Explored systems, turns 51–100: 7.9–11.0 / 9.8–14.6 (ours 11.4–16.7 on the small maps);
  claimed systems 6.0–6.5 / 6.9–8.2. In the calm updates of an empire with contact, the
  territory borders an unexplored system in 40 % / 60 % of them (41 % on our small maps).
- Ships by design type per empire at turn 50 / 100: attack ships 4.6 / 13.2 (ours 5.1 / 8.1),
  carriers – / 1.3 (– / 1.8), colony ships 1.7 / 2.1 (1.5 / 1.8), population and troop
  transports 1.1 / 1.1 (0.7 / 0.7), Defense Bases 0.3 / 0.4 (0.1 / 0.0), Base Space Yards
  0.3 / 0.2 (0.1 / 0.1).

**Defense Bases.** All 119 Defense Base placements of the three games were made in
Infrastructure, by 9 of the 15 empires (one made 58), and every one went to the K-th queue of
the empire's queue list (spec 05 §7.5 "Placement"); 9 of them reached a queue with a yard. So
the original's bases come from its time in Infrastructure, under the rule ours already
follow.

**Lists and claims.**
- Claimed systems: in 1,463 of the 1,485 start-of-turn updates of the three games the claims
  were exactly those of spec 05 §7.2 "Territory", from the colonies and the warp links of the
  turn before (computer players closed warp points in two games). Each of the other 22 held
  one system more, for that turn only, as a traded system would; none lacked one.
- The economy step of player 1 found the lists of player 5's start-of-turn step on every turn;
  players 2–5 built their own (spec 05 §7.2 "Whose lists the economy step reads").

**Scratch experiments** (the engine at 6ef2c3e with the changes named, following spec 05
§7.2 and §7.5; not kept; the 24 games above; turns 51–100 unless marked "all"):

| Change | Defend, all | Defend | Infrastructure, all | Infrastructure | Attack ships in fleets, 81–100 | War colonies gone in 10 turns |
|---|---|---|---|---|---|---|
| none | 53 % | 76 % | 7 % | 5 % | 44 % | 13 % |
| attack candidates only when settleable or the owner is below None | 52 % | 74 % | 9 % | 6 % | 44 % | 14 % |
| one-turn Seek orders | 50 % | 73 % | 8 % | 6 % | 46 % | 15 % |
| Join Fleet pursuit | 52 % | 75 % | 8 % | 6 % | 46 % | 14 % |
| one-turn orders and pursuit | 48 % | 70 % | 8 % | 5 % | 47 % | 16 % |
| all three changes | 48 % | 70 % | 11 % | 8 % | 44 % | 16 % |

The exploration rules (12 games with Quick Start's races) moved Defend (Short Term) from
77 % to 76 % and Infrastructure from 4 % to 7 % of turns 51–100, within the noise. Together
the changes close about half of the gap in Defend (Short Term) and a quarter of the gap in
Infrastructure; the rest goes with the original's larger fleets, its extra ships and
resources and the faster removal of hostile colonies (spec 05 questions 61–63).

### Pace after the movement rules (OpenSE4, 2026-10-02)

Measured again once the engine followed the rules this session settled (spec 05 §7.2, §7.5,
question 60): one-turn Seek movement orders and the stored Attack, the Join Fleet pursuit,
the attack candidates' test, the explorers and their point list, the claim rewrite first in
the Politics minister's run, the economy step's borrowed colonization list, and the queue
list's yard and order details (commits 59652e4 to 28fad22; the choices the text leaves open
are spec 05 question 64). The set-up and records are those of "Pace observed under a
debugger": seeds 1–24, a Small quadrant, five computer players, simultaneous turns, 100
`processTurn` calls. Original / before (41f7d8b) / after:

| | Original | Before | After |
|---|---|---|---|
| Defend (Short Term), turns 26–50 / 51–100 / all | 45 / 63 / 46 % | 46 / 76 / 53 % | 44 / 72 / 51 % |
| Infrastructure, turns 26–50 / 51–100 / all | 14 / 16 / 12 % | 11 / 5 / 7 % | 10 / 8 / 8 % |
| Exploration, turns 1–25 / all | 80 / 37 % | 72 / 34 % | 75 / 35 % |
| Empires ever in Infrastructure | 10 of 15 | 63 of 120 | 63 of 120 |
| Defend turns in spells of 20+ / 5–19 turns | 35 / 43 % | 63 / 25 % | 64 / 25 % |
| Defend spells ending in Exploration / Infrastructure | 75 / 25 % | 67 / 32 % | 67 / 32 % |
| Enemy-in-territory list not empty, turns 51–100 | 63 % | 76 % | 72 % |
| Per update: colonies at war / no treaty, ships at war / no treaty | 1.01 / 0.32, 0.92 / 0.14 | 1.77 / 0.56, 1.56 / 0.40 | 1.84 / 0.28, 1.46 / 0.23 |
| Defend turns with a colony at war listed / with ships only | 46 / 42 % | 61 / 25 % | 64 / 27 % |
| Listed colonies at war gone 10 turns later, turns 51–100 | 33 % | 13 % (190 of 1,456) | 15 % (236 of 1,626) |
| Attack ships in fleets, turns 21–40 / 41–60 / 61–80 / 81–100 | 81 / 74 / 78 / 77 % | 68 / 51 / 45 / 44 % | 79 / 73 / 70 / 69 % |

Means per empire at turns 25 / 50 / 75 / 100:

| | Original | Before | After |
|---|---|---|---|
| Colonies | 5.3 / 11.3 / 14.7 / 15.5 | 5.1 / 10.4 / 13.8 / 15.7 | 5.3 / 10.4 / 13.9 / 15.6 |
| Research | 6.6k / 11.0k / 13.8k / 13.8k | 5.5k / 10.3k / 12.8k / 14.1k | 5.5k / 10.7k / 13.2k / 14.6k |
| Resources produced | 13.9k / 23.6k / 32.9k / 36.3k | 12.3k / 20.7k / 25.9k / 30.4k | 12.6k / 21.4k / 26.4k / 29.6k |
| Ships | 3.4 / 8.3 / 14.4 / 19.7 | 3.5 / 7.6 / 10.8 / 13.1 | 3.4 / 7.3 / 11.8 / 14.0 |
| Bases | 0.0 / 0.5 / 0.6 / 0.6 | 0.2 / 0.2 / 0.1 / 0.1 | 0.2 / 0.2 / 0.1 / 0.2 |

- **Fleets** now hold the original's share of the attack ships (69–79 % against 74–78 %),
  and the hostile ships listed in our territories fell by about a fifth (on 120 seeds,
  below): the warships are free again at each start of turn and the recruits reach their
  fleets.
- **Defend (Short Term) barely moved.** On these 24 seeds the share fell by 2 points over
  all turns and 4 over turns 51–100, but the spread between seeds is as large. On 120 seeds
  (1–120, the same set-up; before / after) the shares are 50 / 50 % of all turns and 72 /
  72 % of turns 51–100, Infrastructure 8 / 7 % and 6 / 7 %, 332 / 307 of 600 empires ever in
  Infrastructure, the list not empty 72 / 72 % of turns 51–100, with 1.95 / 1.59 hostile
  ships and 2.14 / 2.21 hostile colonies per update, and 16 / 15 % of the listed colonies at
  war gone ten turns later. So the 24 seeds of "Pace observed under a debugger" sit 3–4
  points above our long-run Defend share; against the original's three games the gap is
  about 4 points over all turns and 9 over turns 51–100.
- **Rule by rule** (120 seeds, paired by seed; Defend over all turns / turns 51–100,
  standard errors about 1 point): the one-turn orders with the Join Fleet pursuit −1.6 /
  −2.0 points, Infrastructure +0.5; the attack candidates' test Infrastructure +0.6 more,
  Defend unchanged; the exploration rules +1.5 / +1.1 back, Infrastructure −1.9, Exploration
  +0.8; the remaining rules nothing measurable. The explorers' rule takes, besides idle ships,
  those whose first order is a Seek, so the Exploration minister, which acts after Defense and
  Attack, sends some of their ships exploring instead (spec 05 §7.5): a scratch run in which
  it left the ships ordered that turn alone gave −2.2 / −1.8 points with Exploration +4.1.
- **What remains** is the hostile colonies at war in our territories: 1.8 per update against
  the original's 1.0, removed at 15 % within ten turns against 33 % (spec 05 question 63),
  with fewer ships and resources from turn 75 (questions 61 and 62). The bases follow the
  Infrastructure time, which did not grow.

### Resources, ships and colony losses under a debugger (2026-10-02)

**How.** Two more games of the original under the debugger of "Pace observed under a
debugger", with the same set-up, on maps of 33 and 34 systems; each game's four computer
races were read from the Players window (`reference/observe/2026-10-02/`). New read-only
breakpoints recorded, besides the records of that section: each colony's production of each
resource at the income step of every fifth turn (the facilities' generation, the planet's
value, the value-adjusted amount, the step-4 percentage and the output, spec 02 §5.1); each
colony's happiness update every turn, term by term, with the empire-wide part; each empire's
list of happiness events at its update; every ship's identity, owner, design type, position
and fleet, every colony and every planet's values, every turn. Ours: the 24 games of that
section on the engine after the movement rules (0d71f41), with the same records and the same
happiness breakdown, and for each of the five original games 24 games of ours with that
game's line-up (the races fixed, everything else as before). "Original" below is all five
games unless a game is named.

**The five games together.** With the two new games the original's averages move toward
ours; the first three games sat at the favourable end:

| Turns 51–100 unless marked | Original, 3 games | Original, 5 games | Ours (0d71f41) |
|---|---|---|---|
| Defend (Short Term), all turns / turns 51–100 | 46 / 63 % | 48 / 70 % | 51 / 72 % |
| Infrastructure, all turns / turns 51–100 | 12 / 16 % | 9 / 11 % | 8 / 8 % |
| Exploration, turns 1–25 | 80 % | 85 % | 75 % |
| Enemy-in-territory list not empty | 63 % | 70 % | 72 % |
| Per update: colonies at war / no treaty | 1.01 / 0.32 | 1.55 / 0.78 | 1.84 / 0.28 |
| Defend turns in spells of 20 turns or more | 35 % | 49 % | 64 % |
| Listed colonies at war gone ten turns later | 33 % | 18 % (53 of 291) | 15 % |
| Attack ships in fleets, turns 41–100 | 74–78 % | 75–78 % | 69–73 % |

The game on 34 systems spent 94 % of turns 51–100 in Defend (Short Term), more than any of
our 24 games (at most 91 %).

**Statistics.** Means per empire, original (25 empires) / ours (120):

| Turn | Colonies | Research | Resources produced | Ships | Tech levels |
|---|---|---|---|---|---|
| 25 | 5.7 / 5.3 | 6.9k / 5.5k | 14.0k / 12.6k | 3.5 / 3.4 | 19.0 / 18.9 |
| 50 | 12.2 / 10.4 | 12.1k / 10.7k | 25.6k / 21.4k | 8.6 / 7.3 | 23.9 / 23.2 |
| 75 | 16.6 / 13.9 | 15.5k / 13.2k | 34.5k / 26.4k | 14.1 / 11.8 | 29.9 / 29.0 |
| 100 | 17.0 / 15.6 | 15.5k / 14.6k | 36.9k / 29.6k | 17.4 / 14.0 | 35.6 / 34.8 |

Per game, against the spread of our 24 games: resources produced 25 % higher at turn 100
(z 3.4) and 31 % at turn 75 (z 3.3); ships 24 % higher at turn 100 (z 1.5); colonies 19 %
higher at turn 75 (z 1.6) and 9 % at turn 100 (z 0.7).

**Production.**
- Every output of every colony at the income step of every fifth turn of the two new games
  (13,100 values) followed spec 02 §5.1, and the step-4 percentages gave each race exactly
  the effects of our random race build; 45 values came out one below the exact product
  (the x87 product), as ours do.
- The five line-ups came in group order 1, 2, 4, 1 (spec 05 §7.1). The race draw matters:
  three of the original's five games drew the group-4 race with +20 % minerals (the other
  one has none) and two drew a group-1 race with +20 % on all three resources. With each
  game's own line-up (the same four races, set in our scratch program), our engine produces
  30.0k, 26.7k, 27.0k, 29.8k and 27.5k at turn 75 and 34.1k, 29.7k, 30.4k, 34.4k and 30.2k
  at turn 100 (standard deviations 5.4k–9.1k); the original produced 38.1k, 31.7k, 28.9k,
  33.2k and 40.4k, and 37.2k, 38.7k, 32.9k, 34.0k and 41.8k. So the matched gap is 22 % at
  turn 75 (z 2.2) and 16 % at turn 100 (z 1.6).
- Generation of the colonies' facilities (the sum of their `Resource Generation` values),
  per empire, original / ours: 24.3k / 19.8k at turn 50, 33.1k / 25.3k at 75, 35.1k / 28.5k
  at 100. Resources produced per unit of generation: 1.05 / 1.04 at turns 75 and 100 (per
  game 0.95–1.34 in the original, 0.64–1.39 in ours). Colonized planets have the same
  values (72–78 % on each resource at turn 50 in the new game against 74–76 % in ours; all
  planets 0–150 %, asteroid fields 50–300 %), and the mood and population percentages
  weighted by generation are the same (1.14 / 1.14 at turn 75, 1.12 / 1.14 at turn 100).
- So the extra resources come from colonies: 16.6 against 13.9 at turn 75, 17.0 against
  15.6 at turn 100, with more colonies per colonized system (4.3 against 3.5 at turn 75) and
  slightly more of their facilities generating resources (2.50 of 4.90 per colony against
  2.33 of 4.94 at turn 75; 2.66 of 5.08 against 2.37 of 4.93 at turn 100). Per colony the
  generation is 7–12 % higher.
- Colony ships built per empire in turns 1–25, 26–50, 51–75 and 76–100: 6.7, 6.8, 6.2 and
  4.0, and 6.5, 9.4, 7.2 and 5.2 in the two new games, against our 5.5, 6.4, 4.8 and 4.2.
  The original's computers spend more of turns 1–25 in Exploration (85 % against 75 %),
  where the stock vehicle table asks for a colony ship per three colonies (one per eight in
  Infrastructure, one per ten in Defend (Short Term)); ours spend 7 % of those turns in
  Infrastructure (the original 2 %) and 15 % in Defend (Short Term) (12 %). In turns 1–25
  ours have explored 5.7 systems on average (the original 4.8) and have more attack ships,
  the explorers (1.63 against 1.17 in turns 1–10, 1.94 against 1.62 in turns 11–20), so in
  calm turns their territory borders unexplored space less often (76 % against 90 % of the
  calm turns with contact).

**Happiness.**
- Per colony and turn, in tenths (two new games / ours), turns 51–75: empire-wide part
  −1.3 / −1.6, drift +8.8 / +8.8, sector and system events −0.4 / −0.6, ships present
  −5.4 / −6.1, total +1.7 / +0.4; turns 76–100: +3.6 / +0.2, +3.2 / +8.1, −0.1 / −0.3,
  −5.5 / −6.9, total +1.0 / +0.8. Moods at turns 76–100: Happy 25 / 36 %, Jubilant 4 / 10 %,
  Unhappy or worse 13 / 6 %.
- Jubilant colonies per empire at turn 75 in the five games: 8.6, 0.4, 0.6, 1.6 and 0.0;
  ours 0.8 on average, at most 4.2 in a game. The first game, where most neighbours held
  Non-Aggression or better, is the only happy one: a new trade treaty or better calms every
  colony of both empires at once (spec 02 §4).
- No update in the two new games met a `Ship Constructed` or `Facility Constructed` event:
  the event list is not saved, and the host reloads the game every turn (spec 02 §4).
  Battle, ship-loss, population-loss, colonization and treaty events arrived every turn
  they happened.

**Ships.** Attack ships built / lost per empire, turns 26–50, 51–75 and 76–100 (identities
compared turn by turn):

| | 26–50 | 51–75 | 76–100 | Turns 26–100 |
|---|---|---|---|---|
| Original, five games | 5.2 / 2.0 | 8.0 / 4.1 | 8.3 / 6.4 | 21.4 / 12.5 (per game 7.6–15.0 lost) |
| Ours | 5.3 / 2.5 | 8.9 / 6.2 | 10.0 / 9.2 | 24.2 / 17.9 (16.7–18.9 with each original line-up) |

Lost per attack ship and turn in turns 51–100: 1.9 % in the original (2.2 % in fleets, 1.0 %
outside) against 4.2 % in ours (4.0 % and 4.9 %); before the movement rules ours lost 4.4 %.

**Battles** per empire and 25 turns, turns 51–100 (60–100 for the first new game), from
the happiness events of the original and our battle reports, by where they were fought and
whether the empire won, lost or drew:

| Where | Original, game 4 / game 5 | Ours, same line-ups |
|---|---|---|
| No colony in the sector | won 4.1 / 3.3, lost 4.1 / 3.3, drawn 1.0 / 1.2 | won 5.2 / 4.9, lost 5.3 / 5.0, drawn 9.2 / 7.0 |
| An enemy colony | won 2.1 / 3.2, lost 0.9 / 0.8, drawn 0.6 / 0.2 | won 0.9 / 1.1, lost 0.4 / 0.7, drawn 1.4 / 1.1 |
| Its own colony | won 0.9 / 0.8, lost 2.1 / 3.2, drawn 0.6 / 0.2 | won 0.3 / 0.6, lost 0.9 / 1.2, drawn 1.4 / 1.0 |
| Colonies lost / population killed | 2.2 / 3.6, 128M / 208M | 1.0 / 1.4, 72M / 156M |

A battle at an enemy colony is won only when the colony is gone at its end (spec 04 §15
"The verdict"). Our battles away from colonies are seven times as often drawn, mostly with
no losses on either side; the decisive ones cost about the same per battle.

**Hostile colonies in our territories**, fate ten turns after being listed (turns 51–90),
original / ours: colonies at war gone 18 / 13 % (the planet left, the colony depopulated
in a battle), owner changed 1 / 1 %, still hostile 82 / 85 %; colonies of empires without
a treaty gone 20 / 23 %, now at Non-Aggression or better 13 / 10 % (35 % in the first three
games). No planet was destroyed. Of the colonies that disappeared in turns 51–100, 93 % in
the first three games and 80 % in ours had 21–100M people at the last record; 2 % (ours 6 %)
were held by another empire at the next record.

**What this settles** (spec 05 questions 61–63, 65–67): no rule of production or happiness
differs except the unsaved event list; the race draw explains part of the resource gap and
the colony count the rest; the extra ships of the original are fewer losses, not more
construction; and its hostile colonies go in battles it wins at the colony, at a rate that
with five games is close to ours.

### Battles, bases and the first turns under a debugger (2026-10-02)

**How.** Two more games of the original (games 6 and 7) under the debugger of "Pace
observed under a debugger", with the same set-up, on maps of 30 and 25 systems. New
read-only breakpoints recorded every battle at its setup and at its verdict: each piece's
owner, kind (ship or base, planet, fighter, satellite or drone group, neutral obstacle),
hit points at the start and at the end, shields and the sector it came from, for ships the
design type, the number and first kind of their orders, the fleet and the number of
weapons, for planets the population, and the number of combat turns fought. They also
recorded every Defense Base placement with the empire's queue list and which entries have a
working yard, each empire's soft-cap test with the revenue and maintenance it compared, and
every design's owner, type, creation date and obsolete flag. Ours: the 24 games of
"Resources, ships and colony losses under a debugger" (0d71f41), with the same battle
records plus the shots, hits and moves of each battle, and scratch builds that change one
rule at a time (24 games each unless marked). A side is "armed" when one of its ships has a
weapon, "unarmed" when it has ships but none armed, "units" when it has only unit groups,
"colony" when it has a populated planet; a battle is drawn when more than one side has
pieces left at its end.

**Battles** per empire and 25 turns of turns 51–100, each battle counted once (drawn share
in brackets):

| Sides | Original, game 6 | Original, game 7 | Ours |
|---|---|---|---|
| Armed against a colony | 3.2 (16 %) | 6.3 (39 %) | 1.8 (32 %) |
| Armed against armed | 1.9 (0 %) | 1.3 (15 %) | 3.2 (7 %) |
| Armed against unarmed | 0.6 (0 %) | 3.7 (86 %) | 3.0 (57 %) |
| Armed against units | 0.3 (0 %) | 7.8 (94 %) | 1.0 (62 %) |
| Unarmed against unarmed, or a colony against unarmed | 0.1 | 0 | 1.5 (82 %) |
| All | 6.1 | 19.1 | 11.3 |
| Second or later battle in the same sector and turn | 13 % (up to 2) | 61 % (up to 29) | 24 % (up to 10) |

Counted the way of "Resources, ships and colony losses under a debugger" (each empire's
battle events, which matched the battle records one for one in both new games), the four
original games with full records had 0.8, 1.2, 0.0 and 21.5 drawn battles away from
colonies per empire and 25 turns, against 1.0–21.8 in our games (median 6.2), and won 3.4,
3.3, 2.3 and 2.1 away from colonies, against 4.9 in ours (3.0–10.3 per game). Attack ships
lost in battle per empire and 25 turns: 4.2 and 4.9 in the new games, 6.1 in ours.

- Most of game 7's drawn battles were one standoff: five attack ships of one empire, three
  of them damaged, in a sector with another empire's satellite group and an unarmed ship.
  Each battle ran 30 combat turns without a hit, and because a survivor was below full
  structure it was fought again on most days (spec 03 §6.3 step 6): 106 battles in that
  sector in turns 51–100. Some of those ships had no orders.
- 27 % of our battles of turns 51–100 end without a shot, nearly all drawn: armed ships
  against unarmed ships (38 % of them), a colony against unarmed ships (15 %), armed ships
  against satellite groups (15 %), unarmed against unarmed (14 %). Traced in a scratch
  build: an attack ship with only missiles beside a satellite group (the missiles'
  `Weapon Target` leaves satellites out, so the ship has nothing to aim at and keeps away,
  while the satellites never come in range); a damaged attack ship with one movement point
  after a troop transport with three; an attack ship stopped behind a large piece by the
  step rule of spec 04 §5.
- The unarmed sides of our drawn battles against armed ships: troop transports in 49 % of
  them and carriers in 28 %, nearly three in four in fleets. Fleet-turns of turns 51–100 by
  members: ours 7 % troop transports alone, 6 % carriers alone; the original (game 6) 89 %
  attack ships alone, 1 % carriers alone, no troop fleet: a troop transport or boarding ship
  never leads a new fleet (spec 05 §7.5, confirmed: binary).
- Of our drawn battles, 55 % were in a sector that also had a battle the turn before; in
  game 6 two of six, in game 7 63 %.

**Battles at an enemy colony** (two sides, armed attackers), per empire and 25 turns of
turns 51–100: the colony gone at the end 2.2 and 1.9 in the new games, 0.9 in ours. The
colonies taken had a median 425 and 420 hit points (spec 04 §11; ours 520), and fell to a
median two armed ships in 11–12 combat turns (ours 10). The attacks that failed: game 6, 10
battles on colonies of median 666 hit points; game 7, 43 battles on colonies of median
15,490 with two unit groups; ours 224 battles on 62 colonies (one colony 56 times, 24 %
without a shot), median 2,675 hit points, the attackers in fleets with Seek orders in nine
cases of ten. Colonies lost per empire and 25 turns, all four games with full records:
1.8, 3.6, 2.2 and 2.0, against our 1.1. Without the + 1 our defend list adds for each
enemy colony (spec 05 §7.2), 17 of our 24 games played out identically.

**Losses outside battle and the soft cap.** Every ship of game 6 that vanished outside a
battle (16) did so on a turn its empire was over the soft cap, and no base was scrapped.
Turns over the soft cap: game 6 0 % of turns 26–50 and 14 % of turns 51–100, game 7 0 and
16 %; ours 14 and 23 %, and 4 and 14 % with colony ships left out of the maintenance as the
original does (spec 05 §7.5). Our attack ships lost per empire and 25 turns of turns 51–100:
6.1 in battle, 1.1 scrapped, 0.4 otherwise.

**Bases.** All 55 Defense Base placements of the new games went to the K-th queue (spec 05
§7.5), 16 of them to a queue with a yard (54 and 16 in game 6, which spent 18 % of turns
51–100 in Infrastructure; game 7 spent none and placed one). Per placement the list held a
median 11 queues, 23 % of them with a yard; ours 13 and 28 %, and 21 % of our placements
reached a yard (153 of 742). Per empire in 100 turns, game 6 / ours: Defense Bases built
0.6 / 0.58, lost 0 / 0.44; Base Space Yards built 0.8 / 0.40, lost 0.2 / 0.30; bases at
turn 100 1.2 / 0.24. Ours lose them to scrapping: the original scraps the oldest design
among all its ships that can move and the bases at a yard, ours only among the ships at a
yard, which bases always are (spec 05 §7.5 *Scrap*). Scratch runs, bases per empire at turn
100: the soft cap without colony ships 0.29; the original's scrap candidates 0.42; both
0.41; both with the fleet leader rule 0.50.

**Fleet leaders, scratch run** (spec 05 §7.5, the original's leader rule): battles 11.3 →
9.7 per empire and 25 turns (standard error 0.9 over 24 games), armed against unarmed 3.0
→ 1.9, unarmed against unarmed 0.8 → 0.6, a colony against unarmed 0.7 → 0.5; attack ships
lost in turns 26–100 17.9 → 16.5; time in Defend (Short Term) 72 → 70 % of turns 51–100.
With all three rules (leaders, soft cap, scrap candidates) battles were 10.9.

**The combat step, scratch run** (spec 04 §5, the original's tries around a blocked
square, 12 games): battles with an armed piece stopped in at least five combat turns 11 →
3 %, drawn battles 42 → 39 %, battles 10.1 → 10.1 per empire and 25 turns.

**The first turns.** Four original games (20 empires): first attack ship at turn 2, second
at turn 9 (median), first colony ship at turn 7, second at turn 11; attack ships alive at
turns 5 / 10 / 20: 1.05 / 1.8 / 2.05. Ours: 2, 5, 9 and 11; 2.0 / 1.9 / 2.0. The homeworld
queues of the new games, every empire alike: after turn 1, two attack ships and a weapon
platform; on turn 2 the first attack ship is finished, a new Attack Ship design is made
and the turn-1 design becomes obsolete, the second attack ship leaves the queue, and the
ministers queue weapon platforms, satellites and a colony ship; an attack ship of the new
design is queued on turn 4 (one empire of ten on turn 3), behind them. Ours queue it on
turn 2 (spec 05 questions 65, 70).

**What this settles** (spec 05 questions 65–67, 68–71 new): drawn battles and their daily
repeats happen in the original by the same rules and vary from game to game; the
differences found are the fleet leader rule, the soft cap without colony ships, the scrap
candidates and the combat step, and what is left is how often our computers fight decided
battles away from colonies and how rarely they attack weak enemy colonies.

### Pace after the scrap, cap and fleet rules (OpenSE4, 2026-10-02)

Measured again once the engine followed the rules "Battles, bases and the first turns under a
debugger" found: the soft and hard caps without colony ships, the scrap candidates and the
Move To a yard before scrapping, the fleet leaders, the defend list's colony threat (spec 05
§7.2, §7.5, question 72), the step toward a square in combat (spec 04 §5, question 90), and
the happiness events that no game file keeps (spec 02 §4). The set-up and records are those
of "Pace observed under a debugger" (seeds 1–24, a Small quadrant, five computer players,
simultaneous turns, 100 `processTurn` calls), with two more records: each empire's soft-cap
test at the start of every turn, made by the engine's own planner, and every battle's pieces
with their owner, kind, design type and fate. The runs after the change drop the waiting
happiness events after each turn, as the original's hosted games and our local simultaneous
games do; on the engine before the change that changes nothing measurable (120 seeds: +0.4
points over the soft cap, standard error 0.5; −0.2 battles, 0.5). Original / before
(856e694) / after; battles count each battle once, per empire and 25 turns of turns 51–100:

| | Original | Before | After |
|---|---|---|---|
| Bases per empire at turns 50 / 75 / 100 | 0.5 / 0.6 / 0.6 (three games); at turn 100 0.4–0.6 in five games, 1.2 and 0.0 in two more | 0.22 / 0.09 / 0.23 | 0.43 / 0.36 / 0.35 |
| Bases built / lost per empire in 100 turns | 1.4 / 0.2 (game 6) | 0.98 / 0.74 | 0.73 / 0.37 |
| Turns over the soft cap, 26–50 / 51–100 / 26–100 | 0 / 14–16 / 9–11 % (two games) | 14 / 23 / 20 % | 4 / 18 / 13 % |
| Battles | 6.1 and 19.1 (games 6, 7) | 11.4 (42 % drawn) | 9.6 (33 % drawn) |
| Decided battles away from colonies | 2.1–3.4 (four games) | 4.9 | 4.8 |
| Attack ships lost in battle (away from colonies) | 4.2 and 4.9 | 6.1 (5.1) | 5.6 (4.7) |
| Battles at an enemy colony ending with the colony gone; colonies lost | 2.2 and 1.9; 1.8–3.6 (four games) | 0.9; 1.1 | 1.0; 1.1 |
| Attack ships built / lost per empire, turns 26–100; lost per ship and turn, 51–100 | 21.4 / 12.5; 1.9 % | 24.1 / 17.9; 4.2 % | 22.8 / 16.8; 4.0 % |
| Attack ships lost outside battles | — | 1.6 | 1.4 |
| Fleet-turns of turns 51–100: troop transports alone / carriers alone | none / 1 % (game 6) | 7 / 6 % | 0 / 6 % |
| Defend (Short Term), all turns / turns 51–100 | 48 / 70 % (five games) | 51 / 72 % | 50 / 71 % |
| Infrastructure, all turns / turns 51–100 | 9 / 11 % | 8 / 8 % | 8 / 7 % |
| Exploration, turns 1–25 | 85 % | 75 % | 75 % |

Means per empire at turns 25 / 50 / 75 / 100 (the original's five games):

| | Original | Before | After |
|---|---|---|---|
| Colonies | 5.7 / 12.2 / 16.6 / 17.0 | 5.3 / 10.4 / 13.9 / 15.6 | 5.2 / 10.4 / 13.4 / 15.0 |
| Resources produced | 14.0k / 25.6k / 34.5k / 36.9k | 12.6k / 21.4k / 26.4k / 29.6k | 12.5k / 21.2k / 26.3k / 29.5k |
| Ships | 3.5 / 8.6 / 14.1 / 17.4 | 3.4 / 7.3 / 11.8 / 14.0 | 3.5 / 7.7 / 11.6 / 13.5 |

On 120 seeds (1–120, the same set-up) the colonies at turn 100 go from 15.7 to 15.4, the
resources stay at 30.8k and the ships go from 14.2 to 14.7, within the spread between seeds.

**Rule by rule** (120 seeds, every run dropping the waiting events, each step adding one
rule to the one before; paired by seed, standard errors of the change in brackets):

| | Turns 26–100 over the soft cap | Bases per empire at turn 100 | Battles | Drawn battles | Defend (Short Term), turns 51–100 |
|---|---|---|---|---|---|
| Before | 20.6 % | 0.14 | 12.1 | 47 % | 72 % |
| Soft cap without colony ships | 9.4 % (−11.2, 0.6) | 0.23 (+0.09, 0.03) | 11.8 | 46 % | 70 % (−2.5, 0.9) |
| Scrap candidates | 11.6 % (+2.2, 0.4) | 0.37 (+0.14, 0.03) | 11.7 | 47 % | 70 % |
| Fleet leaders | 11.7 % | 0.36 | 11.1 (−0.7, 0.6) | 45 % | 69 % |
| Defend list colony threat | 11.6 % | 0.37 | 11.1 | 44 % | 68 % |
| Combat step | 12.1 % | 0.36 | 10.2 (−0.9, 0.6) | 37 % | 70 % |
| All, against before | −8.6 (0.7) | +0.21 (0.04) | −2.0 (0.7) | | −1.8 (1.0) |

- **The soft cap** alone brings the turns over it close to the original's; the bases the
  scrap candidates keep add their maintenance back (+2 points). What is left is turns 26–50:
  3.5 % against none (spec 05 question 71).
- **The bases** stay: 0.36 per empire at turn 100 against 0.14 before and the original's
  0.4–0.6. Of the 0.33 per empire lost in 100 turns, 0.31 are scrapped. In a trace of the
  24 games, two thirds of the bases scrapped were of designs made on the first turn, most
  of them tied on the creation date with a ship design of that turn, and the tie goes to
  the first in the vehicle list, often the base; the others were the oldest design left.
- **The fleet leaders** take every troop-transport fleet away; fleets of carriers alone stay
  at 5 %, as a carrier with fighters aboard may lead.
- **The defend list's colony threat** changed nothing in 95 of the 120 games.
- **The combat step** takes a seventh off the drawn battles (47 to 37 % of all battles with
  every rule; per game −6.4 points, 2.2).
- **What remains**: decided battles away from colonies (4.8 against 2.1–3.4) and attack
  ships lost in battle (5.6–6.0 against 4.2–4.9), spec 05 question 68; enemy colonies taken
  (1.0 against 1.9–2.2), question 69; the colonies, ships and resources from turn 50,
  questions 61, 62 and 65.

### Pace after the tie-break and budget rules (OpenSE4, 2026-10-02)

Measured again once the engine followed the scrap tie-break (candidates met in slot order,
dates compared strictly) and the construction budget's queue commitments (net income less
the maintenance of the moment and what the colony queues will spend this turn, the
start-of-turn figure kept for the upgrades; spec 05 §7.5, questions 71 and 73). Set-up and
records as in "Pace after the scrap, cap and fleet rules", the waiting happiness events
dropped after each turn. Original / before (6fcd48e) / after, seeds 1–24:

| | Original | Before | After |
|---|---|---|---|
| Bases per empire at turns 50 / 75 / 100 | 0.5 / 0.6 / 0.6 (three games) | 0.43 / 0.36 / 0.35 | 0.30 / 0.37 / 0.35 |
| Turns over the soft cap, 26–50 / 51–100 | 0 / 14–16 % (two games) | 4.5 / 17.7 % | 1.0 / 11.9 % |
| Ships per empire at turns 25 / 50 | 3.5 / 8.6 (five games) | 3.5 / 7.7 | 3.2 / 7.3 |
| Defend (Short Term), all turns / turns 51–100 | 48 / 70 % | 50 / 71 % | 50 / 71 % |
| Infrastructure, all turns / turns 51–100 | 9 / 11 % | 8 / 7 % | 8 / 7 % |
| Exploration, turns 1–25 | 85 % | 75 % | 74 % |
| Colonies / resources produced at turn 100 | 17.0 / 36.9k | 15.0 / 29.5k | 16.4 / 32.5k |

Over 120 seeds, paired by seed (standard errors of the change in brackets): turns over the
soft cap 3.5 → 1.2 % of turns 26–50 (−2.4, 0.4) and 16.3 → 11.0 % of turns 51–100 (−5.4,
0.7); ships at turns 25 / 50 3.5 / 8.1 → 3.3 / 7.6 (−0.27, 0.04; −0.46, 0.11), at turn 100
14.7 → 15.0; bases at turns 50 / 75 / 100 0.35 / 0.36 / 0.36 → 0.29 / 0.33 / 0.34 (−0.07,
0.02 at turn 50); colonies at turns 50 / 100 10.7 / 15.4 → 11.0 / 16.4 (+0.33, 0.08; +0.98,
0.21); resources produced at turn 100 30.8k → 33.2k (+2.4k, 0.5k); Defend (Short Term) in
turns 51–100 70 → 72 % (+1.5, 1.1), Infrastructure 6.5 → 6.2 %, Exploration in turns 1–25
77 % unchanged.

- **The budget rule** does it all: the tie-break alone changed the course of 116 of the 120
  games and none of these figures measurably (turns 51–100 over the soft cap −0.8 points,
  0.4).
- **Turns 26–50 over the soft cap** are now close to the original's none (77 of 120 games
  have none). **Turns 51–100** fell below the original's two games: per game, our median
  is 11 % and our quartiles 6 and 15 %, so the original's 14 and 16 % sit at our upper
  quartile. Per game the share does not follow the ships held (correlation −0.15 with the
  ships at turn 75).
- **What the budget kept goes to colonies**: a colony more per empire at turn 100 and 8 %
  more resources, closing three fifths of the gap to the original in colonies and two
  fifths in resources (spec 05 questions 61, 65), with fewer ships at turns 25 and 50 and
  about as many from turn 75 (question 62). The bases placed early fall a little (0.29
  against 0.35 at turn 50).

### The computer players' second round under a debugger (2026-10-03)

**How.** More games of the original under the debugger of "Pace observed under a debugger",
with the same set-up (game 10 on a map of 30 systems; later games below), on a nested
display of their own. New read-only breakpoints recorded: every vehicle's daily action in
the movement phase (the day, the vehicle, its number of orders before the action and its
sector after it), the day of every battle, every ship's orders after its empire's
start-of-turn ministers (count, first order's kind and target), and in turns 1–30 the Ship
Construction minister's counts per design type before its clean-up of obsolete items and the
backlog test of each placement. Ours: the engine at aee3b7f with the same records, 120 seeds
of the pace set-up (the waiting happiness events kept, so the battles run a little above
"Pace after the tie-break and budget rules"), and scratch builds that each add one rule.

**The second attack ship** (spec 05 question 70). On turn 2 every empire of game 10 counted
two attack ships, both still queued, before the clean-up removed the second (its turn-1
design had become obsolete that turn); the Attack Ship row ("at least 2") was satisfied, and
the minister queued two weapon platforms, satellites and a colony ship. The second attack
ship came on turn 9 and the first colony ship on turn 7 (median, as in the four earlier games).

**Idle ships and the daily battle check** (question 68). Of the ships' daily actions in
game 10, 45 % were made with an empty order list; 14 of its 202 battles began in a sector
where only such idle ships had acted that day (125 where a vehicle with orders had acted, 63
where only other vehicles had).

**Fleets and their orders** (questions 68, 69). After the start-of-turn ministers:

| Attack ships, game 10 / ours (24 seeds) | Turns 26–50 | Turns 51–100 |
|---|---|---|
| Per empire | 3.2 / 3.3 | 9.6 / 7.4 |
| In fleets | 90 / 83 % | 77 / 74 % |
| First order a Seek | 36 / 34 % | 37 / 46 % |
| First order a Move To | 44 / 26 % | 15 / 19 % |
| First order a Warp | 1 / 24 % | 0 / 11 % |
| No orders | 16 / 8 % | 37 / 12 % |
| Fleets with no orders, turns in Defend (Short Term) with enemies listed | 14 / 2 % (all turns) | |

The original's fleets that explore carry a Move To toward the frontier point and the Warp,
kept turn after turn until they arrive (27 % of its attack ships in turns 26–50 carried the
same Move To as the turn before); ours carry a Seek and the Warp, and the Warp is left first
after each movement phase. In Defend (Short Term) with enemies listed, the original's attack
fleets and the defence fleets left over stay idle (spec 05 §7.5 `AI_Fleets` *Orders*).
Enemy colonies targeted per empire-turn of turns 51–100: game 10 0.47, with 1.3 attack ships
each (median 1), colonies of a median 90M people; ours 0.46, with 2.5 (median 2), 58M.

**Game 10's battles** (turns 51–100, per empire and 25 turns): 17.2 battles (decided:
armed against armed 97 %, armed against a colony 34 %), decided battles won away from
colonies 4.8 and drawn 10.1 (the four earlier games 2.1–3.4 and 0.0–21.5), battles ending
with the enemy colony gone 1.9, colonies lost 1.7, ships lost 9.0 (attack ships in battle
8.0); over the soft cap in 20 % of turns 51–100 and 2 % of turns 26–50; bases at turn 100
0.75 per empire.

**Rule by rule in our engine** (120 seeds each, turns 51–100 per empire and 25 turns,
event counts as in "Resources, ships and colony losses under a debugger"; each scratch rule
alone, then all five):

| | Battles | Decided won away from colonies | Drawn away | Ships lost in battle | Battles ending with the colony gone | Colonies lost | Defend (Short Term), turns 51–100 | Colonies at turn 100 | Ships at turn 50 | Second attack ship (turn) |
|---|---|---|---|---|---|---|---|---|---|---|
| Engine at aee3b7f | 11.1 | 5.0 | 6.1 | 7.8 | 1.0 | 1.2 | 72 % | 16.4 | 7.5 | 5 |
| Vehicle table counts before the clean-up | 11.1 | 5.1 | 5.9 | 7.9 | 1.0 | 1.3 | 73 % | 16.3 | 8.0 | 9 |
| Idle vehicles mark their sector | 13.2 | 5.0 | 9.2 | 7.8 | 1.0 | 1.2 | 72 % | 16.4 | 7.5 | 5 |
| Exploring fleets on a Move To | 10.8 | 5.1 | 5.8 | 8.0 | 1.0 | 1.2 | 72 % | 16.4 | 7.6 | 5 |
| Defend (Short Term) fleet rule | 8.2 | 4.1 | 3.2 | 5.9 | 1.6 | 1.8 | 67 % | 15.3 | 7.8 | 5 |
| All five | 9.7 | 3.9 | 4.9 | 5.9 | 1.6 | 1.8 | 67 % | 15.6 | 8.0 | 9 |
| Original | 6.1–19.1 (three games) | 2.1–4.8 (five) | 0.0–21.5 (five) | 5.1–9.0 (five) | 1.9–2.2 (three) | 1.7–3.6 (five) | 70 % | 17.0 | 8.6 | 9 |

Standard errors over the 120 seeds are about 0.15 for the decided battles and 0.07 for the
colonies lost. With the Defend (Short Term) fleet rule the colonies targeted rose to 0.57 per
empire-turn with 1.9 attack ships each (median 1). Colonies founded or taken / lost per empire
and 25 turns of turns 51–100: the original (five games) 4.5 / 2.3, ours 3.8 / 1.2, with the
fleet rule 4.1 / 1.8 (question 76).

**The first 25 turns** (question 75). Explored systems per empire at turns 10 / 25: the
original (nine games) 4.0 / 6.6, ours 5.5 / 7.6, with the counts before the clean-up 4.8 /
7.5; Exploration 83 % of turns 1–25 in the original, 77 % in ours and 78 % with the rule;
Infrastructure 3 against 6 %.

**What this settles** (spec 05 questions 68–73): the second attack ship (counts taken before
the clean-up); idle ships in the battle check; the exploring fleets' Move To; the Defend
(Short Term) fleet rule, the largest lever found for decided battles, losses and colonies
taken; the scrap and fleet-leader details; the net income's timing. Open: what a Scrap does
to a fleet (question 74), the first 25 turns (75) and the colonies held (76).

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

Session 4 (2026-10-02), a debugger on the running game, under the same rules as session 3:
- Start the game under Wine's debugger from the first instruction (`winedbg --gdb
  --no-start` on the copy's `Se4.exe`) and attach gdb to its port. Attaching to a game that
  is already running works, but under the WoW64 build of Wine 11.18 the game crashes when
  the debugger resumes or detaches. Tell gdb to pass every signal without stopping, and
  resume the first stop with `signal 0`.
- Breakpoints only read: each one runs a gdb Python command that reads memory, writes a
  line to a log under `reference/re/notes/` and continues. Nothing is written to the game's
  memory and the executable is unchanged.
- A game in which every empire is a computer player: a new game with Different Machines
  and simultaneous turns (one human empire is required at Begin Game), a master password,
  then loaded with that password as Game Master, the human empire marked computer-
  controlled in the Players window, and saved. Each turn the host loads the game, End Turn
  is pressed, the host processes the turn and exits; a script relaunches it for the next
  turn (about 20 s a turn under the debugger).
- Per-colony production and happiness terms come from breakpoints inside the production
  and happiness routines. The production ones are enabled only while the income step
  gathers the empire's totals, and only on every fifth turn, which keeps a turn near 20 s;
  the happiness routine keeps a running total per term, so each colony's terms are the
  differences between consecutive readings.

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

  This matches the SystemTypes header (0..12). The manual's "196 sectors" is wrong for
  this version.

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

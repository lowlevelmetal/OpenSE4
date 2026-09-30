# Spec 06: User interface, presentation and installed assets

Status: reference spec for client work. It was written clean-room from the SE4 Deluxe
manual (text and its screenshots, viewed only), the self-documenting data files, the
install's Readme and version history, and a survey of the install's asset folders
(listing, image headers and dimensions, visual inspection of sprite sheets). No
executable was opened. Everything is paraphrased. Facts measured from files are marked
**[M]**, facts from the manual text **[T]**, facts read off manual screenshots **[S]**,
and guesses **(inferred)**. Section 7 lists what to check in the running game.

Data-file grammar (`Key := Value` records between `*BEGIN*`/`*END*`) is specified in
spec 01 §1 and is not repeated here.

Conventions that hold across the whole UI:

- **Fixed-pixel layouts at two resolutions.** The game ships screen art for 800x600 and
  1024x768 only [M]. Windows are fixed-size bitmapped frames, not resizable.
- **Left-click acts, right-click reports.** In nearly every list, a left-click performs
  the primary action (select, add to a queue, remove from a queue, jump to the object in
  the main window), and a right-click opens a read-only report popup about the item
  (ship, planet, race, component, tech area) [T]. Our client should keep this.
- **Shift+click multi-selects** in the ship list and in the construction-queue list [T].
- **Column headers sort** the big list windows (Colonies, Planets, Ships) [T].
- **Tooltips on order buttons** show the order's hotkey [T].
- **Radio "lights".** Tab and filter buttons show a small green lamp when active; on/off
  options are rows with a lamp that is lit when set [S]. Lists scroll with up/down arrow
  buttons in a narrow column, not standard scrollbars [S].
- **Report popups close by clicking anywhere on them** (component, facility, ship-size
  and formation reports) or via a Close button (ship, planet, race reports) [T].

---

## 1. Screen inventory

Dialog size classes seen in the manual's 800x600 captures [S]: full screen (800x600);
large dialog (780x475, used by most management windows); tall dialog (780x668:
Colonies, Construction Queues); object report (310x420, with tab buttons along the
bottom); item report (about 298 wide, height fits content); picker (340x370 or
420x520); File Menu (a narrow 173x320 column of buttons); prompts (about 309x140).

The large dialog has one layout everywhere: a title strip across the top, the content
area on the left (about 570 px wide), and on the right a column of up to 13 stacked
buttons (about 180x28 each). Tab/filter buttons sit at the top of the column; actions
sit below; **Close is always the bottom slot** [S].

### 1.1 Start-up, setup and meta screens

| Screen | Purpose | Regions / controls | Reached from |
|---|---|---|---|
| Intro | Title screen. | Background art, version label, "Loading" progress while data files load (buttons disabled until done); buttons Quick Start, New Game, Resume Game (loads the last save), Load Game, Tutorial, Scenario, Credits, Quit Game in two rows along the bottom [T][S]. | Program start. |
| Quick Start | One-click game. | Grid of race portraits (the list of styles comes from `Quick Start Style N` in Settings.txt [M]); Begin Game, Cancel. | Intro. |
| Game Setup (8 pages) | Configure a new game. Pages: Quadrant, Events, Technology, Player Settings, Players, Victory Conditions, Game Settings, Mechanics. | Full-screen; page buttons plus Begin Game / Cancel on every page. Quadrant page has a map preview with Generate Map Now and Load Map; Players page has Add New, Add Existing, Remove, Save To File [T]. | Intro → New Game; File Menu → New. |
| Empire Setup (6 pages) | Create an empire. Pages: General, Environment, Culture, Characteristics, Advanced Traits, Description. | Full-screen; page buttons plus Create Empire / Cancel. General page has a race-style picture chooser with arrows, design-name-file picker, minister style [T]. | Game Setup → Players → Add New. |
| Culture Modifiers | Compare cultures. | List; Close. | Empire Setup → Culture. |
| Load Game | Pick a save. | Two-column list (name without extension, last-modified time), scroll arrows, Cancel. Clicking a row loads it [T][S]. | Intro; File Menu → Load. |
| Load Map | Pick a saved map. | (not documented) | Game Setup → Quadrant. |
| Scenario chooser | Pick a scenario. | (not documented; scenarios listed from `Scenarios/*_Settings.txt`) (inferred) | Intro → Scenario. |
| Credits | Rolling credits. | — | Intro. |
| Login / password prompt | Enter an empire's (or the game master's) password before playing a turn. | — | Loading a multiplayer or password-protected save [T]. |

### 1.2 Main window and the windows opened from its command buttons

| Screen | Purpose | Regions / controls | Reached from |
|---|---|---|---|
| Main Window | Everything during a turn. | See §2. | Game start / load. |
| Game Menu ("File Menu") | File operations. | Vertical buttons: New, Load, Save Game, Save Map, Save Empire, Players, Options, Delete Game, Quit, Close [T][S]. | F2 / first command button. |
| Save Game, Delete Game | Name / remove a save. | (not documented) | Game Menu. |
| Player Computer Control | Toggle AI control per player. | Player list with lamps; OK / Cancel. Also reused by the Combat Simulator [T]. | Game Menu → Players. |
| Designs | Browse and manage designs. | Design list; detail pane (stats, then either components or, with Stats\Strategy on, build/loss/kill counts and default strategy). Tabs: Ship Designs, Unit Designs, Enemy Ship Dsgn, Enemy Unit Dsgn. Actions: Create, Copy, Edit, Upgrade, Make Obsolete, Hide Obsolete, Stats\Strategy, Simulator [T]. | F3. |
| Create Design | Ship designer. | Size, type and name pickers; running totals; components-on-design and components-available lists; warnings box; hover detail. Comp Type (group filter), Weap Mount, Condensed View, Only Latest, Weapons Report, Create Design, Cancel [T]. | Designs → Create / Copy / Edit. |
| Weapon Mounts | Pick a mount for weapons added next. | List; Cancel. | Create Design; Weapons Report. |
| Combat Simulator | Set up a test battle. | Vehicles-in-combat list, design list, owner picker; Tactical / Strategic choice; No Obsolete, Strategies, Computer Control, Fleets for Plr, Change Cargo, Begin, Cancel [T]. | Designs → Simulator. |
| Planets | All planets seen. | Statistics block; galaxy mini-map highlighting the hovered row; planet list. Filter tabs: All, Colonizable, All Colonies, Enemy Colonies, Ally Colonies, Coloniz\Empty, Coloniz\Breathe, Ship Enroute, Asteroids, Special; toggle No Sys To Avoid; Send Colony Ship [T]. | F4. |
| Colonies | Own colonies. | Statistics; mini-map; sortable list whose columns depend on the tab: General, Value, Production, Facilities, Cargo, Construction, Status (status icons), Races, Orders. Actions: Scrap Facil Types, Set Colony Type [T]. | F5. |
| Ships\Units | Own ships, units, fleets. | Statistics; mini-map; list. Column tabs: General, Orders, Cargo, Fleet, Maintenance (each starts with a picture column); toggles Show Ships / Show Units / Show Fleets [T]. | F6. |
| Construction Queues | All queues. | Statistics; quadrant map; queue list. Column tabs Rate, Usage, Planet Value, Facilities, Cargo; inclusion toggles Ships, Planets, Ship SY, Planet SY; Multi-Add (acts on the shift-selected queues), Scrap Facilities, Upgrade Facilities [T]. | F7; Log → Constr. Queues. |
| Set Construction Queue | Edit one queue. | Buildable-items list (tabs Ships, Facilities, Units, Upgrades; Only Latest); queue owner header; the queue itself; hover detail. Toggles Emergency Build, Repeat Build, Queue On Hold; Set Move To / Clear Move To; Fill Queue; Clear Queue; Reorder Queue [T]. | Order "Build Queue" (Q); row in Construction Queues. |
| Select Queue Type | Fill a queue from a template. | Template list; Add Type, Delete Type, Cancel. | Set Construction Queue → Fill Queue. |
| Research | Choose research. | Points available; tech-area list (left-click adds); current projects in pages of four (Projects 1-4, 5-8, 9-12); Repeat Projects, Divide Pts Evenly, Tech Tree (only if the game allows it), Reorder Projects [T]. | F8. |
| Tech Tree | Whole tree. | Two list views: Tech Areas (prerequisites), Tech Levels (what each level unlocks); Export (writes the view to a text file) [T]. | Research → Tech Tree. |
| Empires | Diplomacy hub. | One portrait per known empire with a stat column under each; tabs Treaty, Trade, Tariff. Buttons History, Treaty Grid, Intelligence, Borders, Victory Conditions, Scores, Comparisons, Our Race [T]. Left-click a portrait opens Communicate; right-click opens Race Report. | F9. |
| Log | Turn news. | See §4.1. | F10; auto-opens at turn start. |
| Empire Status | Budget and empire settings. | Balance sheet (income lines, expense lines, net per turn, treasury and storage cap; see spec 02). Buttons Empire Options, Ministers, Systems To Avoid, Waypoints, Strategies, Repair Priorities, Change Email, Change Password [T]. | F11. |
| Options (Empire Options) | Per-empire UI and behaviour switches. | Scrolling list grouped under headings: General Options (show the log at turn start; confirmation prompts before ending the turn, scrapping, stellar manipulation, deleting research or intel projects; show the colony-type picker when colonizing), Next/Previous (skip ships under construction, skip damaged ships, stop once per location), Ship Movement (avoid minefields, avoid restricted systems), Ship Orders (clear orders on entering a system with enemies / with any other empire), System Display (warp point names, planet names, facility letter markers, movement lines...) [S]. | Empire Status → Empire Options; probably also Game Menu → Options (see Q8). |
| Ministers | Automation switches. | Minister list with lamps; Select All/None; individual ministers on/off; Complete AI On/Off [T]. | Empire Status. |
| Systems To Avoid | Mark systems for pathing. | Map (click to toggle); tabs Avoid, Presence, Ally Claimed, Enemy Claimed [T]. | Empire Status. |
| Waypoints | Manage waypoints. | Waypoint list; quadrant map; ships heading there; yards auto-sending there; Set (returns to main window to pick a spot), Delete, Rename [T]. | Empire Status. |
| Strategies | Combat AI presets. | Strategy list and property pages (Movement, Firing, Launching, Formation); Add [T]. | Empire Status; Combat Simulator. |
| Repair Priorities | Repair order by component group. | Available groups; ordered priority list; Remove All [T]. | Empire Status. |
| Help | In-game encyclopedia. | Item list; detail pane (128 px picture, cost with resource icons, stats, abilities) [S]. Tabs: Components, Facilities, Ship Sizes, Unit Sizes, Tech Areas, Treaties, Intel Projects, Formations, Hotkeys; Weapons Report [T]. Lists only what the empire knows. | F1. |
| Weapons Report | Weapon damage-by-range grid. | Filters All, Direct Fire, Seeking, Point-Defense, Warhead; Weapon Mount; Only Latest; range pages Dmg 1-10 / 11-20 [T]. | Help; Create Design. |
| Galaxy Map | Enlarged strategic map. | See §2.6. | Right-click the galaxy panel. |

### 1.3 Order dialogs and pickers (opened by order buttons in the command panel)

| Screen | Purpose | Reached from |
|---|---|---|
| Select Waypoint | Choose a waypoint as a destination. | Move To Waypoint; Set Construction Queue → Set Move To. |
| Fleet Transfer | Create fleets, move ships in and out (left-click moves an item between the two lists); Create Fleet, Formation, Strategy, Existing Fleets, Add All, Remove All. | Order F; Combat Simulator. |
| Cargo Transfer | Two cargo lists (from / to) with transfer step Move One / Five / Ten / All. Not available in simultaneous games. | Order T; Combat Simulator. |
| Launch \ Recover Units | Same two-list pattern for units in space. Not available in simultaneous games. | Order U; tactical combat orders. |
| Cargo-type / unit-type picker, then location pick | Deferred Load, Drop, Launch-Remote, Recover-Remote orders. | Orders L, D, I, O. |
| Stellar Manipulation | Location view plus one button per effect (create/destroy planet, star, storm, nebula, black hole; open/close warp point; Construct for ringworld/sphereworld). | Order B. |
| Scrap | Vehicles at the location with scrap value, research potential, status, unmothball cost, self-destruct and fire-on flags; Scrap, Analyze, Retrofit, Mothball, Unmothball, Self-destruct, Fire On. | Order G. |
| View Orders | Pending orders of the selected object. | Order V. |
| Select Facilities | Pick facilities to scrap on a planet. | Scrap Facilities order. |
| Select Component | Pick a usable component. | Use Component order. |
| Select Facility | Pick a usable facility. | Use Facility order. |
| Convert Resources | Choose source/target resource and amount. | Convert Resources order (Ctrl+V). |
| Change Name | Text prompt. | Order N. |
| Formation \ Strategy picker | For a selected fleet. | Order H. |
| Abandon Planet confirmation | Choose whether to scrap facilities. | Ctrl+A. |
| Colony-type picker | Shown when a colonize order completes, if the empire option is on. | Colonize. |
| Attack Sector prompt | Yes/No when a move would enter a sector holding enemies [S]. | Move orders. |
| Reorder | Generic list ordering: Move Up, Move Down, Move To Top, Move To Bottom, OK, Cancel. | Research, Intelligence, queues. |

### 1.4 Report popups

All are about 310x420 with tab buttons along the bottom [S]; in the main window the same
reports render inside the right-hand panel (§2.5).

| Report | Tabs / content |
|---|---|
| Ship Report | Detail (owner flag, 128 px portrait with status icons under it, name, class, size, movement, damage, supplies, crew experience, fleet, maintenance), Comps, Cargo, Ability. Less detail for foreign ships. |
| Planet Report | Detail (owner flag, picture, status icons, physical data, value, description, then colony data: type, population, mood, output, construction), Facil, Cargo, Ability. An "Empty" variant without tabs for planets that are not ours. |
| Race Report | Detail, Descr (free-text blocks), Race (characteristics, traits), Tech (known tech). |
| Fleet, Storm reports | Fleet: movement, supply pool, experience, formation, strategy, ships. Storm: picture, size, description, abilities. |
| Component / Facility / Ship Size / Formation reports | Cost with resource icons, size, damage resistance, vehicle types, weapon data including damage by range, abilities; formation diagram. |

### 1.5 Diplomacy and empire comparison

| Screen | Purpose | Reached from |
|---|---|---|
| Communicate | Compose a political message: target race portrait, Message Type, Message Tone, free text; Report, View Trade/Tribute/Gift (enabled when last turn's offer exists), Edit Package, Edit Trade, Start Again, Send Message, Cancel. | Empires → portrait; Log → Send Reply. |
| Select Package | Build a give/take package from tabs Systems, Planets, Resources, Technology, Ships, Units, Star Charts, Treaty, Comm Channels; Clear Package. | Communicate. |
| History | Per-empire timeline with galaxy map highlight. | Empires. |
| Treaty Grid | Empire × empire treaty abbreviations with legend; paged 10 empires at a time. | Empires. |
| Intelligence | Intel points, project list, current projects in pages of four, Repeat, Divide Evenly, Reorder. | Empires. |
| Borders | Map of claimed systems with empire filter (Select All, Allies, Enemies, Us). | Empires. |
| Victory Conditions | Grid of conditions × empires, paged by 10. | Empires. |
| Scores | Flags with score, resources, research, intel, tech levels, systems, planets, population, units, ships, bases, rank. | Empires. |
| Comparisons | Line graph over time of one chosen metric (same metrics as Scores) for selected empires. | Empires. |

### 1.6 Combat screens

| Screen | Purpose | Reached from |
|---|---|---|
| Combat resolution prompt | "Tactical" or "Strategic" choice (tactical hidden when the game disables it) [S][T]. | Combat starting with a human player present. |
| Tactical Combat | Full screen. Status bar (location, combat turn, participants, next/prev selectors for ships that can move or fire); tactical map (bottom left; left-click selects own ship, moves to empty space, fires at enemy; right-click reports); current-piece panel (portrait, flag, group badge: blue = leader, red = member; shield and damage bars; weapon grid with reload pips, click to toggle a weapon); target-piece panel (1024x768 and up only; shows blue/red shield/internal damage numbers); overview map with a dotted view box. Buttons Options, Orders, Auto, End Turn. Pointer: arrow, 8 directional move arrows, crosshairs over enemies [T]. | Prompt; Combat Simulator. |
| Tactical Combat Orders | Launch Units, Launch Fighters in Groups, Drop Troops, Ram Ship, Capture Ship, Resolve Combat, group leader/member/clear. | Tactical → Orders. |
| Tactical Combat Options / Combat Replay Options | Animation, speed and display options (not itemised in the manual). | Tactical → Options; Replay → Options (adds Stop Replay). |
| Combat Piece Report | Movement, shields, damage, supply, max targets, combat group, formation. | Right-click a piece. |
| Strategic Combat | Watch-only: system, coordinates, combat turn; forces list (flag, then per vehicle size current and lost counts); small map of coloured squares; Begin, Close. | Prompt; simulator. |
| Ground Combat | Planet details, facilities, defender and attacker lists; Begin, Close. | After troops land. |
| Combat Replay | Same layout as tactical, playback only; Options, Next. | Log → Combat Replay. |

### 1.7 Multiplayer, tutorial and end of game

| Screen | Purpose |
|---|---|
| TCP/IP Host | Player list, own IP addresses, a status line naming the current step of the host cycle (spec 05), hideable chat; Begin Game, Process Turn, Add/Remove Empire, Play Turn, Chat, Minimize Game, Quit Game [T]. |
| TCP/IP Player | Source and host IP, player name, status line, chat; Connect to Host, Create Empire, Play Turn, Chat, Minimize, Quit [T]. |
| Movement log replay | Not a window: in simultaneous games the main window replays the host's 30-day movement (full, stepped by day, per ship, rewind) [T]. |
| Tutorial / Scenario text window | Titled text with a 128x128 picture and previous/next through a series; re-opened with Ctrl+H or the "T" button in the status bar [T][M]. |
| Finale | Full picture for victory, defeat, or "human players all dead", chosen from lists in Settings.txt [M]. |

---

## 2. Main window layout

### 2.1 Geometry

Five regions [T]; positions below are read off the manual's 800x600 capture [S] and the
frame bitmaps [M].

```
800x600                                      x=0 ............................. 800
 y=0   +--------------------------------------------------------------------------+
       | STATUS BAR (full width, ~32 px)                                          |
 y~32  +--------------------------------------------+-----------------------------+
       | COMMAND PANEL (487x74)                     | SHIP LIST / REPORT          |
 y~106 +--------------------------------------------+ panel (292x363)             |
       |                                            |                             |
       | SYSTEM PANEL (494x491; 490x490 background) |                             |
       |                                            +-----------------------------+
       |                                            | GALAXY PANEL (304x202)      |
 y=600 +--------------------------------------------+-----------------------------+
```

Note that the manual's overview text swaps the descriptions of the ship-list and galaxy
panels; the screenshot and the frame bitmaps show the report/list panel at the top right
and the galaxy map at the bottom right.

Frame pieces live in `Pictures/Game/Screens/800X600/` and `.../1024X768/` [M]:

| Piece | 800x600 | 1024x768 | Role (inferred) |
|---|---|---|---|
| Top, Toptitle | 800x5 | 1024x5 | top edge of the window / status bar |
| Topsys | 499x9 | 1024x11 | top edge of the system panel (full width at 1024) |
| Topgal | 306x9 | 362x9 | top edge of the right column |
| Left | 11x600 | 12x768 | left border |
| Middle | 13x571 | 16x662 | divider between system panel and right column |
| Right | 12x600 | 67x768 | right border; at 1024 a 67 px strip |
| RightFiller | 1x1 | 62x365 | filler inside that strip (1024 only) |
| Bottom | 800x6 | 1024x8 | bottom border |
| Intro | 800x600 | 1024x768 | intro screen background |
| Starmap | 490x490 (4-bit) | 660x660 (4-bit) | legacy system background |

At 1024x768 the system panel background is 660x660 and a 67 px right-edge strip
appears; what fills it (a second column of order buttons is the likeliest candidate) is
Q1.

### 2.2 Status bar

Left to right: large empire flag, empire name, emperor title and name, "Game Date" (the
game starts at 2400.0 and each turn adds 0.1), then stored minerals, organics and
radioactives, each followed by its resource icon (blue crystal, green organic, red
radiation symbol). At the far right a minimize button, and during the tutorial a "T"
button that re-opens the tutorial text [T][S].

### 2.3 Command panel

Three groups, left to right [T][S]:

1. **Command buttons**: 12 buttons of 34x34 in two rows of six. Top row: Game Menu,
   Designs, Planets, Colonies, Ships\Units, Construction Queues. Bottom row: Research,
   Empires, Log, Empire Status, Help, End Turn. They match F2..F11, F1 and F12 (§3).
   Pressing End Turn disables the main window's panels until every computer player has
   moved.
2. **Order buttons**: a paged strip showing 2 rows × 5 order buttons of 34x34 between
   tall left/right paging arrows. Buttons light up only when the current selection can
   execute them. The full order set and hotkeys are in §3.1.
3. **Selection buttons**: three stacked "previous | icon | next" controls (48x24) cycling
   through ships, fleets and colonies. In turn-based games the ship cycle visits ships with
   movement left; in simultaneous games, ships without orders. Empire options can make
   the cycle skip ships under construction or damaged ships, or stop once per location
   [T][S].

### 2.4 System panel

A 13x13 sector grid (coordinates 0..12; the star normally sits in the centre square and
"rings" 2-7 lie 1-6 squares out) [M: SystemTypes.txt] drawn over a per-system-type
background image. The system name is drawn at the top left. Contents [T][S]:

- Planets, asteroid fields, the star(s), storms and warp points as 36x36 sprites. A warp
  point that the empire has travelled through shows the destination system's name under
  it.
- A colonised planet has small population bars at its top right, coloured by owner.
- Ships: a single-owner stack shows one ship sprite with a count in the bottom-right
  corner; a location with several empires, or ships orbiting a planet, shows small empire
  flags instead.
- Colonisation hint: a small green star on a planet means colonisable and breathable; red
  means colonisable but would be domed; no star means not colonisable by this empire.
- The selected location is framed by four small yellow corner arrows.
- Waypoints appear as a numbered marker.
- Optional movement lines (Ctrl+L): a line from a moving ship to its destination with a
  small circle per movement point and the number of turns until each point is reached.
- Nebula and black-hole systems use a full background picture instead of a star field.

Clicks (left and right behave the same): one object → its report in the right panel;
several objects → a list; empty space → a report about the whole system.

### 2.5 Ship list / report panel

Shows either a list of everything at the selected location, or a single report (ship,
planet, storm, fleet, system). Each list row has the object's 36x36 picture, its name,
for ships the class (design) name, and for own objects a row of status icons on the
right. Clicking a row replaces the list with that object's report and enables the order
buttons; an up-arrow button at the top right of the report returns to the list. Shift+click
tags several ships (a green arrow is drawn on each) so one order goes to all of them;
the group dissolves after the order; Shift+A tags all, Shift+C clears [T].

### 2.6 Galaxy panel and Galaxy Map window

The panel shows the whole quadrant over a faint grid (one square is about 10 light
years). Systems are small circles, warp connections are lines [T]:

| Symbol | Meaning |
|---|---|
| dark grey circle | never explored |
| white circle | explored, no own presence now |
| circle in own colour | only this empire present |
| circle in another colour | another empire present |
| triangle | several empires present |
| double circle with filled centre | the system shown in the system panel |
| short stub from a circle | a warp point seen but not yet traversed (destination unknown) |

Hovering shows the name of the nearest explored system; left-click selects a system;
right-click opens the **Galaxy Map window** (780x475): a larger map with overlay
buttons Presence, Avoid (avoided systems in yellow), Ally Claimed, Enemy Claimed,
Spaceports and Resupply Depots (green = colonised with the facility, yellow = colonised
without), Goto System (list of seen systems; picking one closes the map and shows it),
Show Distances (light-year distances from the hovered system), Show Names, Close.
Clicking a system there edits its free-text player notes; the hovered system's notes show
at the bottom [T][S].

### 2.7 Turn flow as the player sees it

1. Turn starts; the Log window opens automatically if there are new entries and the
   option is on.
2. The player selects objects and issues orders. In turn-based games movement happens
   immediately; entering a sector with enemies asks the Attack Sector question, and
   combat asks Tactical or Strategic.
3. End Turn (F12, with an optional confirmation) locks the panels while the AIs play.
4. In simultaneous games, orders are only recorded; after the host processes the turn
   the player can replay the movement log in the system panel (Ctrl+P/O/I/U).

---

## 3. Hotkeys

Hotkeys are listed in the Help window's Hotkeys tab and in the Readme [T][M].

### 3.1 Main window: orders

| Key | Order | Notes |
|---|---|---|
| M | Move To | then pick a destination in any system |
| (button only) | Move To Waypoint | opens Select Waypoint |
| Ctrl+0..9 | Move To Waypoint # | |
| Alt+0..9 | Set Waypoint # | at the selected location |
| W | Warp | only needed for unexplored destinations |
| A | Attack | same-sector attack; in simultaneous games, pursue a target |
| C | Colonize | queues load-population, move, colonize |
| S | Resupply at nearest | |
| R | Repair at nearest | |
| Backspace / Del | Clear Orders | |
| F | Fleet Transfer | |
| Q | Set Construction Queue | |
| T | Cargo Transfer | not in simultaneous games |
| U | Launch\Recover Units | not in simultaneous games |
| L / D | Load Cargo / Drop Cargo (deferred) | pick type, then location |
| I / O | Launch / Recover Units Remotely | pick type, then location |
| Y | Sentry | |
| E | Explore | |
| P | Set Patrol | also turns on Repeat Orders |
| K | Repeat Orders (toggle) | |
| B | Stellar Manipulation | |
| V | View Orders | |
| G | Scrap / Analyze / Mothball | |
| H | Change Formation \ Strategy | fleets |
| N | Change Name | |
| J | Jettison Cargo | |
| Z / X | Cloak / Decloak | |
| Ctrl+M | Sweep Mines | |
| Ctrl+T / Ctrl+R | Add / Remove Tagged Minefield | |
| Ctrl+A | Abandon Planet | |
| Ctrl+V | Convert Resources | |
| (button only) | Use Component, Use Facility, Scrap Facilities, Toggle Minister Control | |

### 3.2 Main window: windows, selection, display

| Key | Action |
|---|---|
| F1 | Help |
| F2 | Game Menu |
| F3 | Designs |
| F4 | Planets |
| F5 | Colonies |
| F6 | Ships\Units |
| F7 | Construction Queues |
| F8 | Research |
| F9 | Empires |
| F10 | Log |
| F11 | Empire Status |
| F12 | End Turn |
| Space or Ctrl+N / Ctrl+B | Next / previous ship |
| Ctrl+F / Ctrl+D | Next / previous fleet |
| Ctrl+C / Ctrl+X | Next / previous colony |
| Ctrl+P | Movement log replay, full |
| Ctrl+O | Movement log replay, reload (rewind) |
| Ctrl+I | Movement log replay, single step (one day) |
| Ctrl+U | Movement log replay, follow ship |
| Ctrl+L | Toggle ship movement lines |
| Ctrl+S | Toggle sound |
| Ctrl+H | Show tutorial / scenario window |
| Shift+A / Shift+C | Tag all / clear tagged ships in the ship list |
| Shift+click | Tag one ship in the ship list |

### 3.3 Tactical combat window

| Key | Action |
|---|---|
| Alt+0..9 | Make selected ship leader of group # |
| Ctrl+0..9 | Make selected ship member of group # |
| L | Launch units |
| T | Drop troops |
| R | Ram ship |
| C | Capture ship |
| E | End combat turn (manual only; absent from the Readme) |
| Space or Ctrl+N / Ctrl+B | Next / previous ship with movement |
| Ctrl+F / Ctrl+D | Next / previous ship that can fire |
| Shift+A / Shift+C | Select all / clear weapons |

Discrepancies: the Readme calls the Ctrl+P/O/I/U family "Phase Replay". Our current
client binds Enter (end turn), G (galaxy), Tab (next idle ship) and F11; for SE4 parity G
must become Scrap, F11 Empire Status, and End Turn F12 (Enter can stay as an extra).

---

## 4. Messages, log and status icons

### 4.1 Log window

Layout (780x475) [S][T]: top left, the **Log Messages** list (one row per entry with a
small coloured bullet, see Q11); bottom left, a galaxy mini-map that highlights the
entry's system; centre, **Log Details** with the game date, the entry's 128x128 picture,
its title, its date, and a body. Combat entries show a **Combat Forces** table: for each
empire its flag and name, then one row per vehicle or planet with Start and Lost counts.
Right column: category filters **All, Construction, Research, Intelligence, Events,
Politics, Combat, Misc** (categories with no entries appear greyed out (inferred)), then
**Send Reply** (enabled for messages from another empire; opens Communicate), **Combat
Replay** (labelled "Combat Report" in the capture; enabled for combat entries), **Constr.
Queues**, **Goto** (closes the log, shows the location in the main window) and **Close**.

The log holds entries since the player's last turn (including those generated during
it). The version history says the window remembers the selected entry when re-opened.

Where entries come from:

- **Random and stellar events**: Events.txt records carry title and body templates with
  tokens such as `[%VehicleName]` and `[%SystemName]`, plus a `Picture` name that
  resolves to `Pictures/Events/<Picture>.bmp` [M].
- **Intelligence**: IntelProjects.txt gives a picture for the acting side and one for
  the target side [M].
- **Hard-coded events** use the remaining `Pictures/Events/` names, which by their
  names cover new colonies (one per planet composition × atmosphere, e.g.
  `ColonyRockOxygen`), combat results, gifts, new tech, unit/ship caps, failed orders,
  ruins, plague, unrest, planet changes, warp points and stellar objects created or
  destroyed, and eliminated players [M] (mapping inferred).
- **Political messages** from other empires (§4.2).

Related Settings.txt switches [M]: `Use Old Log Political Message Display`,
`Create Log Text File for Game`, `Create Log Text Files for Players`,
`Create Combat Replay`.

### 4.2 Political message types

Composed in Communicate; each message has a type, a tone and free text, and some types
carry a package [T]. Types:

- **General message**: text only.
- **Treaties**: propose, accept (takes effect at once), refuse, counter-propose (acts as
  a new proposal), break (immediate, no reply needed), declare war (immediate, overrides
  any treaty).
- **Trade**: propose (package of gives and takes; takes may be "Any" placeholders for
  the other side to fill), accept (not allowed while an "Any" remains; counter instead),
  refuse, counter-propose.
- **Gift** and **Tribute**: offer, accept (items transfer on acceptance), refuse. A
  tribute is a gift offered to a stronger power.
- **Surrender**: hands the whole empire to the recipient; confirmed by a prompt.
- **Grant independence**: pick a colony; the handover is not immediate.
- **Demand / Request / Warn** group (non-binding, 16 subtypes plus accept/refuse
  demand): ask for a gift, tribute or surrender; ask the recipient to pull ships or
  colonies out of a system or leave a planet; to stop hostilities against, break a
  treaty with, declare war on, make peace with, or fight a third empire (optionally in a
  given system or against a given planet); to stop espionage, sabotage, or attacks in a
  system. Each prompts for its parameter (system, planet or empire).

### 4.3 Other message surfaces

- **History window**: per-empire list of dated events with map highlight (in
  multiplayer these come from a per-player `_events.txt`) [T].
- **Comparisons window**: graphs from a per-player `_stats.txt` [T].
- **Confirmation prompts**, switchable in Empire Options (end turn, scrapping, stellar
  manipulation, deleting research/intel projects); always-on prompts for surrender,
  removing the first queue item, quitting as TCP/IP host or player [S][M].
- **TCP/IP chat** with a sound on receipt [M].

### 4.4 Status icons

20x20 icons drawn under report portraits, at the right of ship-list rows and in the
Colonies "Status" tab [T]. They are the bottom two rows (19 icons each, y = 136 and 156)
of `Pictures/Game/General.bmp`, row-major; the manual's own icon images are numbered so
that **cell = number − 1**, and all but one match the atlas pixel-for-pixel [M]:

| Cell | Meaning | Cell | Meaning |
|---|---|---|---|
| 0 | Space yard present | 16 | Being repaired |
| 2 | Repeat orders on | 17 | Domed colony |
| 3 | Sentry orders | 18 | Fighters in cargo |
| 4 | Low supplies (below `Supply Amount for Low Supply Warning`, 1000 in stock data) | 19 | Satellites in cargo |
| 5 | Out of supplies | 20 | Mines in cargo |
| 6 | Mothballed | 22 | Population in cargo |
| 9 | Cloaked | 25 | Troops in cargo |
| 10 | Under minister control | 26 | Weapon platforms in cargo |
| 11 | Can repair others | 30 | No spaceport in system (resources not delivered) |
| 12 | Building something | 33 | Damaged |
| 15 | Ruins on planet | 34? | Drones in cargo (manual image does not match cell 34 exactly; Q13) |

The other 16 cells are undocumented (Q13).

---

## 5. Asset catalog

### 5.1 General rules for loading the user's install

- **Root.** Everything is relative to the `se4` directory. `Path.txt` names an optional
  mod directory (`Using Mod Directory := None` by default). A mod is a sub-directory that
  mirrors the whole tree and must contain every data file, but **bitmaps missing from the
  mod fall back to the base tree** [M]. Mods in `Extras/` (zip archives) follow this
  mirror layout.
- **Case-insensitive lookup is mandatory.** File-name case is inconsistent between data
  references and disk (`blackhole2.bmp` vs `BlackHole2.bmp`, `Mini_` vs `mini_`,
  `BattleCruiser` vs `Battlecruiser`, `.BMP` vs `.bmp`) [M]. Build a lower-cased index of
  the tree once at start-up.
- **Images**: every picture is an uncompressed Windows BMP with a 40-byte info header,
  bottom-up rows, 24 bits per pixel; the only exceptions are the two legacy 4-bit
  `Starmap.bmp` frames [M]. There is no alpha channel; backgrounds are pure black, so
  treat RGB(0,0,0) as transparent for sprites (inferred; Q14). Explosions and beams
  probably look right with additive blending.
- **Placeholders**: unused cells in sprite sheets are black squares with a red or white
  one-pixel frame [M]. Treat them as "no picture".
- **Never copy** these files into the repository; resolve them at runtime from a
  user-configured install path.

### 5.2 Index conventions (the load-bearing part)

| Asset | Sheet | Cell | Portrait (128x128) | Index field |
|---|---|---|---|---|
| Components | `Pictures/Components/Components.bmp`, 864x432 = 24 × 12 cells of 36x36 | `Pic Num − 1` | `Comp_%03d.bmp` of `Pic Num` | Components.txt `Pic Num` (1-based; Bridge = 1 = cell 0) |
| Facilities | `Pictures/Facilities/Facility.bmp`, 720x180 = 20 × 5 | `Pic Num − 1` | `Facil_%03d.bmp` of `Pic Num` | Facility.txt `Pic Num` (1-based) |
| Planets and other sector objects | `Pictures/Planets/Planets.BMP`, 720x576 = 20 × 16 | `Picture Num` | `p%04d.BMP` of `Picture Num + 1` | SectType.txt `Picture Num` (**0-based**) |
| Status icons | `Pictures/Game/General.bmp` rows at y=136,156, 19 per row, 20x20 | icon number − 1 | — | built in |
| Beam graphics | `Pictures/Combat/Beams.bmp`, 200x40 = 10 × 2 of 20x20 (16 used) | probably `Weapon Display − 1` (values 1..16 in data) | — | Components.txt `Weapon Display` when `Weapon Display Type := Beam` |
| Torpedo graphics | `Pictures/Combat/Torps.bmp`, 200x80 = 10 × 4 of 20x20 (35 used) | probably `Weapon Display` (values 0..32) | — | same, `Torp` |
| Seeker graphics | race `_Main.bmp` slots 4-6 | `Weapon Display` 0..2 | — | same, `Seeker` |

All sheets are row-major, cells packed with no gutter, and may grow by whole rows or
columns (the manual says so for the 36 px sheets). Cells were verified by comparing sheet
cells with portrait files and with recognisable items (bridge, monolith, ringworld) [M].
Portrait files exist only for used indices plus a few spares; some sheet cells have no
portrait. The planet sheet also holds stars of each colour, gas clouds, asteroid fields,
black-hole swirls, and the ringworld and sphereworld (cells 300, 301).

### 5.3 Pictures/ folder by folder

- **Components/**, **Facilities/**, **Planets/**: see §5.2.
- **Events/**: 67 pictures, 128x128, named by event (`ShipDamaged`, `NewTechLevel`,
  `Colony<Rock|Ice|Gas><Oxygen|Methane|Hydrogen|CarbDiox|Argon|None>` ...). Used in
  the Log details pane and event pop-ups [M].
- **Combat/**: `Explosions.bmp` 288x504 = 14 rows × 8 frames of 36x36 (13 colour
  variants); `Shields.bmp` 288x288 = 8 rows × 8 frames of 36x36 (shield-hit rings in
  several colours); `BigExplosions.bmp` 576x144 = 2 rows × 8 frames of 72x72; beams and
  torps as above [M].
- **Races/<Style>/** (20 styles, about 81 files each) [M]:
  - `<Style>_Mini_<Bitmap>.bmp` 36x36: map sprite. Ships, carriers, transports and
    fighters are drawn top-down facing up and rotated by the engine to the heading;
    bases, satellites, mines, troops, platforms, drones and group icons are
    non-directional [T].
  - `<Style>_Portrait_<Bitmap>.bmp` 128x128: isometric portrait, bow towards the lower
    left [T].
  - `<Bitmap>` is `Primary Bitmap Name` from VehicleSize.txt, falling back to
    `Alternate Bitmap Name` (e.g. Baseship falls back to Starbase) [M]. There are 35
    names, covering every hull class plus Fleet, FighterGroup, MineGroup and
    SatelliteGroup group icons.
  - `<Style>_Main.bmp` 100x20, packed strip [T][M]: x 0-25 large flag (26x18); x 26-39
    small flag (14x10) above a solid **empire-colour swatch** (14x10); then three 20x20
    projectile sprites: seeker, capital-ship missile, plasma torpedo.
  - `<Style>_Race_Portrait.bmp` 128x128 (race portrait in Empires, reports, setup);
    `<Style>_Pop_Portrait.bmp` 36x36 and `<Style>_Pop_Mini.bmp` 20x20 (population in
    cargo lists and on planets).
  - `<Style>_BigExplosion.bmp` 576x72 (8 frames of 72) and `<Style>_Shields.bmp`
    288x36 (8 frames of 36): the race's own ship-death and shield-hit animations.
  - `<Style>_AI_*.txt`: race AI overrides (spec 05).
- **RaceGeneric/**: `Generic_*` fallback for any missing race picture: all 35 minis
  and portraits, Main, Shields, BigExplosion, Pop_Mini (no race portrait) [M].
- **RaceNeutral/Neutral001..010/**: neutral races have only Main, Race_Portrait,
  Pop_Portrait, Pop_Mini and AI files; their ships use RaceGeneric [T][M].
- **Systems/** [M]:
  - `<Res>/<Name>.bmp` (490x490 at 800X600, 660x660 at 1024X768): system-panel
    background named by SystemTypes.txt `Background Bitmap` (normal systems use
    `Starmap.bmp`; storms, nebulae, black holes, giants and so on have their own). The data
    file's comment says these live in `Game/Screens/<Res>`, but only `Starmap.bmp`
    exists there; the rest are in `Systems/<Res>`. Search both.
  - `<Name>.bmp` 128x128: one per background, probably the system report picture or
    the combat-map centre picture when `Non-Tiled Center Pic := TRUE` (Q5).
  - `<Storm1..5|Asteroids1>Tile<n>.bmp` 72x72 (9, 5, 5, 7, 10 and 9 tiles):
    probably tiled over the tactical map in storm or asteroid sectors (Q5).
- **Stellar/**: 13 horizontal film strips of 128x128 frames (20 frames; `Ring.bmp` 16)
  for create/destroy planet, star, storm, nebula, black hole, warp point open/close and
  ringworld construction; shown in the Stellar Manipulation window [M].
- **Game/** (UI chrome) [M]:
  - `Buttons/Main.bmp` 442x102: 13 command icons × 3 state rows, 34x34.
  - `Buttons/Orders.bmp` 816x408: 24 columns × 12 rows of 34x34, in three bands of four
    state rows (normal, hover with a mesh overlay, pressed/lit, disabled/dark); 59
    icons in use. The cell of each order is in spec 07 §UI.
  - `Buttons/TabBtns.bmp` 576x150: 8 labelled tabs (Detail, Comps, Cargo, Ability,
    Facil, Descr, Race, Tech) of 72x30 × 5 state rows: the report tab strips.
  - `Buttons/Nextprev.bmp` 192x96: 4 selectors (ship, fleet, planet, crosshair) of 48x24
    × 4 states.
  - `Buttons/Arrows.bmp` 96x96 (list scroll arrows, 24x24); `BigUpDownArrows.bmp` 128x68
    (64x17); `BigLeftRightArrows.bmp` 28x200 (left+right halves of 14x50 × 4 states, the
    order-strip pager); `SmallLeftRightArrows.bmp` 32x152 (16x38 halves × 4);
    `DetailUp.bmp` 33x84 (the report-to-list button, 4 states of 33x21); `Close.bmp`
    60x60 (minimize, close and "T" buttons of 20x20 × 3 states).
  - `Dialogs/MainParts.bmp` 200x126: pieces of the blue tech-style dialog frame;
    `Rowgrid.bmp` 750x600: grid texture behind panels and maps; `Selection.bmp` 36x36:
    selection frame; `StartMenu.bmp` 736x536: background for the start-up menu window
    added in 1.95 (Q9).
  - `General.bmp` 384x176: irregular atlas: 16 px resource/research/intel/population
    icons (y=0), 32 px versions (y≈32), coloured indicator lamps and small arrows,
    green/red colonisation stars, two 8-frame 36 px explosion strips (y≈64, y≈100), a
    dome overlay for domed colonies, and the status icons (§4.4). Needs a hand-written
    coordinate table.
  - `Finale/`: `Victory.bmp`, `Lose00N.bmp`, `HumnDead.bmp` (426x341 or 400x300),
    chosen through Settings.txt `Finale ... Picture N`.
  - Logos: `Se4logo.bmp`, `Mmlogo.bmp`, `SFI.bmp`; `Backgrd.bmp` 20x20 tile.
  - Cursors, 32x32 `.cur`: `Normal` (hotspot 2,2), `Select`, `Target`, `Hourglass`,
    and `ArrowN/NE/E/SE/S/SW/W/NW` (hotspot 15,15), the tactical-map move pointers.

### 5.4 Fonts

`Fonts/` holds six Windows 3.x bitmap font files (`.fon`, NE resource containers with
fixed pixel sizes) [M]: Futurist Medium (13 px), Futurist small (10 px), SE4 Block 1
Large / Medium / Small (12 / 10 / 8 px), SE4 Text button (17 px). FreeType reads `.fon`
files directly, so the client can load them from the install through its normal font
path; bundle our own open fonts as the default.

### 5.5 Sounds and music

- `Sounds/*.wav`: 89 files, PCM 16-bit stereo 44.1 kHz; `Sounds/New/` holds 88
  same-named remastered versions. The version history describes an option to choose the
  new or the classic set [M].
- Weapon sounds are named directly by Components.txt `Weapon Sound` (about 70 distinct
  files; names ending `_s` are separate sounds, not variants) [M].
- UI and event sounds are not referenced by data, so the engine picks them by name
  (inferred from names): `button`, `cmdbtn` (command button), `ordbtn` (order button),
  `close`, `endturn`, `boom1..3` (explosions), `cloakon`/`cloakoff`, `openwp`/`closewp`,
  `crteplnt`/`crtestrm`/`crtesun` and `destplnt`/`deststrm`/`destsun` (stellar
  manipulation), `cannon`, `smrtbomb` [M].
- `Music/`: 15 MP3 tracks named `Space Empires IV - Track NN.mp3`. Settings.txt defines
  three playlists by file name [M]: intro (1 track), background (8), combat (6). Music
  volume is a game option [M].

### 5.6 Scenarios and tutorial

`Scenarios/` holds, per scenario `<Name>`: `<Name>_Settings.txt` (keys `Name`,
`Description`, `Starting Game` = a `.gam` file in the same folder), `<Name>_Text.txt`,
the starting `.gam`, and 128x128 BMP illustrations. Only `Tutorial` ships [M].

### 5.7 AI files and other folders

`Ai/Default_AI_*.txt` are the defaults; `Ai/<Aggressive|Defensive|Neutral>/` hold
minister-personality overrides (General, Anger, Politics, Settings, Speech); race folders
hold race overrides (spec 05) [M]. `SaveGame/`, `Maps/`, `History/`, `Backup/` and
`temp/` are empty in a fresh install. `Extras/` holds third-party mods as archives plus
a description file. `Manual/` is the HTML manual.

---

## 6. File formats of the player-data folders

| Folder / file | Kind | Structure |
|---|---|---|
| `Dsgnname/*.TXT` | text, CRLF, Windows-1252 (some accented names) | One candidate design name per line, no header, alphabetical; 15 lists of 40–436 names with DOS 8.3 upper-case file names. The empire's "Design Name File" picks one; the designer offers names from it [M][T]. Ship names in manual captures read `<name> <4-digit serial> (<code>)`, where the code is the hull's two-letter VehicleSize.txt `Code` (LC light cruiser, CL colony ship) [S][M]. |
| `Scenarios/<N>_Settings.txt` | text | Standard record format, one record: Name, Description, Starting Game [M]. |
| `Scenarios/<N>_Text.txt` | text | Records with `ID`, `Series`, `Segment` (order inside the series; previous/next browse a series), `Turn` (0 = first turn), `For Players` (player numbers), `Text Title`, `Text Number of Paragraphs`, `Text Paragraph <n>`, `Image` (a BMP in Scenarios/) [M]. |
| `Path.txt` | text | One record: `Using Mod Directory` [M]. |
| `Empires/*.emp` | **binary** | 3–4 KB, entropy about 7.2–7.6 bits/byte, no readable strings. The start looks like a type-tagged value stream (a tag byte, then 1, 2 or 4 bytes, or a length-prefixed string whose bytes are obfuscated) [M]. Holds a saved empire (name, race style, traits, experience, password...) [T]. Treat as opaque; import is out of scope. |
| `SaveGame/*.gam` | **binary** | Same tagged/obfuscated style (the tutorial `.gam` is 75 KB), except for one **plain ASCII fixed-width summary block** near the start: the format version (`1.58`), the date, the player count, `Turn Based`/`Simultaneous`, `Same Machine`/`Different Machines`, then per player a number, empire name, emperor title and name, and `Alive` [M]. This is likely what the load/login screens read without decoding the rest (inferred). |
| `*.plr`, `*.trn`, `*.cmb` | binary (inferred) | Simultaneous play: player orders sent to the host; turn movement log (for the replay); combat log (for combat replays) [T]. |
| `<game>_events.txt`, `<game>_stats.txt` | text (per the manual) | Per-player history events and comparison statistics shipped with each turn [T]. Exact layout unknown (Q12). |
| `Maps/` | unknown | Written by File → Save Map and the map editor; read by Game Setup → Load Map. No sample exists (Q12). |

For our engine: keep our own save format (deterministic state plus a command log, as in
DESIGN.md). Reading `.gam`/`.emp` is not needed for parity and would mean reverse
engineering an obfuscated format; the one useful piece, the save list's
name/date/summary, can be shown from our own saves.

---

## 7. Open questions to verify in the running game

1. **1024x768 layout.** *Answered in spec 07 §UI (panel rectangles and frame strips).*
   Exact panel rectangles; what fills the 67 px right strip and
   RightFiller (more order buttons?); whether the layout follows the desktop resolution
   or an option.
2. **System grid pitch.** How the 13x13 grid maps onto 490 and 660 px backgrounds
   (about 37.7 and 50.8 px per cell); are 36 px sprites scaled at 1024?
3. **Order icon map.** *Answered in spec 07 §UI (the 20×2 strip and every cell).*
   Which `Orders.bmp` cell is which order; what the extra icons are
   (59 used vs about 43 documented orders); which `Main.bmp` icon is the 13th.
4. **Order availability.** When each order button is enabled (selection type,
   simultaneous mode, cloak), how many order pages exist and whether the page follows
   the selection.
5. **System art.** Where the 128x128 `Systems/*.bmp` and the 72x72 storm/asteroid tiles
   are drawn, and what `Mask Background Objs` and `Non-Tiled Center Pic` change.
6. **Empire colour.** Taken from the `_Main.bmp` swatch or chosen per player? What
   happens when two players share a style?
7. **Sprite rotation.** Heading steps for top-down minis (8 or free).
8. **Options windows.** Is Game Menu → Options the same window as Empire Options, or a
   separate game-options window (music volume, classic/new sounds, autosave, reset
   passwords)? Record both full lists, including the cut-off System Display group.
9. **Start-up menu (1.95).** What the `StartMenu.bmp` window is and how it relates to
   the Intro screen.
10. **Map colours.** RGB for unexplored, explored and empire colours; claimed overlays.
11. **Log details.** Meaning of the blue/green row bullets, sort order, whether filters
    persist between turns.
12. **Save folder contents.** After save, save-map, save-empire, autosave and one
    simultaneous turn: files created, extensions, per-player names, use of `History/`,
    `Backup/`, `temp/`; are `_events.txt`/`_stats.txt` plain text?
13. **Status icons.** The drones cell and the 16 undocumented cells.
14. **Transparency.** Black colour key for sprites? Additive blending for explosions,
    shields, beams?
15. **UI sounds and music.** Which sound goes with which action; when the background and
    combat playlists switch.
16. **Tooltips and focus.** Where tooltips appear and their delay; whether hotkeys work
    while a dialog is open; whether Esc closes dialogs.
17. **Weapon graphic index base.** Confirm `Weapon Display` is 1-based for beams and
    0-based for torpedoes by firing known weapons in the combat simulator.
18. **Ship naming.** Confirm the `<name-list entry> <serial> (<code>)` pattern and how
    serials are assigned.
19. **Planets filters.** What Coloniz\Empty adds to Colonizable (our client assumes: no
    other empire has a colony in that system), which treaties make a colony an "ally"
    one (we use Non-Aggression or better), and what makes a planet Special (we use: it
    has stellar abilities such as ruins or value bonuses). (inferred)
20. **Construction Queues toggles.** What Ships and Ship SY each include. Our client
    assumes Ships = mobile ships with a space yard, Ship SY = bases with one, Planets =
    colonies without a yard, Planet SY = colonies with one. (inferred)
21. **Tactical Combat details.** The Options window's list (the manual does not itemise
    it): our client offers animation on or off, animation speed, the square grid, the
    selected piece's reach, piece names, and ending the phase when no enemy is left. The
    move pointers and crosshairs are drawn by the client (the install's cursor files are
    not used yet). Right-clicking a piece opens a small Combat Piece Report. Which
    Orders are hotkeys and which need a target click (our client: Ram, Capture and Drop
    Troops are armed, then the target is clicked)? (inferred)
22. **Combat Simulator details.** How many virtual empires the owner picker offers (our
    client: up to four), how items are removed, and what Fleets for Plr does exactly
    (our client puts the selected ships in their side's fleet, with a formation and a
    strategy). (inferred)

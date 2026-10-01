# Spec 06: User interface, presentation and installed assets

Status: reference spec for client work. It was written clean-room from the SE4 Deluxe
manual (text and its screenshots, viewed only), the self-documenting data files, the
install's Readme and version history, and a survey of the install's asset folders
(listing, image headers and dimensions, visual inspection of sprite sheets). Everything is
paraphrased. Facts measured from files are marked **[M]**, facts from the manual text
**[T]**, facts read off manual screenshots **[S]**, and guesses **(inferred)**. Section 7
lists the open questions. Most of them were later settled by static analysis of the
executable's code and form resources (docs/CLEANROOM.md); those answers, and the facts
they added to the main sections, are marked **(confirmed: binary)**. Coordinates marked
that way are in frame pixels before the frame is centred on the screen.

Data-file grammar (`Key := Value` records between `*BEGIN*`/`*END*`) is specified in
spec 01 §1 and is not repeated here.

Conventions that hold across the whole UI:

- **Fixed-pixel layouts at two resolutions.** The game ships screen art for 800x600 and
  1024x768 only [M]. Windows are fixed-size bitmapped frames, not resizable. The game
  uses the 800x600 layout when the desktop is at most 800 px wide and the 1024x768
  layout otherwise, centred on the screen; there is no option (confirmed: binary).
- **Left-click acts, right-click reports.** In nearly every list, a left-click performs
  the primary action (select, add to a queue, remove from a queue, jump to the object in
  the main window), and a right-click opens a read-only report popup about the item
  (ship, planet, race, component, tech area) [T]. Our client should keep this.
- **Shift+click multi-selects** in the ship list and in the construction-queue list [T].
- **Column headers sort** the big list windows (Colonies, Planets, Ships) [T]. A click
  makes that column the first sort key; the keys clicked before stay as tie-breakers, up
  to five, newest first. Each column has a fixed direction, and clicking again does not
  reverse it. The sort keys are stored with the empire (confirmed: binary; §1.8).
- **No pop-up tooltips.** Hovering a command or order button writes the button's name
  and key at the top of the system panel (§2.3). No other control has a hint
  (confirmed: binary).
- **Dialogs are modal.** While any window is open the main window takes no keys; each
  dialog handles a few keys of its own (§3.4) (confirmed: binary).
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
| Launcher (added in 1.95) | Pick what to start. | The `StartMenu.bmp` picture as a 736x536 window centred on the screen; one column of ten text buttons, 311 px wide, every 31 px from (43,210): Play, Readme, History, Extras, PDF Manual, HTML Manual, Map Editor, two website links, Quit. Play closes it and opens the Intro; Quit exits; the others open a document, a web page or the separate map editor and leave the launcher open (confirmed: binary). | Program start without command-line arguments (the Steam shortcut). Any argument skips it; a fourth argument names a mod folder and overrides `Path.txt` (confirmed: binary). |
| Intro | Title screen. | First the publisher's logo (4 s), then the developer's logo (4 s); a key or click skips to the intro picture. The picture comes from `Game/Screens/1024X768` when the screen is wider than 800 px, else from `800X600`; data loading starts 2 s after it appears (at once after a skip) (confirmed: binary). Background art, version label, "Loading" progress while data files load (buttons disabled until done); buttons Quick Start, New Game, Resume Game (loads the last saved game, which the game records per computer), Load Game, Tutorial, Scenario, Credits, Quit Game in two rows along the bottom [T][S]. | Launcher → Play, or program start with arguments. |
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
| Game Menu ("File Menu") | File operations. | Vertical buttons: New, Load, Save Game, Save Map, Save Empire, Players, Options, Delete Game, Quit, Close [T][S]. Save Map is enabled only when the game's setup lets players save the map; Save Empire asks whether to include the designs; Esc closes the menu (confirmed: binary). | F2 / first command button. |
| Save Game, Delete Game | Name / remove a save. | Save Game: buttons New Save Game, Change Directory, Cancel; files go to the folder in §6.1 (confirmed: binary). | Game Menu. |
| Player Computer Control | Toggle AI control per player. | Player list with lamps; OK / Cancel. Also reused by the Combat Simulator [T]. Asks for the Game Master password first when one is set; switching an empire also switches its ministers (confirmed: binary). | Game Menu → Players. |
| Options (per computer) | Sound, music, animation and autosave. | Lamp list under headings; Close. Full list in §1.9 (confirmed: binary). | Game Menu → Options. |
| Designs | Browse and manage designs. | Design list; detail pane (stats, then either components or, with Stats\Strategy on, build/loss/kill counts and default strategy). Tabs: Ship Designs, Unit Designs, Enemy Ship Dsgn, Enemy Unit Dsgn. Actions: Create, Copy, Edit, Upgrade, Make Obsolete, Hide Obsolete, Stats\Strategy, Simulator [T]. | F3. |
| Create Design | Ship designer. | Size, type and name pickers; running totals; components-on-design and components-available lists; warnings box; hover detail. Comp Type (group filter), Weap Mount, Condensed View, Only Latest, Weapons Report, Create Design, Cancel [T]. | Designs → Create / Copy / Edit. |
| Weapon Mounts | Pick a mount for weapons added next. | List; Cancel. | Create Design; Weapons Report. |
| Combat Simulator | Set up a test battle. | Vehicles-in-combat list, design list, owner picker; Tactical / Strategic choice; No Obsolete, Strategies, Computer Control, Fleets for Plr, Change Cargo, Begin, Cancel [T]. Details in §1.10.4 (confirmed: binary). | Designs → Simulator. |
| Planets | All planets seen. | Statistics block; galaxy mini-map highlighting the hovered row; planet list. Filter tabs: All, Colonizable, All Colonies, Enemy Colonies, Ally Colonies, Coloniz\Empty, Coloniz\Breathe, Ship Enroute, Asteroids, Special; toggle No Sys To Avoid; Send Colony Ship [T]. Rules and layout in §1.8.1 (confirmed: binary). | F4. |
| Colonies | Own colonies. | Statistics; mini-map; sortable list whose columns depend on the tab: General, Value, Production, Facilities, Cargo, Construction, Status (status icons), Races, Orders. Actions: Scrap Facil Types, Set Colony Type [T]. | F5. |
| Ships\Units | Own ships, units, fleets. | Statistics; mini-map; list. Column tabs: General, Orders, Cargo, Fleet, Maintenance (each starts with a picture column); toggles Show Ships / Show Units / Show Fleets [T]. | F6. |
| Construction Queues | All queues. | Statistics; quadrant map; queue list. Column tabs Rate, Usage, Planet Value, Facilities, Cargo; inclusion toggles Ships, Planets, Ship SY, Planet SY; Multi-Add (acts on the shift-selected queues), Scrap Facilities, Upgrade Facilities [T]. Rules and layout in §1.8.2 (confirmed: binary). | F7; Log → Constr. Queues. |
| Set Construction Queue | Edit one queue. | Buildable-items list (tabs Ships, Facilities, Units, Upgrades; Only Latest); queue owner header; the queue itself; hover detail. Toggles Emergency Build, Repeat Build, Queue On Hold; Set Move To / Clear Move To; Fill Queue; Clear Queue; Reorder Queue [T]. | Order "Build Queue" (Q); row in Construction Queues. |
| Select Queue Type | Fill a queue from a template. | Template list; Add Type, Delete Type, Cancel. | Set Construction Queue → Fill Queue. |
| Research | Choose research. | Points available; tech-area list (left-click adds); current projects in pages of four (Projects 1-4, 5-8, 9-12); Repeat Projects, Divide Pts Evenly, Tech Tree (only if the game allows it), Reorder Projects [T]. | F8. |
| Tech Tree | Whole tree. | Two list views: Tech Areas (prerequisites), Tech Levels (what each level unlocks); Export (writes the view to a text file) [T]. | Research → Tech Tree. |
| Empires | Diplomacy hub. | One portrait per known empire with a stat column under each; tabs Treaty, Trade, Tariff. Buttons History, Treaty Grid, Intelligence, Borders, Victory Conditions, Scores, Comparisons, Our Race [T]. Left-click a portrait opens Communicate; right-click opens Race Report. | F9. |
| Log | Turn news. | See §4.1. | F10; auto-opens at turn start. |
| Empire Status | Budget and empire settings. | Balance sheet (income lines, expense lines, net per turn, treasury and storage cap; see spec 02). Buttons Empire Options, Ministers, Systems To Avoid, Waypoints, Strategies, Repair Priorities, Change Email, Change Password [T]. | F11. |
| Options (Empire Options) | Per-empire UI and behaviour switches, saved with the game. | Scrolling list grouped under headings: General Options, Next/Previous, Ship Movement, Ship Orders, System Display, Galaxy Display, Latest Items, Politics [S]. Full list with defaults in §1.9 (confirmed: binary). | Empire Status → Empire Options; Log → Goto on some entries. Not Game Menu → Options, which opens the per-computer Options window (confirmed: binary). |
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
| Cargo Transfer | Two cargo lists (from / to) with transfer step Move One / Five / Ten / All. The manual says it is not available in simultaneous games, but the executable lights the button and opens the window in both modes (confirmed: binary). | Order T; Combat Simulator. |
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
| System Report | Shown when empty space in the system panel is clicked. The 128x128 picture `Pictures/Systems/<Background Bitmap>` (the system type's background name, folder root; normal systems use `Starmap.bmp`) at the top left; "<Name> System" right-aligned at the top (left edge at most x 120, y 4); the type's description in grey (160,160,160) from y 140, in a box 42 px tall; the system's abilities from y 197 (confirmed: binary). |
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
| Combat resolution prompt | "Tactical" or "Strategic" choice (tactical hidden when the game disables it) [S][T]. Who is asked, and when: §1.10.5 (confirmed: binary). | Combat starting with a piece of a human-controlled empire. |
| Tactical Combat | Full screen. Status bar (location, combat turn, participants, next/prev selectors for ships that can move or fire); tactical map (bottom left; left-click selects own ship, moves to empty space, fires at enemy; right-click reports); current-piece panel (portrait, flag, group badge: blue = leader, red = member; shield and damage bars; weapon grid with reload pips, click to toggle a weapon); target-piece panel (1024x768 and up only; shows blue/red shield/internal damage numbers); overview map with a dotted view box. Buttons Options, Orders, Auto, End Turn. Pointer: arrow, 8 directional move arrows, crosshairs over enemies [T]. Layout, Begin, mouse and pointers in §1.10.1 (confirmed: binary). | Prompt; Combat Simulator. |
| Tactical Combat Orders | Launch Units, Launch Fighters in Groups, Drop Troops, Ram Ship, Capture Ship, Resolve Combat, group leader/member/clear. What each does: §1.10.2 (confirmed: binary). | Tactical → Orders. |
| Combat Options / Combat Replay Options | Animation, sound, music and display switches; lists in §1.10.3 (confirmed: binary). | Tactical → Options; Replay → Options (adds Stop Replay). |
| Combat Piece Report | Movement, shields, damage, supply, max targets, combat group, formation. A full report window with picture and the object's tabs; unit groups and seekers have none (§1.10.1) (confirmed: binary). | Right-click a piece. |
| Strategic Combat | Watch-only: system, coordinates, combat turn; forces list (flag, then per vehicle size current and lost counts); small map of coloured squares; Begin, Close. Opens before the first combat turn; the battle is then fought live, the map moving step by step and the list updated after each combat turn, with no delay (§1.10.5) (confirmed: binary). | Prompt; simulator. |
| Ground Combat | Planet details, facilities, defender and attacker lists; Begin, Close. Fought round by round, the counts updated after each round, about 0.9 s per round (§1.10.6) (confirmed: binary). | After troops land; a ground stalemate at the colony owner's end of turn. |
| Combat Replay | Same layout as tactical, playback only; Options, Next. Space = Next, Esc closes. Title strip: "Combat Replay", the location, the combat turn and the empires' flags; no log, event list or summary of any kind (confirmed: binary). | Log → Combat Replay. |

### 1.7 Multiplayer, tutorial and end of game

| Screen | Purpose |
|---|---|
| TCP/IP Host | Player list, own IP addresses, a status line naming the current step of the host cycle (spec 05), hideable chat; Begin Game, Process Turn, Add/Remove Empire, Play Turn, Chat, Minimize Game, Quit Game [T]. |
| TCP/IP Player | Source and host IP, player name, status line, chat; Connect to Host, Create Empire, Play Turn, Chat, Minimize, Quit [T]. |
| Movement log replay | Not a window: in simultaneous games the main window replays the host's 30-day movement (full, stepped by day, per ship, rewind) [T]. |
| Tutorial / Scenario text window | Titled text with a 128x128 picture and previous/next through a series; re-opened with Ctrl+H or the "T" button in the status bar [T][M]. |
| Finale | Full picture for victory, defeat, or "human players all dead", chosen from lists in Settings.txt [M]. |

### 1.8 Planets and Construction Queues windows

All of this subsection is (confirmed: binary).

#### 1.8.1 Planets

**What is listed.** Planets in systems the empire has explored. Planets hidden by a
planetary cloak stronger than the empire's sensors are left out; with the Omnipresent
view option only this cloak test applies. Asteroid fields appear only under the
Asteroids tab. A planet counts as a colony only while the empire can see the colony, so a
cloaked colony looks uncolonized.

Terms used by the tabs:

- **Colonizable**: the empire can colonize the planet's surface type (Rock, Ice, Gas
  Giant). At the start that is only the homeworld's type; colonization technology adds
  the others. The game options "only home planet type" and "only breathable atmosphere"
  narrow it further. It says nothing about whether the planet is already colonized.
- **Breathable**: the planet's atmosphere is the race's own.
- **Enemy**: another empire whose treaty with us is below Non-Aggression (war,
  non-intercourse, none, or no contact yet). **Ally**: Non-Aggression or better,
  including subjugation and protectorate treaties.
- **Colony ship**: one of our ships with a Colonize Planet ability (rock, ice or gas). It
  is **available** when it has no orders, is not mothballed and is not out of supplies. A
  colony ship with orders marks the planet of its last Colonize order as en route.

| Tab | Lists |
|---|---|
| All | every planet, no asteroid fields |
| Colonizable | planets of a colonizable type, colonized or not, ours included |
| All Colonies | visible colonies of any empire, ours included |
| Enemy Colonies | visible colonies of enemy empires |
| Ally Colonies | visible colonies of allied empires |
| Coloniz\Empty | colonizable type, and the planet itself is not colonized |
| Coloniz\Breathe | as Coloniz\Empty, and breathable |
| Ship Enroute | planets that one of our colony ships has a pending Colonize order for |
| Asteroids | asteroid fields only |
| Special | planets with the `Ancient Ruins` or `Ancient Ruins Unique` ability |

**No Sys To Avoid** (off by default) hides planets in the empire's Systems To Avoid. The
toggle is stored with the empire when clicked; the tab (default All) when the window
closes. Both come back on the next opening and are saved with the game.

**Button column.** Slots 1–10 the tabs, 11 empty, 12 No Sys To Avoid, 13 Send Colony
Ship (dim while no colony ship is available), 14 Close.

**Send Colony Ship** opens a "Select Planet to Colonize" picker over the current tab's
planets. For the planet picked, the game takes the available colony ships that can
colonize its type (in a turn-based game they also need movement left) and chooses the one
with the shortest route. That ship gets, in order: Load Cargo (population; only when it
carries none yet), Move To the planet, Colonize. In a turn-based game the window then
closes and the main window shows the ship; in a simultaneous game it stays open and
refreshes.

**Rows.** Left-click closes the window and shows the planet in the main window;
right-click opens the planet report; hovering highlights the system on the mini-map.
Shift+click does nothing here.

**Statistics** (box at about (17,38); labels in label blue, #7D9FFF, at x 18, one every 16 px from
y 40; values right-aligned at x 289): known systems; planets (no asteroid fields);
colonizable planets; of those, owned by enemies, by allies, by non-aligned empires, not
colonized, not colonized and breathable; then, after a gap, colonizing ships and how many
of them are available. These counts use the real owner, not visibility. Non-aligned is
always 0, since the treaty test puts every empire in one of the other two groups.

**Columns** (list at (12,252), 563 px wide, 36 px rows, header 20 px above): picture
(40 px); Name (145 px) with a grey (#A0A0A0) second line giving type and size, such as
"Rock - Medium"; Atmosphere (95 px); three Value columns (58 px each) in the resource
colours, showing a percentage, or the remaining amount when finite resources are on;
Ship Enroute (the rest), the name of the colony ship heading there.

**Sort directions.** Name and Atmosphere A to Z (case ignored); values highest first;
picture and Ship Enroute by planet size, smallest first. Default: by name.

#### 1.8.2 Construction Queues

**Which objects have a queue.** Every colony. A ship or base only while it has a working
space yard (the `Space Yard` ability, not cloaked) and is not mothballed; when it loses
the working yard, its queue is cleared at its next update.

| Toggle | Includes |
|---|---|
| Ships | queues of ships and bases whose yard is not working right now. Such queues are cleared at the vehicle's next update, so this group is normally empty |
| Ship SY | queues of ships and bases with a working yard: in practice every vehicle queue, mobile or base |
| Planets | colonies without a working space yard (no Space Yard facility, or the colony is cloaked) |
| Planet SY | colonies with a working space yard |

All four toggles are on by default; with all off the list is empty. The tab (default
Rate) and the toggles are stored with the empire when the window closes.

**Button column.** Slots 1–5 the tabs Rate, Usage, Planet Value, Facilities, Cargo; 6
empty; 7–10 the toggles Ships, Planets, Ship SY, Planet SY; 11 Multi-Add; 12 Scrap
Facilities; 13 Upgrade Facilities; 14 Close.

**Rows.** Left-click opens Set Construction Queue for that queue, over this window.
Shift+left-click tags or untags a queue for Multi-Add (tagged rows show a marker).
Right-click opens the object's report. Hovering highlights the system on the mini-map.

**Columns.** Name (170 px): picture, name, and the status icons on a second line. Tab
column (150 px), whose header and content follow the tab: Rate (three rates), "Usage Per
Turn" (three usages), Planet Value (three percentages), "Number of Facilities" (used /
slots, colonies only), "Cargo Space"; under the value, in yellow (#FFFF00), the queue's
build-mode note when one applies. Construction Queue (the rest): the first three items,
one per 12 px line, with the count when above 1; at the right the time left in years
(turns × 0.1), shown as "Never" at 9999 turns or more and "On Hold" for a held queue,
both in yellow; "(Repeat)" under the time when repeat is on. Rows are separated by a
line in #647EC7.

**Sort directions.** Name A to Z; tab column highest first (the sum of the three values,
or the facility count, or the cargo; vehicles count 0 for facilities and cargo); queue
column by the first item's name, A to Z. Default: by name.

**Statistics** (box at about (17,38)): resources generated per turn and queue usage per
turn (three amounts each); total, planetary and ship space yards; queues on hold; and a
one-line hint at (18,218) that clicking a row changes its queue.

**Multi-Add** is always enabled. With nothing tagged it shows a message asking the player
to Shift+click queues first. Otherwise it opens the real Set Construction Queue window on
an empty temporary queue: the Ships tab is enabled only if every tagged queue can build
ships, the Units tab likewise for units, and Facilities, Upgrades and the queue option
buttons are disabled. When that window closes, every item placed in it is appended, in
order and with its count, to each tagged queue. The tags stay set.

**Scrap Facilities** ignores the tags: it opens a "Select Planet" picker over the listed
colonies, then the scrap checklist for the planet picked. **Upgrade Facilities** ignores
the tags: it queues every possible facility upgrade on every colony of the empire, with
no cost limit.

### 1.9 Options windows

There are two windows (confirmed: binary). Both show one scrolling list of lamp rows under
headings, filling the content area (x 15–575, y 56–456), with "Options In Use" in label
blue at (15,38) and Close in the bottom slot. Groups marked "pick one" act as radio
groups.

**Options (Game Menu → Options), per computer.** Stored in the user's settings on that
computer, not in the game, except the autosave choice, which belongs to the game.

| Heading | Rows |
|---|---|
| Animation | animate ship movement in the system window; animate ship movement in combat |
| Sound | Sound On; Classic Sound Effects (the original set in `Sounds/` instead of the remastered `Sounds/New/`) |
| Music (pick one) | Music Off; Music Volume 20%, 40%, 60%, 80%, 100% (shows Music Off when music is off or Settings.txt does not allow music) |
| Tactical Combat | Fast Tactical Combat |
| System Display | Display Ship Movement Lines (the switch Ctrl+L toggles (inferred)) |
| Autosave For This Game (pick one) | None; Every Turn; Every 2, 3, 5 or 10 Turns |

A **Reset Passwords** button appears only on the host of a simultaneous game, between
turns, when no player is signed in. The host picks players; each gets a new six-digit
password (three random two-digit numbers from 11 to 99), which a message shows to the
host. Drawing these numbers does not disturb the game's random sequence.

The same per-computer store keeps the last saved game's name (Resume Game), the tactical
display switches of §1.10.3, and up to ten remembered TCP/IP host addresses and player
names. With nothing stored yet (a fresh install), the game starts with: both animations
on, Sound On on, Classic Sound Effects off (so the remastered set plays), music on at
100 %, Fast Tactical Combat off, ship movement lines off, and the tactical display
defaults of §1.10.3.

**Empire Options (Empire Status → Empire Options), per empire.** Stored with the empire
and saved with the game, so each hotseat player has their own. Defaults for a new empire
in brackets.

| Heading | Rows |
|---|---|
| General Options | show the log at the start of the turn [on]; confirm ending the turn [on]; confirm scrapping [on]; confirm stellar manipulation [on]; confirm deleting a research project [on]; confirm deleting an intelligence project [on]; confirm deleting the first item of a construction queue [on]; show the colony-type picker when colonizing [on]; note when similar system-wide abilities exist [on] |
| Next/Previous | skip ships under construction [off]; skip damaged ships [off]; stop once per location [off]; skip ships in fleets [off] |
| Ship Movement | avoid minefields [on]; avoid restricted systems [on] |
| Ship Orders | clear orders on entering a system with an enemy [on]; clear orders on entering a system with any other empire [off] |
| System Display | warp point names [on]; planet names [off]; colonizable planets [on]; system grid [off]; coordinate location [on]; then one row per group of facility letter markers, all [off]: R/S/Y (resupply depot, spaceport, planetary space yard), Ca/Cc/Cv (atmosphere, conditions and value changers), St/Ft, Rc/Rr, Sst/Sft, Spv/Spc, Sph/Spa, Scm/Sdm, Srm/Ssm, Spp/Src, Sbe/Sbi, Slr |
| Galaxy Display | Show Grid Lines [on]; Show Warp Lines [on] |
| Latest Items | only latest items for construction [off]; only latest components for designs [off] |
| Politics | automatically claim every system we colonize [on] |

### 1.10 Combat windows in detail

All of this subsection is (confirmed: binary). W×H is the tactical window's size.

#### 1.10.1 Tactical Combat

- **Layout.** Map at (10,38), (W−256)×(H−44), 36 px squares. Overview map 218×190 at
  (W−230, H−196); clicking it centres the view (3 px per square). Four 113×30 buttons
  in two rows, 70 px above the overview: Options and Orders, then Auto and the fourth
  button. Current-piece panel 216×64 at (W−232, 36) with the weapon list under it. The
  target-piece panel only when the window is at least 1024 wide.
- **Begin.** The window opens with the fourth button labelled Begin and Orders disabled.
  The first press starts the battle, relabels it End Turn and enables Orders. When the
  battle ends, a "Combat Complete" message appears and the window closes by itself.
- **Mouse.** Left-click on an empty square moves the selected piece (a leader's members
  move to their formation places); on another empire's piece, fires at it; on one of our
  pieces, selects it. Clicking the current-piece header opens that piece's report.
- **Pointers** (the install's `.cur` files): Normal by default; Target over another
  empire's piece while one of ours is selected; one of the eight Arrow pointers over an
  empty square while a piece is selected, by the signs of the column and row offsets from
  that piece (Normal on its own square, or when a drone is selected); Select everywhere
  over the map while Ram or Capture waits for a target; Hourglass while the game is busy.
  All twelve are loaded once at start-up from `Pictures/Game/` (`Normal`, `Hourglass`,
  `Select`, `Target` and `ArrowN`, `ArrowS`, `ArrowE`, `ArrowW`, `ArrowNE`, `ArrowNW`,
  `ArrowSE`, `ArrowSW`, each `.cur`, names compared without case) as Windows cursors,
  each with the hotspot stored in its file (§5.3). Normal is also the pointer of the main
  window and of most dialogs, and Hourglass replaces the pointer everywhere while the game
  works (turn processing, loading, long computer moves); nothing else draws a pointer.
- **Right-click on a piece** opens the object's ordinary report for a neutral obstacle,
  and otherwise the **Combat Piece Report**: a 353×422 report window built like the
  object reports of §1.4, whose Detail page (290×361) holds the 128×128 picture at the
  top left with the owner's flag on it, the name at the top (from x 120, or right-aligned
  10 px from the page's right edge when it is too long), and these lines: labels in
  #7D9FFF at x 130, y 20, 50, 80 … (30 px apart), each value in white at x 140, 15 px
  below its label.

  | Line | Value |
  |---|---|
  | Movement | movement points left / maximum, as "3/6" |
  | Population (planets, in place of Movement) | the colony's total population, as for colonies elsewhere ("0M" without a colony) |
  | Shields | now / maximum |
  | Damage | taken / full hit points, for every kind of piece: full hit points as in the overkill limit (spec 04 §16: a ship's full design structure, a unit group's units at full health, a planet's hit points at the battle's start, a seeker's resistance), taken = full − current, at least 0 |
  | Supply | ships, bases, fighter and drone groups: now / capacity, a number above 100000 written in thousands with "K"; "Endless" with unlimited supply. Planets and satellite groups: "Never". Seekers: "None" |
  | Max Targets | the piece's target budget (spec 04 §6) |
  | Combat Group | "Group N - Leader" when it leads group N, else "Group N - Wingman" when it belongs to group N, else None. Fleet groups are numbered groups like the others (spec 04 §3 step 5), so a fleet's ships show their number. A drone group has **Target** here instead: its drone target's name, or None |
  | Formation (not for drone groups) | the formation of the group the piece leads; None for every other piece. The value is not cleared when the piece stops leading, so a former leader still shows it |
  | Conditions (planets with plague) | at y 230, with "Plague N" under it |

  **Tabs** follow the object: a ship or base has Detail, Comps (its design's
  components), Cargo (only when it has cargo space) and Ability; a planet Detail,
  Facil, Cargo (likewise) and Ability; Detail is selected first. A fighter, satellite or
  drone group has no tabs: the Detail page is cut to 249 px and the group's units are
  listed under it in a 108 px unit grid at y 253. A seeker has no tabs, and its Detail
  page shows the picture of the weapon that launched it.

#### 1.10.2 Tactical Combat Orders

A 326×350 window without border or title bar, holding 11 stacked 306×30 buttons with no
gap between them (330 px from (10,10)). It is modal; a click on a button closes it, and
only then does the chosen order run, so every picker below is a window of its own that
opens after the menu has gone:

| Button | Key | Effect |
|---|---|---|
| Launch Units | L | opens the Launch Units window for the selected piece |
| Launch Fighters in Groups | — | opens a list window titled "Select Fighters Per group", one column "Amount", rows 5, 8, 10, 15, 20, 30, 40, 50 |
| Drop Troops | T | acts at once, no target click: lands all troops of the selected ship on the adjacent colony of another empire that comes last in piece order, **whatever the treaty**; refused when that colony already holds a third empire's troops; ground combat follows at once (spec 04 §11). For a side played by hand a message explains a refusal: no colony adjacent, troops of another empire already there, or no troops aboard |
| Ram Ship | R | arms a target mode; the next left-click on a piece rams it, a click on an empty square cancels |
| Capture Ship | C | as Ram Ship, boarding instead |
| Resolve Combat | — | the game's confirmation window, titled "Resolve Combat", asking whether the rest of the battle should be fought automatically; confirming hands every side to its strategies (spec 04 §4) |
| Set Group Leader / Set Group Member | Alt / Ctrl + digit | opens a list window titled "Select Combat Group" with the numbers 1 to 9; a new leader then picks a formation in a "Select Formation" list window. Closing that one without a choice clears the piece's group marks |
| Clear Group Assignment, Clear All Group Assignments | Alt or Ctrl + 0 clears the selected piece | act at once |
| Cancel | | closes the menu |

Group numbers run from 1 to 9: the list offers no 0, and Alt+0 or Ctrl+0 clears the
selected piece's marks instead of setting group 0. The group orders act only on a
selected piece of the side whose phase it is, never on a seeker, and refuse silently
(spec 04 §5).

#### 1.10.3 Combat Options and Combat Replay Options

**Combat Options** (Tactical → Options), per computer, shared with the Options window of
§1.9 where the rows are the same. Fresh-install defaults in brackets:

| Heading | Rows |
|---|---|
| Animation | animate ship movement in combat [on] |
| Sound | Sound On [on]; Music On [on, unless music is off or not allowed] |
| Tactical Combat | Fast Tactical Combat [off] (removes every wait from the animations, below); Show Group Identifiers [off]; Show Viewing Rectangle on Map [on] (the dotted box on the overview); Center Map on Current Ship [on]; Show Weapon To Hit Chances [off] (with it on, hovering an enemy while a piece is selected shows each weapon's chance in the weapon list); Show Grid [off] |

Closing the window saves the switches and starts or stops the music. In the Combat
Simulator only, a Stop Combat button ends the battle and closes the tactical window.
Layout as in §1.9 ("Options In Use" at (15,38), list at (15,56), 560×400).

**Fast Tactical Combat** keeps every animation and every frame of it, and removes the
waits between frames; there is no speed factor. Without it the Tactical Combat window
waits:

| Animation | Wait |
|---|---|
| a piece moving one square, with "animate ship movement in combat" on | it slides to the square, 1 ms after each frame |
| the same with that switch off | the piece jumps, then 0.1 s |
| a piece turning to a new facing | 0.01 s after each frame of the turn |
| a beam | 0.00001 s after each stamp while it is drawn, 0.00005 s after each while it is erased |
| a torpedo | 1 ms after each frame of its flight |
| a hit (explosion or shield-hit animation, 8 frames drawn in place) | 0.1 s after each frame and after the last is wiped, 0.9 s in all |
| after each hit | 0.3 s |

With the switch on, all of these are drawn without waiting, so the pace is the drawing's
own. Fast Tactical Combat in Combat Replay Options does the same for the replay. Neither
switch touches the Ground Combat window (§1.10.6) or the Strategic Combat window, which
has no animations (§1.10.5).

**Combat Replay Options** (Replay → Options), per empire and saved with the game:
animate ship movement in combat replay [on]; Fast Tactical Combat [off]; Show Viewing
Rectangle on Map [on]; Show Grid [off]; and a Stop Replay button that closes the replay.

#### 1.10.4 Combat Simulator

- **Buttons**, top to bottom: Tactical and Strategic (tabs; Tactical selected); No
  Obsolete (toggle, off, saved per empire; hides only the player's own obsolete designs);
  Strategies (the player's Strategies window); Computer Control (the Player Computer
  Control list for the 10 sides; side 1 by hand and sides 2–10 by the computer at
  first); Fleets For Plr (Fleet Transfer for the side chosen in the owner picker, holding
  that side's ships; fleets, formation and strategy are set there); Change Cargo (Cargo
  Transfer over all the simulator's vehicles and planets); Begin; Cancel.
- **Combat vehicles** (left; (17,75), 295×370, 36 px rows with flag, picture and name),
  sorted by side 1 to 10, then neutral objects. A left-click on a row removes it.
- **Items** (top right; (322,75), 250×230, picture and name): every design whose hull is
  a ship, base, fighter, satellite or drone, both the player's own and the other
  empires' designs the player has seen (no mines, troops or weapon platforms); and the
  objects of the player's home system that are colonized (any owner) or unowned (stars,
  warp points, comets, storms, empty planets and asteroid fields); no vehicle or unit
  group of the real game is offered. Left-click adds the item to the chosen side;
  right-click opens its report. Storms and uncolonised asteroid fields can be added but
  take no part in the battle (spec 04 §17).
- **Owner for item** (bottom right; (322,325), 250×120): a lamp list from "Race 1" to
  "Race 10", each row with the side's numbered colour box (spec 04 §17: 1 red, 2 blue,
  3 green, 4 yellow, 5 purple, 6 white, 7 aqua, 8 lime, 9 maroon, 10 olive), never an
  empire flag, whoever the side copies; Race 1 is selected at first. The Computer
  Control list shows the same boxes with "Race N", and so do the battle windows of a
  simulation wherever a real battle shows a flag.
- **One click adds** one ship, fully supplied, named after its design with the side's
  next serial number ("<design> 0001", one counter per side for all designs, never
  lowered by removals; spec 04 §17); or one unit to the side's group of that kind
  (fighters, satellites), while a drone design always makes a new group; or an unowned
  object as a neutral obstacle. The first item given to a side makes that side a copy of
  the real empire that owns it (spec 04 §17).
- **Begin** refuses when no side has a vehicle. With Strategic selected, the Strategic
  Combat window opens over the simulator, which stays open. With Tactical selected, the
  simulator and Designs close and the battle is fought in the Tactical Combat window;
  afterwards Designs and the simulator reopen with the same setup. The Tactical/Strategic
  choice does not depend on Computer Control. **Cancel** discards the setup.

#### 1.10.5 Who sees a battle, and the Strategic Combat window

- **Turn-based, one machine.** Every battle with a piece of a human-controlled empire
  asks the question when it starts, whoever's move started it, during computer turns
  too. The question is a "Select Combat Type" window with Strategic and Tactical buttons;
  when the No Tactical Combat game option is on it is the Strategic Combat window with
  Begin and Close instead. When the empire whose turn it is is computer-controlled, a
  253×150 notice comes first, naming the system and showing the empires' flags and
  names, with a Begin button (Esc or Enter also continue).
- **Turn-based, different machines.** No question: the Strategic Combat window opens
  with Begin and Close when the player whose turn it is is human; otherwise the battle is
  not shown.
- **Simultaneous.** The window opens for every battle, and only when
  `Simultaneous Games Show Strategic Combat` is on.
- **The sequence**, step by step. A battle interrupts whatever started it (a group's
  move, an Attack or Seek order, spec 04 §2); nothing else in the game happens until it
  is over and its window closed.
  1. **Set-up.** The battle is set up at once: pieces made and placed, the phase order
     drawn, each participant's starting pieces recorded for its report, the combat
     replay started when `Create Combat Replay` is on (spec 04 §3, §15).
  2. **Who sees it** is decided as above. A battle nobody sees is fought to its end
     there and then, with every side on its strategies and no window, and the sequence
     goes on at step 7.
  3. **Notice.** On one machine, when the empire whose turn it is is computer-controlled,
     the 253×150 notice comes first.
  4. **The window opens before the first combat turn**, modal over the main window: in
     its question form ("Select Combat Type", buttons Strategic and Tactical) when
     tactical combat is offered, otherwise in its own form ("Strategic Combat", Begin,
     and Close dim). Either way the forces list and the small map already show the
     battle as set up; "Combat Turn" is not drawn yet. The window cannot be closed: it
     closes only when the battle is over or Tactical was chosen. Tactical closes it and
     the battle is fought in the Tactical Combat window instead (§1.10.1).
  5. **Fought live.** Strategic in the question form retitles the window "Strategic
     Combat", turns the second button into a dim Close and starts at once; in the other
     form Begin starts. Both buttons go dim, every side is handed to its strategies, and
     the window fights whole combat turns one after another, each with every empire's
     phase in the fixed order (drones, seekers, the other pieces; spec 04 §4). The small
     map is redrawn after every single step of any piece or seeker, as it is taken. The
     window lets the system handle its pending messages after each empire's phase and
     after each combat turn, so it repaints as it goes. After each combat turn and the
     next one's upkeep it recounts the forces, redraws "Combat Turn N", the list and the
     map, and checks the end (spec 04 §4). A computer side's landing in the middle of a
     phase opens Ground Combat over it at once (§1.10.6); the battle waits until that
     window is closed.
  6. **Over.** When the end check finds the battle over, the end-of-battle steps run
     while the window is still open (carriers recover their units, regenerating armor
     is restored, the survivors are recorded; spec 04 §15) and Close lights up. The game
     waits for Close.
  7. **After Close** (or after an unseen battle): the system's display is refreshed,
     destroyed objects leave the game, every participant gets its battle report and
     happiness event (spec 04 §15), the replay is stored, pieces that were cloaked
     cloak again (spec 04 §2), and the interrupted move or order resumes (its group's
     orders fail or are used up, spec 04 §2) and the turn goes on.
- **Pace.** There is no wait anywhere in the strategic battle: no delay between steps,
  phases or combat turns, no pause, skip or speed control. A combat turn stays on screen
  only as long as the next one takes to compute and draw, so the pace is the machine's
  own speed. The window has no shots, explosions or sounds; only the squares of the
  small map move.
- **Layout.** Title strip at y 10: "System" in #7D9FFF at x 270 with the system's name
  right after it in white; "Coordinates" in #7D9FFF at x 460 with the sector's
  coordinates right after it; "Combat Turn" in #7D9FFF at x 630 with the number right
  after it, drawn only while the battle runs. "Combat Forces" at (15,38), "Current" and
  "Lost" at +220 and +270, all in #7D9FFF. List at (15,55), 329×400, 18 px rows. Map at
  (354,55), 218×191, 3 px squares, framed by a #647EC7 line one pixel outside it.
- **Forces list.** Its rows are made once, when the window opens, and never added
  later. For each empire in player-number order that has any row: a row with its flag
  (in a simulation the side's numbered box, spec 04 §17) and name; then one row per hull
  (the VehicleSize name, in VehicleSize order) that the empire had in the battle at
  set-up, with Current and Lost; then its colonized planets, at most 5 per empire, the
  first in piece order, by name. Counting, redone after each combat turn: each ship or
  base piece counts 1 under its design's hull; each unit group counts, stack by stack,
  the stack's living units under the hull of that stack's design, so a group mixing
  designs spreads over several hull rows; seekers are not counted. Lost is the highest
  count seen minus the current count. A hull that first appears during the battle (units
  launched from cargo, a captured ship of a new hull) gets no row, though launches do
  raise the highest count of a row that exists. A planet row shows Current 1, Lost 0
  while a piece of that empire with the planet's name exists, and 0 / 1 otherwise; a
  planet taken by troops shows as lost for its old owner and does not appear under the
  new one.

#### 1.10.6 Ground Combat

- **When it opens:**
  - **During a space battle shown in a window**, at once when troops land: by hand in the
    Tactical Combat window, or by a computer side in the Tactical or Strategic Combat
    window, in the middle of that side's phase. It opens over the combat window, without
    a notice, and the space battle waits until it is closed.
  - **At the colony owner's ground-combat step** (the end of that empire's turn, spec 05
    §8 step 17), for each of its colonies where troops of an empire below Non-Aggression
    with it still stand after an unfinished fight: first the 253×150 notice of §1.10.5
    in its ground form (the system's name and the two empires' flags and names, Begin),
    then this window over the main window. The turn's processing waits for it.
  - It opens only in turn-based games and when at least one of the two empires is
    human-controlled; in the simulator always. Simultaneous games, fights between two
    computer empires and battles nobody sees run the rounds silently. Apart from the
    hand-over at the colony owner's step, treaties play no part (spec 04 §13).
- **Running.** The window opens with Begin, and Close dim; it cannot be closed before the
  fight is over. Begin goes dim and the rounds follow one another with no further input.
  After every round, in this order: the round counter is redrawn; the facility grid and
  both unit grids are refreshed with the counts left after that round (the grids list
  troop and militia stacks only, not the other stored units); an explosion (the 8-frame
  36 px strip of `General.bmp` at y 64) plays over a random 36×36 spot inside the planet
  picture, 0.1 s after each frame and after it is wiped, 0.9 s in all; then `boom1` or
  `boom2`, one at random, starts playing and the planet picture is redrawn. The Fast
  Tactical Combat switches do not shorten this. When the fight ends, "Victorious!"
  appears in yellow at (190,264), beside Defender, when no invader is left, or at
  (190,378), beside Attacker, when the planet fell; nothing appears after a stalemate
  (both sides left when the round limit is reached). Close then lights up.
- **Round counter.** "Ground Combat Turn" in #7D9FFF at (580,10), and "N/M" in white
  right-aligned to x 760: N the rounds fought so far, M the `Number Of Ground Combat
  Turns` setting. It is drawn from the start, so it reads "0/10" before Begin.
- **Log.** Whether the window is shown or not, the two empires get their ground combat
  log entries when the fight is over (spec 04 §13), except in the simulator.
- **Layout.** "System X" at (270,10), "Coordinates (x,y)" at (450,10). Planet picture at
  (12,40), its name at (160,40). Labels Type, Atmosphere, Conditions, Value, Population at
  x 170 (y 60, 90, 120, 150, 180) with values at x 180; Population adds "(Domed)" for a
  domed colony. "Facilities" at (350,60) over the facility grid at (350,75), 218×146.
  "Defender" at (40,264) over its units at (40,279), 504×72; "Attacker" at (40,378) over
  its units at (40,393), 504×72.

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

Exact regions, top-left corner and size, in frame pixels (confirmed: binary). At
1024x768 on a larger desktop everything is shifted by ((screen width − 1024) / 2,
(screen height − 768) / 2).

| Region | 800x600 | 1024x768 |
|---|---|---|
| Status bar | (11,5) 778x24 | (11,5) 1002x24 |
| Command panel | (11,34) 485x72 | (11,34) 1002x72 |
| System panel | (8,113) 484x484 | (8,113) 652x652 |
| Report / list panel | (499,34) 290x361 | (667,109) 290x361 |
| Galaxy panel | (503,404) 285x190 | (671,479) 342x284 |

At 1024x768 the command panel runs the full width and the report panel moves below it.

Frame pieces live in `Pictures/Game/Screens/800X600/` and `.../1024X768/` [M]. They are
drawn 1:1, black transparent, with their top-left corners at the positions given
(confirmed: binary; the 1024x768 positions match spec 07 §UI):

| Piece | 800x600: size, position | 1024x768: size, position | Role |
|---|---|---|---|
| Top | 800x5 at (0,0) | 1024x5 at (0,0) | top edge of the window |
| Toptitle | 800x5 at (0,29) | 1024x5 at (0,29) | under the status bar |
| Topsys | 499x9 at (0,106) | 1024x11 at (0,106) | top edge of the system panel (full width at 1024) |
| Topgal | 306x9 at (494,395) | 362x9 at (662,470) | top edge of the galaxy panel |
| Left | 11x600 at (0,0) | 12x768 at (0,0) | left border |
| Middle | 13x571 at (490,29) | 16x662 at (655,106) | divider between system panel and right column |
| Right | 12x600 at (788,0) | 67x768 at (957,0) | right border; at 1024 a 67 px strip |
| RightFiller | 1x1 at (0,0) | 62x365 at (958,107) | filler inside that strip (1024 only) |
| Bottom | 800x6 at (0,594) | 1024x8 at (0,760) | bottom border |
| Intro | 800x600 | 1024x768 | intro screen background |
| Starmap | 490x490 (4-bit) | 660x660 (4-bit) | legacy system background |

What fills the 67 px strip at 1024x768 is answered in spec 07 §UI (the selectors and the
filler).

### 2.2 Status bar

Left to right: large empire flag, empire name, emperor title and name, "Game Date" (the
game starts at 2400.0 and each turn adds 0.1), then stored minerals, organics and
radioactives, each followed by its resource icon (blue crystal, green organic, red
radiation symbol). At the far right a minimize button, and during the tutorial a "T"
button that re-opens the tutorial text [T][S].

### 2.3 Command panel

Three groups, left to right [T][S]:

1. **Command buttons**: 12 buttons of 34x34 in two rows of six, from 2 px right of and
   below the panel's top-left corner. Top row: Game Menu,
   Designs, Planets, Colonies, Ships\Units, Construction Queues. Bottom row: Research,
   Empires, Log, Empire Status, Help, End Turn. They match F2..F11, F1 and F12 (§3).
   Pressing End Turn disables the main window's panels until every computer player has
   moved. A left-click plays `cmdbtn` when sound is on (confirmed: binary).
2. **Order buttons**: 40 order places with fixed positions (the order and cells are in
   spec 07 §UI), filled column by column, top then bottom (confirmed: binary). The order
   area starts at (230,36), 219 px right of the panel's left edge and 2 px below its top;
   it is (columns × 34 + 32) px wide and 68 px tall, with a 16 px pager half at each end,
   so the buttons start at x 246.
   - 800x600: 5 columns per page, so 4 pages (places 1–10, 11–20, 21–30, 31–40). The
     arrows wrap: left on page 1 goes to page 4, right on page 4 to page 1.
   - 1024x768: all 20 columns on one page; the arrows are drawn dim and do nothing.
   - The page never follows the selection; only the arrows change it.
   - A button is lit only when the selection can execute the order (§2.8). A left-click
     on a lit button plays `ordbtn` when sound is on and runs the order; right-clicks
     and dim buttons do nothing.
3. **Selection buttons**: three stacked "previous | icon | next" controls (48x24) cycling
   through ships, fleets and colonies, at the panel's top right: 48 px left of its right
   edge, at (448,34) at 800x600 and (965,34) at 1024x768 (confirmed: binary). In
   turn-based games the ship cycle visits ships with
   movement left; in simultaneous games, ships without orders. Empire options can make
   the cycle skip ships under construction, damaged ships or ships in fleets, or stop
   once per location [T][S] (confirmed: binary).

**Hover hint** (confirmed: binary). Hovering a command or order button writes two
centred lines of white text, with no box or border, straight over the system picture in
a 300x30 area centred on the system panel's width, 10 px below its top edge (x 100–400
at 800x600, x 184–484 at 1024x768; y 123–153). The first line is the button's name in
the button font; the second, at +18 in Futurist small, is its key in brackets, such as
"(Key M)", "(Ctrl W)" or "(Key F2)". It appears at once, changes as the pointer moves
from button to button, and is erased when the pointer leaves.

### 2.4 System panel

A 13x13 sector grid (coordinates 0..12; the star normally sits in the centre square and
"rings" 2-7 lie 1-6 squares out) [M: SystemTypes.txt] drawn over a per-system-type
background image. The system name is drawn at the top left.

**Geometry** (confirmed: binary). The panel is at (8,113) in both layouts (§2.1). The 13
cells sit inside it after a margin:

| Layout | Panel | Cell | Margin | Sector (c, r) top-left, frame pixels | Sector centre |
|---|---|---|---|---|---|
| 800x600 | 484x484 | 36 px | 8 px | (16 + 36c, 121 + 36r) | (34 + 36c, 139 + 36r) |
| 1024x768 | 652x652 | 50 px | 1 px | (9 + 50c, 114 + 50r) | (34 + 50c, 139 + 50r) |

- A click maps to column (x − panel x − margin) div cell, and the same for the row;
  results outside 0..12 are ignored. (A click in the margin left of or above the grid
  therefore counts as column or row 0.)
- **Sprites are never scaled.** Every object sprite is drawn at its native 36x36,
  centred in its cell: offset 0 at 800x600, 7 px at 1024x768.
- **Background.** The system picture (490x490 or 660x660, §5.3) is copied 1:1 from its
  top-left corner; the rightmost and bottom 6 px (800x600) or 8 px (1024x768) are never
  shown. An unexplored system shows `Starmap.bmp` with the word "Unexplored" centred at
  one third of the panel's height.
- **Grid lines** only with the empire option "system grid" (§1.9): 14 vertical and 14
  horizontal 1 px lines on the cell boundaries, in RGB(21,32,59).
- **Black as transparent.** When the system type has `Mask Background Objs` TRUE, every
  sprite (planets, stars, storms, warp points, comets, ships) is drawn with black
  transparent. When it is FALSE, each sprite is copied as an opaque 36x36 square, black
  included, except at 800x600 with the system grid on, where black is always
  transparent so the lines show through.
- **Headings** (confirmed: binary). The minis of hulls with `Requirement Uses Engines :=
  True`, and fighter and drone groups, are turned to the object's heading; bases and other
  engineless hulls, satellites and mines are always drawn upright. A heading is one of 8
  directions in 45° steps clockwise from "up"; a new ship faces up. Each move within a
  system sets it to the bearing from the old square to the new one, rounded to the
  nearest 45° (23–67° gives 45°, 68–112° gives 90°, and so on; 338–22° is up). Arriving
  through a warp point keeps the old heading. The sprite is turned clockwise by that
  angle, sampling each pixel nearest-neighbour with no smoothing (45° views look
  jagged); pixels from outside the source come out black, so transparent.

Contents [T][S]:

- Planets, asteroid fields, the star(s), storms and warp points as 36x36 sprites. A warp
  point that the empire has travelled through shows the destination system's name under
  it.
- A colonised planet has small population bars at its top right, coloured by owner.
- Ships: a single-owner stack shows one ship sprite with a count in the bottom-right
  corner; a location with several empires, or ships orbiting a planet, shows small empire
  flags instead. The counts are drawn in the owner's empire colour (§5.3), next to each
  flag when there are several (confirmed: binary).
- A cloaked ship gets a ring around its 36x36 cell in its empire's colour (dark cyan,
  RGB(0,128,128), if it has none) (confirmed: binary).
- Colonisation hint: a small green star on a planet means colonisable and breathable; red
  means colonisable but would be domed; no star means not colonisable by this empire.
- The selected location is framed by four small yellow corner arrows.
- Waypoints 1–10: a cyan rectangle around the sector with the number in cyan. A cyan "M"
  marks the sectors of a second list of locations, probably the tagged minefields of
  Ctrl+T / Ctrl+R (confirmed: binary; the meaning of "M" is inferred).
- Optional movement lines (Ctrl+L): a line from a moving ship to its destination with a
  small circle per movement point and the number of turns until each point is reached.
- Nebula and black-hole systems use a full background picture instead of a star field.

Clicks (left and right behave the same): one object → its report in the right panel;
several objects → a list; empty space → a report about the whole system. Only visible
objects count: with exactly one, its report opens at once and its orders light up; with
several, the list opens and every order stays dim until a row is chosen (confirmed:
binary).

### 2.5 Ship list / report panel

Shows either a list of everything at the selected location, or a single report (ship,
planet, storm, fleet, system). Each list row has the object's 36x36 picture, its name,
for ships the class (design) name, and for own objects a row of status icons on the
right. Clicking a row replaces the list with that object's report and enables the order
buttons; an up-arrow button at the top right of the report returns to the list. Shift+click
tags several ships (a green arrow is drawn on each) so one order goes to all of them;
the group dissolves after the order; Shift+A tags all, Shift+C clears [T]. Tagging a ship
that is in a fleet tags the whole fleet; Shift+A tags every own object in the list, with
fleets expanded; clicking another sector clears the tags (confirmed: binary). The orders
a tagged group can take are in §2.8.

Status icons in a list row are drawn in 20 px steps from the row's right edge leftwards,
6 per row, then a second row (confirmed: binary; §4.4).

### 2.6 Galaxy panel and Galaxy Map window

The panel shows the whole quadrant over a faint grid (one square is about 10 light
years). Systems are small circles, warp connections are lines [T]. Exact drawing,
colours as RGB (confirmed: binary):

| Element | Drawing |
|---|---|
| Background | black |
| Grid | always 68 × 47 cells, whatever the quadrant size: cell width = panel width div 68, height = panel height div 47; lines (21,32,59). In the panel only with the empire option Show Grid Lines (§1.9); in the Galaxy Map window always |
| System | a circle filling its cell, black inside |
| Never explored | ring (126,126,126) |
| Explored, nobody seen there | ring (238,238,238) |
| One empire seen there | ring in that empire's colour |
| Two or more empires | solid triangle (bottom-left, bottom-right and top-middle of the cell) in the viewer's colour if the viewer is among them, otherwise in the colour of the highest-numbered empire present |
| System shown in the system panel (panel only) | the symbol filled with its colour, plus a ring 2 px outside the cell |
| Warp lines | (165,176,179), drawn only from explored systems; a link to an unexplored system is a stub two grid cells long. In the panel only with Show Warp Lines; in the Galaxy Map window always |
| Hovered system | cyan (0,255,255) ring 2 px outside the cell; its name in cyan at the first corner that fits: above-right, below-right, above-left, below-left |

Hovering shows the name of the nearest explored system; left-click selects a system;
right-click opens the **Galaxy Map window** (780x475): a larger map with overlay
buttons Presence, Avoid, Ally Claimed, Enemy Claimed, Spaceports and Resupply Depots,
Goto System (list of seen systems; picking one closes the map and shows it), Show
Distances (light-year distances from the hovered system), Show Names, Close. Clicking a
system there edits its free-text player notes; the hovered system's notes show at the
bottom [T][S].

The window's map is 544x376, and Presence is the default tab. Overlays (confirmed:
binary):

- **Presence** draws the symbols of the table above. Every other tab draws only the
  neutral base rings, (126,126,126) and (238,238,238), and then its own marks.
- **Avoid**: avoided explored systems get a ring in the viewer's own empire colour (the
  manual says yellow).
- **Spaceports / Resupply Depots** (the `Spaceport` and `Supply Generation` abilities):
  where the viewer has a colony, a green (0,128,0) ring if a facility there has the
  ability, otherwise yellow (255,255,0).
- **Ally Claimed / Enemy Claimed**: counts the systems' claimants among the viewer's
  allies (and the viewer) or among its enemies. None: ring (238,238,238); one: ring in
  that empire's colour; two or more: a yellow (255,255,0) filled disc with an outer ring.
- **Show Names**: every explored system's name in cyan.

The Systems To Avoid window uses the same four schemes, and the Borders window the
Claimed scheme over the empires picked with Select All, Allies, Enemies or Us.

### 2.7 Turn flow as the player sees it

1. Turn starts; the Log window opens automatically if there are new entries and the
   option is on.
2. The player selects objects and issues orders. In turn-based games movement happens
   immediately; entering a sector with enemies asks the Attack Sector question, and
   combat asks Tactical or Strategic.
3. End Turn (F12, with an optional confirmation) locks the panels while the AIs play.
4. In simultaneous games, orders are only recorded; after the host processes the turn
   the player can replay the movement log in the system panel (Ctrl+P/O/I/U).

### 2.8 When order buttons are lit

All of this subsection is (confirmed: binary).

**General rules.**

- Every order button is dim while the main window is locked: while End Turn is being
  processed, with no game loaded, and on the host's between-turns screen in a
  simultaneous game.
- The four movement-log buttons are lit in every simultaneous game, whatever is
  selected, and always dim in turn-based games.
- Every other button depends only on the object whose report the right panel shows, or
  on the tagged group. All are dim when nothing is selected; when the panel shows a list
  of several objects, a system report, a star, a storm or a warp point; and when the
  object belongs to another empire.
- A hotkey runs its order only when that order's button is lit.

**Own ship, not mothballed.**

| Order | Lit when |
|---|---|
| Scrap, Change Name, Fleet Transfer, Drop Cargo, Cargo Transfer, Toggle Minister Control | always (Cargo Transfer in both turn modes) |
| Launch\Recover Units | turn-based games only; no cargo check |
| Clear Orders, View Orders, Repeat Orders | the ship has orders |
| Move To, Explore, Resupply, Repair, Set Patrol, Attack | speed above 0 (Attack even when unarmed) |
| Warp | speed above 0 and a hull that can warp |
| Move To Waypoint | speed above 0 and at least one of the empire's ten waypoints is set |
| Colonize | a Colonize Planet ability (rock, ice or gas) |
| Build Queue | a space yard, and not cloaked |
| Load Cargo, Recover Units Remotely | cargo capacity above 0 |
| Launch Units Remotely, Jettison Cargo | the cargo holds anything |
| Sweep Mines | the `Mine Sweeping` ability |
| Sentry | supplies not below `Supply Amount for Low Supply Warning` |
| Stellar Manipulation | any stellar-manipulation ability |
| Cloak / Decloak | can cloak (a cloaking component and supplies above 0) and is not cloaked / is cloaked |
| Use Component | `Emergency Resupply` or `Emergency Energy` |
| Change Formation\Strategy | never, for a single ship |

**Own ship, mothballed:** only Scrap, plus Clear Orders and View Orders when it has
orders.

**Own fleet.**

| Order | Lit when |
|---|---|
| Change Name, Fleet Transfer, Change Formation\Strategy, Scrap, Load Cargo, Drop Cargo, Cargo Transfer, Sentry, Launch Units Remotely, Recover Units Remotely, Toggle Minister Control | always |
| Launch\Recover Units | turn-based games only |
| Move To, Warp, Explore, Resupply, Repair, Set Patrol, Attack | fleet speed above 0 |
| Move To Waypoint | fleet speed above 0 and a waypoint is set |
| Colonize | any member can colonize |
| Clear Orders, View Orders, Repeat Orders | any member has orders |
| Cloak / Decloak | any member can cloak and is not cloaked / any member is cloaked |

**Own colony.**

| Order | Lit when |
|---|---|
| Change Name, Cargo Transfer, Build Queue, Scrap, Toggle Minister Control, Abandon Planet | always (every colony has a queue) |
| Launch\Recover Units | turn-based games only |
| Clear Orders, View Orders, Repeat Orders | the colony has orders |
| Jettison Cargo, Launch Units Remotely | the cargo holds anything |
| Recover Units Remotely | cargo capacity above 0 |
| Scrap Facilities | the colony has facilities |
| Cloak, Decloak | as for ships |
| Use Facility | the planet or its facilities give `Emergency Resupply` or `Emergency Energy` |
| Convert Resources | the planet or its facilities give `Resource Conversion` |

Abandon Planet stays lit whatever the population, and the homeworld is not special.
After the player confirms, a message refuses the order when the population is above
`Maximum Population For Abandon Planet Order`; otherwise, if the planet has facilities,
the game asks whether to scrap them.

**Unit groups in space.**

| Group | Lit |
|---|---|
| Fighters | always Fleet Transfer, Scrap, Minister; when mobile, Move To, Explore, Resupply, Repair, Set Patrol, Attack, Move To Waypoint (never Warp); with orders, Clear, View and Repeat Orders; Sentry when not low on supplies; Cloak and Decloak as for ships |
| Satellites | Scrap, Minister; Clear and View Orders when it has orders; Cloak and Decloak |
| Mines | Scrap, Minister; Clear and View Orders when it has orders |
| Drones | Scrap, Minister, Attack; View Orders when it has orders; Cloak and Decloak |

**Tagged group** (Shift+click, Shift+A; §2.5). Lit whatever the members' abilities:
Move To, Warp, Explore, Resupply, Repair, Set Patrol, Attack, Scrap, Minister, Clear
Orders, and Move To Waypoint when a waypoint is set. Cloak only if every member can cloak
and none is cloaked; Decloak only if every member is cloaked. If any tagged object is a
drone group, only Attack and Minister are lit.

---

## 3. Hotkeys

Hotkeys are listed in the Help window's Hotkeys tab and in the Readme [T][M]. The
executable binds every key below; the five Ctrl keys marked (confirmed: binary) were
missing from the manual's lists. A main-window key works only while no window is open
and the turn is not being ended or the movement log replayed, and an order key only
when its button is lit (§2.8). The main window has no Esc or Enter binding (confirmed:
binary).

### 3.1 Main window: orders

| Key | Order | Notes |
|---|---|---|
| M | Move To | then pick a destination in any system |
| Ctrl+W | Move To Waypoint | opens Select Waypoint (confirmed: binary) |
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
| T | Cargo Transfer | the manual says not in simultaneous games; the executable allows it in both (§2.8) |
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
| Ctrl+Z | Use Component | (confirmed: binary) |
| Ctrl+J | Use Facility | (confirmed: binary) |
| Ctrl+K | Scrap Facilities | (confirmed: binary) |
| Ctrl+Y | Toggle Minister Control | (confirmed: binary) |

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
| Ctrl+U | Movement log replay, the button the executable names "for all ships" (the manual says follow ship) |
| Ctrl+L | Toggle ship movement lines |
| Ctrl+S | Toggle sound effects (the Sound On switch of §1.9; music is not touched) |
| Ctrl+H | Show tutorial / scenario window |
| Shift+A / Shift+C | Tag all / clear tagged ships in the ship list |
| Shift+click | Tag one ship in the ship list |

### 3.3 Tactical combat window

| Key | Action |
|---|---|
| Alt+1..9 | Make selected ship leader of group # |
| Ctrl+1..9 | Make selected ship member of group # |
| Alt+0 or Ctrl+0 | Clear the selected ship's group marks (confirmed: binary) |
| L | Open Launch Units for the selected ship |
| T | Drop troops at once on the adjacent colony (§1.10.2) |
| R | Ram ship (then click the target) |
| C | Capture ship (then click the target) |
| E | End combat turn, once the battle has begun (manual only; absent from the Readme) |
| Space or Ctrl+N / Ctrl+B | Next / previous ship with movement |
| Ctrl+F / Ctrl+D | Next / previous ship that can fire |
| Shift+A / Shift+C | Select all / clear weapons |

Esc does nothing in the tactical window (confirmed: binary).

### 3.4 Keys in dialogs

Every window opens modally. Keys each kind of window handles (confirmed: binary):

| Window | Keys |
|---|---|
| Large dialogs (the 780-wide family: Log, Planets, Options and the rest) | Esc or Enter closes when the bottom button is Close; Esc closes when it is Cancel; otherwise no keys |
| Message boxes | Esc or Enter = OK. In Yes/No prompts Esc or Enter = **No**, Y = Yes, N = No. In a prompt offering Tactical and Strategic, T and S pick them |
| Text prompts (Change Name and the like), the folder chooser, Create Fleet, galaxy-map notes | Esc cancels, Enter accepts (notes: saves the text) |
| Game Menu | Esc = Close |
| Report windows (scenario text, package, general and design reports, population, object picker, orders list, design-name picker) | Esc closes |
| Battle notices with a Begin button | Esc and Enter both mean Begin |
| Ending, Next Player | Esc or Enter continues |
| Player sign-in | Enter submits |
| Combat Replay | Space = Next, Esc closes |
| Tactical Combat | §3.3; no Esc |
| Intro | any key or click skips the opening logos |
| TCP/IP host and player | Enter sends the chat line |
| Selection pickers, check lists (Players, Scrap Facilities), Reorder lists, setup screens, ship and planet report pop-ups | no keys at all |

Discrepancies: the Readme calls the Ctrl+P/O/I/U family "Phase Replay". Our client binds
every key of §3.1 and §3.2, rebindable in Settings → Controls, and Help → Hotkeys lists
them as bound. It keeps a few of its own: Enter also ends the turn (and finishes a
patrol route), Esc clears a pick or the selection, Ctrl+Comma opens Settings, Alt+Enter
switches full screen, Ctrl+H shows the lesson panel and Shift+F1 the manual page of the
window in front (§7 Q4, Q16).

---

## 4. Messages, log and status icons

### 4.1 Log window

Layout (780x475) [S][T]: top left, the **Log Messages** list (one row per entry with a
small lamp); bottom left, a galaxy mini-map that highlights the
entry's system; centre, **Log Details** with the game date, the entry's 128x128 picture,
its title, its date, and a body. Right column: category filters **All, Construction,
Research, Intelligence, Events, Politics, Combat, Misc**, then **Send Reply**, **Combat
Replay** (labelled "Combat Report" in the capture), **Constr. Queues**, **Goto** and
**Close**.

Exact rules (confirmed: binary):

- **Positions.** "Log Messages" at (17,40) and "Log Details" at (296,40) in label blue;
  "Game Date:" at (450,40) in label blue with the date at (530,40) in white Futurist
  Medium. Message list at (17,57), 272×207; mini-map at (17,272), 272×190; details at
  (296,57), 278×405. Button column: All, the seven filters, one empty slot, Send Reply,
  Combat Replay, Constr. Queues, Goto, Close.
- **Rows** are 16 px tall. Each starts with a 13×13 lamp from `General.bmp`: green (the
  cell at (190,0)) for the selected entry, blue (the cell at (177,0)) for every other
  row. The colour does not show category or read state. The title starts 17 px in and is
  cut to the list width. The row under the pointer gets the `RowGrid.bmp` texture
  behind it.
- **Which entries.** Only the current turn's entries; entries made during the player's own
  turn are stamped with the current turn when the Log opens. There is no view of earlier
  turns. Entries are listed in the order they were made, without sorting.
- **Filters.** All is always enabled; a category button is disabled when the turn has no
  entry of that category. Each filter click is stored with the empire (so saved with the
  game) and restored on every later opening; if that category has no entries now, the
  window opens on All.
- **Selection.** When the Log closes, and when Goto is pressed, the window stores the
  selected entry's position in the list and the scroll position with the empire. The
  next opening restores both if the position exists in the filtered list, otherwise
  selects the first row. Since a position is stored, a later turn re-selects whatever
  entry now sits there.
- **Send Reply** is enabled only for diplomatic messages. It opens Communicate addressed
  to the sender, or shows a "Cannot Reply" notice when a message already went to that
  empire this turn.
- **Combat Replay** is enabled only for combat entries, and only when Settings.txt
  `Create Combat Replay` is TRUE. When the replay closes, a new background track starts.
- **Constr. Queues** is always enabled and opens Construction Queues over the Log.
- **Goto** is enabled when the entry has a target. A location target closes the Log and
  shows that system in the main window with the sector selected. A window target opens
  Construction Queues, Research, Intelligence, Empire Options, Designs or Empires over
  the Log, which stays open.
- **Details.** A normal entry shows its picture, its title, "Date:" and its text. A
  combat entry shows its picture and "Combat in <system>" in the button font; "Date:"
  at +4 with its value at +40, "Coord:" at +132 with its value at +180 (labels in label
  blue, values white); then the headings **Combat Forces** (x +4) and **Damage** (x 200)
  over a list of 20 px rows: per empire in the battle a row with its flag and name, then
  one row per ship, unit group and planet with its damage as a percentage, or "Dead"
  (destroyed), or "Taken" (captured). (The manual describes Start and Lost counts here;
  the executable draws damage instead. Current and lost counts are what the Strategic
  Combat window shows, §1.10.5.) Political messages have their
  own layout unless `Use Old Log Political Message Display` is on.

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
  manipulation, deleting research/intel projects, removing the first item of a
  construction queue) (confirmed: binary); always-on prompts for surrender, quitting as
  TCP/IP host or player [S][M]. In every Yes/No prompt, Esc and Enter mean No (§3.4).
- **TCP/IP chat** with a sound on receipt [M].

### 4.4 Status icons

20x20 icons drawn under report portraits, at the right of ship-list rows and in the
Colonies "Status" tab [T]. They are the bottom two rows (19 icons each, y = 136 and 156)
of `Pictures/Game/General.bmp`, row-major; the manual's own icon images are numbered so
that **cell = number − 1** [M]. Cells 0–18 are the y = 136 row, cells 19–37 the y = 156
row.

The executable draws these cells, in this order, and only these (confirmed: binary). Ship
icons appear only on the viewer's own ships; planet icons, except ruins, only on the
viewer's own colonies.

**Ships and bases:**

| Order | Cell | Shown when |
|---|---|---|
| 1 | 5, else 4 | out of supplies (supplies 0); else low supplies (below `Supply Amount for Low Supply Warning`, 1000 in stock data). Not for mothballed ships |
| 2 | 33 | has damaged components |
| 3 | 16 | damaged, and a repair source of the same owner is in the sector |
| 4 | 3 | the first order is Sentry |
| 5 | 9 | cloaked |
| 6 | 0, else 11 | has a space yard; otherwise 11 if it has `Component Repair` |
| 7 | 2 | repeat orders on |
| 8 | 10 | under minister control |
| 9 | 6 | mothballed |
| 10 | 12 | building, with a non-empty queue |
| 11 | 25, 18, 20, 19, 26, 37, 22 | cargo, in this order: troops, fighters, mines, satellites, weapon platforms, drones, population |
| 12 | 36 | remote mining: the design has `Remote Resource Generation`, the sector holds an uncolonized planet or asteroid field, and this is the first such miner in the sector |

**Planets:** cell 9 cloaked; 0 space yard; 10 minister control; 12 building, with a
non-empty queue; 15 `Ancient Ruins` or `Ancient Ruins Unique` (shown on any planet); 17
domed (the population cannot breathe the atmosphere); 11 can repair, only when there is
no space yard; then the seven cargo cells in the ship order; 30 not connected (the empire
has a spaceport-type facility somewhere and none serves this colony, so its resources are
not delivered). The planet report also draws the domed icon at (230,196).

**Fleets:** cell 10 (minister), then 9 (cloaked).

Drones in cargo use cell 37 (the manual's drone image is that cell, not 34). Cells 1, 7,
8, 13, 14, 21, 23, 24, 27, 28, 29, 31, 32, 34 and 35 are never drawn.

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
  `Starmap.bmp` frames [M]. There is no alpha channel; backgrounds are pure black.
  The game draws with plain copies only: a sprite is either copied opaque or drawn with
  exact black, RGB(0,0,0), transparent. There is no additive or alpha blending anywhere,
  not for explosions, shield hits, beams or torpedoes either (confirmed: binary). Which
  sector sprites are keyed depends on `Mask Background Objs` (§2.4).
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
| Beam graphics | `Pictures/Combat/Beams.bmp`, 200x40 = 10 × 2 of 20x20 (16 used) | `Weapon Display − 1` (values 1..16 in data) (confirmed: binary) | — | Components.txt `Weapon Display` when `Weapon Display Type := Beam` |
| Torpedo graphics | `Pictures/Combat/Torps.bmp`, 200x80 = 10 × 4 of 20x20 (35 used) | `Weapon Display − 1`; 0 means no picture (only warheads use 0 in stock data) (confirmed: binary) | — | same, `Torp` |
| Seeker graphics | race `_Main.bmp` slots 4-6 | the 20x20 slot at x = 40 + 20 × `Weapon Display` (0..2) of the owner's `_Main.bmp`; a piece standing for several seekers shows the count at its bottom right (confirmed: binary) | — | same, `Seeker` |

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
  torps as above [M]. The executable never loads `Shields.bmp` or `BigExplosions.bmp`:
  shield hits and big explosions always come from the race's own `_Shields.bmp` and
  `_BigExplosion.bmp`, falling back to `RaceGeneric` (confirmed: binary). Beams are
  drawn by stamping the centre 10x10 of the 20x20 cell every 6 px along the path, turned
  to the heading, then erased; a torpedo is its cell turned to the exact bearing of its
  target (whole degrees) and moved along the path; explosions and shield hits are
  8-frame animations drawn in place (confirmed: binary).
- **Races/<Style>/** (20 styles, about 81 files each) [M]:
  - `<Style>_Mini_<Bitmap>.bmp` 36x36: map sprite. Ships, carriers, transports and
    fighters are drawn top-down facing up and rotated by the engine to the heading;
    bases, satellites, mines, troops, platforms, drones and group icons are
    non-directional [T]. The executable turns the minis of hulls that use engines and of
    fighter and drone groups, in 8 steps of 45° (§2.4) (confirmed: binary).
  - `<Style>_Portrait_<Bitmap>.bmp` 128x128: isometric portrait, bow towards the lower
    left [T].
  - `<Bitmap>` is `Primary Bitmap Name` from VehicleSize.txt, falling back to
    `Alternate Bitmap Name` (e.g. Baseship falls back to Starbase) [M]. There are 35
    names, covering every hull class plus Fleet, FighterGroup, MineGroup and
    SatelliteGroup group icons.
  - `<Style>_Main.bmp` 100x20, packed strip [T][M]: x 0-25 large flag (26x18); x 26-39
    small flag (14x10) above a solid **empire-colour swatch** (14x10); then three 20x20
    projectile sprites: seeker, capital-ship missile, plasma torpedo. **The empire's
    colour is the pixel at (28,13)** of this file, found the usual way (the race folder,
    the neutral folder for neutrals, then `RaceGeneric`). It is read when the game is
    created and on every load; players cannot choose a colour, and two empires with the
    same style get the same flag and colour (confirmed: binary). It colours the galaxy
    symbols and overlays (§2.6), ship counts in the system panel, the ring around a
    cloaked ship, and tactical firing lines.
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
    exists there; the rest are in `Systems/<Res>` [M]. The executable reads them only from
    `Systems/<Res>/`, adding ".bmp" when the name lacks it (confirmed: binary).
  - `<Name>.bmp` 128x128: one per background, the picture of the System Report (§1.4)
    (confirmed: binary).
  - `<Storm1..5|Asteroids1>Tile<n>.bmp` 72x72 (9, 5, 5, 7, 10 and 9 tiles): the
    background of the tactical combat and combat replay maps (confirmed: binary). The
    game builds a 432x432 picture from 6×6 tiles, each chosen at random among the files
    matching `Systems/<Name>Tile*` (at most 100), and paints combat square (x, y) with
    the 36x36 part at ((x mod 12) × 36, (y mod 12) × 36), opaque, so the picture repeats
    every 12 squares. `<Name>` is the system's `Background Bitmap` without ".bmp" when
    the system type has `Mask Background Objs` TRUE and `<Name>Tile1.bmp` exists (in stock
    data, the Storm 1–5 types); otherwise the `Combat Tile` of a storm or asteroid field
    in the battle's sector, if any. Failing both, the background is the top-left 432x432
    of `Systems/1024X768/Starmap.bmp`, at either resolution. The system's own
    background (nebula, black hole...) never appears in combat.
  - `Non-Tiled Center Pic` is read in one place only: when TRUE, the 384x384 preview in
    the Stellar Manipulation window shows the plain starfield instead of the system
    background. Combat ignores it, whatever the data file's comment says
    (confirmed: binary).
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
    selection frame; `StartMenu.bmp` 736x536: background of the launcher window added in
    1.95 (§1.1).
  - `General.bmp` 384x176: irregular atlas: 16 px resource/research/intel/population
    icons (y=0), 32 px versions (y≈32), coloured indicator lamps and small arrows,
    green/red colonisation stars, two 8-frame 36 px explosion strips (y≈64, y≈100), a
    dome overlay for domed colonies, and the status icons (§4.4). Needs a hand-written
    coordinate table.
  - `Finale/`: `Victory.bmp`, `Lose00N.bmp`, `HumnDead.bmp` (426x341 or 400x300),
    chosen through Settings.txt `Finale ... Picture N`.
  - Logos: `Se4logo.bmp`, `Mmlogo.bmp`, `SFI.bmp`; `Backgrd.bmp` 20x20 tile.
  - Cursors, 32x32 `.cur`: `Normal` (hotspot 2,2), `Select`, `Target`, `Hourglass`,
    and `ArrowN/NE/E/SE/S/SW/W/NW` (hotspot 15,15), the tactical-map move pointers. Where
    each is used: §1.10.1 (confirmed: binary).

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
- UI and event sounds are not referenced by data; the engine picks them by name [M].
- **Playback** (confirmed: binary). One effect plays at a time; a new one cuts off the
  one playing. A few make the game wait until they end (marked "waits" below). The
  per-computer Sound On switch (§1.9; Ctrl+S flips it) gates every effect. Classic Sound
  Effects on reads `Sounds/`, off reads `Sounds/New/`.

| Sound | When (confirmed: binary) |
|---|---|
| `button` | mouse press on a text button in a dialog's button column |
| `cmdbtn` | left-click on a main-window command button |
| `ordbtn` | left-click on a lit order button; a TCP/IP host or player sends a chat line |
| `close` | a large dialog, picker, check list, reorder list, population window or setup screen closes, by any means |
| `endturn` | the turn is ended (after the optional confirmation) |
| `cloakon` / `cloakoff` | Cloak / Decloak order (waits); `cloakon` also when a TCP/IP chat line arrives |
| `openwp` / `closewp` | Open / Close Warp Point takes effect; on a TCP/IP host, a player connects / leaves |
| `crtestrm` | Create Storm takes effect |
| `deststrm` | Destroy Storm, Destroy Nebula or Destroy Black Hole takes effect |
| `crteplnt` | Create Planet, or ringworld / sphereworld construction, takes effect |
| `destplnt` | Destroy Planet takes effect (waits) |
| `crtesun` | Create Star takes effect |
| `destsun`, then `boom3` | Destroy Star, Create Nebula or Create Black Hole takes effect (waits for `destsun`) |
| `destsun` | TCP/IP alert, played once: the host is ready to generate the turn, or a player needs to take a turn; the game window is also restored if minimized |
| `boom3` | a combat piece is destroyed (tactical combat and replay) |
| `boom1` / `boom2` | a damaging hit in combat (`boom1` for less than 4 damage, `boom2` otherwise); one of the two at random for each ground combat round; `boom1` also with the small explosion animated over a system-panel sector |
| Components.txt `Weapon Sound` | the weapon fires (tactical combat and replay) |
| `cannon`, `smrtbomb` | never played by the executable (only a data file could name them; the stock data does not) |

  Stellar-manipulation sounds play only when the player can see the effect. The
  Strategic Combat window plays no sounds.
- `Music/`: 15 MP3 tracks named `Space Empires IV - Track NN.mp3`. Settings.txt defines
  three playlists by file name [M]: intro (1 track), background (8), combat (6). Music
  volume is a game option [M].
- **Music rules** (confirmed: binary):
  - A list entry is `<List> Song N Filename`. When it is missing, the older `<List> Song
    N` key gives a CD track number T and the file is the `Track` MP3 numbered T − 1 (two
    digits). A track plays only when `Allow CD Music` is TRUE and the file exists.
  - Each switch below picks one random track from its list and loops it; there is no
    stepping through a playlist.
  - Intro: when the intro screen opens.
  - Background: on Resume Game, Load Game, Tutorial and Scenario from the intro screen.
    New Game and Quick Start change nothing, so the intro track keeps looping into the
    game (inferred: no other code changes it). After each processed turn whose new turn
    number is a multiple of 5, a new random background track; on other turns, if nothing
    is playing, the current track restarts.
  - Combat: a new random combat track when the Tactical Combat window or a Combat Replay
    opens. Nothing switches back when tactical combat ends. When a replay opened from
    the Log closes, a new background track starts. The Strategic and Ground Combat
    windows do not change the music.
  - Options: closing an options window with music off stops it; turning it on starts a
    track if none is playing (background from the Options window, combat from Combat
    Options); a new volume applies at once.
  - Volume steps: Music Off, then 20, 40, 60, 80 and 100 % play at −30, −20, −10, −5 and
    0 dB.
  - The original draws the track from the same random generator as the game rules. Ours
    must use a separate source so that music never changes game results.

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
| `Dsgnname/*.TXT` | text, CRLF, Windows-1252 (some accented names) | One candidate design name per line, no header, alphabetical; 15 lists of 40–436 names with DOS 8.3 upper-case file names. The empire's "Design Name File" picks one; the designer offers names from it [M][T]. The player's name picker lists every line in file order; clicking one picks it, or the player types a name. Computer players and ministers take the next name no existing design uses; when the file runs out they reuse it with Roman numerals II to XV added; with no file, names are "Design <n>" (confirmed: binary). **Ship names** are the design name, a space and a four-digit serial with leading zeros, such as "Hood 0007"; the serial is one more than the highest serial among existing objects of that design (read from the last four characters of their names), so a number freed at the top is reused. The Combat Simulator numbers its ships with a counter per side instead (confirmed: binary). Manual captures show a bracketed hull `Code` after the serial [S]; the executable does not put it into the name (§7 Q18). |
| `Scenarios/<N>_Settings.txt` | text | Standard record format, one record: Name, Description, Starting Game [M]. |
| `Scenarios/<N>_Text.txt` | text | Records with `ID`, `Series`, `Segment` (order inside the series; previous/next browse a series), `Turn` (0 = first turn), `For Players` (player numbers), `Text Title`, `Text Number of Paragraphs`, `Text Paragraph <n>`, `Image` (a BMP in Scenarios/) [M]. |
| `Path.txt` | text | One record: `Using Mod Directory` [M]. |
| `Empires/*.emp` | **binary** | 3–4 KB, entropy about 7.2–7.6 bits/byte, no readable strings. The start looks like a type-tagged value stream (a tag byte, then 1, 2 or 4 bytes, or a length-prefixed string whose bytes are obfuscated) [M]. Holds a saved empire (name, race style, traits, experience, password...) [T]. Treat as opaque; import is out of scope. |
| `SaveGame/*.gam` | **binary** | Same tagged/obfuscated style (the tutorial `.gam` is 75 KB), except for one **plain ASCII fixed-width summary block** near the start: the format version (`1.58`), the date, the player count, `Turn Based`/`Simultaneous`, `Same Machine`/`Different Machines`, then per player a number, empire name, emperor title and name, and `Alive` [M]. This is likely what the load/login screens read without decoding the rest (inferred). |
| `*.plr`, `*.trn`, `*.cmb` | binary (inferred) | Simultaneous play: player orders sent to the host; turn movement log (for the replay); combat log (for combat replays) [T]. Names and places in §6.1 (confirmed: binary). |
| `History/plr_<N>_stats.txt`, `plr_<N>_events.txt`, `plr_<N>_log.txt` | plain text, fixed-width columns | Per-player statistics, history events and log copy; layout in §6.1 (confirmed: binary). The manual's `<game>_events.txt` / `<game>_stats.txt` are the copies kept next to a save. |
| `Maps/*.map` | binary (inferred) | Written by File → Save Map and the map editor; read by Game Setup → Load Map. Holds only the systems and their objects (confirmed: binary). |

For our engine: keep our own save format (deterministic state plus a command log, see
ENGINE.md). Reading `.gam`/`.emp` is not needed for parity and would mean reverse
engineering an obfuscated format; the one useful piece, the save list's
name/date/summary, can be shown from our own saves.

### 6.1 Files the game writes

All of this subsection is (confirmed: binary).

- **Folders.** At start-up the game creates `Backup`, `History`, `Maps`, `SaveGame` and
  `temp` in the install. With a mod active it uses the mod's folder of the same name
  when that exists.
- **Save Game** writes `<name>.gam` into the save folder: the Game Setup save-directory
  path when set and present, otherwise `SaveGame`. Every save also becomes the "last
  saved game" that Resume Game loads.
- **Save Map** writes `Maps/<name>.map`; **Save Empire** writes `Empires/<name>.emp`.
  Scenarios are listed from `Scenarios/*_Settings.txt`.
- **Autosave** writes `AutoSav<d>.gam` into the save folder, where d is the last digit
  of the turn number (ten rotating files), after turn processing whenever the turn
  number is a multiple of the chosen interval (§1.9).
- **Turn-based game on different machines.** After each player's turn the game is saved
  as `<game>_<player>_<turn>.gam` (plain numbers); the player is told the file name and
  asked whether to quit.
- **Simultaneous game, player.** End Turn copies the turn's `<game>.gam` to
  `temp/<game>_LastTurn.gam` (where the movement replay starts), then writes
  `<game>_<NNNN>.plr` into the save folder (NNNN = the player number in four digits).
  During the replay the current state is parked in `temp/<game>_CurrTurn.gam`.
- **Simultaneous game, host.** Copies every `.plr` into `Backup`; reads them (a missing
  file from a living human player on another machine raises a prompt, and if processing
  goes ahead the computer plays that empire for the turn); deletes the `.plr` files
  after processing; writes `<game>_Log.trn` (the movement log) and, when `Create Combat
  Replay` is on, `<game>_Combat.cmb`; saves `<game>.gam`, then the autosave if due.
- **`History/`** holds per player number N: `plr_<N>_stats.txt` and
  `plr_<N>_events.txt` (appended each turn), `plr_<N>_log.txt` (rewritten each turn, only
  when `Create Log Text Files for Players` is on), and `Current_Replay_Combat.cmb` (the
  combat replay of a turn-based game (inferred)).
- **Companion files.** A turn-based Save Game, an autosave, the host's saves and the
  per-player turn-based saves copy every `History/` file next to the save as
  `<save name>_<file>` (for example `Foo_plr_1_events.txt`), and the combat replay the
  same way when replays are on. A player's mid-turn save in a simultaneous game does
  not. Loading a game empties `History/` and refills it from the companions.
- **`temp/`** also holds `TempCombatFile.cmb` (scratch for combat replays),
  `GameSetup.dat` and `CurrEmp.dat` (received over TCP/IP; leftover `.dat` and `.emp`
  files there are cleared), and a debug log when debug settings ask for one. Delete Game
  also removes a file of the same name from `temp/`.
- **Layouts** (plain text, fixed-width columns):
  - stats: empire number (width 5), the date in tenths of a year (24001 for 2400.1, width 8;
    spec 05 §3.4), then 11 values of width 12
    in the Comparisons order: score, resources, research, intelligence, tech levels,
    systems, planets, population, units, ships, bases.
  - events: the date in the same form (width 8), the other empire (width 5), two flags (width 5, always
    0), a space, then the text (treaty made or broken, war, surrender, empire destroyed,
    contact made or lost).
  - log copy: a header row (date, header, text), a line of 78 dashes, then per entry the
    date padded to 9, the title padded to 40, and the body with line breaks turned into
    spaces.

---

## 7. Open questions to verify in the running game

Questions 2 and 4–23 were settled from the executable (code and form resources). Each
answer gives the result, points to the main section that now holds the details, and says
where our client differs. Only Q18 keeps an open part. Questions 24–48 are OpenSE4's
own choices (inferred) where those rules leave something open, in the windows brought in
line with Q8, Q11, Q16 and Q19–Q23; questions 49–55 are those of the main window, maps,
art, sound and files brought in line with Q2, Q4–Q7, Q9, Q10, Q12–Q15, Q17 and Q18.
Questions 30–40, those of the combat windows, were settled from the executable on
2026-10-01; each answer says where our engine or client differs. Q38 keeps a small open
part.

1. **1024x768 layout.** *Answered in spec 07 §UI (panel rectangles and frame strips).*
   Exact panel rectangles; what fills the 67 px right strip and
   RightFiller (more order buttons?); whether the layout follows the desktop resolution
   or an option.
2. **System grid pitch.** How does the 13x13 grid map onto the 490 and 660 px
   backgrounds, and are 36 px sprites scaled at 1024? **Answer:** neither background is
   divided by 13. The panel is 484x484 (800x600) or 652x652 (1024x768) at (8,113); its
   cells are 36 px after an 8 px margin, or 50 px after a 1 px margin. Sprites are never
   scaled: each is drawn 36x36, centred in its cell. The background is copied 1:1 from
   its top-left corner, so its right and bottom edges are cut off. An empire option
   draws grid lines. Details in §2.4 (confirmed: binary). The executable's sector
   centres at 1024x768, (34 + 50c, 139 + 50r), lie 1 px right of and 4 px below the
   centres measured on a capture in spec 07; use the executable's.
   Our client follows this at 1024x768 (the panel, the 1 px margin, the cropped
   background, 36x36 sprites at native size, the grid lines). It differs: it has only the
   1024x768 layout.
3. **Order icon map.** *Answered in spec 07 §UI (the 20×2 strip and every cell).*
   Which `Orders.bmp` cell is which order; what the extra icons are
   (59 used vs about 43 documented orders); which `Main.bmp` icon is the 13th.
4. **Order availability.** When is each order button enabled, how many order pages
   exist, and does the page follow the selection? **Answer:** §2.8 gives the rule for
   every button and every kind of selection (ship, mothballed ship, fleet, colony, each
   unit-group kind, tagged group); §2.3 gives the paging (confirmed: binary). In short:
   the buttons depend only on the object shown in the report panel, or on the tagged
   group, and are all dim for a list, a system report, a star, a storm, a warp point or
   another empire's object. The four movement-log buttons are lit in every simultaneous
   game and never in a turn-based one. Launch\Recover Units is turn-based only; Cargo
   Transfer works in both. At 800x600 there are 4 pages of 10 with wrapping arrows; at
   1024x768 one page of 40. The page never follows the selection. A hotkey works only
   when its button is lit, and five more keys exist (Ctrl+W, Ctrl+Y, Ctrl+Z, Ctrl+J,
   Ctrl+K; §3.1).
   Our client lights the buttons by the rules of §2.8, for every kind of selection
   including tagged groups (Shift+click in the list, Shift+A, Shift+C), and binds every
   key of §3.1 (rebindable). It differs:
   - Jettison Cargo, Use Facility and Convert Resources light by the rules, but our engine
     cannot carry them out yet: using one says so.
   - Colonies cannot cloak in our engine, so Cloak and Decloak stay dim for colonies.
   - The movement log is replayed from what the client saw (Q51).
   - An 800x600 layout, if added, needs the 4 wrapping pages.
5. **System art.** Where are the 128x128 `Systems/*.bmp` and the 72x72 storm and
   asteroid tiles drawn, and what do `Mask Background Objs` and `Non-Tiled Center Pic`
   change? **Answer:** the 128x128 picture is the System Report's (§1.4). The tiles build
   the background of the tactical and replay maps: from the system type when it is
   masked, else from the `Combat Tile` of a storm or asteroid field in the sector, else
   the plain starfield (§5.3). `Mask Background Objs` decides whether the system panel
   draws sprites with black transparent or as opaque squares (§2.4), and gates the
   system's tiles in combat. `Non-Tiled Center Pic` only switches the Stellar
   Manipulation preview to the plain starfield (confirmed: binary).
   Our client follows this: sector sprites are keyed only for masked types, the System
   Report shows the 128x128 picture, the combat maps use the tiled or star-field picture,
   opaque, repeating every 12 squares, and `Non-Tiled Center Pic` is read as a flag for
   the Stellar Manipulation preview. It differs: it picks the combat tiles from a seed
   made from the battle's place, not from the game's random numbers (inferred); the
   800x600 exception does not arise.
6. **Empire colour.** From the `_Main.bmp` swatch or chosen per player, and what if two
   players share a style? **Answer:** from the art: the pixel at (28,13) of the empire's
   `_Main.bmp`, inside the swatch, read when the game is created and on every load.
   Players cannot choose it. Two empires with the same style get the same flag and the
   same colour; nothing adjusts it (§5.3) (confirmed: binary). No setup check against
   duplicate styles was found.
   Our client samples the swatch pixel and draws every empire colour from it. It differs:
   the colour a game setup gives an empire (a palette colour by player number, or a
   server setup file's `color`) is kept in the game and used only where the art is
   missing, such as the dedicated server's logs.
7. **Sprite rotation.** Heading steps for top-down minis? **Answer:** 8 headings in 45°
   steps. Only hulls that use engines, and fighter and drone groups, are turned; the
   heading follows the bearing of each move within a system, rounded to the nearest 45°,
   and is kept through warp points; turning samples pixels nearest-neighbour with no
   smoothing (§2.4). Tactical combat uses the same 8 headings; with combat animation on,
   a change of heading turns the sprite 5° per step the shorter way round, with a short
   pause per step unless Fast Tactical Combat is on. Seekers and torpedoes point at the
   exact bearing of their target, in whole degrees (confirmed: binary).
   Our client turns the system view's minis to the 8 headings, nearest-neighbour, for
   the same hulls. It differs: the client follows the moves it sees, so a simultaneous
   turn's moves give one bearing, and after a game is loaded the minis face up until they
   move (the heading is not saved). Its combat map and replay turn pieces to the exact
   angle of each move, start them at a free angle and do not animate turns; they should
   snap to the 8 headings and animate 5° steps. Its numbering of the 8 directions already
   matches the original's.
8. **Options windows.** Is Game Menu → Options the same window as Empire Options?
   **Answer:** no. Game Menu → Options opens a per-computer Options window (animation,
   sound, the classic or remastered sound set, music volume, fast combat, movement
   lines, autosave, and Reset Passwords on a simultaneous host). Empire Status → Empire
   Options is per empire and saved with the game. Both full lists, with defaults, are in
   §1.9 (confirmed: binary).
   Our client follows this: Game Menu → Options opens the per-computer Options window
   (with an extra Settings button for OpenSE4's graphics, controls and effects volume),
   Empire Options lists every row of the table above with its default, kept with the
   empire and saved with the game, and the Game Menu has the original ten buttons; Save
   Empire writes an empire file (ours holds no designs, so it does not ask about them).
   It still differs: Players cannot switch computer control; there is no Reset
   Passwords; no Settings.txt switch turns music off; our own choices are in Q41–Q48.
9. **Start-up menu (1.95).** What is the `StartMenu.bmp` window? **Answer:** a launcher
   built into the game (the 736x536 window spec 07 saw), shown only when the program
   starts without arguments, as the Steam shortcut does. Play opens the Intro; the other
   buttons open documents, web pages or the map editor. Before the intro picture come
   the publisher's and developer's logos, 4 s each (§1.1) (confirmed: binary).
   Our client's intro has the original's buttons in its order, and Resume Game loads the
   last game saved with Save Game. It differs: it has no launcher and no logo sequence
   (the launcher is optional for parity: it only opens documents); Tutorial and Scenario
   open OpenSE4's Learn window; Credits shows our own credits; OpenSE4's Multiplayer,
   Settings and Manual sit in a small row at the top right.
10. **Map colours.** **Answer:** every colour and overlay rule is in §2.6 (galaxy panel,
    Galaxy Map window, Systems To Avoid, Borders) and §2.4 (system grid, waypoint
    markers) (confirmed: binary). Unexplored systems are (126,126,126) rings, explored
    ones (238,238,238), one empire its colour (Q6), several empires a solid triangle;
    the grid is (21,32,59) and always 68 × 47 cells; warp lines (165,176,179); the
    hovered system cyan. Avoided systems use the viewer's own colour, not yellow.
    Our client follows these colours, symbols and overlays, the 68 × 47 grid and the
    hover name, and its system panel draws owner-coloured ship counts and cloak rings. It
    differs: Systems To Avoid also rings avoided systems on its other tabs (ours); quadrant
    maps in other windows fit the 68 × 47 grid to their own size.
11. **Log details.** Meaning of the bullets, sort order, filter persistence?
    **Answer:** a green lamp marks the selected entry and a blue one every other row; the
    colour carries no category. Only the current turn's entries are listed, in the order
    they were made. The filter is stored with the empire (and saved), and comes back on
    every later opening, falling back to All when that category is empty; the selected
    position and the scroll position come back too. Full rules in §4.1, which is also
    corrected: a combat entry lists each object's damage (a percentage, Dead or Taken),
    not Start and Lost counts (confirmed: binary).
    Our client follows this, with the choices of Q41–Q48. It still differs: messages from
    other empires are rows of their own (taken from the game's messages, listed before
    the log entries) and keep our layout whatever `Use Old Log Political Message
    Display` says; refused orders are rows of their own too.
12. **Save folder contents.** **Answer:** see §6.1: the folders created at start-up; the
    names and places of saves, maps, empires and autosaves (`AutoSav<d>.gam`); the
    per-player turn saves; the `.plr`, `.trn` and `.cmb` names; the `History/plr_<N>_*`
    files and their copies next to each save; what goes into `temp/`; and the fixed-width
    layouts of the history files, which are plain text (confirmed: binary).
    Our client names autosaves `AutoSav<d>.gam` and writes `History/plr_<N>_*.txt` with
    the layouts above, copies them next to each Save Game and autosave and restores them
    on loading. It differs: its saves and `History/` live in the user-data folder, not
    the install, and keep our own save format (fine, §6); the log copy's header words are
    ours (Q55).
13. **Status icons.** **Answer:** drones in cargo use cell 37, not 34. The executable
    draws 23 cells and never the other 15. §4.4 lists each cell, its condition and the
    drawing order for ships, planets and fleets, including cell 36 (remote mining),
    which the manual does not mention (confirmed: binary).
    Our client draws these cells in this order for ships, fleets, planets, ship-list rows,
    reports and the Colonies list. It differs: colonies cannot cloak in our engine, so a
    colony never shows cell 9; what counts as "building", a cloaked fleet and "the first
    miner" are our choices (Q50).
14. **Transparency.** **Answer:** the colour key is exact black, RGB(0,0,0), and there
    is no additive or alpha blending anywhere: every sprite is copied opaque or drawn
    with black transparent, explosions, shield hits, beams and torpedoes included
    (§5.1, §5.3). Sector sprites follow `Mask Background Objs` (§2.4) (confirmed:
    binary).
    Our client draws beams, the combat background and hit and miss lines opaque. It
    differs: its own overlays on the combat maps (firing lines, the squares of big pieces,
    seeker dots, the capture ring) still use alpha.
15. **UI sounds and music.** **Answer:** the sound-to-action table, the sound-set choice
    and the music rules are in §5.5, the fresh-install defaults in §1.9 (confirmed:
    binary). One effect plays at a time. Command and order buttons play `cmdbtn` and
    `ordbtn`. Each music switch picks one random track and loops it; the background
    track is re-picked every 5 turns; combat music starts with Tactical Combat or a
    Combat Replay and does not switch back.
    Our client follows the playback and music rules, the cloak and stellar-manipulation
    sounds and the `boom` rules of the combat maps, and its music volume is Off and five
    steps. It differs: it plays no `boom1`/`boom2` per ground combat round and draws no
    small explosion over a system-panel sector; of the TCP/IP sounds only sending a chat
    line plays (`ordbtn`); a stellar manipulation sounds when the player's log reports it;
    a game loaded from the Game Menu keeps the music (Q53); it keeps an effects volume of
    its own.
16. **Tooltips and focus.** **Answer:** there are no pop-up tooltips. Hovering a command
    or order button writes its name and key as white text at the top of the system panel,
    at once, with no delay (§2.3). Every window is modal, so main-window hotkeys do
    nothing while one is open, and nothing while the turn is being ended or the movement
    log replayed. Esc and Enter close only some windows; §3.4 lists the keys of each
    kind. In Yes/No prompts Enter means No (confirmed: binary).
    Our client follows §3.4 in its prompts: Yes/No boxes (Y; N, Esc and Enter mean No; the
    key that opened a box does not answer it), windows with Close (Esc and Enter), battle
    notices (Esc and Enter mean Begin), the Tactical/Strategic question (T and S) and Next
    Player (Esc and Enter), and its command and order buttons write the hint text of §2.3.
    It still differs: other controls show tooltips beside the pointer after a delay, and
    Esc in the main window clears targeting or the selection (an extra that can stay). Its
    own extra keys are listed at the end of §3.
17. **Weapon graphic index base.** **Answer:** beams and torpedoes are both 1-based:
    cell = `Weapon Display` − 1, and 0 means no picture (only warheads use 0 in stock
    data). Seekers use the 20x20 slot at x = 40 + 20 × `Weapon Display` of the owner's
    `_Main.bmp` (§5.2) (confirmed: binary).
    Our client follows this for beams, torpedoes and seekers.
18. **Ship naming.** **Answer:** a new ship is named with its design name, a space and a
    four-digit serial with leading zeros ("Hood 0007"). The serial is one more than the
    highest serial among existing objects of that design, read from the last four
    characters of their names, so a number freed at the top is reused. The Combat
    Simulator counts per side instead. Design names come from the name file as described
    in §6 (confirmed: binary). The executable loads the hull `Code` but never puts it into
    a ship's name. **Open: needs observation.** Where, if anywhere, the bracketed code of
    the manual's captures appears in 1.95 (perhaps appended in a list row, perhaps only in
    an older version) needs a look at a ship list in the running game.
    Our client follows the serial rule (`game::nextVehicleName`).
19. **Planets filters.** What does Coloniz\Empty add to Colonizable, which treaties make
    a colony an ally one, and what makes a planet Special? **Answer:** Coloniz\Empty means
    the planet itself is not colonized (not a test on the system). Ally means
    Non-Aggression or better; every other empire, including one not yet met, is an enemy.
    Special means the `Ancient Ruins` or `Ancient Ruins Unique` ability only. Also,
    Colonizable does not exclude colonized planets, and All lists no asteroid fields.
    The full tab table, the statistics, columns, sorting and Send Colony Ship are in
    §1.8.1 (confirmed: binary).
    Our client follows this (its own choices where the rules are silent are Q24–Q29). It
    still differs only where our engine has nothing to test: it has no planetary cloak,
    so no planet is left out for one and every colony is seen.
20. **Construction Queues toggles.** What do Ships and Ship SY each include?
    **Answer:** the split is by whether a vehicle's space yard works right now, not by
    hull. Ship SY holds every ship and base queue with a working yard; Ships holds vehicle
    queues whose yard does not work (cloaked, or the yard is gone), which are cleared at
    the next update, so it is normally empty. Planets and Planet SY split colonies the
    same way: a cloaked colony's yard does not count. All four are on by default. Rules,
    columns, statistics and the three action buttons are in §1.8.2 (confirmed: binary).
    Our client follows this (its own choices are Q24–Q29). It still differs in: no
    colony cloaking exists in our engine, so a colony's yard always works; status icons
    on a row's second line are drawn for colonies only (ships and bases have none yet).
21. **Tactical Combat details.** The Options list; which Orders act at once and which
    need a target click; the pointers; the right-click report. **Answer:** §1.10.1–
    §1.10.3 and §3.3 (confirmed: binary). The Options window has nine switches under
    Animation, Sound and Tactical Combat, plus Stop Combat in the simulator. Ram and
    Capture arm a target click; Drop Troops acts at once on the adjacent colony; L opens
    Launch Units. The battle starts with a Begin button and ends with a message, after
    which the window closes. The install's `.cur` files are the pointers. Right-click
    opens a full Combat Piece Report window.
    Our client follows this. It still differs where Q34–Q37, Q39 and Q40 say: Fast
    Tactical Combat speeds the animation up instead of removing the waits (Q34); the
    piece report has no tabs and some lines differ (Q35); the orders' pickers replace
    the menu instead of opening as windows of their own (Q36); Drop Troops is refused by
    the engine for an empire it is not hostile to and takes the first adjacent colony
    instead of the last (Q37); the replay keeps a log and a summary beside the map
    (Q39); the pointers are drawn by the client instead of loaded from the `.cur` files
    (Q40).
22. **Combat Simulator details.** How many sides, how items are removed, what Fleets for
    Plr does. **Answer:** always 10 sides, "Race 1" to "Race 10"; a left-click on a combat
    vehicle removes it; Fleets For Plr opens Fleet Transfer for the chosen side. Each click
    on an item adds one vehicle. Begin with Tactical fights in the Tactical Combat window
    and reopens the simulator with the same setup afterwards; Strategic opens the
    Strategic Combat window over it. Details in §1.10.4 (confirmed: binary).
    Our client follows this. It still differs where Q38 says: Fleets For Plr and Change
    Cargo open our own pickers for the simulated ships, not the Fleet Transfer and Cargo
    Transfer windows (those work on the real game); Designs stays open under a tactical
    simulation; sides show flags instead of numbered colour boxes; ships are numbered per
    side and design instead of per side.
23. **Strategic Combat and Ground Combat details.** **Answer:** in a turn-based game on
    one machine, every battle with a piece of a human-controlled empire asks Tactical or
    Strategic when it starts, during computer turns too (with a notice first); on
    different machines the current human player gets the Strategic Combat window; in
    simultaneous games every battle opens it when `Simultaneous Games Show Strategic
    Combat` is on. The battle is fought live, one combat turn at a time, and Close stays
    dim until the end. The forces list counts ships and bases per hull, units per unit
    design's hull, and up to 5 planets by name. Ground Combat opens when troops land
    (tactical, or a computer side in a strategic battle) and for a continuing ground
    stalemate; it fights round by round. Details in §1.10.5 and §1.10.6 (confirmed:
    binary).
    Our client follows this. It still differs where Q30–Q33 say: it holds each combat
    turn 0.45 s and moves the map once per combat turn, not after every step (Q30); a
    few title-strip and forces-list details (Q31); with No Tactical Combat on, and in
    the other cases where the window opens without the question, it shows battles after
    the engine call that fought them instead of when they start (Q32); Ground Combat
    shows the counts at the start and the end only, at 0.45 s per round, and does not
    open for a ground stalemate at the end of the colony owner's turn (Q33).

The Planets and Construction Queues windows follow §1.8, and these choices of ours fill
what the executable's rules leave open (inferred; each needs a look at the running game):

24. **Sort history.** We read the "five-key history" as: the last column header clicked is
    the first key, the ones clicked before it break ties, up to five, each in its fixed
    direction. Is that the original's order, and does a second click on the same header
    change anything?
25. **Available colony ships.** We count a ship as having orders when its fleet has orders,
    and as out of supplies at 0 supply. Send Colony Ship on a planet that is already
    colonized (possible from the Colonizable tab) refuses with a message. What does the
    original do in each case?
26. **Planets statistics.** We leave our own colonies out of "owned by enemies" and "owned
    by allies". Does the original count them in one of the groups?
27. **Construction Queues rows.** We take the time at the right as the whole queue's time,
    show it as "N.N Years", make rows 40 px tall and mark a tagged row with the green lamp
    at its top left. The Ships toggle lists ships and bases that still have a yard part or
    a queue while their yard does not work (cloaked, mothballed or lost). The statistics'
    "resources generated" is the empire's total income per turn. Which of these match?
28. **First-item confirmation.** With "confirm deleting the first item" on we ask whenever
    the first item is removed, with or without progress, and before Clear Queue. Does the
    original ask in both cases, and only for the first item?
29. **Similar system-wide abilities.** We note it after a facility is queued whose
    abilities include one with "System" in its identifier that a facility of one of our
    colonies in the same system already has. Which abilities count, does the original
    also look at queued facilities, and is it a note or a question?

The combat windows (Tactical Combat and its Orders, Launch Units, Combat Options and
Combat Piece Report windows, Combat Replay, Combat Simulator, Strategic Combat and Ground
Combat) were brought in line with Q21–Q23. Questions 30–40 were the choices of ours that
filled what those answers left open; on 2026-10-01 each was settled from the executable:

30. **Strategic Combat pace.** The original fights each combat turn with no coded delay.
    Our window plays the engine's finished battle back and holds each combat turn for
    0.45 s; how long does a turn stay on screen in the original on a period machine?
    **Answer:** there is no hold to match (confirmed: binary; §1.10.5 "Pace"). The
    strategic battle runs in a loop with no wait anywhere: the small map is redrawn after
    every single step of every piece and seeker, the forces list and "Combat Turn N"
    after each combat turn, and the window handles its pending messages after each
    empire's phase and each combat turn so that it repaints. A combat turn stays on
    screen only as long as the next one takes to compute and draw. How long that was on
    a period machine is a matter of the machine's speed, which the executable cannot
    tell; on a current machine the whole battle passes in a moment.
    The client differs: it holds every combat turn 0.45 s and moves the map once per
    combat turn. To match, it must draw every step as it is taken (the battle record has
    to keep each step, Q32) and hold nothing. A minimum hold kept so that a player can
    follow the battle would be OpenSE4's own choice and must be marked so (inferred).
31. **Strategic Combat details.** Where in the title strip do the system name and the
    coordinates go (ours: x 270 and 450, as in Ground Combat)? Are the 5 planets of the
    forces list counted per empire (ours) or for the whole battle? Under which hull does a
    unit group that mixes designs count its units (ours: its first design's)?
    **Answer** (confirmed: binary; §1.10.5 "Layout", "Forces list"): at y 10, "System"
    at x 270 and "Coordinates" at x 460, each label in #7D9FFF with its value right
    after it in white ("Combat Turn" at x 630 while the battle runs). The 5 planets are
    per empire: the first 5 colonized planet pieces of each empire, in piece order. A
    unit group counts each stack's living units under the hull of that stack's own
    design, so a mixed group spreads over several hull rows. One more rule: the rows are
    made once, when the window opens, so a hull an empire did not have in the battle at
    set-up never gets a row, even when units of that hull are launched or a ship of that
    hull is captured.
    The client differs: Coordinates at x 450 and a mixed group counted under its first
    design's hull; and no hull row may be added during the battle (the earlier text of
    §1.10.5 allowed it).
32. **Battles without tactical combat.** With No Tactical Combat on, ours shows every
    battle with a human player's piece after the engine call that fought it, not as the
    battle starts (the engine does not stop for them). Is the window shown before or after
    the battle's results in the original?
    **Answer:** before the battle is fought (confirmed: binary; §1.10.5 "The sequence").
    The window opens right after the battle has been set up and before its first combat
    turn; the battle is fought while it is shown, after Begin; and its results (the
    end-of-battle steps inside the window, then after Close the reports, log entries,
    happiness events, removal of destroyed objects, cloaking again and the fate of the
    moving group's orders) come only after Close. It is the same moment, and the same
    window, as the Tactical or Strategic question: with No Tactical Combat on the window
    opens in its Begin and Close form instead of its question form, still after the
    notice when a computer player's turn is being played. The same holds for the
    Strategic Combat window on different machines and in simultaneous games.
    The engine differs: it does not stop for a battle it does not ask about; it fights
    it within the call, and the client shows the record after the whole call (an order,
    or End Turn with the computer turns) has finished, after later moves and battles,
    with the results already in the log. To match, the engine must stop where it now
    raises its battle question (the battle set up, before combat turn 1) whenever the
    battle is to be shown in the Strategic Combat window (the cases of §1.10.5), hand it
    to the client, and resume only when the window has closed: then finish the battle
    (reports, log, happiness events, cloaking again, removal of destroyed objects) and
    carry on with the interrupted move. Since nothing in the window can change the
    battle, the engine may also fight it at the stop and hand over the fought record,
    as long as nothing later in the call (the main window, the log, other battles)
    becomes visible before Close. The record must keep, for the window: the pieces as
    set up (owner, kind, design or object, size, square); in order, every step of every
    move (seekers' included), every launch, every piece destroyed or removed and every
    change of owner, each with its combat turn and phase; and each ground combat at the
    point of the phase where its troops landed, so the playback stops there and opens
    Ground Combat (Q33). The forces counts of §1.10.5 follow from these; the end of each
    combat turn must be marked, since the list is recounted only then.
33. **Ground Combat rounds.** Our record keeps a ground fight's start and end only, so the
    window counts the rounds and shows the new numbers at the end. Do the original's unit
    counts change after every round? Ours does not yet open Ground Combat for a stalemate
    at the end of the colony owner's turn (that needs the engine to stop there).
    **Answer:** yes (confirmed: binary; §1.10.6). After every round the round counter
    ("Ground Combat Turn" N/M), the facility grid and both unit grids are redrawn with
    the counts left after that round; then an explosion plays over the planet picture
    (0.9 s) and a boom sound starts. Begin is the only input; Close lights up at the end.
    It opens at once when troops land in a battle shown in a window (by hand, or by a
    computer side in the Tactical or the Strategic Combat window), in the middle of the
    phase, while the space battle waits; and at the colony owner's ground-combat step at
    the end of its turn (spec 05 §8 step 17), for each colony where the fight against
    an empire below Non-Aggression goes on: first the notice naming the system and the
    two empires, then the window, while that empire's end-of-turn processing waits. It
    opens only in turn-based games when one of the two empires is human (in the
    simulator always); the log entries are written after the fight either way.
    The engine and the client differ: the engine's ground record keeps the start and the
    end only, the client counts rounds at 0.45 s and shows the final numbers at the end,
    and the engine runs the end-of-turn fights silently, so the client never opens Ground
    Combat for them. To match, each ground fight's record must keep, for every round,
    the count of every troop and militia stack on both sides after that round, with the
    round limit and the outcome; the client must show them round by round at 0.9 s per
    round (the explosion); and the engine must stop at the colony owner's ground-combat
    step when the window is to be shown (or fight it and yield at once), so that the
    notice and the window come before the rest of that empire's end-of-turn processing
    and before the next player's turn.
34. **Fast Tactical Combat.** Ours plays the animation three times faster instead of
    dropping the pauses between steps; the same for Fast Tactical Combat in Combat Replay
    Options.
    **Answer:** the switch removes the waits and nothing else; there is no speed factor
    (confirmed: binary; §1.10.3, which lists every wait: 1 ms per frame of a slide, 0.1 s
    after a jump when movement is not animated, 0.01 s per frame of a turn, a few
    hundredths of a millisecond per beam stamp, 1 ms per frame of a torpedo, 0.1 s per
    frame of a hit animation and 0.3 s after each hit). Every frame is still drawn. The
    replay's switch does the same in the replay; neither touches the Ground Combat or
    Strategic Combat window.
    The client differs: it speeds the animations up threefold. To match, with the switch
    off it must use the waits of §1.10.3, and with it on hold nothing. Since a frame
    drawn without any hold can hardly be seen on a current display, showing each frame
    for one display refresh would be OpenSE4's own choice (inferred).
35. **Combat Piece Report.** Ours shows Damage as structure taken / maximum for a ship and
    as a percentage otherwise, Supply as "-" for a planet, a fleet's own group as "Fleet -
    Leader" or "Fleet - Wingman", Formation only for fleet members, and no tabs; a neutral
    obstacle gets a one-line note instead of its ordinary report. What do the original's
    tabs and these lines show?
    **Answer** (confirmed: binary; §1.10.1, with the table of lines): Damage is "taken /
    full" for every kind of piece, never a percentage; Supply is "Never" for planets and
    satellite groups, "Endless" with unlimited supply, "None" for a seeker, and "now /
    capacity" otherwise; Combat Group reads "Group N - Leader" or "Group N - Wingman"
    for every numbered group, fleet groups included (there is no "Fleet" wording);
    Formation shows a formation only for the group's leader (None for members) and keeps
    showing it after the piece stops leading. Tabs: a ship or base has Detail, Comps,
    Cargo (with cargo space) and Ability; a planet Detail, Facil, Cargo and Ability; a
    unit group none, its units listed in a grid under a shortened Detail page; a seeker
    none. A neutral obstacle opens its ordinary object report.
    The client differs in each point of the question: percentages, "-" for a planet's
    supply, the "Fleet" wording, Formation on members, no tabs and no unit grid, and a
    one-line note for an obstacle.
36. **Tactical Combat Orders.** Ours stacks the 11 buttons with no gap and no title, and
    the pickers (group size, group number 1–9, formation, the Resolve Combat question)
    replace the menu in the same window. Are they separate windows in the original, and
    can a group number be 0 there?
    **Answer:** separate windows; no group 0 (confirmed: binary; §1.10.2). The menu is a
    borderless window without a title bar, its 11 buttons stacked with no gap, as ours.
    A click closes it, and only then does the order run, opening its picker as a modal
    window of its own: "Select Fighters Per group" (5 to 50), "Select Combat Group"
    (1 to 9), then "Select Formation" for a new leader, and the game's confirmation
    window titled "Resolve Combat". The group list offers 1 to 9 only; Alt+0 and Ctrl+0
    clear the selected piece's marks. Closing the formation picker without a choice
    clears the piece's group marks.
    The client differs only in showing the pickers inside the menu's window; to match,
    the menu must close and each picker open as its own modal window.
37. **Drop Troops.** Ours picks the first adjacent colony of another empire that the
    battle accepts. The engine still refuses an empire it is not hostile to (spec 04 §11),
    while §1.10.2 says hostility is not checked: one of the two needs to change.
    **Answer:** spec 04 had it wrong: no treaty is checked anywhere (confirmed: binary;
    spec 04 §11, §13, §16.1, and §1.10.2, which now agree). The order takes the colony
    of another empire adjacent to the selected ship that comes last in piece order,
    hostile or not, and looks at that one only: the landing is refused when it already
    holds a third empire's troops, and does nothing without troops aboard. A computer
    carrier heads only for hostile colonies, but after every move it plans with Drop
    Troops in effect it lands by the same rule on whichever foreign colony is adjacent,
    hostile or not. The ground combat that follows is fought at once and to its end
    whatever the treaty, and the invaders take a friendly or allied colony if they win;
    only at the colony owner's end-of-turn step does a treaty at Non-Aggression or better
    hand surviving invaders over to the colony. After every landing the planet's piece
    is reset: weapons ready, shields full, targets cleared.
    The engine differs: it refuses a colony it is not hostile to; it takes the planet
    the order names (the client names the first adjacent colony) instead of the last
    adjacent one in piece order; a computer carrier lands only on its chosen target; and
    the planet's piece is not reset after a landing that fails to take it. To match, the
    hostility test must go, the order needs no target (the colony follows from the rule
    above), a Drop Troops piece tries the landing after every move, and every landing
    resets the planet's piece.
38. **Combat Simulator.** A race's flag is that of the empire its first item belongs to,
    or the player's while it has none. Every object of the home system is offered,
    asteroid fields and storms included. Fleets For Plr is our list of the race's ships
    with one fleet per race (formation and strategy), and Change Cargo our holder picker,
    not the original's Fleet Transfer and Cargo Transfer windows. Ships are numbered per
    race and design ("Name 0001"). While a tactical simulation is fought, Designs stays
    open underneath. Which of these match the original?
    **Answer** (confirmed: binary; §1.10.4, spec 04 §17):
    - **Flags:** none. The Owner for item and Computer Control lists show each side as a
      box in a fixed colour by side number holding the number (1 red, 2 blue, 3 green,
      4 yellow, 5 purple, 6 white, 7 aqua, 8 lime, 9 maroon, 10 olive), whichever empire
      the side copies, and so do the combat windows of a simulation and the reports
      opened from them. Open: what the Combat Vehicles list's Flag column draws was not
      traced; needs observation.
    - **Items:** the home system's colonized planets (any owner) and every unowned
      object there (stars, warp points, comets, storms, empty planets and asteroid
      fields), so ours matches; storms and uncolonised asteroid fields can be added but
      never become pieces, as in a real battle.
    - **Fleets For Plr and Change Cargo** open the original's Fleet Transfer window
      (the side's ships and its copied fleets) and Cargo Transfer window (all the
      simulator's vehicles and planets).
    - **Names:** "<design> NNNN" with one counter per side for all designs, counting the
      ships ever added to that side, never lowered by removals.
    - **Designs** closes during a tactical simulation and reopens with the simulator.
    The client differs: flags instead of the numbered boxes; its own pickers instead of
    Fleet Transfer and Cargo Transfer; a counter per side and design; Designs stays open.
39. **Combat Replay.** Ours keeps, beside the map and its overview, the turn's events in
    words and the battle's summary (OpenSE4's own help); it has no Close button (Esc and
    Stop Replay close it). Does the original replay keep a log of any kind?
    **Answer:** no (confirmed: binary; §1.6). The original's replay window has the map,
    the overview, the piece panels (hovering a piece shows its design), a title strip
    with "Combat Replay", the location, the combat turn and the empires' flags, and two
    buttons, Options and Next; Space is Next, and Esc or Stop Replay closes it. No log,
    event list or summary. Having no Close button matches.
    The client differs: the events in words and the summary are OpenSE4's own help; to
    match they must go, or stay as a marked OpenSE4 extension (inferred).
40. **Pointers.** Ours draws its own move arrows and crosshairs: no loader for the
    install's `.cur` files exists yet.
    **Answer** (confirmed: binary; §1.10.1 "Pointers", §5.3): the original loads the
    twelve `.cur` files of `Pictures/Game/` once at start-up as Windows cursors, with the
    hotspots stored in the files; Normal is the pointer of the main window and most
    dialogs, Hourglass replaces the pointer everywhere while the game is busy, and the
    tactical map switches between Normal, Target, Select and the eight arrows as §1.10.1
    says. Nothing draws a pointer of its own.
    The client differs: it draws its own arrows and crosshairs. To match, it must read
    the files (the Windows cursor format: an icon file whose header holds the hotspot)
    and set them as the system pointer in the same cases.

The Options windows, the Log, the system window's display options and Abandon Planet
follow §1.9, §4.1 and §2.8; these choices of ours fill what the rules leave open
(inferred; each needs a look at the running game):

41. **Log Goto to a window.** Goto on an entry without a location opens a window chosen by
    the entry's category: Construction → Construction Queues, Research → Research,
    Intelligence → Intelligence, Politics → Empires; Events, Combat and Misc open none.
    Which entries name which window, and which ones open Empire Options or Designs?
42. **Log order and selection.** Messages from other empires come first, then the log
    entries in the order they were made, then the orders the turn refused; a filter click
    selects the first row. Is that the original's order, and what does a filter click
    select?
43. **Log damage of planets.** Our battle records keep no planet hit points, so a planet
    that stands shows "0%" when untouched and "Hit" otherwise; ships and bases show their
    damage now, unit groups the share of their units lost. What does the original show
    for a planet?
44. **Facility markers.** We read the letters as: R Supply Generation, S Spaceport, Y Space
    Yard; Ca, Cc, Cv the planet atmosphere, conditions and value changers; St, Ft ship and
    fleet training; Rc Component Repair; Rr Resource Reclamation; Sst, Sft the system-wide
    ship and fleet training; Spv, Spc the system-wide value and conditions changes; Sph
    the system-wide happiness change; Spa the system-wide population change; Scm, Sdm the
    system combat and damage modifiers; Srm the system reproduction modifier; Ssm the
    system shield modifier; Spp the system plague prevention; Src the system reduced
    maintenance; Sbe, Sbi the system bad event and bad intelligence chances; Slr the
    system long range scanner. We draw them in yellow above our own colonies. Which
    abilities, where, and for which colonies does the original draw them?
45. **Planet names and coordinate location.** We draw a planet's name under it, like a
    warp point's destination, and the coordinates of the sector under the pointer after
    the system name. Where does the original show them?
46. **Confirmations.** "Confirm stellar manipulation" asks before every action. "Confirm
    scrapping" asks before scrapping vehicles or facilities and before self-destruction.
    Do the original's ask in the same cases?
47. **Claims and abandoned planets.** "Claim every system we colonize" applies to human
    empires when the colony is founded (computer players work out their claims each
    turn). Facilities left on an abandoned planet go, all of them, to the next colony
    founded there by any empire. Do these match?
48. **Smaller choices.** The planet report's Time Remaining uses the Construction
    Queues' years ("0.3 Years", "On Hold", "Never"). "Skip ships under construction"
    skips nothing: our ships appear finished, with no Under Construction status. Save
    Empire writes our empire file, which holds no designs, so it does not ask whether to
    include them. Which of these differ from the original?
49. **Ships beside a planet.** Where exactly do the small flags and their counts go in a
    sector that holds a planet, or several empires, and does a sector with a star,
    storm or warp point count like a planet? Our client draws the flags left to right
    along the bottom of the 36x36 sprite area, each count after its flag, and treats any
    stellar object like a planet (inferred).
50. **Status icon conditions.** What makes an object "building", which fleet shows the
    cloaked icon, and in what order "the first miner" is found? Our client: a ship whose
    yard works (not cloaked) with items queued, or a colony with items queued, held or
    not; a fleet with any member cloaked; the first in the game's vehicle order
    (inferred).
51. **Movement log replay.** What does one Ctrl+I step show, and how does Ctrl+U differ
    from Ctrl+P? Our engine keeps no day-by-day log, so our client moves each vehicle it
    saw in a straight line from where it was before the turn to where it is, in ten
    steps, and plays Ctrl+U like Ctrl+P (inferred).
52. **Tagged groups.** What do Scrap and Move To Waypoint show for a tagged group? Our
    client asks to confirm the scrapping of every tagged object, and lists the set
    waypoints (inferred).
53. **Resume Game and the music after loading.** Does an autosave become the "last saved
    game", and does Load Game from the Game Menu change the music? Our client: only Save
    Game sets it, and an in-game load keeps the music (inferred).
54. **Low supply of fighter groups.** Do the Sentry button and the low-supply icon of a
    fighter group use a tenth of `Supply Amount for Low Supply Warning`, as Sentry's own
    end does? Our client lights the button by a tenth and draws the icon by the full
    level (inferred).
55. **The log copy's header.** What words head `plr_<N>_log.txt`? Our engine writes
    "Date", "Title" and "Text" in the columns of §6.1, then the rule (inferred).

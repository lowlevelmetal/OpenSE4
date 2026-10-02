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
  layout otherwise; there is no option. The 1024x768 frame is centred on a wider desktop,
  the 800x600 frame sits at the top-left corner, and dialogs are the same size in both
  and centred on the desktop (§2.1.1) (confirmed: binary).
- **Left-click acts, right-click reports.** In nearly every list, a left-click performs
  the primary action (select, add to a queue, remove from a queue, jump to the object in
  the main window), and a right-click opens a read-only report popup about the item
  (ship, planet, race, component, tech area) [T]. Our client should keep this.
- **Shift+click multi-selects** in the ship list and in the construction-queue list [T].
- **Column headers sort** the big list windows (Colonies, Planets, Ships) [T]. A click
  makes that column the first sort key; the keys clicked before stay as tie-breakers, up
  to five, newest first; an earlier copy of the same column is not removed, so it can
  push out the oldest key. Each column has a fixed direction, and clicking again does not
  reverse it. The sort keys are stored with the empire (confirmed: binary; §1.8, §7 Q24).
  In Colonies and Ships\Units a key is the column itself, whatever tab is shown (§1.8.3).
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
420x520); File Menu (a narrow 173x320 column of buttons); prompts (about 309x140). The
780x668 windows cannot come from an 800x600 screen: at 1024x768 the four list windows are
as tall as the desktop less 100 px, and at 800x600 they are 780x475 like the other large
dialogs (§2.1.1) (confirmed: binary).

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
| Player Computer Control | Hand empires to the computer or take them back. | Check list titled "Empires Under Computer Control", 420x520, OK / Cancel: one row per empire in empire-number order (every empire, neutral and destroyed ones included) with a lamp, the empire's flag and its name; a lit lamp means computer-controlled. The same window serves the Combat Simulator [T] and Reset Passwords (§1.9). Rules in §1.2.1 (confirmed: binary). | Game Menu → Players. |
| Options (per computer) | Sound, music, animation and autosave. | Lamp list under headings; Close. Full list in §1.9 (confirmed: binary). | Game Menu → Options. |
| Designs | Browse and manage designs. | Design list; detail pane (stats, then either components or, with Stats\Strategy on, build/loss/kill counts and default strategy). Tabs: Ship Designs, Unit Designs, Enemy Ship Dsgn, Enemy Unit Dsgn. Actions: Create, Copy, Edit, Upgrade, Make Obsolete, Hide Obsolete, Stats\Strategy, Simulator [T]. | F3. |
| Create Design | Ship designer. | Size, type and name pickers; running totals; components-on-design and components-available lists; warnings box; hover detail. Comp Type (group filter), Weap Mount, Condensed View, Only Latest, Weapons Report, Create Design, Cancel [T]. | Designs → Create / Copy / Edit. |
| Weapon Mounts | Pick a mount for weapons added next. | List; Cancel. | Create Design; Weapons Report. |
| Combat Simulator | Set up a test battle. | Vehicles-in-combat list, design list, owner picker; Tactical / Strategic choice; No Obsolete, Strategies, Computer Control, Fleets for Plr, Change Cargo, Begin, Cancel [T]. Details in §1.10.4 (confirmed: binary). | Designs → Simulator. |
| Planets | All planets seen. | Statistics block; galaxy mini-map highlighting the hovered row; planet list. Filter tabs: All, Colonizable, All Colonies, Enemy Colonies, Ally Colonies, Coloniz\Empty, Coloniz\Breathe, Ship Enroute, Asteroids, Special; toggle No Sys To Avoid; Send Colony Ship [T]. Rules and layout in §1.8.1 (confirmed: binary). | F4. |
| Colonies | Own colonies. | Statistics; mini-map; sortable list whose columns depend on the tab: General, Value, Production, Facilities, Cargo, Construction, Status (status icons), Races, Orders. Actions: Scrap Facil Types, Set Colony Type [T]. Columns and sorting in §1.8.3 (confirmed: binary). | F5. |
| Ships\Units | Own ships, units, fleets. | Statistics; mini-map; list. Column tabs: General, Orders, Cargo, Fleet, Maintenance (each starts with a picture column); toggles Show Ships / Show Units / Show Fleets [T]. Columns and sorting in §1.8.3 (confirmed: binary). | F6. |
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

#### 1.2.1 Player Computer Control

All rules of this subsection are (confirmed: binary).

**Opening the window.**
- The Game Menu's Players button is always enabled, in every kind of game: single player,
  hotseat in both turn styles, turn-based on different machines, and both sides of a
  simultaneous game on different machines (the host's between-turns Game Master view, and a
  player's own machine, by e-mail or TCP/IP).
- When the game has a master password, an input box "Enter Game Master Password" comes
  first. Cancel does nothing; a wrong entry shows "Invalid Password" and the window does not
  open. This comparison is exact (letter case and spaces count), unlike the sign-in windows,
  which ignore letter case and leading or trailing spaces. Without a master password the
  window opens at once.
- Nothing else limits who may switch what. Whoever passes the password check may switch any
  empire: their own, other humans', computer players' and neutral ones.

**The list.** Every lamp starts as the empire's computer-controlled mark, and a click flips it.
OK applies every row whose lamp was clicked at least once, with the state the lamp shows at
the end, even a lamp clicked back to its starting state. Cancel applies nothing.

**Switching to computer control** (a row applied lit), at once:
- the empire is marked computer-controlled;
- all 25 of its ministers are switched on;
- every one of its ships, bases, unit groups, colonies and fleets gets its individual minister
  flag switched on.

**Switching to human control** (a row applied dark), at once:
- the mark is cleared;
- all 25 ministers are switched off; the player's earlier minister settings are not restored;
- every individual minister flag of its vehicles, colonies and fleets is switched off.

So a human empire whose lamp was clicked twice before OK loses all its minister settings and
flags.

**What switching never changes:** the empire's AI state and memory, its stored difficulty, its
minister style, its password, and the Ministers window options "use individual ministers for
newly built vehicles" and "AI should not make changes during a simultaneous game".

**What the computer-controlled mark decides** (as opposed to the minister switches):
- whose turns the game plays by itself, and when the game ends for lack of humans (below);
- whether the host of a game on different machines reports a missing orders file (spec 05
  §9.2);
- the Team Mode sides (spec 05 §7.1): a switched empire changes sides at once;
- two per-turn effects whenever the empire's ministers act (spec 05 §7.5): a
  computer-controlled empire's four movement options are overwritten from its `AI_Settings`,
  and a human empire's stored difficulty is set to Medium. So a random computer player that is
  handed to a human, plays a turn with any minister on and is then handed back plays at
  Medium from then on;
- only human-controlled empires get the start-of-turn log window, scenario texts and the
  per-player text files (statistics, history, log copy; §6.1);
- each time a game is loaded, every computer-controlled empire's combat strategies are
  replaced by those of its `AI_Strategies` file (spec 05 §7.2).

**When a switch takes effect.**
- **Turn-based** (one machine or several): the empire whose turn is in progress is still played
  by hand until End Turn. Its end-of-turn processing at that End Turn already runs all its
  ministers (the economy-step group, spec 05 §8), and writes no text files for it if it is now
  computer-controlled. From then on, whenever its number comes up, the game plays its turn by
  itself like any computer player's (start-of-turn ministers, then the turn ends at once), and
  no sign-in is asked for it. An empire handed back to a human is played by hand from its next
  turn. On different machines, the computer-controlled empires between two humans are played
  on the machine of the human before them, as usual.
- **Simultaneous, hotseat:** the remaining humans are asked for their orders in turn and
  computer-controlled empires are skipped; every computer-controlled empire's ministers act
  when the turn is processed. Orders that an empire switched in mid-phase had already given
  remain.
- **No human left:** in both of the loops above, after each End Turn the game checks whether
  any living empire is human-controlled. If none is, it shows the ending for "all human
  players eliminated" with a Finale "Human Dead" picture and plays no further turn. The host's
  processing of a game on different machines has no such check.
- **Simultaneous on different machines, at the host** (Game Master view between turns): the
  switch goes into the next processed turn and into the game file sent out. A missing orders
  file of a computer-controlled empire is not reported. The host's stand-in rule (spec 05
  §9.2) still applies to every empire without an orders file, so it changes nothing for an
  empire whose ministers are already all on. If the player of a switched empire still sends an
  orders file, the minister settings in that file replace the host's.
- **Simultaneous on different machines, on a player's machine:** the window changes only that
  machine's copy of the game. The orders file carries the player's own empire's minister
  switches and its vehicles' minister flags (probably its fleets' and colonies' flags too
  (inferred)), but never the computer-controlled mark, and nothing about other empires. So
  switching one's own empire to the computer makes all of that empire's ministers act on the
  host from the next processing, while the host still counts it as a human that must send
  orders; switching it back turns them all off. Switching any other empire has no effect
  beyond the local copy, which the next game file replaces.

**Passwords and signing in after a switch.**
- Switching never changes a password.
- A random computer player's password is set to the game's master password when it is
  created, so a random computer player handed to a human asks for the master password at its
  sign-in when the game has one.
- In the Next Player window (hotseat, more than one human), an empire password equal to
  "master" (any letter case) counts as no password: no password box is shown.
- The simultaneous sign-in lists every empire, computer-controlled ones included, plus Game
  Master.

**The engine and client differ** (2026-10-01):
- The Players window (`playersPopup` in `src/client/classic/screens/game_menu.cpp`) is
  read-only, leaves out neutral empires, asks for no Game Master password (local games keep no
  master password in `GameState`) and cannot switch control. It needs the lamp list, the
  password check when the game has one, and OK applying the clicked rows.
- `game::Empire::kind` (`src/game/types.hpp`) is fixed at setup and no command changes it. A
  host-side or local command (not a player command) is needed. To computer: kind Computer,
  every minister on (`ministers`, `ministerAll`), and every own vehicle, fleet and colony
  `minister` flag on. To human: kind Human, every minister off, every individual flag off. It
  keeps `aiDifficulty`, `ministerStyle`, `aiMinimalChanges`, `newVehicles` and
  `passwordHash`. Note that `ai::difficultyOf` (`src/game/ai.cpp`) gives a random computer
  player the options' difficulty again after a round trip, where the original keeps Medium
  once its ministers acted while it was human.
- `src/game/turn_based.cpp` (`resume`, `anyHumanToPlay`) goes on playing a game with no human
  left, one turn per call. The original stops with the "all human players eliminated" ending
  in local turn-based and hotseat play; the client should present that ending (headless
  all-computer runs may keep the engine's behaviour).
- `liveControl` in `src/game/turn_based.cpp` plays a computer empire normally even with
  `aiMinimalChanges` set; see the quirk in spec 05 §7.1.
- Random computer players and rebels have no password in ours (`src/game/events.cpp` clears it
  for rebels); in the original they get the master password. This matters only once switching
  to human exists.

### 1.3 Order dialogs and pickers (opened by order buttons in the command panel)

| Screen | Purpose | Reached from |
|---|---|---|
| Select Waypoint | Choose a waypoint as a destination. | Move To Waypoint; Set Construction Queue → Set Move To. |
| Fleet Transfer | Create fleets, move ships in and out (left-click moves an item between the two lists); Create Fleet, Formation, Strategy, Existing Fleets, Add All, Remove All. | Order F; Combat Simulator. |
| Cargo Transfer | Two cargo lists (from / to) with transfer step Move One / Five / Ten / Hundred / All (titled "Transfer Cargo"; no order buttons, spec 04 §17 for the simulator's use). The manual says it is not available in simultaneous games, but the executable lights the button and opens the window in both modes (confirmed: binary). | Order T; Combat Simulator. |
| Launch \ Recover Units | Same two-list pattern for units in space. Not available in simultaneous games. | Order U; tactical combat orders. |
| Cargo-type / unit-type picker, then location pick | Deferred Load, Drop, Launch-Remote, Recover-Remote orders. | Orders L, D, I, O. |
| Stellar Manipulation | Location view plus one button per effect (create/destroy planet, star, storm, nebula, black hole; open/close warp point; Construct for ringworld/sphereworld). | Order B. |
| Scrap | Selected Vehicles list (own vehicles at the location in no fleet and not cloaked); Statistics: Scrap Value, Research Potential (None, Minor, Moderate, Sizable or Major, of the last selected vehicle; "None" when any selected one cannot be analyzed), Status, Cost to Unmothball, Can Self-Destruct and Can Be Fired On (Yes only when every selected vehicle can), Space Yard In Sector; Scrap, Analyze, Retrofit, Mothball, Unmothball, Self-destruct, Fire On, each lit only when every selected vehicle qualifies. Rules and turn styles in spec 03 §15 (confirmed: binary). | Order G. |
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
| Combat Replay | Same layout as tactical, playback only; Options, Next. Space = Next, Esc closes. Title strip: "Combat Replay", the location, the combat turn and the empires' flags; no log, event list or summary of any kind (confirmed: binary). OpenSE4 keeps its own list of the turn's events in words and the battle's summary beside the map, an extension (Q39). | Log → Combat Replay. |

### 1.7 Multiplayer, tutorial and end of game

| Screen | Purpose |
|---|---|
| TCP/IP Host | Player list, own IP addresses, a status line naming the current step of the host cycle (spec 05), hideable chat; Begin Game, Process Turn, Add/Remove Empire, Play Turn, Chat, Minimize Game, Quit Game [T]. |
| TCP/IP Player | Source and host IP, player name, status line, chat; Connect to Host, Create Empire, Play Turn, Chat, Minimize, Quit [T]. |
| Movement log replay | Not a window: in simultaneous games the main window replays the host's 30-day movement (full, stepped by day, per ship, rewind) [T]. Exact behaviour: §7 Q51 (confirmed: binary). |
| Tutorial / Scenario text window | Titled text with a 128x128 picture and previous/next through a series; re-opened with Ctrl+H or the "T" button in the status bar [T][M]. |
| Finale | Full picture for victory, defeat, or "human players all dead", chosen from lists in Settings.txt [M]. |

### 1.8 Planets, Construction Queues, Colonies and Ships\Units windows

All of this subsection is (confirmed: binary).

#### 1.8.1 Planets

**What is listed.** Planets in systems the empire has explored. Planets hidden by a
planetary cloak stronger than the empire's sensors are left out; with the Omnipresent
view option only this cloak test applies. Asteroid fields appear only under the
Asteroids tab. A planet counts as a colony only while the empire sees the colony by the
detection rule (spec 01 §6.3), which needs a sensor source in the system. A foreign colony in
a system where the empire has none, cloaked or not, is listed as an uncolonized planet: under
All, Colonizable, Coloniz\Empty and Coloniz\Breathe, never under the colony tabs. A planet
hidden by its colony's cloak is left out of every tab, and the map does not draw it either
(spec 01 §6.9 "What other players see") (confirmed: binary).

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
of them are available. These counts use every non-asteroid planet of the explored systems
and its real owner, with no sight test, so they include planets that the tabs leave out or
list as uncolonized (confirmed: binary). Non-aligned is
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
space yard (the `Space Yard` ability, not cloaked) and is not mothballed. The yard is
rechecked at once on cloak, decloak, mothball and unmothball (and at the vehicle's
updates); when it no longer works, the queue is emptied (items, On Hold, Repeat and
progress).

| Toggle | Includes |
|---|---|
| Ships | queues of ships and bases whose yard is not working right now. Such queues are emptied as soon as the yard stops working, so in practice this group is always empty |
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

**Columns.** Rows are 36 px. Name (170 px): the 36x36 picture at the top left, the name
in white Futurist Medium at (40,0) (with a leading space), and the status icons in one
line from x 40 at y 15, 20 px apart, left to right, without wrapping (the ship set for
ships and bases, the planet set for colonies, §4.4). A tagged row carries the 13x13 green
right-pointing arrow of `General.bmp` (the cell at (203,15)), black transparent, at the
row's top left over the picture, the marker the main ship list uses for tagged ships.
Tab column (150 px), whose header and content follow the tab: Rate (three rates), "Usage
Per Turn" (three usages), Planet Value (three percentages), "Number of Facilities" (used
/ slots, colonies only), "Cargo Space"; under the value, in yellow (#FFFF00) Futurist
small at (column x + 10, 15), the queue's build-mode note when one applies. Construction
Queue (the rest): the first three items in white Futurist small, one per 12 px line at y
−1, 11 and 23, as "<name> x <count>" when the count is above 1 (any kind of item); an
empty queue shows nothing. At the right, right-aligned 2 px from the row's edge at y 0,
the time to finish the **first** item in brackets, "(0.3 years)" (§7 Q48 for the
arithmetic), in white; "(Never)" when the queue's three rates sum to 0 and "(On Hold)"
for a held queue that holds items, both in yellow; "(Repeat)" in white Futurist small at
y 12 under it when repeat is on. A 1 px line in #647EC7 on each row's bottom pixel row
(y 35) separates the rows.

**Sort directions.** Name A to Z; tab column highest first (the sum of the three values,
or the facility count, or the cargo; vehicles count 0 for facilities and cargo); queue
column by the first item's name, A to Z. Default: by name.

**Statistics** (box at about (17,38)): "Resources Generated Per Turn" (the colonies'
production only, the same numbers as the Colonies window's "Total Resources Produced":
no trade, tariffs or remote mining) and "Construction Queue Usage Per Turn" (the queues
not on hold), three amounts each; "Total Space Yards", "Total Planetary Space Yards" and
"Total Ship Space Yards" (queues whose yard works) and "Number Of Queues On Hold", all
counted over every queue whatever the toggles; and the hint "(click item to change its
queue)" in label blue Futurist small at (18,218).

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

#### 1.8.3 Colonies and Ships\Units: columns and sorting

All of this subsection is (confirmed: binary; §7 Q56).

**A sort key is a column, not a position.** Every column of these two windows keeps one
identity in every tab; only the picture and Name appear in all tabs, every other column
belongs to one tab and measures one thing. A key stays that column after a tab change,
so it goes on sorting by its quantity while another tab is shown. Each window has its own
five slots (§7 Q24), stored with the empire and saved with the game; both start with Name
only. Ties go to the next key; after five keys the order is not fixed (the sort is not
stable). Every header can be clicked, the picture's included. A few columns have no key:
a click on one still takes the first slot and pushes the oldest out, but sorts nothing.
Text keys compare by character code, so case matters; only Name ignores case.

**Colonies.** Every tab starts with the picture (40 px) and Name (150 px). Under the name
is a grey second line "type - size", or "Blockaded" in red. The picture sorts by planet
size (the size's number in the data file), smallest first. Widths in px; "rest" is the
remaining width.

| Tab | Columns and keys |
|---|---|
| General | Atmosphere (90): the atmosphere's name, A to Z. Conditions (90): best first. Pop (65; population, the maximum on a second line): highest first. Mood (rest; the mood word, empty without population): the word, A to Z |
| Value | Type (120; the colony type): A to Z. Three value columns (70, 70, rest) in the resource colours: the value percentage, highest first |
| Production | Three production columns (60 each), Res (60) and Intel (rest): highest first. A value is shown in brackets when the colony's output is not delivered (blockaded, or no spaceport serves it) |
| Facilities | Num (50; facilities built): highest first. Max (50; facility slots): highest first. Facilities (rest; the list): no key |
| Cargo | Space (65; space used): highest first. Max (65; capacity): highest first. Cargo Items (rest): no key |
| Construction | Under Construction (200; the first item as "<name>", "<name> x N" when its count is above 1, or "None") and Time Remaining (rest; "0.3 years", "Never", "On Hold", or empty for an empty queue, without brackets): both by their text, **Z to A** |
| Status | The planet status icons (rest): no key |
| Races | Population (rest): highest first |
| Orders | The colony's orders (rest): no key |

**Ships\Units.** Every tab starts with the picture (36 px) and Name (134 px). The picture
and Size both sort by the hull's number in `VehicleSize.txt`, lowest first; a unit group
counts as 100 plus its number of units, so groups come after every ship and base.

| Tab | Columns and keys |
|---|---|
| General | Size (99; the hull's name): hull number as above. Type (120; the design type): A to Z. Move (40; "left/max"): movement left, highest first. Dmg (47; "destroyed/total" components): destroyed components, highest first. Supplies (rest): **lowest first**, unlimited supply counting as 60000 |
| Orders | Class (120; the design name): A to Z. Orders (rest; one order per 12 px line): A to Z by the list written as one line ("None", "REPEAT ORDERS", or the orders joined by ", ", the current one in brackets when Repeat is on) |
| Cargo | Space (65) and Max (65): highest first. Cargo List (rest): A to Z by the cargo in words ("N People, …", or "None") |
| Fleet | Experience (120): **lowest first**. Fleet (rest; the fleet's name or "None"): by fleet number, highest first, vehicles in no fleet counting 0 |
| Maintenance | Three cost columns (70, 70, rest) in the resource colours: maintenance, highest first |

Fleet rows, shown with Show Fleets, always come after every ship and unit row, in the
empire's fleet order, and are never sorted.

**Set Construction Queue.** A right-click on a queued ship, base or unit opens the
design's report: the Design Report window, the same one a right-click on a design in the
buildable list opens (name, size, design type, date created, "(Obsolete)", cost,
maintenance cost, movement, shields, cargo space, supply capacity and the components). A
right-click on a queued facility or upgrade opens that facility's report.

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
| System Display | Display Ship Movement Lines (Ctrl+L flips the same switch, §3.2) |
| Autosave For This Game (pick one) | None; Every Turn; Every 2, 3, 5 or 10 Turns |

**Reset Passwords** (confirmed: binary).
- **When the button appears:** in this window when the game is simultaneous and the machine
  is signed in as Game Master, that is in the host's between-turns view, whatever the play
  style (a hotseat simultaneous game opened as Game Master shows it too).
- **The picker.** A click first discards every reset chosen earlier in this session and not
  yet applied, even if the picker is then cancelled. It then opens the Player Computer Control
  window (§1.2.1) titled "Select Empires to have password reset", listing every empire with all
  lamps off.
- **New passwords.** On OK each lit empire gets a new password: three random numbers from 11
  to 99 written one after another, six digits. A message titled "Password Reset" shows the
  host the player number and the new password, one message per empire. The numbers come from
  a clock-seeded draw, and the game's random sequence is saved before and restored after, so
  the game's random numbers are not disturbed (OpenSE4 simply uses a separate source).
- **When it takes effect.** Not at once: the new passwords are written into the empires when
  the host next processes a turn, right after every orders file has been read and the host has
  chosen to go on. An orders file carries the player's own password, so applying the reset
  after reading the files is what lets it win. The pending resets are not saved: if the host
  quits, or abandons the processing, before then, they are lost.
- **Who is told:** only the host, by that message. There is no log entry and nothing goes to
  the player, so the host passes the password on.
- Our client and hosts have no Reset Passwords (`OptionsScreen` in
  `src/client/classic/screens/settings_screen.cpp`, `src/net/host.cpp`, `src/net/pbem.cpp`).
  Needed: a six-digit password from a non-game random source shown to the host, stored as the
  empire's new `passwordHash` at the next turn processing after the orders are read, pending
  resets discarded on a new click and never saved.

**Music and Settings.txt** (confirmed: binary). The Settings.txt key that turns music off is
`Allow CD Music`. With `FALSE` no track ever plays (§5.5). Opening this window with music off
or not allowed lights "Music Off" and sets the computer's stored music volume to 0 at once; on
Close, music is stored as on only if the player picked a volume lamp, so opening and closing
the window while Settings.txt forbids music stores "music off" on that computer. The Combat
Options "Music On" lamp (§1.10.3) is lit only when music is on and allowed. Our playback
honours the key (`readPlaylists` in `src/client/audio_playlist.cpp`), but the music rows here
(`musicRows` in `settings_screen.cpp`) and the Combat Options lamp
(`src/client/classic/screens/tactical.cpp`) ignore it and do not store "off" on close.

Other Settings.txt keys the client must honour (confirmed: binary):
- `Allow Export of Weapon And Component Data`: when TRUE the Weapons Report gets an Export
  button, which writes four plain-text tables (weapons, components, weapon families,
  component families) to the SaveGame folder, each followed by a message titled "Export
  Successful" naming the path. Ours has no Export button (`src/client/classic/screens/help.cpp`).
- `System Ship Movement Delay Milliseconds`: when the system window animates ship movement and
  the value is above 0, the game waits that many milliseconds after each animated one-square
  step (stock value 0). Ours adds no pause (`main_window.cpp`, `movement_replay.cpp`).
- `Num Finale Lose Pictures`, `Num Finale Human Dead Pictures`, `Num Finale Victory Pictures`
  and `Finale <Kind> Picture N`: the ending window's pictures, from `Pictures/Game/Finale/`.
  All humans eliminated uses Human Dead; "your empire was destroyed" uses Lose; galaxy
  conquered and victory conditions met use Victory. One picture is drawn uniformly from the
  list; the original uses the game's random numbers, OpenSE4 must use a separate source. Our
  client has no ending window with these pictures.
- `Use Old Log Political Message Display`: the log layout (§4.1, §7 Q11).
- `Create Log Text File for Game` is read but never used by the original: nothing to honour.

The same per-computer store keeps the last saved game's name (Resume Game), the tactical
display switches of §1.10.3, and up to ten remembered TCP/IP host addresses and player
names. With nothing stored yet (a fresh install), the game starts with: both animations
on, Sound On on, Classic Sound Effects off (so the remastered set plays), music on at
100 %, Fast Tactical Combat off, ship movement lines off, and the tactical display
defaults of §1.10.3. The same defaults are used for every per-computer value when the store is
missing or any value in it cannot be read. Starting a new simultaneous game (the setup's start,
not loading or joining a game) switches Display Ship Movement Lines on and stores it at once,
so after a player's first simultaneous game it stays on until they turn it off (confirmed:
binary). Our client keeps it off by default (`showMovementLines` in
`src/client/classic/settings.hpp`) but never switches it on for a new simultaneous game.

**Empire Options (Empire Status → Empire Options), per empire.** Stored with the empire
and saved with the game, so each hotseat player has their own. Defaults for a new empire
in brackets.

| Heading | Rows |
|---|---|
| General Options | show the log at the start of the turn [on]; confirm ending the turn [on]; confirm scrapping [on]; confirm stellar manipulation [on]; confirm deleting a research project [on]; confirm deleting an intelligence project [on]; confirm deleting the first item of a construction queue [on]; show the colony-type picker when colonizing [on]; note when similar system-wide abilities exist [on] (never read: the note always appears, §7 Q29) |
| Next/Previous | skip ships under construction [off]; skip damaged ships [off]; stop once per location [off]; skip ships in fleets [off] |
| Ship Movement | avoid minefields [on]; avoid restricted systems [on] |
| Ship Orders | clear orders on entering a system with an enemy [on]; clear orders on entering a system with any other empire [off] |
| System Display | warp point names [on]; planet names [off]; colonizable planets [on]; system grid [off]; coordinate location [on]; then one row per group of facility letter markers, all [off]: R/S/Y (resupply depot, spaceport, planetary space yard), Ca/Cc/Cv (atmosphere, conditions and value changers), St/Ft, Rc/Rr, Sst/Sft, Spv/Spc, Sph/Spa, Scm/Sdm, Srm/Ssm, Spp/Src, Sbe/Sbi, Slr |
| Galaxy Display | Show Grid Lines [on]; Show Warp Lines [on] |
| Latest Items | only latest items for construction [off]; only latest components for designs [off] |
| Politics | automatically claim every system we colonize [on] (stored and shown but never read: founding a colony claims nothing, §7 Q47) |

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
  and otherwise the **Combat Piece Report**: a borderless 310×420 report window built like
  the object reports of §1.4 opened on their own (pages at (10,10), the four 72×30 tabs at
  (10,340), a 153×30 Close button centred under them at y 380), whose Detail page (290×361) holds the 128×128 picture at the
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
  | Supply | ships, bases, fighter and drone groups: now / capacity, each number strictly above 100000 written as its thousands, truncated, with "K" (150999 → "150K", 100000 as it is); "Endless" with unlimited supply. Planets and satellite groups: "Never". Seekers: "None" |
  | Max Targets | the piece's target budget (spec 04 §6) |
  | Combat Group | "Group N - Leader" when it leads group N, else "Group N - Wingman" when it belongs to group N, else None. Fleet groups are numbered groups like the others (spec 04 §3 step 5), so a fleet's ships show their number. A drone group has **Target** here instead: its drone target's name, or None |
  | Formation (not for drone groups) | the formation of the group the piece leads; None for every other piece. The value is not cleared when the piece stops leading, so a former leader still shows it |
  | Conditions (planets with plague) | at y 230, with "Plague N" under it |

  **Tabs** follow the object: a ship or base has Detail, Comps (its design's
  components), Cargo (only when it has cargo space) and Ability; a planet Detail,
  Facil, Cargo (likewise) and Ability; Detail is selected first. A fighter, satellite or
  drone group has no tabs: the Detail page is cut to 249 px and the group's units are
  listed under it in a 108 px unit grid at y 253. A seeker has no tabs, and its Detail
  page shows the picture of the weapon that launched it. Facil lists the colony's
  facilities as they are when the report opens, one cell each, none marked lost; lost
  facilities stay in the list until the battle ends, so during a battle this is the set
  the colony began with. Ability lists, for a ship or base, the abilities of its hull,
  then of its whole design (every component, destroyed or not), then its own; for a
  planet only the planet's own abilities, not its facilities' or its colony's (confirmed:
  binary; §7 Q78).

#### 1.10.2 Tactical Combat Orders

A 326×350 window without border or title bar, holding 11 stacked 306×30 buttons with no
gap between them (330 px from (10,10)). It is modal; a click on a button closes it, and
only then does the chosen order run, so every picker below is a window of its own that
opens after the menu has gone:

| Button | Key | Effect |
|---|---|---|
| Launch Units | L | opens the Launch Units window for the selected piece |
| Launch Fighters in Groups | — | opens a list window titled "Select Fighters Per group", one column "Amount", rows 5, 8, 10, 15, 20, 30, 40, 50 |
| Drop Troops | T | acts at once, no target click: lands all troops of the selected ship on the adjacent colonized planet of another side (the side its piece fights for) that comes last in piece order, **whatever the treaty**; refused when that colony already holds a third empire's troops; ground combat follows at once (spec 04 §11). For a side played by hand a message box titled "Drop Troops" explains a refusal; the tests run in this order: no colonized planet of another side adjacent, troops of another empire already on the colony the landing takes, no units of any kind aboard. A ship with units but no troops is refused without a message. The selected piece's kind is not checked: a colony's planet piece drops the troops of its colony's cargo (confirmed: binary; spec 04 §19.4 Q88) |
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

**Fast Tactical Combat** keeps every animation and removes the waits between frames;
there is no speed factor. Besides, a torpedo then moves 6 px a frame instead of 4, and
the shield picture of a hit absorbed by shields is not stamped. **Each wait** first lets
the program handle its pending window messages once, then busy-waits on the system's
millisecond tick counter until it has advanced by the wait; there is no sleep and no
finer timer. So a wait shorter than the counter's step (about 15.6 ms on current Windows,
1 ms under Wine) lasts until the counter's next step, and in a run of frames each frame
lasts about one step: on Windows a 36-frame slide takes about 0.56 s, not 36 ms. Fast
Tactical Combat skips the wait call itself, message handling included. Without the
switch the Tactical Combat window draws and waits (confirmed: binary; §7 Q77):

| Animation | Frames and waits |
|---|---|
| a piece moving one square, with "animate ship movement in combat" on | slides 1 px a frame along the longer axis, so 36 frames for any step, diagonal too, 1 ms after each; seekers slide the same way. No slide and no wait when either square is outside the shown part of the map: the piece just appears on its new square |
| the same with that switch off | the piece jumps, then 0.1 s (also off-screen) |
| a piece turning to a new facing (switch on, square in view) | before the step, on its old square, the shorter way (clockwise for a half-turn): 5° a frame, 9 frames per 45°, 36 for a half-turn, 0.01 s after each. Otherwise the piece simply faces the new way. Seekers never turn |
| a beam | stamps of the 10x10 centre of the 20x20 beam cell, rotated to the bearing, one every 6 px along the longer axis (6 per square), drawn one by one outward with 0.00001 s after each, then erased one by one in the same order with 0.00005 s after each |
| a torpedo | a 40x40 picture (the 20x20 cell rotated to the bearing) moving 4 px a frame (9 frames per square), 1 ms after each |
| a hit that damages structure or destroys the target | the 8-frame explosion drawn in place (36x36, or 72x72 for some losses), 0.1 s after each frame and after the last is wiped, 0.9 s in all, with a boom sound (boom3 for a loss); a destroying shot plays it once, with no second loss animation |
| a hit absorbed entirely by shields | no explosion and no wait; a single shield picture is stamped (not with Fast Tactical Combat) and the next redraw removes it |
| after a seeker's impact on a piece that survives it | 0.3 s (Tactical Combat only; a direct-fire hit has no pause, nor has a seeker that destroys its target) |

The shot's end point is the target's centre on a hit (for a planet in view, a random
point within its 4x4 block), 17 px short of the centre for a hit fully absorbed by
shields, and on a miss 18 px off the centre in both x and y, each sign drawn at random, so
a miss lands half a square off diagonally; a miss adds nothing else. While a shot at a
one-square target with shields up is drawn, the target shows its 8-frame shield shimmer,
one frame every 8 shot frames. The random draws for a miss's direction and a planet's
point come from the battle's own random sequence (§7 Q77). A seeker's launch is not
animated (the launcher is redrawn and the launch sound plays), and neither are a launch
of units, a landing or a successful capture: the map is only redrawn. A boarding outcome
that destroys both pieces plays two explosions (not traced further).

With the switch on, all of these are drawn without waiting, so the pace is the drawing's
own. Combat Replay draws the same frames with its own switches, without the 0.3 s pause;
Fast Tactical Combat in Combat Replay Options does the same for the replay. Neither
switch touches the Ground Combat window (§1.10.6) or the Strategic Combat window, which
has no animations (§1.10.5).

**Combat Replay Options** (Replay → Options), per empire and saved with the game:
animate ship movement in combat replay [on]; Fast Tactical Combat [off]; Show Viewing
Rectangle on Map [on]; Show Grid [off]; and a Stop Replay button that closes the replay.

#### 1.10.4 Combat Simulator

- **Buttons**, top to bottom: Tactical and Strategic (tabs; Tactical selected); No
  Obsolete (toggle, off, saved per empire; hides only the player's own obsolete designs);
  Strategies (the player's Strategies window); Computer Control (the "Empires Under
  Computer Control" list for the 10 sides, columns Pic with the side's box and Name "Race
  N"; side 1 by hand and sides 2–10 by the computer at first); Fleets For Plr (Fleet
  Transfer for the side chosen in the owner picker, holding that side's ships and bases
  and its copied fleets; fleets, formation and strategy are set there; its Existing
  Fleets button opens the real empire's Ships\Units window, in which a click does
  nothing, §7 Q79); Change Cargo (Cargo Transfer: the setup's holders against a temporary
  Storehouse, spec 04 §17, §7 Q80); Begin; Cancel.
- **Combat vehicles** (left; (17,75), 295×370, 36 px rows), sorted by side 1 to 10, then
  neutral objects. A left-click on a row removes it. Columns: Flag, the side's 26×18 box
  5 px from the column's left and 9 px from the row's top (nothing for a neutral object);
  Pic, the item's picture (a colonized planet's with its population bars in its side's
  colour); Name, the name, then in the small font "Cargo:" (what a ship, base or colony
  holds) or "Units:" (a unit group's stacks) at y 14 and "Fleet:" (its simulator fleet)
  at y 24, labels at x 8 and values at x 44, a missing value "None" in grey (96,96,96)
  (confirmed: binary; §7 Q38). Headings "Combat Vehicles", "Items to choose" and "Owner
  for item" in #7D9FFF 17 px above the lists; the hints "(click combat vehicle to remove
  it)" and "(click item to add to vehicles)" right-aligned under them.
- **Items** (top right; (322,75), 250×230, picture and name): every design whose hull is
  a ship, base, fighter, satellite or drone, both the player's own and the other
  empires' designs the player has seen (no mines, troops or weapon platforms); and the
  objects of the player's home system that are colonized (any owner) or unowned (stars,
  warp points, comets, storms, empty planets and asteroid fields); no vehicle or unit
  group of the real game is offered. Left-click adds the item to the chosen side;
  right-click opens its report. Storms and uncolonised asteroid fields can be added but
  take no part in the battle (spec 04 §17).
- **Owner for item** (bottom right; (322,325), 250×120): a lamp list from "Race 1" to
  "Race 10", each 20 px row with the side's 26×18 numbered colour box at x 80, y 1 (spec 04 §17: 1 red, 2 blue,
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
- **Simultaneous.** The window opens for every battle, computer-only battles included,
  and only when `Simultaneous Games Show Strategic Combat` is on; always in its Begin and
  Close form, never after a notice (§7 Q76).
- **The sequence**, step by step. A battle interrupts whatever started it (a group's
  move, an Attack or Seek order, spec 04 §2); nothing else in the game happens until it
  is over and its window closed.
  1. **Set-up.** The battle is set up at once: pieces made and placed, the phase order
     drawn, each participant's starting pieces recorded for its report, the combat
     replay started when `Create Combat Replay` is on (spec 04 §3, §15).
  2. **Who sees it** is decided as above. A battle nobody sees is fought to its end
     there and then, with every side on its strategies and no window, and the sequence
     goes on at step 7.
  3. **Notice.** On one machine, in a turn-based game, when the empire whose turn it is
     is computer-controlled, the 253×150 notice comes first. It belongs to the question
     form only, so a simultaneous battle never has one (§7 Q76).
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
     map is painted straight onto the window after every single step of any piece or
     seeker, as it is taken, not through a repaint request; after each combat turn
     "Combat Turn N" is drawn straight onto the window and the forces list is repainted at
     once. The window lets the system handle its pending messages after each empire's
     phase and after each combat turn, which serves input and other windows (§7 Q73). After each combat turn and the
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

#### 2.1.1 Choosing the layout, and everything that differs at 800x600

All of this subsection is (confirmed: binary).

**Choice.** The game picks the layout once, at program start, from the desktop width
alone: 800 px or less gives the 800x600 layout, anything wider the 1024x768 layout. The
height plays no part, there is no option, registry value or Settings.txt key for it, and
it never changes while the program runs. The main window always covers the whole desktop
and is black outside the frame. At 800x600 the frame sits at the desktop's top-left
corner (0,0), without centring. At 1024x768 the frame is shifted by ((W − 1024) div 2,
(H − 768) div 2), W×H being the desktop, but only when W is above 1024: a desktop 801 to
1024 px wide gets no shift, and one less than 768 px tall gets a negative vertical shift.
The game makes no minimum-size check (only a refusal to run below 16-bit colour).

**What changes between the layouts.** Positions are frame pixels unless a row says
otherwise; anything not listed (report panel, ship list, report tabs, command buttons,
dialog contents) is the same in both layouts.

| Item | 800x600 | 1024x768 |
|---|---|---|
| Frame strips and intro picture | `Pictures/Game/Screens/800X600/` | `Pictures/Game/Screens/1024X768/` |
| System backgrounds (§2.4, §5.3) | `Pictures/Systems/800X600/`, 490x490 | `Pictures/Systems/1024X768/`, 660x660 |
| `RightFiller` | the file is 1x1 and is not drawn (the game skips it when it is not wider than 1 px) | 62x365 at (958,107) |
| Regions and frame pieces | §2.1 tables | §2.1 tables |
| Sector grid | 36 px cells after an 8 px margin | 50 px cells after a 1 px margin |
| Black transparent in the system panel | also for unmasked system types while the system grid is shown | only for masked system types |
| Order strip (§2.3) | at (230,36), 202x68: 5 columns, 4 pages; pager arrows at (231,44) and (417,44) | at (230,36), 712x68: 20 columns, 1 page; arrows dim at (231,44) and (927,44) |
| Selectors | (448,34) | (965,34) |
| Status bar text (§2.2) | "Game Date" at x 390; resource icons at x 570, 640, 710 | "Game Date" at x 410; icons at x 650, 720, 790 |
| Galaxy panel map area | 272x190 at the panel's (7,0): grid cells of 4x4 px | 342x235 at the panel's (0,20): grid cells of 5x5 px |
| Hover hint (§2.3) | x 100–400, y 123–153 | x 184–484, y 123–153 |
| Coordinate line (§2.4) | (18,577) | (18,745) |
| Planets, Colonies, Ships\Units, Construction Queues | 780x475 like every large dialog; list at (12,252), 563x210 (5 full rows of 36 px) | as tall as the desktop less 100 px (668 on a 768 px desktop), centred; the list grows to the window's client height less 265 |
| Tactical Combat and Combat Replay title strip | title at x 17; "Location" 180, its value 240; "Turn" 370, its value 410; "Empires" 430, flags from 485 | title 17; "Location" 200, value 260; "Turn" 410, value 450; "Empires" 500, flags from 555 |

Within the tactical window two more things follow the desktop width rather than the layout:
the empire flags strip is 168 px wide (room for 6 flags, 28 px apart) on a desktop
narrower than 1024 px and 280 px (10 flags) otherwise, and the target-piece panel exists
only on a desktop at least 1024 px wide. The flags strip is drawn at y 7; the flag of the
empire whose phase it is gets a yellow (255,255,0) box 28x19.

**Windows and dialogs.** Every dialog, report window, picker and prompt has the same size
in both layouts and is centred on the desktop (at 800x600 on an 800x600 desktop that is
the frame). The only exceptions are the four list windows in the table. The main window,
the Tactical Combat and Combat Replay windows are maximized and borderless, so they cover
the whole desktop in both layouts: the tactical W×H of §1.10.1 is the desktop size, not
800x600 or 1024x768. The setup screens (Game Setup, Empire Setup, the Quick Start empire
picker) also cover the whole desktop and lay their contents out in an 800x600 area centred
on it, in both layouts.

Our client follows this: it picks the 800x600 layout when the desktop is at most 800 px
wide and the 1024x768 layout otherwise, with each layout's art folders, regions and frame
strips, sector grid, order strip, selectors, status bar places, galaxy map area, hint and
coordinate line, list window height and tactical title strip from the table. It differs:
- The Graphics setting "Screen layout" and the `--layout` switch can force either layout
  (an OpenSE4 extension, for testing and for large screens).
- The frame is scaled to the window and centred in it, not drawn 1:1 at the desktop's
  corner; a wider 1024x768 window may use OpenSE4's extended arrangement, the 800x600
  layout never does (inferred).
- OpenSE4's own windows taller than 580 px (some pickers) are cut to fit the 800x600
  frame; the setup screens use the layout's frame, not an 800x600 area in both layouts.
- At 800x600 a lesson that points at an order on another page of the order strip
  outlines the pager arrow that leads there (ours: the original has no lessons).
- The tactical window keeps OpenSE4's own panels, laid out over the frame's width.

### 2.2 Status bar

Left to right: large empire flag, empire name, emperor title and name, "Game Date" (the
game starts at 2400.0 and each turn adds 0.1), then stored minerals, organics and
radioactives, each followed by its resource icon (blue crystal, green organic, red
radiation symbol). At the far right a minimize button, and during the tutorial a "T"
button that re-opens the tutorial text [T][S].

Positions (confirmed: binary), relative to the status bar's top-left corner ((11,5) in
the frame); all text is Futurist Medium (§5.4) with its top at y 4:

- the large flag at (4,3); the empire name at x 36 and the emperor's title and name at
  x 220, both white;
- "Game Date" in label blue at x 390 (800x600) or 410 (1024x768), the date in white right
  after it;
- the three stockpiles, each a number in its resource colour right-aligned to end 2 px
  left of its 16 px resource icon; the icons at x 570, 640 and 710 (800x600) or 650, 720
  and 790 (1024x768);
- on a simultaneous host's between-turns screen only "Host" at x 36 and the "Game Date"
  pair at x 400, in both layouts;
- the 20x20 buttons from `Close.bmp`: the minimize button 2 px from the bar's right end
  and 2 px below its top, and while a tutorial or scenario runs the "T" button just left
  of it.

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
  stellar object's sprite (planets, asteroid fields, stars, storms, warp points, comets)
  is drawn with black transparent. When it is FALSE, each is copied as an opaque 36x36
  square, black included, except at 800x600 with the system grid on, where black is
  always transparent so the lines show through. Vehicle sprites (ships, bases, unit
  groups, fleet icons) are always drawn with black transparent, whatever the flag
  (confirmed: binary; corrected, an earlier version applied the flag to ships too).
- **Headings** (confirmed: binary). The minis of hulls with `Requirement Uses Engines :=
  True`, and fighter and drone groups, are turned to the object's heading; bases and other
  engineless hulls, satellites and mines are always drawn upright. A heading is one of 8
  directions in 45° steps clockwise from "up"; a new ship faces up. Each move within a
  system sets it to the bearing from the old square to the new one, rounded to the
  nearest 45° (23–67° gives 45°, 68–112° gives 90°, and so on; 338–22° is up). Arriving
  through a warp point keeps the old heading. The sprite is turned clockwise by that
  angle, sampling each pixel nearest-neighbour with no smoothing (45° views look
  jagged); pixels from outside the source come out black, so transparent. Headings are
  saved with the game.
- **Moves as they are made** (confirmed: binary; found while settling §7 Q62, not traced
  in full). With "animate ship movement in the system window" on, a visible move of a
  group within the shown system first turns the mini 5° a frame with 10 ms after each
  frame, then slides the whole group as one sprite 1 px a frame with 1 ms after each
  (the waits of §1.10.3). After each animated step the game pauses for `System Ship
  Movement Delay Milliseconds`, but reads the value as seconds (a defect of the original;
  the stock value 0 hides it). Our client differs: its glides (`ShipGlides`,
  `client/classic/ship_glides.cpp`) take 0.12 s per square (0.35 to 1.0 s in all), have
  no turn animation, and start every heading facing up after loading.

Contents [T][S]:

- Planets, asteroid fields, the star(s), storms and warp points as 36x36 sprites. A warp
  point whose destination system the empire has explored shows that system's name in its
  cell (with the Empire Option "warp point names"; details below) (confirmed: binary).
- A colonised planet has small population bars at its top right, coloured by owner. They
  are drawn only when the viewer sees the colony (spec 01 §6.9); an unseen foreign colony
  shows the colonisation star instead, when its type fits (confirmed: binary).
- Ships: a single-owner stack shows one ship sprite with a count in the bottom-right
  corner; a location with several empires, or ships orbiting a planet, shows small empire
  flags instead. The counts are drawn in the owner's empire colour (§5.3), next to each
  flag when there are several (confirmed: binary). Exact rules: **Sector contents**
  below.
- A cloaked ship shown as the sector's sprite gets a 1 px dotted circle inscribed in its
  36x36 sprite square, in its empire's colour (dark cyan, RGB(0,128,128), if it has
  none); there is no circle when flags are shown (confirmed: binary).
- Colonisation hint: a small green star on a planet means colonisable and breathable; red
  means colonisable but would be domed; no star means not colonisable by this empire.
- The selected location is framed by four small yellow corner arrows.
- Waypoints 1–10: a cyan rectangle around the sector with the number in cyan. A cyan "M"
  marks the sectors of a second list of locations, probably the tagged minefields of
  Ctrl+T / Ctrl+R (confirmed: binary; the meaning of "M" is inferred).
- Movement lines (the per-computer option "Display Ship Movement Lines", Ctrl+L): the
  planned route of the vehicle or fleet whose report is open, as a blue line through the
  sector centres with a small ring on each square and the turn in which that square is
  reached. Exact rules: **Movement lines** below (confirmed: binary).
- Nebula and black-hole systems use a full background picture instead of a star field.

**Sector contents** (confirmed: binary). "Sprite square" is the 36x36 square centred in
the cell, top-left (X, Y): the cell's corner at 800x600, the corner plus 7 at 1024x768.
For each sector the game sorts what the viewer can see into two groups (stellar objects by
the "seeing the planet" test of spec 01 §6.9, vehicles by the detection rule; the sector click
and the planet names use the same tests):

- **The stellar object**: a planet, asteroid field, star, storm, warp point or comet. With
  several, the first in the system's object list is shown, unless a later planet has a
  larger planet-size `Stellar Size` (stars and other non-planets count as size 0).
- **Vehicles**: ships, bases and unit groups, counted per owner, each object counting 1
  (a group of 20 fighters counts once). Objects without an owner are not counted.

Then, in this order:

- A stellar object is drawn first, by the `Mask Background Objs` rule above. When the
  sector holds more than one stellar object, their number is drawn in white Tiny (§5.4),
  with no background, left edge at X and bottom at Y+36.
- **One owner, no stellar object**: one vehicle sprite, the largest vehicle by hull size
  (a ship replaces any unit group chosen before it); when that vehicle is one of the
  viewer's ships in a fleet, the fleet's icon is drawn instead. When the owner has more
  than one object there, the count goes right-aligned to X+36 with its bottom at Y+36. A
  lone unit group shows its own unit count there instead, in white with no background.
  A cloaked vehicle shown this way gets the dotted circle.
- **One owner, with a stellar object**: no vehicle sprite. The owner's small flag (14x10,
  copied opaque) at (X, Y) and the count at (X+14, Y), even when it is 1.
- **Two or more owners** (with or without a stellar object): no vehicle sprite. Each
  owner's small flag at (X, Y + k × step) with its count at (X+14, Y + k × step), in
  player-number order (k = 0, 1, ...). The step is 10 px, or cell height div owners when
  owners × 10 exceeds the cell height (36 or 50): at 800x600, 4 owners give 9 and 5 give
  7; at 1024x768, 6 owners give 8.
- **Counts** are in Tiny (Windows' Small Fonts, 6 pt), in the owner's empire colour, on
  an opaque black box the size of the text.

**Text in the system panel** (confirmed: binary). Panel coordinates; fonts from §5.4; all
text has a transparent background.

- **System name**: SE4 Text button, white, at (5,10), so (13,123) in the frame. During
  the movement-log replay a bracketed addition follows the name (§7 Q51).
- **Warp point names** (Empire Option "warp point names", on by default): for each warp
  point whose destination system the empire has explored, the destination's name in
  Futurist small, white, centred on the cell, its bottom on the cell's bottom edge. A
  second name in the same sector goes one text line lower (below the cell), and so on.
- **Planet names** (Empire Option "planet names", off by default): each planet the empire
  can see (not asteroid fields), named the same way: Futurist small, white, centred,
  bottom on the cell's bottom edge. Planet names and warp point names are stacked
  separately, so the two can overlap. The planet in sector (0,0) never gets its name: the
  game's loop starts at the second sector.
- **Waypoints** 1–10 of the viewer in this system: a cyan (0,255,255) 1 px rectangle on the
  cell's edges and the number in Futurist small cyan at the cell's bottom-right inner
  corner (right edge 1 px in from the cell's right edge, bottom on the cell's bottom). The
  "M" marker of §2.4 is drawn the same way.
- **Coordinate location** (Empire Option, on by default): while the pointer is over a
  sector of the panel, a line in Futurist small, white, at (10, panel height − 20): the
  word "Coordinates" and the sector as "(x, y)" (column and row, both 0–12), and, while
  the system shown has a marked selected sector, three spaces, "Range:", a space and the
  distance in sectors from it to the sector under the pointer (the larger of the column
  and row differences; 0 on the selected sector itself), all as one string:
  "Coordinates (3, 4)   Range: 2". The selected sector is the last one clicked; it is
  marked, and the range shown, only while it holds at least one object the viewer can
  see, tested at every redraw (§7 Q64). The line is empty while the pointer is over the panel's margin outside
  the sectors. Nothing is ever drawn next to the system name.
- **Facility markers** (the System Display rows of Empire Options, all off by default):
  on each colonized planet (never an asteroid field) that the viewer can see and that is
  the viewer's own or belongs to an empire with a Military Alliance or Partnership with
  the viewer. Letters and their tests are in §7 Q44. They are drawn in Tiny (Small Fonts),
  in the colony owner's empire colour, packed with no spaces, right to left: the first
  letter group (R) ends at the right edge of the planet's 36x36 sprite square with its
  bottom on the square's bottom, and each further group is placed immediately to the
  left of the one before, in the order of the table in Q44 (so a colony with a value
  changer, a space yard, a spaceport and a resupply depot reads "CvYSR"). A group that
  would start at or left of the square's left edge starts a new line one text height
  higher, again ending at the right edge.
- **Notices**: "Unexplored" (§2.4, background); on a simultaneous host's between-turns
  screen, a line telling the host to press End Turn; and "Player N taking turn..." while
  the panel is blanked for another player's move. All in SE4 Text button, white, asked
  for at 24 px (drawn at the face's only size, 17 px (inferred)), centred, top at one
  third of the panel height.

**Movement lines** (confirmed: binary).

*Whose line.* There is at most one line. It belongs to the single object whose report is
open in the report panel (§2.5), and only to a ship or base of the viewer (the current
player), a fighter group or drone group of the viewer, or a fleet. Nothing is drawn for other
empires' vehicles (allies' and scanned ships included), satellite groups, minefields, planets,
stars, storms, warp points, the system report, or a sector list showing several objects;
tagging ships adds nothing. The object needs at least one order. Selecting a sector replaces
the line: the new report sets its own, and an empty sector or a list removes it. Showing
another system (a click in the galaxy panel, Find System) leaves the report open, so the line
is kept and the panel draws the part of the route that lies in the system now shown.

*The route.* It starts at the object's current square (the fleet's location for a fleet) and
follows the order list in the order the orders will be carried out; with Repeat on it starts
at the order now due and goes once round the list.
- Only Move To, Move To Waypoint and the Seek orders aimed at a location add to the route.
  Composite orders (Warp, Colonize, Load and Drop Cargo, Explore, Resupply, Repair, Set
  Patrol) store their moving part as an ordinary Move To (spec 03 §8), which counts. Every
  other order adds nothing (the Warp itself, Attack and its pursuit, Sentry, the Colonize
  action) and the route goes on from where the last move ended.
- Move To Waypoint uses the waypoint's location when the line is worked out; an unset
  waypoint adds nothing.
- A move within the current system follows the in-system step rule of spec 03 §6.2 square by
  square (the greedy steps with their replacement squares, or the destructive-centre cost
  map). A move to another system uses the Move To route search of spec 03 §6.2: in-system
  steps to each warp point, then the jump. An unreachable destination adds nothing, and the
  next order starts from the same square.
- The route is a list of points: the start square, then one point per square entered; a jump
  gives one point, the exit warp point's square in the next system. Consecutive repeats are
  dropped. Every point after the start is one movement point. Let s be a point's number of
  steps from the start (s = 0 for the start).

*When it is worked out.* Whenever the open report is set up or refreshed: when the object is
selected, after any order is given to it, and after it carries out orders (in a turn-based
game the line shrinks as the ship moves). When the result differs from the stored route the
system panel is redrawn. Because a blocked step takes a random replacement square, the
original draws from the game's own random sequence each time it works a line out, and the
detour shown can differ from the one taken later. OpenSE4 must not copy this: it uses a
display-only random source (or a fixed choice), so that showing a line never changes game
results.

*Turn numbers.* Let A be the movement the object has left now and B its full movement per
turn (0 when mothballed). For a fleet, A and B are the smallest values among its members at
the fleet's location (both 0 when there are none). A point's number is 0 when s ≤ A;
otherwise ((s − A − 1) div B) + 1 when B > 0, and 0 when B = 0. So 0 means reached with the
movement left now, 1 during the next turn, and so on. In a turn-based game A is what is left
this turn; in a simultaneous game a vehicle normally has its full movement during the
player's turn, so the squares reached in the coming turn processing read 0.

*Drawing.* Only while the option is on, and only the points that lie in the system shown.
Nothing is drawn over an unexplored system, or while the panel is blanked for another player
or for the host's notice.
- With (X, Y) the point's sprite-square top-left ("Sector contents" above), its centre is
  C = (X + 18, Y + 18): the sector centres of the geometry table, frame (34 + 36c, 139 + 36r)
  at 800x600 and (34 + 50c, 139 + 50r) at 1024x768.
- Pen: solid, 1 px, pure blue RGB(0,0,255). No fill: rings are outlines, and the text has no
  background.
- The points that lie in the shown system are handled in route order:
  1. Except for the start point, a ring: a Windows ellipse on the box from C − (4,4) to
     C + (4,4), right and bottom edges excluded, so 8 px across, spanning C − 4 to C + 3 on
     each axis.
  2. If an earlier point of the route was drawn in this system, a line from that last drawn
     centre to C (a Windows line, so the end pixel C itself is not set). Then the number of the
     route point just before this one is written, centred on that last drawn centre.
  3. If this point is the last of the route, or the next point lies in another system, its own
     number is written, centred on C.
- Numbers are written after the line through their square, so they sit on top of lines and
  rings. Text: the decimal number in Tiny (Small Fonts 6 pt, §5.4), white RGB(255,255,255),
  no background, top-left at (C.x − (w div 2), C.y − (h div 2)), w and h being the text's width
  and height.
- So the start square always shows a "0" over the vehicle's own sprite (A is never negative),
  and a vehicle whose orders include no move shows only that "0".
- Quirk: the last drawn point is not forgotten when the route leaves the shown system. A route
  that leaves and later comes back draws a straight segment from the last square before
  leaving to the first square after returning, and the last square before leaving gets a
  second number over its first (that of the route point just before the return). OpenSE4 may
  break the line there instead.
- The line is drawn last, over the background, sprites, counts, flags, names, facility markers
  and waypoint frames, and again after any single sector is redrawn (for example during the
  movement animation).

*The switch.* While "Display Ship Movement Lines" is off, the stored route is neither updated
nor cleared. When Ctrl+L switches it back on, the panel shows the last route stored while it
was on, which may belong to another vehicle or be out of date, until the open report is next
refreshed. OpenSE4 should work the route out again when the option is switched on.

Our client differs (`src/client/classic/main_window.cpp`, the "Movement line for the selected
own vehicle" block and the Ctrl+L handler):
1. Style: a dashed light-green line (1.5 px, 6/4 dashes) instead of a solid 1 px pure blue one.
2. Shape: straight segments from the vehicle to each order's target, instead of the route
   square by square. A display-only form of the engine's greedy step (now inside
   `src/game/movement.cpp`, drawing from the game's generator) is needed; `game::findPath` is a
   shortest-path search, not that step, and can show a different route.
3. No rings, no turn numbers, no "0" on the start square.
4. Every order with a location counts, Warp and Colonize targets included; the original counts
   only Move To, Move To Waypoint and the location Seek orders, with waypoints looked up when
   the line is worked out.
5. A segment is drawn only when both ends are in the shown system; the original draws every
   route point in it, so a line can end at a warp point and start again at an entry point.
6. It follows the selected vehicle, satellites and mines included, with its fleet's orders; the
   original follows the open report (the viewer's ship or base, fighter or drone group, or a
   fleet, with A and B from the members). Ours also hides the line during the movement-log
   replay (the original's behaviour there was not examined).
7. It is drawn before the selection corners and waypoint frames; the original draws it last.
8. Ctrl+L flips the switch and saves the settings at once; the original only flips it (§3.2).
   Keeping ours is harmless. Ours never shows a stale line, since it draws from live state.
9. Starting a new simultaneous game does not switch the option on (§1.9).

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
- A hotkey runs its order only when that order's button is lit. The exception is the
  four movement-log keys (Ctrl+P, Ctrl+O, Ctrl+I, Ctrl+U), which start the replay
  without checking their buttons (§7 Q51).

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
when its button is lit (§2.8; the movement-log keys are the exception). The main window
has no Esc or Enter binding (confirmed: binary).

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
| Ctrl+L | Toggle ship movement lines: flips the per-computer Display Ship Movement Lines switch (§1.9) and redraws the system panel. It is not stored at once but with the other per-computer settings the next time they are saved (a game save or autosave, closing an Options, Combat Options or Combat Replay Options window, starting a new simultaneous game); loading or starting a game before then reads the stored value back and undoes the toggle (confirmed: binary). Ours saves at once. |
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
  turns. Entries are listed in the order they were made, without sorting. Diplomatic
  messages and refused orders are ordinary entries in that order (§7 Q42).
- **Filters.** All is always enabled; a category button is disabled when the turn has no
  entry of that category. Each filter click is stored with the empire (so saved with the
  game) and restored on every later opening; if that category has no entries now, the
  window opens on All and stores All. A filter click shows the filtered list scrolled to
  the top with its first row selected (none when it is empty).
- **Selection.** When the Log closes, and when Goto is pressed, the window stores the
  selected entry's index in the empire's whole log (not its row in the filtered list)
  and the scroll position with the empire. The next opening restores both if the entry
  at that index is in the filtered list, otherwise selects the first row. Since an index
  is stored, a later turn re-selects whatever entry now has it.
- **Send Reply** is enabled only for diplomatic messages. It opens Communicate addressed
  to the sender, or shows a "Cannot Reply" notice when a message already went to that
  empire this turn.
- **Combat Replay** is enabled only for combat entries, and only when Settings.txt
  `Create Combat Replay` is TRUE. When the replay closes, a new background track starts.
- **Constr. Queues** is always enabled and opens Construction Queues over the Log.
- **Goto** is enabled when the entry has a target, which is fixed per kind of entry when
  it is made (§7 Q41). A location target closes the Log and shows that system in the
  main window with the sector selected; if the entry names no system, nothing happens
  and the Log stays open. A window target opens Research, Intelligence or Empires over
  the Log, which stays open. (The handler also knows Construction Queues, Empire Options
  and Designs, but no entry is ever made with those targets.)
- **Details.** A normal entry shows its picture, its title, "Date:" and its text. A
  combat entry shows its picture and "Combat in <system>" in the button font; "Date:"
  at +4 with its value at +40, "Coord:" at +132 with its value at +180 (labels in label
  blue, values white); then the headings **Combat Forces** (x +4) and **Damage** (x 200)
  over a list of 20 px rows: per empire in the battle a row with its flag (x 4) and name
  (x 34), then one row per piece of that empire present when the battle began, in setup
  order (no neutral obstacles, seekers or units launched during the battle). A ship or
  base row reads "<name> (<hull Code>)", other rows the name alone; the text starts 12 px
  in and is cut, without an ellipsis, before the Damage column at x 200. The Damage value
  is fixed when the battle ends: "N%" with N = 100 − round(remaining ÷ full × 100)
  (halves to even), 100 when full is 0 and 0 when remaining exceeds full; for a ship or
  base, full is the design's whole structure and remaining the structure of its intact
  components less its damage pool (so damage from before the battle counts); for a unit
  group, full is all its units at full health, including those killed in this battle,
  and remaining the living units' hit points less the group's pool; for a planet, full is
  its hit points (spec 04 §11) at the start and remaining its hit points at the end (lost
  facilities do not count). A piece whose name is not among its empire's survivors shows
  "Taken" when the name is among another empire's survivors (a captured ship, a conquered
  planet), otherwise "Dead" (so a colony whose population died is "Dead"); the match is
  by name. All of this text is white. (The manual describes Start and Lost counts here;
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
- **Political messages** from other empires (§4.2): each delivered message, acceptances
  of trades, gifts and tributes included, becomes an ordinary entry when it arrives,
  titled "Message", category Politics, Goto Empires, its text naming the sender and
  quoting the message (confirmed: binary).
- **Packages** (trade, gift, tribute): when the acceptance arrives, each item carried out
  makes its own entries, the receiver's first, then the giver's, category Politics; the
  package as a whole makes none. The kinds, titles and Goto targets are in §7 Q70
  (confirmed: binary).
- **Refused and failed orders**: each one is an ordinary entry made when the order fails,
  with its own title (unable to move, unable to drop cargo, cannot mothball or unmothball,
  retrofit problem, launch and recover warnings, colonization failed and so on), category
  Misc (scrap and retrofit results: Construction), Goto the object's location (confirmed:
  binary).

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
  construction queue) (confirmed: binary; which actions ask: §7 Q28, Q46); always-on
  prompts for surrender, quitting as TCP/IP host or player [S][M]. In every Yes/No prompt, Esc and Enter mean No (§3.4).
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
| 1 | 5, else 4 | out of supplies (supplies 0); else low supplies (strictly below `Supply Amount for Low Supply Warning`, 1000 in stock data). Not for mothballed ships; no exception for ships that use no supply or have unlimited supply. A fighter or drone group is out at 0, and low below a tenth of the setting (integer division) only while it holds at least one unit; satellite groups and mine fields never show either cell (§7 Q54, Q61) |
| 2 | 33 | has damaged components |
| 3 | 16 | damaged, and a repair source of the same owner is in the sector |
| 4 | 3 | the first order is Sentry |
| 5 | 9 | cloaked |
| 6 | 0, else 11 | has a working space yard as of its last update and is not cloaked (a mothballed, uncloaked yard ship still shows 0); otherwise 11 if it has `Component Repair` |
| 7 | 2 | repeat orders on |
| 8 | 10 | under minister control |
| 9 | 6 | mothballed |
| 10 | 12 | building: not mothballed, a working space yard as of its last update, and at least one item in its queue, held or not |
| 11 | 25, 18, 20, 19, 26, 37, 22 | cargo, in this order: troops, fighters, mines, satellites, weapon platforms, drones, population |
| 12 | 36 | remote mining: the design has `Remote Resource Generation`, the sector holds an uncolonized planet or asteroid field (visible or not), and this is the first object with that ability in the system's object list in that sector, whatever its owner (the list keeps the order in which objects entered the system (inferred)); only that object shows it |

**Planets:** cell 9 cloaked; 0 working space yard (a cloaked colony's yard does not work,
spec 01 §6.9); 10 minister control; 12 building, with at least one item in the queue, held
or not (every colony has a queue); 15 `Ancient Ruins` or `Ancient Ruins Unique` (shown on any
planet); 17 domed (the population cannot breathe the atmosphere); 11 can repair, only when
cell 0 is not shown; then the seven cargo cells in the ship order; 30 not connected (the
empire has a spaceport-type facility somewhere and none serves this colony, so its resources
are not delivered). The planet report also draws the domed icon at (230,196).

**Fleets:** cell 10 when the fleet's own minister flag is set, then 9 when any member
belonging to the fleet's owner is cloaked (confirmed: binary). Unit groups' list rows use
the ship routine.

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
    and `ArrowN/NE/E/SE/S/SW/W/NW` (hotspot 15,15), the tactical-map move pointers. How
    they are loaded and where each is used: §5.8 (confirmed: binary).

### 5.4 Fonts

`Fonts/` holds six Windows 3.x raster font files (`.fon`: an NE container with one font
resource each, one fixed pixel size, proportional, ANSI character set, characters 32–255,
regular weight, made for 120 DPI) [M]:

| File | Face name in the file | Cell height | Ascent | Internal leading | Character height | Registered by the game |
|---|---|---|---|---|---|---|
| `FutMed.fon` | Futurist Medium | 16 | 13 | 3 | 13 | yes |
| `FutSml.fon` | Futurist small | 12 | 10 | 2 | 10 | yes |
| `SE4TXBTN.FON` | SE4 Text button | 17 | 12 | 0 | 17 | yes |
| `SE4BLK1L.FON` | SE4 Block 1 Large | 15 | 13 | 3 | 12 | yes, but never selected |
| `SE4BLK1M.FON` | SE4 Block 1 Medium | 12 | 10 | 2 | 10 | no |
| `SE4BLK1S.FON` | SE4 Block 1 Small | 9 | 7 | 1 | 8 | no |

The three SE4 Block faces have capitals only.

**Loading** (confirmed: binary). At program start, before any window opens, the game
registers four of the files with the system for the session (the system's
add-font-resource call: nothing is copied into the system font folder and nothing is
written to the registry), in this order: `FutSml.fon`, `FutMed.fon`, `se4blk1l.fon`,
`se4txbtn.fon`, each looked up in the active mod's `Fonts/` first and then in the base
`Fonts/`. It removes them again when the program exits. The two other SE4 Block files are
never registered. All drawing then asks the system for a face by name and size.

**The five font settings** (confirmed: binary). The drawing code sets fonts in only these
ways; all are regular weight, not italic, not underlined:

| Setting | Face asked for | Size asked for | Drawn at | Default colour |
|---|---|---|---|---|
| Body | Futurist Medium | 10 pt (13 px at 96 DPI) | the file's 16 px cell | white |
| Small | Futurist small | 8 pt (11 px) | the file's 12 px cell | white |
| Button | SE4 Text Button | 12 pt (16 px) | the file's 17 px cell | white |
| Button column | SE4 Text Button | 12 pt | 17 px | the button's state colour (below) |
| Tiny | Small Fonts | 6 pt (8 px) | the system's own size | white |

- Each file holds a single size and every request is close to it, so the text always
  appears at the file's own pixel size, unscaled, at 96 or 120 DPI alike (Windows scales
  raster faces only by whole multiples (inferred)). The face names match the files'
  names ignoring case ("SE4 Text Button" finds "SE4 Text button").
- **Small Fonts** is Windows' own small raster face, not part of the install. It is used
  only for the smallest numbers and letters on the maps (table below). An implementer
  needs a substitute: a small raster face about 8 px tall with digits and the facility
  marker letters (inferred).
- "SE4 Block 1 Medium" appears only as the design-time font of the report windows, and
  "MS Sans Serif" only in a few leftover standard controls (the data-error list window and
  an unused system finder); every visible text has its font set by code, so neither face
  is seen in normal play.

**Colour and smoothing** (confirmed: binary). Text is drawn in one solid colour with a
transparent background (the one exception: the ship counts on system-panel sectors sit on
an opaque black box the size of the text, §2.4), without outline or shadow (the one
shadow: on the intro screen the
version line and the loading line are drawn first in black 1 px right and 1 px down, then
in white). There is no anti-aliasing: raster fonts are never smoothed, so every glyph pixel
is either the text colour or left as it was. Text colours used:

| Colour | RGB | Used for |
|---|---|---|
| white | (255,255,255) | default: names, values, list rows, titles |
| label blue | (125,159,255), #7D9FFF | field labels, captions over lists and boxes, sortable column headings, the "(click ...)" hints |
| grey | (160,160,160), #A0A0A0 | second lines of list rows (Planets, Colonies), descriptions in reports |
| silver | (192,192,192), #C0C0C0 | the block headings of Empire Status (Button face) |
| yellow | (255,255,0) | states and warnings: "(Obsolete)", queue modes, "Never", "On Hold", "Victorious!" |
| resource colours | (70,101,204), (0,128,0), (255,0,0) | minerals, organics, radioactives amounts |
| cyan | (0,255,255) | waypoint numbers, the "M" marker, hovered system names and Show Names |
| blue / red | (0,0,255) / (255,0,0) | shield and damage numbers of the tactical target panel |
| owner colour | §5.3 | ship counts on system-panel sectors; facility letter markers |

**Button column labels** (the 180-wide buttons of every large dialog and the tactical
buttons) are centred, with their top at (button height − text height) / 2 + 2, in the
state colour, which the button's outline also takes: normal (97,123,194) #617BC2; under the
pointer (108,138,220) #6C8ADC; while the mouse button is held on it (125,159,255) #7D9FFF;
disabled (45,45,45) #2D2D2D. Under the pointer and while held, the `RowGrid.bmp` texture
is drawn behind the label.

**Where each setting is used** (confirmed: binary; positions are in the window or panel):

| Text | Setting | Colour, position |
|---|---|---|
| Large dialog titles | Button | white, at (17,11) |
| Titles of small windows (text prompts, pickers, check lists, the orders list, Create Fleet, population, the design-name picker, TCP/IP windows) | Button | white, at (10,10) |
| Message boxes | Button title, Body text | title white centred at y 10 |
| Captions over lists and boxes ("Options In Use", "Log Messages", "Research Areas" and so on) | Body | label blue |
| Empire Status block headings | Button | silver |
| "(click ...)" hints under lists | Small | label blue, usually right-aligned to the list's edge |
| List rows, statistics values, box contents | Body | white or the column's colour |
| Second lines of list rows | Small | grey in Planets and Colonies; white in the ship list ("<design> Class" at (46,16) of the row, under the name at (40,0)) |
| Column headings of sortable lists | Body | label blue; under the pointer (140,166,255) and while held (196,209,255), both over the `RowGrid.bmp` texture; grey (128,128,128) for a column that cannot be clicked; resource-value columns always in their resource colour |
| Main-window status bar | Body | §2.2 |
| Hover hint of command and order buttons | Button (name), Small (key) | white, §2.3 |
| System panel: system name and notices | Button | §2.4 |
| System panel: warp point and planet names, waypoint numbers, "M", coordinate line | Small | §2.4 |
| System panel: ship counts, unit-group counts, seeker counts, facility letters, movement-line numbers | Tiny | §2.4, §7 Q44, Q49 |
| Quadrant maps that label every system in white | Small when a grid cell is less than 12 px tall, else Body | white |
| Galaxy maps: hovered name, Show Names | Body | cyan |
| Galaxy Map window: Show Distances numbers | Tiny | white, just right of the system's cell, bottom 2 px above its top |
| Report panel and report windows: object name | Button | white, centred over the area right of the picture, top at y 4 |
| Report labels and values | Body | labels label blue at x 130, values white at x 140 one line (15 px) lower |
| Report descriptions and fine print | Small | grey |
| Log details: entry title, "Combat in ..." | Button | white |
| Tactical Combat and Combat Replay: window title | Button | white, at y 11 (x by layout, §2.1.1) |
| Tactical: "Location", "Turn", "Empires" and their values | Body | labels label blue, values white, at y 9 |
| Tactical piece list: name; "Size", "Move" or "Dist" with values | Body; Small | white; labels label blue |
| Strategic and Ground Combat: labels and values; planet name | Body; Button | label blue and white |
| Intro: version and loading lines | Body | white with the black shadow |

Our client follows this: it reads `FutMed.fon`, `FutSml.fon` and `SE4TXBTN.FON` with its
own reader, from the active mod's `Fonts/` first, and draws them at their own pixel
sizes with no smoothing (each pixel ink or nothing), in the colours above; button
captions and outlines take the four state colours with `RowGrid.bmp` behind the label
under the pointer and while held, and the caption's top follows the rule above; warp
point and planet names, the coordinate line and waypoint numbers use Futurist small. It
differs:
- The map numbers and letters (Tiny) use OpenSE4's own small raster face, drawn for the
  client: an 8 px cell (ascent 6), capitals and digits 5 px tall, characters 32–126
  (inferred stand-in for Small Fonts, §7 Q60).
- The frame is scaled to the window, so at a scale that is not a whole number the glyphs'
  pixels are repeated unevenly (nearest neighbour) rather than drawn 1:1.
- OpenSE4's own windows (Learn, the manual, the lesson panel) keep our own font (Noto
  Sans) for their text, and it stands in for any `.fon` file that is missing.

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

### 5.8 Pointers

All of this subsection is (confirmed: binary).

**Files.** Twelve `.cur` files in `Pictures/Game/`, each one 32x32 image with 1 bit per
pixel: every pixel is black, white or transparent (none inverts the screen). Hot spots,
from the files: `Normal.cur` (2,2); `Hourglass.cur`, `Select.cur`, `Target.cur` and the
eight `Arrow<dir>.cur` (15,15). The game uses no other pointer, built-in or from the
system, except over the few leftover standard controls of §5.4.

**Loading.** At program start, right after the fonts, the game loads all twelve with the
system's load-cursor-from-file call (the active mod's `Pictures/Game/` first, then the
base folder) and registers them as its own pointer shapes. The file names it asks for
are lower case (`normal.cur`, `arrowne.cur`); the files on disk are mixed case.

**Where each is shown.**

| Pointer | When |
|---|---|
| `Normal` | Every game window sets it as its own pointer when it is created: the main window, all dialogs, report windows, pickers, prompts, the setup screens and the intro (not the 1.95 launcher, which keeps the system's arrow). So it shows everywhere, over the system panel, the galaxy panel and the Galaxy Map window too. The main window has no other pointer: picking a destination for Move To, a waypoint or a target keeps `Normal`. |
| `Hourglass` | For the whole program, over every window, while it saves or loads a game, map, empire, player (`.plr`) or turn file, while a simultaneous host processes the turn, while a combat replay file is read, and while the Tech Tree window or the Weapons Report exports its text files. Afterwards the program-wide pointer is set back to `Normal`. |
| `Target`, `Select`, `Arrow<dir>` | Only in the Tactical Combat window, over the map (§1.10.1). The arrow follows the signs of the column and row offsets from the selected piece to the square under the pointer: right and above NE, right E, right and below SE, below S, left and below SW, left W, left and above NW, above N; the piece's own square gives `Normal`. Before Begin is pressed, outside the map and while the window is busy the pointer is `Normal`. |
| (Combat Replay) | `Normal` over the whole window; the replay has no move or target pointers. |

Our client follows this: it decodes the twelve files with its own reader (the active
mod's `Pictures/Game/` first) and shows them as the program's pointers with their hot
spots: `Normal` over every classic window and OpenSE4's own, `Hourglass` while it
processes a turn, saves or loads a game, saves an empire or exports the tech tree, and
`Target`, `Select` and the arrows on the tactical map by the rules above. It differs: the
pointers grow with the classic screens by whole multiples (nearest neighbour) on a
scaled window (§7 Q63); a missing file falls back to the system's own pointer of that
kind, and the tactical map then draws its own move arrows and crosshairs.

---

## 6. File formats of the player-data folders

| Folder / file | Kind | Structure |
|---|---|---|
| `Dsgnname/*.TXT` | text, CRLF, Windows-1252 (some accented names) | One candidate design name per line, no header, alphabetical; 15 lists of 40–436 names with DOS 8.3 upper-case file names. The empire's "Design Name File" picks one; the designer offers names from it [M][T]. The player's name picker lists every line in file order; clicking one picks it, or the player types a name. Computer players and ministers take the next name no existing design uses; when the file runs out they reuse it with Roman numerals II to XV added; with no file, names are "Design <n>" (confirmed: binary). **Ship names** are the design name, a space and a four-digit serial with leading zeros, such as "Hood 0007"; the serial is one more than the highest serial among existing objects of that design (read from the last four characters of their names), so a number freed at the top is reused. The Combat Simulator numbers its ships with a counter per side instead (confirmed: binary). Manual captures show a bracketed hull `Code` after the serial [S]; the 1.95 executable never puts VehicleSize.txt `Code` into a name. It shows it in one place only: the Log's combat details, where a ship or base row reads "<ship name> (<Code>)" (§4.1, §7 Q18) (confirmed: binary). |
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
  path when set and present, otherwise `SaveGame`. Every successful save of a game file
  records its full path as the "last saved game" that Resume Game loads: Save Game, every
  autosave, the per-player turn-based saves, the multiplayer and host saves, and even the
  `temp/<game>_CurrTurn.gam` written when a movement-log replay starts (§7 Q53).
- **Save Map** writes `Maps/<name>.map`; **Save Empire** writes `Empires/<name>.emp` under
  the name typed in the empire file picker (§7 Q72).
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
  - log copy: a header row with "Date" at column 1, "Header" at column 10 and "Text" at
    column 51 ("Date", 5 spaces, "Header", 35 spaces, "Text"); a line of 78 dashes; then
    per entry the date with one decimal (2400.1) padded with spaces to 9 characters, the
    title padded to 40, one space, and the text with each CR LF pair turned into one
    space (a title longer than 40 is followed by that single space only). CR LF line
    ends. The file is rewritten each turn only when the setting is on and the log is not
    empty; otherwise the old file is left as it is.

---

## 7. Open questions to verify in the running game

Questions 1, 2 and 4–23 were settled from the executable (code and form resources).
Each answer gives the result, points to the main section that now holds the details, and
says where our client differs ("Our client differs: ..."), so the client can be changed
from the text alone. Questions 24–55 began as OpenSE4's own choices (inferred) where those
rules left something open. On 2026-10-01 they were settled from the executable too, each
with an **Answer** marked (confirmed: binary) and the differences of our client or engine:
Q24–Q29 and Q40–Q55 and the last part of Q18, then Q30–Q39 (the combat windows). The
questions our implementation of those answers raised (Q56, Q61–Q64, Q70–Q82 and the open
part of Q38) were settled from the executable the same day; Q63, Q71, Q74, Q75 and Q81
are OpenSE4 choices with no counterpart. Still open, for an observer of the running game:
Q60 (the size of the original's small map lettering), what a viewer sees of a strategic
battle (Q73) and the measured timings of Q77. Fonts and pointers are in §5.4 and §5.8,
the 800x600 layout in §2.1.1.

1. **1024x768 layout.** *Answered in spec 07 §UI (panel rectangles and frame strips).*
   Exact panel rectangles; what fills the 67 px right strip and
   RightFiller (more order buttons?); whether the layout follows the desktop resolution
   or an option. **Answer:** the desktop width alone picks it (800 px or less: 800x600),
   with no option; every difference between the two layouts, including the 800x600
   positions, is in §2.1.1 (confirmed: binary). Our client follows this; its differences
   are listed at the end of §2.1.1.
2. **System grid pitch.** How does the 13x13 grid map onto the 490 and 660 px
   backgrounds, and are 36 px sprites scaled at 1024? **Answer:** neither background is
   divided by 13. The panel is 484x484 (800x600) or 652x652 (1024x768) at (8,113); its
   cells are 36 px after an 8 px margin, or 50 px after a 1 px margin. Sprites are never
   scaled: each is drawn 36x36, centred in its cell. The background is copied 1:1 from
   its top-left corner, so its right and bottom edges are cut off. An empire option
   draws grid lines. Details in §2.4 (confirmed: binary). The executable's sector
   centres at 1024x768, (34 + 50c, 139 + 50r), lie 1 px right of and 4 px below the
   centres measured on a capture in spec 07; use the executable's.
   Our client follows this in both layouts (the panel, the 8 or 1 px margin, the cropped
   background, 36x36 sprites at native size, the grid lines).
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
   - The movement log replay is ours where the engine differs (Q51).
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
   the Stellar Manipulation preview, and at 800x600 it keys black for unmasked types too
   while the grid shows. It differs: it picks the combat tiles from a seed made from the
   battle's place, not from the game's random numbers (inferred).
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
   It still differs: the Players window is read-only (§1.2.1); there is no Reset
   Passwords (§1.9); the Options music rows ignore `Allow CD Music` (playback honours it);
   the other Settings.txt gaps are listed in §1.9; our own choices are in Q41–Q48.
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
    Our client follows this, with the answers of Q41–Q43: messages from other empires are
    ordinary "Message" entries in the order they arrived, and a combat entry lists each
    piece's damage fixed when the battle ended. It still differs: a message's details keep
    our layout whatever `Use Old Log Political Message Display` says; commands a network
    or play-by-e-mail host refused follow the entries as Misc rows (Q71).
12. **Save folder contents.** **Answer:** see §6.1: the folders created at start-up; the
    names and places of saves, maps, empires and autosaves (`AutoSav<d>.gam`); the
    per-player turn saves; the `.plr`, `.trn` and `.cmb` names; the `History/plr_<N>_*`
    files and their copies next to each save; what goes into `temp/`; and the fixed-width
    layouts of the history files, which are plain text (confirmed: binary).
    Our client names autosaves `AutoSav<d>.gam` and writes `History/plr_<N>_*.txt` with
    the layouts above, copies them next to each Save Game and autosave and restores them
    on loading. It differs: its saves and `History/` live in the user-data folder, not
    the install, and keep our own save format (fine, §6). The log copy's layout matches
    (Q55).
13. **Status icons.** **Answer:** drones in cargo use cell 37, not 34. The executable
    draws 23 cells and never the other 15. §4.4 lists each cell, its condition and the
    drawing order for ships, planets and fleets, including cell 36 (remote mining),
    which the manual does not mention (confirmed: binary).
    Our client draws these cells in this order for ships, fleets, planets, ship-list rows,
    reports and the Colonies list, with the conditions of Q50, a cloaked colony's cell 9
    first (since 2026-10-01); the rest is in Q50.
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
    a ship's name. **Answer (the remaining part):** VehicleSize.txt `Code` appears in
    exactly one place: the Log's combat details, where each ship or base row reads
    "<ship name> (<Code>)" (§4.1). It is not in ship names, not in ship-list rows (whose
    second line is "<design name> Class"), not in the ship report (Class is the design
    name, Size the hull name with its tonnage), and not in design names. The "Code" label
    in the Help window belongs to the Weapon Mount report and shows the mount's code
    (confirmed: binary). Captures in the manual that show the code elsewhere come from an
    older version (inferred); nothing remains open.
    Our client follows the serial rule (`game::nextVehicleName`), its Log combat rows
    carry the "(<Code>)" suffix (Q43), and its ship-size report shows the hull name
    without the code.
19. **Planets filters.** What does Coloniz\Empty add to Colonizable, which treaties make
    a colony an ally one, and what makes a planet Special? **Answer:** Coloniz\Empty means
    the planet itself is not colonized (not a test on the system). Ally means
    Non-Aggression or better; every other empire, including one not yet met, is an enemy.
    Special means the `Ancient Ruins` or `Ancient Ruins Unique` ability only. Also,
    Colonizable does not exclude colonized planets, and All lists no asteroid fields.
    The full tab table, the statistics, columns, sorting and Send Colony Ship are in
    §1.8.1 (confirmed: binary).
    Our client follows this (its own choices where the rules are silent are Q24–Q29),
    planetary cloaks included since 2026-10-01.
20. **Construction Queues toggles.** What do Ships and Ship SY each include?
    **Answer:** the split is by whether a vehicle's space yard works right now, not by
    hull. Ship SY holds every ship and base queue with a working yard; Ships holds vehicle
    queues whose yard does not work (cloaked, or the yard is gone), which are cleared at
    the next update, so it is normally empty. Planets and Planet SY split colonies the
    same way: a cloaked colony's yard does not count. All four are on by default. Rules,
    columns, statistics and the three action buttons are in §1.8.2 (confirmed: binary).
    Our client follows this (its own choices are Q24–Q29), a cloaked colony's yard not
    counting since 2026-10-01. It still differs in: status icons on a row's second line
    are drawn for colonies only (ships and bases have none yet).
21. **Tactical Combat details.** The Options list; which Orders act at once and which
    need a target click; the pointers; the right-click report. **Answer:** §1.10.1–
    §1.10.3 and §3.3 (confirmed: binary). The Options window has nine switches under
    Animation, Sound and Tactical Combat, plus Stop Combat in the simulator. Ram and
    Capture arm a target click; Drop Troops acts at once on the adjacent colony; L opens
    Launch Units. The battle starts with a Begin button and ends with a message, after
    which the window closes. The install's `.cur` files are the pointers. Right-click
    opens a full Combat Piece Report window.
    Our client follows this, with Q34–Q36 and Q39 brought in line on 2026-10-01 (the
    waits of Fast Tactical Combat, the piece report's lines and tabs, the pickers as
    windows of their own, the replay's log kept as a marked extension) and Q37 (Drop
    Troops without a treaty check or a target). It shows the install's pointers (Q40).
22. **Combat Simulator details.** How many sides, how items are removed, what Fleets for
    Plr does. **Answer:** always 10 sides, "Race 1" to "Race 10"; a left-click on a combat
    vehicle removes it; Fleets For Plr opens Fleet Transfer for the chosen side. Each click
    on an item adds one vehicle. Begin with Tactical fights in the Tactical Combat window
    and reopens the simulator with the same setup afterwards; Strategic opens the
    Strategic Combat window over it. Details in §1.10.4 (confirmed: binary).
    Our client follows this, with Q38 brought in line on 2026-10-01: Fleets For Plr and
    Change Cargo open the Fleet Transfer and Cargo Transfer windows over a sandbox of the
    setup, Designs closes during a tactical simulation, sides show numbered colour boxes,
    and one counter per side numbers the ships. Its own choices there are Q77–Q82.
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
    Our client follows this, with the answers of Q30–Q33 since 2026-10-01. It still
    differs in games played on different machines, whose host never stops: their
    battles are shown afterwards (Q74).

The Planets and Construction Queues windows follow §1.8. Questions 24–29 were our own
choices where those rules left something open; all six are now settled from the
executable:

24. **Sort history.** We read the "five-key history" as: the last column header clicked is
    the first key, the ones clicked before it break ties, up to five, each in its fixed
    direction. Is that the original's order, and does a second click on the same header
    change anything? **Answer:** each list window keeps five slots of column numbers,
    stored with the empire and saved with the game (Planets and Construction Queues have
    their own), written as soon as a header is clicked. A click shifts every slot down by
    one, drops the fifth and puts the clicked column first; an earlier copy of the same
    column is not removed. So clicking the header that is already first changes nothing
    on screen but pushes the oldest tie-breaker out. Empty slots are skipped. Both
    windows start with Name as the only key. Each key has its fixed direction and a
    second click never reverses it. The sort is not stable: rows equal on every key come
    out in no fixed order. In Construction Queues the middle column's key means whatever
    the tab shown at sorting time measures (confirmed: binary).
    Our client follows this in all four list windows: five slots each, stored with the
    empire and saved with the game (`InterfaceOptions::planetsSort`, `coloniesSort`,
    `shipsSort`, `queuesSort`), written at once through `cmd::SetInterfaceOptions`, Name
    the only key at first, an earlier copy of the column kept. It differs: its sort is
    stable (harmless). In Colonies and Ships\Units our client stores the column's position
    in the tab shown; the original stores the column itself, with its own direction
    (§1.8.3, Q56).
25. **Available colony ships.** We count a ship as having orders when its fleet has orders,
    and as out of supplies at 0 supply. Send Colony Ship on a planet that is already
    colonized (possible from the Colonizable tab) refuses with a message. What does the
    original do in each case? **Answer:**
    - Colony ships are our ships (not units) with a Colonize Planet ability (rock, ice or
      gas). One is available when its own order list is empty, it is not mothballed and
      its supply is above 0; a fleet member's own list holds its fleet's orders (spec 03
      Q65), so fleet orders count. Out of supplies means exactly 0 and never applies to a
      mothballed ship. A ship with orders but no Colonize order is neither available nor
      en route.
    - Send Colony Ship opens "Select Planet to Colonize" over the current tab's list and
      does not check whether the planet is colonized. Among the available ships that can
      colonize its type (turn-based: with movement left) it takes the one with the
      shortest route, the first in list order on a tie. If none qualifies, or the picker
      is cancelled, nothing happens and nothing is shown.
    - The orders go into that one ship's own list: Load Cargo (population; only when the
      ship has cargo space and carries no population), Move To the planet, Colonize. On
      an already colonized planet the orders are given anyway, and Colonize fails when
      the ship arrives.

    (confirmed: binary)
    Our client follows this: the availability test, no check of the planet, silence when
    no ship qualifies, and Load Cargo only with cargo space and no population aboard. It
    still differs: it gives the orders through the engine's order command, which reaches
    every member of the ship's fleet, where the original writes only to the chosen ship;
    it searches vehicles in game order, so ties can resolve differently (minor).
26. **Planets statistics.** We leave our own colonies out of "owned by enemies" and "owned
    by allies". Does the original count them in one of the groups? **Answer:** our own
    colonies count only in "Number of Colonizable Planets"; they are left out of the
    enemy, ally and non-aligned lines and of "not Colonized" and "Breathable", so the
    sub-lines do not add up to the colonizable total. Non-Aligned is always 0
    (confirmed: binary). Our client matches.
27. **Construction Queues rows.** We take the time at the right as the whole queue's time,
    show it as "N.N Years", make rows 40 px tall and mark a tagged row with the green lamp
    at its top left. The Ships toggle lists ships and bases that still have a yard part or
    a queue while their yard does not work (cloaked, mothballed or lost). The statistics'
    "resources generated" is the empire's total income per turn. Which of these match?
    **Answer:** rows are 36 px; the time at the right is the first item's (with its whole
    count, less what is paid), in brackets with "years" in lower case, "(0.3 years)";
    a tagged row carries the green right-pointing arrow of `General.bmp` (cell (203,15))
    at its top left; status icons go in one line from x 40 at y 15, 20 px apart, for
    ships and bases too. Mothballed vehicles are never listed, and cloaking empties a
    vehicle's queue at once, so the Ships group is in practice always empty. "Resources
    Generated Per Turn" is the colonies' production only. Every detail is in §1.8.2
    (confirmed: binary).
    Our client follows this: 36 px rows, the first item's time in brackets, the green
    arrow as the tag marker, the name at (40,0), status icons for ships, bases and
    colonies in one line from (40,15), every item as "<name> x <count>", mothballed
    vehicles never listed, the colonies' production as "Resources Generated Per Turn",
    the original's statistics labels and hint. It still differs: the list box starts at
    x 15 and is 556 px wide (the dialog's content box); the status icons stop at the
    Name column's edge; a vehicle whose yard stopped working is listed under Ships while
    its queue still holds items, because our engine does not yet empty a vehicle's queue
    when its yard stops working (cloaking a ship with a working yard should empty it).
28. **First-item confirmation.** With "confirm deleting the first item" on we ask whenever
    the first item is removed, with or without progress, and before Clear Queue. Does the
    original ask in both cases, and only for the first item? **Answer:** the switch
    controls three Yes/No prompts in Set Construction Queue (none in the Multi-Add
    temporary queue; with the switch off nothing asks):
    1. A left-click on a queued entry deletes the whole entry with its count at once
       (right-click opens its report). Only the first entry asks ("Remove First Queue
       Item": delete the item being built?), whatever its progress; other entries never
       ask. Removing the first entry, or emptying the queue, discards the progress.
    2. Clear Queue asks ("Remove All Queue Items") when the queue holds anything.
       Clearing also turns off On Hold and Repeat Build and discards the progress.
    3. Reorder Queue: after the reorder list is confirmed, if another entry would come
       first, it asks ("Move First Queue Item": lose the progress?). Yes discards the
       progress and applies the new order; No drops the whole reorder.

    (confirmed: binary)
    Our client follows this: a left-click deletes an entry, only the first asking; Clear
    Queue asks and also turns off On Hold and Repeat Build; Reorder Queue opens the
    reorder list and asks "Move First Queue Item" when another entry would come first, No
    dropping the whole reorder; nothing asks in the Multi-Add queue. When the first item
    changes, its progress is discarded by queueing it again at its new place. It differs
    only in the report a right-click opens on a queued ship or unit: ours opens the hull's,
    the original the design's Design Report (§1.8.3, Q56).
29. **Similar system-wide abilities.** We note it after a facility is queued whose
    abilities include one with "System" in its identifier that a facility of one of our
    colonies in the same system already has. Which abilities count, does the original
    also look at queued facilities, and is it a note or a question? **Answer:**
    - It is checked only when a facility is added by a left-click in the available list
      of Set Construction Queue on a real queue; Fill Queue, Multi-Add, upgrades and
      Upgrade Facilities never check. The Empire Options switch is never read, so the
      note always appears.
    - The abilities that count are these 18: `Resource Gen Modifier System - Minerals`,
      `- Organics`, `- Radioactives`; `System Point Generation Modifier - Research`,
      `- Intelligence`; `Combat Modifier - System`, `Damage Modifier - System`, `Change
      Bad Event Chance - System`, `Change Bad Intelligence Chance - System`, `Change
      Population Happiness - System`, `Modify Reproduction - System`, `Change Population -
      System`, `Plague Prevention - System`, `Ship Training - System`, `Fleet Training -
      System`, `Long Range Scanner - System`, `Reduced Maintenance Cost - System`,
      `Shield Modifier - System`. `Planet Value Change - System`, `Planet Conditions
      Change - System` and the `System - ...` movement and destruction abilities do not
      count.
    - The new facility's abilities are compared with every object of ours in the queue
      owner's system: our colonies' facilities and our ships' and bases' components.
      Queued facilities do not count.
    - On a match an OK-only box titled "Note" says that a facility with similar
      system-wide abilities already exists in this system; the facility is added anyway.

    (confirmed: binary)
    Our client follows this: the 18 abilities, only for a facility added by a left-click to
    a planet's queue, our colonies' built facilities and our ships' and bases' components
    in that system, a modal OK-only "Note" box, and the switch is not read.

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
    Our client follows this since 2026-10-01: the window fights the battle itself (a
    stepped combat::TacticalBattle), holds nothing, and lets each displayed frame show
    one empire's phase, the map after its steps and the forces list and "Combat Turn N"
    after each combat turn. That a frame shows a phase, not each step, is our own choice
    (inferred, Q73).
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
    Our client follows this since 2026-10-01 (`CombatForces`). A battle a network or
    PBEM host fought, shown afterwards from its record, counts a mixed group under its
    first design's hull, the only one the record keeps (inferred, Q75).
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
    Our engine and client follow this since 2026-10-01 (game/turn.hpp, "Battles shown as
    they happen"): on one machine every battle that the original shows stops the engine
    call once it is set up, before combat turn 1, in a turn-based game (the question, or
    with No Tactical Combat the Begin and Close form) and in a simultaneous game whose
    Settings show battles. The client fights it in its window while showing it (the
    record keeps each step, launch, loss and change of owner with its combat turn, and
    each ground combat with the place of its landing in the events); after Close the
    call is made again, replays to the same point and fights the battle the same way,
    so the reports, log entries, happiness events and the rest of the call come only
    then, and the results are the same whether a window shows the battle or not. Games
    on different machines differ: their host fights every battle without stopping, and
    the client shows the battles afterwards (Q74).
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
    Our engine and client follow this since 2026-10-01: each ground fight's record keeps
    the counts of every stack on both sides and the militia after every round
    (`GroundCombat::perRound`); the window shows them round by round, 0.9 s a round with
    the explosion and a boom; and at the colony owner's ground-combat step of a
    turn-based game on one machine, a fight with a human side stops the call (the engine
    fights it and hands the record over), shown after the notice, before the rest of
    that empire's end-of-turn processing and the next player's turn. Both empires' log
    entries are written either way (spec 04 §13).
34. **Fast Tactical Combat.** Ours plays the animation three times faster instead of
    dropping the pauses between steps; the same for Fast Tactical Combat in Combat Replay
    Options.
    **Answer:** the switch removes the waits and nothing else; there is no speed factor
    (confirmed: binary; §1.10.3, which lists every wait: 1 ms per frame of a slide, 0.1 s
    after a jump when movement is not animated, 0.01 s per frame of a turn, a few
    hundredths of a millisecond per beam stamp, 1 ms per frame of a torpedo, 0.1 s per
    frame of a hit animation and 0.3 s after each hit; corrected by Q77: the 0.3 s comes
    only after a seeker's impact on a piece that survives it, and a torpedo moves farther
    per frame with the switch on). Every frame is still drawn. The
    replay's switch does the same in the replay; neither touches the Ground Combat or
    Strategic Combat window.
    The client differs: it speeds the animations up threefold. To match, with the switch
    off it must use the waits of §1.10.3, and with it on hold nothing. Since a frame
    drawn without any hold can hardly be seen on a current display, showing each frame
    for one display refresh would be OpenSE4's own choice (inferred).
    Since 2026-10-01 our client does this, in the Tactical Combat window and, with its
    own switches, in Combat Replay (client/classic/replay.hpp): every frame of every
    animation is drawn, followed without the switch by the wait of §1.10.3 and with it by
    none; no speed factor is left. Each frame stays on screen for at least one display
    refresh, and how many frames our own drawings take is ours (Q77) (inferred).
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
    Since 2026-10-01 our client follows the table of §1.10.1 line by line and draws the
    report as §1.10.1 lays it out, with the tabs along the bottom, the unit grid of a
    unit group and the launching weapon's picture for a seeker; a neutral obstacle opens
    its object report. Its own choices are in Q78.
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
    Since 2026-10-01 our client does so: a click closes the menu and then runs the
    order; "Select Fighters Per group" (one column, Amount), "Select Combat Group" (1 to
    9), "Select Formation" (closed without a choice, the piece's group marks are cleared)
    and the "Resolve Combat" confirmation are modal windows of their own over the
    tactical window. Refused group orders say nothing. Without any formation in the data
    set, a new leader takes none and no formation picker opens (inferred).
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
    Our engine follows this since 2026-10-01 (`Battle::landingColony`, `dropTroops`):
    no treaty is checked, the order names no planet, a Drop Troops piece tries the
    landing after every move it plans, and every landing resets the planet's piece. Its
    remaining choices were settled as spec 04 §19.4 Q87–Q89: who counts as another side
    and the refusals still differ there.
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
      opened from them. The Combat Vehicles list's Flag column shows the same box (26×18,
      5 px from the column's left, 9 px from the row's top); a neutral object's row
      leaves it empty. Its Name column adds "Cargo:" or "Units:" and "Fleet:" lines in
      the small font (§1.10.4). The Owner for item rows (20 px) carry the 26×18 box at x
      80, y 1, and the Computer Control window ("Empires Under Computer Control") a Pic
      column with the box and a Name column "Race N" (confirmed: binary).
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
    Since 2026-10-01 our client follows each point. The numbered boxes stand for the
    sides in the Owner for item and Computer Control lists, in the Tactical and
    Strategic Combat windows of a simulation and in the reports opened from them. It
    still differs in the simulator window itself (`screens/simulator.cpp`): `vehicles()`
    draws the copied empire's flag (`sideStyle()`) and one line "name xN (Race k)" or
    "(neutral)" where the original draws the side's box and the name with its Cargo or
    Units and Fleet lines; `owners()` and `computerPopup()` draw an 18×14 box at x 22
    instead of the 26×18 box; the Computer Control popup is titled "Player Computer
    Control"; the Items heading reads "Items"; and the two hints are missing. Each ship item takes its side's next numbers when it is added
    (game::combat::simulatorNumberShips). Fleets For Plr and Change Cargo open the real
    Fleet Transfer and Cargo Transfer windows over a sandbox of the setup, and the setup
    takes back the fleets or the cargo when the window closes; the real game never
    changes (Q79–Q81). Designs closes when a tactical simulation begins and opens again
    with the simulator when the battle is over.
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
    They stay, as an OpenSE4 extension (inferred): the list of the combat turn's events
    in words and the battle's summary beside the map help a player follow the battle,
    and nothing in the rules depends on them. Everything else of the window follows the
    answer.
40. **Pointers.** Ours draws its own move arrows and crosshairs: no loader for the
    install's `.cur` files exists yet. **Answer:** the game loads all twelve `.cur` files
    at start-up and uses no other pointer. `Normal` is the pointer of every window,
    `Hourglass` covers the whole program during loading, saving and turn processing, and
    the tactical map alone uses `Target`, `Select` and the eight arrows; the main window
    never changes the pointer (§5.8, §1.10.1) (confirmed: binary). The hotspots are the
    ones stored in the files. Our client follows this; its differences are at the end of
    §5.8.

The Options windows, the Log, the system window's display options and Abandon Planet
follow §1.9, §4.1 and §2.8. Questions 41–48 were our own choices where those rules left
something open; all are now settled from the executable:

41. **Log Goto to a window.** Goto on an entry without a location opens a window chosen by
    the entry's category: Construction → Construction Queues, Research → Research,
    Intelligence → Intelligence, Politics → Empires; Events, Combat and Misc open none.
    Which entries name which window, and which ones open Empire Options or Designs?
    **Answer:** the target is fixed per kind of entry when the entry is made; it does not
    follow the category. No entry is ever made with the Construction Queues, Empire
    Options or Designs targets, so Goto opens only these:
    - **Location** (closes the Log, shows the system with the sector selected; nothing
      happens when the entry names no system): almost every entry. Construction: items
      built, facilities upgraded, a queue stopped for lack of resources, no storage for a
      new unit, scrapping, retrofit results, self-destruction, units destroyed by our own
      fire. Events: random events, completed stellar manipulations, ships damaged or
      destroyed by natural events, minefields, plague cured, atmosphere converted, first
      contact. Combat: battle reports and ground combat. Politics: planets or vehicles
      received or transferred, star charts received, new system maps, colonies growing
      happier or unhappy. Misc: every refused or failed order, ships lost for lack of
      supply or maintenance, damage while moving, colonization results and failures,
      ruins found, planet abandoned, resources converted, rioting. Intelligence: the
      outcome of our own project when its target is a ship, planet or system (an enemy
      project's effect on us is an Events entry with the same rule).
    - **Research window**: every Research entry (new tech level, item developed or
      discovered, new area, all projects completed), and technology received or
      transferred by diplomacy (Politics).
    - **Intelligence window**: counter-intelligence defeated an attack; all intelligence
      projects completed.
    - **Empires window**: every diplomatic message; surrender, subjugation, treaty lost,
      resources received or transferred, treaty enacted, communication channels received
      or opened (Politics); lost contact (Events).
    - **No Goto**: our project failed or was defeated; a project outcome without a
      location; maximum ships reached; an empire destroyed; a password reset.

    (confirmed: binary)
    Our client follows this: each entry keeps the target it was made with
    (`game::LogGoto`), Goto opens that window over the Log or shows the location, and a
    location entry without a system leaves the Log open. It differs where our engine's
    entries have no exact counterpart: a trade, gift or tribute is one entry for the whole
    package, whose target follows the first kind of item it holds (Q70); declarations of
    war, refused treaties, missing package items and cancelled trades open Empires
    (inferred); first contact has no location, so its Goto does nothing.
42. **Log order and selection.** Messages from other empires come first, then the log
    entries in the order they were made, then the orders the turn refused; a filter click
    selects the first row. Is that the original's order, and what does a filter click
    select? **Answer:** the Log is one list in the order its entries were made. Each
    delivered diplomatic message (acceptances of trades, gifts and tributes included) is
    an ordinary entry made when it arrives: title "Message", category Politics, Goto
    Empires, its text naming the sender and quoting the message; messages are not
    grouped first. Each refused or failed order is an ordinary entry made when the order
    fails, with its own title, category Misc (scrap and retrofit results Construction)
    and Goto its location. A filter click shows the filtered list scrolled to the top
    with its first row selected (none if empty) and stores the filter with the empire.
    When the stored category has no entries, the window opens on All and stores All. The
    stored selection is the entry's index in the whole log, not its row in the filtered
    list (§4.1) (confirmed: binary).
    Our client follows this: each delivered message is a "Message" entry (it also names
    the message, for the details and Send Reply), the Log lists only entries in the
    order they were made, stores the entry's index in the whole log and, falling back to
    All, stores All. It differs only for commands a network or play-by-e-mail host
    refused, which have no counterpart in the original: they follow the entries as Misc
    rows "Order not carried out" without a Goto (Q71).
43. **Log damage of planets.** Our battle records keep no planet hit points, so a planet
    that stands shows "0%" when untouched and "Hit" otherwise; ships and bases show their
    damage now, unit groups the share of their units lost. What does the original show
    for a planet? **Answer:** a planet shows the share of its hit points (spec 04 §11) lost
    between the start and the end of the battle, as a percentage like every other row;
    lost facilities do not count. Every value is fixed when the battle ends, from the
    piece's state then: ships and bases against the design's whole structure (so damage
    from before the battle counts), unit groups against all their units at full health.
    Rows list only the pieces present at the start; ships and bases add "(<hull Code>)"
    to the name; "Dead" and "Taken" are found by name among the survivors; all of it is
    white. Formula and details in §4.1 (confirmed: binary).
    Our client follows this: the battle record keeps, for each piece present at the
    start, the damage fixed when the battle ended and who held it then
    (`CombatPiece::damage`, `survivor`), and the Log reads Dead and Taken from them by
    name. No difference is known.
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
    **Answer:** each Empire Options row gates its letters. Every test reads the abilities
    of the colony's built facilities, except Y:

    | Row | Letters and what they test |
    |---|---|
    | R/S/Y | R `Supply Generation`; S `Spaceport`; Y a working space yard (a `Space Yard` facility on a colony that is not cloaked) |
    | Ca/Cc/Cv | Ca `Planet - Change Atmosphere`; Cc `Planet - Change Conditions`; Cv any of the three `Planet - Change ... Value` abilities |
    | St/Ft | St `Ship Training`; Ft `Fleet Training` |
    | Rc/Rr | Rc `Resource Conversion`; Rr `Resource Reclamation` |
    | Sst/Sft | Sst `Ship Training - System`; Sft `Fleet Training - System` |
    | Spv/Spc | Spv `Planet Value Change - System`; Spc `Planet Conditions Change - System` |
    | Sph/Spa | Sph `Change Population Happiness - System`; Spa `Change Population - System` |
    | Scm/Sdm | Scm `Combat Modifier - System`; Sdm `Damage Modifier - System` |
    | Srm/Ssm | Srm `Reduced Maintenance Cost - System`; Ssm `Shield Modifier - System` |
    | Spp/Src | Spp `Plague Prevention - System`; Src `Modify Reproduction - System` |
    | Sbe/Sbi | Sbe `Change Bad Event Chance - System`; Sbi `Change Bad Intelligence Chance - System` |
    | Slr | `Long Range Scanner - System` |

    They are drawn on visible colonies of the viewer and of empires with a Military
    Alliance or Partnership with the viewer, in the colony owner's colour, in Small Fonts,
    packed right to left from the bottom-right corner of the planet's sprite square, as
    described in §2.4 (confirmed: binary).
    Our client follows this: the letters and tests of the table, on our colonies and our
    Military Alliance and Partnership partners', in the owner's colour, packed right to
    left from the sprite square's bottom right, new lines one text height higher, on the
    colonies the viewer sees, and Y only for a colony that is not cloaked (since
    2026-10-01). It differs: the letters use OpenSE4's small raster face (§5.4).
45. **Planet names and coordinate location.** We draw a planet's name under it, like a
    warp point's destination, and the coordinates of the sector under the pointer after
    the system name. Where does the original show them? **Answer:** a planet's name, like
    a warp point's destination name, is drawn inside the planet's cell: Futurist small,
    white, centred, with the bottom of the text on the cell's bottom edge; only planets the
    viewer can see, never asteroid fields. The coordinate location is a line at the
    bottom-left of the system panel, at (10, panel height − 20), in Futurist small white:
    "Coordinates (x, y)" for the sector under the pointer (column and row 0–12), followed
    by "Range: N" when a sector of the shown system is selected, N being the larger of
    the column and row differences between the two sectors. Details in §2.4 (confirmed:
    binary).
    Our client follows this. The line reads "Coordinates (x, y)   Range: N"; the text and
    spacing match the original (Q64).
46. **Confirmations.** "Confirm stellar manipulation" asks before every action. "Confirm
    scrapping" asks before scrapping vehicles or facilities and before self-destruction.
    Do the original's ask in the same cases? **Answer:**
    - "Confirm scrapping" asks only in the Scrap window (order G), before four of its
      actions: Scrap (scrap the selected vehicles for their raw materials), Analyze
      (deconstruct them and study their components), Self-Destruct and Fire On (have our
      other ships destroy them). Each is a Yes/No box titled "Confirm Action"; with the
      option off they act at once. Retrofit (which opens its picker), Mothball and
      Unmothball never ask, nor do the Scrap Facilities check lists (Ctrl+K,
      Construction Queues → Scrap Facilities, Colonies → Scrap Facil Types). Abandon
      Planet's two questions are always asked, whatever the option.
    - "Confirm stellar manipulation": each of the 13 Stellar Manipulation buttons asks a
      "Confirm Action" Yes/No naming the effect (create a planet in this sector, destroy
      the star in this sector, close the warp point in this sector, collapse the black
      hole in this system, and so on). Construct first asks for the size and then asks
      whether to construct that size. With the option off the order is given at once.

    (confirmed: binary)
    Our client follows this: Scrap and Self-Destruct ask "Confirm Action" with the option
    on, the facility check lists never ask, and each Stellar Manipulation button asks
    "Confirm Action" naming its effect in our words. It differs: Analyze and Fire On are
    disabled buttons in ours (the engine has neither), so their questions do not exist yet;
    our engine picks a constructed world's size itself, so Construct asks once, without a
    size question.
47. **Claims and abandoned planets.** "Claim every system we colonize" applies to human
    empires when the colony is founded (computer players work out their claims each
    turn). Facilities left on an abandoned planet go, all of them, to the next colony
    founded there by any empire. Do these match? **Answer:**
    - The "automatically claim any system colonized" option is stored, saved and shown,
      but nothing reads it: founding a colony never claims a system. Systems become
      claimed only as the home system at empire creation, by a click in the Borders
      window (which toggles the claim), through a trade or gift package (the giver loses
      it and the receiver claims it, Q70), and by the Politics minister's territory pass each turn (spec 05) for every
      empire whose Politics minister is on: every computer player, and a human who turns
      that minister on.
    - Abandon Planet empties the population and resets the colony's anger to 25. If the
      player chose to scrap the facilities, they are scrapped with the usual refund
      (spec 02). Only when no facility remains is the colony removed and the planet left
      uncolonized. If facilities remain, the planet stays the same empire's colony, with
      no population and its facilities; nobody can colonize it (Colonize refuses because
      a colony is already there). In a turn-based game this happens at once.

    (confirmed: binary)
    Our engine follows this: founding a colony claims nothing, the territory pass claims
    for exactly the empires whose Politics minister is on, and an abandoned colony that
    keeps facilities stays with its owner, empty, with anger 25.
48. **Smaller choices.** The planet report's Time Remaining uses the Construction
    Queues' years ("0.3 Years", "On Hold", "Never"). "Skip ships under construction"
    skips nothing: our ships appear finished, with no Under Construction status. Save
    Empire writes our empire file, which holds no designs, so it does not ask whether to
    include them. Which of these differ from the original? **Answer:**
    - **Time Remaining** (planet report; the same text in the ship report, the Colonies
      list and the Construction Queues rows) is the time to finish the first item with
      its whole count: for each resource whose construction rate is above 0, the turns
      ⌈(cost × count − progress) / rate⌉, and the largest of these. It is shown as turns ×
      0.1 with one decimal and " years" in lower case ("0.3 years"); 0 turns shows as one
      turn, "0.1 years". It shows "Never" when all three rates are 0 (9999 turns or more),
      "On Hold" for a held queue, and nothing for an empty queue. Under Construction shows
      "None", or the first item's name with " x N" when its count is above 1; upgrades are
      named "Upg. <facility>".
    - **Skip ships under construction** skips ships whose status is Under Construction,
      which a ship has only from its creation until its components are first updated; a
      space yard runs that update at once (and every supply update does too), so in a
      normal game the option skips nothing.
    - **Save Empire** first asks whether to save the designs with the empire (Yes/No; Esc
      and Enter mean No), then opens the empire file picker. The file holds a copy of the
      empire with the game state cleared (research, intelligence, diplomacy, queues, log,
      fleets, systems to avoid, waypoints, tagged minefields, computer control) and all the
      player's designs on Yes, none on No. The cleared state leaves the empire's combat
      strategies; each design keeps its strategy, creation date and statistics but loses
      its obsolete and built marks and the other empires' sighting dates (Q72).

    (confirmed: binary)
    Our client follows Time Remaining and Under Construction in the planet and ship
    reports, the Colonies list and the Construction Queues rows ("0.3 years", "0.1 years"
    for 0 turns, "Never", "On Hold", nothing for an empty queue; " x N" for any item;
    "Upg. <facility>"). Save Empire asks about the designs first and our empire file
    keeps them on Yes; it differs in having no file picker and in what it keeps of a
    design (Q72). "Skip ships under construction" skipping nothing already matches.

Questions 49–55 were our own choices for the main window, maps, art, sound and files;
all are now settled from the executable:

49. **Ships beside a planet.** Where exactly do the small flags and their counts go in a
    sector that holds a planet, or several empires, and does a sector with a star,
    storm or warp point count like a planet? Our client draws the flags left to right
    along the bottom of the 36x36 sprite area, each count after its flag, and treats any
    stellar object like a planet (inferred). **Answer:** any stellar object (planet,
    asteroid field, star, storm, warp point, comet) counts. With one owner and a stellar
    object, the owner's small flag goes at the sprite square's top-left corner with the
    count right of it (even a count of 1); with several owners the flags are stacked
    down the left edge, 10 px apart (less when they would not fit), each with its count
    right of it, in player-number order; counts are per object (a unit group counts 1),
    in Small Fonts in the owner's colour on a black box. Without a stellar object, one
    owner shows the largest vehicle's sprite (a fleet icon for the viewer's own fleet
    ship) with the count at the bottom right; a lone unit group shows its unit count.
    With more than one stellar object their number is drawn at the bottom left. Vehicle
    sprites are always keyed black. The full rules are under **Sector contents** in §2.4
    (confirmed: binary).
    Our client follows this: the stellar object choice and count, one vehicle (or the fleet
    icon) with its count or a lone group's unit count, the owners' flags stacked down the
    left edge with their counts, counts per object on black boxes in the owner's colour,
    vehicle minis always keyed, and the dotted circle on a cloaked vehicle shown. It
    differs: the numbers use OpenSE4's small raster face (§5.4); ships gliding to their new
    square (ours, with the movement animation on) are drawn on their way, outside the
    sector's count until they arrive.
50. **Status icon conditions.** What makes an object "building", which fleet shows the
    cloaked icon, and in what order "the first miner" is found? Our client: a ship whose
    yard works (not cloaked) with items queued, or a colony with items queued, held or
    not; a fleet with any member cloaked; the first in the game's vehicle order
    (inferred). **Answer:** a ship is "building" when it is not mothballed, its space yard
    worked at its last update (`Space Yard` ability, not cloaked) and its queue holds at
    least one item, held or not; its space-yard icon also needs it uncloaked (a cloaked
    yard ship shows the repair icon instead if it can repair; a mothballed, uncloaked yard
    ship still shows the yard icon). A colony is "building" when its queue holds at least
    one item, held or not. A fleet shows the minister icon when its own minister flag is
    set and the cloaked icon when any member belonging to the fleet's owner is cloaked.
    The remote-mining icon goes to the first object with `Remote Resource Generation` in
    the system's object list in that sector, whatever its owner, when the sector holds an
    uncolonized planet or asteroid field, visible or not. Supply icons: out at 0, low
    strictly below the setting, skipped only for mothballed ships. All in §4.4
    (confirmed: binary).
    Our client follows this. It differs: the "first miner" is the first in the game's
    vehicle order (the system's object list order is not kept apart); unit groups other
    than fighter groups show no supply icons (inferred, §7 Q61).
51. **Movement log replay.** What does one Ctrl+I step show, and how does Ctrl+U differ
    from Ctrl+P? Our engine keeps no day-by-day log, so our client moves each vehicle it
    saw in a straight line from where it was before the turn to where it is, in ten
    steps, and plays Ctrl+U like Ctrl+P (inferred). **Answer:**
    - The replay reloads the start of the turn: it first parks the current state in
      `temp/<game>_CurrTurn.gam` (on the first start only), loads
      `temp/<game>_LastTurn.gam` and reads `<game>_Log.trn`, which must be of the current
      turn. If a file is missing or the log is of another turn, a "Replay Unavailable"
      message appears (what normally happens in a turn-based game). While it runs the
      command buttons and selectors are disabled and the system name reads "<Name>  (Day
      N)".
    - Each day applies that day's log entries: moves within a system, warp jumps to
      another system, colonies founded, objects removed and colonies removed; then the
      system and galaxy panels are redrawn. There is no pause between days. With "animate
      ship movement in the system window" on, visible moves inside the shown system are
      animated: the sprite first turns in 5° steps of 10 ms each, then slides 1 px per
      step, waiting at least 1 ms per step; warp jumps are never animated. Each entry (one
      per vehicle and step) is animated on its own, one after another, in the log's order;
      key presses while a day is shown are ignored (Q62). After day 30 the current turn is
      reloaded and the panels unlocked.
    - Ctrl+P (Play Movement Log) ends any replay in progress and plays days 1–30 in one go.
    - Ctrl+I (one step): the first press loads the start of the turn and shows Day 0
      without applying anything; each later press applies one more day; the step past day
      30 restores the current turn.
    - Ctrl+O (rewind) ends any replay in progress, reloads the start of the turn at Day 0
      and waits for steps.
    - Ctrl+U ("Play Movement Log For All Ships") collects the viewer's own objects that
      moved or jumped, in order of first appearance in the log. For each in turn it
      reloads the start of the turn, shows that object's system and sector and plays all
      30 days (every entry applied), switching the panel before each day to the system the
      object is in, so the view follows it. At the end the current turn and the shown
      system are restored.
    - The four keys start the replay without checking whether their buttons are lit
      (§2.8, §3).

    (confirmed: binary)
    Our client follows this in local and hotseat simultaneous games. It keeps the state
    each processed turn started from; the first replay key of a turn plays that turn again
    from it (the engine is deterministic, and reports the state after each movement day to
    an observer) and records where every vehicle is after each of the 30 days, which
    appear or are removed, and the colonies founded or lost. Ctrl+P, Ctrl+I, Ctrl+O and
    Ctrl+U then work as above, starting without their buttons; "(Day N)" follows the system
    name; the command buttons and selectors are disabled and no other main-window key
    works; with the animation option on, visible moves in the shown system turn 5° per
    10 ms and slide 1 px per millisecond; Ctrl+U shows each object's system and sector
    day by day and brings back the shown system at the end. It differs:
    - Network and PBEM clients have only their own view of the game, so they rebuild a log
      from where they saw each vehicle before and after the turn: steps of one sector
      along a straight line spread over the 30 days, a jump to another system on day 15,
      vehicles no longer seen removed after day 30 (inferred).
    - No turn file is written: the log lives in memory, so a game just loaded has none and
      the keys say Replay Unavailable, as a notice in the system panel rather than a box.
    - Without the animation one day is shown per frame (inferred). A fleet's members that
      make the same move are animated as one and minis start the replay facing up, where
      the original animates each member's entry on its own and starts from the headings
      saved with the turn (Q62).
52. **Tagged groups.** What do Scrap and Move To Waypoint show for a tagged group? Our
    client asks to confirm the scrapping of every tagged object, and lists the set
    waypoints (inferred). **Answer:**
    - Scrap opens the ordinary Scrap window for the sector (where the first tagged object
      is). It lists all the player's vehicles there; the tags pre-select nothing, and only
      the window's own "confirm scrapping" prompt applies.
    - Move To Waypoint (button or Ctrl+W) opens the same Select Waypoint picker as for one
      ship; Ctrl+0..9 skip it. The order is appended to every tagged object's own orders.
    - Resupply, Repair and Explore find the destination once, from the first tagged
      object's sector, and give the same move to every tagged object. Clear Orders clears
      every tagged object's orders. Cloak and Decloak act at once on each (sound
      `cloakon`). Minister flips each object's own flag, so a mixed group stays mixed.
      Move To, Warp, Attack and Set Patrol ask for the target ("Please select target for
      '<order>'") and apply it to every tagged object.
    - The tags clear after an order that acts at once (the sector is re-selected), and
      after the target is picked for a targeted order.

    (confirmed: binary)
    Our client follows this for Scrap (the Scrap window of the first tagged object's
    sector, nothing selected) and Minister (each object's flag flips). It differs: Move
    To Waypoint with tags uses its own list of the set waypoints (the same effect);
    Resupply, Repair and Explore go to each tagged object as orders the engine resolves
    for each object, not as one destination found from the first object's sector.
53. **Resume Game and the music after loading.** Does an autosave become the "last saved
    game", and does Load Game from the Game Menu change the music? Our client: only Save
    Game sets it, and an in-game load keeps the music (inferred). **Answer:** every
    successful save records its full path as the last saved game: Save Game, every
    autosave `AutoSav<d>.gam`, the per-player turn-based saves, multiplayer and host saves,
    and even the `temp/<game>_CurrTurn.gam` written when a movement-log replay starts
    (§6.1). Resume Game loads that path when the file exists and starts a background
    track; otherwise it does nothing. Load Game from the Game Menu does not change the
    music (confirmed: binary).
    Our client follows this: every successful save of a game, Save Game and every
    autosave (also a hosting player's), becomes the last saved game; our movement-log
    replay writes no temporary save, so that quirk does not arise. The music after an
    in-game load matches.
54. **Low supply of fighter groups.** Do the Sentry button and the low-supply icon of a
    fighter group use a tenth of `Supply Amount for Low Supply Warning`, as Sentry's own
    end does? Our client lights the button by a tenth and draws the icon by the full
    level (inferred). **Answer:** a fighter group is low on supplies when it holds at least
    one fighter and its supply is below `Supply Amount for Low Supply Warning` div 10, and
    out of supplies at 0. The Sentry button, Sentry's own end and the list's low-supply
    and out-of-supply icons all use these tests (confirmed: binary).
    Our client follows this: the button, the end of Sentry and the icons.
55. **The log copy's header.** What words head `plr_<N>_log.txt`? Our engine writes
    "Date", "Title" and "Text" in the columns of §6.1, then the rule (inferred).
    **Answer:** "Date" at column 1, "Header" at column 10 and "Text" at column 51, then 78
    dashes; each entry is the date padded to 9, the title padded to 40, one space, and
    the text with each CR LF pair turned into one space; CR LF line ends; the file is
    rewritten only when the setting is on and the log is not empty (§6.1) (confirmed:
    binary).
    Our engine's log writer matches. Our engine's texts break lines with a single LF,
    which it turns into one space like a CR LF pair.

Raised while implementing the answers above (inferred, open):

56. **Colonies and Ships\Units details.** The spec gives the five-slot sort history
    (Q24) for every list window but the columns and directions only for Planets and
    Construction Queues. Ours stores, for Colonies and Ships\Units, the table's column
    number (1 the name, then the tab's own columns), so a key stands for whatever that
    column of the tab shown measures, as in Construction Queues; names and text sort A
    to Z and numbers highest first; both windows start with Name. A right-click on a
    queued ship or unit in Set Construction Queue opens its hull's report. What are the
    original's columns per tab, their directions, and the report a queued design opens?
    **Answer:** the columns, keys and directions per tab are in §1.8.3 (confirmed:
    binary). In short: a sort key is a column with one identity in every tab, not a
    position, so it keeps sorting by its quantity after a tab change; every header,
    the picture's included, can be clicked, and a few columns have no key; text compares
    by character code except Name, which ignores case; several directions are not ours
    (Construction's two columns Z to A, Supplies and Experience lowest first, the Fleet
    column by fleet number); fleet rows always come last, unsorted. A right-click on a
    queued ship, base or unit opens the design's Design Report.

    Our client differs (`screens/planets.cpp`, `screens/ships.cpp`, `screens/queues.cpp`,
    `state.hpp`):
    1. `InterfaceOptions::coloniesSort` and `shipsSort` hold the column's position in the
       tab shown (`table()` in `planets.cpp`, `sortRows()` and `table()` in `ships.cpp`).
       They must hold the column's identity as in §1.8.3; saved values need a mapping.
    2. The picture headings are not clickable (`{"", 0, 0, false}` in both files). They
       must sort: planet size smallest first, hull number lowest first.
    3. Our Colonies tabs have other columns (General: Type, Colony Type, Population, Mood,
       Facil.; Value adds Type, Atmosphere, Conditions; Production adds Delivery;
       Facilities: Used, Facilities; Cargo: Space, Contents; Construction: Building,
       Progress, Time, Items, Mode; Status adds Mood and Anger). `columns()` in
       `planets.cpp` must give the original's sets.
    4. Directions: in Colonies, Mood sorts by the word (ours by anger), Under Construction
       and Time Remaining Z to A by text, and Facilities, Cargo Items, Status and Orders
       have no key (ours sort by counts). In Ships\Units, Size sorts by hull number (ours
       by the hull's name), Supplies and Experience lowest first, Fleet by fleet number
       highest first (ours by the fleet's name).
    5. `buildRows()` and `sortRows()` (`ships.cpp`) sort fleet rows with the vehicles.
       Fleets must follow every vehicle, in fleet order, unsorted.
    6. `openItemReport()` (`queues.cpp`) opens the hull's report (`ItemRef::Kind::Hull`).
       It must open the design's report, like the simulator's "Design Report" popup
       (`screens/simulator.cpp`).

Questions 60–64 are choices of ours made while implementing the fonts, pointers, the
800x600 layout and the movement log replay (inferred). Q61, Q62 and Q64 are settled from
the executable and Q63 has no counterpart; Q60 needs observation:

60. **The Small Fonts stand-in.** The map numbers and letters use OpenSE4's own raster
    face: an 8 px cell with ascent 6, capitals and digits 5 px tall and 3 px wide (M, N,
    W and a few others wider), lower case 4 px tall, one blank column between
    characters. How tall and wide are the original's digits and letters on the system
    panel at 96 DPI, so the stand-in can match their size and spacing?
    Open: needs observation. Capture the system panel at 1:1 with the facility markers
    on and the numbers drawn on the map in view (§2.4), and measure the cap height, digit width and spacing of the
    letters. The face is the system's Small Fonts, not a file of the game, so note
    whether the capture comes from Windows or from Wine, whose stand-in may differ.
61. **Supply icons of unit groups.** §4.4 gives the supply icons for ships, bases and
    fighter groups. Satellite, mine, drone and weapon platform groups carry no supplies in
    our engine, so ours show neither icon. Does the original show the out-of-supplies icon
    on them?
    **Answer:** fighter and drone groups by the same rule; satellite groups and mine
    fields never (confirmed: binary; §4.4). Unit-group rows and the group reports draw
    their status icons with the ship routine, whose first cell asks the object:
    - a fighter or drone group is out of supplies at 0 supply, and low while it holds at
      least one unit and its supply is below `Supply Amount for Low Supply Warning` div 10;
      a drone group at 0 is destroyed at the end of the turn (spec 03 §7), so it shows the
      icon only until then;
    - a satellite group or a mine field always answers no, so it shows neither icon;
    - weapon platforms never form a group in space (they exist only as cargo), so they
      have no row of their own.

    Our client differs: `vehicleStatusCells()` (`client/classic/status_icons.cpp`) draws
    the supply cell only for ships, bases and fighter groups. It must treat drone groups
    like fighter groups. The same low-supply rule ends a Sentry order for each member
    (§7 Q54), so a drone group's threshold is also the tenth, with at least one unit; the
    engine's `sentry()` (`movement.cpp`), which uses the tenth for fighters only, and the
    client's `lowOnSupply()` (`order_rules.cpp`) differ there. (Whether the original's
    Sentry button for drone groups uses this test was not traced.)
62. **Movement log animation details.** Ours animates the moves of one day in the log's
    order, a fleet's identical moves as one, shows one day per frame without the
    animation, and starts every mini facing up. In what order does the original animate
    a day's moves, does each fleet member move on its own, and which heading does a mini
    have at Day 0?
    **Answer** (confirmed: binary; Q51):
    - **One entry per vehicle and step.** When a group steps one sector or jumps through
      a warp point, each member gets its own log entry for that day, one after another in
      the group's order. A day of the replay applies its entries in the order they were
      written, which is the order in which that day's movement carried them out.
    - **No grouping.** Each entry is animated on its own, and in full, before the next
      starts, so a fleet's members turn and slide one after another along the same path.
    - **When an entry is animated:** only with "animate ship movement in the system
      window" on, for a move that stays in one system, when that system is shown and the
      viewer sees the vehicle.
    - **The turn.** When the bearing from the vehicle's sector to its new one, taken to
      the nearest of the 8 headings, differs from its heading, the mini first turns the
      shorter way (clockwise for a half-turn), 5° a frame (9 frames per 45°), 0.01 s after
      each frame.
    - **The slide.** The vehicle is then placed in its new sector with its new heading
      (its fleet's location moves with it), its old square is redrawn without it, and the
      mini slides 1 px a frame along the longer axis: 36 frames per sector at 800x600, 50
      at 1024x768, diagonal steps the same, 1 ms after each frame (with the tick counter
      of §1.10.3, so about 15.6 ms a frame on current Windows).
    - **Never animated or paused:** no switch shortens these waits; the Settings pause
      after a normal move is not used in the replay; warp jumps, colonies and removals are
      never animated; after a day's entries the system and galaxy panels are redrawn with
      no pause.
    - **One Ctrl+I step** is one such day: all its entries, each animated in turn, then
      the redraw. Key presses during a day's animations are ignored; they neither finish
      nor skip the day.
    - **Day 0.** The replay reloads the game saved at the start of the turn, which keeps
      each ship's, fighter group's and drone group's heading, so each mini faces where it
      faced when the turn began, not up. Satellite and mine groups have no heading; a
      vehicle whose hull uses no engines (a base) is always drawn facing up.

    Our client differs (`client/classic/movement_replay.cpp`):
    - `MovementRecorder::day` builds each day's moves from the states before and after the
      day, in `GameState::vehicles` order, so several steps of one day become one move and
      the order is not that of movement. The observer `ClassicSession::replayLastTurn` uses
      must report each step of each vehicle as it is made, and the log keep that order.
    - `MovementReplay::applyDay` merges a fleet's identical moves into one animation. Each
      entry must be animated on its own, in sequence.
    - `MovementReplay::heading` starts every mini facing up. It must start from each
      vehicle's heading at the start of the turn, so the engine must keep a vehicle's
      heading as state (saved with the game); bases always face up.
    - `MovementReplay::update` finishes the day at once on a step key pressed during its
      animations, then applies the next. Such a press must be ignored.
    - One day per displayed frame without the animation stays an OpenSE4 choice (the
      original plays its 30 days without any pause).
63. **Pointer size on a scaled screen.** Ours shows the 32x32 pointers at the whole
    multiple nearest the frame's scale (twice as large on a frame drawn twice its size).
    The original draws the frame 1:1, so this has no counterpart; is a fixed 32x32
    pointer preferable on large screens?
    **No answer needed:** an OpenSE4 choice with no counterpart in the original, which
    never scales its frame (confirmed: binary; §2.1.1). Ours stays as it is.
64. **The coordinate line's text.** Ours reads "Coordinates (3, 4)   Range: 2". What
    separates the parts in the original, and is the range shown when the selected sector
    is the one under the pointer (Range 0)?
    **Answer:** the text matches ours character for character, and Range 0 is shown
    (confirmed: binary; §2.4). The line is one string: "Coordinates", a space, "(x, y)"
    with ", " between the numbers, then, while a selected sector of the shown system is
    marked, three spaces, "Range:", a space and the distance: "Coordinates (3, 4)   Range:
    2". The selected sector is the last one clicked in the panel. It is marked, and the
    range shown, only while it holds at least one object the viewer can see (a star,
    planet, warp point, vehicle or group), tested at every redraw: after a click on an
    empty sector neither the marker nor the range appears, though the sector stays
    selected. Nothing excludes the selected sector itself, so with the pointer on it the
    line ends in "Range: 0". With no marked selection only the coordinates show.

    Our client differs: `selectSector()` (`client/classic/main_window.cpp`) selects an
    empty sector too, and the coordinate line then adds the range and the selection
    brackets are drawn. Both must appear only while the selected sector holds an object
    the viewer sees, tested at every frame; the selection itself stays as clicked.

Our own choices made while implementing Q41–Q55 for the Log, the engine and Save Empire
(inferred). Q70 and Q72 are settled from the executable; Q71 has no counterpart:

70. **One entry per package.** Our engine logs a completed trade, gift or tribute as one
    entry for the whole package, where the original makes one entry per item. Its Goto
    follows the first of these the package holds: technology (Research), then resources,
    treaties or communication channels (Empires), else planets, vehicles and star charts
    (the first planet's or vehicle's location). Does the original's entry per item use
    these targets, and in what order are the entries made?
    **Answer:** one entry per item for each party concerned, none for the package
    (confirmed: binary; §4.1, spec 05 §3.4).
    - **Order.** The package is carried out when the acceptance arrives: a gift's or
      tribute's items from giver to receiver; for a trade, the proposer's offered items
      first (proposer to accepter), then the requested ones (accepter to proposer). Items
      go in package order. Each item makes the receiver's entry first, then the giver's.
      Every entry is category Politics. After the items, the acceptance itself becomes the
      proposer's (or the gift giver's) "Message" entry (§4.1).
    - **Planet** (only when it still exists, still belongs to the giver and is colonized;
      otherwise nothing at all): "Planet Received" for the receiver and "Planet Transfered"
      for the giver (the original's spelling), each naming the planet and the other
      empire. Goto: the planet's sector.
    - **Ship, base or unit group** (only when it exists and still belongs to the giver):
      "Vehicle Received" / "Vehicle Transfered", naming it. Goto: its sector.
    - **Resources:** the amount moved is the package's amount × 1000, at most the giver's
      stock; both entries are made even when nothing moves. "Resources Received" /
      "Resources Transfered" with the amount and the resource. Goto: Empires.
    - **Technology:** "Technology Received" for the receiver, saying the giver sent its
      data on the area, with a closing remark when the data is of no use (an area the
      receiver cannot research, data not ahead of the receiver's level, or an area unique
      or racially unique to the giver); "Technology Transfered" for the giver. Goto:
      Research for both. A level the receiver gains then adds its usual Research entries.
    - **Star charts:** "Starcharts Received" for the receiver only, naming the system and
      the giver. Goto: that system, with no sector. The giver gets nothing; nothing
      checks that the giver has explored the system.
    - **Treaty:** "Treaty Enacted" for both, naming the treaty and the other party. Goto:
      Empires. The entry is of the same kind as a first-contact entry, so each human
      party's history file also gets a contact line naming the other party (spec 05
      §3.4).
    - **Communication channels** (only when the receiver has no contact with the named
      empire C): the receiver and C are both set to treaty None, without a first-contact
      entry; "Comm Channels Received" for the receiver, "Comm Channels Opened" for C.
      Goto: Empires. The giver gets nothing. Nothing checks the giver's own contact with
      C or that C is alive.
    - **System** (a border claim): no entry; the giver's claim goes and the receiver
      claims the system.
    - Items no longer valid are skipped without any entry; there is no "unavailable"
      entry. "Any" items never reach this point. The code that carries a package out
      tests no game option, Allow Technology Gifts included (whether the option is
      enforced when a message is composed was not traced).
    - Accepting a treaty proposal makes no treaty entry, only the "Message" entry.

    The engine differs (`diplomacy.cpp`):
    - `acceptPackage` writes one "<Trade|Gift|Tribute> Completed" entry per party with a
      single target (`packageGoto`). It must write the entries per item above, in that
      order, with those titles and targets, and none for the package.
    - `executePackage` writes "Items Unavailable" entries; invalid items must be skipped
      silently.
    - Treaty items go through `setTreaty`, which logs "New Treaty" for both parties (and
      does so for accepted treaty proposals too, which the original never logs). A
      package treaty must log "Treaty Enacted" instead, plus the history's contact line;
      an accepted proposal logs nothing beyond its "Message".
    - Channel items go through `makeContact`, which makes "First Contact" entries and
      history lines and requires the giver's contact and a living C. They must set both
      sides to None with the two entries above, without those checks.
    - Technology is refused when `!allowTechTrades`; the package code must not test it.
    - A System item only drops the giver's claim; the receiver must claim the system.
    - Star charts require the giver to have explored the system and also copy the giver's
      known warp links; neither is in the original's package code.
71. **Refused commands.** A network or play-by-e-mail host can refuse a command that the
    player's client accepted; the original has no such case. Ours lists each one after
    the turn's entries as a Misc row "Order not carried out" without a Goto, not stored in
    the empire's log. Should they become ordinary log entries?
    **No answer needed:** an OpenSE4 choice with no counterpart. The original's host takes
    a player's turn as a snapshot of its part of the game (spec 05 §9.2), not as commands
    it could refuse one by one. Ours stays as it is.
72. **Save Empire.** Ours writes the empire file under the empire's name in our own
    folder, without the original's file picker. Designs come back in a new game after its
    starting designs, renamed when another design has their name, with the obsolete flag
    they had and the first combat strategy; designs whose hull, components or mounts the
    data set lacks are left out with a warning. Does the original keep the strategy?
    **Answer** (confirmed: binary; this has a counterpart in the original):
    - **Name.** The file name comes only from the empire file picker, which opens after
      the question about designs; cancelling it saves nothing. The game setup's empire
      window offers the same save.
    - **Strategies are kept.** The cleared game state leaves the empire's list of combat
      strategies, so the file holds it, and each design keeps its strategy (its place in
      that list). Loading restores both.
    - **Design marks.** Each saved design loses its obsolete and built marks, so it comes
      back current and editable, and the other empires' sighting dates. It keeps its
      creation date and its statistics.
    - **Where the designs go.** Loading an empire file that has designs first deletes all
      of that empire's designs, then adds the file's designs in file order, each into the
      first free place of the game's design list or at its end. So the file's designs are
      the empire's only designs.
    - **Parts and checks.** The hull, components and mounts are stored as positions in the
      data set's lists, not by name, so a data set with other lists silently gives other
      parts. Each design must pass the validity rules of spec 03 §4.2 without rule 2 (the
      technology test); a design that fails is dropped without a message. The mount
      technology test of rule 5 still runs, against the saved empire's research, which
      Save Empire cleared, so a design with a mount that needs research would be dropped
      (stock mounts need none; not traced further). Names are not checked, so duplicates
      stay (spec 03 Q50).

    The client and engine differ:
    - Our empire file stores neither the strategy list nor each design's strategy, so a
      loaded design gets the first strategy (`setup.cpp`, "Designs an empire file
      brought"). The file must store both and setup must restore both.
    - Ours keeps the obsolete flag (`setup_model.cpp` writes it, `setup.cpp` copies it). It
      must clear it (and the built mark) and keep the creation date and statistics.
    - Ours adds the file's designs after the empire's starting designs. In the original
      they replace every design the empire has, and an empire has none at creation (spec
      01 §3.6 "Starting assets").
    - Ours checks only that the hull, components and mounts exist. It must also drop
      designs that fail the validity rules other than the technology test.
    - OpenSE4 choices that stand: renaming on a name clash (spec 03 Q50), parts stored by
      name with a warning when one is missing (our own file format), and the file named
      after the empire without a picker.

Bringing the battle flow in line with Q30–Q33 (2026-10-01) left these choices of ours,
each marked "(inferred)" in the client. Q73 and Q76 are settled from the executable (what
a viewer sees of Q73 needs observation); Q74 and Q75 have no counterpart:

73. **What a displayed frame shows of a strategic battle.** The original redraws the small
    map after every step and repaints when it handles its messages, after each empire's
    phase and each combat turn, with no hold anywhere (Q30). Our Strategic Combat window
    fights one empire's phase per displayed frame and draws the map after it, so the
    steps of a phase appear together; nothing is held. Is that what a current machine
    shows of the original (it repaints only at those points), or are the steps of a
    phase visible one by one there?
    **Answer** (confirmed: binary; §1.10.5 step 5): the original does not wait for
    repaints. After every single step of a piece or seeker it paints the small map
    straight onto the window; after each combat turn it draws "Combat Turn N" straight
    onto the window, repaints the forces list at once and paints the map. The message
    handling after each empire's phase and each combat turn only serves input and other
    windows. Nothing waits anywhere. So each step is drawn as it is taken and stays only
    until the next is drawn: on a period machine without desktop composition each step
    appeared as drawn; on a current machine many steps, often whole combat turns, fall
    between two display refreshes, and only the state at each refresh is seen.

    Our client differs: `screens/strategic_combat.cpp` (the window's `update`) fights one
    empire's phase per displayed frame, so a battle lasts at least as many refreshes as it
    has phases, slower than the original on a current machine. To match, it would fight
    on, step by step, until the next refresh is due, then show that state.

    Open: needs observation of what a viewer sees. Record the original's Strategic Combat
    window with a frame-accurate capture (60 fps or more) during a simulator battle of
    several combat turns between two or three sides; count the distinct map states and
    the "Combat Turn" values seen between Begin and the moment Close lights up, and time
    that span.
74. **Battles of games on different machines.** The original shows the Strategic Combat
    window, before the battle, to the human whose turn it is (§1.10.5). Our network and
    PBEM hosts fight every battle without stopping, so our client shows a network game's
    battles the player was in, and a PBEM player's battles started by its own orders,
    afterwards, played back from the record one combat turn per frame, with the results
    already in the game. To match, the host (or the PBEM player's own turn) would have to
    stop as a local game does.
    **No answer needed:** an OpenSE4 choice with no counterpart. The original has no host
    for a turn-based game on different machines: the game file passes from player to
    player, and each battle is fought on the machine of the player whose turn it is (spec
    05 §9.1). Our hosts are an OpenSE4 extension (spec 05 §9.5), and showing their battles
    afterwards is part of it.
75. **A mixed group in a battle shown afterwards.** The battle record keeps a unit group's
    first design only, so a battle shown afterwards (Q74) counts a group that mixes
    designs under that design's hull, where the live window counts each stack under its
    own (§1.10.5).
    **No answer needed:** an OpenSE4 choice with no counterpart; it concerns only the
    playback of Q74, which the original does not have.
76. **A notice before a shown simultaneous battle.** On one machine the notice comes first
    when the empire whose turn it is is computer-controlled (§1.10.5). A simultaneous
    turn has no such empire; ours shows no notice there. Does the original?
    **Answer:** no (confirmed: binary; §1.10.5). The notice belongs only to the question
    form: it comes when a battle on one machine is about to ask Tactical or Strategic and
    the empire whose turn it is is computer-controlled. A simultaneous battle never asks.
    With `Simultaneous Games Show Strategic Combat` on, the Strategic Combat window opens
    at once in its Begin and Close form for every battle, computer-only battles included:
    the setting takes the place of the test for a human side.
    Our client matches for the notice (`ClassicMode::drawBattleQuestion`,
    `classic_mode.cpp`, shows it only in turn-based games). The engine differs: `resolve()`
    (`combat_space.cpp`) stops a simultaneous turn to show a battle only when it has a
    human side (it requires `humans` to be non-empty; the comment in `turn.cpp` says the
    same). With the setting on in a simultaneous game, every battle on that machine must
    stop to be shown.

Bringing the combat windows in line with Q34–Q36 and Q38 on 2026-10-01 left these
choices of ours (inferred) (Q73–Q76 are those of the live battle flow). All are settled
from the executable; Q77's timings still need measuring and Q81 has no counterpart:

77. **Animation frames.** How many frames does the original's slide over one square
    take, and how many its turn to a new facing (one per 45 degrees?), its torpedo's
    flight (one per square?) and a beam (one stamp per square?)? Ours: 6 for a slide, one
    per 45 degrees, one per square, and one stretched stamp drawn then erased; a
    seeker's step is drawn as a torpedo's flight. A loss plays the 8-frame explosion
    with the hit's waits; a miss adds nothing to its shot; a launch, a landing or a
    capture flashes for 4 frames without a wait. Every frame stays at least one display
    refresh.
    **Answer** (confirmed: binary; §1.10.3 holds the full table). The same holds in Combat
    Replay with its own switches, except that the replay has no 0.3 s pause.
    - **Waits:** each one first handles pending window messages, then busy-waits on the
      system's millisecond tick counter (§1.10.3). Under Wine the nominal waits hold; on
      current Windows each frame lasts about 15.6 ms.
    - **Slide:** 1 px a frame along the longer axis, 36 frames for any one-square step,
      diagonal too, 1 ms after each; no slide and no wait when either square is outside
      the shown part of the map. With the movement animation off the piece jumps and then
      waits 0.1 s.
    - **Turn:** before the step, the shorter way (clockwise for a half-turn), 5° a frame,
      9 frames per 45°, 0.01 s after each; only with the movement animation on and the
      square in view.
    - **Seekers** never turn; a seeker's step is the ship's slide, not a torpedo's
      flight. Their launch is not animated.
    - **Torpedo:** 4 px a frame (9 frames per square), 1 ms after each; with Fast Tactical
      Combat 6 px a frame (6 per square).
    - **Beam:** one stamp every 6 px (6 per square), drawn one by one outward with
      0.00001 s after each, then erased one by one in the same order with 0.00005 s after
      each.
    - **Shots:** a hit ends at the target's centre (a random point of a planet's 4x4
      block), a shield-only hit 17 px short of it, a miss 18 px off in x and y with random
      signs; a one-square target with shields up shimmers (8 frames, one per 8 shot
      frames) while the shot is drawn.
    - **Impact:** only a hit that damages structure or destroys the target plays the
      8-frame explosion (0.1 s per frame and after the wipe), once, also for a loss; a
      shield-only hit stamps one shield picture (not with Fast) and has no wait; a miss
      adds nothing. The 0.3 s pause comes only in Tactical Combat after a seeker's impact
      on a piece that survives it.
    - **Launch, landing and capture:** no animation, only a redraw.
    - **Random numbers:** a miss's direction (two draws) and the point on a planet (two
      draws) are drawn from the battle's own random sequence. So a battle shown in the
      Tactical Combat window uses more random numbers than the same battle fought unseen,
      and what follows in it can differ. Whether Combat Replay draws from the game's
      sequence too was not traced.

    Our client differs (`CombatPlayback::framesOf` in `client/classic/replay.cpp`, the
    drawing in `screens/combat_map.cpp`):
    - `kSlideFrames` 6 must become 36 (1 px a frame), and a seeker's step must be this
      slide, not a torpedo's flight.
    - `turnSteps` must give 9 frames per 45° (5° each, clockwise on a tie), and no turn
      frames when `animateMoves` is off or the square is out of view.
    - Torpedo: 9 frames per square, 6 with Fast Tactical Combat.
    - Beam: 6 stamps per square drawn one by one and then erased one by one, instead of
      one stamp stretched by `drawSpriteAlong`.
    - `Kind::Hit`: an explosion only when structure is damaged or the target destroyed;
      a shield-only hit gets one shield stamp and no wait; the 0.3 s `AfterHit` frame only
      after a seeker's impact that the target survives, and only in Tactical Combat. The
      battle record must tell structure damage from shield damage.
    - `Kind::Destroyed` must not play a second explosion.
    - `Kind::Captured` and `Kind::Launch` must drop the 4-frame flash (`kFlashFrames`).
    - Misses must end at the diagonal half-square offset, shield-only hits 17 px short,
      and shielded one-square targets shimmer.
    - The engine's battle does not draw the display's random numbers, so a battle shown
      tactically follows another random course in the original than in ours; matching it
      would need the tactical window to draw those numbers from `GameState::rng` at the
      same points (spec 04 §19.1).
    - Keeping each frame on screen for at least one display refresh stays an OpenSE4
      choice; it is close to what Windows gives (about 15.6 ms a frame).

    Open: needs observation, to confirm under Wine with a high-rate capture of the
    Tactical Combat window (Fast Tactical Combat off): the duration of a one-square slide
    (about 36 ms or more), of a 45° and a 90° turn (about 90 and 180 ms), of a beam and a
    torpedo over a known distance; that shield-only hits show no explosion; that misses
    land half a square off diagonally; that only seeker impacts are followed by the 0.3 s
    pause; and which losses use the 72x72 explosion.
78. **Combat Piece Report details.** Ours writes "K" thousands truncated (150999 →
    "150K"); a drone whose drone target has died shows None until it picks another; the
    Facil and Ability tabs show the colony or ship as the battle began; the report has a
    Close button in its title strip, as our tab strip leaves no room under the page.
    **Answer** (confirmed: binary; §1.10.1):
    - **"K":** right. Current supply and capacity are written separately; each number
      strictly above 100000 becomes its thousands, truncated by integer division, then
      "K" (150999 → "150K"; 100000 stays "100000"). Our client matches
      (`supplyNumber()`, `combat_logic.cpp`).
    - **A dead drone target:** the line looks the target up by its piece number; a
      destroyed piece leaves the list at once, so the line reads None until the drone's
      planning picks again. Our client matches. One quirk of the original: a new piece
      can take the dead piece's number, and the drone then shows and goes for that new
      piece (spec 04 §10.7, "A reused piece number"); the engine does not reproduce it.
    - **Facil** shows the colony's facilities as they are when the report opens, none
      marked lost; lost facilities stay in the list until the battle ends, so during a
      battle this is the set the colony began with. Our client matches in effect.
    - **Ability** lists, for a ship or base, its hull's abilities, then its whole
      design's (every component, destroyed or not), then its own; for a planet only the
      planet's own abilities, not its facilities' or its colony's.
    - **The window** is borderless, 310×420, as every report opened on its own (the size
      in the form resource is overridden): pages at (10,10), the four 72×30 tabs at
      (10,340), and a Close button 153×30 centred under them at (79,380), its bottom
      10 px above the window's.

    Our client differs: its Ability tab (`abilities()` in `screens/tactical.cpp`) leaves
    out destroyed components (`vehicleAbilities()`) and lists a planet's facility and
    colony abilities (`colonyAbilities()`); its `CombatPieceReportScreen` is a 353×422
    dialog with a 64×22 Close button in the title strip at (279,7). The window must be
    310×420 with the tab strip and the Close button laid out as above.
79. **Fleets For Plr.** Ours hides Fleet Transfer's Existing Fleets button (the real
    game's fleet list) while it works on the simulator's sandbox. Is it there in the
    original, and what does it list?
    **Answer:** it is there, in the simulator as in the real game (confirmed: binary;
    §1.10.4). Fleet Transfer always has Create Fleet, Formation, Strategy, Existing
    Fleets, Add All and Remove All. Existing Fleets opens the ordinary Ships\Units window
    of the player's real empire, with the empire's saved Show Ships, Show Units and Show
    Fleets switches and tab, never the simulator's ships. Opened this way the window is
    for viewing only: a left-click on a row does nothing (normally it goes to the object
    and closes the window); this holds in the real game's Fleet Transfer too. In the
    simulator, Fleet Transfer itself lists the chosen side's ships and bases from the
    setup (not its unit groups or planets) and that side's copied fleets.

    Our client differs (`screens/fleet_transfer.cpp`): it hides the button over the
    sandbox (`!sandbox_`), and in the real game it opens Ships\Units with only Show
    Fleets on. The button must appear in both cases and open the real game's Ships\Units
    with its saved switches, in a mode where a left-click does nothing.
80. **Change Cargo.** Ours lists every vehicle and colony of the setup in one Cargo
    Transfer window by giving them all to the chosen side in the sandbox; the deferred
    Load and Drop Cargo orders are hidden; population moved onto a ship is not kept; and
    the setup then accepts any unit design a sample colony stores, foreign ones
    included. How does the original list the holders of different sides, and where do
    units for a ship come from without a sample colony that stores them?
    **Answer** (confirmed: binary; spec 04 §17):
    - The window is titled "Transfer Cargo" and has only Move One, Move Five, Move Ten,
      Move Hundred and Move All, in every mode; no Load or Drop Cargo order buttons.
    - **Left list:** every row of the Combat Vehicles list as a holder, all sides
      together in that list's order (sides 1–10, then neutral objects), each followed by
      its population and cargo lines. Planets carry their population bars in their side's
      colour; nothing else marks a side.
    - **Right list:** only a "Storehouse", made when the window opens: a copy of the
      player's first colony (systems in order), owned by side 1, renamed "Storehouse",
      given `Cargo Storage` 500000000, 1000 of every unit design the player owns or has
      seen (troops, fighters, satellites, mines, drones, weapon platforms; sorted by the
      owner's empire name, then the design name), and 10000M of side 1's population on
      top of the copied colony's own population and stored units. It is removed when the
      window closes. Without a colony there is no Storehouse and the right list is empty.
    - **Moves:** a click on a cargo line moves it to the holder selected in the other
      list, so cargo goes only between a setup holder and the Storehouse, never directly
      between two holders, and no owner is checked. Plague quarantine and "leave at
      least 1M" still apply.
    - **What stays:** everything moved stays with the setup's objects; population moved
      onto a ship stays aboard and fights in the battle.

    Our client differs: `simulatorSandbox()` (`combat_logic.cpp`) gives every holder to
    the chosen side in one shared list and takes units only from sample colonies' stores;
    `simulatorTakeBack()` keeps units only, so population moved onto a ship is dropped;
    and `cargo_transfer.cpp` shows "Load Cargo Order" and "Drop Cargo Order" in the real
    game, which the original's window never has (it hides them over the sandbox). The
    holders must be listed as above against a Storehouse built as above, population on
    ships kept, and the two order buttons removed in every mode. Its Move steps must add
    Move Hundred.
81. **Ship numbers of items without one.** An item put in the setup without a number
    (a sample setup made by the program) takes the next numbers of its side, in item
    order, after the side's counter and every number already given.
    **No answer needed:** an OpenSE4 choice with no counterpart (confirmed: binary). In
    the original a ship enters the setup only by a click, which names it at once with
    its side's counter + 1 (spec 04 §17); there are no setups made by the program, and
    units get no numbers. Ours stays as it is (`game::combat::simulatorNumberShips`).
82. **Side colours.** Ours uses the plain colours of those names (red 255,0,0; blue
    0,0,255; green 0,128,0; yellow 255,255,0; purple 128,0,128; white; aqua 0,255,255;
    lime 0,255,0; maroon 128,0,0; olive 128,128,0). Are these the original's values?
    **Answer:** yes, exactly these, white being 255,255,255 (confirmed: binary; spec 04
    §17). The number is white on 1, 2, 3, 5, 9 and 10 and black on the others, as ours.
    The box is a plain filled rectangle with no outline, 26×18 where a large flag would
    stand and 14×10 where a small one would, the number centred both ways in whatever
    text font the window is drawing with.
    Our client differs only in `drawSideBox()` (`screens/combat_map.cpp`): it adds a 1 px
    black outline and always uses the bold font; both must go. (An out-of-range side is
    black in the original and grey in ours; that case never arises.)

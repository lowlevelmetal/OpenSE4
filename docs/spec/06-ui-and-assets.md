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
  dialog handles a few keys of its own (§3.4) (confirmed: binary). Our client follows this
  since 2026-10-04: while a window or one of the game's questions is open, the main window
  takes no input at all (command buttons and their hover hints, order strip, selectors,
  report panel, map clicks and keys), and only the window in front takes input; the windows
  behind it wait until it closes. End Turn is never carried out while a battle is fought
  or asked about.
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
| Launcher (added in 1.95) | Pick what to start. | The `StartMenu.bmp` picture as a 736x536 window centred on the screen; one column of ten text buttons, 311 px wide, every 31 px from (43,210): Play, Readme, History, Extras, PDF Manual, HTML Manual, Map Editor, two website links, Quit. Play closes it and opens the Intro; Quit exits; the others open a document, a web page or the separate map editor and leave the launcher open (confirmed: binary). | Program start without command-line arguments (the Steam shortcut). Any argument skips it: the intro stays hidden and the program reads `<game file> <password> [<player number> [<mod folder>]]`, hosting the game's turn or signing a player in; the fourth names a mod folder and overrides `Path.txt` (spec 05 §9.2; confirmed: binary; corrected 2026-10-14, complete listing). |
| Intro | Title screen. | First the publisher's logo (4 s), then the developer's logo (4 s); a key or click skips to the intro picture. The picture comes from `Game/Screens/1024X768` when the screen is wider than 800 px, else from `800X600`; data loading starts 2 s after it appears (at once after a skip) (confirmed: binary). Background art, version label, "Loading" progress while data files load (buttons disabled until done); buttons Quick Start, New Game, Resume Game (loads the last saved game, which the game records per computer), Load Game, Tutorial, Scenario, Credits, Quit Game in two rows along the bottom [T][S]. | Launcher → Play, or program start with arguments. |
| Quick Start | One-click game. | Grid of race portraits (the list of styles comes from `Quick Start Style N` in Settings.txt [M]); Begin Game, Cancel. Pages of eight in two columns of four, in that list's order, the left column first; an arrow click turns a page (observed, spec 07 session 5). | Intro. |
| Game Setup (8 pages) | Configure a new game. Pages: Quadrant, Events, Technology, Player Settings, Players, Victory Conditions, Game Settings, Mechanics. | Full-screen; page buttons plus Begin Game / Cancel on every page. Quadrant page has a map preview with Generate Map Now and Load Map; Players page has Add New, Add Existing, Remove, Save To File [T]. Opens on the Players page with an empty list and forgets everything between games; every page's layout is in spec 07 session 5 (observed). | Intro → New Game; File Menu → New. |
| Empire Setup (6 pages) | Create an empire. Pages: General, Environment, Culture, Characteristics, Advanced Traits, Description. | Full-screen; page buttons plus Create Empire / Cancel. General page has a race-style picture chooser with arrows (the race portrait and a ship of the style side by side), design-name-file picker, minister style [T], and Password and Email boxes. It starts empty; the ▽ buttons open 340×370 list pickers; layouts in spec 07 session 5 (observed). | Game Setup → Players → Add New. |
| Culture Modifiers Comparison | Compare cultures. | A 780x475 dialog: the list of cultures with ten modifier columns, a legend under it, Close; layout in spec 02 §9 (confirmed: binary). | Empire Setup → Culture → Compare Culture Modifiers. |
| Load Game | Pick a save. | Two-column list (name without extension, last-modified time), scroll arrows, Cancel. Clicking a row loads it [T][S]. A 420×520 dialog: "Save Game Name" and "Date" columns, names in capitals, Change Directory and Cancel under the list (observed, spec 07 session 5); Add Existing's "Load Empire" is the same dialog. It opens sorted by name, A to Z; a click on the "Save Game Name" or "Date" heading sorts by that column, dates newest first, with the usual five-key history of §7 Q24 (confirmed: binary; that a heading click does it, inferred). Ours since 2026-10-14 (`FileSortHistory`, `screens/file_list.hpp`), the history kept while the dialog is open. | Intro; File Menu → Load. |
| Select Directory | Choose a folder. | A 400x500 window: a Drive box ("c:" at first), a 300 px list of the folders (a double click opens one, inferred), an editable Current Directory box, OK and Cancel (confirmed: binary). Our client: the Drive box lists the drives on Windows and the root elsewhere, ".." heads the list, a folder typed into Current Directory with Enter is opened, and Default goes to the saves folder (OpenSE4's); the places inside the window are ours (inferred). | Load Game (and the original's Save Game) → Change Directory. |
| Load Map | Pick a saved map. | (not documented) | Game Setup → Quadrant. |
| Scenario chooser | Pick a scenario. | The Load Game picker in scenario mode, titled "Load Scenario": the first column "Scenario Filename", then Date. It lists the scenario folder's files whose lower-case name contains "_settings.txt", each as its name without that part in capitals, with the file's date; Change Directory and Cancel as in Load Game. A row starts the scenario with the stem folder + name (§5.6); Cancel does nothing. The Tutorial button starts the scenario folder's Tutorial without the picker (confirmed: binary). Ours: the Learn window's Scenarios tab lists them beside the mods' scenarios (§5.6). | Intro → Scenario. |
| Credits | Rolling credits. | — | Intro. |
| Login To Game | Pick who plays a simultaneous game that is loaded. | A row per empire: its flag, its name (or "Dead") and its emperor's name (blank when dead), then a "Game Master" row marked "(Host)"; a password box, Login and Cancel. Both passwords are compared trimmed and in small letters; a wrong one gives an invalid-password note. An empire signs in only when the game's data-set checksums match the install's ("Invalid Data Files"); Game Master opens the host's view (player 0), an empire that empire's turn. The TCP/IP host's continuation of a game lists the empires only (confirmed: binary). Ours since 2026-10-14: see below the table. | Loading any simultaneous game, on one machine or several, a single player's included [T]. |
| Data File Errors | Report a data file's errors at start-up. | A window per data file with errors, its title naming the file and its path ("<File> ... Errors: <path>"), the list of errors and Close (confirmed: binary). Whether the game then goes on is (inferred). Ours since 2026-10-14: the same windows, one after another, over the intro, when the installed data has errors without mods; the client goes on with what loaded (inferred). Errors that come with mods start the game without the mods, or not at all for mods given on the command line, as before. | Program start. |

**Login To Game in OpenSE4** (since 2026-10-14, `ClassicMode::drawLogin`): it opens when a
simultaneous game is loaded from a saved game (Load Game, Resume Game, the Game Menu's Load,
`--load`; not a tutorial's game resumed), before the main window shows. Our client differs:
- OpenSE4 keeps only a hash of each password (or, in a network or e-mail host's file, a
  verifier). The password typed is tried trimmed and in small letters, then trimmed as typed,
  then as typed (`ClassicSession::loginPasswordMatches`): a password set in small letters, as
  every imported one is, opens in any letter case, but one set with capitals opens only with
  those capitals.
- OpenSE4 has no host view on one machine: Game Master (with the game's master password, an
  empty one in a local game) goes on to the first human player's turn behind the Next Player
  window (§1.2.1), which asks that empire's password (inferred stand-in).
- A destroyed empire's row, and a computer-controlled one's, sign nobody in.
- Only a header that names a data set's identity (a host's or e-mail game file) is checked for
  "Invalid Data Files"; OpenSE4's local saves keep the game folder's name, and their mods are
  checked when they load.
- Cancel returns to the intro.

### 1.2 Main window and the windows opened from its command buttons

| Screen | Purpose | Regions / controls | Reached from |
|---|---|---|---|
| Main Window | Everything during a turn. | See §2. | Game start / load. |
| Game Menu ("File Menu") | File operations. | Vertical buttons: New, Load, Save Game, Save Map, Save Empire, Players, Options, Delete Game, Quit, Close [T][S]. Save Map is enabled only when the game's setup lets players save the map; Save Empire asks whether to include the designs; Esc closes the menu. New asks whether to cancel the game and start a new one; Yes drops the game and opens the eight-page Game Setup at once, over a blank backdrop with the star-map picture: its Begin Game starts the new game, its Cancel goes to the intro (confirmed: binary). Ours since 2026-10-14: New's Yes opens OpenSE4's Game Setup the same way, over its own background. | F2 / first command button. |
| Save Game, Delete Game | Name / remove a save. | Save Game is the Load Game dialog titled "Save Game": the saves, name and date; New Save Game, Change Directory and Cancel. A click on a save overwrites it at once, without a question; New Save Game asks for the name ("New Save Game Name"), and a name that cannot be a file name gives "Invalid File Name." (titled "Error") and asks again; Change Directory saves to another folder this once; the window closes after saving, with no message. Delete Game is the same dialog: a click deletes that save, and a file of the same name in `temp/`, without a question, and closes. Files go to the folder in §6.1 (confirmed: binary). Our client differs: OpenSE4 keeps its own Save Game window (a name box, the saves listed, Save, Save for SE IV, Close) and asks before it overwrites or deletes a save. Since 2026-10-14 it has the original's Change Directory, captioned Change Folder (the whole caption does not fit our button): that window's saves go to another folder, the saves folder staying the default for the next one; and it refuses a name that cannot be a file name with that message (which names are refused is ours: `validSaveName`, `screens/file_list.hpp`). | Game Menu. |
| Player Computer Control | Hand empires to the computer or take them back. | Check list titled "Empires Under Computer Control", 420x520, OK / Cancel: one row per empire in empire-number order (every empire, neutral and destroyed ones included) with a lamp, the empire's flag and its name; a lit lamp means computer-controlled. The same window serves the Combat Simulator [T] and Reset Passwords (§1.9). Rules in §1.2.1 (confirmed: binary). | Game Menu → Players. |
| Options (per computer) | Sound, music, animation and autosave. | Lamp list under headings; Close. Full list in §1.9 (confirmed: binary). | Game Menu → Options. |
| Designs | Browse and manage designs. | Design list; detail pane (stats, then either components or, with Stats\Strategy on, build/loss/kill counts and default strategy). Tabs: Ship Designs, Unit Designs, Enemy Ship Dsgn, Enemy Unit Dsgn; the window opens on the tab kept with the empire's options. Actions: Create, Copy, Edit, Upgrade, Make Obsolete, Hide Obsolete, Stats\Strategy, Simulator [T]. Which are lit, and what the enemy tabs list: §7 Q93 (confirmed: binary). | F3. |
| Create Design | Ship designer. | Size, type and name pickers; running totals; components-on-design and components-available lists; warnings box; hover detail. Comp Type (group filter), Weap Mount, Condensed View, Only Latest, Weapons Report, Create Design, Cancel [T]. Ours is taller at 1024x768 (§7 Q94). | Designs → Create / Copy / Edit. |
| Select Vehicle Size | Pick the design's size. | List window: the researched hulls of the vehicle type in data order, columns Pic, Name, Size, Cost; an "Abilities" box under the list; Cancel (§7 Q94, confirmed: binary). | Create Design → Size. |
| Select Component Group | Filter Components Available. | List window: the General Groups of the components available and "All", sorted; Cancel picks All (§7 Q94, confirmed: binary). | Create Design → Comp Type. |
| Select Weapon Mount | Pick a mount for weapons added next. | List window: "Normal Mount" first, then each mount with its description in grey under its name; a description pane under the list; Cancel (§7 Q94, confirmed: binary). | Create Design → Weap Mount; Weapons Report. |
| Combat Simulator | Set up a test battle. | Vehicles-in-combat list, design list, owner picker; Tactical / Strategic choice; No Obsolete, Strategies, Computer Control, Fleets for Plr, Change Cargo, Begin, Cancel [T]. Details in §1.10.4 (confirmed: binary). | Designs → Simulator. |
| Planets | All planets seen. | Statistics block; galaxy mini-map highlighting the hovered row; planet list. Filter tabs: All, Colonizable, All Colonies, Enemy Colonies, Ally Colonies, Coloniz\Empty, Coloniz\Breathe, Ship Enroute, Asteroids, Special; toggle No Sys To Avoid; Send Colony Ship [T]. Rules and layout in §1.8.1 (confirmed: binary). | F4. |
| Colonies | Own colonies. | Statistics; mini-map; sortable list whose columns depend on the tab: General, Value, Production, Facilities, Cargo, Construction, Status (status icons), Races, Orders. Actions: Scrap Facil Types, Set Colony Type [T]. Columns and sorting in §1.8.3 (confirmed: binary). | F5. |
| Ships\Units | Own ships, units, fleets. | Statistics; mini-map; list. Column tabs: General, Orders, Cargo, Fleet, Maintenance (each starts with a picture column); toggles Show Ships / Show Units / Show Fleets [T]. Columns and sorting in §1.8.3 (confirmed: binary). | F6. |
| Construction Queues | All queues. | Statistics; quadrant map; queue list. Column tabs Rate, Usage, Planet Value, Facilities, Cargo; inclusion toggles Ships, Planets, Ship SY, Planet SY; Multi-Add (acts on the shift-selected queues), Scrap Facilities, Upgrade Facilities [T]. Rules and layout in §1.8.2 (confirmed: binary). | F7; Log → Constr. Queues. |
| Set Construction Queue | Edit one queue. | Buildable-items list (tabs Ships, Facilities, Units, Upgrades, grouped under headers; Only Latest); queue owner header; the queue itself; hover detail. Toggles Emergency Build, Repeat Build, Queue On Hold; Set Move To / Clear Move To; Fill Queue; Clear Queue; Reorder Queue [T]. A unit design asks for the "Amount" first; refusals are "Warning" boxes (§1.8.3, spec 02 §6.5). | Order "Build Queue" (Q); row in Construction Queues. |
| Select Queue Type | Fill a queue from one of the empire's queue types. | Type list; Add Type ("Queue Type Name"), Delete Type, Cancel (spec 02 §6.4, confirmed: binary). Ours adds the types kept on this computer and the built-in ones under their own headings, and Keep on Computer. | Set Construction Queue → Fill Queue. |
| Research | Choose research. | Points available; tech-area list (left-click adds; a click on a queued or completed area, or with 12 queued, does nothing; right-click opens the area's report, as on a project box); current projects in pages of four (Projects 1-4, 5-8, 9-12); Repeat Projects, Divide Pts Evenly, Tech Tree (only if the game allows it), Reorder Projects [T]. | F8. |
| Tech Tree | Whole tree. | Titled "Technology Tree": every tech area of the data set under alphabetical group headings. Two list views: Tech Areas (prerequisites), Tech Levels (what each level makes available); Export in slot 13 writes the view to a text file (§7 Q92, confirmed: binary) [T]. | Research → Tech Tree. |
| Empires | Diplomacy hub. | One portrait per known empire with a stat column under each; tabs Treaty, Trade, Tariff. Buttons History, Treaty Grid, Intelligence, Borders, Victory Conditions, Scores, Comparisons, Our Race [T]. Left-click a portrait opens Communicate, and does nothing once a message has gone to that empire this turn; right-click opens Race Report. The columns and the remembered tab are in §1.5 "The Empires window's columns" (confirmed: binary). | F9. |
| Log | Turn news. | See §4.1. | F10; auto-opens at turn start. |
| Empire Status | Budget and empire settings. | Balance sheet (income lines, expense lines, net per turn, treasury and storage cap; see spec 02). Buttons Empire Options, Ministers, Systems To Avoid, Waypoints, Strategies, Repair Priorities, Change Email, Change Password [T]. | F11. |
| Options (Empire Options) | Per-empire UI and behaviour switches, saved with the game. | Scrolling list grouped under headings: General Options, Next/Previous, Ship Movement, Ship Orders, System Display, Galaxy Display, Latest Items, Politics [S]. Full list with defaults in §1.9 (confirmed: binary). | Empire Status → Empire Options; Log → Goto on some entries. Not Game Menu → Options, which opens the per-computer Options window (confirmed: binary). |
| Ministers | Automation switches. | Minister list with lamps; Select All/None; Indiv. On/Off (each asks "Confirm" first); Complete AI On/Off, plain buttons without a state of their own (spec 02 §10, confirmed: binary) [T]. | Empire Status. |
| Systems To Avoid | Mark systems for pathing. | Map (a click toggles the avoid mark, on every tab; confirmed: binary); tabs Avoid, Presence, Ally Claimed, Enemy Claimed [T]. Our client follows this since 2026-10-13 (it used to toggle a claim on Ally Claimed); its Release All Claims on that tab is OpenSE4's own. | Empire Status. |
| Waypoints | Manage waypoints. | Waypoint list; quadrant map; ships heading there; yards auto-sending there; Set (returns to main window to pick a spot), Delete, Rename [T]. The list's rows read "1 - name" to "10 - name"; Rename's prompt starts empty; the ships en route are the own vehicles with a Move To Waypoint order for it anywhere in their lists (a plain Move To there does not count), fleet members one by one, and a right-click on one opens its report (confirmed: binary). Ours follows since 2026-10-14; its Location column is its own. | Empire Status. |
| Strategies | Combat AI presets. | Strategy list and property pages (Movement, Firing, Damage, Launching, Formation); Add asks "Enter Strategy Name" (spec 04 §16, confirmed: binary) [T]. Copy and Remove are ours. | Empire Status; Combat Simulator. |
| Repair Priorities | Repair order by component group. | Available groups; ordered priority list; Remove All [T]. | Empire Status. |
| Help | In-game encyclopedia. | Item list; detail pane (128 px picture, cost with resource icons, stats, abilities) [S]. Tabs: Components, Weap Mount, Facilities, Ship Sizes, Unit Sizes, Tech Areas, Treaties, Intel Projects, Formations, Hotkeys; a gap; Weapons Report (slot 12), Manual (slot 13), Close (observed). Lists only what the empire knows, by name in alphabetical order with a lamp per row; layout in spec 07 session 5. The original's Manual opens the installed `manual\SE4 Manual.htm` in the browser at once (confirmed: binary); ours opens OpenSE4's Manual window, and since 2026-10-14 the gap (slot 11) holds Original Manual, which opens the installed manual in the browser, when the install has one. | F1. |
| Weapons Report | Weapon damage-by-range grid. | Filters All, Direct Fire, Seeking, Point Defense, Warhead; Weapon Mount; Only Latest; range pages Dmg 1-10 / 11-20 [T]. The known weapons of the kind chosen, never filtered by mount or vehicle type: the mount changes only their size and damage figures, and its name is in yellow in the title strip. Weapon Mount opens "Select Weapon Mount" with the mounts valid for the design's vehicle type and hull, from the designer, or only "Normal Mount" from Help; the report starts on Normal Mount (confirmed: binary). Only a right click on a weapon opens its report (confirmed: binary, 2026-10-14; ours opened it on a left click too until then). Ours: a weapon whose type the mount does not take keeps its plain figures (inferred, §7 Q113). | Help; Create Design. |
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
  which ignore leading or trailing spaces, and Login To Game letter case too (§1.1; the Next
  Player window keeps it, below). Without a master password the window opens at once.
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
  players eliminated" with a Finale "Human Dead" picture and plays no further turn: the
  program exits. The host's processing of a game on different machines has no such check.
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

**The Next Player window** (confirmed: binary).
- It comes at each human player's turn whenever more than one empire is human-controlled,
  destroyed ones included, so a hotseat game's last surviving human still sees it every turn.
  In a turn-based game it comes before that player's turn starts: then the autosave if one is
  due (§6.1), the endings (§1.9), and the turn's start (spec 05 §8).
- A 276x188 window: the empire's picture and a line naming the empire and inviting its player,
  and a password box when the empire has a password (not "master", above). Spaces before and
  after the password are ignored and letter case counts (Login To Game, §1.1, ignores both).
  A wrong password changes the box's label to "Invalid Password:"; the fourth wrong one in a
  row exits the program.
- Loading a game: an autosave or a hand-over file opens the Next Player window, then that
  player's turn starts; a plain save asks nothing (spec 08 §5); every simultaneous game,
  a single player's included, asks Login To Game (§1.1) instead.

OpenSE4 follows this since 2026-10-14 (`ClassicMode::drawNextPlayer`, `nextPlayerDue`) in
local and hotseat games, with the map hidden while it waits, its own Begin Turn and Quit Game
buttons, the empire's race portrait, and a line of its own with the game date. Our client
differs: OpenSE4 keeps only a hash of the password, so the password typed is tried trimmed,
then as typed (one set with spaces around it); "master" is recognised in a local game's hash
only, not in a network or e-mail game's verifier; and the fourth wrong password returns to
the intro, where the original exits (§1.7). Before 2026-10-14 the window came only when the
turn passed to another human, a loaded game counted only living humans, passwords were
compared exactly, there was no limit on tries, and a loaded simultaneous game asked no
sign-in.

**The engine and client follow this subsection since 2026-10-01:**
- The Players window (`playersPopup` in `src/client/classic/screens/game_menu.cpp`) is the
  check list above, every empire in number order, behind the master password when the game
  has one (exact comparison, `ClassicSession::masterPasswordMatches`); OK applies every row
  clicked at least once.
- `game::ai::setComputerControl` (`src/game/ai.cpp`) is the switch, a local or host-side
  change rather than a player command: to computer, kind Computer, every minister and every
  own vehicle's, fleet's and colony's flag on; to human, all off. Since 2026-10-03 it works on
  every empire: a neutral empire handed to a human keeps neutrality in a mark of its own
  (`Empire::neutral`; `isNeutral()` is what the neutral rules read), as §7 Q84 answers. It keeps `ministerStyle`,
  `aiMinimalChanges`, `ministersForNewVehicles`, `passwordHash` and a stored `aiDifficulty`.
  A human empire whose ministers act stores Medium each turn (`ai::recordAiDecisions`), so a
  round trip plays at Medium as in the original.
- A local or hotseat game in which no living empire is human-controlled plays no further
  turn (`ClassicSession::humansGone`, `LiveOptions::endWithoutHumans`) and shows the Finale
  "Human Dead" ending; hosts and automated runs play on. A human destroyed at the start of its
  turn, playing that turn marked dead (spec 05 §6), still counts until the turn ends
  (`ai::anyHumanLeft`). Our client differs: closing the ending returns to the intro, where the
  original exits (§1.7, since 2026-10-14).
- On a player's copy of a game on different machines (network or e-mail), the window
  changes only that copy; for the player's own empire the session also gives
  `cmd::SetMinisters` (every area and the fleets' flag) and `cmd::SetMinister` (complete
  control), and, of its ships, units and colonies, a `cmd::SetMinister` for each one changed
  during the turn as §7 Q99(d) counts it, before or after the switch
  (`ClassicSession::markChanged`), so the orders carry what §7 Q84 lists, never the mark.
- The TCP/IP host's toggle (`HostSession::setAiControl`, the in-game host's Empires list)
  flips only the mark (`ai::setComputerMark`), spec 05 §9.4, on every row: the list holds
  every empire but the host's own, and an empire that was a computer player from the start,
  handed to human control, is the host's to play ("[Host]").

OpenSE4's own choices, (inferred):
- Local games keep no master password; a network or e-mail host's game file opened here
  keeps its host's (`SaveInfo::masterPasswordVerifier`). A network or e-mail player's copy
  holds none (an e-mail player gets a turn file, not the host's game file), and the
  in-game host's Players window works on its own player's copy like any player's; the host
  hands empires over with the toggle.
- A human empire that never had a stored difficulty gets Medium when it is handed to the
  computer (ours stores none until it is needed).
- Random computer players and rebels keep no password in ours (the original gives them the
  master password), and `liveControl` still plays a computer empire with
  `aiMinimalChanges` set normally (the quirk of spec 05 §7.1 is not copied).

### 1.3 Order dialogs and pickers (opened by order buttons in the command panel)

| Screen | Purpose | Reached from |
|---|---|---|
| Select Waypoint | Choose a waypoint as a destination. | Move To Waypoint; Set Construction Queue → Set Move To. |
| Fleet Transfer | Create fleets, move ships in and out (left-click moves an item between the two lists); Create Fleet, Formation, Strategy, Existing Fleets, Add All, Remove All. The vehicle list holds only vehicles in no fleet that may join one, cloaked ones too; the fleet list the fleets whose location is the sector, with their members there. Create Fleet asks for a name (OK, Cancel; "Alpha Fleet" and on) and makes an empty fleet; one still empty when the window closes is deleted. Rules in spec 03 §9 (confirmed: binary). Disband Fleet and Rename Fleet are OpenSE4's own. | Order F; Combat Simulator. |
| Cargo Transfer | Two cargo lists (from / to) with transfer step Move One / Five / Ten / Hundred / All (titled "Transfer Cargo"; no order buttons, spec 04 §17 for the simulator's use). Every holder with cargo space is listed, each with its cargo lines, a colony's population one line per race; holders with no cargo space are left out. The empire's options keep the step, Ten at first (spec 08 §3.6.7); loading from a planet quarantined by plague is refused in a message box. The manual says it is not available in simultaneous games, but the executable lights the button and opens the window in both modes (confirmed: binary). | Order T; Combat Simulator. |
| Launch \ Recover Units | Two lists. Left, the carriers of the sector: the own colonies (also with nothing aboard) and the own ships and bases that can take part, each with "Can Launch:" and a line each for fighters, satellites, mines and drones (what it may still launch this turn against its rate), then the units it carries. Right, the own unit groups in space there. A click on a unit launches it from that carrier; a click on a group recovers it into the carrier selected on the left; each acts at once on exactly that holder, giving and running no order and logging nothing (spec 03 §12). Move One / Move Five / Move Ten / Move All, kept with the empire's options, Ten at first (spec 08 §3.6.7). A launch or recovery that cannot fully happen says why in a message box titled "Launch Warning" or "Recover Warning". Not available in simultaneous games. In a battle the same window opens in battle mode (§1.10.2, spec 04 §10.4) (confirmed: binary). | Order U; tactical combat orders. |
| Location pick, then cargo-kind / unit-kind picker | Load, Drop, Launch Units Remotely and Recover Units Remotely: the sector is picked first (and for Drop its destination, §2.9); at the click a picker offers the kinds, the same whatever the selection carries: Population, Troops, Fighters, Mines, Satellites, Drones, Weapon Platforms for Load and Drop; Fighters, Satellites, Mines, Drones for Launch; Fighters, Satellites, Drones for Recover. A colony's Launch and Recover ask only the kind: the order is given at once at the colony (spec 03 §8, confirmed: binary). | Orders L, D, I, O. |
| Stellar Manipulation | Location view plus one button per effect (create/destroy planet, star, storm, nebula, black hole; open/close warp point; Construct for ringworld/sphereworld). How it gives its orders: below the table (confirmed: binary). | Order B. |
| Scrap | Selected Vehicles list (own vehicles at the location in no fleet and not cloaked; on a colony too, the order lists the vehicles in its sector), opened with nothing selected; each row the vehicle's name, "<design> Class" and its status icons; Order By Name (the default: name with letter case ignored, then design) and Order By Class, each refilling the list and dropping the selection; Select All and Select None. Statistics: Scrap Value and Cost to Unmothball (the selection's sums, 0 unless that button is lit), Research Potential (None, Minor, Moderate, Sizable or Major, of the last selected vehicle; "None" when any selected one cannot be analyzed), Status (the last selected vehicle's word), Can Self-Destruct and Can Be Fired On (Yes only when every selected vehicle can), Space Yard In Sector, and two fixed note lines; Scrap, Analyze, Retrofit, Mothball, Unmothball, Self-destruct, Fire On, each lit only when every selected vehicle qualifies. Retrofit opens Select New Design (spec 03 §14). Rules and turn styles in spec 03 §15 (confirmed: binary). Our client follows this since 2026-10-13; its Facilities tab, shown with an own colony in the sector, is OpenSE4's own (opened with `kFacilitiesTab`, for Scrap Facilities), and so are its status line and the reason a picker row gives when hovered. | Order G. |
| View Orders | Pending orders of the selected object, read-only; with Repeat on the list ends with a "(Repeat Orders)" row (confirmed: binary). Ours adds Move Up, Move Down, Delete Order, Clear Orders and the Repeat Orders box, and shows the current order first. | Order V. |
| Select Facilities | Pick facilities to scrap on a planet. | Scrap Facilities order. |
| Select Component | Pick a usable component. | Use Component order. |
| Select Facility | Pick a usable facility. | Use Facility order. |
| Convert Resources | Choose source/target resource and amount. | Convert Resources order (Ctrl+V). |
| Change Name | Text prompt "Enter New Name" holding the current name (the fleet's for a fleet). A name exactly equal, case counting, to the name of any space object of the game (a ship, base or unit group, a planet, star, warp point or other stellar object, of any empire) is refused with an "Invalid Name" box saying the name is in use, and the prompt opens again until the name is free or the player cancels; fleets' names are not compared (confirmed: binary). The original also refuses OK on the unchanged current name, as in use. Our client differs: that is taken as no change. Since 2026-10-14 the engine refuses such names for every player (`cmd::Rename`, `spaceObjectNameInUse`) and our prompt shows the box. | Order N. |
| Formation \ Strategy picker | For a selected fleet: two pickers in turn, the formation, then the strategy (confirmed: binary). Ours sets the formation as it is picked, and Cancel on the strategy picker keeps it (inferred). | Order H. |
| Abandon Planet confirmation | Two questions: abandon the planet, then (with facilities) whether to scrap them; the answer becomes the colony's Abandon order (spec 02 §5, confirmed: binary). | Ctrl+A. |
| Pick Object | Choose one planet, warp point, own object or target when the sector clicked holds several (§2.9), or the object a stellar manipulation acts on when the vehicle's sector holds several (below) (confirmed: binary). | Colonize, Warp, Drop Cargo, a pursuing Attack, Stellar Manipulation. |
| Colony-type picker | Shown when a colonize order completes, if the empire option is on. | Colonize. |
| Attack Sector prompt | Yes/No when a move would enter a sector holding enemies [S]. | Move orders. |
| Reorder | Generic list ordering: Move Up, Move Down, Move To Top, Move To Bottom, OK, Cancel. | Research, Intelligence, queues. |

**Stellar Manipulation** (confirmed: binary unless marked). The window shows the selected
vehicle's sector and a button for each of the 13 manipulations.

- A button is lit when the vehicle itself has the matching ability (a working part that
  gives it); Create Planet, Destroy Planet, Open Warp Point and Construct also need the
  ability's value above 0. Nothing else is tested: the order's own checks (spec 01 §9) are
  made when it is carried out.
- Construct first asks for the world's size: the PlanetSize records whose `Special Ability
  ID` **equals** the vehicle's best `Create Constructed Planet` value (the largest Val 1 over
  all its abilities), and the order stores the chosen record's position in PlanetSize.txt,
  counted from 1 (confirmed: binary; corrected 2026-10-14, complete listing: not every world
  up to the value). Our list offers them in PlanetSize.txt order and always shows the picker
  (inferred, §7 Q114).
- With Empire Options' "confirm stellar manipulation" on, a "Confirm Action" Yes/No names
  the effect; for Construct it names the size, asking whether to build that world around
  this star (§7 Q46). No gives no order.
- Then the target. Create Planet, Create Star, Create Storm, Destroy Nebulae and Destroy
  Black Hole name none: they act where the vehicle is. Open Warp Point opens the Galaxy Map
  in destination mode (§2.6) and takes the system clicked there. The others take an object
  of the vehicle's sector: a star for Destroy Star, Create Nebulae, Create Black Hole and
  Construct, a warp point for Close Warp Point, a storm for Destroy Storm, a planet for
  Destroy Planet. One such object is the target at once; with several the Pick Object
  window asks which; with none a "Cannot Perform Action" box says the sector holds no such
  object, and no order is given. The candidates are every object of that kind in the
  sector, in the system's object order, whether or not the player sees it, as for Colonize
  (§2.9) (confirmed: binary, 2026-10-14; §7 Q114).
- The order is appended to the vehicle's **own** order list, never given to its fleet: the
  other members' lists stay as they are (a fleet member's own list can then differ from
  theirs; spec 03 §8 says how a fleet acts on such lists).
- In a simultaneous game the window then closes. In a turn-based game the vehicle's list is
  carried out at once, the effect's picture plays and the window shows the result.

Our client follows this since 2026-10-13 (`screens/stellar.cpp`; the order is
`cmd::SetOrders` with `ownList`, the size `Order::planetSize`). Its own additions stay: the
film strip of the action under the pointer, and the line that says, for the action under
the pointer, what the order's checks would say if it were carried out now.

### 1.4 Report popups

All are about 310x420 with tab buttons along the bottom [S]; in the main window the same
reports render inside the right-hand panel (§2.5).

| Report | Tabs / content |
|---|---|
| Ship Report | Detail (owner flag, 128 px portrait with status icons under it, name, class, size, movement, damage, supplies, crew experience, fleet, maintenance), Comps, Cargo, Ability. Less detail for foreign ships. Movement is the movement left and the maximum as "3/6", with a small gauge beside it when the maximum is above 0 (confirmed: binary, spec 03 §19 Q85; ours since 2026-10-14, without the gauge). Damage counts destroyed components ("Damage on the Ship Report" below). |
| Planet Report | Detail (owner flag, picture, status icons, physical data, value, description, then colony data: type, population, mood, output, construction), Facil, Cargo, Ability. An "Empty" variant without tabs for planets that are not ours. |
| Race Report | Detail, Descr (free-text blocks), Race (characteristics, each with its level word as on Empire Setup's Characteristics page (spec 02 §9, confirmed: binary), traits), Tech (known tech). Detail (confirmed: binary): the race's 128×128 portrait at (0,0) with no flag; the empire's name in the Button face at y 4 from x 120, further left when long; for another empire, labels in #7D9FFF at x 130, Treaty (y 20), Trade (y 50) and Mood (y 80), with white values at x 140 15 px under each: our treaty with it by name in the treaty colours ("Unknown" with no contact; red for war, non-intercourse and no contact, yellow for none, non-aggression, subjugation and protectorate, green from trade alliance up), the trade percentage with "%" as in the Empires window, its mood word toward us; a conquered empire shows Treaty "Conquered" in white and "None" for Trade and Mood. For every empire, labels in #7D9FFF at x 4 with white values at x 140 on the same line: Age (y 134, the race-age word), Culture (148), Demeanor (162), Home Atmosphere (190), Home Planet Type (204), Race Directory (232, the race's art folder with a capital first letter); for our own, Experience Points (260); our own has no Treaty, Trade or Mood. The Tech page is headed "Known Technology" for another empire and "Technology used on our Ships" for our own, listing without repeats and sorted the technology areas of the components of that empire's designs the viewer has seen (all of ours for our own; medium confidence on the exact list). It opens from Empires, Communicate, population right-clicks and an object report's flag (§1.4 "Right-clicks"). Our client draws it since 2026-10-04 like every report opened on its own (§1.10.1, §7 Q87): borderless, 310x420, the pages at (10,10), the four tabs at (10,340), a 153x30 Close at (79,380); the other pages scroll with the lists' arrow column. It is modal over the window that opened it (Empires, Communicate, the main window, the Combat Piece Report) and closes with it; over a report popup it opens as a popup of its own. Esc closes it (inferred, as the Combat Piece Report). Since 2026-10-14 its Detail page follows the original's (§7 Q120), "Conquered" meaning an empire no longer alive (inferred); our Tech page still lists the empire's known areas with their levels, for our own empire and partners only. |
| Fleet, Storm reports | Fleet: movement, supply pool, experience, formation, strategy, ships. Storm: picture, size, description, abilities. |
| System Report | Shown when empty space in the system panel is clicked. The 128x128 picture `Pictures/Systems/<Background Bitmap>` (the system type's background name, folder root; normal systems use `Starmap.bmp`) at the top left; "<Name> System" right-aligned at the top (left edge at most x 120, y 4); the type's description in grey (160,160,160) from y 140, in a box 42 px tall; the system's abilities from y 197 (confirmed: binary). Our client follows these places since 2026-10-04 and adds the type and the location as label lines beside the picture; the picture, opaque where a planet's portrait is keyed, sits at (0,0) just inside the panel's frame rail and is clipped there (v0.9.0 drew it 3 px higher, over the rail: GitHub issue #2); it wraps the description and every ability (a black hole's are long), and when they do not fit the panel (a long text, a larger Text size) the description and the abilities scroll together with the lists' arrow column from y 140 down (inferred: what the original does with text longer than its boxes is not described). |
| Component / Facility / Ship Size / Formation reports | Cost with resource icons, size, damage resistance, vehicle types, weapon data including damage by range, abilities; formation diagram. The Component Report (confirmed: binary, 2026-10-14): beside the picture the component's description, then labelled lines in this order: Cost, Size, Damage Resistance, Supplies Used, Vehicle Types; Mount Used, when a mount applies (when exactly is medium confidence); for a weapon Weapon Type (with "(Speed N)" for a seeker), Can Target, Fire Rate, Damage Type, Seeker Dmg Res and Damage At Range; then Abilities. Ours follows since 2026-10-14 (`componentDetail` in `screens/item_reports.cpp`; the name above the description, Seeker Dmg Res for seekers only, Fire Rate as the reload number, the damage as a table: ours, inferred); before, it had its own labels and layout (Group, Mount, Structure, Supply Use, Vehicles, Limit, Requires, a Weapon section with Type, Targets, Damage, Reload and To Hit) and the description last. OpenSE4's option "More in item reports" (Options, off by default) adds Group, Limit, Requires and a mount's To Hit after the original's lines. |

**Right-clicks inside a report's pages** (confirmed: binary; observed, spec 07 session 6).
They work wherever the report appears: the main window's report panel, a report popup and
the Combat Piece Report. Left-clicks on these pages do nothing.

- **Facil** (planet) is a grid of 36×36 cells, one per facility, so a type the colony holds
  twice fills two cells, with "Facilities" and the count of facilities against the
  planet's room under it and page arrows when there are more cells than fit. A right-click
  on a facility opens that facility's report: the small item report that a right-click on
  a facility in Set Construction Queue's buildable list also opens (picture, name,
  description, cost, and the facility's abilities as written in its data, such as a space
  yard's construction rates). It is modal and closes on any click on it or on Esc.
- **Comps** (ship or base) is the same kind of grid, with "Damaged Components" under it. A
  right-click on a component opens its Component Report, with the weapon mount the design
  gave it.
- **Cargo**: a right-click on units opens their design's report, on population the race's
  report.
- **Detail** (a Planet Report of a colony): a right-click at x 230–290, y 196–236 of the
  page, where an own colony's race pictures are drawn (from (230,196), 20 px apart, three
  to a row, the next row at y 216), opens **Races On Planet**, a list of the races living
  there, one row each with the race's picture, its name and its population, and a Close
  button. It is read-only (spec 03 §11). Only a colony is checked for, not whose it is: it
  opens for another empire's colony too, though that page draws nothing there.
- **Detail** (the owner's flag, §7 Q120): on the Ship Report (ships and bases), the Planet
  Report and the unit-group reports (fighter, satellite and drone groups, mine fields), a
  right-click on the owner's flag, x 0–26 and y 0–18 of the page with both ends included,
  opens that empire's **Race Report**, modal over the window holding the report (the main
  window or the report popup), on Detail. It needs an owner (a planet's is its colony's, so
  an uncolonised planet does nothing), the object seen by the viewer (a cloaked colony shows
  no flag) and a report not opened from the Combat Simulator, whose corner shows the side's
  box; no treaty or contact is checked, so it opens for our own empire, any empire met, one
  never contacted and a neutral one. A left-click does nothing; on a Planet Report only the
  owner's flag at the top answers, not the invader's under it (apart from its top pixel
  row). Storm, star, warp point and system reports and the Fleet Report have no flag. A
  quirk: the unit-group report popups test the window's corner, x 0–26, y 0–18, although
  their picture with its flag is drawn at (10,10), so only the flag's top-left part answers
  there. Elsewhere the Race Report opens from the Empires window (a portrait's right-click,
  Our Race), Communicate's Report, a right-click on population on a Cargo page or in
  Jettison Cargo, and the empire lists of the diplomacy pickers; in the main window only the
  report's flag opens it (the system panel's owner flags select the sector, the status
  bar's flag opens nothing).

Since 2026-10-04 our client follows for Facil and Comps (§7 Q105), still drawn as rows,
not the 36 px grid; Cargo takes no right-click. Since 2026-10-14 the Detail page's
right-clicks open Races On Planet at the original's place, for any colony, and the Race
Report from the owner's flag (§7 Q120, GitHub issue #15). Our client differs: it takes
the flag's click on the whole flag as drawn, also in the unit-group report popups and on
the Combat Piece Report's picture (the original's quirk above is left out), and only on
Detail, where the original's other pages draw no flag (inferred). The window Jettison Cargo
takes the same right-clicks as the Cargo page on its lines (a design's report, a race's
report; confirmed: binary).

**Damage on the Ship Report** (confirmed: binary). The Detail page of a ship or base has
"Damage" in #7D9FFF at (130,110) and under it, in white at (140,125), "D/N" with no spaces
and no percentage: D the vehicle's destroyed components, N the components of its design,
each mounted component counted once (ten armour plates count ten). Structure points appear
nowhere on the page. At (220,125) follows a bar: a box framed in #647EC7, 13 px tall, with
3×9 px cells every 4 px from 2 px in, one per component, the first D red (255,0,0) and the
rest green (0,255,0); from 16 components on it has 15 cells, truncate(15 × D ÷ N) of them
red. The Comps page starts with "Damaged Components" in #7D9FFF at (4,4) and the same
"D/N" in white, right-aligned to x 200. The unit-group reports (fighters, satellites,
drones, mines) and the Fleet Report have no damage line: fighter groups show Number of
Fighters, Movement and Supplies, drone groups Movement and Supplies, satellite groups and
mine fields Number of Satellites or Number of Mines. The Movement, Damage and Supplies
lines are shown for the player's own ships and bases unless mothballed (then "Status" at
(130,80) with "Mothballed" under it instead), and for another empire's only while the
player's long-range scanners inspect it (spec 03 `Long Range Scanner`) and it is not
jammed; otherwise only Class and Size, and the grey note "Long range scanning being
jammed." at (4,182) when a scan is blocked by a jammer. Repair counts in the same unit: a
repair point restores one destroyed component, and the `Component Repair` that space yards
and repair bays show is components per turn (spec 03 §13). Since 2026-10-14 our client
follows (§7 Q121, GitHub issue #16) in its own layout (label lines, the bar 80 px after
the value). Our client differs: hovering the Damage line shows the structure lost against
the design's ("Structure lost: S of T (P%)"), which the original shows nowhere; our
unit-group reports keep their Units and Movement lines; the bar's frame ends 2 px after
the last cell (inferred).

**The Ability page** (confirmed: binary; observed, spec 07 session 6). A plain list of
text lines. Each line starts with the blue 13×13 lamp of `Pictures/Game/General.bmp` (the
cell at (177,0)) at (0,2) of the line; the text starts at x 13 and wraps at the page width
less 23 px. Nothing is combined, summed, sorted or de-duplicated: each ability entry gives
one line, which is the entry's `Descr` from the data file exactly as written (Val 1 and
Val 2 are never shown, and a substitution token such as the shield points one is printed
as it stands; only the component report fills it in). An empty page stays empty, with no
placeholder.

- **Planet Report**, filled only for the player's own colony (other planets show no tabs):
  1. the planet's own natural abilities, in the planet's order;
  2. the `Description` of each racial trait the owner has whose trait type concerns
     planets (Reproduction, the three resource productions, Research and Intelligence
     Production, SY Rate, No Plagues, No Spaceports, Population Happiness, Planet Storage
     Space, Planetary SY Rate, the three resource storages, Production, Trade, Ground
     Combat, Tolerance);
  3. one line per racial characteristic that is not 100 % and concerns planets, in
     characteristic order: a "Racial Trait:" prefix, the difference from 100 as a signed
     percentage, and what it changes (Physical Strength: ground combat; Intelligence:
     research; Cunning: intelligence; Environmental Resistance: tolerance; Reproduction;
     Happiness; Political Savvy: trade; the three aptitudes: that resource's production;
     Construction Aptitude: space-yard rate);
  4. one line per non-zero modifier of the owner's culture that concerns planets
     (Production, Research, Intelligence, Trade, Ground Combat, Happiness, SY Rate, in the
     culture's order Production, Research, Intelligence, Trade, Space Combat, Ground
     Combat, Happiness, Maintenance, SY Rate, Repair): a "Cultural Trait:" prefix, the
     signed percentage and the name of what it changes;
  5. the population-level lines: the first row of the population-modifier table (spec 02)
     whose amount is at least the colony's total population; if its production % is not
     100, one line with the signed difference as a production change for the population
     level L, and if its space-yard rate % is not 100, a second line the same way for the
     construction rate. L is 0 for the first row, else the previous row's amount plus 1,
     written as a population (M, or B above 9999). No matching row, no line;
  6. the mood line: if the colony's mood band's `Mood … Modifier` is not 100, one line with
     the signed difference as a production change from happiness.

  The colony's facilities are not on this page (each facility's abilities show on its own
  report, opened from Facil), nor its cargo, units or the system's abilities.
- **Ship Report** (own ships, and foreign ships whose details are shown): the hull's
  entries, then any entries the design or the vehicle carries of its own (none in the stock
  game), then the owner's racial lines for ships: trait descriptions whose type concerns
  ships (Maintenance Cost, Supply Cost, Vehicle Speed, Ship Bonus, Space Combat, Repair,
  Ship Attack, Ship Defense), "Racial Trait:" lines for Aggressiveness (attack),
  Defensiveness (defense), Repair Aptitude (repair rate) and Maintenance Aptitude
  (maintenance), and one line per non-zero culture modifier that concerns ships (Space
  Combat, Maintenance, Repair) with no prefix, just the signed percentage and what it
  changes. The components' abilities are not on this page: a stock attack ship showed its
  hull's line and the racial and culture lines and none of its engines' or weapons'
  (observed); they show on each component's report, opened from Comps. Mothballing does
  not change the page.
- In the racial and culture lines a positive value carries "+"; in the population and mood
  lines a negative value is preceded by a space instead (a quirk). Reports in the Combat
  Simulator leave the racial and culture lines out.

Since 2026-10-04 our client follows (§7 Q106), in our own words for the racial,
cultural, population-level and mood lines.

### 1.5 Diplomacy and empire comparison

| Screen | Purpose | Reached from |
|---|---|---|
| Communicate | Compose a political message: target race portrait, Message Type, Message Tone, free text; Report, View Trade/Tribute/Gift, Edit Package, Edit Trade, Start Again, Send Message, Cancel. Details below (confirmed: binary). | Empires → portrait; Log → Send Reply. |
| Select Package | Build a give/take package from tabs Systems, Planets, Resources, Technology, Ships, Units, Star Charts, Treaty, Comm Channels; Clear Package. What each tab lists: spec 05 §3.4 (confirmed: binary). | Communicate. |
| History | A list of the empires that appear in the player's history on the left, with "Other Events" last; the chosen list's lines in the file's order, oldest first, under decade headings ("2400 - 2410") (confirmed: binary). Our client follows since 2026-10-14 and keeps its galaxy map (now under the list), which shows the selected line's system and the empire's known colonies; its lists are our empire, every empire we know and every empire our record names, each starting with the empire's founding and the changes its statistics show (OpenSE4's lines, spec 05 §3.4 "OpenSE4 choices"). | Empires. |
| Treaty Grid | Empire × empire treaty abbreviations with legend; paged 10 empires at a time. The player first, then every known empire. The cell of row A and column B is filled only when we may see their treaty (spec 05 §3.2: A or B is us, or we hold Non-Aggression or better with A or with B) and A and B hold a real treaty; None and "no contact" cells are blank, and the legend has no entry for None. The codes are War, NI, NA, Sub, Pro, TA, TR, MA and P; a Subjugation or Protectorate cell carries the roles in (row, column) order, S the subordinate and D the dominant side: "Sub (D, S)". The "AI State" button is a debugging aid only (confirmed: binary). Our client follows since 2026-10-14. It keeps the large dialog's size (since 2026-10-04, GitHub issue #3): the row names take 124 px and the ten columns of a page share the rest, each headed by the empire's flag over the first three letters of its name (the whole name on hover); every code is drawn whole, a long name ends in "…" with a tooltip, and the rows scroll when they do not fit above the legend. v0.9.0 gave every column 52 px, which the dialog could not hold: the last four were squeezed to slivers. | Empires. |
| Intelligence | Intel points, project list, current projects in pages of four, Repeat, Divide Evenly, Reorder. The Research window's layout: points in the title strip, projects under silver group headings with their cost, four 140 px project boxes side by side, Repeat Projects and Divide Pts Evenly in slots 5 and 6, Reorder Projects in slot 13 (observed, spec 07 session 5). | Empires. |
| Borders | Map of claimed systems with empire filter (Select All, Allies, Enemies, Us). Allies checks us and every empire we hold Non-Aggression or better with; Enemies every empire below Non-Aggression (confirmed: binary; ours since 2026-10-14, where Allies took Military Alliance and up). | Empires. |
| Victory Conditions | Grid of conditions × empires, paged by 10. An "X" where an empire meets a condition: its score has reached the value, it has the percentage of every other living empire's score, its capped tech levels reach the share; the years, the peace and the "no victory before" qualifier are game-wide, so once met they mark every column shown. A condition that is off shows its plain title only. The columns read the Score Display option one step off: "own empire only" shows the empires we hold Non-Aggression or better with, "Non-Aggression partners" shows every empire (confirmed: binary, a quirk ours copies); we always see our own column, never a destroyed empire's, and every one once the game is over. Our client follows since 2026-10-14 and adds the Game column, each figure's progress on hover and the paging. | Empires. |
| Scores | Flags with score, resources, research, intel, tech levels, systems, planets, population, units, ships, bases, rank. A column for each empire whose score we may see (spec 05 §5), in player order. Score, Resources, Research and Intelligence are given in thousands with one decimal and "K" ("12.3K"), in millions with "M" from 1000K; the population in billions with "B", trillions with "T". The rank is an ordinal ("1st", "2nd", "3rd", otherwise the number and "th", so "21th"); ranks are dense (equal scores share a rank and the next score gets the next number) and every empire is ranked, destroyed ones included (confirmed: binary). Our client follows since 2026-10-14 in the large dialog of v0.10 (since 2026-10-04, GitHub issue #3): ten columns to a page (Empires 1-10, ...), each headed by its flag over the first letters of its name, a row per figure named in full, rank last; the other figures are given whole or shortened to fit, with the exact value on hover (inferred: the original's places are not described). | Empires. |
| Comparisons | Line graph over time of one chosen metric (same metrics as Scores) for selected empires. It plots only the rows of the player's statistics file, so an empire has a gap for the turns it was not visible, and lists every empire with a row, destroyed ones and those no longer visible included (spec 05 §5, confirmed: binary). Ours since 2026-10-14 (`Empire::statistics`); it plotted the whole history of the empires visible now, and the turn in progress. | Empires. |

**Communicate in detail** (confirmed: binary):

- Choosing a message type opens its pickers at once (the treaty, the empire, the system, the
  planet, or Select Package for a type with a package); cancelling one returns to Start
  Again. Edit Package is lit for exactly Give Gift, Offer Tribute, Want a gift and Want a
  tribute, and opens Select Package on the side the type fills: the side we give for a gift
  or tribute, the side we ask for (with "Any" items) for a request. Edit Trade is lit for
  Propose and Counter Trade. View Trade, View Gift and View Tribute are separate buttons,
  each lit when the newest message from the empire (as below) is of its kind.
- The text box is filled, when a type is chosen, with a random line of the player's own
  race's `_AI_Speech` pool "Send <type>" (by the computer player's name of the type, spec 05
  §7.5), its tokens filled in; an Accept or Refuse Demand takes "Response Friend|Enemy YES|NO
  <demand>", Friend when the player holds Non-Aggression or better with the recipient.
- Opened from a portrait, the window looks at the newest political message from that empire
  that is dated after the date − 2 (in a turn-based game, after the date − 1 when the
  sender's player number is lower than ours; corrected 2026-10-14, complete listing: this
  said "simultaneous", spec 05 §3.4 "Answers"), and adds the types that answer it: Accept,
  Refuse and Counter Treaty after a Propose or Counter Treaty; Accept, Refuse and Counter
  Trade after a trade; Accept and Refuse Gift after a gift; Accept and Refuse Tribute after a
  tribute; Accept and Refuse Demand after any demand or request. A Counter Trade starts from
  the received packages with the sides swapped (spec 05 §3.4).
- A click on a portrait does nothing once a message has gone to that empire this turn.

Our client follows since 2026-10-14: the separate View and Edit buttons, a package type
opening Select Package at once, the text from the speech pool (drawn with a random source of
the window's own, never the game's), the answers from the portrait (sent as
`cmd::AnswerMessage`, for the newest recent message only, `replyableMessage`; until the
correction above ours had the turn style of the one-turn case backwards) and the inert
portrait. Our client differs: the type, treaty, empire, system and planet are drop-down
lists in the window rather than pickers opened at once, Select Package returns with Done, a
received message opened from the Log's Send Reply shows OpenSE4's Accept, Refuse, Counter and
Reply buttons (Accept, Refuse and Counter lit only for the newest recent message from that
empire, as the portrait's), and the demands and requests are listed flat rather than behind a "Demand /
Request / Warn" entry that switches the list to its 16 types (cosmetic). The Declare War and
Surrender confirmations are OpenSE4's.

**The Empires window's columns** (confirmed: binary). The tab (Treaty, Trade or Tariff) is kept
with the empire's options and saved with the game (spec 08 §3.6.7); ours since 2026-10-14
(`InterfaceOptions::empiresTab`, save format 10), stored when the window closes. Under each
portrait, by tab:

- Treaty: the treaty in its colour, a Subjugation or Protectorate followed by the roles in
  brackets ("We Dominate Them", "They Dominate Us", "We Protect Them", "They Protect Us");
  "Trade N%", where N is the trade percentage (spec 05 §3.3: the counter capped at `Maximum
  Trade Percentage`) from Trade Alliance up and 0 below; the "Mood" of every empire, human
  players included, in the words of its anger toward us (spec 05 §7.3: below 10 Brotherly,
  20 Amiable, 30 Receptive, 40 Warm, 60 Moderate, 70 Cool, 80 Displeased, 90 Angry, else
  Murderous); and one status line, the first that applies: "Message Sent" when we wrote to
  it this turn; "Offered Trade", "Offered Gift" or "Offered Tribute" when its newest message
  (as Communicate's) is one; "Proposed Treaty" and the treaty when it proposed one other than
  the current treaty.
- Trade: "Trade Amount", what trade with that empire brings us this turn: minerals, organics,
  radioactives, research and intelligence, each with its icon.
- Tariff: "Tariff Received" with the three amounts when any of them is above 0, otherwise
  "Tariff Given" with the three amounts.

Our client follows since 2026-10-14, from the economy report's breakdown by empire
(`EconomyReport::partners`, the projection for the coming turn as the rest of that report), and
keeps OpenSE4's further lines after the original's: race, player, since, last war, the anger
value after a computer player's mood, "Inbox: N waiting"; the trade percentage of the maximum,
what is shared, shared sight and resupply; and who pays whom.

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
| Combat Replay | Same layout as tactical, playback only; Options, Next. Space = Next, Esc closes. Title strip: "Combat Replay", the location, the combat turn and the empires' flags; no log, event list or summary of any kind (confirmed: binary). As seen (observed, spec 07 session 5): no navigation buttons, no piece or target panel, an empty weapon grid of eight rows (216×289 at (W − 232,226)), Options and Next side by side at (W − 232,534) and (W − 119,534); Next dims after the last turn. OpenSE4 follows this layout and can list the turn's events in words and the battle's summary in the empty weapon grid's place, an option of Combat Replay Options that is off by default (an extension, Q39). | Log → Combat Replay. |

### 1.7 Multiplayer, tutorial and end of game

| Screen | Purpose |
|---|---|
| TCP/IP Host | Player list, own IP addresses, a status line naming the current step of the host cycle (spec 05), hideable chat; Begin Game, Process Turn, Add/Remove Empire, Play Turn, Chat, Minimize Game, Quit Game [T]. |
| TCP/IP Player | Source and host IP, player name, status line, chat; Connect to Host, Create Empire, Play Turn, Chat, Minimize, Quit [T]. |
| Movement log replay | Not a window: in simultaneous games the main window replays the host's 30-day movement (full, stepped by day, per ship, rewind) [T]. Exact behaviour: §7 Q51 (confirmed: binary). |
| Tutorial / Scenario text window | A page of the scenario's text: title, a 128x128 picture, Previous and Next through its series; re-opened with Ctrl+H, and in the tutorial with the "T" buttons. Layout and rules in §5.6 (confirmed: binary). |
| Finale | Full picture for victory, defeat, or "human players all dead", chosen from lists in Settings.txt [M]. Ours: §1.9 "Other Settings.txt keys". |
| Game Completed | An OK box, titled "Game Completed", saying the game is over: at the first wrap of the game turn after a turn-based game's last turn (spec 05 §6), before anything of the next game turn runs (confirmed: binary). A simultaneous game has none: it goes on, the Victory ending shown at every turn (spec 05 §6). |
| Progress | A small modal window, a caption centred over a dark blue gauge from 0 to 100 %: "processing the turn" while a simultaneous game's turn is processed at the host (not shown when the host runs without a window; the work counted in the number of empires × 4 + 30 steps), and "creating the galaxy" and "placing the players" while a new game starts (confirmed: binary). |

**Endings that end the game** (confirmed: binary). Each of these ends the turn loop and exits
the program: No to "Do you want to continue playing?" after the conquest of the galaxy; the
Human Dead ending (§1.2.1 "No human left"); Game Completed; the fourth wrong password in the
Next Player window (§1.2.1); and Yes to "shut down now?" after a turn of a turn-based game on
different machines (No goes back to the intro). Our client differs: it returns to the intro
instead, at each of them that it has (since 2026-10-14; the different-machines question is not
OpenSE4's: its network and e-mail games hand turns over otherwise).

**Progress and Game Completed in OpenSE4** (since 2026-10-14). A local or hotseat simultaneous
game's turn is processed on a thread of its own behind the Progress window ("Processing the
turn"), the windows meanwhile showing the game as it was (`ClassicSession::setBackground`,
`ClassicMode::drawProgress`); the gauge counts the engine's steps (`TurnOptions::progress`: each
empire's orders, messages, start of turn and end-of-turn processing, and the 30 movement days,
inferred); a battle the Settings show restarts it from 0. A script's run processes the turn at
once, without the window. Our client differs: the network host's processing and the start of a
new game show no Progress window (the setup screens start the game at once). Game Completed is
`ClassicMode::drawGameCompleted`, after the last turn's moves and battles are shown; its OK
returns to the intro.

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
| General | Size (99; the hull's name): hull number as above. Type (120; the design type): A to Z. Move (40; "left/max"): movement left, highest first. Dmg (47; "destroyed/total" components, each mounted component counted once, as on the Ship Report; blank for unit groups, confirmed: binary): destroyed components, highest first. Supplies (rest): **lowest first**, unlimited supply counting as 60000 |
| Orders | Class (120; the design name): A to Z. Orders (rest; one order per 12 px line): A to Z by the list written as one line ("None", "REPEAT ORDERS", or the orders joined by ", ", the current one in brackets when Repeat is on) |
| Cargo | Space (65) and Max (65): highest first. Cargo List (rest): A to Z by the cargo in words ("N People, …", or "None") |
| Fleet | Experience (120): **lowest first**. Fleet (rest; the fleet's name or "None"): by fleet number, highest first, vehicles in no fleet counting 0 |
| Maintenance | Three cost columns (70, 70, rest) in the resource colours: maintenance, highest first |

Fleet rows, shown with Show Fleets, always come after every ship and unit row, in the
empire's fleet order, and are never sorted.

**What the cells show** (confirmed: binary). A fleet's row: Size is the number of its
members at the fleet's location, Type its formation's name; Dmg, the cargo columns, Class
and Fleet are blank. A unit group's Type is up to three lines "design - count", one for each
design in it. The Cargo List has one line per cargo entry. The Orders column writes the list
in its own order, and its sort text brackets the current order only while Repeat is on.
Total Maintenance at the top is the sum over the vehicles as they are now, not the last
economy update's figure. A left-click on a fleet's row selects its first member at the
fleet's location. Our client follows since 2026-10-14; a fleet row's Move and Supplies are
ours (over its members at the location), and its Orders cell lists the fleet's orders. The
order cells write a repeating list from its first order, as the original keeps it
(`game::originalOrderList`).

**Set Construction Queue** (confirmed: binary). The tabs: Ships only while the queue's
owner has a working space yard (a colony without one, or a cloaked colony, has it dim);
Facilities on planets only; Units always; Upgrades always, an empty list for a ship. The
tab shown is stored with the empire's options when the window closes, and the window
opens on it, or on the first tab that is lit (spec 08 §3.6.7). The available list is
grouped: ships and units under their design type, facilities and upgrades under their
`Facility Group`; the groups A to Z ignoring case, each under a header row in grey #C0C0C0
that a click does nothing on; the items indented 10 px, in the game's design order or
Facility.txt order. A design we cannot build yet (§6.5 of spec 02) is drawn in grey
#A0A0A0, and an upgrade already queued stays listed and is refused when clicked. A click on
a unit design opens the "Amount" list (spec 02 §6.5); any other click adds one. A click on a
design drawn grey, on the Ships or the Units tab, is ignored, here and in Multi-Add: no
"Amount" list opens, no "Warning" appears, and nothing is added (confirmed: binary;
corrected 2026-10-14, complete listing). Since 2026-10-14 our client follows (`queues.cpp`
`add`); before, it opened the Amount list for a unit design and then warned, and warned at
once for a ship design. Adding a
facility by hand first shows the "Note" of §7 Q29 when it applies; a refusal then comes as
an OK box titled "Warning" with the reason, one box per refused item (Fill Queue gives one
for each). Since 2026-10-13 our client follows; ours names the group of designs without a
type "No Type", keeps a status line for what was added, and says in the detail pane why an
item cannot be built.

A right-click on a queued ship, base or unit opens the
design's report: the Design Report window, the same one a right-click on a design in the
buildable list opens (name, size, design type, date created, "(Obsolete)", cost,
maintenance cost, movement, shields, cargo space, supply capacity and the components). A
right-click on a queued facility or upgrade opens that facility's report.

**The queue's owner box** (confirmed: binary; observed, spec 07 session 6). Above the queue
list, a 263×60 box at (311,56) of the window describes the queue's owner: its 36×36
picture at the left, its name from x 40 (moved left when too long), and under the name, in
label blue, "Cargo Space" at y 16, "Facilities" at y 30 (colonies only) and "Rate" at y 44,
their values in white from x 160 (the rate as the three resource amounts). A right-click
anywhere in the box opens the owner's report as a popup over the window, with a Close
button: the Planet Report for a colony, the Ship Report for a ship or base, the same report
a right-click on the owner's row in Construction Queues opens, on its Detail tab (§2.5),
which shows the planet's type, conditions and resource values. A left-click on the box
does nothing. In the Multi-Add window the box lists the tagged queues and has no single
owner; the original does not guard a right-click there (it reads an owner that is not
there), so OpenSE4 should ignore it. Since 2026-10-04 our client follows (§7 Q104); the
box's contents are still ours.

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
  OpenSE4 writes six such numbers, twelve digits (an OpenSE4 choice): its password
  verifiers can be guessed offline, and six digits would fall to that at once
  (docs/MULTIPLAYER.md, "Security").
- **When it takes effect.** Not at once: the new passwords are written into the empires when
  the host next processes a turn, right after every orders file has been read and the host has
  chosen to go on. An orders file carries the player's own password, so applying the reset
  after reading the files is what lets it win. The pending resets are not saved: if the host
  quits, or abandons the processing, before then, they are lost.
- **Who is told:** only the host, by that message. There is no log entry and nothing goes to
  the player, so the host passes the password on.
- OpenSE4 follows this since 2026-10-01. The in-game TCP/IP host's Options window shows the
  button for a simultaneous game (`OptionsScreen` in
  `src/client/classic/screens/settings_screen.cpp`); `HostSession::resetPasswords` draws
  each password with `net::resetPassword` (not the game's random numbers), discards the
  earlier choice, keeps them in memory only and writes their verifiers into the empires
  right after the next turn is processed from the orders. The e-mail host takes them with
  `opense4-server pbem process --reset-passwords=N,M` and prints them. A headless server has
  no window: a player who gave the master password stands in for its host (the same button,
  an admin request, the passwords coming back to that player only) (inferred).

**Music and Settings.txt** (confirmed: binary). The Settings.txt key that turns music off is
`Allow CD Music`. With `FALSE` no track ever plays (§5.5). Opening this window with music off
or not allowed lights "Music Off" and sets the computer's stored music volume to 0 at once; on
Close, music is stored as on only if the player picked a volume lamp, so opening and closing
the window while Settings.txt forbids music stores "music off" on that computer. The Combat
Options "Music On" lamp (§1.10.3) is lit only when music is on and allowed. Our client
follows this since 2026-10-01: playback honours the key (`readPlaylists` in
`src/client/audio_playlist.cpp`), opening the Options window stores music off at once when it
is off or not allowed (`openMusicRows` in `src/client/classic/settings.cpp`, called by
`OptionsScreen`), music is stored on only while a volume lamp is lit, and the Combat Options
lamp shows on only when music is on and allowed (`musicLampLit`, `tactical.cpp`).

Other Settings.txt keys the client must honour (confirmed: binary):
- `Allow Export of Weapon And Component Data`: when TRUE the Weapons Report gets an Export
  button, which writes four plain-text tables (weapons, components, weapon families,
  component families) to the SaveGame folder, each followed by a message titled "Export
  Successful" naming the path. Each file is written on its own: one that cannot be written
  gives a message saying it could not be exported, and the others are still written
  (confirmed: binary, 2026-10-14; ours since then, `writeExportTables`, with our own words and
  the title "Export Failed"; before, ours stopped at the first failure). The original's files are `Weapons.txt`, `Comps.txt`,
  `WeaponFamilies.txt` and `CompFamilies.txt`, every component of the data set in fixed-width
  columns described in §7 Q83 (confirmed: binary). Ours writes the same four files with those
  columns since 2026-10-03 (`help.cpp`, `src/client/classic/data_export.cpp`), into the
  client's saves folder.
- `System Ship Movement Delay Milliseconds`: when the system window animates ship movement and
  the value is above 0, the game pauses after each animated one-square step (stock value 0);
  the original reads the value as seconds, not milliseconds (§2.4 "Moves as they are made").
  The movement log replay never pauses (§7 Q62). Ours does the same since 2026-10-01: a ship
  that turned and slid to its new square waits there, the value read as seconds, once for each
  step its move stands for (`ShipGlides::stepPause`, `ship_glides.cpp`); the replay has no
  pause (`movement_replay.cpp`). OpenSE4's movement speed (below) does not shorten the pause.
- `Num Finale Lose Pictures`, `Num Finale Human Dead Pictures`, `Num Finale Victory Pictures`
  and `Finale <Kind> Picture N`: the ending window's pictures, from `Pictures/Game/Finale/`.
  All humans eliminated uses Human Dead; "your empire was destroyed" uses Lose; galaxy
  conquered and victory conditions met use Victory. One picture is drawn uniformly from the
  list; the original uses the game's random numbers, OpenSE4 must use a separate source. The
  original shows each ending as it comes, at a turn's start: a lone human who loses
  everything sees Lose at the start of a last turn, then Human Dead at the next End Turn (§7
  Q83, confirmed: binary). Ours does the same since 2026-10-03 (`screens/finale_screen.cpp`,
  `finale.hpp`): one picture of the kind's list drawn with a source of its own, a few words of
  ours, Scores and Close, the endings due at a turn's start one after another in the order of
  §7 Q83; the engine marks a human-controlled empire dead only at the end of the last turn
  that began with it defeated (`score::checkDestruction`) in a simultaneous game, and at the
  end of that turn's start in a turn-based game, which it then plays marked dead (§7 Q99,
  since 2026-10-14). The Lose ending of a turn-based game reads the engine's mark from the
  moment the turn came up (`PlayerTurn::loseEnding`); a turn-based game's last turn shows the
  Victory ending to every human at its own turn, saying it is the last turn and pointing to
  the Scores window, where every score shows (`score::scoreVisible`, confirmed: binary); a
  simultaneous game marked completed shows it at every turn a player opens, as the original
  does (spec 05 §6, since 2026-10-14).
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
binary). Our client does the same (`showMovementLines` in `src/client/classic/settings.hpp`,
`newGameStarted`). So a player who only loads games, an imported game of the original included,
sees no movement lines on a fresh install until switching them on (Ctrl+L, or this window),
while the original on the same player's computer may have had them on for years.

**OpenSE4's movement speed** (an OpenSE4 addition; the original has no such row). Since
2026-10-06 our Options window has, under an "OpenSE4" heading after System Display, a "Ship
movement speed" slider: the waits of the system window's movement animation (§2.4 "Moves as
they are made") divided by 1/8, 1/4, 1/2, 1, 2, 4 or 8, kept with this computer's settings
(`systemMovementSpeed`, `system_movement_speed` in `classic_settings.toml`; a value between the
steps is read as the nearest). The default, 1x and marked "(the original's)", plays the
original's waits as written. The movement log replay (§7 Q51) takes the same speed; the Settings
pause after each step does not change with it (`OptionsScreen`, `movementSpeedRow` in
`screens/settings_screen.cpp`; `movement_pace.hpp`).

**OpenSE4's ship movement** (an OpenSE4 addition, since 2026-10-14; §7 Q112). Under the speed
slider two lamps, pick one: "Ship movement: one group at a time (the original's)", lit by
default, and "Ship movement: all at once", kept with this computer's settings
(`shipsMoveAllAtOnce`, `ship_movement = "one-group-at-a-time"` or `"all-at-once"` in
`classic_settings.toml`; anything else reads as the first). One group at a time shows each
group's steps when the group before has arrived, square by square, the view following the
player's own groups, and the movement log replay's entries one after another, as the original
does (§2.4 "Moves as they are made", §7 Q62). All at once starts every group's steps together,
each group still square by square, the view going at once to where the last of the player's
groups ran, and starts a replayed day's entries together, each vehicle's in turn
(`shipMovementRows` in `screens/settings_screen.cpp`; `ship_glides.hpp`, `movement_replay.hpp`).
Under them a lamp of its own, "Also animate other empires' and simultaneous turns' moves",
off by default (`animateOthersMoves`, `animate_other_moves` in `classic_settings.toml`): the
original animates only the moves made for the human player whose turn it is in a turn-based
game (§2.4 "Moves as they are made"), and off those ships are simply drawn on their new
squares; on, the other empires' moves the player sees and a simultaneous turn's moves at the
next turn's start turn and slide too, in the order they were made (OpenSE4's own, since
2026-10-14; before, they were always animated).

**OpenSE4's notes on the tactical map** (an OpenSE4 addition, an option since 2026-10-14).
Under the ship movement lamps, "Say on the map why a click fired nothing", off by default
and kept with this computer's settings (`tacticalShotNotes`, `tactical_shot_notes` in
`classic_settings.toml`): with it on, a click on an enemy in the Tactical Combat window that
fires no weapon writes why at the map's top left (out of range, reloading, no supplies, no
weapon ticked). The original shows nothing then (§1.10.1 "Mouse"). Combat Options holds the
same switch under its own "OpenSE4" heading (§1.10.3).

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
| Politics | automatically claim every system we colonize [on] (a Colonize that founds a colony claims the planet's system for the empire, §7 Q47) |

### 1.10 Combat windows in detail

All of this subsection is (confirmed: binary). W×H is the tactical window's size.

#### 1.10.1 Tactical Combat

- **Layout.** Map at (10,38), (W−256)×(H−44), 36 px squares. Overview map 218×190 at
  (W−230, H−196); clicking it centres the view (3 px per square). Four 113×30 buttons
  in two rows, 70 px above the overview: Options and Orders, then Auto and the fourth
  button. Current-piece panel 216×64 at (W−232, 36) with the weapon list under it. The
  target-piece panel only when the window is at least 1024 wide.
- **Begin.** The window opens with the fourth button labelled Begin and Orders disabled;
  Auto is live already, only Orders is dim. Before Begin a left-click on any piece, an
  enemy's too, puts it in the current-piece panel and the weapon grid; a click on an
  empty square does nothing; E presses the fourth button whatever it says, so it begins
  the battle. The first press starts the battle, relabels it End Turn and enables Orders.
  When the battle ends, a "Combat Complete" message appears and the window closes by
  itself. After Resolve Combat is confirmed all four buttons dim, Options included, and
  Auto shows pressed.
- **Mouse.** Left-click on an empty square moves the selected piece (a leader's members
  move to their formation places); on another empire's piece, fires at it; on one of our
  pieces, selects it. A click on a piece that is not hostile (an ally, our own drone group)
  while one of ours is selected also tries to fire, and is refused in a "Cannot Fire
  Weapons" message box saying that allies cannot be fired on. That is the only refusal of
  a click that is told: a click on an enemy that fires nothing (out of range, reloading,
  no supplies, no weapon ticked) shows no message of any kind, and the ticked weapons that
  did not fire lose their target. A click on a square whose piece is a seeker, ours or an
  enemy's, counts as a click on an empty square: the selected piece moves there. A seeker
  is never fired at or selected by a click; only point-defense engages it, on its own. A
  left-click on a neutral obstacle does nothing at all (confirmed: binary; corrected
  2026-10-14, complete listing; spec 04 §19.6 Q96). Since 2026-10-14 ours follows
  (`TacticalCombatScreen::click`); it used to fire at enemy seekers, refuse obstacles and
  our own seekers in the box, and write the other reasons on the map. Writing them on the
  map is now an OpenSE4 option, off by default (Options and Combat Options, under OpenSE4:
  "Say on the map why a click fired nothing"). Clicking the current-piece header opens
  that piece's report.
- **Weapon ticks.** The ticks of the weapon grid are the window's, not the piece's. Every
  time a piece becomes selected (a click, Space and the Ctrl keys, the start of each phase
  played by hand) and after every move of the piece, they go to every intact weapon that is
  not reloading. A click on an enemy fires the ticked weapons; those that fired are
  unticked. Shift+A ticks only the intact weapons that are ready, Shift+C unticks all
  (spec 04 §6). A click on a weapon cell toggles its tick with no test at all, so a
  reloading or destroyed weapon can be ticked too; firing skips it (spec 04 §19.6 Q91).
- **Next and previous piece** (Space, Ctrl+N and Ctrl+B for movement, Ctrl+F and Ctrl+D
  for firing, and the title strip's selectors). The search makes up to three passes around
  the side's pieces, seekers and drones skipped, each starting after the current piece:
  first the group leaders (fleet leaders included) that can move (or fire), then any piece
  that can, and for movement only the first planet of the side found walking from the
  piece after the current one in the search direction (not the first planet in piece
  order; corrected 2026-10-14, complete listing). A piece can move when it has movement
  left, and fire when it has an intact weapon that is ready and, where it needs them,
  supplies. "Next" walks the list backwards, "previous" forwards. At the start of each
  phase played by hand the selection is made the same way walking forward from the first
  piece, so the first group leader with movement left is selected. Since 2026-10-14 ours
  follows (`nextTacticalPiece`).
- **The current piece follows the battle** (confirmed: binary; added 2026-10-14, complete
  listing). Every time a piece other than a seeker starts a move, it becomes the current
  piece, with its panel and weapon list, and the view follows it when Center Map on
  Current Ship is on; every time a piece fires, the firer becomes the current piece and
  its target goes into the target panel. This happens in computer phases, in the moves of
  drones and in the player's own phases alike, so after a group moves by hand its last
  member is the current piece. Since 2026-10-14 ours follows as its playback passes each
  move and shot (`TacticalCombatScreen::passEvents`).
- **Target panel.** It shows a piece only while the pointer is over a piece of another
  empire (allies too) with one of ours selected; moving onto an empty square or a neutral
  piece, or arming Ram or Capture, empties it, and it is empty before Begin. The window
  never picks a target by itself. A seeker's impact puts the piece it struck in the panel
  (and, with Center Map on Current Ship, in view as for a newly selected piece), and so
  does a shot (above). Each time the window draws a hit on any piece while the panel shows
  a piece, a 220×20 line 66 px below the panel's top is written anew: "Damage: " in white,
  the struck piece's shields before the hit less after it in blue, "/" in white, and its
  current hit points before less after in red. A ship's current hit points are its intact
  structure less its damage pool (spec 04 §9.1), so damage that only went into the pool
  counts; a shot that destroys its target counts all the shields and hit points it had
  left (confirmed: binary; corrected 2026-10-14, complete listing, spec 04 §19.6 Q92). The
  line seems to stay until the next drawn hit writes it again, blank when the panel is
  then empty (inferred, question 92). Since 2026-10-14 ours counts the pool
  (`Battle::markHit`) and keeps the line that way.
- **Weapon cells** (confirmed: binary; corrected 2026-10-14, complete listing, spec 04
  §19.6 Q93). The list has one row for every weapon entry of the piece: for a unit group
  every weapon of every unit, for a planet every weapon of every platform, each row with
  its own tick, reload bars and to-hit chance. There is no count anywhere (counts show only
  in the Design window's grouped mode). A ticked weapon's cell has a teal background (not
  a ready one's). Up to five red bars 3 px wide, one per combat turn of reload left, are
  drawn from y 2 to y 8 of the cell leftward from x 32, 4 px apart. With Show Weapon To Hit
  Chances, the chance of a weapon that is intact and ready is right-aligned at the cell's
  top right. Since 2026-10-14 ours follows (`weaponRows`, `combat_logic.cpp`): a unit
  group's or planet's row for each living unit or platform (a weapon whose units are all
  dead keeps one row, shown destroyed; inferred); it used to group a unit group's weapon
  with an instance count under the chance. **A fighter group's rows** (confirmed: binary;
  2026-10-15): when one ticked row fires (ready, intact, able to damage the target there),
  the shot takes every intact row of the group with the same component and mount, ticked
  or not, given the target or not, even one still reloading, whose reload starts again;
  each rolls on its own and they make one combined hit (spec 04 §6), and every row that
  fired loses its tick. Rows of another component or mount fire only on their own ticks.
  Ours does this since 2026-10-15: the group's identical weapons are one weapon of the
  engine with one reload, the window sends one Fire order for them and unticks all their
  rows when it fires.
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
  then the entries its design or the vehicle carries of its own (none in stock), not its
  components'; for a planet only the planet's own abilities, not its facilities' or its
  colony's, and none of the racial, cultural, population and mood lines of the main
  window's Ability page (§1.4) (corrected 2026-10-04: an earlier reading had the design's
  whole component list here; confirmed: binary, observed on the ship report in spec 07
  session 6; §7 Q78, Q106).

#### 1.10.2 Tactical Combat Orders

A 326×350 window without border or title bar, holding 11 stacked 306×30 buttons with no
gap between them (330 px from (10,10)). Every button is always enabled: an order that
cannot be given is refused once chosen. It is modal; a click on a button closes it, and
only then does the chosen order run, so every picker below is a window of its own that
opens after the menu has gone:

| Button | Key | Effect |
|---|---|---|
| Launch Units | L | opens the Launch \ Recover Units window in battle mode for the selected piece (spec 04 §10.4): the left list holds only that piece and its units, with "Can Launch:" and what it may still launch this combat turn of each kind; the right list only the groups launched while the window has been open, which a click takes back aboard |
| Launch Fighters in Groups | — | opens a list window titled "Select Fighters Per group", one column "Amount", rows 5, 8, 10, 15, 20, 30, 40, 50 |
| Drop Troops | T | acts at once, no target click: lands all troops of the selected ship on the adjacent colonized planet of another side (the side its piece fights for) that comes last in piece order, **whatever the treaty**; refused when that colony already holds a third empire's troops; ground combat follows at once (spec 04 §11). For a side played by hand a message box titled "Drop Troops" explains a refusal; the tests run in this order: no colonized planet of another side adjacent, troops of another empire already on the colony the landing takes, no units of any kind aboard. A ship with units but no troops is refused without a message. The selected piece's kind is not checked: a colony's planet piece drops the troops of its colony's cargo (confirmed: binary; spec 04 §19.4 Q88) |
| Ram Ship | R | arms a target mode, even with nothing selected; the next left-click on a piece rams it, a click on an empty square cancels. Refusals (spec 04 §10.3), in a message box titled "Ram Ship": no piece of ours selected, no target, not adjacent, no movement left, a target that is ours or an ally's |
| Capture Ship | C | as Ram Ship, boarding instead. Refusals (spec 04 §12), titled "Capture Ship": no piece of ours selected, no target, a target that is unowned or not a ship, a boarder that is not a ship, our own ship, not adjacent, no boarding ability, the target's shields up; and last a target of an empire we are not hostile to, titled "Ram Ship" |
| Resolve Combat | — | the game's confirmation window, titled "Resolve Combat", asking whether the rest of the battle should be fought automatically; confirming hands every side to its strategies (spec 04 §4) |
| Set Group Leader / Set Group Member | Alt / Ctrl + digit | opens a list window titled "Select Combat Group" with the numbers 1 to 9; a new leader, or the leader of that very number, then picks a formation in a "Select Formation" list window (Alt + digit goes straight there). Closing that one without a choice clears the piece's group marks. A member, or the leader of another number, gets no formation list |
| Clear Group Assignment, Clear All Group Assignments | Alt or Ctrl + 0 clears the selected piece | act at once |
| Cancel | | closes the menu |

Group numbers run from 1 to 9: the list offers no 0, and Alt+0 or Ctrl+0 clears the
selected piece's marks instead of setting group 0. The group orders act only on a
selected piece of the side whose phase it is, never on a seeker, and refuse silently
(spec 04 §5).

**Message boxes.** The Ram and Capture refusals show only for a side played by hand, that
is while Auto is off; Capture's last refusal (a target of an empire we are not hostile to)
is titled "Ram Ship" and shows even for a side the computer plays. A boarding's outcome
(spec 04 §12) is told in a message box to each side played by hand at that moment,
checked separately for the boarder's side and the target's: when the ship is taken, a box
titled "Ship Boarded" tells the boarder's side that its ship boarded and took over the
target, or, when the boarder's side is played by the computer and the target's by hand,
tells the target's side that its ship was boarded and taken over; "Boarding Failed" says
the ship failed to board the target, and "Self-Destruct" that the target blew itself up
during the boarding and both ships are gone, to either side or both. The computer's phases
use the same code, so a human defending tactically is told of a computer's boarding. With
every side automatic (Strategic, Auto, Resolve Combat) there are no boxes. "Ship Boarded"
and "Boarding Failed" appear the moment the boarding is decided, before the ship changes
owner, before the boarding's hits and before anything of it is drawn; the game waits until
the box is closed, and the map is then updated. "Self-Destruct" comes after its two
explosions (confirmed: binary; corrected 2026-10-14, complete listing, spec 04 §19.6 Q95).
Since 2026-10-14 ours follows: the record's `Boarding` event comes before the capture and
the hits, a self-destruct's after them, and the playback stops at it until the box is
closed.

#### 1.10.3 Combat Options and Combat Replay Options

**Combat Options** (Tactical → Options), per computer, shared with the Options window of
§1.9 where the rows are the same. Fresh-install defaults in brackets:

| Heading | Rows |
|---|---|
| Animation | animate ship movement in combat [on] |
| Sound | Sound On [on]; Music On [on, unless music is off or not allowed] |
| Tactical Combat | Fast Tactical Combat [off] (removes every wait from the animations, below); Show Group Identifiers [off] (on every piece with a group number, of any empire, a 14×12 box at the top right of its square, blue for a leader and red for a member, with the number in white in its middle); Show Viewing Rectangle on Map [on] (the dotted box on the overview); Center Map on Current Ship [on] (the view moves to a newly selected piece only when its square is outside the part of the map shown; a piece struck by a seeker likewise); Show Weapon To Hit Chances [off] (with it on, hovering an enemy while a piece is selected shows each ready weapon's chance in the weapon list); Show Grid [off] |

Closing the window saves the switches and starts or stops the music. In the Combat
Simulator only, a Stop Combat button ends the battle and closes the tactical window.
OpenSE4 adds an "OpenSE4" heading with one switch of its own, also in the Options window
under OpenSE4: "Say on the map why a click fired nothing" [off], which writes on the
tactical map why a click on an enemy fired no weapon (out of range, reloading, no
supplies, nothing ticked); the original says nothing then (§1.10.1 "Mouse").
Layout as in §1.9 ("Options In Use" at (15,38), list at (15,56), 560×400). Our client draws
the rows as 18 px check boxes under silver headings, as Combat Replay Options was seen (spec
07 session 5; inferred for this window).

**Fast Tactical Combat** keeps every animation and removes the waits between frames;
there is no speed factor. Besides, a torpedo then moves 6 px a frame instead of 4, and
the shield picture of a hit absorbed by shields is not stamped. **Each wait** first lets
the program handle its pending window messages once, then busy-waits on the system's
millisecond tick counter until it has advanced by the wait; there is no sleep and no
finer timer. So a wait shorter than the counter's step (about 15.6 ms on current Windows,
16 ms under Wine 11.18, observed in spec 07 session 3) lasts until the counter's next step,
and in a run of frames each frame lasts about one step: a 36-frame slide takes about
0.56 s on Windows and 0.58 s under Wine (observed), not 36 ms. Fast
Tactical Combat skips the wait call itself, message handling included. Without the
switch the Tactical Combat window draws and waits (confirmed: binary; §7 Q77):

| Animation | Frames and waits |
|---|---|
| a piece moving one square, with "animate ship movement in combat" on | slides 1 px a frame along the longer axis, so 36 frames for any step, diagonal too, 1 ms after each; seekers slide the same way. No slide and no wait when either square is outside the shown part of the map: the piece just appears on its new square |
| the same with that switch off | the piece jumps, then 0.1 s (also off-screen) |
| a piece turning to a new facing (switch on, square in view) | before the step, on its old square, the shorter way (clockwise for a half-turn): 5° a frame, 9 frames per 45°, 36 for a half-turn, 0.01 s after each. Otherwise the piece simply faces the new way. Seekers never turn |
| a beam | stamps of the 10x10 centre of the 20x20 beam cell, rotated to the bearing, one every 6 px along the longer axis (6 per square), drawn one by one outward with 0.00001 s after each, then erased one by one in the same order with 0.00005 s after each |
| a torpedo | a 40x40 picture (the 20x20 cell rotated to the bearing) moving 4 px a frame (9 frames per square), 1 ms after each |
| a hit that damages structure or destroys the target | the 8-frame explosion drawn in place (36x36, or 72x72 for some losses: every ship and base loss observed, §7 Q77), 0.1 s after each frame and after the last is wiped, 0.9 s in all, with a boom sound (boom3 for a loss); a destroying shot plays it once, with no second loss animation |
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
Our client has the layout seen in spec 07 session 5 (the list at (15,57), the headings
Animation and Tactical Combat over check boxes) and adds, under an "OpenSE4" heading, the
per-computer switch for its list of the replay's events (§7 Q39).

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
- **Begin** refuses only when no item belongs to a side, with a message that at least one
  player is needed: one side is enough, and nobody needs to see an enemy (spec 04 §17).
  Each click on an object of the home system adds another copy, so the same planet can
  take part several times. With Strategic selected, the Strategic Combat window opens
  over the simulator, which stays open. With Tactical selected, the
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
- **The small map** (confirmed: binary). Every paint draws the whole picture anew: black,
  then every piece in the battle's piece list, in piece order, with square (x, y) at
  (3x, 3y) of the map; no grid and no view box (the box belongs to the Tactical Combat
  overview). A one-square piece (ship, base, fighter, satellite or drone group, and a
  seeker in flight) is a filled 3×3 square, the same size whatever the hull; a planet or
  neutral obstacle is a hollow 12×12 outline, 1 px wide, round its 4×4 squares, through
  which smaller pieces show. The colour is the owner's empire colour (a simulation's side
  colour); a piece with no owner, such as a planet whose colony died, is grey #808080. A
  captured ship takes its new owner's colour at the next paint. Mines are no pieces and
  never show. A destroyed piece leaves the piece list at once, with every seeker aimed at
  it (spec 04 §10.1), so its square is gone at the next paint, which comes after the next
  step of any piece or seeker or at the latest at the redraw after the combat turn. The
  last paint is the one after the last combat turn, before the end check, and the
  end-of-battle steps change nothing on it: with Close lit the map shows the pieces still
  in the battle. A seeker can be left on it only when the turn counter ended the battle,
  since a flying seeker keeps the battle going (spec 04 §4). Since 2026-10-14 our client
  follows (§7 Q122, GitHub issue #17; `CombatMapPainter::squares`, which also draws the
  Tactical Combat overview's pieces so, inferred); the piece under the pointer named in a
  tooltip is OpenSE4's.
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
  its units at (40,393), 504×72. As seen (observed, spec 07 session 5, a simulation): the
  side's mark, a 26×18 box (the numbered colour box in a simulation), at (120,259) and
  (120,373); the facility grid is 6 × 4 cells of 36 px drawn with 1 px #617BC2 lines, each
  facility's picture with its level's numeral at the bottom right; each unit grid is 14 × 2
  such cells, one stack per cell with its count in small white type at the cell's bottom
  right, the defender's militia as a stack with the race's population picture.
  Our client follows this since 2026-10-03 (`GroundCombatScreen`), with the empire's flag as
  the side's mark in a real battle (inferred), the level's numeral as a Roman numeral
  (inferred) and the name of a facility or stack shown under the pointer (ours).
  `--open=ground-combat` reaches the window headless: the player's troop transports land on
  its homeworld in a sample strategic simulation (it needs a troop design, so add turns).

### 1.11 The windows as observed (spec 07 session 3)

Seen side by side with the running original on 2026-10-01 (spec 07 session 3, "Side by side"),
our windows differed in the ways that session lists. Since 2026-10-01 our client follows what
was observed; details the observation does not give are OpenSE4's choices, marked (inferred)
in the code and asked in §7 Q89–Q98.

- **Lists** (observed). Every list scrolls with an up arrow and a down arrow in a narrow column
  at its right, not a scroll bar, and a picture column is headed "Pic". Ours follows
  §7 Q89 since 2026-10-03 (`beginList`, `beginListTable` in `screens/list_widgets.*`): the 24 px
  column of `Arrows.bmp` cells with the thumb and the track, a row a click, a held arrow
  repeating every 100 ms, and the wheel a row per notch.
- **On/off settings in button columns** (observed): a check box that holds the lamp when on:
  Construction Queues' Ships, Planets, Ship SY and Planet SY, Ships\Units' Show Ships, Show
  Units and Show Fleets, Designs' Hide Obsolete and Stats\Strategy, Planets' No Sys To Avoid.
  Ours also draws Set Construction Queue's Only Latest, Emergency Build, Repeat Build and Queue
  On Hold, the designer's To Hit Modifiers, Condensed View and Only Latest, and the Weapons
  Report's Only Latest that way.
- **Planets** (observed): the statistics read Known Systems, Number of Planets, Number of
  Colonizable Planets, "... which are owned by Enemies", "... by Allies", "... by
  Non-Aligned", "... which are not Colonized", "... which are Breathable", then Colonizing
  Ships and "... which are available"; the value columns are three "Value" headings, each with
  its resource's icon in its resource colour; a row's atmosphere is on the name's line; every
  name is white, our own colonies' too.
- **Colonies** (observed): a summary of Systems with Colonies, Number of Colonies, Number of
  Blockaded Colonies, Total Population, the research and intelligence points produced, Total
  Resources Produced and Maximum Resource Storage (with icons, the storage as "50kT"), beside
  the mini-map with nothing cut off; no Constr. Queue or Goto button; Close in the 14th slot.
  The original's labels, places and clicks are in §7 Q90 (confirmed: binary), which ours
  follows since 2026-10-03.
- **Construction Queues** (observed): the Rate values carry resource icons; ours gives the Usage
  values icons as well.
- **Research** (observed): completed areas stay in the list, dimmed (#606060), with "Complete"
  as the cost, and a click on one does nothing; a small box under each project box (ours holds
  the project's progress); Reorder Projects in the 13th slot. Besides (confirmed: binary): a
  queued area is drawn green and still shows its next level's cost; the group headings are in
  alphabetical order, the areas in data order under each; the heading over the boxes reads "1
  Current Project" or "N Current Projects"; the reorder window's rows are the areas' names; a
  right-click on an area or a project box opens the area's report. Ours since 2026-10-11 (the
  green is the progress bar's #00FF00, inferred, §7 Q113); the description under the pointer
  is ours. The original shows Tech Tree only when the game lets players see the complete tech
  tree (§1.2, spec 01 §2.2), and the project boxes of §7 Q92 (confirmed: binary); ours too
  since 2026-10-03 (`GameOptions::completeTechTree`).

  *The area's report* (GitHub issue #11, on 0.12.0: "In the original game, right clicking on a
  research in progress would give a popup about the research."; the player wants to see "which
  items are going to be available on my next research, like Armor gives different types of
  armor every few levels"). Ours opens it on a right-click on a project in progress as on an
  area's row: the area's group, level, the next level's cost, requirements and description,
  and "Level N brings" with what the next level makes available. **Ours adds**, since
  2026-10-14, "Later levels bring" under it: a line per later level up to the area's maximum
  that makes something available, "Level M" and the items' names, tech areas marked "(tech
  area)"; levels that bring nothing are left out (OpenSE4's own; `levelUnlocksFrom` in
  `screens/empire_logic.cpp`, `techDetail` in `screens/item_reports.cpp`). The report popup
  is at most the frame's height less 40 px (so it fits the 800x600 frame) and scrolls beyond.
  The Research window's own line under the pointer keeps the next level alone.
- **Designs** (observed, and spec 07 §UI): rows under design-type headings, each with a lamp
  (green for the selected design, blue for the others), the picture, the name, the hull and
  "Prototype"; the detail shows Size, Design Type and Date Created by the picture, then Cost,
  Movement, Shields, Cargo Space and Supply Capacity, and the components as a grid of icons, 8
  to a row; a note says obsolete designs are deleted automatically (spec 03 §4.1). The original's
  rows, order and grid are in §7 Q93 (confirmed: binary), which ours follows since 2026-10-03.
- **Create Design** (observed): Create asks for the vehicle type first, among the types the
  empire has a hull of, and the designer is titled after it ("Ship Design"); it starts with no
  size, and the warnings ask for one. The picture; Size, Design Type and Design Name, each with
  a down-arrow button that opens its list (Design Name can also be typed); the figures box
  (Space Used, Total Cost on three lines, Movement, Shields, Cargo Space, Supply Capacity);
  "Components on Design" as a strip of icons with arrows; "Components Available" as a 3-column
  grid of tiles (icon, name, kT); the Warnings and Component Details boxes; buttons Comp Type,
  Weap Mount, To Hit Modifiers, Condensed View and Only Latest (check boxes), Weapons Report,
  Create Design and Cancel in the 13th and 14th slots. Copy starts with an empty name (spec 03
  §4.1). The original's places, sizes and the meaning of To Hit Modifiers are in §7 Q94 (confirmed: binary), which ours follows since 2026-10-03.
- **Empire Status** (observed): the blocks Resource Production Per Turn (From Our Colonies, From
  Trade, From Tariffs, From Remote Mining, Total), Resource Expenses Per Turn (Tariffs,
  Maintenance Cost, Construction Queue Usage, Total), Net Resources Per Turn and Resources in
  Treasury (Current Total, Maximum Resource Storage), with icons on the first row; Change Email
  in the 12th slot and Change Password in the 13th. Change Email asks for the empire's
  address and keeps it (`cmd::SetEmail`, since 2026-10-03); the missed-turn switch ("AI should
  not make changes during a simultaneous game", §1.2.1) is in the Ministers window. The original's layout and Change Email are in §7 Q95 (confirmed: binary); ours follows the
  layout since 2026-10-03.
- **Empires** (observed): Treaty, Trade, Tariff, a gap, History, Treaty Grid, Intelligence (dim
  without contact), Borders, Scores, Victory Conditions, Comparisons, a gap, Our Race; Borders
  without a check box; no heading or explanation over the empty strip. Borders is a window of its
  own (§7 Q96, confirmed: binary), in ours too since 2026-10-03.
- **Ships\Units and Log** (observed): no hint paragraph; the title "Ships\Units"; the Show check
  boxes in slots 11–13; an empty Log list stays empty, with Goto lit (§7 Q91, confirmed: binary; ours keeps Goto's
  last state since 2026-10-03).
- **Combat Simulator** (observed): the items in alphabetical order (ours ignores letter case and
  mixes designs and objects); Tactical, Strategic, four gaps, No Obsolete, Strategies, Computer
  Control, Fleets For Plr, Change Cargo, a gap, Begin in the 13th slot, Cancel.
- **Tactical Combat** (observed): the title strip holds the title, Location, Turn and Empires,
  and navigation arrows with a stop button at its right; the map fills the left part directly
  (§1.10.1: from (10,38), (W−256)×(H−44)), with no box around each piece; the right column
  holds the current piece's report (picture, Size, Move, a damage bar, a 6×6 weapon grid), the
  target's report below it, Options and Orders, an Auto check box and End Turn as a 2×2 group,
  and the overview map at the bottom (§1.10.1). Ours titles a simulation's battle "Tactical
  Combat" too; since 2026-10-03 it has the original's places and panels (§7 Q97, confirmed:
  binary and observed).
- **The report panel** (observed): no "List" text button; the up-arrow button of §2.5 returns
  to the list (place and states: §7 Q98, confirmed: binary; ours follows since 2026-10-03).

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
- Since 2026-10-14 Create Design grows at 1024x768 as the four list windows do, its lower
  boxes taking the height (§7 Q94, GitHub issue #14).
- At 800x600 a lesson that points at an order on another page of the order strip
  outlines the pager arrow that leads there (ours: the original has no lessons).
- The tactical window keeps OpenSE4's own panels, laid out over the frame's width.

### 2.2 Status bar

Left to right: large empire flag, empire name, emperor title and name, "Game Date" (the
game starts at 2400.0 and each turn adds 0.1), then stored minerals, organics and
radioactives, each followed by its resource icon (blue crystal, green organic, red
radiation symbol). At the far right a minimize button, and during the tutorial a "T"
button that re-opens the tutorial text [T][S]. The "T" shows in a game of the tutorial
only, not in other scenarios, which have Ctrl+H alone (confirmed: binary, §5.6).

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
  and 2 px below its top, and in a game of the tutorial the "T" button just left of it
  (not in the other scenarios; every large 780x475 dialog of the tutorial has a "T" of
  its own, §5.6). Ours shows the same "T" during OpenSE4's lessons, where it shows the
  lesson panel again (docs/LEARNING.md).

### 2.3 Command panel

Three groups, left to right [T][S]:

1. **Command buttons**: 12 buttons of 34x34 in two rows of six, from 2 px right of and
   below the panel's top-left corner. Top row: Game Menu,
   Designs, Planets, Colonies, Ships\Units, Construction Queues. Bottom row: Research,
   Empires, Log, Empire Status, Help, End Turn. They match F2..F11, F1 and F12 (§3).
   Pressing End Turn disables the main window's panels until every computer player has
   moved. A left-click plays `cmdbtn` when sound is on (confirmed: binary). Our client
   processes the turn within one frame; the clicks and keys given meanwhile are dropped
   when it ends (`ClassicMode::dropInputMadeWhileBusy`), so a second click on End Turn
   does not end the next turn as well. While a battle is being shown, End Turn does
   nothing (the battle's window is modal, §1.10.5).
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
   edge, at (448,34) at 800x600 and (965,34) at 1024x768 (confirmed: binary).
   **The ship cycle** (confirmed: binary). The top selector's arrows, Space, Ctrl+N and
   Ctrl+B all run one command over the same ships: next (Space, Ctrl+N, the right arrow)
   or previous (Ctrl+B, the left arrow) through the vehicles in object order, wrapping
   round the list's end. A candidate is one of the player's own ships or bases, never a
   unit group, whose maximum movement is above 0 and that is not mothballed (so a base or
   a mothballed ship is never visited); in a turn-based game it has movement left, in a
   simultaneous game its own order list is empty; and its first order is not Sentry,
   whatever the turn style (so a sentried ship with movement left is passed by in a
   turn-based game). The Empire Options' four Next/Previous switches then filter [T][S]:
   skip ships under construction, skip damaged ships, skip ships in fleets, and stop once
   per location. "Stop once per location" keeps a table of the places the cycle has
   stopped at and passes by a candidate whose place is in it; the table is emptied when
   the cycle wraps round the list's end, and when the player or the direction changes.
   Our client follows this since 2026-10-14 (`cycleShip`, `order_rules.hpp`). Its
   choices (inferred, §7 Q115): the place of the ship the cycle leaves counts as visited,
   so a press from a ship selected by a click goes to another place; "skip ships under
   construction" has nothing to skip, as our ships appear finished.

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
  centred in its cell: offset 0 at 800x600, 7 px at 1024x768. Our client scales the whole
  frame to the window; since 2026-10-04 every picture it cuts from a sheet (the planet
  sheet's cells, flags, buttons, icons) is a texture of its own, so the smoothing of a
  frame scaled by a fraction no longer takes in the edge of the next cell, which drew a thin
  line, its colour that of the next row, under warp points, asteroid fields and other
  objects.
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
- **Moves as they are made** (confirmed: binary; settled in full on 2026-10-14, §7 Q112;
  observed, spec 07 session 8).
  With "animate ship movement in the system window" on, a move is animated only while it is
  carried out for the human player whose turn it is, in a turn-based game. The test is made on
  the player whose turn it is, and nothing else is tested, not the shown system and not whether
  the viewer sees the vehicle: the only moves made then are that player's own, and the view has
  been switched to the mover's system before its orders run (§2.7). A computer player's turn
  (an empire under computer control counts as one) and every move of a simultaneous turn's
  processing are never animated; the movement log replay is the only other animation (§7
  Q51).
  - *Groups one after another, never interleaved.* A group is the set of vehicles that carries
    out one order together (spec 03 §8): a vehicle alone, a fleet's members at its location,
    the tagged vehicles, or a human's drone group with the drone groups that join it. Its run
    (its acting vehicle's list from the first order on, completed orders chaining, at most 21,
    spec 05 §8) is carried out square by square, every square animated, and is over before the
    next group starts; so a Move To, a patrol or a repeating list moves its group as far as it
    can go, all its legs, before anything else moves. At the start of a human's turn the
    ministers give their orders first; then each own object whose list holds orders takes its
    turn in the game's object order, vehicles and colonies alike: it is selected (§2.7: its
    report opened, its orders lit, the yellow marks on its square), then its run is carried
    out and animated, and the next object is selected right after its last square (observed).
    An Attack Sector question in a run is asked in its step, so the pass waits for the answer
    and the next object acts only once the group has entered (and fought) or its order has
    failed (confirmed: binary; spec 03 §19 Q55).
    Vehicles that moved with an earlier group of the pass (a fleet's members) are skipped. An
    order given during the turn (to the selected vehicle, a fleet or the tagged vehicles, by
    any of the ways of §2.7) is one run, the tagged vehicles one group moving together.
  - *Each square, in this order:* the Attack Sector question when the square ahead holds
    enemies (spec 03 §6.2); when the bearing from the old square to the new one, taken to the
    nearest of the 8 headings, differs from the heading of the group's first vehicle, the turn:
    the angle steps 5° a frame the shorter way with 10 ms after each frame, but nothing is drawn
    meanwhile: the whole group is taken off the map first and the turned mini would be drawn
    only for a vehicle still in a system, which it no longer is, so the screen keeps the old
    square exactly as it was (the vehicle at its old heading, or the stack with its count and
    the yellow marks) for the turn's waits, a still pause in proportion to the angle (about
    0.3 s per 90° under Wine, observed); the whole group is placed on the new square, each
    member taking the new heading (a fleet's location moves with it); the old square is
    redrawn without the group and the mini slides 1 px a frame along the longer axis (36
    frames a square at 800x600, 50 at 1024x768, diagonals the same) with 1 ms after each (the
    waits of §1.10.3), at the new heading from its first frame; the game pauses for `System
    Ship Movement Delay Milliseconds`, but reads the value as seconds (a defect of the
    original; the stock value 0 hides it); the old and the new square are redrawn in full;
    then what the new square does to the group (hazard damage and its "Damage Taken" box,
    minefields, a battle on arrival), each of which also redraws the galaxy panel and the
    square or the system and selects the current sector again. A move over several squares is
    never one straight slide: a route that bends pauses still at each bend, then slides on at
    the new heading; straight on, the squares slide one after another with no gap (observed).
  - *One sprite.* During the slide every member of the group is off the map and one mini is
    drawn: the group's first vehicle's own design mini at its heading, with the dotted circle
    when it is cloaked; for a fleet the first of its members in its sector's list of objects,
    not the fleet icon and not necessarily the member that acts for it; for tagged vehicles
    the first one tagged; for a drone group with companions the acting one. The last frame of
    the slide stays through the pause, and the redraw after it brings back the square's normal
    drawing (the fleet icon, the counts). Between the steps only the two squares are redrawn;
    the report panel is refreshed after a jump and once when the group's run is over.
  - *Not animated:* warp jumps (the arrival system is then shown, §2.7, and the steps there are
    animated in it), every other empire's moves, a simultaneous turn's moves. With the
    animation off there is no turn, slide or pause, but the squares are still redrawn after
    every step and the view still follows, so each group appears at once where its run ends
    (the look inferred).
  - *Mines.* When a minefield strikes the group during an animated move (the human whose turn
    it is, turn-based, with the animation option on or off), once for the square, after every
    minefield there has acted however many mines went off: a small explosion is drawn over the
    square's 36x36 sprite place, never scaled, over whatever the square shows (the group still
    there, the members the mines destroyed included): eight 36x36 frames from a row of
    `Pictures/Game/General.bmp` (frame n at x = 36 (n − 1)), the row at y 64 when no member
    was destroyed and the one at y 100 when one or more were, each cell's background keyed
    (the key colour inferred), each frame held 0.1 s; then the square as it was before the
    strike is put back and held 0.1 s more; then `boom1` starts (with sounds on) and the game
    waits 0.5 s; then the square is redrawn without the destroyed members, the whole system
    panel redrawn, the "Minefield" message box shown, the current sector selected again, and
    the group's run stops there. Moves that are not animated get no explosion, no sound and no
    wait.
  - *Input* (confirmed: binary; observed). Every wait of the animation first handles the
    window's pending messages, so the window repaints and input is dispatched in the middle of
    the moves; what is taken depends on how the moves came:
    - At a human's turn start the pass runs inside the End Turn call, which marks the main
      window as ending the turn: clicks on the system and galaxy maps, the main window's keys,
      the order buttons, End Turn, the load and quit commands and the other command buttons
      are ignored, neither queued nor a skip (a click on a planet, F3, F4 and End Turn tried:
      nothing opened, then or after the Log); only the buttons' hover hints are written.
    - An order given by a click on the map (a target picked there): while that click is
      handled, which includes all its moves, map clicks, keys, the command and order buttons
      and End Turn are ignored, then and after (a click on a planet, F3, the Designs button
      and End Turn tried); moving the pointer does not update the coordinate line; only the
      hover hints are written.
    - An order given by an order button (Resupply tried): only a second order button is
      refused (confirmed: binary); a map click selects at once while the group slides on (the
      run's closing refresh then shows the report of what was selected); a key or a command
      button opens its window, and the moves stand still under it until it is closed, then go
      on from where they stopped; End Turn asks "Confirm End Turn" with the moves standing
      still: No lets them go on; Yes ends the turn inside them, the computer players' turns
      and the next turn's start run unseen, and after the Log the interrupted group slides one
      more square and the program faults (an access violation), after which the order buttons
      and keys do nothing until the program is restarted (observed: a defect of the original).
    Nothing the turn's processing opens comes before the moves: at a turn's start the player
    sees the moves first and the Log after them (§2.7).

  Since 2026-10-01 our client animates moves (`ShipGlides`, `client/classic/ship_glides.cpp`):
  the turn, then the slide, 1 px a frame (36 or 50 frames a square), then the pause, read as
  seconds; headings come from the engine and survive loading. Since 2026-10-14 nothing of the
  turn is drawn: for the turn's frames and waits, the same as before, the group stays drawn on
  its old square as it was, at its old heading (`ShipGlides::heldAt`, `heldHeading`;
  `ShipGlides::sliding` is false), and the one mini appears at the new heading on the slide's
  first frame; before, the mini turned on the old square 5° a frame. Since 2026-10-06:
  - **Pace.** The frames come by the time elapsed, whatever the display's refresh rate
    (`MovementPace`, `client/classic/movement_pace.hpp`): by default at the original's
    waits as written, 10 ms after a turn frame and 1 ms after a slide frame, so a square
    takes 50 ms at 1024x768 and a 45° turn 90 ms. OpenSE4's movement speed (§1.9) divides
    the waits. Before, each frame stayed at least one display refresh: about 0.8 s a square
    at 1024x768 on a 60 Hz display, which a player found much too slow (§7 Q112). How long
    the original's frames last depends on the system's millisecond counter its waits read
    (§1.10.3): each frame about one step of it, 16 ms under Wine (observed), so a square
    there takes about 0.8 s; whether players' Windows machines show the faster waits as
    written is open (§7 Q112).
  - **Order.** Since 2026-10-14 (§7 Q112) ours follows the order above. The engine reports
    each group's step as it makes it (`game::MovementStep`: the members in the group's order,
    the vehicle whose mini stands for the group, the two squares, the heading and the warp
    point of a jump; and, as a step of its own kind, mines striking the group), to an observer
    the caller passes in (`TurnOptions::movementStep`, `LiveOptions::movementStep`,
    `applyLive`); observing changes nothing in the game. The session hands the steps of each
    engine call to the main window in the order they were made, the turn processing on its
    own thread included, with the points where the view follows the player's own groups
    (`ClassicSession::takeMoves`, §2.7), each marked when the original animates it (the
    player's own, in its turn of a turn-based game), and `ShipGlides` plays them as a queue:
    each square its own step, the group one sprite (the mini of the vehicle the engine names:
    for a fleet the member first in its system's list of objects, `Vehicle::arrival`, which
    ours keeps in place of the sector's own list (inferred where the two would differ), not
    the fleet's icon, with the dotted circle when it is cloaked), each step when the one
    before is over, so a group's run is over before the next group moves; a group waiting for
    its turn is drawn on its square. Steps in another system, steps of vehicles the player
    does not see now or did not see before (its own excepted) and warp jumps take no time, in
    their turn. The player's own vehicles that the moves destroyed are drawn until their steps
    are shown (`ClassicSession::takeGone`). Mines striking the player's group show the
    explosion above, its frames, its sound and its waits, with the animation on or off
    (`MineBlast`, `ShipGlides::blasts`). The moves the original does not animate (other
    empires', a simultaneous turn's at the next turn's start) take no time in their turn, and
    those ships are simply drawn on their new squares, unless the player asks for them in the
    Options window (§1.9, OpenSE4's own). Ours differs (OpenSE4's own, docs/PARITY_GAPS.md):
    1. OpenSE4's option "Also animate other empires' and simultaneous turns' moves" (off by
       default) shows the other empires' moves the player sees in the shown system, in the
       order they were made (a turn-based game's computer players, and humans under computer
       control, before the player's own turn start, where the original shows a black main
       window with "Player N taking turn..." and their moves not at all, §2.4 "Notices"), and a
       simultaneous turn's moves at the next turn's start, in the order the movement days made
       them, where the original shows none outside the replay.
    2. OpenSE4's option "Ship movement: all at once" (§1.9) starts every group together, each
       still square by square, and the view goes at once to where the last of the player's
       groups ran.
    3. Since 2026-10-14 a network client gets its own moves with the host's state, as the
       host's engine made them (protocol 10, docs/MULTIPLAYER.md "Protocol"): its commands'
       and its turn start's steps and where its view follows, so they are shown, and the view
       follows, as on one computer. An e-mail client's new turn file, and other players'
       moves on a network client, come without steps: each vehicle it saw before and sees in
       a new place glides there in one straight move when it is animated at all (its own in a
       turn-based game, the others only with the option of 1), the moves grouped by where they
       start and end and shown one group after another in the game's vehicle order (all
       together with "all at once"), with the pause of each square (inferred).
    4. A click or a key shows a turn's start's moves at once (**Skip**, below).
    5. A window or question opened over the moves of an order given by an order button holds
       them, as the original's does, but End Turn's Yes first shows them at once and then ends
       the turn (the original faults there).

    A battle that stops an engine call (§2.7, §1.10.5) comes, as the original's, after the
    moves that led to it: the call stopped at the battle gives the steps, LiveSteps and
    messages it made before the stop (`TurnResult::battle`, which leaves the game as before
    the call), the session hands them on marked provisional (`ShownMove::provisional`), and
    they are shown, the move into the battle's sector last, before the battle's question or
    window opens; meanwhile their groups stay drawn where those steps took them
    (`ShipGlides::release`, the game still holding them where they were). The call made again
    with the answer makes the same steps again first, and the session hands on only the rest,
    shown after the battle (since 2026-10-14; before, the battle came first and all the
    call's moves after it). An Attack Sector question at the start of the player's turn stops
    the call in the same way in a local or hotseat game (`BattleQuestion::Kind::Enter`, spec 03
    §19 Q55): the moves before it are shown, the question is asked (`ClassicSession::questions`,
    the call's stop), and the answer carries the call on, the groups after it moving only then.
    A network or e-mail game's host does not wait for it (OpenSE4's own, docs/PARITY_GAPS.md):
    the later groups' moves come with the player's new state, and the question after them.

    Whatever the call opens waits for these glides: the battles to watch, the endings, the
    questions and the Log of a turn's start (§2.7), and a question an order given during the
    turn raised. Meanwhile the main window takes no input, as under a window. Since
    2026-10-14 a message box the player's own orders raise (a failed Colonize, a minefield's)
    comes where the moves reach it, as the original's comes as the order runs: the engine
    counts the messages made before each step (`MovementStep::messages`,
    `PlayerMessage::liveSteps`), the session puts each in its place among the moves
    (`ClassicSession::ShownMove::message`), and the box holds the moves after it until it is
    answered; before, every box waited for all the call's moves.
    Since 2026-10-14 the moves of an order given during the turn take input as the original's
    (`mainWindowTakes`, `client/classic/turn_start.hpp`; `MainWindow::orderGiven`): after an
    order given by a click on the map (a target picked there, or OpenSE4's right-click Move
    To), the main window takes nothing until its moves are over, not its map clicks, keys,
    command and order buttons, selectors, report panel or End Turn, and only the buttons'
    hover hints show; after an order given by an order button (or, ours, by its key, inferred),
    everything but a second order button: a click selects, a key or a button opens its window,
    and a window or question opened over the moves holds them, their clock standing still,
    until it is closed (`MainWindow::holdMoves`). Before, those moves glided while the player
    played on, whatever gave the order.
  - **Skip** (an OpenSE4 convenience; the original's animation cannot be skipped, and at a
    turn's start it ignores clicks and keys, confirmed: binary, observed). While the main
    window waits for the moves of a turn's start (or for something they hold back), a click or
    a key shows them at once: every ship is drawn on its square, pauses included, and what
    waited for them opens in the next frame; the press does nothing else. The movement log
    replay keeps the original's rule (§7 Q62: presses during a day's animations are ignored).

Contents [T][S]:

- Planets, asteroid fields, the star(s), storms and warp points as 36x36 sprites. A warp
  point whose destination system the empire has explored shows that system's name in its
  cell (with the Empire Option "warp point names"; details below) (confirmed: binary).
- A colonised planet has a small box with population bars at its top right, coloured by
  owner. It is drawn only when the viewer sees the colony (spec 01 §6.9); an unseen foreign
  colony shows the colonisation star instead, when its type fits. Only the planet the
  sector shows can carry it (**Sector contents** below) (confirmed: binary).
- Ships: a single-owner stack shows one ship sprite with a count in the bottom-right
  corner; a location with several empires, or ships orbiting a planet, shows small empire
  flags instead. The counts are drawn in the owner's empire colour (§5.3), next to each
  flag when there are several (confirmed: binary). Exact rules: **Sector contents**
  below.
- A cloaked ship shown as the sector's sprite gets a 1 px dotted circle inscribed in its
  36x36 sprite square, in its empire's colour (dark cyan, RGB(0,128,128), if it has
  none); there is no circle when flags are shown (confirmed: binary).
- Colonisation hint: a small green star on a planet means colonisable and breathable; red
  means colonisable but would be domed; no star means not colonisable by this empire, or
  colonised as far as the viewer sees. Only the planet the sector shows can carry it, and
  never together with the colony box (**Sector contents** below) (confirmed: binary).
- The selected location is marked by `Dialogs/Selection.bmp` (eight small yellow marks),
  drawn over the sector's 36x36 square with black transparent (observed); our client draws
  it (without the picture, four corner lines). Exact rules: **Selection** below.
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
- **One planet's marks per sector.** Only the shown stellar object can carry the colony
  box, the colonisation star and the facility letters. Colonising never changes which
  object is shown, and the other planets of the sector get no mark of any kind: a colony
  on a moon beside a larger planet has no box on the map, and the sector shows the larger
  planet's star if that planet qualifies (observed, spec 07 session 6). The shown planet
  gets either the box or the star, never both:
  - **Colony box**, when the viewer sees the colony: a 1 px outline in the owner's empire
    colour from (X+20, Y+1) to (X+35, Y+11) (15×10, right and bottom edges excluded), with
    0 to 3 filled bars of the same colour inside, each 3×6 with its top at Y+3, at X+30,
    X+26 and X+22, filled right to left: none for population 0, one up to 200M, two under
    1000M, three from 1000M. There is no flag picture.
  - **Colonisation star**, when the System Display row "colonizable planets" (on by
    default) is on, the object is a planet (not an asteroid field), the viewer's empire
    can colonise its surface type and atmosphere (the two game options that limit
    colonisation apply) and it carries no colony the viewer sees: the 12×8 cell of
    `Pictures/Game/General.bmp` at x 234 (green, breathable for the viewer) or x 258 (red,
    otherwise), y 16, drawn at (X+24, Y).
  - Order of drawing: the stellar sprite with its colony box, the star, the facility
    letters, the white count of stellar objects, and last the vehicle sprite or the
    owners' flags and counts below, over all of these.
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

**Selection** (observed, spec 07 session 3). The selected object's sector shows
`Dialogs/Selection.bmp` (36x36: eight small yellow marks at the corners and the middles of
the sides) drawn over the sector's sprite square with black transparent; a capture matched
the picture pixel for pixel at the sprite square's top-left (266,371) for sector (5,5) at
1024x768. Since 2026-10-01 our client draws the picture there (`main_window.cpp`), while the
selected sector is marked (it holds an object the viewer sees, §7 Q64), with four corner
lines only when the picture is missing; the movement line (below) is drawn over it.

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
- **Waypoints** 1–10 of the viewer in this system (the number shown is the waypoint's, 1 to
  10, never 0; confirmed: binary): a cyan (0,255,255) 1 px rectangle on the
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
  on the sector's shown planet (**Sector contents**) when it is colonized (never an
  asteroid field), the viewer can see it and it is
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

Our client (since 2026-10-01: `game::movement::planRoute` in `src/game/movement_route.cpp`
works the route out with the engine's own step rule, `inSystemStep`, and the route search;
`src/client/classic/movement_line.cpp` chooses the object and the marks, which
`main_window.cpp` draws last in the panel overlay) follows these rules, with these
differences:
1. The random replacement squares of blocked steps come from a display-only generator seeded
   from the object and the date, so the line is the same on every machine and every frame and
   never takes a number from the game's sequence.
2. A route that leaves the shown system and comes back is broken there (no joining segment,
   no second number), as allowed above.
3. The ring's pixels match the original's under Wine (§7 Q85, observed).
4. With ships tagged the report panel shows the tag list, so no line is drawn (inferred).
5. During the movement-log replay the route of the report open when it began stays drawn, and its end selects sector 0 of the shown system, as in the original (§7 Q86, confirmed: binary; ours since 2026-10-03).
6. Ctrl+L saves the settings at once and shows a short note ("Movement lines on" or "off"), as
   our Ctrl+S does; the original only flips the switch (§3.2). Ours never shows a stale line,
   since it works the route out from the live state.
7. A new simultaneous game started from Game Setup or Quick Start, or hosted on the network,
   switches the option on and stores it (§1.9); loading or joining a game does not.
8. During the movement animation (§2.4 "Moves as they are made"), since 2026-10-06: while the
   object whose report is open (a fleet: any of its members) glides or waits for its steps
   to be shown, the line is the route as
   it stood before the move, worked out on the game as the engine call found it (an order
   given during the turn already in its list, not yet carried out; at a turn-based turn's
   start with the vehicles' movement back first, so the squares of the coming move read 0),
   drawn over the mini sliding along it; when its glide is over the route is worked out again
   from where it arrived, as the original refreshes the report after the object carried its
   orders out (`ClassicSession::movesBefore`, `MainWindow::lineSubjectGliding`). Before, the
   line came from the square the move ended on, or vanished at once when the move used up the
   object's orders. Since 2026-10-14 a turn-based turn's start selects each object as its
   moves come to be shown (§2.4, §2.7), so its line is drawn while it moves, as in the
   original; before, the moves of one call glided together and only the line of the last
   object selected was drawn during all of them. With OpenSE4's "Ship movement: all at once"
   that is still so.

Left-clicks: one object → its report in the right panel; several objects → a list; empty
space → a report about the whole system. Only visible objects count: with exactly one, its
report opens at once and its orders light up; with several, the list opens and every order
stays dim until a row is chosen (confirmed: binary). Only the left button acts in the system
panel: a right-click does nothing there, and there is no double-click action, so a
double-click on a warp point is two clicks on its sector and never shows the system it
leads to (confirmed: binary; observed, spec 07 session 6). Since 2026-10-04 our client
follows (§7 Q103); OpenSE4's optional right-click Move To still uses the right button.
With no pick armed, a left-click on a sector of a system the player has not explored does
nothing: the selection, the report, the tags and the marked sector stay. A pick armed by an
order, a window or the location keys (§3.1) completes there as anywhere else (confirmed:
binary). Our client follows since 2026-10-14.

### 2.5 Ship list / report panel

Shows either a list of everything at the selected location, or a single report (ship,
planet, storm, fleet, system). Each list row has the object's 36x36 picture, its name,
for ships the class (design) name, and for own objects a row of status icons on the
right. Clicking a row replaces the list with that object's report and enables the order
buttons; an up-arrow button at the top right of the report returns to the list. Shift+click
tags several ships (a green arrow is drawn on each) so one order goes to all of them;
the group dissolves after the order; Shift+A tags all, Shift+C clears [T]. Tagging a ship
that is in a fleet tags the whole fleet; Shift+A tags every own object in the list, with
fleets expanded; every left-click on a sector clears the tags, on the same sector too
(confirmed: binary). The orders a tagged group can take are in §2.8, and how the tagged
vehicles carry an order out is spec 03 §8 "Tagged vehicles".

**Fleets in the list** (confirmed: binary; observed, spec 07 session 7). The list holds the
objects of the sector that the player sees, except that each of the player's own fleets is
one row: its members in the sector stand as one entry (the first of them in the system's
object order), drawn as the fleet, with the fleet's picture, the fleet's name and the
fleet's status icons (§4.4), and no class line. Other empires' ships are listed one by one,
fleet members included. So a sector holding one of the player's fleets and nothing else the
player sees counts one object, and a click on it opens the Fleet Report at once (§2.4). The
rows are sorted: stellar objects (planets, stars, warp points, storms) first; then by fleet
number, highest first, vehicles in no fleet last; then by the owner's player number; then by
hull, in VehicleSize.txt order, unit groups after ships; then by name, ignoring case. A
left-click on a row opens its report (a fleet's row the Fleet Report), and so does a
right-click; Shift with the left button tags the row instead, a fleet's row tagging every
member at the fleet's location.

**The Fleet Report** (confirmed: binary; observed, spec 07 session 7). Opened for an own
fleet, it fills the panel alone: the fleet's name, Movement, Supply Pool, Fleet Experience,
Formation, Strategy and the count of Ships In Fleet, then a list of the members at the
fleet's location, sorted as the sector's list, each row with the member's picture, its name
followed by "(Fleet Leader)" on the leader's row, and its status icons. No member's own
report is shown in the panel, and the lit orders are the fleet's (§2.8). In the member list
a left-click makes that member the fleet's leader (spec 03 §9) and redraws the list; a
right-click opens the member's Ship Report as a popup window, on Detail like every report
popup. Neither changes the panel, the order buttons or the selection. The up-arrow returns
to the sector's list. Since 2026-10-05 our client follows (§7 Q110), with these choices of
its own: the stellar objects are sorted among themselves by name (inferred); a fleet member
selected any other way (the ship arrows, a Goto) shows the Fleet Report alone too
(inferred, §7 Q111); and the Fleet Report ends with the fleet's order list, as our Ship
Report does (an OpenSE4 addition).

**What replaces the report** (confirmed: binary; observed, spec 07 session 6). The panel
changes only when the player selects something:

- a left-click on a sector of the system panel;
- the Log's Goto to a location, which selects the entry's sector as a click would; an
  entry with a system but no sector empties the panel;
- Goto in the Ships\Units, Planets or Colonies windows, and the next/previous ship, fleet
  and colony buttons and keys, which show the object's system with its sector current and
  open its report (the colony buttons select the sector as a click would, so a sector with
  several visible objects gives the list);
- the game itself, which selects the current sector again at the end and the start of a
  turn, after an order to a tagged group, and while it follows a player's turn-based moves
  (§2.7).

Showing another system (the galaxy panel's left-click, the Galaxy Map's Goto System)
never changes the panel (§2.6). Since 2026-10-04 our client follows (§7 Q103).

**The report's tab is not remembered** (confirmed: binary; observed, spec 07 session 6).
Every time a report is filled for an object it opens on its first tab, Detail: on a click
in the system panel (the object already shown included), a click on a row of the sector's
list, a Goto, the up-arrow, and each re-selection by the game listed above. Only a redraw
of the report in place keeps its tab: after an order to the shown object, or when its data
change. Report popups are new windows each time and open on Detail too. The rule is the
same for every report with tabs (planet, ship, race, combat piece). Since 2026-10-04 our
client follows (§7 Q107).

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

Hovering shows the name of the nearest explored system. A left-click shows the hovered
system (the one nearest the pointer) in the system panel and changes nothing else
(confirmed: binary; observed, spec 07 session 6): no sector of it is current, so nothing
is marked and the coordinate line shows no Range; the report panel stays exactly as it was,
with the same report or list, the same tab, the same lit order buttons and the same tags,
and the movement line shows the part of the route in the new system (§2.4). So a player can
select a ship, show another system and give the ship an order with a target there. A click
on the system already shown does nothing. Going back to the ship's system this way leaves
its sector unmarked until it is clicked. Since 2026-10-04 our client follows (§7 Q103).
A right-click opens the **Galaxy Map window** (780x475): a larger map with overlay
buttons Presence, Avoid, Ally Claimed, Enemy Claimed, Spaceports and Resupply Depots,
Goto System (list of seen systems; picking one closes the map and shows it, exactly as a
left-click on the galaxy panel does, leaving the report panel alone), Show
Distances (light-year distances from the hovered system), Show Names, Close. Clicking a
system there edits its free-text player notes; the hovered system's notes show at the
bottom [T][S].

The window's map is 544x376, and Presence is the default tab. As seen in version 1.95
(observed, spec 07 session 5): the map's frame at (22,43) of the window (144,189 on a
1024×768 screen), grid lines every 8 px; under it "(click on a system to set its player
notes)" right-aligned in small #7D9FFF type; the buttons Presence, Avoid, Ally Claimed,
Enemy Claimed, Spaceports, Resupply Depots in slots 1–6, Goto System in slot 11, Show Names
(a check box) in slot 13 and Close; there is no Show Distances button. Overlays (confirmed:
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

**Show Names** (confirmed: binary) starts from an option of the empire's that Empire Options
does not show. A click on any button of the window writes the check box's state back into
it, so it is kept between openings and saved with the game (spec 08 §3.6.7).

**Notes** (confirmed: binary). A left-click anywhere on the map takes the system nearest the
pointer, explored or not, and opens a Notes window titled "Notes for" and the system's name,
or "Notes for Unexplored System" when the player has not explored it (an unexplored system is
never named, spec 01 §6.1). It holds a text box with the system's notes, OK and Cancel. OK or
Enter saves the text and marks the system's notes as changed; Cancel or Esc leaves them as
they were. Hovering writes the notes of the system nearest the pointer, wherever the pointer
is on the map, on the line under it.

**Destination mode** (confirmed: binary). Open Warp Point in the Stellar Manipulation window
(§1.3) opens the window in a mode of its own:

- the six overlay buttons are hidden; a Show Distances check box sits in slot 12, and Cancel
  in slot 14, in Close's place;
- the line under the map says that every distance is in light-years from the system under
  the pointer, and the hint asks for a click on the system to take as the destination;
- the vehicle's own system is marked;
- Show Distances starts from a second option of the empire's, kept and saved as Show Names
  is;
- a left-click anywhere on the map takes the system nearest the pointer, explored or not, as
  the destination and closes the window; Cancel or Esc closes it with no destination, and no
  order is given.

Goto System (slot 11) and Show Names (slot 13) stay as in the plain mode (inferred: the
analysis names only the changes above).

Since 2026-10-13 our client follows (`screens/galaxy_map.cpp`; the options are
`InterfaceOptions::galaxyMapNames` and `galaxyMapDistances`, save format 10). The
destination mode replaces OpenSE4's earlier pick of a sector of the destination system in
the main window. OpenSE4's own additions stay: in the plain mode its Show Distances in slot 7
(off at each opening, not saved), the legend in slots 8 to 10, and the system's name before
its notes on the line under the map. Ours marks the origin as the galaxy panel marks the
system it shows (inferred: how the original marks it).

### 2.7 Turn flow as the player sees it

1. Turn starts; the Log window opens automatically if there are new entries and the
   option is on.
2. The player selects objects and issues orders. In turn-based games movement happens
   immediately; entering a sector with enemies asks the Attack Sector question, and
   combat asks Tactical or Strategic.
3. End Turn (F12, with an optional confirmation) locks the panels while the AIs play.
4. In simultaneous games, orders are only recorded; after the host processes the turn
   the player can replay the movement log in the system panel (Ctrl+P/O/I/U).

**What a turn's start shows, in order.** The original processes the turn with the moves in
the shown system animated as they are made (§2.4), and a battle shown in a window interrupts
whatever started it (§1.10.5); the Log opens only when the player gets the turn. So a
turn-based End Turn shows, in this order: the computer players' moves and their battles; at
the start of the human's turn the Next Player window (when several empires are
human-controlled, §1.2.1), the autosave if due (§6.1), the endings (the Lose ending's test is
made before the turn's start, the destruction check at its end, spec 05 §8, §7 Q99); then the
human's own orders carried over, animated as they run, with the Attack Sector questions, the
colony-type picker (shown when a colonize order completes, §1.3) and the message boxes of
failed orders they raise (spec 03 §8); the Log last (confirmed: binary for each part; their
order as a whole inferred from the turn's sequence). Our engine runs the whole turn start in
the End Turn call, so the Next Player window comes before what that start shows, and the
autosave is written by the session before the start runs (`LiveOptions::beforeHumanTurn`). Since 2026-10-06 our client keeps
this order (`turnStartGate`, `client/classic/turn_start.hpp`; `ClassicMode`): first the moves
of the shown system glide (§2.4 "Moves as they are made"), then the battles to watch (a
network or e-mail game's, after the turn, §1.10.5), the endings, the questions and the
picker, and the Log last, each waiting for the ones before it; since 2026-10-14 the message
boxes come in their place among the moves, as the original's (§2.4 "Order"), and so does an
Attack Sector question of the turn's start in a local or hotseat game: the turn's start stops
at it, the moves before it are shown, and the groups after it move once it is answered (spec 03
§19 Q55). Before, the Log
opened at once, the ships gliding under it, and a question or the picker waited for the Log to
be closed. Since 2026-10-14 a battle that stops the engine call comes after the moves that
led to it, as the original's: the moves the call made before the stop are shown first, the
game still as before the call (ClassicSession, "Battles shown as they happen"; §2.4
"Order"); before, the battle came first.

**The view follows the player's own moves** (confirmed: binary; observed, spec 07
session 6). The main window moves its system view by itself only while a human player's
own orders are carried out in front of them, which happens only during that player's turn
in a turn-based game. No option controls it; Center Map on Current Ship (§1.10.3) concerns
the tactical map only. It applies to every vehicle or colony of that player whose orders
run, not only to the selected one:

- Before each order runs, if the object is in a system other than the one shown, its
  system is shown and its sector becomes the current sector (the one the yellow marks and
  the coordinate line's Range use, §2.4).
- Steps inside a system are drawn where they happen (slid when animation is on, after a
  turn that draws nothing, §2.4). Each run of an order that travels to a square (Move To, Move
  To Waypoint and the other orders that head for a target square) first makes sector 0, the
  top-left square of the shown system, the current sector, for every player and in every mode
  (confirmed: binary); a jump later makes the exit warp point's square current. What the
  player sees of it (observed, §7 Q123): only when the run actually moves; the moving
  object's report stays open (refreshed when its run is over) and its orders stay lit; the
  yellow marks vanish from its square when its slide starts and are drawn on (0, 0) only
  when that square is redrawn or selected; the coordinate line's Range counts from (0, 0)
  while something the player sees stands there; after a tagged group's order the tags clear
  and (0, 0) is selected once the moves are over (its object's report with the marks, or the
  system report).
- After each jump through a warp point, whether by a Warp order (one given while standing
  on the warp point included) or by a Move To whose route crosses systems, the arrival
  system is shown, the exit warp point's sector becomes the current sector, both panels are
  redrawn and the open report is refreshed: the report of the ship that jumped stays open,
  showing its new place, and as it moves on in the new system the marks stay on the
  arrival warp point. (The executable also has a per-empire pause after each jump, in
  tenths of a second, which is 0 for every empire and set by no window; it can be
  ignored.)
- Orders run at once when given to the selected object: targeted orders, order buttons
  such as Explore, Resupply and Repair, the Stellar Manipulation window, and Send Colony
  Ship in the Planets window. At the start of the player's turn the view stored with the
  empire when it ended its last turn is restored; then every own object whose list still
  holds orders is selected in turn (its system shown, its sector current, its report
  opened) and carries out its orders, the view following its jumps, so the turn opens on
  the last object that acted. A message such as a failed Colonize appears over that view
  (spec 03 §8).
- In a simultaneous game orders run only when the turn is processed and the view never
  moves by itself; the movement-log replay's Ctrl+U follows each own object instead
  (§7 Q51). A computer player's moves in a turn-based game never move the view.

Since 2026-10-04 our client follows (§7 Q102) in local, hotseat and play-by-e-mail games,
and since 2026-10-14 in a network game too, from the moves its host sends with the player's
own new states (protocol 10, §2.4 "Order"). Since 2026-10-14 the view follows as the moves
are shown (§2.4 "Moves as they are made"): each object is selected, and each jump's arrival
system shown, once the steps made before it have been shown (`MainWindow::followMove`, the
session's LiveSteps as markers in the glides' queue); before, the view went at once to where
the last object ran, and the glides of other systems ended there. With OpenSE4's "Ship
movement: all at once", the animation off, or a click or a key that shows the moves at once,
it still goes there at once. Since 2026-10-14 ours makes (0, 0) current when a move run of
the player's own moves, the marks staying where they were drawn until that square is redrawn,
and selects the current square again after a tagged group's moves (`MainWindow::followMove`'s
corner and reselect markers, `marksAt`; §7 Q123); before, the current sector stayed put.

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

The fleet's speed and its Colonize, Cloak and Decloak tests look only at the members at the
fleet's location; a member elsewhere counts for neither. Decloak is lit for any cloaked
object, whether or not it could still cloak (confirmed: binary). Our client follows since
2026-10-14 (before, a fleet counted every member and a ship's Decloak needed a working cloak).

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

### 2.9 Giving an order its target

(confirmed: binary; observed, spec 07 session 6) An order that needs a place (Move To, Warp,
Colonize, Attack, Set Patrol, the cargo and remote unit orders) arms a pick: the system panel
writes, in white and centred low in its bottom row of sectors, a request to select a target
for the order, naming it, and the next left-click on a sector of the system panel completes
it. Four orders then need one object of that sector, and the game gathers the candidates
there, in the system's object order:

| Order | Candidates in the clicked sector |
|---|---|
| Colonize | every planet, colonized or not, of any type, whether or not the ship can colonize it, and whether or not the player sees it (a planet a storm, a nebula or a colony's cloak hides is listed too); never an asteroid field (spec 03 §8, §19 Q80) |
| Warp | every warp point |
| Drop Cargo | every object of the player's own (colonies and vehicles) |
| Attack, when it becomes a pursuit (a simultaneous game, or a drone group) | every object of another empire that the player can see; for a drone group only ships, colonies and satellite groups, plus any warp point |

A system the player has not explored gives no candidate. With none, the order is not
given and the pick ends silently (a turn-based Attack, which names no target, is given
anyway). With one, it is the target at once. With two or more the **Pick Object** window
asks:

- a borderless black window, 248×253, its title at (12,8) in a 228×21 label: "Please select"
  and what (the planet to colonize, the warp connection to use, the destination for cargo,
  the target to attack);
- its top-left corner at the clicked sector's top-right corner, moved up when it would pass
  the bottom of the main window; while it is open the clicked sector is outlined with a 1 px
  #647EC7 frame;
- below the title, from (8,37) and 232 px wide down to 16 px above the button bar, a list with
  one row per candidate: the object's system-panel picture at the left and its name from x 40;
  the row under the pointer is drawn highlighted;
- a left-click on a row picks that object and closes the window; Cancel (the bar's only
  button) or Esc closes it with no pick, and then no order is given.

The picked object becomes the order's target: Colonize gives Load Cargo (population, when the
ship carries none), Move To and Colonize naming that planet; Warp gives Move To and Warp
through that warp point; Drop Cargo delivers into that object; the pursuit follows that
object. Load, Drop, Launch Remotely and Recover Remotely then ask the kind of cargo (§1.3);
for Drop the destination comes first and the kind after it (inferred: which of the two the
original asks first is not known). Nothing about the target is checked when it is picked; the order's own tests run when
it is carried out (spec 03 §8). In a turn-based game the selected vehicle's orders then run at
once.

Since 2026-10-04 our client follows (§7 Q100), the Colonize candidates included: every planet
of the clicked sector, hidden ones too, and one alone gives the order at once (spec 03 §19
Q80; `colonizeCandidates`, `pickStep`). Its prompt is still its own ("Colonize: pick a planet"
and similar, at the bottom left of the system panel). It differs in network and e-mail games:
a player's view there holds no foreign colony whose planet that player does not see (spec 05
§9.5), so such a colony is no candidate, while an uncolonized planet that a storm or a nebula
hides is (docs/PARITY_GAPS.md).

---

## 3. Hotkeys

Hotkeys are listed in the Help window's Hotkeys tab and in the Readme [T][M]. The
executable binds every key below; the five Ctrl keys marked (confirmed: binary) were
missing from the manual's lists. A main-window key works only while no window is open
and the turn is not being ended or the movement log replayed, and an order key only
when its button is lit (§2.8; the movement-log keys are the exception). The main window
has no Esc or Enter binding (confirmed: binary).

A held key repeats: the main window acts on every key-down message the system sends, so
holding Space keeps cycling through the ships and holding Ctrl+I keeps stepping the movement
log's days (inferred: the usual key-down repeat, with nothing in the handlers telling a
repeat apart). Our client differs: only the selection cycles (Space, Ctrl+N, Ctrl+B, Ctrl+F,
Ctrl+D, Ctrl+C, Ctrl+X) and the movement log's single step repeat; a held window, order or
End Turn key acts once, so that it never opens a window again, flips an order twice or ends
several turns.

### 3.1 Main window: orders

| Key | Order | Notes |
|---|---|---|
| M | Move To | then pick a destination in any system |
| Ctrl+W | Move To Waypoint | opens Select Waypoint (confirmed: binary) |
| Ctrl+1..9, Ctrl+0 | Move To Waypoint # | waypoints 1 to 9, and 10 for key 0 (confirmed: binary) |
| Alt+1..9, Alt+0 | Set Waypoint # | asks for a sector: the next left-click on the system panel sets that waypoint there (below) |
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
| L / D | Load Cargo / Drop Cargo (deferred) | pick the location, then the kind |
| I / O | Launch / Recover Units Remotely | pick the location, then the kind (a colony: the kind only) |
| Y | Sentry | when the list already starts with a Sentry, the list is cleared instead (spec 03 §19 Q76) |
| E | Explore | |
| P | Set Patrol | each point clicked is added at once; a click on the object's own sector ends it and turns on Repeat Orders (spec 03 §8) |
| K | Repeat Orders (toggle) | |
| B | Stellar Manipulation | |
| V | View Orders | |
| G | Scrap / Analyze / Mothball | |
| H | Change Formation \ Strategy | fleets |
| N | Change Name | |
| J | Jettison Cargo | |
| Z / X | Cloak / Decloak | |
| Ctrl+M | Sweep Mines | |
| Ctrl+T / Ctrl+R | Add / Remove Tagged Minefield | asks for a sector, as Alt+digit does (below) |
| Ctrl+A | Abandon Planet | |
| Ctrl+V | Convert Resources | |
| Ctrl+Z | Use Component | (confirmed: binary) |
| Ctrl+J | Use Facility | (confirmed: binary) |
| Ctrl+K | Scrap Facilities | (confirmed: binary) |
| Ctrl+Y | Toggle Minister Control | (confirmed: binary) |

**Location picks** (confirmed: binary). Alt+0..9, Ctrl+T and Ctrl+R arm a pick of a
location, with a prompt asking to select a location for the waypoint, for the tagged
minefield to add, or for the one to remove; the next left-click on a sector of the system
panel completes it. They work with nothing selected and in any system shown, explored or
not (the pick is tested before the explored test of a click, §2.4), need no lit button,
and leave the selection as it was. Alt+n sets waypoint n (key 0 is waypoint 10) at that
sector, keeping the waypoint's name, or "Waypoint n" when it has none. Ctrl+T adds the
sector to the tagged minefields when it is not one; Ctrl+R removes it when it is. Since
2026-10-14 our client follows (`MainWindow::startLocationPick`); its prompts are its own.

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
| Space or Ctrl+N / Ctrl+B | Next / previous ship: one cycle, the top selector's (§2.3) |
| Ctrl+F / Ctrl+D | Next / previous fleet |
| Ctrl+C / Ctrl+X | Next / previous colony |
| Ctrl+P | Movement log replay, full |
| Ctrl+O | Movement log replay, reload (rewind) |
| Ctrl+I | Movement log replay, single step (one day) |
| Ctrl+U | Movement log replay, the button the executable names "for all ships" (the manual says follow ship) |
| Ctrl+L | Toggle ship movement lines: flips the per-computer Display Ship Movement Lines switch (§1.9) and redraws the system panel. It is not stored at once but with the other per-computer settings the next time they are saved (a game save or autosave, closing an Options, Combat Options or Combat Replay Options window, starting a new simultaneous game); loading or starting a game before then reads the stored value back and undoes the toggle (confirmed: binary). Ours saves at once and shows a short note (§2.4). |
| Ctrl+S | Toggle sound effects (the Sound On switch of §1.9; music is not touched) |
| Ctrl+H | Show the scenario's text window again at the page shown last, in a game of a scenario whose pages were read (§5.6). Ours: during OpenSE4's lessons it shows or hides the lesson panel instead (docs/LEARNING.md); a game of the original's scenarios is never a lesson |
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
| R | Ram ship (then click the target), even with nothing selected |
| C | Capture ship (then click the target), even with nothing selected |
| E | Presses the fourth button: Begin before the battle begins, then End Turn (manual only; absent from the Readme; confirmed: binary) |
| Space or Ctrl+N / Ctrl+B | Next / previous ship with movement (the search of §1.10.1: leaders first, then any piece, then the first planet; "next" walks the list backwards) |
| Ctrl+F / Ctrl+D | Next / previous ship that can fire |
| Shift+A / Shift+C | Tick the intact weapons that are ready / untick all (§1.10.1) |

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
  empire this turn. It is the only way to answer a message, so the chance to reply ends
  when the message's entry leaves the Log (spec 05 §3.4 "Log lifetime"). Our client's
  Empires window (Treaty tab) also counts each empire's messages that wait for an answer,
  "Inbox: N waiting", an OpenSE4 line: since 2026-10-04 only those still in the Log, as
  the count stayed for the ten turns OpenSE4 keeps a message (GitHub issue #4;
  `messagesAwaitingReply`).
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
- **Picture** (confirmed: binary; observed, spec 07 session 6). Only the details pane has a
  picture, the selected entry's; list rows never have one. It is drawn at the details
  area's top-left corner at the file's own size (128×128 for the stock art), unscaled,
  opaque and without a frame. The title, in the button font, goes to its right, from
  x = picture width + 10 to 2 px short of the area's right edge, word-wrapped within the
  picture's height. "Date:" in label blue is at (4, picture height + 10) with the date at
  (40, same y), and the body is word-wrapped from (4, picture height + 30), 8 px narrower
  than the area and at most 300 px tall. A diplomatic message (unless `Use Old Log
  Political Message Display` is on) has the same picture, title and date, with its text in
  a scrolling box below. The combat layout draws its picture the same way.
- **Loading.** The picture is looked up only when the selected entry changes, resolved
  like every picture and loaded only when the file exists. When an entry has no picture,
  or its file is missing, the pane keeps the picture it showed before; when none has been
  shown since the Log opened, there is no picture and the title, date and body start at
  x 10, y 10 and y 30.
- **Items developed.** An entry for a component, facility or hull developed, or a new
  intelligence project available, has no picture, title or date in the pane: the pane
  shows that item's details as the Research window does (the item's picture and figures,
  or the project's text).

Where entries come from:

- **Random and stellar events**: Events.txt records carry title and body templates with
  tokens such as `[%VehicleName]` and `[%SystemName]`, plus a `Picture` name that
  resolves to `Pictures/Events/<Picture>.bmp` [M].
- **Intelligence**: IntelProjects.txt gives a picture for the acting side and one for
  the target side [M].
- **Pictures of the other entries** (confirmed: binary). The picture is fixed by the kind
  of entry when the entry is made and resolved when it is shown. Names below are files in
  `Pictures/Events/` unless stated otherwise.
  - A diplomatic message: the sender's race portrait (`<Style>_Race_Portrait`). First
    contact, contact lost, a treaty enacted or lost, a surrender or subjugation: the other
    empire's race portrait.
  - Anything transferred away (planet, vehicle, resources, technology): `GiftGiven`.
    Anything received (planet, vehicle, resources, technology, star charts, communication
    channels) and new system maps: `GiftReceived`. Communication channels opened: none.
  - A vehicle built: its design's hull portrait for the owner's race, as on the ship
    report, with the `RaceGeneric` fallback. A facility built, or facilities upgraded:
    that facility's picture (§5.2). A queue stopped for lack of resources: the picture of
    the planet or ship that owns the queue. Maximum ships: `AtMaxShips`. No storage for a
    new unit: `NotEnoughCargoSpace`. Launch and recover warnings: `AtMaxUnits`.
  - A vehicle scrapped, retrofitted, self-destructed or destroyed by its own side: the
    design's hull portrait; for a unit group the race's `_Portrait_FighterGroup`,
    `_Portrait_MineGroup` or `_Portrait_SatelliteGroup`, with no fallback.
  - Refused or failed orders (unable to move, load or drop cargo, cannot mothball or
    unmothball, ship mothballed, a failed stellar manipulation, planet abandoned,
    resources converted): the acting object's own picture (the hull portrait for a ship or
    base, the planet's picture for a planet, the race's group portrait for a unit group);
    for a fleet member the race's `_Portrait_Fleet`, with the `RaceGeneric` fallback. The
    exception is a failed Colonize: `OrdersNotCompleted` (spec 03 §8).
  - A colony founded: `Colony<Surface><Atmosphere>`, from the planet's surface (Rock, Ice,
    Gas) and atmosphere (Oxygen, Methane, Hydrogen, CarbDiox, Argon, None).
  - Ships lost for lack of supply, abandoned for maintenance, damaged while moving or
    warping, or damaged by a natural event: `ShipDamaged`.
  - A minefield met: `MineExplosion`. Ruins found: `Ruins`. Plague damage:
    `PlagueBreaksOut`; plague cured: `PlagueCured`. Rioting: `PlanetRiots`. Colonies
    happier or unhappy: `PopulationHappy` or `PopulationAngry`. Conditions at maximum:
    `PlanetConditionsAtMax`. Value at maximum or worse: `PlanetValueAtMax` or
    `PlanetValueWorse`. Atmosphere converted: the planet's picture.
  - Stellar manipulation completed: `PlanetCreated`, `PlanetDestroyed`, `StarCreated`,
    `StarDestroyed`, `WPOpened`, `WPClosed`, `NebulaeDestroyed`, `BlackHoleCreated`,
    `BlackHoleDestroyed`; a nebula created shows `StarDestroyed` (a quirk), a new stellar
    construction the new object's picture.
  - Research: a new tech level `NewTechLevel`; a new tech area, and all research projects
    completed, `NewTechArea`; an item developed shows its details (above).
  - Intelligence: the outcome of our project, the project's `Source Picture`; a hostile
    project's effect on us, its `Target Picture`; a counter-intelligence success,
    `IntelSabotageByUs`; all intelligence projects completed, `NewTechArea`.
  - Combat, per empire: `CombatWin` when it had pieces, some survived and no other
    empire's did; `CombatLose` when it had pieces, none survived and another empire's did;
    otherwise `CombatStalemate`. Ground combat: the planet's picture, or `PlanetRiots` when
    the planet no longer exists.
  - An empire destroyed: `PlayerDied`. A password reset: `DetectProblem`.
  - The executable knows some names that have no file in the stock install; such an
    entry keeps the previous picture (above).

  Since 2026-10-04 our client follows (§7 Q101).
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
- **Surrender**: hands the whole empire to the recipient; confirmed by a prompt. Offered
  only with Allow Surrender on (confirmed: binary).
- **Grant independence**: never offered by the original (confirmed: binary; the manual
  describes it). An OpenSE4 extension: pick a colony, which the sender abandons for the
  recipient to settle (spec 05 §3.4).
- **Demand / Request / Warn** group (non-binding, 16 subtypes plus accept/refuse
  demand): ask for a gift, tribute or surrender; ask the recipient to pull ships or
  colonies out of a system or leave a planet; to stop hostilities against, break a
  treaty with, declare war on, make peace with, or fight a third empire (optionally in a
  given system or against a given planet); to stop espionage, sabotage, or attacks in a
  system. Each prompts for its parameter (system, planet or empire); a request for a gift
  or tribute opens Select Package on the side asked for. Offered whatever the treaty, war
  included; Demand your surrender only with Allow Surrender on (confirmed: binary).
- Which types the list offers, and what it refuses to send, is spec 05 §3.4 "What a player
  may send"; the window's buttons and its answers from a portrait are §1.5 "Communicate in
  detail".

### 4.3 Other message surfaces

- **History window**: per-empire list of dated events (in multiplayer these come from a
  per-player `_events.txt`) [T]; the lines in the file's order, oldest first, under decade
  headings, the empires that appear in it in a list on the left with "Other Events" last
  (§1.5, confirmed: binary).
- **Comparisons window**: graphs from a per-player `_stats.txt` [T], its rows only (spec 05
  §5, confirmed: binary).
- **Confirmation prompts**, switchable in Empire Options (end turn, scrapping, stellar
  manipulation, deleting research/intel projects, removing the first item of a
  construction queue) (confirmed: binary; which actions ask: §7 Q28, Q46); always-on
  prompts for surrender, quitting as TCP/IP host or player [S][M]. In every Yes/No prompt, Esc and Enter mean No (§3.4).
- **Quit**: during a game, every close of the main window (its close button, Alt+F4, the Game
  Menu's Quit) asks whether to end the game, Yes or No; No keeps playing. It is not asked
  while a turn is processed, nor again once a quit was confirmed (confirmed: binary). Our
  client asks the same since 2026-10-14 (`Mode::closeRequested`, `ClassicMode::drawQuitQuestion`;
  the Game Menu's Quit has its own question), in the Next Player and Login To Game windows
  too; a script's run quits at once.
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
    green/red colonisation stars, two 8-frame 36 px explosion strips (y 64 and y 100: the
    Ground Combat window's and the system panel's minefield explosion, §2.4 "Mines"), a
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
  marker letters (inferred). Under Wine the face is Wine's own `smalle.fon`, which holds
  only an 11 px size (7 pt; ascent 9, capitals and digits 7 px tall), so there the
  lettering is drawn larger (observed, §7 Q60).
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
- **Text size** (Settings → Graphics, 0.75 to 1.5, OpenSE4's; since 2026-10-04): it
  enlarges the text that can take the room. That is OpenSE4's own text (the lesson panel,
  the manual, the Learn window, the multiplayer screens, and the questions our client draws:
  End Turn, Colony Type, Attack Sector, Next Player), and the classic windows' reading
  text, which wraps and scrolls: the Log's entries, messages, a race's description, and the
  reports of systems, stars, storms and warp points. Text at the classic layouts' fixed
  places (button and tab captions, titles, the status bar, label and value lines, list rows
  and headings, the system panel's names, the battle notice) keeps the sizes above at every
  setting, so no layout changes; where even that does not fit its box (a long name), the
  text is cut short with "…" and shows whole as a tooltip under the pointer. Settings
  itself keeps its size, so the setting can always be changed back. Before, every classic
  font grew with the setting, and from 1.2 on captions ran out of their buttons and lines
  over each other.

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

  Our client plays the TCP/IP sounds in its network games (over its own protocol): the
  host hears `openwp` and `closewp` as players connect, come back and leave, in the lobby
  too; a chat line from another player plays `cloakon` and sending one `ordbtn`, in the
  lobby and the game; `destsun` sounds when this computer's player has a turn to play (a
  new turn of a simultaneous game, a turn-based game's turn passing to it) and brings back
  the game window if it is minimized. Our client differs: its host generates the turn by
  itself once every order is in, so it has no alert for being ready to generate it; the
  host's player hears the new turn's alert instead.
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
  - OpenSE4 cuts an effect off with a fade of a few milliseconds rather than at once, fades
    a track out over a quarter second before the next one, and loops each track without a
    gap (`src/client/audio_mixer.*`), so none of these changes clicks.

### 5.6 Scenarios and tutorial

`Scenarios/` holds, per scenario `<Name>`: `<Name>_Settings.txt` (keys `Name`,
`Description`, `Starting Game` = a `.gam` file in the same folder), `<Name>_Text.txt`,
the starting `.gam`, and 128x128 BMP illustrations. Only `Tutorial` ships [M]; its
starting game is a saved game of version 1.58 (spec 08 §5.2).

The rest of this section is **(confirmed: binary)** unless marked otherwise.

**Files.** The scenario folder is `Scenarios` in the install, or the folder of that
name in the mod folder `Path.txt` names when it has one. A scenario is known by its
*stem*, the full path of its files without the suffixes. `<stem>_Settings.txt` is read
for `Name`, `Description` and `Starting Game`; only `Starting Game` is used (Name and
Description are never shown, spec 01 §12). `<stem>_Text.txt` holds the pages, kept in
the file's order; their `ID` is ignored. A file that is not at its path is looked for
under the same file name in the scenario folder. Both files are always read; errors in
either are listed in the game's usual error list, titled "Scenario Settings Errors:
<file>" or "Scenario Text Errors: <file>", and then the scenario does not start.

**Starting**, in this order: the music switches to the background list; both files are
read; the Starting Game is loaded from the scenario folder (whatever folder a picker
showed) with the normal loader, so the saved game decides the human players, the turn
style, the options and the victory conditions, and the scenario sets none of them; the
scenario flag is set, and the tutorial flag for the Tutorial only; the stem is kept as a
full path and both files are read again (loading the game cleared them); the status
bar's buttons are rebuilt, the main window shows, the pages are checked (below), and the
intro closes. Nothing ends a scenario but the victory conditions of its saved game.

**Turns.** A page's `Turn` counts game turns from the date 2400.0: it matches when
(game date − 2400.0) × 10 equals it.

**Pages that come up by themselves.** At the start of each human empire's turn (never a
computer-controlled one's) and right after the scenario starts, the game opens the first
page in file order whose Turn matches and whose `For Players` contains the current
player's number. Only that page opens, modally; the player browses the series from
there. The test is a search of the text "<n>," in the list's text with a comma added, so
player 1 also gets a page listed for player 11. A plain load shows no page; a load that
opens the Next Player screen (a hand-over file, an autosave) does, after it. In a
simultaneous game the turn processing checks once per living human empire, so the pages
come up on the host's screen; a network player's computer never shows them.

**The window**: about 416x453, without a title bar. The page's 128x128 `Image`, from the
scenario folder, at (10,10); none when the field is empty or "None". The title to the
right of the picture, or across the top without one. "Page i of n", right-aligned just
above the text, where i is the page's place in the whole file and n the number of pages
of its series. The text is a scrolling list with one entry per paragraph, each indented
three spaces; without a picture it starts at y 36. One row of three equal buttons:
Previous (the file's previous page; dim when the page's `Segment` is 1 or less), Close,
Next (the file's next page; dim on the file's last page or when the next page belongs to
another `Series`). Esc closes. On closing, the place in the file of the page on screen
becomes the "last page shown" (1 at the start), which the saved game keeps (spec 08
§3.4).

**Showing it again.** Ctrl+H opens the last page shown whenever the scenario flag is set
and the pages were read. In the tutorial only, a "T" button in the status bar just left
of the minimize button (§2.2) and a 20x20 "T" at (746,8) in every large 780x475 dialog
open it too. Other scenarios have Ctrl+H alone.

**Saved games** keep the two flags, the stem and the last page shown, and nothing about
which pages were seen (spec 08 §3.4). Loading one restores the flags and reads the files
again; loading another game clears them.

**Quirks.** Loading a game from the intro (Load Game, Resume Game) clears scenario mode
after the load: no pages, no Ctrl+H, no "T"; the Game Menu's Load keeps it. The Game
Menu's New clears nothing, so the tutorial's flag, its "T" and its pages carry into the
new game (inferred: not traced to its end).

**Ours** (`client/classic/classic_scenario.hpp`, `screens/scenario_text.cpp`):

- The intro's Tutorial and Scenario buttons open OpenSE4's Learn window, as before
  (docs/LEARNING.md). Its Scenarios tab, shown when there are any, lists the original's
  scenarios by their stem beside the mods' scenarios, the Tutorial among them, with the
  errors of their files; Start Game starts one as above. The scenario folder is the
  install's `Scenarios`, or the `Scenarios` folder of the last classic mod in use that has
  one (OpenSE4's classic mods stand for the `Path.txt` mod folder). A scenario's Starting
  Game is a conversion the player did not ask for: what its import approximated goes to
  the log file, not to a note on the screen.
- The game keeps the flags and the stem (`GameState::classicScenario`, save format 10),
  apart from the mods' scenarios (`GameState::scenario`), and the page shown last per
  empire (`InterfaceOptions::scenarioPage`), stored with Set Interface Options when the
  window closes; the pages are read again from the stem on every load.
- In a local game a page comes up for the human whose turn starts on this computer: at
  the scenario's start, at each turn start, and after the Next Player screen of a hotseat
  game (a load that opens it included). Our client differs in network and e-mail games:
  each player's own computer shows that player's page at the start of their turn (an
  e-mail turn file opening counts as one), where the original showed them all on the
  host's screen. The page opens after the Log, over it (inferred: where the original
  opens it among the turn's windows is not traced).
- In OpenSE4's lessons Ctrl+H and the status bar's "T" show or hide the lesson panel as
  before; a game of the original's scenarios is never a lesson, and there they show its
  last page. The large dialogs' "T" is the tutorial's alone.
- Our client differs from these quirks of the original: `For Players` is a list of
  numbers, so player 1 never gets player 11's page; "Page i of n" counts i within the
  series (its place among the series' pages in the file); loading a scenario game from the
  intro keeps it a scenario game; and a new game from the Game Menu (New returns to the
  intro) never carries the old game's tutorial flag.
- The window's places beyond the picture, the title and the text's top (the page line,
  the text's bottom, the buttons) are ours (inferred).

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
| `Scenarios/<N>_Settings.txt` | text | Standard record format, one record: Name, Description, Starting Game [M]. Only Starting Game is used (§5.6, confirmed: binary). |
| `Scenarios/<N>_Text.txt` | text | Records with `ID`, `Series`, `Segment` (order inside the series; previous/next browse a series), `Turn` (0 = the date 2400.0), `For Players` (player numbers), `Text Title`, `Text Number of Paragraphs`, `Text Paragraph <n>`, `Image` (a BMP in Scenarios/) [M]. Pages keep the file's order and their ID is ignored (§5.6, confirmed: binary). |
| `Path.txt` | text | One record: `Using Mod Directory` [M]. |
| `Empires/*.emp` | **binary** | 3–4 KB: the saved-game container (spec 08 §2) holding one empire record and its designs, laid out in spec 08 §13 (confirmed: binary; observed: 163 files decoded to their end). Holds a saved empire (name, race, traits, experience, password, ministers, Empire Options, strategies, designs). OpenSE4 reads and writes them beside its own empire files (spec 08 §13.6). |
| `SaveGame/*.gam` | **binary** | Same tagged/obfuscated style (the tutorial `.gam` is 75 KB), except for one **plain ASCII fixed-width summary block** near the start: the format version (`1.58`), the date, the player count, `Turn Based`/`Simultaneous`, `Same Machine`/`Different Machines`, then per player a number, empire name, emperor title and name, and `Alive` [M]. This is likely what the load/login screens read without decoding the rest (inferred). |
| `*.plr`, `*.trn`, `*.cmb` | binary (inferred) | Simultaneous play: player orders sent to the host; turn movement log (for the replay); combat log (for combat replays) [T]. Names and places in §6.1 (confirmed: binary). |
| `History/plr_<N>_stats.txt`, `plr_<N>_events.txt`, `plr_<N>_log.txt` | plain text, fixed-width columns | Per-player statistics, history events and log copy; layout in §6.1 (confirmed: binary). The manual's `<game>_events.txt` / `<game>_stats.txt` are the copies kept next to a save. |
| `Maps/*.map` | **binary** | Written by File → Save Map (and perhaps the map editor, spec 08 §11.1 Q29); read by Game Setup → Load Map. The saved-game container holding the systems, the starting points and the stellar objects, laid out in spec 08 §14 (confirmed: binary). OpenSE4 reads and writes them beside its own maps (spec 08 §14.5). |

For our engine: keep our own save format (deterministic state plus a command log, see
ENGINE.md), and read and write the original's `.gam`, `.emp` and `.map` files beside it
(spec 08: saved games §12, empire files §13, map files §14).

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
  of the turn number (ten rotating files), whenever the turn number is a multiple of the
  chosen interval (§1.9): in a simultaneous game after turn processing; in a turn-based game
  at the start of the first human player's turn of each new game turn, after its Next Player
  window and before its endings and its turn's start. The file is marked as an autosave, and
  loading it opens the Next Player window, then that player's turn starts (spec 08 §3.1, §5).
  OpenSE4 does the same since 2026-10-14 (`SaveInfo::autosave`, save format 10;
  `ClassicSession` through `LiveOptions::beforeHumanTurn`): the autosave holds the game as it
  was before that turn's start, which loading it runs again. It writes none for the game turn
  a session begins in (a new or loaded game; inferred). Before, a turn-based game was saved
  after the whole End Turn call, with that turn's start already made.
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
are OpenSE4 choices with no counterpart. Q60 (under Wine), Q73 and Q77 were answered by
observing the running game on 2026-10-01 (spec 07 session 3); only the size of Windows' own
Small Fonts (Q60) is still open. Fonts and pointers are in §5.4 and §5.8,
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
   Since 2026-10-01 the Players window switches computer control (§1.2.1), the host of a
   simultaneous game has Reset Passwords (§1.9) and the client honours the Settings.txt keys
   of §1.9 (the music rows and `Allow CD Music` included); our own choices are in Q41–Q48
   and Q83.
9. **Start-up menu (1.95).** What is the `StartMenu.bmp` window? **Answer:** a launcher
   built into the game (the 736x536 window spec 07 saw), shown only when the program
   starts without arguments, as the Steam shortcut does. Play opens the Intro; the other
   buttons open documents, web pages or the map editor. Before the intro picture come
   the publisher's and developer's logos, 4 s each (§1.1) (confirmed: binary).
   Our client's intro has the original's buttons in its order, and Resume Game loads the
   last game saved with Save Game. It differs: it has no launcher and no logo sequence
   (the launcher is optional for parity: it only opens documents); Tutorial and Scenario
   open OpenSE4's Learn window, whose Scenarios tab starts the original's tutorial and
   scenarios (§5.6); Credits shows our own credits; OpenSE4's Multiplayer,
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
    (Q55). It ends every line of the three files with CR LF on every platform, as
    Windows text files do (for the statistics and history files inferred).
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
    differs: its own overlays on the combat maps (firing lines, the capture ring) still
    use alpha. The small maps' squares and outlines are opaque since 2026-10-14 (Q122).
15. **UI sounds and music.** **Answer:** the sound-to-action table, the sound-set choice
    and the music rules are in §5.5, the fresh-install defaults in §1.9 (confirmed:
    binary). One effect plays at a time. Command and order buttons play `cmdbtn` and
    `ordbtn`. Each music switch picks one random track and loops it; the background
    track is re-picked every 5 turns; combat music starts with Tactical Combat or a
    Combat Replay and does not switch back.
    Our client follows the playback and music rules, the cloak and stellar-manipulation
    sounds and the `boom` rules of the combat maps, and its music volume is Off and five
    steps. It differs: it plays no `boom1`/`boom2` per ground combat round; a stellar
    manipulation sounds when the player's log reports it;
    a game loaded from the Game Menu keeps the music (Q53); it keeps an effects volume of
    its own. Since 2026-10-14 it draws the system panel's minefield explosion with its
    `boom1` (§2.4 "Mines"; `MineBlast`, `client/classic/ship_glides.hpp`).
16. **Tooltips and focus.** **Answer:** there are no pop-up tooltips. Hovering a command
    or order button writes its name and key as white text at the top of the system panel,
    at once, with no delay (§2.3). Every window is modal, so main-window hotkeys do
    nothing while one is open, and nothing while the turn is being ended or the movement
    log replayed. Esc and Enter close only some windows; §3.4 lists the keys of each
    kind. In Yes/No prompts Enter means No (confirmed: binary).
    Our client follows §3.4 in its prompts and, since 2026-10-04, makes every window and
    question modal as above (§1): Yes/No boxes (Y; N, Esc and Enter mean No; the
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
    Clearing also clears the queue's automatic Move To waypoint, and keeps Emergency Build
    and its turn counter (confirmed: binary; spec 02 §6.4 "Clearing a queue").
    Our client follows this: a left-click deletes an entry, only the first asking; Clear
    Queue asks and also turns off On Hold and Repeat Build and clears Move To (since
    2026-10-13); an empty queue's Clear Queue turns those off without asking; Reorder Queue opens the
    reorder list and asks "Move First Queue Item" when another entry would come first, No
    dropping the whole reorder; nothing asks in the Multi-Add queue. When the first item
    changes, its progress is discarded by queueing it again at its new place. A
    right-click on a queued ship or unit opens the design's Design Report (§1.8.3, Q56;
    since 2026-10-01, before which ours opened the hull's).
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
    Strategic Combat windows of a simulation and in the reports opened from them, and
    since the third pass of 2026-10-01 in the simulator window itself
    (`screens/simulator.cpp`): the Combat Vehicles list draws the side's 26×18 box and the
    name with its Cargo or Units and Fleet lines (`SimulatorRow::cargo`, `fleet`), the
    Owner for item and Computer Control lists the 26×18 boxes, the popup is titled
    "Empires Under Computer Control", the headings read "Items to choose" and "Owner for
    item", and the two hints are right-aligned under the lists. A colonized planet's
    picture there has no population bars (our client draws them nowhere yet). Each ship item takes its side's next numbers when it is added
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
    in words and the battle's summary help a player follow the battle, and nothing in the
    rules depends on them. Since 2026-10-03 the window has the original's layout (§1.6, the
    Tactical Combat frame) and the list is an option, off by default: "List each combat
    turn's events and the summary" under an "OpenSE4" heading in Combat Replay Options,
    kept per computer, puts it in the empty weapon grid's place, the summary at its end
    once the last combat turn has played. The places were seen at 1024×768; on an 800×600
    frame ours keeps the buttons 234 px above the bottom and the grid the rows that fit
    under the piece panel, six (inferred).
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
    All, stores All. Since 2026-10-04 it restores the scroll position only with the stored
    entry (§4.1 "Selection"); it used to restore it always, so a list whose stored entry
    was gone opened part way down, its first rows (those of the computer players' turns
    among them) above the view (looked at for GitHub issue #10). It differs only for commands a network or play-by-e-mail host
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
    Our client follows this: Scrap, Analyze, Self-Destruct and Fire On ask "Confirm Action"
    with the option on (`screens/scrap.cpp`), Retrofit, Mothball, Unmothball and the facility
    check lists never ask, and each Stellar Manipulation button asks "Confirm Action" naming
    its effect in our words. Since 2026-10-13 Construct asks for the size first and then whether to build that size
    (§1.3, `Order::planetSize`).
47. **Claims and abandoned planets.** "Claim every system we colonize" applies to human
    empires when the colony is founded (computer players work out their claims each
    turn). Facilities left on an abandoned planet go, all of them, to the next colony
    founded there by any empire. Do these match? **Answer:**
    - (Corrected on 2026-10-14, GitHub issue #18: "Empire Option 'Claim every system we
      colonize' isn't working." "I have colonized many systems and only the system I
      started in and two I claimed manually right after I started have been claimed.")
      The "automatically claim any system colonized" option works. When a Colonize order
      founds a colony and the colonizing ship's empire has the option on, the planet's
      system is claimed for that empire at once, in both turn styles, before the new
      colony's log entry. It applies to every empire, computer players included: the
      option is on for a new empire and in every stock empire file, and nothing in a
      computer player's turn changes it. Nothing else is tested: not the Politics
      minister, not the system's claims. A claim of ours stays as it is (the claim is set,
      not toggled); another empire's claim stands beside it, and the system becomes
      contested. Nothing else reads the option, so switching it on later claims nothing
      for colonies founded before, and switching it off only stops later claims. The
      earlier answer, that nothing reads the option, came from a decompiled export that
      left out the end of the Colonize order, where the read sits.
    - Systems become claimed in these ways only: the home system when an empire is
      created (a new game, a player added at setup, the empire a rebelling planet forms);
      at game setup the system of every starting planet, without condition, so with 3, 5
      or 10 starting planets the systems holding the extra ones are claimed too (spec 02
      §9); a Colonize with the option on; a click in the Borders window (which toggles the
      claim); a System item of an accepted trade or gift (the giver loses it and the
      receiver claims it, Q70); and the Politics minister's territory pass each turn (spec
      05 §7.2) for every empire whose Politics minister is on: every computer player, and a
      human who turns that minister on. That pass clears and rewrites the claims, so for
      such an empire a claim made at founding lasts until its next pass, which claims the
      colony's system anyway. Capturing a planet, receiving one in a trade or gift and the
      movement replay claim nothing; loading a game restores its claims.
    - Abandon Planet empties the population and resets the colony's anger to 25. If the
      player chose to scrap the facilities, they are scrapped with the usual refund
      (spec 02). Only when no facility remains is the colony removed and the planet left
      uncolonized. If facilities remain, the planet stays the same empire's colony, with
      no population and its facilities; nobody can colonize it (Colonize refuses because
      a colony is already there). In a turn-based game this happens at once.

    (confirmed: binary)
    Our engine follows this. Since 2026-10-14 a colony founded by an empire whose option
    is on claims its system (`colonize`, `game/movement_upkeep.cpp`;
    `InterfaceOptions::autoClaimColonized`), and game setup claims the system of every
    starting planet (`createGame`, `game/setup.cpp`); before, founding a colony claimed
    nothing and setup claimed the home system only, so a human's claims grew only by hand,
    as the report found. The territory pass claims for exactly the empires whose Politics
    minister is on, and an abandoned colony that keeps facilities stays with its owner,
    empty, with anger 25. Abandon Planet is a colony order since 2026-10-13: a
    simultaneous game carries it out during turn processing (spec 02 §5).
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
      normal game the option skips nothing. This answer was not rechecked when the claims
      that something is never read or never done were checked again on the complete
      listing (2026-10-14); it rests on the listing that cut routines short at their
      cleanup blocks, so a reader of the status may have been missed. To recheck.
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
      animated: first the turn's waits, 5° a step with 10 ms each, during which nothing is
      drawn (the vehicle stays on its square at its old heading, §2.4), then the sprite
      slides 1 px per step at its new heading, waiting at least 1 ms per step; warp jumps
      are never animated. Each entry (one
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
    works; with the animation option on, visible moves in the shown system wait out their
    turn, 5° per 10 ms, drawing nothing (since 2026-10-14; before, the mini turned), and
    slide 1 px per millisecond; Ctrl+U shows each object's system and sector
    day by day and brings back the shown system at the end. It differs:
    - Network and PBEM clients have only their own view of the game, so they rebuild a log
      from where they saw each vehicle before and after the turn: steps of one sector
      along a straight line spread over the 30 days, a jump to another system on day 15,
      vehicles no longer seen removed after day 30 (inferred). Since protocol 10 the host
      sends a player its own moves of a turn-based game with its states (§2.4 "Order"); a
      simultaneous turn's log is not sent: each entry would need the host to decide what
      the player saw at that moment of the replay, with the vehicles as they stood then,
      more than the protocol carries (2026-10-14).
    - No turn file is written: the log lives in memory, so a game just loaded has none and
      the keys say Replay Unavailable, as a notice in the system panel rather than a box.
    - Without the animation one day is shown per frame (inferred).
    - OpenSE4's "Ship movement: all at once" (§1.9) starts a day's entries together, each
      vehicle's own one after another; the default, one group at a time, plays each entry on
      its own after the one before, as above (since 2026-10-14).

    A group's step makes one log entry per member, in the group's order, and each is animated
    on its own, as a group of one with its own mini, so a fleet's members turn and slide one
    after another along the same path (confirmed: binary, 2026-10-14, Q62). Since 2026-10-14
    the engine reports the group's step once with its members (`game::MovementStep`) and the
    log makes the members' entries from it (`MovementRecorder::step`), as before.
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

    Since 2026-10-01 our client follows §1.8.3. A key is the column's identity
    (`ColonyColumn` in `colony_logic.hpp`, `shipui::ShipColumn` in `ships_logic.hpp`),
    stored as identity + 1; Name keeps the number 1 it always had, and a stored value
    that names no column sorts nothing. Every heading can be clicked, the picture's
    included, and each column sorts in the direction §1.8.3 gives it
    (`compareColonies()`, `compareShips()`); Facilities, Cargo Items, Status and Orders
    take a slot but sort nothing. The Colonies tabs have the original's columns and
    widths, the grey "type - size" line (or a red "Blockaded") under the name, the
    maximum population under the population, and Production's values in brackets when
    they are not delivered; the Orders tab shows the colony's own order list. Fleet rows
    follow every ship and unit row in the empire's fleet order, unsorted. A right-click
    on a queued ship, base or unit opens the design's Design Report
    (`DesignReportPopup`, `colony_widgets.hpp`), which the buildable list and the Combat
    Simulator open too. Our own choices (inferred): Supplies reads "now/capacity", or "now"
    alone when that is wider than the column; a
    fleet's number is its place in the game's fleet list, counted from 1; a mixed unit
    group's Type is "N designs"; the second lines are grey in the small font; "REPEAT
    ORDERS" stands for Repeat on an empty list (question 88).

Questions 60–64 are choices of ours made while implementing the fonts, pointers, the
800x600 layout and the movement log replay (inferred). Q61, Q62 and Q64 are settled from
the executable and Q63 has no counterpart; Q60 was observed under Wine:

60. **The Small Fonts stand-in.** The map numbers and letters use OpenSE4's own raster
    face: an 8 px cell with ascent 6, capitals and digits 5 px tall and 3 px wide (M, N,
    W and a few others wider), lower case 4 px tall, one blank column between
    characters. How tall and wide are the original's digits and letters on the system
    panel at 96 DPI, so the stand-in can match their size and spacing?
    **Answer** (observed under Wine 11.18 only; spec 07 session 3): Wine supplies
    "Small Fonts" from its own `smalle.fon`, which holds a single 7 pt face at 96 DPI: an
    11 px cell, ascent 9, internal leading 2. The 6 pt request gets that face, so on the
    system panel:
    - capitals and digits are 7 px tall (cell rows 2–8), lower case 5 px with 2 px
      descenders;
    - every glyph has one blank column at its left: digits 4 px of ink in an advance of 5
      ("1" 2 in 3); Y and S 5 in 6; R, C and N 6 in 7; W 7 in 8; M 8 in 9;
    - facility letters end at the right edge of the 36x36 square with the cell's bottom on
      the square's bottom (glyph bottoms 2 px above it), so "YSR" reads with one blank
      column between letters; a count starts 1 px right of the square's left edge.

    Windows' own Small Fonts at 6 pt was not measured (no Windows machine), so it is still
    open whether Windows draws the 8 px cell our stand-in assumes. Our client keeps its
    8 px stand-in as the Windows look (inferred); a player under Wine sees the original's
    lettering about 1.4 times taller. If a Windows capture shows the 11 px size too, the
    stand-in must take the measurements above.
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

    The same low-supply rule ends a Sentry order for each member (§7 Q54), so a drone
    group's threshold is also the tenth, with at least one unit. Since 2026-10-01 our client
    and engine follow this: `vehicleStatusCells()` (`client/classic/status_icons.cpp`) treats
    drone groups like fighter groups, and so do the engine's `sentry()` (`movement.cpp`) and
    the client's `lowOnSupply()` (`order_rules.cpp`), which lights the Sentry button.
    (Whether the original's Sentry button for drone groups uses this test was not traced;
    ours does, inferred.)
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
      starts, so a fleet's members wait out their turns and slide one after another along
      the same path.
    - **When an entry is animated:** only with "animate ship movement in the system
      window" on, for a move that stays in one system, when that system is shown and the
      viewer sees the vehicle.
    - **The turn.** When the bearing from the vehicle's sector to its new one, taken to
      the nearest of the 8 headings, differs from its heading, the turn comes first: the
      angle steps the shorter way (clockwise for a half-turn), 5° a frame (9 frames per
      45°), 0.01 s after each frame, but nothing is drawn meanwhile, as in the moves as
      they are made (§2.4; confirmed: binary 2026-10-14, the same turn routine): the
      vehicle stays on its square as it was, at its old heading, for the turn's waits.
      (Corrected on 2026-10-14: this answer said the mini turned.)
    - **The slide.** The vehicle is then placed in its new sector with its new heading
      (its fleet's location moves with it), its old square is redrawn without it, and the
      mini slides at its new heading 1 px a frame along the longer axis: 36 frames per
      sector at 800x600, 50 at 1024x768, diagonal steps the same, 1 ms after each frame
      (with the tick counter
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

    Since 2026-10-01 our client and engine follow this (`client/classic/movement_replay.cpp`):
    the engine reports each group's step as movement makes it (`TurnOptions::movementStep`;
    since 2026-10-14 once per group, `game::MovementStep` with its members in the group's
    order) and the log keeps one entry per vehicle and step in that order
    (`MovementRecorder::step`); each entry is animated on its own, frame by frame (5° a
    turn frame, since 2026-10-14 with nothing drawn, the vehicle held on its square,
    `MovementReplay::held`, where the mini used to turn; 1 px a slide frame, 36 or 50
    frames a sector; since 2026-10-06 by the time
    elapsed, at the original's waits divided by OpenSE4's movement speed, §1.9, §2.4, where
    each frame used to stay at least one display refresh); the engine keeps each vehicle's
    heading, saved with the game
    (`Vehicle::heading`), and the replay starts from the headings of the start of the turn,
    engineless minis facing up; a key pressed during a day's animations is ignored. One day
    per displayed frame without the animation stays an OpenSE4 choice (the original plays
    its 30 days without any pause), and so does starting a network client's rebuilt log from
    the headings it saw before the turn (inferred).
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

    Since 2026-10-01 our client follows this: the range (`map_style::coordinateLine`) and
    the selection brackets appear only while the selected sector holds an object the
    viewer sees (`MainWindow::selectedSectorMarked`, tested at every frame); the selection
    itself stays as clicked.

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

    Since 2026-10-01 the engine follows this (`diplomacy.cpp`): `executePackage` writes
    the entries per item above, in that order, with those titles and targets (a star
    chart's Goto is the system with no sector, which the Log turns into showing the
    system), and none for the package; invalid items are skipped silently; a package
    treaty logs "Treaty Enacted" (`TreatyEntry::Enacted`), which the history file reads as
    a contact line, and an accepted proposal logs nothing beyond its "Message"
    (`TreatyEntry::None`); channel items set both sides to None without checks or a
    first-contact entry; no game option is tested; the receiver claims a System item's
    system; star charts need no exploration and copy no warp links. The acceptance's
    "Message" entry follows the items' entries. Our resources item holds the three
    resources at once, so its entries name every resource it moves (inferred).
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

    Since 2026-10-01 the client and engine follow this: our empire file (format 3) stores
    the strategy list and each design's strategy and creation date, and setup restores
    them (`EmpireSetup::strategies`, `setup.cpp` "Designs an empire file brought"); Save
    Empire clears the obsolete and built marks; the file's designs replace the empire's,
    and a design that fails the design rules other than the technology test is dropped
    without a message. What remains:
    - The design statistics are not kept: in ours a built count makes a design a
      non-prototype, which a design brought back must not be (inferred).
    - The mount technology test of rule 5 is skipped with the rest of the technology tests
      (stock mounts need no technology; inferred).
    - An empire starts with no design of its own (spec 01 §3.6), so the file's designs are
      its only designs, as in the original.
    - OpenSE4 choices that stand: renaming on a name clash (spec 03 Q50), parts stored by
      name with a warning when one is missing (our own file format), and the file named
      after the empire without a picker.

    Since 2026-10-14 our empire file (format 4) also keeps the password's check, the e-mail
    address, the minister switches and their two flags, the Empire Options and window
    memories and the name lists, as the original's does (spec 08 §13.2); the copy has
    computer control off. The empire window's Save To File asks about the designs too.
    After saving, *Save for SE IV* also writes the empire as the original's empire file
    (`.emp`, spec 08 §13), with its designs on Yes. Since 2026-10-14 Game Setup's Save To File
    writes no design into either file, whatever the answer, as the original's (spec 08 §11.1
    Q26, confirmed: binary), unless the OpenSE4 option "Game Setup's Save To File saves the
    designs" (Options) is on; the Game Menu's Save Empire saves them on Yes.

Bringing the battle flow in line with Q30–Q33 (2026-10-01) left these choices of ours,
each marked "(inferred)" in the client. Q73 and Q76 are settled from the executable (what
a viewer sees of Q73 was observed); Q74 and Q75 have no counterpart:

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

    **Answer** (observed under Wine 11.18 at 60 fps; spec 07 session 3): Combat Simulator
    battles of three sides of identical escorts, 30 combat turns. With 3 escorts a side the
    window showed 8 and 7 distinct map states between Begin and Close, with "Combat Turn"
    2, 7, 11, 15, 19, 24, 28, 30 and 2, 6, 10, 13, 20, 26, 30, over 0.12 s and 0.10 s; with 6 a
    side 15 states (2, 4, 6, 8, 9, 11, 12, 14, 16, 18, 20, 23, 25, 28, 30) over 0.23 s. Every
    frame showed a new map and a new number: two to five combat turns pass between two
    60 Hz frames, no single step of a phase is ever seen, "Combat Turn 1" never shows, and
    Close lights in the frame that first shows 30. Our client, at one phase per frame,
    took 90 frames (1.5 s) for the same 3-sided battle.

    Since 2026-10-01 our client does the same: `screens/strategic_combat.cpp` (the
    window's `update`) fights on, phase after phase, for up to 10 ms of each displayed
    frame (until the battle ends, or a landing opens Ground Combat), then shows the state
    it reached, so several combat turns pass between two refreshes as on a current
    machine.
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
    `classic_mode.cpp`, shows it only in turn-based games). It takes whose turn it is
    from the battle's own copy of the game (`BattleQuestion::state`): the session's game
    stays as it was before the call, still the player's own turn when the battle came in
    a computer player's turn after the player's End Turn (until 2026-10-04 the notice was
    missing there). Since 2026-10-01 the engine matches too: with the setting on,
    `resolve()` (`combat_space.cpp`) stops a simultaneous turn at every battle on that
    machine, computer-only ones included (a Show question with no human side).

Bringing the combat windows in line with Q34–Q36 and Q38 on 2026-10-01 left these
choices of ours (inferred) (Q73–Q76 are those of the live battle flow). All are settled
from the executable; Q77's timings were measured in the running game and Q81 has no
counterpart:

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

    **Answer** (observed under Wine 11.18 at 60 fps, Fast Tactical Combat off; spec 07
    session 3). Wine's tick counter steps every 16 ms, so every 1 ms or 10 ms wait lasts
    16 ms, as on Windows:
    - one-square slide, straight or diagonal: 36 steps of 1 px, about 0.58 s (high
      confidence);
    - turns: 5° a step, 45° in about 0.15 s, 90° in 0.28 s, 180° in 0.57 s (medium-high);
    - beam: stamps 6 px apart drawn and then erased about one per step, about 0.19 s per
      square in all (1.0 s over 5.9 squares, 1.3 s over 7.2) (medium);
    - torpedo: 4 px a step, about 0.14 s per square (0.70 s over 5.4 squares) (medium);
    - a hit on structure: 8 explosion pictures about 0.11 s apart and the wipe, about 1.0 s;
    - shield-only hits (beams): no explosion, no wait, the shot ends about 17 px short and
      the target's cyan ring shimmers while the shot is drawn; no separate shield picture
      was seen (confirmed);
    - misses end 18 px off the centre in both x and y, with nothing more drawn (confirmed);
    - only a seeker impact on a survivor pauses: 0.42 s (0.1 s + 0.3 s) after its last
      explosion picture, against 0.12 s after beam hits and 0.1 s after a seeker that
      destroyed its target (one sample of the pause: medium);
    - every loss seen used the 72x72 explosion: two Space Station bases (a seeker, beams), a
      cruiser and two escorts; none used the 36x36 pictures, and none played a second
      explosion. So "some losses" is at least every ship and base of 150–500 kT.

    Since 2026-10-01 our client follows the frame counts (`CombatPlayback::framesOf` in
    `client/classic/replay.cpp`, the drawing in `screens/combat_map.cpp`): 36-frame
    slides, a seeker's step the same slide; 9 turn frames per 45°, clockwise on a tie,
    none with the movement animation off or a square out of view (`CombatPace::inView`);
    torpedoes 9 frames a square (6 with Fast Tactical Combat); beams 6 stamps a square
    drawn and then erased one by one; an explosion only for a hit that damages structure
    or destroys its target (the battle record says which, `CombatEvent::flags`), a
    shield-only hit one shield picture without a wait, the 0.3 s pause only after a
    seeker's impact on a survivor in Tactical Combat; no second explosion for a loss, no
    flash for launches and captures; misses end 18 px off diagonally, shield-only hits 17
    px short, and the target's shield ring shimmers. Each wait lasts whole 16 ms ticks
    (`kCombatTick`, the step measured under Wine), and each frame stays at least one
    display refresh (an OpenSE4 choice). What remains:
    - The engine's battle does not draw the display's random numbers (a miss's signs and a
      planet's point); ours derive them from the event's place, so a battle shown
      tactically does not change its random course (spec 04 §19.1, kept as recorded).
    - The shield shimmer shows only for hits the shields took whole, the one case where
      the record knows the shields are up (inferred).
    - Losses of ships and bases use the 72x72 explosion, other losses the 36x36 pictures
      (inferred beyond the ships and bases observed).
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
      piece (spec 04 §10.7, "A reused piece number"); since 2026-10-01 the engine does the
      same, so our report shows that new piece too.
    - **Facil** shows the colony's facilities as they are when the report opens, none
      marked lost; lost facilities stay in the list until the battle ends, so during a
      battle this is the set the colony began with. Our client matches in effect.
    - **Ability** lists, for a ship or base, its hull's abilities, then the entries its
      design or the vehicle carries of its own (none in stock), not its components'; for
      a planet only the planet's own abilities, not its facilities' or its colony's
      (corrected 2026-10-04, §1.10.1, Q106).
    - **The window** is borderless, 310×420, as every report opened on its own (the size
      in the form resource is overridden): pages at (10,10), the four 72×30 tabs at
      (10,340), and a Close button 153×30 centred under them at (79,380), its bottom
      10 px above the window's.

    Since 2026-10-01 our client follows, and since 2026-10-04 with the correction: the
    Ability tab lists the hull's entries, never the components', and a planet's own
    abilities only, each as its `Descr` line after the lamp (`pieceReportAbilities()` in
    `combat_logic.cpp`; our vehicles have no abilities of their own to add, Q106);
    `CombatPieceReportScreen` is a borderless 310×420 window with the pages at (10,10), the
    tabs at (10,340) and the 153×30 Close button at (79,380).
    The pages are 290×327 with or without tabs, as question 87 settled.
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
    setup (not its unit groups or planets) and that side's copied fleets, and a right click
    on a fleet's row there opens no Fleet Report (confirmed: binary, 2026-10-14; ours opened
    one until then, `fleet_transfer.cpp`).

    Since 2026-10-01 our client follows this (`screens/fleet_transfer.cpp`,
    `screens/ships.cpp`): the button is there in both cases and opens the real game's
    Ships\Units window (`kViewOnly`) with the tab and the three switches kept with the
    empire (`InterfaceOptions::shipsTab`, `shipsShown`, saved with the game), where a
    left-click does nothing.
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

    Since 2026-10-01 our client follows this (`combat_logic.cpp` `simulatorSandbox()`,
    `simulatorTakeBack()`; `screens/cargo_transfer.cpp`): the window is titled "Transfer
    Cargo" with Move One, Five, Ten, Hundred and All and no order buttons in every mode;
    over the simulator's sandbox the left list holds the Combat Vehicles list's rows and
    the right list the Storehouse built as above; people moved onto a ship are kept
    (`SimulatorItem::people`) and fight. In the sandbox side 1 plays every holder, which
    stands for "no owner is checked".
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
    Since 2026-10-01 `drawSideBox()` (`screens/combat_map.cpp`) draws no outline and uses
    the window's text font, kept within the box's height (inferred). (An out-of-range side
    is black in the original and grey in ours; that case never arises.)
83. **Export and ending choices (§1.9).** OpenSE4 chose, marked (inferred) in the code:
    the Weapons Report's export lists every component of the data set, researched or not,
    in tab-separated tables of our own layout named `OpenSE4_<table>.txt`; and the ending
    window shows Victory first (the game is over), then Human Dead (local and hotseat games
    with no living human-controlled empire), then Lose (the player's own empire destroyed),
    so a single player who loses everything sees Human Dead. Which components does the
    original's export list (all, or the empire's), and does a lone human's destruction
    show Lose or Human Dead?

    **Answer:** (confirmed: binary)
    - *Export.* The Weapons Report's Export button (slot 9, only with `Allow Export of Weapon
      And Component Data`) writes four plain-text files into the save folder, each on its
      own and each followed by an "Export Successful" message naming the file (or one saying
      that file could not be exported, the others still written; corrected 2026-10-14,
      complete listing), in this order: `Weapons.txt`,
      `Comps.txt`, `WeaponFamilies.txt`, `CompFamilies.txt`. They list **every component of
      the data set** in data order, researched or not, with base values (no empire, no
      mount): the two weapon files every component that is a weapon, the two component files
      every component. The files are fixed-width columns padded with spaces, each with a
      header line and a line of dashes:
      - Weapons: name (40 wide), the damage at ranges 1 to 20 (4 wide each), reload rate,
        tonnage, the three costs (6 wide each), the damage type's name (20 wide);
      - Comps: name (40), tonnage and structure (6 each), the three costs (8 each), family,
        level (roman numeral value) and custom group (6 each), then the ability names joined
        by ", ";
      - WeaponFamilies: name (40), weapon family number (15), vehicle type text (6);
      - CompFamilies: name (40), family number (15), vehicle type text (6).
    - *Endings.* Each ending is its own window, shown when a turn starts. After every End
      Turn of a local or hotseat game the order of the checks is: first "no living
      human-controlled empire" (Human Dead; the game stops there). Otherwise, for the human
      whose turn begins and who is not yet marked dead: Lose when its empire owns no colony
      and no vehicle; then Victory (victory conditions met) when the game has been flagged
      over; then Victory (conquered the galaxy) when every other empire is dead, followed by
      "Do you want to continue playing?". Several can show in one turn change. An empire is
      marked dead only after its Lose window, at the end of that last turn ("This will be
      your last turn ..."). So a lone human who loses everything sees **Lose first**, plays
      the last turn, and at the next End Turn sees **Human Dead**, and the game ends.
    Our client follows the answer since 2026-10-03: the four files and their columns (the
    alignment inside a column is ours: names left, numbers right; our header words and
    message text), and the endings at a turn's start in that order, one after another, a
    human-controlled empire marked dead at the end of its last turn (`score::checkDestruction`).
    Since 2026-10-14 a turn-based game follows Q99 (a): the destruction check comes at the end
    of a human's turn start, and the human plays that turn marked dead. After the conquest's
    question, No leaves the game for the intro, where the original exits (§1.7).
84. **Player Computer Control and Reset Passwords: OpenSE4's choices (§1.2.1, §1.9).**
    Marked (inferred) in the code:
    - Our engine keeps neutrality and the computer-controlled mark in one field
      (`Empire::kind`), so the Players window lists neutral empires but cannot switch them.
      Can the original's window hand a neutral empire to a human, and how does that empire
      then play?
    - A local game keeps no master password; a network or e-mail host's game file opened
      here keeps its host's, and a network or e-mail player's copy holds none (the host
      keeps it). The in-game
      host's Players window works on its own player's copy; it hands empires over with the
      toggle of spec 05 §9.4. Which empires' rows does the original's Game Master view list
      as switchable on a TCP/IP host?
    - On a player's copy the orders carry the player's own minister switches and the flags
      of its vehicles, fleets and colonies (the spec's "probably" for fleets and colonies).
    - A headless server has no window: a player who gave its master password may ask it for
      Reset Passwords, and the passwords come back to that player only. The original's
      headless processing has no Reset Passwords at all.
    - An empire that was a computer player from the start cannot be handed to a human with
      the TCP/IP host's toggle (its slot has no player).

    **Answer:** (confirmed: binary)
    - *Neutral empires.* The Players window lists every empire, neutral and dead ones too,
      and switches a neutral one like any other: only the computer-controlled mark changes.
      Neutrality is a separate mark that no window changes. A neutral empire handed to a
      human is then played by hand at its turns (sign-in, its own orders), but stays neutral:
      its vehicles still cannot use warp points (spec 01 §8) and every other neutral rule
      keeps applying.
    - *TCP/IP host.* Once the empires exist, the host's list has one row per empire: the
      connected players' empires and every computer player made at setup (shown as
      "[Computer]"). "Toggle Empire AI On/Off" works on any row, so an empire that was a
      computer player from the start can be handed to human control; its row then reads
      "[Host]" and the host plays it. The host's Game Master Players window (Game Menu) also
      lists and switches every empire.
    - *What a player's orders file carries.* The empire's own data (its 25 minister
      switches, its fleets with their minister flags) always; of its ships, units and
      colonies only those marked as changed during the turn. A Players-window switch sets
      the flags without marking the objects, so a vehicle's or colony's new flag reaches the
      host only if the player also changed that object that turn (an order, or the order
      panel's minister button, which marks it).
    - *Reset Passwords.* The original has no headless processing with Reset Passwords; it is
      the in-game Machine Options button of the Game Master view only (§1.9).
    Our client follows the answer since 2026-10-03: neutral empires switch and stay neutral
    (`Empire::neutral`, `isNeutral()`); the TCP/IP host's list and toggle cover every empire
    but the host's own; a player's copy sends its minister switches, the fleets' flag
    (`cmd::SetMinisters::fleets`) and the flags of the ships, units and colonies changed that
    turn (`ClassicSession::markChanged`; what marks an object changed is Q99(d), settled on
    2026-10-14). Our client differs: our headless server offers Reset Passwords to an admin (an
    OpenSE4 extension).

Implementing the movement lines (§2.4) on 2026-10-01 left these choices (inferred):

85. **The ring's pixels.** A Windows ellipse with a 1 px pen on an 8 x 8 box: ours sets the
    usual pixels of an 8 px circle (rows from the top: columns 2-5; 1 and 6; 0 and 7 four
    times; 1 and 6; 2-5). Which pixels does the original set?

    **Answer:** (observed, spec 07 session 5) The ring is drawn by the operating system's
    ellipse routine (1 px solid pen RGB(0,0,255), clear brush, box C − (4,4) to C + (4,4);
    confirmed: binary), so the game itself does not choose the pixels. Under Wine 11.18 they
    are, from the top of the 8 × 8 box: columns 2–5; 1 and 6; 0 and 7 on four rows; 1 and 6;
    2–5. Windows was not seen (inferred: the same).
    Our client matches.
86. **Movement lines during the movement-log replay.** Ours draws none while the replay
    runs. Does the original keep the line of the open report during the replay?

    **Answer:** (confirmed: binary) Yes. Starting the replay loads the start of the turn
    without refreshing the report panel, and nothing during the replay touches the stored
    route, so the route of the report open when the replay began (worked out from the
    current turn's state, before the replay) is drawn again at every redraw of the system
    panel while the days play, over the vehicles moving underneath, whenever the shown system
    holds points of it (Ctrl+U switches systems, and each system shows its part). When the
    replay ends the current turn is reloaded, the selected sector becomes sector 0 of the
    shown system and the selection is refreshed: one visible object there opens its report
    (and its route, if any); none or several clear the line.
    Our client follows this since 2026-10-03: the route is worked out when the replay
    begins, drawn while it runs, and the replay's end selects sector 0 of the shown system
    as a click would (not counted as the player's selection).

Settling Q56 and Q78 on 2026-10-01 left these choices (inferred):

87. **The Combat Piece Report's page and tabs.** §1.10.1 gives the Detail page as
    290×361 from (10,10), which reaches y 371, and the tab strip at (10,340). Is the
    page drawn under the tabs (its last 31 px hidden), or is a page with tabs shorter?
    Our client cuts a page with tabs to 328 px, ending 2 px above the strip, and keeps
    361 px for a seeker's page, which has no tabs (inferred).

    **Answer:** (confirmed: binary) The report window is 310×420 (as §1.10.1); its pages are
    290×327 areas at (10,10), ending at y 336, 3 px above the tab strip (four 72×30 tabs from
    (10,340)), with or without tabs: a seeker's page (tabs hidden) is 327 px too, and a unit
    group's page is cut to 249 px with the unit grid (108 px) at y 263 in the window. The
    Detail picture of 290×361 is drawn into the page **10 px right and 10 px down** of the
    page's corner and cut off by the page, so on screen it starts at (20,20) and its last
    10 columns and its bottom 44 rows (unit group: 122 rows) never show. Close is a 153×30
    button at (79,380).
    Our client follows this since 2026-10-03; the other tabs' pages start at the page's
    corner (inferred).
88. **"REPEAT ORDERS" in the order lists.** §1.8.3 names "REPEAT ORDERS" among the
    texts the Ships\Units Orders column sorts by, beside "None" and the joined orders,
    without saying when it is written. Ours writes it for Repeat on an empty list, and the
    orders (the current one in brackets) whenever there are any, in the Ships\Units and
    Colonies Orders columns alike (inferred). When does the original write it, and does
    the Colonies Orders column write its list the same way?

    **Answer:** (confirmed: binary) "REPEAT ORDERS" is never drawn in a list; it is part of
    the text the Ships\Units Orders column **sorts** by. That sort text is "None" for an
    empty list with Repeat off, "REPEAT ORDERS" for an empty list with Repeat on, and
    otherwise the orders joined with ", ", the current order in square brackets and
    ", REPEAT ORDERS" appended only when Repeat is on. The Orders cells themselves, in
    Ships\Units and in Colonies alike, draw one order per line (12 px apart from y −1 of the
    row, small type), no brackets, no "None" and no "REPEAT ORDERS"; an empty list leaves
    the cell empty. The Colonies Orders heading has no sort key: clicking it leaves the
    order of the rows to the earlier sort keys (the same is true of its Facilities, Cargo
    Items and Status headings).
    Our client follows the answer since 2026-10-03 (`orderListLines` for the cells,
    `orderSortText` for the Ships\Units sort text).

Following the side-by-side observation of spec 07 session 3 on 2026-10-01 (§1.11) left these
choices (inferred):

89. **The lists' arrow column.** `Buttons/Arrows.bmp` holds 24x24 list scroll arrows (§5.3). Ours
    draws its own: a 16 px column inside the list's box, a 16x16 up arrow at its top and a down
    arrow at its bottom (an outlined box with a triangle in the button colours, dim when the list
    cannot scroll that way), nothing between them; a click scrolls one row (36 px in the four
    list windows, a 16 px line in lists of text), holding repeats, and the mouse wheel scrolls too. "Pic"
    heads every picture column, in the colours of the other headings. Which cells and state
    rows of the sheet do the lists use; how wide is the column, and is it inside the list's
    box; is there anything between the arrows (a position marker, a page click); how far does
    one click scroll, and does holding the button repeat?

    **Answer:** (confirmed: binary; the thumb also observed in the setup pickers, spec 07)
    Every standard list control is a box with a 1 px frame in #647EC7; inside it the rows
    take (2,2) to (W − 28, H − 2) and a 24 px column at the right holds, from x W − 26:
    - the up arrow, 24×24, at the top (y 2), and the down arrow, 24×24, at the bottom
      (y H − 26), both from `Game/Buttons/Arrows.bmp`, a sheet of 4×4 cells of 24×24: the
      down arrow uses column 0 and the up arrow column 1 (columns 2 and 3 are not used by
      lists); the row is the state: 0 normal, 1 under the pointer, 2 held down, 3 disabled
      (dim, when the list cannot scroll that way);
    - between them a track (24 px wide, H − 52 high) with a **thumb**: an outlined box in the
      frame colour (#2D2D2D when the list is disabled), 24 px wide, its length the visible
      share of the rows (never under 6 px) and its place the scroll position. Pressing or
      dragging with the left button anywhere in the track scrolls straight to that position.
    One click on an arrow scrolls one row (whatever the row height); holding the button down
    repeats it on a 100 ms timer. Lists without the column (a flag of the control) use the
    whole box. The mouse wheel scrolls the list too, about one row per notch (observed in
    Designs, spec 07 session 5).
    Our client follows the answer since 2026-10-03, for every list (`list_widgets.cpp`). Our
    choices: a press in the track centres the thumb on the pointer and stops on a whole row;
    scroll steps are whole rows from the nearest one (Dear ImGui keeps whole pixels); the
    column's parts are UI tags for lessons and input scripts.
90. **The Colonies summary and buttons.** Ours lays the summary out as Planets' statistics
    (labels from x 18 one every 16 px, values right-aligned at x 289), with the lines Systems with
    Colonies, Number of Colonies, Number of Blockaded Colonies, Total Population (with the
    population icon), Research Points Produced and Intelligence Points Produced (each with its
    icon), Total Resources Produced and Maximum Resource Storage, the last two with their three
    amounts on the next line, the storage in whole thousands ("52500" gives "52kT"). The map is
    where Planets has it. Scrap Facil Types and Set Colony Type take slots 12 and 13. A click on
    a row selects it (Ctrl or Shift: several), a double click shows the colony in the main
    window. What are the exact labels and places (one line for research and intelligence, or
    two; "System" or "Systems")? How are storage amounts that are not whole thousands written?
    Which slots hold the two buttons? What does a click on a row do in the original, and how
    does the player reach a colony's construction queue from this window, if at all?

    **Answer:** (confirmed: binary)
    - *Summary*, in the 284×190 box at (17,38) like Planets': labels in #7D9FFF at x 18:
      "System with Colonies" (singular) y 40, "Number of Colonies" 56, "Number of Blockaded
      Colonies" 72, "Total Population" 88, "Research Points Produced" 104, "Intelligence
      Points Produced" 120 (two lines), "Total Resources Produced" 152 and "Maximum Resource
      Storage" 184. The first four values are white, right-aligned at x 289; the research and
      intelligence values are right-aligned at x 271 with their 16 px icon at x 273 (y 105
      and 121). The three resource amounts of the last two labels are on the line below
      (y 168 and 200): minerals in #4665CC right-aligned at x 68, organics in #008000 at
      x 138, radioactives in #FF0000 at x 208, each followed by its icon at that x. Amounts
      are shortened: under 10,000 as is; up to 99,999,999 as thousands rounded **up**
      (truncate(n / 1000 + 0.999)) with "kT", so 52500 gives "53kT" and 50000 "50kT"; above
      that as millions rounded up with "mT". The same shortening applies to the production
      line.
    - *Buttons:* the tabs General, Value, Production, Facilities, Cargo, Construction,
      Status, Races, Orders in slots 1–9; Scrap Facil Types in slot 12, Set Colony Type in
      slot 13, Close in 14.
    - *Clicks:* a left click on a row shows that colony in the main window and closes the
      window (as Planets and Ships\Units); Shift+click does nothing; a right click opens the
      planet report. There is no selection and no way to the construction queue from here.
    - *Set Colony Type* asks for the planet first ("Select Planet to Set", a picker over the
      rows as listed, starting at the top row); a homeworld is refused with "Cannot Set
      Colony Type"; then the colony-type picker. *Scrap Facil Types* opens the facility-type
      check list and scraps every checked type on **every** colony of the empire, at
      `Scrap Facility Percent Returned`.
    - *The colony-type picker* (confirmed: binary) is a list window of the empire's colony
      types with Add Type ("Colony Type Name") and Delete Type (spec 02 §10). Cancel sets the
      colony's type to an empty one.
    - *The window's tab* is stored with the empire's options when the window closes and comes
      back on the next opening (spec 08 §3.6.7).
    Our client follows the answer since 2026-10-03; the type list's Add Type and Delete Type,
    the plain refund of Scrap Facil Types and the remembered tab since 2026-10-13
    (`InterfaceOptions::coloniesTab`). Our choices: Total Population is written "2000M";
    Select Planet to Set is a list window of ours, the homeworld's refusal in our words; the
    colony-type picker shares the design-type picker's list window (Q94), and its Cancel
    keeps the colony's type; the facility-type check list shows each type's count and
    refund.
91. **The Log's Goto with nothing selected.** The original's Goto is lit in an empty Log. Ours
    lights it whenever no entry is selected, and a click then does nothing. Is it lit for any
    other reason, and does a click do anything?

    **Answer:** (confirmed: binary) Goto (slot 13) is created lit. Each time an entry is
    selected the buttons follow it: Goto lit when the entry has a Goto target, dim when it
    has none; Send Reply (slot 10) lit only for a political message; Combat Replay (slot 11)
    lit only when `Create Combat Replay` is on and the entry has a replay; Constr. Queues
    (slot 12) is always lit. When no entry is selected nothing is changed, so Goto keeps the
    state it last had (lit if no entry was ever selected). A click on Goto with nothing
    selected does nothing.
    Our client follows the answer since 2026-10-03 for Goto; Send Reply and Combat Replay
    still follow the selection only while an entry is selected (as before, dim with none).
92. **Research.** Ours keeps Repeat Projects and Divide Pts Evenly in slots 5 and 6, shows the
    project's progress (points paid of the cost) in the small box under each project box, and,
    having no "Players can see the complete tech tree" setting, always shows Tech Tree, in slot
    12. Which slots hold the two check boxes and, when the game allows it, Tech Tree? What does
    the small box show? With the setting off, can the Tech Tree window be reached at all, and
    what does "complete" add to it?

    **Answer:** (confirmed: binary)
    - *Buttons:* the tabs Projects 1 - 4, 5 - 8, 9 - 12 in slots 1–3; Repeat Projects
      (check box) in slot 5 and Divide Pts Evenly (check box) in slot 6; Tech Tree in slot 12
      **only** when the game's "Players can see the complete tech tree" setting is on;
      Reorder Projects in slot 13; Close in 14.
    - *Tech Tree:* the Research window is the only way to open it, so with the setting off it
      cannot be reached at all. The setting adds nothing to the window's content; it only
      decides whether the button exists. The window (tabs Tech Areas and Tech Levels, Export
      in slot 13) lists every tech area of the data set, and per level what it makes
      available; nothing is hidden, racial, unique and disallowed areas included (see *Tech
      Tree window* below).
    - *Project boxes:* the area list is at (15,56), 560×243; the four project boxes of the
      shown page are 140×130 cells side by side from (15,330). Each cell: black (a patterned
      fill under the pointer when it holds a project), its top, right and bottom edges in
      #617BC2; the area's name word-wrapped in (3,3)–(138,55) ("None" for an empty slot);
      for a project "Research Level N" (the level being bought) at (3,54) in small type,
      "Completion:" at (3,75) with the time left at (82,75) as "N.N years" (whole turns
      rounded up, at least one, times the turn length), "Cost Per Turn:" at (3,87) with the
      points this project gets per turn at (82,87). The **small box** is the cell's foot,
      outlined in #617BC2 from (1,102) to (138,128): a progress bar of up to 19 green
      (#00FF00) blocks 5×22 px, 7 px apart from x 3, y 104; the number of blocks is
      truncate(percent paid × 19 / 100), the percent being truncate(points paid × 100 /
      level cost) held to 0–100.
    - *Tech Tree window* (confirmed: binary): titled "Technology Tree"; every tech area of
      the data set under the groups' headings in alphabetical order; an area the empire can
      research now (requirements met, allowed, below its maximum) is white, every other grey
      #606060. **Tech Areas** has the columns Tech Name, Required Tech and Level, one row per
      requirement; a racial area adds a row naming the racial trait that opens it, with
      "Racial Trait" in the Level column; a unique area adds a row reading "Unique Technology"
      and "(Must Be Found)". **Tech Levels** has the columns Tech Name, Level and "Item [Number
      of Tech Dependencies]": each level lists the vehicle sizes, components, facilities and
      intelligence projects it makes available, an item's name followed by " [N]" when it has
      more than one requirement (N of them). **Export** (slot 13) writes the view shown to
      `TechAreas.txt` or `TechLevels.txt` in the save folder (§6.1), one comma-separated line
      per row, then a box titled "Export Successful" names the file (the Levels lines' format
      is the less certain part).
    Our client follows the answer since 2026-10-03 (`GameOptions::completeTechTree`, off in a
    new game; the boxes of `projectBoxes`, shared with Intelligence), and the Tech Tree window
    since 2026-10-11 (`techAreaRows`, `techLevelRows`, `techTreeExport` in
    `screens/empire_logic.cpp`). Our choices: each line of the wrapped name is centred;
    Completion reads "Never" for a project that gets no points; the Tech Tree window can also
    be opened from the manual's window links (an OpenSE4 extension); its list's places and
    column widths are ours, the description of the area under the pointer shows, the area's
    name is on each of its rows, an area that needs nothing gets one row with its name only,
    a level that makes nothing available one row without an item, and the export writes no
    heading line (inferred, §7 Q113); the files go to OpenSE4's save folder, in the user's
    data, never the install. Until 2026-10-11 ours hid disallowed, racial and unique areas, drew its own
    layout and colours, had Export in slot 4 and wrote `tech_areas.txt` and `tech_levels.txt`
    in a format of its own.
93. **The Designs list and detail.** Ours: 36 px rows with the lamp at (2,11), the 32 px
    picture at (19,2), the name at (55,2), the hull under it in grey small type and "Prototype"
    right-aligned on that line for a design never built; an obsolete design's name in grey; the
    headings in the order of the empire's design-type list, then any other type, and on the enemy
    tabs the owners' names; the note under the list in small label-blue type, in our words;
    "(Obsolete)" in yellow under Date Created; the component grid of 36 px cells, one per
    component, the mount's code in yellow on the icon. What are the rows' places, the headings'
    order and the enemy tabs' grouping, the note's place, and the grid's cell size; do identical
    components share a cell?

    **Answer:** (confirmed: binary)
    - *Buttons:* tabs Ship Designs, Unit Designs, Enemy Ship Dsgn, Enemy Unit Dsgn (slots
      1–4); Create, Copy, Edit, Upgrade, Make Obsolete (6–10); Hide Obsolete and
      Stats\Strategy check boxes (11, 12); Simulator (13); Close (14). The enemy tabs switch
      Stats\Strategy off. Create and Stats\Strategy are dim on the enemy tabs; Copy, Edit,
      Upgrade and Make Obsolete are dim there and with nothing selected; Edit is dim for a
      design that has been built, and lit for one in a construction queue, where a click
      shows a "Cannot Edit Design" box saying that a design in a queue cannot be edited.
      The window opens on the tab kept with the empire's options (spec 08 §3.6.7).
    - *Which designs:* Hide Obsolete applies to the own tabs only. The enemy tabs list every
      design of another empire seen at least once, obsolete or not, leaving out the designs
      of eliminated empires.
    - *List:* "Designs" at (16,39) and "Design Detail" at (269,39) in #7D9FFF; the list at
      (16,58), 246×393. Headings are rows 25 px high, their text in the large font, silver
      #C0C0C0, at x 4 resting on the row's foot. On the own tabs the headings are the design
      types' names, on the enemy tabs the owners' empire names, each list **sorted
      alphabetically** (no repeats); under each heading the designs sorted alphabetically by
      name. Design rows are 36 px: the lamp (General.bmp, 13×13; the selected design's green,
      the others' blue) at (4,11); the hull's picture at (21,0); the name in white at
      (61,−2); the hull's name in small type at (71,12); "Prototype" at (71,23), under the
      hull, for a design never built, on every tab, the enemy tabs included (confirmed:
      binary, 2026-10-14; ours showed it on the player's own tabs only until then; a
      player's view of another empire's design has no statistics, so ours reads "never
      built" there from the design's built-at-least-once mark, `Design::everBuilt`). An
      obsolete design looks like any other.
    - *Note:* "(obsolete designs are deleted automatically)" in small #7D9FFF type at
      (16,450), just under the list.
    - *Detail:* a box outlined in #617BC2 from (269,58) to (573,461). Its top part (302×150
      from (270,59)): the picture frame (#617BC2) from (4,4) to (134,134) in it, the name in
      the large font at (140,4), then in #7D9FFF / white: "Size" at (140,40) with the value at
      (152,55), "Design Type" (140,70) / (152,85), "Date Created" (140,100) / (152,115),
      "(Obsolete)" in yellow #FFFF00 at (202,115); "Cost" at (4,135) and "Maintenance Cost"
      at (4,150) with their three amounts. The part below (from (270,209)) holds Movement,
      Shields, Cargo Space and Supply Capacity (labels at x 4, values right-aligned at x 160,
      15 px apart), "(Insufficient technology)" in grey #A0A0A0 when it applies, and
      "Components on Design" at (274,291) over the component grid. With Stats\Strategy on the
      grid is hidden and the part shows Number Constructed, Number In Service, Number Lost,
      Number Scrapped and Enemy Tonnage Destroyed (labels at x 6, values right-aligned at
      x 270, 15 px apart from y 97 of that part) and "Default Strategy" with its picker.
    - *Component grid:* at (276,308), 290×146: 36×36 cells, 8 to a row, 4 rows; **one cell
      per component** (identical components do not share a cell here); a 64×34 pair of
      up/down arrows (`BigUpDownArrows.bmp`, 64×17 each) at (501,274) shows when it scrolls.
    Our client follows the answer since 2026-10-03. Our choices: the letter case is ignored in
    the alphabetical order; the hull's picture is drawn 36 px; the three amounts of Cost and
    Maintenance Cost right-aligned at x 172, 225 and 278 of the top part, each with its icon;
    an enemy design shows its owner in place of its design type; the default strategy is a
    drop-down; an obsolete design's button reads Make Current; a design just made is selected
    and scrolled into view, and our notes (a design made, a refusal) go in the title strip.
    Hide Obsolete and Stats\Strategy are kept with the empire's options in the saved game, as
    the original keeps them (spec 08 §3.6.7), so the window opens again as it was left: ours
    since 2026-10-04 (`InterfaceOptions::designsHideObsolete`, `designsStatsView`, save format
    8; GitHub issue #1), where v0.9.0 forgot them at every closing. Since 2026-10-11 the tab
    is kept too (`InterfaceOptions::designsTab`, save format 10, carried in the original's
    saves), the buttons are lit as above, and the enemy tabs list as above; ours hid enemy
    designs their owner had made obsolete and listed eliminated empires' designs, lit
    Create and Stats\Strategy everywhere and dimmed Edit for a design in a queue. Ours still
    opens on the tab of a design it was opened for, and writes "(Obsolete)" only for an own
    design (whether another empire made a design obsolete is its own business; inferred,
    §7 Q113).
94. **Create Design.** Ours: the vehicle-type picker is a list window titled "Select Vehicle
    Type", the same from every tab; the designer is a 780x475 window. Places in its content
    area: the picture at (3,3); Size, Design Type and Design Name at x 140 with their boxes
    190 px wide and 20 px arrow buttons; the figures box (358,3)-(553,131); "Components on
    Design" at y 137 over a strip of 36 px cells between 16 px arrows; "Components Available" at
    y 197 over the tile grid (366 px wide, 36 px rows, the list arrows moving a row at a time);
    Warnings at (375,197) and Component Details at (375,309). A new design starts with no name
    and our usual design type. The warnings are yellow (§5.4). Comp Type and Weap Mount are plain
    buttons opening their lists; Weapons Report is in slot 7; with To Hit Modifiers on, a
    weapon's tile shows its to-hit modifier with the chosen mount in place of its size;
    Condensed View shows each component once in the strip with its count. Our Clear Design
    button is gone. What are the original's places and sizes, the picker's title and its list
    from the Unit Designs tab, the starting design type and name, the warnings' colour, the slots
    of Weapons Report, and what do To Hit Modifiers and Condensed View change?

    **Answer:** (confirmed: binary) Positions are in the window (the 780×475 frame of the
    other main windows).
    - *Picker:* Create opens the same list window from every tab, titled "Select Vehicle
      Type", one "Name" column listing the vehicle types (of the eight) for which the empire
      has a researched hull size. The designer is then titled "<vehicle type> Design" ("Ship
      Design", "Base Design", ...).
    - *Buttons:* Comp Type (slot 1), Weap Mount (2), To Hit Modifiers (8, check box), Condensed
      View (9, check box), Only Latest (10, check box), Weapons Report (11), Create Design
      (13), Cancel (14).
    - *Picture:* 131×131 at (19,40), the hull's picture drawn from (1,1).
    - *Selectors:* labels "Size" at (155,39), "Design Type" (155,79), "Design Name" (155,119)
      in #7D9FFF; under each a drop-down box 185×20 at x 160, y 55, 95 and 135; only Design
      Name can be typed in. A design without a name shows the words "Design Name" in its box.
    - *Figures box:* 223×140 at (349,40), framed in #617BC2; lines 16 px apart from y 4:
      "Space Used" (used/total), "Total Cost" over three lines (minerals #4665CC, organics
      #008000, radioactives #FF0000, each with its icon at x 174), "Movement", then
      "Shields", "Cargo Space", "Supply Capacity"; labels at x 4, values at x 120.
    - *Components on Design:* label at (19,181), hint "(click to remove component)"
      right-aligned to x 573 at y 184 in small #7D9FFF; the strip at (25,198), 536×38.
    - *Components Available:* label "Components Available (<type>)" at (19,238); hint
      "(click to add component)" right-aligned to x 573 at y 240; the tile grid at (19,254),
      550×114: tiles 173×38, 3 per row, 3 rows visible.
    - *Warnings* label at (19,369) over a list at (19,385), 283×76, 3 rows; *Component
      Details* label at (309,369) over the details box at (309,385), 264×76.
    - *To Hit Modifiers* (an empire option) changes the figures box: instead of Shields, Cargo
      Space and Supply Capacity it shows "Offense Bonus" and "Defense Bonus", the design's
      combat to-hit bonuses as signed percentages ("+10%").
    - *Condensed View* (an empire option) merges identical components (same component and
      mount) into one cell of the strip, with the count written on it when above 1. It also
      reorders the design itself: when the box is ticked, and after each component added
      while it is on, identical entries move next to the first of their kind, the order
      otherwise kept. Weapons fire in the design's order (spec 03 §4.1), so this changes a
      battle's firing order.
    - *Size* opens a list window, "Select Vehicle Size": the empire's researched hulls of
      the vehicle type in data order, columns Pic, Name, Size (the tonnage) and Cost (the
      minerals, organics and radioactives in their colours), with an "Abilities" box under
      the list for the selected hull. Cancel leaves the design with no size. Without a size
      the box reads "Vehicle Size", and a click on a component does nothing.
    - *Comp Type* opens "Select Component Group": the General Groups of the available
      components with "All" sorted in among them, the letter case ignored; picking nothing
      means All. *Weap Mount* opens "Select Weapon Mount": "Normal Mount" first, then each
      mount's name with its description in grey under it, and a description pane under the
      list. The label reads "Components Available (<group>)"; the mount's name is drawn
      apart, to its right, at x 319.
    - *Names and Create Design:* the Design Name list shows every line of the name file in
      file order, names in use included; without a file it is empty. The list reads the name
      file each time the Design Name list window opens, from the designer's Design Name
      button, and only then: when the empire has a names file set that cannot be opened, a
      "Load Error" box says so and names the file (confirmed: binary, 2026-10-15; not at
      startup, not when the designer opens, not when a name is proposed). Ours since then, in
      our own words, showing the box instead of the empty list (inferred). A design without a name
      holds the words "Design Name" in its box, so a design made without typing is named
      "Design Name", and an erased name is accepted as empty. Create Design (the button keeps
      that caption when editing) refuses a design with any warning in a box titled "Design
      Warnings" (it cannot be made while it has warnings); after that the name is checked,
      exactly, against every design in the game, and a clash gets a box of the same title
      saying the name is in use. The name is never one of the warnings.
    - *Start and warnings* (observed, spec 07 session 3 capture of a new Ship Design,
      measured in session 5): a new design has no design type, its box showing the words
      "Design Type" as the name box shows "Design Name". Each warning is a line of white
      text after a small red ball, 18 px apart from the list's top, one per requirement the
      design does not meet yet.
    Our client follows the answer since 2026-10-03 (To Hit Modifiers and Condensed View kept
    with the empire, `InterfaceOptions::designToHit`, `designCondensed`; the bonuses from
    `game::designToHit`), and the reordering, list windows, names and Create Design checks
    since 2026-10-11 (`condenseEntries`, `designNameClashes` in `screens/design_tools.cpp`;
    `screens/design_pickers.cpp`). The tiles of Components Available are all drawn alike,
    nothing greyed whether the component fits or not, with a thin separator line between the
    picture and the text (confirmed: binary, 2026-10-14; ours since then, the line in the
    frame colour, inferred; before, ours dimmed the tiles that did not fit). Our choices: the
    strip's arrows are the small left and right arrows (`SmallLeftRightArrows.bmp`), 16 × 38,
    at its ends; a tile holds the 36 px picture, the name and the size in small type; the Component Details box shows our compact
    report; a design opened while Condensed View is on is reordered only at its next added
    component, the strip merging its identical entries meanwhile; the size list's Abilities
    box shows the hull under the pointer, else the design's, a design being edited keeps its
    hull listed, and Cancel clears a size chosen before (inferred, as in Design Type's list, §7 Q113);
    Cancel in "Select Weapon Mount" picks Normal Mount, and the mount's name at x 319 is white
    and reads "Normal Mount" without one (inferred, §7 Q113); blank lines of a name file are
    left out; a mount's figures show under the pointer in its list. **Our client differs:**
    the words "Design Name" are only a hint in an empty box, and Create Design refuses an
    empty name in the "Design Warnings" box, so no design is named "Design Name" or nothing
    by default. Until 2026-10-11 ours listed name problems among the warnings, showed a
    refusal as a warnings line, let components be added before a size, read "Save Design"
    when editing, offered only the unused names of the list (or "<hull> N" without one),
    merged Condensed View on screen only, and used plain pop-up lists.

    *Height* (GitHub issue #14, on 0.12.0 at 1920×1080: "Could the Ship and Unit Design
    screens be stretched some so that the Component Details section is a bit more visible? I
    have to scroll to see pretty much anything in it."; the player adds that some scrolling
    would be fine). **Our client differs:** since 2026-10-14, at 1024x768 the designer is as
    tall as the four list windows (the frame's height less 100, 668 px, §2.1.1), and the
    193 px it gains go to the Warnings and Component Details boxes, both 269 px tall from
    y 385, so a component's compact report (a weapon's damage by range included) shows
    without scrolling; everything above them keeps the places of the answer. At 800x600 it
    keeps the original's 780×475 and its 76 px boxes, which scroll. It keeps a large
    window's T button in the original's tutorial (§5.6), and the damage-by-range rows of
    the report hold five ranges where ten do not fit (`DialogSize::LargeTall`,
    `CreateDesignScreen::sideBoxes` in `screens/designs.cpp`; `damageTable` in
    `screens/item_reports.cpp`).

    *Design Type* (confirmed: binary): the box opens "Select Design Type", a list window of
    the empire's design types with one "Name" column, Add Type, Delete Type, OK and Cancel.
    Add Type asks "Design Type Name" and adds the name at the end when it is not empty, not
    already listed and the list holds fewer than 250; otherwise nothing happens and nothing
    is said. Delete Type removes the selected type without asking and without looking at
    the designs that use it. The list belongs to the empire and is saved with the game.
    Cancel, or OK with nothing picked, leaves the design with no type (its box reads
    "Design Type"). Since 2026-10-13 our client follows, with the colony-type pickers' list
    window (spec 02 §10, `TypeListPicker`): a click selects a row, and OK, a double click or
    a click on the row already selected picks it; the changes go out as
    `cmd::SetDesignTypes`. Our client differs: Cancel keeps the type the design had.
95. **Empire Status.** Ours draws one table per block under its silver heading, the amounts in
    the resource colours, each block's first row with the resource icons, Net Resources Per Turn
    as a block of one unlabelled row. Our engine's other income (resources generated by
    abilities, the income floor and the computer players' bonus) is added to From Our Colonies.
    Our engine keeps no e-mail address for an empire (the original keeps one and carries it in
    the orders file, spec 05 §9.2), so Change Email is dim; the missed-turn switch is in
    Ministers. Where does the original count those incomes? What colours do the amounts and
    the net take, and which rows carry icons? What does Change Email ask for, and where is the
    address used?

    **Answer:** (confirmed: binary)
    - *Layout:* block headings in the large font, silver #C0C0C0, at x 20: "Resource
      Production Per Turn" y 40, "Resource Expenses Per Turn" y 180, "Resources in Treasury"
      y 350. Row labels in #7D9FFF at x 30: From Our Colonies 60, From Trade 80, From Tariffs
      100, From Remote Mining 120, Total 150; Tariffs 200, Maintenance Cost 220, Construction
      Queue Usage 240, Total 270; Net Resources Per Turn 310 (a labelled row of its own under
      the expenses block, not a block); Current Total 370, Maximum Resource Storage 390. A
      #7D9FFF rule from (330,145) to (570,145) and from (330,265) to (570,265) over each
      Total.
    - *Amounts:* three columns right-aligned at x 380, 460 and 540, in the resource colours
      (minerals #4665CC, organics #008000, radioactives #FF0000), written in full (not
      shortened). Only the first row, From Our Colonies, carries the resource icons (after
      each number). The net amounts are in the same colours, a positive one with "+".
    - *Incomes:* From Our Colonies is the colonies' production total (the same figure as
      Colonies' Total Resources Produced); for a computer-controlled empire it is shown
      multiplied by the computer players' bonus. From Trade, From Tariffs and From Remote
      Mining are their own sums; the Total is the four added. Income from abilities and the
      minimum-income floor have no row of their own (inferred: inside the colonies' total,
      as spec 02 counts them in production).
    - *Buttons:* Empire Options, Ministers, Systems to Avoid, Waypoints, Strategies, Repair
      Priorities (slots 1–6), Change Email (12), Change Password (13), Close (14).
    - *Change Email* opens an input box "Please Enter Email Address" holding the current
      address; OK stores it as the empire's address (the password is kept). The address is
      entered in Empire Setup's "Email" field, saved with the empire in the game file and in
      every orders file, and shown back there; the game itself never sends mail (it has no
      mail code), so the address is for the players' own use.
    Our client follows the layout since 2026-10-03; our engine keeps the income of abilities,
    the minimum-income floor and the computer players' bonus apart (`otherIncome`) and adds it
    to From Our Colonies, where the original's colony total holds it (inferred). Change Email follows the answer since 2026-10-03 (`Empire::email`, set in Empire Setup's
    Email box and by `cmd::SetEmail`, saved with the game); the address travels in a player's
    orders only as that command, empire files do not keep it, and a player's view and the
    network lobby leave out other players' addresses (OpenSE4 choices: no window shows another
    player's address).
96. **Borders.** §1.5 lists Borders as a window reached from Empires. Ours keeps it as a view of
    the Empires window that the plain Borders button shows and hides (a tab returns to the
    portraits too). Is it a window of its own in the original, and what are its layout and
    buttons?

    **Answer:** (confirmed: binary; observed, spec 07) A window of its own, titled "Borders",
    in the same 780×475 frame, opened over Empires by its Borders button; closing it returns
    to Empires. Places in the window: "Empires" at (17,43) over a narrow list (18,52), 70 px
    wide and as high as the map, one row per known empire with a check box and the empire's
    flag; "Systems claimed by Empires" at (94,43) over the galaxy map at (94,52), 476×329
    (grid and system circles, the systems claimed by the checked empires in their colours,
    a system claimed by more than one in yellow); the hint "(click on a system to claim it for
    your empire)" in small #7D9FFF, right-aligned to x 569 at y 390; "Legend" at (94,394)
    and a yellow dot with "Contested" under it. A click on a system claims it for the
    player's empire, or gives the claim up. Buttons: Select All (slot 1: checks every
    empire), Allies (2: the player and every empire it holds Non-Aggression or better with),
    Enemies (3: every empire below Non-Aggression), Us (4, lit at opening: the player only),
    slots 5–13 empty, Close (14). (Allies and Enemies by treaty: confirmed: binary, 2026-10-11
    audit; ours took Military Alliance and up as allies until 2026-10-14.)
    Our client follows the answer since 2026-10-03 (the window `borders`). Our choices: the
    list's rows are 20 px with the small flag; any system can be claimed by a click, explored
    or not; the filter buttons act as tabs, the last one pressed lit.
97. **Tactical Combat.** Ours: the weapon grid's 36 px cells at (W−232,104), 6x6; the target
    panel, 216x64, at (W−232,324); each panel shows the 64 px picture with the owner's mark and
    the group badge, the name at x 67, "Size" and "Move" (the target: "Dist") in small type at
    y 17 and 28 with the values at x 101, and the shield and damage bars at y 42 and 52 (the
    target's numbers at x 162); the four buttons right-aligned with the overview map, from
    x W−238; the navigation buttons 20x20 at (W−84,7), (W−62,7) and (W−40,7), the previous and
    next of the side's pieces that can still move or fire, the stop button between them
    clearing the selection; a simulation's battle titled "Tactical Combat"; before Begin the
    panel shows the player's first piece; a refused order's reason in yellow at the map's top
    left. What are the original's places and contents of the panels and the grid, the buttons'
    x, what do the navigation and stop buttons do, how is a simulation's battle titled, and
    what does the panel show before Begin?

    **Answer:** (confirmed: binary) With W the window's width and H its height:
    - *Current piece panel:* 216×64 at (W − 232, 36), 2 px above the map's top (y 38); a
      click on it opens the piece's report.
    - *Weapon grid:* at (W − 232, 102), 216×217 (36 px less when the screen is narrower than
      1024): 36×36 cells, 6 to a row, 6 rows; the cells also show the weapon's to-hit
      percentage. Its up/down arrows (`BigUpDownArrows.bmp`, 64×34) sit just under it, at
      (W − 232, 319).
    - *Target panel:* only when the screen is at least 1024 wide: 216×100 at (W − 232, 373),
      that is the grid's bottom (y 319) plus the arrows' 34 px plus 20.
    - *Buttons:* a 2×2 group of 113×30 buttons, 226 wide, at (W − 234, H − 266), 70 px above
      the overview map (218×190 at (W − 230, H − 196)): Options (top left), Orders (top
      right, dim until Begin), Auto (bottom left, a toggle), Begin (bottom right, which
      becomes End Turn).
    - *Navigation:* four 24×24 buttons in a row at (W − 146, 5), left to right: next piece
      that can still move (as Space and Ctrl+N), previous one that can still move (Ctrl+B),
      next piece that can still fire (Ctrl+F), previous one that can still fire (Ctrl+D).
      Then a single 20×20 button at (W − 34, 7) that asks the window to close, which it
      refuses while the battle is not over (the battle is stopped from Options). The
      buttons do nothing while an action is being animated.
    - *As seen* (observed, spec 07 session 3 captures of a simulation at 1024×768, measured
      in session 5): a simulation's battle is titled "Tactical Combat" in the title strip.
      Before Begin the piece panel is empty (no picture, name or bars), the weapon grid
      shows empty cells, Orders is dim and the fourth button reads Begin. The navigation
      buttons are drawn as two groups of 46×22 at (W − 145,6) and (W − 97,6), each a left
      arrow, a 22 px picture (a ship for the pieces that can move, a red crosshair for those
      that can fire) and a right arrow; the closing button is a 20×20 box with a bar at
      (W − 34,7). With a piece selected the panel shows its picture in a 38×38 frame at
      (W − 230,38); its name in white at (W − 190,40); "Size" and "Move" in #7D9FFF small type
      at (W − 185,59) and (W − 185,70) with the values in white at x W − 141 (the hull size's
      name, and the moves left over the moves of the turn, "0/4"); the side's 26×18 numbered
      box at (W − 42,39); and a bar frame (#4F65A2) from (W − 230,78) to (W − 28,97) holding
      the shield row (upper) and the structure row (lower, red blocks 3×7 px, 4 px apart from
      x W − 228, y 89–95).
    - *Target panel and a real battle* (observed, spec 07 session 5): pointing at an enemy
      piece fills the target panel: picture frame 38×38 at (W − 230,375), name at
      (W − 190,377), "Size" and "Dist" (the distance in squares) at (W − 185,396) and
      (W − 185,407) with the values at x W − 141, the owner's flag at (W − 42,376), and the
      bar frame (W − 230,415) to (W − 28,434) with the shield row in blue (#0000FF) over the
      structure row in red, no numbers. In a real battle the flags replace the numbered
      boxes, and a planet's panel writes its structure in thousands in red at the bar's
      right end ("21k").
    Our client follows this since 2026-10-03, with these choices (inferred): the grid's
    cells, empty ones too, drawn with 1 px #617BC2 lines; its arrows shown only when the
    piece has more weapons than cells (as in Designs, Q93); the navigation selectors in
    their disabled row before Begin and while no side of the player is in its phase; a row
    of blocks lights one block for every 2 % left, a part rounding up; a planet's thousands
    on a black ground so they read over the blocks; the group badge (8×8, blue a leader, red
    a member) on the picture's top right corner; a destroyed weapon drawn red, and a unit
    group's identical weapons sharing a cell. Since the parity audit of 2026-10-11 the
    weapon cells, the target panel and its damage line follow §1.10.1 (the details left
    open are spec 04 §19.6 Q92, Q93); the cells no longer show a weapon switched off, the
    ready count or the chance across the middle.
98. **The report's up-arrow button.** Ours draws `DetailUp.bmp` (state rows normal, under the
    pointer, held; the fourth unused) at the report panel's top right, 35 px from its right edge
    and 2 px below its top, whenever the shown object's sector holds more than one object.
    Where exactly is it, which row is which state, and does it show only when the report was
    reached from the list?

    **Answer:** (confirmed: binary, except where marked) Every object report (290×361) has
    the button: a single 33×21 cell at the report's top right, from (257,0) in the report,
    so flush with its top and right edges. `Game/Buttons/DetailUp.bmp` is 33×84, four rows of
    21: row 0 normal, 1 under the pointer, 2 held down, 3 disabled. A click makes the shown
    object's sector the selected sector of the main window and selects it again: several
    visible objects there bring the list back, one opens the same report. The button is
    created hidden. It is shown when the report was opened from the sector's list (a click
    on an entry of the list a sector with several objects shows); a report opened by a click
    on a sector with one object, or set up by the game at a turn's start, has none (observed,
    spec 07 session 5).
    Our client follows this since 2026-10-03; it draws the disabled row while the movement
    log replays, when the selectors are disabled too (inferred).

Implementing Q83, Q84, Q88–Q96 and the session 5 windows on 2026-10-03 left these choices
(inferred):

99. **Endings, targets and changed objects.** (a) A human who loses its last colony and
    vehicle during its own turn of a turn-based game: ours marks the empire dead at the end of
    that turn, without a Lose window at a turn's start first. Does the original wait for the
    next start of its turn? (b) After the conquest's "Do you want to continue playing?", No
    leaves the game for the intro in ours. What does the original do? (c) An intelligence
    project that needs a target asks for the empire, then for the planet, ship, technology
    area or third empire, in list windows of ours; each box of the page shows "vs <empire>"
    under the project's name. What are the original's pickers and the box's lines for an
    intelligence project? (d) "Changed during the turn" (Q84) is, in ours, a ship, unit group,
    fleet member or colony given orders, or whose minister button was pressed. Do other
    commands (a rename, a construction queue change) mark an object too?
    **Answer to (d)** (confirmed: binary): the original marks an object changed during the
    turn
    - whenever an order button or key's handler runs for the selected object (for each
      tagged object), at its end: Change Name, View Orders, Fleet Transfer, Cargo Transfer,
      Scrap, Toggle Minister Control and Cloak or Decloak included, and merely arming a
      targeted order, even one then abandoned; only a dim button or no selection marks
      nothing;
    - when its order list changes: orders added, cleared or repeated, a fleet's orders
      marking each member;
    - when a ship's or fighter group's fleet changes;
    - for a construction queue, when Set Construction Queue is opened for the object by any
      route (even with no change), when items are added and by Multi-Add;
    - when a colony's type is set (Planets, Colonies), by the Scrap window's actions, by Cargo
      Transfer and by launching or recovering units.
    A system's notes have a "notes changed" mark of their own, set by OK or Enter in the
    notes window.
    Since 2026-10-14 our client follows (`ClassicSession::markChanged`): the main window marks
    the selected object, or each tagged one, after every order it runs (`MainWindow::runOrder`),
    and the commands that change an object's orders, fleet, name, minister flag, cloak,
    construction queue, colony type or cargo, and the Scrap window's, mark it as they are
    given. Opening Set Construction Queue from the Construction Queues window marks nothing
    until a change is made there (inferred, a simplification of ours); our system notes travel
    as commands of their own.

    **Answer (a), (b)** (confirmed: binary): (a) No destruction check runs for a human at the
    end of its own turn: one that lost everything during it is alive while the others play. At
    its next turn's start, in order: the Next Player window (when several empires are
    human-controlled), the autosave if due, the Lose ending (when it owns no colony and no
    vehicle), the turn's start (orders carried over, ministers, Log); then the destruction
    check destroys it at once (the others are told, its treaties end, it is marked dead), and
    it plays that last turn marked dead. The spec's earlier "marked dead at the end of that
    last turn" was not right for turn-based games. (b) No ends the game: the program exits
    (§1.7). Our engine follows (a) since 2026-10-14 (`turn_based.cpp` `startPlayerTurn`,
    `PlayerTurn::loseEnding`, `playsLastTurn`; spec 05 §6, §8), and our client returns to the
    intro for (b).
    **Answer to (c)** (confirmed: binary): spec 05 §2.1 "Adding a project" and "A project's
    box". Intelligence Defense takes no target; every other project asks for the empire in
    "Select Empire" (no "Any"), then, by its type, in "Select Ship", "Select Planet",
    "Select Technology" or a second "Select Empire", each starting with an "Any ..." row; the
    box shows the target picked (or "Any Ship" and the like) at y 40 and "Against " with the
    target empire's flag at y 54. Our client follows since 2026-10-11; it asked for a third
    empire only for Fake and Prevent Messages, titled that list "Select Third Empire" without
    "Any", read "Any" in every list, listed ships unsorted and every non-racial, non-unique
    area in data order, and wrote "vs <empire>" in the box.

**Players' reports on v0.8.1 (2026-10-04).** Players compared OpenSE4 0.8.1 with the
original and reported eleven differences. Each was settled from the executable and,
where a running game could show it, observed (spec 07 session 6). Q100–Q109 below cover the
windows; the population move between colonies is spec 03 §19 Q79.

100. **Colonizing in a sector with several planets.** "It doesn't let you pick which one to
     colonize; a moon cannot be colonized; when the primary planet does not fit the
     colony ship but a moon does, the order fails next turn and the ship's orders are
     cancelled." **Answer:** the original asks. A Colonize aimed at a sector with two or
     more planets (a planet and its moons) opens the Pick Object window listing every
     planet there, colonized or not, of any type; one planet is taken at once; none, or
     Cancel, gives no order. The picked planet becomes the order's target; nothing is
     checked until the Colonize runs on arrival. Then a wrong pick fails with a reason,
     which in a turn-based game is a message box titled "Colonize" (for instance that the
     ship cannot colonize an ice planet) and in a simultaneous game one log entry from the
     Colonization Minister; the failure clears the ship's whole list, as every failed
     order does. Aiming at a colony never adds the ship's population to it (§2.9, spec 03
     §8) (confirmed: binary; observed, spec 07 session 6: a huge ice planet and its tiny
     rock moon listed in that order, the ice pick failing on arrival with the message box
     and an empty list, the moon pick colonizing at once).
     Since 2026-10-04 our client follows: `MainWindow::completePick` gathers the candidates
     of the table (`pickCandidates`), gives the target at once when there is one, gives
     nothing and says nothing when there is none (a turn-based Attack excepted, which names its
     sector), and opens the Pick Object window (`drawPickObject`, tag `pick-object:list`) when
     there are several, with the clicked sector outlined; Drop Cargo names the object picked as
     its destination (`Order::object` or `Order::vehicle`, `dropCargo`). The engine checks a
     Colonize only in the planet's sector, in the order of spec 03 §8; a failure clears the list
     and, in a turn-based game, comes to the human whose turn it is as a message box titled
     "Colonize" (`TurnResult::messages`, not logged; a network host sends it as a notice), in a
     simultaneous game as one "Unable to Colonize" entry from the Colonization Minister with the
     picture `OrdersNotCompleted`. Ours still writes its own prompt ("Colonize: pick a planet")
     at the bottom left of the system panel.
101. **Log pictures.** "All the images that should be showing on the event log each turn
     are missing." **Answer:** the original shows one picture, the selected entry's, at
     the top-left of the details pane, with the title to its right, and picks it by the kind
     of entry: a race portrait, a hull portrait, a facility's or planet's picture, or a
     file of `Pictures/Events/` (§4.1 "Picture", "Loading", "Items developed" and
     "Pictures of the other entries") (confirmed: binary; observed, spec 07 session 6: a
     built ship's entry shows its hull portrait).
     Since 2026-10-04 our client follows: every entry records its picture when it is made
     (`game/log_picture.hpp`: an Events file name, or a subject resolved when shown: a race
     portrait, a design's hull portrait, a planet's or facility's picture, a unit group's or
     fleet's portrait, an item developed), and `log.cpp` draws it unframed at its own size with
     the title to its right, the date at its height + 10 and the body at + 30, keeps the last
     picture for an entry without one, and shows a developed item's details. Entries the table
     does not name take a picture by analogy (the sentry, launch and target-gone notes the
     acting object's own; bombardment and a colony lost in battle the planet's; a cancelled
     package and "Treaty Not Possible" the other empire's portrait) or none (storms created or
     destroyed, Game Over, the last empire standing) (inferred). Without any picture shown yet,
     the title starts at x 10 and the date and body follow under it (inferred: the original's
     places would put them on the title's line).
102. **The view and a ship that warps.** "Ships moving through warp points at your command
     don't automatically change the view to the new system." **Answer:** in a turn-based
     game, during the human's own turn, the view follows every own object whose orders
     run, at once when given and at the turn's start, when each is selected in turn: the
     arrival system is shown with the exit warp point's sector current, and the ship's
     report stays open. No option controls it (Center Map on Current Ship is the tactical
     map's); in simultaneous games only the movement-log replay's Ctrl+U follows ships
     (§2.7) (confirmed: binary; observed, spec 07 session 6).
     Since 2026-10-04 our client follows in local, hotseat and play-by-e-mail games: the engine
     reports each group whose orders run and each warp jump of a turn-based run
     (`TurnResult::liveSteps`, not saved), and `MainWindow::followOwnMoves` shows the system of
     an own object acting outside the system shown, the arrival system with the exit warp point
     current after a jump (the selection and its report kept), and at the turn's start selects
     each object with orders in turn, ending on the last. A network client's view does not
     follow yet: its host carries the orders out and sends only the new state.
103. **Selection when another system is shown.** "When selecting a ship and then another
     system, it doesn't retain the selected ship and replaces the ship in the middle window
     with the new system." **Answer:** the galaxy panel's left-click and the Galaxy Map's
     Goto System change only the system shown and leave no sector current; the report or
     list, its tab, the lit order buttons, the tags and the movement line stay, so the ship
     can be given an order with a target in the new system. A click on the system already
     shown does nothing. Sector clicks, the Log's Goto, list Gotos and the next/previous
     buttons replace the report (§2.5, §2.6). In the system panel only the left button acts
     and a double-click on a warp point does not show the system it leads to (§2.4)
     (confirmed: binary; observed, spec 07 session 6).
     Since 2026-10-04 our client follows: a galaxy-panel click and Goto System
     (`MainWindow::showOtherSystem`) change only the system shown and drop the current sector;
     the list keeps the sector it was built for (`listAt_`); the Log's Goto to a system without a
     sector empties the report panel; a right-click in the system panel does nothing but
     OpenSE4's optional right-click Move To; every left-click on a sector clears the tags.
104. **The queue owner's report.** "In the construction queue of a planet, you used to be
     able to right-click the planet to see its details like mineral value." **Answer:**
     the owner box at the top of Set Construction Queue's queue column: a right-click opens
     the owner's Planet or Ship Report as a popup on Detail, the same report a right-click
     on a Construction Queues row opens (§1.8.3) (confirmed: binary; observed, spec 07
     session 6).
     Since 2026-10-04 our client follows: a right-click on the header of Set Construction
     Queue opens the owner's Planet or Ship Report popup on Detail (`ownerReport_`); Multi-Add's
     header takes no click. The header's contents are still ours.
105. **Facility reports from the Facil tab.** "On the Facil tab of the planet details
     screen, you used to be able to right-click a facility to get information about it."
     **Answer:** yes: a right-click on a facility of the Facil grid opens that facility's
     report, and on the ship report's Comps grid the component's report with its mount;
     on Cargo, units open their design's report and population the race's (§1.4)
     (confirmed: binary; observed, spec 07 session 6).
     Since 2026-10-04 our client follows for Facil and Comps: a right-click on a facility or a
     component row opens its item report (`ItemReportPopup`, with the component's mount) in the
     report panel, both report popups and the Combat Piece Report; the report functions return
     the item clicked. The pages are still drawn as rows, not the 36 px grid, and Cargo takes no
     right-click.
106. **The Ability tab.** "It now shows the abilities individually instead of combined, and
     the space yard shows index values for minerals, organics and radioactives instead of
     text saying it can construct with 2000 minerals per turn." **Answer:** the original
     combines nothing either, and its planet Ability tab never lists facilities. Each line
     is one ability entry's `Descr` text; the planet page lists the planet's own
     abilities, then racial, cultural, population-level and mood lines; the ship page the
     hull's entries and the racial and cultural lines, not the components'. The sentence
     the player remembers is the space yard's own `Descr`, shown on that facility's report,
     which Q105 makes reachable from the Facil tab (§1.4 "The Ability page") (confirmed:
     binary; observed, spec 07 session 6).
     Since 2026-10-04 our client follows: `planetAbilityLines` and `vehicleAbilityLines`
     (`ability_lines.cpp`) give the lines of §1.4, each drawn after the `General.bmp` lamp with
     the text at x 13; an empty page stays empty; the Combat Simulator's reports leave the racial
     and culture lines out; `pieceReportAbilities()` lists the hull's or planet's own entries
     only. Our words for the racial, cultural, population-level and mood lines are our own.
107. **The report's tab across selections.** "A tab picked on a planet's report stays picked
     on a new planet, even in another system." **Answer:** the original resets it: every
     report filled for an object opens on Detail, the same object clicked again included;
     nothing is stored per kind of report or for the game (§2.5) (confirmed: binary;
     observed, spec 07 session 6).
     Since 2026-10-04 our client follows: `clearSelection()` (every selection, the list's rows,
     the game's own re-selections) and `ReportPopup::openPlanet()`/`openVehicle()` reset the tab
     to Detail; showing another system keeps it (Q103).
108. **The colonizable marker in a sector with a colony.** "If you colonize a planet or moon
     and there's another colonizable one in that sector, it still shows the red star in the
     top right of the sector through your flag. Is that intended?" **Answer:** no. Only the
     planet the sector shows (the first stellar object, or a later, larger planet) can
     carry a mark, and it carries either the colony box or the star, never both; the
     other planets of the sector get none, so a colony on a moon beside a larger planet has
     no mark on the map (§2.4 "One planet's marks per sector") (confirmed: binary;
     observed, spec 07 session 6).
     Since 2026-10-04 our client follows: `drawSystem` marks only each sector's shown stellar
     object, with the colony box (outline and bars, no flag) or the 12×8 star at the places of
     §2.4, and the facility letters go on that planet only.
109. **Only Latest.** "With Contra-Terrene engines researched, Only Latest still shows Ion
     Engine III as the most recent." **Answer:** the original keeps, of each run of
     neighbouring items with the same `Family` in data-file order, the last one; numerals
     and names play no part. The stock large engines share one family in the order Ion,
     Contra-Terrene, Jacketed-Photon, Quantum, so with Contra-Terrene Engine I researched
     the designer shows it and hides Ion Engine III (spec 02 §6.4 "Only Latest")
     (confirmed: binary).
     Since 2026-10-04 our client follows: `Rules::onlyLatestComponents` and
     `Rules::onlyLatestFacilities` keep the last of each run of neighbours; the designer's and
     Set Construction Queue's boxes write the Empire Options rows back. Upgrades keep the
     highest numeral researched (`latestFacilityOfFamily`, spec 02 §6.6), which is right for
     facility upgrades; a design's Upgrade takes the family's last researched component in
     file order instead, and since 2026-10-05 ours does too (spec 03 §4.1, §19 Q81).

**Players' reports on v0.9.0 (2026-10-05).** Three reports: the design Upgrade and tagged
ships attacking together are spec 03 §19 Q81 and Q82; the fleet in the sector list is Q110.

110. **A fleet in the sector list.** "Fleets show all ships in the sector pane instead of as a
     fleet. Is this intentional? It does select the fleet, which is nice, and gives
     information about the selected ship beneath the fleet information, but you also can't
     select a different ship in the fleet for information without backing out first."
     **Answer:** no. In the original each of the player's own fleets is one row of the
     sector's list, with the fleet's picture, name and status icons; other empires' fleet
     members are listed one by one. The row opens the Fleet Report, which fills the panel
     alone (no ship report under it) and lists the members at the fleet's location: a
     left-click on a member makes it the fleet's leader, a right-click opens that ship's
     report as a popup window, so any member's report is one right-click away without going
     back to the list (§2.5 "Fleets in the list", "The Fleet Report") (confirmed: binary;
     observed, spec 07 session 7).
     Since 2026-10-05 our client follows: `sectorListRows` (`client/classic/sector_view.cpp`)
     makes the list's rows, one per own fleet, in the original's order, and a sector whose
     only row is a fleet opens its report at once; `fleetReport`
     (`client/classic/reports.cpp`) fills the panel alone, its member rows taking a
     left-click (`cmd::SetFleetLeader`) and a right-click (the Ship Report popup of
     `MainWindow`); Shift and a click on the fleet's row tag every member there.

111. **A fleet member selected without the list.** The ship arrows (next and previous ship),
     the next-fleet buttons, a Goto from the Ships\Units window and the Log select one
     vehicle, which can be a member of one of the player's fleets. Does the original's panel
     then show the Fleet Report, as for the fleet's row of the list, or the member's own Ship
     Report? OpenSE4 shows the Fleet Report alone, whatever selected the member, so the
     panel never shows a member's own report (inferred, §2.5).

**A player's report on v0.11.0 (2026-10-06).** Three points about ship movement at End Turn, in
an imported turn-based game of the original.

112. **Ship movement at End Turn.** "When you hit end turn the end turn log shows up and then
     the ships move under that screen"; "Can the animated ship movement speed be sped up,
     preferably by a slider control ... Right now it is too slow"; "There are no ship movement
     lines when the ships move but they exist in standard SE4."
     **Answer:**
     - *Order.* The original animates the visible moves as they are made, inside the turn's
       processing, and opens the Log only when the player gets the turn, so the moves come
       first (§2.4 "Moves as they are made", §2.7; confirmed: binary for each part, the order
       of a turn's start as a whole inferred from the turn's sequence).
     - *Pace.* The original waits 10 ms after each 5° frame of a turn and 1 ms after each 1 px
       frame of a slide (confirmed: binary). Ours drew each frame for at least one display
       refresh, about 0.8 s a square at 1024x768 on a 60 Hz display.
     - *Lines.* The original draws the open report's route again over every sector redrawn
       during the animation, the route stored before the move (§2.4 "Movement lines",
       confirmed: binary). Ours drew the route from the square the move ended on, and none at
       all when the move used up the object's orders. Besides, Display Ship Movement Lines is
       off on a fresh install until a new simultaneous game is started (§1.9, confirmed:
       binary), so a player who loads an imported game on a fresh install of OpenSE4 has no
       lines at all until switching them on, while the original on the same computer may have
       had them on for years.

     Since 2026-10-06 our client shows the moves first and opens what a turn's start brings
     after them, a click or a key showing the moves at once (§2.7, §2.4); plays the frames by
     the time elapsed at the original's waits as written, with OpenSE4's "Ship movement speed"
     (§1.9); and draws the route as it stood before the move while the object glides (§2.4
     "Movement lines", item 8). The per-computer default of the movement lines stays the
     original's.

     Open: how long the original's frames last on players' Windows machines. Its waits read
     the system's millisecond counter and last at least one step of it (§1.10.3): 16 ms under
     Wine (observed, spec 07 session 3), so that a square at 1024x768 takes about 0.8 s, as ours
     did before; §1.10.3's 15.6 ms on current Windows is that counter's usual step, not a
     measurement. The player found ours too slow next to the original on their own computer,
     which suggests a finer counter there and the waits as written, the pace OpenSE4 now takes
     by default (inferred). Measuring the original on Windows would settle it.

     A later report (Discord, 2026-10-14, on v0.14.0): "Ships still move all together, they
     should move individually (or as a group if grouped/fleeted) & the game should show them
     moving. That's part of the fun of designing your own ships, to see them moving about. At
     least there should be an option to do that or to have all move at the same time."
     **Answer** (confirmed: binary): the original animates only the moves of the human player
     whose turn it is, in a turn-based game, one group after another, never interleaved: each
     group's whole run, every square of it a turn (when the heading changes, a still pause in
     which nothing is drawn), a slide and the pause, is over before the next group moves; the
     group is one sprite, its first vehicle's mini; at a turn's start each own object with
     orders is selected in turn, then moves; warp
     jumps, computer players' turns and simultaneous turns are not animated (§2.4 "Moves as they
     are made", §2.7). Ours glided all the moves of an engine call together once it had made
     them all, several squares of a group as one straight glide.

     Since 2026-10-14 ours follows: the engine reports each group's step as it makes it
     (`game::MovementStep`), the session passes them on in order with where the view follows
     (`ClassicSession::takeMoves`), and the main window plays them as a queue, square by
     square, one group after another, the view following the player's own groups
     (`ShipGlides`, `MainWindow::followMove`). Our client differs where the owner chose so
     (§2.4 "Order", docs/PARITY_GAPS.md): the Options window has OpenSE4's "Ship movement: one
     group at a time (the original's)", the default, and "all at once", every group starting
     together, each still square by square (§1.9); a network or e-mail client, which got no
     steps from its host, glided each changed group straight to its new square, one group
     after another (inferred); and it showed the other empires' moves the player sees and a
     simultaneous turn's moves at the next turn's start.

     Watched in the running original the same day (observed, spec 07 session 8), and settled
     for the input from the executable (confirmed: binary): a turn draws nothing (the old
     square stays as it was for the turn's waits, then the mini slides at its new heading);
     at a turn's start clicks and keys are ignored throughout the moves; after an order given
     by a click on the map nothing is taken until its moves are over; after an order given by
     an order button everything but a second order button gets through, a window opened
     meanwhile holding the moves until it closes, and End Turn's Yes ending the turn inside
     them, after which the original faults (§2.4 "Input"); a move run makes (0, 0) current
     (Q123); a minefield strike shows its explosion (§2.4 "Mines"). Since 2026-10-14 ours
     follows all of this, with the original's moves alone animated by default: OpenSE4's
     option "Also animate other empires' and simultaneous turns' moves", off by default,
     shows the others (§1.9); the moves a network client's host made for it come with its
     states (protocol 10); a click or a key still shows a turn's start's moves at once, and
     End Turn's Yes shows an order's moves at once before the turn ends (§2.4 "Order").

113. **Choices left by the designs and research audit (2026-10-11).** The executable gave the
     rules of spec 05 §2.1 and Q92–Q94 above; these details it did not settle, and OpenSE4
     chose them (inferred):
     (a) *Tech Tree rows.* Is an area's name written on each of its rows or only on the
     first? Does an area that needs nothing get a row of its own, and a level that makes
     nothing available? Does the export write a heading line, and are the Tech Levels
     lines exactly "Tech Name,Level,Item [N]"? Ours: the name on each row, one row for
     each such area and level, no heading line, cells joined by a bare comma.
     (b) *Select Vehicle Size.* Does the Abilities box follow the row under the pointer or a
     row selected by a first click (and a second click or double click pick it)? Does
     Cancel also clear a size chosen before? Ours: the pointer, a single click picks, and
     Cancel clears it.
     (c) *Select Weapon Mount.* What does Cancel pick, and what does the label at x 319
     show without a mount (and in which colour)? Ours: Normal Mount; "Normal Mount", white.
     (d) *Research.* Which green are queued areas drawn in? Ours: #00FF00.
     (e) *Intelligence.* In which order do the "Select Empire" lists give the empires, and
     do the sorted lists ignore the letter case? Ours: empire order; the case ignored.
     (f) *Weapons Report.* Does a mount change the figures of a weapon whose type it does
     not take (a direct-fire mount and a missile)? Ours: no, that weapon keeps its plain
     figures.
     (g) *Designs.* Does the detail of an enemy design say "(Obsolete)" when its owner made
     it obsolete? Ours: no, only an own design's does.

114. **The objects a stellar manipulation can be aimed at.** The Stellar Manipulation window
     takes its target from the vehicle's sector after the player confirms (§1.3), and asks
     with Pick Object when there are several. Does it list the objects the player cannot see
     (a planet a storm hides, a cloaked colony's planet), as Colonize's pick does (§2.9)? And
     does the Construct size picker list the constructed worlds in PlanetSize.txt order, and
     appear when only one size qualifies? Our client lists every object of the kind, seen or
     not, and always shows the size picker, its sizes in file order (inferred).
     **Answer, in part** (confirmed: binary, 2026-10-14): the targets are every object of the
     kind in the vehicle's sector, with no sight test, in the system's object order, as ours;
     the size picker lists only the records whose `Special Ability ID` equals the vehicle's
     best value (§1.3). Whether the picker shows for a single size, and its order, stay open.
     Since 2026-10-14 ours follows (`shipui::constructSizes`).

**The REA parity audit's main window (2026-10-14).** The audit of the original's main window
against the executable settled the ship cycle, the location keys, Set Patrol, Change Name,
Cloak and Decloak, the waypoints' numbers and what marks an object changed (§2.3, §2.4,
§2.8, §3.1, Q99(d), spec 03 §8, §8.1). Implementing them left these choices (inferred):

115. **The ship cycle's start.** Ours counts the place of the ship the cycle leaves as
     visited by "Stop once per location", so that a press from a ship the player selected
     with a click goes to another place. Does the original's table hold only the places the
     cycle itself stopped at? And does a press start from the selected ship, or from the last
     ship the cycle selected?
116. **Set Patrol in a turn-based game.** Each point clicked adds its Move To at once. In a
     turn-based game every order given runs at once, so in ours the ship sets out for the
     first point as soon as it is clicked, and a point it reaches leaves the list before the
     route is closed. Does the original run the list after each point, or only when the route
     ends?

     **Answer** (confirmed: binary, 2026-10-14; spec 03 §8 Set Patrol): only when the route
     ends. In a turn-based game the points are only appended while the player picks; the click
     on the object's own sector closes the route, Repeat goes on, and only then does the list
     run, from its first order. Since 2026-10-14 our client appends each point held
     (`cmd::SetOrders::hold`, `cmd::OrderTagged::hold`, save format 11), so nothing runs
     before the route closes (`MainWindow::addPatrolPoint`, `finishPatrol`).
117. **Held keys.** Which of the main window's keys repeat while held, and does End Turn?
     Ours repeats only the selection cycles and the movement log's single step (§3).
118. **Change Formation\Strategy cancelled half way.** Ours sets the formation as it is
     picked; Cancel on the strategy picker that follows keeps it. Does the original keep the
     formation, or change nothing unless both are picked?

**From implementing the REA parity audit's units package (2026-10-14).**

119. **The units and cargo windows' open points.** The audit settled the windows of §1.3 and
     §1.10.2 and spec 03 §8 and §12; OpenSE4 chose these where it was silent (inferred):
     - *Launch \ Recover Units on the map.* The audit says the original's two lists are
       swapped against ours of 2026-10-11 (carriers on the left, groups in space on the
       right), and also that in a battle the left list holds the carrier and the right the
       launched groups. Ours keeps the carriers on the left in both modes. Which side holds
       which on the map?
     - Which own ships and bases the map's left list holds besides the colonies: ours lists
       those with a launcher, with units aboard, and the one the window opened with.
       **Answer** (confirmed: binary, 2026-10-14): the empire's own objects in the sector
       that hold cargo, have cargo space above 0, and are a colony or have a fighter,
       satellite, mine or drone launch ability, in the system's object order (colonies and
       vehicles mixed). Since 2026-10-14 ours follows (`launchCarriers` in
       `screens/ships_logic.cpp`); before, it also listed ships without a bay that carried
       units and the one the window opened with, tested no cargo space, and put colonies
       first.
     - *The pickers of Load, Drop, Launch and Recover Remotely.* For Drop Cargo, does the
       kind picker come before or after the Pick Object window? Ours asks the destination
       first. Their titles and look are ours.
     - *Races On Planet* for a colony of another empire: ours opened it for own colonies only.
       Settled by Q120: the original opens it for any colony, and ours does since 2026-10-14.
     - *The step bytes of the empire options* (spec 08 §3.6.7) are taken as the radio's
       position counted from 1 (Move One 1 .. Move All 5 for Transfer Cargo, 4 for the
       window Launch \ Recover Units); the stock value 3 is Move Ten either way.

**Players' reports on v0.12.0: the reports and the Strategic Combat map (GitHub issues #15–#17,
2026-10-14).**

120. **The owner's flag in a report.** "When viewing a different empire's ships or planets in
     the sector view, you should be able to right-click on their flag to see information about
     the empire, as in the original game." (GitHub issue #15.) **Answer** (confirmed: binary):
     a right-click on the owner's flag on the Detail page of a Ship, Planet or unit-group
     report opens that empire's Race Report, modal over the window holding the report, wherever
     the report is shown (the main window's panel, report popups, the Combat Piece Report),
     for any owned object the viewer sees, with no treaty or contact check, never from the
     Combat Simulator (§1.4 "Right-clicks"). The Race Report's Detail page shows the original's
     lines (§1.4 table). Two side answers: Races On Planet's area is x 230–290, y 196–236, where
     an own colony's race pictures are, not the x 196–236, y 230–290 this spec gave; and it
     opens for any colony, another empire's too (Q119).
     Since 2026-10-14 our client follows: the report functions return `ItemRef::Kind::Race`
     for a right-click on the flag (`vehicleReport`, `planetReport`), which the main window
     opens as the Race Report window and a report popup as a popup over itself
     (`RaceReportPopup`, `client/classic/screens/race_report.cpp`, through `ItemReportPopup`);
     the Combat Piece Report opens the window over itself, a simulation's side box nothing.
     The Race Report's Detail page draws the original's lines; the Planet Report draws an own
     colony's race pictures at the original's place, where the right-click opens Races On
     Planet for any colony. Our client differs: the flag answers as a whole wherever it is
     drawn (the unit-group popups' quirk is left out), and only on Detail.

121. **What the Ship Report's damage counts.** "Damaged vessels show the combined structure
     size of the damaged components, rather than the number of components damaged, while the
     number of components repaired per turn is the number displayed by Space Yards/Repair
     Bays." The player added: "It's interesting seeing the structure tonnage, though."
     (GitHub issue #16: ours showed "290 / 490 (59%)".) **Answer** (confirmed: binary): the
     original's Damage line is "D/N", the destroyed components against the design's, with a
     bar of red and green cells; the Comps page and the Ships\Units window's Dmg column give
     the same figure (the column blank for unit groups), and the unit-group reports have no
     damage line. The line shows for own ships and bases that are not mothballed, and for
     another empire's only under long-range scan (§1.4 "Damage on the Ship Report"). Repair
     works off the same count (spec 03 §13), so what the report shows is what the space yards'
     and repair bays' figures repair.
     Since 2026-10-14 our client follows: `vehicleReport` (`client/classic/reports.cpp`) shows
     "D/N" and the bar (`shipui::damageBar`), the Comps page starts with "Damaged Components",
     the Dmg column is blank for unit groups, the Movement and Damage lines follow the
     original's conditions, and a foreign report blocked by a jammer says so
     (`sight::scannerBlocked`). Our client differs: hovering the Damage line shows the
     structure lost ("Structure lost: S of T (P%)"), which keeps the figure the player
     liked without putting it on the line.

122. **A square left on the Strategic Combat map.** "At the end of a strategic combat,
     sometimes it still shows a dot for a ship even if there are no ships of that empire left
     intact." (GitHub issue #17: the forces list showed the empire's only ship lost, and a
     small dot in its colour stayed on the map.) **Answer** (confirmed: binary): the dot was a
     seeker in flight. In the original a flying seeker is a piece for the battle's end check:
     the battle goes on while any empire's seeker flies and an empire hostile to it still has
     a piece, so the beaten side's seekers strike, are shot down or vanish first, and a seeker
     can be left on the map with Close lit only when the turn counter ended the battle (spec 04
     §4). A seeker whose launcher dies flies on; one whose target leaves the battle goes with
     it at once (spec 04 §10.1). The map draws every piece, a seeker as a filled 3×3 square,
     planets and obstacles as hollow 12×12 outlines (§1.10.5 "The small map"). OpenSE4's end
     check left seekers out, so its battle ended with the seeker still flying, and its map drew
     seekers as small round dots.
     Since 2026-10-14 the engine follows: `Battle::hasPieces` counts seekers for the end check
     and the phases, `Battle::hasSurvivors` leaves them out of the verdict, and
     `Battle::dropSeekersAimedAt` takes the seekers aimed at a piece that leaves the battle;
     battles with seekers can now last longer than before. Our client draws the small maps as
     §1.10.5 says (`CombatMapPainter::squares`). What the original credits for the kill of a
     seeker whose launcher has died was settled on 2026-10-14 from the complete listing:
     nothing. A seeker's impact has no attacking piece, so it gives no experience, and its
     tonnage goes to the launcher only while that is still in the battle (spec 04 §10.1;
     ours since then, `Battle::moveSeekers`).
123. **The current sector during a move order.** Each run of an order that travels to a
     square (Move To, Move To Waypoint and the other orders that head for a target square: a
     patrol leg, a pursuit) first makes sector 0, the top-left square of the shown system, the
     current sector, for every player and in every mode, before its first step; a jump later
     makes the exit warp point's square current (§2.7; confirmed: binary, found while settling
     Q112). What does the player see of it?
     **Answer** (observed, spec 07 session 8; a Move To given by a map click and a Resupply
     given by its order button alike):
     - The moving object's report stays open in the report panel, refreshed when its run is
       over, and its orders stay lit.
     - The yellow marks vanish from its starting square when its slide starts and are not
       drawn anywhere afterwards, not on (0, 0) either, even with one of the player's ships
       standing there.
     - The coordinate line's Range counts from (0, 0) while something the player sees stands
       there (Range 5 over (5, 5)), and shows no Range at all while (0, 0) is empty.
     - After a tagged group's order the tags are cleared and (0, 0) is selected once all the
       moves are over: the report of what stands there with the marks round it, or the
       system's report and no marks when it is empty.
     - When nothing moves (a Move To kept for the next turn by a ship with no movement left,
       a tagged group that could not move) the current sector stays, and so do the marks.
       Whether the jump to (0, 0) needs movement left or the first step taken was not
       separated (inferred: the first step).
     - At a turn's start each object's selection makes its own sector current again, so after
       the pass the marks stand on the last object that moved (there a ship that ended on
       (0, 0), whose arrival redrew it).
     In the original the marks are drawn on a square when it is selected or redrawn while it
     is the current sector (inferred from the above). Not watched: a simultaneous turn's
     processing and the computer players' turns, which also run move orders; their current
     sector is not shown to anyone until a human's turn restores its own view (§2.7).

     Since 2026-10-14 ours does the same in a turn-based game: when a run of the player's own
     makes its first step the shown system's (0, 0) becomes current; the selection, the report
     and the lit orders stay; the marks stay where they were drawn until that square is
     redrawn (a slide leaving it, a step ending on it) and come on the current square only
     when it is selected or redrawn; the coordinate line's Range counts from it; after a
     tagged group's order the current square is selected again once its moves are shown
     (`MainWindow::followMove`'s corner and reselect markers, `marksAt`,
     `noteRedrawnSquares`). Before, the current sector stayed where it was.

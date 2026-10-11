# Files audit: the original's empire, map and game-setup files (analyst findings, plain language)
Observed: decoded 163 .emp files exactly to end of file: 20 stock (install has 20, not 26) + 143 from four bundled mods (AtrocitiesRaces 75, Devnull171 47, Imperium Verus 20, Proportions 1). None carries designs ("with designs" layout from code only). No .map or setup-file samples locally (layouts from code only). Versions seen 1.49–1.91; stock files are 1.51.

## Findings
1. MISSING (M): no import/export of .emp; Add Existing doesn't offer the shipped empires. Original: Add Existing opens Load Empire picker on the Empires folder (the active mod's when it has one) -> lists the 20 shipped empires and any mod's. Ours: setup.cpp:678-680 lists only own TOML from user data; classic_save_codec.cpp:1432 refuses any version other than 1.95 (stock .emp are 1.51). Add import ALONGSIDE TOML. Spec: new spec 08 "Empire files"; update spec 06 §6 .emp row ("treat as opaque; import out of scope") and the line under the table; spec 02 §1.10; spec 08 §1.3.
2. MISSING (L): no import/export of .map. Original: Load Map lists Maps folder's .map; both Save Map buttons write that format. Ours TOML only (docs/MAPS.md). Spec: new spec 08 "Map files"; spec 01 §12 ("treat the format as ours") and spec 06 §6 "binary (inferred)" row should point to it.
3. DIFFERENT (L–M): OpenSE4's empire file keeps far less. Original .emp holds the whole empire record with game state cleared; Begin Game copies each listed empire into the game whole, so next game inherits the file's e-mail, password, empire options (switches and window memories), repair priorities, design-type and colony-type lists, queue templates, minister switches and their two flags, computer-player memory. Observed 45 of 163 files have all 25 ministers on. Ours setup_model.cpp:680-750 writes identity, race, minister style, experience, strategies, designs only; in-game Save Empire (game_menu.cpp:217-240) doesn't copy passwordHash (password lost). Spec 02 §1.10 already says minister settings saved (code gap). Whole copy confirmed: binary; that nothing resets ministers/options at game creation is inferred (not traced).
4. MISSING (L): Edit on Players page asks for the empire's password if it has one (input "Enter Empire Password"), EXACT compare (case and spaces count); wrong -> "Invalid Password" box (incorrect), editor doesn't open; Cancel nothing. Add Existing, Remove, Save To File never ask. Ours openEditor (setup.cpp:231) opens directly. Spec 02 §1.10 says it [H] (code gap); exact compare new. (Same as setup-scenarios #8.)
5. DIFFERENT (L): Add Existing checks at once after a successful read, against the setup on screen; failure -> one "Invalid Empire" box, not added. Technology: sum of empire's tech levels (each capped at its area's maximum) must not exceed the sum over allowed areas of the level a new player gets at the chosen tech level (Low: area's start level; Medium: its raise level; High: its maximum); message: empire's technology setup differs from what this game allows. Racial points: traits + characteristics must not cost more than setup's racial points for new players; message: empire spent more racial points than the game allows. Both fail -> racial-points message. Save Empire files have every tech level 0 so first check only refuses hand-made/old files. Ours adds then refuses at Begin Game (setup_model.cpp:268-271).
6. DIFFERENT (L): Players-page Save To File asks "Save Designs" yes/no then Save Empire picker; saved copy always has computer control cleared; Remove frees the designs that empire brought, no confirmation. Ours no question, keeps computer flag (setup.cpp:688, setup_model.cpp:700). Not gaps: no file picker, file named after empire (spec 06 Q72 deliberate).
7. Side (L, inferred): game seed drawn at Begin Game only when the quadrant is generated there; after Load Map or Generate Map Now not redrawn; value kept = open question (spec 01).

## Spec text for docs/spec/08 (new sections)

### Empire files (Empires\*.emp)
Layout (confirmed: binary; observed: decoded 163 files, versions 1.49 to 1.91, each ending exactly at end of file):
1. Key header (§2.2), new random keys every file.
2. Version string (§2.5); 1.95 writes "1.95". Loader takes it as the version for §5.2 gates for the rest of the file.
3. One empire record (§3.6) in its empire-file form.
4. A word: number of designs n, always written (0 when saved without designs). Then n design records (§3.7).
No plain-text summary, prologue, options, victory block, globals, systems, objects or closing list.

Empire-file form (confirmed: binary): identical to the saved-game record field for field, except the eight derived flags of §3.6.1 are left out (can colonize Rock, Ice, Gas Giant; the five "breathes" flags). Files older than 1.14 still contain them. Racial-trait flags have no stored count (§3.6.5) so a file reads only with a data set with the same number of racial traits (observed: stock files decode only with 14; mods' files only with their own data set's counts 15, 10, 20; each of 20 stock files tested over 0..80: only 14 reaches exact end). Importer should require exact end of file and can report which trait counts fit, as for a saved game.

Values written by Save Empire: saver copies the empire and clears the game state of the copy before writing (confirmed: binary; observed in all 163):
- Control flags: computer controlled off; neutral off; destroyed off.
- Pools: three stored resources, research pool, intelligence pool = 0.
- Research: repeat off, divide evenly on; no unique areas; one tech level per tech area of the data set, every level 0 (observed counts 83 to 158, following each data set); empty queue.
- Intelligence: empty project list; its two flags kept.
- Politics: all 20 entries no contact (code 3), not dominant, counter 0.
- Lists: empty log; no fleets (fleets-ever-created counter 0, observed); ten waypoints empty name, system and sector 0; no tagged mine fields; no systems to avoid.
- Maintenance and reproduction bytes: the two "unused copies of the starting maintenance and reproduction percentages" hold the empire's effective percentages at save time (Settings + racial modifiers), observed 8..105 and 0..43; nothing reads them; any value safe.
- Kept as in the game: identity: four name strings, player number in source game (observed 1 in 146 files, up to 20 in others), "use race minister style" box; race and art: race folder (always lower case), art folder (= race folder in all 163), emblem override (empty in all), ship-name file; home system and sector of source game (0 when saved from Game Setup, observed in 95); race description: atmosphere, surface, three texts, demeanor, happiness type, culture, traits, characteristics (observed 50..150); experience; default formation, default strategy, planet strategy; computer difficulty; unused fields; password and e-mail in plain text; name lists: design types, colony types, queue templates; empire options; repair priorities; the whole ministers block: computer memory, minister style, 25 switches, both flags, anger 50 and turns since war (999 and up), unused bytes 0,0,0,1,1,1,0, name counters, capability flags; network name; combat strategies.
- Observed split: files saved from Game Setup's Players page (95 of 163) have no strategies and no design-type or colony-type list. Files saved during a game (68, incl all 20 stock) have 10 or 11 strategies, 22 or 23 design types, 8 colony types.

Designs (confirmed: binary; no sample has any): on Yes the file holds every design of the game's design list owned by the saving empire's player number, list order, free slots excluded. Each a full §3.7 record of a copy with id 0; owner = that player number; obsolete off, built-at-least-once off; 20 last-seen dates 0; changed off. Everything else kept (type, template, name, creation date, cached speed and cost, parts and mounts, strategy position, type code, extra abilities, four statistics). On No count is 0.

Version gates (confirmed: binary; all 163 decode with them). Version compared as a string as in §5.2. For files from 1.40 up:
| below 1.46 | no network name |
| below 1.51 | no drone-name counter; strategies have 13 target categories instead of 14 in both the type-priority list and the break-formation list; reading appends the design types Anti-Planet Drone and Anti-Ship Drone |
| below 1.62 | designs have no template name |
| below 1.78 | empire options lack facility marker groups 2..12 (11 booleans) |
| below 1.81 | strategies lack "drones per target" |
| below 0.61 | design block not read |

Reading (confirmed: binary):
1. Read version string, then the empire into a fresh empire.
2. Clear the eight derived flags and derive them again: empire breathes only its native atmosphere and can colonize only its native surface. If computer controlled (never true in a saved file), its strategies replaced by the race's AI strategy file.
3. When caller asks for designs (Add Existing and network host always do): first every design the game list holds for this empire is blanked. For each design in file: owner becomes this empire; cost and speed RECOMPUTED from the data set (unlike a saved game); design checked against the designer rules except the component technology test (spec 03 §4.2 rule 2). Hull and components stored as positions in the data set (§4), so a different data set silently gives other parts. Valid design goes into first free slot of the list or at its end, marked changed; invalid dropped without message.
4. Any stream error ends the load: "Load Error" box (empire file could not be loaded, names it); nothing added.

Game Setup, Players page: buttons in order Add New, Add Existing, Edit, Remove, Save To File (confirmed: binary).
- Add Existing: Load Empire picker: one column "Empire Filename" listing every *.emp in the Empires folder (mod's when a mod active and has one) + Change Directory; no password asked; reads file with designs, then technology and racial-points checks (finding 5); if both pass, added as next player; stored player number ignored (becomes place in list).
- Edit: password as finding 4. Remove: no confirmation, frees designs the empire brought. Begin Game: copies every listed empire whole into game, renumbers players, hands each its designs.

Saving (Save To File, Game Menu Save Empire) (confirmed: binary): 1. "Save Designs" yes/no; Esc and Enter mean No. 2. Save Empire picker: .emp list, New Empire File (asks Empire Name, makes <folder>\<name>.emp), Change Directory, Cancel. 3. Full path lower-cased before writing. 4. Error -> message empire could not be saved. Game Menu saves the empire of the player whose turn it is.

Network (context; OpenSE4 replaces): TCP/IP player's Game Setup writes player's empire with designs in this format to temp\CurrEmp.dat and sends it; host stores as temp\<player>.emp and reads with designs, sets network name from lobby; failure "Empire Load Error".

Mapping to OpenSE4: Import: four name strings -> EmpireSetup; race folder -> Race::style matched ignoring case; atmosphere, surface, culture, happiness, demeanor mapped as §3.6; traits by position; characteristics clamped as for TOML files; ship-name file, minister style, "use race minister style" box, experience; password: hash the trimmed, lower-cased text; e-mail; strategies as §3.6.13; designs as §3.7, then looked up by name for OpenSE4's own files; options, ministers, name lists, queue templates, repair priorities only once the empire file can hold them. Export: write "1.95", the reset values above, the data set's trait count and tech-area count (all levels 0), ministers defaults: computer state 1, system bytes 0, empty lists; anger 50, turns since war 999; zero byte then 0,0,0,1,1,1,0; name counter 0, eight capability flags off, drone counter 0.

### Map files (Maps\*.map)
Layout (confirmed: binary; no sample):
1. Key header (§2.2). 2. Version string.
3. Byte count of systems, then each system in map form: the §3.5 record up to and including its ability list (name, number, x, y, type description, physical type, empires-can-start flag, mask flag, non-tiled flag, background bitmap, changed flag, abilities), WITHOUT the explored set, the claimed set and the 20 notes.
4. The two starting-point lists exactly as in §3.5: specific (system, sector, player) and common (system, sector), word counts and word fields. Written in maps and saved games alike: Game Setup's Save Map writes the loaded map's lists; a game started from a map keeps them (spec 01 §12).
5. Word count of objects, then every object of class 1 star, 2 warp point, 3 storm, 4 planet or asteroid field, 6 comet, in list order; each the class byte + its §3.8 record written exactly as in a saved game except: planet's "colony follows" flag always false; blank slots of those classes included.
No ships, unit groups, designs, empires, options or closing list. Each object's stored id is its id in the source game (not renumbered); warp destinations are (system, sector) pairs so they survive.
Reading (Game Setup Load Map; confirmed: binary): 1. version gates as for a saved game. 2. object list emptied; system list rebuilt from file: new systems, nobody explored/claimed, no notes. 3. both starting-point lists replaced, exact duplicates dropped (§3.5). 4. objects read with the saved-game reader and numbered by position; stored ids ignored; each system's object list rebuilt. 5. error -> message map could not be loaded, naming file; whatever was read stays. 6. After Load Map (even failed) or Generate Map Now, Begin Game doesn't generate a quadrant; plays on systems/objects present; places empires from starting points first (spec 01 §3.6).
Saving: Save Map on Quadrant page or Game Menu's Save Map when allowed (spec 01 §12). Picker "Save Map": "Map Filename" list, New Map File (asks Map Name), Change Directory, Cancel. Path lower-cased. Error -> could not be saved.
Mapping: systems -> StarSystem, type found as §3.5; objects -> SpaceObject (sectorType = value - 1; ability ids from Appendix A); warp links by destination's warp point at (system, sector); specific points: player p -> slot p; common points -> common starts; export writes ids as position + 1.

### Game setup files
Layout (confirmed: binary; no sample): key header; version string; game options (§3.2); victory block (§3.3). Nothing else. Name not lower-cased.
Use: only TCP/IP games: host writes temp\GameSetup.dat after Game Setup and sends to each connected player; player reads into its options and victory conditions, then Game Setup opens with every option shown but locked (player adds/creates an empire). Errors: could not be saved/loaded. No button saves or loads a setup file; every Game Setup starts from defaults. -> No gap for OpenSE4 (replaces TCP/IP).

### Open questions (for §11)
1. Does game creation reset anything an empire file brings (minister switches, options, anger, turns since war)? Not traced.
2. Does a setup-saved file with empty strategy/type lists get defaults at game creation, and does an in-game file get them added a second time?
3. Save To File may pick wrong designs (inferred): selects designs by the empire's current place in the list while designs brought by Add Existing keep the key they were added with; after a Remove they can differ.
4. What game seed is used after a loaded or pre-generated map?
5. Map saved from a finite-resources game stores remaining stocks as planet values; read as percentages in a normal game (inferred)?
6. Does the install's separate map-editor program write this same format? Not analysed.

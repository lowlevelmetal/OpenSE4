# WP designs — designs, research, tech tree, weapons report, intelligence
Branch: rea/designs (create it from rea/integration if it does not exist on the remote; see ../README.md for its status).
Findings: findings/design-research.md items 2–14 (item 1, the design-type picker, belongs to the economy package; don't touch that picker in designs.cpp). findings/game-menu.md's "Q99(c)" paragraph agrees with item 2.
Decisions:
- #2 intelligence targets: the project's chosen target (or "Any") must be kept with the queued project and used by the engine when the project completes (check spec 05 §2 for how targets are drawn today; when a specific target was picked, use it; when "Any", the existing random draw). If this adds saved state, format 10, and import/export with the original's .gam if spec 08 holds the target.
- #3 Condensed View reorders the design's stored component list (affects combat firing order) — do it as the findings say.
- #9 names: keep OpenSE4's refusal of an empty name (document as "Our client differs"); apply the rest (Design Warnings box, name check by exact match against every design in the game, components ignored before a size is chosen, button label stays "Create Design").
- #5: keep the hover tooltip (OpenSE4's), add right-click reports.
- #12 Tech Tree: list all areas with the colouring rules; Export in slot 13 writing TechAreas.txt / TechLevels.txt with comma-separated lines into OpenSE4's saves folder (user data, never the install), then an "Export Successful" box naming the path.
- #13 Weapons Report as the findings say.
Other packages own: empire_logic.cpp's diplomacy functions (you own the intelligence ones such as intelTargetKind), designs.cpp's design-type picker.

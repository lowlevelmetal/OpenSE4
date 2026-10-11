# WP mainwin — main window keys, picks, selection and order issuing
Branch: rea/mainwin (create it from rea/integration if it does not exist on the remote; see ../README.md for its status).
Findings: findings/main-window.md items 1, 2, 3, 4, 10, 11, 12 and Q99(d); findings/orders.md items 1, 8, 10, 11, 12, 14, 15, 16, 17, 22, and from 25: the Ships\Units rows/sorting/totals bullets, the Waypoints window bullet, View Orders' "(Repeat Orders)" row, Formation and Strategy as two pickers.
Decisions:
- Patrol: keep Enter and right-click as OpenSE4 ways to end the pick, add the original's (append live, click own sector ends with Repeat on, lone order cleared).
- Rename (#4 / orders #14): refuse names in use as the findings say, but accept OK on the unchanged current name as "no change" (don't copy that quirk; document it). The check needs to see every space object's name: do it in the engine's command validation so it holds for every player, and show the prompt again in the client.
- Waypoints 1–10 (key 0 = waypoint 10): fix the classic importer/exporter mapping too (classic_save_import.cpp ~791 and the export) — touch only that mapping in classic_save_*.
- Cloak/Decloak (orders #1): immediate, like the colony cloak; keep the order kinds for imported games if needed.
- Q99(d): ClassicSession::carryFlags should follow the original's list of what marks an object changed.
Other packages own: main_window.cpp's chooseCargo and remote Load/Drop/Launch/Recover flow (units package), the status-bar T button (scenarios), the quit confirmation (gameflow), Stellar Manipulation and the galaxy map (another package), the Scrap window and Create Fleet / Fleet Transfer (scrap package). commands.cpp: you own Rename, SetMinister (add the fleet field) and SetWaypoint only.

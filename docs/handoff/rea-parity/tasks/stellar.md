# WP stellar — Stellar Manipulation window and the Galaxy Map
Branch: rea/stellar (create it from rea/integration if it does not exist on the remote; see ../README.md for its status).
Findings: findings/orders.md items 6 and 20; findings/main-window.md items 6, 7, 8.
Decisions:
- Galaxy map destination mode for Open Warp Point (main-window #6): add it; keep OpenSE4's sector pick only if it doesn't conflict (the original picks on the galaxy map).
- #7 the Show Names (and destination-mode Show Distances) switches are empire options saved with the game (format 10; import/export the spec 08 bytes). Keep OpenSE4's normal-mode Show Distances.
- #8 the notes window per system; FIX the leak of an unexplored system's real name (spec 01: unexplored systems are never named).
- Stellar Manipulation (orders #6, #20): buttons lit by the vehicle's own ability, target chosen after the confirmation (Pick Object when several), the order appended to the vehicle's own list, window closes in simultaneous games; the Construct size picker.
Other packages own: main_window.cpp (avoid; if a hook is unavoidable keep it minimal), ships_logic.cpp parts other than Stellar Manipulation.

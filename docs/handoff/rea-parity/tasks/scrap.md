# WP scrap — Scrap window and its actions, Retrofit, Create Fleet, Fleet Transfer, Abandon Planet, Systems To Avoid, AI scrapping fleet members
Branch: rea/scrap (create it from rea/integration if it does not exist on the remote; see ../README.md for its status).
Findings: findings/orders.md items 2, 3, 9, 13, 18, 19, 21, 24, the Scrap statistics bullet of 25, and the "Spec 05 Q74 traced" paragraph (rewrite the AI's scrapping of a fleet member to follow it; update spec 05 Q74 and the PARITY_GAPS row near its end).
Decisions:
- #2: failed Scrap-window orders count as done (spec 03 §15 corrected).
- #9 Abandon Planet becomes a stored colony order (spec 08 order kind 64; import/export it), keeping OpenSE4's confirmations.
- #18 Create Fleet: empty fleet, default Greek-letter names, deleted when still empty at close, minister flag from the empire's "new vehicles use ministers".
- Keep OpenSE4's additions in these windows unless a finding says the original's control replaces them; add the original's controls (Order By Name/Class, Select All/None) alongside.
Other packages own: commands.cpp's Rename/SetMinister/SetWaypoint/queue functions (you own AbandonPlanet, CreateFleet and fleet transfer), main_window.cpp (don't touch), empire_status.cpp's ministers/strategies/waypoints parts (you own Systems To Avoid).

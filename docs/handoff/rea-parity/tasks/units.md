# WP units — remote cargo/unit orders by kind, Launch/Recover (map and combat), mines in combat
Branch: rea/units (create it from rea/integration if it does not exist on the remote; see ../README.md for its status).
Findings: findings/orders.md items 4, 5, 7, and from item 25 the Cargo Transfer, transfer-step memory, Launch/Recover layout and Jettison right-click bullets, and item 23 (Races On Planet); findings/main-window.md item 9 (order of the steps); findings/combat.md items 2 and 3.
Decisions:
- Remote Load/Drop/Launch/Recover (orders #4 + main-window #9): location first, then the kind picker; orders act on every design of the kind (engine change, keep determinism). Colony Launch/Recover: kind only, no location pick. Imported games' per-design orders (if any) keep working.
- #5 warnings: message boxes from the window; log entries (titles "Launch Warning"/"Recover Warning") from orders for human owners, never in combat.
- #7: the Launch/Recover window acts at once on exactly the clicked holder, without touching or running order lists, and logs nothing.
- Combat #2/#3: one Launch/Recover window shared by map and battle (battle mode as described); mines can be laid in combat (rates and caps as the findings say). Whether a recovery in battle gives back launch capacity is unknown: choose, mark "(inferred)" and add an open question.
- Transfer-step memory lives in the empire options (spec 08 §3.6.7 bytes; import/export them).
Other packages own: the rest of main_window.cpp (you own chooseCargo and the remote Load/Drop/Launch/Recover pick flow), tactical.cpp except its launch window, combat_tactical.cpp except launching.

# WP gameflow — game flow, endings, sign-in, menus and files dialogs
Branch: rea/gameflow (create it from rea/integration if it does not exist on the remote; see ../README.md for its status).
Findings: findings/game-menu.md items 1–10, 12, 13, 14 (item 11 belongs to the economy package); findings/main-window.md item 5 (= game-menu 3); findings/setup-scenarios.md items 3 (Login To Game) and 9 (password comparison).
Decisions:
- Where the original exits the program (game-menu 5, the fourth wrong Next Player password, Game Completed), OpenSE4 returns to the intro instead (its existing stand-in); apply that consistently, including after Human Dead. Document as "Our client differs".
- #1 victory's last turn: implement for turn-based games as traced; for simultaneous games, keep today's behaviour unless the spec already says otherwise, and add an open question to spec 05.
- #2 order-failure message boxes: the engine reports failures as events; in a turn-based game the client shows a titled OK box when the failure happens while the human whose turn it is carries out orders; otherwise the matching minister's log entry. Move and Colonize are confirmed; for the inferred cases mark the spec "(inferred)".
- #6: engine rule change (a human is destroyed at the start of its next turn, after the Lose window and turn start), keep determinism.
- #9: keep OpenSE4's Save Game window and its confirmations; add Change Directory (for one save) and the invalid-name refusal; the rest is optional.
- #8: default sort by name A→Z (names in capitals), with sortable Name and Date headers.
- #13: keep OpenSE4's Manual window, add a direct way to open the installed manual.
- Login To Game: for loaded simultaneous games (local and different machines), as the findings say; passwords trimmed and lower-cased there; Next Player trims but keeps case. OpenSE4 hashes passwords: compare by hashing the normalized form, and keep reading existing saves (normalize at the comparison, not by rewriting stored hashes, unless safe).
Other packages own: main_window.cpp (don't touch except, if unavoidable, a minimal hook), setup screens, movement.cpp other than the failure-reporting path, session.cpp's carryFlags.

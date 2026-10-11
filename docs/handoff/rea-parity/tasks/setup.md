# WP setup — Game Setup, Empire Setup, Players page, command line, TCP/IP sounds, Compare Culture Modifiers
Branch: rea/setup (create it from rea/integration if it does not exist on the remote; see ../README.md for its status).
Findings: findings/setup-scenarios.md items 2, 4, 5, 6, 7, 10, 11, 12, 14 (item 8 belongs to the files package); findings/diplomacy.md item 16 (= setup-scenarios 13, Compare Culture Modifiers).
Decisions:
- #10 Ring 8/9: keep the Ring 7 fallback as a stated choice; close spec 01 Q46 with the finding.
- #11 command line: add the positional mod-folder override (argument 4 when four or more positional arguments) without breaking OpenSE4's own options; correct spec 05 §9.2.
- #12 Change Directory: optional; if done, a folder browser in the original's layout alongside OpenSE4's typed path.
- #16 culture compare: the original's dialog, keeping the current-culture highlight.
- Remove the PARITY_GAPS "OpenSE4 choice" rows these close (spin steps, characteristic words, Compare Culture Modifiers).
Other packages own: the Load Empire / Load Map file dialogs' format handling (files package), Login To Game and passwords at sign-in (gameflow).

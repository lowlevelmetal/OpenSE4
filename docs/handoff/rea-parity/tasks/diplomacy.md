# WP diplomacy — messages, packages, treaties and the empire reports
Branch: rea/diplomacy (create it from rea/integration if it does not exist on the remote; see ../README.md for its status).
Findings: findings/diplomacy.md items 1–15, 17, 18 (item 16 belongs to another package).
Decisions:
- Keep OpenSE4's additions: the Declare War confirmation, the Log's Send Reply, Only Latest and delete, our extra lines in the Empires window, free number entry for resources in packages (#13 toggles are not needed), the current-culture highlight.
- #7 Grant independence: the original never lists it. Keep OpenSE4's Grant independence as an OpenSE4 extension, but document it as such in spec 05/06 (correct the [M] text). Apply the other #7 rules (Allow Surrender gating, Break Treaty from Non-Aggression up, trade and requests offered at war, Want gift/tribute independent of the gifts option). The Demand/Request/Warn sub-list (#7 last bullet) is optional.
- #9 default message text: draw with a UI-local random source, never GameState::rng.
- #1 changes what the computer players receive (spec 05 §7.4 already assumes a package): check ai_diplomacy's payUp handles real packages.
- #4 is a client-side check in the original; also enforce it in the engine's command validation so network players can't bypass it.
- #6: plot only recorded rows (the player's statistics as recorded each turn); if OpenSE4 doesn't record per-turn visibility, record what's needed (format 10).
Other packages own: empire_logic.cpp's intelligence functions (intelTargetKind), setup_empire.cpp.

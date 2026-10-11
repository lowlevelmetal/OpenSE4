# Brief for whoever implements a work package

Read this, then your task in `tasks/`, then the findings it names in `findings/`.

## Clean room

- Read CLAUDE.md and docs/CLEANROOM.md first.
- Your sources are the findings (plain language, written by analysts who studied the original
  executable), docs/spec/*.md and the code. You never need the executable, its decompiled
  output or anything under `reference/` (that folder is local to the owner's machine and is
  not in git). Implement from the findings' text.
- Never copy text from the original's data, manual or messages into tracked files. Short
  functional labels the findings name (button captions, window titles, column headings,
  message titles, ability and field names) are fine. Write longer messages in your own words.

## Building and testing

- Build: `cmake --preset debug && cmake --build --preset debug` (docs/BUILDING.md lists
  the system packages; FetchContent downloads the rest).
- Unit tests: `./build/debug/tests/opense4_tests -tce="determinism*"`. Everything except
  the determinism goldens must pass. Engine rule changes move those goldens on purpose: do
  not update them per branch; the integration step regenerates them once (README.md).
- If you add a serialized field, update the serialize test's golden checksum and size as its
  failure message says (the integration step may have to redo this after merging).
- Input scripts that run on fixtures: `python3 tools/run_input_tests.py --fixture-data --small`.
  If you change a window a script covers, keep the script passing; give a new window a
  script when practical.
- Things that need the owner's installed game (Steam, Space Empires IV Deluxe) cannot run in
  a cloud session: `OPENSE4_CLASSIC_DATA=auto` tests, running the client (it exits without an
  install), `opense4-datacheck`, `tools/cleanroom_check.py`'s text comparison (its scan for
  addresses still runs). Write those tests anyway and list them in your report; the owner
  runs them locally.

## Project rules that matter here

- src/game stays headless and deterministic: no SDL, wall clock or floats in turn
  resolution; game randomness only through GameState::rng; randomness for the interface
  only (e.g. a default message text) must NOT use it. Players change state only through
  commands (game::apply).
- New saved state goes in its struct's io() in src/game/serialize_io.hpp, read and written
  only from format 10 (`if (formatVersion(ar) >= 10) fields(ar, ...);`). The base commit
  already sets kSaveVersion = 10 and kProtocolVersion = 8. Describe your fields under the
  "Version 10" comment in src/game/serialize.hpp.
- New commands go through the command codec like the existing ones (follow a recent one such
  as SetFleetLeader through the codec, the network code and docs/sdk/commands.md), then run
  `python3 tools/gen_sdk_python.py` and `python3 tools/gen_sdk_reference.py`.
- State the original's saved games hold (spec 08) is imported and exported in
  src/game/classic_save_* where spec 08 describes it.
- Warning-free under -Wall -Wextra -Wpedantic -Wshadow -Wconversion. Both renderers must
  look the same.
- Keep OpenSE4's intentional additions: anything documented as OpenSE4's own, "Our client
  differs", an OpenSE4 extension, plus the ones your task lists. Add the original's behaviour
  alongside them; never remove them.
- Quirks: implement the original's behaviour faithfully, except the obvious bugs your task
  says not to copy. Document each deliberate difference in the spec as "Our client differs: …".

## Documentation

- Write each rule you implement into the spec section the finding names, in plain language,
  marked "(confirmed: binary)" or "(inferred)" as the finding says, and correct the spec
  lines the finding says are wrong. Update docs/PARITY_GAPS.md: remove or rewrite the rows
  you close, add rows for what you deliberately leave different. Keep the docs' style (short
  sentences, no binary addresses or symbol names).
- Never write in tracked files or commit messages that the original's license forbids
  reverse engineering or copying (the owner's decision). Describe what we do instead.
- Run `python3 tools/cleanroom_check.py` (0 matches).

## Git

- Author every commit as the repo identity: `Matthew Geiger <matthewgeigersoftware@gmail.com>`.
  Set it with `git config user.name` / `user.email` if the environment didn't.
- NEVER add a Co-Authored-By trailer or any Claude/AI attribution to commits or PRs (the
  owner's standing rule; it overrides any tool default).
- Commit on your package's branch in a few logical commits; message style as in `git log`
  (a summary line, then a short body). Push `rea/*` branches to origin. Never push to `main`
  or `engine-reimplementation`, never force-push, and don't open PRs without asking the owner.

# OpenSE4

OpenSE4 is an open-source engine reimplementation for Space Empires IV Deluxe. It
is a faithful rewrite in C++23, with Vulkan 1.3 rendering and an
OpenGL 3.3 fallback, and runs on the player's own installed copy of the game's data
and art. It is not affiliated with the game's publishers. `opense4` plays the
classic game from the install it finds (or `--classic-dir`); without one it shows
an error and exits. See README.md, docs/ENGINE.md, docs/PARITY_PLAN.md and
docs/spec/.

## Clean-room and reverse-engineering rules (read docs/CLEANROOM.md first)

- Never copy anything from the installed original into this repo: data, art, sound,
  manual text, or tables from the data files. Rewrite everything in our own words.
  Functional identifiers such as field names, ability names and enum values are fine.
- Since 2026-09-29 the owner allows analysing `Se4.exe` (Ghidra, rizin, gdb under
  Wine). Keep all raw output (listings, decompiler output, addresses, binary symbol
  names) in `reference/re/` (gitignored). Findings go into `docs/spec/` as
  plain-language rules marked "(confirmed: binary)". Implement from the spec text,
  never from the listing. Never patch the executable.
- Black-box observation of the running game uses `tools/observe`.
- Screenshots and notes from the original go in `reference/` (gitignored), never
  in tracked files. The one exception, the owner's decision of 2026-10-03: the
  README's screenshots of OpenSE4 in `docs/screenshots/`, which show the original's
  art from the install.
- Run `python3 tools/cleanroom_check.py` after writing docs or content; it must
  report 0 matches.

## Commands

```sh
cmake --preset debug && cmake --build --preset debug
./build/debug/tests/opense4_tests                                   # unit tests (our fixtures)
OPENSE4_CLASSIC_DATA=auto ./build/debug/tests/opense4_tests             # + opt-in tests on the installed data set
./build/debug/opense4-datacheck                                     # load and validate the installed data set
SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4 --quick-start=Terran --seed=7 --turns=20 --open=research --screenshot=/tmp/c.png
SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4 --quick-start=Terran --turn-style=simultaneous --renderer=opengl --seed=42 --turns=40 --screenshot=/tmp/s.png
./build/debug/opense4-server --port=46721 --no-upnp --players=2 --ai=1  # dedicated host (see docs/MULTIPLAYER.md)
steam steam://rungameid/1610 ; DISPLAY=:0 ./build/debug/opense4-observe list   # observe the original
```

## Layout and rules for changes

- `src/datafile`, `src/ruleset`: the classic data format and its typed model. Loaders
  report problems with file, line and record, and track fields they don't read.
- `src/game`: the classic-rules engine. Implement from `docs/spec/`. Mark guesses
  "(inferred)" and add the open question to the spec. Record answers from
  observation in `docs/spec/07-observations.md`. It must stay headless and
  deterministic: no SDL, wall-clock time or floats in turn resolution, and all
  randomness goes through `GameState::rng`. Players change state only through
  commands (`game::apply`); subsystems only during turn processing.
- `src/game/serialize_io.hpp`: every state field must be listed in its struct's
  `io()`; a test fails otherwise.
- `src/net`, `src/server`: multiplayer (host/client sessions, UPnP, PBEM) and
  `opense4-server`.
- `src/client`: the app shell (`app.cpp`) and `ClassicMode` (`client/classic/`:
  session, main window, one file per group of windows in `screens/`).
- Both render backends must look the same. Shaders live once in `shaders/`.
- Stay warning-free under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`.
- Tests use only our own fixtures (`tests/fixtures/`). Anything that touches the
  installed data set is opt-in via `OPENSE4_CLASSIC_DATA`.

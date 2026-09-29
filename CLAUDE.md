# OpenSE4

OpenSE4 is an open-source engine reimplementation for Space Empires IV Deluxe. It
is a clean-room, faithful rewrite in C++23, with Vulkan 1.3 rendering and an
OpenGL 3.3 fallback, and runs on the player's own installed copy of the game's data
and art. It is not affiliated with the game's publishers. Until the classic engine
is playable, a prototype with our own simplified rules is the default mode. See
README.md, docs/PARITY_PLAN.md and docs/spec/.

## Clean-room rules (read docs/CLEANROOM.md before touching the original game)

- Never copy anything from the installed original into this repo: data, art, sound,
  manual text, or tables from the data files. Rewrite everything in our own words.
  Functional identifiers such as field names, ability names and enum values are fine.
- Never disassemble, decompile, hex-dump or run `strings` on the original
  executables. Observe the running game black-box only, with `tools/observe`.
- Screenshots and notes from the original go in `reference/` (gitignored), never
  in tracked files.
- Run `python3 tools/cleanroom_check.py` after writing docs or content; it must
  report 0 matches.

## Commands

```sh
cmake --preset debug && cmake --build --preset debug
./build/debug/tests/opense4_tests                                   # unit tests (our fixtures)
OPENSE4_CLASSIC_DATA=auto ./build/debug/tests/opense4_tests             # + opt-in tests on the installed data set
./build/debug/opense4-datacheck                                     # load and validate the installed data set
SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4 --classic --seed=7 --screenshot=/tmp/c.png
SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4 --renderer=opengl --seed=42 --turns=40 --screenshot=/tmp/p.png
steam steam://rungameid/1610 ; DISPLAY=:0 ./build/debug/opense4-observe list   # observe the original
```

## Layout and rules for changes

- `src/datafile`, `src/ruleset`: the classic data format and its typed model. Loaders
  report problems with file, line and record, and track fields they don't read.
- `src/game`: the classic-rules engine. Implement from `docs/spec/`. Mark guesses
  "(inferred)" and add the open question to the spec. Record answers from
  observation in `docs/spec/07-observations.md`.
- `src/sim`: the prototype rules. It must stay headless and deterministic: no SDL,
  wall-clock time or floats in turn resolution, and all randomness goes through
  `GameState::rng`. All state changes go through `sim::applyCommand`. The same
  principles apply to `src/game`.
- `src/client`: the app shell (`app.cpp`) plus modes: `PrototypeMode` and
  `ClassicMode` (`client/classic/`).
- Both render backends must look the same. Shaders live once in `shaders/`.
- Stay warning-free under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`.
- Tests use only our own fixtures (`tests/fixtures/`). Anything that touches the
  installed data set is opt-in via `OPENSE4_CLASSIC_DATA`.

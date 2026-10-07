# OpenSE4

OpenSE4 is an open-source engine reimplementation for Space Empires IV Deluxe. It
is a faithful rewrite in C++23, with Vulkan 1.3 rendering and an
OpenGL 3.3 fallback, and runs on the player's own installed copy of the game's data
and art. It is not affiliated with the game's publishers. `opense4` plays the
classic game from the install it finds (or `--classic-dir`); without one it shows
an error and exits. Its modding SDK (Python computer players and rules scripts on a
built-in MicroPython, data patches, asset and interface mods, `opense4-sdk`) layers
mods over that install. See README.md, docs/ENGINE.md, docs/PARITY_PLAN.md,
docs/spec/, docs/MODDING_SDK.md and docs/sdk/README.md.

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
./build/debug/opense4-sdk check mods/examples/small-ai             # a mod on the installed game (docs/sdk/README.md)
./build/debug/opense4-sdk test mods/hegemon --turns=5              # its tests/, then short games of its players, rules, scenarios
./build/debug/opense4-sdk new ai /tmp/my-ai --id=me.my-ai          # a mod from a template (or: new --from-example small-ai DIR)
./build/debug/opense4-sdk arena --mod=opense4.hegemon --ai=opense4.hegemon:Hegemon --ai=builtin --games=4 --turns=50 --out=/tmp/arena
python3 tools/gen_sdk_python.py --check && python3 tools/gen_sdk_reference.py --check   # without --check: regenerate
OPENSE4_SDK_FIXTURES_OUT=/tmp/fx.json ./build/debug/tests/opense4_tests -tc="sdk python*" && python3 tests/sdk/python/run_sdk_tests.py --fixtures /tmp/fx.json  # the package's tests under CPython by hand
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
- The modding SDK (docs/MODDING_SDK.md is its design, docs/sdk/ the modder's docs):
  - `src/script`: `script::Value`, JSON and the MicroPython runtime with its sandbox.
    MicroPython is vendored in `third_party/micropython`, made by
    `tools/update_micropython.sh` from the pinned release, our `patches/` and
    `src/script/port/mpconfigport.h`; never edit it by hand: change a patch or the port
    and rerun the script.
  - `src/mods`: packages, manifests, mod sets and identity, the layered game files, data
    patches. `src/sdk`: the engine's side (view, command codec, players and controllers,
    rules hooks and effects, `ui/` files, scenarios). `tools/sdk*.cpp`: `opense4-sdk`.
  - `python/opense4`: the package mods and external bots use. It runs on the game's
    MicroPython and on CPython 3.10+, so use only what both have (docs/sdk/runtime.md;
    stand-ins in `python/lib`). The docs are its schema: `tools/gen_sdk_python.py`
    generates its enums, records and constructors from docs/sdk/view.md and commands.md,
    `tools/gen_sdk_reference.py` docs/sdk/reference from its docstrings. Rerun both after
    changing those; the tests run their `--check`, and fail when the docs miss a command,
    field or enum value the code has (tests/sdk/test_sdk_docs.cpp).
  - `mods/`: `hegemon`, the mod that comes with OpenSE4 (listed in `mods/bundled.txt`,
    copied into the packages' `mods/`), and `examples/` (shipped in `sdk/examples`), each
    with tests the SDK's tests run. `tests/sdk`: the SDK's tests (C++, the Python tests in
    `tests/sdk/python` under both runtimes, fixture mods in `tests/fixtures/mods`).
  - The docs' examples are tested: a `python` block in docs/sdk, MODDING_SDK.md or a mod's
    README runs (`tests/sdk/python/check_doc_snippets.py`) unless marked `python no-run`
    (compiles only) or `fragment`; `toml` blocks must read as the file they show.
- Scripts are part of the rules (docs/MODDING_SDK.md §7.3, §14): rules scripts and in-game
  computer players run on the host inside turn processing and must resolve the same on
  every computer. They get the engine's random numbers only, give whole numbers back,
  run on bytecode budgets (never clocks), and get a fresh interpreter per engine call
  (what lasts goes in AI memory or mod data, which are saved and checksummed); every AI
  answer is journaled with the turn. The engine never runs a script inside its own
  loops: events wait for the next safe point. Mods carry no native code.
- SDK work leaves unmodded games unchanged: with `stateChecksum` hashing at format 8
  (`serial::hash(s, 8)`, temporarily), the engine must reproduce the determinism goldens
  recorded before the SDK (tests/test_determinism.cpp at fc5a7f2^) and the serialize
  test's 0x933770c7b7260925. New state is read and written only from the format that
  added it; 0.11.0 shipped save format 9 and protocol 7, so the next new field bumps them.
- Both render backends must look the same. Shaders live once in `shaders/`.
- Stay warning-free under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`.
- Tests use only our own fixtures (`tests/fixtures/`). Anything that touches the
  installed data set is opt-in via `OPENSE4_CLASSIC_DATA`.

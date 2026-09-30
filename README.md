# OpenSE4

**An open-source engine reimplementation for Space Empires IV Deluxe.**

OpenSE4 is a new engine, written from scratch in C++23, with Vulkan rendering and an
OpenGL fallback. It is being built to play Space Empires IV Deluxe faithfully, the way
OpenXcom plays UFO: Enemy Unknown or OpenTTD plays Transport Tycoon Deluxe.

- **You need your own copy of Space Empires IV Deluxe.** It is available on Steam.
  OpenSE4 reads the game's data files, art and sound from your installation at
  runtime. None of the original game ships with this project.
- **Clean-room.** The rules are reimplemented from the manual, the game's documented
  data formats and observation of the running game. The original executable is never
  decompiled or disassembled. See [docs/CLEANROOM.md](docs/CLEANROOM.md).
- **Playable.** Every part of the classic rules is implemented:
  - economy and population;
  - ships and movement;
  - combat, with replays;
  - research, intelligence, diplomacy and events;
  - computer players.

  The game has the classic windows, runs hotseat, network (with UPnP) and PBEM
  multiplayer, and includes a dedicated server. [docs/PARITY_PLAN.md](docs/PARITY_PLAN.md)
  tracks what remains for full parity:
  - tactical combat;
  - sound;
  - open rule questions to settle against the original.

`opense4` starts the classic game when it finds your install. Without one, or with
`--prototype`, it runs a **prototype game** with our own simplified rules and content.
See [docs/DESIGN.md](docs/DESIGN.md) and [docs/ENGINE.md](docs/ENGINE.md).

## Building

Full instructions for Linux, Windows and macOS are in
[docs/BUILDING.md](docs/BUILDING.md). In short, you need:

- a C++23 compiler (GCC 14+, Clang 18+, MSVC 17.10+), CMake 3.25+ and Ninja;
- SDL3, which CMake fetches if the system has none;
- `glslc`, from the Vulkan SDK or `shaderc`.

```sh
cmake --preset debug              # or: release, asan
cmake --build --preset debug
ctest --preset debug
./build/debug/opense4
```

On Arch Linux, install them with `pacman -S cmake ninja sdl3 shaderc vulkan-headers`.

## Setting up the game data

Classic mode reads your installed copy of Space Empires IV Deluxe in place.
[docs/SETUP.md](docs/SETUP.md) explains:

- how to get the files, including on Linux and macOS;
- how OpenSE4 finds them;
- how to check a data set or a mod with `opense4-datacheck`.

## Multiplayer

Games are hosted from the client or with the dedicated `opense4-server`.

- **TCP:** direct play over the network. The default port is 6720, and the router is
  forwarded automatically over UPnP when the router allows it.
- **Hotseat:** several players on one machine.
- **PBEM:** turn files passed by mail or a shared folder.

See [docs/MULTIPLAYER.md](docs/MULTIPLAYER.md).

## Running

```
opense4 [--renderer=auto|vulkan|opengl] [--seed=N] [--systems=N] [--empires=N]
        [--shape=spiral|elliptical|ring|clusters] [--race=human|vashk|seren|oru|ilthari|kethra]
```

`--help` lists every option. With `--renderer=auto` (the default) the game tries
Vulkan 1.3 and falls back to OpenGL 3.3 if Vulkan is missing or unsuitable.

### Classic mode (needs the original game installed)

```sh
./build/debug/opense4                                # auto-detects a Steam install
./build/debug/opense4 --classic-dir=/path/to/se4     # or point at it
./build/debug/opense4 --quick-start=Terran           # skip the intro
./build/debug/opense4-datacheck                      # validate an installed or modded data set
./build/debug/opense4-server --players=2 --ai=3      # host a network game without playing
```

The intro offers Quick Start, New Game (the full game and empire setup), Load Game and
Multiplayer. In the game, the classic hotkeys work: F1–F12 open the windows and End
Turn, and letter keys give orders. See [docs/SETUP.md](docs/SETUP.md) and
[docs/MULTIPLAYER.md](docs/MULTIPLAYER.md).

Headless screenshots, used for testing, work with SDL's offscreen driver:

```sh
SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4 --quick-start=Terran --turns=30 --open=colonies --screenshot=shot.png
SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4 --prototype --seed=42 --turns=40 --screenshot=proto.png
```

### Prototype controls

| Input | Action |
|---|---|
| Left click | Select. Click the same sector again to cycle through what's in it |
| Double click | Open a system / follow a warp point |
| Right click | Move the selected ship, or colonize the planet under the cursor |
| Drag, middle drag, WASD, arrows | Pan |
| Mouse wheel | Zoom |
| Tab | Select the next idle ship |
| Enter | End turn |
| Esc / G | Back to the galaxy map |
| F1 / F2 / F3 / F9 | Controls / Research / Event log / Renderer info |
| Alt+Enter | Toggle fullscreen |

## Modding

Game content lives in plain TOML files in [`data/`](data), in the spirit of SE4's
text data files. They define technologies, hulls, components, facilities, races,
starting designs and core rules. The loader reports mistakes with the file, entry
and field, and rejects unknown fields to catch typos:

```
hulls.toml:95 [hull 'broken']: unknown field 'speeed'
```

## Layout

```
src/core      math, deterministic RNG, typed ids, logging
src/datafile  reader for the classic "Key := Value" data format
src/ruleset   typed model of a complete classic data set
src/game      classic-rules engine (clean-room, implemented from docs/spec/)
src/net       multiplayer: sessions, protocol, UPnP port mapping
src/server    opense4-server: dedicated host and PBEM turn processor
src/assets    runtime access to the installed classic art
src/sim       the prototype's own simplified rules (headless, deterministic)
src/gfx       RHI with Vulkan and OpenGL backends, 2D batch renderer, ImGui bridge
src/client    the app shell plus two modes: prototype and classic
tools/        opense4-datacheck, opense4-observe (drive the original), cleanroom_check.py
docs/spec/    clean-room rules specs, written in our own words
shaders/      GLSL shared by both backends
data/         the prototype's content (TOML)
assets/       fonts (Noto Sans, SIL OFL)
tests/        doctest unit tests (our own fixtures only)
```

## License

Code: not yet chosen. The Noto Sans fonts are under the SIL Open Font License
(see `assets/fonts/OFL.txt`).

## Trademarks

Space Empires is a trademark of its respective owner. OpenSE4 is an independent
project. It is not affiliated with, endorsed by or sponsored by Strategy First or
Malfador Machinations. The name is used only to say which game this engine is for.

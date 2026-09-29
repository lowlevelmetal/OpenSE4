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
- **Work in progress.** `opense4 --classic` currently generates and browses quadrants
  from your data set. [docs/PARITY_PLAN.md](docs/PARITY_PLAN.md) tracks the road to
  full parity.

Until the engine is playable end to end, `opense4` without `--classic` runs a
**prototype game** with our own simplified rules and content: explore, colonize,
research, build and fight against computer empires. See [docs/DESIGN.md](docs/DESIGN.md).

## Building

Requirements:

- A C++23 compiler (GCC 14+, Clang 18+, MSVC 17.10+), CMake 3.25+ and Ninja.
- SDL3, taken from the system or fetched automatically.
- `glslc`, from the Vulkan SDK or the `shaderc` package, used to compile shaders at build time.

Other dependencies are fetched at pinned versions with verified hashes: Dear ImGui,
volk, VMA, Vulkan-Headers, toml++, stb and doctest. The Vulkan loader is *not* linked.
It is loaded at runtime, so the game still starts on machines without Vulkan.

```sh
cmake --preset debug              # or: release, asan
cmake --build --preset debug
ctest --preset debug              # unit tests for the game rules
./build/debug/opense4
```

On Arch Linux: `pacman -S cmake ninja sdl3 shaderc`. Install `vulkan-validation-layers`
too if you want `--validation` to do anything.

## Running

```
opense4 [--renderer=auto|vulkan|opengl] [--seed=N] [--systems=N] [--empires=N]
        [--shape=spiral|elliptical|ring|clusters] [--race=human|vashk|seren|oru|ilthari|kethra]
```

`--help` lists every option. With `--renderer=auto` (the default) the game tries
Vulkan 1.3 and falls back to OpenGL 3.3 if Vulkan is missing or unsuitable.

### Classic mode (needs the original game installed)

```sh
./build/debug/opense4 --classic                  # auto-detects a Steam install
./build/debug/opense4 --classic-dir=/path/to/se4 --quadrant="Spiral Arm" --systems=80
./build/debug/opense4-datacheck                      # validate an installed or modded data set
```

Classic mode currently covers milestone 1: it generates a quadrant from your data set
and lets you browse it in the classic main-window layout.

Headless screenshots, used for testing, work with SDL's offscreen driver:

```sh
SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4 --seed=42 --turns=40 --screenshot=shot.png
```

### Controls

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
src/game      classic-rules engine (clean-room; milestone by milestone)
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

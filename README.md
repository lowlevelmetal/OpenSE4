# OpenSE4

**An open-source engine reimplementation for Space Empires IV Deluxe.**

OpenSE4 is a new engine, written from scratch in C++23, with Vulkan rendering and an
OpenGL fallback. It is being built to play Space Empires IV Deluxe faithfully, the way
OpenXcom plays UFO: Enemy Unknown or OpenTTD plays Transport Tycoon Deluxe.

- **You need your own copy of Space Empires IV Deluxe.** It is available on Steam.
  OpenSE4 reads the game's data files, art and sound from your installation at
  runtime. None of the original game ships with this project.
- **Written from specs, nothing copied.** The rules are reimplemented from the manual,
  the game's documented data formats, observation of the running game and, since
  2026-09-29, analysis of the original executable. Findings are written up as
  plain-language specs and the code is written from those; no code, data, art or
  text from the original is copied or shipped. The game's license forbids reverse
  engineering, so this project is not clean-room in the strict sense. See
  [docs/CLEANROOM.md](docs/CLEANROOM.md).
- **Playable.** Every part of the classic rules is implemented:
  - economy and population;
  - ships and movement;
  - combat: strategic and tactical, with replays and the combat simulator;
  - research, intelligence, diplomacy and events;
  - computer players.

  The game has the classic windows, runs hotseat, network (with UPnP) and PBEM
  multiplayer, and includes a dedicated server. [docs/PARITY_PLAN.md](docs/PARITY_PLAN.md)
  tracks what remains for full parity:
  - sound;
  - open rule questions to settle against the original.

`opense4` finds your install on its own, or takes its location with `--classic-dir`.
Without an install it explains where it looked and exits: there is no game to play
without your copy. [docs/ENGINE.md](docs/ENGINE.md) is the map of the engine's code.

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

OpenSE4 reads your installed copy of Space Empires IV Deluxe in place.
[docs/SETUP.md](docs/SETUP.md) explains:

- how to get the files, including on Linux and macOS;
- how OpenSE4 finds them;
- how to check a data set or a mod with `opense4-datacheck`.

## Multiplayer

Games are hosted from the client or with the dedicated `opense4-server`.

- **TCP:** direct play over the network, with simultaneous turns or one player after
  another. The default port is 6720, and the router is forwarded automatically over
  UPnP when the router allows it.
- **Hotseat:** several players on one machine.
- **PBEM:** turn files passed by mail or a shared folder.

See [docs/MULTIPLAYER.md](docs/MULTIPLAYER.md).

## Running

```
opense4 [--renderer=auto|vulkan|opengl] [--classic-dir=DIR] [--quick-start[=RACE]] [--seed=N]
```

`--help` lists every option. With `--renderer=auto` (the default) the game tries
Vulkan 1.3 and falls back to OpenGL 3.3 if Vulkan is missing or unsuitable.

```sh
./build/debug/opense4                                # auto-detects a Steam install
./build/debug/opense4 --classic-dir=/path/to/se4     # or point at it
./build/debug/opense4 --quick-start=Terran           # skip the intro
./build/debug/opense4-datacheck                      # validate an installed or modded data set
./build/debug/opense4-server --players=2 --ai=3      # host a network game without playing
```

On Windows, the release's `-setup.exe` installs OpenSE4 for all users. It goes into
Program Files with a Start menu entry and, if you choose, a desktop shortcut, and you
remove it from Apps & features. The zip holds the same programs to run from any folder.
Saved games and settings live in `%APPDATA%\OpenSE4` either way.

On Linux, the game can appear in the desktop's application list (GNOME, KDE Plasma
and other freedesktop.org desktops):

- **Release package:** run `./install-desktop-entry.sh` in the unpacked folder. It adds
  the entry and icon for your user and starts the game from that folder. Run it again
  after moving the folder; `--uninstall` removes the entry.
- **Source build:** `cmake --install build/release --prefix ~/.local` (or
  `/usr/local`, with `sudo`) installs the programs, the desktop entry, the icons and
  the AppStream metadata.

The intro offers Quick Start, New Game (the full game and empire setup), Load Game and
Multiplayer. In the game, the classic hotkeys work: F1–F12 open the windows and End
Turn, and letter keys give orders. See [docs/SETUP.md](docs/SETUP.md) and
[docs/MULTIPLAYER.md](docs/MULTIPLAYER.md).

Headless screenshots, used for testing, work with SDL's offscreen driver:

```sh
SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4 --quick-start=Terran --turns=30 --open=colonies --screenshot=shot.png
SDL_VIDEO_DRIVER=offscreen ./build/debug/opense4 --quick-start=Terran --turn-style=simultaneous --turns=20 --screenshot=sim.png
```

## Modding

Mods for the original game are replacement data files, and sometimes replacement
art. OpenSE4 plays whatever data set the game directory holds: apply the mod to a
copy of the game directory and pass that copy with `--classic-dir`. Run
`opense4-datacheck` on it first. It loads every data file and reports anything it
does not understand with the file, line and record. See
[docs/SETUP.md](docs/SETUP.md#mods).

## Layout

```
src/core      math, deterministic RNG, typed ids, logging
src/datafile  reader for the classic "Key := Value" data format
src/ruleset   typed model of a complete classic data set
src/game      classic-rules engine (implemented from docs/spec/)
src/net       multiplayer: sessions, protocol, UPnP port mapping
src/server    opense4-server: dedicated host and PBEM turn processor
src/assets    runtime access to the installed classic art
src/gfx       RHI with Vulkan and OpenGL backends, 2D batch renderer, ImGui bridge
src/client    the app shell and the classic client (windows, front end, multiplayer)
tools/        opense4-datacheck, opense4-observe (drive the original), cleanroom_check.py
docs/spec/    rules specs, written in our own words
shaders/      GLSL shared by both backends
assets/       fonts (Noto Sans, SIL OFL)
packaging/    Linux desktop entry, application icon and AppStream metadata
tests/        doctest unit tests (our own fixtures only)
```

## License

OpenSE4 is free software: you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version. See
[LICENSE](LICENSE).

The Noto Sans fonts are under the SIL Open Font License (see
`assets/fonts/OFL.txt`). Bundled third-party libraries keep their own licences,
which the release packages list in `THIRD_PARTY_NOTICES.txt`. The GPL covers only
OpenSE4's own code and content; the original game's files stay the property of
their owners and are never part of OpenSE4.

## Trademarks

Space Empires is a trademark of its respective owner. OpenSE4 is an independent
project. It is not affiliated with, endorsed by or sponsored by Strategy First or
Malfador Machinations. The name is used only to say which game this engine is for.

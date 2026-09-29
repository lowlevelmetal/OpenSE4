# Design

> **Direction update.** The project's primary goal is now a faithful, clean-room
> reimplementation of the classic game's rules, run on the player's own installed
> data. See [PARITY_PLAN.md](PARITY_PLAN.md), [CLEANROOM.md](CLEANROOM.md) and
> [spec/](spec/). This document describes the original prototype (`src/sim`), which
> stays playable until the classic engine (`src/game`) replaces it. Its
> architecture principles (headless deterministic rules, the command layer, the
> Vulkan/GL RHI) carry over.

## Vision

A spiritual successor to **Space Empires IV Deluxe**. The goal is to keep what made SE4 great:

- **Deep, moddable rules.** Nearly everything is data: technologies, hulls, components,
  facilities, races. Modders edit text files; the engine asks "how much of ability X
  does this have" and never hard-codes a specific part.
- **Ship design as the heart of the game.** Hulls with tonnage, and components (bridge,
  life support, crew, engines, weapons, armor, shields, colony modules) that
  compete for space.
- **Turn-based, grid-based space.** A galaxy of star systems linked by warp points. Each
  system is a sector grid; ships move sector by sector.
- **Simultaneous movement** (SE4's multiplayer mode). Everyone's ships move at once,
  interleaved by speed, so a faster ship really is faster within a turn.
- **Readable 2D presentation.** Clear icons, dense information panels and tooltips
  everywhere.

We add what an SE4 fan misses today: modern rendering, HiDPI, fast turns, deterministic
simulation (replays, desync detection, play-by-email) and friendlier modding errors.

Names, text and art are original. Mechanics are inspired by SE4, but no data or assets
are copied.

## Architecture

```
core ─┬─> sim ────────────┐
      └─> gfx (RHI) ───────┴─> client (opense4 executable)
```

- **`sim`** is the whole game and nothing else. It has no rendering or SDL dependencies
  and is fully unit-tested. It is deterministic: the seed plus the command stream
  fully determines the game.
  - `Content` is the immutable rules database loaded from `data/*.toml`.
  - `GameState` is plain data.
  - `applyCommand` validates and applies player intent (`MoveShip`, `Colonize`,
    `BuildShip`, …). Human players and the AI use this same path, and commands are
    small value types, ready for serialization (PBEM, network).
  - `advanceTurn` runs the AI's orders, then `processTurn` resolves movement →
    combat → colonization → economy/research → construction → growth.
  - Integer math only during turn resolution. Galaxy *generation* uses floats
    (`sqrt`, `cos`, `log`), and their results can differ between compilers and math
    libraries. So "same seed, same galaxy" only holds on one platform, and
    multiplayer must send the generated `GameState`, not the seed.
  - Orders only use what the issuing empire knows. Routes can't go through
    warp points in unexplored systems, and colonize orders need an explored
    system. This matters for untrusted PBEM clients.
  - `stateChecksum` gives a cheap desync and determinism check. A test plays 60
    all-AI turns twice and compares the checksum every turn.
- **`gfx`** is a deliberately small RHI: textures plus batches of transient 2D
  geometry, drawn with one premultiplied-alpha uber shader (textured, SDF
  disc/ring, additive glow, antialiased line).
  - *Vulkan 1.3*: dynamic rendering, synchronization2, VMA, volk, two frames in
    flight. The loader is `dlopen`ed via SDL, so a missing Vulkan runtime is not
    fatal.
  - *OpenGL 3.3 core*: the fallback, with the same shader source (`#ifdef VULKAN`
    for bindings) and the same blending and framebuffer format, so both backends
    look the same. Diffing screenshots shows under 0.5% of pixels differ, all at
    antialiased edges and by at most 15/255.
  - The Dear ImGui UI is drawn through the same RHI (`ImGuiRenderer`, using ImGui
    1.92's dynamic texture protocol), so the UI needs no per-backend code.
- **`client`** holds the SDL3 app, the galaxy and system map views, and the ImGui
  panels. Views read `GameState` directly and write only through commands.

### Why a small RHI instead of bgfx/Diligent/SDL_GPU?

The project asked for Vulkan with an OpenGL fallback, and a 2D strategy game needs
very little: textures and streamed triangles. About 1,000 lines of Vulkan and 400 of
GL cover it completely and keep full control. The interface (`gfx/device.hpp`) can
grow (more pipelines, offscreen targets for bloom or post-processing) without
touching game code.

## What works now (v0.1)

- Galaxy generation: spiral, elliptical, ring and cluster shapes. The warp network
  is a Euclidean MST plus extra non-crossing lanes, so it is always connected and
  planar. Star classes, planets on orbits (surface, atmosphere, size, resource
  values) and procedural names.
- Six races (native surface and atmosphere, bonus techs) with farthest-apart homeworld placement.
- Exploration (per-empire knowledge), visibility of enemy ships only where you have presence.
- Ship movement with Dijkstra pathfinding across sectors and warp points. Moves are
  interleaved by speed, and ships are intercepted when they enter a hostile sector.
- Automatic combat: weapons, shields that recharge each battle, structure damage,
  and repairs at space yards.
- Colonization with surface-specific colony modules. Population growth, reduced
  capacity on worlds with unbreathable air.
- Economy: minerals, organics and radioactives from facilities, scaled by planet
  richness and population. Facility slots by planet size.
- Construction queues: ships need a space yard. Spending is rate-limited per turn,
  and cancelling refunds what was spent.
- Research: tech areas with levels, prerequisites, a per-level queue, and gating of
  hulls, components and facilities.
- A computer player that explores, colonizes, researches and builds.
- UI: galaxy map and system map; selection panels for systems, planets and ships;
  colony management; research; event log; a new-game dialog; tooltips.

## Roadmap

Roughly in priority order:

1. **Ship designer.** Create designs from available hulls and components, with live
   stats and validation (`computeDesignStats` already reports problems), and mark old
   designs obsolete.
2. **Fleets**: group ships, move them together at the slowest speed, and set stances.
3. **Supply and fuel.** Movement costs supply, refilled at spaceports and resupply depots (SE4-style).
4. **Tactical combat viewer**: replay or command battles on a combat grid. Weapon
   range and accuracy already exist in the data.
5. **Planetary combat**: planetary weapons and shields, troops and invasion, capturing colonies.
6. **Diplomacy**: treaties, trade, alliances, and an intel/espionage layer
   (`atWar` is the single hook).
7. **Save and load**: serialize `GameState` plus a command log (replays for free).
8. **Multiplayer**: hot-seat first, then PBEM (commands per empire per turn), then
   network games. Determinism and checksums are already in place.
9. **Better AI**: threat assessment, war planning, design upgrades, and a
   personality per race.
10. **Presentation**: ship and planet art (sprites through `Renderer2D::sprite`),
    animated movement between turns, sound, and a custom UI skin.
11. **Content**: many more techs, components, facilities, space monsters, ruins,
    asteroid fields, nebulae and storms.
12. **Balance.** Resources pile up by the midgame, so costs, rates and AI spending
    need tuning. A good start: an automated all-AI harness that reports economy
    curves.

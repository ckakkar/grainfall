# Grainfall — architecture and team contract

Grainfall is a falling-sand chemistry sandbox: a 2D grid of cells (sand, water,
oil, lava, fire, acid, gunpowder, ...) with gravity, fluids, gases, heat flow,
phase changes, combustion and explosions, rendered with SDL3.

## Layout and ownership

| Path | Owner | What |
|---|---|---|
| `CMakeLists.txt`, `src/core/*`, `tests/testing.hpp`, `tests/test_main.cpp`, `tests/test_core.cpp`, `docs/ARCHITECTURE.md` | lead (Claude) | shared contract |
| `src/app/*` | lead (Claude) | SDL3 window, renderer, input, HUD, `--bench` |
| `src/sim/*`, `tests/test_sim.cpp` | partner (Codex) | the simulation engine |
| `src/io/*`, `tests/test_io.cpp` | intern (agy) | `.grain` save/load |
| `src/scenes/*`, `tests/test_scenes.cpp` | intern (agy) | starter scenes |

Edit only files you own. If you need a change in a file someone else owns
(for example a new field in `Cell`), say so in your report instead of editing it.
Nobody but the lead touches git.

## Build

```sh
# Engine + tests only (no SDL needed). Use YOUR build directory:
cmake -S . -B build-<you> -DGRAINFALL_BUILD_APP=OFF && cmake --build build-<you> -j 8
./build-<you>/grainfall_tests            # all tests
./build-<you>/grainfall_tests sim_       # filter by name substring
```

Build directories: lead `build/`, partner `build-codex/`, intern `build-agy/`.

## Core contract (src/core)

- `World` is a `width x height` grid of `Cell`, y grows downward, row-major.
- `Cell` = material, `shade` (render noise), `clock`/`flags`/`aux` (sim-owned
  scratch), `life` (lifetime in ticks for transient materials, 0 = forever),
  `temp` (degrees C).
- `World::make_cell(m)` / `set` / `paint_circle` create fresh cells with the
  material's spawn temperature and a random lifetime.
- All randomness goes through `world.rng()`. Same seed + same edits => same world.
- Material properties live in one table in `src/core/material.cpp`. Only append
  new materials to the enum (the save format stores the enum value).

## Simulation rules (src/sim) — what "correct" means

Each `Simulation::step()` advances one tick:

1. **Movement**
   - Powder (sand, gunpowder): fall straight down if the cell below is empty or a
     less dense liquid/gas (swap). Otherwise try down-left / down-right (random
     order). Piles form at roughly 45 degrees.
   - Liquid: fall like powder; otherwise move sideways up to `dispersion` cells
     (stop at the first blocked cell). Denser liquids sink through lighter
     liquids (oil floats on water, lava sinks through water while reacting).
   - Gas: mirror image of liquid — rise, drift sideways, lighter gases rise
     through heavier gases and through liquids/powders only by swapping with
     *gas or empty* (gas never displaces liquids/powders).
   - Static cells never move. A cell moves at most once per tick (use `clock`).
   - Avoid directional bias: alternate horizontal scan direction per tick/row.
2. **Heat**: each tick heat diffuses between neighbouring cells, weighted by both
   cells' `conductivity`. Empty cells relax toward `kAmbientTemp`. Metal spreads
   heat fast; wood and oil slowly. Total heat need not be conserved exactly but
   must be stable (no blow-ups, no NaN).
3. **Phase changes** (with a little randomness so fronts look organic):
   water > 100 C -> steam; water < 0 C -> ice; ice > 0 C -> water;
   steam < 90 C -> water (also: steam at end of life -> water with 30% chance,
   else vanishes); lava < 700 C -> stone; stone or sand > 1200 C -> lava.
4. **Combustion**: fire has a lifetime; when it runs out it becomes smoke (40%)
   or empty. Fire heats neighbours strongly. A flammable cell next to fire, or
   hotter than its `ignite_temp`, ignites with probability `flammability` per
   tick. Wood and plant burn for a while (`kFlagBurning`, spawning fire into
   empty neighbours) before turning to fire/smoke; oil turns into fire almost
   immediately; gunpowder detonates.
5. **Reactions**: lava + water -> stone + steam; acid dissolves neighbouring
   cells that are not stone, metal, acid or empty (acid is consumed with some
   probability, sometimes leaving smoke); plant grows into adjacent water
   slowly; ice slowly freezes adjacent water while it is below 0 C.
6. **Explosions** (`explode`): clears or converts cells within the radius
   (stone and metal survive; stone may crack to sand near the centre), fills
   part of the blast with fire, heats everything, flings movable cells outward,
   and chains into nearby gunpowder.
7. **Chunks**: the world is split into `kChunkSize`-square chunks. A chunk with
   no changes for a tick goes to sleep and is skipped until something nearby
   changes or `wake_rect` is called. Hot cells and transient cells keep their
   chunk awake. This is an optimisation only — results must match the same
   world simulated with every chunk awake as far as tests can tell (counts,
   no lost or duplicated cells).

Performance target: a 320x200 world fully awake steps in under 4 ms on this
machine in a Release build; mostly-settled worlds should be much faster.

## Save format (src/io)

Specified in `src/io/save.hpp`. Round-trip must be exact for every saved field.

## App (src/app)

SDL3 window, world drawn into a streaming texture (one texel per cell, scaled),
heat-view overlay, chunk-debug overlay, brush tools, material palette, HUD via
`SDL_RenderDebugText`, save/load with F5/F9, scene cycling, and a headless
`grainfall --bench N` that steps a scene N ticks and prints timing.

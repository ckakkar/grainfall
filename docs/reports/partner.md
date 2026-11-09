# Simulation partner report

Implemented the complete simulation engine in `src/sim/simulation.cpp` and
private additions to `src/sim/simulation.hpp`, with 25 `sim_` tests in
`tests/test_sim.cpp`. The public API is unchanged. No additional simulation
source files or CMake changes are needed. No git state was changed.

## Design

- Movement uses swaps, density checks, alternating row scan directions, and
  separate downward/upward passes. Non-empty participants receive movement stamps;
  vacated empty positions can be reused immediately by following particles.
  A full 64-bit stamp supplements `Cell::clock`, so byte wraparound and stale
  clocks in sleeping cells cannot cause duplicate moves or skipped particles.
  The stamp starts from the saved world tick when reconstructing a simulation.
- Heat uses a synchronous Jacobi pass with harmonic conductivity weights. The
  four-neighbour weights sum to at most 0.8; empty cells relax to ambient.
  Temperatures are sanitized and bounded to [-273.15, 20000] C, including NaN
  and infinity supplied by external edits.
- Lava/water contact reactions happen before diffusion and phase changes, so
  contact always produces stone and steam. Other reactions include acid
  dissolution/consumption, plant growth, and freezing beside ice.
- Phase transitions use a 35% per-tick chance and boiling/condensation
  hysteresis. Failed transitions stay awake so an ambient-temperature ice cell
  will still melt. Transient lifetimes, 30% steam condensation on expiry, and
  40% smoke on fire expiry are implemented.
- Combustion uses the material table's ignition temperatures and probabilities.
  Wood/plant retain burning flags and counters before becoming fire or smoke;
  oil changes quickly, and gunpowder detonates. Flames heat neighbours toward
  1100 C at up to 85 C per tick, tapering near the ceiling. Flame cells stay
  between 800 and 1100 C when evolved or created by the engine. Existing hotter
  lava/explosion debris is not artificially cooled by neighbouring flames.
- Explosions use an iterative queue for chains. Gunpowder is consumed when
  queued, preventing duplicate detonation in overlapping blasts. Blasts heat,
  destroy or convert cells, fling movable cells outward into empty cells, leave
  fire/smoke, preserve metal, and optionally crack central stone into sand.
  Large radii include diagonal corners and are bounded before iteration.
- Dirty chunks wake a one-chunk halo. Nonlocal blast changes wake affected
  chunks during the same tick, including flying debris. Hot/transient cells
  and pending stochastic reactions remain awake. Settled cells consume no RNG,
  so forced-awake idle chunks do not perturb chemistry elsewhere. All random
  decisions use `World::rng()`. Every step fills `SimStats`.

## Liquid settling correction

The water repro exposed two separate causes. Stamping the empty source of a
move prevented a falling column from following its leading cell during the
same tick, while sideways dispersion could still create more vacancies. This
produced 609 holes and 35,011 moves over the final 50 ticks of the new regression
at tick 1500. Allowing empty vacancies to be reused removed all holes, but
unconditional lateral moves still produced 1,250 moves over those 50 ticks.

Liquid lateral movement now requires column pressure or a reachable downward
outlet. Pressure is transmitted through contiguous liquid in a row; checking
only directly above a free edge would leave a staircase. Each actual lateral
move still stops at the first blocked cell and respects material dispersion.
Gas keeps its ordinary sideways drift. Liquid changes wake horizontal chunk
strips once per tick, including the affected load below, so distant free edges
cannot sleep through a change in transmitted pressure. At equilibrium this
extra waking stops and the whole pool sleeps.

`sim_water_settles_flat` uses the exact 120x60, seed-5, 38x44 water-block repro.
`sim_oil_water_settle_layered` starts the same volume with water above oil. By
tick 1450 both have zero holes, surface-height spread 1, preserved counts, and
correct oil/water ordering. Ticks 1451-1500 have zero moves, most chunks sleep,
and forcing every chunk awake still gives zero moves. The original standalone
3000-tick repro now reports **zero moves and 0/32 chunks awake**.

## Verification

The requested command succeeded in the default Release configuration:

```sh
cmake -S . -B build-codex -DGRAINFALL_BUILD_APP=OFF
cmake --build build-codex -j 8
./build-codex/grainfall_tests sim_
```

Result: **25/25 simulation tests passed** as part of the complete project runner,
which passed **52/52 tests**.

Coverage includes sand piles, oil/water separation, powder/liquid and gas
density swaps, dispersion barriers, all phase transitions, heat transfer,
transient expiry, burning wood/plant/oil, gunpowder chains, stone/metal blast
survival, flinging and radius clipping, acid resistance, growth/freezing,
particle identity and material counts over 650 ticks, identical seeded worlds
after 500 ticks, continuation after reconstructing a Simulation, chunk sleeping
and waking, full versus sleeping state/RNG equality, and chain blasts reaching
distant sleeping chunks.

The requested `sim_large_fire_temperature_is_bounded` burns a 40x24 wooden
structure for 600 ticks. Its measured peak was **1100 C**, all wood burned,
all 40 stone and 40 sand cells survived, and no lava formed.

The final full-project AddressSanitizer/UndefinedBehaviorSanitizer run passed
**52/52 tests without diagnostics**, including all 25 simulation tests, in
`build-codex/sanitized`, configured with Debug and
`-fsanitize=address,undefined -fno-omit-frame-pointer`:

```sh
cmake --build build-codex/sanitized -j 8
./build-codex/sanitized/grainfall_tests
```

## Release timing

Each fully awake case forces all 260 chunks awake before every tick and
averages 180 ticks, including the initial reactive blast activity:

| 320x200 workload | Mean step time |
| --- | ---: |
| All 15 materials, heat and reactions | 2.31 ms |
| Dense sand/water/oil/gunpowder/empty movement | 2.83 ms |
| Settled sand floor, 1000 sleeping ticks | 0.0039 ms |

Both awake workloads enforce the under-4-ms requirement in Release tests.

## Known gaps and integration notes

No known missing simulation rules. Chemistry is deliberately stochastic, and
individual large chain explosions may exceed 4 ms; the required mean remains
under the target. External painting/loading must call `wake_rect`/`wake_all` as
specified by the API. Recreate the Simulation if replacing the World with new
dimensions; the app already does this on load. No changes to shared core types
were required.

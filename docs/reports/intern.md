# Intern Report: World Save & Load (`.grain`) Implementation

**Author:** intern (agy)  
**Task:** Implement `.grain` file format save and load routines and tests.  
**Assigned Files:** `src/io/save.cpp`, `tests/test_io.cpp`

---

## 1. Summary of Changes

### `src/io/save.cpp`
- **`fnv1a`**: Implemented standard 32-bit FNV-1a checksum algorithm over arbitrary byte spans (`offset_basis = 0x811c9dc5`, `prime = 0x01000193`).
- **`encode`**:
  - Emits 32-byte header: magic `"GRN1"`, version `1` (uint16), reserved `0` (uint16), width (uint32), height (uint32), seed (uint64), tick (uint64).
  - Encodes cells in row-major order using run-length encoding (RLE). Consecutive cells are compared for equality across all saved fields (`mat`, `shade`, `flags`, `life`, `aux`, and bit-exact `temp` via `std::bit_cast`).
  - Run lengths are capped at 65,535 (`0xffff`) to fit in uint16, splitting larger uniform regions into multiple runs.
  - Serializes all integers little-endian explicitly byte by byte (no struct `memcpy`).
  - Appends 32-bit FNV-1a checksum of all preceding bytes.
- **`decode`**:
  - Full structural and bounds validation against every malformed condition listed in `save.hpp`.
  - Rejects: wrong magic, unknown version, nonzero reserved bytes, out-of-range dimensions (< 1 or > 4096), material byte $\ge$ `Material::Count`, runs overflowing `width * height`, too few cells, trailing bytes before checksum, truncation at any point, and checksum mismatch.
  - Ensures safe bounds checking throughout so arbitrary or corrupted inputs never read out of bounds or crash.
  - Initializes `Cell::clock` to `0` on load.
- **`save_world` & `load_world`**:
  - Binary file stream operations with clear error descriptions on I/O failures.

### `tests/test_io.cpp`
Implemented 16 comprehensive tests using the framework in `tests/testing.hpp`:
1. `io_fnv1a_vector`: Checks FNV-1a test vector `"a"` -> `0xe40c292c`, plus empty span -> `0x811c9dc5`.
2. `io_empty_world_size_under_100_bytes`: Verifies a 320x200 empty world encodes to 49 bytes (< 100 bytes) and decodes accurately.
3. `io_exact_round_trip_all_materials`: Verifies exact preservation of all 15 materials with varied temperatures (negative, zero, fractions, high), lifetimes, aux values, flags, shades, seed, tick, and confirms `clock` loads as 0.
4. `io_reject_wrong_magic`: Rejects invalid magic header.
5. `io_reject_unknown_version`: Rejects versions other than 1.
6. `io_reject_nonzero_reserved`: Rejects non-zero reserved fields.
7. `io_reject_out_of_range_dimensions`: Rejects width/height of 0 or > 4096.
8. `io_reject_invalid_material`: Rejects material byte $\ge$ `Material::Count`.
9. `io_reject_runs_overflow`: Rejects run count exceeding grid size.
10. `io_reject_too_few_cells`: Rejects run data terminating before total cells are read.
11. `io_reject_trailing_bytes`: Rejects extraneous data before checksum.
12. `io_reject_truncation`: Rejects truncated files at various cutoffs.
13. `io_reject_checksum_mismatch`: Rejects corrupted data and corrupted checksum.
14. `io_fuzz_random_flips_and_truncation`: 2,000 iterations of random bit flips and truncations; verifies `decode` never crashes or throws.
15. `io_save_and_load_world_file`: Tests file I/O round-trip through temp file and non-existent file error handling.
16. `io_large_run_count_splitting`: Verifies worlds with $> 65,535$ identical cells split into multiple runs cleanly.

---

## 2. Test Output Summary

Built and ran using:
```sh
cmake -S . -B build-agy -DGRAINFALL_BUILD_APP=OFF && cmake --build build-agy -j 8 && ./build-agy/grainfall_tests io_
```

Output:
```
[ RUN  ] io_fnv1a_vector
[  OK  ] io_fnv1a_vector
[ RUN  ] io_empty_world_size_under_100_bytes
[  OK  ] io_empty_world_size_under_100_bytes
[ RUN  ] io_exact_round_trip_all_materials
[  OK  ] io_exact_round_trip_all_materials
[ RUN  ] io_reject_wrong_magic
[  OK  ] io_reject_wrong_magic
[ RUN  ] io_reject_unknown_version
[  OK  ] io_reject_unknown_version
[ RUN  ] io_reject_nonzero_reserved
[  OK  ] io_reject_nonzero_reserved
[ RUN  ] io_reject_out_of_range_dimensions
[  OK  ] io_reject_out_of_range_dimensions
[ RUN  ] io_reject_invalid_material
[  OK  ] io_reject_invalid_material
[ RUN  ] io_reject_runs_overflow
[  OK  ] io_reject_runs_overflow
[ RUN  ] io_reject_too_few_cells
[  OK  ] io_reject_too_few_cells
[ RUN  ] io_reject_trailing_bytes
[  OK  ] io_reject_trailing_bytes
[ RUN  ] io_reject_truncation
[  OK  ] io_reject_truncation
[ RUN  ] io_reject_checksum_mismatch
[  OK  ] io_reject_checksum_mismatch
[ RUN  ] io_fuzz_random_flips_and_truncation
[  OK  ] io_fuzz_random_flips_and_truncation
[ RUN  ] io_save_and_load_world_file
[  OK  ] io_save_and_load_world_file
[ RUN  ] io_large_run_count_splitting
[  OK  ] io_large_run_count_splitting

16/16 tests passed in 9 ms
```
Full suite `./build-agy/grainfall_tests`: 20/20 passed in 3 ms.

---

## 3. Notes / Clarifications

- **Run counts $> 65,535$**: For worlds with large contiguous spans (e.g. 300x300 = 90,000 empty cells), runs exceeding uint16 max are split across multiple consecutive runs.
- **Float temp comparison**: Temp comparison in RLE uses `std::bit_cast<std::uint32_t>` for strict bit-for-bit equivalence matching the spec.


---

# Task 2: Save/Load Fix & Starter Scenes Implementation

**Task:**
1. Fix `decode()` in `src/io/save.cpp` to verify checksum and validate minimum file size before constructing `World`.
2. Implement 6–8 starter scenes in `src/scenes/scenes.cpp` and unit tests in `tests/test_scenes.cpp`.

---

## 1. Save/Load Fix (`src/io/save.cpp` + `tests/test_io.cpp`)
- **Early Checksum & Dimension Validation**:
  - `decode()` now computes and verifies the 32-bit FNV-1a checksum against the stored checksum before constructing `World`.
  - Added a minimum cell section size check: since each run can cover at most 65,535 cells and consumes at least 13 bytes, the cell section must contain at least $\lceil (\text{width} \times \text{height}) / 65535 \rceil \times 13$ bytes.
  - Files claiming large dimensions without sufficient payload are rejected immediately with a clear error (`"too few cells: file too small for dimensions"`), preventing any memory allocation for invalid or malicious inputs.
- **Unit Test**:
  - Added `io_rejects_tiny_file_claiming_huge_world` in `tests/test_io.cpp`: crafts a 36-byte file with a valid 4096×4096 header and valid FNV-1a checksum but zero cell runs, verifying immediate rejection without constructing `World`.

---

## 2. Starter Scenes (`src/scenes/scenes.cpp` + `tests/test_scenes.cpp`)

Implemented 7 distinct scenes highlighting materials and interactions:
1. **"Volcano"** (Scene 0, primary starter scene): An imposing stone volcano featuring a deep magma chamber, an eruption conduit, a bubbling crater lava pool, a crater lake, and foothill forests with wooden trunks and plant foliage.
2. **"Powder keg"**: Wooden structures, heavy stone roof weights, an underground oil reservoir, a central gunpowder stash, and an active burning fuse about to trigger a chain explosion.
3. **"Glacier"**: A massive ice shelf with snow dusting over deep ocean water, situated above a subterranean lava fissure and hydrothermal vent generating steam.
4. **"Acid rain"**: A suspended metal vat dripping acid drops onto a diverse cityscape consisting of acid-soluble wood buildings, acid-resistant stone monuments, impervious metal towers, and rooftop plant gardens.
5. **"Garden"**: Terraced stone and sand landscape with flowing irrigation aqueducts, footbridges, and abundant plant foliage.
6. **"Hourglass"**: Stone hourglass housing sand in the upper bulb that trickles through a narrow neck into the lower chamber.
7. **"Sandbox"** (Last entry): A pristine canvas bounded by 1-cell stone walls and floor for freeform building.

### Compliance with Architectural Rules:
- **World Cleared First**: Every builder calls `world.clear()` at the start.
- **Public API Only**: Built using `World` public methods (`set`, `paint_circle`, `try_at`, `width`, `height`, `rng`).
- **Deterministic**: Random variations (smoke drift, jitter) use `world.rng()`.
- **Resolution Scaling**: All coordinates scale relative to `world.width()` and `world.height()`, tested and verified across 64×64, 320×180, and 640×360.
- **Border Rule**: Every builder enforces a 1-cell stone border on left, right, and bottom edges via `apply_border(world)`.

---

## 3. Test Output Summary

Ran:
```sh
./build-agy/grainfall_tests scene_ && ./build-agy/grainfall_tests io_
```

Output:
```
[ RUN  ] scene_names_unique_and_non_empty
[  OK  ] scene_names_unique_and_non_empty
[ RUN  ] scene_builds_on_various_resolutions
[  OK  ] scene_builds_on_various_resolutions
[ RUN  ] scene_determinism
[  OK  ] scene_determinism
[ RUN  ] scene_border_rule_holds
[  OK  ] scene_border_rule_holds

4/4 tests passed in 11 ms
[ RUN  ] io_fnv1a_vector
[  OK  ] io_fnv1a_vector
[ RUN  ] io_empty_world_size_under_100_bytes
[  OK  ] io_empty_world_size_under_100_bytes
[ RUN  ] io_exact_round_trip_all_materials
[  OK  ] io_exact_round_trip_all_materials
[ RUN  ] io_reject_wrong_magic
[  OK  ] io_reject_wrong_magic
[ RUN  ] io_reject_unknown_version
[  OK  ] io_reject_unknown_version
[ RUN  ] io_reject_nonzero_reserved
[  OK  ] io_reject_nonzero_reserved
[ RUN  ] io_reject_out_of_range_dimensions
[  OK  ] io_reject_out_of_range_dimensions
[ RUN  ] io_reject_invalid_material
[  OK  ] io_reject_invalid_material
[ RUN  ] io_reject_runs_overflow
[  OK  ] io_reject_runs_overflow
[ RUN  ] io_reject_too_few_cells
[  OK  ] io_reject_too_few_cells
[ RUN  ] io_reject_trailing_bytes
[  OK  ] io_reject_trailing_bytes
[ RUN  ] io_reject_truncation
[  OK  ] io_reject_truncation
[ RUN  ] io_reject_checksum_mismatch
[  OK  ] io_reject_checksum_mismatch
[ RUN  ] io_rejects_tiny_file_claiming_huge_world
[  OK  ] io_rejects_tiny_file_claiming_huge_world
[ RUN  ] io_fuzz_random_flips_and_truncation
[  OK  ] io_fuzz_random_flips_and_truncation
[ RUN  ] io_save_and_load_world_file
[  OK  ] io_save_and_load_world_file
[ RUN  ] io_large_run_count_splitting
[  OK  ] io_large_run_count_splitting

17/17 tests passed in 6 ms
```

Full test suite (`./build-agy/grainfall_tests`): 47/47 tests passed in 1.3 s.


---

# Task 3: Scene Fixes (Hourglass Leak & Powder Keg Ignition)

**Task:**
1. Fix Hourglass in `src/scenes/scenes.cpp` to prevent diagonal sand leakage by constructing 4-connected walls ($\ge 2$ cells thick) and add test `scene_hourglass_holds_sand` in `tests/test_scenes.cpp`.
2. Fix Powder keg in `src/scenes/scenes.cpp` to reliably ignite the fuse by heating the tip above `ignite_temp` (400 °C, `kFlagBurning`), and add test `scene_powder_keg_explodes` in `tests/test_scenes.cpp`.

---

## 1. Summary of Changes

### `src/scenes/scenes.cpp`
- **Hourglass Wall Reinforcement**:
  - Replaced 1-cell diagonal wall drawing with thick ($\ge 3$ cells) 4-connected wall segments.
  - Successive rows connect horizontally and vertically so that no diagonal gap exists for powder particles to slip through.
  - Sand placement is constrained strictly within the inner bulb contours, leaving margins near the neck and outer frame.
- **Powder Keg Fuse Ignition**:
  - Replaced the transient `Material::Fire` gas cell with reliable thermal ignition: the first 3 cells of the fuse are heated to 400 °C (exceeding Wood's 300 °C `ignite_temp`) and tagged with `kFlagBurning` with ample burn time (`aux = 120`).
  - Fire reliably propagates down the wooden fuse, reaches the gunpowder stash, and triggers a full chain detonation.

### `tests/test_scenes.cpp`
Added 2 simulation-driven tests using `gf::Simulation`:
1. `scene_hourglass_holds_sand`:
   - Builds "Hourglass" on 320×180 and runs the simulation for 1,500 steps.
   - Verifies that total sand count is perfectly conserved (`CHECK_EQ(world.count(Material::Sand), initial_sand)`).
   - Verifies that zero sand particles escape the hourglass bounding box (`[70, 250] × [21, 158]`).
2. `scene_powder_keg_explodes`:
   - Builds "Powder keg" on 320×180 and runs the simulation for 2,500 steps.
   - Verifies that the fuse reaches the gunpowder, triggering an explosion that reduces the gunpowder count below 10% of its initial quantity (`CHECK(final_gunpowder < initial_gunpowder / 10)`).

---

## 2. Test Output Summary

Ran:
```sh
./build-agy/grainfall_tests scene_ && ./build-agy/grainfall_tests io_
```

Output:
```
[ RUN  ] scene_names_unique_and_non_empty
[  OK  ] scene_names_unique_and_non_empty
[ RUN  ] scene_builds_on_various_resolutions
[  OK  ] scene_builds_on_various_resolutions
[ RUN  ] scene_determinism
[  OK  ] scene_determinism
[ RUN  ] scene_border_rule_holds
[  OK  ] scene_border_rule_holds
[ RUN  ] scene_hourglass_holds_sand
[  OK  ] scene_hourglass_holds_sand
[ RUN  ] scene_powder_keg_explodes
[  OK  ] scene_powder_keg_explodes

6/6 tests passed in 1618 ms
[ RUN  ] io_fnv1a_vector
[  OK  ] io_fnv1a_vector
[ RUN  ] io_empty_world_size_under_100_bytes
[  OK  ] io_empty_world_size_under_100_bytes
[ RUN  ] io_exact_round_trip_all_materials
[  OK  ] io_exact_round_trip_all_materials
[ RUN  ] io_reject_wrong_magic
[  OK  ] io_reject_wrong_magic
[ RUN  ] io_reject_unknown_version
[  OK  ] io_reject_unknown_version
[ RUN  ] io_reject_nonzero_reserved
[  OK  ] io_reject_nonzero_reserved
[ RUN  ] io_reject_out_of_range_dimensions
[  OK  ] io_reject_out_of_range_dimensions
[ RUN  ] io_reject_invalid_material
[  OK  ] io_reject_invalid_material
[ RUN  ] io_reject_runs_overflow
[  OK  ] io_reject_runs_overflow
[ RUN  ] io_reject_too_few_cells
[  OK  ] io_reject_too_few_cells
[ RUN  ] io_reject_trailing_bytes
[  OK  ] io_reject_trailing_bytes
[ RUN  ] io_reject_truncation
[  OK  ] io_reject_truncation
[ RUN  ] io_reject_checksum_mismatch
[  OK  ] io_reject_checksum_mismatch
[ RUN  ] io_rejects_tiny_file_claiming_huge_world
[  OK  ] io_rejects_tiny_file_claiming_huge_world
[ RUN  ] io_fuzz_random_flips_and_truncation
[  OK  ] io_fuzz_random_flips_and_truncation
[ RUN  ] io_save_and_load_world_file
[  OK  ] io_save_and_load_world_file
[ RUN  ] io_large_run_count_splitting
[  OK  ] io_large_run_count_splitting

17/17 tests passed in 6 ms
```

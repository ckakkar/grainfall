# Grainfall

A falling-sand chemistry sandbox in C++20 + SDL3. Every pixel is a cell of
sand, water, oil, lava, ice, steam, smoke, fire, wood, plant, acid, gunpowder,
stone or metal. Cells fall, flow, float, rise, conduct heat, melt, freeze,
boil, burn, dissolve, grow and explode.

Built by a three-agent team in [Herdr](https://herdr.dev): Claude Code (lead:
architecture, core, app, review), Codex (partner: simulation engine) and
Antigravity (intern: save format, scenes). See `docs/ARCHITECTURE.md` and the
reports in `docs/reports/`.

## Build and run

```sh
brew install cmake sdl3
cmake -S . -B build && cmake --build build -j 8
./build/grainfall                 # open the sandbox
./build/grainfall_tests           # run the test suite
./build/grainfall --bench 2000    # headless performance run
./build/grainfall --shot out.bmp --ticks 300 --view heat   # headless screenshot
```

## Controls

| Input | Action |
|---|---|
| Left drag | paint the selected material (fire also ignites flammable things) |
| Right drag | erase |
| Shift + drag | paint only into empty space |
| `1`-`0`, `M`, `G`, `Tab`, or click the palette | pick a material |
| `P` / `B` / `H` | paint / bomb / heat gun (left heats, right chills) |
| `[` `]` or mouse wheel | brush size |
| `Space` / `.` | pause / single step |
| `Up` / `Down` | simulation speed (1-8 ticks per frame) |
| `V` | view: normal / heat map / sleeping-chunk debug |
| `Left` / `Right`, `R` | previous / next scene, reset scene |
| `C` | clear |
| `F5` / `F9` | save / load `grainfall.grain` |
| `F1` or `/` | help overlay |

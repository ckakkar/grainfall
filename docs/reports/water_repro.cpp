#include <cstdio>
#include "core/world.hpp"
#include "sim/simulation.hpp"
using namespace gf;
int main() {
    World w(120, 60, 5); Simulation s(w);
    for (int x=0;x<120;++x) w.set(x,59,Material::Stone);
    for (int y=0;y<60;++y) { w.set(0,y,Material::Stone); w.set(119,y,Material::Stone); }
    for (int x=80;x<118;++x) for (int y=15;y<59;++y) w.set(x,y,Material::Water);
    s.wake_all();
    for (int i = 0; i < 3000; ++i) s.step();
    for (int y = 30; y < 60; ++y) { for (int x = 0; x < 120; ++x) { auto m = w.at(x,y).mat; std::putchar(m==Material::Water?'~':m==Material::Stone?'#':m==Material::Empty?'.':'?'); } std::putchar('\n'); }
    int moved = 0; for (int i = 0; i < 10; ++i) { s.step(); moved += s.stats().moved_cells; }
    std::printf("moved cells over 10 more ticks: %d, awake %d/%d\n", moved, s.stats().active_chunks, s.stats().total_chunks);
}

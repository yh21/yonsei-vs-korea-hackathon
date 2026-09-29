#define POLPROF 1
#include "harness.hpp"
#include "../pol17.hpp"
#include <chrono>
using namespace std;
int main(int argc, char** argv) {
    vector<hx::GameMap> maps; hx::readMaps(argv[1], maps);
    int seed = atoi(argv[2]);
    const hx::GameMap* gm = nullptr; for (auto& m : maps) if (m.seed == seed) gm = &m;
    sim::Map M; hx::buildMap(*gm, M); static pol::Near NR; NR.build(M);
    int games = argc > 3 ? atoi(argv[3]) : 20; long calls = 0;
    pol::Params p = pol::Params::v17();
    clock_t c0 = clock();
    for (int g = 0; g < games; g++) {
        sim::State s; s.init(M); pol::Persist ps[2]; sim::Orders o[2]; int res = -1;
        while (res < 0) {
            for (int t = 0; t < 2; t++) { (getenv("MODE0") ? pol::decide17ref : (getenv("FAST") ? pol::decide17f : pol::decide17))(M, NR, s, t, p, ps[t], o[t]); calls++; }
            sim::step(M, s, o[0], o[1], true); res = sim::judge(M, s);
        }
    }
    double ms = 1000.0 * (clock() - c0) / CLOCKS_PER_SEC;
    printf("total %.1f ms, %ld calls, %.1f us/call\n", ms, calls, 1000 * ms / calls);
    printf("fwd/call %.2f rev/call %.2f\n", (double)pol::g_nfwd / calls, (double)pol::g_nrev / calls);
    printf("repair scan/call %.1f upd %.1f pops %.1f | masks %.1f nodes %.1f\n", (double)pol::g_scan / calls, (double)pol::g_upd / calls, (double)pol::g_pops / calls, (double)pol::g_mask / calls, (double)pol::g_maskNodes / calls);
    uint64_t tot = 0; for (int i = 0; i < 32; i++) tot += pol::g_prof[i];
    for (int i = 0; i < 12; i++) printf("sec %d: %.1f%%\n", i, 100.0 * pol::g_prof[i] / tot);
}

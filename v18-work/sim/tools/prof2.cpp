#include "harness.hpp"
#include "../pol17.hpp"
#include <ctime>
using namespace std;
int main(int argc, char** argv) {
    vector<hx::GameMap> maps; hx::readMaps(argv[1], maps);
    int seed = atoi(argv[2]); int games = atoi(argv[3]);
    const hx::GameMap* gm = nullptr; for (auto& m : maps) if (m.seed == seed) gm = &m;
    sim::Map M; hx::buildMap(*gm, M); static pol::Near NR; NR.build(M);
    pol::Params p = pol::Params::v17();
    double tm[3] = {0, 0, 0}; long calls = 0;
    for (int g = 0; g < games; g++) {
        sim::State s; s.init(M); pol::Persist ps[2]; sim::Orders o[2]; int res = -1;
        while (res < 0) {
            for (int t = 0; t < 2; t++) {
                pol::Persist a = ps[t], b = ps[t], c = ps[t]; sim::Orders oa, ob;
                clock_t c0 = clock(); pol::decide17ref(M, NR, s, t, p, a, oa); clock_t c1 = clock();
                pol::decide17(M, NR, s, t, p, b, o[t]); clock_t c2 = clock();
                pol::decide17f(M, NR, s, t, p, c, ob); clock_t c3 = clock();
                tm[0] += c1 - c0; tm[1] += c2 - c1; tm[2] += c3 - c2; calls++;
                ps[t] = b;
            }
            sim::step(M, s, o[0], o[1], true); res = sim::judge(M, s);
        }
    }
    printf("calls %ld: ref(Dial) %.1f us, hybrid %.1f us, fast %.1f us\n", calls, 1e6 * tm[0] / CLOCKS_PER_SEC / calls, 1e6 * tm[1] / CLOCKS_PER_SEC / calls, 1e6 * tm[2] / CLOCKS_PER_SEC / calls);
}

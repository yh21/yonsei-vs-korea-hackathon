// fastfid.cpp: how far do fast-mode rollouts drift from exact-mode play? (v17 vs v16 pairs, branch at several turns)
#include "harness.hpp"
#include "../pol17.hpp"
using namespace std;
int main(int argc, char** argv) {
    vector<hx::GameMap> maps; hx::readMaps(argv[1], maps);
    int lo = atoi(argv[2]), n = atoi(argv[3]);
    const int KS[] = {1, 3, 5, 10, 20};
    double dW[5] = {0}, dS[5] = {0}, dPos[5] = {0}; int cnt = 0;
    for (int si = 0; si < n; si++) {
        const hx::GameMap* gm = nullptr; for (auto& m : maps) if (m.seed == lo + si) gm = &m;
        sim::Map M; hx::buildMap(*gm, M); static pol::Near NR; NR.build(M);
        pol::Params P[2] = {pol::Params::v17(), pol::Params::v16()};
        sim::State s; s.init(M); pol::Persist ps[2]; sim::Orders o[2];
        for (int turn = 1; turn <= 140; turn++) {
            if (turn == 15 || turn == 40 || turn == 70 || turn == 100) {
                sim::State a = s, b = s; pol::Persist pa[2] = {ps[0], ps[1]}, pb[2] = {ps[0], ps[1]}; sim::Orders oa[2], ob[2];
                int ki = 0;
                for (int k = 1; k <= 20; k++) {
                    for (int t = 0; t < 2; t++) { pol::decide17(M, NR, a, t, P[t], pa[t], oa[t]); pol::decide17f(M, NR, b, t, P[t], pb[t], ob[t]); }
                    sim::step(M, a, oa[0], oa[1], true); sim::step(M, b, ob[0], ob[1], true);
                    if (k == KS[ki]) {
                        double w = 0, pos = 0;
                        for (int t = 0; t < 2; t++) { w += abs(sim::totalW(a, t) - sim::totalW(b, t)); for (int c = 0; c < sim::NC; c++) pos += abs(a.Wn[t][c] - b.Wn[t][c]); }
                        dW[ki] += w; dPos[ki] += pos;
                        dS[ki] += abs(sim::score2(M, a, 0) - sim::score2(M, b, 0)) / 2.0 + abs(sim::score2(M, a, 1) - sim::score2(M, b, 1)) / 2.0;
                        ki++;
                        if (ki == 5) break;
                    }
                }
                cnt++;
            }
            for (int t = 0; t < 2; t++) pol::decide17(M, NR, s, t, P[t], ps[t], o[t]);
            sim::step(M, s, o[0], o[1], true);
            if (sim::judge(M, s) >= 0) break;
        }
    }
    for (int i = 0; i < 5; i++) printf("k=%2d: mean |dW total| %.2f  mean W position L1 %.1f  mean |dScore| (both teams) %.2f\n", KS[i], dW[i] / cnt, dPos[i] / cnt, dS[i] / cnt);
}

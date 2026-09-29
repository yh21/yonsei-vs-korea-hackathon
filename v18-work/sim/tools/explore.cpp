// explore.cpp: how often does a randomly perturbed Pol17 beat exact Pol17(v17) on a given map?
#include "harness.hpp"
#include "../pol17.hpp"
#include <chrono>
using namespace std;

struct Rng { uint64_t s; Rng(uint64_t x) : s(x * 2654435761u + 88172645463325252ull) {} uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; } double u() { return (next() >> 11) * (1.0 / 9007199254740992.0); } int ri(int a, int b) { return a + next() % (b - a + 1); } };

pol::Params randomParams(Rng& r, double amp) {
    pol::Params p = pol::Params::v17();
    static const int big[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 17, 18, 19, 20, 21, 22, 23, 24, 25};
    for (int i : big) {
        if (r.u() < 0.5) continue;
        double f = exp((r.u() * 2 - 1) * amp);
        int v = (int)lround(p.P[i] * f);
        if (v < 1) v = 1;
        p.P[i] = v;
    }
    for (int i = 0; i < 4; i++) if (r.u() < 0.4) p.P[i] = max(2, p.P[i] + r.ri(-2, 2));
    if (r.u() < 0.3) p.P[15] = r.ri(1, 5);
    if (r.u() < 0.3) p.P[16] = r.ri(1, 4);
    if (r.u() < 0.3) p.garrisonAdd = r.ri(-1, 2);
    if (r.u() < 0.2) p.fAdjust = !p.fAdjust;
    return p;
}

// returns margin (score diff of team0 minus team1, in score units), sets win: 1 win, 0 draw, -1 loss
int playGame(const sim::Map& M, const pol::Near& NR, const pol::Params& p0, const pol::Params& p1, int* win, bool* instant = nullptr) {
    sim::State s; s.init(M);
    pol::Persist ps[2];
    sim::Orders o[2];
    int res = -1;
    while (res < 0) {
        for (int t = 0; t < 2; t++) pol::decide17(M, NR, s, t, t == 0 ? p0 : p1, ps[t], o[t]);
        sim::step(M, s, o[0], o[1], true);
        res = sim::judge(M, s, instant);
    }
    *win = res == 0 ? 1 : (res == 1 ? -1 : 0);
    return (sim::score2(M, s, 0) - sim::score2(M, s, 1)) / 2;
}

int main(int argc, char** argv) {
    vector<hx::GameMap> maps; hx::readMaps(argv[1], maps);
    int lo = atoi(argv[2]), n = atoi(argv[3]), variants = atoi(argv[4]);
    double amp = argc > 5 ? atof(argv[5]) : 0.5;
    string opp = argc > 6 ? argv[6] : "v17";
    pol::Params popp = opp == "v16" ? pol::Params::v16() : opp == "v15" ? pol::Params::v15() : pol::Params::v17();
    int seedsWithWinner = 0; double sumWinFrac = 0;
    for (int si = 0; si < n; si++) {
        const hx::GameMap* gm = nullptr;
        for (auto& m : maps) if (m.seed == lo + si) gm = &m;
        sim::Map M; hx::buildMap(*gm, M);
        static pol::Near NR; NR.build(M);
        Rng r(lo + si);
        int wins = 0, losses = 0, draws = 0; int bestMargin = -999;
        int baseWin; int baseMargin = playGame(M, NR, pol::Params::v17(), popp, &baseWin);
        for (int v = 0; v < variants; v++) {
            pol::Params p = randomParams(r, amp);
            int w; int mg = playGame(M, NR, p, popp, &w);
            wins += w == 1; losses += w == -1; draws += w == 0; bestMargin = max(bestMargin, mg);
        }
        printf("seed %d: base(v17 vs %s)=%s margin %d | variants: %d W %d D %d L, best margin %d\n", lo + si, opp.c_str(), baseWin == 1 ? "W" : baseWin == 0 ? "D" : "L", baseMargin, wins, draws, losses, bestMargin);
        seedsWithWinner += wins > 0; sumWinFrac += (double)wins / variants;
    }
    printf("seeds with >=1 winning variant: %d/%d, mean winfrac %.3f\n", seedsWithWinner, n, sumWinFrac / n);
}

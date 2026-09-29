// gen_value.cpp: generate (features, final margin) samples from self-play with perturbed v17-family policies.
// usage: gen_value maps.txt seedBase nGames out.csv
#include "harness.hpp"
#include "../pol17.hpp"
#include "../eval.hpp"
using namespace std;
struct XRng { uint64_t s; XRng(uint64_t x) : s(x * 2654435761u + 88172645463325252ull) { for (int i = 0; i < 5; i++) next(); } uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; } double u() { return (next() >> 11) * (1.0 / 9007199254740992.0); } int ri(int a, int b) { return a + next() % (b - a + 1); } };
static pol::Params randomP(XRng& r, double amp) {
    pol::Params p = pol::Params::v17();
    if (r.u() < 0.2) return p;
    static const int big[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 17, 18, 19, 20, 21, 22, 23, 24, 25};
    for (int i : big) { if (r.u() < 0.4) continue; double f = exp((r.u() * 2 - 1) * amp); int v = (int)lround(p.P[i] * f); if (v < 1) v = 1; p.P[i] = v; }
    for (int i = 0; i < 4; i++) if (r.u() < 0.4) p.P[i] = max(2, p.P[i] + r.ri(-2, 2));
    if (r.u() < 0.3) p.P[15] = r.ri(1, 5);
    if (r.u() < 0.3) p.P[16] = r.ri(1, 4);
    if (r.u() < 0.3) p.garrisonAdd = r.ri(-1, 2);
    if (r.u() < 0.2) p.fAdjust = !p.fAdjust;
    return p;
}
int main(int argc, char** argv) {
    vector<hx::GameMap> maps; hx::readMaps(argv[1], maps);
    int base = atoi(argv[2]), ng = atoi(argv[3]);
    FILE* out = fopen(argv[4], "w");
    pol::Params dflt = pol::Params::v17();
    for (int g = 0; g < ng; g++) {
        XRng r(base * 100003ull + g);
        const hx::GameMap& gm = maps[r.ri(0, (int)maps.size() - 1)];
        sim::Map M; hx::buildMap(gm, M);
        static pol::Near NR; NR.build(M);
        pol::Params P[2] = {randomP(r, 0.5), randomP(r, 0.5)};
        sim::State s; s.init(M);
        pol::Persist ps[2]; sim::Orders o[2];
        // sample turns
        bool sample[sim::TOTAL_TURNS + 2] = {false};
        for (int k = 0; k < 8; k++) sample[r.ri(6, 150)] = true;
        for (int turn = 1; turn <= sim::TOTAL_TURNS; turn++) {
            if (sample[turn - 1] && turn > 1) {
                // branch: features now, default continuation to the end
                double f0[ev::NF], f1[ev::NF];
                ev::features(M, s, 0, f0); ev::features(M, s, 1, f1);
                double margin = 0; int nbr = 4; double winSum = 0;
                for (int br = 0; br < nbr; br++) {
                    pol::Params cp[2] = {br == 0 ? dflt : randomP(r, 0.4), br == 0 ? dflt : randomP(r, 0.4)};
                    sim::State b = s; pol::Persist bp[2] = {ps[0], ps[1]}; sim::Orders bo[2]; int res = -1;
                    while (res < 0) {
                        for (int t = 0; t < 2; t++) pol::decide17f(M, NR, b, t, cp[t], bp[t], bo[t]);
                        sim::step(M, b, bo[0], bo[1], true);
                        res = sim::judge(M, b);
                    }
                    margin += (sim::score2(M, b, 0) - sim::score2(M, b, 1)) * 0.5 / nbr;
                    winSum += (res == 0 ? 1 : (res == 1 ? -1 : 0)) / (double)nbr;
                }
                int win = winSum > 0.25 ? 1 : (winSum < -0.25 ? -1 : 0);
                fprintf(out, "%d,0,%.1f,%d", s.turn, margin, win); for (int i = 0; i < ev::NF; i++) fprintf(out, ",%.4f", f0[i]); fprintf(out, "\n");
                fprintf(out, "%d,1,%.1f,%d", s.turn, -margin, -win); for (int i = 0; i < ev::NF; i++) fprintf(out, ",%.4f", f1[i]); fprintf(out, "\n");
            }
            for (int t = 0; t < 2; t++) pol::decide17f(M, NR, s, t, P[t], ps[t], o[t]);
            sim::step(M, s, o[0], o[1], true);
            if (sim::judge(M, s) >= 0) break;
        }
    }
    fclose(out);
}

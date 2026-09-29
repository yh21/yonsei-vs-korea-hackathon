// tune.cpp: (1+lambda)-ES over the v17-family parameter vector, fitness = mean score margin vs a pool of family opponents.
// usage: tune maps.txt seedLo nSeeds generations outPrefix [poolMode]
#include "harness.hpp"
#include "../pol17.hpp"
#include <chrono>
using namespace std;
struct XRng { uint64_t s; XRng(uint64_t x) : s(x * 2654435761u + 88172645463325252ull) { for (int i = 0; i < 5; i++) next(); } uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; } double u() { return (next() >> 11) * (1.0 / 9007199254740992.0); } int ri(int a, int b) { return a + next() % (b - a + 1); } double g() { double a = u() + 1e-12, b = u(); return sqrt(-2 * log(a)) * cos(6.283185307 * b); } };
static pol::Params randomPP(int k, double amp) {
    XRng r(k);
    pol::Params p = pol::Params::v17();
    static const int big[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 17, 18, 19, 20, 21, 22, 23, 24, 25};
    for (int i : big) { if (r.u() < 0.4) continue; double f = exp((r.u() * 2 - 1) * amp); int v = (int)lround(p.P[i] * f); if (v < 1) v = 1; p.P[i] = v; }
    for (int i = 0; i < 4; i++) if (r.u() < 0.4) p.P[i] = max(2, p.P[i] + r.ri(-2, 2));
    if (r.u() < 0.3) p.P[15] = r.ri(1, 5);
    if (r.u() < 0.3) p.P[16] = r.ri(1, 4);
    if (r.u() < 0.3) p.garrisonAdd = r.ri(-1, 2);
    if (r.u() < 0.2) p.fAdjust = !p.fAdjust;
    return p;
}
static const int TUNE_IDX[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 17, 18, 19, 20, 21, 22, 23, 24, 25};
constexpr int NT = 20;
struct Opp { pol::Params P; };
static vector<hx::GameMap> gMaps;
static const hx::GameMap* findMap(int seed) { for (auto& m : gMaps) if (m.seed == seed) return &m; return nullptr; }
struct MapCtx { sim::Map M; pol::Near NR; };
static map<int, MapCtx*> ctxCache;
static MapCtx* ctxFor(int seed) {
    auto it = ctxCache.find(seed); if (it != ctxCache.end()) return it->second;
    MapCtx* c = new MapCtx; hx::buildMap(*findMap(seed), c->M); c->NR.build(c->M); ctxCache[seed] = c; return c;
}
// returns margin (points) of team 0 (params a) vs team 1 (params b), both exact policies
static double play(int seed, const pol::Params& a, const pol::Params& b) {
    MapCtx* c = ctxFor(seed);
    sim::State s; s.init(c->M);
    pol::Persist ps[2]; sim::Orders o[2]; int res = -1;
    while (res < 0) {
        pol::decide17(c->M, c->NR, s, 0, a, ps[0], o[0]);
        pol::decide17(c->M, c->NR, s, 1, b, ps[1], o[1]);
        sim::step(c->M, s, o[0], o[1], true);
        res = sim::judge(c->M, s);
    }
    double margin = (sim::score2(c->M, s, 0) - sim::score2(c->M, s, 1)) * 0.5;
    margin = max(-25.0, min(25.0, margin));
    return margin + (res == 0 ? 10 : (res == 1 ? -10 : 0));   // reward winning beyond the margin
}
static pol::Params fromVec(const pol::Params& base, const double* x) {
    pol::Params p = base;
    for (int i = 0; i < NT; i++) p.P[TUNE_IDX[i]] = max(1, (int)lround(exp(x[i])));
    return p;
}
int main(int argc, char** argv) {
    hx::readMaps(argv[1], gMaps);
    int lo = atoi(argv[2]), ns = atoi(argv[3]), gens = atoi(argv[4]);
    string outp = argv[5];
    int poolMode = argc > 6 ? atoi(argv[6]) : 0;
    vector<Opp> pool;
    pool.push_back({pol::Params::v17()}); pool.push_back({pol::Params::v16()}); pool.push_back({pol::Params::v15()});
    for (int k = 9; k <= 20; k++) pool.push_back({randomPP(k, 0.5)});
    pol::Params base = pol::Params::v17();
    double x[NT], bestx[NT];
    for (int i = 0; i < NT; i++) x[i] = log((double)base.P[TUNE_IDX[i]]);
    memcpy(bestx, x, sizeof(x));
    XRng rng(12345 + poolMode);
    double sigma = 0.25;
    // a fixed evaluation panel that is re-drawn each generation (opponent, seed)
    auto evalVec = [&](const double* v, const vector<pair<int, int>>& panel) {
        pol::Params p = fromVec(base, v); double sum = 0;
        for (auto& pr : panel) sum += play(pr.second, p, pool[pr.first].P);
        return sum / panel.size();
    };
    for (int g = 0; g < gens; g++) {
        vector<pair<int, int>> panel;
        for (int i = 0; i < 24; i++) panel.push_back({rng.ri(0, (int)pool.size() - 1), lo + rng.ri(0, ns - 1)});
        double parent = evalVec(bestx, panel);
        double bestScore = parent; double cand[NT], bestc[NT]; bool improved = false;
        for (int l = 0; l < 6; l++) {
            for (int i = 0; i < NT; i++) cand[i] = bestx[i] + sigma * rng.g() * (rng.u() < 0.5 ? 1.0 : 0.0);
            double sc = evalVec(cand, panel);
            if (sc > bestScore + 1e-9) { bestScore = sc; memcpy(bestc, cand, sizeof(cand)); improved = true; }
        }
        if (improved) { memcpy(bestx, bestc, sizeof(bestx)); sigma = min(0.5, sigma * 1.1); } else sigma = max(0.05, sigma * 0.92);
        if (g % 5 == 4 || g == gens - 1) {
            pol::Params p = fromVec(base, bestx);
            printf("gen %d sigma %.3f parent %.2f best %.2f |", g, sigma, parent, bestScore);
            for (int i = 0; i < NT; i++) printf(" P%d=%d", TUNE_IDX[i], p.P[TUNE_IDX[i]]);
            printf("\n"); fflush(stdout);
        }
    }
    pol::Params p = fromVec(base, bestx);
    FILE* f = fopen((outp + ".txt").c_str(), "w");
    for (int i = 0; i < NT; i++) fprintf(f, "%d %d\n", TUNE_IDX[i], p.P[TUNE_IDX[i]]);
    fclose(f);
}

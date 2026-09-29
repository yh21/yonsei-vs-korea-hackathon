// duel.cpp - in-process match runner. Bots are linked as separate objects (decide_<name>A for Y, decide_<name>B for K).
// usage: duel maps.txt seed Yname Kname [replay.json]
// prints one result line:  R seed Y K winner reason scoreY scoreK turns maxms_Y avgms_Y maxms_K avgms_K
#include "harness.hpp"
#include <chrono>
#include "../pol17.hpp"
using namespace std;
using DecideFn = vector<string> (*)(const p::View&, const p::Init&);
#include "registry.inc"

struct XRng { uint64_t s; XRng(uint64_t x) : s(x * 2654435761u + 88172645463325252ull) { for (int i = 0; i < 5; i++) next(); } uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; } double u() { return (next() >> 11) * (1.0 / 9007199254740992.0); } int ri(int a, int b) { return a + next() % (b - a + 1); } };
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

// ---- synthetic non-family opponents operating directly on the sim state ----
static void styleBot(const string& kind, const sim::Map& M, const sim::State& s, int t, sim::Orders& o) {
    using namespace sim;
    o.clear();
    int op = 1 - t;
    int eng = ownedCount(M, s, t, T_ENG); int wc = max(3 - eng, 2);
    int res = s.res[t];
    int totF = totalF(s, t);
    int wantF = kind == "blob" ? 3 : 4;
    int nf = 0;
    if (totF < wantF) { nf = min({wantF - totF, 2, res / 5}); res -= nf * 5; o.addSpawn(K_F, nf, -1); }
    int nw = res / wc; if (nw > 0) o.addSpawn(K_W, nw, -1);
    int Wc[NC], Fc[NC];
    for (int c = 0; c < NC; c++) { Wc[c] = s.Wn[t][c]; Fc[c] = s.F[t][c]; }
    Wc[M.base[t]] += nw; Fc[M.base[t]] += nf;
    auto stepToward = [&](int from, int to) -> int {
        int best = -1, bd = M.dist[from][to];
        for (int k = 0; k < 4; k++) { int q = M.nbr[from][k]; if (q >= 0 && M.dist[q][to] < bd) { bd = M.dist[q][to]; best = k; } }
        return best;
    };
    if (kind == "blob") {
        int big = -1;
        for (int c = 0; c < NC; c++) if (Wc[c] > 0 && (big < 0 || Wc[c] > Wc[big])) big = c;
        if (big < 0) big = M.base[t];
        int tgt = -1; double bv = -1e9;
        for (int b = 0; b < M.nb; b++) if (s.owner[b] != t) {
            double v = M.score2[b] * 100 + (s.owner[b] == op ? 60 : 0) - 12.0 * M.dist[big][M.bcell[b]];
            if (v > bv) { bv = v; tgt = b; }
        }
        if (tgt < 0) return;
        int tc = M.bcell[tgt];
        for (int c = 0; c < NC; c++) if (Wc[c] > 0 && c != tc) { int k = stepToward(c, tc); if (k >= 0) o.addMove(c, K_W, k, Wc[c]); }
        for (int c = 0; c < NC; c++) if (Fc[c] > 0) {
            int b = M.bat[c];
            if (b >= 0 && s.owner[b] != t) continue;   // capturing: stay
            int k = stepToward(c, Wc[big] > 0 && big != c ? big : tc);
            if (c == big) k = stepToward(c, tc);
            if (k >= 0) o.addMove(c, K_F, k, Fc[c]);
        }
    } else {  // "split": every stack goes to the nearest unowned building (by hop distance), F likewise
        for (int c = 0; c < NC; c++) if (Wc[c] > 0 || Fc[c] > 0) {
            int tgt = -1, bd = 1000;
            for (int b = 0; b < M.nb; b++) if (s.owner[b] != t) { int d = M.dist[c][M.bcell[b]]; if (d < bd) { bd = d; tgt = b; } }
            if (tgt < 0) continue;
            int b0 = M.bat[c];
            if (bd == 0) continue;
            int k = stepToward(c, M.bcell[tgt]);
            if (k < 0) continue;
            if (Wc[c] > 0) o.addMove(c, K_W, k, Wc[c]);
            if (Fc[c] > 0 && !(b0 >= 0 && s.owner[b0] != t)) o.addMove(c, K_F, k, Fc[c]);
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "usage\n"); return 2; }
    vector<hx::GameMap> maps;
    hx::readMaps(argv[1], maps);
    int seed = atoi(argv[2]);
    const hx::GameMap* gm = nullptr;
    for (auto& m : maps) if (m.seed == seed) gm = &m;
    if (!gm) { fprintf(stderr, "seed not found\n"); return 2; }
    string names[2] = {argv[3], argv[4]};
    const char* replayPath = argc > 5 ? argv[5] : nullptr;
    DecideFn fn[2];
    for (int t = 0; t < 2; t++) {
        fn[t] = nullptr;
        for (auto& e : registry) if (names[t] == e.name) fn[t] = t == 0 ? e.A : e.B;
        if (!fn[t] && names[t].rfind("pp:", 0) != 0) { fprintf(stderr, "unknown bot %s\n", names[t].c_str()); return 2; }
    }
    sim::Map M; hx::buildMap(*gm, M);
    sim::State s; s.init(M);
    p::Init init[2] = {hx::buildInit(*gm, "Y"), hx::buildInit(*gm, "K")};
    bool stage1[sim::MAXB] = {false};
    double maxms[2] = {0, 0}, summs[2] = {0, 0};
    int result = -1; bool instant = false; int forfeit = -1;
    FILE* rf = replayPath ? fopen(replayPath, "w") : nullptr;
    if (rf) {
        fprintf(rf, "{\"seed\":%d,\"map\":{\"terrain\":[", seed);
        for (int y = 0; y < 15; y++) fprintf(rf, "%s\"%s\"", y ? "," : "", gm->terrain[y].c_str());
        fprintf(rf, "],\"buildings\":[");
        for (size_t i = 0; i < gm->blds.size(); i++) { auto& b = gm->blds[i]; fprintf(rf, "%s{\"id\":%d,\"x\":%d,\"y\":%d,\"type\":\"%s\",\"score\":%d}", i ? "," : "", b.id, b.x, b.y, b.type.c_str(), b.score); }
        fprintf(rf, "]},\"teams\":{\"Y\":{\"name\":\"%s\"},\"K\":{\"name\":\"%s\"}},\"turns\":[", names[0].c_str(), names[1].c_str());
    }
    int turns = 0;
    bool difftest = getenv("DIFFTEST") != nullptr;
    static pol::Near NR; if (difftest) NR.build(M);
    pol::Persist ps[2]; pol::Persist psPP[2];
    bool isPP[2] = {names[0].rfind("pp:", 0) == 0, names[1].rfind("pp:", 0) == 0};
    pol::Params ppParams[2];
    static pol::Near NRpp; if (isPP[0] || isPP[1]) NRpp.build(M);
    for (int t = 0; t < 2; t++) if (isPP[t]) {
        string nm = names[t].substr(3);
        if (nm == "rush") { ppParams[t] = pol::Params::v17(); ppParams[t].extraGoal = M.base[1 - t]; ppParams[t].extraGoalValue = 6000; ppParams[t].extraGoalNeed = 80; }
        else if (nm == "rush2") { ppParams[t] = pol::Params::v17(); ppParams[t].extraGoal = M.bcell[0]; ppParams[t].extraGoalValue = 6000; ppParams[t].extraGoalNeed = 60; }  // plaza
        else if (nm == "turtle") { ppParams[t] = pol::Params::v17(); ppParams[t].garrisonAdd = 3; ppParams[t].P[13] = 100; ppParams[t].P[14] = 80; ppParams[t].P[9] = 200; ppParams[t].P[11] = 1600; }
        else if (nm == "swarm") { ppParams[t] = pol::Params::v17(); ppParams[t].P[0] = 3; ppParams[t].P[1] = 3; ppParams[t].P[2] = 3; ppParams[t].P[3] = 3; ppParams[t].P[10] = 700; ppParams[t].P[9] = 1500; }
        else ppParams[t] = randomPP(atoi(nm.c_str()), 0.5);
    }
    long dtTotal[2] = {0, 0}, dtBad[2] = {0, 0};
    double portMs = 0; long portCalls = 0;
    while (result < 0) {
        vector<string> out[2];
        for (int t = 0; t < 2; t++) {
            if (names[t] == "pp:blob" || names[t] == "pp:split") { sim::Orders po; styleBot(names[t].substr(3), M, s, t, po); out[t] = hx::ordersToStrings(M, po); continue; }
            if (isPP[t]) { sim::Orders po; pol::decide17(M, NRpp, s, t, ppParams[t], psPP[t], po); out[t] = hx::ordersToStrings(M, po); continue; }
            p::View v = hx::buildView(M, s, *gm, t, stage1);
            auto t0 = chrono::steady_clock::now(); clock_t cc0 = clock();
            try { out[t] = fn[t](v, init[t]); }
            catch (const exception& e) { fprintf(stderr, "bot %d threw %s\n", t, e.what()); forfeit = t; }
            double wms = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
            double ms = 1000.0 * (clock() - cc0) / CLOCKS_PER_SEC;   // CPU time (robust against machine load)
            (void)wms;
            maxms[t] = max(maxms[t], ms); summs[t] += ms;
            if (ms > 300.0 && turns > 0) forfeit = t;  // would have been a forfeit
        }
        if (forfeit >= 0) { result = 1 - forfeit; break; }
        if (difftest) {
            for (int t = 0; t < 2; t++) {
                pol::Params pr;
                if (names[t] == "v17") pr = pol::Params::v17(); else if (names[t] == "v16") pr = pol::Params::v16(); else if (names[t] == "v15") pr = pol::Params::v15(); else continue;
                sim::Orders po;
                auto t0 = chrono::steady_clock::now();
                if (getenv("DIFFFAST")) pol::decide17f(M, NR, s, t, pr, ps[t], po); else pol::decide17(M, NR, s, t, pr, ps[t], po);
                { double ms_ = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count(); portMs += ms_; portCalls++; if (getenv("PORTTIME")) fprintf(stderr, "pt %d %d %.1f\n", turns + 1, t, ms_ * 1000); }
                auto mine = hx::ordersToStrings(M, po);
                dtTotal[t]++;
                if (mine != out[t]) {
                    dtBad[t]++;
                    if (dtBad[t] <= 3) {
                        fprintf(stderr, "DIFF seed %d turn %d team %d (%s)\n  orig:", seed, turns + 1, t, names[t].c_str());
                        for (auto& l : out[t]) fprintf(stderr, " [%s]", l.c_str());
                        fprintf(stderr, "\n  port:");
                        for (auto& l : mine) fprintf(stderr, " [%s]", l.c_str());
                        fprintf(stderr, "\n");
                    }
                }
            }
        }
        if (getenv("BENCH_TURN") && turns + 1 == atoi(getenv("BENCH_TURN"))) {
            static pol::Near NR2; NR2.build(M);
            pol::Params pr = pol::Params::v17(); pol::Persist p2; sim::Orders po;
            int N = 3000;
            auto t0 = chrono::steady_clock::now();
            for (int i = 0; i < N; i++) pol::decide17(M, NR2, s, 0, pr, p2, po);
            double us = chrono::duration<double, micro>(chrono::steady_clock::now() - t0).count() / N;
            fprintf(stderr, "BENCH turn %d: decide17 %.1f us/call (W me %d, opp %d)\n", turns + 1, us, sim::totalW(s, 0), sim::totalW(s, 1));
            if (getenv("BENCH_LOOP")) { for (;;) pol::decide17(M, NR2, s, 0, pr, p2, po); }
            return 0;
        }
        sim::Orders o[2];
        for (int t = 0; t < 2; t++) hx::parseCmds(M, out[t], o[t]);
        int8_t before[sim::MAXB];
        for (int b = 0; b < M.nb; b++) before[b] = s.owner[b];
        sim::step(M, s, o[0], o[1], true);
        for (int b = 0; b < M.nb; b++) stage1[b] = (before[b] >= 0 && s.owner[b] < 0);
        turns++;
        if (rf) {
            fprintf(rf, "%s{\"turn\":%d,\"commands\":{\"Y\":[", turns > 1 ? "," : "", turns);
            for (size_t i = 0; i < out[0].size(); i++) fprintf(rf, "%s\"%s\"", i ? "," : "", out[0][i].c_str());
            fprintf(rf, "],\"K\":[");
            for (size_t i = 0; i < out[1].size(); i++) fprintf(rf, "%s\"%s\"", i ? "," : "", out[1][i].c_str());
            fprintf(rf, "]},\"state\":{\"resources\":{\"Y\":%d,\"K\":%d},\"units\":[", s.res[0], s.res[1]);
            bool first = true;
            for (int t = 0; t < 2; t++) for (int k = 0; k < 2; k++) for (int c = 0; c < sim::NC; c++) {
                int n = k == 0 ? s.F[t][c] : s.Wn[t][c];
                if (n > 0) { fprintf(rf, "%s[\"%s\",\"%s\",%d,%d,%d]", first ? "" : ",", t ? "K" : "Y", k ? "W" : "F", c % 15, c / 15, n); first = false; }
            }
            fprintf(rf, "],\"buildings\":[");
            for (int b = 0; b < M.nb; b++) fprintf(rf, "%s{\"id\":%d,\"owner\":\"%s\",\"stage\":%d}", b ? "," : "", b, s.owner[b] < 0 ? "N" : (s.owner[b] ? "K" : "Y"), s.owner[b] < 0 ? 0 : 2);
            fprintf(rf, "],\"occupation\":{\"Y\":%d,\"K\":%d}}}", s.occ[0] / 2, s.occ[1] / 2);
        }
        result = sim::judge(M, s, &instant);
    }
    int sy = sim::score2(M, s, 0) / 2, sk = sim::score2(M, s, 1) / 2;
    const char* w = result == 0 ? "Y" : result == 1 ? "K" : "D";
    const char* reason = forfeit >= 0 ? "forfeit" : (instant ? "instant" : "score");
    if (rf) {
        fprintf(rf, "],\"result\":{\"winner\":\"%s\",\"reason\":\"%s\",\"score\":{\"Y\":%d,\"K\":%d},\"turns\":%d}}\n", w, reason, sy, sk, turns);
        fclose(rf);
    }
    if (difftest) fprintf(stderr, "DIFFTEST seed %d: Y %ld/%ld bad, K %ld/%ld bad, port avg %.1f us\n", seed, dtBad[0], dtTotal[0], dtBad[1], dtTotal[1], 1000.0 * portMs / max(1L, portCalls));
    printf("R %d %s %s %s %s %d %d %d %.1f %.2f %.1f %.2f\n", seed, names[0].c_str(), names[1].c_str(), w, reason, sy, sk, turns,
           maxms[0], summs[0] / max(1, turns), maxms[1], summs[1] / max(1, turns));
    return 0;
}

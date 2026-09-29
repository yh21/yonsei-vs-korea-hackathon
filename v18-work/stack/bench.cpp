// load a dump, advance sim to turn T by replaying commands, then benchmark policy & sim
#include "sim.hpp"
#include "policy.hpp"
#include "search.hpp"
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace std; using namespace sim;
int main(int argc, char** argv) {
    ifstream in(argv[1]); int TT = atoi(argv[2]); string line, tag;
    int W, H; in >> tag >> W >> H; vector<string> terr(H); for (auto& r : terr) in >> r;
    int nb; in >> tag >> nb; vector<BInit> bs;
    int scs[MAXB];
    for (int i = 0; i < nb; i++) { int id, x, y, sc; string ty; in >> id >> x >> y >> ty >> sc; bs.push_back({id, x, y, ty}); scs[id] = sc; }
    int bx[2], by[2]; for (int i = 0; i < 2; i++) { string t; in >> tag >> t >> bx[i] >> by[i]; }
    initGeo(W, H, terr, bs, bx[0], by[0], bx[1], by[1]);
    for (int i = 0; i < nb; i++) sc10[i] = scs[i] * 10;
    GS s; memset(&s, 0, sizeof(s)); s.t[0].res = s.t[1].res = 10;
    int T; in >> tag >> T; getline(in, line);
    for (int turn = 1; turn <= TT; turn++) {
        Plan pl[2];
        for (int k = 0; k < 2; k++) {
            int n; string tm; in >> tag >> tm >> n; getline(in, line);
            for (int i = 0; i < n; i++) {
                getline(in, line); istringstream ss(line); string c; ss >> c;
                if (c == "SPAWN") { string kd; int cnt; ss >> kd >> cnt; int kind = kd == "F" ? 0 : kd == "W" ? 1 : 2; int x = -1, y = -1; if (ss >> x >> y) pl[k].addSpawn(kind, y * W + x, cnt); else pl[k].addSpawn(kind, G.base[k], cnt); }
                else if (c == "MOVE") { int x, y, cnt; string kd, d; ss >> x >> y >> kd >> cnt >> d; int kind = kd == "F" ? 0 : kd == "W" ? 1 : 2; int dir = d == "U" ? 0 : d == "D" ? 1 : d == "L" ? 2 : 3; pl[k].addMove(y * W + x, kind, dir, cnt); }
                else if (c == "TELE") { if (pl[k].teleSrc >= 0) continue; int x, y, cnt, tx, ty; string kd; ss >> x >> y >> kd >> cnt >> tx >> ty; pl[k].teleSrc = y * W + x; pl[k].teleDst = ty * W + tx; pl[k].teleKind = kd == "F" ? 0 : kd == "W" ? 1 : 2; pl[k].teleN = cnt; }
                else if (c == "PRIORITY") { int x, y; pl[k].nprio = 0; while (ss >> x >> y) { int b = G.bAt[y * W + x]; if (b >= 0) pl[k].prio[pl[k].nprio++] = b; } }
            }
        }
        step(s, pl[0], pl[1]);
        int r0, r1; in >> tag >> r0 >> r1; int nu; in >> tag >> nu; for (int i = 0; i < nu; i++) { string a, b; int x, y, c; in >> a >> b >> x >> y >> c; } in >> tag; for (int b = 0; b < nb; b++) { int o; in >> o; }
    }
    // benchmark
    pol::Doctrine d; Plan p; int iters = 2000;
    clock_t t0 = clock();
    for (int i = 0; i < iters; i++) { d.plan(s, 0, p); }
    double ms = (clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
    printf("policy: %.1f us per call (nmv=%d)\n", ms * 1000 / iters, p.nmv);
    Plan q; d.plan(s, 1, q);
    GS s2;
    t0 = clock();
    for (int i = 0; i < 200000; i++) { s2 = s; step(s2, p, q); }
    ms = (clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
    printf("sim step: %.2f us\n", ms * 1000 / 200000);
    {
        srch::Searcher sr; 
        clock_t t1 = clock(); Plan best; int reps = 20; int ev = 0;
        for (int i = 0; i < reps; i++) { best = sr.search(s, 0); ev += sr.evals; }
        double ms = (clock() - t1) * 1000.0 / CLOCKS_PER_SEC;
        printf("search: %.2f ms per call, %.1f evals/call, %.1f us/eval\n", ms / reps, double(ev) / reps, ms * 1000 / std::max(1, ev));
    }
    return 0;
}

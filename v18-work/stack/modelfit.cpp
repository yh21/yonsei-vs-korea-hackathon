// mean accuracy of one parametrised P-model over several recorded games. usage: modelfit key=val ... -- dump1 side1 dump2 side2 ...
#include "sim.hpp"
#include "policy.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <map>
using namespace std; using namespace sim;
static void parseCmds(istream& in, int n, int k, Plan& pl) {
    string line; int W = G.W;
    for (int i = 0; i < n; i++) {
        getline(in, line); istringstream ss(line); string c; ss >> c;
        if (c == "SPAWN") { string kd; int cnt; ss >> kd >> cnt; int kind = kd == "F" ? 0 : kd == "W" ? 1 : 2; int x = -1, y = -1; if (ss >> x >> y) pl.addSpawn(kind, y * W + x, cnt); else pl.addSpawn(kind, G.base[k], cnt); }
        else if (c == "MOVE") { int x, y, cnt; string kd, d; ss >> x >> y >> kd >> cnt >> d; int kind = kd == "F" ? 0 : kd == "W" ? 1 : 2; int dir = d == "U" ? 0 : d == "D" ? 1 : d == "L" ? 2 : 3; pl.addMove(y * W + x, kind, dir, cnt); }
        else if (c == "TELE") { if (pl.teleSrc >= 0) continue; int x, y, cnt, tx, ty; string kd; ss >> x >> y >> kd >> cnt >> tx >> ty; pl.teleSrc = y * W + x; pl.teleDst = ty * W + tx; pl.teleKind = kd == "F" ? 0 : kd == "W" ? 1 : 2; pl.teleN = cnt; }
        else if (c == "PRIORITY") { int x, y; pl.nprio = 0; while (ss >> x >> y) { int b = G.bAt[y * W + x]; if (b >= 0) pl.prio[pl.nprio++] = b; } }
    }
}
int main(int argc, char** argv) {
    map<string, int> kv; int i = 1;
    for (; i < argc && string(argv[i]) != "--"; i++) { string a = argv[i]; auto p = a.find('='); kv[a.substr(0, p)] = atoi(a.substr(p + 1).c_str()); }
    i++;
    double totalAcc = 0; int games = 0;
    for (; i + 1 < argc; i += 2) {
        ifstream in(argv[i]); int me = atoi(argv[i + 1]); int op = 1 - me;
        string line, tag; int W, H; in >> tag >> W >> H; vector<string> terr(H); for (auto& r : terr) in >> r;
        int nb; in >> tag >> nb; vector<BInit> bs; int scs[MAXB];
        for (int k = 0; k < nb; k++) { int id, x, y, sc; string ty; in >> id >> x >> y >> ty >> sc; bs.push_back({id, x, y, ty}); scs[id] = sc; }
        int bx[2], by[2]; for (int k = 0; k < 2; k++) { string t; in >> tag >> t >> bx[k] >> by[k]; }
        initGeo(W, H, terr, bs, bx[0], by[0], bx[1], by[1]);
        for (int k = 0; k < nb; k++) sc10[k] = scs[k] * 10;
        GS s; memset(&s, 0, sizeof(s)); s.t[0].res = s.t[1].res = 10;
        int T; in >> tag >> T; getline(in, line);
        pol::Doctrine md;
        auto& c = md.cfg;
        auto set = [&](const char* k, int& f) { auto it = kv.find(k); if (it != kv.end()) f = it->second; };
        set("riskMul", c.riskMul); set("regMul", c.regMul); set("huntValue", c.huntValue); set("defValue", c.defValue); set("adaptF", c.adaptF);
        set("dF0", c.dF0); set("dF1", c.dF1); set("dF2", c.dF2); set("dF3", c.dF3); set("holdVal", c.holdVal); set("dispatch", c.dispatch); set("dispatchBase", c.dispatchBase);
        set("rally", c.rally); set("huntNeed", c.huntNeed); set("engDenial", c.engDenial); set("garrisonN", c.garrisonN); set("hospBonus", c.hospBonus); set("dispatchScale", c.dispatchScale);
        double accSum = 0; int cnt = 0;
        for (int turn = 1; turn <= T; turn++) {
            Plan pl[2];
            for (int k = 0; k < 2; k++) { int n; string tm; in >> tag >> tm >> n; getline(in, line); parseCmds(in, n, k, pl[k]); }
            Plan E; md.plan(s, op, E);
            GS pred = s; const Plan* pa = me == 0 ? &pl[me] : &E; const Plan* pb = me == 0 ? &E : &pl[me]; step(pred, *pa, *pb);
            step(s, pl[0], pl[1]);
            int r0, r1; in >> tag >> r0 >> r1; int nu; in >> tag >> nu; for (int k = 0; k < nu; k++) { string a, b; int x, y, cc; in >> a >> b >> x >> y >> cc; } in >> tag; for (int b = 0; b < nb; b++) { int o; in >> o; }
            int tot = 0; for (int q = 0; q < G.N; q++) tot += s.t[op].W[q] + s.t[op].F[q];
            if (tot == 0) continue;
            int mt = 0; for (int q = 0; q < G.N; q++) mt += min<int>(pred.t[op].W[q], s.t[op].W[q]) + min<int>(pred.t[op].F[q], s.t[op].F[q]);
            accSum += double(mt) / tot; cnt++;
        }
        totalAcc += accSum / max(1, cnt); games++;
    }
    printf("%.4f\n", totalAcc / max(1, games));
}

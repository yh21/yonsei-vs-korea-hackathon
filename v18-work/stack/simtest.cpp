#include "sim.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace std;
using namespace sim;
int main(int argc, char** argv) {
    ifstream in(argv[1]); string line, tag;
    int W, H; in >> tag >> W >> H; G.W = W; G.H = H; G.N = W * H;
    vector<string> terr(H); for (auto& r : terr) in >> r;
    for (int c = 0; c < G.N; c++) G.pass[c] = terr[c / W][c % W] != '#';
    int nb; in >> tag >> nb; G.nB = nb;
    for (int i = 0; i < W * H; i++) G.bAt[i] = -1;
    for (int i = 0; i < nb; i++) { int id, x, y, sc; string ty; in >> id >> x >> y >> ty >> sc; G.bcell[id] = y * W + x; G.btype[id] = typeFromString(ty); G.bAt[y * W + x] = id; sc10[id] = sc * 10; }
    for (int i = 0; i < 2; i++) { string t; int x, y; in >> tag >> t >> x >> y; G.base[t == "Y" ? 0 : 1] = y * W + x; }
    // geometry
    const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
    for (int c = 0; c < G.N; c++) {
        G.nn[c] = 0; G.nln[c] = 0; G.nl[c][G.nln[c]++] = c;
        for (int d = 0; d < 4; d++) {
            int x = c % W + dx[d], y = c / W + dy[d];
            if (x < 0 || y < 0 || x >= W || y >= H || !G.pass[y * W + x]) { G.nb[c][d] = -1; continue; }
            G.nb[c][d] = y * W + x; G.nn[c]++; G.nl[c][G.nln[c]++] = y * W + x;
        }
    }
    GS s; memset(&s, 0, sizeof(s)); s.t[0].res = s.t[1].res = 10;
    int T; in >> tag >> T; getline(in, line);
    int bad = 0;
    for (int turn = 1; turn <= T; turn++) {
        Plan pl[2];
        for (int k = 0; k < 2; k++) {
            int n; string tm; in >> tag >> tm >> n; getline(in, line);
            for (int i = 0; i < n; i++) {
                getline(in, line); istringstream ss(line); string c; ss >> c;
                if (c == "SPAWN") { string kd; int cnt; ss >> kd >> cnt; int kind = kd == "F" ? 0 : kd == "W" ? 1 : 2; int x = -1, y = -1; if (ss >> x >> y) pl[k].addSpawn(kind, y * W + x, cnt); else pl[k].addSpawn(kind, G.base[k], cnt); }
                else if (c == "MOVE") { int x, y, cnt; string kd, d; ss >> x >> y >> kd >> cnt >> d; int kind = kd == "F" ? 0 : kd == "W" ? 1 : 2; int dir = d == "U" ? 0 : d == "D" ? 1 : d == "L" ? 2 : 3; pl[k].addMove(y * W + x, kind, dir, cnt); }
                else if (c == "TELE") { if (pl[k].teleSrc >= 0) continue; int x, y, cnt, tx, ty; string kd; ss >> x >> y >> kd >> cnt >> tx >> ty; pl[k].teleSrc = y * W + x; pl[k].teleDst = ty * W + tx; pl[k].teleKind = kd == "F" ? 0 : kd == "W" ? 1 : 2; pl[k].teleN = cnt; }
                else if (c == "PRIORITY") { int x, y; pl[k].nprio = 0; while (ss >> x >> y) { int b = G.bAt[y * W + x]; if (b >= 0) pl[k].prio[pl[k].nprio++] = b; } }
                else if (c == "MOVE2") { cerr << "MOVE2 unsupported\n"; }
            }
        }
        step(s, pl[0], pl[1]);
        // expected
        int r0, r1; in >> tag >> r0 >> r1;
        int nu; in >> tag >> nu;
        static int eF[2][MAXC], eW[2][MAXC], eS[2][MAXC];
        memset(eF, 0, sizeof(eF)); memset(eW, 0, sizeof(eW)); memset(eS, 0, sizeof(eS));
        for (int i = 0; i < nu; i++) { string tm, kd; int x, y, c; in >> tm >> kd >> x >> y >> c; int t = tm == "Y" ? 0 : 1; (kd == "F" ? eF : kd == "W" ? eW : eS)[t][y * W + x] = c; }
        in >> tag; int eo[MAXB]; for (int b = 0; b < nb; b++) in >> eo[b];
        bool ok = s.t[0].res == r0 && s.t[1].res == r1;
        for (int t = 0; t < 2; t++) for (int c = 0; c < G.N; c++) ok &= s.t[t].F[c] == eF[t][c] && s.t[t].W[c] == eW[t][c] && s.t[t].S[c] == eS[t][c];
        for (int b = 0; b < nb; b++) ok &= s.own[b] == eo[b];
        if (!ok) { bad++; if (bad < 4) cerr << "MISMATCH at turn " << turn << " res " << s.t[0].res << "/" << s.t[1].res << " vs " << r0 << "/" << r1 << "\n"; if (bad == 1) { for (int b = 0; b < nb; b++) if (s.own[b] != eo[b]) cerr << " bld " << b << " got " << int(s.own[b]) << " exp " << eo[b] << "\n"; }
            // resync is hard; stop
            break; }
    }
    cout << (bad ? "FAIL" : "OK") << " turns=" << T << "\n";
    return bad != 0;
}

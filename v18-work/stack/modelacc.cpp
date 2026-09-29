// offline: how well do candidate opponent models predict the recorded enemy moves? usage: modelacc dump.txt [mySide 0|1]
#include "sim.hpp"
#include "policy.hpp"
#include "search.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
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
    ifstream in(argv[1]); int me = argc > 2 ? atoi(argv[2]) : 0; int op = 1 - me;
    string line, tag; int W, H; in >> tag >> W >> H; vector<string> terr(H); for (auto& r : terr) in >> r;
    int nb; in >> tag >> nb; vector<BInit> bs; int scs[MAXB];
    for (int i = 0; i < nb; i++) { int id, x, y, sc; string ty; in >> id >> x >> y >> ty >> sc; bs.push_back({id, x, y, ty}); scs[id] = sc; }
    int bx[2], by[2]; for (int i = 0; i < 2; i++) { string t; in >> tag >> t >> bx[i] >> by[i]; }
    initGeo(W, H, terr, bs, bx[0], by[0], bx[1], by[1]);
    for (int i = 0; i < nb; i++) sc10[i] = scs[i] * 10;
    GS s; memset(&s, 0, sizeof(s)); s.t[0].res = s.t[1].res = 10;
    int T; in >> tag >> T; getline(in, line);
    const int NM = 7; pol::Doctrine mdl[NM];
    mdl[1].cfg.riskMul = 8; mdl[1].cfg.regMul = 2; mdl[1].cfg.huntValue = 1100; mdl[1].cfg.defValue = 1600;
    mdl[2] = mdl[1]; mdl[2].cfg.dF0 = 6; mdl[2].cfg.dF1 = 5; mdl[2].cfg.dF2 = 4; mdl[2].cfg.dF3 = 3; mdl[2].cfg.adaptF = 0;
    mdl[3] = mdl[0]; mdl[3].cfg.holdVal = 1; mdl[3].cfg.dispatch = 3; mdl[3].cfg.dispatchBase = 500; mdl[3].cfg.dF1 = 5; mdl[3].cfg.dF2 = 4; mdl[3].cfg.dF3 = 3; // my own doctrine as a model
    double accSum[NM] = {}, wSum = 0; int turns = 0;
    static int momDirW[MAXC], momDirF[MAXC]; for (int c = 0; c < MAXC; c++) momDirW[c] = momDirF[c] = -1;
    double accPhase[NM][4] = {}, wPhase[4] = {};
    for (int turn = 1; turn <= T; turn++) {
        Plan pl[2];
        for (int k = 0; k < 2; k++) { int n; string tm; in >> tag >> tm >> n; getline(in, line); parseCmds(in, n, k, pl[k]); }
        // predictions from state s (start of this turn)
        Plan E[NM]; GS pred[NM];
        for (int m = 0; m < 4; m++) { mdl[m].plan(s, op, E[m]); }
        // model 4: strike variant of model 0 (enemy stacks switch to where they win against my current units)
        { srch::Searcher tmp; Plan none; E[4] = E[0]; tmp.adversarialReply(s, none, E[0], me, E[4]); }
        // model 6: momentum (enemy stacks keep the direction they arrived with)
        E[6].clear(); E[6] = E[0]; E[6].nmv = 0;
        for (int c = 0; c < G.N; c++) {
            if (s.t[op].W[c] > 0 && momDirW[c] >= 0 && G.nb[c][momDirW[c]] >= 0) E[6].addMove(c, 1, momDirW[c], s.t[op].W[c]);
            if (s.t[op].F[c] > 0 && momDirF[c] >= 0 && G.nb[c][momDirF[c]] >= 0) E[6].addMove(c, 0, momDirF[c], s.t[op].F[c]);
        }
        // model 5: nobody moves
        E[5] = E[0]; E[5].nmv = 0; E[5].teleSrc = -1;
        for (int m = 0; m < NM; m++) { pred[m] = s; const Plan* pa = me == 0 ? &pl[me] : &E[m]; const Plan* pb = me == 0 ? &E[m] : &pl[me]; step(pred[m], *pa, *pb); }
        // update momentum from the enemy's actual orders of this turn
        for (int c = 0; c < MAXC; c++) momDirW[c] = momDirF[c] = -1;
        { int bestW[MAXC] = {}, bestF[MAXC] = {};
          for (int i = 0; i < pl[op].nmv; i++) { const Mv& mv = pl[op].mv[i]; int d = G.nb[mv.src][mv.dir]; if (d < 0) continue;
            if (mv.kind == 1 && mv.n > bestW[d]) { bestW[d] = mv.n; momDirW[d] = mv.dir; }
            if (mv.kind == 0 && mv.n > bestF[d]) { bestF[d] = mv.n; momDirF[d] = mv.dir; } } }
        step(s, pl[0], pl[1]);
        int r0, r1; in >> tag >> r0 >> r1; int nu; in >> tag >> nu; for (int i = 0; i < nu; i++) { string a, b; int x, y, c; in >> a >> b >> x >> y >> c; } in >> tag; for (int b = 0; b < nb; b++) { int o; in >> o; }
        int tot = 0; for (int c = 0; c < G.N; c++) tot += s.t[op].W[c] + s.t[op].F[c];
        if (tot == 0) continue;
        int ph = turn < 30 ? 0 : turn < 70 ? 1 : turn < 110 ? 2 : 3;
        for (int m = 0; m < NM; m++) { int mt = 0; for (int c = 0; c < G.N; c++) mt += min<int>(pred[m].t[op].W[c], s.t[op].W[c]) + min<int>(pred[m].t[op].F[c], s.t[op].F[c]); double a = double(mt) / tot; accSum[m] += a; accPhase[m][ph] += a; }
        wSum += 1; wPhase[ph] += 1; turns++;
    }
    const char* nm[NM] = {"v17", "v16", "v15", "mine", "strike", "still", "moment"};
    printf("turns=%d ", turns);
    for (int m = 0; m < NM; m++) printf("%s=%.3f ", nm[m], accSum[m] / max(1.0, wSum));
    printf("| by phase:");
    for (int ph = 0; ph < 4; ph++) { printf(" ["); for (int m = 0; m < NM; m++) printf("%.2f ", accPhase[m][ph] / max(1.0, wPhase[ph])); printf("]"); }
    printf("\n");
}

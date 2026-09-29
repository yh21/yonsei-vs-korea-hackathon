// blotto.hpp - bot logic (v18 candidate): exact one-turn look-ahead + Blotto-style resource allocation search
// on top of a v17-style heuristic proposal. C++20, standard library only.
#pragma once
#include "protocol.hpp"
#include "engine.hpp"
#include "v15.hpp"
#include "v16.hpp"
#include "v17.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <sstream>

using namespace std;
using namespace eng;

namespace blotto {

#ifdef BTUNE
struct PrmTable { double c[64]; bool has[64]; PrmTable() { for (int i = 0; i < 64; i++) { char n[8]; snprintf(n, 8, "Q%d", i); const char* e = getenv(n); has[i] = e != nullptr; c[i] = e ? atof(e) : 0; } } };
inline const PrmTable& prmTable() { static PrmTable t; return t; }
inline double prm(int i, double d) { const PrmTable& t = prmTable(); return t.has[i] ? t.c[i] : d; }
#else
constexpr double prm(int, double d) { return d; }
#endif

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

struct Plan {
    int16_t wm[NC][4];
    int16_t fm[NC][4];
    int nsite = 0;
    int site[10];   // cells; site[0] = base
    int spF[10], spW[10];
    int teleSrc = -1, teleDst = -1, teleKind = 1, teleN = 0;
    void clear() {
        memset(wm, 0, sizeof(wm)); memset(fm, 0, sizeof(fm));
        nsite = 0; memset(spF, 0, sizeof(spF)); memset(spW, 0, sizeof(spW));
        teleSrc = teleDst = -1; teleN = 0;
    }
};

struct Bot {
    bool ready = false;
    Map M;
    string me, opp;
    p::Init initEn, initMe;
    vector<string> btypeStr;
    v17::Planner pMe, scrM;
    // enemy models (candidate opponent policies): 0 = v17, 1 = v16, 2 = v15
    v17::Planner e17, s17; v16::Planner e16, s16; v15::Planner e15, s15;
    double modelScore[3] = {0.0, 0.0, 0.0};
    int em = 0;                       // currently chosen enemy model
    Orders prevMy, prevModel[3]; State prevS0; bool havePrev = false;
    vector<int> known;  // protocol id -> known score (-1 unknown)
    vector<int> idToB;
    uint8_t depClaimed[MAXB] = {0};
    int prioOrder[MAXB];
    StepCfg cfg;
    int W_ = 15;

    void init_once(const p::Init& init) {
        if (ready) return;
        ready = true;
        me = init.team; opp = init.opp;
        initEn = init; initEn.team = init.opp; initEn.opp = init.team; initMe = init;
        for (auto& b : init.buildings) btypeStr.push_back(b.type);
        memset(&M, 0, sizeof(M));
        for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) M.pass[y * GW + x] = init.terrain[y][x] != '#';
        M.nb = (int)init.buildings.size();
        int maxid = 0; for (auto& b : init.buildings) maxid = max(maxid, b.id);
        idToB.assign(maxid + 1, -1); known.assign(maxid + 1, -1);
        for (int i = 0; i < M.nb; i++) {
            auto& b = init.buildings[i];
            M.bcell[i] = b.y * GW + b.x; M.btype[i] = btypeFromStr(b.type); M.bid[i] = b.id; idToB[b.id] = i;
            M.score2[i] = 3;
            if (b.type == "PLAZA") known[b.id] = 3;
        }
        auto myb = init.bases[me == "Y" ? 0 : 1], enb = init.bases[me == "Y" ? 1 : 0];
        M.base[0] = myb.second * GW + myb.first; M.base[1] = enb.second * GW + enb.first;
        M.finish();
        cfg.homeFlip[0] = myb.first * 2 > GW - 1;
        cfg.homeFlip[1] = enb.first * 2 > GW - 1;
    }

    void updateScores(const p::View& v) {
        for (auto& b : v.buildings) {
            int i = idToB[b.id];
            if (b.score >= 0) { known[b.id] = b.score; int sy = M.sym[i]; if (sy >= 0) known[M.bid[sy]] = b.score; }
        }
        for (int i = 0; i < M.nb; i++) {
            int k = known[M.bid[i]];
            if (k >= 0) M.score2[i] = 2 * k;
            else {
                int x = M.bcell[i] % GW;
                M.score2[i] = (x >= 5 && x <= 9) ? 6 : 3;  // expected 3 for central, 1.5 elsewhere
            }
        }
    }

    // ---- view -> state
    void buildState(const p::View& v, State& s) {
        s.clear();
        s.turn = (int16_t)(v.turn - 1);
        s.res[0] = (int16_t)v.my_resource; s.res[1] = (int16_t)v.opp_resource;
        for (auto& u : v.units) {
            int t = u.team == me ? 0 : 1; int c = u.y * GW + u.x;
            if (u.kind == "F") s.F[t][c] += u.count; else if (u.kind == "W") s.W[t][c] += u.count;
        }
        for (auto& b : v.buildings) {
            int i = idToB[b.id];
            s.owner[i] = b.owner == me ? 0 : (b.owner == opp ? 1 : -1);
            if (M.btype[i] == T_DEPOT) {
                if (b.owner == me) depClaimed[i] |= 1;
                if (b.owner == opp) depClaimed[i] |= 2;
                s.dep[i] = 0;
                if (depClaimed[i] & 1) s.dep[i] |= 1;
                if (depClaimed[i] & 2) s.dep[i] |= 2;
            }
        }
    }

    // ---- command strings -> Plan / Orders
    static int dirIdx(const string& d) { return d == "U" ? 0 : d == "D" ? 1 : d == "L" ? 2 : 3; }

    void parseToOrders(const vector<string>& cmds, Orders& o, int team) {
        o.clear();
        int baseCell = M.base[team];
        (void)baseCell;
        for (auto& c : cmds) {
            istringstream ss(c); string tag; ss >> tag;
            if (tag == "SPAWN") {
                string k; int n; ss >> k >> n; int x = -1, y = -1;
                if (ss >> x >> y) o.addSpawn(k == "F" ? 0 : 1, n, y * GW + x); else o.addSpawn(k == "F" ? 0 : 1, n, -1);
            } else if (tag == "MOVE") {
                int x, y, n; string k, d; ss >> x >> y >> k >> n >> d; if (k == "S") continue;
                o.addMove(y * GW + x, k == "F" ? 0 : 1, dirIdx(d), n);
            } else if (tag == "TELE") {
                int x, y, n, tx, ty; string k; ss >> x >> y >> k >> n >> tx >> ty; if (k == "S") continue;
                o.addTele(y * GW + x, k == "F" ? 0 : 1, n, ty * GW + tx);
            } else if (tag == "PRIORITY") {
                int x, y; while (ss >> x >> y) { int b = M.bat[y * GW + x]; if (b >= 0) o.addPrio(b); }
            }
        }
    }

    // Build Plan from my Orders (baseline proposal)
    void ordersToPlan(const Orders& o, const State& s, Plan& pl, int team = 0) {
        pl.clear();
        pl.nsite = 1; pl.site[0] = M.base[team];
        for (int b = 0; b < M.nb; b++) if (M.btype[b] == T_HOSPITAL && s.owner[b] == team && pl.nsite < 10) pl.site[pl.nsite++] = M.bcell[b];
        for (int i = 0; i < o.nsp; i++) {
            auto& sp = o.sp[i]; int cell = sp.cell < 0 ? M.base[team] : sp.cell;
            for (int j = 0; j < pl.nsite; j++) if (pl.site[j] == cell) { (sp.kind == 0 ? pl.spF : pl.spW)[j] += sp.n; break; }
        }
        for (int i = 0; i < o.nmv; i++) {
            auto& m = o.mv[i];
            if (m.dir < 4) (m.kind == 0 ? pl.fm : pl.wm)[m.cell][m.dir] += m.n;
            else { pl.teleSrc = m.cell; pl.teleDst = m.cell2; pl.teleKind = m.kind; pl.teleN = m.n; }
        }
    }

    static bool samePlan(const Plan& a, const Plan& b) {
        for (int q = 0; q < NC; q++) for (int d = 0; d < 4; d++) if (a.wm[q][d] != b.wm[q][d] || a.fm[q][d] != b.fm[q][d]) return false;
        for (int j = 0; j < 10; j++) if (a.spF[j] != b.spF[j] || a.spW[j] != b.spW[j]) return false;
        return a.teleSrc == b.teleSrc && a.teleDst == b.teleDst && a.teleN == b.teleN;
    }
    void planToOrders(const Plan& pl, Orders& o) const {
        o.clear();
        for (int j = 0; j < pl.nsite; j++) {
            int cell = j == 0 ? -1 : pl.site[j];
            if (pl.spF[j] > 0) o.addSpawn(0, pl.spF[j], cell);
        }
        for (int j = 0; j < pl.nsite; j++) {
            int cell = j == 0 ? -1 : pl.site[j];
            if (pl.spW[j] > 0) o.addSpawn(1, pl.spW[j], cell);
        }
        if (pl.teleSrc >= 0 && pl.teleN > 0) o.addTele(pl.teleSrc, pl.teleKind, pl.teleN, pl.teleDst);
        for (int q = 0; q < NC; q++) for (int d = 0; d < 4; d++) if (pl.wm[q][d] > 0) o.addMove(q, 1, d, pl.wm[q][d]);
        for (int q = 0; q < NC; q++) for (int d = 0; d < 4; d++) if (pl.fm[q][d] > 0) o.addMove(q, 0, d, pl.fm[q][d]);
        for (int i = 0; i < M.nb; i++) o.addPrio(prioOrder[i]);
    }

    vector<string> planToStrings(const Plan& pl, const State& s) const {
        vector<string> out;
        auto XY = [&](int c) { return make_pair(c % GW, c / GW); };
        for (int j = 0; j < pl.nsite; j++) {
            if (pl.spF[j] > 0) out.push_back(j == 0 ? p::spawn("F", pl.spF[j]) : p::spawn("F", pl.spF[j], XY(pl.site[j]).first, XY(pl.site[j]).second));
        }
        for (int j = 0; j < pl.nsite; j++) {
            if (pl.spW[j] > 0) out.push_back(j == 0 ? p::spawn("W", pl.spW[j]) : p::spawn("W", pl.spW[j], XY(pl.site[j]).first, XY(pl.site[j]).second));
        }
        (void)s;
        // priority: buildings where my F will end (F on building after moves) — filled by caller via prioOrder; emit all buildings ordered
        // (only meaningful ones: buildings not owned by me)
        static const char* dn[4] = {"U", "D", "L", "R"};
        if (pl.teleSrc >= 0 && pl.teleN > 0) out.push_back(p::tele(pl.teleSrc % GW, pl.teleSrc / GW, pl.teleKind == 0 ? "F" : "W", pl.teleN, pl.teleDst % GW, pl.teleDst / GW));
        for (int q = 0; q < NC; q++) for (int d = 0; d < 4; d++) if (pl.wm[q][d] > 0) out.push_back(p::move(q % GW, q / GW, "W", pl.wm[q][d], dn[d]));
        for (int q = 0; q < NC; q++) for (int d = 0; d < 4; d++) if (pl.fm[q][d] > 0) out.push_back(p::move(q % GW, q / GW, "F", pl.fm[q][d], dn[d]));
        return out;
    }

    void appendPriority(vector<string>& out, const Plan& pl, const State& s0, const vector<string>& base) {
        // recompute priority: buildings that my F might capture (F on/arriving at non-owned building), by value
        (void)pl; (void)s0;
        for (auto& c : base) if (c.rfind("PRIORITY", 0) == 0) { out.insert(out.begin(), c); break; }
    }

    // ---- evaluation
    double ptsWeight(int t) const { return 3.0 + 0.22 * t; }
    double horizon(int t) const { return min(60, max(0, 160 - t)) * 0.5; }

    double evalState(const State& s) const {
        int t = s.turn;
        double val = 0;
        int sc[2] = {score2(M, s, 0), score2(M, s, 1)};
        val += ptsWeight(t) * 0.5 * (sc[0] - sc[1]);
        int hall[2], eng[2];
        double prod[2];
        for (int k = 0; k < 2; k++) {
            hall[k] = owned(M, s, k, T_HALL); eng[k] = owned(M, s, k, T_ENG) > 0 ? 1 : 0;
            prod[k] = (10.0 + 2 * hall[k]) / (3 - eng[k]);
        }
        val += (prod[0] - prod[1]) * horizon(t);
        int Wt[2] = {0, 0}, Ft[2] = {0, 0};
        for (int c = 0; c < NC; c++) { Wt[0] += s.W[0][c]; Wt[1] += s.W[1][c]; Ft[0] += s.F[0][c]; Ft[1] += s.F[1][c]; }
        val += (Wt[0] - Wt[1]) * 1.0 + (Ft[0] - Ft[1]) * 3.0;
        val += (s.res[0] / double(3 - eng[0]) - s.res[1] / double(3 - eng[1])) * 1.0;
        return val;
    }


    // ---- per-turn context for the potential (marginal building values in W-equivalents)
    double vHold[MAXB], vTake[MAXB];
    int enSites[10], nEnSites = 0;
    bool enCanSpawnF = false;

    void buildCtx(const State& s0) {
        State t = s0; t.turn = s0.turn + 1;
        for (int b = 0; b < M.nb; b++) {
            int keep = t.owner[b];
            t.owner[b] = 0; double a = evalState(t);
            t.owner[b] = -1; double n = evalState(t);
            t.owner[b] = 1; double e = evalState(t);
            t.owner[b] = keep;
            vHold[b] = a - n;
            vTake[b] = (s0.owner[b] == 1) ? (a - e) : (a - n);
            if (M.btype[b] == T_DEPOT && !((s0.dep[b] >> 0) & 1)) { vTake[b] += 7.5; }
            if (vHold[b] < 0) vHold[b] = 0;
            if (vTake[b] < 0) vTake[b] = 0;
        }
        nEnSites = 0; enSites[nEnSites++] = M.base[1];
        for (int b = 0; b < M.nb; b++) if (M.btype[b] == T_HOSPITAL && s0.owner[b] == 1 && nEnSites < 10) enSites[nEnSites++] = M.bcell[b];
        enCanSpawnF = s0.res[1] >= 5;
    }

    static double sig(double z) { return 1.0 / (1.0 + exp(-z)); }

    double potential(const State& s) const {
        int mwc[NC], mwn[NC], nmw = 0, ewc[NC], ewn[NC], new_ = 0, mfc[NC], nmf = 0, efc[NC], nef = 0;
        for (int c = 0; c < NC; c++) {
            if (s.W[0][c]) { mwc[nmw] = c; mwn[nmw++] = s.W[0][c]; }
            if (s.W[1][c]) { ewc[new_] = c; ewn[new_++] = s.W[1][c]; }
            if (s.F[0][c]) mfc[nmf++] = c;
            if (s.F[1][c]) efc[nef++] = c;
        }
        static const double rho[8] = {1.0, 1.0, 0.85, 0.6, 0.4, 0.25, 0.12, 0.0};
        const double K = prm(1, 4.0);
        double tot = 0;
        for (int b = 0; b < M.nb; b++) {
            int cb = M.bcell[b];
            bool mine = s.owner[b] == 0;
            int tf = 99;
            if (mine) {
                for (int i = 0; i < nef; i++) tf = min<int>(tf, M.dist[efc[i]][cb]);
                if (enCanSpawnF) for (int i = 0; i < nEnSites; i++) tf = min<int>(tf, M.dist[enSites[i]][cb] + 1);
            } else {
                for (int i = 0; i < nmf; i++) tf = min<int>(tf, M.dist[mfc[i]][cb]);
            }
            if (tf > 6) continue;
            int tau = max(1, tf);
            double D = 0, E = 0;
            for (int i = 0; i < nmw; i++) { int d = M.dist[mwc[i]][cb]; if (d <= tau) D += mwn[i]; else if (d == tau + 1) D += 0.5 * mwn[i]; }
            for (int i = 0; i < new_; i++) { int d = M.dist[ewc[i]][cb]; if (d <= tau) E += ewn[i]; else if (d == tau + 1) E += 0.5 * ewn[i]; }
            double x = (D - E) / (D + E + 2.0);
            if (mine) tot -= vHold[b] * rho[tf] * sig(-K * x);
            else tot += vTake[b] * rho[tf] * sig(K * x);
        }
        return tot;
    }


    // ---- adversarial ("worst-case response") scenario: enemy stacks converge on my F destinations
    Plan ePlan; bool haveEPlan = false;
    double pDev = 0.25;   // trust in the prediction: weight of the adversary scenario

    void buildAdversary(const State& s0, const Plan& my, Orders& oAdv) const {
        int Wdest[NC], Fdest[NC];
        memset(Wdest, 0, sizeof(Wdest)); memset(Fdest, 0, sizeof(Fdest));
        for (int kind = 0; kind < 2; kind++) {
            int* dest = kind == 0 ? Fdest : Wdest;
            for (int q = 0; q < NC; q++) {
                int pool = poolAt(s0, my, kind, q); if (pool <= 0) continue;
                const int16_t* mv = kind == 0 ? my.fm[q] : my.wm[q];
                int sum = 0;
                for (int d = 0; d < 4; d++) if (mv[d] > 0 && M.nbr[q][d] >= 0) { dest[M.nbr[q][d]] += mv[d]; sum += mv[d]; }
                dest[q] += max(0, pool - sum);
            }
        }
        // enemy plan copy
        static thread_local Plan ep;
        ep = ePlan;
        bool used[NC]; memset(used, 0, sizeof(used));
        // F cells sorted by F count desc
        int fc[NC], nf = 0;
        for (int c = 0; c < NC; c++) if (Fdest[c] > 0) fc[nf++] = c;
        sort(fc, fc + nf, [&](int a, int b) { return Fdest[a] > Fdest[b]; });
        for (int i = 0; i < nf; i++) {
            int c = fc[i];
            int need = Wdest[c];   // my defenders arriving at c
            int cand[5], nc = 0;
            int cells[5] = {c, M.nbr[c][0], M.nbr[c][1], M.nbr[c][2], M.nbr[c][3]};
            for (int k = 0; k < 5; k++) { int q = cells[k]; if (q >= 0 && !used[q] && s0.W[1][q] > 0) cand[nc++] = q; }
            sort(cand, cand + nc, [&](int a, int b) { return s0.W[1][a] > s0.W[1][b]; });
            int tot = 0; for (int k = 0; k < nc; k++) tot += s0.W[1][cand[k]];
            if (tot <= need) continue;
            // assign minimal set (largest first) until exceeding need
            int acc = 0;
            for (int k = 0; k < nc && acc <= need; k++) {
                int q = cand[k]; used[q] = true; acc += s0.W[1][q];
                for (int d = 0; d < 4; d++) ep.wm[q][d] = 0;
                if (q != c) { for (int d = 0; d < 4; d++) if (M.nbr[q][d] == c) ep.wm[q][d] = (int16_t)s0.W[1][q]; }
            }
        }
        // rebuild orders: keep spawns/prio/tele from oEn, replace moves
        oAdv = oEn; int keep = 0;
        for (int i = 0; i < oAdv.nmv; i++) if (oAdv.mv[i].dir == 4) oAdv.mv[keep++] = oAdv.mv[i];
        oAdv.nmv = keep;
        for (int q = 0; q < NC; q++) for (int d = 0; d < 4; d++) if (ep.wm[q][d] > 0) oAdv.addMove(q, 1, d, ep.wm[q][d]);
        for (int q = 0; q < NC; q++) for (int d = 0; d < 4; d++) if (ep.fm[q][d] > 0) oAdv.addMove(q, 0, d, ep.fm[q][d]);
    }

    // ---- main decision
    Orders oEn;
    State predState; bool havePred = false;
    FILE* logf = nullptr; bool logInit = false;
    void logAcc(const State& s0) {
        if (!logInit) { logInit = true; const char* e = getenv("BLOG"); if (e) logf = fopen(e, "a"); }
        if (!logf || !havePred) return;
        int tot = 0, hitW = 0, totF = 0, hitF = 0;
        for (int c = 0; c < NC; c++) { tot += s0.W[1][c]; hitW += min<int>(s0.W[1][c], predState.W[1][c]); totF += s0.F[1][c]; hitF += min<int>(s0.F[1][c], predState.F[1][c]); }
        fprintf(logf, "T%d predW %d/%d predF %d/%d model %d\n", s0.turn + 1, hitW, tot, hitF, totF, em); fflush(logf);
    }
    int nEvals = 0;
    bool timeUp(double deadline) const { return nEvals > (int)prm(9, 3000) || (prm(8, 0) < 0.5 && nowMs() > deadline); }
    double evalPlan(const State& s0, const Plan& pl) {
        nEvals++;
        Orders o; planToOrders(pl, o);
        State s = s0;
        step(M, s, o, oEn, cfg);
        double v = evalState(s) + prm(0, 0.0) * potential(s);
        if (pDev > 0.0 && haveEPlan) {
            Orders oa; buildAdversary(s0, pl, oa);
            State s2 = s0; step(M, s2, o, oa, cfg);
            double va = evalState(s2) + prm(0, 0.0) * potential(s2);
            v = (1.0 - pDev) * v + pDev * va;
        }
        return v;
    }


    // ---- local search over Plan (Blotto-style reallocation of stacks)
    int poolAt(const State& s0, const Plan& pl, int kind, int q) const {
        int n = kind == 0 ? s0.F[0][q] : s0.W[0][q];
        for (int j = 0; j < pl.nsite; j++) if (pl.site[j] == q) n += kind == 0 ? pl.spF[j] : pl.spW[j];
        return n;
    }
    void localSearch(const State& s0, Plan& pl, double& bestVal, double deadline) {
        vector<int> cellsW, cellsF;
        for (int pass = 0; pass < 4; pass++) {
            bool improved = false;
            for (int kind = 1; kind >= (prm(2, 0) > 0.5 ? 1 : 0); kind--) {
                // cells with units (after spawns)
                vector<pair<int,int>> cs;
                for (int q = 0; q < NC; q++) { int n = poolAt(s0, pl, kind, q); if (n > 0) cs.push_back({-n, q}); }
                sort(cs.begin(), cs.end());
                for (auto& pr : cs) {
                    int q = pr.second, n = -pr.first;
                    if (timeUp(deadline)) return;
                    int16_t* mv = kind == 1 ? pl.wm[q] : pl.fm[q];
                    // options: 0..3 dirs, 4 = stay
                    for (int b = 0; b < 5; b++) {
                        if (b < 4 && M.nbr[q][b] < 0) continue;
                        for (int a = 0; a < 5; a++) {
                            if (a == b) continue;
                            int cur[5]; int sum = 0;
                            for (int d = 0; d < 4; d++) { cur[d] = mv[d]; sum += mv[d]; }
                            cur[4] = max(0, n - sum);
                            if (cur[a] <= 0) continue;
                            int cand[3] = {1, (cur[a] + 1) / 2, cur[a]};
                            int last = -1;
                            for (int di = 0; di < 3; di++) {
                                int dl = cand[di]; if (dl == last) continue; last = dl;
                                int16_t save[4]; memcpy(save, mv, sizeof(save));
                                if (a < 4) mv[a] -= dl; else { /* taking from stay: nothing to subtract */ }
                                if (b < 4) mv[b] += dl;
                                double val = evalPlan(s0, pl);
                                if (val > bestVal + 1e-9) { bestVal = val; improved = true; n = poolAt(s0, pl, kind, q); }
                                else memcpy(mv, save, sizeof(save));
                            }
                        }
                    }
                }
            }
            if (prm(4, 0.0) > 0.5) { if (searchSpawn(s0, pl, bestVal, deadline)) improved = true; }
            if (prm(5, 0.0) > 0.5) { if (searchConverge(s0, pl, bestVal, deadline)) improved = true; }
            if (!improved) break;
        }
    }


    // spawn-site reallocation
    bool searchSpawn(const State& s0, Plan& pl, double& bestVal, double deadline) {
        bool improved = false;
        for (int kind = 0; kind < 2; kind++) {
            int* sp = kind == 0 ? pl.spF : pl.spW;
            for (int a = 0; a < pl.nsite; a++) {
                if (sp[a] <= 0) continue;
                for (int b = 0; b < pl.nsite; b++) {
                    if (a == b) continue;
                    if (timeUp(deadline)) return improved;
                    int cands[2] = {1, sp[a]};
                    for (int di = 0; di < 2; di++) {
                        int dl = cands[di]; if (dl > sp[a] || dl <= 0) continue; if (di == 1 && cands[0] == cands[1]) continue;
                        sp[a] -= dl; sp[b] += dl;
                        double val = evalPlan(s0, pl);
                        if (val > bestVal + 1e-9) { bestVal = val; improved = true; }
                        else { sp[a] += dl; sp[b] -= dl; }
                        if (sp[a] <= 0) break;
                    }
                }
            }
        }
        return improved;
    }

    // convergence moves: send every W stack adjacent to cell c into c
    bool searchConverge(const State& s0, Plan& pl, double& bestVal, double deadline) {
        bool improved = false;
        bool isCand[NC]; memset(isCand, 0, sizeof(isCand));
        for (int c = 0; c < NC; c++) if (s0.W[1][c] > 0 || s0.F[1][c] > 0 || s0.F[0][c] > 0) isCand[c] = true;
        for (int b = 0; b < M.nb; b++) isCand[M.bcell[b]] = true;
        for (int c = 0; c < NC; c++) {
            if (!isCand[c] || !M.pass[c]) continue;
            if (timeUp(deadline)) return improved;
            Plan cand = pl; bool changed = false;
            for (int d = 0; d < 4; d++) {
                int q = M.nbr[c][d]; if (q < 0) continue;
                int pool = poolAt(s0, cand, 1, q); if (pool <= 0) continue;
                // direction from q to c
                int dir = -1; for (int e = 0; e < 4; e++) if (M.nbr[q][e] == c) dir = e;
                if (dir < 0) continue;
                if (cand.wm[q][dir] == pool) continue;
                for (int e = 0; e < 4; e++) cand.wm[q][e] = 0;
                cand.wm[q][dir] = (int16_t)pool; changed = true;
            }
            { int pool = poolAt(s0, cand, 1, c); if (pool > 0) { bool any = false; for (int e = 0; e < 4; e++) if (cand.wm[c][e]) any = true; if (any) { for (int e = 0; e < 4; e++) cand.wm[c][e] = 0; changed = true; } } }
            if (!changed) continue;
            double val = evalPlan(s0, cand);
            if (val > bestVal + 1e-9) { bestVal = val; pl = cand; improved = true; }
        }
        return improved;
    }


    // ---- rollouts: play both sides with the v17 policy from a hypothetical state
    p::View makeView(const State& s, int turnNo, bool mine) const {
        p::View v; v.turn = turnNo;
        v.my_resource = mine ? s.res[0] : s.res[1]; v.opp_resource = mine ? s.res[1] : s.res[0];
        for (int t = 0; t < 2; t++) for (int k = 0; k < 2; k++) for (int c = 0; c < NC; c++) {
            int n = k == 0 ? s.F[t][c] : s.W[t][c];
            if (n > 0) { p::Unit u; u.team = t == 0 ? me : opp; u.kind = k == 0 ? "F" : "W"; u.x = c % GW; u.y = c / GW; u.count = n; v.units.push_back(u); }
        }
        for (int b = 0; b < M.nb; b++) {
            p::Building pb; pb.id = M.bid[b]; pb.x = M.bcell[b] % GW; pb.y = M.bcell[b] / GW; pb.type = btypeStr[b];
            pb.owner = s.owner[b] < 0 ? "N" : (s.owner[b] == 0 ? me : opp); pb.stage = s.owner[b] < 0 ? 0 : 2;
            pb.score = known[M.bid[b]];
            v.buildings.push_back(pb);
        }
        return v;
    }

    struct Group { int kind, cell, n, target; };   // my units (kind 0=F, 1=W) walking to target and holding there

    int stepDir(int from, int to) const {
        if (from == to) return -1;
        int best = -1, bd = 255;
        for (int d = 0; d < 4; d++) { int q = M.nbr[from][d]; if (q >= 0 && M.dist[q][to] < bd) { bd = M.dist[q][to]; best = d; } }
        return best;
    }

    void overrideGroup(Orders& o, const State& s, int kind, int x, int g, int dir) const {
        int pool = kind == 0 ? s.F[0][x] : s.W[0][x];
        for (int i = 0; i < o.nsp; i++) { auto& sp = o.sp[i]; if (sp.kind == kind) { int cell = sp.cell < 0 ? M.base[0] : sp.cell; if (cell == x) pool += sp.n; } }
        int moved = 0;
        for (int i = 0; i < o.nmv; i++) if (o.mv[i].kind == kind && o.mv[i].dir < 4 && o.mv[i].cell == x) moved += o.mv[i].n;
        int stay = max(0, pool - moved);
        int need = g - stay;
        for (int i = o.nmv - 1; i >= 0 && need > 0; i--) if (o.mv[i].kind == kind && o.mv[i].dir < 4 && o.mv[i].cell == x) { int r = min<int>(need, o.mv[i].n); o.mv[i].n -= r; need -= r; }
        if (dir >= 0) o.addMove(x, kind, dir, g);
    }

    // rollout from state s (already advanced by the committed first step). turnNo = turn number of the next decision.
    // firstLoss[b]: first step index at which a building I owned at the start is no longer mine; firstGain[b]: first step at which a building I did not own becomes mine.
    double rollout(State s, int turnNo, int H, vector<Group> groups, int* firstLoss, int* firstGain = nullptr) {
        scrM = pMe;
        if (em == 0) s17 = e17; else if (em == 1) s16 = e16; else s15 = e15;
        bool ownStart[MAXB]; for (int b = 0; b < M.nb; b++) ownStart[b] = s.owner[b] == 0;
        if (firstLoss) for (int b = 0; b < M.nb; b++) firstLoss[b] = 99;
        if (firstGain) for (int b = 0; b < M.nb; b++) firstGain[b] = 99;
        double mid = 0;
        for (int k = 0; k < H; k++) {
            int tn = turnNo + k;
            if (tn > 160) break;
            Orders om, oe;
            { p::View v = makeView(s, tn, true); vector<string> c = scrM.decide(v, initMe); parseToOrders(c, om, 0); }
            { p::View v = makeView(s, tn, false); vector<string> c = em == 0 ? s17.decide(v, initEn) : (em == 1 ? s16.decide(v, initEn) : s15.decide(v, initEn)); parseToOrders(c, oe, 1); }
            for (auto& g : groups) if (g.n > 0) overrideGroup(om, s, g.kind, g.cell, g.n, stepDir(g.cell, g.target));
            step(M, s, om, oe, cfg);
            for (auto& g : groups) if (g.n > 0) { int d = stepDir(g.cell, g.target); int nc = d < 0 ? g.cell : M.nbr[g.cell][d]; g.cell = nc; g.n = min<int>(g.n, g.kind == 0 ? s.F[0][nc] : s.W[0][nc]); }
            for (int b = 0; b < M.nb; b++) {
                if (firstLoss && ownStart[b] && s.owner[b] != 0 && firstLoss[b] == 99) firstLoss[b] = k;
                if (firstGain && !ownStart[b] && s.owner[b] == 0 && firstGain[b] == 99) firstGain[b] = k;
            }
            if (k == H / 2) mid = evalState(s);
        }
        return 0.5 * evalState(s) + 0.5 * mid;
    }

    // apply a group's first step to a plan; returns the tracking group (position after the first step, before combat losses)
    Group applyGroup(Plan& cand, int kind, int q, int n, int target) const {
        int16_t* mv = kind == 0 ? cand.fm[q] : cand.wm[q];
        for (int e = 0; e < 4; e++) mv[e] = 0;
        int dir = stepDir(q, target);
        if (dir >= 0) mv[dir] = (int16_t)n;
        int nq = dir >= 0 ? M.nbr[q][dir] : q;
        return {kind, nq, n, target};
    }

    // Macro search: given plan P1 (exact-best single step), try reinforcing threatened buildings and attacking valuable ones.
    void macroSearch(const State& s0, Plan& P1, double deadline, int turnNo) {
        int H = (int)prm(10, 8);
        Plan cur = P1; vector<Group> G;
        auto sim1 = [&](const Plan& p, State& out) { out = s0; Orders o; planToOrders(p, o); step(M, out, o, oEn, cfg); };
        State s1; sim1(cur, s1);
        int firstLoss[MAXB], firstGain[MAXB];
        double bestV = rollout(s1, turnNo + 1, H, G, firstLoss, firstGain);
        int tried = 0; const int maxTry = (int)prm(11, 6); const double thr = prm(12, 3.0);
        // ---- phase A: defence of threatened buildings
        vector<pair<double,int>> thr1;
        for (int b = 0; b < M.nb; b++) if (firstLoss[b] < 99 && s1.owner[b] == 0) thr1.push_back({-vHold[b], b});
        sort(thr1.begin(), thr1.end());
        for (auto& tb : thr1) {
            int b = tb.second; int cb = M.bcell[b]; int lossAt = firstLoss[b];
            vector<pair<int,int>> st;
            for (int q = 0; q < NC; q++) if (s0.W[0][q] >= 2) { int d = M.dist[q][cb]; if (d < 255 && d <= lossAt + 3) st.push_back({d, q}); }
            sort(st.begin(), st.end());
            for (size_t i = 0; i < st.size() && i < 3; i++) {
                int q = st[i].second; int pool = poolAt(s0, cur, 1, q);
                for (int variant = 0; variant < 2; variant++) {
                    int g = variant == 0 ? pool : (pool + 1) / 2; if (g < 2 || (variant == 1 && g == pool)) continue;
                    if (nowMs() > deadline || tried >= maxTry) goto phaseB;
                    Plan cand = cur; Group gr = applyGroup(cand, 1, q, g, cb);
                    State sc; sim1(cand, sc);
                    gr.n = min<int>(gr.n, sc.W[0][gr.cell]);
                    vector<Group> GG = G; GG.push_back(gr);
                    double v = rollout(sc, turnNo + 1, H, GG, nullptr);
                    tried++;
                    if (v > bestV + thr) { bestV = v; cur = cand; G = GG; }
                }
            }
        }
        phaseB:
        // ---- phase B: attack valuable buildings not taken in the baseline rollout
        if (prm(15, 1) > 0.5) {
            State sc0; sim1(cur, sc0);
            vector<pair<double,int>> tg;
            for (int b = 0; b < M.nb; b++) if (s1.owner[b] != 0 && firstGain[b] == 99) tg.push_back({-vTake[b], b});
            sort(tg.begin(), tg.end());
            for (size_t ti = 0; ti < tg.size() && ti < 3; ti++) {
                int b = tg[ti].second; int cb = M.bcell[b];
                // nearest F stack
                int qF = -1, dF = 255;
                for (int q = 0; q < NC; q++) if (s0.F[0][q] > 0) { int d = M.dist[q][cb]; if (d < dF) { dF = d; qF = q; } }
                if (qF < 0 || dF > H) continue;
                vector<pair<int,int>> st;
                for (int q = 0; q < NC; q++) if (s0.W[0][q] >= 2) { int d = M.dist[q][cb]; if (d < 255 && d <= dF + 1) st.push_back({d, q}); }
                sort(st.begin(), st.end());
                if (nowMs() > deadline || tried >= maxTry) break;
                Plan cand = cur; vector<Group> GG = G;
                Group gf = applyGroup(cand, 0, qF, 1, cb); GG.push_back(gf);
                for (size_t i = 0; i < st.size() && i < 2; i++) { int q = st[i].second; int pool = poolAt(s0, cand, 1, q); Group gw = applyGroup(cand, 1, q, pool, cb); GG.push_back(gw); }
                State sc; sim1(cand, sc);
                for (auto& g : GG) if (&g - &GG[0] >= (long)G.size()) g.n = min<int>(g.n, g.kind == 0 ? sc.F[0][g.cell] : sc.W[0][g.cell]);
                double v = rollout(sc, turnNo + 1, H, GG, nullptr);
                tried++;
                if (v > bestV + thr) { bestV = v; cur = cand; G = GG; }
            }
        }
        P1 = cur;
    }

    vector<string> decide(const p::View& v, const p::Init& init) {
        double t0 = nowMs(); nEvals = 0; pDev = prm(3, 0.0);
        init_once(init);
        updateScores(v);
        State s0; buildState(v, s0);
        logAcc(s0);
        buildCtx(s0);
        // priority order for my captures: by (rough) value desc
        {
            vector<int> ord(M.nb); for (int i = 0; i < M.nb; i++) ord[i] = i;
            stable_sort(ord.begin(), ord.end(), [&](int a, int b) {
                auto val = [&](int i) { int w = 0; int ty = M.btype[i]; if (ty == T_ENG) w = 100; else if (ty == T_HALL) w = 90; else if (ty == T_DEPOT) w = 50; else if (ty == T_HOSPITAL) w = 30; return w + M.score2[i]; };
                return val(a) > val(b);
            });
            for (int i = 0; i < M.nb; i++) prioOrder[i] = ord[i];
        }
        // enemy prediction: run all candidate models, score them on last turn's outcome, use the best
        {
            p::View ve = v; ve.my_resource = v.opp_resource; ve.opp_resource = v.my_resource;
            if (havePrev) {
                for (int k = 0; k < 3; k++) {
                    State st = prevS0; step(M, st, prevMy, prevModel[k], cfg);
                    int tot = 0, hit = 0;
                    for (int c = 0; c < NC; c++) { tot += s0.W[1][c] + s0.F[1][c]; hit += min<int>(s0.W[1][c], st.W[1][c]) + min<int>(s0.F[1][c], st.F[1][c]); }
                    double acc = tot > 0 ? double(hit) / tot : 1.0;
                    modelScore[k] = 0.8 * modelScore[k] + acc;
                }
                int bestk = 0; for (int k = 1; k < 3; k++) if (modelScore[k] > modelScore[bestk] + 0.05) bestk = k;
                if (prm(14, 1) > 0.5) em = bestk;
            }
            vector<string> c17 = e17.decide(ve, initEn), c16 = e16.decide(ve, initEn), c15 = e15.decide(ve, initEn);
            parseToOrders(c17, prevModel[0], 1); parseToOrders(c16, prevModel[1], 1); parseToOrders(c15, prevModel[2], 1);
            oEn = prevModel[em];
            ordersToPlan(oEn, s0, ePlan, 1); haveEPlan = true;
            prevS0 = s0;
        }
        // baseline proposal
        vector<string> base = pMe.decide(v, init);
        Orders oBase; parseToOrders(base, oBase, 0);
        Plan pl; ordersToPlan(oBase, s0, pl);
        // baseline strings (keeps v17's own priority)
        Plan best = pl;
        double bestVal = evalPlan(s0, best);
        double baseVal = bestVal;
#ifndef NOSEARCH
        localSearch(s0, best, bestVal, t0 + 60);
#endif
        if (prm(13, 1) > 0.5 && v.turn >= 5 && v.turn <= 155) macroSearch(s0, best, t0 + 85, v.turn);
        {
            Orders om; planToOrders(best, om); predState = s0; step(M, predState, om, oEn, cfg); havePred = true;
            prevMy = om; havePrev = true;
        }
        vector<string> out;
#ifdef NOSEARCH
        out = base;
#else
        if (!samePlan(best, pl)) { out = planToStrings(best, s0); appendPriority(out, best, s0, base); }
        else out = base;
#endif
        double dt = nowMs() - t0;
        if (logf) {
            int cw = 0, cf = 0, cs = 0;
            for (int q = 0; q < NC; q++) for (int d = 0; d < 4; d++) { cw += abs(best.wm[q][d] - pl.wm[q][d]); cf += abs(best.fm[q][d] - pl.fm[q][d]); }
            for (int j = 0; j < best.nsite; j++) cs += abs(best.spW[j] - pl.spW[j]) + abs(best.spF[j] - pl.spF[j]);
            fprintf(logf, "CHG %d W%d F%d SP%d dv %.1f\n", v.turn, cw / 2, cf / 2, cs / 2, bestVal - baseVal);
            static const char* dn[5] = {"U", "D", "L", "R", "."};
            if (getenv("BDUMP") && atoi(getenv("BDUMP")) == v.turn) {
                for (int kind = 0; kind < 2; kind++) for (int q = 0; q < NC; q++) for (int d = 0; d < 4; d++) {
                    int a = (kind ? pl.wm : pl.fm)[q][d], b = (kind ? best.wm : best.fm)[q][d];
                    if (a != b) fprintf(logf, "  %c (%d,%d) %s base %d -> %d\n", kind ? 'W' : 'F', q % GW, q / GW, dn[d], a, b);
                }
            }
        }
        if (logf) { fprintf(logf, "TIME %d %.2f evals %d\n", v.turn, dt, nEvals); fflush(logf); }
        (void)dt;
        return out;
    }
};

}  // namespace blotto

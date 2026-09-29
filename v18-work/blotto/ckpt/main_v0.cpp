// blotto bot (v18 candidate): exact one-turn look-ahead + Blotto-style resource allocation search
// on top of a v17-style heuristic proposal. C++20, standard library only.
#include "protocol.hpp"
#include "engine.hpp"
#include "v17.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <sstream>

using namespace std;
using namespace eng;

namespace {

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
    p::Init initEn;
    v17::Planner pMe, pEn;
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
        initEn = init; initEn.team = init.opp; initEn.opp = init.team;
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
    void ordersToPlan(const Orders& o, const State& s, Plan& pl) {
        pl.clear();
        pl.nsite = 1; pl.site[0] = M.base[0];
        for (int b = 0; b < M.nb; b++) if (M.btype[b] == T_HOSPITAL && s.owner[b] == 0) pl.site[pl.nsite++] = M.bcell[b];
        for (int i = 0; i < o.nsp; i++) {
            auto& sp = o.sp[i]; int cell = sp.cell < 0 ? M.base[0] : sp.cell;
            for (int j = 0; j < pl.nsite; j++) if (pl.site[j] == cell) { (sp.kind == 0 ? pl.spF : pl.spW)[j] += sp.n; break; }
        }
        for (int i = 0; i < o.nmv; i++) {
            auto& m = o.mv[i];
            if (m.dir < 4) (m.kind == 0 ? pl.fm : pl.wm)[m.cell][m.dir] += m.n;
            else { pl.teleSrc = m.cell; pl.teleDst = m.cell2; pl.teleKind = m.kind; pl.teleN = m.n; }
        }
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

    // ---- main decision
    Orders oEn;
    State predState; bool havePred = false;
    FILE* logf = nullptr; bool logInit = false;
    void logAcc(const State& s0) {
        if (!logInit) { logInit = true; const char* e = getenv("BLOG"); if (e) logf = fopen(e, "a"); }
        if (!logf || !havePred) return;
        int tot = 0, hitW = 0, totF = 0, hitF = 0;
        for (int c = 0; c < NC; c++) { tot += s0.W[1][c]; hitW += min<int>(s0.W[1][c], predState.W[1][c]); totF += s0.F[1][c]; hitF += min<int>(s0.F[1][c], predState.F[1][c]); }
        fprintf(logf, "T%d predW %d/%d predF %d/%d\n", s0.turn + 1, hitW, tot, hitF, totF);
    }
    double evalPlan(const State& s0, const Plan& pl) {
        Orders o; planToOrders(pl, o);
        State s = s0;
        step(M, s, o, oEn, cfg);
        return evalState(s);
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
            for (int kind = 1; kind >= 0; kind--) {
                // cells with units (after spawns)
                vector<pair<int,int>> cs;
                for (int q = 0; q < NC; q++) { int n = poolAt(s0, pl, kind, q); if (n > 0) cs.push_back({-n, q}); }
                sort(cs.begin(), cs.end());
                for (auto& pr : cs) {
                    int q = pr.second, n = -pr.first;
                    if (nowMs() > deadline) return;
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
            if (!improved) break;
        }
    }

    vector<string> decide(const p::View& v, const p::Init& init) {
        double t0 = nowMs();
        init_once(init);
        updateScores(v);
        State s0; buildState(v, s0);
        logAcc(s0);
        // priority order for my captures: by (rough) value desc
        {
            vector<int> ord(M.nb); for (int i = 0; i < M.nb; i++) ord[i] = i;
            stable_sort(ord.begin(), ord.end(), [&](int a, int b) {
                auto val = [&](int i) { int w = 0; int ty = M.btype[i]; if (ty == T_ENG) w = 100; else if (ty == T_HALL) w = 90; else if (ty == T_DEPOT) w = 50; else if (ty == T_HOSPITAL) w = 30; return w + M.score2[i]; };
                return val(a) > val(b);
            });
            for (int i = 0; i < M.nb; i++) prioOrder[i] = ord[i];
        }
        // enemy prediction (mirror model)
        {
            p::View ve = v; ve.my_resource = v.opp_resource; ve.opp_resource = v.my_resource;
            vector<string> ec = pEn.decide(ve, initEn);
            parseToOrders(ec, oEn, 1);
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
        {
            Orders om; planToOrders(best, om); predState = s0; step(M, predState, om, oEn, cfg); havePred = true;
        }
        vector<string> out;
#ifdef NOSEARCH
        out = base;
#else
        if (bestVal > baseVal + 1e-9) { out = planToStrings(best, s0); appendPriority(out, best, s0, base); }
        else out = base;
#endif
        double dt = nowMs() - t0;
        if (logf) fprintf(logf, "TIME %d %.2f\n", v.turn, dt);
        (void)dt;
        return out;
    }
};

Bot bot;
}  // namespace

vector<string> decide(const p::View& view, const p::Init& init) { return bot.decide(view, init); }
int main() { ios::sync_with_stdio(false); cin.tie(nullptr); return p::run(decide); }

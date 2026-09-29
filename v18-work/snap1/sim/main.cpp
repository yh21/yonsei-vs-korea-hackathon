// v18 "sim": simulation-based look-ahead bot.
// - sim.hpp   : exact re-implementation of the game engine (verified against the reference engine on replays)
// - pol17.hpp : fast array re-implementation of the v15/16/17 decision procedure (bit-exact in EXACT mode)
// Each turn: build the game state from the input, predict the opponent's move with a shadow model,
// generate candidate doctrines (parameter sets / injected goals) for our own base policy, roll each out for
// H turns against the opponent model, and play the first move of the best rollout.
// C++20, standard library only.
#include "protocol.hpp"
#include "sim.hpp"
#include "pol17.hpp"
#include "eval.hpp"
#include "valuecoef.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace std;
using Clock = chrono::steady_clock;

#ifdef DEV
static int devInt(const char* name, int d) { const char* e = getenv(name); return e ? atoi(e) : d; }
#else
static int devInt(const char*, int d) { return d; }
#endif

namespace {
using namespace sim;

struct Cand {
    pol::Params P;
    int tag = 0;
    Orders orders;
    pol::Persist ps;
    double value = -1e18;
};

struct Bot {
    bool ready = false;
    Map M;
    pol::Near NR;
    string meS, oppS;
    int me = 0;
    int knownScore[MAXB];
    uint32_t oppRev = 0;
    int32_t occHist[2] = {0, 0}; int occTurn = 0;
    uint8_t everOwned[MAXB];  // bit0: me, bit1: opp
    pol::Persist psMe;
    struct Model { pol::Params P; pol::Persist ps, psPre; double cum = 0; Orders lastPred; };
    Model models[3];
    State prevRoot; bool havePrev = false; Orders prevMyOrders;
    int curModel = 0;
    pol::Params prevBestP; bool havePrevBest = false;
    int prevChoice = 0;
    uint64_t rngState = 88172645463325252ull;
    uint64_t rnd() { rngState ^= rngState << 13; rngState ^= rngState >> 7; rngState ^= rngState << 17; return rngState; }
    double rndu() { return (rnd() >> 11) * (1.0 / 9007199254740992.0); }
    int rndi(int a, int b) { return a + (int)(rnd() % (uint64_t)(b - a + 1)); }

    // ---------------- init ----------------
    void initOnce(const p::Init& init) {
        ready = true;
        meS = init.team; oppS = init.opp;
        me = 0;
        for (int c = 0; c < NC; c++) M.pass[c] = init.passable(c % GW, c / GW);
        M.nb = (int)init.buildings.size();
        for (auto& b : init.buildings) {
            M.bcell[b.id] = b.y * GW + b.x;
            M.btype[b.id] = btypeFromStr(b.type);
            M.score2[b.id] = 0;
        }
        auto mb = init.base(); auto ob = init.bases[meS == "Y" ? 1 : 0];
        M.base[0] = mb.second * GW + mb.first;
        M.base[1] = ob.second * GW + ob.first;
        M.flip[0] = meS == "K"; M.flip[1] = meS != "K";
        for (int b = 0; b < MAXB; b++) { knownScore[b] = -1; everOwned[b] = 0; }
        M.finish();
        NR.build(M);
        models[0].P = pol::Params::v17(); models[1].P = pol::Params::v16(); models[2].P = pol::Params::v15();
        for (int b = 0; b < M.nb; b++) if (M.btype[b] == T_PLAZA) knownScore[b] = 3;
        refreshScores();
    }

    void refreshScores() {
        M.total2 = 0;
        for (int b = 0; b < M.nb; b++) {
            int ks = knownScore[b];
            if (ks < 0 && M.sym[b] >= 0) ks = knownScore[M.sym[b]];
            int x = M.bcell[b] % GW;
            M.score2[b] = ks >= 0 ? 2 * ks : ((x >= 5 && x <= 9) ? 6 : 3);
            M.total2 += M.score2[b];
        }
    }

    // ---------------- state construction ----------------
    void buildState(const p::View& v, State& S, const p::Init& init) {
        S.init(M);
        S.turn = (int16_t)(v.turn - 1);
        S.res[0] = (int16_t)v.my_resource; S.res[1] = (int16_t)v.opp_resource;
        static int oppS_[NC];
        for (int c = 0; c < NC; c++) oppS_[c] = 0;
        for (auto& u : v.units) {
            int c = u.y * GW + u.x;
            int t = (u.team == meS) ? 0 : 1;
            if (u.kind == "F") S.F[t][c] += u.count;
            else if (u.kind == "W") S.Wn[t][c] += u.count;
            else if (t == 1) oppS_[c] += u.count;
        }
        uint32_t myRev = 0;
        for (auto& b : v.buildings) {
            int id = b.id;
            S.owner[id] = b.owner == meS ? 0 : (b.owner == oppS ? 1 : -1);
            if (S.owner[id] >= 0) everOwned[id] |= (1 << S.owner[id]);
            if (b.score >= 0) { myRev |= 1u << id; knownScore[id] = b.score; }
        }
        refreshScores();
        // opp reveal tracking (unit presence, ownership, scouts within 2, owned watch within 3)
        for (int b = 0; b < M.nb; b++) {
            int c = M.bcell[b], bx = c % GW, by = c / GW;
            bool r = S.owner[b] == 1 || S.F[1][c] || S.Wn[1][c] || oppS_[c];
            if (!r) {
                for (int q = 0; q < NC && !r; q++) if (oppS_[q] && max(abs(q % GW - bx), abs(q / GW - by)) <= 2) r = true;
                for (int w = 0; w < M.nb && !r; w++)
                    if (M.btype[w] == T_WATCH && S.owner[w] == 1) {
                        int wc = M.bcell[w];
                        if (max(abs(wc % GW - bx), abs(wc / GW - by)) <= 3) r = true;
                    }
            }
            if (r) oppRev |= 1u << b;
        }
        S.rev[0] = myRev; S.rev[1] = oppRev;
        for (int b = 0; b < M.nb; b++) {
            if (everOwned[b] & 1) S.dep[b] |= 1;
            if (everOwned[b] & 2) S.dep[b] |= 2;
        }
        // cumulative occupation (tie-break #2), accumulated from observed states (belief scores)
        if (occTurn < v.turn - 1) {
            occHist[0] += score2(M, S, 0); occHist[1] += score2(M, S, 1);
            occTurn = v.turn - 1;
        }
        S.occ[0] = occHist[0]; S.occ[1] = occHist[1];
        (void)init;
    }

    // ---------------- evaluation ----------------
    double evalOld(const State& s) const {
        int sc0 = score2(M, s, 0), sc1 = score2(M, s, 1);
        int w0 = totalW(s, 0), w1 = totalW(s, 1), f0 = totalF(s, 0), f1 = totalF(s, 1);
        double v = 50.0 * (sc0 - sc1);
        v += 2.0 * (w0 - w1) + 6.0 * (f0 - f1) + 1.0 * (s.res[0] - s.res[1]);
        int h0 = ownedCount(M, s, 0, T_HALL), h1 = ownedCount(M, s, 1, T_HALL);
        int e0 = ownedCount(M, s, 0, T_ENG) > 0, e1 = ownedCount(M, s, 1, T_ENG) > 0;
        v += 20.0 * (h0 - h1) + 40.0 * (e0 - e1);
        return v * 0.01;   // (scaled to roughly "points")
    }
    // Linear regression of the final score margin (points) on state features (see tools/fit_value.py)
    double evalState(const State& s) const {
        if (useOldEval) return evalOld(s);
        double f[ev::NF];
        ev::features(M, s, 0, f);
        double v = vc::VB;
        for (int i = 0; i < ev::NF; i++) v += vc::VW[i] * f[i];
        return v;
    }
    bool useOldEval = false;
    int exactPlies = 1;
    double stickBonus = 2.0, baseBonus = 1.0, earlyBonus = 0.0;
    double lastMs = 0;   // wall-clock cost of the previous decide() (slow-machine protection)

    // A partially advanced rollout (so that promising candidates can be extended to a longer horizon without redoing work)
    struct RS {
        State s; pol::Persist pm, po;
        int k = 0;               // plies already played
        bool ended = false;      // game finished inside the rollout
        double endVal = 0;
    };
    void rsStart(RS& r, const State& root, const Cand& c, const pol::Persist& poAfter) const {
        r.s = root; r.pm = c.ps; r.po = poAfter; r.k = 0; r.ended = false; r.endVal = 0;
    }
    // advances the rollout until `toK` plies have been played (ply 0 uses the given first orders)
    void rsAdvance(RS& r, const Cand& c, const Orders& oppFirst, const pol::Params& oppP, int toK) const {
        Orders om, oo;
        while (r.k < toK && !r.ended) {
            if (r.k == 0) { om = c.orders; oo = oppFirst; }
            else if (r.k < exactPlies) {
                pol::decide17(M, NR, r.s, 0, c.P, r.pm, om);
                pol::decide17(M, NR, r.s, 1, oppP, r.po, oo);
            } else {
                pol::decide17f(M, NR, r.s, 0, c.P, r.pm, om);
                pol::decide17f(M, NR, r.s, 1, oppP, r.po, oo);
            }
            step(M, r.s, om, oo, true);
            r.k++;
            int res = judge(M, r.s);
            if (res >= 0) {
                double margin = (score2(M, r.s, 0) - score2(M, r.s, 1)) * 0.5;
                r.endVal = (res == 0 ? 100.0 : (res == 1 ? -100.0 : 0.0)) + margin;
                r.ended = true;
            }
        }
    }
    double rsValue(const RS& r) const { return r.ended ? r.endVal : evalState(r.s); }

    // ---------------- candidate generation ----------------
    pol::Params randomParams(double amp) {
        pol::Params p = pol::Params::v17();
        static const int big[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 17, 18, 19, 20, 21, 22, 23, 24, 25};
        for (int i : big) {
            if (rndu() < 0.6) continue;
            double f = exp((rndu() * 2 - 1) * amp);
            int v = (int)lround(p.P[i] * f);
            if (v < 1) v = 1;
            p.P[i] = v;
        }
        for (int i = 0; i < 4; i++) if (rndu() < 0.3) p.P[i] = max(2, p.P[i] + rndi(-2, 2));
        if (rndu() < 0.25) p.P[15] = rndi(1, 5);
        if (rndu() < 0.25) p.P[16] = rndi(1, 4);
        if (rndu() < 0.25) p.garrisonAdd = rndi(-1, 2);
        if (rndu() < 0.15) p.fAdjust = !p.fAdjust;
        return p;
    }

    void makeCandidates(const State& S, vector<Cand>& cands, int nRandom) {
        Cand base; base.P = pol::Params::v17(); base.tag = 0; cands.push_back(base);
        if (havePrevBest && !prevBestP.same(base.P)) { Cand c; c.P = prevBestP; c.tag = 5; cands.push_back(c); }
        int varMask = devInt("SIM_VAR", 15);   // bit0 aggressive, bit1 defensive, bit2 few F, bit3 many F, bit4 eco
        vector<Cand> variants, strikes;
        if (varMask & 1) { Cand c = base; c.tag = 1; c.P.P[13] = 700; c.P.P[14] = 500; c.P.P[9] = 900; variants.push_back(c); }
        if (varMask & 2) { Cand c = base; c.tag = 2; c.P.P[11] = 1300; c.P.garrisonAdd = 1; variants.push_back(c); }
        if ((varMask & 16) || devInt("SIM_ECO", 0)) {
            Cand c = base; c.tag = 6; c.P.P[22] = 260; c.P.P[23] = 340; c.P.P[24] = 200; variants.push_back(c);
            Cand d = base; d.tag = 7; d.P.P[25] = 170; d.P.P[24] = 220; d.P.P[22] = 220; variants.push_back(d);
        }
        if (varMask & 4) { Cand c = base; c.tag = 3; c.P.P[0] = 5; c.P.P[1] = 4; c.P.P[2] = 4; c.P.P[3] = 3; variants.push_back(c); }
        if (varMask & 8) { Cand c = base; c.tag = 4; c.P.P[0] = 9; c.P.P[1] = 8; c.P.P[2] = 6; c.P.P[3] = 5; variants.push_back(c); }
        // strike candidates: inject a high-value goal on an enemy / neutral building
        {
            int nstrike = devInt("SIM_STRIKE", 4);
            int ids[MAXB], n = 0;
            for (int b = 0; b < M.nb; b++) if (S.owner[b] != 0) ids[n++] = b;
            auto typeBonus = [&](int b) {
                switch (M.btype[b]) {
                    case T_HALL: return 30; case T_ENG: return 26; case T_DEPOT: return (S.dep[b] & 1) ? 0 : 20;
                    case T_HOSPITAL: return 14; case T_LIBRARY: return 14; case T_STATION: return 8; case T_PLAZA: return 10; default: return 4;
                }
            };
            sort(ids, ids + n, [&](int a, int b) {
                int va = M.score2[a] * 10 + typeBonus(a) + (S.owner[a] == 1 ? 15 : 0) - M.dist[M.base[0]][M.bcell[a]];
                int vb = M.score2[b] * 10 + typeBonus(b) + (S.owner[b] == 1 ? 15 : 0) - M.dist[M.base[0]][M.bcell[b]];
                return va > vb;
            });
            static const int needs[3] = {8, 16, 30};
            for (int i = 0; i < min(n, nstrike); i++)
                for (int k = 0; k < 3; k++) {
                    Cand c = base; c.tag = 100 + i * 3 + k;
                    c.P.extraGoal = M.bcell[ids[i]]; c.P.extraGoalValue = 2500; c.P.extraGoalNeed = needs[k];
                    strikes.push_back(c);
                }
        }
        if (devInt("SIM_STRIKE2", 0)) {
            // annihilation strikes on the largest enemy stacks and defensive strikes on our most threatened buildings
            int cells[NC], n = 0;
            for (int c = 0; c < NC; c++) if (S.Wn[1][c] >= 4) cells[n++] = c;
            sort(cells, cells + n, [&](int a, int b) { return S.Wn[1][a] > S.Wn[1][b]; });
            for (int i = 0; i < min(n, 2); i++) {
                Cand c = base; c.tag = 200 + i;
                c.P.extraGoal = cells[i]; c.P.extraGoalValue = 2500; c.P.extraGoalNeed = min(70, (int)S.Wn[1][cells[i]] + 4);
                strikes.push_back(c);
            }
            int bestB = -1, bestT = 0;
            for (int b = 0; b < M.nb; b++) if (S.owner[b] == 0) {
                int c0 = M.bcell[b], thr = 0, mine = 0;
                for (int q = 0; q < NC; q++) { int d = M.dist[c0][q]; if (d <= 3) thr += S.Wn[1][q]; if (d <= 2) mine += S.Wn[0][q]; }
                int deficit = thr - mine;
                if (deficit > bestT) { bestT = deficit; bestB = b; }
            }
            if (bestB >= 0) {
                Cand c = base; c.tag = 300;
                c.P.extraGoal = M.bcell[bestB]; c.P.extraGoalValue = 2500; c.P.extraGoalNeed = min(70, bestT + 4);
                strikes.push_back(c);
            }
        }
        if (devInt("SIM_ORDER", 0) == 0) { for (auto& c : variants) cands.push_back(c); for (auto& c : strikes) cands.push_back(c); }
        else { for (auto& c : strikes) cands.push_back(c); for (auto& c : variants) cands.push_back(c); }
        for (int i = 0; i < nRandom; i++) { Cand c; c.P = randomParams(0.45); c.tag = 10 + i; cands.push_back(c); }
    }

    // update opponent model fits using the previous turn's predictions
    void updateFits(const State& root) {
        if (!havePrev) return;
        for (int m = 0; m < 3; m++) {
            State st = prevRoot;
            step(M, st, prevMyOrders, models[m].lastPred, true);
            double mm = 0;
            for (int c = 0; c < NC; c++) mm += abs(st.F[1][c] - root.F[1][c]) + abs(st.Wn[1][c] - root.Wn[1][c]);
            mm += abs(st.res[1] - root.res[1]);
            models[m].cum = models[m].cum * 0.7 + mm;
        }
        int b = 0;
        for (int m = 1; m < 3; m++) if (models[m].cum < models[b].cum - 1e-9) b = m;
        curModel = b;
    }

    // ---------------- output ----------------
    static void toStrings(const Map& Mm, const Orders& o, vector<string>& out) {
        static const char* KN[2] = {"F", "W"};
        static const char* DN[4] = {"U", "D", "L", "R"};
        for (int i = 0; i < o.nsp; i++) {
            if (o.sp[i].cell >= 0) out.push_back(p::spawn(KN[o.sp[i].kind], o.sp[i].n, o.sp[i].cell % GW, o.sp[i].cell / GW));
            else out.push_back(p::spawn(KN[o.sp[i].kind], o.sp[i].n));
        }
        for (int i = 0; i < o.nmv; i++) if (o.mv[i].dir == D_TELE) {
            auto& m = o.mv[i];
            out.push_back(p::tele(m.cell % GW, m.cell / GW, KN[m.kind], m.n, m.cell2 % GW, m.cell2 / GW));
        }
        if (o.npr > 0) {
            vector<pair<int, int>> co;
            for (int i = 0; i < o.npr; i++) co.push_back({Mm.bcell[o.prio[i]] % GW, Mm.bcell[o.prio[i]] / GW});
            out.push_back(p::priority(co));
        }
        for (int i = 0; i < o.nmv; i++) if (o.mv[i].dir != D_TELE) {
            auto& m = o.mv[i];
            out.push_back(p::move(m.cell % GW, m.cell / GW, KN[m.kind], m.n, DN[m.dir]));
        }
    }

    // ---------------- main decision ----------------
    vector<string> decide(const p::View& v, const p::Init& init) {
        auto t0 = Clock::now();
        if (!ready) initOnce(init);
        State root;
        buildState(v, root, init);

        updateFits(root);
        // shadow opponent models (exact procedures): predict this turn's opponent orders and advance their memories
        for (int m = 0; m < 3; m++) { models[m].psPre = models[m].ps; pol::decide17(M, NR, root, 1, models[m].P, models[m].ps, models[m].lastPred); }
        Model& om = models[curModel];
        const pol::Params oppP = om.P;
        const Orders oppOrders = om.lastPred;
        const pol::Persist poAfter = om.ps;

        exactPlies = devInt("SIM_EK", 1);
        stickBonus = devInt("SIM_STICK10", 20) * 0.1; baseBonus = devInt("SIM_BASEB10", 10) * 0.1; earlyBonus = devInt("SIM_EARLYB10", 0) * 0.1;
        useOldEval = devInt("SIM_OLDEVAL", 0) != 0;
        int H = devInt("SIM_H", 20);
        H = min(H, TOTAL_TURNS - (v.turn - 1));
        double budgetMs = min(devInt("SIM_MS", 55), 1000000);
        if (lastMs > 110.0 && budgetMs < 1000.0) { budgetMs = 12; H = min(H, 10); }   // machine is struggling: play cheap
        int maxCands = devInt("SIM_CANDS", 1000);
        vector<Cand> cands;
        makeCandidates(root, cands, devInt("SIM_RAND", 0));
        // opponent scenarios: the current model, plus (when the fit is poor and hedging is on) two style variants of it
        struct Scn { pol::Params P; Orders first; pol::Persist po; };
        vector<Scn> scn;
        scn.push_back({oppP, oppOrders, poAfter});
        int hedgeMode = devInt("SIM_HEDGE", 0);
        bool unconf = om.cum > devInt("SIM_HEDGE_THR", 25);
        double lambdaMin = 0.0;   // weight of the worst scenario in the final value
        if ((hedgeMode & 1) && unconf) {
            for (int k = 0; k < 2; k++) {
                Scn sc; sc.P = oppP;
                if (k == 0) { sc.P.P[9] = sc.P.P[9] * 3 / 2; sc.P.P[13] = sc.P.P[13] * 14 / 10; sc.P.P[14] = sc.P.P[14] * 14 / 10; }
                else { sc.P.P[11] = sc.P.P[11] * 14 / 10; sc.P.garrisonAdd += 1; }
                sc.po = om.psPre;
                pol::decide17(M, NR, root, 1, sc.P, sc.po, sc.first);
                scn.push_back(sc);
            }
        }
        if ((hedgeMode & 2) && unconf && v.turn >= devInt("SIM_HEDGE_T", 125)) {
            // endgame: also test resistance against an opponent that strikes one of our most valuable buildings
            int ids[MAXB], n = 0;
            for (int b = 0; b < M.nb; b++) if (root.owner[b] == 0) ids[n++] = b;
            sort(ids, ids + n, [&](int a, int b) { return M.score2[a] > M.score2[b]; });
            for (int i = 0; i < min(n, 3); i++) {
                Scn sc; sc.P = oppP;
                sc.P.extraGoal = M.bcell[ids[i]]; sc.P.extraGoalValue = 2500; sc.P.extraGoalNeed = 20;
                sc.po = om.psPre;
                pol::decide17(M, NR, root, 1, sc.P, sc.po, sc.first);
                scn.push_back(sc);
            }
            lambdaMin = 0.5;
        }
        int best = 0;
        double perCand = 0;   // wall-clock cost of one candidate (exact orders + rollout), used to stay inside the budget
        int H1 = devInt("SIM_H1", 0);                 // two-stage evaluation: all candidates to H1, the best few to H
        int keepK = devInt("SIM_KEEP", 4);
        bool twoStage = H1 > 0 && H1 < H;
        int Hs = twoStage ? H1 : H;
        int nsc = (int)scn.size();
        vector<RS> rss((size_t)cands.size() * nsc);
        size_t nEval = 0;
        auto bonusOf = [&](const Cand& c) {
            double bonus = 0;
            if (c.tag == 5) bonus += stickBonus;
            if (c.tag == 0) bonus += baseBonus + earlyBonus * exp(-(double)(v.turn - 1) / 25.0);
            return bonus;
        };
        auto meanValue = [&](size_t i) {
            double sum = 0, mn = 1e18;
            for (int q = 0; q < nsc; q++) { double x = rsValue(rss[i * nsc + q]); sum += x; mn = min(mn, x); }
            return (1.0 - lambdaMin) * sum / nsc + lambdaMin * mn;
        };
        for (size_t i = 0; i < cands.size() && (int)i < maxCands; i++) {
            double now = chrono::duration<double, milli>(Clock::now() - t0).count();
            if (i > 0 && now + 1.5 * perCand > budgetMs) break;
            Cand& c = cands[i];
            c.ps = psMe;
            pol::decide17(M, NR, root, 0, c.P, c.ps, c.orders);
            for (int q = 0; q < nsc; q++) {
                RS& r = rss[i * nsc + q];
                rsStart(r, root, c, scn[q].po);
                rsAdvance(r, c, scn[q].first, scn[q].P, Hs);
            }
            c.value = meanValue(i) + bonusOf(c);
            nEval = i + 1;
            if (i == 0 || c.value > cands[best].value + 1e-9) best = (int)i;
            double after = chrono::duration<double, milli>(Clock::now() - t0).count();
            perCand = max(perCand * 0.7, after - now);
        }
        if (twoStage && nEval > 1) {
            // stage 2: extend the most promising candidates (plus base / previous choice) to the full horizon
            vector<int> order;
            for (size_t i = 0; i < nEval; i++) order.push_back((int)i);
            sort(order.begin(), order.end(), [&](int a, int b) { return cands[a].value > cands[b].value; });
            vector<int> fin;
            for (int i : order) if ((int)fin.size() < keepK) fin.push_back(i);
            for (size_t i = 0; i < nEval; i++) if ((cands[i].tag == 0 || cands[i].tag == 5) && find(fin.begin(), fin.end(), (int)i) == fin.end()) fin.push_back((int)i);
            best = -1;
            for (int i : fin) {
                double now = chrono::duration<double, milli>(Clock::now() - t0).count();
                if (best >= 0 && now + 1.2 * perCand * 0.6 > budgetMs * 1.15) break;
                Cand& c = cands[i];
                for (int q = 0; q < nsc; q++) rsAdvance(rss[(size_t)i * nsc + q], c, scn[q].first, scn[q].P, H);
                c.value = meanValue((size_t)i) + bonusOf(c);
                if (best < 0 || c.value > cands[best].value + 1e-9) best = i;
            }
            if (best < 0) best = 0;
        }
        psMe = cands[best].ps;
        prevChoice = cands[best].tag;
        prevBestP = cands[best].P; havePrevBest = true;
        prevRoot = root; prevMyOrders = cands[best].orders; havePrev = true;
#ifdef DEV
        if (getenv("SIM_LOG")) {
            fprintf(stderr, "T%d model=%d fit=%.1f/%.1f/%.1f best=%d (", v.turn, curModel, models[0].cum, models[1].cum, models[2].cum, cands[best].tag);
            for (auto& c : cands) fprintf(stderr, " %d:%.0f", c.tag, c.value);
            fprintf(stderr, " )\n");
        }
#endif
        vector<string> out;
        toStrings(M, cands[best].orders, out);
        lastMs = chrono::duration<double, milli>(Clock::now() - t0).count();
        return out;
    }
};

Bot bot;
}  // namespace

vector<string> decide(const p::View& view, const p::Init& init) {
    try { return bot.decide(view, init); }
    catch (...) { return {}; }   // never crash: an empty answer only costs one turn
}
int main() { ios::sync_with_stdio(false); cin.tie(nullptr); return p::run(decide); }

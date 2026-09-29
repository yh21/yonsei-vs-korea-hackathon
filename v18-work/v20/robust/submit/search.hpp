// Simulation-backed search on top of the base policy: candidate plan modifications are scored by
// short rollouts (both sides follow the base policy after the first turn).
#pragma once
#include "sim.hpp"
#include "policy.hpp"
#include <chrono>
#include <ctime>

namespace srch {
using namespace sim;
using pol::prm;

struct EvalParams {
    double pts = 20;      // res-equivalent per score point
    double engW = 100, hallW = 60, hospW = 8, libW = 4, staW = 4;
    double wVal = 3, fVal = 6, resVal = 0.6, expoW = 0;
};
inline EvalParams EP;

// next-turn exposure of team t's flags: flags sitting where the other side can bring more warriors
inline double exposure(const GS& s, int t) {
    int o = 1 - t; double e = 0;
    static thread_local int thr[MAXC];
    bool any = false;
    for (int c = 0; c < G.N; c++) if (s.t[t].F[c]) { any = true; break; }
    if (!any) return 0;
    for (int c = 0; c < G.N; c++) thr[c] = 0;
    for (int q = 0; q < G.N; q++) if (s.t[o].W[q]) for (int k = 0; k < G.nln[q]; k++) thr[G.nl[q][k]] += s.t[o].W[q];
    for (int c = 0; c < G.N; c++) if (s.t[t].F[c] && thr[c] > s.t[t].W[c]) e += s.t[t].F[c];
    return e;
}

// value of the state from team `me` perspective (res-equivalent units)
inline double evalState(const GS& s, int me) {
    int op = 1 - me; double v = 0;
    if (s.turn >= 161) { // terminal (after turn 160): score decides
        int d = score10(s, me) - score10(s, op);
        int uv = 0; for (int c = 0; c < G.N; c++) uv += 5 * (s.t[me].F[c] - s.t[op].F[c]) + 3 * (s.t[me].W[c] - s.t[op].W[c]) + 2 * (s.t[me].S[c] - s.t[op].S[c]);
        return 100000.0 * (d > 0) - 100000.0 * (d < 0) + 50.0 * d + 0.01 * uv;
    }
    double remFrac = std::min(1.0, std::max(0.0, (160 - s.turn) / 60.0));
    for (int b = 0; b < G.nB; b++) {
        int ow = s.own[b]; if (!ow) continue;
        double w = EP.pts * sc10[b] / 10.0;
        switch (G.btype[b]) {
            case ENG: w += EP.engW * remFrac; break;
            case HALL: w += EP.hallW * remFrac; break;
            case HOSPITAL: w += EP.hospW * remFrac; break;
            case LIBRARY: w += EP.libW * remFrac; break;
            case STATION: w += EP.staW * remFrac; break;
            default: break;
        }
        v += (ow == me + 1) ? w : -w;
    }
    int wm = 0, fm = 0, wo = 0, fo = 0;
    for (int c = 0; c < G.N; c++) { wm += s.t[me].W[c]; fm += s.t[me].F[c]; wo += s.t[op].W[c]; fo += s.t[op].F[c]; }
    int wc = std::max(2, 3 - engCount(s, me)), wco = std::max(2, 3 - engCount(s, op));
    v += wm * wc - wo * wco + (fm - fo) * EP.fVal + (s.t[me].res - s.t[op].res) * EP.resVal;
    return v;
}

struct Searcher {
    pol::Doctrine dm;
    static constexpr int NM = 5;
    pol::Doctrine mdl[NM];      // opponent models: 0 = v17-like, 1 = v16-like, 2 = v15-like, 3 = tempo-like
    int chosen = 0; bool modelsInit = false;
    GS predK[NM]; double accEmaK[NM] = {1.0, 0.99, 0.98, 0.97, 0.96};
    void initModels() {
        if (modelsInit) return; modelsInit = true;
        mdl[1].cfg.riskMul = 8; mdl[1].cfg.regMul = 2; mdl[1].cfg.huntValue = 1100; mdl[1].cfg.defValue = 1600;
        mdl[2] = mdl[1]; mdl[2].cfg.dF0 = 6; mdl[2].cfg.dF1 = 5; mdl[2].cfg.dF2 = 4; mdl[2].cfg.dF3 = 3; mdl[2].cfg.adaptF = 0;
        mdl[3] = mdl[0]; mdl[3].cfg.tempo = 1;
        mdl[4] = mdl[0]; mdl[4].cfg.tempo = 2;
    }
    int H = 2;
    double wStay = 0.0;
    double margin = 3.0;
    int maxEvals = 150, maxCost = 340;
    int evals = 0, cost = 0;
    bool budgetLeft() const { return evals < maxEvals && cost < maxCost && !timeUp(); }
    double expoLow = 0, expoHigh = 0, trust = 1.0, expoNow = 0;
    double budgetMs = 50;
    double prevWall = 0, prevCpu = 0; clock_t cpu0 = 0; // load detection: wall time far above cpu time means we are being descheduled
    bool havePred = false; double accW = 1.0, accF = 1.0, accEma = 1.0, lastAcc = 1.0;
    void updateAccuracy(const GS& cur, int me) {
        int op = 1 - me;
        if (!havePred) return;
        for (int k = 0; k < NM; k++) {
            int mw = 0, tw = 0, mf = 0, tf = 0;
            for (int c = 0; c < G.N; c++) { mw += std::min<int>(predK[k].t[op].W[c], cur.t[op].W[c]); tw += cur.t[op].W[c]; mf += std::min<int>(predK[k].t[op].F[c], cur.t[op].F[c]); tf += cur.t[op].F[c]; }
            double a = (tw + tf) ? double(mw + mf) / (tw + tf) : 1.0;
            accEmaK[k] = 0.6 * accEmaK[k] + 0.4 * a;
            if (k == chosen) { lastAcc = a; accW = tw ? double(mw) / tw : 1.0; accF = tf ? double(mf) / tf : 1.0; }
        }
        int bestK = chosen;
        for (int k = 0; k < NM; k++) if (accEmaK[k] > accEmaK[bestK] + 0.02) bestK = k;
        chosen = bestK;
        accEma = accEmaK[chosen];
    }
    std::chrono::steady_clock::time_point t0;
    bool timeUp() const { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() > budgetMs; }

    // rollout: first turn uses given plans, subsequent turns both use base policy
    double rollout(const GS& s0, int me, const Plan& mine, const Plan& enemy, int horizon) {
        GS s = s0; int op = 1 - me; evals++; cost += horizon;
        const Plan* pa = me == 0 ? &mine : &enemy; const Plan* pb = me == 0 ? &enemy : &mine;
        step(s, *pa, *pb);
        double pen = expoNow != 0 ? expoNow * (exposure(s, me) - 0.5 * exposure(s, op)) : 0;
        if (horizon > 1) {
            pol::Doctrine a = dm, b = mdl[chosen]; a.cacheMode = 2; b.cacheMode = 2; static thread_local Plan p0, p1;
            for (int h = 1; h < horizon; h++) {
                a.plan(s, me, p0); b.plan(s, op, p1);
                if (me == 0) step(s, p0, p1); else step(s, p1, p0);
            }
        }
        return evalState(s, me) - pen;
    }

    // adversarial enemy reply to our plan: each enemy warrior stack (largest first) may switch to the
    // neighbouring cell where it destroys the most of our flags / holds; other enemy orders stay as predicted.
    void adversarialReply(const GS& s0, const Plan& mine, const Plan& e0, int me, Plan& out) const {
        int op = 1 - me;
        static thread_local int myW[MAXC], myF[MAXC], arr[MAXC];
        for (int c = 0; c < G.N; c++) { myW[c] = s0.t[me].W[c]; myF[c] = s0.t[me].F[c]; arr[c] = 0; }
        for (int i = 0; i < mine.nsp; i++) { const Spawn& sp = mine.sp[i]; if (sp.kind == 1) myW[sp.cell] += sp.n; else if (sp.kind == 0) myF[sp.cell] += sp.n; }
        for (int i = 0; i < mine.nmv; i++) { const Mv& mv = mine.mv[i]; int d = G.nb[mv.src][mv.dir]; if (d < 0) continue;
            if (mv.kind == 1) { int n = std::min<int>(mv.n, myW[mv.src]); myW[mv.src] -= n; myW[d] += n; }
            else if (mv.kind == 0) { int n = std::min<int>(mv.n, myF[mv.src]); myF[mv.src] -= n; myF[d] += n; } }
        out = e0;
        int order[MAXC], no = 0;
        for (int c = 0; c < G.N; c++) if (s0.t[op].W[c] > 0) order[no++] = c;
        std::sort(order, order + no, [&](int a, int b) { return s0.t[op].W[a] > s0.t[op].W[b]; });
        for (int oi = 0; oi < no; oi++) {
            int c = order[oi]; int n = s0.t[op].W[c];
            int bestD = -1; double bestPay = 0;
            for (int k = 0; k < G.nln[c]; k++) {
                int d = G.nl[c][k];
                int tot = arr[d] + n;
                if (tot <= myW[d]) continue;                 // cannot win there
                double pay = 5.0 * myF[d];
                int b = G.bAt[d]; if (b >= 0 && s0.own[b] == me + 1 && myF[d] > 0) pay += 2.0 * sc10[b];
                if (pay > bestPay) { bestPay = pay; bestD = d; }
            }
            if (bestD < 0) { arr[c] += n; continue; }        // keep predicted behaviour (approximately: stays)
            // replace all predicted warrior orders of this stack by one order to bestD
            int k2 = 0; for (int i = 0; i < out.nmv; i++) if (!(out.mv[i].src == c && out.mv[i].kind == 1)) out.mv[k2++] = out.mv[i]; out.nmv = k2;
            if (bestD != c) out.addMove(c, 1, pol::Doctrine::dirBetween(c, bestD), n);
            arr[bestD] += n;
        }
    }

    // detach `take` warriors from the stack at q (first from those that stay, then from planned moves) and send them one step in dir
    static bool detachAndSend(Plan& p, const GS& s, int me, int q, int take, int dir) {
        int have = s.t[me].W[q] + spawnedAt(p, 1, q);
        int moved = 0; for (int i = 0; i < p.nmv; i++) if (p.mv[i].src == q && p.mv[i].kind == 1) moved += p.mv[i].n;
        int stay = std::max(0, have - moved);
        if (take > have) take = have;
        if (take <= 0) return false;
        int fromMoves = std::max(0, take - stay);
        for (int i = 0; i < p.nmv && fromMoves > 0; i++) if (p.mv[i].src == q && p.mv[i].kind == 1) { int r = std::min<int>(fromMoves, p.mv[i].n); p.mv[i].n -= r; fromMoves -= r; }
        int k = 0; for (int i = 0; i < p.nmv; i++) if (p.mv[i].n > 0) p.mv[k++] = p.mv[i]; p.nmv = k;
        p.addMove(q, 1, dir, take);
        return true;
    }

    static void dropMoves(Plan& p, int src, int kind) {
        int k = 0; for (int i = 0; i < p.nmv; i++) if (!(p.mv[i].src == src && p.mv[i].kind == kind)) p.mv[k++] = p.mv[i]; p.nmv = k;
    }
    static int spawnedAt(const Plan& p, int kind, int c) { int n = 0; for (int i = 0; i < p.nsp; i++) if (p.sp[i].kind == kind && p.sp[i].cell == c) n += p.sp[i].n; return n; }

    Plan search(const GS& s, int me) {
        Plan r = searchImpl(s, me);
        prevWall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        prevCpu = (clock() - cpu0) * 1000.0 / CLOCKS_PER_SEC;
        return r;
    }

    Plan searchImpl(const GS& s, int me) {
        t0 = std::chrono::steady_clock::now(); cpu0 = clock();
        int op = 1 - me;
        updateAccuracy(s, me);
        budgetMs = prm(44, 50);
        if (budgetMs <= 200 && prevWall > 60 && prevWall > 3 * prevCpu) budgetMs = prevWall > 150 ? 0.0 : std::min(budgetMs, 18.0); // overloaded machine: shrink the search
        H = prm(40, 2); { int endT = prm(48, 150); int rem = 161 - s.turn; if (s.turn >= endT) H = std::max(1, rem); else H = std::min(H, std::max(1, rem)); }
        wStay = prm(41, 0) / 100.0; margin = prm(42, 3); maxEvals = prm(43, 150); maxCost = prm(54, 340); cost = 0;
        expoLow = prm(45, 0) / 10.0; expoHigh = prm(49, 0) / 10.0;
        trust = std::clamp((accEma - prm(70, 60) / 100.0) / (prm(71, 95) / 100.0 - prm(70, 60) / 100.0), 0.0, 1.0);
        expoNow = expoLow * (1 - trust) + expoHigh * trust;
        margin += prm(72, 0) * (1 - trust);
        dm.cfg.tempo = prm(110, 1); dm.cfg.rally = prm(60, 0); dm.cfg.holdVal = prm(61, 1); dm.cfg.engDenial = prm(62, 0); dm.cfg.garrisonN = prm(63, 0); dm.cfg.dispatch = prm(64, 5); dm.cfg.dispatchBase = prm(65, 500); dm.cfg.dispatchMinW = prm(66, 10); dm.cfg.hospBonus = prm(100, 145); dm.cfg.dispatchScale = prm(94, 50); dm.cfg.dispatchMax = prm(95, 6); dm.cfg.huntValue = prm(67, 660); dm.cfg.huntNeed = prm(68, 3); dm.cfg.dF0 = prm(0, 7); dm.cfg.dF1 = prm(1, 5); dm.cfg.dF2 = prm(2, 4); dm.cfg.dF3 = prm(3, 3);
        EP.pts = prm(80, 20); EP.engW = prm(81, 100); EP.hallW = prm(82, 60); EP.hospW = prm(83, 8); EP.libW = prm(84, 4); EP.staW = prm(85, 4); EP.fVal = prm(86, 6) ; EP.resVal = prm(87, 6) / 10.0;
        int useJoint = prm(46, 1), useSpawn = prm(47, 0);
        initModels();
        Plan base, enemy; static Plan enemyK[NM];
        for (int k = 0; k < NM; k++) if (k != chosen && (k < 3 || prm(111, 1))) { mdl[k].cacheMode = 0; mdl[k].plan(s, op, enemyK[k]); }
        dm.cacheMode = 1; mdl[chosen].cacheMode = 1;
        dm.plan(s, me, base); mdl[chosen].plan(s, op, enemy);
        dm.cacheMode = 0; mdl[chosen].cacheMode = 0;
        enemyK[chosen] = enemy;
        if (maxEvals <= 0) return base;
        Plan enemyStay = enemy; enemyStay.nmv = 0; enemyStay.teleSrc = -1;
        double wHedge = prm(90, 0) / 100.0 * (1 - trust);
        auto valueOf = [&](const Plan& p) {
            double v = (1 - wStay) * rollout(s, me, p, enemy, H);
            if (wStay > 0) v += wStay * rollout(s, me, p, enemyStay, H);
            if (wHedge > 0) { static thread_local Plan adv; adversarialReply(s, p, enemy, me, adv); v = (1 - wHedge) * v + wHedge * rollout(s, me, p, adv, H); }
            return v;
        };
        static thread_local bool hot[MAXC];
        for (int c = 0; c < G.N; c++) hot[c] = false;
        for (int q = 0; q < G.N; q++) if (s.t[op].W[q] | s.t[op].F[q] | s.t[op].S[q]) for (int i = 0; i < G.nball3[q]; i++) hot[G.ball3[q][i]] = true;
        Plan best = base; double bestV = valueOf(base);
        evals = 0;
        auto tryPlan = [&](const Plan& cand) {
            double v = valueOf(cand);
            if (v > bestV + margin) { bestV = v; best = cand; }
        };
        // ---- macro candidates: reinforce a threatened building (enemy force close, my local force too small);
        //      scored over a longer rollout (arrival time + 2) against the base plan at the same horizon
        if (prm(93, 1) && budgetLeft()) {
            const int mnear = prm(96, 5), mrad = prm(97, 3), msrc = prm(98, 6);
            for (int b = 0; b < G.nB && budgetLeft(); b++) {
                if (s.own[b] != me + 1) continue;
                int z = G.bcell[b];
                int ew3 = 0, ef4 = 0, near = 99;
                for (int q = 0; q < G.N; q++) { int d = G.dist[z][q]; if (d <= mrad) ew3 += s.t[op].W[q]; if (d <= mrad + 1) ef4 += s.t[op].F[q]; if ((s.t[op].W[q] | s.t[op].F[q]) && d < near) near = d; }
                if (near > mnear || (ef4 == 0 && ew3 == 0)) continue;
                int need = ew3 + 1;
                int local = 0; for (int k = 0; k < G.nln[z]; k++) local += s.t[me].W[G.nl[z][k]] + spawnedAt(best, 1, G.nl[z][k]);
                if (local >= need) continue;
                // sources: warrior stacks within 6 steps, nearest first
                int cellsBy[MAXC], nc = 0;
                for (int q = 0; q < G.N; q++) if (G.dist[q][z] <= msrc && G.dist[q][z] >= 2 && s.t[me].W[q] + spawnedAt(best, 1, q) > 0) cellsBy[nc++] = q;
                std::sort(cellsBy, cellsBy + nc, [&](int a, int c2) { return G.dist[a][z] < G.dist[c2][z]; });
                Plan cand = best; int got = local; int maxD = 0; bool changed = false;
                for (int i = 0; i < nc && got < need; i++) {
                    int q = cellsBy[i]; int have = s.t[me].W[q] + spawnedAt(best, 1, q);
                    int take = std::min(have, need - got);
                    int bd = -1, bestDist = G.dist[q][z];
                    for (int k = 0; k < 4; k++) { int c2 = G.nb[q][k]; if (c2 >= 0 && G.dist[c2][z] < bestDist) { bestDist = G.dist[c2][z]; bd = k; } }
                    if (bd < 0) continue;
                    if (detachAndSend(cand, s, me, q, take, bd)) { got += take; maxD = std::max<int>(maxD, G.dist[q][z]); changed = true; }
                }
                if (!changed) continue;
                int Hb = std::clamp(maxD + 2, 3, 6); Hb = std::min(Hb, std::max(1, 161 - s.turn));
                int saveH = H; H = Hb;
                double vb = valueOf(best), vc = valueOf(cand);
                H = saveH;
                if (vc > vb + margin) { best = cand; bestV = valueOf(best); }
            }
        }
        // ---- macro intercept: chase enemy raid parties (flags / warriors) that are inside my half
        if (prm(101, 0) && budgetLeft()) {
            int myBase = G.base[me], enBase = G.base[op];
            int targets[16], nt = 0;
            for (int c = 0; c < G.N && nt < 16; c++) if ((s.t[op].F[c] > 0 || s.t[op].W[c] >= 2) && G.dist[c][myBase] < G.dist[c][enBase]) targets[nt++] = c;
            for (int ti = 0; ti < nt && budgetLeft(); ti++) {
                int z = targets[ti];
                int ew = 0; for (int k = 0; k < G.nln[z]; k++) ew += s.t[op].W[G.nl[z][k]];
                int need = ew + 1;
                int cellsBy[MAXC], nc = 0;
                for (int q = 0; q < G.N; q++) if (G.dist[q][z] <= 6 && G.dist[q][z] >= 2 && s.t[me].W[q] + spawnedAt(best, 1, q) > 0) cellsBy[nc++] = q;
                if (!nc) continue;
                std::sort(cellsBy, cellsBy + nc, [&](int a, int c2) { return G.dist[a][z] < G.dist[c2][z]; });
                Plan cand = best; int got = 0, maxD = 0; bool changed = false;
                for (int i = 0; i < nc && got < need; i++) {
                    int q = cellsBy[i]; int have = s.t[me].W[q] + spawnedAt(best, 1, q);
                    int take = std::min(have, need - got);
                    int bd = -1, bestDist = G.dist[q][z];
                    for (int k = 0; k < 4; k++) { int c2 = G.nb[q][k]; if (c2 >= 0 && G.dist[c2][z] < bestDist) { bestDist = G.dist[c2][z]; bd = k; } }
                    if (bd < 0) continue;
                    if (detachAndSend(cand, s, me, q, take, bd)) { got += take; maxD = std::max<int>(maxD, G.dist[q][z]); changed = true; }
                }
                if (!changed || got < need) continue;
                int Hb = std::clamp(maxD + 2, 3, 6); Hb = std::min(Hb, std::max(1, 161 - s.turn));
                int saveH = H; H = Hb;
                double vb = valueOf(best), vc = valueOf(cand);
                H = saveH;
                if (vc > vb + margin) { best = cand; bestV = valueOf(best); }
            }
        }
        // ---- macro strike: send a squad at an enemy-held valuable building whose local defence is weak
        if (prm(99, 0) && budgetLeft()) {
            for (int b = 0; b < G.nB && budgetLeft(); b++) {
                if (s.own[b] != op + 1) continue;
                int ty = G.btype[b]; if (ty != ENG && ty != HALL && ty != HOSPITAL && ty != DEPOT && ty != PLAZA) continue;
                int z = G.bcell[b];
                int ew3 = 0; for (int q = 0; q < G.N; q++) if (G.dist[z][q] <= 3) ew3 += s.t[op].W[q];
                int need = ew3 + 2;
                int cellsBy[MAXC], nc = 0;
                for (int q = 0; q < G.N; q++) if (G.dist[q][z] <= 8 && G.dist[q][z] >= 2 && s.t[me].W[q] + spawnedAt(best, 1, q) > 0) cellsBy[nc++] = q;
                std::sort(cellsBy, cellsBy + nc, [&](int a, int c2) { return G.dist[a][z] < G.dist[c2][z]; });
                Plan cand = best; int got = 0; int maxD = 0; bool changed = false;
                for (int i = 0; i < nc && got < need; i++) {
                    int q = cellsBy[i]; int have = s.t[me].W[q] + spawnedAt(best, 1, q);
                    int take = std::min(have, need - got);
                    int bd = -1, bestDist = G.dist[q][z];
                    for (int k = 0; k < 4; k++) { int c2 = G.nb[q][k]; if (c2 >= 0 && G.dist[c2][z] < bestDist) { bestDist = G.dist[c2][z]; bd = k; } }
                    if (bd < 0) continue;
                    if (detachAndSend(cand, s, me, q, take, bd)) { got += take; maxD = std::max<int>(maxD, G.dist[q][z]); changed = true; }
                }
                if (!changed || got < need) continue;
                int Hb = std::clamp(maxD + 3, 4, 8); Hb = std::min(Hb, std::max(1, 161 - s.turn));
                int saveH = H; H = Hb;
                double vb = valueOf(best), vc = valueOf(cand);
                H = saveH;
                if (vc > vb + margin) { best = cand; bestV = valueOf(best); }
            }
        }
        // ---- joint candidates: converge on a target cell
        if (useJoint) {
            int zs[64]; int nz = 0;
            static thread_local int enThr[MAXC];
            for (int c = 0; c < G.N; c++) enThr[c] = 0;
            for (int q = 0; q < G.N; q++) if (s.t[op].W[q]) for (int k = 0; k < G.nln[q]; k++) enThr[G.nl[q][k]] += s.t[op].W[q];
            for (int c = 0; c < G.N && nz < 64; c++) {
                bool tgt = false;
                if (s.t[op].F[c]) tgt = true;                       // enemy flags
                else if (s.t[op].W[c] && hot[c]) tgt = true;       // enemy stacks
                else { int b = G.bAt[c]; if (b >= 0 && (s.t[me].F[c] || s.t[op].F[c] || enThr[c] > 0) ) tgt = true; }
                if (tgt) zs[nz++] = c;
            }
            for (int zi = 0; zi < nz && budgetLeft(); zi++) {
                int z = zs[zi];
                for (int variant = 0; variant < 2 && budgetLeft(); variant++) {
                    Plan cand = best;
                    // gather W stacks within distance 1 of z
                    int srcs[5], ns = 0;
                    for (int k = 0; k < G.nln[z]; k++) { int q = G.nl[z][k]; int have = s.t[me].W[q] + spawnedAt(cand, 1, q); if (have > 0) srcs[ns++] = q; }
                    if (ns == 0) continue;
                    int need = variant == 0 ? 1 << 20 : enThr[z] + 1;
                    // biggest stacks first
                    std::sort(srcs, srcs + ns, [&](int a, int b) { return s.t[me].W[a] + spawnedAt(cand, 1, a) > s.t[me].W[b] + spawnedAt(cand, 1, b); });
                    int got = 0; bool changed = false;
                    for (int i = 0; i < ns && got < need; i++) {
                        int q = srcs[i]; int have = s.t[me].W[q] + spawnedAt(cand, 1, q);
                        int take = variant == 0 ? have : std::min(have, need - got);
                        dropMoves(cand, q, 1);
                        if (q != z) { int d = pol::Doctrine::dirBetween(q, z); cand.addMove(q, 1, d, take); }
                        got += have; changed = true;
                    }
                    if (changed) tryPlan(cand);
                }
            }
        }
        // ---- regroup: every other warrior stack steps toward the main body
        if (prm(53, 0) && budgetLeft()) {
            int mainC = -1, mainN = 0;
            for (int c = 0; c < G.N; c++) { int n = s.t[me].W[c] + spawnedAt(best, 1, c); if (n > mainN) { mainN = n; mainC = c; } }
            if (mainC >= 0) {
                for (int variantHot = 0; variantHot < 2 && budgetLeft(); variantHot++) {
                    Plan cand = best; bool changed = false;
                    for (int c = 0; c < G.N; c++) {
                        int have = s.t[me].W[c] + spawnedAt(best, 1, c);
                        if (have <= 0 || c == mainC) continue;
                        if (variantHot == 1 && hot[c]) continue; // second variant: only stacks away from the enemy regroup
                        int dd = G.dist[c][mainC]; if (dd >= 255 || dd > 8) continue;
                        int bestD = -1, bestDist = dd;
                        for (int k = 0; k < 4; k++) { int q = G.nb[c][k]; if (q >= 0 && G.dist[q][mainC] < bestDist) { bestDist = G.dist[q][mainC]; bestD = k; } }
                        if (bestD < 0) continue;
                        dropMoves(cand, c, 1); cand.addMove(c, 1, bestD, have); changed = true;
                    }
                    if (changed) tryPlan(cand);
                }
            }
        }
        // ---- single-stack modifications (most tactically relevant stacks first)
        {
            struct Item { int kind, c, pri; };
            Item items[2 * MAXC]; int ni = 0;
            for (int kind = 0; kind <= 1; kind++) for (int c = 0; c < G.N; c++) {
                int have = (kind == 0 ? s.t[me].F[c] : s.t[me].W[c]) + spawnedAt(best, kind, c);
                if (have <= 0) continue;
                if (kind == 1 && !hot[c]) continue;
                int pri = 99;
                if (kind == 0) { for (int b = 0; b < G.nB; b++) if (s.own[b] != me + 1) pri = std::min<int>(pri, G.dist[c][G.bcell[b]]); pri = pri * 2; }
                else { for (int q = 0; q < G.N; q++) if (s.t[op].W[q] | s.t[op].F[q]) pri = std::min<int>(pri, G.dist[c][q]); pri = pri * 2 + 1; }
                items[ni++] = {kind, c, pri};
            }
            std::stable_sort(items, items + ni, [](const Item& a, const Item& b) { return a.pri < b.pri; });
            for (int ii = 0; ii < ni && budgetLeft(); ii++) {
                int kind = items[ii].kind, c = items[ii].c;
                int have = (kind == 0 ? s.t[me].F[c] : s.t[me].W[c]) + spawnedAt(best, kind, c);
                if (have <= 0) continue;
                Plan bestLocal = best; double bestLocalV = bestV;
                for (int opt = -1; opt < 4; opt++) {
                    if (opt >= 0 && G.nb[c][opt] < 0) continue;
                    if (!budgetLeft()) break;
                    Plan cand = best; dropMoves(cand, c, kind);
                    if (opt >= 0) cand.addMove(c, kind, opt, have);
                    double v = valueOf(cand);
                    if (v > bestLocalV + margin) { bestLocalV = v; bestLocal = cand; }
                }
                best = bestLocal; bestV = bestLocalV;
            }
        }
        // ---- spawn variants
        if (useSpawn && budgetLeft()) {
            // (a) no spawn at all, (b) convert planned F spawn into W, (c) all W spawns removed (bank)
            Plan c1 = best; c1.nsp = 0; tryPlan(c1);
            bool hasF = false; for (int i = 0; i < best.nsp; i++) if (best.sp[i].kind == 0) hasF = true;
            if (hasF && budgetLeft()) {
                Plan c2 = best; int k = 0; for (int i = 0; i < c2.nsp; i++) if (c2.sp[i].kind != 0) c2.sp[k++] = c2.sp[i]; c2.nsp = k;
                // move any W spawn to base site with the leftover resource
                int wc = std::max(2, 3 - engCount(s, me)); int spent = 0;
                for (int i = 0; i < best.nsp; i++) if (best.sp[i].kind == 0) spent += 5 * best.sp[i].n;
                int site = G.base[me]; for (int i = 0; i < best.nsp; i++) if (best.sp[i].kind == 1) site = best.sp[i].cell;
                c2.addSpawn(1, site, spent / wc); tryPlan(c2);
            }
        }
        for (int k = 0; k < NM; k++) { predK[k] = s; const Plan* pa = me == 0 ? &best : &enemyK[k]; const Plan* pb = me == 0 ? &enemyK[k] : &best; step(predK[k], *pa, *pb); }
        havePred = true;
        return best;
    }
};
} // namespace srch

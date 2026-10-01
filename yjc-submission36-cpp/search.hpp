// Simulation-backed search on top of the base policy: candidate plan modifications are scored by
// short rollouts (both sides follow the base policy after the first turn).
#pragma once
#include "sim.hpp"
#include "policy.hpp"
#include "ordsort.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <ctime>
#include <cstdlib>

namespace srch {
using namespace sim;
using pol::prm;

struct EvalParams {
    double pts = 20;      // res-equivalent per score point
    double engW = 100, hallW = 60, hospW = 8, libW = 4, staW = 4;
    double wVal = 3, fVal = 6, resVal = 0.6, expoW = 0;
    int remOff = 0;       // v22: econ (ENG/HALL/hospital/...) value fades to zero remOff turns before the end
    int occTie = 0;       // v22: score tie -> decided by accumulated occupation-turns
    int engMode = 0; double engW2 = 20; // v22: 1 = ENG value counts only 'has >= 1 ENG' (2nd ENG only points + engW2)
    int udT = 0; double udMin = 0.3; // v22: late-game discount of unit/resource value (0 = off)
};
inline EvalParams EP;

// next-turn exposure of team t's flags: flags sitting where the other side can bring more warriors
inline double exposure(const GS& s, int t) {
    int o = 1 - t; double e = 0;
    static int thr[MAXC];
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
        if (EP.occTie && d == 0) { int oc = s.occ10[me] - s.occ10[op]; if (oc != 0) return oc > 0 ? 90000.0 : -90000.0; }
        int uv = 0; for (int c = 0; c < G.N; c++) uv += 5 * (s.t[me].F[c] - s.t[op].F[c]) + 3 * (s.t[me].W[c] - s.t[op].W[c]) + 2 * (s.t[me].S[c] - s.t[op].S[c]);
        return 100000.0 * (d > 0) - 100000.0 * (d < 0) + 50.0 * d + 0.01 * uv;
    }
    double remFrac = std::min(1.0, std::max(0.0, (160 - EP.remOff - s.turn) / (60.0 - EP.remOff)));
    for (int b = 0; b < G.nB; b++) {
        int ow = s.own[b]; if (!ow) continue;
        double w = EP.pts * sc10[b] / 10.0;
        switch (G.btype[b]) {
            case ENG: w += (EP.engMode ? EP.engW2 : EP.engW) * remFrac; break;
            case HALL: w += EP.hallW * remFrac; break;
            case HOSPITAL: w += EP.hospW * remFrac; break;
            case LIBRARY: w += EP.libW * remFrac; break;
            case STATION: w += EP.staW * remFrac; break;
            default: break;
        }
        v += (ow == me + 1) ? w : -w;
    }
    if (EP.engMode) { int em = engCount(s, me), eo = engCount(s, op); v += EP.engW * remFrac * ((em > 0) - (eo > 0)); }
    int wm = 0, fm = 0, wo = 0, fo = 0;
    for (int c = 0; c < G.N; c++) { wm += s.t[me].W[c]; fm += s.t[me].F[c]; wo += s.t[op].W[c]; fo += s.t[op].F[c]; }
    // Existing warriors keep a fixed value when production costs change.
    double um = 1.0; if (EP.udT > 0 && s.turn >= EP.udT) um = std::max(EP.udMin, (160.0 - s.turn) / (160.0 - EP.udT));
    v += um * ((wm - wo) * 3.0 + (fm - fo) * EP.fVal + (s.t[me].res - s.t[op].res) * EP.resVal);
    return v;
}

struct Searcher {
    pol::Doctrine dm;
    static constexpr int NM = 10; int nm = 7;
    pol::Doctrine mdl[NM];      // opponent models: 0 = v17-like, 1 = v16-like, 2 = v15-like, 3 = tempo(v18)-like, 4 = tempo(snap2)-like, 5 = v19 doctrine (no eng/hall raid features), 6 = own current doctrine
    int chosen = 0; bool modelsInit = false;
    GS predK[NM]; double accEmaK[NM] = {1.0, 0.99, 0.98, 0.97, 0.96, 0.95, 0.94, 0.93, 0.92, 0.91};
    void initModels() {
        if (modelsInit) return; modelsInit = true;
        mdl[1].cfg.riskMul = 8; mdl[1].cfg.regMul = 2; mdl[1].cfg.huntValue = 1100; mdl[1].cfg.defValue = 1600;
        mdl[3] = mdl[0]; mdl[3].cfg.ehBV = 100; mdl[3].cfg.ehN = 2; mdl[3].cfg.ehGoal = 900; mdl[3].cfg.goalFirst = 1;
        mdl[4] = mdl[3]; mdl[4].cfg.ehRaid = 1200;
        // the enemy may be a copy of our own base doctrine: 5 = the v19 doctrine, 6 = the current one
        mdl[6] = mdl[0]; mdl[6].cfg = dm.cfg;
        if (dm.cfg.raid > 0) { mdl[nm] = mdl[6]; mdl[nm].cfg.raid = 0; nm++; } // v24: exact model of the doctrine without the raid pass
        if (prm(150, 0)) { mdl[nm] = mdl[6]; mdl[nm].cfg.raid = 0; mdl[nm].cfg.dF0 = 5; mdl[nm].cfg.dF1 = 4; mdl[nm].cfg.dF2 = 3; mdl[nm].cfg.dF3 = 3; nm++; } // v24: 5-flag opponent (real server winners: F10 = 5)
        if (prm(151, 0)) { mdl[nm] = mdl[6]; mdl[nm].cfg.raid = 0; mdl[nm].cfg.dF0 = 5; mdl[nm].cfg.dF1 = 4; mdl[nm].cfg.dF2 = 3; mdl[nm].cfg.dF3 = 3; mdl[nm].cfg.ehRaid = prm(152, 1200); nm++; }
        mdl[5] = mdl[6]; mdl[5].cfg.ehBV = 0; mdl[5].cfg.ehN = 0; mdl[5].cfg.ehGoal = 0; mdl[5].cfg.goalFirst = 0; mdl[5].cfg.ehRaid = 0; mdl[5].cfg.hospRaid = 0; mdl[5].cfg.openEngMul = 0; mdl[5].cfg.openHallMul = 0; mdl[5].cfg.escValue = 0; mdl[5].cfg.hospMul = 0; mdl[5].cfg.escHosp = 0; mdl[5].cfg.ehScale = 0; mdl[5].cfg.recapBV = 0; mdl[5].cfg.recapGoal = 0; mdl[5].cfg.noBV = 0;
        mdl[2] = mdl[1]; mdl[2].cfg.dF0 = 6; mdl[2].cfg.dF1 = 5; mdl[2].cfg.dF2 = 4; mdl[2].cfg.dF3 = 3; mdl[2].cfg.adaptF = 0;
    }
    unsigned char prevOwn[MAXB] = {}; bool ownInit = false;
    void noteOwn(const GS& s, int me) {
        bool anyEng = false, anyHall = false;
        for (int b = 0; b < G.nB; b++) if (s.own[b] == me + 1) { if (G.btype[b] == ENG) anyEng = true; if (G.btype[b] == HALL) anyHall = true; }
        if (ownInit) for (int b = 0; b < G.nB; b++) if (prevOwn[b] == me + 1 && s.own[b] != me + 1 && (G.btype[b] == ENG || G.btype[b] == HALL)) dm.lostT[b] = s.turn;
        for (int b = 0; b < G.nB; b++) prevOwn[b] = (unsigned char)s.own[b];
        ownInit = true;
        const int noFrom = prm(228, 25);
        if (s.turn < noFrom) { dm.noEngT = 0; dm.noHallT = 0; }
        else { dm.noEngT = anyEng ? 0 : dm.noEngT + 1; dm.noHallT = anyHall ? 0 : dm.noHallT + 1; }
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
        for (int k = 0; k < nm; k++) {
            int mw = 0, tw = 0, mf = 0, tf = 0;
            for (int c = 0; c < G.N; c++) { mw += std::min<int>(predK[k].t[op].W[c], cur.t[op].W[c]); tw += cur.t[op].W[c]; mf += std::min<int>(predK[k].t[op].F[c], cur.t[op].F[c]); tf += cur.t[op].F[c]; }
            double a = (tw + tf) ? double(mw + mf) / (tw + tf) : 1.0;
            accEmaK[k] = 0.6 * accEmaK[k] + 0.4 * a;
            if (k == chosen) { lastAcc = a; accW = tw ? double(mw) / tw : 1.0; accF = tf ? double(mf) / tf : 1.0; }
        }
        int bestK = chosen;
        for (int k = 0; k < nm; k++) if (accEmaK[k] > accEmaK[bestK] + 0.02) bestK = k;
        chosen = bestK;
        accEma = accEmaK[chosen];
    }
    std::chrono::steady_clock::time_point t0;
    double preMs = 0;
    bool cpuMode = false; // TUNE only: budget measured in process CPU time (load independent; ~ server wall / 6.7)
    bool timeUp() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() > budgetMs;
    }

    // rollout: first turn uses given plans, subsequent turns both use base policy.
    // Everything after the first step is a pure function of that state, and different candidate plans often lead to the very same
    // state, so results are memoised per search by exact state equality (evals/cost are still charged as before).
    static constexpr int RMAX = 256;
    static inline GS rmS[RMAX]; static inline uint64_t rmH[RMAX]; static inline double rmV[RMAX]; static inline int8_t rmHor[RMAX], rmMe[RMAX];
    static inline int16_t rmSlot[1024]; static inline int rmN = 0; static inline bool rmOn = true;
    static void rmReset() { rmN = 0; std::memset(rmSlot, 0xFF, sizeof(rmSlot)); }
    static uint64_t hashBytes(const void* p, size_t n) {
        const unsigned char* q = (const unsigned char*)p; uint64_t h = 0x9E3779B97F4A7C15ULL; size_t i = 0;
        for (; i + 8 <= n; i += 8) { uint64_t w; std::memcpy(&w, q + i, 8); h = (h ^ w) * 0xff51afd7ed558ccdULL; h ^= h >> 32; }
        for (; i < n; i++) h = (h ^ q[i]) * 0x100000001b3ULL;
        return h;
    }
    double rollout(const GS& s0, int me, const Plan& mine, const Plan& enemy, int horizon) {
        GS s = s0; int op = 1 - me; evals++; cost += horizon;
        const Plan* pa = me == 0 ? &mine : &enemy; const Plan* pb = me == 0 ? &enemy : &mine;
        step(s, *pa, *pb);
        int slot = -1; unsigned pos = 0; uint64_t hs = 0;
        if (rmOn) {
            hs = hashBytes(&s, sizeof(GS)) ^ ((uint64_t)horizon * 0x9E3779B1ULL + (uint64_t)me);
            pos = (unsigned)(hs & 1023);
            while (rmSlot[pos] != -1) {
                int e = rmSlot[pos];
                if (rmH[e] == hs && rmHor[e] == horizon && rmMe[e] == me && std::memcmp(&rmS[e], &s, sizeof(GS)) == 0) {
                    return rmV[e];
                }
                pos = (pos + 1) & 1023;
            }
            if (rmN < RMAX) { slot = rmN; std::memcpy(&rmS[slot], &s, sizeof(GS)); rmH[slot] = hs; rmHor[slot] = (int8_t)horizon; rmMe[slot] = (int8_t)me; }
        }
        double pen = expoNow != 0 ? expoNow * (exposure(s, me) - 0.5 * exposure(s, op)) : 0;
        if (horizon > 1) {
            pol::Doctrine a = dm, b = mdl[chosen]; a.cacheMode = 2; b.cacheMode = 2; static Plan p0, p1;
            for (int h = 1; h < horizon; h++) {
                static pol::Scan sc; pol::scanState(s, sc);
                a.plan(s, me, p0, &sc); b.plan(s, op, p1, &sc);
                if (me == 0) step(s, p0, p1); else step(s, p1, p0);
            }
        }
        double r = evalState(s, me) - pen;
        if (slot >= 0) { rmV[slot] = r; rmSlot[pos] = (int16_t)slot; rmN++; }
        return r;
    }

    // adversarial enemy reply to our plan: each enemy warrior stack (largest first) may switch to the
    // neighbouring cell where it destroys the most of our flags / holds; other enemy orders stay as predicted.
    void adversarialReply(const GS& s0, const Plan& mine, const Plan& e0, int me, Plan& out) const {
        int op = 1 - me;
        static int myW[MAXC], myF[MAXC], arr[MAXC];
        arrivals(s0,me,mine,1,myW); arrivals(s0,me,mine,0,myF);
        for(int c=0;c<G.N;c++) arr[c]=0;
        out = e0;
        int order[MAXC], no = 0;
        for (int c = 0; c < G.N; c++) if (s0.t[op].W[c] > 0) order[no++] = c;
        ord::sort(order, order + no, [&](int a, int b) { return s0.t[op].W[a] > s0.t[op].W[b]; });
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

    // Best local one-turn counterplay, including capture threats on EMPTY owned buildings.
    // This is a pessimistic tactical check, not a claim to know the opponent's actual orders.
    double replyLoss(const GS& s,int me,const Plan& mine,const Plan& predicted) {
        const int op=1-me; Plan enemy=predicted;
        auto val=[&](const Plan& p){ GS z=s; if(me==0)step(z,mine,p);else step(z,p,mine);return evalState(z,me); };
        double nominal=val(enemy),worst=nominal;
        // Coordinate descent over flag destinations: a small change can neutralize a building.
        for(int c=0;c<G.N;c++) {
            int n=s.t[op].F[c]+spawnedAt(enemy,0,c);if(n<=0)continue;
            Plan bestLocal=enemy;
            for(int k=0;k<G.nln[c];k++) {
                int z=G.nl[c][k];Plan p=enemy;dropMoves(p,c,0);
                if(z!=c)p.addMove(c,0,pol::Doctrine::dirBetween(c,z),n);
                double v=val(p);if(v<worst-0.01){worst=v;bestLocal=p;}
            }
            enemy=bestLocal;
        }
        int mf[MAXC],ef[MAXC];arrivals(s,me,mine,0,mf);arrivals(s,op,enemy,0,ef);
        struct Target{int c,pri;};Target tg[MAXC];int nt=0;
        for(int z=0;z<G.N;z++)if(mf[z] || ef[z]) {
            int b=G.bAt[z];int pr=10*mf[z]+(b>=0?sc10[b]+(G.btype[b]==ENG || G.btype[b]==HALL?40:0):0);
            tg[nt++]={z,pr};
        }
        std::sort(tg,tg+nt,[](const Target&a,const Target&b){return a.pri>b.pri;});
        for(int ti=0;ti<nt && ti<prm(511,12);ti++) {
            int z=tg[ti].c;Plan p=enemy;bool change=false;
            for(int k=0;k<G.nln[z];k++) {
                int c=G.nl[z][k],n=s.t[op].W[c]+spawnedAt(p,1,c);if(n<=0)continue;
                dropMoves(p,c,1);if(c!=z)p.addMove(c,1,pol::Doctrine::dirBetween(c,z),n);change=true;
            }
            if(change){double v=val(p);if(v<worst-0.01){worst=v;enemy=p;}}
        }
        return std::max(0.0,nominal-worst);
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

    // v28 flag safety state: enemy warrior reach at turn start (fsThr) and my warriors arriving under a plan (fsArr)
    int fsThr[MAXC], fsArr[MAXC];
    int fsDef(int q) const { return std::max(0, fsThr[q] - fsArr[q]); }
    void fsThreat(const GS& s, int me) {
        int op = 1 - me; const Team& Et = s.t[op];
        for (int c = 0; c < G.N; c++) fsThr[c] = 0;
        for (int q = 0; q < G.N; q++) if (Et.W[q]) { int w = Et.W[q]; for (int k = 0; k < G.nln[q]; k++) fsThr[G.nl[q][k]] += w; }
        if (prm(325, 1)) {
            bool eng = engCount(s, op) > 0; int sp = Et.res / (eng ? 2 : 3);
            if (sp > 0) {
                static bool seenSp[MAXC]; for (int c = 0; c < G.N; c++) seenSp[c] = false;
                int sites[16]; int ns = 0; sites[ns++] = G.base[op];
                for (int b = 0; b < G.nB; b++) if (s.own[b] == op + 1 && G.btype[b] == HOSPITAL && ns < 16) sites[ns++] = G.bcell[b];
                for (int i = 0; i < ns; i++) for (int k = 0; k < G.nln[sites[i]]; k++) { int z = G.nl[sites[i]][k]; if (!seenSp[z]) { seenSp[z] = true; if (G.pass[z]) fsThr[z] += sp; } }
            }
        }
        for(int b=0;b<G.nB;b++) if(G.btype[b]==STATION && s.own[b]==op+1) {
            int z=G.bcell[b],extra=0;
            for(int a=0;a<G.nB;a++) if(a!=b && G.btype[a]==STATION && s.own[a]==op+1 && G.dist[G.bcell[a]][z]>1)
                extra=std::max(extra,std::min(5,int(Et.W[G.bcell[a]])));
            fsThr[z]+=extra;
        }

    }
    // Exact simultaneous arrivals: incoming units never become available to move again.
    void arrivals(const GS& s, int me, const Plan& p, int kind, int* out) const {
        int left[MAXC] = {}, incoming[MAXC] = {};
        const Team& t = s.t[me];
        for (int c=0;c<G.N;c++) left[c] = kind == 0 ? t.F[c] : kind == 1 ? t.W[c] : t.S[c];
        int res=t.res;
        for (int i=0;i<p.nsp;i++) {
            const Spawn& sp=p.sp[i]; int b=G.bAt[sp.cell];
            if (sp.cell!=G.base[me] && !(b>=0 && G.btype[b]==HOSPITAL && s.own[b]==me+1)) continue;
            int co=sp.kind==0 ? 5 : sp.kind==1 ? wCost(s,me) : 2;
            int n=std::min<int>(sp.n,res/co); res-=co*n;
            if(sp.kind==kind) left[sp.cell]+=n;
        }
        if(p.teleSrc>=0 && p.teleKind==kind) {
            int sb=G.bAt[p.teleSrc],db=G.bAt[p.teleDst];
            if(sb>=0 && db>=0 && sb!=db && G.btype[sb]==STATION && G.btype[db]==STATION && s.own[sb]==me+1 && s.own[db]==me+1) {
                int n=std::min({int(p.teleN),5,left[p.teleSrc]});
                left[p.teleSrc]-=n; incoming[p.teleDst]+=n;
            }
        }
        for (int i=0;i<p.nmv;i++) {
            const Mv& mv=p.mv[i]; if(mv.kind!=kind) continue;
            int d=G.nb[mv.src][mv.dir]; if(d<0) continue;
            int n=std::min<int>(mv.n,left[mv.src]); left[mv.src]-=n; incoming[d]+=n;
        }
        for(int c=0;c<G.N;c++) out[c]=left[c]+incoming[c];
    }
    void fsArrivals(const GS& s, int me, const Plan& p) { arrivals(s,me,p,1,fsArr); }

    // Normalize unsafe flag endpoints before scoring a candidate and again before output.
    // Use exact friendly arrivals and worst-case enemy one-turn reach. If there is no
    // safe neighbour, preserve the candidate so the simulator can compare sacrifices.
    void repairFlags(const GS& s, int me, Plan& p) {
        fsThreat(s, me); fsArrivals(s, me, p);
        struct Grp { int dest, n; };
        static Plan np; np = p; np.nmv = 0;
        bool changed = false;
        for (int i = 0; i < p.nmv; i++) if (p.mv[i].kind != 0) np.mv[np.nmv++] = p.mv[i]; // warrior moves stay as they are
        for (int c = 0; c < G.N; c++) {
            int have = s.t[me].F[c] + spawnedAt(p, 0, c);
            if (have <= 0) continue;
            Grp g[6]; int ng = 0; int moved = 0;
            for (int i = 0; i < p.nmv; i++) if (p.mv[i].kind == 0 && p.mv[i].src == c) { int d = G.nb[c][p.mv[i].dir]; if (d < 0) continue; int n = std::min<int>(p.mv[i].n, have - moved); if (n <= 0) continue; if (ng < 6) { g[ng].dest = d; g[ng].n = n; ng++; moved += n; } }
            if (moved < have && ng < 6) { g[ng].dest = c; g[ng].n = have - moved; ng++; }
            for (int gi = 0; gi < ng; gi++) {
                int dest = g[gi].dest;
                if (fsDef(dest) <= 0) continue;
                int bestCell = -1, bestKey = 1 << 30;
                for (int k = -1; k < 4; k++) {
                    int q = k < 0 ? c : G.nb[c][k]; if (q < 0 || fsDef(q) > 0) continue;
                    int key = (int)G.dist[q][dest] * 4 + (q == c ? 0 : 1);
                    if (key < bestKey) { bestKey = key; bestCell = q; }
                }
                if (bestCell >= 0 && bestCell != dest) { g[gi].dest = bestCell; changed = true; }
            }
            for (int gi = 0; gi < ng; gi++) if (g[gi].dest != c) {
                int dir = pol::Doctrine::dirBetween(c, g[gi].dest);
                bool merged = false;
                for (int i = 0; i < np.nmv; i++) if (np.mv[i].kind == 0 && np.mv[i].src == c && np.mv[i].dir == dir) { np.mv[i].n = (int16_t)(np.mv[i].n + g[gi].n); merged = true; break; }
                if (!merged) np.addMove(c, 0, dir, g[gi].n);
            }
        }
        if (changed) p = np;
    }

    Plan search(const GS& s, int me) {
        Plan r = searchImpl(s, me);
        prevWall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        prevCpu = (double)(clock() - cpu0) * 1000.0 / CLOCKS_PER_SEC;
        return r;
    }

    Plan searchImpl(const GS& s, int me) {
        t0 = std::chrono::steady_clock::now(); cpu0 = clock();
        int op = 1 - me;
        updateAccuracy(s, me);
        noteOwn(s, me);
        budgetMs = prm(44, 50);
        if (!cpuMode && budgetMs <= 200 && prevWall > 60 && prevWall > 3 * prevCpu) budgetMs = prevWall > 150 ? 0.0 : std::min(budgetMs, 18.0); // overloaded machine: shrink the search
        H = prm(40, 2); { int endT = prm(48, 150); int rem = 161 - s.turn; if (s.turn >= endT) H = std::max(1, rem); else H = std::min(H, std::max(1, rem)); }
        wStay = prm(41, 0) / 100.0; margin = prm(42, 3); maxEvals = prm(43, 150); maxCost = prm(54, 340); if (s.turn >= prm(48, 150)) maxCost = prm(239, 340); cost = 0;
        expoLow = prm(45, 0) / 10.0; expoHigh = prm(49, 0) / 10.0;
        trust = std::clamp((accEma - prm(70, 60) / 100.0) / (prm(71, 95) / 100.0 - prm(70, 60) / 100.0), 0.0, 1.0);
        expoNow = expoLow * (1 - trust) + expoHigh * trust;
        margin += prm(72, 0) * (1 - trust);
        dm.cfg.rally = prm(60, 0); dm.cfg.holdVal = prm(61, 1); dm.cfg.engDenial = prm(62, 0); dm.cfg.garrisonN = prm(63, 0); dm.cfg.dispatch = prm(64, 5); dm.cfg.dispatchBase = prm(65, 500); dm.cfg.dispatchMinW = prm(66, 10); dm.cfg.hospBonus = prm(100, 145); dm.cfg.dispatchScale = prm(94, 50); dm.cfg.dispatchMax = prm(95, 6); dm.cfg.huntValue = prm(67, 660); dm.cfg.huntNeed = prm(68, 3); dm.cfg.ehBV = prm(130, 100); dm.cfg.ehN = prm(131, 2); dm.cfg.ehMinW = prm(132, 10); dm.cfg.ehPri = prm(133, 35000); dm.cfg.ehGoal = prm(134, 900); dm.cfg.goalFirst = prm(135, 1); dm.cfg.goalFirstD = prm(136, 5); dm.cfg.ehRaid = prm(137, 0); dm.cfg.hospRaid = prm(138, 0); dm.cfg.dF0 = prm(0, 7); dm.cfg.dF1 = prm(1, 5); dm.cfg.dF2 = prm(2, 4); dm.cfg.dF3 = prm(3, 3); dm.cfg.openEngMul = prm(200, 300); dm.cfg.openHallMul = prm(202, 0); dm.cfg.openT = prm(201, 20); dm.cfg.openEngMaxF = prm(203, 2); dm.cfg.escValue = prm(210, 1500); dm.cfg.escNeed = prm(211, 3); dm.cfg.escT = prm(212, 45); dm.cfg.escD = prm(213, 14); dm.cfg.escEng = prm(214, 2); dm.cfg.ehScale = prm(220, 0); dm.cfg.ehR = prm(221, 4); dm.cfg.ehMax = prm(222, 10); dm.cfg.recapBV = prm(223, 0); dm.cfg.recapGoal = prm(224, 0); dm.cfg.recapT = prm(225, 6); dm.cfg.noBV = prm(226, 0); dm.cfg.noT = prm(227, 3); dm.cfg.holdLateT = prm(240, 125); dm.cfg.ehOffT = prm(241, 999); dm.cfg.teleDef = prm(231, 0); dm.cfg.teleR = prm(232, 3); dm.cfg.raid = prm(140, 40); dm.cfg.raidMaxNeed = prm(141, 10); dm.cfg.raidMinW = prm(142, 20); dm.cfg.raidMaxD = prm(143, 20); dm.cfg.raidExtra = prm(144, 1); dm.cfg.raidTypes = prm(145, 1); dm.cfg.raidMinT = prm(146, 46); dm.cfg.hospMul = prm(300, 200); dm.cfg.hospSide = prm(301, 3); dm.cfg.hospT = prm(302, 20); dm.cfg.hospReact = prm(303, 8); dm.cfg.escHosp = prm(304, 1); dm.cfg.tgtPen = prm(322, 0); dm.cfg.emBV = prm(410, 0); dm.cfg.emMul = prm(411, 100); dm.cfg.emEsc = prm(412, 0); dm.cfg.emF = prm(413, 0); dm.cfg.emMinT = prm(414, 12); dm.cfg.emMaxT = prm(415, 110); dm.cfg.emHall = prm(416, 0); dm.cfg.emGoal = prm(417, 0); dm.cfg.defRE = prm(418, 3); dm.cfg.defEH = prm(419, 0); dm.cfg.defWR = prm(420, 2); dm.cfg.lastN = prm(421, 0); dm.cfg.emRaidT = prm(422, 0);
        EP.pts = prm(80, 20); EP.engW = prm(81, 100); EP.hallW = prm(82, 60); EP.hospW = prm(83, 8); EP.libW = prm(84, 4); EP.staW = prm(85, 4); EP.fVal = prm(86, 6) ; EP.resVal = prm(87, 6) / 10.0; EP.remOff = prm(242, 0); EP.occTie = prm(237, 0); EP.engMode = prm(235, 0); EP.engW2 = prm(236, 20); EP.udT = prm(229, 0); EP.udMin = prm(230, 30) / 100.0;
        int useJoint = prm(46, 1), useSpawn = prm(47, 0);
        initModels();
        Plan base, enemy; static Plan enemyK[NM];
        for (int k = 0; k < nm; k++) if (k != chosen) { mdl[k].cacheMode = 0; mdl[k].plan(s, op, enemyK[k]); }
        dm.cacheMode = 1; mdl[chosen].cacheMode = 1;
        dm.plan(s, me, base); mdl[chosen].plan(s, op, enemy);
        dm.cacheMode = 0; mdl[chosen].cacheMode = 0;
        enemyK[chosen] = enemy;
        if (maxEvals <= 0) return base;
        Plan enemyStay = enemy; enemyStay.nmv = 0; enemyStay.teleSrc = -1;
        double wHedge = prm(90, 0) / 100.0 * (1 - trust);
        if (s.turn >= prm(233, 999)) wHedge = std::max(wHedge, prm(234, 50) / 100.0); // v22: robust (adversarial-reply) scoring in the final turns
        // The rollout value is a pure function of (plan, horizon) within one search; identical candidates recur often, so the value is
        // memoised (evals/cost are still charged exactly as before, so the search follows the same trajectory).
        static uint16_t vmSlot[1024]; static uint64_t vmHash[1024]; static double vmVal[1024]; static uint32_t vmOff[1024], vmLen[1024]; static int vmN; static std::vector<unsigned char> vmKeys;
        std::memset(vmSlot, 0xFF, sizeof(vmSlot)); vmN = 0; vmKeys.clear();
        rmReset();
        auto valueOf = [&](Plan p) {
            if (prm(350, 0) && trust * 100.0 < prm(351, 101)) repairFlags(s, me, p);
            if (wStay == 0 && wHedge == 0) {
                static unsigned char kb[4096]; int kn = 0;
                auto put = [&](const void* q, int n) { std::memcpy(kb + kn, q, (size_t)n); kn += n; };
                int hh = H; put(&hh, 4); put(&p.nsp, 4);
                for (int i = 0; i < p.nsp; i++) { put(&p.sp[i].kind, 1); put(&p.sp[i].cell, 2); put(&p.sp[i].n, 2); }
                put(&p.nmv, 4); for (int i = 0; i < p.nmv; i++) { put(&p.mv[i].src, 2); put(&p.mv[i].kind, 1); put(&p.mv[i].dir, 1); put(&p.mv[i].n, 2); }
                put(&p.teleSrc, 2); if (p.teleSrc >= 0) { put(&p.teleDst, 2); put(&p.teleN, 2); put(&p.teleKind, 1); }
                put(&p.nprio, 4); for (int i = 0; i < p.nprio; i++) put(&p.prio[i], 1);
                uint64_t h = 1469598103934665603ULL; for (int i = 0; i < kn; i++) { h ^= kb[i]; h *= 1099511628211ULL; } h ^= h >> 29;
                unsigned pos = (unsigned)(h & 1023);
                while (vmSlot[pos] != 0xFFFF) {
                    int e = vmSlot[pos];
                    if (vmHash[e] == h && (int)vmLen[e] == kn && std::memcmp(&vmKeys[vmOff[e]], kb, (size_t)kn) == 0) {
                        evals++; cost += H;
                        return vmVal[e];
                    }
                    pos = (pos + 1) & 1023;
                }
                double v = rollout(s, me, p, enemy, H) - (prm(510,35)/100.0) * replyLoss(s,me,p,enemy);
                if (vmN < 900) { vmSlot[pos] = (uint16_t)vmN; vmHash[vmN] = h; vmVal[vmN] = v; vmOff[vmN] = (uint32_t)vmKeys.size(); vmLen[vmN] = (uint32_t)kn; vmKeys.insert(vmKeys.end(), kb, kb + kn); vmN++; }
                return v;
            }
            double v = (1 - wStay) * rollout(s, me, p, enemy, H);
            if (wStay > 0) v += wStay * rollout(s, me, p, enemyStay, H);
            if (wHedge > 0) { static Plan adv; adversarialReply(s, p, enemy, me, adv); v = (1 - wHedge) * v + wHedge * rollout(s, me, p, adv, H); }
            return v;
        };
        static bool hot[MAXC];
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
                ord::sort(cellsBy, cellsBy + nc, [&](int a, int c2) { return G.dist[a][z] < G.dist[c2][z]; });
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
                ord::sort(cellsBy, cellsBy + nc, [&](int a, int c2) { return G.dist[a][z] < G.dist[c2][z]; });
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
                ord::sort(cellsBy, cellsBy + nc, [&](int a, int c2) { return G.dist[a][z] < G.dist[c2][z]; });
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
        // Joint flag-and-escort candidates cross the coordinate-search barrier:
        // moving either component alone can be bad although moving both captures safely.
        if (prm(500,1) && budgetLeft()) {
            fsThreat(s,me);
            struct FC { int src,dst,pri; }; FC candF[5*MAXC]; int ncf=0;
            for(int c=0;c<G.N;c++) if(s.t[me].F[c]+spawnedAt(best,0,c)>0) {
                for(int k=0;k<G.nln[c];k++) {
                    int z=G.nl[c][k], b=G.bAt[z];
                    int d=99; for(int a=0;a<G.nB;a++) if(s.own[a]!=me+1) d=std::min<int>(d,G.dist[z][G.bcell[a]]);
                    if(d>prm(501,1)) continue;
                    int need=fsThr[z] + ((b>=0 && s.t[op].F[z]>0)?1:0);
                    int cap=0;for(int j=0;j<G.nln[z];j++){int q=G.nl[z][j];cap+=s.t[me].W[q]+spawnedAt(best,1,q);}
                    if(cap<need)continue;
                    if(c==z && need==0)continue;
                    int pri=100*d + (b>=0 && s.own[b]==me+1?200:0) - (b>=0?sc10[b]:0);
                    candF[ncf++]={c,z,pri};
                }
            }
            std::stable_sort(candF,candF+ncf,[](const FC&a,const FC&b){return a.pri<b.pri;});
            for(int ci=0;ci<ncf && ci<prm(502,20) && budgetLeft();ci++) {
                int c=candF[ci].src,z=candF[ci].dst,b=G.bAt[z];
                Plan cand=best; int have=s.t[me].F[c]+spawnedAt(cand,0,c);
                // Detach just one flag, keeping unrelated orders unchanged.
                int used=0;for(int i=0;i<cand.nmv;i++)if(cand.mv[i].src==c && cand.mv[i].kind==0)used+=cand.mv[i].n;
                if(have-used<1)for(int i=0;i<cand.nmv;i++)if(cand.mv[i].src==c && cand.mv[i].kind==0 && cand.mv[i].n>0){cand.mv[i].n--;break;}
                int ii=0;for(int i=0;i<cand.nmv;i++)if(cand.mv[i].n>0)cand.mv[ii++]=cand.mv[i];cand.nmv=ii;
                if(c!=z)cand.addMove(c,0,pol::Doctrine::dirBetween(c,z),1);
                int wa[MAXC];arrivals(s,me,cand,1,wa);
                int need=std::max(0,fsThr[z]+((b>=0 && s.t[op].F[z]>0)?1:0)-wa[z]);
                // Prefer sources whose existing plan already keeps them close.
                int sources[5],ns=0;
                for(int k=0;k<G.nln[z];k++){int q=G.nl[z][k];if(s.t[me].W[q]+spawnedAt(cand,1,q)>0)sources[ns++]=q;}
                std::sort(sources,sources+ns,[&](int a,int b){return s.t[me].W[a]+spawnedAt(cand,1,a)>s.t[me].W[b]+spawnedAt(cand,1,b);});
                for(int k=0;k<ns && need>0;k++) {
                    int q=sources[k],av=s.t[me].W[q]+spawnedAt(cand,1,q);
                    if(cand.teleSrc==q && cand.teleKind==1)av-=std::min<int>(cand.teleN,av);
                    int already=0;
                    if(q==z){int moved=0;for(int i=0;i<cand.nmv;i++)if(cand.mv[i].src==q && cand.mv[i].kind==1)moved+=cand.mv[i].n;already=std::max(0,av-moved);}
                    else for(int i=0;i<cand.nmv;i++)if(cand.mv[i].src==q && cand.mv[i].kind==1 && G.nb[q][cand.mv[i].dir]==z)already+=cand.mv[i].n;
                    int take=std::min(need,std::max(0,av-already));if(take<=0)continue;
                    // Remove units from orders not already ending at the destination.
                    int rem=take;
                    if(q!=z){int moved=0;for(int i=0;i<cand.nmv;i++)if(cand.mv[i].src==q && cand.mv[i].kind==1)moved+=cand.mv[i].n;rem-=std::min(rem,std::max(0,av-moved));}
                    for(int i=0;i<cand.nmv && rem>0;i++)if(cand.mv[i].src==q && cand.mv[i].kind==1 && G.nb[q][cand.mv[i].dir]!=z){int n=std::min<int>(rem,cand.mv[i].n);cand.mv[i].n-=n;rem-=n;}
                    take-=rem;if(q!=z)cand.addMove(q,1,pol::Doctrine::dirBetween(q,z),take);need-=take;
                }
                int j=0;for(int i=0;i<cand.nmv;i++)if(cand.mv[i].n>0)cand.mv[j++]=cand.mv[i];cand.nmv=j;
                if(need<=0)tryPlan(cand);
            }
        }
        // ---- joint candidates: converge on a target cell
        if (useJoint) {
            int zs[64]; int nz = 0;
            static int enThr[MAXC];
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
                    ord::sort(srcs, srcs + ns, [&](int a, int b) { return s.t[me].W[a] + spawnedAt(cand, 1, a) > s.t[me].W[b] + spawnedAt(cand, 1, b); });
                    int got = 0; bool changed = false;
                    for (int i = 0; i < ns && got < need; i++) {
                        int q = srcs[i]; int have = s.t[me].W[q] + spawnedAt(cand, 1, q);
                        int take = variant == 0 ? have : std::min(have, need - got);
                        dropMoves(cand, q, 1);
                        if (q != z) { int d = pol::Doctrine::dirBetween(q, z); cand.addMove(q, 1, d, take); }
                        got += take; changed = true;
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
            struct Item { int kind, c, pri, seq; };
            Item items[2 * MAXC]; int ni = 0;
            for (int kind = 0; kind <= 1; kind++) for (int c = 0; c < G.N; c++) {
                int have = (kind == 0 ? s.t[me].F[c] : s.t[me].W[c]) + spawnedAt(best, kind, c);
                if (have <= 0) continue;
                if (kind == 1 && !hot[c]) continue;
                int pri = 99;
                if (kind == 0) { for (int b = 0; b < G.nB; b++) if (s.own[b] != me + 1) pri = std::min<int>(pri, G.dist[c][G.bcell[b]]); pri = pri * 2; }
                else { for (int q = 0; q < G.N; q++) if (s.t[op].W[q] | s.t[op].F[q]) pri = std::min<int>(pri, G.dist[c][q]); pri = pri * 2 + 1; }
                items[ni] = {kind, c, pri, ni}; ni++;
            }
            std::sort(items, items + ni, [](const Item& a, const Item& b) { return a.pri != b.pri ? a.pri < b.pri : a.seq < b.seq; }); // == stable sort by pri (total order)
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
        if (prm(350, 0) && trust * 100.0 < prm(351, 101)) repairFlags(s, me, best); // trust: predicted-vs-actual enemy accuracy (0..1)
        for (int k = 0; k < nm; k++) { predK[k] = s; const Plan* pa = me == 0 ? &best : &enemyK[k]; const Plan* pb = me == 0 ? &enemyK[k] : &best; step(predK[k], *pa, *pb); }
        havePred = true;
        return best;
    }
};
} // namespace srch

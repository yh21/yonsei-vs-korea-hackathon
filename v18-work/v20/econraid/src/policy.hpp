// Base policy (lean port of the v16/v17 heuristics onto the simulator state) + W doctrine hooks.
#pragma once
#include "sim.hpp"
#include <cstdio>
#include <cstdlib>
#include <queue>

namespace pol {
using namespace sim;
constexpr int INF = 1000000000;
#ifdef TUNE
inline int prm(int i, int d) {
    static int c[256]; static bool init[256];
    if (!init[i]) { init[i] = true; char n[8]; snprintf(n, 8, "P%d", i); const char* e = getenv(n); c[i] = e ? atoi(e) : d; }
    return c[i];
}
#else
constexpr int prm(int, int d) { return d; }
#endif

using IA = std::array<int, MAXC>;

struct RouteCache { bool valid = false; int risk[MAXC]; int rc[MAXB][MAXC]; };

struct Cfg {
    int rally = 0;        // leftover warriors rally to the biggest stack instead of overflow-attacking
    int holdVal = 0;      // garrison priority uses econ-aware hold value
    int engDenial = 0;    // bonus for capturing enemy ENG/HALL
    int garrisonN = 0;    // extra garrison count
    int dispatch = 0;     // guarantee a warrior on the top-N owned buildings (by hold value)
    int dispatchBase = 250;
    int dispatchMinW = 10;
    int dispatchScale = 0;  // >0: picket size grows with enemy warriors within 5 steps (need = 1 + ew5*scale/100, capped)
    int dispatchMax = 6;
    int hospBonus = 145;    // feature bonus of hospitals in buildingValue (t < 100)
    int huntValue = 660;  // goal value for enemy flag cells
    int huntNeed = 3;     // extra warriors over local enemy threat needed for an enemy-flag goal
    int dF0 = 7, dF1 = 6, dF2 = 5, dF3 = 4; // desired flag counts (early / many targets left / some / few)
    int ehBV = 0;         // tempo: extra buildingValue for capturing enemy ENG/HALL
    int ehN = 0;          // tempo: own ENG/HALL crit need (0 = off)
    int ehMinW = 10;
    int ehPri = 35000;
    int ehGoal = 0;       // tempo: goal value for own ENG/HALL
    int ehRaid = 0;       // tempo snap2: extra goal value for unowned ENG/HALL
    int hospRaid = 0;     // extra goal value for unowned hospitals
    int goalFirst = 0;    // tempo: goal-first W assignment
    int goalFirstD = 5;
    int riskMul = 13, regMul = 3, defValue = 960, adaptF = 1; // lineage parameters (v17 defaults)
};

struct Doctrine {
    Cfg cfg;
    int me = 0;
    int reserveOverride = 0;
    int16_t prevWFrom[MAXC];
    Doctrine() { for (int i = 0; i < MAXC; i++) prevWFrom[i] = -1; }

    // scratch (shared)
    static inline thread_local int routeCost[MAXB][MAXC];
    static inline thread_local RouteCache rcache[2];
    int cacheMode = 0; // 0 none, 1 compute all + store, 2 reuse stored (rollouts)

    int cellOrderAt(int i) const { return G.flip[me] ? G.N - 1 - i : i; }

    int expectedScore(int b) const { return sc10[b]; }

    int featureBonus(const GS& s, int b) const {
        int t = s.turn; int ty = G.btype[b];
        if (ty == PLAZA) return 50;
        if (ty == DEPOT) { bool claimed = s.depot[b] >> me & 1; return claimed ? 5 : (t < 30 ? prm(25, 110) : (t < 80 ? 60 : 15)); }
        if (ty == HALL) return t < 90 ? prm(22, 170) : (t < 130 ? 75 : 15);
        if (ty == ENG) { int owned = engCount(s, me); if (cfg.engDenial && s.own[b] == (1 - me) + 1) return t < 120 ? cfg.engDenial : 60; return t < 100 ? (owned ? 35 : prm(23, 240)) : 40; }
        if (ty == HOSPITAL) { int bx = G.cx(G.bcell[b]); return (t < 100 ? cfg.hospBonus : 30) + ((bx >= 5 && bx <= 9) ? 15 : 0); }
        if (ty == LIBRARY) return t < 100 ? 35 : 10;
        if (ty == STATION) return t < 110 ? 20 : 10;
        return 0;
    }
    int buildingValue(const GS& s, int b) const {
        int ow = s.own[b]; bool mine = ow == me + 1, theirs = ow == (1 - me) + 1;
        if (s.turn >= 140) return expectedScore(b) * (theirs ? 8 : (mine ? 6 : 7));
        int val = expectedScore(b) * 4 + featureBonus(s, b);
        if (theirs) { val = val * 15 / 10 + 20; if (cfg.ehBV && (G.btype[b] == ENG || G.btype[b] == HALL)) val += cfg.ehBV; }
        else if (!mine) val += 15;
        else val = val / 5;
        int bx = G.cx(G.bcell[b]);
        if (bx >= 5 && bx <= 9) val += 15;
        if (s.turn > 120 && theirs) val += 70;
        return val;
    }

    // value of keeping an owned building (loss if flipped)
    int holdValue(const GS& s, int b) const {
        int ty = G.btype[b]; int v = expectedScore(b) * 4;
        int t = s.turn;
        if (ty == ENG) v += t < 120 ? prm(50, 260) : 60;
        else if (ty == HALL) v += t < 110 ? prm(51, 180) : 40;
        else if (ty == PLAZA) v += 60;
        else if (ty == HOSPITAL) v += prm(92, 250);
        else if (ty == DEPOT) v += (s.depot[b] >> me & 1) ? 0 : 20;
        else if (ty == STATION) v += 15;
        else if (ty == LIBRARY) v += 20;
        int bx = G.cx(G.bcell[b]); if (bx >= 5 && bx <= 9) v += 15;
        return v;
    }

    // direction order normalised to home orientation
    void dirOrder(int out[4]) const {
        if (G.flip[me]) { out[0] = 1; out[1] = 0; out[2] = 3; out[3] = 2; }
        else { out[0] = 0; out[1] = 1; out[2] = 2; out[3] = 3; }
    }
    static int dirBetween(int s, int d) {
        for (int k = 0; k < 4; k++) if (G.nb[s][k] == d) return k;
        return -1;
    }

    // One planning attempt. Returns required reserve computed from final F arrivals.
    int planOnce(const GS& s, Plan& out, int& neededReserveOut);

    void plan(const GS& s, int me_, Plan& out) {
        me = me_;
        reserveOverride = 0;
        for (int it = 0; it < 6; it++) {
            out.clear();
            int needed = 0;
            int req = planOnce(s, out, needed);
            if (req > needed) { reserveOverride = req; continue; }
            break;
        }
    }
};

inline int Doctrine::planOnce(const GS& s, Plan& out, int& neededReserveOut) {
    const int op = 1 - me;
    const Team& Mt = s.t[me]; const Team& Et = s.t[op];
    const int N = G.N;
    int dirs[4]; dirOrder(dirs);
    IA myF, myW, enF, enW, enS;
    int totalF = 0, totalW = 0, oppWTotal = 0;
    for (int c = 0; c < N; c++) {
        myF[c] = Mt.F[c]; myW[c] = Mt.W[c]; enF[c] = Et.F[c]; enW[c] = Et.W[c]; enS[c] = Et.S[c];
        totalF += myF[c]; totalW += myW[c]; oppWTotal += enW[c];
    }
    // building order (home normalised)
    int bOrder[MAXB];
    {
        int nb = G.nB; for (int i = 0; i < nb; i++) bOrder[i] = i;
        std::sort(bOrder, bOrder + nb, [&](int a, int b) { return G.homeKey(me, G.bcell[a]) < G.homeKey(me, G.bcell[b]); });
    }
    const int NB = G.nB;

    // ---------------- threats
    IA enemyThreat{}, enemyFReach{}, enemySpawn{};
    int enemySites[16]; int nSites = 0;
    enemySites[nSites++] = G.base[op];
    bool enemyEng = false;
    for (int b = 0; b < NB; b++) if (s.own[b] == op + 1) {
        if (G.btype[b] == ENG) enemyEng = true;
        if (G.btype[b] == HOSPITAL && nSites < 16) enemySites[nSites++] = G.bcell[b];
    }
    for (int q = 0; q < N; q++) if (enW[q] || enF[q]) {
        for (int k = 0; k < G.nln[q]; k++) { int z = G.nl[q][k]; enemyThreat[z] += enW[q]; enemyFReach[z] += enF[q]; }
    }
    {
        int sp = Et.res / (enemyEng ? 2 : 3);
        for (int i = 0; i < nSites; i++) for (int k = 0; k < G.nln[enemySites[i]]; k++) enemySpawn[G.nl[enemySites[i]][k]] = sp;
        for (int z = 0; z < N; z++) if (G.pass[z]) enemyThreat[z] += enemySpawn[z];
    }
    {
        int est[MAXB], ne = 0;
        for (int b = 0; b < NB; b++) if (s.own[b] == op + 1 && G.btype[b] == STATION) est[ne++] = G.bcell[b];
        if (ne >= 2) for (int i = 0; i < ne; i++) {
            int z = est[i], extraW = 0, extraF = 0;
            for (int j = 0; j < ne; j++) { int q = est[j]; if (q != z && G.dist[q][z] > 1) { extraW += enW[q]; extraF += enF[q]; } }
            enemyThreat[z] += std::min(5, extraW); enemyFReach[z] += std::min(5, extraF);
        }
    }

    int eng = 0, halls = 0, stations = 0, libs = 0;
    int spawnSites[MAXB + 1]; int nSpawn = 0; spawnSites[nSpawn++] = G.base[me];
    for (int b = 0; b < NB; b++) if (s.own[b] == me + 1) {
        int ty = G.btype[b];
        if (ty == ENG) eng++; if (ty == HALL) halls++; if (ty == STATION) stations++; if (ty == LIBRARY) libs++;
        if (ty == HOSPITAL) spawnSites[nSpawn++] = G.bcell[b];
    }
    std::sort(spawnSites, spawnSites + nSpawn, [&](int a, int b) { return G.homeKey(me, a) < G.homeKey(me, b); });
    int wcost = std::max(2, 3 - eng);
    int nonOwned = 0; for (int b = 0; b < NB; b++) if (s.own[b] != me + 1) nonOwned++;

    int R = Mt.res;
    int capture_cost = 0;
    for (int i = 0; i < NB; i++) { int b = bOrder[i]; if (s.own[b] != me + 1 && myF[G.bcell[b]] > 0) { int cost = (G.btype[b] == PLAZA ? 4 : 2) - (libs > 0 ? 1 : 0); capture_cost += std::max(1, cost); } }
    int expected_income = 10 + halls * 2;
    int needed_reserve = std::max(reserveOverride, std::max(0, capture_cost - expected_income));
    neededReserveOut = needed_reserve;
    int min_spend = std::max(0, R + expected_income - 40);
    int budget = R - needed_reserve;
    if (budget < min_spend) budget = min_spend;
    budget = std::clamp(budget, 0, R);

    int desiredF = (s.turn <= 15 ? cfg.dF0 : (nonOwned >= 6 ? cfg.dF1 : (nonOwned >= 3 ? cfg.dF2 : cfg.dF3)));
    if (cfg.adaptF && s.turn < 120 && totalW + 6 < oppWTotal) desiredF = std::max(3, desiredF - 1);
    if (cfg.adaptF && s.turn < 120 && totalW > oppWTotal + 18 && nonOwned >= 3) desiredF = std::min(8, desiredF + 1);
    if (s.turn > 145 && nonOwned > 0 && totalW > oppWTotal) desiredF = std::max(desiredF, 5);

    int makeF = 0, makeW = 0;
    if (s.turn == 1) {
        makeF = std::min(2, budget / 5); budget -= 5 * makeF;
        if (budget >= wcost) { makeW = budget / wcost; budget -= makeW * wcost; }
    } else {
        int needF = std::max(0, desiredF - totalF);
        makeF = std::min({needF, (s.turn < 25 ? 2 : 1), budget / 5});
        budget -= 5 * makeF;
        makeW = budget / wcost; budget -= makeW * wcost;
    }

    auto targetValueFrom = [&](int sc, int kind) -> std::pair<int, int> { // kind 0 = F, 1 = W
        int best = -INF; int pos = G.base[me];
        for (int i = 0; i < NB; i++) {
            int b = bOrder[i];
            if (kind == 0 && s.own[b] == me + 1) continue;
            int d = G.dist[sc][G.bcell[b]]; if (d >= 255) continue;
            int val = buildingValue(s, b);
            if (kind == 1) { int ef = enF[G.bcell[b]]; if (ef) val += 220; if (s.own[b] == me + 1) val += 20; }
            int util = val * 100 / (d + 2);
            if (util > best) { best = util; pos = G.bcell[b]; }
        }
        return {best, pos};
    };
    auto bestSpawnSite = [&](int kind) -> int {
        int best = -INF; int site = G.base[me];
        for (int i = 0; i < nSpawn; i++) {
            int sc = spawnSites[i];
            int u = targetValueFrom(sc, kind).first;
            if (kind == 0) u -= std::max(0, enemyThreat[sc] - myW[sc]) * 3000;
            else {
                for (int j = 0; j < N; j++) { int q = cellOrderAt(j); if (myF[q] && G.dist[sc][q] <= 2) u += std::min(20, enemyThreat[q]) * 160 / (G.dist[sc][q] + 1); }
            }
            if (u > best) { best = u; site = sc; }
        }
        return site;
    };
    auto emitSpawn = [&](int kind, int n, int site) {
        if (n <= 0) return;
        out.addSpawn(kind, site, n);
        if (kind == 0) { myF[site] += n; totalF += n; } else if (kind == 1) { myW[site] += n; totalW += n; }
    };
    emitSpawn(0, makeF, bestSpawnSite(0));
    emitSpawn(1, makeW, bestSpawnSite(1));

    // ---------------- teleport
    IA fixedW{};
    if (stations >= 2) {
        int ownSt[MAXB], nst = 0;
        for (int i = 0; i < NB; i++) { int b = bOrder[i]; if (s.own[b] == me + 1 && G.btype[b] == STATION) ownSt[nst++] = b; }
        int src = -1, dst = -1, sendN = 0, bestUrg = -1;
        for (int i = 0; i < nst; i++) {
            int b = ownSt[i]; int bz = G.bcell[b], threatF = 0, threatW = 0;
            for (int j = 0; j < N; j++) { int z = cellOrderAt(j); if (enF[z] || enW[z]) { int d = G.dist[z][bz]; if (d <= 4) threatF += enF[z] * (5 - d); if (d <= 3) threatW += enW[z] * (4 - d); } }
            int urg = 3 * threatF + threatW; if (urg <= bestUrg) continue;
            int candSrc = -1, candN = 0, bestSlack = -1;
            for (int k = 0; k < nst; k++) if (ownSt[k] != b) {
                int a = ownSt[k]; int az = G.bcell[a], c = myW[az]; if (c <= 1) continue;
                int avail = c - 1; if (avail > bestSlack) { bestSlack = avail; candSrc = a; candN = std::min(5, avail); }
            }
            if (candSrc >= 0 && candN > 0) { bestUrg = urg; src = candSrc; dst = b; sendN = candN; }
        }
        if (!(src >= 0 && dst >= 0 && bestUrg > 0)) {
            int bestGain = 1; src = -1; dst = -1; sendN = 0;
            for (int i = 0; i < nst; i++) { int a = ownSt[i]; if (myW[G.bcell[a]] > 1) {
                for (int k = 0; k < nst; k++) { int b = ownSt[k]; if (a == b) continue;
                    int ga = INF, gb = INF;
                    for (int q = 0; q < NB; q++) if (s.own[q] != me + 1) { ga = std::min<int>(ga, G.dist[G.bcell[a]][G.bcell[q]]); gb = std::min<int>(gb, G.dist[G.bcell[b]][G.bcell[q]]); }
                    int gain = ga - gb;
                    if (gain > bestGain) { bestGain = gain; src = a; dst = b; sendN = std::min(5, myW[G.bcell[a]] - 1); }
                } } }
        }
        if (src >= 0 && dst >= 0 && sendN > 0) {
            out.teleSrc = G.bcell[src]; out.teleDst = G.bcell[dst]; out.teleKind = 1; out.teleN = sendN;
            myW[G.bcell[src]] -= sendN; fixedW[G.bcell[dst]] += sendN;
        }
    }

    // ---------------- risk map and routes
    IA friendlyReach{}, regionDiff{}, risk{};
    int (*RC)[MAXC] = routeCost;
    if (cacheMode == 2 && rcache[me].valid) {
        for (int z = 0; z < N; z++) risk[z] = rcache[me].risk[z];
        RC = rcache[me].rc;
    } else {
        for (int q = 0; q < N; q++) if (myW[q]) for (int k = 0; k < G.nln[q]; k++) friendlyReach[G.nl[q][k]] += myW[q];
        for (int q = 0; q < N; q++) { int df = enW[q] - myW[q]; if (df) for (int i = 0; i < G.nball3[q]; i++) regionDiff[G.ball3[q][i]] += df; }
        for (int z = 0; z < N; z++) risk[z] = std::min(prm(4, 48), std::max(0, enemyThreat[z] - friendlyReach[z]) * cfg.riskMul) + std::min(prm(6, 18), std::max(0, regionDiff[z]) * cfg.regMul);
        if (cacheMode == 1) RC = rcache[me].rc;
        for (int i = 0; i < NB; i++) {
            int b = bOrder[i];
            int* dd = RC[b]; for (int c = 0; c < N; c++) dd[c] = INF;
            if (cacheMode != 1 && s.own[b] == me + 1) continue; // only non-owned buildings are F targets
            int target = G.bcell[b];
            static thread_local std::vector<int> bk[128];
            for (int i2 = 0; i2 < 128; i2++) bk[i2].clear();
            dd[target] = 0; bk[0].push_back(target);
            int remaining = 1;
            for (int cur = 0; remaining > 0; cur++) {
                auto& v = bk[cur & 127];
                for (size_t vi = 0; vi < v.size(); vi++) {
                    int z = v[vi]; if (dd[z] != cur) continue;
                    int w = cur + 10 + risk[z];
                    for (int k = 0; k < 4; k++) { int q = G.nb[z][k]; if (q < 0) continue; if (w < dd[q]) { dd[q] = w; bk[w & 127].push_back(q); remaining++; } }
                }
                remaining -= (int)v.size(); v.clear();
            }
        }
        if (cacheMode == 1) { for (int z = 0; z < N; z++) rcache[me].risk[z] = risk[z]; rcache[me].valid = true; }
    }
    auto flagStep = [&](int z, int bid) -> int {
        int answer = -1; int best = RC[bid][z];
        for (int k = 0; k < 4; k++) { int dir = dirs[k]; int q = G.nb[z][dir]; if (q < 0) continue; int score = RC[bid][q] + risk[q]; if (score < best) { best = score; answer = dir; } }
        return answer;
    };

    struct FMove { int src, count, target, dest; bool locked; };
    std::vector<FMove> fplans; IA fAvail = myF; bool occupied[MAXB] = {};
    for (int i = 0; i < NB; i++) {
        int b = bOrder[i]; int z = G.bcell[b];
        if (s.own[b] != me + 1 && fAvail[z] > 0) { fplans.push_back({z, 1, z, z, true}); fAvail[z]--; occupied[b] = true; }
    }
    int targetIds[MAXB], nT = 0;
    for (int i = 0; i < NB; i++) { int b = bOrder[i]; if (s.own[b] != me + 1 && !occupied[b]) targetIds[nT++] = b; }
    struct PairCand { int util, src, bid; };
    std::vector<PairCand> cand;
    for (int j = 0; j < N; j++) { int z = cellOrderAt(j); if (fAvail[z] > 0) {
        for (int i = 0; i < nT; i++) { int bid = targetIds[i]; int d = G.dist[z][G.bcell[bid]]; if (d >= 255 || (s.turn >= 140 && d > 161 - s.turn)) continue;
            cand.push_back({buildingValue(s, bid) * 1000 / (RC[bid][z] + prm(8, 25)), z, bid}); } } }
    std::sort(cand.begin(), cand.end(), [&](const PairCand& a, const PairCand& b) {
        if (a.util != b.util) return a.util > b.util;
        return G.homeKey(me, a.src) < G.homeKey(me, b.src);
    });
    bool usedBid[MAXB] = {};
    for (auto& c : cand) {
        if (fAvail[c.src] <= 0 || usedBid[c.bid]) continue;
        int x = c.src; int d = flagStep(c.src, c.bid); int dz = c.src; if (d >= 0) dz = G.nb[x][d];
        fplans.push_back({c.src, 1, c.bid, dz, false}); fAvail[c.src]--; usedBid[c.bid] = true;
    }
    for (int j = 0; j < N; j++) { int z = cellOrderAt(j); while (fAvail[z] > 0) {
        int best = -INF, bid = -1;
        for (int i = 0; i < NB; i++) { int b = bOrder[i]; if (s.own[b] != me + 1) {
            int d = G.dist[z][G.bcell[b]]; if (d >= 255 || (s.turn >= 140 && d > 161 - s.turn)) continue;
            int u = buildingValue(s, b) * 1000 / (RC[b][z] + prm(8, 25)); if (u > best) { best = u; bid = b; } } }
        if (bid < 0) { fplans.push_back({z, fAvail[z], -1, z, false}); fAvail[z] = 0; break; }
        int d = flagStep(z, bid); int dz = z; if (d >= 0) dz = G.nb[z][d];
        fplans.push_back({z, 1, bid, dz, false}); fAvail[z]--;
    } }

    // ---------------- warriors
    IA wAvail = myW, reservedStay{}, plannedArrive = fixedW;
    struct WMove { int s, d, n; };
    std::vector<WMove> wmoves;
    struct Crit { int pri, z, need; };
    std::vector<Crit> crit;
    for (int i = 0; i < NB; i++) { int b = bOrder[i]; if (s.own[b] == me + 1) {
        int z = G.bcell[b];
        if (enemyFReach[z] > 0) {
            bool assault = myF[z] > 0;
            for (auto& fp : fplans) if (fp.src == z) {
                int dst = G.bAt[fp.dest];
                if (fp.dest == z || dst < 0 || s.own[dst] == me + 1) assault = false;
            }
            crit.push_back({(assault ? 70000 : 100000) + buildingValue(s, b), z, enemyThreat[z] + 1});
        } } }
    for (auto& fp : fplans) {
        int z = fp.dest; int bb = G.bAt[z];
        if (bb >= 0) {
            int minEscort = (enW[z] > 0 || enemyThreat[z] > 0) ? (enemyThreat[z] + 1) : 1;
            if (s.own[bb] == op + 1) minEscort = std::max(minEscort, 2);
            crit.push_back({90000 + enemyFReach[z] * 20, z, minEscort});
        } else if (enemyThreat[z] > 0) crit.push_back({80000, z, enemyThreat[z] + 1});
    }
    if (totalW >= prm(20, 22)) {
        std::vector<int> ownVal;
        for (int i = 0; i < NB; i++) { int b = bOrder[i]; if (s.own[b] == me + 1) ownVal.push_back(b); }
        if (cfg.holdVal) std::sort(ownVal.begin(), ownVal.end(), [&](int a, int b) { return holdValue(s, a) > holdValue(s, b); });
        else std::sort(ownVal.begin(), ownVal.end(), [&](int a, int b) { return buildingValue(s, a) > buildingValue(s, b); });
        int cap = std::min<int>(ownVal.size(), (totalW >= prm(21, 65) ? 5 : 3) + cfg.garrisonN);
        for (int i = 0; i < cap; i++) crit.push_back({30000 + (cfg.holdVal ? holdValue(s, ownVal[i]) : buildingValue(s, ownVal[i])), G.bcell[ownVal[i]], 1});
    }
    if (cfg.ehN > 0 && totalW >= cfg.ehMinW) {
        for (int i = 0; i < NB; i++) { int b = bOrder[i]; if (s.own[b] == me + 1 && (G.btype[b] == ENG || G.btype[b] == HALL)) crit.push_back({cfg.ehPri + buildingValue(s, b), G.bcell[b], cfg.ehN}); }
    }
    std::sort(crit.begin(), crit.end(), [&](const Crit& a, const Crit& b) {
        if (a.pri != b.pri) return a.pri > b.pri;
        return G.homeKey(me, a.z) < G.homeKey(me, b.z);
    });

    IA secured{};
    for (auto& c : crit) {
        int z = c.z; if (secured[z]) continue;
        int need = std::max(0, c.need - plannedArrive[z]);
        int capacity = wAvail[z];
        for (int k = 0; k < 4; k++) { int sq = G.nb[z][k]; if (sq >= 0) capacity += wAvail[sq]; }
        // note: v17 iterates dirOrder over (x-dx,y-dy) i.e. the reverse neighbour, same set of cells
        if (capacity < need) continue;
        int take = std::min(need, wAvail[z]);
        if (take) { reservedStay[z] += take; wAvail[z] -= take; plannedArrive[z] += take; need -= take; }
        for (int k = 0; k < 4; k++) {
            if (need <= 0) break;
            int dir = dirs[k];
            // v17: source cell = z - delta(dir)
            int sq = G.nb[z][dir ^ 1]; // opposite direction index (0<->1, 2<->3)
            if (sq < 0) continue;
            int n = std::min(need, wAvail[sq]);
            if (n) { wAvail[sq] -= n; wmoves.push_back({sq, z, n}); plannedArrive[z] += n; need -= n; }
        }
        if (need <= 0) secured[z] = 1;
    }

    IA goalValue{}, goalNeed{}, goalAssigned = plannedArrive;
    auto addGoal = [&](int z, int value, int need) { goalValue[z] = std::max(goalValue[z], value); goalNeed[z] = std::max(goalNeed[z], need); };
    IA flagETA; flagETA.fill(INF);
    for (int z = 0; z < N; z++) if (myF[z]) for (int q = 0; q < N; q++) flagETA[q] = std::min<int>(flagETA[q], G.dist[z][q]);
    for (int j = 0; j < N; j++) { int z = cellOrderAt(j);
        if (enF[z]) addGoal(z, cfg.huntValue + 50 * enF[z], enemyThreat[z] + cfg.huntNeed);
        if (enW[z]) addGoal(z, prm(10, 150) + std::min(180, 2 * enW[z]), enemyThreat[z] + prm(15, 3));
    }
    for (int i = 0; i < NB; i++) {
        int b = bOrder[i]; int z = G.bcell[b];
        int efDist = INF, ewLocal = 0;
        for (int q = 0; q < N; q++) { if (enF[q]) efDist = std::min<int>(efDist, G.dist[z][q]); if (G.dist[z][q] <= 2) ewLocal += enW[q]; }
        if (cfg.ehGoal > 0 && cfg.ehN > 0 && totalW >= cfg.ehMinW && s.own[b] == me + 1 && (G.btype[b] == ENG || G.btype[b] == HALL)) addGoal(z, cfg.ehGoal, cfg.ehN);
        if (s.own[b] == me + 1 && efDist <= 3) addGoal(z, cfg.defValue + buildingValue(s, b) - efDist * prm(12, 150), std::max(2, ewLocal + 2));
        if (s.own[b] != me + 1) {
            int value = s.own[b] == op + 1 ? prm(13, 500) + 2 * buildingValue(s, b) : prm(14, 350) + buildingValue(s, b);
            if (flagETA[z] > 3 && !enF[z]) value = value * 4 / (flagETA[z] + 1);
            if (cfg.ehRaid && (G.btype[b] == ENG || G.btype[b] == HALL)) value += cfg.ehRaid;
            if (cfg.hospRaid && G.btype[b] == HOSPITAL) value += cfg.hospRaid;
            addGoal(z, value, enemyThreat[z] + prm(16, 2));
        }
    }
    if (cfg.dispatch > 0 && totalW >= cfg.dispatchMinW) {
        std::vector<int> ov;
        for (int i = 0; i < NB; i++) { int b = bOrder[i]; if (s.own[b] == me + 1) ov.push_back(b); }
        std::sort(ov.begin(), ov.end(), [&](int a, int b) { return holdValue(s, a) > holdValue(s, b); });
        int cap = std::min<int>(ov.size(), cfg.dispatch);
        for (int i = 0; i < cap; i++) {
            int z = G.bcell[ov[i]]; int need = 1;
            if (cfg.dispatchScale > 0) {
                int ew5 = 0; for (int q = 0; q < N; q++) if (enW[q] && G.dist[z][q] <= 5) ew5 += enW[q];
                need = std::min(cfg.dispatchMax, 1 + ew5 * cfg.dispatchScale / 100);
            }
            if (myW[z] + plannedArrive[z] < need) addGoal(z, cfg.dispatchBase + holdValue(s, ov[i]), need);
        }
    }
    auto stepToward = [&](int sc, int tz, int n) -> int {
        int dz = sc;
        if (sc != tz) {
            int bestStep = INF;
            for (int k = 0; k < 4; k++) { int dir = dirs[k]; int q = G.nb[sc][dir]; if (q < 0) continue;
                int dd = G.dist[q][tz]; int danger = std::max(0, enW[q] + enemySpawn[q] - (n + plannedArrive[q]));
                int score = dd * prm(18, 30) + danger * prm(19, 85) - std::min(20, plannedArrive[q]) * 2 + (prevWFrom[sc] == q ? 9 : 0);
                if (score < bestStep) { bestStep = score; dz = q; } }
        }
        return dz;
    };
    if (cfg.goalFirst) {
        std::vector<int> gl; for (int j = 0; j < N; j++) { int z = cellOrderAt(j); if (goalValue[z] > 0) gl.push_back(z); }
        std::stable_sort(gl.begin(), gl.end(), [&](int a, int b) { return goalValue[a] > goalValue[b]; });
        for (int g : gl) {
            while (goalAssigned[g] < goalNeed[g]) {
                int bs = -1, bd = INF;
                for (int j = 0; j < N; j++) { int sc = cellOrderAt(j); if (wAvail[sc] > 0) {
                    int d = G.dist[sc][g]; if (d >= 255 || d > cfg.goalFirstD || (s.turn >= 145 && d > 161 - s.turn)) continue;
                    if (d < bd || (d == bd && wAvail[sc] > wAvail[bs])) { bd = d; bs = sc; } } }
                if (bs < 0) break;
                int n = std::min(wAvail[bs], goalNeed[g] - goalAssigned[g]);
                int dz = stepToward(bs, g, n);
                goalAssigned[g] += n;
                if (dz == bs) reservedStay[bs] += n; else wmoves.push_back({bs, dz, n});
                plannedArrive[dz] += n; wAvail[bs] -= n;
            }
        }
    }
    std::vector<int> sources;
    for (int j = 0; j < N; j++) { int z = cellOrderAt(j); if (wAvail[z] > 0) sources.push_back(z); }
    std::sort(sources.begin(), sources.end(), [&](int a, int b) {
        if (wAvail[a] != wAvail[b]) return wAvail[a] > wAvail[b];
        return G.homeKey(me, a) < G.homeKey(me, b);
    });
    for (int sc : sources) {
        int left = wAvail[sc];
        while (left > 0) {
            int tz = -1, best = -INF;
            for (int j = 0; j < N; j++) { int z = cellOrderAt(j); if (goalValue[z] > 0 && goalAssigned[z] < goalNeed[z]) {
                int d = G.dist[sc][z]; if (d >= 255 || (s.turn >= 145 && d > 161 - s.turn)) continue;
                int utility = goalValue[z] * 100 / (d + prm(17, 4)); if (utility > best) { best = utility; tz = z; } } }
            bool overflow = false;
            if (tz < 0 && !cfg.rally) {
                overflow = true;
                for (int j = 0; j < N; j++) { int z = cellOrderAt(j); if (enW[z] || enF[z]) {
                    int d = G.dist[sc][z]; if (d >= 255 || (s.turn >= 145 && d > 161 - s.turn)) continue;
                    int utility = (enF[z] ? 1100 : 600) + std::min(300, 3 * enW[z]) - 25 * d; if (utility > best) { best = utility; tz = z; } } }
            }
            if (tz < 0 && cfg.rally) {
                int bz = -1, bn = 0;
                for (int j = 0; j < N; j++) { int z = cellOrderAt(j); if (myW[z] > bn) { bn = myW[z]; bz = z; } }
                if (bz >= 0 && bz != sc && G.dist[sc][bz] < 255) { tz = bz; overflow = true; }
            }
            if (tz < 0) { reservedStay[sc] += left; plannedArrive[sc] += left; break; }
            int n = overflow ? left : std::min(left, goalNeed[tz] - goalAssigned[tz]);
            int dz = sc;
            if (sc != tz) {
                int bestStep = INF;
                for (int k = 0; k < 4; k++) { int dir = dirs[k]; int q = G.nb[sc][dir]; if (q < 0) continue;
                    int dd = G.dist[q][tz]; int danger = std::max(0, enW[q] + enemySpawn[q] - (n + plannedArrive[q]));
                    int score = dd * prm(18, 30) + danger * prm(19, 85) - std::min(20, plannedArrive[q]) * 2 + (prevWFrom[sc] == q ? 9 : 0);
                    if (score < bestStep) { bestStep = score; dz = q; } }
            }
            goalAssigned[tz] += n;
            if (dz == sc) reservedStay[sc] += n; else wmoves.push_back({sc, dz, n});
            plannedArrive[dz] += n; left -= n;
        }
        wAvail[sc] = 0;
    }

    // ---------------- final flag destinations
    struct FMoveO { int s, d, n; };
    std::vector<FMoveO> fmoves; IA finalFStay{}, finalFArrive{};
    for (auto& fp : fplans) {
        int sc = fp.src, bid = fp.target;
        if (fp.locked) { bid = G.bAt[sc]; }
        if (bid < 0 || bid >= NB) bid = -1;
        int choices[5]; int nch = 0; choices[nch++] = sc;
        for (int k = 0; k < 4; k++) { int q = G.nb[sc][dirs[k]]; if (q >= 0) choices[nch++] = q; }
        int best = INF, dest = sc;
        for (int ci = 0; ci < nch; ci++) {
            int q = choices[ci];
            int deficit = std::max(0, enemyThreat[q] - plannedArrive[q]);
            int score = deficit * 100000;
            int qb = G.bAt[q];
            if (deficit && qb >= 0 && s.own[qb] == me + 1) score += 50000;
            score += bid >= 0 ? RC[bid][q] : 0;
            score += (q == sc ? 7 : 0);
            if (fp.locked && q == sc) score -= 40;
            if (q == fp.dest) score -= 4;
            score -= std::min(10, plannedArrive[q]);
            if (s.turn == 160) {
                if (qb >= 0 && s.own[qb] != me + 1 && (enemyFReach[q] == 0 || plannedArrive[q] > enemyThreat[q])) score -= 1000 * expectedScore(qb);
                if (q == sc && qb >= 0 && s.own[qb] == me + 1 && enemyFReach[q] && plannedArrive[q] <= enemyThreat[q]) score -= 1000 * expectedScore(qb);
            }
            if (score < best) { best = score; dest = q; }
        }
        if (dest == sc) finalFStay[sc] += fp.count; else { fmoves.push_back({sc, dest, fp.count}); finalFArrive[dest] += fp.count; }
    }
    int actualCaptureCost = 0;
    for (int i = 0; i < NB; i++) { int b = bOrder[i]; int z = G.bcell[b];
        if (s.own[b] != me + 1 && finalFStay[z] + finalFArrive[z] > 0) actualCaptureCost += std::max(1, (G.btype[b] == PLAZA ? 4 : 2) - (libs > 0));
    }
    int requiredReserve = std::max(0, actualCaptureCost - expected_income);
    if (requiredReserve > needed_reserve) return requiredReserve;

    // priority
    {
        std::vector<int> pr;
        for (int i = 0; i < NB; i++) { int b = bOrder[i]; int z = G.bcell[b]; if (s.own[b] != me + 1 && (finalFStay[z] + finalFArrive[z] > 0)) pr.push_back(b); }
        std::sort(pr.begin(), pr.end(), [&](int a, int b) {
            int va = buildingValue(s, a), vb = buildingValue(s, b);
            if (va != vb) return va > vb;
            return G.homeKey(me, G.bcell[a]) < G.homeKey(me, G.bcell[b]);
        });
        for (int b : pr) if (out.nprio < MAXB) out.prio[out.nprio++] = b;
    }
    // merge moves
    {
        std::vector<WMove> wm;
        std::stable_sort(wmoves.begin(), wmoves.end(), [](const WMove& a, const WMove& b) { return a.s != b.s ? a.s < b.s : a.d < b.d; });
        for (auto& m : wmoves) if (m.n > 0) { if (!wm.empty() && wm.back().s == m.s && wm.back().d == m.d) wm.back().n += m.n; else wm.push_back(m); }
        // v17 orders map keys by (src cell index, dst cell index) in absolute coordinates
        for (int i = 0; i < N; i++) prevWFrom[i] = -1;
        static int prevWCount[MAXC]; for (int i = 0; i < N; i++) prevWCount[i] = 0;
        for (auto& m : wm) {
            out.addMove(m.s, 1, dirBetween(m.s, m.d), m.n);
            int d = m.d, sc = m.s, n = m.n;
            if (n > prevWCount[d] || (n == prevWCount[d] && (prevWFrom[d] < 0 || G.homeKey(me, sc) < G.homeKey(me, prevWFrom[d])))) { prevWFrom[d] = sc; prevWCount[d] = n; }
        }
        std::vector<FMoveO> fm = fmoves;
        std::stable_sort(fm.begin(), fm.end(), [](const FMoveO& a, const FMoveO& b) { return a.s != b.s ? a.s < b.s : a.d < b.d; });
        std::vector<FMoveO> fm2;
        for (auto& m : fm) if (m.n > 0) { if (!fm2.empty() && fm2.back().s == m.s && fm2.back().d == m.d) fm2.back().n += m.n; else fm2.push_back(m); }
        for (auto& m : fm2) out.addMove(m.s, 0, dirBetween(m.s, m.d), m.n);
    }
    return requiredReserve;
}
} // namespace pol

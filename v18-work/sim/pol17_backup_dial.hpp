// pol17.hpp - array-based re-implementation of the v15/v16/v17 decision procedure (yjc-submission1x-cpp/main.cpp).
// Verified bit-exact against the original binaries with tools/difftest (same commands on the same view).
// Used (a) as the model of the opponent lineage and (b) as our own base policy family (parameterised).
#pragma once
#include "sim.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <queue>
#include <vector>

namespace pol {
using namespace sim;
constexpr int INF = 1000000000;
#ifdef POLPROF
inline uint64_t rdt() { uint64_t v; asm volatile("mrs %0, cntvct_el0" : "=r"(v)); return v; }
inline uint64_t g_prof[32]; inline long g_nfwd = 0, g_nrev = 0, g_ncalls = 0; inline int g_pcur = -1; inline uint64_t g_plast = 0;
inline void PSEC(int i) { uint64_t t = rdt(); if (g_pcur >= 0) g_prof[g_pcur] += t - g_plast; g_pcur = i; g_plast = t; }
#else
inline void PSEC(int) {}
#endif

struct Params {
    int P[40];
    bool fAdjust = true;   // v16+ desiredF adjustments
    // ---- extensions (all neutral by default => exact v17) ----
    int extraGoal = -1;        // cell index of an injected goal (or -1)
    int extraGoalValue = 0;
    int extraGoalNeed = 0;
    int garrisonAdd = 0;       // added to "critical" garrison needs of own buildings
    int noSpawnW = 0;          // 1: do not spawn W
    Params() {
        const int d[26] = {7, 6, 5, 4, 48, 13, 18, 3, 25, 660, 150, 960, 150, 500, 350, 3, 2, 4, 30, 85, 22, 65, 170, 240, 145, 110};
        for (int i = 0; i < 40; i++) P[i] = i < 26 ? d[i] : 0;
    }
    static Params v17() { return Params(); }
    static Params v16() { Params p; p.P[5] = 8; p.P[7] = 2; p.P[9] = 1100; p.P[11] = 1600; return p; }
    static Params v15() {
        Params p = v16();
        p.P[0] = 6; p.P[1] = 5; p.P[2] = 4; p.P[3] = 3; p.fAdjust = false;
        return p;
    }
};

struct Persist {
    int16_t prevWFrom[NC];
    Persist() { reset(); }
    void reset() { for (int i = 0; i < NC; i++) prevWFrom[i] = -1; }
};

struct FPlan { int src, count, target, dest; bool locked; };

// Cells within path distance <=3 lists (built lazily per Map)
struct Near {
    int n1[NC][5], c1[NC];
    int n3[NC][25], c3[NC];
    void build(const Map& M) {
        for (int z = 0; z < NC; z++) {
            c1[z] = c3[z] = 0;
            if (!M.pass[z]) continue;
            for (int q = 0; q < NC; q++) {
                int d = M.dist[z][q];
                if (d <= 1) n1[z][c1[z]++] = q;
                if (d <= 3) n3[z][c3[z]++] = q;
            }
        }
    }
};

inline void dirBetween(int s, int d, int& dir) {
    int x = s % GW, y = s / GW, a = d % GW, b = d / GW;
    if (a == x && b == y - 1) dir = D_U;
    else if (a == x && b == y + 1) dir = D_D;
    else if (a == x - 1 && b == y) dir = D_L;
    else dir = D_R;
}

// Main entry: writes this turn's orders of team `me` into `out`.
// EXACT=true reproduces the original bots bit-exactly; EXACT=false replaces the weighted route search by a DP over
// shortest-hop paths (identical unless a risk-avoiding detour is optimal) and is several times faster.
template <bool EXACT>
inline void decide17T(const Map& M, const Near& NR, const State& S, int me, const Params& PR, Persist& ps, Orders& out,
                      int* dbgReserve = nullptr) {
    const int op = 1 - me;
    const int turn = S.turn + 1;
    const int* P = PR.P;
    const int* cord = M.cord[me];
    const int* bord = M.bord[me];
    int dirOrder[4] = {D_U, D_D, D_L, D_R};
    if (M.flip[me]) { dirOrder[0] = D_D; dirOrder[1] = D_U; dirOrder[2] = D_R; dirOrder[3] = D_L; }
    static const int OPPD[4] = {D_D, D_U, D_R, D_L};
    const int nb = M.nb;
    auto hk = [&](int z) { return M.homeKey[me][z]; };
    auto dst = [&](int a, int b) -> int { int d = M.dist[a][b]; return d == 255 ? INF : d; };

    // knowledge of scores (v17: knownScore memory incl. symmetric partner)
    bool known[MAXB];
    for (int b = 0; b < nb; b++) {
        bool k = ((S.rev[me] >> b) & 1) != 0;
        int sy = M.sym[b];
        if (sy >= 0 && ((S.rev[me] >> sy) & 1)) k = true;
        if (M.btype[b] == T_PLAZA) k = true;
        known[b] = k;
    }
    auto expectedScore = [&](int b) -> int {
        if (known[b]) return M.score2[b] * 5;
        if (M.btype[b] == T_PLAZA) return 30;
        int x = M.bcell[b] % GW;
        return (x >= 5 && x <= 9) ? 30 : 15;
    };
    int engOwned = 0;
    for (int b = 0; b < nb; b++) if (S.owner[b] == me && M.btype[b] == T_ENG) engOwned++;
    int bval[MAXB];
    for (int b = 0; b < nb; b++) {
        int t = turn;
        int ow = S.owner[b];
        int bx = M.bcell[b] % GW;
        int es = expectedScore(b);
        int val;
        if (t >= 140) { bval[b] = es * (ow == op ? 8 : (ow == me ? 6 : 7)); continue; }
        int fb = 0;
        switch (M.btype[b]) {
            case T_PLAZA: fb = 50; break;
            case T_DEPOT: { bool claimed = ((S.dep[b] >> me) & 1) != 0; fb = claimed ? 5 : (t < 30 ? P[25] : (t < 80 ? 60 : 15)); break; }
            case T_HALL: fb = t < 90 ? P[22] : (t < 130 ? 75 : 15); break;
            case T_ENG: fb = t < 100 ? (engOwned ? 35 : P[23]) : 40; break;
            case T_HOSPITAL: fb = (t < 100 ? P[24] : 30) + ((bx >= 5 && bx <= 9) ? 15 : 0); break;
            case T_LIBRARY: fb = t < 100 ? 35 : 10; break;
            case T_STATION: fb = t < 110 ? 20 : 10; break;
            default: fb = 0;
        }
        val = es * 4 + fb;
        if (ow == op) val = val * 15 / 10 + 20;
        else if (ow < 0) val += 15;
        else val = val / 5;
        if (bx >= 5 && bx <= 9) val += 15;
        if (t > 120 && ow == op) val += 70;
        bval[b] = val;
    }

    int reserveOverride = 0;
    int guard = 0;
    for (;;) {
        PSEC(0);
        out.clear();
        int myF[NC], myW[NC], enF[NC], enW[NC];
        int totalFv = 0, totalWv = 0, oppWTotal = 0, oppFTotal = 0;
        for (int c = 0; c < NC; c++) {
            myF[c] = S.F[me][c]; myW[c] = S.Wn[me][c]; enF[c] = S.F[op][c]; enW[c] = S.Wn[op][c];
            totalFv += myF[c]; totalWv += myW[c]; oppFTotal += enF[c]; oppWTotal += enW[c];
        }
        PSEC(1);
        int enemyThreat[NC], enemyFReach[NC], enemySpawn[NC];
        for (int c = 0; c < NC; c++) enemyThreat[c] = enemyFReach[c] = enemySpawn[c] = 0;
        int enemySites[MAXB + 1], nes = 0;
        enemySites[nes++] = M.base[op];
        bool enemyEng = false;
        for (int b = 0; b < nb; b++) if (S.owner[b] == op) {
            if (M.btype[b] == T_ENG) enemyEng = true;
            if (M.btype[b] == T_HOSPITAL) enemySites[nes++] = M.bcell[b];
        }
        int spawnVal = S.res[op] / (enemyEng ? 2 : 3);
        for (int z = 0; z < NC; z++) {
            if (!M.pass[z]) continue;
            int th = 0, fr = 0;
            for (int i = 0; i < NR.c1[z]; i++) { int q = NR.n1[z][i]; th += enW[q]; fr += enF[q]; }
            enemyThreat[z] = th; enemyFReach[z] = fr;
        }
        for (int i = 0; i < nes; i++) {
            int q = enemySites[i];
            for (int j = 0; j < NR.c1[q]; j++) enemySpawn[NR.n1[q][j]] = spawnVal;
        }
        for (int z = 0; z < NC; z++) if (M.pass[z]) enemyThreat[z] += enemySpawn[z];
        {
            int est[MAXB], ns = 0;
            for (int b = 0; b < nb; b++) if (S.owner[b] == op && M.btype[b] == T_STATION) est[ns++] = M.bcell[b];
            if (ns >= 2) for (int i = 0; i < ns; i++) {
                int z = est[i], extraW = 0, extraF = 0;
                for (int j = 0; j < ns; j++) { int q = est[j]; if (q != z && M.dist[q][z] > 1) { extraW += enW[q]; extraF += enF[q]; } }
                enemyThreat[z] += std::min(5, extraW); enemyFReach[z] += std::min(5, extraF);
            }
        }
        PSEC(2);
        int eng = 0, halls = 0, stations = 0, libs = 0;
        int spawnSites[MAXB + 1], nss = 0;
        spawnSites[nss++] = M.base[me];
        for (int b = 0; b < nb; b++) if (S.owner[b] == me) {
            switch (M.btype[b]) {
                case T_ENG: eng++; break; case T_HALL: halls++; break; case T_STATION: stations++; break; case T_LIBRARY: libs++; break;
                case T_HOSPITAL: spawnSites[nss++] = M.bcell[b]; break; default: break;
            }
        }
        std::sort(spawnSites, spawnSites + nss, [&](int a, int b) { return hk(a) < hk(b); });
        int wcost = std::max(2, 3 - eng);
        int nonOwned = 0;
        for (int b = 0; b < nb; b++) if (S.owner[b] != me) nonOwned++;

        int R = S.res[me];
        int capture_cost = 0;
        for (int i = 0; i < nb; i++) {
            int b = bord[i];
            if (S.owner[b] != me && myF[M.bcell[b]] > 0) {
                int cost = (M.btype[b] == T_PLAZA ? 4 : 2) - (libs > 0 ? 1 : 0);
                capture_cost += std::max(1, cost);
            }
        }
        int expected_income = 10 + halls * 2;
        int needed_reserve = std::max(reserveOverride, std::max(0, capture_cost - expected_income));
        int min_spend = std::max(0, R + expected_income - 40);
        int budget = R - needed_reserve;
        if (budget < min_spend) budget = min_spend;
        budget = std::clamp(budget, 0, R);

        int desiredF = (turn <= 15 ? P[0] : (nonOwned >= 6 ? P[1] : (nonOwned >= 3 ? P[2] : P[3])));
        if (PR.fAdjust) {
            if (turn < 120 && totalWv + 6 < oppWTotal) desiredF = std::max(3, desiredF - 1);
            if (turn < 120 && totalWv > oppWTotal + 18 && nonOwned >= 3) desiredF = std::min(8, desiredF + 1);
        }
        if (turn > 145 && nonOwned > 0 && totalWv > oppWTotal) desiredF = std::max(desiredF, 5);

        int makeF = 0, makeW = 0;
        if (turn == 1) {
            makeF = std::min(2, budget / 5); budget -= 5 * makeF;
            if (budget >= wcost) { makeW = budget / wcost; budget -= makeW * wcost; }
        } else {
            int needF = std::max(0, desiredF - totalFv);
            makeF = std::min({needF, (turn < 25 ? 2 : 1), budget / 5});
            budget -= 5 * makeF;
            makeW = budget / wcost;
            budget -= makeW * wcost;
        }
        if (PR.noSpawnW) makeW = 0;

        PSEC(3);
        auto targetValueFrom = [&](int z0, int kind) -> int {
            int best = -INF;
            for (int i = 0; i < nb; i++) {
                int b = bord[i];
                if (kind == K_F && S.owner[b] == me) continue;
                int d = dst(z0, M.bcell[b]); if (d >= INF) continue;
                int val = bval[b];
                if (kind == K_W) {
                    if (enF[M.bcell[b]]) val += 220;
                    if (S.owner[b] == me) val += 20;
                }
                int util = val * 100 / (d + 2);
                if (util > best) best = util;
            }
            return best;
        };
        auto bestSpawnSite = [&](int kind) -> int {
            int best = -INF, site = M.base[me];
            for (int i = 0; i < nss; i++) {
                int z = spawnSites[i];
                int u = targetValueFrom(z, kind);
                if (kind == K_F) u -= std::max(0, enemyThreat[z] - myW[z]) * 3000;
                else {
                    for (int k = 0; k < NR.c3[z]; k++) {
                        int q = NR.n3[z][k];
                        if (myF[q] && M.dist[z][q] <= 2) u += std::min(20, enemyThreat[q]) * 160 / (M.dist[z][q] + 1);
                    }
                }
                if (u > best) { best = u; site = z; }
            }
            return site;
        };
        auto emitSpawn = [&](int kind, int n, int site) {
            if (n <= 0) return;
            out.addSpawn(kind, n, site == M.base[me] ? -1 : site);
            if (kind == K_F) { myF[site] += n; totalFv += n; } else { myW[site] += n; totalWv += n; }
        };
        { int site = bestSpawnSite(K_F); emitSpawn(K_F, makeF, site); }
        { int site = bestSpawnSite(K_W); emitSpawn(K_W, makeW, site); }

        PSEC(4);
        int fixedW[NC];
        for (int c = 0; c < NC; c++) fixedW[c] = 0;
        if (stations >= 2) {
            int ownSt[MAXB], nst = 0;
            for (int i = 0; i < nb; i++) { int b = bord[i]; if (S.owner[b] == me && M.btype[b] == T_STATION) ownSt[nst++] = M.bcell[b]; }
            int src = -1, dstc = -1, sendN = 0, bestUrg = -1;
            for (int i = 0; i < nst; i++) {
                int bz = ownSt[i], threatF = 0, threatW = 0;
                for (int k = 0; k < NC; k++) {
                    int z = cord[k];
                    if (enF[z] || enW[z]) {
                        int d = dst(z, bz);
                        if (d <= 4) threatF += enF[z] * (5 - d);
                        if (d <= 3) threatW += enW[z] * (4 - d);
                    }
                }
                int urg = 3 * threatF + threatW;
                if (urg <= bestUrg) continue;
                int candSrc = -1, candN = 0, bestSlack = -1;
                for (int j = 0; j < nst; j++) if (j != i) {
                    int az = ownSt[j], c = myW[az]; if (c <= 1) continue;
                    int avail = c - 1;
                    if (avail > bestSlack) { bestSlack = avail; candSrc = az; candN = std::min(5, avail); }
                }
                if (candSrc >= 0 && candN > 0) { bestUrg = urg; src = candSrc; dstc = bz; sendN = candN; }
            }
            if (!(src >= 0 && dstc >= 0 && bestUrg > 0)) {
                int bestGain = 1; src = -1; dstc = -1; sendN = 0;
                for (int i = 0; i < nst; i++) if (myW[ownSt[i]] > 1) {
                    int az = ownSt[i];
                    for (int j = 0; j < nst; j++) if (i != j) {
                        int bz = ownSt[j];
                        int ga = INF, gb = INF;
                        for (int q = 0; q < nb; q++) if (S.owner[q] != me) {
                            ga = std::min(ga, dst(az, M.bcell[q]));
                            gb = std::min(gb, dst(bz, M.bcell[q]));
                        }
                        int gain = ga - gb;
                        if (gain > bestGain) { bestGain = gain; src = az; dstc = bz; sendN = std::min(5, myW[az] - 1); }
                    }
                }
            }
            if (src >= 0 && dstc >= 0 && sendN > 0) {
                out.addTele(src, K_W, sendN, dstc);
                myW[src] -= sendN;
                fixedW[dstc] += sendN;
            }
        }

        PSEC(5);
        int friendlyReach[NC], regionDiff[NC], risk[NC];
        for (int c = 0; c < NC; c++) friendlyReach[c] = regionDiff[c] = 0;
        for (int q = 0; q < NC; q++) {
            if (!(myW[q] | enW[q])) continue;
            if (!M.pass[q]) continue;
            for (int i = 0; i < NR.c1[q]; i++) friendlyReach[NR.n1[q][i]] += myW[q];
            int df = enW[q] - myW[q];
            for (int i = 0; i < NR.c3[q]; i++) regionDiff[NR.n3[q][i]] += df;
        }
        for (int z = 0; z < NC; z++)
            risk[z] = std::min(P[4], std::max(0, enemyThreat[z] - friendlyReach[z]) * P[5]) + std::min(P[6], std::max(0, regionDiff[z]) * P[7]);

        // ---- route costs. rc(q->t) = min over paths of sum over nodes after q (incl. t) of (10+risk).
        // Forward Dijkstra from each distinct F cell gives rc(z->t) for all t plus the set of optimal first steps.
        static thread_local int FD[24][NC];
        static thread_local uint8_t FM[24][NC];
        int fdIndex[NC];
        for (int c = 0; c < NC; c++) fdIndex[c] = -1;
        int nfd = 0;
        static thread_local int head[128], nxt[4096], nodeOf[4096], keyOf[4096];
        auto getFwd = [&](int z) -> int {
            if (fdIndex[z] >= 0) return fdIndex[z];
#ifdef POLPROF
            g_nfwd++;
#endif
            int idx = nfd < 24 ? nfd++ : 23;
            fdIndex[z] = idx;
            int* dd = FD[idx]; uint8_t* mk = FM[idx];
            for (int c = 0; c < NC; c++) { dd[c] = INF; mk[c] = 0; }
            if constexpr (!EXACT) {
                dd[z] = 0;
                const uint8_t* ordz = M.byDist[z];
                int cnt = M.byDistN[z];
                for (int i = 1; i < cnt; i++) {
                    int x = ordz[i];
                    unsigned m = M.predMask[z][x];
                    int best = INF; uint8_t bm = 0;
                    while (m) {
                        int k = __builtin_ctz(m); m &= m - 1;
                        int p = M.nbr[x][k];
                        int v = dd[p];
                        uint8_t fm = (p == z) ? (uint8_t)(1 << OPPD[k]) : mk[p];
                        if (v < best) { best = v; bm = fm; } else if (v == best) bm |= fm;
                    }
                    dd[x] = best + 10 + risk[x]; mk[x] = bm;
                }
                return idx;
            }
            for (int i = 0; i < 128; i++) head[i] = -1;
            uint64_t bm0 = 0, bm1 = 0;
            int ne = 0, remaining = 0, cur = 0;
            dd[z] = 0;
            nodeOf[ne] = z; keyOf[ne] = 0; nxt[ne] = head[0]; head[0] = ne; ne++; remaining++; bm0 |= 1ull;
            while (remaining > 0) {
                // advance cur to next non-empty bucket (circular bitmap of 128 buckets)
                {
                    int p0 = cur & 127;
                    uint64_t m0 = p0 < 64 ? (bm0 >> p0) << p0 : 0ull;   // bits >= p0 in word 0
                    uint64_t m1 = p0 < 64 ? bm1 : (bm1 >> (p0 - 64)) << (p0 - 64);
                    int f;
                    if (p0 < 64 && m0) f = __builtin_ctzll(m0);
                    else if (m1) f = 64 + __builtin_ctzll(m1);
                    else f = bm0 ? __builtin_ctzll(bm0) : 64 + __builtin_ctzll(bm1);   // wrap around
                    cur += (f - p0) & 127;
                }
                int bi = cur & 127;
                int e = head[bi];
                head[bi] = nxt[e];
                if (head[bi] < 0) { if (bi < 64) bm0 &= ~(1ull << bi); else bm1 &= ~(1ull << (bi - 64)); }
                remaining--;
                int x = nodeOf[e], d = keyOf[e];
                if (d != dd[x]) continue;
                for (int k = 0; k < 4; k++) {
                    int y = M.nbr[x][k]; if (y < 0) continue;
                    int nd = d + 10 + risk[y];
                    uint8_t fm = (x == z) ? (uint8_t)(1 << k) : mk[x];
                    if (nd < dd[y]) {
                        dd[y] = nd; mk[y] = fm;
                        if (ne < 4096) {
                            int nb2 = nd & 127;
                            nodeOf[ne] = y; keyOf[ne] = nd; nxt[ne] = head[nb2]; head[nb2] = ne; ne++; remaining++;
                            if (nb2 < 64) bm0 |= 1ull << nb2; else bm1 |= 1ull << (nb2 - 64);
                        }
                    } else if (nd == dd[y]) mk[y] |= fm;
                }
            }
            return idx;
        };
        // lazily computed reverse searches (exact rc(q->t) for arbitrary q)
        static thread_local int rc[MAXB][NC];
        bool rcDone[MAXB];
        for (int b = 0; b < nb; b++) rcDone[b] = false;
        auto computeRC = [&](int b) {
            if (rcDone[b]) return;
            rcDone[b] = true;
#ifdef POLPROF
            g_nrev++;
#endif
            int* dd = rc[b];
            for (int c = 0; c < NC; c++) dd[c] = INF;
            if constexpr (!EXACT) {
                // DP over hop layers from the target (shortest-hop paths only)
                int tg = M.bcell[b];
                const uint8_t* ordt = M.byDist[tg];
                int cnt = M.byDistN[tg];
                dd[tg] = 0;
                for (int i = 1; i < cnt; i++) {
                    int x = ordt[i];
                    unsigned m = M.predMask[tg][x];
                    int best = INF;
                    while (m) {
                        int k = __builtin_ctz(m); m &= m - 1;
                        int pn = M.nbr[x][k];
                        int v = dd[pn] + 10 + risk[pn];
                        if (v < best) best = v;
                    }
                    dd[x] = best;
                }
                return;
            }
            for (int i = 0; i < 128; i++) head[i] = -1;
            uint64_t bm0 = 0, bm1 = 0;
            int ne = 0, remaining = 0, cur = 0;
            int tg = M.bcell[b];
            dd[tg] = 0;
            nodeOf[ne] = tg; keyOf[ne] = 0; nxt[ne] = head[0]; head[0] = ne; ne++; remaining++; bm0 |= 1ull;
            while (remaining > 0) {
                {
                    int p0 = cur & 127;
                    uint64_t m0 = p0 < 64 ? (bm0 >> p0) << p0 : 0ull;
                    uint64_t m1 = p0 < 64 ? bm1 : (bm1 >> (p0 - 64)) << (p0 - 64);
                    int f;
                    if (p0 < 64 && m0) f = __builtin_ctzll(m0);
                    else if (m1) f = 64 + __builtin_ctzll(m1);
                    else f = bm0 ? __builtin_ctzll(bm0) : 64 + __builtin_ctzll(bm1);
                    cur += (f - p0) & 127;
                }
                int bi = cur & 127;
                int e = head[bi];
                head[bi] = nxt[e];
                if (head[bi] < 0) { if (bi < 64) bm0 &= ~(1ull << bi); else bm1 &= ~(1ull << (bi - 64)); }
                remaining--;
                int z = nodeOf[e], d = keyOf[e];
                if (d != dd[z]) continue;
                int w = d + 10 + risk[z];
                for (int k = 0; k < 4; k++) {
                    int q = M.nbr[z][k]; if (q < 0) continue;
                    if (w < dd[q]) {
                        dd[q] = w;
                        if (ne < 4096) {
                            int nb2 = w & 127;
                            nodeOf[ne] = q; keyOf[ne] = w; nxt[ne] = head[nb2]; head[nb2] = ne; ne++; remaining++;
                            if (nb2 < 64) bm0 |= 1ull << nb2; else bm1 |= 1ull << (nb2 - 64);
                        }
                    }
                }
            }
        };
        auto flagStep = [&](int z, int b) -> int {  // returns dir or -1
            int fi = getFwd(z);
            uint8_t m = FM[fi][M.bcell[b]];
            for (int k = 0; k < 4; k++) if ((m >> dirOrder[k]) & 1) return dirOrder[k];
            return -1;
        };

        PSEC(6);
        std::vector<FPlan> fplans; fplans.reserve(16);
        int fAvail[NC];
        for (int c = 0; c < NC; c++) fAvail[c] = myF[c];
        bool occupied[MAXB];
        for (int b = 0; b < nb; b++) occupied[b] = false;
        for (int i = 0; i < nb; i++) {
            int b = bord[i], z = M.bcell[b];
            if (S.owner[b] != me && fAvail[z] > 0) {
                fplans.push_back({z, 1, z, z, true});
                fAvail[z]--;
                occupied[b] = true;
            }
        }
        int targetIds[MAXB], ntg = 0;
        for (int i = 0; i < nb; i++) { int b = bord[i]; if (S.owner[b] != me && !occupied[b]) targetIds[ntg++] = b; }

        struct PairCand { int util, src, bid; };
        std::vector<PairCand> cand; cand.reserve(128);
        for (int k = 0; k < NC; k++) {
            int z = cord[k];
            if (fAvail[z] <= 0) continue;
            for (int i = 0; i < ntg; i++) {
                int bid = targetIds[i];
                int d = dst(z, M.bcell[bid]);
                if (d >= INF || (turn >= 140 && d > 161 - turn)) continue;
                int fi = getFwd(z);
                cand.push_back({bval[bid] * 1000 / (FD[fi][M.bcell[bid]] + P[8]), z, bid});
            }
        }
        std::sort(cand.begin(), cand.end(), [&](const PairCand& a, const PairCand& b) {
            if (a.util != b.util) return a.util > b.util;
            return hk(a.src) < hk(b.src);
        });
        bool usedBid[MAXB];
        for (int b = 0; b < nb; b++) usedBid[b] = false;
        for (auto& c : cand) {
            if (fAvail[c.src] <= 0 || usedBid[c.bid]) continue;
            int dr = flagStep(c.src, c.bid);
            int dz = c.src;
            if (dr >= 0) dz = M.nbr[c.src][dr];
            fplans.push_back({c.src, 1, c.bid, dz, false});
            fAvail[c.src]--;
            usedBid[c.bid] = true;
        }
        for (int k = 0; k < NC; k++) {
            int z = cord[k];
            while (fAvail[z] > 0) {
                int best = -INF, bid = -1;
                for (int i = 0; i < nb; i++) {
                    int b = bord[i];
                    if (S.owner[b] != me) {
                        int d = dst(z, M.bcell[b]); if (d >= INF || (turn >= 140 && d > 161 - turn)) continue;
                        int fi = getFwd(z);
                        int u = bval[b] * 1000 / (FD[fi][M.bcell[b]] + P[8]);
                        if (u > best) { best = u; bid = b; }
                    }
                }
                if (bid < 0) { fplans.push_back({z, fAvail[z], -1, z, false}); fAvail[z] = 0; break; }
                int dr = flagStep(z, bid);
                int dz = z;
                if (dr >= 0) dz = M.nbr[z][dr];
                fplans.push_back({z, 1, bid, dz, false});
                fAvail[z]--;
            }
        }

        PSEC(7);
        int wAvail[NC], reservedStay[NC], plannedArrive[NC];
        for (int c = 0; c < NC; c++) { wAvail[c] = myW[c]; reservedStay[c] = 0; plannedArrive[c] = fixedW[c]; }
        struct WMove { int s, d, n; };
        std::vector<WMove> wmoves; wmoves.reserve(64);
        struct Crit { int pri, z, need; };
        std::vector<Crit> crit; crit.reserve(48);

        for (int i = 0; i < nb; i++) {
            int b = bord[i];
            if (S.owner[b] != me) continue;
            int z = M.bcell[b];
            if (enemyFReach[z] > 0) {
                bool assault = myF[z] > 0;
                for (auto& fp : fplans) if (fp.src == z) {
                    int db = M.bat[fp.dest];
                    if (fp.dest == z || db < 0 || S.owner[db] == me) assault = false;
                }
                crit.push_back({(assault ? 70000 : 100000) + bval[b], z, enemyThreat[z] + 1 + PR.garrisonAdd});
            }
        }
        for (auto& fp : fplans) {
            int z = fp.dest;
            int bb = M.bat[z];
            if (bb >= 0) {
                int minEscort = (enW[z] > 0 || enemyThreat[z] > 0) ? (enemyThreat[z] + 1) : 1;
                if (S.owner[bb] == op) minEscort = std::max(minEscort, 2);
                crit.push_back({90000 + (enemyFReach[z] * 20), z, minEscort});
            } else if (enemyThreat[z] > 0) {
                crit.push_back({80000, z, enemyThreat[z] + 1});
            }
        }
        if (totalWv >= P[20]) {
            int ownVal[MAXB], nov = 0;
            for (int i = 0; i < nb; i++) { int b = bord[i]; if (S.owner[b] == me) ownVal[nov++] = b; }
            std::sort(ownVal, ownVal + nov, [&](int a, int b) { return bval[a] > bval[b]; });
            int cap = std::min(nov, totalWv >= P[21] ? 5 : 3);
            for (int i = 0; i < cap; i++) crit.push_back({30000 + bval[ownVal[i]], M.bcell[ownVal[i]], 1});
        }
        std::sort(crit.begin(), crit.end(), [&](const Crit& a, const Crit& b) {
            if (a.pri != b.pri) return a.pri > b.pri;
            return hk(a.z) < hk(b.z);
        });

        bool secured[NC];
        for (int c = 0; c < NC; c++) secured[c] = false;
        for (auto& c : crit) {
            int z = c.z; if (secured[z]) continue;
            int need = std::max(0, c.need - plannedArrive[z]);
            int capacity = wAvail[z];
            int srcs[4], nsrc = 0;
            for (int k = 0; k < 4; k++) {
                int q = M.nbr[z][OPPD[dirOrder[k]]];
                if (q >= 0) { srcs[nsrc++] = q; capacity += wAvail[q]; }
            }
            if (capacity < need) continue;
            int take = std::min(need, wAvail[z]);
            if (take) { reservedStay[z] += take; wAvail[z] -= take; plannedArrive[z] += take; need -= take; }
            for (int k = 0; k < nsrc; k++) {
                if (need <= 0) break;
                int ss = srcs[k];
                int n = std::min(need, wAvail[ss]);
                if (n) { wAvail[ss] -= n; wmoves.push_back({ss, z, n}); plannedArrive[z] += n; need -= n; }
            }
            if (need <= 0) secured[z] = true;
        }

        PSEC(8);
        int goalValue[NC], goalNeed[NC], goalAssigned[NC];
        for (int c = 0; c < NC; c++) { goalValue[c] = 0; goalNeed[c] = 0; goalAssigned[c] = plannedArrive[c]; }
        auto addGoal = [&](int z, int value, int need) {
            goalValue[z] = std::max(goalValue[z], value);
            goalNeed[z] = std::max(goalNeed[z], need);
        };
        int flagETA[NC];
        for (int c = 0; c < NC; c++) flagETA[c] = INF;
        for (int z = 0; z < NC; z++) if (myF[z]) for (int q = 0; q < NC; q++) flagETA[q] = std::min(flagETA[q], dst(z, q));
        // enemy stacks list (cord order)
        int estk[NC], nest = 0;
        for (int k = 0; k < NC; k++) { int z = cord[k]; if (enF[z] || enW[z]) estk[nest++] = z; }
        for (int i = 0; i < nest; i++) {
            int z = estk[i];
            if (enF[z]) addGoal(z, P[9] + 50 * enF[z], enemyThreat[z] + 3);
            if (enW[z]) addGoal(z, P[10] + std::min(180, 2 * enW[z]), enemyThreat[z] + P[15]);
        }
        for (int i = 0; i < nb; i++) {
            int b = bord[i];
            int z = M.bcell[b];
            int efDist = INF, ewLocal = 0;
            for (int j = 0; j < nest; j++) {
                int q = estk[j];
                int d = dst(z, q);
                if (enF[q]) efDist = std::min(efDist, d);
                if (d <= 2) ewLocal += enW[q];
            }
            if (S.owner[b] == me && efDist <= 3) addGoal(z, P[11] + bval[b] - efDist * P[12], std::max(2, ewLocal + 2));
            if (S.owner[b] != me) {
                int value = S.owner[b] == op ? P[13] + 2 * bval[b] : P[14] + bval[b];
                if (flagETA[z] > 3 && !enF[z]) value = value * 4 / (flagETA[z] + 1);
                addGoal(z, value, enemyThreat[z] + P[16]);
            }
        }
        if (PR.extraGoal >= 0) addGoal(PR.extraGoal, PR.extraGoalValue, PR.extraGoalNeed);
        PSEC(9);
        int goalCells[NC], ngoal = 0;
        for (int k = 0; k < NC; k++) { int z = cord[k]; if (goalValue[z] > 0) goalCells[ngoal++] = z; }
        int sources[NC], nsrcs = 0;
        for (int k = 0; k < NC; k++) { int z = cord[k]; if (wAvail[z] > 0) sources[nsrcs++] = z; }
        std::sort(sources, sources + nsrcs, [&](int a, int b) {
            if (wAvail[a] != wAvail[b]) return wAvail[a] > wAvail[b];
            return hk(a) < hk(b);
        });
        for (int si = 0; si < nsrcs; si++) {
            int s = sources[si];
            int left = wAvail[s];
            while (left > 0) {
                int tz = -1, best = -INF;
                for (int gi = 0; gi < ngoal; gi++) {
                    int z = goalCells[gi];
                    if (!(goalValue[z] > 0 && goalAssigned[z] < goalNeed[z])) continue;
                    int d = dst(s, z); if (d >= INF || (turn >= 145 && d > 161 - turn)) continue;
                    int utility = goalValue[z] * 100 / (d + P[17]);
                    if (utility > best) { best = utility; tz = z; }
                }
                bool overflow = false;
                if (tz < 0) {
                    overflow = true;
                    for (int i = 0; i < nest; i++) {
                        int z = estk[i];
                        int d = dst(s, z); if (d >= INF || (turn >= 145 && d > 161 - turn)) continue;
                        int utility = (enF[z] ? 1100 : 600) + std::min(300, 3 * enW[z]) - 25 * d;
                        if (utility > best) { best = utility; tz = z; }
                    }
                }
                if (tz < 0) { reservedStay[s] += left; plannedArrive[s] += left; break; }
                int n = overflow ? left : std::min(left, goalNeed[tz] - goalAssigned[tz]);
                int dz = s;
                if (s != tz) {
                    int bestStep = INF;
                    for (int k = 0; k < 4; k++) {
                        int q = M.nbr[s][dirOrder[k]]; if (q < 0) continue;
                        int dd = dst(q, tz);
                        int danger = std::max(0, enW[q] + enemySpawn[q] - (n + plannedArrive[q]));
                        int score = dd * P[18] + danger * P[19] - std::min(20, plannedArrive[q]) * 2 + (ps.prevWFrom[s] == q ? 9 : 0);
                        if (score < bestStep) { bestStep = score; dz = q; }
                    }
                }
                goalAssigned[tz] += n;
                if (dz == s) reservedStay[s] += n;
                else wmoves.push_back({s, dz, n});
                plannedArrive[dz] += n;
                left -= n;
            }
            wAvail[s] = 0;
        }

        PSEC(10);
        struct FMv { int s, d, n; };
        std::vector<FMv> fmoves; fmoves.reserve(16);
        int finalFStay[NC], finalFArrive[NC];
        for (int c = 0; c < NC; c++) { finalFStay[c] = 0; finalFArrive[c] = 0; }
        for (auto& fp : fplans) {
            int s = fp.src, bid = fp.target;
            if (fp.locked) { int bb = M.bat[s]; bid = bb; }
            if (bid < 0 || bid >= nb) bid = -1;
            int choices[5], chDir[5], nch = 0;
            choices[nch] = s; chDir[nch++] = -1;
            for (int k = 0; k < 4; k++) { int q = M.nbr[s][dirOrder[k]]; if (q >= 0) { choices[nch] = q; chDir[nch++] = dirOrder[k]; } }
            int best = INF, dest = s;
            int tcell = bid >= 0 ? M.bcell[bid] : -1;
            int fdS = -1;
            if (bid >= 0 && tcell != s) fdS = getFwd(s);
            for (int i = 0; i < nch; i++) {
                int q = choices[i];
                int deficit = std::max(0, enemyThreat[q] - plannedArrive[q]);
                int score = deficit * 100000;
                int qb = M.bat[q];
                if (deficit && qb >= 0 && S.owner[qb] == me) score += 50000;
                score += (q == s ? 7 : 0);
                if (fp.locked && q == s) score -= 40;
                if (q == fp.dest) score -= 4;
                score -= std::min(10, plannedArrive[q]);
                if (turn == 160) {
                    if (qb >= 0 && S.owner[qb] != me && (enemyFReach[q] == 0 || plannedArrive[q] > enemyThreat[q]))
                        score -= 1000 * expectedScore(qb);
                    if (q == s && qb >= 0 && S.owner[qb] == me && enemyFReach[q] && plannedArrive[q] <= enemyThreat[q])
                        score -= 1000 * expectedScore(qb);
                }
                if (bid >= 0) {
                    int rcv;
                    if (q == s) rcv = tcell == s ? 0 : FD[fdS][tcell];
                    else if (tcell == s) rcv = 10 + risk[s];      // D_q(s): every path ends by entering s
                    else {
                        int wq = 10 + risk[q];
                        int dS = FD[fdS][tcell];
                        if ((FM[fdS][tcell] >> chDir[i]) & 1) rcv = dS - wq;   // q lies on an optimal path
                        else {
                            int lb = std::max(dS - wq + 1, 10 * (int)M.dist[q][tcell]);
                            if (score + lb >= best) continue;                  // cannot beat the current best
                            computeRC(bid); rcv = rc[bid][q];
                        }
                    }
                    score += rcv;
                }
                if (score < best) { best = score; dest = q; }
            }
            if (dest == s) finalFStay[s] += fp.count;
            else { fmoves.push_back({s, dest, fp.count}); finalFArrive[dest] += fp.count; }
        }

        PSEC(11);
        int actualCaptureCost = 0;
        for (int i = 0; i < nb; i++) {
            int b = bord[i], z = M.bcell[b];
            if (S.owner[b] != me && finalFStay[z] + finalFArrive[z] > 0)
                actualCaptureCost += std::max(1, (M.btype[b] == T_PLAZA ? 4 : 2) - (libs > 0));
        }
        int requiredReserve = std::max(0, actualCaptureCost - expected_income);
        if (requiredReserve > needed_reserve && guard++ < 16) { reserveOverride = requiredReserve; continue; }

        // PRIORITY
        int pr[MAXB], npr = 0;
        for (int i = 0; i < nb; i++) {
            int b = bord[i], z = M.bcell[b];
            if (S.owner[b] != me && (finalFStay[z] + finalFArrive[z] > 0)) pr[npr++] = b;
        }
        std::sort(pr, pr + npr, [&](int a, int b) {
            int va = bval[a], vb = bval[b];
            if (va != vb) return va > vb;
            return hk(M.bcell[a]) < hk(M.bcell[b]);
        });
        for (int i = 0; i < npr; i++) out.addPrio(pr[i]);

        // moves: merged and sorted by (src,dst) as the original std::map iteration
        std::sort(wmoves.begin(), wmoves.end(), [](const WMove& a, const WMove& b) { return a.s != b.s ? a.s < b.s : a.d < b.d; });
        int prevCount[NC];
        for (int c = 0; c < NC; c++) { ps.prevWFrom[c] = -1; prevCount[c] = 0; }
        for (size_t i = 0; i < wmoves.size();) {
            size_t j = i; int n = 0;
            while (j < wmoves.size() && wmoves[j].s == wmoves[i].s && wmoves[j].d == wmoves[i].d) { n += wmoves[j].n; j++; }
            int s = wmoves[i].s, d = wmoves[i].d;
            if (n > 0) {
                int dir; dirBetween(s, d, dir);
                out.addMove(s, K_W, dir, n);
                if (n > prevCount[d] || (n == prevCount[d] && hk(s) < hk(ps.prevWFrom[d]))) { ps.prevWFrom[d] = (int16_t)s; prevCount[d] = n; }
            }
            i = j;
        }
        std::sort(fmoves.begin(), fmoves.end(), [](const FMv& a, const FMv& b) { return a.s != b.s ? a.s < b.s : a.d < b.d; });
        for (size_t i = 0; i < fmoves.size();) {
            size_t j = i; int n = 0;
            while (j < fmoves.size() && fmoves[j].s == fmoves[i].s && fmoves[j].d == fmoves[i].d) { n += fmoves[j].n; j++; }
            if (n > 0) { int dir; dirBetween(fmoves[i].s, fmoves[i].d, dir); out.addMove(fmoves[i].s, K_F, dir, n); }
            i = j;
        }
        PSEC(-1);
        if (dbgReserve) *dbgReserve = reserveOverride;
        return;
    }
}

inline void decide17(const Map& M, const Near& NR, const State& S, int me, const Params& PR, Persist& ps, Orders& out) {
    decide17T<true>(M, NR, S, me, PR, ps, out);
}
inline void decide17f(const Map& M, const Near& NR, const State& S, int me, const Params& PR, Persist& ps, Orders& out) {
    decide17T<false>(M, NR, S, me, PR, ps, out);
}

}  // namespace pol

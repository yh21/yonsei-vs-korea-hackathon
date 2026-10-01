// Base policy (lean port of the v16/v17 heuristics onto the simulator state) + W doctrine hooks.
#pragma once
#include "sim.hpp"
#include "ordsort.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace pol {
using namespace sim;
constexpr int INF = 1000000000;
// v30 parameter defaults. Production builds use fixed values and read no environment variables.
constexpr int dflt(int key, int d) {
    switch (key) {
        case 235: return 1; // engineering discount applies once per team
        case 236: return 20; // residual strategic value of each engineering building

        case 0: return 5;    // desired flags: t <= 15 (v25: 7)
        case 1: return 4;    // many targets left (v25: 5)
        case 2: return 3;    // some targets left (v25: 4)
        case 3: return 3;    // few targets left (v25: 3)
        case 47: return 1;   // search production alternatives (no spawn / convert flag spawn into warriors)
        case 351: return 75; // safety normalization when opponent prediction is uncertain
        case 350: return 1;  // v28: post-search flag safety repair (flag stacks never end a turn where more enemy warriors can reach than mine arrive, if a safe neighbour exists)
        case 237: return 1;  // exact terminal ordering: score, then accumulated occupation-turns, then unit value
        case 410: return 300; // v28 economy emergency (own ENG 0 while enemy has one / fewer HALLs than enemy): extra building value of the missing type
        case 411: return 200; //   flag target utility x200% for those buildings
        case 412: return 1;   //   warrior escort for flags heading there at any turn
        case 413: return 1;   //   one extra desired flag
        case 416: return 1;   //   HALL deficit counts as emergency too
        case 417: return 600; //   extra warrior goal value on those buildings
        case 418: return 5;   // v28: defence goal for my ENG/HALL starts when an enemy flag is within 5 (v27: 3)
        case 419: return 500; //   extra goal value of that defence
        case 420: return 3;   //   enemy warriors counted within 3 of the building (v27: 2)
        case 322: return 100; // v27: flag target utility x 100/(100 + 100*deficit) where deficit = enemy warriors reaching the building - my warriors reaching it (0 = off)
        case 67: return 1200;   // v28 (v27 default: see prm(67, ...) call site)
        case 68: return 2;   // v28 (v27 default: see prm(68, ...) call site)
        default: return d;
    }
}
constexpr int prm(int key, int d) { return dflt(key, d); }

// fixed-size int array indexed by int (std::array's size_t index would trigger -Wsign-conversion at ~100 sites); IA x{} zero-fills
struct IA {
    int a[MAXC];
    int& operator[](int i) { return a[i]; }
    const int& operator[](int i) const { return a[i]; }
    int* data() { return a; }
    const int* data() const { return a; }
};

// route-cost memo: shortest-path costs to every building are a pure function of the risk map, so identical risk maps
// (several opponent models / consecutive turns) share one computation.  Rows are filled lazily (bit b of `have`).
struct RouteMemo { int risk[MAXC]; int rc[MAXB][MAXC]; uint32_t have = 0; bool used = false; };
struct RouteCache { bool valid = false; RouteMemo* m = nullptr; };

struct Scan;

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
    int openEngMul = 0;   // v21 opening: pct multiplier on F-target util of unowned ENG while I own no ENG (t <= openT); 0 = off
    int openHallMul = 0;  // same for HALL while I own no HALL
    int openT = 12;
    int openEngMaxF = 2;  // at most this many flags per turn get the ENG boost
    int escValue = 0;     // v21 opening: >0 (t <= escT): W escort goal on target building of each F heading to unowned/enemy ENG/HALL
    int escNeed = 2;
    int escT = 30;
    int escD = 14;        // escorts are drawn from warrior stacks within escD steps (goalFirstD applies to normal goals)
    int escEng = 1;       // 1: ENG only, 2: ENG+HALL
    int ehScale = 0;      // v22: dynamic own ENG/HALL garrison goal: need = max(ehN, min(ehMax, 1 + enemyW within ehR * ehScale/100)); 0 = off
    int ehR = 4, ehMax = 10;
    int hospMul = 0, hospSide = 1, hospT = 20, hospReact = 0, escHosp = 0; // v25 (ported from v22 hallrace): reactive own-side hospital denial: F-target util x hospMul% (t <= hospT), only while an enemy flag is within hospReact of it (or enemy owns it); escHosp: W escort for flags heading there
    int recapBV = 0, recapGoal = 0, recapT = 6; // v22: recently lost ENG/HALL gets extra building/goal value for recapT turns
    int holdLateT = 0, ehOffT = 999; // v22: from holdLateT on holdValue is points-only; from ehOffT on the ENG/HALL crit/goal is dropped
    int teleDef = 0, teleR = 3; // v22: TELE reinforcement urgency for stations near threatened own ENG/HALL (0 = off; pct)
    int noBV = 0, noT = 3;                       // v22: ENG/HALL extra value while I own none of that type for >= noT turns
    int raid = 0;         // v24: % of W that may be diverted to raid parties on enemy/neutral ENG/HALL (0 = off)
    int raidMinT = 46, raidMaxNeed = 8, raidMinW = 20, raidMaxD = 20, raidExtra = 2, raidTypes = 1;
    int tgtPen = 0;       // v27 flag safety: flag target utility is divided by (1 + tgtPen/100 * deficit), deficit = enemy warriors that can reach the building - my warriors that can reach it (0 = off)
    // v28 economy emergency (FM1): I own no ENG while the enemy owns one (emEng), or fewer HALLs than the enemy (emHall) -> the missing type gets recapture priority
    int emBV = 0;         // extra buildingValue of unowned/enemy buildings of the missing type (0 = off)
    int emMul = 100;      // flag target utility multiplier (%) for those buildings
    int emEsc = 0;        // 1: warrior escort goal for flags heading to those buildings at any turn (not only t <= escT)
    int emF = 0;          // extra desired flags while in emergency
    int emMinT = 12, emMaxT = 110, emHall = 0;
    int emGoal = 0;       // extra warrior goal value on those buildings
    int defRE = 3, defWR = 2, defEH = 0; // v28: own ENG/HALL defence goal: enemy flag radius, enemy-warrior count radius, extra goal value (defRE=3/defWR=2/defEH=0 = v27)
    int lastN = 0;        // v28: extra garrison need on my only ENG / only HALL
    int emRaidT = 0;      // v28: >0: while in economy emergency warrior raids on ENG/HALL start at this turn (instead of raidMinT)
    int riskMul = 13, regMul = 3, defValue = 960, adaptF = 1; // lineage parameters (v17 defaults)
};

struct Doctrine {
    Cfg cfg;
    int me = 0;
    int reserveOverride = 0;
    int16_t prevWFrom[MAXC];
    int lostT[MAXB]; int noEngT = 0, noHallT = 0; // v22 memory (maintained by Searcher::noteOwn)
    Doctrine() { for (int i = 0; i < MAXC; i++) prevWFrom[i] = -1; for (int i = 0; i < MAXB; i++) lostT[i] = -1000; }

    // scratch (shared)
    static inline RouteMemo memo[8];
    static inline int memoNext = 0;
    static inline RouteCache rcache[2];
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
        if (!mine && (G.btype[b] == ENG || G.btype[b] == HALL)) {
            if (cfg.recapBV && s.turn - lostT[b] <= cfg.recapT) val += cfg.recapBV;
            if (cfg.noBV && ((G.btype[b] == ENG && noEngT >= cfg.noT) || (G.btype[b] == HALL && noHallT >= cfg.noT))) val += cfg.noBV;
        }
        int bx = G.cx(G.bcell[b]);
        if (bx >= 5 && bx <= 9) val += 15;
        if (s.turn > 120 && theirs) val += 70;
        return val;
    }

    // value of keeping an owned building (loss if flipped)
    int holdValue(const GS& s, int b) const {
        int ty = G.btype[b]; int v = expectedScore(b) * 4;
        int t = s.turn;
        if (cfg.holdLateT && t >= cfg.holdLateT) { if (ty == PLAZA) v += 60; int bx0 = G.cx(G.bcell[b]); if (bx0 >= 5 && bx0 <= 9) v += 15; return v; }
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
    int planOnce(const GS& s, Plan& out, int& neededReserveOut, const Scan& scn);

    void plan(const GS& s, int me_, Plan& out, const Scan* pre = nullptr);
};

// Dial shortest paths to `target` where stepping from q to a neighbour z costs 10 + risk[z] (exactly the old bucket search).
inline void routeRow(const int* risk, int target, int* dd) {
    const int N = G.N;
    for (int c = 0; c < N; c++) dd[c] = INF;
    int head[128]; for (int i = 0; i < 128; i++) head[i] = -1;
    static int pnode[1100], pnext[1100]; int np = 0;
    dd[target] = 0; pnode[0] = target; pnext[0] = -1; head[0] = 0; np = 1;
    int remaining = 1;
    for (int cur = 0; remaining > 0; cur++) {
        int idx = head[cur & 127]; head[cur & 127] = -1;
        while (idx >= 0) {
            int z = pnode[idx]; idx = pnext[idx]; remaining--;
            if (dd[z] != cur) continue;
            int w = cur + 10 + risk[z];
            const int16_t* nl = G.nl[z]; int nn = G.nln[z];
            for (int k = 1; k < nn; k++) { int q = nl[k]; if (w < dd[q]) { dd[q] = w; pnode[np] = q; pnext[np] = head[w & 127]; head[w & 127] = np++; remaining++; } }
        }
    }
}

// occupied-cell lists of both teams (ascending cell index), shared by the two plan calls of a rollout step
struct Scan {
    int16_t wl[2][MAXC], fl[2][MAXC]; int nw[2], nf[2], totW[2], totF[2];
};
inline void scanTeam(const Team& t, int16_t* wl, int& nw, int16_t* fl, int& nf, int& totW, int& totF) {
    const int N = G.N; int w_ = 0, f_ = 0, tw = 0, tf = 0; int c = 0;
    for (; c + 4 <= N; c += 4) {
        uint64_t x, y; std::memcpy(&x, &t.W[c], 8); std::memcpy(&y, &t.F[c], 8);
        if (!(x | y)) continue;
        for (int k = 0; k < 4; k++) { int w = t.W[c + k], f = t.F[c + k]; tw += w; tf += f; wl[w_] = (int16_t)(c + k); w_ += w != 0; fl[f_] = (int16_t)(c + k); f_ += f != 0; }
    }
    for (; c < N; c++) { int w = t.W[c], f = t.F[c]; tw += w; tf += f; wl[w_] = (int16_t)c; w_ += w != 0; fl[f_] = (int16_t)c; f_ += f != 0; }
    nw = w_; nf = f_; totW = tw; totF = tf;
}
inline void scanState(const GS& s, Scan& sc) {
    for (int t = 0; t < 2; t++) scanTeam(s.t[t], sc.wl[t], sc.nw[t], sc.fl[t], sc.nf[t], sc.totW[t], sc.totF[t]);
}
inline void insertSorted(int* l, int& n, int v) { int i = 0; while (i < n && l[i] < v) i++; if (i < n && l[i] == v) return; for (int j = n; j > i; j--) l[j] = l[j - 1]; l[i] = v; n++; }

inline int Doctrine::planOnce(const GS& s, Plan& out, int& neededReserveOut, const Scan& scn) {
    const int op = 1 - me;
    const Team& Mt = s.t[me]; const Team& Et = s.t[op];
    const int N = G.N;
    const bool flipMe = G.flip[me];
    int dirs[4]; dirOrder(dirs);
    const int16_t* enF = Et.F; const int16_t* enW = Et.W;
    IA myF, myW;
    for (int c = 0; c < N; c++) { myF[c] = Mt.F[c]; myW[c] = Mt.W[c]; }
    int totalF = scn.totF[me], totalW = scn.totW[me], oppWTotal = scn.totW[op];
    // occupied cells, ascending (mine are kept up to date when spawns are added)
    int mwl[MAXC], nmw = scn.nw[me], mfl[MAXC], nmf = scn.nf[me];
    for (int i = 0; i < nmw; i++) mwl[i] = scn.wl[me][i];
    for (int i = 0; i < nmf; i++) mfl[i] = scn.fl[me][i];
    const int16_t* ewl = scn.wl[op]; const int newc = scn.nw[op];
    const int16_t* efl = scn.fl[op]; const int nef = scn.nf[op];
    auto ordAt = [&](const int* l, int n, int i) { return flipMe ? l[n - 1 - i] : l[i]; };
    const int* bOrder = G.bOrd[me];
    const int NB = G.nB;

    // ---------------- threats
    IA enemyThreat{}, enemyFReach{}, enemySpawn{};
    int enemySites[16]; int nSites = 0;
    enemySites[nSites++] = G.base[op];
    bool enemyEng = false; int enemyEngN = 0, enemyHallN = 0;
    for (int b = 0; b < NB; b++) if (s.own[b] == op + 1) {
        if (G.btype[b] == ENG) { enemyEng = true; enemyEngN++; }
        if (G.btype[b] == HALL) enemyHallN++;
        if (G.btype[b] == HOSPITAL && nSites < 16) enemySites[nSites++] = G.bcell[b];
    }
    for (int i = 0; i < newc; i++) { int q = ewl[i]; int w = enW[q]; const int16_t* nl = G.nl[q]; for (int k = 0; k < G.nln[q]; k++) enemyThreat[nl[k]] += w; }
    for (int i = 0; i < nef; i++) { int q = efl[i]; int f = enF[q]; const int16_t* nl = G.nl[q]; for (int k = 0; k < G.nln[q]; k++) enemyFReach[nl[k]] += f; }
    {
        int sp = Et.res / (enemyEng ? 2 : 3);
        if (sp > 0) for (int i = 0; i < nSites; i++) for (int k = 0; k < G.nln[enemySites[i]]; k++) {
            int z = G.nl[enemySites[i]][k];
            if (enemySpawn[z] == 0) { enemySpawn[z] = sp; if (G.pass[z]) enemyThreat[z] += sp; }
        }
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
    // per-building values (state is constant during a planning call)
    int bv[MAXB];
    for (int b = 0; b < NB; b++) bv[b] = buildingValue(s, b);
    const bool emOn = (cfg.emBV | cfg.emEsc | cfg.emF | cfg.emGoal | cfg.emRaidT) != 0 || cfg.emMul != 100;
    const bool emEng = emOn && s.turn >= cfg.emMinT && s.turn < cfg.emMaxT && eng == 0 && enemyEngN >= 1;
    const bool emHall = emOn && cfg.emHall && s.turn >= cfg.emMinT && s.turn < cfg.emMaxT && halls < enemyHallN;
    auto emKind = [&](int b) -> bool { return s.own[b] != me + 1 && ((emEng && G.btype[b] == ENG) || (emHall && G.btype[b] == HALL)); };
    if (emEng || emHall) for (int b = 0; b < NB; b++) if (emKind(b)) bv[b] += cfg.emBV;
    int hv[MAXB]; bool hvOk = false;
    auto ensureHV = [&]() { if (!hvOk) { hvOk = true; for (int b = 0; b < NB; b++) if (s.own[b] == me + 1) hv[b] = holdValue(s, b); } };

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
    if (emEng || emHall) desiredF += cfg.emF;

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

    auto targetUtil = [&](int sc, int kind) -> int { // kind 0 = F, 1 = W
        int best = -INF; const uint8_t* dsc = G.dist[sc];
        for (int b = 0; b < NB; b++) {
            if (kind == 0 && s.own[b] == me + 1) continue;
            int d = dsc[G.bcell[b]]; if (d >= 255) continue;
            int val = bv[b];
            if (kind == 1) { if (enF[G.bcell[b]]) val += 220; if (s.own[b] == me + 1) val += 20; }
            int util = val * 100 / (d + 2);
            if (util > best) best = util;
        }
        return best;
    };
    auto bestSpawnSite = [&](int kind) -> int {
        int best = -INF; int site = G.base[me];
        for (int i = 0; i < nSpawn; i++) {
            int sc = spawnSites[i];
            int u = targetUtil(sc, kind);
            if (kind == 0) u -= std::max(0, enemyThreat[sc] - myW[sc]) * 3000;
            else {
                const uint8_t* dsc = G.dist[sc];
                for (int fi = 0; fi < nmf; fi++) { int q = mfl[fi]; if (myF[q] && dsc[q] <= 2) u += std::min(20, enemyThreat[q]) * 160 / (dsc[q] + 1); }
            }
            if (u > best) { best = u; site = sc; }
        }
        return site;
    };
    auto emitSpawn = [&](int kind, int n, int site) {
        if (n <= 0) return;
        out.addSpawn(kind, site, n);
        if (kind == 0) { myF[site] += n; totalF += n; insertSorted(mfl, nmf, site); } else if (kind == 1) { myW[site] += n; totalW += n; insertSorted(mwl, nmw, site); }
    };
    if (makeF > 0) emitSpawn(0, makeF, bestSpawnSite(0));
    if (makeW > 0) emitSpawn(1, makeW, bestSpawnSite(1));

    // ---------------- teleport
    IA fixedW{};
    if (stations >= 2) {
        int ownSt[MAXB], nst = 0;
        for (int i = 0; i < NB; i++) { int b = bOrder[i]; if (s.own[b] == me + 1 && G.btype[b] == STATION) ownSt[nst++] = b; }
        int src = -1, dst = -1, sendN = 0, bestUrg = -1;
        for (int i = 0; i < nst; i++) {
            int b = ownSt[i]; int bz = G.bcell[b], threatF = 0, threatW = 0;
            const uint8_t* dbz = G.dist[bz];
            for (int j = 0; j < nef; j++) { int z = efl[j]; int d = dbz[z]; if (d <= 4) threatF += enF[z] * (5 - d); }
            for (int j = 0; j < newc; j++) { int z = ewl[j]; int d = dbz[z]; if (d <= 3) threatW += enW[z] * (4 - d); }
            if (cfg.teleDef > 0) {
                for (int j = 0; j < NB; j++) if (s.own[j] == me + 1 && (G.btype[j] == ENG || G.btype[j] == HALL) && G.dist[bz][G.bcell[j]] <= cfg.teleR) {
                    int zz = G.bcell[j], ew = 0, mw = 0;
                    for (int q = 0; q < N; q++) { int dq = G.dist[zz][q]; if (dq <= 4) ew += enW[q]; if (dq <= 2) mw += myW[q]; }
                    if (ew > mw) threatW += std::min(20, ew - mw) * cfg.teleDef / 100;
                }
            }
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
            int gm[MAXB];
            for (int i = 0; i < nst; i++) { int g = INF; for (int q = 0; q < NB; q++) if (s.own[q] != me + 1) g = std::min<int>(g, G.dist[G.bcell[ownSt[i]]][G.bcell[q]]); gm[i] = g; }
            for (int i = 0; i < nst; i++) { int a = ownSt[i]; if (myW[G.bcell[a]] > 1) {
                for (int k = 0; k < nst; k++) { if (i == k) continue;
                    int gain = gm[i] - gm[k];
                    if (gain > bestGain) { bestGain = gain; src = a; dst = ownSt[k]; sendN = std::min(5, myW[G.bcell[a]] - 1); }
                } } }
        }
        if (src >= 0 && dst >= 0 && sendN > 0) {
            out.teleSrc = (int16_t)G.bcell[src]; out.teleDst = (int16_t)G.bcell[dst]; out.teleKind = 1; out.teleN = (int16_t)sendN;
            myW[G.bcell[src]] -= sendN; fixedW[G.bcell[dst]] += sendN;
        }
    }

    // ---------------- risk map and routes
    IA riskLoc; const int* risk; RouteMemo* rm_ = nullptr;
    if (cacheMode == 2 && rcache[me].valid) {
        rm_ = rcache[me].m; risk = rm_->risk;
    } else {
        IA friendlyReach{}, regionDiff{};
        for (int i = 0; i < nmw; i++) { int q = mwl[i]; int w = myW[q]; const int16_t* nl = G.nl[q]; for (int k = 0; k < G.nln[q]; k++) friendlyReach[nl[k]] += w; }
        for (int i = 0; i < newc; i++) { int q = ewl[i]; int df = enW[q] - myW[q]; if (df) for (int k = 0; k < G.nball3[q]; k++) regionDiff[G.ball3[q][k]] += df; }
        for (int i = 0; i < nmw; i++) { int q = mwl[i]; if (enW[q] == 0) { int df = -myW[q]; if (df) for (int k = 0; k < G.nball3[q]; k++) regionDiff[G.ball3[q][k]] += df; } }
        const int rm = cfg.riskMul, gm2 = cfg.regMul;
        for (int z = 0; z < N; z++) riskLoc[z] = std::min(prm(4, 48), std::max(0, enemyThreat[z] - friendlyReach[z]) * rm) + std::min(prm(6, 18), std::max(0, regionDiff[z]) * gm2);
        RouteMemo* m = nullptr;
        for (int i = 0; i < 8; i++) if (memo[i].used && std::memcmp(memo[i].risk, riskLoc.data(), sizeof(int) * (size_t)N) == 0) { m = &memo[i]; break; }
        if (!m) {
            int v = memoNext;
            for (int tries = 0; tries < 8; tries++) { RouteMemo* c = &memo[v]; if (c != rcache[0].m && c != rcache[1].m) break; v = (v + 1) & 7; }
            m = &memo[v]; memoNext = (v + 1) & 7;
            std::memcpy(m->risk, riskLoc.data(), sizeof(int) * (size_t)N); m->have = 0; m->used = true;
        }
        if (cacheMode == 1) { rcache[me].m = m; rcache[me].valid = true; }
        rm_ = m; risk = m->risk;
    }
    // route cost rows are computed on first use (they depend on the risk map only)
    auto rcRow = [&](int b) -> const int* {
        if (!(rm_->have >> b & 1)) { routeRow(rm_->risk, G.bcell[b], rm_->rc[b]); rm_->have |= 1u << b; }
        return rm_->rc[b];
    };
    auto flagStep = [&](int z, int bid) -> int {
        int answer = -1; const int* rr = rcRow(bid); int best = rr[z];
        for (int k = 0; k < 4; k++) { int dir = dirs[k]; int q = G.nb[z][dir]; if (q < 0) continue; int score = rr[q] + risk[q]; if (score < best) { best = score; answer = dir; } }
        return answer;
    };

    struct FMove { int src, count, target, dest; bool locked; };
    static std::vector<FMove> fplans; fplans.clear(); IA fAvail = myF; bool occupied[MAXB] = {};
    for (int i = 0; i < NB; i++) {
        int b = bOrder[i]; int z = G.bcell[b];
        if (s.own[b] != me + 1 && fAvail[z] > 0) { fplans.push_back({z, 1, z, z, true}); fAvail[z]--; occupied[b] = true; }
    }
    auto hospSideOk = [&](int b) -> bool {
        if (G.btype[b] != HOSPITAL) return false;
        int c = G.bcell[b]; int dm = G.dist[G.base[me]][c], dp = G.dist[G.base[op]][c];
        if (cfg.hospSide == 1) return dm < dp;
        if (cfg.hospSide == 3) return dm <= dp;
        if (cfg.hospSide == 2) return dm > dp;
        return true;
    };
    auto hospActive = [&](int b) -> bool {
        if (!hospSideOk(b)) return false;
        if (cfg.hospReact <= 0 || s.own[b] == op + 1) return true;
        int c = G.bcell[b];
        for (int q = 0; q < N; q++) if (enF[q] && G.dist[q][c] <= cfg.hospReact) return true;
        return false;
    };
    auto hospMulOf = [&](int b) -> int { return (cfg.hospMul && s.turn <= cfg.hospT && hospActive(b)) ? cfg.hospMul : 100; };
    int tPenPct[MAXB]; // percent multiplier (<= 100) applied to the flag utility of each building
    for (int b = 0; b < NB; b++) {
        tPenPct[b] = 100;
        if (cfg.tgtPen > 0) {
            int z = G.bcell[b]; int th = enemyThreat[z];
            if (th > 0) {
                int reach = 0; for (int k = 0; k < G.nln[z]; k++) reach += myW[G.nl[z][k]];
                int def = th - reach; if (def > 0) tPenPct[b] = 10000 / (100 + cfg.tgtPen * def);
            }
        }
    }
    int targetIds[MAXB], nT = 0;
    for (int i = 0; i < NB; i++) { int b = bOrder[i]; if (s.own[b] != me + 1 && !occupied[b]) targetIds[nT++] = b; }
    struct PairCand { int64_t key; int src, bid; }; // key: util descending, then home order of the source (single integer compare; ties stay ties: ordered by ord::sort, see ordsort.hpp)
    static std::vector<PairCand> cand; cand.clear();
    for (int fi = 0; fi < nmf; fi++) { int z = ordAt(mfl, nmf, fi); if (fAvail[z] > 0) {
        const uint8_t* dz = G.dist[z];
        for (int i = 0; i < nT; i++) { int bid = targetIds[i]; int d = dz[G.bcell[bid]]; if (d >= 255 || (s.turn >= 140 && d > 161 - s.turn)) continue;
            int uu = bv[bid] * 1000 / (rcRow(bid)[z] + prm(8, 25));
            if (cfg.hospMul) uu = uu * hospMulOf(bid) / 100;
            if (cfg.tgtPen) uu = uu * tPenPct[bid] / 100;
            if (cfg.emMul != 100 && (emEng || emHall) && emKind(bid)) uu = uu * cfg.emMul / 100;
            if (s.turn <= cfg.openT) {
                if (cfg.openEngMul && eng == 0 && G.btype[bid] == ENG) uu = uu * cfg.openEngMul / 100;
                if (cfg.openHallMul && halls == 0 && G.btype[bid] == HALL) uu = uu * cfg.openHallMul / 100;
            }
            cand.push_back({((int64_t)(0x40000000 - (int64_t)uu) << 32) | G.homeKey(me, z), z, bid}); } } }
    ord::sort(cand.data(), cand.data() + cand.size(), [](const PairCand& a, const PairCand& b) { return a.key < b.key; });
    bool usedBid[MAXB] = {};
    int engTaken = 0;
    for (auto& fp : fplans) if (fp.locked && G.btype[G.bAt[fp.src]] == ENG) engTaken++;
    const bool engBoostOn = cfg.openEngMul && s.turn <= cfg.openT && eng == 0;
    for (auto& c : cand) {
        if (fAvail[c.src] <= 0 || usedBid[c.bid]) continue;
        if (engBoostOn && G.btype[c.bid] == ENG) { if (engTaken >= cfg.openEngMaxF) continue; engTaken++; }
        int x = c.src; int d = flagStep(c.src, c.bid); int dz = c.src; if (d >= 0) dz = G.nb[x][d];
        fplans.push_back({c.src, 1, c.bid, dz, false}); fAvail[c.src]--; usedBid[c.bid] = true;
    }
    for (int fi = 0; fi < nmf; fi++) { int z = ordAt(mfl, nmf, fi);
        int cBid = -2, cDir = -2; // without the ENG opening boost every further flag of this cell gets the same choice
        while (fAvail[z] > 0) {
        int bid;
        if (!engBoostOn && cBid != -2) bid = cBid;
        else {
        int best = -INF; bid = -1;
        for (int i = 0; i < NB; i++) { int b = bOrder[i]; if (s.own[b] != me + 1) {
            int d = G.dist[z][G.bcell[b]]; if (d >= 255 || (s.turn >= 140 && d > 161 - s.turn)) continue;
            int u = bv[b] * 1000 / (rcRow(b)[z] + prm(8, 25));
            if (cfg.hospMul) u = u * hospMulOf(b) / 100;
            if (cfg.tgtPen) u = u * tPenPct[b] / 100;
            if (cfg.emMul != 100 && (emEng || emHall) && emKind(b)) u = u * cfg.emMul / 100;
            if (s.turn <= cfg.openT) {
                if (engBoostOn && engTaken < cfg.openEngMaxF && G.btype[b] == ENG) u = u * cfg.openEngMul / 100;
                if (cfg.openHallMul && halls == 0 && G.btype[b] == HALL) u = u * cfg.openHallMul / 100;
            }
            if (u > best) { best = u; bid = b; } } }
        cBid = bid;
        }
        if (bid < 0) { fplans.push_back({z, fAvail[z], -1, z, false}); fAvail[z] = 0; break; }
        if (engBoostOn && G.btype[bid] == ENG) engTaken++;
        int d;
        if (!engBoostOn && cDir != -2) d = cDir; else { d = flagStep(z, bid); cDir = d; }
        int dz = z; if (d >= 0) dz = G.nb[z][d];
        fplans.push_back({z, 1, bid, dz, false}); fAvail[z]--;
    } }

    // ---------------- warriors
    IA wAvail = myW, reservedStay{}, plannedArrive = fixedW;
    struct WMove { int s, d, n; };
    static std::vector<WMove> wmoves; wmoves.clear();
    struct Crit { int pri, z, need; };
    static std::vector<Crit> crit; crit.clear();
    for (int i = 0; i < NB; i++) { int b = bOrder[i]; if (s.own[b] == me + 1) {
        int z = G.bcell[b];
        if (enemyFReach[z] > 0) {
            bool assault = myF[z] > 0;
            for (auto& fp : fplans) if (fp.src == z) {
                int dst = G.bAt[fp.dest];
                if (fp.dest == z || dst < 0 || s.own[dst] == me + 1) assault = false;
            }
            crit.push_back({(assault ? 70000 : 100000) + bv[b], z, enemyThreat[z] + 1});
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
        int ownVal[MAXB], nov = 0;
        for (int i = 0; i < NB; i++) { int b = bOrder[i]; if (s.own[b] == me + 1) ownVal[nov++] = b; }
        // equal values: order fixed by ord::sort (library independent, identical to the clang/libc++ order this bot was tuned with)
        if (cfg.holdVal) { ensureHV(); ord::sort(ownVal, ownVal + nov, [&](int a, int b) { return hv[a] > hv[b]; }); }
        else ord::sort(ownVal, ownVal + nov, [&](int a, int b) { return bv[a] > bv[b]; });
        int cap = std::min<int>(nov, (totalW >= prm(21, 65) ? 5 : 3) + cfg.garrisonN);
        for (int i = 0; i < cap; i++) crit.push_back({30000 + (cfg.holdVal ? hv[ownVal[i]] : bv[ownVal[i]]), G.bcell[ownVal[i]], 1});
    }
    if (cfg.ehN > 0 && totalW >= cfg.ehMinW && s.turn < cfg.ehOffT) {
        for (int i = 0; i < NB; i++) { int b = bOrder[i]; if (s.own[b] == me + 1 && (G.btype[b] == ENG || G.btype[b] == HALL)) crit.push_back({cfg.ehPri + bv[b], G.bcell[b], cfg.ehN + ((G.btype[b] == ENG ? eng : halls) == 1 ? cfg.lastN : 0)}); }
    }
    ord::sort(crit.data(), crit.data() + crit.size(), [&](const Crit& a, const Crit& b) {
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
    IA escG{};
    int gcells[MAXC]; int ngc = 0;
    auto addGoal = [&](int z, int value, int need) { if (goalValue[z] == 0 && value > 0) gcells[ngc++] = z; goalValue[z] = std::max(goalValue[z], value); goalNeed[z] = std::max(goalNeed[z], need); };
    if (cfg.escValue > 0 && (s.turn <= cfg.escT || (cfg.emEsc && (emEng || emHall)))) {
        for (auto& fp : fplans) {
            int b = fp.locked ? G.bAt[fp.src] : fp.target;
            if (b < 0 || b >= NB || s.own[b] == me + 1) continue;
            if (s.turn > cfg.escT && !emKind(b)) continue;
            int ty = G.btype[b]; if (!(ty == ENG || (cfg.escEng >= 2 && ty == HALL) || (cfg.escHosp && ty == HOSPITAL && hospActive(b)))) continue;
            int z = G.bcell[b]; if (!fp.locked && G.dist[fp.src][z] > 14) continue;
            addGoal(z, cfg.escValue, std::max(cfg.escNeed, enemyThreat[z] + 1)); escG[z] = 1;
        }
    }
    for (int i = 0; i < nef; i++) { int z = efl[i]; addGoal(z, cfg.huntValue + 50 * enF[z], enemyThreat[z] + cfg.huntNeed); }
    for (int i = 0; i < newc; i++) { int z = ewl[i]; addGoal(z, prm(10, 150) + std::min(180, 2 * enW[z]), enemyThreat[z] + prm(15, 3)); }
    for (int i = 0; i < NB; i++) {
        int b = bOrder[i]; int z = G.bcell[b];
        const uint8_t* dzr = G.dist[z];
        if (cfg.ehGoal > 0 && cfg.ehN > 0 && totalW >= cfg.ehMinW && s.turn < cfg.ehOffT && s.own[b] == me + 1 && (G.btype[b] == ENG || G.btype[b] == HALL)) {
            int need = cfg.ehN + ((G.btype[b] == ENG ? eng : halls) == 1 ? cfg.lastN : 0);
            if (cfg.ehScale > 0) { int ew = 0; for (int q = 0; q < N; q++) if (enW[q] && G.dist[z][q] <= cfg.ehR) ew += enW[q]; if (ew > 0) need = std::max(need, std::min(cfg.ehMax, 1 + ew * cfg.ehScale / 100)); }
            addGoal(z, cfg.ehGoal, need);
        }
        if (s.own[b] == me + 1) {
            int efDist = INF; for (int k = 0; k < nef; k++) efDist = std::min<int>(efDist, dzr[efl[k]]);
            const bool ehB = G.btype[b] == ENG || G.btype[b] == HALL;
            if (efDist <= (ehB ? cfg.defRE : 3)) {
                int wr = ehB ? cfg.defWR : 2;
                int ewLocal = 0; for (int k = 0; k < newc; k++) { int q = ewl[k]; if (dzr[q] <= wr) ewLocal += enW[q]; }
                addGoal(z, cfg.defValue + bv[b] - efDist * prm(12, 150) + (ehB ? cfg.defEH : 0), std::max(2, ewLocal + 2));
            }
        } else {
            int value = s.own[b] == op + 1 ? prm(13, 500) + 2 * bv[b] : prm(14, 350) + bv[b];
            int fEta = INF; for (int k = 0; k < nmf; k++) fEta = std::min<int>(fEta, dzr[mfl[k]]);
            if (fEta > 3 && !enF[z]) value = value * 4 / (fEta + 1);
            if (cfg.ehRaid && (G.btype[b] == ENG || G.btype[b] == HALL)) value += cfg.ehRaid;
            if (cfg.recapGoal && (G.btype[b] == ENG || G.btype[b] == HALL) && s.turn - lostT[b] <= cfg.recapT) value += cfg.recapGoal;
            if (cfg.hospRaid && G.btype[b] == HOSPITAL) value += cfg.hospRaid;
            if (cfg.emGoal && (emEng || emHall) && emKind(b)) value += cfg.emGoal;
            addGoal(z, value, enemyThreat[z] + prm(16, 2));
        }
    }
    if (cfg.dispatch > 0 && totalW >= cfg.dispatchMinW) {
        int ov[MAXB], nov = 0;
        for (int i = 0; i < NB; i++) { int b = bOrder[i]; if (s.own[b] == me + 1) ov[nov++] = b; }
        ensureHV();
        ord::sort(ov, ov + nov, [&](int a, int b) { return hv[a] > hv[b]; });
        int cap = std::min<int>(nov, cfg.dispatch);
        for (int i = 0; i < cap; i++) {
            int z = G.bcell[ov[i]]; int need = 1;
            if (cfg.dispatchScale > 0) {
                int ew5 = 0; for (int k = 0; k < newc; k++) { int q = ewl[k]; if (G.dist[z][q] <= 5) ew5 += enW[q]; }
                need = std::min(cfg.dispatchMax, 1 + ew5 * cfg.dispatchScale / 100);
            }
            if (myW[z] + plannedArrive[z] < need) addGoal(z, cfg.dispatchBase + hv[ov[i]], need);
        }
    }
    auto stepToward = [&](int sc, int tz, int n) -> int {
        int dz = sc;
        if (sc != tz) {
            int bestStep = INF; const uint8_t* dtz = G.dist[tz]; // symmetric: dist[q][tz] == dist[tz][q]
            for (int k = 0; k < 4; k++) { int dir = dirs[k]; int q = G.nb[sc][dir]; if (q < 0) continue;
                int dd = dtz[q]; int danger = std::max(0, enW[q] + enemySpawn[q] - (n + plannedArrive[q]));
                int score = dd * prm(18, 30) + danger * prm(19, 85) - std::min(20, plannedArrive[q]) * 2 + (prevWFrom[sc] == q ? 9 : 0);
                if (score < bestStep) { bestStep = score; dz = q; } }
        }
        return dz;
    };
    // goal cells in orientation order
    std::sort(gcells, gcells + ngc);
    if (flipMe) std::reverse(gcells, gcells + ngc);
    // warrior source cells (orientation order) with warriors left after the critical assignments
    int wSrc[MAXC], nws = 0;
    for (int i = 0; i < nmw; i++) { int sc = ordAt(mwl, nmw, i); if (wAvail[sc] > 0) wSrc[nws++] = sc; }
    if (cfg.raid > 0 && totalW >= cfg.raidMinW && s.turn >= (((emEng || emHall) && cfg.emRaidT > 0) ? std::min(cfg.raidMinT, cfg.emRaidT) : cfg.raidMinT) && nws > 0) {
        struct RT { int z, val, need; }; RT rts[MAXB]; int nrt = 0;
        for (int i = 0; i < NB; i++) { int b = bOrder[i]; if (s.own[b] == me + 1) continue;
            int ty = G.btype[b];
            bool ok = ty == ENG || ty == HALL || (cfg.raidTypes >= 2 && ty == HOSPITAL) || (cfg.raidTypes >= 3 && ty != DEPOT && ty != PLAZA) || cfg.raidTypes >= 4;
            if (!ok) continue;
            int z = G.bcell[b]; int need = enemyThreat[z] + cfg.raidExtra;
            if (need > cfg.raidMaxNeed) continue;
            RT r{z, bv[b] + (ty == ENG ? 50 : 0), need}; int j = nrt++; while (j > 0 && rts[j - 1].val < r.val) { rts[j] = rts[j - 1]; j--; } rts[j] = r; }
        int budgetW = totalW * cfg.raid / 100;
        for (int ri = 0; ri < nrt; ri++) { const RT& rt = rts[ri];
            if (budgetW <= 0) break;
            int need = rt.need - goalAssigned[rt.z]; if (need <= 0) continue;
            int bs = -1, bd = INF; const uint8_t* dr = G.dist[rt.z];
            for (int jj = 0; jj < nws; jj++) { int sc = wSrc[jj]; if (wAvail[sc] < need) continue;
                int d = dr[sc]; if (d >= 255 || d > cfg.raidMaxD || (s.turn >= 140 && d > 161 - s.turn)) continue;
                if (d < bd || (d == bd && wAvail[sc] > wAvail[bs])) { bd = d; bs = sc; } }
            if (bs < 0) continue;
            int n = std::min(wAvail[bs], std::min(need, budgetW));
            if (n < need) continue;
            int dz = stepToward(bs, rt.z, n);
            goalAssigned[rt.z] += n; budgetW -= n;
            if (dz == bs) reservedStay[bs] += n; else wmoves.push_back({bs, dz, n});
            plannedArrive[dz] += n; wAvail[bs] -= n;
        }
        { int k = 0; for (int jj = 0; jj < nws; jj++) if (wAvail[wSrc[jj]] > 0) wSrc[k++] = wSrc[jj]; nws = k; }
    }
    if (cfg.goalFirst) {
        int gl[MAXC]; int ngl = ngc;
        for (int i = 0; i < ngc; i++) { int g = gcells[i]; int j = i; while (j > 0 && goalValue[gl[j - 1]] < goalValue[g]) { gl[j] = gl[j - 1]; j--; } gl[j] = g; } // stable, goalValue descending
        for (int gi = 0; gi < ngl && nws > 0; gi++) {
            int g = gl[gi];
            if (goalAssigned[g] >= goalNeed[g]) continue;
            const uint8_t* dg = G.dist[g]; const int maxD = escG[g] ? cfg.escD : cfg.goalFirstD;
            // candidate sources in range, keyed (distance asc, stack size desc, orientation order asc): the same order in which the
            // repeated "nearest, then biggest" scans pick them (only the chosen source changes between scans)
            int64_t ck[MAXC]; int nck = 0;
            for (int jj = 0; jj < nws; jj++) { int sc = wSrc[jj];
                int d = dg[sc]; if (d >= 255 || d > maxD || (s.turn >= 145 && d > 161 - s.turn)) continue;
                ck[nck++] = ((int64_t)d << 40) | ((int64_t)(0xFFFFF - wAvail[sc]) << 20) | jj; }
            bool removed = false;
            while (goalAssigned[g] < goalNeed[g]) {
                int bi = -1; int64_t bk = INT64_MAX;
                for (int i = 0; i < nck; i++) if (ck[i] < bk) { bk = ck[i]; bi = i; }
                if (bi < 0) break;
                int jj = (int)(bk & 0xFFFFF); int bs = wSrc[jj];
                ck[bi] = ck[--nck];
                int n = std::min(wAvail[bs], goalNeed[g] - goalAssigned[g]);
                int dz = stepToward(bs, g, n);
                goalAssigned[g] += n;
                if (dz == bs) reservedStay[bs] += n; else wmoves.push_back({bs, dz, n});
                plannedArrive[dz] += n; wAvail[bs] -= n;
                if (wAvail[bs] <= 0) { wSrc[jj] = -1; removed = true; }
            }
            if (removed) { int k = 0; for (int jj = 0; jj < nws; jj++) if (wSrc[jj] >= 0) wSrc[k++] = wSrc[jj]; nws = k; }
        }
    }
    // sources: cells still holding warriors, biggest stack first (ties: home order = list order)
    int sources[MAXC], nsrc = 0;
    for (int i = 0; i < nws; i++) { int z = wSrc[i]; if (wAvail[z] <= 0) continue; int j = nsrc++; while (j > 0 && wAvail[sources[j - 1]] < wAvail[z]) { sources[j] = sources[j - 1]; j--; } sources[j] = z; }
    int ug[MAXC], nug = 0; // unsatisfied goal cells (orientation order)
    for (int i = 0; i < ngc; i++) { int z = gcells[i]; if (goalAssigned[z] < goalNeed[z]) ug[nug++] = z; }
    int enCellsOrd[MAXC], nenO = 0; bool enBuilt = false;
    for (int si = 0; si < nsrc; si++) {
        int sc = sources[si];
        int left = wAvail[sc]; const uint8_t* dsc = G.dist[sc];
        while (left > 0) {
            int tz = -1, best = -INF;
            for (int jj = 0; jj < nug; jj++) { int z = ug[jj];
                int d = dsc[z]; if (d >= 255 || (s.turn >= 145 && d > 161 - s.turn)) continue;
                int utility = goalValue[z] * 100 / (d + prm(17, 4)); if (utility > best) { best = utility; tz = z; } }
            bool overflow = false;
            if (tz < 0 && !cfg.rally) {
                overflow = true;
                if (!enBuilt) { // enemy cells (W or F) in orientation order
                    enBuilt = true; int a = 0, b2 = 0;
                    while (a < newc || b2 < nef) { int x = a < newc ? ewl[a] : INF, y = b2 < nef ? efl[b2] : INF; int v = std::min(x, y); enCellsOrd[nenO++] = v; if (x == v) a++; if (y == v) b2++; }
                    if (flipMe) std::reverse(enCellsOrd, enCellsOrd + nenO);
                }
                for (int jj = 0; jj < nenO; jj++) { int z = enCellsOrd[jj]; {
                    int d = dsc[z]; if (d >= 255 || (s.turn >= 145 && d > 161 - s.turn)) continue;
                    int utility = (enF[z] ? 1100 : 600) + std::min(300, 3 * enW[z]) - 25 * d; if (utility > best) { best = utility; tz = z; } } }
            }
            if (tz < 0 && cfg.rally) {
                int bz = -1, bn = 0;
                for (int j = 0; j < N; j++) { int z = flipMe ? N - 1 - j : j; if (myW[z] > bn) { bn = myW[z]; bz = z; } }
                if (bz >= 0 && bz != sc && G.dist[sc][bz] < 255) { tz = bz; overflow = true; }
            }
            if (tz < 0) { reservedStay[sc] += left; plannedArrive[sc] += left; break; }
            int n = overflow ? left : std::min(left, goalNeed[tz] - goalAssigned[tz]);
            int dz = stepToward(sc, tz, n);
            goalAssigned[tz] += n;
            if (dz == sc) reservedStay[sc] += n; else wmoves.push_back({sc, dz, n});
            plannedArrive[dz] += n; left -= n;
            if (goalAssigned[tz] >= goalNeed[tz]) { for (int j = 0; j < nug; j++) if (ug[j] == tz) { for (int k = j; k + 1 < nug; k++) ug[k] = ug[k + 1]; nug--; break; } }
        }
        wAvail[sc] = 0;
    }

    // ---------------- final flag destinations
    struct FMoveO { int s, d, n; };
    static std::vector<FMoveO> fmoves; fmoves.clear(); IA finalFStay{}, finalFArrive{};
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
            score += bid >= 0 ? rcRow(bid)[q] : 0;
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
        int pr[MAXB], npr = 0;
        for (int i = 0; i < NB; i++) { int b = bOrder[i]; int z = G.bcell[b]; if (s.own[b] != me + 1 && (finalFStay[z] + finalFArrive[z] > 0)) pr[npr++] = b; }
        std::sort(pr, pr + npr, [&](int a, int b) {
            if (bv[a] != bv[b]) return bv[a] > bv[b];
            return G.homeKey(me, G.bcell[a]) < G.homeKey(me, G.bcell[b]);
        });
        for (int i = 0; i < npr; i++) if (out.nprio < MAXB) out.prio[out.nprio++] = (int8_t)pr[i];
    }
    // merge moves
    {
        // sort by (src, dst); equal keys are summed afterwards so their relative order is irrelevant
        auto sortMoves = [](auto* a, int n) { for (int i = 1; i < n; i++) { auto v = a[i]; int j = i; while (j > 0 && (a[j - 1].s > v.s || (a[j - 1].s == v.s && a[j - 1].d > v.d))) { a[j] = a[j - 1]; j--; } a[j] = v; } };
        int nw_ = (int)wmoves.size();
        sortMoves(wmoves.data(), nw_);
        static WMove wm[MAXC * 4 + 8]; int nwm = 0;
        for (int i = 0; i < nw_; i++) { const WMove& m = wmoves[(size_t)i]; if (m.n > 0) { if (nwm > 0 && wm[nwm - 1].s == m.s && wm[nwm - 1].d == m.d) wm[nwm - 1].n += m.n; else wm[nwm++] = m; } }
        // v17 orders map keys by (src cell index, dst cell index) in absolute coordinates
        for (int i = 0; i < N; i++) prevWFrom[i] = -1;
        static int prevWCount[MAXC]; for (int i = 0; i < N; i++) prevWCount[i] = 0;
        for (int i = 0; i < nwm; i++) { const WMove& m = wm[i];
            out.addMove(m.s, 1, dirBetween(m.s, m.d), m.n);
            int d = m.d, sc = m.s, n = m.n;
            if (n > prevWCount[d] || (n == prevWCount[d] && (prevWFrom[d] < 0 || G.homeKey(me, sc) < G.homeKey(me, prevWFrom[d])))) { prevWFrom[d] = (int16_t)sc; prevWCount[d] = n; }
        }
        int nf_ = (int)fmoves.size();
        sortMoves(fmoves.data(), nf_);
        static FMoveO fm2[MAXC * 4 + 8]; int nfm = 0;
        for (int i = 0; i < nf_; i++) { const FMoveO& m = fmoves[(size_t)i]; if (m.n > 0) { if (nfm > 0 && fm2[nfm - 1].s == m.s && fm2[nfm - 1].d == m.d) fm2[nfm - 1].n += m.n; else fm2[nfm++] = m; } }
        for (int i = 0; i < nfm; i++) out.addMove(fm2[i].s, 0, dirBetween(fm2[i].s, fm2[i].d), fm2[i].n);
    }
    return requiredReserve;
}

inline void Doctrine::plan(const GS& s, int me_, Plan& out, const Scan* pre) {
        me = me_;
        reserveOverride = 0;
        static Scan local; const Scan* sc = pre; if (!sc) { scanState(s, local); sc = &local; }
        for (int it = 0; it < 6; it++) {
            out.clear();
            int needed = 0;
            int req = planOnce(s, out, needed, *sc);
            if (req > needed) { reserveOverride = req; continue; }
            break;
        }
    }
} // namespace pol

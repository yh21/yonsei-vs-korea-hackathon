// turtle: defensive / economy-first zoo bot (written from scratch for v18 testing).
// Style: grab the economy buildings (ENG, HALL, DEPOT, LIB, HOSPITAL) with escorted flag units,
// keep a garrison of warriors on every owned building sized by nearby enemy pressure,
// pull all surplus warriors into ONE doom-stack that only marches when it clearly out-masses
// the defenders of a target, and let flags trail the stack. C++20, std only.
#include "protocol.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <vector>
using namespace std;

namespace {
constexpr int INF = 1000000000;
enum Type { PLAZA, HALL, STATION, LIB, ENG, HOSP, WATCH, DEPOT };

#ifdef TUNE
int prm(int i, int d) {
    static int c[64]; static bool init[64];
    if (!init[i]) { init[i] = true; char n[8]; snprintf(n, 8, "P%d", i); const char* e = getenv(n); c[i] = e ? atoi(e) : d; }
    return c[i];
}
#else
constexpr int prm(int, int d) { return d; }
#endif

FILE* LG = nullptr;
bool lgInit = false;
inline FILE* lg() { if (!lgInit) { lgInit = true; const char* e = getenv("TURTLE_LOG"); if (e) LG = fopen(e, "a"); } return LG; }
#define LOGF(...) do { if (lg()) { fprintf(lg(), __VA_ARGS__); fflush(lg()); } } while (0)
struct Mv { int s, d, n; };
struct Result {
    vector<Mv> wm, fm;
    vector<int> fEnd;
    int capCost = 0;
    vector<pair<int,int>> unmet;   // (cell, deficit) hard demands that could not be covered
    int armyCell = -1;             // cell of army target building (-1 none)
    bool advance = false;
    int hub = -1;
    int freeTargets = 0;
    int bc = -1;
    vector<pair<int,int>> fAssign;   // (destination cell, target building) per flag
    int engCapNow = 0;             // an F ends on an unowned ENG while we own none
};

struct Bot {
    bool ready = false;
    int W = 15, H = 15, N = 225;
    string me, opp;
    bool flip = false;
    vector<vector<int>> dist;
    vector<vector<int>> nbr;
    vector<vector<int>> byDist;
    vector<char> pass;
    vector<int> known, sym;
    vector<char> depotMine;
    vector<int> bcell, btype, bAt;
    int nb = 0;
    int baseZ = 0, ebaseZ = 0;
    vector<string> dirName;
    vector<pair<int,int>> dirDelta;

    // per turn
    int T = 0, R = 0, oppR = 0;
    vector<int> owner, stage;   // owner: 0 neutral, 1 me, 2 opp
    vector<int> myF, myW, eF, eW;
    vector<int> E1, EF1, E23, E4, eSmall, eCl;
    int engs = 0, halls = 0, libs = 0, stations = 0, eEngs = 0;
    vector<int> hospCells;
    int prevBC = -1;
    vector<vector<int>> fMem;   // per cell: buildings targeted by flags that ended last turn there

    int hk(int z) const { return flip ? N - 1 - z : z; }
    static int clampi(int v, int lo, int hi) { return max(lo, min(hi, v)); }
    bool isMine(int b) const { return owner[b] == 1 && stage[b] == 2; }

    void init_once(const p::Init& init) {
        if (ready) return;
        ready = true; W = init.width; H = init.height; N = W * H; me = init.team; opp = init.opp;
        flip = init.base().first * 2 > W - 1;
        dirName = flip ? vector<string>{"D", "U", "R", "L"} : vector<string>{"U", "D", "L", "R"};
        for (auto& d : dirName) dirDelta.push_back(p::delta(d));
        pass.assign(N, 0);
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) pass[y * W + x] = init.passable(x, y);
        nbr.assign(N, {});
        for (int z = 0; z < N; z++) if (pass[z]) for (auto [dx, dy] : dirDelta) {
            int x = z % W + dx, y = z / W + dy;
            if (init.passable(x, y)) nbr[z].push_back(y * W + x);
        }
        dist.assign(N, vector<int>(N, INF));
        for (int s = 0; s < N; s++) if (pass[s]) {
            queue<int> q; dist[s][s] = 0; q.push(s);
            while (!q.empty()) {
                int v = q.front(); q.pop();
                for (int u : nbr[v]) if (dist[s][u] > dist[s][v] + 1) { dist[s][u] = dist[s][v] + 1; q.push(u); }
            }
        }
        byDist.assign(N, {});
        for (int z = 0; z < N; z++) if (pass[z]) {
            for (int q = 0; q < N; q++) if (pass[q] && dist[z][q] < INF) byDist[z].push_back(q);
            sort(byDist[z].begin(), byDist[z].end(), [&](int a, int b) {
                if (dist[z][a] != dist[z][b]) return dist[z][a] < dist[z][b];
                return hk(a) < hk(b);
            });
        }
        nb = init.buildings.size();
        int maxid = 0; for (auto& b : init.buildings) maxid = max(maxid, b.id);
        nb = maxid + 1;
        bcell.assign(nb, 0); btype.assign(nb, 0); known.assign(nb, -1); sym.assign(nb, -1); depotMine.assign(nb, 0);
        bAt.assign(N, -1);
        map<pair<int,int>, int> at;
        for (auto& b : init.buildings) {
            bcell[b.id] = b.y * W + b.x; bAt[bcell[b.id]] = b.id; at[{b.x, b.y}] = b.id;
            int t = 0;
            if (b.type == "PLAZA") t = PLAZA; else if (b.type == "HALL") t = HALL; else if (b.type == "STATION") t = STATION;
            else if (b.type == "LIBRARY") t = LIB; else if (b.type == "ENG") t = ENG; else if (b.type == "HOSPITAL") t = HOSP;
            else if (b.type == "WATCH") t = WATCH; else if (b.type == "DEPOT") t = DEPOT;
            btype[b.id] = t;
            if (t == PLAZA) known[b.id] = 3;
        }
        for (auto& b : init.buildings) { auto it = at.find({W - 1 - b.x, H - 1 - b.y}); if (it != at.end()) sym[b.id] = it->second; }
        auto mb = init.bases[init.team == "Y" ? 0 : 1], eb = init.bases[init.team == "Y" ? 1 : 0];
        baseZ = mb.second * W + mb.first; ebaseZ = eb.second * W + eb.first;
    }

    void readState(const p::View& v) {
        T = v.turn; R = v.my_resource; oppR = v.opp_resource;
        owner.assign(nb, 0); stage.assign(nb, 0);
        engs = halls = libs = stations = eEngs = 0; hospCells.clear();
        for (auto& b : v.buildings) {
            if (b.id >= nb) continue;
            if (b.score >= 0) { known[b.id] = b.score; if (sym[b.id] >= 0) known[sym[b.id]] = b.score; }
            owner[b.id] = b.owner == me ? 1 : (b.owner == opp ? 2 : 0);
            stage[b.id] = b.stage;
            if (btype[b.id] == DEPOT && owner[b.id] == 1) depotMine[b.id] = 1;
            if (owner[b.id] == 1 && b.stage == 2) {
                if (btype[b.id] == ENG) engs++;
                if (btype[b.id] == HALL) halls++;
                if (btype[b.id] == LIB) libs++;
                if (btype[b.id] == STATION) stations++;
                if (btype[b.id] == HOSP) hospCells.push_back(bcell[b.id]);
            }
            if (owner[b.id] == 2 && btype[b.id] == ENG) eEngs++;
        }
        myF.assign(N, 0); myW.assign(N, 0); eF.assign(N, 0); eW.assign(N, 0);
        for (auto& u : v.units) {
            int z = u.y * W + u.x;
            if (u.team == me) { if (u.kind == "F") myF[z] += u.count; else if (u.kind == "W") myW[z] += u.count; }
            else { if (u.kind == "F") eF[z] += u.count; else if (u.kind == "W") eW[z] += u.count; }
        }
        // enemy cluster masses (radius 1) and the part of the enemy that is in small raiding parties
        eCl.assign(N, 0); eSmall.assign(N, 0);
        for (int z = 0; z < N; z++) if (pass[z]) { eCl[z] = eW[z]; for (int q : nbr[z]) eCl[z] += eW[q]; }
        for (int z = 0; z < N; z++) if (pass[z] && eW[z] > 0 && eCl[z] <= prm(6, 14)) eSmall[z] = eW[z];
        // enemy threat maps
        E1.assign(N, 0); EF1.assign(N, 0); E23.assign(N, 0); E4.assign(N, 0);
        for (int z = 0; z < N; z++) if (pass[z]) for (int q : byDist[z]) {
            int d = dist[z][q];
            if (d > 4) break;
            if (d <= 1) { E1[z] += eW[q]; EF1[z] += eF[q]; }
            else if (d <= 3) E23[z] += eW[q];
            else E4[z] += eW[q];
        }
        int ecost = max(2, 3 - eEngs);
        int espawn = min(12, oppR / ecost);
        vector<int> esites; esites.push_back(ebaseZ);
        for (auto& b : v.buildings) if (b.owner == opp && b.stage == 2 && btype[b.id] == HOSP) esites.push_back(bcell[b.id]);
        if (espawn > 0) for (int s : esites) for (int z : nbr[s]) E1[z] += espawn;
        // enemy teleport potential
        vector<int> est;
        for (auto& b : v.buildings) if (b.owner == opp && b.stage == 2 && btype[b.id] == STATION) est.push_back(bcell[b.id]);
        if (est.size() >= 2) for (int z : est) {
            int extra = 0; for (int q : est) if (q != z && dist[q][z] > 1) extra += eW[q];
            E1[z] += min(5, extra);
        }
    }

    // ---------------------------------------------------------------- valuation
    int expScore10(int b) const {
        if (known[b] >= 0) return known[b] * 10;
        if (btype[b] == PLAZA) return 30;
        int x = bcell[b] % W;
        return (x >= 5 && x <= 9) ? 25 : 15;
    }
    int bval(int b) const {
        int v = expScore10(b) * 3;
        int t = T;
        switch (btype[b]) {
            case HALL: v += t < 110 ? prm(0, 110) : (t < 140 ? 40 : 0); break;
            case ENG: v += engs == 0 ? (t < 120 ? prm(1, 150) : 30) : 15; break;
            case LIB: v += libs == 0 ? (t < 120 ? 60 : 10) : 10; break;
            case DEPOT: v += depotMine[b] ? 0 : (t < 110 ? prm(2, 90) : 25); break;
            case HOSP: v += t < 120 ? prm(3, 70) : 20; break;
            case STATION: v += t < 120 ? 25 : 10; break;
            case WATCH: v += 15; break;
            case PLAZA: v += 30; break;
        }
        if (owner[b] == 2) v = v * 13 / 10 + 20;
        return v;
    }
    int gdesired(int b) const {
        int z = bcell[b];
        int g = T >= 6 ? 1 : 0;
        if (T >= 30 && E23[z] + E4[z] > 0 && (btype[b] == HALL || btype[b] == ENG || btype[b] == HOSP || btype[b] == DEPOT)) g += 1;
        return g;
    }
    int capCostOf(int b) const {
        int c = btype[b] == PLAZA ? 4 : 2;
        if (libs > 0) c -= 1;
        return max(1, c);
    }

    // ---------------------------------------------------------------- planning
    Result plan(const vector<int>& mW0, const vector<int>& mF0) {
        Result res;
        static int planCount = 0; bool logOn = lg() && (++planCount % 2 == 0);
        vector<int> avail = mW0, present(N, 0), committed(N, 0);
        vector<int> movedOut(N, 0);
        res.fEnd.assign(N, 0);

        auto addMove = [&](int s, int d, int n) {
            if (n <= 0) return;
            present[d] += n;
            if (s != d) res.wm.push_back({s, d, n});
        };
        auto supplyNear = [&](int z) {
            int s = avail[z];
            for (int q : nbr[z]) s += avail[q];
            return s;
        };
        // one step from s toward goal for a group of n units
        auto stepToward = [&](int s, int goal, int n, bool fight) -> int {
            if (s == goal) return s;
            int ds = dist[s][goal];
            int best = s;
            long bestSc = 1000L * (fight ? 0 : max(0, E1[s] - present[s] - n)) + 10L * ds + 5;
            for (int nbz : nbr[s]) {
                int d2 = dist[nbz][goal];
                if (d2 >= ds) continue;
                int danger = (fight && nbz == goal) ? 0 : max(0, E1[nbz] - present[nbz] - n);
                long sc = 1000L * danger + 10L * d2 - min(6, present[nbz]);
                if (sc < bestSc) { bestSc = sc; best = nbz; }
            }
            return best;
        };
        auto pullNear = [&](int z, int need) -> int {
            int got = 0;
            int t = min(need, avail[z]);
            if (t > 0) { avail[z] -= t; addMove(z, z, t); committed[z] += t; got += t; need -= t; }
            vector<int> ns = nbr[z];
            sort(ns.begin(), ns.end(), [&](int a, int b) { if (avail[a] != avail[b]) return avail[a] > avail[b]; return hk(a) < hk(b); });
            for (int q : ns) {
                if (need <= 0) break;
                t = min(need, avail[q]);
                if (t > 0) { avail[q] -= t; addMove(q, z, t); committed[z] += t; got += t; need -= t; }
            }
            return got;
        };
        auto pullFar = [&](int z, int need, int maxD) -> int {
            int got = 0;
            for (int q : byDist[z]) {
                if (need <= 0) break;
                int d = dist[z][q]; if (d > maxD) break;
                int t = min(need, avail[q]); if (t <= 0) continue;
                avail[q] -= t; need -= t; got += t; committed[z] += t;
                if (d == 0) addMove(z, z, t);
                else if (d == 1) addMove(q, z, t);
                else { int nx = stepToward(q, z, t, false); addMove(q, nx, t); }
            }
            return got;
        };

        // ---- T1: hard garrisons on owned buildings under direct threat
        struct Dem { int z, need, pri; };
        vector<Dem> hard;
        for (int b = 0; b < nb; b++) if (owner[b] == 1) {
            int z = bcell[b], need = 0;
            if (EF1[z] > 0) need = E1[z] + 1; else if (myF[z] > 0 && E1[z] > 0) need = E1[z] + 1;
            if (need > 0) hard.push_back({z, need, bval(b) + (stage[b] == 2 ? 1000 : 0)});
        }
        sort(hard.begin(), hard.end(), [&](const Dem& a, const Dem& b) { if (a.pri != b.pri) return a.pri > b.pri; return hk(a.z) < hk(b.z); });
        for (auto& d : hard) {
            int sup = supplyNear(d.z);
            if (sup >= d.need) pullNear(d.z, d.need);
            else res.unmet.push_back({d.z, d.need - sup});
        }
        // ---- stay reservations: each owned building keeps its garrison
        vector<int> gdes(nb, 0);
        for (int b = 0; b < nb; b++) if (owner[b] == 1) {
            gdes[b] = gdesired(b);
            int z = bcell[b];
            int want = gdes[b] - committed[z];
            if (want > 0) { int t = min(want, avail[z]); if (t > 0) { avail[z] -= t; addMove(z, z, t); committed[z] += t; } }
        }

        // ---- flag planning
        vector<char> taken(nb, 0);
        vector<int> fLeft = mF0;
        auto hardNeed = [&](int q) { return E1[q] > 0 ? E1[q] + 1 : 0; };
        auto commitF = [&](int s, int d, int softEscort) {
            int need = max(hardNeed(d), softEscort);
            if (need > 0) {
                // escorts: prefer units already at the flag's cell, then neighbors of the destination
                int got = 0;
                int t = min(need, avail[s]);
                if (t > 0 && dist[s][d] <= 1) { avail[s] -= t; addMove(s, d, t); committed[d] += t; got += t; }
                if (got < need) pullNear(d, need - got);
            }
            if (d != s) res.fm.push_back({s, d, 1});
            res.fEnd[d]++;
        };
        auto safeNeed = [&](int q) { int hn = hardNeed(q); return hn == 0 || supplyNear(q) >= hn; };
        // 1) flags already standing on an unowned building keep capturing (or flee if hopeless)
        for (int z = 0; z < N; z++) if (fLeft[z] > 0 && bAt[z] >= 0 && !isMine(bAt[z]) && !taken[bAt[z]]) {
            int b = bAt[z]; taken[b] = 1; fLeft[z]--;
            int dest = z;
            if (!safeNeed(z)) {
                int bestSc = INF;
                for (int q : nbr[z]) if (safeNeed(q)) { int sc = E1[q] * 10 + hk(q) % 7; if (sc < bestSc) { bestSc = sc; dest = q; } }
            }
            commitF(z, dest, (T >= 3 ? 1 : 0) + (owner[b] == 2 ? 1 : 0));
            res.fAssign.push_back({dest, dest == z ? b : -1});
        }
        // 2) free flags: keep last turn's target when still valid, otherwise greedy (flag, building) matching
        auto okTarget = [&](int zF, int b, long* util, bool ignoreTaken = false) -> bool {
            if ((taken[b] && !ignoreTaken) || isMine(b)) return false;
            int bc = bcell[b]; int d = dist[zF][bc]; if (d >= INF) return false;
            if (T >= 140 && d > 160 - T) return false;
            int grp = 0; for (int q : byDist[zF]) { if (dist[zF][q] > 2) break; grp += mW0[q]; }
            int e = E1[bc] + E23[bc];
            int loc = 0; for (int q : byDist[bc]) { if (dist[bc][q] > 2) break; loc += mW0[q]; }
            if (e > 0 && max(grp, loc) < e + 1 + (owner[b] == 2 ? 2 : 0)) return false;
            if (eF[bc] > 0 && max(grp, loc) < E1[bc] + 2) return false;
            long u = (long)bval(b) * 100 / (d + 3);
            int safety = dist[ebaseZ][bc] - dist[baseZ][bc];
            u = u * (safety >= -3 && safety <= 3 ? prm(10, 130) : (safety < -3 ? 60 : 100)) / 100;
            if (util) *util = u;
            return true;
        };
        struct FA { int z, b; };
        vector<FA> fas;
        // remembered assignments
        for (int z = 0; z < N; z++) if (fLeft[z] > 0 && z < (int)fMem.size()) {
            for (int b : fMem[z]) {
                if (fLeft[z] <= 0) break;
                if (b < 0 || taken[b] || isMine(b)) continue;
                if (!okTarget(z, b, nullptr, true)) continue;
                taken[b] = 1; fLeft[z]--; fas.push_back({z, b});
            }
        }
        // new assignments: greedy by utility
        {
            struct P { long u; int z, b; };
            vector<P> ps;
            for (int z = 0; z < N; z++) if (fLeft[z] > 0) for (int b = 0; b < nb; b++) { long u; if (okTarget(z, b, &u)) ps.push_back({u, z, b}); }
            sort(ps.begin(), ps.end(), [&](const P& a, const P& b) { if (a.u != b.u) return a.u > b.u; if (a.z != b.z) return hk(a.z) < hk(b.z); return a.b < b.b; });
            for (auto& q : ps) if (fLeft[q.z] > 0 && !taken[q.b]) { taken[q.b] = 1; fLeft[q.z]--; fas.push_back({q.z, q.b}); }
            for (int z = 0; z < N; z++) while (fLeft[z] > 0) { fLeft[z]--; fas.push_back({z, -1}); }
        }
        sort(fas.begin(), fas.end(), [&](const FA& a, const FA& b) { if (a.z != b.z) return hk(a.z) < hk(b.z); return a.b < b.b; });
        int hubF = -1;
        {
            double hw = -1;
            for (int b = 0; b < nb; b++) if (owner[b] == 1) {
                int z = bcell[b];
                int thr = 0; for (int q : byDist[z]) { if (dist[z][q] > prm(11, 5)) break; thr += eW[q]; }
                double w = bval(b) * (1.0 + min(4.0, thr / 10.0));
                if (w > hw) { hw = w; hubF = z; }
            }
        }
        for (auto& fa : fas) {
            int z = fa.z, b = fa.b, dest = z;
            if (b >= 0) {
                int tc = bcell[b];
                if (z != tc) {
                    int bestSc = INF; int ds = dist[z][tc];
                    for (int q : nbr[z]) {
                        if (dist[q][tc] >= ds) continue;
                        if (!safeNeed(q)) continue;
                        int sc = E1[q] * 20 - min(8, present[q] + avail[q]) * 2 + hk(q) % 5;
                        if (sc < bestSc) { bestSc = sc; dest = q; }
                    }
                }
            } else if (E1[z] > 0 || !safeNeed(z)) {
                int bestSc = INF;
                for (int q : nbr[z]) if (safeNeed(q)) { int sc = E1[q] * 10 + hk(q) % 7; if (sc < bestSc) { bestSc = sc; dest = q; } }
            } else if (T >= 10 && hubF >= 0 && dist[z][hubF] > 1 && dist[z][hubF] < INF) {
                int bestSc = INF;
                for (int q : nbr[z]) if (dist[q][hubF] < dist[z][hubF] && safeNeed(q)) { int sc = dist[q][hubF] * 10 + E1[q] * 20 + hk(q) % 5; if (sc < bestSc) { bestSc = sc; dest = q; } }
            }
            if (logOn) LOGF("  T%d F@%d,%d -> tgt %d dest %d,%d\n", T, z % W, z / W, b, dest % W, dest / W);
            commitF(z, dest, (T >= 3 ? 1 : 0) + (b >= 0 && owner[b] == 2 ? 1 : 0));
            res.fAssign.push_back({dest, b});
        }
        for (int b = 0; b < nb; b++) { long u; if (okTarget(baseZ, b, &u) && owner[b] != 2) res.freeTargets++; }
        // capture cost bookkeeping
        for (int b = 0; b < nb; b++) if (!isMine(b) && res.fEnd[bcell[b]] > 0) {
            res.capCost += capCostOf(b);
            if (btype[b] == ENG && engs == 0) res.engCapNow = 1;
        }

        // ---- anticipatory garrisons: an enemy FLAG is the only thing that can take a building, so
        // size the garrison by the enemy warriors that can travel with a flag arriving at time tF.
        {
            vector<int> order;
            for (int b = 0; b < nb; b++) if (owner[b] == 1 || (!isMine(b) && res.fEnd[bcell[b]] > 0)) order.push_back(b);
            sort(order.begin(), order.end(), [&](int a, int b) {
                int va = bval(a), vb = bval(b);
                if (va != vb) return va > vb;
                return a < b;
            });
            for (int b : order) {
                int z = bcell[b];
                int A = max(gdes[b], (!isMine(b)) ? 1 : 0);
                int tF = INF;
                for (int f = 0; f < N; f++) if (eF[f] > 0 && dist[f][z] < tF) tF = dist[f][z];
                if (tF >= 2 && tF <= 5) {
                    int md = 0, sup = 0;
                    for (int q : byDist[z]) {
                        int d = dist[z][q]; if (d > tF + prm(12, 0)) break;
                        if (d <= tF) md += eW[q];
                        sup += avail[q];
                    }
                    if (sup + committed[z] >= md + 1) A = max(A, md + 1 + (tF >= 3 ? 1 : 0));
                }
                int want = A - committed[z];
                if (want > 0) {
                    int got = pullFar(z, want, (tF >= 2 && tF <= 5) ? max(tF, 2) + prm(12, 0) : 6);
                    if (got < want && gdes[b] > committed[z]) res.unmet.push_back({z, want - got});
                }
            }
        }
        // ---- strikes: crush enemy stacks/flags we can out-mass this turn
        {
            struct St { int e; long pr; };
            vector<St> sts;
            for (int e = 0; e < N; e++) if (pass[e] && (eW[e] > 0 || eF[e] > 0)) {
                if (dist[e][ebaseZ] <= 1) continue;
                int dm = INF;
                for (int b = 0; b < nb; b++) if (owner[b] == 1) dm = min(dm, dist[e][bcell[b]]);
                long pr = (bAt[e] >= 0 && owner[bAt[e]] == 1 ? 5000 : 0) + 400L * eF[e] + 2L * eW[e] + max(0, 5 - dm) * 50;
                sts.push_back({e, pr});
            }
            sort(sts.begin(), sts.end(), [&](const St& a, const St& b) { if (a.pr != b.pr) return a.pr > b.pr; return hk(a.e) < hk(b.e); });
            for (auto& st : sts) {
                int e = st.e;
                int need = E1[e] + 1 + min(3, E23[e] / 8);
                int reach = supplyNear(e);
                if (reach < need) continue;
                bool worth = eF[e] > 0 || (bAt[e] >= 0 && owner[bAt[e]] == 1) || st.pr >= 100 || reach * 10 >= need * 15;
                if (!worth) continue;
                pullNear(e, min(reach, need + need / 5 + 1));
            }
        }
        // ---- objectives: kill enemy flags / take enemy buildings, but only with enough mass, arriving together
        {
            struct Ob { int cell; int R; double pri; };
            vector<Ob> objs;
            vector<int> myHold;
            for (int b = 0; b < nb; b++) if (owner[b] == 1) myHold.push_back(bcell[b]);
            for (int e = 0; e < N; e++) if (pass[e] && eF[e] > 0) {
                if (dist[e][ebaseZ] <= 2) continue;
                double Eloc = E1[e] + 0.5 * E23[e];
                int R = (int)ceil(1.2 * Eloc) + 2;
                int dm = INF; for (int z : myHold) dm = min(dm, dist[e][z]);
                double V = 300 + 80 * min(eF[e], 4) + (dm <= 1 ? 400 : (dm <= 3 ? 150 : 0));
                objs.push_back({e, R, V / (1.0 + 0.06 * R)});
            }
            for (int b = 0; b < nb; b++) if (owner[b] == 2 && stage[b] == 2 && eF[bcell[b]] == 0) {
                int e = bcell[b];
                if (dist[e][ebaseZ] <= 1) continue;
                double Eloc = E1[e] + 0.5 * E23[e];
                int R = (int)ceil(1.25 * Eloc) + 2;
                double V = bval(b) + 50;
                objs.push_back({e, R, 0.5 * V / (1.0 + 0.06 * R)});
            }
            sort(objs.begin(), objs.end(), [&](const Ob& a, const Ob& b) { if (a.pri != b.pri) return a.pri > b.pri; return hk(a.cell) < hk(b.cell); });
            const int DMAX = prm(8, 7);
            for (auto& o : objs) {
                int total = 0;
                for (int q : byDist[o.cell]) { if (dist[o.cell][q] > DMAX) break; total += avail[q]; }
                if (total < o.R) continue;
                int take = min(total, o.R + o.R / 5 + 1);
                struct G { int q, n, d; };
                vector<G> gs;
                int left = take, dfar = 0, near1 = 0;
                for (int q : byDist[o.cell]) {
                    if (left <= 0) break;
                    int d = dist[o.cell][q]; if (d > DMAX) break;
                    int t = min(left, avail[q]); if (t <= 0) continue;
                    gs.push_back({q, t, d}); left -= t; dfar = max(dfar, d); if (d <= 1) near1 += t;
                }
                for (auto& g : gs) {
                    avail[g.q] -= g.n;
                    int dest = g.q;
                    if (g.d == 0) dest = g.q;
                    else if (g.d == 1) dest = (near1 >= o.R || dfar <= 1) ? o.cell : g.q;
                    else if (g.d >= dfar - 1) dest = stepToward(g.q, o.cell, g.n, g.d <= 2);
                    addMove(g.q, dest, g.n);
                }
            }
        }
        // ---- reserves: spread the remaining warriors over our buildings according to local enemy pressure
        {
            struct Rs { int b, z; int level; double w; };
            vector<Rs> rs;
            int hubB = -1; double hubW = -1;
            for (int b = 0; b < nb; b++) if (owner[b] == 1 || (!isMine(b) && res.fEnd[bcell[b]] > 0)) {
                int z = bcell[b];
                int thr = 0; for (int q : byDist[z]) { if (dist[z][q] > prm(11, 5)) break; thr += eW[q]; }
                int level = clampi(2 + (int)(prm(9, 50) / 100.0 * thr), 2, 40);
                double w = bval(b) * (1.0 + min(4.0, thr / 10.0));
                rs.push_back({b, z, level, w});
                if (w > hubW) { hubW = w; hubB = b; }
            }
            int hub = hubB >= 0 ? bcell[hubB] : baseZ;
            res.hub = hub;
            vector<int> srcs;
            for (int q : byDist[baseZ]) if (avail[q] > 0) srcs.push_back(q);
            sort(srcs.begin(), srcs.end(), [&](int a, int b) { if (avail[a] != avail[b]) return avail[a] > avail[b]; return hk(a) < hk(b); });
            for (int q : srcs) {
                while (avail[q] > 0) {
                    int bestI = -1; double bestS = 0;
                    for (int i = 0; i < (int)rs.size(); i++) {
                        int need = rs[i].level - committed[rs[i].z]; if (need <= 0) continue;
                        int d = dist[q][rs[i].z]; if (d >= INF) continue;
                        double sc = rs[i].w * (1.0 + min(need, 10) / 10.0) / (d + 2);
                        if (sc > bestS) { bestS = sc; bestI = i; }
                    }
                    if (bestI < 0) {
                        int n = avail[q]; avail[q] = 0;
                        int dest = stepToward(q, hub, n, false);
                        addMove(q, dest, n);
                        committed[hub] += 0;
                        break;
                    }
                    int z = rs[bestI].z;
                    int n = min(avail[q], rs[bestI].level - committed[z]);
                    avail[q] -= n; committed[z] += n;
                    int dest = (q == z) ? q : stepToward(q, z, n, false);
                    addMove(q, dest, n);
                }
            }
        }
        // aggregate / clamp moves against available units per source cell
        {
            map<pair<int,int>, int> agg;
            for (auto& m : res.wm) agg[{m.s, m.d}] += m.n;
            res.wm.clear();
            vector<int> left = mW0;
            for (auto& kv : agg) {
                int s = kv.first.first, d = kv.first.second;
                int n = min(kv.second, left[s]);
                if (n > 0) { left[s] -= n; res.wm.push_back({s, d, n}); }
            }
        }
        return res;
    }

    string dirBetween(int s, int d) const {
        int sx = s % W, sy = s / W, dx = d % W, dy = d / W;
        if (dx == sx && dy == sy - 1) return "U";
        if (dx == sx && dy == sy + 1) return "D";
        if (dx == sx - 1 && dy == sy) return "L";
        return "R";
    }

    int pickSite(const vector<pair<int,int>>& unmet, int hubCell, int makeW, const vector<int>& mW0) {
        vector<int> sites; sites.push_back(baseZ);
        for (int h : hospCells) sites.push_back(h);
        int best = baseZ; long bestSc = -(1L << 60);
        for (int s : sites) {
            if (s != baseZ && E1[s] > mW0[s] + makeW) continue;
            long sc = 0;
            for (auto [z, def] : unmet) if (dist[s][z] < INF) sc += (long)def * max(0, 8 - dist[s][z]) * 100;
            int tgt = hubCell >= 0 ? hubCell : baseZ;
            if (dist[s][tgt] < INF) sc -= dist[s][tgt] * 10;
            if (s != baseZ) sc += 5;
            if (sc > bestSc) { bestSc = sc; best = s; }
        }
        return best;
    }

    vector<string> decide(const p::View& v, const p::Init& init) {
        auto t0 = chrono::steady_clock::now();
        init_once(init); readState(v);
        vector<string> out;
        int totalF = 0, totalW = 0, eTotW = 0;
        for (int z = 0; z < N; z++) { totalF += myF[z]; totalW += myW[z]; eTotW += eW[z]; }
        int income = 10 + 2 * halls;
        int wcost = max(2, 3 - engs);

        Result r1 = plan(myW, myF);
        int reserve = max(0, r1.capCost - income);
        Result fin;
        int spawnF = 0, spawnW = 0, fSite = baseZ, wSite = baseZ;
        for (int iter = 0; iter < 3; iter++) {
            int budget = max(0, R - reserve);
            int minSpend = max(0, R + income - 40);
            budget = max(budget, min(R, minSpend));
            int fcap = T <= 30 ? 8 : 4;
            if (T > 150) fcap = 0;
            int needF = max(0, min(r1.freeTargets, fcap - totalF));
            if (T < 145 && T >= 8) needF = max(needF, min(2 - totalF, fcap - totalF));
            if (T < 120 && eTotW > totalW + 8 && T > 20) needF = min(needF, max(0, 1 - totalF));
            spawnF = min({needF, T <= 12 ? 4 : 2, budget / 5});
            if (T >= 12 && totalW < 1 + totalF) spawnF = min(spawnF, 1);
            budget -= 5 * spawnF;
            spawnW = budget / wcost;
            if (r1.engCapNow && engs == 0 && T < 100 && r1.unmet.empty() && totalW >= 2 && R + income <= 40) spawnW = 0;
            if (T >= 158) spawnW = 0;
            wSite = pickSite(r1.unmet, prevBC >= 0 ? prevBC : r1.hub, spawnW, myW);
            fSite = baseZ;
            vector<int> mW = myW, mF = myF;
            mW[wSite] += spawnW; mF[fSite] += spawnF;
            fin = plan(mW, mF);
            int nr = max(0, fin.capCost - income);
            if (nr <= reserve) break;
            reserve = nr;
        }
        prevBC = fin.bc;
        fMem.assign(N, {});
        for (auto& fa : fin.fAssign) if (fa.second >= 0) fMem[fa.first].push_back(fa.second);
        auto emitSpawn = [&](const string& k, int n, int site) {
            if (n <= 0) return;
            if (site == baseZ) out.push_back(p::spawn(k, n));
            else out.push_back(p::spawn(k, n, site % W, site / W));
        };
        emitSpawn("F", spawnF, fSite);
        emitSpawn("W", spawnW, wSite);

        vector<pair<int,int>> pr;
        vector<int> pb;
        for (int b = 0; b < nb; b++) if (!isMine(b) && fin.fEnd[bcell[b]] > 0) pb.push_back(b);
        sort(pb.begin(), pb.end(), [&](int a, int b) { int va = bval(a), vb = bval(b); if (va != vb) return va > vb; return hk(bcell[a]) < hk(bcell[b]); });
        for (int b : pb) pr.push_back({bcell[b] % W, bcell[b] / W});
        if (!pr.empty()) out.push_back(p::priority(pr));
        for (auto& m : fin.wm) out.push_back(p::move(m.s % W, m.s / W, "W", m.n, dirBetween(m.s, m.d)));
        {
            map<pair<int,int>, int> fa;
            for (auto& m : fin.fm) fa[{m.s, m.d}] += m.n;
            for (auto& kv : fa) out.push_back(p::move(kv.first.first % W, kv.first.first / W, "F", kv.second, dirBetween(kv.first.first, kv.first.second)));
        }
        if (lg()) { double ms = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count(); LOGF("T%d TIME %.2f ms\n", T, ms); }
        if (getenv("TURTLE_DEBUG")) {
            fprintf(stderr, "T%d R%d sF%d sW%d@%d adv%d hub%d army%d unmet%zu cap%d\n", T, R, spawnF, spawnW, wSite, (int)fin.advance, fin.hub, fin.armyCell, fin.unmet.size(), fin.capCost);
        }
        return out;
    }
};

Bot bot;
}

vector<string> decide(const p::View& view, const p::Init& init) { return bot.decide(view, init); }
int main() { ios::sync_with_stdio(false); cin.tie(nullptr); return p::run(decide); }

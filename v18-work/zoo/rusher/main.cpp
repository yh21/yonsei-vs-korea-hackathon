// rusher: ENG-first, mass-W "deathball" opponent for the v18 test zoo.
// Independent decision logic (only protocol.hpp / generated.hpp are shared).
// C++20, standard library only.
//
// Style: grab the home ENG (W cost 3 -> 2) with the first flags, expand to a few
// safe home buildings, bank money until ENG, then mass-produce W into ONE big stack
// that marches on the enemy's buildings with a couple of flag escorts.  Almost no
// home defence (only opportunistic interception of raiders), never retreats on purpose.

#include "protocol.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <string>
#include <vector>
#ifdef TIMING
#include <chrono>
#endif

using namespace std;

#ifdef DBG
#define DBGF(...) do { if (dbgOn) fprintf(stderr, __VA_ARGS__); } while (0)
#else
#define DBGF(...) do {} while (0)
#endif

namespace {
constexpr int INF = 1000000000;
const int DX[4] = {0, 0, -1, 1};
const int DY[4] = {-1, 1, 0, 0};
const char* const DN[4] = {"U", "D", "L", "R"};

#ifdef TUNE
int prm(int i, int d) {
    static int c[64];
    static bool init[64];
    if (!init[i]) {
        init[i] = true;
        char n[8];
        snprintf(n, 8, "P%d", i);
        const char* e = getenv(n);
        c[i] = e ? atoi(e) : d;
    }
    return c[i];
}
#else
constexpr int prm(int, int d) { return d; }
#endif

struct Bot {
    bool ready = false;
    int W = 15, H = 15, N = 225;
    string me, opp;
    vector<vector<int>> dist;
    vector<vector<pair<int, int>>> nb;  // per cell: (dir, cell) in home-normalised order
    int myBase = 0, enBase = 0;
    vector<int> knownScore, symId;
    vector<char> depotMine;
    vector<int> idCell;
    vector<string> idType;

    // persistent
    bool launched = false;
    int lastBall = -1;
    int prevTarget = -1;
    vector<int> holdCnt, tgtMem;

    // per-turn
    int T = 0;
    bool dbgOn = false;
    vector<int> mF, mW, mS, eF, eW, eS;
    vector<int> pot, pot2;
    vector<int> remW, remF, finW, finF;
    vector<int> bAt;
    vector<string> out;
    const p::View* V = nullptr;

    int idx(int x, int y) const { return y * W + x; }
    int cx(int c) const { return c % W; }
    int cy(int c) const { return c / W; }

    void init_once(const p::Init& init) {
        if (ready) return;
        ready = true;
        W = init.width;
        H = init.height;
        N = W * H;
        me = init.team;
        opp = init.opp;
        vector<int> order = (init.base().first * 2 > W - 1) ? vector<int>{1, 0, 3, 2} : vector<int>{0, 1, 2, 3};
        nb.assign(N, {});
        for (int c = 0; c < N; c++) {
            if (!init.passable(cx(c), cy(c))) continue;
            for (int d : order) {
                int nx = cx(c) + DX[d], ny = cy(c) + DY[d];
                if (init.passable(nx, ny)) nb[c].push_back({d, idx(nx, ny)});
            }
        }
        dist.assign(N, vector<int>(N, INF));
        for (int s = 0; s < N; s++) {
            if (!init.passable(cx(s), cy(s))) continue;
            queue<int> q;
            dist[s][s] = 0;
            q.push(s);
            while (!q.empty()) {
                int v = q.front();
                q.pop();
                for (auto& e : nb[v]) {
                    int u = e.second;
                    if (dist[s][u] > dist[s][v] + 1) {
                        dist[s][u] = dist[s][v] + 1;
                        q.push(u);
                    }
                }
            }
        }
        myBase = idx(init.base().first, init.base().second);
        auto eb = init.bases[me == "Y" ? 1 : 0];
        enBase = idx(eb.first, eb.second);
        int maxid = 0;
        for (auto& b : init.buildings) maxid = max(maxid, b.id);
        knownScore.assign(maxid + 1, -1);
        symId.assign(maxid + 1, -1);
        depotMine.assign(maxid + 1, 0);
        idCell.assign(maxid + 1, -1);
        idType.assign(maxid + 1, "");
        holdCnt.assign(N, 0);
        tgtMem.assign(N, -1);
        map<int, int> at;
        for (auto& b : init.buildings) {
            idCell[b.id] = idx(b.x, b.y);
            idType[b.id] = b.type;
            at[idx(b.x, b.y)] = b.id;
            if (b.type == "PLAZA") knownScore[b.id] = 3;
        }
        for (auto& b : init.buildings) {
            auto it = at.find(idx(W - 1 - b.x, H - 1 - b.y));
            if (it != at.end()) symId[b.id] = it->second;
        }
    }

    void memory(const p::View& v) {
        for (auto& b : v.buildings) {
            if (b.id >= (int)knownScore.size()) continue;
            if (b.score >= 0) {
                knownScore[b.id] = b.score;
                if (symId[b.id] >= 0) knownScore[symId[b.id]] = b.score;
            }
            if (b.type == "DEPOT" && b.owner == me) depotMine[b.id] = 1;
        }
    }

    bool central(const p::Building& b) const {
        if (b.type == "PLAZA" || b.type == "WATCH" || b.type == "DEPOT") return true;
        if (b.type == "STATION") return b.x >= 5 && b.x <= 9;
        return false;
    }
    int sc10(const p::Building& b) const {
        if (knownScore[b.id] >= 0) return knownScore[b.id] * 10;
        if (b.type == "PLAZA") return 30;
        return central(b) ? 30 : 15;
    }
    bool myHalf(int c) const { return dist[c][myBase] < dist[c][enBase]; }

    int bval(const p::Building& b, bool haveEng) const {
        int tb = 0;
        if (b.type == "ENG") tb = haveEng ? (b.owner == opp ? prm(1, 45) : 10) : prm(0, 110);
        else if (b.type == "HALL") tb = prm(2, 50);
        else if (b.type == "HOSPITAL") tb = prm(3, 40);
        else if (b.type == "DEPOT") tb = depotMine[b.id] ? 5 : prm(4, 60);
        else if (b.type == "STATION") tb = 25;
        else if (b.type == "LIBRARY") tb = 22;
        else if (b.type == "WATCH") tb = 12;
        else if (b.type == "PLAZA") tb = 45;
        int v = sc10(b) * 2 + tb;
        if (b.owner == opp) v += prm(5, 25);
        return v;
    }

    const p::Building* bldById(const p::View& v, int id) const {
        for (auto& b : v.buildings)
            if (b.id == id) return &b;
        return nullptr;
    }

    // ---------- movement helpers
    void mv(int c, int kind, int n, int d) {  // kind 0=F 1=W
        if (n <= 0) return;
        int to = -1;
        for (auto& e : nb[c])
            if (e.first == d) to = e.second;
        if (to < 0) return;
        out.push_back(p::move(cx(c), cy(c), kind ? "W" : "F", n, DN[d]));
        (kind ? remW : remF)[c] -= n;
        (kind ? finW : finF)[to] += n;
    }

    // best neighbour stepping strictly closer to tg; returns index into nb[c] or -1.
    int stepIdx(int c, int tg, int kind, int n, bool hard) {
        int best = -1, bc = INF;
        for (int k = 0; k < (int)nb[c].size(); k++) {
            int q = nb[c][k].second;
            if (dist[q][tg] >= dist[c][tg]) continue;
            int cost = 0;
            if (pot[q] > 0) {
                int support = finW[q] + (kind == 1 ? n : 0);
                if (kind == 1) {
                    if (pot[q] >= support) cost += 50;
                } else {
                    if (pot[q] > support) cost += hard ? 100000 : 50;
                }
            }
            cost = cost * 8 + max(abs(cx(q) - cx(tg)), abs(cy(q) - cy(tg)));
            if (cost < bc) {
                bc = cost;
                best = k;
            }
        }
        if (best >= 0 && bc >= 100000 * 8) return -1;
        return best;
    }

    vector<string> decide(const p::View& v, const p::Init& init) {
        init_once(init);
        memory(v);
        V = &v;
        out.clear();
        T = v.turn;
#ifdef DBG
        { const char* e = getenv("DBG_TURN"); dbgOn = e && atoi(e) == v.turn; }
#endif
        mF.assign(N, 0); mW.assign(N, 0); mS.assign(N, 0);
        eF.assign(N, 0); eW.assign(N, 0); eS.assign(N, 0);
        for (auto& u : v.units) {
            int c = idx(u.x, u.y);
            auto& arr = (u.team == me) ? (u.kind == "F" ? mF : u.kind == "W" ? mW : mS)
                                       : (u.kind == "F" ? eF : u.kind == "W" ? eW : eS);
            arr[c] += u.count;
        }
        bAt.assign(N, -1);
        for (int i = 0; i < (int)v.buildings.size(); i++) bAt[idx(v.buildings[i].x, v.buildings[i].y)] = i;
        auto bo = [&](int c) -> const p::Building* { return bAt[c] >= 0 ? &v.buildings[bAt[c]] : nullptr; };

        int halls = 0, eng = 0, engE = 0, libs = 0;
        vector<int> mySites{myBase}, enSites{enBase};
        for (auto& b : v.buildings) {
            if (b.owner == me) {
                if (b.type == "HALL") halls++;
                if (b.type == "ENG") eng++;
                if (b.type == "LIBRARY") libs++;
                if (b.type == "HOSPITAL") mySites.push_back(idx(b.x, b.y));
            } else if (b.owner == opp) {
                if (b.type == "ENG") engE++;
                if (b.type == "HOSPITAL") enSites.push_back(idx(b.x, b.y));
            }
        }
        int income = 10 + 2 * halls;
        int wc = eng ? 2 : 3, ewc = engE ? 2 : 3;
        bool haveEng = eng > 0;

        // ---------- enemy potential per cell (W that could stand there after their move)
        pot.assign(N, 0);
        int eSpawn = v.opp_resource / ewc;
        vector<int> eStations;
        for (auto& b : v.buildings)
            if (b.owner == opp && b.type == "STATION") eStations.push_back(idx(b.x, b.y));
        for (int c = 0; c < N; c++) {
            if (nb[c].empty()) continue;
            int s = eW[c];
            for (auto& e : nb[c]) s += eW[e.second];
            int sp = 0;
            for (int st : enSites)
                if (dist[c][st] <= 1) sp = eSpawn;
            pot[c] = s + sp;
        }
        pot2.assign(N, 0);
        for (int c = 0; c < N; c++) {
            if (nb[c].empty()) continue;
            int sm = 0;
            for (int q = 0; q < N; q++)
                if (dist[c][q] <= 2) sm += eW[q];
            int sp = 0;
            for (int st : enSites)
                if (dist[c][st] <= 2) sp = eSpawn;
            pot2[c] = sm + sp;
        }
        if (eStations.size() >= 2) {
            for (int z : eStations) {
                int extra = 0;
                for (int q : eStations)
                    if (q != z) extra += eW[q];
                pot[z] += min(5, extra);
            }
        }

        // ---------- spawn
        int nF = 0, nW = 0;
        for (int c = 0; c < N; c++) nF += mF[c], nW += mW[c];
        int res = v.my_resource;
        int capNeed = 0;
        for (auto& b : v.buildings) {
            int c = idx(b.x, b.y);
            if (b.owner != me && mF[c] > 0 && eF[c] == 0) {
                int cost = (b.type == "PLAZA" ? 4 : 2) - (libs ? 1 : 0);
                capNeed += max(1, cost);
            }
        }
        int reserve = max(0, capNeed - income);
        int budget = max(0, res - reserve);
        int minSpend = max(0, res + income - 40);
        // F wish
        int notMineHome = 0, engTargetDist = INF;
        for (auto& b : v.buildings) {
            int c = idx(b.x, b.y);
            if (b.owner != me && myHalf(c)) notMineHome++;
            if (b.type == "ENG" && b.owner == "N" && myHalf(c))
                for (int q = 0; q < N; q++)
                    if (mF[q] > 0) engTargetDist = min(engTargetDist, dist[q][c]);
        }
        int wantF = min(prm(10, 4), notMineHome) + (T >= 6 ? (T < 30 ? prm(11, 3) : prm(29, 5)) : 0);
        if (T > 120) wantF = max(wantF, 3);
        int makeF = 0, makeW = 0;
        {
            int needF = max(0, wantF - nF);
            makeF = min({needF, T <= 25 ? 2 : 1, budget / 5});
            if (T == 1) makeF = min(2, budget / 5);
            budget -= 5 * makeF;
            bool bank = (!eng && engTargetDist <= prm(30, 4) && T < 25);
            if (bank) {
                int spend = min(budget, minSpend);
                makeW = spend / wc;
            } else {
                makeW = budget / wc;
            }
        }
        // spawn site: base or a nearer owned hospital
        int site = myBase;
        {
            int anchorGuess = -1, bw = 0;
            for (int c = 0; c < N; c++)
                if (mW[c] > bw) bw = mW[c], anchorGuess = c;
            int goal = anchorGuess >= 0 ? anchorGuess : enBase;
            int bestd = dist[myBase][goal];
            for (int s : mySites) {
                if (s == myBase) continue;
                if (pot[s] > mW[s] + makeW) continue;
                if (dist[s][goal] + 1 < bestd) bestd = dist[s][goal] + 1, site = s;
            }
        }
        auto emitSpawn = [&](const char* kind, int n) {
            if (n <= 0) return;
            if (site == myBase) out.push_back(p::spawn(kind, n));
            else out.push_back(p::spawn(kind, n, cx(site), cy(site)));
            if (kind[0] == 'F') mF[site] += n, nF += n;
            else mW[site] += n, nW += n;
        };
        emitSpawn("F", makeF);
        emitSpawn("W", makeW);

        remW = mW; remF = mF;
        finW.assign(N, 0); finF.assign(N, 0);

        // ---------- guard: my buildings with enemy flags close by get a minimal garrison
        {
            struct G { int cell, need, val, dF; };
            vector<G> gs;
            for (auto& b : v.buildings) {
                if (b.owner != me) continue;
                int bc = idx(b.x, b.y);
                int dF = INF;
                for (int q = 0; q < N; q++)
                    if (eF[q] > 0) dF = min(dF, dist[q][bc]);
                if (dF > prm(36, 4)) continue;
                gs.push_back({bc, pot2[bc] + 1, bval(b, haveEng), dF});
            }
            sort(gs.begin(), gs.end(), [](const G& a, const G& b) { return a.val > b.val; });
            for (auto& g : gs) {
                vector<pair<int, int>> cand;
                int avail = 0;
                for (int q = 0; q < N; q++)
                    if (remW[q] > 0 && dist[q][g.cell] <= min(prm(37, 8), g.dF + 1)) cand.push_back({dist[q][g.cell], q}), avail += remW[q];
                if (avail < g.need) continue;
                sort(cand.begin(), cand.end());
                int left = g.need;
                for (auto& cq : cand) {
                    if (left <= 0) break;
                    int q = cq.second;
                    int k = min(remW[q], left);
                    if (q == g.cell) {
                        remW[q] -= k;
                        finW[q] += k;
                        left -= k;
                        continue;
                    }
                    int si = stepIdx(q, g.cell, 1, k, false);
                    if (si < 0) continue;
                    mv(q, 1, k, nb[q][si].first);
                    left -= k;
                }
            }
        }

        // ---------- raid interception (enemy flags in my half / on my buildings)
        {
            struct Raid { int c, threat, need; };
            vector<Raid> raids;
            for (int c = 0; c < N; c++) {
                if (eF[c] <= 0) continue;
                const p::Building* b = bo(c);
                bool onMine = b && b->owner == me;
                bool nearMine = false;
                for (auto& q : v.buildings)
                    if (q.owner == me && dist[c][idx(q.x, q.y)] <= 1) nearMine = true;
                if (!(myHalf(c) || onMine)) continue;
                int threat = 5 * eF[c] + (onMine ? 30 : nearMine ? 15 : 0) + ((b && b->owner != opp) ? 10 : 0);
                raids.push_back({c, threat, pot[c] + 1 + prm(18, 1)});
            }
            sort(raids.begin(), raids.end(), [](const Raid& a, const Raid& b) { return a.threat > b.threat; });
            for (auto& r : raids) {
                vector<pair<int, int>> cand;
                int avail = 0;
                for (int q = 0; q < N; q++)
                    if (remW[q] > 0 && dist[q][r.c] <= prm(12, 5)) cand.push_back({dist[q][r.c], q}), avail += remW[q];
                if (avail < r.need - prm(18, 1) || cand.empty()) continue;
                sort(cand.begin(), cand.end());
                int left = r.need;
                int dmin = cand[0].first;
                for (auto& cq : cand) {
                    if (left <= 0) break;
                    int q = cq.second;
                    if (cq.first > dmin + 1 && cq.first > 1) continue;
                    int k = min(remW[q], left);
                    int si = stepIdx(q, r.c, 1, k, false);
                    if (si < 0) continue;
                    mv(q, 1, k, nb[q][si].first);
                    left -= k;
                }
            }
        }

        // ---------- balls: every W stack that is not a home recruit marches on its own
        struct Ball { int c, dest, target; };
        vector<Ball> balls;
        vector<int> newHold(N, 0), newTgt(N, -1);
        vector<char> claimed(idCell.size(), 0);
        int Smin = prm(13, 8);
        int bigTarget = -1;
        {
            vector<int> snapW = remW;
            vector<int> order;
            for (int c = 0; c < N; c++)
                if (remW[c] > 0) order.push_back(c);
            sort(order.begin(), order.end(), [&](int a, int b) {
                if (remW[a] != remW[b]) return remW[a] > remW[b];
                return dist[a][enBase] < dist[b][enBase];
            });
            vector<int> tiny;
            for (int c : order) {
                int M = remW[c];
                if (M <= 0) continue;
                bool home = find(mySites.begin(), mySites.end(), c) != mySites.end();
                DBGF("stack (%d,%d) M=%d F=%d home=%d\n", cx(c), cy(c), M, remF[c], (int)home);
                if (home && M < Smin && T < 140) continue;  // recruit waits for company
                int mem = tgtMem[c];
                bool memValid = mem >= 0 && bldById(v, mem) && bldById(v, mem)->owner != me;
                if (!home && M < prm(17, 4) && !memValid && remF[c] == 0) { tiny.push_back(c); continue; }

                // ---- target choice (buildings, plus off-building enemy flag stacks)
                int bestBid = -1, tgc = enBase;
                {
                    double bestV = -1e18;
                    for (auto& b : v.buildings) {
                        if (b.owner == me) continue;
                        int bc = idx(b.x, b.y);
                        if (dist[c][bc] >= INF) continue;
                        double val = bval(b, haveEng) - 5.0 * min(dist[c][bc], 30);
                        if (!myHalf(bc)) val += prm(20, 30);
                        val += prm(25, 20) * eF[bc];
                        int need = pot2[bc] + 2;
                        if (need > M) val -= 4.0 * (need - M);
                        if (b.id == mem) val += prm(26, 30);
                        if (b.id == prevTarget) val += prm(15, 15);
                        if (claimed[b.id]) val -= 1000;
                        if (val > bestV) bestV = val, bestBid = b.id, tgc = bc;
                    }
                    for (int q = 0; q < N; q++) {
                        if (eF[q] <= 0 || bAt[q] >= 0) continue;
                        if (M >= prm(32, 2 * 8) && T < 150) continue;  // big stacks send small hunting parties instead
                        int d = dist[c][q];
                        if (d >= INF || d > 5) continue;
                        if (pot[q] + 1 > M) continue;
                        double val = 40.0 + prm(25, 20) * eF[q] - 6.0 * d;
                        if (val > bestV) bestV = val, bestBid = -1, tgc = q;
                    }
                }
                if (bigTarget < 0) bigTarget = bestBid;

                // ---- squads: detach capture parties for nearby targets (each with one flag)
                int detached = 0;
                if (M >= Smin && T < 150) {
                    struct Cd { double val; int bid, cell, need; bool needF; };
                    vector<Cd> cds;
                    int mainNeed = (bestBid >= 0 ? pot2[tgc] + 2 : pot[tgc] + 1);
                    for (auto& b : v.buildings) {
                        if (b.owner == me || b.id == bestBid || claimed[b.id]) continue;
                        int bc = idx(b.x, b.y);
                        int d = dist[c][bc];
                        if (d >= INF || d > prm(22, 9) || d < 1) continue;
                        cds.push_back({bval(b, haveEng) - 5.0 * d + prm(25, 20) * eF[bc], b.id, bc, pot2[bc] + prm(27, 2), true});
                    }
                    for (int q = 0; q < N; q++) {
                        if (eF[q] <= 0 || bAt[q] >= 0 || q == tgc) continue;
                        int d = dist[c][q];
                        if (d >= INF || d > prm(33, 6) || d < 1) continue;
                        cds.push_back({prm(34, 60) + prm(25, 20) * eF[q] - 6.0 * d, -1, q, pot[q] + prm(27, 2), false});
                    }
                    sort(cds.begin(), cds.end(), [](const Cd& a, const Cd& b) { return a.val > b.val; });
                    int squads = 0;
                    for (auto& cd : cds) {
                        if (squads >= prm(23, 10)) break;
                        if (cd.needF && remF[c] < 2) continue;
                        if (M - detached - cd.need < max(prm(24, 6), mainNeed)) continue;
                        int si = stepIdx(c, cd.cell, 1, cd.need, false);
                        if (si < 0) continue;
                        int d = nb[c][si].first, to = nb[c][si].second;
                        mv(c, 1, cd.need, d);
                        if (cd.needF) mv(c, 0, 1, d);
                        detached += cd.need;
                        squads++;
                        if (cd.bid >= 0) claimed[cd.bid] = 1;
                        newTgt[to] = cd.bid;
                        balls.push_back({c, to, cd.bid});
                    }
                }

                // ---- hold rules (capturing / waiting for flags)
                bool hold = false;
                const p::Building* bA = bo(c);
                bool needCap = bA && bA->owner != me;
                if (needCap && eF[c] == 0) {
                    if (remF[c] > 0 && holdCnt[c] < 3) hold = true;
                    else if (remF[c] == 0 && c == tgc && holdCnt[c] < prm(19, 8)) {
                        bool fnear = false;
                        for (int q = 0; q < N; q++)
                            if (mF[q] > 0 && dist[q][c] <= 6) fnear = true;
                        if (fnear) hold = true;
                    }
                }
                DBGF("  target bid=%d cell=(%d,%d) detached=%d hold=%d\n", bestBid, cx(tgc), cy(tgc), detached, (int)hold);
                int Mcore = remW[c];
                int bestC = c;
                double bestU = -1e18;
                vector<int> opts{c};
                for (auto& e : nb[c]) opts.push_back(e.second);
                bool stayUnsafe = false;
                for (int oc : opts) {
                    int Mc = 0;
                    for (int q = 0; q < N; q++)
                        if (dist[q][oc] <= 1) Mc += snapW[q];
                    Mc -= detached;
                    double u = -12.0 * dist[oc][tgc];
                    u += prm(35, 30) * min(3, eF[oc]) + 5.0 * eS[oc];
                    const p::Building* bc = bo(oc);
                    if (bc && bc->owner != me && mF[c] > 0) u += 25;
                    bool safe = Mc > pot[oc] + prm(21, 0);
                    if (oc == c && !safe) stayUnsafe = true;
                    if (!safe) u -= 1000 + 40.0 * (pot[oc] - Mc);
                    u -= 0.25 * (abs(cx(oc) - 7) + abs(cy(oc) - 7));
                    u -= 0.5 * max(abs(cx(oc) - cx(tgc)), abs(cy(oc) - cy(tgc)));
                    if (oc == c) u += 0.5;
                    DBGF("    opt (%d,%d) Mc=%d pot=%d safe=%d u=%.1f\n", cx(oc), cy(oc), Mc, pot[oc], (int)safe, u);
                    if (u > bestU) bestU = u, bestC = oc;
                }
                if (hold && !stayUnsafe) bestC = c;
                if (bestC == c) newHold[c] = holdCnt[c] + 1;
                int dest = bestC;
                if (bestC != c) {
                    int d = -1;
                    for (auto& e : nb[c])
                        if (e.second == bestC) d = e.first;
                    mv(c, 1, Mcore, d);
                    if (remF[c] > 0) {
                        const p::Building* b0 = bo(c);
                        bool capturing = b0 && b0->owner != me && eF[c] == 0;
                        if (!capturing) mv(c, 0, remF[c], d);
                    }
                }
                if (newTgt[dest] < 0 || true) newTgt[dest] = bestBid;
                balls.push_back({c, dest, bestBid});
            }
            // tiny stacks join the nearest ball (or go home)
            for (int c : tiny) {
                if (remW[c] <= 0) continue;
                int bestd = INF, tg = myBase;
                for (auto& b : balls)
                    if (dist[c][b.dest] < bestd) bestd = dist[c][b.dest], tg = b.dest;
                if (bestd >= INF) tg = myBase;
                if (c == tg) continue;
                int si = stepIdx(c, tg, 1, remW[c], false);
                if (si < 0) continue;
                mv(c, 1, remW[c], nb[c][si].first);
            }
        }
        holdCnt = newHold;
        tgtMem = newTgt;
        if (bigTarget >= 0) prevTarget = bigTarget;
        for (int c = 0; c < N; c++) finW[c] += remW[c], remW[c] = 0;

        auto nearestBallDest = [&](int c) {
            int bd = INF, tg = -1;
            for (auto& b : balls)
                if (dist[c][b.dest] < bd) bd = dist[c][b.dest], tg = b.dest;
            return make_pair(bd, tg);
        };

        // ---------- flags
        {
            vector<int> assigned(idCell.size(), 0);
            // endangered flags run to the safest neighbouring cell
            for (int c = 0; c < N; c++) {
                if (remF[c] <= 0) continue;
                int danger = pot[c] - finW[c];
                if (danger < prm(31, 1)) continue;
                int bestq = -1, bestd = danger;
                for (auto& e : nb[c]) {
                    int q = e.second;
                    int dq = pot[q] - finW[q];
                    if (dq < bestd || (dq == bestd && bestq >= 0 && dist[q][myBase] < dist[bestq][myBase])) {
                        if (dq < danger) bestd = dq, bestq = q;
                    }
                }
                if (bestq < 0) continue;
                int d = -1;
                for (auto& e : nb[c]) if (e.second == bestq) d = e.first;
                mv(c, 0, remF[c], d);
            }
            // flags already on a capturable building stay and capture
            for (auto& b : v.buildings) {
                int c = idx(b.x, b.y);
                if (remF[c] <= 0) continue;
                if (b.owner == me) continue;
                if (eF[c] > 0 && finW[c] == 0) continue;  // contested, pointless
                remF[c] -= 1;
                finF[c] += 1;
                assigned[b.id] = 1;
            }
            // remaining flags: greedy assignment to buildings
            struct Pair { int val, c, bid; };
            vector<Pair> cand;
            for (int c = 0; c < N; c++) {
                if (remF[c] <= 0) continue;
                for (auto& b : v.buildings) {
                    if (b.owner == me || assigned[b.id]) continue;
                    int bc = idx(b.x, b.y);
                    if (dist[c][bc] >= INF) continue;
                    if (pot[bc] > finW[bc]) continue;
                    if (eF[bc] > 0 && finW[bc] == 0) continue;
                    int val = bval(b, haveEng) - 6 * dist[c][bc];
                    auto nbd = nearestBallDest(bc);
                    if (!myHalf(bc) && nbd.first > prm(28, 2)) continue;
                    if (nbd.first <= 2) val += 15;
                    cand.push_back({val, c, b.id});
                }
            }
            sort(cand.begin(), cand.end(), [](const Pair& a, const Pair& b) { return a.val > b.val; });
            for (auto& pr : cand) {
                if (remF[pr.c] <= 0 || assigned[pr.bid]) continue;
                int bc = idCell[pr.bid];
                if (pr.c == bc) {
                    remF[pr.c]--; finF[pr.c]++; assigned[pr.bid] = 1;
                    continue;
                }
                int si = stepIdx(pr.c, bc, 0, 1, true);
                assigned[pr.bid] = 1;
                if (si < 0) {
                    remF[pr.c]--; finF[pr.c]++;
                    continue;
                }
                mv(pr.c, 0, 1, nb[pr.c][si].first);
            }
            // leftovers follow the nearest ball (or wait at home)
            for (int c = 0; c < N; c++) {
                if (remF[c] <= 0) continue;
                auto nbd = nearestBallDest(c);
                int tg = nbd.second >= 0 ? nbd.second : myBase;
                if (c == tg) {
                    finF[c] += remF[c]; remF[c] = 0; continue;
                }
                int si = stepIdx(c, tg, 0, remF[c], true);
                if (si < 0) {
                    finF[c] += remF[c]; remF[c] = 0; continue;
                }
                mv(c, 0, remF[c], nb[c][si].first);
            }
        }

        // ---------- capture priority
        {
            vector<pair<int, pair<int, int>>> pr;
            for (auto& b : v.buildings) {
                int c = idx(b.x, b.y);
                if (b.owner != me && finF[c] > 0) pr.push_back({-bval(b, haveEng), {b.x, b.y}});
            }
            if (pr.size() >= 2) {
                sort(pr.begin(), pr.end());
                vector<pair<int, int>> co;
                for (auto& e : pr) co.push_back(e.second);
                out.push_back(p::priority(co));
            }
        }
        return out;
    }
};
}  // namespace

#ifdef TIMING
struct TimingReport {
    double mx = 0, sum = 0;
    int n = 0;
    ~TimingReport() { fprintf(stderr, "TIMING max_ms=%.3f avg_ms=%.3f turns=%d\n", mx, n ? sum / n : 0.0, n); }
} timingReport;
#endif

int main() {
    Bot bot;
    return p::run([&](const p::View& v, const p::Init& i) {
#ifdef TIMING
        auto t0 = std::chrono::steady_clock::now();
        auto r = bot.decide(v, i);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        timingReport.mx = std::max(timingReport.mx, ms);
        timingReport.sum += ms;
        timingReport.n++;
        return r;
#else
        return bot.decide(v, i);
#endif
    });
}

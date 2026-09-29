// blitz: test-zoo opponent for v18.
// Style: hospital / station raider. Grabs the hospital + engineering hall early, then uses
// station teleports as a conveyor, scouts scores, and strikes places where the local
// warrior count guarantees a win. Completely independent decision logic (no fork of v16/v17).
//
// All reasoning is done in "internal" coordinates where our base is always on the left
// (x <= 4); when we play K the board is rotated by 180 degrees on input and rotated back on output.
#include "protocol.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace std;

namespace {
constexpr int INF = 1000000000;
enum BType { PLAZA = 0, HALL, LIB, ENG, HOSP, STATION, WATCH, DEPOT };

int typeOf(const string& s) {
    if (s == "PLAZA") return PLAZA;
    if (s == "HALL") return HALL;
    if (s == "LIBRARY") return LIB;
    if (s == "ENG") return ENG;
    if (s == "HOSPITAL") return HOSP;
    if (s == "STATION") return STATION;
    if (s == "WATCH") return WATCH;
    return DEPOT;
}

struct Bd { int id = -1, z = -1, type = 0, owner = 2, stage = 0, score = -1; };  // owner 0 me, 1 opp, 2 neutral
struct Mv { int s, d, n; };
struct Op { int z, value, need, cls; };  // cls 0 guard, 1 kill, 2 defend, 3 attack, 4 rally
struct FUnit { int src, dst, tgt; };
struct TeleCmd { int src, dst, n; char kind; };
struct PlanOut {
    vector<Mv> wmoves;
    vector<FUnit> funits;
    vector<TeleCmd> teles;
    vector<int> failed;
    vector<int> fDestCnt;
};

struct Bot {
    // ---------------- static ----------------
    bool ready = false;
    int W = 15, H = 15, N = 225;
    bool flip = false;
    string me, opp;
    vector<char> pass;
    vector<vector<int>> dist;
    vector<array<int, 4>> nb;  // internal neighbours (U D L R), -1 when blocked
    int myBase = 0, opBase = 0;
    int nBld = 0;
    vector<int> symId, knownScore;
    vector<char> depotClaimed;
    int scoutsBuilt = 0;

    int toI(int ox, int oy) const {
        if (flip) { ox = W - 1 - ox; oy = H - 1 - oy; }
        return oy * W + ox;
    }
    pair<int, int> toO(int z) const {
        int x = z % W, y = z / W;
        if (flip) { x = W - 1 - x; y = H - 1 - y; }
        return {x, y};
    }
    string dirO(int s, int d) const {
        auto [sx, sy] = toO(s);
        auto [dx, dy] = toO(d);
        if (dx == sx && dy == sy - 1) return "U";
        if (dx == sx && dy == sy + 1) return "D";
        if (dx == sx - 1 && dy == sy) return "L";
        return "R";
    }
    int X(int z) const { return z % W; }
    int Y(int z) const { return z / W; }
    int cheb(int a, int b) const { return max(abs(X(a) - X(b)), abs(Y(a) - Y(b))); }

    void init_once(const p::Init& init) {
        if (ready) return;
        ready = true;
        W = init.width; H = init.height; N = W * H;
        me = init.team; opp = init.opp; flip = (me == "K");
        pass.assign(N, 0);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) pass[toI(x, y)] = init.terrain[y][x] != '#';
        nb.assign(N, array<int, 4>{-1, -1, -1, -1});
        const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
        for (int z = 0; z < N; z++) {
            if (!pass[z]) continue;
            for (int k = 0; k < 4; k++) {
                int x = X(z) + dx[k], y = Y(z) + dy[k];
                if (x < 0 || y < 0 || x >= W || y >= H) continue;
                int q = y * W + x;
                if (pass[q]) nb[z][k] = q;
            }
        }
        dist.assign(N, vector<int>(N, INF));
        for (int s = 0; s < N; s++) {
            if (!pass[s]) continue;
            queue<int> q; q.push(s); dist[s][s] = 0;
            while (!q.empty()) {
                int v = q.front(); q.pop();
                for (int u : nb[v]) if (u >= 0 && dist[s][u] > dist[s][v] + 1) { dist[s][u] = dist[s][v] + 1; q.push(u); }
            }
        }
        auto mb = init.base();
        auto ob = init.bases[me == "Y" ? 1 : 0];
        myBase = toI(mb.first, mb.second);
        opBase = toI(ob.first, ob.second);
        int maxid = 0;
        for (auto& b : init.buildings) maxid = max(maxid, b.id);
        nBld = maxid + 1;
        symId.assign(nBld, -1); knownScore.assign(nBld, -1); depotClaimed.assign(nBld, 0);
        map<int, int> at;
        for (auto& b : init.buildings) at[toI(b.x, b.y)] = b.id;
        for (auto& b : init.buildings) {
            int z = toI(b.x, b.y);
            int sz = (H - 1 - Y(z)) * W + (W - 1 - X(z));
            auto it = at.find(sz);
            if (it != at.end()) symId[b.id] = it->second;
            if (b.type == "PLAZA") knownScore[b.id] = 3;
        }
    }

    // ---------------- per turn ----------------
    int turn = 0, R = 0, RE = 0;
    vector<Bd> B;
    vector<int> bAt;
    vector<int> myF, myW, myS, enF, enW, enS;
    vector<int> Tpot, Tpot2;
    int eng = 0, halls = 0, stations = 0, libs = 0, hosp = 0, watchN = 0, oppStations = 0, oppEng = 0;
    int income = 10, wc = 3, wcOpp = 3, unknownLeft = 0;
    int totalMyW = 0, totalEnW = 0, totalMyF = 0, totalMyS = 0, totalEnF = 0;
    bool dbg = false;

    int estScore10(const Bd& b) const {
        if (knownScore[b.id] >= 0) return knownScore[b.id] * 10;
        if (b.type == PLAZA) return 30;
        int x = X(b.z);
        return (x >= 5 && x <= 9) ? 30 : 15;
    }

    // Worth of owning building b (independent of current owner).
    int ownWorth(const Bd& b) const {
        int t = turn;
        int sc = estScore10(b);
        int pts = (t >= 125 ? sc * 10 : sc * 5);
        int feat = 0;
        int x = X(b.z);
        switch (b.type) {
            case PLAZA: feat = t < 125 ? 90 : 0; break;
            case HALL: feat = t < 60 ? 200 : (t < 100 ? 120 : (t < 130 ? 40 : 0)); break;
            case ENG: feat = eng > 0 ? 15 : (t < 100 ? 280 : (t < 130 ? 60 : 0)); break;
            case HOSP:
                feat = hosp == 0 ? (t < 90 ? 240 : 60) : (t < 90 ? 70 : 20);
                if (x >= 5 && x <= 9) feat += 30;
                break;
            case STATION:
                feat = stations < 2 ? (t < 110 ? 170 : 40) : (t < 110 ? 45 : 10);
                if (b.owner == 1 && oppStations >= 2) feat += 50;
                break;
            case WATCH: feat = unknownLeft > 0 ? (t < 100 ? 90 : 30) : (t < 100 ? 25 : 0); break;
            case DEPOT: feat = depotClaimed[b.id] ? 0 : (t < 110 ? 240 : 70); break;
            case LIB: feat = t < 100 ? 60 : (t < 130 ? 15 : 0); break;
        }
        int val = pts * (b.owner == 1 ? 2 : 1) + (b.owner == 1 ? feat * 13 / 10 : feat);
        if (b.owner == 1) val += 20;
        return val;
    }
    // Worth of capturing b (0 for buildings we own).
    int capWorth(const Bd& b) const { return b.owner == 0 ? 0 : ownWorth(b); }

    int flagCost(const Bd& b) const {
        int c = (b.type == PLAZA ? 4 : 2) - (libs > 0 ? 1 : 0);
        return max(1, c);
    }

    // ---------------- main ----------------
    vector<string> decide(const p::View& v, const p::Init& init) {
        init_once(init);
        dbg = getenv("BLITZ_DEBUG") != nullptr;
        turn = v.turn; R = v.my_resource; RE = v.opp_resource;
        load(v);
        computeThreat();

        vector<string> out;
        vector<Mv> dummy;
        production(out);

        vector<char> banned(N, 0);
        PlanOut plan;
        for (int it = 0; it < 4; it++) {
            plan = makePlan(banned);
            if (plan.failed.empty()) break;
            for (int z : plan.failed) banned[z] = 1;
        }
        emit(out, plan);
        return out;
    }

    void load(const p::View& v) {
        B.assign(nBld, Bd{});
        bAt.assign(N, -1);
        eng = halls = stations = libs = hosp = watchN = oppStations = oppEng = 0;
        for (auto& b : v.buildings) {
            if (b.id < 0 || b.id >= nBld) continue;
            Bd d;
            d.id = b.id; d.z = toI(b.x, b.y); d.type = typeOf(b.type);
            d.owner = b.owner == me ? 0 : (b.owner == opp ? 1 : 2);
            d.stage = b.stage; d.score = b.score;
            B[b.id] = d; bAt[d.z] = b.id;
            if (b.score >= 0) {
                knownScore[b.id] = b.score;
                if (symId[b.id] >= 0) knownScore[symId[b.id]] = b.score;
            }
            if (d.type == DEPOT && d.owner == 0) depotClaimed[b.id] = 1;
        }
        unknownLeft = 0;
        for (auto& b : B) {
            if (b.id < 0) continue;
            if (b.owner == 0) {
                if (b.type == ENG) eng++;
                if (b.type == HALL) halls++;
                if (b.type == STATION) stations++;
                if (b.type == LIB) libs++;
                if (b.type == HOSP) hosp++;
                if (b.type == WATCH) watchN++;
            } else if (b.owner == 1) {
                if (b.type == STATION) oppStations++;
                if (b.type == ENG) oppEng++;
            }
            if (knownScore[b.id] < 0) unknownLeft++;
        }
        income = 10 + 2 * halls;
        wc = max(2, 3 - (eng > 0 ? 1 : 0));
        wcOpp = max(2, 3 - (oppEng > 0 ? 1 : 0));
        myF.assign(N, 0); myW.assign(N, 0); myS.assign(N, 0);
        enF.assign(N, 0); enW.assign(N, 0); enS.assign(N, 0);
        totalMyW = totalEnW = totalMyF = totalMyS = totalEnF = 0;
        for (auto& u : v.units) {
            int z = toI(u.x, u.y);
            if (u.team == me) {
                if (u.kind == "F") myF[z] += u.count, totalMyF += u.count;
                else if (u.kind == "W") myW[z] += u.count, totalMyW += u.count;
                else myS[z] += u.count, totalMyS += u.count;
            } else {
                if (u.kind == "F") enF[z] += u.count, totalEnF += u.count;
                else if (u.kind == "W") enW[z] += u.count, totalEnW += u.count;
                else enS[z] += u.count;
            }
        }
    }

    void computeThreat() {
        Tpot.assign(N, 0); Tpot2.assign(N, 0);
        vector<int> sites{opBase};
        for (auto& b : B) if (b.id >= 0 && b.owner == 1 && b.type == HOSP) sites.push_back(b.z);
        int sp = RE / wcOpp;
        for (int z = 0; z < N; z++) {
            if (!pass[z]) continue;
            int s1 = enW[z];
            for (int q : nb[z]) if (q >= 0) s1 += enW[q];
            int s2 = 0;
            for (int q = 0; q < N; q++) if (dist[z][q] <= 2) s2 += enW[q];
            bool near1 = false, near2 = false;
            for (int s : sites) { if (dist[s][z] <= 1) near1 = true; if (dist[s][z] <= 2) near2 = true; }
            Tpot[z] = s1 + (near1 ? sp : 0);
            Tpot2[z] = s2 + (near2 ? sp : 0);
        }
        if (oppStations >= 2) {
            vector<int> st;
            for (auto& b : B) if (b.id >= 0 && b.owner == 1 && b.type == STATION) st.push_back(b.z);
            for (int z : st) {
                int extra = 0;
                for (int q : st) if (q != z) extra = max(extra, enW[q]);
                extra = min(5, extra);
                Tpot[z] += extra; Tpot2[z] += extra;
            }
        }
    }

    // ---------------- production ----------------
    vector<pair<int, int>> coreItems;  // (cell, value) where units are wanted
    int pullAt(int z) const {
        int best = 0;
        for (auto& it : coreItems) {
            int d = dist[z][it.first];
            if (d >= INF) continue;
            best = max(best, it.second * 100 / (d + 4));
        }
        return best;
    }

    void production(vector<string>& out) {
        coreItems.clear();
        for (auto& b : B) {
            if (b.id < 0) continue;
            if (b.owner != 0) coreItems.push_back({b.z, capWorth(b)});
        }
        for (int z = 0; z < N; z++) if (enF[z] > 0) coreItems.push_back({z, 700});
        for (auto& b : B) {
            if (b.id < 0 || b.owner != 0) continue;
            for (int z = 0; z < N; z++) if (enF[z] > 0 && dist[z][b.z] <= 3) { coreItems.push_back({b.z, 600}); break; }
        }

        vector<int> sites{myBase};
        for (auto& b : B) if (b.id >= 0 && b.owner == 0 && b.type == HOSP) sites.push_back(b.z);

        int budget = R;
        // capture reserve: flags next to non-owned buildings pay the capture after income
        int capCost = 0;
        for (auto& b : B) {
            if (b.id < 0 || b.owner == 0) continue;
            bool near = false;
            for (int z = 0; z < N && !near; z++) if (myF[z] > 0 && dist[z][b.z] <= 1) near = true;
            if (near) capCost += flagCost(b);
        }
        int reserve = max(0, capCost - income);
        budget = max(0, budget - reserve);
        int minSpend = max(0, R + income - 40);
        budget = max(budget, min(R, minSpend));

        int nonOwned = 0;
        for (auto& b : B) if (b.id >= 0 && b.owner != 0) nonOwned++;
        int wantF;
        if (turn <= 15) wantF = 6;
        else if (nonOwned >= 6) wantF = 5;
        else if (nonOwned >= 3) wantF = 4;
        else wantF = 3;
        if (turn > 130) wantF = min(wantF, 3);
        if (turn >= 150) wantF = 0;
        if (turn < 100 && totalMyW + 6 < totalEnW) wantF = max(2, wantF - 1);

        int makeF = 0, makeS = 0, makeW = 0;
        if (turn == 1) {
            makeF = min(2, budget / 5);
        } else {
            int need = max(0, wantF - totalMyF);
            int cap = turn <= 6 ? 2 : 1;
            makeF = min({need, cap, budget / 5});
        }
        budget -= 5 * makeF;
        if (turn >= 2 && turn < 70 && totalMyS == 0 && unknownLeft > 2 && scoutsBuilt < 3 && budget >= 2 + wc) {
            makeS = 1; budget -= 2; scoutsBuilt++;
        }
        makeW = budget / wc;
        if (turn >= 158) { makeW = 0; makeF = 0; }
        budget -= makeW * wc;

        auto spawnAt = [&](const string& k, int n, int site) {
            if (n <= 0) return;
            if (site == myBase) out.push_back(p::spawn(k, n));
            else { auto [x, y] = toO(site); out.push_back(p::spawn(k, n, x, y)); }
            if (k == "F") myF[site] += n, totalMyF += n;
            else if (k == "W") myW[site] += n, totalMyW += n;
            else myS[site] += n, totalMyS += n;
        };

        // W site: closest to where warriors are wanted
        int wSite = myBase, bestPull = -1;
        for (int s : sites) {
            int pl = pullAt(s);
            if (pl > bestPull) { bestPull = pl; wSite = s; }
        }
        spawnAt("W", makeW, wSite);
        // F site: closest to the best capture target, but never into a hot spot
        int fSite = myBase, bestF = -INF;
        for (int s : sites) {
            int u = 0;
            for (auto& b : B) {
                if (b.id < 0 || b.owner == 0) continue;
                if (dist[s][b.z] >= INF) continue;
                u = max(u, capWorth(b) * 100 / (dist[s][b.z] + 4));
            }
            u -= max(0, Tpot[s] - myW[s]) * 300;
            if (u > bestF) { bestF = u; fSite = s; }
        }
        spawnAt("F", makeF, fSite);
        spawnAt("S", makeS, myBase);
    }

    // ---------------- planning ----------------
    PlanOut makePlan(const vector<char>& banned) {
        PlanOut po;
        po.fDestCnt.assign(N, 0);

        // Mnear: my warriors that can reach z this turn
        vector<int> Mnear(N, 0);
        for (int z = 0; z < N; z++) {
            if (!pass[z]) continue;
            int s = myW[z];
            for (int q : nb[z]) if (q >= 0) s += myW[q];
            Mnear[z] = s;
        }
        vector<int> risk(N, 0);
        for (int z = 0; z < N; z++) risk[z] = min(60, 12 * max(0, Tpot[z] - Mnear[z]));

        // ---- route costs to every non-owned building ----
        vector<vector<int>> route(nBld);
        for (auto& b : B) {
            if (b.id < 0 || b.owner == 0) continue;
            auto& dd = route[b.id];
            dd.assign(N, INF);
            priority_queue<pair<int, int>, vector<pair<int, int>>, greater<>> pq;
            dd[b.z] = 0; pq.push({0, b.z});
            while (!pq.empty()) {
                auto [d, z] = pq.top(); pq.pop();
                if (d != dd[z]) continue;
                for (int q : nb[z]) {
                    if (q < 0) continue;
                    int nd = d + 10 + risk[z];
                    if (nd < dd[q]) { dd[q] = nd; pq.push({nd, q}); }
                }
            }
        }

        // ---- flag plan ----
        vector<int> fa = myF;
        vector<FUnit> fu;
        vector<char> taken(nBld, 0);
        for (auto& b : B) {
            if (b.id < 0 || b.owner == 0) continue;
            if (fa[b.z] > 0 && !banned[b.z]) {
                fu.push_back({b.z, b.z, b.id});
                fa[b.z]--; taken[b.id] = 1;
            }
        }
        struct Cand { int util, src, bid; };
        vector<Cand> cand;
        for (int z = 0; z < N; z++) {
            if (fa[z] <= 0) continue;
            for (auto& b : B) {
                if (b.id < 0 || b.owner == 0 || taken[b.id]) continue;
                int d = dist[z][b.z];
                if (d >= INF) continue;
                if (turn + d + (b.owner == 1 ? 1 : 0) > 161) continue;
                int val = capWorth(b);
                int contest = max(0, Tpot2[b.z] - 0);
                int mine2 = 0;
                for (int q = 0; q < N; q++) if (dist[q][b.z] <= 2) mine2 += myW[q];
                val -= 35 * max(0, contest - mine2);
                if (enF[b.z] > 0) val -= 120;
                val = max(val, 5);
                cand.push_back({val * 1000 / (route[b.id][z] + 25), z, b.id});
            }
        }
        sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b) {
            if (a.util != b.util) return a.util > b.util;
            if (a.src != b.src) return a.src < b.src;
            return a.bid < b.bid;
        });
        vector<int> fTgt;  // pending (src, bid)
        for (auto& c : cand) {
            if (fa[c.src] <= 0 || taken[c.bid]) continue;
            fu.push_back({c.src, c.src, c.bid});
            fa[c.src]--; taken[c.bid] = 1;
        }
        for (int z = 0; z < N; z++) while (fa[z] > 0) { fu.push_back({z, z, -1}); fa[z]--; }

        // choose the actual step for each flag
        for (auto& f : fu) {
            int z = f.src;
            int best = INF, dest = z;
            vector<int> ch{z};
            for (int q : nb[z]) if (q >= 0) ch.push_back(q);
            bool lockedHere = (f.tgt >= 0 && B[f.tgt].z == z);
            for (int q : ch) {
                int sc = 0;
                int deficit = max(0, Tpot[q] - Mnear[q]);
                sc += deficit * 100000;
                if (banned[q] && q != z) sc += 50000;
                if (banned[q] && q == z) sc += 50000;
                if (enF[q] > 0) sc += 300;
                if (f.tgt >= 0) sc += route[f.tgt][q];
                else {
                    // idle flag: prefer to stay near home buildings, away from danger
                    sc += (bAt[q] >= 0 && B[bAt[q]].owner == 0 ? 0 : 6) + dist[q][myBase];
                }
                if (q == z) sc += 7;
                if (lockedHere && q == z) sc -= 40;
                if (sc < best) { best = sc; dest = q; }
            }
            f.dst = dest;
            po.fDestCnt[dest]++;
        }

        // ---- warrior plan ----
        vector<int> wAvail = myW;
        vector<int> planned(N, 0);
        vector<Mv> moves;
        auto commitStay = [&](int z, int n) { wAvail[z] -= n; planned[z] += n; };
        auto commitMove = [&](int s, int d, int n) {
            if (n <= 0) return;
            wAvail[s] -= n; planned[d] += n;
            if (s != d) moves.push_back({s, d, n});
        };
        auto gather = [&](int z, int need) -> bool {
            int cap = planned[z] + wAvail[z];
            for (int q : nb[z]) if (q >= 0) cap += wAvail[q];
            if (cap < need) return false;
            int rem = need - planned[z];
            if (rem <= 0) return true;
            int t = min(rem, wAvail[z]);
            if (t > 0) { commitStay(z, t); rem -= t; }
            vector<int> ns;
            for (int q : nb[z]) if (q >= 0 && wAvail[q] > 0) ns.push_back(q);
            sort(ns.begin(), ns.end(), [&](int a, int b) { if (wAvail[a] != wAvail[b]) return wAvail[a] > wAvail[b]; return a < b; });
            for (int q : ns) {
                if (rem <= 0) break;
                int n = min(rem, wAvail[q]);
                commitMove(q, z, n); rem -= n;
            }
            return rem <= 0;
        };
        auto relaxedNeed = [&](int z) {
            int extra = max(0, Tpot[z] - enW[z]);
            return enW[z] + (extra + 1) / 2 + 1;
        };

        vector<Op> ops;
        // guards for flags
        for (int z = 0; z < N; z++) if (po.fDestCnt[z] > 0 && Tpot[z] > 0) ops.push_back({z, 1000000, Tpot[z], 0});
        // kill enemy flags
        for (int z = 0; z < N; z++) if (enF[z] > 0) {
            int val = 700 + 40 * min(6, enF[z]);
            if (bAt[z] >= 0) val += max(0, capWorth(B[bAt[z]])) / 2 + (B[bAt[z]].owner == 0 ? 200 : 0);
            ops.push_back({z, val, relaxedNeed(z), 1});
        }
        // defend own buildings with enemy flags approaching
        for (auto& b : B) {
            if (b.id < 0 || b.owner != 0) continue;
            int df = INF;
            for (int z = 0; z < N; z++) if (enF[z] > 0 && z != b.z) df = min(df, dist[z][b.z]);
            if (df <= 3) ops.push_back({b.z, 500 + ownWorth(b) - df * 50, max(1, relaxedNeed(b.z)), 2});
        }
        // attack buildings we want that have defenders around
        for (auto& b : B) {
            if (b.id < 0 || b.owner == 0) continue;
            int df = INF;
            for (int z = 0; z < N; z++) if (enF[z] > 0) df = min(df, dist[z][b.z]);
            if (Tpot2[b.z] > 0 || df <= 2) {
                ops.push_back({b.z, capWorth(b), max(1, relaxedNeed(b.z)), 3});
            }
        }

        // -------- pass A: guards --------
        {
            vector<Op*> g;
            for (auto& o : ops) if (o.cls == 0) g.push_back(&o);
            sort(g.begin(), g.end(), [&](Op* a, Op* b) {
                int va = bAt[a->z] >= 0 ? ownWorth(B[bAt[a->z]]) : 0, vb = bAt[b->z] >= 0 ? ownWorth(B[bAt[b->z]]) : 0;
                if (va != vb) return va > vb;
                return a->z < b->z;
            });
            for (auto* o : g) if (!gather(o->z, o->need)) po.failed.push_back(o->z);
        }

        // -------- TELE conveyor (warriors) --------
        auto pullOps = [&](int z) {
            int best = 0;
            for (auto& o : ops) {
                if (o.cls == 0) continue;
                int d = dist[z][o.z]; if (d >= INF) continue;
                best = max(best, o.value * 100 / (d + 4));
            }
            return best;
        };
        // rally target (best approachable op / building even if not yet feasible)
        int mainCell = -1, mainN = 0;
        for (int z = 0; z < N; z++) if (myW[z] > mainN) { mainN = myW[z]; mainCell = z; }
        int rallyZ = -1;
        {
            int bestU = -INF;
            for (auto& b : B) {
                if (b.id < 0 || b.owner == 0) continue;
                int from = mainCell >= 0 ? mainCell : myBase;
                int d = dist[from][b.z]; if (d >= INF) continue;
                int u = capWorth(b) * 100 / (d + 5);
                // avoid rallying into an enemy fortress
                u -= max(0, Tpot2[b.z] - totalMyW) * 20;
                if (u > bestU) { bestU = u; rallyZ = b.z; }
            }
            for (auto& o : ops) if (o.cls == 1 && o.z >= 0) {
                int from = mainCell >= 0 ? mainCell : myBase;
                int d = dist[from][o.z]; if (d >= INF) continue;
                int u = o.value * 100 / (d + 5);
                if (u > bestU) { bestU = u; rallyZ = o.z; }
            }
            if (rallyZ >= 0) ops.push_back({rallyZ, 50, 0, 4});
        }
        if (stations >= 2) {
            vector<int> st;
            for (auto& b : B) if (b.id >= 0 && b.owner == 0 && b.type == STATION) st.push_back(b.z);
            long long bestGain = 0; int bA = -1, bB = -1, bN = 0;
            for (int a : st) {
                if (wAvail[a] <= 0) continue;
                int n = min(5, wAvail[a]);
                if (Tpot[a] > 0) n = min(n, max(0, wAvail[a] + planned[a] - Tpot[a]));
                if (n <= 0) continue;
                for (int b : st) {
                    if (a == b) continue;
                    if (Tpot[b] > 0 && planned[b] + wAvail[b] + n < Tpot[b]) continue;
                    int diff = pullOps(b) - pullOps(a);
                    if (diff < 250) continue;
                    long long gain = (long long)diff * n;
                    if (gain > bestGain) { bestGain = gain; bA = a; bB = b; bN = n; }
                }
            }
            if (bA >= 0) {
                po.teles.push_back({bA, bB, bN, 'W'});
                wAvail[bA] -= bN; planned[bB] += bN;
            }
        }

        // -------- pass B: immediate strikes --------
        {
            vector<Op*> g;
            for (auto& o : ops) if (o.cls >= 1 && o.cls <= 3) g.push_back(&o);
            sort(g.begin(), g.end(), [&](Op* a, Op* b) {
                if (a->cls != b->cls) return a->cls < b->cls;
                if (a->value != b->value) return a->value > b->value;
                return a->z < b->z;
            });
            for (auto* o : g) {
                if (gather(o->z, o->need)) o->cls += 10;  // done
            }
        }

        // step helper for warriors
        auto stepFor = [&](int s, int n, int tz, int holdDist) {
            int best = INF, dz = s;
            vector<int> ch{s};
            for (int q : nb[s]) if (q >= 0) ch.push_back(q);
            for (int q : ch) {
                int dd = dist[q][tz]; if (dd >= INF) continue;
                int total = planned[q] + n;
                int danger = max(0, Tpot[q] - total);
                int sc = dd * 30 + danger * 85 - min(20, planned[q]) * 2;
                if (dd < holdDist) sc += 1000;
                if (q == s) sc += 3;
                if (sc < best) { best = sc; dz = q; }
            }
            return dz;
        };
        auto sourcesSorted = [&]() {
            vector<int> s;
            for (int z = 0; z < N; z++) if (wAvail[z] > 0) s.push_back(z);
            sort(s.begin(), s.end(), [&](int a, int b) { if (wAvail[a] != wAvail[b]) return wAvail[a] > wAvail[b]; return a < b; });
            return s;
        };

        // -------- pass C: approach ops --------
        {
            vector<Op*> g;
            for (auto& o : ops) if (o.cls >= 1 && o.cls <= 3) g.push_back(&o);
            auto util = [&](Op* o) {
                int dmin = INF;
                for (int z = 0; z < N; z++) if (wAvail[z] > 0) dmin = min(dmin, dist[z][o->z]);
                if (dmin >= INF) return -INF;
                return o->value * 100 / (dmin + 4);
            };
            vector<pair<int, Op*>> gu;
            for (auto* o : g) gu.push_back({util(o), o});
            sort(gu.begin(), gu.end(), [](auto& a, auto& b) { return a.first > b.first; });
            for (auto& pr : gu) {
                Op* o = pr.second;
                if (pr.first <= -INF) continue;
                int tot = 0;
                vector<pair<int, int>> ss;  // dist, cell
                for (int z = 0; z < N; z++) if (wAvail[z] > 0 && dist[z][o->z] <= 7) { ss.push_back({dist[z][o->z], z}); tot += wAvail[z]; }
                if (tot < o->need) continue;
                sort(ss.begin(), ss.end());
                int acc = 0;
                for (auto& pz : ss) {
                    int z = pz.second; int n = wAvail[z];
                    int dz = stepFor(z, n, o->z, 1);
                    if (dz == z) commitStay(z, n); else commitMove(z, dz, n);
                    acc += n;
                    if (acc >= o->need + 1) break;
                }
            }
        }

        // -------- pass D: rally leftovers --------
        {
            int tz = rallyZ;
            for (int s : sourcesSorted()) {
                int n = wAvail[s];
                if (tz < 0) { commitStay(s, n); continue; }
                int dz = stepFor(s, n, tz, 2);
                if (dz == s) commitStay(s, n); else commitMove(s, dz, n);
            }
        }

        po.wmoves = moves;
        po.funits = fu;

        // fix: flags whose final cell is unsafe after planning -> report failure so they get banned
        {
            vector<int> plannedAll = planned;
            for (int z = 0; z < N; z++) {
                if (po.fDestCnt[z] > 0 && Tpot[z] > plannedAll[z]) {
                    bool have = false;
                    for (int f : po.failed) if (f == z) have = true;
                    if (!have) po.failed.push_back(z);
                }
            }
        }
        return po;
    }

    // ---------------- output ----------------
    void emit(vector<string>& out, PlanOut& plan) {
        // teleports
        for (auto& t : plan.teles) {
            auto [sx, sy] = toO(t.src);
            auto [tx, ty] = toO(t.dst);
            out.push_back(p::tele(sx, sy, string(1, t.kind), t.n, tx, ty));
        }
        // flags
        map<pair<int, int>, int> fm;
        vector<int> fFinal(N, 0);
        for (auto& f : plan.funits) {
            if (f.dst != f.src) fm[{f.src, f.dst}]++;
            fFinal[f.dst]++;
        }
        map<pair<int, int>, int> wm;
        for (auto& m : plan.wmoves) wm[{m.s, m.d}] += m.n;
        // priority for captures
        vector<pair<int, int>> pr;  // (value, cell)
        for (auto& b : B) {
            if (b.id < 0 || b.owner == 0 || fFinal[b.z] <= 0) continue;
            pr.push_back({capWorth(b), b.z});
        }
        sort(pr.begin(), pr.end(), [](auto& a, auto& b) { if (a.first != b.first) return a.first > b.first; return a.second < b.second; });
        if (!pr.empty()) {
            vector<pair<int, int>> co;
            for (auto& q : pr) co.push_back(toO(q.second));
            out.push_back(p::priority(co));
        }
        for (auto& kv : wm) {
            auto [x, y] = toO(kv.first.first);
            out.push_back(p::move(x, y, "W", kv.second, dirO(kv.first.first, kv.first.second)));
        }
        for (auto& kv : fm) {
            auto [x, y] = toO(kv.first.first);
            out.push_back(p::move(x, y, "F", kv.second, dirO(kv.first.first, kv.first.second)));
        }
        scoutMoves(out, plan);
    }

    void scoutMoves(vector<string>& out, PlanOut&) {
        for (int z = 0; z < N; z++) {
            for (int k = 0; k < myS[z]; k++) {
                // best unknown building
                int tgt = -1, bu = -INF;
                for (auto& b : B) {
                    if (b.id < 0 || knownScore[b.id] >= 0) continue;
                    int d = dist[z][b.z]; if (d >= INF) continue;
                    int x = X(b.z);
                    int val = (x >= 5 && x <= 9) ? 120 : 40;
                    int u = val * 100 / (d + 3);
                    if (u > bu) { bu = u; tgt = b.z; }
                }
                if (tgt < 0) break;
                // choose destination in <=2 steps
                int bestSc = INF, bestDest = z, bestD1 = -1, bestD2 = -1;
                const int ddx[4] = {0, 0, -1, 1}, ddy[4] = {-1, 1, 0, 0};
                (void)ddx; (void)ddy;
                vector<array<int, 3>> opts;  // dest, d1, d2
                opts.push_back({z, -1, -1});
                for (int a = 0; a < 4; a++) {
                    int m = nb[z][a]; if (m < 0) continue;
                    opts.push_back({m, a, -1});
                    for (int b = 0; b < 4; b++) {
                        int q = nb[m][b]; if (q < 0) continue;
                        opts.push_back({q, a, b});
                    }
                }
                for (auto& o : opts) {
                    int q = o[0];
                    int sc = (cheb(q, tgt) <= 2 ? 0 : dist[q][tgt] * 10);
                    sc += (Tpot[q] > 0 ? 400 : 0) + (enW[q] > 0 ? 600 : 0);
                    // also avoid cells adjacent to enemy warriors
                    for (int nq : nb[q]) if (nq >= 0 && enW[nq] > 0) { sc += 150; break; }
                    if (o[1] < 0) sc += 2;
                    if (sc < bestSc) { bestSc = sc; bestDest = q; bestD1 = o[1]; bestD2 = o[2]; }
                }
                if (bestDest == z) continue;
                auto [x, y] = toO(z);
                const char* names[4] = {"U", "D", "L", "R"};
                auto rot = [&](int d) { return flip ? (d ^ 1 ^ 0) : d; };
                // internal dir index: 0 U,1 D,2 L,3 R. flipped: U<->D, L<->R
                auto flipDir = [&](int d) { if (!flip) return d; return d ^ 1; };
                (void)rot;
                if (bestD2 < 0) out.push_back(p::move(x, y, "S", 1, names[flipDir(bestD1)]));
                else out.push_back(p::move2(x, y, 1, names[flipDir(bestD1)], names[flipDir(bestD2)]));
            }
        }
    }
};

Bot bot;
}  // namespace

vector<string> decide(const p::View& view, const p::Init& init) { return bot.decide(view, init); }
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    return p::run(decide);
}

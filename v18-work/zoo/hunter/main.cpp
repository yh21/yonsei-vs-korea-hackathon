// hunter: "flag hunter" bot.
// Style: one mobile mass of warriors (the anchor stack) that only steps into cells where it is
// strictly stronger than everything the enemy can bring there this turn, escorts a few flag
// bearers, snipes enemy flag bearers (which also rips flags off enemy buildings) and retreats to
// merge with reinforcements when outnumbered. Scouts reveal building scores.
// C++20, standard library only. No coordinates are hard-coded: everything is home-normalised.

#include "protocol.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <cstdlib>
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

#ifdef TUNE
double prm(int i, double d) {
    static double c[64];
    static bool init[64];
    if (!init[i]) {
        init[i] = true;
        char n[8];
        snprintf(n, 8, "P%d", i);
        const char* e = getenv(n);
        c[i] = e ? atof(e) : d;
    }
    return c[i];
}
#else
constexpr double prm(int, double d) { return d; }
#endif

FILE* dbg() {
    static FILE* f = nullptr;
    static bool init = false;
    if (!init) { init = true; const char* e = getenv("HUNTER_LOG"); if (e) f = fopen(e, "a"); }
    return f;
}
#define DLOG(...) do { if (FILE* _f = dbg()) { fprintf(_f, __VA_ARGS__); fflush(_f); } } while (0)

struct Bld {
    int id = 0, cell = 0, owner = 0, stage = 0, score = -1;  // owner 0 neutral, 1 me, 2 opp
    string type;
};

struct MoveRec {
    int cell, kind, cnt, dest;  // kind 0=F 1=W 2=S
};

struct Hunter {
    bool ready = false;
    int W = 15, H = 15, N = 225;
    string me, opp;
    bool flip = false;
    vector<vector<int>> dist, nb, n1;
    vector<int> symId, knownScore, idCell;
    vector<string> idType;
    vector<char> myDepot;
    int meBase = 0, opBase = 0;
    int waitStreak = 0, waitF = 0;

    // per turn
    int turn = 0, res = 0, opRes = 0;
    vector<int> myW, myF, myS, enW, enF, enS;
    vector<int> remW, remF, remS, endW, endF, endS;
    vector<int> thr;
    vector<Bld> blds;
    vector<int> bAt;  // cell -> index into blds or -1
    int myEng = 0, opEng = 0, myHall = 0, opHall = 0, costW = 3, opCostW = 3;
    bool myLib = false;
    int income = 10, opIncome = 10;
    vector<int> mySites, opSites;
    vector<MoveRec> moves;
    int scoutTargetCell = -1;

    // ------------------------------------------------------------------ geometry
    int idx(int x, int y) const { return y * W + x; }
    int cx(int c) const { return c % W; }
    int cy(int c) const { return c / W; }
    int homeKey(int c) const {
        int x = cx(c), y = cy(c);
        if (flip) { x = W - 1 - x; y = H - 1 - y; }
        return y * W + x;
    }
    string dirTo(int a, int b) const {
        int d = b - a;
        if (d == -W) return "U";
        if (d == W) return "D";
        if (d == -1) return "L";
        return "R";
    }

    void initOnce(const p::Init& init) {
        if (ready) return;
        ready = true;
        W = init.width; H = init.height; N = W * H;
        me = init.team; opp = init.opp;
        flip = init.base().first * 2 > W - 1;
        meBase = idx(init.base().first, init.base().second);
        auto ob = init.bases[me == "Y" ? 1 : 0];
        opBase = idx(ob.first, ob.second);
        int maxid = 0;
        for (auto& b : init.buildings) maxid = max(maxid, b.id);
        knownScore.assign(maxid + 1, -1); symId.assign(maxid + 1, -1); idCell.assign(maxid + 1, 0);
        idType.assign(maxid + 1, ""); myDepot.assign(maxid + 1, 0);
        map<int, int> at;
        for (auto& b : init.buildings) {
            idCell[b.id] = idx(b.x, b.y); idType[b.id] = b.type;
            if (b.type == "PLAZA") knownScore[b.id] = 3;
            at[idx(b.x, b.y)] = b.id;
        }
        for (auto& b : init.buildings) {
            auto it = at.find(idx(W - 1 - b.x, H - 1 - b.y));
            if (it != at.end()) symId[b.id] = it->second;
        }
        // neighbours in home-normalised order
        vector<pair<int, int>> dv = flip ? vector<pair<int, int>>{{0, 1}, {0, -1}, {1, 0}, {-1, 0}}
                                         : vector<pair<int, int>>{{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        nb.assign(N, {}); n1.assign(N, {});
        for (int c = 0; c < N; c++) {
            if (!init.passable(cx(c), cy(c))) continue;
            for (auto [dx, dy] : dv) {
                int nx = cx(c) + dx, ny = cy(c) + dy;
                if (init.passable(nx, ny)) nb[c].push_back(idx(nx, ny));
            }
            n1[c].push_back(c);
            for (int q : nb[c]) n1[c].push_back(q);
        }
        dist.assign(N, vector<int>(N, INF));
        for (int s = 0; s < N; s++) {
            if (!init.passable(cx(s), cy(s))) continue;
            queue<int> q; q.push(s); dist[s][s] = 0;
            while (!q.empty()) {
                int v = q.front(); q.pop();
                for (int u : nb[v]) if (dist[s][u] == INF) { dist[s][u] = dist[s][v] + 1; q.push(u); }
            }
        }
    }

    // ------------------------------------------------------------------ knowledge
    int scoreOf(const Bld& b) const {  // best guess of the score
        int k = knownScore[b.id];
        if (k >= 0) return k * 2;  // doubled units (so that expectations can be half points)
        if (b.type == "PLAZA") return 6;
        if (b.type == "DEPOT" || b.type == "WATCH") return 6;
        if (b.type == "STATION") {
            int d1 = dist[b.cell][meBase], d2 = dist[b.cell][opBase];
            return abs(d1 - d2) <= 4 ? 6 : 3;
        }
        return 3;
    }
    // value of owning building b (in "half points" units *  weights)
    double bval(const Bld& b) const {
        double s = scoreOf(b) / 2.0;
        double val = 2.0 * s;
        if (b.owner == 2) val = 3.4 * s;
        double f = 0;
        double fade = turn < 100 ? 1.0 : (turn < 135 ? (135 - turn) / 35.0 : 0.0);
        const string& t = b.type;
        if (t == "ENG") f = myEng == 0 ? prm(0, 10.0) : (b.owner == 2 && opEng == 1 ? prm(25, 10.0) : 0.3);
        else if (t == "HALL") f = prm(1, 5.0) + (b.owner == 2 ? 2.0 : 0);
        else if (t == "DEPOT") f = (b.id < (int)myDepot.size() && myDepot[b.id]) ? 0.3 : prm(2, 5.0);
        else if (t == "HOSPITAL") f = prm(3, 2.5);
        else if (t == "LIBRARY") f = myLib ? 0.3 : 1.5;
        else if (t == "STATION") f = 1.0;
        else if (t == "WATCH") f = unknownCount() > 0 ? 1.8 : 0.5;
        else if (t == "PLAZA") f = 1.0;
        return val + f * fade;
    }
    // value of KEEPING an owned building (what a loss would cost me)
    double keepVal(const Bld& b) const {
        double v = bval(b);
        if (b.owner != 1) return v;
        double fade = turn < 100 ? 1.0 : (turn < 135 ? (135 - turn) / 35.0 : 0.0);
        const string& t = b.type;
        if (t == "ENG") v += (myEng <= 1 ? prm(47, 25.0) : 4.0) * fade;
        else if (t == "HALL") v += prm(48, 10.0) * fade;
        return v;
    }
    // value of a building for flag raids: economy buildings of the enemy are worth an extra push
    double fval(const Bld& b) const {
        double v = bval(b);
        if (b.owner == 2 && turn < 110) {
            if (b.type == "ENG" && opEng == 1) v += prm(57, 12.0);
            else if (b.type == "HALL") v += prm(58, 6.0);
        }
        return v;
    }
    int unknownCount() const {
        int c = 0;
        for (auto& b : blds) if (knownScore[b.id] < 0) c++;
        return c;
    }
    void updateMemory(const p::View& v) {
        for (auto& b : v.buildings) {
            if (b.id >= (int)knownScore.size()) continue;
            if (b.score >= 0) {
                knownScore[b.id] = b.score;
                int s = symId[b.id];
                if (s >= 0) knownScore[s] = b.score;
            }
            if (b.type == "DEPOT" && b.owner == me) myDepot[b.id] = 1;
        }
    }

    // ------------------------------------------------------------------ helpers
    double regionMy(int t, int k) const {
        double s = 0;
        for (int q = 0; q < N; q++) if (myW[q] > 0 && dist[q][t] <= k) s += myW[q];
        for (int site : mySites) if (dist[site][t] <= k) s += 0.5 * income * (k - dist[site][t]) / costW;
        return s;
    }
    double regionEn(int t, int k) const {
        double s = 0;
        for (int q = 0; q < N; q++) if (enW[q] > 0 && dist[q][t] <= k) s += enW[q];
        for (int site : opSites)
            if (dist[site][t] <= k) s += (opRes + 0.6 * opIncome * (k - dist[site][t])) / opCostW;
        return s;
    }
    int joinMass(int c) const {
        int s = 0;
        for (int q : n1[c]) s += remW[q];
        return s;
    }
    void assign(int cell, int kind, int cnt, int dest) {
        if (cnt <= 0) return;
        if (kind == 0) { remF[cell] -= cnt; endF[dest] += cnt; }
        else if (kind == 1) { remW[cell] -= cnt; endW[dest] += cnt; }
        else { remS[cell] -= cnt; endS[dest] += cnt; }
        if (dest != cell) moves.push_back({cell, kind, cnt, dest});
    }

    // ------------------------------------------------------------------ main
    vector<string> decide(const p::View& v, const p::Init& init) {
        initOnce(init);
        turn = v.turn; res = v.my_resource; opRes = v.opp_resource;
        myW.assign(N, 0); myF = myW; myS = myW; enW = myW; enF = myW; enS = myW;
        for (auto& u : v.units) {
            int z = idx(u.x, u.y);
            if (u.team == me) (u.kind == "F" ? myF : u.kind == "W" ? myW : myS)[z] += u.count;
            else (u.kind == "F" ? enF : u.kind == "W" ? enW : enS)[z] += u.count;
        }
        updateMemory(v);
        blds.clear(); bAt.assign(N, -1);
        for (auto& b : v.buildings) {
            Bld x; x.id = b.id; x.cell = idx(b.x, b.y); x.type = b.type; x.stage = b.stage;
            x.owner = b.owner == me ? 1 : (b.owner == opp ? 2 : 0);
            x.score = knownScore[b.id];
            bAt[x.cell] = (int)blds.size();
            blds.push_back(x);
        }
        // economy
        myEng = opEng = myHall = opHall = 0; myLib = false;
        mySites = {meBase}; opSites = {opBase};
        for (auto& b : blds) {
            if (b.owner == 1) {
                if (b.type == "ENG") myEng++;
                else if (b.type == "HALL") myHall++;
                else if (b.type == "LIBRARY") myLib = true;
                else if (b.type == "HOSPITAL") mySites.push_back(b.cell);
            } else if (b.owner == 2) {
                if (b.type == "ENG") opEng++;
                else if (b.type == "HALL") opHall++;
                else if (b.type == "HOSPITAL") opSites.push_back(b.cell);
            }
        }
        costW = max(2, 3 - myEng); opCostW = max(2, 3 - opEng);
        income = 10 + 2 * myHall; opIncome = 10 + 2 * opHall;

        computeThreat();
        moves.clear();
        vector<string> out;
        planSpawn(out);
        remW = myW; remF = myF; remS = myS;
        endW.assign(N, 0); endF = endW; endS = endW;
        assignFlagGoals();
        planWarriors();
        moveFlags();
        planScouts();
        emitMoves(out);
        return out;
    }

    void computeThreat() {
        thr.assign(N, 0);
        for (int c = 0; c < N; c++) {
            int s = 0;
            for (int q : n1[c]) s += enW[q];
            thr[c] = s;
        }
        int spawnPot = opRes / opCostW;
        vector<char> mark(N, 0);
        for (int s : opSites) for (int q : n1[s]) mark[q] = 1;
        for (int q = 0; q < N; q++) if (mark[q]) thr[q] += spawnPot;
        vector<int> st;
        for (auto& b : blds) if (b.owner == 2 && b.type == "STATION") st.push_back(b.cell);
        if (st.size() >= 2)
            for (int d : st) {
                int m = 0;
                for (int s : st) if (s != d) m = max(m, enW[s]);
                thr[d] += min(5, m);
            }
    }

    // ------------------------------------------------------------------ spawning
    void planSpawn(vector<string>& out) {
        int curF = 0, curS = 0;
        for (int c = 0; c < N; c++) { curF += myF[c]; curS += myS[c]; }
        // pending capture costs
        double pend = 0;
        for (auto& b : blds) {
            if (b.owner == 1) continue;
            int cst = max(1, (b.type == "PLAZA" ? 4 : 2) - (myLib ? 1 : 0));
            if (myF[b.cell] > 0) pend += cst;
            else {
                bool f = false;
                for (int q : nb[b.cell]) f = f || myF[q] > 0;
                if (f) pend += 0.5 * cst;
            }
        }
        int reserve = max(0, (int)pend - income);
        int budget = max(0, res - reserve);
        int nonOwned = 0;
        for (auto& b : blds) if (b.owner != 1) nonOwned++;
        int needF = turn <= 30 ? (int)prm(12, 8) : (turn <= 100 ? (int)prm(17, 8) : (int)prm(44, 6));
        if (turn >= 148) needF = 0;
        needF = min(needF, nonOwned);
        int nF = max(0, min({needF - curF, budget / 5, turn <= (int)prm(43, 10) ? 2 : 1}));
        if (turn == 1) nF = min(2, budget / 5);
        budget -= nF * 5;
        int nS = 0;
        int needS = (turn >= 2 && turn <= 70 && unknownCount() > 0) ? (int)prm(59, 2) : 0;
        if (curS < needS && budget >= 2) nS = 1;
        budget -= nS * 2;
        int nW = budget / costW;
        // choose the spawn site: base or a hospital that is closer to the action
        int site = meBase;
        int refCell = -1;
        {
            long best = -1;
            for (int c = 0; c < N; c++) if (myW[c] > 0) {
                long m = 0;
                for (int q = 0; q < N; q++) if (myW[q] > 0 && dist[c][q] <= 2) m += myW[q];
                if (m > best) { best = m; refCell = c; }
            }
        }
        bool homeQuiet = true;
        for (int c = 0; c < N; c++) if ((enW[c] > 0 || enF[c] > 0) && dist[c][meBase] <= 6) homeQuiet = false;
        if (refCell >= 0 && mySites.size() > 1 && homeQuiet && prm(32, 1.0) > 0.5) {
            int bestd = dist[meBase][refCell];
            for (int s : mySites) {
                if (s == meBase) continue;
                if (thr[s] > myW[s] + nW) continue;
                if (dist[s][refCell] + (int)prm(33, 5) < bestd) { bestd = dist[s][refCell] + (int)prm(33, 5); site = s; }
            }
        }
        auto emit = [&](const string& k, int n) {
            if (n <= 0) return;
            if (site == meBase) out.push_back(p::spawn(k, n));
            else out.push_back(p::spawn(k, n, cx(site), cy(site)));
            (k == "F" ? myF : k == "W" ? myW : myS)[site] += n;
        };
        emit("F", nF); emit("S", nS); emit("W", nW);
    }

    // ------------------------------------------------------------------ garrisons (small detachments)
    struct GMem { int cell, target, cnt; };
    vector<GMem> gMem, newMem;
    vector<int> gHave;
    int gUsed = 0, gBudget = 0;

    bool safeCell(int c, int mass) const { return thr[c] == 0 || mass > thr[c]; }

    int safeStepToward(int q, int goal, int cnt) {
        int pick = q; bool have = false; int bd = INF, bj = -INF;
        for (int c : n1[q]) {
            int m = endW[c] + cnt;
            if (!safeCell(c, m)) continue;
            int d = dist[c][goal];
            if (!have || d < bd || (d == bd && m > bj)) { have = true; bd = d; bj = m; pick = c; }
        }
        if (!have) {
            int bm = -INF;
            for (int c : n1[q]) {
                int m = endW[c] + cnt - thr[c];
                if (m > bm) { bm = m; pick = c; }
            }
        }
        return pick;
    }

    int garrisonNeed(int t) const {
        int need = (int)prm(11, 2);
        int eF = 0, eW = 0;
        for (int q = 0; q < N; q++) {
            if (enF[q] > 0 && dist[q][t] <= 5) eF += enF[q];
            if (enW[q] > 0 && dist[q][t] <= 2) eW += enW[q];
        }
        for (int s : opSites) if (dist[s][t] <= 2) eW += opRes / opCostW;
        if (eF > 0) need += 1;
        need = max(need, (int)ceil(prm(13, 1.1) * eW) + 1);
        return min(need, (int)prm(14, 12));
    }

    // W stacks that were sent to a building last turn keep going / keep standing there
    void continueGarrisons() {
        for (auto& g : gMem) {
            int c = g.cell;
            if (remW[c] <= 0) continue;
            int bi = bAt[g.target];
            if (bi < 0) continue;
            if (blds[bi].owner != 1) {
                bool wanted = false;
                for (auto& u : fUnits) if (u.goal == g.target) wanted = true;
                if (!wanted) continue;
            }
            int cnt = min(g.cnt, remW[c]);
            int dest = c == g.target ? c : safeStepToward(c, g.target, cnt);
            assign(c, 1, cnt, dest);
            gHave[g.target] += cnt; gUsed += cnt;
            newMem.push_back({dest, g.target, cnt});
        }
    }

    // flag bearers with a solo goal travel together with an escort taken from their own cell
    void planTeams() {
        for (auto& u : fUnits) {
            if (u.goal < 0 || u.stay || u.done) continue;
            int f = u.cell, g = u.goal;
            if (remF[f] <= 0) continue;
            int want = max(u.esc, max(0, garrisonNeed(g) - gHave[g]));
            int esc = min(remW[f], want);
            int pick = f; bool have = false; int bd = INF;
            for (int c : n1[f]) {
                int support = (c == f ? remW[f] : esc) + (c == f ? 0 : remW[c]);
                if (c == f) support = remW[f];
                if (!(thr[c] == 0 || support >= thr[c])) continue;
                int d = dist[c][g];
                if (!have || d < bd || (d == bd && support > 0 && c != f)) { have = true; bd = d; pick = c; }
            }
            if (!have) continue;  // falls back to the plain flag logic
            if (pick != f && esc > 0) {
                assign(f, 1, esc, pick);
                gHave[g] += esc; gUsed += esc;
                newMem.push_back({pick, g, esc});
            } else if (pick == f && remW[f] > 0 && f == g) {
                // capturing here: the escort already present stays as garrison
            }
            if (pick != f && remW[pick] > 0) {  // W already standing on the destination stay put
                assign(pick, 1, remW[pick], pick);
            }
            assign(f, 0, 1, pick);
            u.done = true;
            fMem.push_back({pick, g});
        }
    }

    void newGarrisons() {
        int totW = 0, nOwn = 0;
        for (int c = 0; c < N; c++) totW += myW[c];
        vector<int> order;
        for (auto& b : blds) if (b.owner == 1) { order.push_back(b.cell); nOwn++; }
        int softG = max(2, min((int)prm(18, 30), (int)(totW / (max(1, nOwn) * prm(19, 5.0)))));
        for (auto& u : fUnits) if (u.goal >= 0 && bAt[u.goal] >= 0 && blds[bAt[u.goal]].owner != 1) order.push_back(u.goal);
        sort(order.begin(), order.end(), [&](int a, int b) { return bval(blds[bAt[a]]) > bval(blds[bAt[b]]); });
        for (int t : order) {
            if (gUsed >= gBudget) break;
            int need = garrisonNeed(t);
            if (blds[bAt[t]].owner == 1) need = max(need, softG);
            if (blds[bAt[t]].type == "ENG" || blds[bAt[t]].type == "HALL") need += (int)prm(26, 5);
            int have = gHave[t] + remW[t];
            for (int g : groupCells) if (dist[g][t] <= 2) have += 1000;  // a hunting group covers it
            int deficit = need - have;
            while (deficit > 0 && gUsed < gBudget) {
                int bq = -1, bd = INF;
                for (int q = 0; q < N; q++) {
                    if (remW[q] <= 0 || q == t) continue;
                    int av = isGroup[q] ? (int)(remW[q] * prm(60, 0.0)) : remW[q];
                    if (av <= 0) continue;
                    int d = dist[q][t];
                    if (d < bd) { bd = d; bq = q; }
                }
                if (bq < 0 || bd > 12) break;
                int give = min(deficit, isGroup[bq] ? (int)(remW[bq] * prm(60, 0.0)) : remW[bq]);
                int dest = safeStepToward(bq, t, give);
                assign(bq, 1, give, dest);
                gHave[t] += give; gUsed += give; deficit -= give;
                newMem.push_back({dest, t, give});
            }
        }
    }

    // Reactive convergence: when hostile warriors can strike one of my buildings (or flag stacks) this
    // turn, adjacent SMALL stacks step in if that makes the cell strictly stronger than the threat.
    void planConvergence() {
        const int cmax = (int)prm(27, 25);
        vector<int> cells;
        for (auto& b : blds) if (b.owner == 1) cells.push_back(b.cell);
        for (int c = 0; c < N; c++) if (myF[c] > 0 && bAt[c] < 0) cells.push_back(c);
        sort(cells.begin(), cells.end(), [&](int a, int b) {
            double va = bAt[a] >= 0 ? bval(blds[bAt[a]]) : 4.0 * myF[a];
            double vb = bAt[b] >= 0 ? bval(blds[bAt[b]]) : 4.0 * myF[b];
            return va > vb;
        });
        for (int t : cells) {
            if (thr[t] <= 0) continue;
            int J = 0; bool any = false;
            for (int q : n1[t]) if (remW[q] > 0 && remW[q] <= cmax) { J += remW[q]; if (q != t) any = true; }
            if (!any || J <= thr[t]) continue;
            for (int q : n1[t]) if (remW[q] > 0 && remW[q] <= cmax) assign(q, 1, remW[q], t);
        }
    }

    // Strikes: an enemy stack (or flag) that all my adjacent stacks together beat outright, even if every
    // hostile warrior around could join the defence (thr), is attacked by all of them at once.
    void planStrikes() {
        struct S { int e; double v; };
        vector<S> list;
        for (int e = 0; e < N; e++) {
            if (enW[e] < (int)prm(49, 6) && enF[e] == 0) continue;
            int J = joinMass(e);
            int adj = 0;
            for (int q : n1[e]) if (q != e && remW[q] > 0) adj++;
            if (adj == 0 && remW[e] == 0) continue;
            if (J <= prm(52, 1.15) * thr[e] + (int)prm(50, 2)) continue;
            double v = enW[e] + 40.0 * enF[e];
            int bi = bAt[e];
            if (bi >= 0) v += 20.0 * (blds[bi].owner == 1 ? 1.5 : 1.0);
            bool near = bi >= 0;
            for (int q : n1[e]) if (bAt[q] >= 0 && blds[bAt[q]].owner == 1) near = true;
            if (enF[e] == 0 && !near && enW[e] < (int)prm(53, 40)) continue;
            list.push_back({e, v});
        }
        sort(list.begin(), list.end(), [&](const S& a, const S& b) { return a.v > b.v; });
        for (auto& x : list) {
            int e = x.e;
            int J = joinMass(e);
            if (J <= prm(52, 1.15) * thr[e] + (int)prm(50, 2)) continue;
            bool any = false;
            for (int q : n1[e]) if (remW[q] > 0 && q != e) any = true;
            if (!any) continue;
            for (int q : n1[e]) if (remW[q] > 0) assign(q, 1, remW[q], e);
        }
    }

    // ------------------------------------------------------------------ warriors
    struct Cand { int target; int kind; double score; };
    struct Group { int cell, next, target; };
    vector<Group> groups;
    vector<int> groupCells;
    vector<char> isGroup;
    vector<pair<int, int>> tMem, newTMem;  // (cell after move, target): hysteresis of missions
    vector<char> claimedT;
    int anchorCell = -1, anchorNext = -1, missionT = -1;

    void selectGroups() {
        groupCells.clear();
        isGroup.assign(N, 0);
        const int mmin = (int)prm(28, 1);
        // the main body: the cell with the largest mass within two steps (ties: the bigger single stack)
        int main0 = -1; long bestKey = -1;
        for (int c = 0; c < N; c++) if (remW[c] > 0) {
            long m = 0;
            for (int q = 0; q < N; q++) if (remW[q] > 0 && dist[c][q] <= 2) m += remW[q];
            long key = prm(55, 0.0) > 0.5 ? m * 100000 + remW[c] : (long)remW[c] * 100000 + m;
            if (key > bestKey || (key == bestKey && homeKey(c) < homeKey(main0))) { bestKey = key; main0 = c; }
        }
        if (main0 < 0) return;
        groupCells.push_back(main0);
        long mainMass = 0;
        for (int q = 0; q < N; q++) if (remW[q] > 0 && dist[main0][q] <= 2) mainMass += remW[q];
        if (mainMass >= (long)prm(54, 0)) {
            vector<int> cand;
            for (int c = 0; c < N; c++)
                if (dist[c][main0] > 2 && remW[c] >= mmin && remW[c] <= prm(34, 1.0) * mainMass) cand.push_back(c);
            sort(cand.begin(), cand.end(), [&](int a, int b) { return remW[a] > remW[b] || (remW[a] == remW[b] && homeKey(a) < homeKey(b)); });
            for (int c : cand) {
                bool near = false;
                for (int g : groupCells) if (dist[c][g] <= 2) near = true;
                if (!near) groupCells.push_back(c);
            }
        }
        for (int g : groupCells) isGroup[g] = 1;
    }

    void planWarriors() {
        anchorCell = anchorNext = missionT = -1;
        groups.clear();
        newMem.clear(); newTMem.clear();
        gHave.assign(N, 0);
        gUsed = 0;
        claimedT.assign(N, 0);
        int totW = 0;
        for (int c = 0; c < N; c++) totW += myW[c];
        gBudget = (int)(prm(10, 0.4) * totW);
        if (prm(20, 1.0) > 0.5) planConvergence();
        if (prm(51, 0.0) > 0.5) planStrikes();
        continueGarrisons();
        planTeams();
        selectGroups();
        gMem = newMem;
        newMem.clear();
        newGarrisons();
        for (auto& g : newMem) gMem.push_back(g);
        // groups may have lost stacks to garrison duty (never: donors exclude them), plan each of them
        vector<int> gc = groupCells;
        for (int g : gc) if (remW[g] > 0) planGroup(g);
        tMem = newTMem;
        if (!groups.empty()) { anchorCell = groups[0].cell; anchorNext = groups[0].next; missionT = groups[0].target; }

        // ---- reserves: snipe flags next to them or join the nearest hunting group
        vector<int> order;
        for (int c = 0; c < N; c++) if (remW[c] > 0) order.push_back(c);
        sort(order.begin(), order.end(), [&](int a, int b) { return remW[a] > remW[b] || (remW[a] == remW[b] && homeKey(a) < homeKey(b)); });
        for (int q : order) {
            if (remW[q] <= 0) continue;
            int bestE = -1; double bv = 0;
            for (int e : n1[q]) if (enF[e] > 0) {
                int J = joinMass(e);
                if (J > thr[e]) {
                    double v2 = 2.0 + enF[e] + (bAt[e] >= 0 && blds[bAt[e]].owner != 0 ? 2.0 : 0.0);
                    if (v2 > bv) { bv = v2; bestE = e; }
                }
            }
            if (bestE >= 0) {
                for (int u : n1[bestE]) if (remW[u] > 0) assign(u, 1, remW[u], bestE);
                continue;
            }
            int cnt = remW[q];
            int goalc = meBase; int bdg = INF;
            for (auto& g : groups) {
                int d = dist[q][g.next];
                if (d < bdg) { bdg = d; goalc = g.next; }
            }
            int pick = q; bool have = false; int bd = INF, bj = -INF;
            for (int c : n1[q]) {
                int m = endW[c] + cnt;
                if (!safeCell(c, m)) continue;
                int d = dist[c][goalc];
                if (!have || d < bd || (d == bd && m > bj)) { have = true; bd = d; bj = m; pick = c; }
            }
            if (!have) {
                int bm = -INF;
                for (int c : n1[q]) {
                    int m = endW[c] + cnt - thr[c];
                    if (m > bm) { bm = m; pick = c; }
                }
            }
            assign(q, 1, cnt, pick);
        }
    }

    void planGroup(int A) {
        int lastTarget = -1;
        for (auto& m : tMem) if (m.first == A) { lastTarget = m.second; break; }
        int nA = remW[A];
        double ratio = prm(4, 1.0);
        int myScore = 0, opScore = 0;
        for (auto& b : blds) {
            if (b.owner == 1) myScore += scoreOf(b);
            else if (b.owner == 2) opScore += scoreOf(b);
        }
        if (turn >= 100 && myScore < opScore) ratio = prm(5, 1.0);
        auto fDist = [&](int t) {
            int d = INF;
            for (auto& u : fUnits) if (u.goal < 0 && !u.stay) d = min(d, dist[u.cell][t]);
            if (d == INF) d = dist[meBase][t] + 3;
            return d;
        };
        long massTF = 0;
        for (int q = 0; q < N; q++) if (remW[q] > 0 && dist[A][q] <= (int)prm(35, 4) && (!isGroup[q] || q == A)) massTF += remW[q];
        const int hz = (int)prm(6, 3);
        auto feas = [&](int t, int k, double rat) {
            int h = min(k, hz);
            double my = (k <= hz) ? regionMy(t, h) : (double)massTF;
            double en = regionEn(t, h);
            return my >= rat * en + prm(31, 1.0);
        };
        const double hys = prm(7, 1.5);
        vector<Cand> cands;
        for (auto& b : blds) {
            int t = b.cell;
            int k = dist[A][t];
            if (k >= INF || claimedT[t]) continue;
            if (b.owner == 1) {
                double pressure = 0, fthreat = 0;
                for (int q = 0; q < N; q++) {
                    int d = dist[q][t];
                    if (enF[q] > 0 && d <= 3) fthreat += 1.0 * enF[q] / (d + 1);
                    if (enW[q] >= 3 && d <= 5) pressure += enW[q] * (d <= 1 ? 1.0 : d == 2 ? 0.7 : d == 3 ? 0.45 : 0.25);
                }
                double garrison = remW[t] + gHave[t];
                double danger = fthreat + max(0.0, pressure - garrison) / 8.0;
                if (danger <= 0) continue;
                if (!feas(t, k, ratio)) continue;
                double sc = 2.0 * keepVal(b) * min(1.0, danger) / pow(k + 2.0, prm(9, 1.3));
                if (k == 0) sc *= prm(15, 0.6);
                if (t == lastTarget) sc *= hys;
                cands.push_back({t, 2, sc});
                continue;
            }
            if (enF[t] > 0) continue;  // handled as a flag hunt below
            double val = bval(b);
            int fd = fDist(t);
            int Ttot = max(k, fd);
            int need = (b.owner == 2 ? 2 : 1);
            if (turn + Ttot + need > 158) continue;
                        double sc = val / pow(Ttot + 2.0, prm(9, 1.3));
            if (!feas(t, k, ratio)) continue;
            if (t == lastTarget) sc *= hys;
            cands.push_back({t, 0, sc});
        }
        for (int e = 0; e < N; e++) if (enF[e] > 0 && !claimedT[e]) {
            int k = dist[A][e];
            if (k >= INF || k > 6) continue;
            if (bAt[e] < 0 && k > 2) continue;
            if (!feas(e, k, ratio)) continue;
            double val = 4.0 + 0.6 * enF[e];
            int bi = bAt[e];
            if (bi >= 0) {
                const Bld& b = blds[bi];
                if (b.owner == 1) val += bval(b) * 1.2;
                else if (b.owner == 2) val += bval(b) * 0.9;
                else val += bval(b) * 0.4;
            }
            bool nearOwn = false;
            for (auto& b : blds) if (b.owner == 1 && dist[b.cell][e] <= 4) { nearOwn = true; break; }
            if (nearOwn) val *= prm(8, 2.0);
            double sc = val / pow(k + 2.0, prm(9, 1.3));
            if (e == lastTarget) sc *= hys;
            cands.push_back({e, 1, sc});
        }
        for (int e = 0; e < N; e++) if (enW[e] >= 3 && enF[e] == 0 && bAt[e] < 0 && !claimedT[e]) {
            int k = dist[A][e];
            if (k >= INF || k > 6) continue;
            double kr = enW[e] < 30 ? prm(45, 1.6) : (enW[e] < 100 ? prm(46, 1.3) : prm(29, 1.12));
            if (k <= 1) kr = prm(36, 1.0);
            else if (k == 2) kr = min(kr, prm(37, 1.15));
            if (!feas(e, k, kr)) continue;
            double near = 0;
            for (auto& b : blds) if (b.owner == 1 && dist[b.cell][e] <= 3) near += bval(b) * 0.25;
            double sc = (1.2 + prm(30, 0.1) * enW[e] + near) / pow(k + 2.0, prm(9, 1.3));
            if (e == lastTarget) sc *= hys;
            cands.push_back({e, 3, sc});
        }
        int T = -1; double bs = 0;
        for (auto& c : cands) if (c.score > bs) { bs = c.score; T = c.target; }
        if (T >= 0) claimedT[T] = 1;
        if (dbg()) {
            vector<Cand> cs = cands;
            sort(cs.begin(), cs.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
            fprintf(dbg(), "T%d cands@(%d,%d):", turn, cx(A), cy(A));
            for (int i = 0; i < (int)cs.size() && i < 6; i++) fprintf(dbg(), " (%d,%d)k%d:%.2f/%d", cx(cs[i].target), cy(cs[i].target), cs[i].kind, cs[i].score, dist[A][cs[i].target]);
            fprintf(dbg(), "\n");
        }

        // ---- choose next cell
        int goal = T >= 0 ? T : A;
        int chosen = A;
        {
            struct O { int c; bool safe; int d; int j; };
            vector<O> os;
            for (int c : n1[A]) os.push_back({c, safeCell(c, joinMass(c)), dist[c][goal], joinMass(c)});
            int bi = -1;
            for (int i = 0; i < (int)os.size(); i++) {
                if (!os[i].safe) continue;
                if (bi < 0) { bi = i; continue; }
                auto& a = os[i]; auto& b = os[bi];
                if (a.d < b.d || (a.d == b.d && a.j > b.j)) bi = i;
            }
            if (bi >= 0) chosen = os[bi].c;
            else {
                int bm = -INF;
                for (auto& o : os) {
                    int m = o.j - thr[o.c];
                    if (m > bm) { bm = m; chosen = o.c; }
                }
            }
        }
        // brief waits: merge big adjacent stragglers; let flag bearers catch up when a capture is imminent
        if (groups.empty() && chosen != A && safeCell(A, joinMass(A))) {
            long strag = 0;
            for (int q = 0; q < N; q++) if (remW[q] > 0 && q != A && dist[q][A] == 1 && !isGroup[q]) {
                bool inNext = false;
                for (int u : n1[chosen]) if (u == q) inNext = true;
                if (!inNext) strag += remW[q];
            }
            int fNear = 0, fComing = 0;
            for (auto& u : fUnits) if (u.goal < 0 && !u.stay) {
                if (dist[A][u.cell] <= 1) fNear++;
                else if (dist[A][u.cell] <= 3) fComing++;
            }
            bool nearTarget = T >= 0 && dist[A][T] <= 4 && bAt[T] >= 0;
            if (strag >= max(3L, (long)(prm(22, 0.25) * nA)) && waitStreak < 1) { chosen = A; waitStreak++; }
            else if (nearTarget && fNear == 0 && fComing > 0 && waitF < 3) { chosen = A; waitF++; waitStreak = 0; }
            else { waitStreak = 0; if (!nearTarget) waitF = 0; }
        } else if (groups.empty()) { waitStreak = 0; waitF = 0; }
        DLOG("T%d G=(%d,%d) n=%d massTF=%ld T=(%d,%d) chosen=(%d,%d) ncand=%d wait=%d/%d thr(A)=%d\n", turn, cx(A), cy(A), nA, massTF,
             T >= 0 ? cx(T) : -1, T >= 0 ? cy(T) : -1, cx(chosen), cy(chosen), (int)cands.size(), waitStreak, waitF, thr[A]);
        groups.push_back({A, chosen, T});
        newTMem.push_back({chosen, T});
        for (int q : n1[chosen]) if (remW[q] > 0 && (!isGroup[q] || q == A || dist[q][A] <= 1)) assign(q, 1, remW[q], chosen);
    }

    // ------------------------------------------------------------------ flag bearers
    struct FUnit { int cell; int goal; bool stay; bool done = false; int esc = 0; };
    vector<FUnit> fUnits;
    vector<pair<int, int>> fMem;  // (cell after move, goal) from the previous turn

    bool fSafe(int c) const { return thr[c] == 0 || endW[c] >= thr[c]; }

    bool soloOk(const Bld& b, int f) const {
        int d = dist[f][b.cell];
        if (d >= INF) return false;
        int rr = min(d + 1, (int)prm(16, 3));
        for (int q = 0; q < N; q++) if (enW[q] > 0 && dist[q][b.cell] <= rr) return false;
        for (int s : opSites) if (dist[s][b.cell] <= min(rr, (int)prm(56, 1))) return false;
        return true;
    }

    // decide, for every flag bearer, a solo target (goal >= 0), or to follow the anchor (goal = -1)
    void assignFlagGoals() {
        fUnits.clear();
        vector<char> claimed(N, 0);
        vector<pair<int, int>> mem = fMem;
        fMem.clear();
        vector<int> units;
        for (int c = 0; c < N; c++) if (remF[c] > 0) {
            int cnt = remF[c];
            int bi = bAt[c];
            if (bi >= 0 && blds[bi].owner != 1 && !claimed[c]) {  // capturing in place
                claimed[c] = 1; cnt--;
                fUnits.push_back({c, c, true});
                // drop one remembered goal for this cell
                for (size_t i = 0; i < mem.size(); i++) if (mem[i].first == c) { mem.erase(mem.begin() + i); break; }
            }
            for (int i = 0; i < cnt; i++) units.push_back(c);
        }
        vector<char> done(units.size(), 0);
        // sticky goals from last turn
        for (int u = 0; u < (int)units.size(); u++) {
            int f = units[u];
            for (size_t i = 0; i < mem.size(); i++) if (mem[i].first == f) {
                int g = mem[i].second;
                mem.erase(mem.begin() + i);
                if (g < 0 || bAt[g] < 0 || claimed[g]) break;
                const Bld& b = blds[bAt[g]];
                int d = dist[f][g];
                if (b.owner == 1 || d >= INF || turn + d + (b.owner == 2 ? 2 : 1) > 158 || !soloOk(b, f)) break;
                done[u] = 1; claimed[g] = 1;
                fUnits.push_back({f, g, false});
                break;
            }
        }
        // spare warriors that may escort a flag bearer standing in the same cell
        vector<int> spare(N, 0);
        for (int c = 0; c < N; c++) if (remW[c] >= 6) {
            int keep = thr[c] > 0 ? thr[c] + 2 : (int)(prm(38, 0.4) * remW[c]);
            spare[c] = max(0, remW[c] - keep);
        }
        while (true) {
            int bu = -1, bb = -1; double bsc = -1; int besc = 0;
            for (int u = 0; u < (int)units.size(); u++) {
                if (done[u]) continue;
                int f = units[u];
                for (auto& b : blds) {
                    if (b.owner == 1 || claimed[b.cell]) continue;
                    int d = dist[f][b.cell];
                    if (d >= INF) continue;
                    if (turn + d + (b.owner == 2 ? 2 : 1) > 158) continue;
                    double sc = fval(b) / (d + 2.0);
                    int esc = 0;
                    if (!soloOk(b, f)) {
                        int need = garrisonNeed(b.cell) + (b.owner == 2 ? 2 : 0);
                        if (d > (int)prm(39, 12) || spare[f] < need || prm(40, 1.0) < 0.5) continue;
                        esc = need; sc *= prm(41, 1.0);
                    }
                    if (sc > bsc) { bsc = sc; bu = u; bb = b.cell; besc = esc; }
                }
            }
            if (bu < 0) break;
            done[bu] = 1; claimed[bb] = 1;
            if (besc > 0) spare[units[bu]] -= besc;
            FUnit fu{units[bu], bb, false}; fu.esc = besc;
            fUnits.push_back(fu);
        }
        for (int u = 0; u < (int)units.size(); u++) if (!done[u]) fUnits.push_back({units[u], -1, false});
    }

    int flagStep(int f, int goal) {
        int pick = f; bool have = false; int bd = INF, be = -1;
        for (int c : n1[f]) {
            if (!fSafe(c)) continue;
            int d = dist[c][goal];
            if (!have || d < bd || (d == bd && endW[c] > be)) { have = true; bd = d; be = endW[c]; pick = c; }
        }
        if (!have) {
            int bm = -INF;
            for (int c : n1[f]) {
                int m = endW[c] - thr[c];
                if (m > bm || (m == bm && dist[c][meBase] < dist[pick][meBase])) { bm = m; pick = c; }
            }
        }
        return pick;
    }

    void moveFlags() {
        for (auto& u : fUnits) {
            int f = u.cell;
            if (u.done) continue;
            if (u.stay && fSafe(f)) { assign(f, 0, 1, f); continue; }
            int goal = u.goal;
            if (u.stay || goal < 0) {
                goal = meBase; int bdd = INF;
                for (auto& g : groups) { int d = dist[f][g.cell]; if (d < bdd) { bdd = d; goal = g.next; } }
            }
            int dest = flagStep(f, goal);
            assign(f, 0, 1, dest);
            if (u.goal >= 0 && !u.stay) fMem.push_back({dest, u.goal});
        }
    }

    // ------------------------------------------------------------------ scouts
    void planScouts() {
        vector<int> unk;
        for (auto& b : blds) if (knownScore[b.id] < 0) unk.push_back(b.cell);
        for (int s = 0; s < N; s++) {
            while (remS[s] > 0) {
                int goalCell = -1, bestd = INF;
                for (int b : unk) {
                    // nearest reveal cell (chebyshev <= 2) that is not threatened
                    for (int r = 0; r < N; r++) {
                        if (dist[s][r] >= INF) continue;
                        if (max(abs(cx(r) - cx(b)), abs(cy(r) - cy(b))) > 2) continue;
                        if (thr[r] > 0) continue;
                        int d = dist[s][r];
                        if (d < bestd) { bestd = d; goalCell = r; }
                    }
                }
                int pick = s;
                if (goalCell >= 0 && bestd > 0) {
                    // up to two steps along a shortest path
                    int cur = s; int steps = min(2, bestd);
                    for (int i = 0; i < steps; i++) {
                        int nx = -1;
                        for (int c : nb[cur]) if (dist[c][goalCell] == dist[cur][goalCell] - 1 && thr[c] == 0) { nx = c; break; }
                        if (nx < 0) break;
                        cur = nx;
                    }
                    pick = cur;
                } else if (thr[s] > 0) {
                    int bt = thr[s];
                    for (int c : nb[s]) if (thr[c] < bt) { bt = thr[c]; pick = c; }
                }
                assign(s, 2, 1, pick);
                if (pick != s) {
                    // if two steps: fix the move record into a MOVE2 in emitMoves
                }
            }
        }
    }

    // ------------------------------------------------------------------ output
    void emitMoves(vector<string>& out) {
        // PRIORITY
        {
            vector<pair<double, int>> pr;
            for (auto& b : blds) if (b.owner != 1 && endF[b.cell] > 0) pr.push_back({-bval(b), b.cell});
            sort(pr.begin(), pr.end());
            if (pr.size() >= 2) {
                vector<pair<int, int>> co;
                for (auto& x : pr) co.push_back({cx(x.second), cy(x.second)});
                out.push_back(p::priority(co));
            }
        }
        map<tuple<int, int, int>, int> agg;  // (cell, kind, dest)
        for (auto& m : moves) agg[{m.cell, m.kind, m.dest}] += m.cnt;
        const char* kn[3] = {"F", "W", "S"};
        for (auto& [k, cnt] : agg) {
            auto [cell, kind, dest] = k;
            if (kind == 2 && dist[cell][dest] == 2) {
                // two-step scout move: find the middle cell
                int mid = -1;
                for (int c : nb[cell]) if (dist[c][dest] == 1) { mid = c; break; }
                if (mid >= 0) {
                    out.push_back(p::move2(cx(cell), cy(cell), cnt, dirTo(cell, mid), dirTo(mid, dest)));
                    continue;
                }
            }
            if (dist[cell][dest] != 1) continue;
            out.push_back(p::move(cx(cell), cy(cell), kn[kind], cnt, dirTo(cell, dest)));
        }
    }
};
}  // namespace

int main() {
    Hunter h;
    return p::run([&](const p::View& v, const p::Init& i) { return h.decide(v, i); });
}

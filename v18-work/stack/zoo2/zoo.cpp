// diverse simple opponents for robustness tests. build: g++ -std=c++20 -O2 -DMODE=1 zoo.cpp -o rush
#include "protocol.hpp"
#include <queue>
#include <set>
#include <map>
#include <cstdint>
using namespace std;
#ifndef MODE
#define MODE 1
#endif
// 1 rush, 2 turtle, 3 chaser, 4 random, 5 centercamp, 6 fhoard(F heavy), 7 splitter
static uint64_t rng = 88172645463325252ULL;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 11); }
static const char* DS[4] = {"U", "D", "L", "R"};
static int DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};
struct Ctx {
    const p::View& v; const p::Init& in;
    vector<vector<int>> dist; // dist from each cell
    Ctx(const p::View& v_, const p::Init& in_) : v(v_), in(in_) {}
    int W() const { return in.width; } int H() const { return in.height; }
};
static vector<int> bfsAll(const p::Init& in, int sx, int sy) {
    int W = in.width, H = in.height; vector<int> d(W * H, 1e9); queue<int> q; d[sy * W + sx] = 0; q.push(sy * W + sx);
    while (!q.empty()) { int c = q.front(); q.pop(); for (int k = 0; k < 4; k++) { int x = c % W + DX[k], y = c / W + DY[k]; if (!in.passable(x, y)) continue; if (d[y * W + x] > d[c] + 1) { d[y * W + x] = d[c] + 1; q.push(y * W + x); } } }
    return d;
}
// step from (x,y) toward nearest of targets; returns dir or -1
static int stepToward(const p::Init& in, int x, int y, const vector<pair<int,int>>& targets) {
    if (targets.empty()) return -1;
    int W = in.width; int best = 1e9, bk = -1;
    vector<vector<int>> ds; for (auto& t : targets) ds.push_back(bfsAll(in, t.first, t.second));
    int cur = 1e9; for (auto& d : ds) cur = min(cur, d[y * W + x]);
    if (cur == 0) return -1;
    for (int k = 0; k < 4; k++) { int nx = x + DX[k], ny = y + DY[k]; if (!in.passable(nx, ny)) continue; int m = 1e9; for (auto& d : ds) m = min(m, d[ny * W + nx]); if (m < best) { best = m; bk = k; } }
    return bk;
}
vector<string> decide(const p::View& v, const p::Init& in) {
    vector<string> out;
    vector<pair<int,int>> enemyBase = {in.bases[in.team == "Y" ? 1 : 0]};
    auto myBase = in.base();
    int res = v.my_resource;
    int eng = 0, myB = 0; for (auto& b : v.buildings) { if (b.owner == in.team) { myB++; if (b.type == "ENG") eng++; } }
    int wc = max(2, 3 - eng);
    int nF = 0, nW = 0; for (auto& u : v.units) if (u.team == in.team) { if (u.kind == "F") nF += u.count; if (u.kind == "W") nW += u.count; }
    // capture reserve
    int reserve = 0; for (auto& u : v.my_units(in, "F")) { auto b = v.building_at(u.x, u.y); if (b && b->owner != in.team) reserve += (b->type == "PLAZA" ? 4 : 2); }
    int budget = max(0, res - min(reserve, 6));
    int mkF = 0, mkW = 0;
#if MODE == 6
    int wantF = 8;
#else
    int wantF = (MODE == 2) ? 3 : 4;
#endif
    if (v.turn == 1) { mkF = min(2, budget / 5); budget -= 5 * mkF; }
    else if (nF < wantF && budget >= 5) { mkF = 1; budget -= 5; }
    mkW = budget / wc;
#if MODE == 6
    mkW = 0; { int extraF = budget / 5; mkF += extraF; }
#endif
    if (mkF) out.push_back(p::spawn("F", mkF));
    if (mkW) out.push_back(p::spawn("W", mkW));
    // targets for F
    vector<pair<int,int>> ft; for (auto& b : v.buildings) if (b.owner != in.team) {
#if MODE == 2
        int d0 = abs(b.x - myBase.first) + abs(b.y - myBase.second); if (d0 > 7) continue;
#endif
        ft.push_back({b.x, b.y}); }
    if (ft.empty()) for (auto& b : v.buildings) ft.push_back({b.x, b.y});
    // W moves
    vector<pair<int,int>> enF, enW, ownB, neutralB;
    for (auto& u : v.units) if (u.team == in.opp) { if (u.kind == "F") enF.push_back({u.x, u.y}); if (u.kind == "W") enW.push_back({u.x, u.y}); }
    for (auto& b : v.buildings) { if (b.owner == in.team) ownB.push_back({b.x, b.y}); }
    vector<pair<int,int>> plaza; for (auto& b : v.buildings) if (b.type == "PLAZA") plaza.push_back({b.x, b.y});
    int wi = 0;
    for (auto& u : v.units) if (u.team == in.team && u.kind == "W") {
        int d = -1; int cnt = u.count;
#if MODE == 1
        d = stepToward(in, u.x, u.y, enemyBase);
#elif MODE == 2
        // garrison: 1 W stays on each own building, rest wander near base
        auto b = v.building_at(u.x, u.y);
        if (b && b->owner == in.team) { d = -1; }
        else { vector<pair<int,int>> tg = ownB; if (tg.empty()) tg.push_back(myBase); d = stepToward(in, u.x, u.y, tg); }
#elif MODE == 3
        vector<pair<int,int>> tg = !enF.empty() ? enF : (!enW.empty() ? enW : enemyBase); d = stepToward(in, u.x, u.y, tg);
#elif MODE == 4
        if (rnd() % 10 < 7) d = rnd() % 4;
#elif MODE == 5
        d = stepToward(in, u.x, u.y, plaza);
#elif MODE == 7
        // split: half stays (defense), half hunts nearest non-owned building
        if ((wi++ % 2) == 0) { d = -1; } else { vector<pair<int,int>> tg = ft; d = stepToward(in, u.x, u.y, tg); }
#else
        d = -1;
#endif
        if (d >= 0) out.push_back(p::move(u.x, u.y, "W", cnt, DS[d]));
    }
    for (auto& u : v.units) if (u.team == in.team && u.kind == "F") {
        int d;
#if MODE == 4
        d = (rnd() % 10 < 6) ? (int)(rnd() % 4) : stepToward(in, u.x, u.y, ft);
#else
        d = stepToward(in, u.x, u.y, ft);
#endif
        if (d >= 0) out.push_back(p::move(u.x, u.y, "F", u.count, DS[d]));
    }
    return out;
}
int main() { return p::run(decide); }

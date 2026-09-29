// C++ port of yk-development-tools/engine (pipeline.py etc.) for fast in-process self-play.
#pragma once
#include <array>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <sstream>
#include <cstdio>
#include <cstring>
#include "../protocol.hpp"

namespace eng {
using namespace std;
constexpr int W = 15, H = 15, NC = 225;
enum { F = 0, WK = 1, S = 2 };
enum BT { PLAZA, HALL, STATION, LIBRARY, ENG, HOSPITAL, WATCH, DEPOT };
inline BT btype_of(const string& s) {
    if (s == "PLAZA") return PLAZA; if (s == "HALL") return HALL; if (s == "STATION") return STATION;
    if (s == "LIBRARY") return LIBRARY; if (s == "ENG") return ENG; if (s == "HOSPITAL") return HOSPITAL;
    if (s == "WATCH") return WATCH; return DEPOT;
}
struct Bld { int id, x, y; BT type; string tname; int score; int owner = 0; int stage = 0; };
struct GameMap {
    int seed = 0;
    array<string, H> terrain;
    int bx[2], by[2];
    vector<Bld> blds;
};

inline vector<GameMap> load_maps(const char* path) {
    vector<GameMap> out;
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open maps\n"); return out; }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        int seed;
        if (sscanf(line, "SEED %d", &seed) != 1) continue;
        GameMap m; m.seed = seed;
        for (int y = 0; y < H; y++) { fgets(line, sizeof line, f); m.terrain[y] = string(line, W); }
        fgets(line, sizeof line, f);
        sscanf(line, "BASE %d %d %d %d", &m.bx[0], &m.by[0], &m.bx[1], &m.by[1]);
        fgets(line, sizeof line, f);
        int nb; sscanf(line, "NB %d", &nb);
        for (int i = 0; i < nb; i++) {
            fgets(line, sizeof line, f);
            Bld b; char t[32];
            sscanf(line, "%d %d %d %31s %d", &b.id, &b.x, &b.y, t, &b.score);
            b.tname = t; b.type = btype_of(t);
            m.blds.push_back(b);
        }
        out.push_back(m);
    }
    fclose(f);
    return out;
}

struct Cmd {
    enum K { SPAWN, MOVE, MOVE2, TELE, PRIORITY } k;
    int kind = 0, count = 0, x = -1, y = -1, tx = -1, ty = -1, d1 = -1, d2 = -1;
    vector<pair<int,int>> coords;
};
inline int kind_of(const string& s) { if (s == "F") return F; if (s == "W") return WK; if (s == "S") return S; return -1; }
inline int dir_of(const string& s) { if (s == "U") return 0; if (s == "D") return 1; if (s == "L") return 2; if (s == "R") return 3; return -1; }
static const int DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};
inline bool to_int(const string& s, int& v) {
    if (s.empty()) return false; size_t i = 0; if (s[0] == '-' || s[0] == '+') i = 1; if (i >= s.size()) return false;
    for (size_t j = i; j < s.size(); j++) if (!isdigit((unsigned char)s[j])) return false;
    v = atoi(s.c_str()); return true;
}
inline bool parse_cmd(const string& line, Cmd& c) {
    istringstream is(line); vector<string> t; string w; while (is >> w) t.push_back(w);
    if (t.empty()) return false;
    int n = t.size(); int v;
    if (t[0] == "SPAWN") {
        if (n != 3 && n != 5) return false;
        c.k = Cmd::SPAWN; c.kind = kind_of(t[1]); if (c.kind < 0) return false;
        if (!to_int(t[2], c.count) || c.count <= 0) return false;
        if (n == 5) { if (!to_int(t[3], c.x) || !to_int(t[4], c.y)) return false; }
        return true;
    }
    if (t[0] == "MOVE") {
        if (n != 6) return false; c.k = Cmd::MOVE;
        if (!to_int(t[1], c.x) || !to_int(t[2], c.y)) return false;
        c.kind = kind_of(t[3]); if (c.kind < 0) return false;
        if (!to_int(t[4], c.count) || c.count <= 0) return false;
        c.d1 = dir_of(t[5]); if (c.d1 < 0) return false; return true;
    }
    if (t[0] == "MOVE2") {
        if (n != 7 || t[3] != "S") return false; c.k = Cmd::MOVE2; c.kind = S;
        if (!to_int(t[1], c.x) || !to_int(t[2], c.y)) return false;
        if (!to_int(t[4], c.count) || c.count <= 0) return false;
        c.d1 = dir_of(t[5]); c.d2 = dir_of(t[6]); if (c.d1 < 0 || c.d2 < 0) return false; return true;
    }
    if (t[0] == "TELE") {
        if (n != 7) return false; c.k = Cmd::TELE;
        if (!to_int(t[1], c.x) || !to_int(t[2], c.y)) return false;
        c.kind = kind_of(t[3]); if (c.kind < 0) return false;
        if (!to_int(t[4], c.count) || c.count <= 0) return false;
        if (!to_int(t[5], c.tx) || !to_int(t[6], c.ty)) return false; return true;
    }
    if (t[0] == "PRIORITY") {
        if ((n - 1) % 2 != 0) return false; c.k = Cmd::PRIORITY;
        for (int i = 1; i + 1 < n; i += 2) { int a, b; if (!to_int(t[i], a) || !to_int(t[i + 1], b)) return false; c.coords.push_back({a, b}); }
        return true;
    }
    (void)v; return false;
}

struct State {
    array<string, H> terrain;
    int base[2][2];
    vector<Bld> b;             // by id order (ids are 0..n-1)
    int res[2] = {10, 10};
    int u[2][3][NC];
    int turn = 0;
    bool depotClaimed[2][32];
    vector<char> revealed[2];
    long long occ[2] = {0, 0};
    State() { memset(u, 0, sizeof u); memset(depotClaimed, 0, sizeof depotClaimed); }
    bool passable(int x, int y) const { return x >= 0 && y >= 0 && x < W && y < H && terrain[y][x] != '#'; }
    int bldAt(int x, int y) const { for (auto& q : b) if (q.x == x && q.y == y) return q.id; return -1; }
    bool hasUnits(int x, int y, int t) const { int z = y * W + x; return u[t][0][z] > 0 || u[t][1][z] > 0 || u[t][2][z] > 0; }
};

inline State make_state(const GameMap& m) {
    State s; s.terrain = m.terrain; s.b = m.blds;
    s.base[0][0] = m.bx[0]; s.base[0][1] = m.by[0]; s.base[1][0] = m.bx[1]; s.base[1][1] = m.by[1];
    s.revealed[0].assign(m.blds.size(), 0); s.revealed[1].assign(m.blds.size(), 0);
    return s;
}

inline int eng_count(const State& s, int t) { int c = 0; for (auto& q : s.b) if (q.type == ENG && q.owner == t + 1) c++; return c; }
inline bool owns_lib(const State& s, int t) { for (auto& q : s.b) if (q.type == LIBRARY && q.owner == t + 1) return true; return false; }
inline int unit_cost(const State& s, int t, int kind) {
    static const int base[3] = {5, 3, 2};
    if (kind == WK) return max(base[WK] - eng_count(s, t), 2);
    return base[kind];
}
inline void add_res(State& s, int t, int a) { s.res[t] = min(s.res[t] + a, 40); }

struct Result { int winner = -2; const char* reason = ""; int score[2] = {0, 0}; };  // winner: 0 Y, 1 K, -1 draw, -2 none

inline int team_score(const State& s, int t) { int r = 0; for (auto& q : s.b) if (q.owner == t + 1) r += q.score; return r; }

inline int homekey(const State& s, int team, const Bld& b) {
    if (s.base[team][0] * 2 > W - 1) return (H - 1 - b.y) * 100 + (W - 1 - b.x);
    return b.y * 100 + b.x;
}

// One turn. cmds[t] = list of parsed commands in output order.
inline Result run_turn(State& s, const vector<Cmd> cmds[2]) {
    s.turn++;
    for (auto& q : s.b) if (q.owner == 0 && q.stage == 1) q.stage = 0;
    // 2. spawn
    for (int t = 0; t < 2; t++) for (auto& c : cmds[t]) if (c.k == Cmd::SPAWN) {
        int bx, by;
        if (c.x < 0 && c.y < 0 && false) {}
        bool hasxy = (c.x != -1 || c.y != -1);
        // note: python treats x/y None as missing; explicit coordinates may be any int
        // (our parse leaves -1,-1 only when absent; coordinates -1 cannot be valid anyway)
        if (!hasxy) { bx = s.base[t][0]; by = s.base[t][1]; }
        else {
            bx = c.x; by = c.y;
            int id = (bx >= 0 && by >= 0 && bx < W && by < H) ? s.bldAt(bx, by) : -1;
            if (id < 0 || s.b[id].type != HOSPITAL || s.b[id].owner != t + 1) continue;
        }
        int cost = unit_cost(s, t, c.kind);
        int n = min(c.count, s.res[t] / cost);
        if (n <= 0) continue;
        s.res[t] -= n * cost;
        s.u[t][c.kind][by * W + bx] += n;
    }
    // 3. move
    static int movable[2][3][NC], arr[2][3][NC];
    memcpy(movable, s.u, sizeof movable); memset(arr, 0, sizeof arr);
    auto take = [&](int x, int y, int t, int k, int cnt) -> int {
        if (x < 0 || y < 0 || x >= W || y >= H) return 0;
        int z = y * W + x; int n = min(max(0, cnt), movable[t][k][z]); movable[t][k][z] -= n; return n;
    };
    for (int t = 0; t < 2; t++) {
        int tele_used = 0;
        for (auto& c : cmds[t]) {
            if (c.k == Cmd::MOVE) {
                int nx = c.x + DX[c.d1], ny = c.y + DY[c.d1];
                if (!s.passable(nx, ny)) continue;
                int n = take(c.x, c.y, t, c.kind, c.count);
                if (n > 0) arr[t][c.kind][ny * W + nx] += n;
            } else if (c.k == Cmd::MOVE2) {
                int mx = c.x + DX[c.d1], my = c.y + DY[c.d1], nx = mx + DX[c.d2], ny = my + DY[c.d2];
                if (!s.passable(mx, my) || !s.passable(nx, ny)) continue;
                int n = take(c.x, c.y, t, S, c.count);
                if (n > 0) arr[t][S][ny * W + nx] += n;
            } else if (c.k == Cmd::TELE) {
                if (tele_used >= 1) continue;
                if (c.x == c.tx && c.y == c.ty) continue;
                int a = (c.x >= 0 && c.y >= 0 && c.x < W && c.y < H) ? s.bldAt(c.x, c.y) : -1;
                int d = (c.tx >= 0 && c.ty >= 0 && c.tx < W && c.ty < H) ? s.bldAt(c.tx, c.ty) : -1;
                if (a < 0 || d < 0) continue;
                if (s.b[a].type != STATION || s.b[d].type != STATION) continue;
                if (s.b[a].owner != t + 1 || s.b[d].owner != t + 1) continue;
                int st = 0; for (auto& q : s.b) if (q.type == STATION && q.owner == t + 1) st++;
                if (st < 2) continue;
                int want = min(c.count, 5);
                int n = take(c.x, c.y, t, c.kind, want);
                if (n <= 0) continue;
                arr[t][c.kind][c.ty * W + c.tx] += n;
                tele_used++;
            }
        }
    }
    for (int t = 0; t < 2; t++) for (int k = 0; k < 3; k++) for (int z = 0; z < NC; z++) s.u[t][k][z] = movable[t][k][z] + arr[t][k][z];
    // 4. combat
    for (int z = 0; z < NC; z++) {
        int x = z % W, y = z / W;
        bool yh = s.u[0][0][z] > 0 || s.u[0][1][z] > 0 || s.u[0][2][z] > 0;
        bool kh = s.u[1][0][z] > 0 || s.u[1][1][z] > 0 || s.u[1][2][z] > 0;
        if (!(yh && kh)) continue;
        int yW = s.u[0][WK][z], kW = s.u[1][WK][z];
        int m = min(yW, kW);
        if (m > 0) { s.u[0][WK][z] -= m; s.u[1][WK][z] -= m; yW -= m; kW -= m; }
        int id = s.bldAt(x, y);
        auto wipe = [&](int victim) {
            bool had = s.u[victim][F][z] > 0;
            s.u[victim][F][z] = 0; s.u[victim][S][z] = 0;
            if (id >= 0 && s.b[id].owner == victim + 1 && had) { s.b[id].owner = 0; s.b[id].stage = 1; }
        };
        if (yW > 0 && kW == 0) wipe(1);
        else if (kW > 0 && yW == 0) wipe(0);
    }
    // 6. income
    for (int t = 0; t < 2; t++) {
        int halls = 0; for (auto& q : s.b) if (q.type == HALL && q.owner == t + 1) halls++;
        add_res(s, t, 10 + halls * 2);
    }
    // 7. capture
    bool lib[2] = {owns_lib(s, 0), owns_lib(s, 1)};
    int depotPending[2] = {0, 0};
    for (int t = 0; t < 2; t++) {
        int opp = 1 - t;
        vector<int> cand;
        for (auto& q : s.b) {
            int z = q.y * W + q.x;
            if (s.u[t][F][z] <= 0) continue;
            if (s.u[opp][F][z] > 0) continue;
            if (q.owner == t + 1 && q.stage >= 2) continue;
            cand.push_back(q.id);
        }
        vector<int> ordered; vector<char> used(s.b.size(), 0);
        vector<pair<int,int>> pri;
        for (auto& c : cmds[t]) if (c.k == Cmd::PRIORITY) pri = c.coords;   // last wins
        for (auto& pc : pri) {
            for (int id : cand) if (s.b[id].x == pc.first && s.b[id].y == pc.second && !used[id]) { ordered.push_back(id); used[id] = 1; }
        }
        vector<int> rest;
        for (int id : cand) if (!used[id]) rest.push_back(id);
        stable_sort(rest.begin(), rest.end(), [&](int a, int b2) {
            int sa = s.revealed[t][a] ? s.b[a].score : 0, sb = s.revealed[t][b2] ? s.b[b2].score : 0;
            if (sa != sb) return sa > sb;
            return homekey(s, t, s.b[a]) < homekey(s, t, s.b[b2]);
        });
        for (int id : rest) ordered.push_back(id);
        for (int id : ordered) {
            Bld& q = s.b[id];
            int cost = 2; if (q.type == PLAZA) cost *= 2; if (lib[t]) cost -= 1; cost = max(cost, 1);
            if (s.res[t] < cost) continue;
            s.res[t] -= cost;
            if (q.owner == 0) {
                q.owner = t + 1; q.stage = 2; s.revealed[t][id] = 1;
                if (q.type == DEPOT) { if (!s.depotClaimed[t][id]) { s.depotClaimed[t][id] = 1; depotPending[t] += 15; } }
            } else { q.owner = 0; q.stage = 1; }
        }
    }
    for (int t = 0; t < 2; t++) if (depotPending[t]) add_res(s, t, depotPending[t]);
    for (int t = 0; t < 2; t++) for (auto& q : s.b) if (q.owner == t + 1) s.occ[t] += q.score;
    // 8. reveal
    for (int t = 0; t < 2; t++) {
        vector<int> scz; for (int z = 0; z < NC; z++) if (s.u[t][S][z] > 0) scz.push_back(z);
        for (auto& q : s.b) {
            if (s.revealed[t][q.id]) continue;
            int z = q.y * W + q.x;
            bool r = false;
            if (s.u[t][0][z] > 0 || s.u[t][1][z] > 0 || s.u[t][2][z] > 0) r = true;
            else if (q.owner == t + 1) r = true;
            else { for (int sz : scz) if (max(abs(sz % W - q.x), abs(sz / W - q.y)) <= 2) { r = true; break; } }
            if (!r) for (auto& w : s.b) if (w.type == WATCH && w.owner == t + 1 && max(abs(w.x - q.x), abs(w.y - q.y)) <= 3) { r = true; break; }
            if (r) s.revealed[t][q.id] = 1;
        }
    }
    // 9. victory
    Result r; int sc[2] = {team_score(s, 0), team_score(s, 1)}; r.score[0] = sc[0]; r.score[1] = sc[1];
    int total = 0; for (auto& q : s.b) total += q.score;
    for (int t = 0; t < 2; t++) if (sc[1 - t] == 0 && sc[t] * 2 > total) { r.winner = t; r.reason = "instant"; return r; }
    if (s.turn < 160) return r;
    if (sc[0] != sc[1]) { r.winner = sc[0] > sc[1] ? 0 : 1; r.reason = "score"; return r; }
    if (s.occ[0] != s.occ[1]) { r.winner = s.occ[0] > s.occ[1] ? 0 : 1; r.reason = "occupation_turns"; return r; }
    long long uv[2] = {0, 0}; static const int val[3] = {5, 3, 2};
    for (int t = 0; t < 2; t++) for (int k = 0; k < 3; k++) for (int z = 0; z < NC; z++) uv[t] += (long long)val[k] * s.u[t][k][z];
    if (uv[0] != uv[1]) { r.winner = uv[0] > uv[1] ? 0 : 1; r.reason = "units"; return r; }
    r.winner = -1; r.reason = "draw"; return r;
}

// Build bot inputs
inline p::Init make_init(const State& s, int team) {
    p::Init i; i.width = W; i.height = H; i.team = team == 0 ? "Y" : "K"; i.opp = team == 0 ? "K" : "Y";
    for (int y = 0; y < H; y++) i.terrain.push_back(s.terrain[y]);
    for (auto& q : s.b) { p::Building b; b.id = q.id; b.x = q.x; b.y = q.y; b.type = q.tname; i.buildings.push_back(b); }
    i.bases[0] = {s.base[0][0], s.base[0][1]}; i.bases[1] = {s.base[1][0], s.base[1][1]};
    return i;
}
inline p::View make_view(const State& s, int team, int turn) {
    p::View v; v.turn = turn; v.my_resource = s.res[team]; v.opp_resource = s.res[1 - team];
    static const char* KN[3] = {"F", "W", "S"};
    for (int t = 0; t < 2; t++) for (int k = 0; k < 3; k++)
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            int c = s.u[t][k][y * W + x]; if (c > 0) { p::Unit u; u.team = t == 0 ? "Y" : "K"; u.kind = KN[k]; u.x = x; u.y = y; u.count = c; v.units.push_back(u); }
        }
    for (auto& q : s.b) {
        p::Building b; b.id = q.id; b.x = q.x; b.y = q.y; b.type = q.tname;
        b.owner = q.owner == 0 ? "N" : (q.owner == 1 ? "Y" : "K"); b.stage = q.stage; b.score = s.revealed[team][q.id] ? q.score : -1;
        v.buildings.push_back(b);
    }
    return v;
}
inline string serialize_init(const State& s, int team) {
    ostringstream o; o << "INIT 15 15\nTEAM " << (team == 0 ? "Y" : "K") << "\n";
    for (int y = 0; y < H; y++) o << "MAP " << s.terrain[y] << "\n";
    o << "BUILDINGS " << s.b.size() << "\n";
    for (auto& q : s.b) o << q.id << " " << q.x << " " << q.y << " " << q.tname << "\n";
    o << "BASE Y " << s.base[0][0] << " " << s.base[0][1] << "\nBASE K " << s.base[1][0] << " " << s.base[1][1] << "\nEND\n";
    return o.str();
}
inline string serialize_turn(const State& s, int team, int turn) {
    p::View v = make_view(s, team, turn);
    ostringstream o; o << "TURN " << turn << "\nRESOURCE " << v.my_resource << " " << v.opp_resource << "\nUNITS " << v.units.size() << "\n";
    for (auto& u : v.units) o << u.team << " " << u.kind << " " << u.x << " " << u.y << " " << u.count << "\n";
    o << "BUILDINGS " << v.buildings.size() << "\n";
    for (auto& b : v.buildings) o << b.id << " " << b.x << " " << b.y << " " << b.type << " " << b.owner << " " << b.stage << " " << b.score << "\n";
    o << "END\n";
    return o.str();
}
}  // namespace eng

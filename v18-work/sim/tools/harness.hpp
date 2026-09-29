// harness.hpp - shared helpers for the in-process match runner / validator (NOT part of the submission)
#pragma once
#include "../protocol.hpp"
#include "../sim.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace hx {
using namespace std;

struct GameMap {
    int seed = 0;
    int by[2][2];  // base (x,y) of Y and K
    vector<string> terrain;
    struct B { int id, x, y; string type; int score; };
    vector<B> blds;
};

inline bool readMaps(const string& path, vector<GameMap>& out) {
    ifstream f(path);
    if (!f) return false;
    string tag;
    GameMap g;
    bool have = false;
    string line;
    while (getline(f, line)) {
        istringstream is(line);
        is >> tag;
        if (tag == "SEED") {
            if (have) out.push_back(g);
            g = GameMap(); have = true;
            is >> g.seed;
        } else if (tag == "BASES") {
            is >> g.by[0][0] >> g.by[0][1] >> g.by[1][0] >> g.by[1][1];
        } else if (tag == "T") {
            string row; is >> row; g.terrain.push_back(row);
        } else if (tag == "B") {
            int n; is >> n;
            for (int i = 0; i < n; i++) {
                getline(f, line);
                istringstream q(line);
                GameMap::B b; q >> b.id >> b.x >> b.y >> b.type >> b.score;
                g.blds.push_back(b);
            }
        }
    }
    if (have) out.push_back(g);
    return true;
}

// Build truth Map (score2 = 2*true score). Team 0 = Y, team 1 = K.
inline void buildMap(const GameMap& g, sim::Map& M) {
    for (int c = 0; c < sim::NC; c++) M.pass[c] = g.terrain[c / 15][c % 15] != '#';
    M.nb = (int)g.blds.size();
    for (auto& b : g.blds) {
        M.bcell[b.id] = b.y * 15 + b.x;
        M.btype[b.id] = sim::btypeFromStr(b.type);
        M.score2[b.id] = 2 * b.score;
    }
    M.base[0] = g.by[0][1] * 15 + g.by[0][0];
    M.base[1] = g.by[1][1] * 15 + g.by[1][0];
    M.flip[0] = false; M.flip[1] = true;
    M.finish();
}

inline p::Init buildInit(const GameMap& g, const string& team) {
    p::Init i;
    i.width = 15; i.height = 15; i.team = team; i.opp = team == "Y" ? "K" : "Y";
    i.terrain = g.terrain;
    for (auto& b : g.blds) { p::Building pb; pb.id = b.id; pb.x = b.x; pb.y = b.y; pb.type = b.type; i.buildings.push_back(pb); }
    i.bases[0] = {g.by[0][0], g.by[0][1]};
    i.bases[1] = {g.by[1][0], g.by[1][1]};
    return i;
}

inline p::View buildView(const sim::Map& M, const sim::State& s, const GameMap& g, int t, const bool* stage1) {
    p::View v;
    v.turn = s.turn + 1;
    v.my_resource = s.res[t];
    v.opp_resource = s.res[1 - t];
    const char* TN[2] = {"Y", "K"};
    for (int tm = 0; tm < 2; tm++) {
        for (int k = 0; k < 2; k++) {
            // ordering: (team Y->K, kind F->W, y, x)
            for (int c = 0; c < sim::NC; c++) {
                int n = k == 0 ? s.F[tm][c] : s.Wn[tm][c];
                if (n > 0) { p::Unit u; u.team = TN[tm]; u.kind = k == 0 ? "F" : "W"; u.x = c % 15; u.y = c / 15; u.count = n; v.units.push_back(u); }
            }
        }
    }
    // sort by (team, kind, y, x) -- already y-major by construction for each (team, kind)
    for (auto& b : g.blds) {
        p::Building pb; pb.id = b.id; pb.x = b.x; pb.y = b.y; pb.type = b.type;
        int o = s.owner[b.id];
        pb.owner = o < 0 ? "N" : TN[o];
        pb.stage = o >= 0 ? 2 : (stage1[b.id] ? 1 : 0);
        pb.score = ((s.rev[t] >> b.id) & 1) ? b.score : -1;
        v.buildings.push_back(pb);
    }
    return v;
}

inline bool parseInt(const string& s, int& out) {
    if (s.empty()) return false;
    char* e = nullptr;
    long v = strtol(s.c_str(), &e, 10);
    if (*e != 0) return false;
    out = (int)v;
    return true;
}

// Mirrors runner/protocol.py parse_commands + engine ordering (S units are not modelled: their commands are dropped).
inline void parseCmds(const sim::Map& M, const vector<string>& lines, sim::Orders& o, bool* spawnedS = nullptr) {
    o.clear();
    for (auto& raw : lines) {
        istringstream is(raw);
        vector<string> tok; string w;
        while (is >> w) tok.push_back(w);
        if (tok.empty()) continue;
        const string& op = tok[0];
        size_t n = tok.size();
        auto kindOf = [&](const string& s) -> int { return s == "F" ? 0 : s == "W" ? 1 : s == "S" ? 2 : -1; };
        auto dirOf = [&](const string& s) -> int { return s == "U" ? 0 : s == "D" ? 1 : s == "L" ? 2 : s == "R" ? 3 : -1; };
        int a, b, c, d, e;
        if (op == "SPAWN") {
            if (n == 3) {
                int k = kindOf(tok[1]); if (k < 0 || !parseInt(tok[2], a) || a <= 0) continue;
                if (k == 2) { if (spawnedS) *spawnedS = true; continue; }
                o.addSpawn(k, a, -1);
            } else if (n == 5) {
                int k = kindOf(tok[1]); if (k < 0 || !parseInt(tok[2], a) || a <= 0 || !parseInt(tok[3], b) || !parseInt(tok[4], c)) continue;
                if (k == 2) { if (spawnedS) *spawnedS = true; continue; }
                if (b < 0 || b >= 15 || c < 0 || c >= 15) continue;
                int cell = c * 15 + b;
                // base coordinates explicitly given are invalid (not a hospital) -> engine ignores; step() validates
                o.addSpawn(k, a, cell);
            }
        } else if (op == "MOVE") {
            if (n != 6) continue;
            int k = kindOf(tok[3]); int dr = dirOf(tok[5]);
            if (k < 0 || dr < 0 || !parseInt(tok[1], a) || !parseInt(tok[2], b) || !parseInt(tok[4], c) || c <= 0) continue;
            if (k == 2) { if (spawnedS) *spawnedS = true; continue; }
            if (a < 0 || a >= 15 || b < 0 || b >= 15) continue;
            o.addMove(b * 15 + a, k, dr, c);
        } else if (op == "TELE") {
            if (n != 7) continue;
            int k = kindOf(tok[3]);
            if (k < 0 || !parseInt(tok[1], a) || !parseInt(tok[2], b) || !parseInt(tok[4], c) || c <= 0 || !parseInt(tok[5], d) || !parseInt(tok[6], e)) continue;
            if (k == 2) continue;
            if (a < 0 || a >= 15 || b < 0 || b >= 15 || d < 0 || d >= 15 || e < 0 || e >= 15) continue;
            o.addTele(b * 15 + a, k, c, e * 15 + d);
        } else if (op == "PRIORITY") {
            if ((n - 1) % 2 != 0) continue;
            bool ok = true; vector<int> v;
            for (size_t i = 1; i < n; i++) { int x; if (!parseInt(tok[i], x)) { ok = false; break; } v.push_back(x); }
            if (!ok) continue;
            o.npr = 0;  // last valid PRIORITY wins
            for (size_t i = 0; i + 1 < v.size(); i += 2) {
                int x = v[i], y = v[i + 1];
                if (x < 0 || x >= 15 || y < 0 || y >= 15) continue;
                int bb = M.bat[y * 15 + x];
                if (bb >= 0) o.addPrio(bb);
            }
        }
    }
}

// Orders -> command strings in the same order the original bots emit them (spawns, TELE, PRIORITY, moves)
inline vector<string> ordersToStrings(const sim::Map& M, const sim::Orders& o) {
    vector<string> out;
    static const char* KN[2] = {"F", "W"};
    static const char* DN[4] = {"U", "D", "L", "R"};
    for (int i = 0; i < o.nsp; i++) {
        string s = string("SPAWN ") + KN[o.sp[i].kind] + " " + to_string(o.sp[i].n);
        if (o.sp[i].cell >= 0) s += " " + to_string(o.sp[i].cell % 15) + " " + to_string(o.sp[i].cell / 15);
        out.push_back(s);
    }
    for (int i = 0; i < o.nmv; i++) if (o.mv[i].dir == sim::D_TELE) {
        auto& m = o.mv[i];
        out.push_back("TELE " + to_string(m.cell % 15) + " " + to_string(m.cell / 15) + " " + KN[m.kind] + " " + to_string(m.n) + " " + to_string(m.cell2 % 15) + " " + to_string(m.cell2 / 15));
    }
    if (o.npr > 0) {
        string s = "PRIORITY";
        for (int i = 0; i < o.npr; i++) s += " " + to_string(M.bcell[o.prio[i]] % 15) + " " + to_string(M.bcell[o.prio[i]] / 15);
        out.push_back(s);
    }
    for (int i = 0; i < o.nmv; i++) if (o.mv[i].dir != sim::D_TELE) {
        auto& m = o.mv[i];
        out.push_back("MOVE " + to_string(m.cell % 15) + " " + to_string(m.cell / 15) + " " + KN[m.kind] + " " + to_string(m.n) + " " + DN[m.dir]);
    }
    return out;
}

}  // namespace hx

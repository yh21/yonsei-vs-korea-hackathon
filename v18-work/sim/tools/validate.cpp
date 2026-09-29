// validate.cpp - replays commands from a python replay (converted to text) through sim::step and compares states.
#include "harness.hpp"
using namespace std;

int main(int argc, char** argv) {
    ifstream f(argv[1]);
    string line, tag;
    hx::GameMap g;
    // header
    while (getline(f, line)) {
        istringstream is(line); is >> tag;
        if (tag == "SEED") is >> g.seed;
        else if (tag == "BASES") is >> g.by[0][0] >> g.by[0][1] >> g.by[1][0] >> g.by[1][1];
        else if (tag == "T") { string r; is >> r; g.terrain.push_back(r); }
        else if (tag == "B") {
            int n; is >> n;
            for (int i = 0; i < n; i++) { getline(f, line); istringstream q(line); hx::GameMap::B b; q >> b.id >> b.x >> b.y >> b.type >> b.score; g.blds.push_back(b); }
            break;
        }
    }
    sim::Map M; hx::buildMap(g, M);
    sim::State s; s.init(M);
    int bad = 0, turns = 0;
    while (getline(f, line)) {
        istringstream is(line); is >> tag;
        if (tag == "EOF") break;
        if (tag != "TURN") continue;
        int tn; is >> tn;
        vector<string> cmds[2];
        for (int t = 0; t < 2; t++) {
            getline(f, line); istringstream q(line); string ct; int n; q >> ct >> n;
            for (int i = 0; i < n; i++) { getline(f, line); cmds[t].push_back(line); }
        }
        sim::Orders o[2];
        for (int t = 0; t < 2; t++) hx::parseCmds(M, cmds[t], o[t]);
        sim::step(M, s, o[0], o[1], true);
        turns++;
        // expected
        int ry, rk, oy, ok_;
        getline(f, line); { istringstream q(line); q >> tag >> ry >> rk; }
        getline(f, line); { istringstream q(line); q >> tag >> oy >> ok_; }
        getline(f, line);
        vector<string> own; { istringstream q(line); q >> tag; string w; while (q >> w) own.push_back(w); }
        getline(f, line);
        uint32_t rev[2] = {0, 0};
        { istringstream q(line); q >> tag; string w; int side = 0; while (q >> w) { if (w == "|") side = 1; else rev[side] |= 1u << atoi(w.c_str()); } }
        getline(f, line); int nu; { istringstream q(line); q >> tag >> nu; }
        int16_t F[2][sim::NC] = {}, Wn[2][sim::NC] = {};
        for (int i = 0; i < nu; i++) {
            getline(f, line); istringstream q(line); string tm, k; int x, y, c; q >> tm >> k >> x >> y >> c;
            (k == "F" ? F : Wn)[tm == "Y" ? 0 : 1][y * 15 + x] = c;
        }
        bool ok = true;
        string why;
        if (s.res[0] != ry || s.res[1] != rk) { ok = false; why += " res(" + to_string(s.res[0]) + "," + to_string(s.res[1]) + " vs " + to_string(ry) + "," + to_string(rk) + ")"; }
        if (s.occ[0] != 2 * oy || s.occ[1] != 2 * ok_) { ok = false; why += " occ"; }
        for (int b = 0; b < M.nb; b++) {
            int eo = own[b][0] == 'N' ? -1 : (own[b][0] == 'Y' ? 0 : 1);
            if (s.owner[b] != eo) { ok = false; why += " owner" + to_string(b); }
        }
        if (s.rev[0] != rev[0] || s.rev[1] != rev[1]) { ok = false; why += " rev"; }
        for (int t = 0; t < 2; t++)
            for (int c = 0; c < sim::NC; c++)
                if (s.F[t][c] != F[t][c] || s.Wn[t][c] != Wn[t][c]) { ok = false; why += " unit@" + to_string(c) + "t" + to_string(t); break; }
        if (!ok) { bad++; if (bad <= 5) printf("seed %d turn %d MISMATCH:%s\n", g.seed, tn, why.c_str()); }
    }
    printf("seed %d: %d turns, %d mismatched\n", g.seed, turns, bad);
    return bad != 0;
}

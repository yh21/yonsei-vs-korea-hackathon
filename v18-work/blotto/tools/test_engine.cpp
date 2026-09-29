#include "../engine.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
using namespace std;
using namespace eng;
int main(int argc, char** argv) {
    ifstream in(argv[1]); string line;
    Map M; memset(&M, 0, sizeof(M));
    vector<string> rows; getline(in, line); // MAP
    for (int y = 0; y < 15; y++) { getline(in, line); rows.push_back(line); }
    for (int y = 0; y < 15; y++) for (int x = 0; x < 15; x++) M.pass[y * 15 + x] = rows[y][x] != '#';
    M.nb = 0; map<int,int> idToB;
    State s; s.clear(); s.res[0] = s.res[1] = 10;
    StepCfg cfg;
    Orders O[2];
    int turn = 0, fails = 0, checked = 0;
    auto teamIdx = [](const string& t) { return t == "Y" ? 0 : 1; };
    bool started = false;
    auto finishInit = [&]() { M.finish(); cfg.homeFlip[0] = (M.base[0] % 15) * 2 > 14; cfg.homeFlip[1] = (M.base[1] % 15) * 2 > 14; };
    auto dirIdx = [](const string& d) { return d == "U" ? 0 : d == "D" ? 1 : d == "L" ? 2 : 3; };
    State expect; bool haveExpect = false;
    auto compare = [&]() {
        // s (after step) vs expect
        bool bad = false;
        for (int t = 0; t < 2 && !bad; t++) for (int c = 0; c < NC; c++) {
            if (s.F[t][c] != expect.F[t][c] || s.W[t][c] != expect.W[t][c]) { bad = true; cerr << "unit mismatch turn " << turn << " team " << t << " cell " << c % 15 << "," << c / 15 << " F " << s.F[t][c] << "/" << expect.F[t][c] << " W " << s.W[t][c] << "/" << expect.W[t][c] << "\n"; break; }
        }
        for (int b = 0; b < M.nb; b++) if (s.owner[b] != expect.owner[b]) { bad = true; cerr << "owner mismatch turn " << turn << " b " << b << " " << (int)s.owner[b] << "/" << (int)expect.owner[b] << "\n"; }
        if (s.res[0] != expect.res[0] || s.res[1] != expect.res[1]) { bad = true; cerr << "res mismatch turn " << turn << " " << s.res[0] << "," << s.res[1] << " / " << expect.res[0] << "," << expect.res[1] << "\n"; }
        checked++; if (bad) { fails++; if (fails > 5) exit(1); }
    };
    while (getline(in, line)) {
        istringstream ss(line); string tag; ss >> tag;
        if (tag == "B") { int id, x, y, sc; string ty; ss >> id >> x >> y >> ty >> sc; int b = M.nb++; M.bcell[b] = y * 15 + x; M.btype[b] = btypeFromStr(ty); M.bid[b] = id; M.score2[b] = sc * 2; idToB[id] = b; }
        else if (tag == "BASE") { string t; int x, y; ss >> t >> x >> y; M.base[teamIdx(t)] = y * 15 + x; }
        else if (tag == "TURN" || tag == "E") {
            if (!started) { started = true; finishInit(); }
            if (tag == "TURN") { if (haveExpect) {} ss >> turn; O[0].clear(); O[1].clear(); expect.clear(); haveExpect = true; }
            else { int a, b; ss >> a >> b; expect.res[0] = a; expect.res[1] = b; }
        }
        else if (tag == "C") {
            string t, k; ss >> t >> k; int ti = teamIdx(t);
            if (k == "S") { string kind; int n, x, y; ss >> kind >> n >> x >> y; O[ti].addSpawn(kind == "F" ? 0 : 1, n, x < 0 ? -1 : y * 15 + x); }
            else if (k == "M") { int x, y, n; string kind, d; ss >> x >> y >> kind >> n >> d; O[ti].addMove(y * 15 + x, kind == "F" ? 0 : 1, dirIdx(d), n); }
            else if (k == "T") { int x, y, n, tx, ty; string kind; ss >> x >> y >> kind >> n >> tx >> ty; O[ti].addTele(y * 15 + x, kind == "F" ? 0 : 1, n, ty * 15 + tx); }
            else if (k == "P") { int id; while (ss >> id) O[ti].addPrio(idToB[id]); }
        }
        else if (tag == "U") { string t, k; int x, y, c; ss >> t >> k >> x >> y >> c; (k == "F" ? expect.F : expect.W)[teamIdx(t)][y * 15 + x] = c; }
        else if (tag == "O") {
            int id; string o; ss >> id >> o; expect.owner[idToB[id]] = o == "N" ? -1 : teamIdx(o);
            if (id == M.bid[M.nb - 1]) { // last building line => run compare (step the sim first)
                step(M, s, O[0], O[1], cfg);
                compare();
            }
        }
    }
    cout << "checked " << checked << " turns, failures " << fails << "\n";
    return fails != 0;
}

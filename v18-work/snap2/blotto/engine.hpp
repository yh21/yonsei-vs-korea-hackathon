// engine.hpp - compact exact re-implementation of the game turn pipeline (for in-bot look-ahead).
// Teams are indexed 0/1 (0 = "me" inside the bot). Scouts are not modelled.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cstdlib>

namespace eng {

constexpr int GW = 15, GH = 15, NC = GW * GH, MAXB = 17;
enum BType : int { T_PLAZA = 0, T_HALL, T_STATION, T_LIBRARY, T_ENG, T_HOSPITAL, T_WATCH, T_DEPOT };
constexpr int RES_CAP = 40;

inline int btypeFromStr(const std::string& s) {
    if (s == "PLAZA") return T_PLAZA;
    if (s == "HALL") return T_HALL;
    if (s == "STATION") return T_STATION;
    if (s == "LIBRARY") return T_LIBRARY;
    if (s == "ENG") return T_ENG;
    if (s == "HOSPITAL") return T_HOSPITAL;
    if (s == "WATCH") return T_WATCH;
    return T_DEPOT;
}

struct Map {
    bool pass[NC];
    int nb = 0;
    int bcell[MAXB], btype[MAXB], sym[MAXB], bid[MAXB];  // bid = protocol id
    int score2[MAXB];                                      // 2 * believed score
    int bat[NC];
    int nbr[NC][4];  // U D L R
    uint8_t dist[NC][NC];
    int base[2] = {0, 0};
    int idToB[64];   // protocol id -> local index
    void finish() {
        for (int c = 0; c < NC; c++) bat[c] = -1;
        for (int b = 0; b < nb; b++) bat[bcell[b]] = b;
        static const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
        for (int c = 0; c < NC; c++) {
            int x = c % GW, y = c / GW;
            for (int k = 0; k < 4; k++) {
                int nx = x + dx[k], ny = y + dy[k];
                nbr[c][k] = (pass[c] && nx >= 0 && ny >= 0 && nx < GW && ny < GH && pass[ny * GW + nx]) ? ny * GW + nx : -1;
            }
        }
        for (int s = 0; s < NC; s++) {
            for (int c = 0; c < NC; c++) dist[s][c] = 255;
            if (!pass[s]) continue;
            int q[NC], qh = 0, qt = 0;
            dist[s][s] = 0; q[qt++] = s;
            while (qh < qt) {
                int v = q[qh++];
                for (int k = 0; k < 4; k++) { int u = nbr[v][k]; if (u >= 0 && dist[s][u] == 255) { dist[s][u] = dist[s][v] + 1; q[qt++] = u; } }
            }
        }
        for (int b = 0; b < nb; b++) {
            int x = bcell[b] % GW, y = bcell[b] / GW;
            int sc = (GH - 1 - y) * GW + (GW - 1 - x);
            sym[b] = bat[sc];
        }
    }
    int d(int a, int b) const { return dist[a][b]; }
};

struct State {
    int16_t F[2][NC];
    int16_t W[2][NC];
    int8_t owner[MAXB];  // -1 neutral, 0/1 team
    uint8_t dep[MAXB];   // bit t: team t already got depot bonus
    int16_t res[2];
    int16_t turn;
    int32_t occ[2];
    void clear() { memset(this, 0, sizeof(State)); for (int b = 0; b < MAXB; b++) owner[b] = -1; }
};

struct SpawnCmd { int16_t cell; int8_t kind; int16_t n; };   // cell -1 = base; kind 0=F,1=W
struct MoveCmd { int16_t cell; int8_t kind; int8_t dir; int16_t n; int16_t cell2; };  // dir 4 = tele to cell2
constexpr int MAXSP = 48, MAXMV = 400, MAXPR = 20;
struct Orders {
    SpawnCmd sp[MAXSP];
    MoveCmd mv[MAXMV];
    int8_t prio[MAXPR];
    int nsp = 0, nmv = 0, npr = 0;
    void clear() { nsp = nmv = npr = 0; }
    void addSpawn(int kind, int n, int cell = -1) { if (n > 0 && nsp < MAXSP) sp[nsp++] = {(int16_t)cell, (int8_t)kind, (int16_t)n}; }
    void addMove(int cell, int kind, int dir, int n) { if (n > 0 && nmv < MAXMV) mv[nmv++] = {(int16_t)cell, (int8_t)kind, (int8_t)dir, (int16_t)n, (int16_t)-1}; }
    void addTele(int src, int kind, int n, int dst) { if (n > 0 && nmv < MAXMV) mv[nmv++] = {(int16_t)src, (int8_t)kind, (int8_t)4, (int16_t)n, (int16_t)dst}; }
    void addPrio(int b) { if (npr < MAXPR) prio[npr++] = (int8_t)b; }
};

inline int owned(const Map& M, const State& s, int t, int type) {
    int c = 0;
    for (int b = 0; b < M.nb; b++) c += (s.owner[b] == t && M.btype[b] == type);
    return c;
}
inline int wcost(const Map& M, const State& s, int t) { return std::max(3 - owned(M, s, t, T_ENG), 2); }
inline int score2(const Map& M, const State& s, int t) {
    int r = 0;
    for (int b = 0; b < M.nb; b++) if (s.owner[b] == t) r += M.score2[b];
    return r;
}

// Default (home-oriented) tiebreak key for capture ordering when no PRIORITY is given.
// homeFlip[t]: team t's base lies in the "second half" => rotate coordinates by 180deg.
struct StepCfg { bool homeFlip[2] = {false, false}; };

// One full engine turn (steps 2..7). Both teams' orders are applied simultaneously.
inline void step(const Map& M, State& s, const Orders& o0, const Orders& o1, const StepCfg& cfg = StepCfg()) {
    const Orders* O[2] = {&o0, &o1};
    s.turn++;
    // ---- 2. spawn
    for (int t = 0; t < 2; t++) {
        int wc = wcost(M, s, t);
        for (int i = 0; i < O[t]->nsp; i++) {
            const SpawnCmd& c = O[t]->sp[i];
            if (c.n <= 0) continue;
            int cell;
            if (c.cell < 0) cell = M.base[t];
            else {
                int b = M.bat[c.cell];
                if (b < 0 || M.btype[b] != T_HOSPITAL || s.owner[b] != t) continue;
                cell = c.cell;
            }
            int cost = c.kind == 0 ? 5 : wc;
            int n = std::min<int>(c.n, s.res[t] / cost);
            if (n <= 0) continue;
            s.res[t] -= n * cost;
            if (c.kind == 0) s.F[t][cell] += n; else s.W[t][cell] += n;
        }
    }
    // ---- 3. move
    int16_t arr[2][2][NC];
    memset(arr, 0, sizeof(arr));
    for (int t = 0; t < 2; t++) {
        bool teleUsed = false;
        int stations = -1;
        for (int i = 0; i < O[t]->nmv; i++) {
            const MoveCmd& m = O[t]->mv[i];
            int16_t* pool = m.kind == 0 ? s.F[t] : s.W[t];
            if (m.dir < 4) {
                int nc = M.nbr[m.cell][m.dir];
                if (nc < 0) continue;
                int n = std::min<int>(m.n, pool[m.cell]);
                if (n <= 0) continue;
                pool[m.cell] -= n;
                arr[t][m.kind][nc] += n;
            } else {
                if (teleUsed) continue;
                if (m.cell == m.cell2) continue;
                int b1 = M.bat[m.cell], b2 = M.bat[m.cell2];
                if (b1 < 0 || b2 < 0) continue;
                if (M.btype[b1] != T_STATION || M.btype[b2] != T_STATION) continue;
                if (s.owner[b1] != t || s.owner[b2] != t) continue;
                if (stations < 0) stations = owned(M, s, t, T_STATION);
                if (stations < 2) continue;
                int n = std::min<int>(std::min<int>(m.n, 5), pool[m.cell]);
                if (n <= 0) continue;
                pool[m.cell] -= n;
                arr[t][m.kind][m.cell2] += n;
                teleUsed = true;
            }
        }
    }
    for (int t = 0; t < 2; t++)
        for (int c = 0; c < NC; c++) { s.F[t][c] += arr[t][0][c]; s.W[t][c] += arr[t][1][c]; }
    // ---- 4. combat
    for (int c = 0; c < NC; c++) {
        bool h0 = (s.F[0][c] | s.W[0][c]) != 0, h1 = (s.F[1][c] | s.W[1][c]) != 0;
        if (!(h0 && h1)) continue;
        int w0 = s.W[0][c], w1 = s.W[1][c];
        int m = std::min(w0, w1);
        w0 -= m; w1 -= m;
        s.W[0][c] = w0; s.W[1][c] = w1;
        int victim = -1;
        if (w0 > 0 && w1 == 0) victim = 1;
        else if (w1 > 0 && w0 == 0) victim = 0;
        if (victim >= 0) {
            bool had = s.F[victim][c] > 0;
            s.F[victim][c] = 0;
            int b = M.bat[c];
            if (b >= 0 && s.owner[b] == victim && had) s.owner[b] = -1;
        }
    }
    // ---- 6. income
    for (int t = 0; t < 2; t++) {
        int halls = owned(M, s, t, T_HALL);
        s.res[t] = (int16_t)std::min(s.res[t] + 10 + 2 * halls, RES_CAP);
    }
    // ---- 7. capture
    bool lib[2] = {owned(M, s, 0, T_LIBRARY) > 0, owned(M, s, 1, T_LIBRARY) > 0};
    int depotPending[2] = {0, 0};
    for (int t = 0; t < 2; t++) {
        int cand[MAXB], nc = 0;
        for (int b = 0; b < M.nb; b++) {
            int c = M.bcell[b];
            if (s.F[t][c] <= 0) continue;
            if (s.F[1 - t][c] > 0) continue;
            if (s.owner[b] == t) continue;
            cand[nc++] = b;
        }
        if (!nc) continue;
        int ord[MAXB], no = 0;
        bool used[MAXB] = {false};
        for (int i = 0; i < O[t]->npr; i++) {
            int b = O[t]->prio[i];
            if (b < 0 || b >= M.nb || used[b]) continue;
            for (int j = 0; j < nc; j++) if (cand[j] == b) { ord[no++] = b; used[b] = true; break; }
        }
        int rest[MAXB], nr = 0;
        for (int j = 0; j < nc; j++) if (!used[cand[j]]) rest[nr++] = cand[j];
        auto key = [&](int b) {
            int x = M.bcell[b] % GW, y = M.bcell[b] / GW;
            if (cfg.homeFlip[t]) { x = GW - 1 - x; y = GH - 1 - y; }
            return y * GW + x;
        };
        std::sort(rest, rest + nr, [&](int a, int b) {
            if (M.score2[a] != M.score2[b]) return M.score2[a] > M.score2[b];
            return key(a) < key(b);
        });
        for (int j = 0; j < nr; j++) ord[no++] = rest[j];
        for (int j = 0; j < no; j++) {
            int b = ord[j];
            int cost = M.btype[b] == T_PLAZA ? 4 : 2;
            if (lib[t]) cost -= 1;
            if (cost < 1) cost = 1;
            if (s.res[t] < cost) continue;
            s.res[t] -= cost;
            if (s.owner[b] < 0) {
                s.owner[b] = (int8_t)t;
                if (M.btype[b] == T_DEPOT && !((s.dep[b] >> t) & 1)) {
                    s.dep[b] |= (uint8_t)(1 << t);
                    depotPending[t] += 15;
                }
            } else {
                s.owner[b] = -1;
            }
        }
    }
    for (int t = 0; t < 2; t++)
        if (depotPending[t]) s.res[t] = (int16_t)std::min<int>(s.res[t] + depotPending[t], RES_CAP);
    for (int t = 0; t < 2; t++) s.occ[t] += score2(M, s, t);
}

}  // namespace eng

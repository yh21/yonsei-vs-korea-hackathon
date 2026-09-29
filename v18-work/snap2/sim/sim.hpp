// sim.hpp - exact C++ re-implementation of the game engine (pipeline.py) used for look-ahead.
// Team index t in {0,1}. Scouts (S) are not modelled (they never influence score/combat outcomes
// except being wiped; our bot does not build them).
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <queue>
#include <string>
#include <vector>

namespace sim {

constexpr int GW = 15, GH = 15, NC = GW * GH, MAXB = 17, TOTAL_TURNS = 160;
enum BType : int { T_PLAZA = 0, T_HALL, T_STATION, T_LIBRARY, T_ENG, T_HOSPITAL, T_WATCH, T_DEPOT };
enum { K_F = 0, K_W = 1 };
enum { D_U = 0, D_D = 1, D_L = 2, D_R = 3, D_TELE = 4 };
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
inline const char* btypeName(int t) {
    static const char* n[] = {"PLAZA", "HALL", "STATION", "LIBRARY", "ENG", "HOSPITAL", "WATCH", "DEPOT"};
    return n[t];
}

struct Map {
    bool pass[NC];
    int nb = 0;
    int bcell[MAXB], btype[MAXB], score2[MAXB], sym[MAXB];  // score2 = 2 * (believed) score
    int bat[NC];
    int nbr[NC][4];  // U D L R, -1 if blocked / off-map
    uint8_t dist[NC][NC];
    int base[2] = {0, 0};
    bool flip[2] = {false, false};
    int homeKey[2][NC];
    int cord[2][NC];    // cell indices in team-home order (ascending homeKey)
    int bord[2][MAXB];  // building ids in team-home order of their cells
    uint8_t predMask[NC][NC];  // predMask[z][x]: bit k set if nbr[x][k] is one step closer to z than x
    uint8_t byDist[NC][NC];  // cells reachable from s ordered by increasing path distance (s first)
    int byDistN[NC];
    int total2 = 0;

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
            dist[s][s] = 0;
            q[qt++] = s;
            while (qh < qt) {
                int v = q[qh++];
                for (int k = 0; k < 4; k++) {
                    int u = nbr[v][k];
                    if (u >= 0 && dist[s][u] == 255) { dist[s][u] = dist[s][v] + 1; q[qt++] = u; }
                }
            }
        }
        for (int s0 = 0; s0 < NC; s0++) {
            int n = 0;
            if (pass[s0]) {
                // BFS order is already by increasing distance
                int q[NC], qh = 0, qt = 0; bool seen[NC] = {false};
                q[qt++] = s0; seen[s0] = true;
                while (qh < qt) {
                    int v = q[qh++]; byDist[s0][n++] = (uint8_t)v;
                    for (int k = 0; k < 4; k++) { int u = nbr[v][k]; if (u >= 0 && !seen[u]) { seen[u] = true; q[qt++] = u; } }
                }
            }
            byDistN[s0] = n;
        }
        for (int z = 0; z < NC; z++)
            for (int x = 0; x < NC; x++) {
                uint8_t m = 0;
                if (dist[z][x] != 255 && x != z)
                    for (int k = 0; k < 4; k++) { int pn = nbr[x][k]; if (pn >= 0 && dist[z][pn] + 1 == dist[z][x]) m |= (uint8_t)(1 << k); }
                predMask[z][x] = m;
            }
        for (int b = 0; b < nb; b++) {
            sym[b] = -1;
            int x = bcell[b] % GW, y = bcell[b] / GW;
            int sc = (GH - 1 - y) * GW + (GW - 1 - x);
            sym[b] = bat[sc];
        }
        for (int t = 0; t < 2; t++)
            for (int c = 0; c < NC; c++) {
                int x = c % GW, y = c / GW;
                if (flip[t]) { x = GW - 1 - x; y = GH - 1 - y; }
                homeKey[t][c] = y * GW + x;
            }
        for (int t = 0; t < 2; t++) {
            for (int c = 0; c < NC; c++) cord[t][c] = c;
            std::sort(cord[t], cord[t] + NC, [&](int a, int b) { return homeKey[t][a] < homeKey[t][b]; });
            for (int b = 0; b < nb; b++) bord[t][b] = b;
            std::sort(bord[t], bord[t] + nb, [&](int a, int b) { return homeKey[t][bcell[a]] < homeKey[t][bcell[b]]; });
        }
        total2 = 0;
        for (int b = 0; b < nb; b++) total2 += score2[b];
    }
    int d(int a, int b) const { return dist[a][b]; }
};

struct State {
    int16_t F[2][NC];
    int16_t Wn[2][NC];
    int8_t owner[MAXB];  // -1 neutral, 0/1 team
    uint8_t dep[MAXB];   // bit t: team t already claimed depot bonus
    int16_t res[2];
    int16_t turn;  // completed turns
    int32_t occ[2];
    uint32_t rev[2];

    void init(const Map& M) {
        memset(this, 0, sizeof(State));
        for (int b = 0; b < MAXB; b++) owner[b] = -1;
        res[0] = res[1] = 10;
        (void)M;
    }
};

struct SpawnCmd { int16_t cell; int8_t kind; int16_t n; };  // cell -1 = base
struct MoveCmd { int16_t cell; int8_t kind; int8_t dir; int16_t n; int16_t cell2; };  // dir 4 = TELE to cell2
constexpr int MAXSP = 96, MAXMV = 600, MAXPR = 24;
struct Orders {
    SpawnCmd sp[MAXSP];
    MoveCmd mv[MAXMV];
    int8_t prio[MAXPR];  // building ids
    int nsp = 0, nmv = 0, npr = 0;
    void clear() { nsp = nmv = npr = 0; }
    void addSpawn(int kind, int n, int cell = -1) {
        if (n <= 0 || nsp >= MAXSP) return;
        sp[nsp++] = {(int16_t)cell, (int8_t)kind, (int16_t)n};
    }
    void addMove(int cell, int kind, int dir, int n) {
        if (n <= 0 || nmv >= MAXMV) return;
        mv[nmv++] = {(int16_t)cell, (int8_t)kind, (int8_t)dir, (int16_t)n, (int16_t)-1};
    }
    void addTele(int src, int kind, int n, int dst) {
        if (n <= 0 || nmv >= MAXMV) return;
        mv[nmv++] = {(int16_t)src, (int8_t)kind, (int8_t)D_TELE, (int16_t)n, (int16_t)dst};
    }
    void addPrio(int b) {
        if (npr < MAXPR) prio[npr++] = (int8_t)b;
    }
};

inline int ownedCount(const Map& M, const State& s, int t, int type) {
    int c = 0;
    for (int b = 0; b < M.nb; b++) c += (s.owner[b] == t && M.btype[b] == type);
    return c;
}
inline int wcost(const Map& M, const State& s, int t) {
    int e = ownedCount(M, s, t, T_ENG);
    return std::max(3 - e, 2);
}
inline int score2(const Map& M, const State& s, int t) {
    int r = 0;
    for (int b = 0; b < M.nb; b++)
        if (s.owner[b] == t) r += M.score2[b];
    return r;
}
inline int totalW(const State& s, int t) {
    int r = 0;
    for (int c = 0; c < NC; c++) r += s.Wn[t][c];
    return r;
}
inline int totalF(const State& s, int t) {
    int r = 0;
    for (int c = 0; c < NC; c++) r += s.F[t][c];
    return r;
}

// Returns -1 ongoing, 0/1 winner, 2 draw. Uses s.turn (completed turns).
inline int judge(const Map& M, const State& s, bool* instant = nullptr) {
    int sc0 = score2(M, s, 0), sc1 = score2(M, s, 1);
    if (instant) *instant = false;
    if (sc1 == 0 && sc0 * 2 > M.total2) { if (instant) *instant = true; return 0; }
    if (sc0 == 0 && sc1 * 2 > M.total2) { if (instant) *instant = true; return 1; }
    if (s.turn < TOTAL_TURNS) return -1;
    if (sc0 != sc1) return sc0 > sc1 ? 0 : 1;
    if (s.occ[0] != s.occ[1]) return s.occ[0] > s.occ[1] ? 0 : 1;
    int u0 = 0, u1 = 0;
    for (int c = 0; c < NC; c++) { u0 += s.F[0][c] * 5 + s.Wn[0][c] * 3; u1 += s.F[1][c] * 5 + s.Wn[1][c] * 3; }
    if (u0 != u1) return u0 > u1 ? 0 : 1;
    return 2;
}

// One full engine turn. Both teams' orders are applied simultaneously.
inline void step(const Map& M, State& s, const Orders& o0, const Orders& o1, bool doReveal = false) {
    const Orders* O[2] = {&o0, &o1};
    s.turn++;
    // ---- 2. spawn
    for (int t = 0; t < 2; t++) {
        int eng = ownedCount(M, s, t, T_ENG);
        int wc = std::max(3 - eng, 2);
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
            int cost = c.kind == K_F ? 5 : wc;
            int n = std::min<int>(c.n, s.res[t] / cost);
            if (n <= 0) continue;
            s.res[t] -= n * cost;
            if (c.kind == K_F) s.F[t][cell] += n; else s.Wn[t][cell] += n;
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
            int16_t* pool = m.kind == K_F ? s.F[t] : s.Wn[t];
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
                if (stations < 0) stations = ownedCount(M, s, t, T_STATION);
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
        for (int c = 0; c < NC; c++) { s.F[t][c] += arr[t][0][c]; s.Wn[t][c] += arr[t][1][c]; }
    // ---- 4. combat
    for (int c = 0; c < NC; c++) {
        bool h0 = s.F[0][c] | s.Wn[0][c], h1 = s.F[1][c] | s.Wn[1][c];
        if (!(h0 && h1)) continue;
        int w0 = s.Wn[0][c], w1 = s.Wn[1][c];
        int m = std::min(w0, w1);
        w0 -= m; w1 -= m;
        s.Wn[0][c] = w0; s.Wn[1][c] = w1;
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
        int halls = ownedCount(M, s, t, T_HALL);
        s.res[t] = (int16_t)std::min(s.res[t] + 10 + 2 * halls, RES_CAP);
    }
    // ---- 7. capture
    bool lib[2] = {ownedCount(M, s, 0, T_LIBRARY) > 0, ownedCount(M, s, 1, T_LIBRARY) > 0};
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
            for (int j = 0; j < nc; j++)
                if (cand[j] == b) { ord[no++] = b; used[b] = true; break; }
        }
        int rest[MAXB], nr = 0;
        for (int j = 0; j < nc; j++) if (!used[cand[j]]) rest[nr++] = cand[j];
        auto vs = [&](int b) { return ((s.rev[t] >> b) & 1) ? M.score2[b] : 0; };
        std::sort(rest, rest + nr, [&](int a, int b) {
            int va = vs(a), vb = vs(b);
            if (va != vb) return va > vb;
            return M.homeKey[t][M.bcell[a]] < M.homeKey[t][M.bcell[b]];
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
                s.rev[t] |= 1u << b;
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
    // ---- 8. reveal
    if (doReveal) {
        for (int t = 0; t < 2; t++) {
            for (int b = 0; b < M.nb; b++) {
                if ((s.rev[t] >> b) & 1) continue;
                int c = M.bcell[b];
                bool r = (s.F[t][c] | s.Wn[t][c]) != 0 || s.owner[b] == t;
                if (!r) {
                    int bx = c % GW, by = c / GW;
                    for (int w = 0; w < M.nb && !r; w++)
                        if (M.btype[w] == T_WATCH && s.owner[w] == t) {
                            int wx = M.bcell[w] % GW, wy = M.bcell[w] / GW;
                            if (std::max(std::abs(bx - wx), std::abs(by - wy)) <= 3) r = true;
                        }
                }
                if (r) s.rev[t] |= 1u << b;
            }
        }
    }
}

}  // namespace sim

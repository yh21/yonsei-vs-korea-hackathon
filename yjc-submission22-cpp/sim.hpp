// Exact game simulator (mirrors yk-development-tools/engine/pipeline.py) for lookahead.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sim {
constexpr int MAXC = 225, MAXB = 17;
enum BT : uint8_t { PLAZA, HALL, STATION, LIBRARY, ENG, HOSPITAL, WATCH, DEPOT };
inline int typeFromString(const std::string& s) {
    if (s == "PLAZA") return PLAZA; if (s == "HALL") return HALL; if (s == "STATION") return STATION;
    if (s == "LIBRARY") return LIBRARY; if (s == "ENG") return ENG; if (s == "HOSPITAL") return HOSPITAL;
    if (s == "WATCH") return WATCH; return DEPOT;
}
// static geometry (one per game)
struct Geo {
    int W = 15, H = 15, N = 225;
    bool pass[MAXC];
    int16_t nb[MAXC][4]; // neighbor cell in dir 0=U 1=D 2=L 3=R, -1 if none
    int8_t nn[MAXC];      // number of passable neighbors
    int16_t nl[MAXC][5];  // N[c]: c itself + passable neighbours
    int8_t nln[MAXC];
    uint8_t dist[MAXC][MAXC]; // 255 unreachable
    int nB = 0;
    int bcell[MAXB]; uint8_t btype[MAXB]; int bAt[MAXC]; int sym[MAXB];
    int16_t ball3[MAXC][32]; int8_t nball3[MAXC];
    int base[2] = {0, 0}; // cells of team 0/1 bases
    bool flip[2] = {false, false}; // orientation: team's base on the right side
    int cx(int c) const { return c % W; }
    int cy(int c) const { return c / W; }
    int idx(int x, int y) const { return y * W + x; }
    int homeKey(int t, int c) const { // orientation-normalised (y,x) key for team t
        int x = c % W, y = c / W;
        if (flip[t]) { x = W - 1 - x; y = H - 1 - y; }
        return y * W + x;
    }
    int D(int a, int b) const { return dist[a][b]; }
};
inline Geo G;

struct Team {
    int16_t F[MAXC], W[MAXC], S[MAXC];
    int16_t res;
};
struct GS {
    int turn = 0;
    Team t[2];
    int8_t own[MAXB]; // 0 neutral, 1 team0, 2 team1
    uint8_t depot[MAXB]; // bit t: team t already claimed the bonus
    int32_t occ10[2];    // v22: accumulated occupation-turns (x10 score units), the 2nd tie-break
};
struct Spawn { int8_t kind; int16_t cell; int16_t n; };
struct Mv { int16_t src; int8_t kind; int8_t dir; int16_t n; };
constexpr int MAXMV = 320;
struct Plan {
    Spawn sp[32]; int nsp = 0;
    Mv mv[MAXMV]; int nmv = 0;
    int16_t teleSrc = -1, teleDst = -1; int8_t teleKind = 1; int16_t teleN = 0;
    int8_t prio[MAXB]; int nprio = 0;
    void clear() { nsp = 0; nmv = 0; teleSrc = -1; nprio = 0; }
    void addSpawn(int kind, int cell, int n) { if (n > 0 && nsp < 32) sp[nsp++] = {(int8_t)kind, (int16_t)cell, (int16_t)n}; }
    void addMove(int src, int kind, int dir, int n) { if (n > 0 && dir >= 0 && dir < 4 && nmv < MAXMV) mv[nmv++] = {(int16_t)src, (int8_t)kind, (int8_t)dir, (int16_t)n}; }
};
// building score belief x10 (unknown -> expected)
inline int sc10[MAXB];

inline int16_t* unitArr(Team& t, int kind) { return kind == 0 ? t.F : kind == 1 ? t.W : t.S; }

inline int score10(const GS& s, int t) { int r = 0; for (int b = 0; b < G.nB; b++) if (s.own[b] == t + 1) r += sc10[b]; return r; }
inline int engCount(const GS& s, int t) { int e = 0; for (int b = 0; b < G.nB; b++) if (G.btype[b] == ENG && s.own[b] == t + 1) e++; return e; }
inline int wCost(const GS& s, int t) { return std::max(2, 3 - engCount(s, t)); }

inline void step(GS& s, const Plan& pa, const Plan& pb) {
    const Plan* pl[2] = {&pa, &pb};
    s.turn++;
    // 2. spawn
    for (int t = 0; t < 2; t++) {
        Team& T = s.t[t]; int eng = engCount(s, t);
        for (int i = 0; i < pl[t]->nsp; i++) {
            const Spawn& sp = pl[t]->sp[i];
            bool ok = sp.cell == G.base[t];
            if (!ok) { int b = G.bAt[sp.cell]; ok = b >= 0 && G.btype[b] == HOSPITAL && s.own[b] == t + 1; }
            if (!ok) continue;
            int cost = sp.kind == 0 ? 5 : sp.kind == 1 ? std::max(2, 3 - eng) : 2;
            int n = std::min<int>(sp.n, T.res / cost);
            if (n <= 0) continue;
            T.res -= n * cost; unitArr(T, sp.kind)[sp.cell] += n;
        }
    }
    // 3. moves
    static thread_local int16_t arr[2][3][MAXC];
    std::memset(arr, 0, sizeof(arr));
    for (int t = 0; t < 2; t++) {
        Team& T = s.t[t];
        if (pl[t]->teleSrc >= 0) {
            int sb = G.bAt[pl[t]->teleSrc], db = G.bAt[pl[t]->teleDst];
            if (sb >= 0 && db >= 0 && sb != db && G.btype[sb] == STATION && G.btype[db] == STATION && s.own[sb] == t + 1 && s.own[db] == t + 1) {
                int st = 0; for (int b = 0; b < G.nB; b++) if (G.btype[b] == STATION && s.own[b] == t + 1) st++;
                if (st >= 2) {
                    int16_t* a = unitArr(T, pl[t]->teleKind);
                    int n = std::min<int>(std::min<int>(pl[t]->teleN, 5), a[pl[t]->teleSrc]);
                    if (n > 0) { a[pl[t]->teleSrc] -= n; arr[t][pl[t]->teleKind][pl[t]->teleDst] += n; }
                }
            }
        }
    
        for (int i = 0; i < pl[t]->nmv; i++) {
            const Mv& m = pl[t]->mv[i];
            int d = G.nb[m.src][m.dir]; if (d < 0) continue;
            int16_t* a = unitArr(T, m.kind);
            int n = std::min<int>(m.n, a[m.src]); if (n <= 0) continue;
            a[m.src] -= n; arr[t][m.kind][d] += n;
        }
    }
    for (int t = 0; t < 2; t++) for (int c = 0; c < G.N; c++) {
        s.t[t].F[c] += arr[t][0][c]; s.t[t].W[c] += arr[t][1][c]; s.t[t].S[c] += arr[t][2][c];
    }
    // 4. combat
    Team &A = s.t[0], &B = s.t[1];
    for (int c = 0; c < G.N; c++) {
        bool a = A.F[c] | A.W[c] | A.S[c], b = B.F[c] | B.W[c] | B.S[c];
        if (!(a && b)) continue;
        int m = std::min(A.W[c], B.W[c]); A.W[c] -= m; B.W[c] -= m;
        int bi = G.bAt[c];
        if (A.W[c] > 0 && B.W[c] == 0) {
            bool had = B.F[c] > 0; B.F[c] = 0; B.S[c] = 0;
            if (bi >= 0 && s.own[bi] == 2 && had) s.own[bi] = 0;
        } else if (B.W[c] > 0 && A.W[c] == 0) {
            bool had = A.F[c] > 0; A.F[c] = 0; A.S[c] = 0;
            if (bi >= 0 && s.own[bi] == 1 && had) s.own[bi] = 0;
        }
    }
    // 6. income
    for (int t = 0; t < 2; t++) {
        int halls = 0; for (int b = 0; b < G.nB; b++) if (G.btype[b] == HALL && s.own[b] == t + 1) halls++;
        s.t[t].res = (int16_t)std::min(40, s.t[t].res + 10 + 2 * halls);
    }
    // 7. capture
    bool lib[2]; int depotPend[2] = {0, 0};
    for (int t = 0; t < 2; t++) { lib[t] = false; for (int b = 0; b < G.nB; b++) if (G.btype[b] == LIBRARY && s.own[b] == t + 1) lib[t] = true; }
    for (int t = 0; t < 2; t++) {
        Team& T = s.t[t]; Team& O = s.t[1 - t];
        int cand[MAXB], nc = 0; bool used[MAXB] = {};
        int order[MAXB], no = 0;
        for (int i = 0; i < pl[t]->nprio; i++) {
            int b = pl[t]->prio[i]; if (b < 0 || b >= G.nB || used[b]) continue;
            int c = G.bcell[b];
            if (T.F[c] > 0 && O.F[c] == 0 && s.own[b] != t + 1) { used[b] = true; order[no++] = b; }
        }
        for (int b = 0; b < G.nB; b++) if (!used[b]) {
            int c = G.bcell[b];
            if (T.F[c] > 0 && O.F[c] == 0 && s.own[b] != t + 1) cand[nc++] = b;
        }
        std::sort(cand, cand + nc, [&](int x, int y) {
            if (sc10[x] != sc10[y]) return sc10[x] > sc10[y];
            return G.homeKey(t, G.bcell[x]) < G.homeKey(t, G.bcell[y]);
        });
        for (int i = 0; i < nc; i++) order[no++] = cand[i];
        for (int i = 0; i < no; i++) {
            int b = order[i];
            int cost = (G.btype[b] == PLAZA ? 4 : 2) - (lib[t] ? 1 : 0); cost = std::max(1, cost);
            if (T.res < cost) continue;
            T.res -= cost;
            if (s.own[b] == 0) {
                s.own[b] = t + 1;
                if (G.btype[b] == DEPOT && !(s.depot[b] >> t & 1)) { s.depot[b] |= 1 << t; depotPend[t] += 15; }
            } else s.own[b] = 0;
        }
    }
    for (int t = 0; t < 2; t++) s.t[t].res = (int16_t)std::min(40, s.t[t].res + depotPend[t]);
    for (int t = 0; t < 2; t++) s.occ10[t] += score10(s, t);
}

// Build static geometry. terr[y] rows use '#' for obstacles. Buildings: (id,x,y,type)
struct BInit { int id, x, y; std::string type; };
inline void initGeo(int W, int H, const std::vector<std::string>& terr, const std::vector<BInit>& bs, int bx0, int by0, int bx1, int by1) {
    G.W = W; G.H = H; G.N = W * H;
    for (int c = 0; c < G.N; c++) G.pass[c] = terr[c / W][c % W] != '#';
    for (int i = 0; i < G.N; i++) G.bAt[i] = -1;
    G.nB = (int)bs.size();
    for (auto& b : bs) { G.bcell[b.id] = b.y * W + b.x; G.btype[b.id] = typeFromString(b.type); G.bAt[b.y * W + b.x] = b.id; }
    G.base[0] = by0 * W + bx0; G.base[1] = by1 * W + bx1;
    const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
    for (int c = 0; c < G.N; c++) {
        G.nn[c] = 0; G.nln[c] = 0; G.nl[c][G.nln[c]++] = c;
        for (int d = 0; d < 4; d++) {
            int x = c % W + dx[d], y = c / W + dy[d];
            if (x < 0 || y < 0 || x >= W || y >= H || !G.pass[y * W + x]) { G.nb[c][d] = -1; continue; }
            G.nb[c][d] = y * W + x; G.nn[c]++; G.nl[c][G.nln[c]++] = y * W + x;
        }
    }
    for (int s = 0; s < G.N; s++) {
        for (int t = 0; t < G.N; t++) G.dist[s][t] = 255;
        if (!G.pass[s]) continue;
        int q[MAXC], h = 0, tl = 0; q[tl++] = s; G.dist[s][s] = 0;
        while (h < tl) { int v = q[h++]; for (int d = 0; d < 4; d++) { int u = G.nb[v][d]; if (u >= 0 && G.dist[s][u] == 255) { G.dist[s][u] = G.dist[s][v] + 1; q[tl++] = u; } } }
    }
    for (int c = 0; c < G.N; c++) { G.nball3[c] = 0; if (!G.pass[c]) continue; for (int z = 0; z < G.N; z++) if (G.dist[c][z] <= 3) { if (G.nball3[c] < 32) G.ball3[c][G.nball3[c]++] = z; } }
    // symmetric pairs
    for (auto& b : bs) { int sc = (H - 1 - b.y) * W + (W - 1 - b.x); G.sym[b.id] = G.bAt[sc]; }
    G.flip[0] = (bx0 * 2 > W - 1); G.flip[1] = (bx1 * 2 > W - 1);
}
} // namespace sim

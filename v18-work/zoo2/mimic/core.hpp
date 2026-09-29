// mimic core: 실제 서버 상대(Y)의 관측 패턴(초반 F 몇 기 -> 전부 W, 건물별 W 분산, TELE/전진 병원)을 흉내내는 단순 봇 엔진.
// v19 로직과 무관하게 처음부터 작성. 파라미터로 3개 변종(spread / blob / teleraid)을 만든다.
#pragma once
#include "protocol.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <map>
#include <queue>
#include <string>
#include <vector>

namespace mm {
using namespace std;
constexpr int N = 15, NC = 225;
enum { PLAZA, HALL, STATION, LIB, ENG, HOSP, WATCH, DEPOT, NT };

struct Params {
    int flagsOpen = 7, flagsLate = 5, openTurns = 8;
    int wt[NT] = {45, 90, 50, 55, 100, 60, 30, 80};  // 건물 종류별 가치
    bool blob = false;      // 전 병력 한 덩어리
    bool tele = false;      // TELE 사용
    bool fwd = true;        // 전진 병원 생산
    int hunt = 70;          // 적 깃발 사냥 가치
    int defend = 130;       // 내 건물 방어 가치 배율(%)
    int sep = 3;            // 거리 감쇠 상수
    int teleGain = 3;       // 텔레포트 이득 최소 거리
    int stationBoost = 0;   // 초반(턴<=stTurns) 역 가중 추가
    int needCap = 14;
    int guardVal = 90;
    int raidAll = 0;
    int safeMargin = 0;
    int raidStart = 999, raidBonus = 0, raidNeed = 0;  // 습격: 적 진영 ENG/HALL/병원에 raidNeed 기 배정
    bool guard = true;      // 내 ENG/HALL 에 W 1기 상주(수비)
};

static int tcode(const string& s) {
    if (s == "PLAZA") return PLAZA; if (s == "HALL") return HALL; if (s == "STATION") return STATION;
    if (s == "LIBRARY") return LIB; if (s == "ENG") return ENG; if (s == "HOSPITAL") return HOSP;
    if (s == "WATCH") return WATCH; return DEPOT;
}

struct Bot {
    Params P;
    bool ready = false;
    bool pass[NC];
    int dist[NC][NC];
    int base = 0, ebase = 0;
    vector<int> bcell, btype;
    int nb = 0;
    int blobPrev = -1;
    static int id(int x, int y) { return y * N + x; }
    static int cx(int c) { return c % N; }
    static int cy(int c) { return c / N; }
    static char dirc(int a, int b) {
        int d = b - a;
        if (d == 1) return 'R'; if (d == -1) return 'L'; if (d == N) return 'D'; return 'U';
    }
    void setup(const p::Init& I) {
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) pass[id(x, y)] = I.terrain[y][x] != '#';
        for (int s = 0; s < NC; s++) {
            for (int t = 0; t < NC; t++) dist[s][t] = 999;
            if (!pass[s]) continue;
            queue<int> q; q.push(s); dist[s][s] = 0;
            while (!q.empty()) {
                int c = q.front(); q.pop();
                for (int k = 0; k < 4; k++) {
                    int nx = cx(c) + (k == 0) - (k == 1), ny = cy(c) + (k == 2) - (k == 3);
                    if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
                    int n2 = id(nx, ny);
                    if (!pass[n2] || dist[s][n2] < 999) continue;
                    dist[s][n2] = dist[s][c] + 1; q.push(n2);
                }
            }
        }
        auto b = I.base(); base = id(b.first, b.second);
        auto e = I.bases[I.team == "Y" ? 1 : 0]; ebase = id(e.first, e.second);
        nb = I.buildings.size(); bcell.resize(nb); btype.resize(nb);
        for (auto& bd : I.buildings) { bcell[bd.id] = id(bd.x, bd.y); btype[bd.id] = tcode(bd.type); }
        ready = true;
    }
    static int nbr(int c, int k) {
        int nx = cx(c) + (k == 0) - (k == 1), ny = cy(c) + (k == 2) - (k == 3);
        if (nx < 0 || ny < 0 || nx >= N || ny >= N) return -1;
        return id(nx, ny);
    }

    vector<string> decide(const p::View& v, const p::Init& I) {
        if (!ready) setup(I);
        vector<string> out;
        const string my = I.team, op = I.opp;
        int mW[NC] = {}, mF[NC] = {}, eW[NC] = {}, eF[NC] = {}, pw[NC] = {};
        for (auto& u : v.units) {
            int c = id(u.x, u.y);
            if (u.team == my) { if (u.kind == "W") mW[c] += u.count; else if (u.kind == "F") mF[c] += u.count; }
            else { if (u.kind == "W") eW[c] += u.count; else if (u.kind == "F") eF[c] += u.count; }
        }
        // 건물 상태: 0 내 것 1 적 것 2 중립
        vector<int> own(nb, 2);
        int engOwned = 0;
        for (auto& b : v.buildings) {
            own[b.id] = b.owner == my ? 0 : (b.owner == op ? 1 : 2);
            if (own[b.id] == 0 && btype[b.id] == ENG) engOwned++;
        }
        int res = v.my_resource, turn = v.turn;
        int wcost = engOwned > 0 ? 2 : 3;
        int myF = 0; for (int c = 0; c < NC; c++) myF += mF[c];
        auto eNear = [&](int c, int r) {  // 반경 r(맨해튼) 안 적 W 수
            int s = 0;
            for (int d = 0; d < NC; d++) if (eW[d] && dist[c][d] <= r) s += eW[d];
            return s;
        };
        // ---------- 생산 ----------
        int nf = 0;
        int wantF = turn <= P.openTurns ? P.flagsOpen : P.flagsLate;
        if (myF < wantF) {
            nf = min(wantF - myF, res / 5);
            nf = min(nf, turn <= 1 ? 2 : (turn <= P.openTurns ? 2 : 1));
        }
        res -= 5 * nf;
        int nw = res / wcost;
        // 생산 위치
        int sc = base;
        // 전선 목표: 적 본진 방향의 가장 가까운 비소유 고가치 건물
        auto frontDist = [&](int from) {
            int best = 999;
            for (int b = 0; b < nb; b++) if (own[b] != 0) best = min(best, dist[from][bcell[b]] + (100 - P.wt[btype[b]]) / 25);
            return best;
        };
        if (P.fwd && (nw + nf) > 0) {
            int bestv = frontDist(base) - 2;
            for (int b = 0; b < nb; b++) if (btype[b] == HOSP && own[b] == 0) {
                int h = bcell[b];
                if (eNear(h, 2) > mW[h] + 3) continue;
                int fd = frontDist(h);
                if (fd < bestv) { bestv = fd; sc = h; }
            }
        }
        if (nf > 0) {
            out.push_back(sc == base ? p::spawn("F", nf) : p::spawn("F", nf, cx(sc), cy(sc)));
            mF[sc] += nf;
        }
        if (nw > 0) {
            out.push_back(sc == base ? p::spawn("W", nw) : p::spawn("W", nw, cx(sc), cy(sc)));
            mW[sc] += nw;
        }
        // ---------- 목표 ----------
        struct Goal { int cell, val, need, asg; int kind; };
        const bool pwGuardOK = true;
        vector<Goal> goals;
        for (int b = 0; b < nb; b++) {
            int c = bcell[b], w = P.wt[btype[b]];
            if (P.stationBoost && btype[b] == STATION && turn <= 25) w += P.stationBoost;
            if (own[b] == 0) {
                // 방어: 적 깃발이 붙었거나 적 W 가 가까움
                int en = eNear(c, 2), ef = 0;
                for (int d = 0; d < NC; d++) if (eF[d] && dist[c][d] <= 2) ef += eF[d];
                if (en > 0 || ef > 0) goals.push_back({c, w * P.defend / 100, min(P.needCap, en + 1), 0, 1});
                else if (P.guard && mW[c] == 0 && pwGuardOK)
                    goals.push_back({c, w * P.guardVal / 100, 1, 0, 3});
                else if (P.guard) {
                    // 이미 W 가 서 있으면 그 W 1기는 상주 배정
                    goals.push_back({c, w * P.guardVal / 100, 1, 0, 3});
                }
                continue;
            }
            int en = eNear(c, 1);
            int val = own[b] == 1 ? w * 11 / 10 : w, need = min(P.needCap, 1 + en + (own[b] == 1 ? 1 : 0));
            if (turn >= P.raidStart && (P.raidAll || btype[b] == ENG || btype[b] == HALL || btype[b] == HOSP) && dist[c][ebase] <= dist[c][base] + (P.raidAll ? 2 : 0)) {
                val += P.raidBonus; need = max(need, min(P.needCap, P.raidNeed + en));
            }
            goals.push_back({c, val, need, 0, 0});
        }
        // 내 깃발 지원: 내 깃발이 서 있는 비소유 건물(적 깃발과 대치 포함)에 W 를 보낸다
        for (int b = 0; b < nb; b++) if (own[b] != 0 && mF[bcell[b]] > 0) {
            int c = bcell[b];
            int need = (eF[c] > 0 || eNear(c, 2) > 0) ? min(P.needCap, eNear(c, 2) + 1 + (eF[c] > 0 ? 1 : 0)) : 0;
            if (need > 0) goals.push_back({c, 130, need, 0, 4});
        }
        // 적 깃발 사냥
        for (int c = 0; c < NC; c++) if (eF[c]) {
            int en = eNear(c, 1);
            goals.push_back({c, P.hunt, min(P.needCap, en + 1), 0, 2});
        }
        // ---------- TELE ----------
        vector<int> stations;
        for (int b = 0; b < nb; b++) if (btype[b] == STATION && own[b] == 0) stations.push_back(bcell[b]);
        if (P.tele && stations.size() >= 2) {
            auto sscore = [&](int s) {
                double best = 1e9;
                for (auto& g : goals) if (g.kind != 1 && g.kind != 3) best = min(best, dist[s][g.cell] * 10.0 - g.val / 10.0);
                return best;
            };
            int B = -1; double sb = 1e9;
            for (int s : stations) { double x = sscore(s); if (x < sb) { sb = x; B = s; } }
            int A = -1, bw = 0;
            for (int s : stations) if (s != B && mW[s] > bw && sscore(s) > sb + P.teleGain * 10.0) { bw = mW[s]; A = s; }
            if (A >= 0 && B >= 0 && eNear(B, 1) <= 5 + bw) {
                int k = min(5, mW[A]);
                if (k > 0) {
                    out.push_back(p::tele(cx(A), cy(A), "W", k, cx(B), cy(B)));
                    mW[A] -= k; pw[B] += k;
                }
            }
        }
        // ---------- W 배정 ----------
        struct Mv { int from, to, n; };
        vector<Mv> wm;
        vector<int> order;
        for (int c = 0; c < NC; c++) if (mW[c] > 0) order.push_back(c);
        sort(order.begin(), order.end(), [&](int a, int b) { return mW[a] > mW[b]; });
        // blob: 하나의 목표만 need 무한
        int blobGoal = -1;
        if (P.blob) {
            int tot = 0, sx = 0, sy = 0;
            for (int c = 0; c < NC; c++) if (mW[c]) { tot += mW[c]; sx += cx(c) * mW[c]; sy += cy(c) * mW[c]; }
            int cc = tot ? id(sx / tot, sy / tot) : base;
            if (!pass[cc]) cc = base;
            double best = -1e9;
            for (size_t i = 0; i < goals.size(); i++) {
                if (goals[i].kind == 3 || goals[i].kind == 1) continue;
                if (dist[goals[i].cell][ebase] > dist[goals[i].cell][base] + 1) continue;  // 내 진영 건물은 blob 대상 아님
                int dd = 999; for (int c : order) dd = min(dd, dist[c][goals[i].cell]);
                double sc2 = goals[i].val * 1.0 / (dd + P.sep) + (goals[i].kind == 2 ? 3 : 0);
                if (blobPrev >= 0 && goals[i].cell == blobPrev) sc2 *= 3;
                if (goals[i].kind == 2 && dd > 3) continue;
                if (sc2 > best) { best = sc2; blobGoal = i; }
            }
        }
        blobPrev = blobGoal >= 0 ? goals[blobGoal].cell : -1;
        for (int c : order) {
            int rem = mW[c];
            while (rem > 0) {
                int bi = -1; double bs = -1;
                for (size_t i = 0; i < goals.size(); i++) {
                    auto& g = goals[i];
                    if (P.blob && dist[g.cell][ebase] <= dist[g.cell][base] + 1 && g.kind != 1) continue;
                    if (g.asg >= g.need) continue;
                    if (dist[c][g.cell] >= 999) continue;
                    double s = g.val * 1.0 / (dist[c][g.cell] + P.sep);
                    if (s > bs) { bs = s; bi = i; }
                }
                if (bi < 0) break;
                auto& g = goals[bi];
                int k = min(rem, g.need - g.asg);
                g.asg += k; rem -= k;
                wm.push_back({c, g.cell, k});
            }
            if (rem > 0) {  // 남은 W: 가장 가치 높은(가까운) 목표로 집결
                int bi = -1; double bs = -1;
                if (P.blob && blobGoal >= 0 && (turn % 2 == 0 || rem >= 6)) bi = blobGoal;
                else for (size_t i = 0; i < goals.size(); i++) {
                    auto& g = goals[i];
                    if (g.kind == 3 || dist[c][g.cell] >= 999) continue;
                    double s = g.val * 1.0 / (dist[c][g.cell] * 0.5 + P.sep + 2);
                    if (s > bs) { bs = s; bi = i; }
                }
                wm.push_back({c, bi >= 0 ? goals[bi].cell : ebase, rem});
            }
        }
        // 이동 명령 집계
        map<pair<int, int>, int> wmove;  // (from, dir cell) -> n
        for (auto& m : wm) {
            if (m.from == m.to) { pw[m.from] += m.n; continue; }
            int best = -1, bscore = 1 << 30;
            for (int k = 0; k < 4; k++) {
                int n2 = nbr(m.from, k);
                if (n2 < 0 || !pass[n2] || dist[n2][m.to] >= dist[m.from][m.to]) continue;
                int sco = eW[n2] * 4 - mW[n2];
                if (sco < bscore) { bscore = sco; best = n2; }
            }
            if (best < 0) { pw[m.from] += m.n; continue; }
            // 무의미한 돌진 방지: 도착 칸 적 W 가 압도적이면 대기
            if (eW[best] > m.n + mW[best] + 2 && eW[m.from] == 0) { pw[m.from] += m.n; continue; }
            wmove[{m.from, best}] += m.n;
            pw[best] += m.n;
        }
        for (auto& [k, n] : wmove) out.push_back(p::move(cx(k.first), cy(k.first), "W", n, string(1, dirc(k.first, k.second))));
        // ---------- F 이동 ----------
        auto e1 = [&](int c) {  // c 와 인접 4칸의 적 W
            int s = eW[c];
            for (int k = 0; k < 4; k++) { int n2 = nbr(c, k); if (n2 >= 0) s += eW[n2]; }
            return s;
        };
        auto safe = [&](int c) { return pw[c] + P.safeMargin >= e1(c); };
        vector<char> claimed(goals.size(), 0);
        // 이미 목표 건물 위에 서 있는 깃발
        struct FU { int c; };
        vector<int> flags;  // 셀 (기 단위)
        for (int c = 0; c < NC; c++) for (int i = 0; i < mF[c]; i++) flags.push_back(c);
        vector<int> fdest(flags.size(), -1);
        vector<int> bldOf(NC, -1);
        for (int b = 0; b < nb; b++) bldOf[bcell[b]] = b;
        for (size_t i = 0; i < flags.size(); i++) {
            int c = flags[i], b = bldOf[c];
            if (b >= 0 && own[b] != 0) {
                // 이미 자리 잡음: 다른 깃발이 같은 칸이면 중복 -> 하나만 남긴다
                bool dup = false;
                for (size_t j = 0; j < i; j++) if (flags[j] == c && fdest[j] == c) dup = true;
                if (!dup) fdest[i] = c;
            }
        }
        {
            struct Pr { double s; int i, g; };
            vector<Pr> prs;
            vector<char> gtaken(goals.size(), 0);
            for (size_t gi = 0; gi < goals.size(); gi++)
                for (size_t j = 0; j < flags.size(); j++) if (fdest[j] == goals[gi].cell && goals[gi].kind == 0) gtaken[gi] = 1;
            for (size_t i = 0; i < flags.size(); i++) {
                if (fdest[i] >= 0) continue;
                int c = flags[i];
                for (size_t gi = 0; gi < goals.size(); gi++) {
                    auto& g = goals[gi];
                    if (g.kind != 0 || gtaken[gi] || dist[c][g.cell] >= 999) continue;
                    double s = g.val * 1.0 / (dist[c][g.cell] + P.sep);
                    if (eNear(g.cell, 2) > pw[g.cell] + 2 && dist[c][g.cell] > 1) s *= 0.3;
                    if (P.blob && blobGoal >= 0 && g.cell == goals[blobGoal].cell) s *= 3;
                    prs.push_back({s, (int)i, (int)gi});
                }
            }
            sort(prs.begin(), prs.end(), [](const Pr& a, const Pr& b) { return a.s > b.s; });
            for (auto& pr : prs) {
                if (fdest[pr.i] >= 0 || gtaken[pr.g]) continue;
                fdest[pr.i] = goals[pr.g].cell; gtaken[pr.g] = 1;
            }
            for (size_t i = 0; i < flags.size(); i++) if (fdest[i] < 0) fdest[i] = base;
        }
        map<pair<int, int>, int> fmove;
        for (size_t i = 0; i < flags.size(); i++) {
            int c = flags[i], g = fdest[i];
            int step = c;
            if (c != g) {
                int best = -1, bscore = 1 << 30;
                for (int k = 0; k < 4; k++) {
                    int n2 = nbr(c, k);
                    if (n2 < 0 || !pass[n2] || dist[n2][g] >= dist[c][g]) continue;
                    int sco = (safe(n2) ? 0 : 100) + e1(n2) - pw[n2];
                    if (sco < bscore) { bscore = sco; best = n2; }
                }
                if (best >= 0 && safe(best)) step = best;
                else if (!safe(c)) {  // 후퇴
                    int rb = -1, rs = 1 << 30;
                    for (int k = 0; k < 4; k++) {
                        int n2 = nbr(c, k);
                        if (n2 < 0 || !pass[n2]) continue;
                        int sco = (safe(n2) ? 0 : 100) + e1(n2) - pw[n2] + dist[n2][base] / 4;
                        if (sco < rs) { rs = sco; rb = n2; }
                    }
                    if (rb >= 0 && rs < 100) step = rb;
                }
            } else if (!safe(c)) {
                int rb = -1, rs = 1 << 30;
                for (int k = 0; k < 4; k++) {
                    int n2 = nbr(c, k);
                    if (n2 < 0 || !pass[n2]) continue;
                    int sco = (safe(n2) ? 0 : 100) + e1(n2) - pw[n2] + dist[n2][base] / 4;
                    if (sco < rs) { rs = sco; rb = n2; }
                }
                if (rb >= 0 && rs < 100) step = rb;
            }
            if (step != c) fmove[{c, step}]++;
        }
        for (auto& [k, n] : fmove) out.push_back(p::move(cx(k.first), cy(k.first), "F", n, string(1, dirc(k.first, k.second))));
        // ---------- 점령 우선순위 ----------
        {
            vector<pair<int, int>> pr;
            vector<int> idx(nb); for (int b = 0; b < nb; b++) idx[b] = b;
            sort(idx.begin(), idx.end(), [&](int a, int b) { return P.wt[btype[a]] > P.wt[btype[b]]; });
            for (int b : idx) pr.push_back({cx(bcell[b]), cy(bcell[b])});
            out.push_back(p::priority(pr));
        }
        return out;
    }
};

inline int envi(const char* n, int d) { const char* v = getenv(n); return v ? atoi(v) : d; }
inline void applyEnv(Params& P) {
    P.flagsOpen = envi("MM_FO", P.flagsOpen); P.flagsLate = envi("MM_FL", P.flagsLate); P.openTurns = envi("MM_OT", P.openTurns);
    P.blob = envi("MM_BLOB", P.blob); P.tele = envi("MM_TELE", P.tele); P.fwd = envi("MM_FWD", P.fwd);
    P.hunt = envi("MM_HUNT", P.hunt); P.defend = envi("MM_DEF", P.defend); P.sep = envi("MM_SEP", P.sep);
    P.teleGain = envi("MM_TG", P.teleGain); P.stationBoost = envi("MM_SB", P.stationBoost); P.needCap = envi("MM_NC", P.needCap);
    P.guard = envi("MM_GUARD", P.guard); P.guardVal = envi("MM_GV", P.guardVal);
    P.safeMargin = envi("MM_SM", P.safeMargin); P.raidAll = envi("MM_RA", P.raidAll); P.raidStart = envi("MM_RS", P.raidStart); P.raidBonus = envi("MM_RB", P.raidBonus); P.raidNeed = envi("MM_RN", P.raidNeed);
    static const char* nm[NT] = {"MM_W0", "MM_W1", "MM_W2", "MM_W3", "MM_W4", "MM_W5", "MM_W6", "MM_W7"};
    for (int i = 0; i < NT; i++) P.wt[i] = envi(nm[i], P.wt[i]);
}
inline int run(const Params& P0) {
    Params P = P0; applyEnv(P);
    Bot b; b.P = P;
    return p::run([&](const p::View& v, const p::Init& I) { return b.decide(v, I); });
}
}  // namespace mm

#include "protocol.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <queue>
#include <algorithm>
#include <map>
#include <set>
#include <array>
#include <tuple>
#include <numeric>

using namespace std;

namespace {
constexpr int INF = 1e9;
struct Planner {
    bool ready = false;
    int W = 15, H = 15, N = 225;
    string me, opp;
    vector<vector<int>> dist;
    vector<int> knownScore;
    vector<int> symId;
    vector<char> myDepotClaimed;
    vector<string> dirOrder;
    vector<pair<int,int>> idPos;
    vector<string> idType;

    int idx(int x, int y) const { return y * W + x; }
    pair<int,int> xy(int v) const { return {v % W, v / W}; }
    bool pass(const p::Init& init, int x, int y) const { return init.passable(x, y); }
    int homeKeyCell(int z) const {
        auto [x, y] = xy(z);
        if (me == "K") { x = W - 1 - x; y = H - 1 - y; }
        return y * W + x;
    }
    int homeKeyXY(int x, int y) const {
        if (me == "K") { x = W - 1 - x; y = H - 1 - y; }
        return y * W + x;
    }

    void init_once(const p::Init& init) {
        if (ready) return;
        ready = true; W = init.width; H = init.height; N = W * H; me = init.team; opp = init.opp;
        dirOrder = (init.base().first * 2 > W - 1) ? vector<string>{"D", "U", "R", "L"} : vector<string>{"U", "D", "L", "R"};
        int maxid = 0; for (auto &b : init.buildings) maxid = max(maxid, b.id);
        idPos.resize(maxid + 1); idType.resize(maxid + 1);
        knownScore.assign(maxid + 1, -1); symId.assign(maxid + 1, -1); myDepotClaimed.assign(maxid + 1, 0);
        for (auto &b : init.buildings) { idPos[b.id] = {b.x, b.y}; idType[b.id] = b.type; if (b.type == "PLAZA") knownScore[b.id] = 3; }
        map<pair<int,int>, int> at;
        for (auto &b : init.buildings) at[{b.x, b.y}] = b.id;
        for (auto &b : init.buildings) { auto it = at.find({W - 1 - b.x, H - 1 - b.y}); if (it != at.end()) symId[b.id] = it->second; }
        
        dist.assign(N, vector<int>(N, INF));
        const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
        for (int s = 0; s < N; s++) {
            auto [sx, sy] = xy(s); if (!pass(init, sx, sy)) continue;
            queue<int> q; dist[s][s] = 0; q.push(s);
            while (!q.empty()) {
                int v = q.front(); q.pop(); auto [x, y] = xy(v);
                for (int k = 0; k < 4; k++) {
                    int nx = x + dx[k], ny = y + dy[k]; if (!pass(init, nx, ny)) continue;
                    int u = idx(nx, ny);
                    if (dist[s][u] > dist[s][v] + 1) { dist[s][u] = dist[s][v] + 1; q.push(u); }
                }
            }
        }
    }

    void update_memory(const p::View& v) {
        for (auto &b : v.buildings) {
            if (b.id >= (int)knownScore.size()) continue;
            if (b.score >= 0) {
                knownScore[b.id] = b.score;
                int s = symId[b.id]; if (s >= 0) knownScore[s] = b.score;
            }
            if (b.type == "DEPOT" && b.owner == me) myDepotClaimed[b.id] = 1;
        }
    }

    int expectedScore(const p::Building& b) const {
        if (b.id < (int)knownScore.size() && knownScore[b.id] >= 0) return knownScore[b.id] * 10;
        if (b.type == "PLAZA") return 30;
        return (b.x >= 5 && b.x <= 9) ? 30 : 15;
    }

    // 🔥 범용 승리 전략 1: 씹정석 밸런스 기대값 (어떤 맵, 어떤 적이든 통용됨)
    int featureBonus(const p::Building& b, const p::View& v) const {
        int t = v.turn;
        // 중앙광장은 항상 든든한 꿀자리
        if (b.type == "PLAZA") return 150;
        
        // 보급소는 초반에만 가치 있고, 먹었으면 버림
        if (b.type == "DEPOT") {
            bool claimed = b.id < (int)myDepotClaimed.size() && myDepotClaimed[b.id];
            return claimed ? 0 : (t < 40 ? 60 : 15);
        }
        
        // 초반 경제(HALL)와 생산 효율(ENG)은 모든 전략 게임의 근본
        if (b.type == "HALL") return t < 70 ? 150 : (t < 120 ? 60 : 15);
        if (b.type == "ENG") return t < 80 ? 120 : (t < 120 ? 50 : 15);
        
        if (b.type == "HOSPITAL") {
            int bx = idPos[b.id].first;
            return (t < 100 ? 40 : 15) + ((bx >= 5 && bx <= 9) ? 20 : 0);
        }
        if (b.type == "LIBRARY") return t < 100 ? 40 : 10;
        if (b.type == "STATION") return t < 110 ? 20 : 10;
        return 0;
    }

    int buildingValue(const p::Building& b, const p::View& v) const {
        int val = expectedScore(b) * 4 + featureBonus(b, v);
        if (b.owner == opp) val = val * 19 / 10 + 45; // 뺏는 맛 극대화
        else if (b.owner == "N") val += 20;
        else val = val / 5; // 이미 내 거면 가치 하락
        if (b.x >= 5 && b.x <= 9) val += 15; // 중앙 장악 보너스
        if (v.turn > 120 && b.owner == opp) val += 70; // 후반엔 무조건 남의 거 부수기
        return val;
    }

    string best_step(const p::Init& init, int x, int y, int tx, int ty) const {
        if (x == tx && y == ty) return "";
        int best = INF; string ans = "";
        for (const string &d : dirOrder) {
            auto [dx, dy] = p::delta(d); int nx = x + dx, ny = y + dy;
            if (!pass(init, nx, ny)) continue;
            int dd = dist[idx(nx, ny)][idx(tx, ty)];
            if (dd < best) { best = dd; ans = d; }
        }
        return ans;
    }

    const p::Building* building_at(const p::View& v, int x, int y) const {
        for (auto &b : v.buildings) if (b.x == x && b.y == y) return &b;
        return nullptr;
    }

    vector<string> decide(const p::View& v, const p::Init& init) {
        init_once(init); update_memory(v);
        vector<string> out;
        int cells = N;
        vector<int> myF(cells), myW(cells), myS(cells), enF(cells), enW(cells), enS(cells);
        vector<int> cellOrder(cells); iota(cellOrder.begin(), cellOrder.end(), 0);
        sort(cellOrder.begin(), cellOrder.end(), [&](int a, int b) { return homeKeyCell(a) < homeKeyCell(b); });
        vector<const p::Building*> bOrder;
        for (auto &b : v.buildings) bOrder.push_back(&b);
        sort(bOrder.begin(), bOrder.end(), [&](auto a, auto b) { return homeKeyXY(a->x, a->y) < homeKeyXY(b->x, b->y); });

        int totalF = 0, totalW = 0, oppWTotal = 0, oppFTotal = 0;
        for (auto &u : v.units) {
            int z = idx(u.x, u.y);
            if (u.team == me) {
                if (u.kind == "F") myF[z] += u.count, totalF += u.count;
                else if (u.kind == "W") myW[z] += u.count, totalW += u.count;
            } else {
                if (u.kind == "F") enF[z] += u.count, oppFTotal += u.count;
                else if (u.kind == "W") enW[z] += u.count, oppWTotal += u.count;
                else enS[z] += u.count;
            }
        }

        int eng = 0, halls = 0, stations = 0;
        vector<pair<int,int>> spawnSites; spawnSites.push_back(init.base());
        for (auto &b : v.buildings) {
            if (b.owner == me) {
                if (b.type == "ENG") eng++;
                if (b.type == "HALL") halls++;
                if (b.type == "STATION") stations++;
                if (b.type == "HOSPITAL") spawnSites.push_back({b.x, b.y});
            }
        }
        sort(spawnSites.begin(), spawnSites.end(), [&](auto a, auto b) { return homeKeyXY(a.first, a.second) < homeKeyXY(b.first, b.second); });
        int wcost = max(2, 3 - eng);
        int nonOwned = 0; for (auto &b : v.buildings) if (b.owner != me) nonOwned++;

        // 🔥 범용 승리 전략 2: 완벽한 예산(Budget) 통제
        // 깃발병(F)이 적이나 중립 건물 위에 있으면, '점령 비용'을 무조건 빼두고 생산함.
        int R = v.my_resource;
        int capture_reserve = 0;
        for (auto* bp : bOrder) {
            if (bp->owner != me && myF[idx(bp->x, bp->y)] > 0) {
                capture_reserve += (bp->type == "PLAZA" ? 4 : 2);
            }
        }
        R = max(0, R - capture_reserve); // 점령할 돈은 건들지 마!

        // 적절한 F 유지 (건물 먹방 요원)
        int desiredF = (v.turn <= 20 ? 6 : (nonOwned >= 8 ? 6 : 4));
        if (v.turn > 120 && nonOwned > 0) desiredF = max(desiredF, 5);

        int makeF = 0, makeW = 0;
        if (v.turn == 1) {
            makeF = min(2, R / 5); R -= 5 * makeF;
            if (R >= wcost) { makeW = R / wcost; R -= makeW * wcost; }
        } else {
            int needF = max(0, desiredF - totalF);
            makeF = min({needF, (v.turn < 25 ? 2 : 1), R / 5});
            R -= 5 * makeF;
            makeW = R / wcost; 
            R -= makeW * wcost;
            if (R >= 5 && totalF + makeF < desiredF) { makeF++; R -= 5; }
        }

        auto targetValueFrom = [&](int sx, int sy, const string& kind) -> pair<int, pair<int,int>> {
            int best = -INF; pair<int,int> pos = init.base();
            for (auto *bp : bOrder) {
                const auto &b = *bp;
                if (kind == "F" && b.owner == me) continue;
                int d = dist[idx(sx, sy)][idx(b.x, b.y)]; if (d >= INF) continue;
                int val = buildingValue(b, v);
                if (kind == "W") {
                    int ef = enF[idx(b.x, b.y)]; if (ef) val += 220;
                    if (b.owner == me) val += 20;
                }
                int util = val * 100 / (d + 2);
                if (util > best) { best = util; pos = {b.x, b.y}; }
            }
            return {best, pos};
        };

        auto bestSpawnSite = [&](const string& kind) -> pair<int,int> {
            int best = -INF; pair<int,int> site = init.base();
            for (auto s : spawnSites) {
                auto [u, t] = targetValueFrom(s.first, s.second, kind);
                if (u > best) { best = u; site = s; }
            }
            return site;
        };

        auto emitSpawn = [&](const string& kind, int n, pair<int,int> site) {
            if (n <= 0) return;
            if (site == init.base()) out.push_back(p::spawn(kind, n));
            else out.push_back(p::spawn(kind, n, site.first, site.second));
            int z = idx(site.first, site.second);
            if (kind == "F") myF[z] += n, totalF += n;
            else if (kind == "W") myW[z] += n, totalW += n;
        };

        emitSpawn("F", makeF, bestSpawnSite("F"));
        emitSpawn("W", makeW, bestSpawnSite("W"));

        // (기존 텔레포트 기능 유지)
        vector<int> fixedW(cells, 0);
        if (stations >= 2) {
            vector<const p::Building*> ownSt;
            for (auto *bp : bOrder) if (bp->owner == me && bp->type == "STATION") ownSt.push_back(bp);
            const p::Building *src = nullptr, *dst = nullptr; int sendN = 0, bestUrg = -1;
            for (auto *b : ownSt) {
                int bz = idx(b->x, b->y), threatF = 0, threatW = 0;
                for (int z : cellOrder) {
                    if (enF[z] || enW[z]) {
                        int d = dist[z][bz];
                        if (d <= 4) threatF += enF[z] * (5 - d);
                        if (d <= 3) threatW += enW[z] * (4 - d);
                    }
                }
                int urg = 3 * threatF + threatW; if (urg <= bestUrg) continue;
                const p::Building *candSrc = nullptr; int candN = 0, bestSlack = -1;
                for (auto *a : ownSt) if (a != b) {
                    int az = idx(a->x, a->y), c = myW[az]; if (c <= 1) continue;
                    int avail = c - 1;
                    if (avail > bestSlack) { bestSlack = avail; candSrc = a; candN = min(5, avail); }
                }
                if (candSrc && candN > 0) { bestUrg = urg; src = candSrc; dst = b; sendN = candN; }
            }
            if (!(src && dst && bestUrg > 0)) {
                int bestGain = 1; src = nullptr; dst = nullptr; sendN = 0;
                for (auto *a : ownSt) if (myW[idx(a->x, a->y)] > 1) {
                    for (auto *b : ownSt) if (a != b) {
                        int ga = INF, gb = INF;
                        for (auto &q : v.buildings) if (q.owner != me) {
                            ga = min(ga, dist[idx(a->x, a->y)][idx(q.x, q.y)]);
                            gb = min(gb, dist[idx(b->x, b->y)][idx(q.x, q.y)]);
                        }
                        int gain = ga - gb;
                        if (gain > bestGain) { bestGain = gain; src = a; dst = b; sendN = min(5, myW[idx(a->x, a->y)] - 1); }
                    }
                }
            }
            if (src && dst && sendN > 0) {
                out.push_back(p::tele(src->x, src->y, "W", sendN, dst->x, dst->y));
                myW[idx(src->x, src->y)] -= sendN;
                fixedW[idx(dst->x, dst->y)] += sendN;
            }
        }

        struct FMove { int src, count, target, dest; string dir; bool locked = false; };
        vector<FMove> fplans; vector<int> fAvail = myF; set<int> occupiedTargets;
        for (auto *bp : bOrder) {
            const auto &b = *bp; int z = idx(b.x, b.y);
            if (b.owner != me && fAvail[z] > 0) {
                fplans.push_back({z, 1, z, z, "", true});
                fAvail[z]--;
                occupiedTargets.insert(b.id);
            }
        }
        vector<int> targetIds;
        for (auto *bp : bOrder) if (bp->owner != me && !occupiedTargets.count(bp->id)) targetIds.push_back(bp->id);

        struct PairCand { int util, src, bid; }; vector<PairCand> cand;
        for (int z : cellOrder) if (fAvail[z] > 0) {
            auto [x, y] = xy(z);
            for (int bid : targetIds) {
                auto &b = v.buildings[bid];
                int d = dist[z][idx(b.x, b.y)]; if (d >= INF) continue;
                cand.push_back({buildingValue(b, v) * 100 / (d + 2), z, bid});
            }
        }
        sort(cand.begin(), cand.end(), [&](auto &a, auto &b) {
            if (a.util != b.util) return a.util > b.util;
            return homeKeyCell(a.src) < homeKeyCell(b.src);
        });
        set<int> usedBid;
        for (auto &c : cand) {
            if (fAvail[c.src] <= 0 || usedBid.count(c.bid)) continue;
            auto &b = v.buildings[c.bid]; auto [x, y] = xy(c.src);
            string d = best_step(init, x, y, b.x, b.y);
            int dz = c.src;
            if (!d.empty()) { auto [dx, dy] = p::delta(d); dz = idx(x + dx, y + dy); }
            fplans.push_back({c.src, 1, c.bid, dz, d, false});
            fAvail[c.src]--;
            usedBid.insert(c.bid);
        }
        for (int z : cellOrder) while (fAvail[z] > 0) {
            int best = -INF, bid = -1; auto [x, y] = xy(z);
            for (auto *bp : bOrder) {
                const auto &b = *bp;
                if (b.owner != me) {
                    int d = dist[z][idx(b.x, b.y)]; if (d >= INF) continue;
                    int u = buildingValue(b, v) * 100 / (d + 2);
                    if (u > best) { best = u; bid = b.id; }
                }
            }
            if (bid < 0) { fplans.push_back({z, fAvail[z], z, z, "", false}); fAvail[z] = 0; break; }
            auto &b = v.buildings[bid]; string d = best_step(init, x, y, b.x, b.y);
            int dz = z;
            if (!d.empty()) { auto [dx, dy] = p::delta(d); dz = idx(x + dx, y + dy); }
            fplans.push_back({z, 1, bid, dz, d, false});
            fAvail[z]--;
        }

        vector<int> enemyThreat(cells, 0), enemyFReach(cells, 0);
        const vector<string> dirs = {"U", "D", "L", "R"};
        for (int z : cellOrder) if (pass(init, z % W, z / W)) {
            int x = z % W, y = z / W, th = enW[z], fr = enF[z];
            for (auto &d : dirs) {
                auto [dx, dy] = p::delta(d); int nx = x + dx, ny = y + dy;
                if (pass(init, nx, ny)) { th += enW[idx(nx, ny)]; fr += enF[idx(nx, ny)]; }
            }
            enemyThreat[z] = th; enemyFReach[z] = fr;
        }

        vector<int> wAvail = myW;
        vector<int> reservedStay(cells, 0), plannedArrive = fixedW;
        vector<tuple<int,int,int>> wmoves;
        struct Crit { int pri, z, need; }; vector<Crit> crit;

        for (auto *bp : bOrder) if (bp->owner == me) {
            int z = idx(bp->x, bp->y);
            if (enF[z] > 0) crit.push_back({100000 + buildingValue(*bp, v), z, enemyThreat[z] + 1});
        }

        // 🔥 범용 승리 전략 3: 철통 호위
        // W가 F를 버리고 딴짓 안 하도록 호위 가중치를 균형 있게 맞춤
        for (auto &fp : fplans) {
            int z = fp.dest; bool isB = building_at(v, z % W, z / W) != nullptr;
            if (isB) {
                int minEscort = (enW[z] > 0 || enemyThreat[z] > 0) ? (enemyThreat[z] + 1) : 1;
                auto *b = building_at(v, z % W, z / W);
                if (b && b->owner == opp) minEscort = max(minEscort, 2);
                crit.push_back({90000 + (enemyFReach[z] * 50), z, minEscort});
            } else if (enemyThreat[z] > 0) {
                crit.push_back({80000, z, enemyThreat[z] + 1});
            }
        }

        sort(crit.begin(), crit.end(), [&](auto &a, auto &b) {
            if (a.pri != b.pri) return a.pri > b.pri;
            return homeKeyCell(a.z) < homeKeyCell(b.z);
        });

        vector<int> secured(cells, 0);
        for (auto &c : crit) {
            int z = c.z; if (secured[z]) continue;
            int need = max(0, c.need - plannedArrive[z]);
            auto [x, y] = xy(z);
            int capacity = wAvail[z];
            for (auto &d : dirOrder) {
                auto [dx, dy] = p::delta(d); int sx = x - dx, sy = y - dy;
                if (pass(init, sx, sy)) capacity += wAvail[idx(sx, sy)];
            }
            if (capacity < need) continue;
            int take = min(need, wAvail[z]);
            if (take) { reservedStay[z] += take; wAvail[z] -= take; plannedArrive[z] += take; need -= take; }
            for (auto &d : dirOrder) {
                if (need <= 0) break;
                auto [dx, dy] = p::delta(d); int sx = x - dx, sy = y - dy;
                if (!pass(init, sx, sy)) continue;
                int ss = idx(sx, sy);
                int n = min(need, wAvail[ss]);
                if (n) { wAvail[ss] -= n; wmoves.push_back({ss, z, n}); plannedArrive[z] += n; need -= n; }
            }
            if (need <= 0) secured[z] = 1;
        }

        // 🔥 범용 승리 전략 4: 적폐 척결 (상대 F 제거)
        // 무리해서 적 본진으로 다이빙하진 않되, 적 F가 사정권에 들어오면 확실하게 물어뜯음
        vector<pair<int,int>> warGoals;
        for (int z : cellOrder) if (enF[z] > 0) warGoals.push_back({z, 5000 + 100 * enF[z]});
        for (auto *bp : bOrder) { const auto &b = *bp; if (b.owner == opp) warGoals.push_back({idx(b.x, b.y), 700 + 2 * buildingValue(b, v)}); }
        for (auto *bp : bOrder) { const auto &b = *bp; if (b.owner != me) warGoals.push_back({idx(b.x, b.y), 350 + buildingValue(b, v)}); }
        if (warGoals.empty()) warGoals.push_back({idx(init.bases[opp == "Y" ? 0 : 1].first, init.bases[opp == "Y" ? 0 : 1].second), 200});

        for (int s : cellOrder) if (wAvail[s] > 0) {
            auto [x, y] = xy(s); int best = -INF, tz = -1;
            for (auto [gz, val] : warGoals) {
                int d = dist[s][gz]; if (d >= INF) continue;
                int u = val - 22 * d; if (u > best) { best = u; tz = gz; }
            }
            if (tz < 0) { reservedStay[s] += wAvail[s]; plannedArrive[s] += wAvail[s]; wAvail[s] = 0; continue; }
            int n = wAvail[s]; auto [tx, ty] = xy(tz);
            int bestStepScore = INF, dz = s;
            for (auto &d : dirOrder) {
                auto [dx, dy] = p::delta(d); int nx = x + dx, ny = y + dy;
                if (!pass(init, nx, ny)) continue;
                int q = idx(nx, ny); int dd = dist[q][tz]; if (dd >= INF) continue;
                int hostile = enW[q], friendly = plannedArrive[q];
                int danger = max(0, hostile - (n + friendly));
                int sc = dd * 30 + danger * 85 - min(20, friendly) * 2;
                if (sc < bestStepScore) { bestStepScore = sc; dz = q; }
            }
            if (dz == s) { reservedStay[s] += n; plannedArrive[s] += n; }
            else { wmoves.push_back({s, dz, n}); plannedArrive[dz] += n; }
            wAvail[s] = 0;
        }

        vector<tuple<int,int,int>> fmoves;
        vector<int> finalFStay(cells, 0), finalFArrive = fixedW;
        for (auto &fp : fplans) {
            if (fp.locked || fp.dir.empty()) { finalFStay[fp.src] += fp.count; continue; }
            int s = fp.src, dz = fp.dest; auto [x, y] = xy(s); auto &tb = v.buildings[fp.target];
            auto safe = [&](int z) { return enemyThreat[z] == 0 || plannedArrive[z] > enemyThreat[z]; };
            if (!safe(dz)) {
                int curd = dist[s][idx(tb.x, tb.y)], bestd = INF, bestz = s;
                for (auto &d : dirOrder) {
                    auto [dx, dy] = p::delta(d); int nx = x + dx, ny = y + dy;
                    if (!pass(init, nx, ny)) continue;
                    int z = idx(nx, ny); int dd = dist[z][idx(tb.x, tb.y)];
                    if (dd <= curd && safe(z) && dd < bestd) { bestd = dd; bestz = z; }
                }
                dz = bestz;
            }
            if (dz == s) finalFStay[s] += fp.count;
            else { fmoves.push_back({s, dz, fp.count}); finalFArrive[dz] += fp.count; }
        }

        vector<const p::Building*> pr;
        for (auto *bp : bOrder) {
            int z = idx(bp->x, bp->y);
            if (bp->owner != me && (finalFStay[z] + finalFArrive[z] > 0)) pr.push_back(bp);
        }
        sort(pr.begin(), pr.end(), [&](auto *a, auto *b) {
            int va = buildingValue(*a, v), vb = buildingValue(*b, v);
            if (va != vb) return va > vb;
            return homeKeyXY(a->x, a->y) < homeKeyXY(b->x, b->y);
        });
        if (!pr.empty()) {
            vector<pair<int,int>> coords;
            for (auto *b : pr) coords.push_back({b->x, b->y});
            out.push_back(p::priority(coords));
        }

        auto dirBetween = [&](int s, int d) -> string {
            auto [x, y] = xy(s); auto [a, b] = xy(d);
            if (a == x && b == y - 1) return "U";
            if (a == x && b == y + 1) return "D";
            if (a == x - 1 && b == y) return "L";
            return "R";
        };

        map<pair<int,int>, int> wmerge, fmerge;
        for (auto [s, d, n] : wmoves) if (n > 0) wmerge[{s, d}] += n;
        for (auto [s, d, n] : fmoves) if (n > 0) fmerge[{s, d}] += n;

        for (auto &kv : wmerge) {
            int s = kv.first.first, d = kv.first.second, n = kv.second;
            auto [x, y] = xy(s);
            out.push_back(p::move(x, y, "W", n, dirBetween(s, d)));
        }
        for (auto &kv : fmerge) {
            int s = kv.first.first, d = kv.first.second, n = kv.second;
            auto [x, y] = xy(s);
            out.push_back(p::move(x, y, "F", n, dirBetween(s, d)));
        }

        return out;
    }
};

Planner planner;
}

vector<string> decide(const p::View& view, const p::Init& init) { return planner.decide(view, init); }
int main() { return p::run(decide); }
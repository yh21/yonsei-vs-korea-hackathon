// submission16: adaptive flag economy on top of survival-aware expansion and defense.
// Empirically selected against submission15 after structural experiments and A/B tests.
// C++20, standard library only. Submit the accompanying source ZIP.
//
// Weighted flag routes include local army balance and safe retreat choices.
// Enemy threats include base/hospital production and station teleportation.
// Economy targets prioritize engineering, halls, and useful hospitals.
// Warriors intercept flag raids and coordinate escorts before reserving defense.
// Capture reserves are recomputed from the final planned flag arrivals.
// Late-game targets must be reachable before turn 160; final captures use score.
// All direction/tie choices are normalized to the player's home orientation.

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
#include <cstdio>

using namespace std;

#include <cstdlib>
namespace {
constexpr int INF = 1e9;
#ifdef TUNE
int prm(int i,int d){static int c[64];static bool init[64];if(!init[i]){init[i]=true;char n[8];snprintf(n,8,"P%d",i);const char*e=getenv(n);c[i]=e?atoi(e):d;}return c[i];}
#else
constexpr int prm(int,int d){return d;}
#endif
struct Planner {
    bool ready = false;
    int W = 15, H = 15, N = 225;
    string me, opp;
    vector<vector<int>> dist;
    vector<int> knownScore;
    vector<int> symId;
    vector<char> myDepotClaimed;
    vector<string> dirOrder;
    vector<int> prevWFrom;
    int reserveTurn=-1, reserveOverride=0;
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
        
        prevWFrom.assign(N, -1);
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

    int featureBonus(const p::Building& b, const p::View& v) const {
        int t = v.turn;
        if (b.type == "PLAZA") return 50;
        if (b.type == "DEPOT") {
            bool claimed = b.id < (int)myDepotClaimed.size() && myDepotClaimed[b.id];
            return claimed ? 5 : (t < 30 ? prm(25,110) : (t < 80 ? 60 : 15));
        }
        if (b.type == "HALL") return t < 90 ? prm(22,170) : (t < 130 ? 75 : 15);
        if (b.type == "ENG") {
            int owned = 0; for (auto &q : v.buildings) if(q.owner==me && q.type=="ENG") owned++;
            return t < 100 ? (owned ? 35 : prm(23,240)) : 40;
        }
        if (b.type == "HOSPITAL") {
            int bx = idPos[b.id].first;
            return (t < 100 ? prm(24,145) : 30) + ((bx >= 5 && bx <= 9) ? 15 : 0);
        }
        if (b.type == "LIBRARY") return t < 100 ? 35 : 10;
        if (b.type == "STATION") return t < 110 ? 20 : 10;
        return 0;
    }

    int buildingValue(const p::Building& b, const p::View& v) const {
        if(v.turn>=140) return expectedScore(b)*(b.owner==opp?8:(b.owner==me?6:7));
        int val = expectedScore(b) * 4 + featureBonus(b, v);
        if (b.owner == opp) { val = val * 15 / 10 + 20; if (b.type == "ENG" || b.type == "HALL") val += prm(38,100); }
        else if (b.owner == "N") val += 15;
        else val = val / 5;
        if (b.x >= 5 && b.x <= 9) val += 15;
        if (v.turn > 120 && b.owner == opp) val += 70;
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
        if(reserveTurn!=v.turn){reserveTurn=v.turn;reserveOverride=0;}
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

        vector<int> enemyThreat(cells, 0), enemyFReach(cells, 0), enemySpawn(cells, 0);
        const vector<string> dirs = {"U", "D", "L", "R"};
        vector<int> enemySites;
        auto eb=init.bases[me=="Y"?1:0]; enemySites.push_back(idx(eb.first,eb.second));
        bool enemyEng=false;
        for(auto &b:v.buildings) if(b.owner==opp){
            if(b.type=="ENG") enemyEng=true;
            if(b.type=="HOSPITAL") enemySites.push_back(idx(b.x,b.y));
        }
        for(int z:cellOrder) if(pass(init,z%W,z/W)){
            for(int q:cellOrder) if(dist[z][q]<=1){enemyThreat[z]+=enW[q];enemyFReach[z]+=enF[q];}
            for(int q:enemySites) if(dist[z][q]<=1) enemySpawn[z]=v.opp_resource/(enemyEng?2:3);
            enemyThreat[z]+=enemySpawn[z];
        }

        vector<int> enemyStations;
        for(auto &b:v.buildings) if(b.owner==opp && b.type=="STATION") enemyStations.push_back(idx(b.x,b.y));
        if(enemyStations.size()>=2)for(int z:enemyStations){
            int extraW=0,extraF=0;
            for(int q:enemyStations)if(q!=z && dist[q][z]>1){extraW+=enW[q];extraF+=enF[q];}
            enemyThreat[z]+=min(5,extraW);enemyFReach[z]+=min(5,extraF);
        }

        int eng = 0, halls = 0, stations = 0, libs = 0;
        vector<pair<int,int>> spawnSites; spawnSites.push_back(init.base());
        for (auto &b : v.buildings) {
            if (b.owner == me) {
                if (b.type == "ENG") eng++;
                if (b.type == "HALL") halls++;
                if (b.type == "STATION") stations++;
                if (b.type == "LIBRARY") libs++;
                if (b.type == "HOSPITAL") spawnSites.push_back({b.x, b.y});
            }
        }
        sort(spawnSites.begin(), spawnSites.end(), [&](auto a, auto b) { return homeKeyXY(a.first, a.second) < homeKeyXY(b.first, b.second); });
        int wcost = max(2, 3 - eng);
        int nonOwned = 0; for (auto &b : v.buildings) if (b.owner != me) nonOwned++;

        
        int R = v.my_resource;
        int capture_cost = 0;
        for (auto *bp : bOrder) {
            if (bp->owner != me && myF[idx(bp->x, bp->y)] > 0) {
                
                int cost = (bp->type == "PLAZA" ? 4 : 2) - (libs > 0 ? 1 : 0);
                capture_cost += max(1, cost);
            }
        }
        
        int expected_income = 10 + halls * 2; 
        int needed_reserve = max(reserveOverride, max(0, capture_cost - expected_income)); 
        int min_spend = max(0, R + expected_income - 40); 
        
        int budget = R - needed_reserve;
        if (budget < min_spend) budget = min_spend; 
        budget = clamp(budget, 0, R);

        int desiredF = (v.turn <= 15 ? prm(0,7) : (nonOwned >= 6 ? prm(1,6) : (nonOwned >= 3 ? prm(2,5) : prm(3,4))));
        if (v.turn < 120 && totalW + 6 < oppWTotal) desiredF = max(3, desiredF - 1);
        if (v.turn < 120 && totalW > oppWTotal + 18 && nonOwned >= 3) desiredF = min(8, desiredF + 1);
        if (v.turn > 145 && nonOwned > 0 && totalW > oppWTotal) desiredF = max(desiredF, 5);

        int makeF = 0, makeW = 0;
        if (v.turn == 1) {
            makeF = min(2, budget / 5); budget -= 5 * makeF;
            if (budget >= wcost) { makeW = budget / wcost; budget -= makeW * wcost; }
        } else {
            int needF = max(0, desiredF - totalF);
            makeF = min({needF, (v.turn < 25 ? 2 : 1), budget / 5});
            budget -= 5 * makeF;
            makeW = budget / wcost; 
            budget -= makeW * wcost;
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
                int z=idx(s.first,s.second);
                if(kind=="F") u-=max(0,enemyThreat[z]-myW[z])*3000;
                else {
                    for(int q:cellOrder) if(myF[q] && dist[z][q]<=2)
                        u+=min(20,enemyThreat[q])*160/(dist[z][q]+1);
                }
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

        vector<int> friendlyReach(cells), regionDiff(cells);
        for(int z:cellOrder) {
            for(int q:cellOrder){
                if(dist[z][q]<=1) friendlyReach[z]+=myW[q];
                if(dist[z][q]<=3) regionDiff[z]+=enW[q]-myW[q];
            }
        }
        vector<vector<int>> routeCost(idPos.size(),vector<int>(cells,INF));
        vector<int> risk(cells);
        for(int z:cellOrder) risk[z]=min(prm(4,48), max(0,enemyThreat[z]-friendlyReach[z])*prm(5,13))
                                    +min(prm(6,18),max(0,regionDiff[z])*prm(7,3));
        for(auto *bp:bOrder){
            int target=idx(bp->x,bp->y);auto &dd=routeCost[bp->id];
            priority_queue<pair<int,int>,vector<pair<int,int>>,greater<pair<int,int>>> pq;
            dd[target]=0;pq.push({0,target});
            while(!pq.empty()){
                auto [d,z]=pq.top();pq.pop();if(d!=dd[z])continue;
                for(auto &dir:dirOrder){auto [dx,dy]=p::delta(dir);int x=z%W+dx,y=z/W+dy;
                    if(!pass(init,x,y))continue;int q=idx(x,y),nd=d+10+risk[z];
                    if(nd<dd[q]){dd[q]=nd;pq.push({nd,q});}
                }
            }
        }
        auto flagStep=[&](int z,int bid){
            string answer="";int best=routeCost[bid][z];
            for(auto &dir:dirOrder){auto [dx,dy]=p::delta(dir);int x=z%W+dx,y=z/W+dy;
                if(!pass(init,x,y))continue;int q=idx(x,y),score=routeCost[bid][q]+risk[q];
                if(score<best){best=score;answer=dir;}
            }
            return answer;
        };

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
                int d = dist[z][idx(b.x, b.y)]; if (d >= INF || (v.turn>=140 && d>161-v.turn)) continue;
                cand.push_back({buildingValue(b, v) * 1000 / (routeCost[bid][z] + prm(8,25)), z, bid});
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
            string d = flagStep(c.src, c.bid);
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
                    int d = dist[z][idx(b.x, b.y)]; if (d >= INF || (v.turn>=140 && d>161-v.turn)) continue;
                    int u = buildingValue(b, v) * 1000 / (routeCost[b.id][z] + prm(8,25));
                    if (u > best) { best = u; bid = b.id; }
                }
            }
            if (bid < 0) { fplans.push_back({z, fAvail[z], -1, z, "", false}); fAvail[z] = 0; break; }
            auto &b = v.buildings[bid]; string d = flagStep(z, bid);
            int dz = z;
            if (!d.empty()) { auto [dx, dy] = p::delta(d); dz = idx(x + dx, y + dy); }
            fplans.push_back({z, 1, bid, dz, d, false});
            fAvail[z]--;
        }

        vector<int> wAvail = myW;
        vector<int> reservedStay(cells, 0), plannedArrive = fixedW;
        vector<tuple<int,int,int>> wmoves;
        struct Crit { int pri, z, need; }; vector<Crit> crit;

        for (auto *bp : bOrder) if (bp->owner == me) {
            int z = idx(bp->x, bp->y);
            if (enemyFReach[z] > 0) {
                bool assault=myF[z]>0;
                for(auto &fp:fplans)if(fp.src==z){
                    auto dst=building_at(v,fp.dest%W,fp.dest/W);
                    if(fp.dest==z || !dst || dst->owner==me)assault=false;
                }
                crit.push_back({(assault?70000:100000)+buildingValue(*bp,v),z,enemyThreat[z]+1});
            }
        }

        for (auto &fp : fplans) {
            int z = fp.dest; bool isB = building_at(v, z % W, z / W) != nullptr;
            if (isB) {
                int minEscort = (enW[z] > 0 || enemyThreat[z] > 0) ? (enemyThreat[z] + 1) : 1;
                auto *b = building_at(v, z % W, z / W);
                if (b && b->owner == opp) minEscort = max(minEscort, 2);
                crit.push_back({90000 + (enemyFReach[z] * 20), z, minEscort});
            } else if (enemyThreat[z] > 0) {
                crit.push_back({80000, z, enemyThreat[z] + 1});
            }
        }

        if (totalW >= prm(20,22)) {
            vector<const p::Building*> ownVal;
            for (auto *bp : bOrder) if (bp->owner == me) ownVal.push_back(bp);
            sort(ownVal.begin(), ownVal.end(), [&](auto *a, auto *b) { return buildingValue(*a, v) > buildingValue(*b, v); });
            int cap = min((int)ownVal.size(), totalW >= prm(21,65) ? prm(36,5) : prm(35,3));
            for (int i = 0; i < cap; i++) {
                int z = idx(ownVal[i]->x, ownVal[i]->y);
                crit.push_back({30000 + buildingValue(*ownVal[i], v), z, prm(37,1)});
            }
        }

        if (prm(30,2) > 0 && totalW >= prm(31,10)) {
            for (auto *bp : bOrder) if (bp->owner == me && (bp->type == "ENG" || bp->type == "HALL")) {
                int z = idx(bp->x, bp->y);
                crit.push_back({prm(32,35000) + buildingValue(*bp, v), z, prm(30,2)});
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

        vector<int> goalValue(cells, 0), goalNeed(cells, 0), goalAssigned = plannedArrive;
        auto addGoal = [&](int z, int value, int need) {
            goalValue[z] = max(goalValue[z], value);
            goalNeed[z] = max(goalNeed[z], need);
        };
        vector<int> flagETA(cells, INF);
        for (int z : cellOrder) if (myF[z])
            for (int q : cellOrder) flagETA[q] = min(flagETA[q], dist[z][q]);
        for (int z : cellOrder) {
            
            if (enF[z]) addGoal(z, prm(9,660) + 50 * enF[z], enemyThreat[z] + 3);
            if (enW[z]) addGoal(z, prm(10,150) + min(180, 2 * enW[z]), enemyThreat[z] + prm(15,3));
        }
        for (auto *bp : bOrder) {
            const auto &b = *bp;
            int z = idx(b.x,b.y);
            int efDist=INF,ewLocal=0;
            for(int q:cellOrder){if(enF[q])efDist=min(efDist,dist[z][q]);if(dist[z][q]<=2)ewLocal+=enW[q];}
            if(b.owner==me && efDist<=3) addGoal(z,prm(11,960)+buildingValue(b,v)-efDist*prm(12,150),max(2,ewLocal+2));
            if (b.owner != me) {
                int value = b.owner == opp ? prm(13,500) + 2 * buildingValue(b,v) : prm(14,350) + buildingValue(b,v);
                if (flagETA[z] > 3 && !enF[z]) value = value * 4 / (flagETA[z] + 1);
                addGoal(z, value, enemyThreat[z] + prm(16,2)); 
            }
        }
        vector<int> sources;
        for (int s : cellOrder) if (wAvail[s] > 0) sources.push_back(s);
        sort(sources.begin(), sources.end(), [&](int a, int b) {
            if (wAvail[a] != wAvail[b]) return wAvail[a] > wAvail[b];
            return homeKeyCell(a) < homeKeyCell(b);
        });
        for (int s : sources) {
            int left = wAvail[s];
            auto [x,y] = xy(s);
            while (left > 0) {
                int tz=-1, best=-INF;
                for (int z : cellOrder) if (goalValue[z]>0 && goalAssigned[z]<goalNeed[z]) {
                    int d=dist[s][z]; if(d>=INF || (v.turn>=145 && d>161-v.turn)) continue;
                    int utility=goalValue[z]*100/(d+prm(17,4));
                    if(utility>best) { best=utility; tz=z; }
                }
                bool overflow=false;
                if(tz<0) {
                    overflow=true;
                    for(int z : cellOrder) if(enW[z] || enF[z]) {
                        int d=dist[s][z]; if(d>=INF || (v.turn>=145 && d>161-v.turn))continue;
                        int utility=(enF[z]?1100:600)+min(300,3*enW[z])-25*d;
                        if(utility>best) {best=utility;tz=z;}
                    }
                }
                if(tz<0) {
                    reservedStay[s]+=left; plannedArrive[s]+=left; break;
                }
                
                int n=overflow?left:min(left,goalNeed[tz]-goalAssigned[tz]);
                int dz=s;
                if(s!=tz) {
                    int bestStep=INF;
                    for(const auto &d : dirOrder) {
                        auto [dx,dy]=p::delta(d); int nx=x+dx,ny=y+dy;
                        if(!pass(init,nx,ny))continue;
                        int q=idx(nx,ny),dd=dist[q][tz];
                        int danger=max(0,enW[q]+enemySpawn[q]-(n+plannedArrive[q]));
                        int score=dd*prm(18,30)+danger*prm(19,85)-min(20,plannedArrive[q])*2+(prevWFrom[s]==q?9:0);
                        if(score<bestStep) {bestStep=score;dz=q;}
                    }
                }
                goalAssigned[tz]+=n;
                if(dz==s) reservedStay[s]+=n;
                else wmoves.push_back({s,dz,n});
                plannedArrive[dz]+=n;
                left-=n;
            }
            wAvail[s]=0;
        }

        vector<tuple<int,int,int>> fmoves;
        vector<int> finalFStay(cells, 0), finalFArrive(cells, 0);
        for(auto &fp:fplans){
            int s=fp.src,bid=fp.target;
            if(fp.locked){auto b=building_at(v,s%W,s/W);bid=b?b->id:-1;}
            if(bid<0 || bid>=(int)routeCost.size())bid=-1;
            vector<int> choices={s};
            for(auto &dir:dirOrder){auto [dx,dy]=p::delta(dir);int x=s%W+dx,y=s/W+dy;
                if(pass(init,x,y))choices.push_back(idx(x,y));}
            int best=INF,dest=s;
            for(int q:choices){
                int deficit=max(0,enemyThreat[q]-plannedArrive[q]);
                int score=deficit*100000;
                if(deficit && building_at(v,q%W,q/W) && building_at(v,q%W,q/W)->owner==me)score+=50000;
                score+=bid>=0?routeCost[bid][q]:0;
                score+=(q==s?7:0);
                if(fp.locked && q==s)score-=40;
                if(q==fp.dest)score-=4;
                score-=min(10,plannedArrive[q]);
                if(v.turn==160){
                    auto qb=building_at(v,q%W,q/W);
                    if(qb && qb->owner!=me && (enemyFReach[q]==0 || plannedArrive[q]>enemyThreat[q]))
                        score-=1000*expectedScore(*qb);
                    if(q==s && qb && qb->owner==me && enemyFReach[q] && plannedArrive[q]<=enemyThreat[q])
                        score-=1000*expectedScore(*qb);
                }
                if(score<best){best=score;dest=q;}
            }
            if(dest==s)finalFStay[s]+=fp.count;
            else{fmoves.push_back({s,dest,fp.count});finalFArrive[dest]+=fp.count;}
        }

        
        int actualCaptureCost=0;
        for(auto *bp:bOrder){int z=idx(bp->x,bp->y);
            if(bp->owner!=me && finalFStay[z]+finalFArrive[z]>0)
                actualCaptureCost+=max(1,(bp->type=="PLAZA"?4:2)-(libs>0));
        }
        int requiredReserve=max(0,actualCaptureCost-expected_income);
        if(requiredReserve>needed_reserve){reserveOverride=requiredReserve;return decide(v,init);}

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

        fill(prevWFrom.begin(),prevWFrom.end(),-1);
        vector<int> prevWCount(cells);
        for (auto &kv : wmerge) {
            int s = kv.first.first, d = kv.first.second, n = kv.second;
            auto [x, y] = xy(s);
            out.push_back(p::move(x, y, "W", n, dirBetween(s, d)));
            if(n>prevWCount[d] || (n==prevWCount[d] && homeKeyCell(s)<homeKeyCell(prevWFrom[d]))){prevWFrom[d]=s;prevWCount[d]=n;}
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
int main() { ios::sync_with_stdio(false); cin.tie(nullptr); return p::run(decide); }
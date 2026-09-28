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
    bool ready=false;
    int W=15,H=15,N=225;
    string me,opp;
    vector<vector<int>> dist; // [cell][cell]
    vector<array<int,4>> nxt;
    vector<int> knownScore;
    vector<int> symId;
    vector<char> myDepotClaimed;
    vector<string> dirOrder;
    vector<pair<int,int>> idPos;
    vector<string> idType;
    int lastTurn=0;

    int idx(int x,int y) const { return y*W+x; }
    pair<int,int> xy(int v) const { return {v%W,v/W}; }
    bool pass(const p::Init& init,int x,int y) const { return init.passable(x,y); }
    int homeKeyCell(int z) const {
        auto [x,y]=xy(z);
        if(me=="K"){ x=W-1-x; y=H-1-y; }
        return y*W+x;
    }
    int homeKeyXY(int x,int y) const {
        if(me=="K"){ x=W-1-x; y=H-1-y; }
        return y*W+x;
    }

    void init_once(const p::Init& init) {
        if(ready) return;
        ready=true; W=init.width; H=init.height; N=W*H; me=init.team; opp=init.opp;
        dirOrder = (init.base().first*2 > W-1) ? vector<string>{"D","U","R","L"} : vector<string>{"U","D","L","R"};
        idPos.resize(init.buildings.size()); idType.resize(init.buildings.size());
        int maxid=0; for(auto &b:init.buildings) maxid=max(maxid,b.id);
        knownScore.assign(maxid+1,-1); symId.assign(maxid+1,-1); myDepotClaimed.assign(maxid+1,0);
        for(auto &b:init.buildings){ if(b.id >= (int)idPos.size()){ idPos.resize(b.id+1); idType.resize(b.id+1); } idPos[b.id]={b.x,b.y}; idType[b.id]=b.type; if(b.type=="PLAZA") knownScore[b.id]=3; }
        map<pair<int,int>,int> at;
        for(auto &b:init.buildings) at[{b.x,b.y}]=b.id;
        for(auto &b:init.buildings){ auto it=at.find({W-1-b.x,H-1-b.y}); if(it!=at.end() && b.id<(int)symId.size()) symId[b.id]=it->second; }
        // all-pairs BFS on tiny grid
        dist.assign(N, vector<int>(N, INF));
        const int dx[4]={0,0,-1,1}, dy[4]={-1,1,0,0};
        for(int s=0;s<N;s++){
            auto [sx,sy]=xy(s); if(!pass(init,sx,sy)) continue;
            queue<int> q; dist[s][s]=0; q.push(s);
            while(!q.empty()){
                int v=q.front(); q.pop(); auto [x,y]=xy(v);
                for(int k=0;k<4;k++){ int nx=x+dx[k],ny=y+dy[k]; if(!pass(init,nx,ny)) continue; int u=idx(nx,ny); if(dist[s][u]>dist[s][v]+1){dist[s][u]=dist[s][v]+1;q.push(u);} }
            }
        }
    }

    void update_memory(const p::View& v){
        lastTurn=v.turn;
        for(auto &b:v.buildings){
            if(b.id >= (int)knownScore.size()) continue;
            if(b.score>=0){ knownScore[b.id]=b.score; int s=symId[b.id]; if(s>=0) knownScore[s]=b.score; }
            if(b.type=="DEPOT" && b.owner==me) myDepotClaimed[b.id]=1;
        }
    }

    int expectedScore(const p::Building& b) const {
        if(b.id<(int)knownScore.size() && knownScore[b.id]>=0) return knownScore[b.id]*10;
        if(b.type=="PLAZA") return 30;
        // integer tenths: side 1.5, center 3.0
        return (b.x>=5 && b.x<=9) ? 30 : 15;
    }

    int featureBonus(const p::Building& b, const p::View& v) const {
        int t=v.turn, rem=161-t;
        if(b.type=="PLAZA") return 55;
        if(b.type=="DEPOT") {
            bool claimed = b.id<(int)myDepotClaimed.size() && myDepotClaimed[b.id];
            return claimed ? 8 : (t<60 ? 100 : t<120 ? 60 : 25);
        }
        if(b.type=="HALL") return t<80 ? 70 : t<130 ? 35 : 10;
        if(b.type=="ENG") return t<90 ? 60 : t<140 ? 30 : 8;
        if(b.type=="HOSPITAL") {
            int bx=idPos[b.id].first;
            int central = (bx>=5 && bx<=9)?15:0;
            return (t<100 ? 30 : 12)+central;
        }
        if(b.type=="LIBRARY") return t<100 ? 38 : 12;
        if(b.type=="STATION") return t<110 ? 18 : 10;
        if(b.type=="WATCH") {
            int unknown=0; for(int s:knownScore) unknown += (s<0);
            return unknown ? 22 : 6;
        }
        return 0;
    }

    int buildingValue(const p::Building& b, const p::View& v) const {
        int val = expectedScore(b)*7/2 + featureBonus(b,v); // score dominates
        if(b.owner==opp) val = val*17/10 + 35; // denial + swing
        else if(b.owner=="N") val += 8;
        else val = val/5;
        // Central real estate and late-game swing.
        if(b.x>=5 && b.x<=9) val += 12;
        if(v.turn>130 && b.owner==opp) val += 50;
        return val;
    }

    string best_step(const p::Init& init,int x,int y,int tx,int ty) const {
        if(x==tx && y==ty) return "";
        int best=INF; string ans="";
        for(const string &d:dirOrder){ auto [dx,dy]=p::delta(d); int nx=x+dx,ny=y+dy; if(!pass(init,nx,ny)) continue; int dd=dist[idx(nx,ny)][idx(tx,ty)]; if(dd<best){best=dd;ans=d;} }
        return ans;
    }

    pair<string,string> best_step2(const p::Init& init,int x,int y,int tx,int ty) const {
        int best=INF; pair<string,string> ans={"",""};
        for(const string &a:dirOrder){ auto [dx1,dy1]=p::delta(a); int mx=x+dx1,my=y+dy1; if(!pass(init,mx,my)) continue;
            for(const string &b:dirOrder){ auto [dx2,dy2]=p::delta(b); int nx=mx+dx2,ny=my+dy2; if(!pass(init,nx,ny)) continue; int dd=dist[idx(nx,ny)][idx(tx,ty)]; if(dd<best){best=dd;ans={a,b};} }
        }
        return ans;
    }

    const p::Building* building_at(const p::View& v,int x,int y) const {
        for(auto &b:v.buildings) if(b.x==x && b.y==y) return &b; return nullptr;
    }

    int captureCost(const p::Building& b, bool lib) const {
        int c=(b.type=="PLAZA"?4:2); if(lib) c=max(1,c-1); return c;
    }

    vector<string> decide(const p::View& v,const p::Init& init){
        init_once(init); update_memory(v);
        vector<string> out;
        int cells=N;
        // grids: counts before our planned commands; spawned units will be added.
        vector<int> myF(cells),myW(cells),myS(cells),enF(cells),enW(cells),enS(cells);
        vector<int> cellOrder(cells); iota(cellOrder.begin(),cellOrder.end(),0);
        sort(cellOrder.begin(),cellOrder.end(),[&](int a,int b){ return homeKeyCell(a)<homeKeyCell(b); });
        vector<const p::Building*> bOrder; bOrder.reserve(v.buildings.size());
        for(auto &b:v.buildings) bOrder.push_back(&b);
        sort(bOrder.begin(),bOrder.end(),[&](auto a,auto b){ return homeKeyXY(a->x,a->y)<homeKeyXY(b->x,b->y); });
        int totalF=0,totalW=0,totalS=0,oppWTotal=0,oppFTotal=0;
        for(auto &u:v.units){ int z=idx(u.x,u.y); if(u.team==me){ if(u.kind=="F") myF[z]+=u.count,totalF+=u.count; else if(u.kind=="W") myW[z]+=u.count,totalW+=u.count; else myS[z]+=u.count,totalS+=u.count; }
            else { if(u.kind=="F") enF[z]+=u.count,oppFTotal+=u.count; else if(u.kind=="W") enW[z]+=u.count,oppWTotal+=u.count; else enS[z]+=u.count; } }

        bool haveLib=false; int eng=0,halls=0,stations=0;
        vector<pair<int,int>> spawnSites; spawnSites.push_back(init.base());
        for(auto &b:v.buildings){ if(b.owner==me){ if(b.type=="LIBRARY")haveLib=true; if(b.type=="ENG")eng++; if(b.type=="HALL")halls++; if(b.type=="STATION")stations++; if(b.type=="HOSPITAL") spawnSites.push_back({b.x,b.y}); } }
        sort(spawnSites.begin(),spawnSites.end(),[&](auto a,auto b){ return homeKeyXY(a.first,a.second)<homeKeyXY(b.first,b.second); });
        int income=10+2*halls;
        int wcost=max(2,3-eng);
        int unknownPairs=0; for(auto &b:v.buildings) if(b.id<(int)knownScore.size() && knownScore[b.id]<0) unknownPairs++;
        int nonOwned=0; for(auto &b:v.buildings) if(b.owner!=me) nonOwned++;

        // Production: current resource is usable now; capture occurs after fresh income.
        int R=v.my_resource;
        int desiredF = (v.turn<=12?10:(nonOwned>=8?10:8));
        if(nonOwned<=5 && v.turn>25 && oppFTotal<=5) desiredF=6;
        if(v.turn>105 && nonOwned>0 && oppFTotal<=5) desiredF=min(desiredF,6);
        int desiredS = (unknownPairs>0 && v.turn<35) ? 0 : 0;
        int makeF=0,makeS=0,makeW=0;
        bool warriorEmergency = false;
        if(v.turn==1){ makeF=min(2,R/5); R-=5*makeF; }
        else {
            int needF=warriorEmergency?0:max(0,desiredF-totalF);
            makeF=min(needF, 2);
            makeF=min(makeF,R/5); R-=5*makeF;
            if(!warriorEmergency && totalS<desiredS && R>=2){ makeS=1; R-=2; }
            // Prefer warriors; leave at most 1 unspent if arithmetic forces it.
            makeW=R/wcost; R-=makeW*wcost;
            // If we have 5 left after an awkward discount arithmetic and need flags, use it.
            if(R>=5 && totalF+makeF<desiredF){ makeF++; R-=5; }
            // Do not overproduce scouts: after information is known, warriors are much more valuable.
            // A small remainder may be carried into the next turn.
        }
        // First turn leftover is zero; from turn 2 fill with W/S.
        if(v.turn==1){ if(R>=wcost){makeW=R/wcost;R-=makeW*wcost;} }
        // Keep the normal tactical production mix through the final turns.


        auto targetValueFrom=[&](int sx,int sy,const string&kind)->pair<int,pair<int,int>>{
            int best=-INF; pair<int,int> pos=init.base();
            for(auto *bp:bOrder){ const auto &b=*bp;
                if(kind=="F" && b.owner==me) continue;
                int d=dist[idx(sx,sy)][idx(b.x,b.y)]; if(d>=INF) continue;
                int val=buildingValue(b,v);
                if(kind=="W"){
                    // warriors care especially about enemy flags and contested structures
                    int ef=enF[idx(b.x,b.y)]; if(ef) val+=180;
                    if(b.owner==me) val+=20;
                }
                if(kind=="S"){
                    bool unk=(b.id<(int)knownScore.size()&&knownScore[b.id]<0);
                    if(!unk) val/=5; else val+=120;
                }
                int util=val*100/(d+2);
                if(util>best){best=util;pos={b.x,b.y};}
            }
            return {best,pos};
        };
        auto bestSpawnSite=[&](const string&kind)->pair<int,int>{
            int best=-INF; pair<int,int> site=init.base();
            for(auto s:spawnSites){ auto [u,t]=targetValueFrom(s.first,s.second,kind); if(u>best){best=u;site=s;} }
            return site;
        };
        auto emitSpawn=[&](const string&kind,int n,pair<int,int> site){ if(n<=0)return; if(site==init.base()) out.push_back(p::spawn(kind,n)); else out.push_back(p::spawn(kind,n,site.first,site.second)); int z=idx(site.first,site.second); if(kind=="F")myF[z]+=n,totalF+=n; else if(kind=="W")myW[z]+=n,totalW+=n; else myS[z]+=n,totalS+=n; };
        // Spawn F/S first only for readability; engine uses command order within spawn phase for budget, counts already fit.
        auto siteF=bestSpawnSite("F"), siteS=bestSpawnSite("S"), siteW=bestSpawnSite("W");
        emitSpawn("F",makeF,siteF); emitSpawn("S",makeS,siteS); emitSpawn("W",makeW,siteW);

        // Station reinforcement: first answer immediate pressure around a station, then fall back
        // to the old distance-saving offensive teleport. Keep a small source garrison when needed.
        vector<int> fixedW(cells,0), fixedF(cells,0), fixedS(cells,0); // arrivals that cannot move again
        if(stations>=2){
            vector<const p::Building*> ownSt;
            for(auto *bp:bOrder) if(bp->owner==me && bp->type=="STATION") ownSt.push_back(bp);
            const p::Building *src=nullptr,*dst=nullptr; int sendN=0;
            int bestUrg=-1;
            for(auto *b:ownSt){
                int bz=idx(b->x,b->y), flagReach=enF[bz], warReach=enW[bz];
                for(auto &d:vector<string>{"U","D","L","R"}){
                    auto [dx,dy]=p::delta(d); int nx=b->x+dx,ny=b->y+dy;
                    if(pass(init,nx,ny)){ int q=idx(nx,ny); flagReach+=enF[q]; warReach+=enW[q]; }
                }
                // Warriors alone cannot capture a station.  Emergency TELE is reserved for a
                // flag that can actually enter this turn, and only fills the strict-majority gap.
                if(flagReach<=0) continue;
                int need=max(1,warReach+1), deficit=max(0,need-myW[bz]);
                if(deficit<=0) continue;
                int urg=100*flagReach+10*warReach+buildingValue(*b,v);
                if(urg<=bestUrg) continue;
                const p::Building *candSrc=nullptr; int candN=0,bestSlack=-1;
                for(auto *a:ownSt) if(a!=b){
                    int az=idx(a->x,a->y), c=myW[az]; if(c<=0) continue;
                    int localFlag=enF[az],localWar=enW[az];
                    for(auto &d:vector<string>{"U","D","L","R"}){
                        auto [dx,dy]=p::delta(d);int nx=a->x+dx,ny=a->y+dy;
                        if(pass(init,nx,ny)){int q=idx(nx,ny);localFlag+=enF[q];localWar+=enW[q];}
                    }
                    int keep=localFlag?max(1,localWar+1):1;
                    int avail=max(0,c-keep), n=min({5,avail,deficit});
                    if(n>0 && avail>bestSlack){bestSlack=avail;candSrc=a;candN=n;}
                }
                if(candSrc&&candN>0){bestUrg=urg;src=candSrc;dst=b;sendN=candN;}
            }
            if(!(src&&dst&&bestUrg>0)){
                int bestGain=1; src=nullptr; dst=nullptr; sendN=0;
                for(auto *a:ownSt) if(myW[idx(a->x,a->y)]>1){
                    for(auto *b:ownSt) if(a!=b){
                        int ga=INF,gb=INF;
                        for(auto &q:v.buildings) if(q.owner!=me){ ga=min(ga,dist[idx(a->x,a->y)][idx(q.x,q.y)]); gb=min(gb,dist[idx(b->x,b->y)][idx(q.x,q.y)]); }
                        int gain=ga-gb; if(gain>bestGain){bestGain=gain;src=a;dst=b;sendN=min(5,myW[idx(a->x,a->y)]-1);}
                    }
                }
            }
            if(src&&dst&&sendN>0){ out.push_back(p::tele(src->x,src->y,"W",sendN,dst->x,dst->y)); myW[idx(src->x,src->y)]-=sendN; fixedW[idx(dst->x,dst->y)]+=sendN; }
        }

        // Tentative flag plans: each non-owned building gets at most one active flag assignment.
        struct FMove {int src, count, target, dest; string dir; bool locked=false;};
        vector<FMove> fplans;
        vector<int> fAvail=myF;
        set<int> occupiedTargets;
        // Keep exactly one flag on every non-owned building it currently occupies so capture can progress.
        for(auto *bp:bOrder){ const auto &b=*bp; int z=idx(b.x,b.y); if(b.owner!=me && fAvail[z]>0){ fplans.push_back({z,1,z,z,"",true}); fAvail[z]--; occupiedTargets.insert(b.id); } }
        // Build one slot per remaining strategic target.
        vector<int> targetIds;
        for(auto *bp:bOrder) if(bp->owner!=me && !occupiedTargets.count(bp->id)) targetIds.push_back(bp->id);
        // Assign individual flags greedily by best value/distance; cap work to a sane number.
        struct PairCand{int util,src,bid;}; vector<PairCand> cand;
        for(int z:cellOrder) if(fAvail[z]>0){ auto [x,y]=xy(z); for(int bid:targetIds){ auto &b=v.buildings[bid]; int d=dist[z][idx(b.x,b.y)]; if(d>=INF)continue; int val=buildingValue(b,v); int util=val*100/(d+2); cand.push_back({util,z,bid}); } }
        sort(cand.begin(),cand.end(),[&](auto&a,auto&b){ if(a.util!=b.util)return a.util>b.util; int as=homeKeyCell(a.src),bs=homeKeyCell(b.src); if(as!=bs)return as<bs; auto &ab=v.buildings[a.bid]; auto &bb=v.buildings[b.bid]; return homeKeyXY(ab.x,ab.y)<homeKeyXY(bb.x,bb.y);});
        set<int> usedBid;
        for(auto &c:cand){ if(fAvail[c.src]<=0||usedBid.count(c.bid))continue; auto &b=v.buildings[c.bid]; auto [x,y]=xy(c.src); string d=best_step(init,x,y,b.x,b.y); int dz=c.src; if(!d.empty()){auto [dx,dy]=p::delta(d);dz=idx(x+dx,y+dy);} fplans.push_back({c.src,1,c.bid,dz,d,false}); fAvail[c.src]--; usedBid.insert(c.bid); }
        // Extra flags: send toward highest-value non-owned target; if everything is ours, keep them away from pointless combat.
        for(int z:cellOrder) while(fAvail[z]>0){
            int best=-INF,bid=-1; auto [x,y]=xy(z);
            for(auto *bp:bOrder){ const auto &b=*bp; if(b.owner!=me){ int d=dist[z][idx(b.x,b.y)]; if(d>=INF)continue; int u=buildingValue(b,v)*100/(d+2); if(u>best){best=u;bid=b.id;} }}
            if(bid<0){ fplans.push_back({z,fAvail[z],z,z,"",false}); fAvail[z]=0; break; }
            auto &b=v.buildings[bid]; string d=best_step(init,x,y,b.x,b.y); int dz=z; if(!d.empty()){auto [dx,dy]=p::delta(d);dz=idx(x+dx,y+dy);} fplans.push_back({z,1,bid,dz,d,false}); fAvail[z]--; }

        // Enemy warrior worst-case one-step concentration per cell.
        vector<int> enemyThreat(cells,0);
        const vector<string> dirs={"U","D","L","R"};
        for(int z:cellOrder) if(pass(init,z%W,z/W)){
            int x=z%W,y=z/W,th=enW[z];
            for(auto &d:dirs){auto [dx,dy]=p::delta(d);int nx=x+dx,ny=y+dy;if(pass(init,nx,ny))th+=enW[idx(nx,ny)];}
            enemyThreat[z]=th;
        }

        // Enemy flags that can reach each cell in one move.  A single surviving W on a
        // contested building clears all arriving enemy flags, so these are prime escort spots.
        vector<int> enemyFReach(cells,0);
        for(int z:cellOrder) if(pass(init,z%W,z/W)){
            int x=z%W,y=z/W,th=enF[z];
            for(auto &d:dirs){auto [dx,dy]=p::delta(d);int nx=x+dx,ny=y+dy;if(pass(init,nx,ny))th+=enF[idx(nx,ny)];}
            enemyFReach[z]=th;
        }

        // Warrior allocation to critical cells. availW units can either stay or move one square.
        vector<int> wAvail=myW;
        vector<int> reservedStay(cells,0), plannedArrive=fixedW;
        vector<tuple<int,int,int>> wmoves; // src,dst,count
        struct Crit{int pri,z,need;}; vector<Crit> crit;
        // 1) enemy flag already on our building: must crush before capture step.
        for(auto *bp:bOrder){ const auto &b=*bp; if(b.owner==me){ int z=idx(b.x,b.y); if(enF[z]>0) crit.push_back({100000+buildingValue(b,v),z,enemyThreat[z]+1}); }}
        // 2) Protect flags that are staying/capturing or entering a threatened building.
        for(auto &fp:fplans){
            int z=fp.dest; bool isB=building_at(v,z%W,z/W)!=nullptr;
            if((fp.locked||isB) && enemyThreat[z]>0) crit.push_back({80000+(isB?5000:0),z,enemyThreat[z]+1});
            // If an enemy flag can also arrive this turn, escort with a surviving warrior.
            // With no enemy W this costs only one W and wins the capture race outright.
            if(isB && enemyFReach[z]>0) crit.push_back({90000+5000+enemyFReach[z]*20,z,enemyThreat[z]+1});
        }
        // 3) likely entry defense: own building with enemy F adjacent/current.
        for(auto *bp:bOrder){ const auto &b=*bp; if(b.owner==me){ int z=idx(b.x,b.y); int ef=enF[z]; int x=b.x,y=b.y; for(auto &d:dirs){auto [dx,dy]=p::delta(d);int nx=x+dx,ny=y+dy;if(pass(init,nx,ny))ef+=enF[idx(nx,ny)];} if(ef>0) crit.push_back({65000+buildingValue(b,v),z,max(1,enemyThreat[z]+1)}); }}
        // 4) Attack stationary-looking enemy flags on buildings.
        for(auto *bp:bOrder){ const auto &b=*bp; int z=idx(b.x,b.y); if(enF[z]>0 && b.owner!=me) crit.push_back({50000+buildingValue(b,v),z,enemyThreat[z]+1}); }
        // 5) Adaptive 0..2 pickets.  Do not waste a warrior on every rear building: reserve
        // guards only when an enemy flag can plausibly reach it soon, or when a large army can
        // cheaply insure a strategically important asset.  Two pickets are used only against
        // close flag + warrior support, otherwise one is enough to kill an unsupported flag.
        if(totalW>=20){
            vector<const p::Building*> ownVal; for(auto *bp:bOrder) if(bp->owner==me) ownVal.push_back(bp);
            sort(ownVal.begin(),ownVal.end(),[&](auto*a,auto*b){
                int va=expectedScore(*a)*7/2+featureBonus(*a,v), vb=expectedScore(*b)*7/2+featureBonus(*b,v);
                if(va!=vb)return va>vb; return homeKeyXY(a->x,a->y)<homeKeyXY(b->x,b->y);
            });
            int picketBudget=min(12,max(2,totalW/9));
            for(auto *bp:ownVal){
                if(picketBudget<=0) break;
                const auto &b=*bp; int z=idx(b.x,b.y), nearestF=INF, nearW=0;
                for(int q:cellOrder){
                    if(enF[q]) nearestF=min(nearestF,dist[q][z]);
                    if(enW[q] && dist[q][z]<=3) nearW+=enW[q];
                }
                int strategic=expectedScore(b)*7/2+featureBonus(b,v);
                int need=0;
                if(nearestF<=2) need=2;
                else if(nearestF<=5) need=1;
                else if(totalW>=65 && strategic>=105) need=1;
                if(nearestF<=4 && nearW>0) need=2;
                need=min(need,picketBudget);
                if(need>0){ crit.push_back({30000+strategic,z,need}); picketBudget-=need; }
            }
        }
        sort(crit.begin(),crit.end(),[&](auto&a,auto&b){if(a.pri!=b.pri)return a.pri>b.pri;return homeKeyCell(a.z)<homeKeyCell(b.z);});
        vector<int> secured(cells,0);
        for(auto &c:crit){
            int z=c.z; if(secured[z]) continue; int need=max(0,c.need-plannedArrive[z]);
            auto [x,y]=xy(z);
            // Never drip-feed an unwinnable local fight.  Only commit when the one-step
            // neighborhood can actually create a strict warrior majority.
            int capacity=wAvail[z];
            for(auto &d:dirOrder){ auto [dx,dy]=p::delta(d); int sx=x-dx,sy=y-dy; if(pass(init,sx,sy)) capacity+=wAvail[idx(sx,sy)]; }
            if(capacity < need) continue;
            int take=min(need,wAvail[z]); if(take){reservedStay[z]+=take;wAvail[z]-=take;plannedArrive[z]+=take;need-=take;}
            for(auto &d:dirOrder){ if(need<=0)break; auto [dx,dy]=p::delta(d); int sx=x-dx,sy=y-dy; if(!pass(init,sx,sy))continue; int ss=idx(sx,sy); int n=min(need,wAvail[ss]); if(n){wAvail[ss]-=n;wmoves.push_back({ss,z,n});plannedArrive[z]+=n;need-=n;} }
            if(need<=0) secured[z]=1;
        }

        // Global fronts for surplus warriors.  Concentrating force is crucial because combat is
        // pure 1-for-1 cancellation and one surviving W wipes all opposing F/S on the cell.
        vector<pair<int,int>> warGoals; // (cell,value)
        for(int z:cellOrder) if(enF[z]>0){
            int val=1050+50*enF[z]; auto *b=building_at(v,z%W,z/W); if(b) val+=buildingValue(*b,v);
            warGoals.push_back({z,val});
        }
        for(auto *bp:bOrder){ const auto &b=*bp; if(b.owner==opp) warGoals.push_back({idx(b.x,b.y),650+2*buildingValue(b,v)}); }
        for(auto *bp:bOrder){ const auto &b=*bp; if(b.owner==me){
            int z=idx(b.x,b.y), near=enF[z];
            for(auto &d:dirs){auto [dx,dy]=p::delta(d);int nx=b.x+dx,ny=b.y+dy;if(pass(init,nx,ny))near+=enF[idx(nx,ny)];}
            if(near) warGoals.push_back({z,1200+buildingValue(b,v)});
        }}
        if(warGoals.empty()) for(auto *bp:bOrder){ const auto &b=*bp; if(b.owner!=me) warGoals.push_back({idx(b.x,b.y),350+buildingValue(b,v)}); }
        if(warGoals.empty()) warGoals.push_back({idx(init.bases[opp=="Y"?0:1].first,init.bases[opp=="Y"?0:1].second),200});

        // Macro warrior movement for leftovers.  If we own a station network, evaluate paths
        // through a station + next-turn TELE as well as ordinary walking.  This makes large
        // formations naturally stage on stations instead of walking past them.
        vector<int> ownStationCells;
        if(stations>=2) for(auto *bp:bOrder) if(bp->owner==me && bp->type=="STATION") ownStationCells.push_back(idx(bp->x,bp->y));
        auto routeDistW=[&](int from,int goal){
            int best=dist[from][goal];
            if((int)ownStationCells.size()<2) return best;
            for(int ss:ownStationCells){
                int toS=dist[from][ss]; if(toS>=INF) continue;
                for(int ds:ownStationCells) if(ds!=ss){
                    int after=dist[ds][goal]; if(after>=INF) continue;
                    // One extra movement phase is spent teleporting after reaching the source station.
                    int via=toS+1+after;
                    // Route through the network only for a meaningful (>=2 turn) saving;
                    // otherwise ordinary walking is less likely to create station oscillations.
                    if(via<=dist[from][goal]-2) best=min(best,via);
                }
            }
            return best;
        };
        auto bestStepToward=[&](int s,int n,int tz){
            auto [x,y]=xy(s); int bestStepScore=INF,dz=s;
            for(auto &d:dirOrder){
                auto [dx,dy]=p::delta(d); int nx=x+dx,ny=y+dy; if(!pass(init,nx,ny))continue;
                int q=idx(nx,ny), dd=routeDistW(q,tz); if(dd>=INF)continue;
                int hostile=enW[q], friendly=plannedArrive[q];
                int danger=max(0,hostile-(n+friendly));
                int stationHere=(find(ownStationCells.begin(),ownStationCells.end(),q)!=ownStationCells.end());
                int sc=dd*30 + danger*80 - min(20,friendly)*2 - (stationHere?8:0);
                if(sc<bestStepScore){bestStepScore=sc;dz=q;}
            }
            if(dz!=s && enW[dz] > n+plannedArrive[dz]+4 && enF[dz]==0){
                int bz=s,bs=INF;
                for(auto &d:dirOrder){
                    auto [dx,dy]=p::delta(d); int nx=x+dx,ny=y+dy; if(!pass(init,nx,ny))continue;
                    int q=idx(nx,ny); int rd=routeDistW(q,tz); if(rd>=INF)continue;
                    int sc=enW[q]*100 - (myW[q]+plannedArrive[q])*8 + rd*10;
                    if(sc<bs){bs=sc;bz=q;}
                }
                dz=bz;
            }
            return dz;
        };

        // Keep the normal doom-stack behavior by default.  Only when one stack is truly large
        // AND our global W count has a healthy margin do we peel off a limited second front.
        // At most one stack is split per turn, preventing gradual fragmentation of the army.
        bool secondFrontUsed=false;
        for(int s:cellOrder) if(wAvail[s]>0){
            int n=wAvail[s];
            vector<tuple<int,int,int>> ranked; // utility, goal, raw goal value
            for(auto [gz,val]:warGoals){
                int d=routeDistW(s,gz); if(d>=INF)continue;
                ranked.push_back({val-24*d,gz,val});
            }
            sort(ranked.begin(),ranked.end(),[&](auto &a,auto &b){
                if(get<0>(a)!=get<0>(b)) return get<0>(a)>get<0>(b);
                return homeKeyCell(get<1>(a))<homeKeyCell(get<1>(b));
            });
            if(ranked.empty()){ reservedStay[s]+=n; plannedArrive[s]+=n; wAvail[s]=0; continue; }
            int tz1=get<1>(ranked[0]), util1=get<0>(ranked[0]);
            int tz2=-1,util2=-INF;
            for(size_t i=1;i<ranked.size();++i){
                int g=get<1>(ranked[i]);
                if(g==tz1 || dist[tz1][g]<5) continue; // genuinely different front
                tz2=g; util2=get<0>(ranked[i]); break;
            }
            int n2=0;
            if(!secondFrontUsed && n>=80 && totalW>=oppWTotal+30 && tz2>=0 && util2>=util1-180){
                n2=max(12,n/4); n2=min(n2,n/3);
                // Do not create a second front that cannot even survive the visible local W mass.
                int step2=bestStepToward(s,n2,tz2);
                if(step2==s || enW[step2] > n2+plannedArrive[step2]+2) n2=0;
            }
            int n1=n-n2;
            auto planPart=[&](int cnt,int target){
                if(cnt<=0)return;
                int dz=bestStepToward(s,cnt,target);
                if(dz==s){reservedStay[s]+=cnt;plannedArrive[s]+=cnt;}
                else {wmoves.push_back({s,dz,cnt});plannedArrive[dz]+=cnt;}
            };
            planPart(n1,tz1);
            if(n2>0){ planPart(n2,tz2); secondFrontUsed=true; }
            wAvail[s]=0;
        }

        // Re-evaluate flag moves: if destination can be crushed and we did not secure it, choose a safer advancing neighbor or stay.
        vector<tuple<int,int,int>> fmoves; // src,dst,count
        vector<int> finalFStay(cells,0), finalFArrive=fixedF;
        for(auto &fp:fplans){
            if(fp.locked || fp.dir.empty()){ finalFStay[fp.src]+=fp.count; continue; }
            int s=fp.src,dz=fp.dest; auto [x,y]=xy(s); auto &tb=v.buildings[fp.target];
            auto safe=[&](int z){ return enemyThreat[z]==0 || plannedArrive[z]>enemyThreat[z]; };
            if(!safe(dz)){
                int curd=dist[s][idx(tb.x,tb.y)], bestd=INF, bestz=s;
                for(auto &d:dirOrder){auto [dx,dy]=p::delta(d);int nx=x+dx,ny=y+dy;if(!pass(init,nx,ny))continue;int z=idx(nx,ny);int dd=dist[z][idx(tb.x,tb.y)]; if(dd<=curd && safe(z) && dd<bestd){bestd=dd;bestz=z;}}
                dz=bestz;
            }
            if(dz==s) finalFStay[s]+=fp.count; else {fmoves.push_back({s,dz,fp.count});finalFArrive[dz]+=fp.count;}
        }

        // Scout planning after combat plans. Move2 toward unknown score clusters, otherwise preserve them away from warrior danger.
        vector<tuple<int,int,int,string,string>> smoves;
        for(int s:cellOrder) if(myS[s]>0){ auto [x,y]=xy(s); int best=-INF,tx=x,ty=y;
            for(auto *bp:bOrder){ const auto &b=*bp; bool unk=b.id<(int)knownScore.size()&&knownScore[b.id]<0; if(!unk && unknownPairs>0)continue; int d=dist[s][idx(b.x,b.y)]; if(d>=INF)continue; int val=(unk?350:40)+expectedScore(b); int u=val*100/(d+2); if(u>best){best=u;tx=b.x;ty=b.y;} }
            auto ab=best_step2(init,x,y,tx,ty); if(ab.first.empty()){continue;} auto [d1x,d1y]=p::delta(ab.first); auto [d2x,d2y]=p::delta(ab.second); int dz=idx(x+d1x+d2x,y+d1y+d2y);
            if(enemyThreat[dz]>0 && v.turn<150) continue; smoves.push_back({s,dz,myS[s],ab.first,ab.second});
        }

        // PRIORITY: capture higher strategic/actual-score buildings first when resources are tight.
        vector<const p::Building*> pr;
        for(auto *bp:bOrder){ const auto &b=*bp; int z=idx(b.x,b.y); int flagHere=finalFStay[z]+finalFArrive[z]; if(b.owner!=me && flagHere>0) pr.push_back(bp); }
        sort(pr.begin(),pr.end(),[&](auto*a,auto*b){int va=buildingValue(*a,v),vb=buildingValue(*b,v);if(va!=vb)return va>vb;return homeKeyXY(a->x,a->y)<homeKeyXY(b->x,b->y);});
        if(!pr.empty()){ vector<pair<int,int>> coords; for(auto *b:pr)coords.push_back({b->x,b->y}); out.push_back(p::priority(coords)); }

        auto dirBetween=[&](int s,int d)->string{auto [x,y]=xy(s);auto [a,b]=xy(d); if(a==x&&b==y-1)return"U";if(a==x&&b==y+1)return"D";if(a==x-1&&b==y)return"L";return"R";};
        // Emit TELE already emitted; movement command order now only matters when same source kind is split. Aggregated counts were budgeted.
        // Merge identical W moves to keep output compact.
        map<pair<int,int>,int> wmerge,fmerge;
        for(auto [s,d,n]:wmoves) if(n>0) wmerge[{s,d}]+=n;
        for(auto [s,d,n]:fmoves) if(n>0) fmerge[{s,d}]+=n;
        for(auto &kv:wmerge){int s=kv.first.first,d=kv.first.second,n=kv.second;auto [x,y]=xy(s);out.push_back(p::move(x,y,"W",n,dirBetween(s,d)));}
        for(auto &kv:fmerge){int s=kv.first.first,d=kv.first.second,n=kv.second;auto [x,y]=xy(s);out.push_back(p::move(x,y,"F",n,dirBetween(s,d)));}
        for(auto &m:smoves){int s,d,n;string a,b;tie(s,d,n,a,b)=m;auto [x,y]=xy(s);out.push_back(p::move2(x,y,n,a,b));}
        return out;
    }
};

Planner planner;
}

vector<string> decide(const p::View& view, const p::Init& init){ return planner.decide(view,init); }
int main(){ return p::run(decide); }

// v29: v27 foundation; calibrated flag targets and raids, extra opponent model,
// exact terminal ordering, and correct partial-force convergence accounting.
#include "protocol.hpp"
#include "sim.hpp"
#include "policy.hpp"
#include "search.hpp"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace {
struct Bot {
    bool ready = false;
    int me = 0;
    int nB = 0;
    int knownScore[sim::MAXB] = {}, symId[sim::MAXB] = {};
    static constexpr int MAXID = 1024;          // external building ids above this are rejected as malformed input
    int idToIdx[MAXID] = {};
    int nId = 0;                                // number of valid entries of idToIdx (max external id + 1)
    unsigned char depotSeen[sim::MAXB][2] = {};
    int ownTurns[sim::MAXB][2] = {};   // v22: turns each building was observed owned (state after turns 1..t-1)
    int lastObsTurn = 0;
    pol::Doctrine doc;
    srch::Searcher sr;
    double maxMs = 0, maxCpu = 0;

    int idOf(int extId) const { return extId >= 0 && extId < nId ? idToIdx[extId] : -1; }

    // Malformed initial input (bad size / coordinates / ids) throws: decide() then answers every turn with an empty command list.
    void init_once(const p::Init& init) {
        if (ready) return;
        const int W = init.width, H = init.height;
        if (W <= 0 || H <= 0 || W * H > sim::MAXC || (int)init.terrain.size() != H) throw std::runtime_error("bad map size");
        for (const auto& row : init.terrain) if ((int)row.size() != W) throw std::runtime_error("bad map row");
        int maxId = 0;
        for (const auto& b : init.buildings) {
            if (b.id < 0 || b.id >= MAXID || b.x < 0 || b.y < 0 || b.x >= W || b.y >= H) throw std::runtime_error("bad building");
            maxId = max(maxId, b.id);
        }
        for (const auto& bp : init.bases) if (bp.first < 0 || bp.second < 0 || bp.first >= W || bp.second >= H) throw std::runtime_error("bad base");
        me = init.team == "Y" ? 0 : 1;
        vector<sim::BInit> bs;
        nId = maxId + 1;
        for (int i = 0; i < nId; i++) idToIdx[i] = -1;
        { int k = 0; for (const auto& b : init.buildings) { if (k >= sim::MAXB) break; idToIdx[b.id] = k; bs.push_back({k, b.x, b.y, b.type}); k++; } }
        sim::initGeo(W, H, init.terrain, bs, init.bases[0].first, init.bases[0].second, init.bases[1].first, init.bases[1].second);
        nB = (int)bs.size();
        for (int b = 0; b < sim::MAXB; b++) { knownScore[b] = -1; symId[b] = -1; }
        for (int b = 0; b < nB; b++) { symId[b] = sim::G.sym[b]; if (sim::G.btype[b] == sim::PLAZA) knownScore[b] = 3; }
        ready = true;
    }

    void observe(const p::View& v) {
        if (v.turn >= 2 && v.turn != lastObsTurn) {
            for (auto& bb : v.buildings) { int id = idOf(bb.id); if (id < 0) continue; if (bb.owner == "Y") ownTurns[id][0]++; else if (bb.owner == "K") ownTurns[id][1]++; }
        }
        lastObsTurn = v.turn;
        for (auto& bb : v.buildings) {
            int id = idOf(bb.id); if (id < 0) continue;
            if (bb.score >= 0) { int sc = min(bb.score, 1000); knownScore[id] = sc; int s = symId[id]; if (s >= 0) knownScore[s] = sc; }
            if (bb.owner == "Y") depotSeen[id][0] = 1;
            if (bb.owner == "K") depotSeen[id][1] = 1;
        }
        for (int b = 0; b < nB; b++) {
            if (knownScore[b] >= 0) sim::sc10[b] = knownScore[b] * 10;
            else if (sim::G.btype[b] == sim::PLAZA) sim::sc10[b] = 30;
            else { int x = sim::G.cx(sim::G.bcell[b]); sim::sc10[b] = (x >= 5 && x <= 9) ? 30 : 15; }
        }
    }

    // Sanity bounds (never reached by a real game: <= ~1500 warriors per team, resource <= 40): keep every intermediate value of the
    // planner/simulator far away from int / int16 overflow even for absurd input.
    static constexpr int MAX_CELL = 2000, MAX_TEAM_UNITS = 4000, MAX_RES = 200, MAX_TURN = 1000;

    void toState(const p::View& v, sim::GS& s) {
        memset((void*)&s, 0, sizeof(s));
        s.turn = clamp(v.turn, 0, MAX_TURN);
        for (int b = 0; b < nB; b++) for (int t = 0; t < 2; t++) s.occ10[t] += ownTurns[b][t] * sim::sc10[b];
        int opp = 1 - me;
        s.t[me].res = (int16_t)clamp(v.my_resource, 0, MAX_RES); s.t[opp].res = (int16_t)clamp(v.opp_resource, 0, MAX_RES);
        int total[2] = {0, 0};
        for (auto& u : v.units) {   // malformed units (unknown team/kind, off-map, non-positive count) are ignored
            if ((u.team != "Y" && u.team != "K") || u.count <= 0 || u.x < 0 || u.y < 0 || u.x >= sim::G.W || u.y >= sim::G.H) continue;
            int t = u.team == "Y" ? 0 : 1; int c = sim::G.idx(u.x, u.y);
            int16_t* dst = u.kind == "F" ? &s.t[t].F[c] : u.kind == "W" ? &s.t[t].W[c] : u.kind == "S" ? &s.t[t].S[c] : nullptr;
            if (!dst) continue;
            int n = min(u.count, MAX_CELL - (int)*dst); n = min(n, MAX_TEAM_UNITS - total[t]);
            if (n <= 0) continue;
            *dst = (int16_t)(*dst + n); total[t] += n;
        }
        for (auto& b : v.buildings) {
            int id = idOf(b.id); if (id < 0) continue;
            s.own[id] = (int8_t)(b.owner == "Y" ? 1 : b.owner == "K" ? 2 : 0);
            s.depot[id] = (uint8_t)(depotSeen[id][0] | (depotSeen[id][1] << 1));
        }
    }

    static string dirStr(int d) { static const char* D[4] = {"U", "D", "L", "R"}; return D[d]; }

    vector<string> emit(const sim::Plan& pl) {
        vector<string> out;
        static const char* K[3] = {"F", "W", "S"};
        for (int i = 0; i < pl.nsp; i++) {
            const auto& sp = pl.sp[i]; int c = sp.cell;
            if (c == sim::G.base[me]) out.push_back(p::spawn(K[sp.kind], sp.n));
            else out.push_back(p::spawn(K[sp.kind], sp.n, sim::G.cx(c), sim::G.cy(c)));
        }
        if (pl.teleSrc >= 0) out.push_back(p::tele(sim::G.cx(pl.teleSrc), sim::G.cy(pl.teleSrc), K[pl.teleKind], pl.teleN, sim::G.cx(pl.teleDst), sim::G.cy(pl.teleDst)));
        if (pl.nprio > 0) {
            vector<pair<int,int>> co;
            for (int i = 0; i < pl.nprio; i++) { int c = sim::G.bcell[pl.prio[i]]; co.push_back({sim::G.cx(c), sim::G.cy(c)}); }
            out.push_back(p::priority(co));
        }
        for (int i = 0; i < pl.nmv; i++) { const auto& m = pl.mv[i]; out.push_back(p::move(sim::G.cx(m.src), sim::G.cy(m.src), K[m.kind], m.n, dirStr(m.dir))); }
        return out;
    }

    vector<string> decide(const p::View& v, const p::Init& init) {
        auto t0 = chrono::steady_clock::now();
        init_once(init); observe(v);
        static sim::GS s; static sim::Plan pl;
        toState(v, s);
        pl = sr.search(s, me);
        auto out = emit(pl);
        double ms = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
        maxMs = max(maxMs, ms);
        return out;
    }
};
Bot bot;
}

vector<string> decide(const p::View& view, const p::Init& init) {
    try { return bot.decide(view, init); }
    catch (...) { return {}; } // never crash: an empty command list only skips this turn
}
int main() { ios::sync_with_stdio(false); cin.tie(nullptr); return p::run(decide); }

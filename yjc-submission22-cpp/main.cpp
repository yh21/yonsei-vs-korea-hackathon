// v18 "stack": simulator-backed doctrine bot. C++20, standard library only, no data files.
//
// Layers (see NOTES.md):
//   sim.hpp    exact re-implementation of the 9 phase turn pipeline (verified against the reference engine)
//   policy.hpp base planner: lean port of the v16/v17 heuristics + stack doctrine additions
//              (hold-value garrison priority, threat-scaled one-warrior pickets on ENG/HALL/hospitals, 7/5/4/3 flags)
//   search.hpp per turn: predict the enemy plan with the best-fitting lineage model, then improve the doctrine plan by
//              simulator-backed local search (macro reinforce, joint converge, single stack moves; 2 turn rollouts,
//              exact terminal scoring in the last ten turns); anytime, <= 50 ms wall clock, ~10 ms CPU typical.
//   main.cpp   protocol glue, memory (revealed scores, depot claims), command emission; never throws.
#include "protocol.hpp"
#include "sim.hpp"
#include "policy.hpp"
#include "search.hpp"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

namespace {
struct Bot {
    bool ready = false;
    int me = 0;
    int nB = 0;
    vector<int> knownScore, symId, idToIdx;
    unsigned char depotSeen[sim::MAXB][2] = {};
    int ownTurns[sim::MAXB][2] = {};   // v22: turns each building was observed owned (state after turns 1..t-1)
    int lastObsTurn = 0;
    pol::Doctrine doc;
    srch::Searcher sr;
    double maxMs = 0, maxCpu = 0;

    int idOf(int extId) const { return extId >= 0 && extId < (int)idToIdx.size() ? idToIdx[extId] : -1; }

    void init_once(const p::Init& init) {
        if (ready) return;
        ready = true;
        me = init.team == "Y" ? 0 : 1;
        vector<sim::BInit> bs;
        int maxId = 0; for (auto& b : init.buildings) maxId = max(maxId, b.id);
        idToIdx.assign(maxId + 1, -1);
        { int k = 0; for (auto& b : init.buildings) { if (k >= sim::MAXB) break; idToIdx[b.id] = k; bs.push_back({k, b.x, b.y, b.type}); k++; } }
        sim::initGeo(init.width, init.height, init.terrain, bs, init.bases[0].first, init.bases[0].second, init.bases[1].first, init.bases[1].second);
        nB = (int)bs.size();
        knownScore.assign(nB, -1); symId.assign(nB, -1);
        for (int b = 0; b < nB; b++) { symId[b] = sim::G.sym[b]; if (sim::G.btype[b] == sim::PLAZA) knownScore[b] = 3; }
    }

    void observe(const p::View& v) {
        if (v.turn >= 2 && v.turn != lastObsTurn) {
            for (auto& bb : v.buildings) { int id = idOf(bb.id); if (id < 0) continue; if (bb.owner == "Y") ownTurns[id][0]++; else if (bb.owner == "K") ownTurns[id][1]++; }
        }
        lastObsTurn = v.turn;
        for (auto& bb : v.buildings) {
            int id = idOf(bb.id); if (id < 0) continue;
            if (bb.score >= 0) { knownScore[id] = bb.score; int s = symId[id]; if (s >= 0) knownScore[s] = bb.score; }
            if (bb.owner == "Y") depotSeen[id][0] = 1;
            if (bb.owner == "K") depotSeen[id][1] = 1;
        }
        for (int b = 0; b < nB; b++) {
            if (knownScore[b] >= 0) sim::sc10[b] = knownScore[b] * 10;
            else if (sim::G.btype[b] == sim::PLAZA) sim::sc10[b] = 30;
            else { int x = sim::G.cx(sim::G.bcell[b]); sim::sc10[b] = (x >= 5 && x <= 9) ? 30 : 15; }
        }
    }

    void toState(const p::View& v, sim::GS& s) {
        memset(&s, 0, sizeof(s));
        s.turn = v.turn;
        for (int b = 0; b < nB; b++) for (int t = 0; t < 2; t++) s.occ10[t] += ownTurns[b][t] * sim::sc10[b];
        int opp = 1 - me;
        s.t[me].res = v.my_resource; s.t[opp].res = v.opp_resource;
        for (auto& u : v.units) {
            int t = u.team == "Y" ? 0 : 1; int c = sim::G.idx(u.x, u.y);
            if (u.kind == "F") s.t[t].F[c] += u.count; else if (u.kind == "W") s.t[t].W[c] += u.count; else s.t[t].S[c] += u.count;
        }
        for (auto& b : v.buildings) {
            int id = idOf(b.id); if (id < 0) continue;
            s.own[id] = b.owner == "Y" ? 1 : b.owner == "K" ? 2 : 0;
            s.depot[id] = depotSeen[id][0] | (depotSeen[id][1] << 1);
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

// eval.hpp - state evaluation features (relative to team `me`) used to score rollout end states.
#pragma once
#include "sim.hpp"
#include <algorithm>
#include <cmath>

namespace ev {
using namespace sim;

constexpr int NF = 28;

// Fills f[0..NF) with features of state s from the point of view of team t. All "d*" features are (t - other).
inline void features(const Map& M, const State& s, int t, double* f) {
    const int o = 1 - t;
    double rem = (TOTAL_TURNS - s.turn) / (double)TOTAL_TURNS;  // 1 -> 0
    int sc_t = score2(M, s, t), sc_o = score2(M, s, o);
    int wt = totalW(s, t), wo = totalW(s, o), ft = totalF(s, t), fo = totalF(s, o);
    double dScore = (sc_t - sc_o) * 0.5;
    int hT = ownedCount(M, s, t, T_HALL), hO = ownedCount(M, s, o, T_HALL);
    int eT = ownedCount(M, s, t, T_ENG) > 0, eO = ownedCount(M, s, o, T_ENG) > 0;
    int hospT = ownedCount(M, s, t, T_HOSPITAL), hospO = ownedCount(M, s, o, T_HOSPITAL);
    int libT = ownedCount(M, s, t, T_LIBRARY) > 0, libO = ownedCount(M, s, o, T_LIBRARY) > 0;
    int stT = ownedCount(M, s, t, T_STATION), stO = ownedCount(M, s, o, T_STATION);
    // local balance around every building (stacks within path distance <= 2, weights 1, .6, .3)
    double safeMine = 0, atkOpp = 0, atkNeutral = 0, riskMine = 0;
    for (int b = 0; b < M.nb; b++) {
        int c = M.bcell[b];
        double L = 0;
        for (int q = 0; q < NC; q++) {
            int d = M.dist[c][q];
            if (d > 2) continue;
            double w = d == 0 ? 1.0 : (d == 1 ? 0.6 : 0.3);
            L += w * (s.Wn[t][q] - s.Wn[o][q]);
        }
        double cl = std::max(-10.0, std::min(10.0, L)) / 10.0;
        double sc = M.score2[b] * 0.5;
        if (s.owner[b] == t) { safeMine += sc * cl; riskMine += sc * std::max(0.0, -cl); }
        else if (s.owner[b] == o) atkOpp += sc * cl;
        else atkNeutral += sc * cl;
    }
    int i = 0;
    f[i++] = dScore;
    f[i++] = dScore * rem;
    f[i++] = wt - wo;
    f[i++] = (wt - wo) * rem;
    f[i++] = ft - fo;
    f[i++] = s.res[t] - s.res[o];
    f[i++] = (hT - hO) * rem;
    f[i++] = (eT - eO) * rem;
    f[i++] = (hospT - hospO) * rem;
    f[i++] = (libT - libO) * rem;
    f[i++] = (stT - stO) * rem;
    f[i++] = safeMine;
    f[i++] = riskMine;
    f[i++] = atkOpp;
    f[i++] = atkNeutral;
    f[i++] = safeMine * rem;
    f[i++] = atkOpp * rem;
    f[i++] = (double)((sc_o == 0) ? 1 : 0);      // opponent has nothing
    f[i++] = (double)((sc_t == 0) ? 1 : 0);      // we have nothing (collapse)
    f[i++] = rem;
    f[i++] = (hT - hO);
    f[i++] = (eT - eO);
    f[i++] = (double)std::min(wt, wo) * rem;      // how much total force is in play
    f[i++] = (wt + wo) * 0.01;
    // force concentration (Herfindahl index of the W distribution) and F standing on capturable buildings
    double hiT = 0, hiO = 0;
    for (int c = 0; c < NC; c++) { hiT += (double)s.Wn[t][c] * s.Wn[t][c]; hiO += (double)s.Wn[o][c] * s.Wn[o][c]; }
    hiT = wt > 0 ? hiT / ((double)wt * wt) : 0; hiO = wo > 0 ? hiO / ((double)wo * wo) : 0;
    int fcapT = 0, fcapO = 0;
    for (int b = 0; b < M.nb; b++) {
        int c = M.bcell[b];
        if (s.owner[b] != t && s.F[t][c] > 0 && s.F[o][c] == 0) fcapT++;
        if (s.owner[b] != o && s.F[o][c] > 0 && s.F[t][c] == 0) fcapO++;
    }
    f[i++] = (hiT - hiO) * 10.0;
    f[i++] = (hiT - hiO) * 10.0 * rem;
    f[i++] = fcapT - fcapO;
    f[i++] = (fcapT - fcapO) * rem;
}

}  // namespace ev

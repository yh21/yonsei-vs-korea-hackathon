    // ------------------------------------------------------------------ warriors
    struct Cand { int target; int kind; double score; };
    struct Group { int cell, next, target; };
    vector<Group> groups;
    vector<int> groupCells;
    vector<char> isGroup;
    vector<pair<int, int>> tMem, newTMem;  // (cell after move, target): hysteresis of missions
    vector<char> claimedT;
    int anchorCell = -1, anchorNext = -1, missionT = -1;

    void selectGroups() {
        groupCells.clear();
        isGroup.assign(N, 0);
        const int mmin = (int)prm(28, 10);
        vector<int> cand;
        for (int c = 0; c < N; c++) if (remW[c] >= mmin) cand.push_back(c);
        sort(cand.begin(), cand.end(), [&](int a, int b) { return remW[a] > remW[b] || (remW[a] == remW[b] && homeKey(a) < homeKey(b)); });
        for (int c : cand) {
            bool near = false;
            for (int g : groupCells) if (dist[c][g] <= 2) near = true;
            if (!near) groupCells.push_back(c);
        }
        if (groupCells.empty()) {
            int A = -1; long best = -1;
            for (int c = 0; c < N; c++) if (remW[c] > 0) {
                long m = 0;
                for (int q = 0; q < N; q++) if (remW[q] > 0 && dist[c][q] <= 2) m += remW[q];
                long key = (long)remW[c] * 100000 + m;
                if (key > best) { best = key; A = c; }
            }
            if (A >= 0) groupCells.push_back(A);
        }
        for (int g : groupCells) isGroup[g] = 1;
    }

    void planWarriors() {
        anchorCell = anchorNext = missionT = -1;
        groups.clear();
        newMem.clear(); newTMem.clear();
        gHave.assign(N, 0);
        gUsed = 0;
        claimedT.assign(N, 0);
        int totW = 0;
        for (int c = 0; c < N; c++) totW += myW[c];
        gBudget = (int)(prm(10, 0.4) * totW);
        if (prm(20, 1.0) > 0.5) planConvergence();
        continueGarrisons();
        planTeams();
        selectGroups();
        gMem = newMem;
        newMem.clear();
        newGarrisons();
        for (auto& g : newMem) gMem.push_back(g);
        // groups may have lost stacks to garrison duty (never: donors exclude them), plan each of them
        vector<int> gc = groupCells;
        for (int g : gc) if (remW[g] > 0) planGroup(g);
        tMem = newTMem;
        if (!groups.empty()) { anchorCell = groups[0].cell; anchorNext = groups[0].next; missionT = groups[0].target; }

        // ---- reserves: snipe flags next to them or join the nearest hunting group
        vector<int> order;
        for (int c = 0; c < N; c++) if (remW[c] > 0) order.push_back(c);
        sort(order.begin(), order.end(), [&](int a, int b) { return remW[a] > remW[b] || (remW[a] == remW[b] && homeKey(a) < homeKey(b)); });
        for (int q : order) {
            if (remW[q] <= 0) continue;
            int bestE = -1; double bv = 0;
            for (int e : n1[q]) if (enF[e] > 0) {
                int J = joinMass(e);
                if (J > thr[e]) {
                    double v2 = 2.0 + enF[e] + (bAt[e] >= 0 && blds[bAt[e]].owner != 0 ? 2.0 : 0.0);
                    if (v2 > bv) { bv = v2; bestE = e; }
                }
            }
            if (bestE >= 0) {
                for (int u : n1[bestE]) if (remW[u] > 0) assign(u, 1, remW[u], bestE);
                continue;
            }
            int cnt = remW[q];
            int goalc = meBase; int bdg = INF;
            for (auto& g : groups) {
                int d = dist[q][g.next];
                if (d < bdg) { bdg = d; goalc = g.next; }
            }
            int pick = q; bool have = false; int bd = INF, bj = -INF;
            for (int c : n1[q]) {
                int m = endW[c] + cnt;
                if (!safeCell(c, m)) continue;
                int d = dist[c][goalc];
                if (!have || d < bd || (d == bd && m > bj)) { have = true; bd = d; bj = m; pick = c; }
            }
            if (!have) {
                int bm = -INF;
                for (int c : n1[q]) {
                    int m = endW[c] + cnt - thr[c];
                    if (m > bm) { bm = m; pick = c; }
                }
            }
            assign(q, 1, cnt, pick);
        }
    }

    void planGroup(int A) {
        int lastTarget = -1;
        for (auto& m : tMem) if (m.first == A) { lastTarget = m.second; break; }
        int nA = remW[A];
        double ratio = prm(4, 1.2);
        int myScore = 0, opScore = 0;
        for (auto& b : blds) {
            if (b.owner == 1) myScore += scoreOf(b);
            else if (b.owner == 2) opScore += scoreOf(b);
        }
        if (turn >= 100 && myScore < opScore) ratio = prm(5, 1.0);
        auto fDist = [&](int t) {
            int d = INF;
            for (auto& u : fUnits) if (u.goal < 0 && !u.stay) d = min(d, dist[u.cell][t]);
            if (d == INF) d = dist[meBase][t] + 3;
            return d;
        };
        long massTF = 0;
        for (int q = 0; q < N; q++) if (remW[q] > 0 && dist[A][q] <= 2) massTF += remW[q];
        const int hz = (int)prm(6, 3);
        auto feas = [&](int t, int k, double rat) {
            int h = min(k, hz);
            double my = (k <= hz) ? regionMy(t, h) : (double)massTF;
            double en = regionEn(t, h);
            return my >= rat * en + 1.5;
        };
        const double hys = prm(7, 1.5);
        vector<Cand> cands;
        for (auto& b : blds) {
            int t = b.cell;
            int k = dist[A][t];
            if (k >= INF || claimedT[t]) continue;
            if (b.owner == 1) {
                double danger = 0;
                for (int q = 0; q < N; q++) {
                    if (enF[q] > 0 && dist[q][t] <= 3) danger += 1.0 * enF[q] / (dist[q][t] + 1);
                    if (enW[q] >= 3 && dist[q][t] <= 2) danger += 0.5 + 0.02 * enW[q];
                }
                if (danger <= 0) continue;
                if (!feas(t, k, ratio)) continue;
                double sc = 2.0 * bval(b) * min(1.0, danger) / pow(k + 2.0, prm(9, 1.3));
                if (k == 0) sc *= prm(15, 0.6);
                if (t == lastTarget) sc *= hys;
                cands.push_back({t, 2, sc});
                continue;
            }
            if (enF[t] > 0) continue;  // handled as a flag hunt below
            double val = bval(b);
            int fd = fDist(t);
            int Ttot = max(k, fd);
            int need = (b.owner == 2 ? 2 : 1);
            if (turn + Ttot + need > 158) continue;
            double sc = val / pow(Ttot + 2.0, prm(9, 1.3));
            if (!feas(t, k, ratio)) continue;
            if (t == lastTarget) sc *= hys;
            cands.push_back({t, 0, sc});
        }
        for (int e = 0; e < N; e++) if (enF[e] > 0 && !claimedT[e]) {
            int k = dist[A][e];
            if (k >= INF || k > 6) continue;
            if (bAt[e] < 0 && k > 2) continue;
            if (!feas(e, k, ratio)) continue;
            double val = 4.0 + 0.6 * enF[e];
            int bi = bAt[e];
            if (bi >= 0) {
                const Bld& b = blds[bi];
                if (b.owner == 1) val += bval(b) * 1.2;
                else if (b.owner == 2) val += bval(b) * 0.9;
                else val += bval(b) * 0.4;
            }
            bool nearOwn = false;
            for (auto& b : blds) if (b.owner == 1 && dist[b.cell][e] <= 4) { nearOwn = true; break; }
            if (nearOwn) val *= prm(8, 2.0);
            double sc = val / pow(k + 2.0, prm(9, 1.3));
            if (e == lastTarget) sc *= hys;
            cands.push_back({e, 1, sc});
        }
        for (int e = 0; e < N; e++) if (enW[e] >= 3 && enF[e] == 0 && bAt[e] < 0 && !claimedT[e]) {
            int k = dist[A][e];
            if (k >= INF || k > 6) continue;
            if (!feas(e, k, 1.6)) continue;
            double near = 0;
            for (auto& b : blds) if (b.owner == 1 && dist[b.cell][e] <= 3) near += bval(b) * 0.25;
            double sc = (1.2 + 0.05 * enW[e] + near) / pow(k + 2.0, prm(9, 1.3));
            if (e == lastTarget) sc *= hys;
            cands.push_back({e, 3, sc});
        }
        int T = -1; double bs = 0;
        for (auto& c : cands) if (c.score > bs) { bs = c.score; T = c.target; }
        if (T >= 0) claimedT[T] = 1;
        if (dbg()) {
            vector<Cand> cs = cands;
            sort(cs.begin(), cs.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
            fprintf(dbg(), "T%d cands@(%d,%d):", turn, cx(A), cy(A));
            for (int i = 0; i < (int)cs.size() && i < 6; i++) fprintf(dbg(), " (%d,%d)k%d:%.2f/%d", cx(cs[i].target), cy(cs[i].target), cs[i].kind, cs[i].score, dist[A][cs[i].target]);
            fprintf(dbg(), "\n");
        }

        // ---- choose next cell
        int goal = T >= 0 ? T : A;
        int chosen = A;
        {
            struct O { int c; bool safe; int d; int j; };
            vector<O> os;
            for (int c : n1[A]) os.push_back({c, safeCell(c, joinMass(c)), dist[c][goal], joinMass(c)});
            int bi = -1;
            for (int i = 0; i < (int)os.size(); i++) {
                if (!os[i].safe) continue;
                if (bi < 0) { bi = i; continue; }
                auto& a = os[i]; auto& b = os[bi];
                if (a.d < b.d || (a.d == b.d && a.j > b.j)) bi = i;
            }
            if (bi >= 0) chosen = os[bi].c;
            else {
                int bm = -INF;
                for (auto& o : os) {
                    int m = o.j - thr[o.c];
                    if (m > bm) { bm = m; chosen = o.c; }
                }
            }
        }
        // brief waits: merge big adjacent stragglers; let flag bearers catch up when a capture is imminent
        if (chosen != A && safeCell(A, joinMass(A))) {
            long strag = 0;
            for (int q = 0; q < N; q++) if (remW[q] > 0 && q != A && dist[q][A] == 1 && !isGroup[q]) {
                bool inNext = false;
                for (int u : n1[chosen]) if (u == q) inNext = true;
                if (!inNext) strag += remW[q];
            }
            int fNear = 0, fComing = 0;
            for (auto& u : fUnits) if (u.goal < 0 && !u.stay) {
                if (dist[A][u.cell] <= 1) fNear++;
                else if (dist[A][u.cell] <= 3) fComing++;
            }
            bool nearTarget = T >= 0 && dist[A][T] <= 4 && bAt[T] >= 0;
            if (strag >= max(3L, (long)(prm(22, 0.25) * nA)) && waitStreak < 1) { chosen = A; waitStreak++; }
            else if (nearTarget && fNear == 0 && fComing > 0 && waitF < 3) { chosen = A; waitF++; waitStreak = 0; }
            else { waitStreak = 0; if (!nearTarget) waitF = 0; }
        } else { waitStreak = 0; waitF = 0; }
        DLOG("T%d G=(%d,%d) n=%d massTF=%ld T=(%d,%d) chosen=(%d,%d) ncand=%d wait=%d/%d thr(A)=%d\n", turn, cx(A), cy(A), nA, massTF,
             T >= 0 ? cx(T) : -1, T >= 0 ? cy(T) : -1, cx(chosen), cy(chosen), (int)cands.size(), waitStreak, waitF, thr[A]);
        groups.push_back({A, chosen, T});
        newTMem.push_back({chosen, T});
        for (int q : n1[chosen]) if (remW[q] > 0 && (!isGroup[q] || q == A || dist[q][A] <= 1)) assign(q, 1, remW[q], chosen);
    }


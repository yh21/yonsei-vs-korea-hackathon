// In-process fast arena. Usage:
//   ./arena --maps maps.txt --a t --opps 17 16 15 ext:../../../bot_opp --seeds 10000 200 [--both] [--jobs 6] [-v]
// Bots: "t" (tempo), "17","16","15" (compiled in), "ext:<path>" (subprocess).
#include "engine.hpp"
#include <chrono>
#include <cstdlib>
#include <functional>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

using namespace std;
typedef vector<string> (*DecideFn)(const p::View&, const p::Init&);
vector<string> decideT(const p::View&, const p::Init&);
vector<string> decide17(const p::View&, const p::Init&);
vector<string> decide16(const p::View&, const p::Init&);
vector<string> decide15(const p::View&, const p::Init&);
#ifdef HAVE_B
vector<string> decideB(const p::View&, const p::Init&);
#endif
#ifdef HAVE_C
vector<string> decideC(const p::View&, const p::Init&);
#endif

struct ExtBot {
    pid_t pid = -1; FILE* in = nullptr; FILE* out = nullptr;
    bool start(const string& path) {
        int a[2], b[2];
        if (pipe(a) || pipe(b)) return false;
        pid = fork();
        if (pid == 0) {
            dup2(a[0], 0); dup2(b[1], 1);
            int dn = open("/dev/null", O_WRONLY); dup2(dn, 2);
            close(a[0]); close(a[1]); close(b[0]); close(b[1]);
            execl(path.c_str(), path.c_str(), (char*)nullptr);
            _exit(127);
        }
        close(a[0]); close(b[1]);
        in = fdopen(a[1], "w"); out = fdopen(b[0], "r");
        return true;
    }
    void send(const string& s) { fputs(s.c_str(), in); fflush(in); }
    vector<string> recv() {
        vector<string> ls; char buf[4096];
        while (fgets(buf, sizeof buf, out)) {
            string l(buf); while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            if (l == "END") return ls;
            ls.push_back(l);
        }
        return ls;
    }
    void stop() { if (in) fclose(in); if (out) fclose(out); if (pid > 0) { kill(pid, SIGKILL); waitpid(pid, nullptr, 0); } }
};

struct Player {
    string spec; DecideFn fn = nullptr; ExtBot* ext = nullptr;
    double maxms = 0, totms = 0; int calls = 0;
};

static DecideFn lookup(const string& n) {
    if (n == "t") return decideT;
    if (n == "17") return decide17;
    if (n == "16") return decide16;
    if (n == "15") return decide15;
#ifdef HAVE_B
    if (n == "b") return decideB;
#endif
#ifdef HAVE_C
    if (n == "c") return decideC;
#endif
    return nullptr;
}

struct GameOut { int winner; string reason; int turns, sy, sk; double maxms, avgms; int hall[2]={0,0}, eng[2]={0,0}, wd[3]={0,0,0}, pd[3]={0,0,0}; };

static GameOut play(const eng::GameMap& m, Player& py, Player& pk) {
    eng::State s = eng::make_state(m);
    Player* pl[2] = {&py, &pk};
    p::Init init[2] = {eng::make_init(s, 0), eng::make_init(s, 1)};
    for (int t = 0; t < 2; t++) if (pl[t]->ext) pl[t]->ext->send(eng::serialize_init(s, t));
    eng::Result r; GameOut g; g.winner = 0;
    while (r.winner == -2) {
        int turn = s.turn + 1;
        vector<eng::Cmd> cmds[2];
        for (int t = 0; t < 2; t++) {
            vector<string> lines;
            if (pl[t]->ext) {
                pl[t]->ext->send(eng::serialize_turn(s, t, turn));
                lines = pl[t]->ext->recv();
            } else {
                p::View v = eng::make_view(s, t, turn);
                auto t0 = chrono::steady_clock::now();
                lines = pl[t]->fn(v, init[t]);
                double ms = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
                pl[t]->maxms = max(pl[t]->maxms, ms); pl[t]->totms += ms; pl[t]->calls++;
            }
            for (auto& l : lines) { eng::Cmd c; if (eng::parse_cmd(l, c)) cmds[t].push_back(c); }
        }
        r = eng::run_turn(s, cmds);
        for (auto& q : s.b) { if (q.owner) { if (q.type == eng::HALL) g.hall[q.owner - 1]++; if (q.type == eng::ENG) g.eng[q.owner - 1]++; } }
        int idx = s.turn == 40 ? 0 : (s.turn == 80 ? 1 : (s.turn == 120 ? 2 : -1));
        if (idx >= 0) { int w[2] = {0, 0}; for (int t = 0; t < 2; t++) for (int z = 0; z < eng::NC; z++) w[t] += s.u[t][eng::WK][z]; g.wd[idx] = w[0] - w[1]; g.pd[idx] = eng::team_score(s, 0) - eng::team_score(s, 1); }
    }
    g.winner = r.winner; g.reason = r.reason; g.turns = s.turn; g.sy = r.score[0]; g.sk = r.score[1];
    g.maxms = py.maxms; g.avgms = py.calls ? py.totms / py.calls : 0;
    return g;
}

int main(int argc, char** argv) {
    string maps = "maps.txt", a = "t"; vector<string> opps; int lo = 10000, n = 100, jobs = 4; bool both = false, verbose = false;
    for (int i = 1; i < argc; i++) {
        string k = argv[i];
        if (k == "--maps") maps = argv[++i];
        else if (k == "--a") a = argv[++i];
        else if (k == "--opps") { while (i + 1 < argc && argv[i + 1][0] != '-') opps.push_back(argv[++i]); }
        else if (k == "--seeds") { lo = atoi(argv[++i]); n = atoi(argv[++i]); }
        else if (k == "--jobs") jobs = atoi(argv[++i]);
        else if (k == "--both") both = true;
        else if (k == "-v") verbose = true;
    }
    auto mv = eng::load_maps(maps.c_str());
    map<int, const eng::GameMap*> bySeed; for (auto& m : mv) bySeed[m.seed] = &m;
    for (auto& opp : opps) {
        struct Job { int seed; bool aY; };
        vector<Job> js;
        for (int s = lo; s < lo + n; s++) { if (!bySeed.count(s)) continue; if (both) { js.push_back({s, true}); js.push_back({s, false}); } else js.push_back({s, s % 2 == 0}); }
        int pfd[2]; if (pipe(pfd)) return 1;
        fcntl(pfd[0], F_SETFL, O_NONBLOCK);
        int running = 0; size_t next = 0;
        string acc;
        vector<string> lines;
        auto drain = [&]() {
            char buf[4096]; ssize_t r;
            while ((r = read(pfd[0], buf, sizeof buf)) > 0) acc.append(buf, r);
            size_t pos;
            while ((pos = acc.find('\n')) != string::npos) { lines.push_back(acc.substr(0, pos)); acc.erase(0, pos + 1); }
        };
        while (next < js.size() || running > 0) {
            while (running < jobs && next < js.size()) {
                Job j = js[next++];
                pid_t pid = fork();
                if (pid == 0) {
                    close(pfd[0]);
                    Player pa, pb;
                    pa.spec = a; pb.spec = opp;
                    auto setup = [&](Player& pp) -> bool {
                        if (pp.spec.rfind("ext:", 0) == 0) { pp.ext = new ExtBot(); return pp.ext->start(pp.spec.substr(4)); }
                        pp.fn = lookup(pp.spec); return pp.fn != nullptr;
                    };
                    if (!setup(pa) || !setup(pb)) _exit(2);
                    Player& py = j.aY ? pa : pb; Player& pk = j.aY ? pb : pa;
                    GameOut g = play(*bySeed[j.seed], py, pk);
                    int aw = g.winner == -1 ? 0 : ((g.winner == 0) == j.aY ? 1 : -1);   // 1 win, -1 loss, 0 draw
                    int as_ = j.aY ? g.sy : g.sk, os = j.aY ? g.sk : g.sy;
                    Player& me = pa;
                    double mx = me.maxms, av = me.calls ? me.totms / me.calls : 0;
                    char buf[512];
                    int me_ = j.aY ? 0 : 1, op_ = 1 - me_; int sg = j.aY ? 1 : -1;
                    int len = snprintf(buf, sizeof buf, "%d %d %d %s %d %d %d %.2f %.3f %d %d %d %d %d %d %d %d %d\n", j.seed, j.aY ? 1 : 0, aw, g.reason.c_str(), g.turns, as_, os, mx, av,
                        g.hall[me_], g.hall[op_], g.eng[me_], g.eng[op_], sg * g.wd[0], sg * g.wd[1], sg * g.wd[2], sg * g.pd[1], sg * g.pd[2]);
                    if (write(pfd[1], buf, len) < 0) {}
                    if (pa.ext) pa.ext->stop(); if (pb.ext) pb.ext->stop();
                    _exit(0);
                }
                running++;
            }
            int st; pid_t w = waitpid(-1, &st, 0); (void)w; running--;
            drain();
        }
        close(pfd[1]); drain();
        int W = 0, D = 0, L = 0, inst = 0, instW = 0; long margin = 0; double maxms = 0, avg = 0; vector<int> lost;
        double sh[9] = {0,0,0,0,0,0,0,0,0};
        for (auto& l : lines) {
            int seed, aY, aw, turns, as_, os; char reason[32]; double mx, av; int x[9];
            if (sscanf(l.c_str(), "%d %d %d %31s %d %d %d %lf %lf %d %d %d %d %d %d %d %d %d", &seed, &aY, &aw, reason, &turns, &as_, &os, &mx, &av, x, x+1, x+2, x+3, x+4, x+5, x+6, x+7, x+8) != 18) continue;
            for (int q = 0; q < 9; q++) sh[q] += x[q];
            if (verbose) printf("  %s\n", l.c_str());
            if (aw > 0) { W++; if (!strcmp(reason, "instant")) instW++; } else if (aw < 0) { L++; lost.push_back(seed); if (!strcmp(reason, "instant")) inst++; } else D++;
            margin += as_ - os; maxms = max(maxms, mx); avg += av;
        }
        int tot = W + D + L;
        printf("%s vs %s: %dW %dD %dL /%d winrate=%.3f margin=%.2f wipeoutLoss=%d wipeoutWin=%d maxms=%.1f avgms=%.2f\n", a.c_str(), opp.c_str(), W, D, L, tot,
               tot ? (W + 0.5 * D) / tot : 0.0, tot ? (double)margin / tot : 0.0, inst, instW, maxms, tot ? avg / tot : 0.0);
        if (tot) printf("   avg: hallT me/op %.0f/%.0f engT me/op %.0f/%.0f Wdiff@40/80/120 %.1f/%.1f/%.1f Pdiff@80/120 %.1f/%.1f\n", sh[0]/tot, sh[1]/tot, sh[2]/tot, sh[3]/tot, sh[4]/tot, sh[5]/tot, sh[6]/tot, sh[7]/tot, sh[8]/tot);
        if (!lost.empty() && lost.size() <= 60) { printf("   lost:"); sort(lost.begin(), lost.end()); for (int s : lost) printf(" %d", s); printf("\n"); }
        fflush(stdout);
    }
    return 0;
}

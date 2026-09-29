// duel.cpp - in-process match harness (dev seeds only). Uses the exact engine (engine.hpp) validated
// against the reference python engine. Runs bots as C++ objects, many games in parallel threads.
//   ./duel --me blotto --opp v17 --seeds 10000 40 --threads 4 [--side Y|K|both] [--maps harness/maps.txt]
#include "../blotto.hpp"
#include "../v15.hpp"
#include "../v16.hpp"
#include <atomic>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
static double cpuMs() { timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }

using namespace std;

struct MapData {
    int seed; int by[2]; vector<string> terr; struct B { int id, x, y; string type; int score; }; vector<B> bs;
};

static vector<MapData> loadMaps(const string& path) {
    vector<MapData> out; ifstream in(path); string line;
    MapData cur; bool have = false;
    while (getline(in, line)) {
        istringstream ss(line); string tag; ss >> tag;
        if (tag == "SEED") { if (have) out.push_back(cur); cur = MapData(); have = true; ss >> cur.seed; }
        else if (tag == "BASES") { int a, b, c, d; ss >> a >> b >> c >> d; cur.by[0] = b * 15 + a; cur.by[1] = d * 15 + c; }
        else if (tag == "T") { string r; ss >> r; cur.terr.push_back(r); }
        else if (tag == "B") { }
        else { int id, x, y, sc; string ty; istringstream s2(line); s2 >> id >> x >> y >> ty >> sc; cur.bs.push_back({id, x, y, ty, sc}); }
    }
    if (have) out.push_back(cur);
    return out;
}

using DecideFn = function<vector<string>(const p::View&, const p::Init&)>;

struct BotHolder {
    shared_ptr<void> obj; DecideFn fn;
};


// ---- subprocess bot (text protocol), for external binaries such as bot_opp
struct SubBot {
    pid_t pid = -1; int toChild = -1, fromChild = -1; string buf;
    bool start(const string& path) {
        int a[2], b[2]; if (pipe(a) || pipe(b)) return false;
        pid = fork();
        if (pid == 0) { dup2(a[0], 0); dup2(b[1], 1); close(a[0]); close(a[1]); close(b[0]); close(b[1]); int dn = open("/dev/null", 1); if (dn >= 0) dup2(dn, 2); execl(path.c_str(), path.c_str(), (char*)nullptr); _exit(127); }
        close(a[0]); close(b[1]); toChild = a[1]; fromChild = b[0]; return pid > 0;
    }
    void send(const string& s) { size_t off = 0; while (off < s.size()) { ssize_t n = write(toChild, s.data() + off, s.size() - off); if (n <= 0) return; off += n; } }
    vector<string> readUntilEnd() {
        vector<string> lines; 
        for (;;) {
            size_t pos = buf.find('\n');
            if (pos == string::npos) { char tmp[4096]; ssize_t n = read(fromChild, tmp, sizeof(tmp)); if (n <= 0) return lines; buf.append(tmp, n); continue; }
            string line = buf.substr(0, pos); buf.erase(0, pos + 1);
            if (line == "END") return lines;
            lines.push_back(line);
        }
    }
    ~SubBot() { if (toChild >= 0) close(toChild); if (fromChild >= 0) close(fromChild); if (pid > 0) { kill(pid, SIGKILL); int st; waitpid(pid, &st, 0); } }
};
static string serInit(const p::Init& I) {
    ostringstream o; o << "INIT " << I.width << " " << I.height << "\nTEAM " << I.team << "\n";
    for (auto& r : I.terrain) o << "MAP " << r << "\n";
    o << "BUILDINGS " << I.buildings.size() << "\n";
    for (auto& b : I.buildings) o << b.id << " " << b.x << " " << b.y << " " << b.type << "\n";
    o << "BASE Y " << I.bases[0].first << " " << I.bases[0].second << "\nBASE K " << I.bases[1].first << " " << I.bases[1].second << "\nEND\n";
    return o.str();
}
static string serTurn(const p::View& v) {
    ostringstream o; o << "TURN " << v.turn << "\nRESOURCE " << v.my_resource << " " << v.opp_resource << "\nUNITS " << v.units.size() << "\n";
    for (auto& u : v.units) o << u.team << " " << u.kind << " " << u.x << " " << u.y << " " << u.count << "\n";
    o << "BUILDINGS " << v.buildings.size() << "\n";
    for (auto& b : v.buildings) o << b.id << " " << b.x << " " << b.y << " " << b.type << " " << b.owner << " " << b.stage << " " << b.score << "\n";
    o << "END\n"; return o.str();
}

static BotHolder makeBot(const string& name) {
    if (!name.empty() && (name[0] == '.' || name[0] == '/')) {
        auto sb = make_shared<SubBot>(); if (!sb->start(name)) { fprintf(stderr, "cannot start %s\n", name.c_str()); exit(2); }
        auto inited = make_shared<bool>(false);
        BotHolder h; h.obj = sb;
        h.fn = [sb, inited](const p::View& v, const p::Init& i) { if (!*inited) { *inited = true; sb->send(serInit(i)); } sb->send(serTurn(v)); return sb->readUntilEnd(); };
        return h;
    }
    BotHolder h;
    if (name == "blotto") { auto b = make_shared<blotto::Bot>(); h.obj = b; h.fn = [b](const p::View& v, const p::Init& i) { return b->decide(v, i); }; }
    else if (name == "v17") { auto b = make_shared<v17::Planner>(); h.obj = b; h.fn = [b](const p::View& v, const p::Init& i) { return b->decide(v, i); }; }
    else if (name == "v16") { auto b = make_shared<v16::Planner>(); h.obj = b; h.fn = [b](const p::View& v, const p::Init& i) { return b->decide(v, i); }; }
    else if (name == "v15") { auto b = make_shared<v15::Planner>(); h.obj = b; h.fn = [b](const p::View& v, const p::Init& i) { return b->decide(v, i); }; }
    else { fprintf(stderr, "unknown bot %s\n", name.c_str()); exit(2); }
    return h;
}

struct Result { int seed; char side; int winner; string reason; int myScore, oppScore, turns; double maxMs, sumMs; int nMs; };

static void parseOrders(const eng::Map& M, const vector<string>& cmds, eng::Orders& o) {
    o.clear();
    for (auto& c : cmds) {
        istringstream ss(c); string tag; ss >> tag;
        if (tag == "SPAWN") {
            vector<string> tk; string t; while (ss >> t) tk.push_back(t);
            if (tk.size() != 2 && tk.size() != 4) continue;
            if (tk[0] != "F" && tk[0] != "W" && tk[0] != "S") continue;
            int n = atoi(tk[1].c_str()); if (n <= 0) continue;
            if (tk[0] == "S") continue;
            if (tk.size() == 4) { int x = atoi(tk[2].c_str()), y = atoi(tk[3].c_str()); o.addSpawn(tk[0] == "F" ? 0 : 1, n, y * 15 + x); }
            else o.addSpawn(tk[0] == "F" ? 0 : 1, n, -1);
        } else if (tag == "MOVE") {
            vector<string> tk; string t; while (ss >> t) tk.push_back(t);
            if (tk.size() != 5) continue;
            int x = atoi(tk[0].c_str()), y = atoi(tk[1].c_str()); int n = atoi(tk[3].c_str());
            if (tk[2] == "S") continue; if (n <= 0) continue;
            int d = tk[4] == "U" ? 0 : tk[4] == "D" ? 1 : tk[4] == "L" ? 2 : tk[4] == "R" ? 3 : -1; if (d < 0) continue;
            if (x < 0 || x >= 15 || y < 0 || y >= 15) continue;
            o.addMove(y * 15 + x, tk[2] == "F" ? 0 : 1, d, n);
        } else if (tag == "TELE") {
            vector<string> tk; string t; while (ss >> t) tk.push_back(t);
            if (tk.size() != 6) continue;
            int x = atoi(tk[0].c_str()), y = atoi(tk[1].c_str()), n = atoi(tk[3].c_str()), tx = atoi(tk[4].c_str()), ty = atoi(tk[5].c_str());
            if (tk[2] == "S" || n <= 0) continue;
            o.addTele(y * 15 + x, tk[2] == "F" ? 0 : 1, n, ty * 15 + tx);
        } else if (tag == "PRIORITY") {
            vector<int> v; int a; while (ss >> a) v.push_back(a);
            if (v.size() % 2) continue;
            o.npr = 0;
            for (size_t i = 0; i + 1 < v.size(); i += 2) { int b = (v[i + 1] >= 0 && v[i + 1] < 15 && v[i] >= 0 && v[i] < 15) ? M.bat[v[i + 1] * 15 + v[i]] : -1; if (b >= 0) o.addPrio(b); }
        }
    }
}

static Result playGame(const MapData& md, const string& meName, const string& oppName, bool meIsY) {
    using namespace eng;
    Map GM; memset(&GM, 0, sizeof(GM));
    for (int y = 0; y < 15; y++) for (int x = 0; x < 15; x++) GM.pass[y * 15 + x] = md.terr[y][x] != '#';
    GM.nb = (int)md.bs.size();
    int total2 = 0;
    for (int i = 0; i < GM.nb; i++) { auto& b = md.bs[i]; GM.bcell[i] = b.y * 15 + b.x; GM.btype[i] = btypeFromStr(b.type); GM.bid[i] = b.id; GM.score2[i] = 2 * b.score; total2 += 2 * b.score; }
    GM.base[0] = md.by[0]; GM.base[1] = md.by[1];
    GM.finish();
    StepCfg cfg; cfg.homeFlip[0] = (GM.base[0] % 15) * 2 > 14; cfg.homeFlip[1] = (GM.base[1] % 15) * 2 > 14;
    State st; st.clear(); st.res[0] = st.res[1] = 10;
    p::Init inits[2];
    const char* tn[2] = {"Y", "K"};
    for (int t = 0; t < 2; t++) {
        p::Init& I = inits[t]; I.width = I.height = 15; I.team = tn[t]; I.opp = tn[1 - t]; I.terrain = md.terr;
        for (auto& b : md.bs) { p::Building pb; pb.id = b.id; pb.x = b.x; pb.y = b.y; pb.type = b.type; I.buildings.push_back(pb); }
        I.bases[0] = {GM.base[0] % 15, GM.base[0] / 15}; I.bases[1] = {GM.base[1] % 15, GM.base[1] / 15};
    }
    BotHolder bots[2] = {makeBot(meIsY ? meName : oppName), makeBot(meIsY ? oppName : meName)};
    uint32_t rev[2] = {0, 0};
    Result R; R.seed = md.seed; R.side = meIsY ? 'Y' : 'K'; R.maxMs = 0; R.sumMs = 0; R.nMs = 0;
    int me = meIsY ? 0 : 1;
    int winner = -1; string reason;
    for (int turn = 1; turn <= 160; turn++) {
        Orders O[2];
        for (int t = 0; t < 2; t++) {
            p::View v; v.turn = turn; v.my_resource = st.res[t]; v.opp_resource = st.res[1 - t];
            for (int tm = 0; tm < 2; tm++) for (int k = 0; k < 2; k++) for (int c = 0; c < NC; c++) {
                int n = k == 0 ? st.F[tm][c] : st.W[tm][c];
                // order: (team Y->K, kind F->W, y, x): c = y*15+x ascending
                if (n > 0) { p::Unit u; u.team = tn[tm]; u.kind = k == 0 ? "F" : "W"; u.x = c % 15; u.y = c / 15; u.count = n; v.units.push_back(u); }
            }
            // unit order fix: need team-major, then kind, then y,x  (loops above are tm,k,c) -> correct
            for (int b = 0; b < GM.nb; b++) {
                p::Building pb; pb.id = GM.bid[b]; pb.x = GM.bcell[b] % 15; pb.y = GM.bcell[b] / 15; pb.type = md.bs[b].type;
                pb.owner = st.owner[b] < 0 ? "N" : tn[st.owner[b]]; pb.stage = st.owner[b] < 0 ? 0 : 2;
                pb.score = ((rev[t] >> b) & 1) ? md.bs[b].score : -1;
                v.buildings.push_back(pb);
            }
            double t0 = cpuMs();
            vector<string> cmds = bots[t].fn(v, inits[t]);
            double dt = cpuMs() - t0;
            if (t == me) { R.maxMs = max(R.maxMs, dt); R.sumMs += dt; R.nMs++; }
            parseOrders(GM, cmds, O[t]);
        }
        step(GM, st, O[0], O[1], cfg);
        // reveal (step 8)
        for (int t = 0; t < 2; t++) for (int b = 0; b < GM.nb; b++) {
            if ((rev[t] >> b) & 1) continue;
            int c = GM.bcell[b]; bool r = st.owner[b] == t || (st.F[t][c] | st.W[t][c]) != 0;
            if (!r) for (int w = 0; w < GM.nb && !r; w++) if (GM.btype[w] == T_WATCH && st.owner[w] == t) {
                int dx = abs(GM.bcell[w] % 15 - c % 15), dy = abs(GM.bcell[w] / 15 - c / 15); if (max(dx, dy) <= 3) r = true; }
            if (r) rev[t] |= 1u << b;
        }
        int s0 = score2(GM, st, 0), s1 = score2(GM, st, 1);
        if (s1 == 0 && s0 * 2 > total2) { winner = 0; reason = "instant"; R.turns = turn; break; }
        if (s0 == 0 && s1 * 2 > total2) { winner = 1; reason = "instant"; R.turns = turn; break; }
        if (turn == 160) {
            R.turns = turn;
            if (s0 != s1) { winner = s0 > s1 ? 0 : 1; reason = "score"; }
            else if (st.occ[0] != st.occ[1]) { winner = st.occ[0] > st.occ[1] ? 0 : 1; reason = "occ"; }
            else {
                int u0 = 0, u1 = 0; for (int c = 0; c < NC; c++) { u0 += st.F[0][c] * 5 + st.W[0][c] * 3; u1 += st.F[1][c] * 5 + st.W[1][c] * 3; }
                if (u0 != u1) { winner = u0 > u1 ? 0 : 1; reason = "units"; } else { winner = 2; reason = "draw"; }
            }
        }
    }
    R.winner = winner == 2 ? 2 : (winner == me ? 1 : 0);   // 1 = me wins
    R.reason = reason;
    R.myScore = score2(GM, st, me) / 2; R.oppScore = score2(GM, st, 1 - me) / 2;
    return R;
}

int main(int argc, char** argv) {
    string meName = "blotto", oppName = "v17", mapsPath = "harness/maps.txt", side = "Y";
    int lo = 10000, n = 40, threads = 4; bool verbose = false;
    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "--me") meName = argv[++i]; else if (a == "--opp") oppName = argv[++i];
        else if (a == "--seeds") { lo = atoi(argv[++i]); n = atoi(argv[++i]); }
        else if (a == "--threads") threads = atoi(argv[++i]); else if (a == "--side") side = argv[++i];
        else if (a == "--maps") mapsPath = argv[++i]; else if (a == "-v") verbose = true;
    }
    if (lo < 10000 || lo + n > 10200) { fprintf(stderr, "dev seeds only (10000..10199)\n"); return 2; }
    auto maps = loadMaps(mapsPath);
    vector<pair<int,bool>> jobs;
    for (int s = lo; s < lo + n; s++) { if (side == "Y" || side == "both") jobs.push_back({s, true}); if (side == "K" || side == "both") jobs.push_back({s, false}); }
    vector<Result> res(jobs.size());
    atomic<int> next(0);
    auto worker = [&]() {
        for (;;) {
            int j = next++; if (j >= (int)jobs.size()) break;
            const MapData* md = nullptr; for (auto& m : maps) if (m.seed == jobs[j].first) md = &m;
            if (!md) { fprintf(stderr, "no map %d\n", jobs[j].first); exit(2); }
            res[j] = playGame(*md, meName, oppName, jobs[j].second);
        }
    };
    vector<thread> th; for (int i = 0; i < threads; i++) th.emplace_back(worker);
    for (auto& t : th) t.join();
    int w = 0, d = 0, l = 0, inst = 0; double margin = 0, mx = 0, avg = 0; int na = 0;
    for (auto& r : res) {
        if (r.winner == 1) w++; else if (r.winner == 2) d++; else { l++; if (r.reason == "instant") inst++; }
        margin += r.myScore - r.oppScore; mx = max(mx, r.maxMs); avg += r.sumMs; na += r.nMs;
        if (verbose) printf("seed %d %c: %s %s %d-%d turns %d\n", r.seed, r.side, r.winner == 1 ? "WIN" : r.winner == 2 ? "DRAW" : "LOSS", r.reason.c_str(), r.myScore, r.oppScore, r.turns);
    }
    printf("%s vs %s: %dW %dD %dL /%d wr=%.3f margin=%+.1f annih=%d  maxMs=%.1f avgMs=%.2f\n", meName.c_str(), oppName.c_str(), w, d, l, (int)res.size(), (w + 0.5 * d) / res.size(), margin / res.size(), inst, mx, avg / max(1, na));
    printf("lost:");
    for (auto& r : res) if (r.winner == 0) printf(" %d%c(%d-%d%s)", r.seed, r.side, r.myScore, r.oppScore, r.reason == "instant" ? "!" : "");
    printf("\n");
    return 0;
}

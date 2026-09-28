#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

namespace bot {

constexpr int CELLS = 225;
constexpr int INF = 1000000;

struct Building {
    int id = 0, pos = 0;
    string type, owner = "N";
    int stage = 0, score = -1;
};

struct Task {
    int pos;
    double value, need, allocated = 0;
};

struct Order {
    int src, dst, count;
};

class Agent {
    int width = 15, height = 15, size = 225;
    int turn = 1, money = 0, enemyMoney = 0;
    string me = "Y", enemy = "K";

    array<int, 2> base{0, 224};
    array<bool, CELLS> pass{};
    array<vector<int>, CELLS> adj;
    array<array<int, CELLS>, CELLS> dist{};
    array<int, CELLS> buildingAt{};

    // [side: us/them][kind: F/W/S][cell]
    int units[2][3][CELLS]{};

    vector<Building> buildings;
    vector<int> known;
    vector<bool> depotClaimed;

    vector<string> output;

    int cell(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height)
            return -1;
        return y * width + x;
    }

    int home() const { return base[me == "Y" ? 0 : 1]; }
    int enemyHome() const { return base[me == "Y" ? 1 : 0]; }

    int key(int p) const {
        return me == "Y" ? p : size - 1 - p;
    }

    int kind(const string& s) const {
        return s == "F" ? 0 : s == "W" ? 1 : 2;
    }

    bool owns(const string& type, const string& team) const {
        for (const auto& b : buildings)
            if (b.owner == team && b.type == type) return true;
        return false;
    }

    int income(const string& team) const {
        int result = 10;
        for (const auto& b : buildings)
            if (b.owner == team && b.type == "HALL") result += 2;
        return result;
    }

    vector<int> sources(const string& team) const {
        vector<int> result{team == me ? home() : enemyHome()};
        for (const auto& b : buildings)
            if (b.owner == team && b.type == "HOSPITAL")
                result.push_back(b.pos);
        return result;
    }

    double points(int b) const {
        if (known[b] >= 0) return known[b];
        int x = buildings[b].pos % width;
        return x >= 5 && x <= 9 ? 3.0 : 1.5;
    }

    double value(int b) const {
        const auto& z = buildings[b];
        double horizon = min(1.0, max(0.0, (160 - turn) / 55.0));
        double v = 10 * points(b);

        if (z.type == "HALL") v += 30 * horizon;
        if (z.type == "ENG")
            v += (owns("ENG", me) ? 10 : 42) * horizon;
        if (z.type == "HOSPITAL") {
            int nearest = INF;
            for (int p : sources(me))
                nearest = min(nearest, dist[p][z.pos]);
            v += (20 + min(18, nearest * 2)) * horizon;
        }
        if (z.type == "LIBRARY")
            v += (owns("LIBRARY", me) ? 2 : 9) * horizon;
        if (z.type == "DEPOT" && !depotClaimed[b])
            v += 14 * horizon;
        if (z.type == "STATION") v += 3 * horizon;

        if (z.owner == enemy) v *= 1.35;
        if (turn > 135) v += points(b) * 8;
        return v;
    }

    vector<int> options(int p) const {
        vector<int> out{p}; // Staying must be a real candidate.
        for (int q : adj[p]) out.push_back(q);
        sort(out.begin(), out.end(), [&](int a, int b) {
            return key(a) < key(b);
        });
        return out;
    }

    void move(int p, int q, int k, int n) {
        if (p == q || n <= 0) return;
        string direction;
        if (q == p - width) direction = "U";
        else if (q == p + width) direction = "D";
        else if (q == p - 1) direction = "L";
        else if (q == p + 1) direction = "R";
        else return;

        static const char* kinds[] = {"F", "W", "S"};
        output.push_back(
            "MOVE " + to_string(p % width) + " " +
            to_string(p / width) + " " + kinds[k] + " " +
            to_string(n) + " " + direction);
    }

    void spawn(int p, int k, int n, int cost) {
        n = min(n, money / cost);
        if (n <= 0) return;

        static const char* kinds[] = {"F", "W", "S"};
        string command =
            "SPAWN " + string(kinds[k]) + " " + to_string(n);

        // Explicit coordinates are valid only for hospitals.
        if (p != home())
            command += " " + to_string(p % width) +
                       " " + to_string(p / width);

        output.push_back(command);
        money -= n * cost;
        units[0][k][p] += n;
    }

public:
    bool initialize(const vector<string>& lines) {
        pass.fill(false);
        buildingAt.fill(-1);
        int row = 0;

        for (size_t i = 0; i < lines.size(); ++i) {
            istringstream in(lines[i]);
            string tag;
            in >> tag;

            if (tag == "INIT") {
                in >> width >> height;
                if (width != 15 || height != 15) return false;
                size = width * height;
            } else if (tag == "TEAM") {
                in >> me;
                enemy = me == "Y" ? "K" : "Y";
            } else if (tag == "MAP") {
                string terrain;
                in >> terrain;
                // Support both "MAP <row>" and a standalone MAP header.
                if (terrain.empty()) {
                    for (int y = 0; y < height; ++y) {
                        if (++i >= lines.size()) return false;
                        istringstream r(lines[i]);
                        string text;
                        r >> text;
                        if (text == "MAP") r >> text;
                        if ((int)text.size() != width) return false;
                        for (int x = 0; x < width; ++x)
                            pass[y * width + x] = text[x] != '#';
                    }
                    row = height;
                } else {
                    if (row >= height || (int)terrain.size() != width)
                        return false;
                    for (int x = 0; x < width; ++x)
                        pass[row * width + x] = terrain[x] != '#';
                    ++row;
                }
            } else if (tag == "BASE") {
                string team;
                int x, y;
                if (!(in >> team >> x >> y)) return false;
                int p = cell(x, y);
                if (p < 0) return false;
                base[team == "Y" ? 0 : 1] = p;
            } else if (tag == "BUILDINGS") {
                int count;
                in >> count;
                for (int j = 0; j < count; ++j) {
                    if (++i >= lines.size()) return false;
                    istringstream r(lines[i]);
                    Building b;
                    int x, y;
                    if (!(r >> b.id >> x >> y >> b.type)) return false;
                    b.pos = cell(x, y);
                    if (b.pos < 0) return false;
                    buildings.push_back(b);
                }
            }
        }

        if (row != height || buildings.empty()) return false;

        known.assign(buildings.size(), -1);
        depotClaimed.assign(buildings.size(), false);
        for (int b = 0; b < (int)buildings.size(); ++b) {
            buildingAt[buildings[b].pos] = b;
            if (buildings[b].type == "PLAZA") known[b] = 3;
        }

        constexpr int dx[] = {0, 0, -1, 1};
        constexpr int dy[] = {-1, 1, 0, 0};

        for (int p = 0; p < size; ++p) {
            if (!pass[p]) continue;
            for (int d = 0; d < 4; ++d) {
                int q = cell(p % width + dx[d], p / width + dy[d]);
                if (q >= 0 && pass[q]) adj[p].push_back(q);
            }
        }

        for (int src = 0; src < size; ++src) {
            dist[src].fill(INF);
            if (!pass[src]) continue;
            queue<int> q;
            q.push(src);
            dist[src][src] = 0;

            while (!q.empty()) {
                int p = q.front();
                q.pop();
                for (int next : adj[p]) {
                    if (dist[src][next] != INF) continue;
                    dist[src][next] = dist[src][p] + 1;
                    q.push(next);
                }
            }
        }
        return true;
    }

    void update(const vector<string>& lines) {
        for (auto& side : units)
            for (auto& k : side)
                fill(begin(k), end(k), 0);

        for (size_t i = 0; i < lines.size(); ++i) {
            istringstream in(lines[i]);
            string tag;
            in >> tag;

            if (tag == "TURN") in >> turn;
            else if (tag == "RESOURCE") in >> money >> enemyMoney;
            else if (tag == "UNITS") {
                int count;
                in >> count;
                for (int j = 0; j < count && i + 1 < lines.size(); ++j) {
                    istringstream r(lines[++i]);
                    string team, k;
                    int x, y, n;
                    if (!(r >> team >> k >> x >> y >> n)) continue;
                    int p = cell(x, y);
                    if (p >= 0 && n > 0)
                        units[team == me ? 0 : 1][kind(k)][p] += n;
                }
            } else if (tag == "BUILDINGS") {
                int count;
                in >> count;
                for (int j = 0; j < count && i + 1 < lines.size(); ++j) {
                    istringstream r(lines[++i]);
                    int id, x, y, stage, score;
                    string type, owner;
                    if (!(r >> id >> x >> y >> type >> owner
                            >> stage >> score)) continue;
                    int p = cell(x, y);
                    int b = p < 0 ? -1 : buildingAt[p];
                    if (b < 0) continue;

                    buildings[b].owner = owner;
                    buildings[b].stage = stage;
                    buildings[b].score = score;
                    if (score >= 0) known[b] = score;
                    if (owner == me && type == "DEPOT")
                        depotClaimed[b] = true;
                }
            }
        }

        // Point symmetry inference; no assumption that IDs are contiguous.
        for (int b = 0; b < (int)buildings.size(); ++b) {
            int mirror = buildingAt[size - 1 - buildings[b].pos];
            if (known[b] < 0 && mirror >= 0 && known[mirror] >= 0)
                known[b] = known[mirror];
        }
    }

    vector<string> decide() {
        output.clear();
        const int nb = (int)buildings.size();
        const int remaining = max(0, 161 - turn);

        auto ownSources = sources(me);
        auto oppSources = sources(enemy);
        const int wc = owns("ENG", me) ? 2 : 3;
        const int enemyWC = owns("ENG", enemy) ? 2 : 3;

        array<int, CELLS> danger{}, nearDanger{};
        array<int, CELLS> finalW{}, assignedFlags{};
        vector<int> incoming(nb, 0);

        for (int p = 0; p < size; ++p) {
            if (!pass[p]) continue;
            for (int q = 0; q < size; ++q) {
                if (dist[p][q] <= 1) danger[p] += units[1][1][q];
                if (dist[p][q] <= 2) nearDanger[p] += units[1][1][q];
            }
            // This is a local upper bound, not a prediction that the enemy
            // spends the same budget at every production source.
            int nearest = INF;
            for (int q : oppSources) nearest = min(nearest, dist[p][q]);
            if (nearest <= 1) danger[p] += enemyMoney / enemyWC;
            if (nearest <= 2) nearDanger[p] += enemyMoney / enemyWC;
        }

        auto chooseFlagTarget = [&](int p, bool useIncoming) {
            int best = -1;
            double bestScore = -1e100;

            for (int b = 0; b < nb; ++b) {
                const auto& z = buildings[b];
                if (z.owner == me) continue;
                int d = dist[p][z.pos];
                int extra = z.owner == enemy ? 1 : 0;
                if (d >= INF || max(1, d) + extra > remaining) continue;

                double score = value(b) / (d + 2.0);
                if (useIncoming) score /= 1.0 + 3.5 * incoming[b];
                score -= 0.12 * danger[z.pos] / (d + 1.0);
                if (p == z.pos) score += 20;
                if (score > bestScore) {
                    bestScore = score;
                    best = b;
                }
            }
            return best;
        };

        // Estimate flag coverage before buying replacements.
        int totalF = 0;
        vector<int> occupied;
        for (int p = 0; p < size; ++p) {
            totalF += units[0][0][p];
            if (units[0][0][p] > 0) occupied.push_back(p);
        }
        sort(occupied.begin(), occupied.end(), [&](int a, int b) {
            return key(a) < key(b);
        });

        for (int p : occupied) {
            for (int n = 0; n < min(20, units[0][0][p]); ++n) {
                int b = chooseFlagTarget(p, true);
                if (b >= 0) ++incoming[b];
            }
        }

        // Reserve only capture spending not covered by this turn's income.
        int captureBudget = 0;
        bool library = owns("LIBRARY", me);
        for (int b = 0; b < nb; ++b) {
            int p = buildings[b].pos;
            bool flagNear = units[0][0][p] > 0;
            for (int q : adj[p]) flagNear |= units[0][0][q] > 0;
            if (flagNear && buildings[b].owner != me)
                captureBudget += library ? 1 :
                    buildings[b].type == "PLAZA" ? 4 : 2;
        }
        int reserve = max(0, captureBudget - income(me));

        int desiredF = turn < 4 ? 2 : turn < 12 ? 4 : 6;
        if (turn > 135) desiredF = 4;
        int buyLimit = turn <= 2 ? 2 : 1;

        while (buyLimit-- > 0 && totalF < desiredF &&
               money >= 5 + reserve) {
            int src = -1, target = -1;
            double best = -1e100;

            for (int p : ownSources) {
                int b = chooseFlagTarget(p, true);
                if (b < 0) continue;
                double score = value(b) /
                    (dist[p][buildings[b].pos] + 2.0) /
                    (1.0 + 3.5 * incoming[b]);

                // Avoid producing exposed flags into an overwhelmed source.
                score -= 4 * max(0, danger[p] - units[0][1][p]);
                if (score > best) {
                    best = score;
                    src = p;
                    target = b;
                }
            }
            if (src < 0 || best <= 0) break;
            spawn(src, 0, 1, 5);
            ++totalF;
            ++incoming[target];
        }

        // Plan flag destinations first, then allocate escorts.
        fill(incoming.begin(), incoming.end(), 0);
        vector<pair<int, int>> flags;
        vector<Task> tasks;

        for (int b = 0; b < nb; ++b) {
            int p = buildings[b].pos;
            int enemyFlagDistance = INF;
            for (int q = 0; q < size; ++q)
                if (units[1][0][q] > 0)
                    enemyFlagDistance = min(enemyFlagDistance, dist[p][q]);

            if (buildings[b].owner != me) {
                tasks.push_back({
                    p, value(b) * 0.65,
                    max(2.0, danger[p] + 1.0), 0
                });
            } else if (enemyFlagDistance <= 5 ||
                       (units[0][0][p] && nearDanger[p])) {
                tasks.push_back({
                    p, value(b) * (enemyFlagDistance <= 2 ? 1.9 : 0.9),
                    max(1.0, danger[p] + 1.0), 0
                });
            }
        }

        for (int p = 0; p < size; ++p) {
            int count = units[0][0][p];
            // Keep runtime bounded even in artificially large states.
            for (int n = 0; n < min(count, 32); ++n) {
                int b = chooseFlagTarget(p, true);
                flags.push_back({p, b});
                if (b < 0) continue;
                ++incoming[b];

                int target = buildings[b].pos;
                int step = p;
                for (int q : options(p))
                    if (dist[q][target] < dist[step][target])
                        step = q;

                double urgency = 38 + value(b) * 0.45;
                tasks.push_back({
                    step, urgency,
                    max(1.0, danger[step] + 1.0), 0
                });
            }
        }

        // Intercept enemy flags, including flags away from buildings.
        for (int p = 0; p < size; ++p) {
            if (units[1][0][p] <= 0) continue;
            tasks.push_back({
                p, 32.0 + min(4, units[1][0][p]) * 5,
                max(1.0, units[1][1][p] + 1.0), 0
            });
        }

        if (tasks.empty()) {
            for (int b = 0; b < nb; ++b)
                tasks.push_back({buildings[b].pos, value(b), 2, 0});
        }

        // Choose production sites by time-to-task, rather than home only.
        vector<int> bought(size, 0);
        int purchases = max(0, (money - reserve) / wc);
        for (int n = 0; n < purchases; ++n) {
            int bestSource = ownSources.front();
            double best = -1e100;

            for (int p : ownSources) {
                double score = 0;
                for (const auto& task : tasks) {
                    if (dist[p][task.pos] >= remaining) continue;
                    score = max(score,
                        task.value / (dist[p][task.pos] + 2.0));
                }
                score /= 1.0 + 0.08 * bought[p];
                if (units[0][0][p] > 0 &&
                    units[0][1][p] < danger[p])
                    score += 30;

                if (score > best) {
                    best = score;
                    bestSource = p;
                }
            }
            spawn(bestSource, 1, 1, wc);
            ++bought[bestSource];
        }

        // Process threatened/engaged groups first.
        vector<int> armies;
        for (int p = 0; p < size; ++p)
            if (units[0][1][p] > 0) armies.push_back(p);

        sort(armies.begin(), armies.end(), [&](int a, int b) {
            double va = nearDanger[a] + 5 * (units[0][0][a] > 0);
            double vb = nearDanger[b] + 5 * (units[0][0][b] > 0);
            if (va != vb) return va > vb;
            return key(a) < key(b);
        });

        // finalW contains only committed arrivals/stays:
        // unplanned armies are not treated as guaranteed support.
        vector<Order> warriorOrders;
        for (int p : armies) {
            int left = units[0][1][p];
            while (left > 0) {
                int n = nearDanger[p] > 0 ? left : min(left, 3);

                int bestTask = 0;
                double bestTaskScore = -1e100;
                for (int t = 0; t < (int)tasks.size(); ++t) {
                    const auto& task = tasks[t];
                    double saturation = task.allocated / max(1.0, task.need);
                    double score = task.value /
                        (dist[p][task.pos] + 2.0) /
                        (1.0 + 1.8 * saturation * saturation);
                    if (score > bestTaskScore) {
                        bestTaskScore = score;
                        bestTask = t;
                    }
                }

                auto& task = tasks[bestTask];
                int dest = p;
                double best = -1e100;
                for (int q : options(p)) {
                    int support = finalW[q] + n;
                    int deficit = max(0, danger[q] - support);
                    double score =
                        -3.8 * dist[q][task.pos]
                        -4.5 * min(n, deficit);

                    if (q == task.pos) score += 3;
                    if (q == p) score += 0.12;
                    if (units[0][0][q] > 0 && danger[q] > 0 &&
                        support >= danger[q])
                        score += 12;
                    if (support > units[1][1][q] &&
                        units[1][0][q] + units[1][2][q] > 0)
                        score += 7;
                    score += 0.04 * min(20, finalW[q]);

                    if (score > best) {
                        best = score;
                        dest = q;
                    }
                }

                finalW[dest] += n;
                task.allocated += n /
                    (1.0 + 0.12 * dist[dest][task.pos]);
                warriorOrders.push_back({p, dest, n});
                left -= n;
            }
        }

        for (const auto& order : warriorOrders)
            move(order.src, order.dst, 1, order.count);

        // Flags are never locked into suicidal capture attempts.
        for (auto [p, b] : flags) {
            int dest = p;
            double best = -1e100;

            for (int q : options(p)) {
                int deficit = max(0, danger[q] - finalW[q]);
                double score = -10000.0 * (deficit > 0) - 30 * deficit;

                if (b >= 0) {
                    int target = buildings[b].pos;
                    score -= 4.5 * dist[q][target];
                    if (q == target) score += 12;
                } else {
                    // With no capture target, preserve flags in safe cells.
                    score -= 0.1 * dist[q][home()];
                }

                score -= 0.15 * max(0, nearDanger[q] - finalW[q]);
                score -= 2 * assignedFlags[q];
                if (q == p) score += 0.15;

                int at = buildingAt[q];
                if (at >= 0 && buildings[at].owner == me &&
                    danger[q] > finalW[q])
                    score -= 40; // Avoid having an owned flag pulled.

                if (score > best) {
                    best = score;
                    dest = q;
                }
            }
            ++assignedFlags[dest];
            move(p, dest, 0, 1);
        }

        // Surplus flags beyond the planning cap remain at their source.
        // Scouts are intentionally not purchased: all units are already
        // visible; spending is focused on capture and combat.

        vector<int> priority(nb);
        iota(priority.begin(), priority.end(), 0);
        sort(priority.begin(), priority.end(), [&](int a, int b) {
            double va = value(a), vb = value(b);
            if (va != vb) return va > vb;
            return key(buildings[a].pos) < key(buildings[b].pos);
        });

        string command = "PRIORITY";
        for (int b : priority) {
            int p = buildings[b].pos;
            command += " " + to_string(p % width) +
                       " " + to_string(p / width);
        }
        output.push_back(command);
        return output;
    }
};

bool readBlock(vector<string>& lines) {
    lines.clear();
    string line;
    while (getline(cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "END") return true;
        if (!line.empty()) lines.push_back(line);
    }
    return false;
}

} // namespace bot

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // Heap allocation avoids placing the distance table on a small stack.
    auto agent = new bot::Agent;
    vector<string> lines;

    if (!bot::readBlock(lines) || !agent->initialize(lines)) {
        delete agent;
        return 0;
    }

    while (bot::readBlock(lines)) {
        bool isTurn = false;
        for (const auto& line : lines)
            if (line.rfind("TURN ", 0) == 0) isTurn = true;
        if (!isTurn) continue;

        agent->update(lines);
        for (const auto& command : agent->decide())
            cout << command << '\n';
        cout << "END\n" << flush;
    }

    delete agent;
    return 0;
}
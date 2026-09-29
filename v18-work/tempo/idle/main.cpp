#include "protocol.hpp"
using namespace std;
vector<string> decide(const p::View&, const p::Init&) { return {}; }
int main() { ios::sync_with_stdio(false); cin.tie(nullptr); return p::run(decide); }

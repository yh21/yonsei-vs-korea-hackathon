// blotto bot (v18 candidate): thin entry point. Logic lives in blotto.hpp.
#include "blotto.hpp"
static blotto::Bot bot;
std::vector<std::string> decide(const p::View& view, const p::Init& init) { return bot.decide(view, init); }
int main() { std::ios::sync_with_stdio(false); std::cin.tie(nullptr); return p::run(decide); }

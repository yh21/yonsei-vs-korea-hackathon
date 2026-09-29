#include <cstdio>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "../pol17.hpp"
using namespace std;
struct XRng { uint64_t s; XRng(uint64_t x) : s(x * 2654435761u + 88172645463325252ull) { for (int i = 0; i < 5; i++) next(); } uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; } double u() { return (next() >> 11) * (1.0 / 9007199254740992.0); } int ri(int a, int b) { return a + next() % (b - a + 1); } };
static pol::Params randomPP(int k, double amp) {
    XRng r(k);
    pol::Params p = pol::Params::v17();
    static const int big[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 17, 18, 19, 20, 21, 22, 23, 24, 25};
    for (int i : big) { if (r.u() < 0.4) continue; double f = exp((r.u() * 2 - 1) * amp); int v = (int)lround(p.P[i] * f); if (v < 1) v = 1; p.P[i] = v; }
    for (int i = 0; i < 4; i++) if (r.u() < 0.4) p.P[i] = max(2, p.P[i] + r.ri(-2, 2));
    if (r.u() < 0.3) p.P[15] = r.ri(1, 5);
    if (r.u() < 0.3) p.P[16] = r.ri(1, 4);
    if (r.u() < 0.3) p.garrisonAdd = r.ri(-1, 2);
    if (r.u() < 0.2) p.fAdjust = !p.fAdjust;
    return p;
}
int main(int argc, char** argv) {
    pol::Params d = pol::Params::v17();
    for (int a = 1; a < argc; a++) {
        int k = atoi(argv[a]); pol::Params p = randomPP(k, 0.5);
        printf("pp:%d:", k);
        for (int i = 0; i < 26; i++) if (p.P[i] != d.P[i]) printf(" P%d=%d(%d)", i, p.P[i], d.P[i]);
        if (p.garrisonAdd) printf(" garr=%d", p.garrisonAdd);
        if (p.fAdjust != d.fAdjust) printf(" fAdj=%d", p.fAdjust);
        printf("\n");
    }
}

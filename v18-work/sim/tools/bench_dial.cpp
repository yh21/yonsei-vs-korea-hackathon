#include "harness.hpp"
#include <ctime>
using namespace std;
int main(int argc, char** argv) {
    vector<hx::GameMap> maps; hx::readMaps(argv[1], maps);
    sim::Map M; hx::buildMap(maps[1], M);
    int risk[225]; unsigned r = 12345;
    for (int i = 0; i < 225; i++) { r = r * 1103515245 + 12345; risk[i] = (r >> 16) % 3 == 0 ? ((r >> 8) % 60) : 0; }
    static int dd[225]; static uint8_t mk[225];
    static int head[128], nxt[4096], nodeOf[4096], keyOf[4096];
    long N = 200000; volatile long sink = 0;
    clock_t c0 = clock();
    for (long it = 0; it < N; it++) {
        int z = 20 + it % 150;
        for (int c = 0; c < 225; c++) { dd[c] = 1000000000; mk[c] = 0; }
        for (int i = 0; i < 128; i++) head[i] = -1;
        uint64_t bm0 = 0, bm1 = 0; int ne = 0, remaining = 0, cur = 0;
        dd[z] = 0; nodeOf[ne] = z; keyOf[ne] = 0; nxt[ne] = head[0]; head[0] = ne; ne++; remaining++; bm0 |= 1ull;
        while (remaining > 0) {
            { int p0 = cur & 127;
              uint64_t m0 = p0 < 64 ? (bm0 >> p0) << p0 : 0ull;
              uint64_t m1 = p0 < 64 ? bm1 : (bm1 >> (p0 - 64)) << (p0 - 64);
              int f;
              if (p0 < 64 && m0) f = __builtin_ctzll(m0);
              else if (m1) f = 64 + __builtin_ctzll(m1);
              else f = bm0 ? __builtin_ctzll(bm0) : 64 + __builtin_ctzll(bm1);
              cur += (f - p0) & 127; }
            int bi = cur & 127; int e = head[bi]; head[bi] = nxt[e];
            if (head[bi] < 0) { if (bi < 64) bm0 &= ~(1ull << bi); else bm1 &= ~(1ull << (bi - 64)); }
            remaining--;
            int x = nodeOf[e], d = keyOf[e];
            if (d != dd[x]) continue;
            for (int k = 0; k < 4; k++) {
                int y = M.nbr[x][k]; if (y < 0) continue;
                int nd = d + 10 + risk[y];
                uint8_t fm = (x == z) ? (uint8_t)(1 << k) : mk[x];
                if (nd < dd[y]) { dd[y] = nd; mk[y] = fm;
                    int nb2 = nd & 127; nodeOf[ne] = y; keyOf[ne] = nd; nxt[ne] = head[nb2]; head[nb2] = ne; ne++; remaining++;
                    if (nb2 < 64) bm0 |= 1ull << nb2; else bm1 |= 1ull << (nb2 - 64);
                } else if (nd == dd[y]) mk[y] |= fm;
            }
        }
        sink += dd[100] + ne;
    }
    double us = 1e6 * (clock() - c0) / CLOCKS_PER_SEC / N;
    printf("dial search: %.2f us (sink %ld)\n", us, (long)sink);
}

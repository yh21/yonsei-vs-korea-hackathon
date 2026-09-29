// mimic/blob: 전 병력을 한 덩어리로 적 고가치 건물(병원/ENG/HALL)부터 차례로 밀어붙이는 데스볼. 깃발은 소수.
#include "../core.hpp"
int main() {
    mm::Params P;
    P.flagsOpen = 7; P.flagsLate = 5; P.openTurns = 8;
    int w[mm::NT] = {40, 80, 40, 40, 100, 90, 10, 60};
    for (int i = 0; i < mm::NT; i++) P.wt[i] = w[i];
    P.blob = true; P.tele = false; P.fwd = true; P.guard = true; P.hunt = 90;
    return mm::run(P);
}

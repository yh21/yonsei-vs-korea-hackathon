// mimic/teleraid: 역+병원+ENG 를 먼저 잡고 TELE 로 5기씩 적 진영 역으로 보내 습격 + 전진 병원 생산 + t>=20 적 진영 습격조.
#include "../core.hpp"
int main() {
    mm::Params P;
    P.flagsOpen = 4; P.flagsLate = 5; P.openTurns = 6;
    int w[mm::NT] = {45, 85, 60, 40, 95, 100, 25, 75};
    for (int i = 0; i < mm::NT; i++) P.wt[i] = w[i];
    P.stationBoost = 60; P.tele = true; P.fwd = true; P.teleGain = 2; P.blob = false; P.hunt = 90; P.sep = 2;
    P.raidAll = 1; P.raidStart = 20; P.raidNeed = 5; P.raidBonus = 100;
    return mm::run(P);
}

// mimic/hoard: 경제 우선(ENG/HALL/DEPOT) + 한동안 W 를 자기 진영에 쌓고 t>=45 에 적 진영 전 건물로 12기씩 일제 공세 (실서버의 W 수백 기 누적형 재현).
#include "../core.hpp"
int main() {
    mm::Params P;
    P.flagsOpen = 7; P.flagsLate = 5; P.openTurns = 8;
    P.tele = false; P.fwd = false; P.blob = false;
    P.raidAll = 1; P.raidStart = 45; P.raidNeed = 12; P.raidBonus = 200;
    return mm::run(P);
}

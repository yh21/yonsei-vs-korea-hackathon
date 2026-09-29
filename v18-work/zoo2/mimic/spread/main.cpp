// mimic/spread: 실제 상대 패턴 재현 - 초반 F 몇 기 -> 이후 자원 전부 W, 건물마다 W 분산 배치, 내 건물 1기 상주,
// t>=20 부터 적 진영 건물마다 W 6기 습격조, TELE 사용. (전진 병원 생산 없음)
#include "../core.hpp"
int main() {
    mm::Params P;
    P.flagsOpen = 7; P.flagsLate = 6; P.openTurns = 8;
    P.tele = true; P.fwd = false; P.blob = false;
    P.raidAll = 1; P.raidStart = 20; P.raidNeed = 6; P.raidBonus = 150;
    return mm::run(P);
}

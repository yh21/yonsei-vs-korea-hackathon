# v18/v19 작업 상태 (재개용 — 세션이 압축/재시작돼도 이 파일을 먼저 읽을 것)

## 사용자 지시 (2026-09-29 저녁)
1. 20:00 중간 점검에 낼 "1차 제출본"을 정한다 = **v18** (`yjc-submission18-cpp/` + zip). 미완성이어도 된다. 제출하는 순간 v18 은 **동결**(이후 수정 금지).
2. 20:00 이후에도 **멈추지 말고** 최상의 봇을 계속 만든다 = **v19** (`yjc-submission19-cpp/`). 목표: 기존 모든 봇(v5~v18, bot_opp, 상대봇 동물원)을 최대한 압도 + 처음 보는 전략의 봇에게도 강함. 100:0 은 이상적 목표.
3. 사용자가 직접 제출한다(나는 업로드/깃 명령 금지). zip 만 만들어서 경로를 알려준다.
4. v17 파일(`yjc-submission17-cpp`, `bot17`)은 사용자가 GitHub 에 올렸으므로 수정 금지.

## 제약
- 5시간 사용량 한도(Max). 리셋 시각 **19:50 (KST)**. 사용량 85% 이상이면 진행 중 워크플로 중단(크론 8cb94913 이 20분마다 점검). 이후 리셋되면 재개.
- 서버: gcc 12.2 / C++20 / 표준 라이브러리만 / 메모리 384MiB / 턴 300ms(첫 턴 3s) / stdout 65536B/턴 / stderr 1MiB / 프로세스 32개. 이 맥엔 clang 만 있어서 **헤더 누락(gcc 는 clang 보다 엄격) 점검 필요**.
- zip 규격: 루트에 main.cpp + 헤더 + submission.json. `.cpp` 는 main.cpp 하나만(다른 .cpp 가 있으면 main 이중 정의 위험). 평면 구조 권장. 빌드는 `yk-development-tools/bots/dist/starter/make_submission.py --source DIR --output ZIP`, 검사는 run_tests.py.
- 진짜 제출 전 검증: 몰수패 0, 턴당 최대 <=100ms (스트레스: runner 의 --turn-timeout-ms 100 으로 돌려서 통과), 전멸(instant) 패배 0.

## 시드 정책
- 개발/튜닝: 10000~10199
- 1차본 선정 평가: 40000~40007 (스냅숏 평가) / 필요시 40000+ 확장
- 최종 토너먼트/최종 검증: 20000~20039 (개발에 절대 사용 금지) 이후 필요하면 60000+
- 게임은 결정적 + 방향 정규화라 같은 시드는 Y/K 바꿔도 거의 같은 결과 → 표본 크기 = 시드 수.

## 도구
- `python3 v18-work/arena.py --bot ./X --opp ./A ./B --seeds LO N --workers 3 [--keep DIR] [--json]`
- `python3 v18-work/tl.py replay.json`, `v18-work/realtl.py`, `v18-work/realstats.py`
- 실제 서버 리플레이(우리가 K, 상대 Y, 전부 패배; 예전 약한 버전들): `v18-work/replays/real_XX.json`. 상대 초반은 판마다 다름(여러 팀 가능성). 40턴에 상대 W 90~140기.
- 후보 소스: `v18-work/{sim,stack,blotto,tempo}/` (에이전트 작업 중), 스냅숏: `v18-work/snap1/<name>/` (소스만, 17:35 시점).
- 동물원(상대 봇): `v18-work/zoo/{rusher,turtle,hunter,blitz}/bot`
- 스냅숏 평가 스크립트: scratchpad/eval_snap.sh (결과 eval_snap1_<name>.txt)

## 워크플로 wf_33f752ac-b03 (1차: 후보 4개 + 동물원 4개 + 토너먼트)
- 17:35 기준 rusher, turtle 완료. 나머지 진행 중. 끝나면 토너먼트 에이전트가 시드 20000~20039 로 측정.
- 사용량 85% 에서 크론이 중단시킬 수 있음. 중단돼도 산출물은 디스크에 있음.

## 알려진 위험/주의
- sim/stack 은 v15~v17 계보를 정확히 재현하는 상대 모델을 써서 계보 상대 승률이 부풀려졌을 수 있음 → 동물원/bot_opp/실서버 유형 상대 성능이 진짜 기준.
- 후보 개발 중 시간초과(몰수패) 발생 사례: sim, stack. 부하 때문일 수 있으나 반드시 100ms 스트레스로 확인.

## 진행 로그 (최신이 아래)
- 17:35 스냅숏 4개 컴파일 성공. 17:39 스냅숏 평가 시작.
- 17:59 snap1 평가(개발 안 쓴 시드 40000~): tempo 가장 견고(7상대 336판 forfeit 0; bot17/bot_opp/rusher/turtle/hunter 100%, blitz 96%, bot15 87.5%). h2h: tempo vs sim 65%, vs blotto 80%(blotto 10판 시간초과), vs stack 40%(stack 이 tempo 를 60% 이김).
- 18:00 **v18 1차 제출본 = tempo 스냅숏(snap1/tempo)** → `yjc-submission18-cpp/` (main.cpp + generated.hpp + protocol.hpp + submission.json), zip=`yjc-submission18-cpp.zip`, 실행파일 `./bot18`. tempo = v17 기반 + (상대 ENG/HALL 점령 가치 +100, 내 ENG/HALL 수비 crit, 목표우선 W 배정) 47줄 차이. hdr_audit OK.
- 18:01 컴퓨터 부하 120(에이전트 8개 동시 대전) → 스트레스/시간초과 측정 왜곡. **워크플로 wddfbxjg1 중단**(TaskStop), 잔여 프로세스 kill. 후보 산출물은 디스크에 있음. 이후 동시 실행량 제한 필수(에이전트당 프로세스 <=2, 전체 동시 에이전트 <=3).
- TODO: 부하 안정 후 (1) bot18 100ms 스트레스 재측정 (2) 후보 4개 최신 소스 스냅숏(snap2) 컴파일 후 깨끗한 환경에서 재평가(forfeit 가 부하 탓이었는지) (3) v19 = 후보 장점 합치기(stack 의 search + tempo 의 ENG/HALL 로직 등).
- 18:08 bot18 검증 완료: 부하 없는 상태에서 100ms 스트레스 56판 forfeit 0, RSS ~23MB, run_tests.py --zip 통과(ok, issues 없음). v18(yjc-submission18-cpp.zip) 제출 가능. (동결: 이후 수정 금지)
- 18:12 snap2(최신 소스, 부하 없음) 재평가: forfeit 전부 0(이전 forfeit 는 부하 탓). stack 이 v18(=tempo snap1)을 79%(24판)로 이김. tempo 최신판은 오히려 퇴보(bot17 75%).
- 18:24 **8시 제출본 확정 = v19 = stack snap2** → `yjc-submission19-cpp/` + `yjc-submission19-cpp.zip`, 실행파일 `./bot19`. v18(tempo, `yjc-submission18-cpp.zip`)은 예비. 둘 다 동결(수정 금지).
  - 공정 비교(시드 62000~62019, 9상대 × 40판): v19 340/360=94.4% (최저 blitz/sim 80%), v18 316/360=87.8% (최저 sim 52.5%). v19 vs v18 직접 58%(60판; 시드에 따라 v19 가 0점 전멸당하는 판 다수 — ENG/HALL 습격형에 취약).
  - v19 검증: run_tests.py --zip 통과, hdr_audit OK, ASan/UBSan 2판 무오류, 스트레스 60ms 42판·100ms 12판 forfeit 0, RSS ~24MB, 턴당 CPU 평균 ~10ms 최대 ~25ms(자체 50ms 벽시계 예산 + 부하 가드).
- v19 알려진 약점: (1) 기습형(blitz: 병원 전진 생산/텔레포트) 80% (2) v18 류 ENG/HALL 습격에 시드 따라 전멸 (3) 상대 모델은 v15~17 계보에만 정확(비계보 정확도 0.5~0.7).
- 다음(v20): v18-work/v20/ 아래 3개 에이전트(econ-raid, robust, mimic) 병렬. 마감 19:40. 시드: 개발 12000~12199, 홀드아웃 20000+/40000+/60000+/62000+ 사용 금지(비교 완료 시드 제외).
- 18:27 v20 워크플로 wy3kxrp8b 시작(에이전트 3개: econraid → v18-work/v20/econraid, robust → v18-work/v20/robust, mimic → v18-work/zoo2/mimic). 마감 19:40. 사용량 점검 크론은 90%에서 중단(15분 간격). 19:53 자동 재개 크론(fdaec487) 유지.
- 재개 시 할 일: 워크플로 결과(journal.jsonl) 확인 → 각 산출물 submit/ 을 채택 기준(v19 직접 55%+, 풀 평균·최저 v19 이상, forfeit 0, 100ms 스트레스)으로 내가 직접 재검증(홀드아웃 시드 20000+/40000+/60000+) → 통과분 합치기/선정 → 다음 라운드(v21) 계획.
- 19:25 v20 워크플로 wy3kxrp8b 완료(63분). econraid/robust 둘 다 채택 주장, mimic 은 패치 실패(v19 미변경, 실서버 대역 spread/hoard/teleraid 제작).
- 19:43 홀드아웃 시드 70000~70015(32판/상대)로 직접 재검증: v19 풀(bot19 제외 14상대) 421/448=94.0%(최저 tempo 68.8%). econraid 443/448=98.9%(최저 96.9%), robust 439/448=98.0%(자체 시간초과 패배 1판 seed 70005 K vs bot18). econraid vs v19 직접 23/32=71.9%, robust vs v19 23/32, econraid vs robust 18/32(56%, 무의미). 실서버 대역(spread/hoard/teleraid): econraid 31/32, 32/32, 31/32.
- 19:46 **v20 확정 = econraid** → `yjc-submission20-cpp/` + `yjc-submission20-cpp.zip`(package.py: 개발코드 제거, 전처리 동일 확인), 실행파일 `./bot20`. 스트레스: 60ms 42판·40ms 16판 forfeit 0, RSS ~22MB. run_tests --zip OK, hdr_audit OK. (thread_local 키워드 사용(sim/search.hpp) — 스레드 생성 없음, 무해.) v20 CPU/턴은 v19 대비 +40%(평균 ~15ms 최대 ~30ms).
- v20 알려진 약점: (1) 일부 시드 초반 ENG/HALL 경쟁에서 점수 0 전멸(70001/70009/70012 vs v19, 70009 vs sim) (2) 상대 모델은 계보/자체 봇에만 정확, 무관한 상대엔 정확도 0.5~0.7 (3) 시간 여유가 v19 보다 얇음(서버 느리면 budgetMs 50→35 고려) (4) 모든 검증 상대가 우리 팀이 만든 봇 — 실제 타 팀 효과 미지수.
- 다음(v21 후보 아이디어): 초반 t<30 ENG/HALL 경쟁 개선(오프닝 북/깊은 탐색), 온라인 상대 모델 학습(비계보 대응), 모델 계획 캐시로 CPU 절감, blotto 식 bank-and-burst 전진 병원 생산, teleraid 류 장기 소모전 대응(HALL/ENG 소규모 W 피켓 강화).

# v18 candidate "stack" - design notes, experiment log, weaknesses

Directory: `v18-work/stack/`  (submit files: `main.cpp sim.hpp policy.hpp search.hpp protocol.hpp generated.hpp submission.json`,
binary for the arena: `v18-work/stack/bot`, built with `g++ -std=c++20 -O2 -o bot main.cpp`; `bot_tune` = same code built with
`-DTUNE` (every knob readable from `P<n>` environment variables); `submit/` = clean copy of the submission files).

## 1. One paragraph summary

The assigned idea was a "deathball + retreat" doctrine.  I first measured what actually decides games in this lineage and
found that concentration by itself is worth nothing (every fight is an exact 1:1 warrior trade, so both sides always lose the
same number of W; games are decided by production - ENG/HALL/DEPOT -, by flag kills and by who flips buildings).  What did
produce a large jump is an exact forward model: I re-implemented the engine in C++ (`sim.hpp`), ported the v16/v17 planner
into a lean class that can be run on simulated states (`policy.hpp`), and put a simulator-backed local search on top
(`search.hpp`).  The search decides every engagement / hold / retreat / converge question by simulating it against a predicted
enemy plan, which is the "only take winning engagements, retreat otherwise, hunt lone flags and use the neutralised-building
combo" part of the assignment, but computed instead of hard-coded.  On the doctrine side the parts that helped in A/B tests are
one-warrior pickets on ENG/HALL/top buildings and fewer flags in mid game; forced concentration (rally / regroup) and blanket
flag hunting did not help and are disabled.

## 2. Architecture

* `sim.hpp` - exact re-implementation of the 9 phase turn pipeline (spawn, move/TELE, combat, income, capture with
  PRIORITY / library / depot rules, flag pull, 2 step capture of enemy buildings).  Verified against the reference Python
  engine: 22 full replays reproduced state-for-state (resources, every unit stack, every owner, every turn); the only replays
  it cannot follow are ones where the opponent moves scouts with MOVE2 (scouts are treated as static).  One `step()` = ~1.5 us.
* `policy.hpp` - `Doctrine`: lean port of the v16/v17 planner (risk weighted flag routes, escort/garrison "crit" reservations,
  goal allocation for warriors, spawn budget with capture reserve, TELE).  With v17 parameters it reproduces ./bot17
  move-for-move (20/20 draws from the Y side and 8/8 from the K side), so it is also an exact model of v17; v16 = v17 with 4
  parameters changed, v15 = v16 with other flag counts.  ~100 us per call.  Doctrine additions (all off in the model copies):
  hold-value garrison priority (ENG/HALL/hospitals first), threat-scaled pickets: the top-5 owned buildings (by hold value) get a
  warrior dispatched, and 1 + (enemy warriors within 5 steps)/2 of them when the enemy is near, flag counts 7/5/4/3 instead of 7/6/5/4.
* `search.hpp` - `Searcher`, per turn:
  1. opponent model: the three lineage variants all predict the enemy's plan; the one that best predicted the enemy's previous
     move (fraction of enemy W/F standing where predicted, EMA) is used for the search (accuracy vs bot17/16/15 = 0.99, vs the
     unrelated bot_opp ~0.5, vs zoo bots 0.5-0.7);
  2. base plan = doctrine plan;
  3. candidates (each scored by `valueOf`): (a) macro "reinforce": for each owned building with enemy units within 5 steps and too
     few of my warriors around it, detach warriors from stacks within 6 steps and send them one step towards it, scored over a
     longer rollout (arrival time + 2 turns, at most 6) against the base plan at the same horizon; (b) joint "converge every stack within 1 step on cell z" (all-in and just-enough
     variants) for enemy flags, enemy stacks and contested buildings; (c) single stack stay/4 directions for every flag stack
     and every warrior stack within 3 of an enemy unit; accept when better than the incumbent by a margin (3);
  4. `valueOf` = 2 turn rollout (turn 1: candidate vs predicted enemy plan, turn 2: both sides follow the policy) followed by a
     static evaluation (buildings with ENG/HALL worth, W/F material, resources); from turn 150 the rollout runs to turn 160 and
     the terminal state is scored exactly by points.
  Budget per turn: <=150 evaluations, cost cap 340 (sum of rollout lengths), 50 ms wall clock (18 ms / 0 ms after a turn that was
  descheduled - load guard).  Measured CPU: mean ~10 ms, max ~25 ms per turn.
* `main.cpp` - protocol glue, memory (revealed scores incl. symmetric partners, depot claims), state conversion, command emission,
  catch-all (an exception only skips the turn).

## 3. Doctrine items of the assignment - what was done and what was measured

Policy-only tests (no search) against ./bot17 ./bot16 ./bot15, seeds 10000-10039, Y side, win rate (v17 mirror = 50 %):

| variant | vs bot17 | vs bot16 | vs bot15 |
|---|---|---|---|
| v17 baseline (measured earlier, mirror-normalised) | 50 | 55 | 70 |
| rally leftover W to biggest stack instead of overflow attack | 52 | 55 | 65 |
| hold-value garrison priority (ENG/HALL first) | 62 | 80 | 88 |
| + dispatch a picket to top-3 buildings | 80 | 80 | 85 |
| + flags 7/5/4/3 (final) | 85 | 85 | 85 |
| blanket flag hunting (goal value 1000-1500, need 1-2) | 50-57 | 57-85 | 57-75 |
| send whole stack to best goal (no need cap) | 10 | 35 | 25 |
| remove garrison completely | 45 | - | 35 |

* (1) few big stacks: rally/regroup did not help (rally neutral, `regroup` candidate in the search: 37 % in a mirror A/B) and
  removing all garrisons or ignoring `need` caps is catastrophic (0:30 wipe-outs).  The reason is in section 4.
* (2) engage only when winning / retreat: done by the search (exact combat maths per candidate, hold/retreat/advance per stack).
* (3) no overflow attack: the search checks every overflow move against "stay"; replacing overflow by rally was neutral.
* (4) flag hunting + neutralised-building combo: joint "converge on enemy flag cell" candidates; the simulator applies the same
  turn capture rule exactly.  Blanket hunting via goals hurt (table).
* (5) wipe-outs (bot17 vs bot15 seeds 7000/7017): the same mechanism everywhere I looked - lone unescorted enemy flags walk to
  ENG/HALL/DEPOT in my half while all warriors are at the centre; one warrior kills a lone flag, so one picket per key building
  removes it.  Early F-vs-F stand-offs on a neutral HALL/ENG corner also lost the building to whoever brings the first warrior.
* (6) scouts / watch stations: not used.

## 4. Facts about the game found on the way

* Every combat is exactly 1:1 in warriors: in all 100+ replays inspected lostW(Y) == lostW(K) at every turn.  So total warrior
  count is conserved up to production; concentration only decides *where* the surplus is, and pickets are the cheapest way to
  stop a flag raid (5 res flag vs 3 res warrior).
* Production decides the snowball: ENG (W cost 3->2 = +50 % warriors per resource), HALL (+2/turn = +20 % income), DEPOT (+15).
  Losing HALL/ENG early (t = 15-30) to a lone flag raid was the cause of nearly all lost games I opened.
* Contested cells (flag vs flag) freeze both captures until a warrior arrives.

## 5. Experiment log (dev seeds 10000-10079 unless noted; win rates of the bot as Y unless noted)

### 5.1 Search bot vs the lineage (bot bot = Y side unless noted, 40 seeds per cell, forfeits 0 with a generous dev timeout)

| build | change | bot17 | bot16 | bot15 | bot_opp |
|---|---|---|---|---|---|
| a1 | first search (H=2, single-stack coordinate ascent, v17 as model), 50 ms wall budget under heavy load | 35/40 | 38/40 | 37/40 | 38/40 |
| a4 | route-cost cache in rollouts (2x faster evals), fixed eval count | 40/40 | 37/40 | 37/40 | 39/40 |
| a11 | opponent model selection (v17/v16/v15) + hold-value garrison + 3 pickets + flags 7/5/4/3 | 40/40 | 39/40 | 39/40 | 40/40 |
| a14 (K side, seeds 10000-10019) | same | 20/20 | 20/20 | 20/20 | 20/20 |
| a20 (held-out seeds 10080-10119) | + joint-refinement, regroup/spawn variants off | 40/40 | 40/40 | 39/40 | 39/40 |
| old bots bot5..bot14 (10 seeds each) | a11 | 100/100 | | | |

Side notes: H=3 -> 39/40 (no gain); mixing a "enemy stays" model (30 %) -> 38/40 (worse); 300 evals -> 40/40 (no gain);
60 evals -> 38/40 and 39/40 (small loss, so the default of 150 is a comfortable margin); exposure-of-flags penalty in
the evaluation hurt (blitz 75 -> 50 %); margin scaling with model distrust hurt (blitz 100 -> 90 %).

### 5.2 Other styles (zoo)

* my own 9 simple bots (rush, turtle, chaser, random, camp, flag-hoard, splitter, example lv1/lv2): 72/72, margin ~ +33.
* other agents' test-zoo bots (snapshot 16:17): blitz / hunter / rusher / turtle: 20/20 each for the a14 doctrine, 20/20, 20/20,
  18/20, 20/20 for the shipped one (before hold+dispatch: blitz 75 %, rusher 92 %).
* other agents' v18 candidates (snapshots 16:58, both sides, 10 seeds): sim 20/20, tempo 19/20, blotto 10/20 with 8 wipe-outs.
  Blotto is the only opponent that beat the search bot; it takes ENG/HALL/hospitals with small raid parties from t~30,
  and bank-and-burst spawns (26 resources banked, 13 W spawned next turn at a forward hospital).  Variants vs blotto
  (20 seeds x 2 sides): base 57 %, hospital hold 250 50 %, hold+5 pickets 62 %, macro-reinforce on 62 % / off 52 %,
  threat-scaled pickets 62 %, final defence bundle 65 % (margin +4.4), wider macro-reinforce radius 60 %, plus macro-"intercept"
  (chase raid parties) 65 %, plus macro-"strike" (squad at a weakly held enemy ENG/HALL/hospital) 70 %, both 52 % - with n=40 the
  standard error is ~8 %, so none of these is distinguishable from the 65 % of the shipped bundle; strike/intercept stay off.

### 5.3 Mirror matches (bot vs bot, 16 seeds x 2 sides) - very noisy (SE ~ 9 %), used only to reject big effects

flag value 3 -> 16 % (real: F-suicide), 10 -> 34 %, 4 -> 56 %; adversarial "worst enemy reply" hedge 30 % -> 50 %,
60 % -> 34 %; H=3 47 %; score weight 35 -> 47 %; 5 pickets 53 %; full budget vs lean budget 59 %; 3x budget vs default 50 %.
A later 48 game run of the "cleaned up" configuration vs the earlier one gave 44 %, i.e. single-factor A/B results of
+-10 % in this table are not significant.

### 5.4 Model accuracy (fraction of enemy W/F standing where the model said, per turn)

bot17 0.99 (v17 model), bot16 0.995, bot15 0.99 (after model selection); bot_opp 0.41-0.59, blotto 0.58-0.67, momentum /
strike / "stay" models are worse than the v17 model on both.


### 5.5 Final validation of the shipped configuration (`bot`, TUNE build with the same defaults, no wall limit, seeds never used for tuning)

| opponent | sides / seeds | result |
|---|---|---|
| bot17 | Y, 10080-10119 | 39/40 |
| bot16 | Y, 10080-10119 | 40/40 |
| bot15 | Y, 10080-10119 | 40/40 |
| bot_opp | Y, 10080-10119 | 40/40 |
| bot17 / bot16 / bot15 / bot_opp | K, 10120-10139 | 20/20, 19/20, 20/20, 20/20 |
| blitz, hunter, turtle, sim*, tempo* | Y+K, 10000-10009 | 20/20 each |
| rusher | Y+K, 10000-10009 | 18/20 |
| blotto* | Y+K, 10000-10019 | 26/40 (65 %, 8 wipe-outs) |
(* = other agents' snapshots, taken 16:58)

Instant (wipe-out) losses: 0 in the 240 lineage games above, 8 in the 40 blotto games.
Forfeits: 0 in every dev run.  With the official 300 ms limit on this (heavily overloaded, load average 100-170) machine one
early run (before the load guard existed) forfeited 2 of 8 games by timeout although the bot's CPU time per turn was ~15 ms
(wall time of 300+ ms came from being descheduled; ./bot17 reached 195 ms wall in the same conditions); after adding the
guard 8/8 and 12/12 official-limit games ended without forfeit.

## 6. Timing

`BOT_TIMING` build (CPU via clock(), wall via steady_clock), full games vs blotto / blitz / tempo:
mean CPU 5.8 / 8.8 / 8.1 ms per turn, max CPU 10.7 / 27.5 / 15.6 ms, evaluations per turn mean 67-97 (max 150);
wall time on the loaded machine: mean 9-22 ms, max 73-168 ms.  Budget knobs: <=150 evaluations, cost cap 340, 50 ms wall
(18 ms after a turn whose wall time was >3x its CPU time and >60 ms, 0 ms - base plan only - after a turn over 150 ms).
ASAN+UBSAN build ran 8 full games (incl. blotto/blitz) without a report.

## 7. Known weaknesses

* Blotto-style opponents (small raid parties on ENG/HALL/hospital from t~30, bank-and-burst spawns at a forward hospital,
  early sweeps) still win ~35 % of the games; all P-family models predict them only at 0.6 accuracy, so the search is close to
  blind there.  Wipe-outs (0:30) happen mostly in these games.
* Against a similar search bot (mirror) the result is ~50 %; no edge from the search itself.
* The opponent model is the v15-v17 planner.  For unrelated opponents the search still helped in every test (bot_opp
  accuracy 0.5, 100 % wins) but the assumption "enemy moves like the planner" is the main risk for unseen styles.
* Scouts and watch stations are not used; unknown building scores are estimated (centre 3, home 1.5) until revealed.
* Flags are lost more often than ideal against tactical strikers (blitz: 12 flags lost per game vs 8 for the opponent).
* Search results depend on the wall-clock budget when the machine is heavily loaded (fewer evaluations), never on the seed.
* Everything is deterministic given the evaluation budget; there is no randomness to exploit or to hide behind.

## 8. Reproduce

```
g++ -std=c++20 -O2 -o v18-work/stack/bot v18-work/stack/main.cpp
python3 v18-work/stack/arena3.py --bot ./v18-work/stack/bot --opp ./bot17 ./bot16 ./bot15 ./bot_opp --seeds 10080 40 --sides Y --tmo 8000
```
Tools in this directory: `arena3.py` (in-process arena with F-loss and timing stats), `simtest.cpp` + `dump.py` (simulator vs engine
replay check), `bench.cpp` (policy/sim/search speed), `modelacc.cpp` / `modelfit.cpp` (opponent-model accuracy on recorded games),
`battle.py fights.py own.py view.py` (replay analysis), `zoo2/` (my simple opponents).  `TUNE` builds (`-DTUNE`) read `P<n>` environment
variables for every knob (`prm(index, default)` in policy.hpp / search.hpp).

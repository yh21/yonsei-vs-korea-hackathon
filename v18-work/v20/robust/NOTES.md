# v20 "robust" - notes (base = v19 stack snapshot)

Directory: `v18-work/v20/robust/`. Source: `src/` (main.cpp, sim.hpp, policy.hpp, search.hpp, protocol.hpp, generated.hpp).
Submission form: `submit/` (made by `package.py`, compile check ok, hdr_audit ok). Binaries: `cand3` (= submit, release),
`tune3` (same code with `-DTUNE`, knobs readable from `P<n>` env vars), `base` (= v19 as-is).

## What was wrong with v19 (diagnosis, dev seeds 12000-12029, 60 games per opponent)

v19 baseline: bot18 71.7 %, snap2/tempo 48.3 % (!), snap2/sim 83.3 %, blitz 90 %, blotto 90 %, rusher 96.7 %, bot15 98.3 %,
bot17 / bot_opp / turtle / hunter 100 %.  Mean over the 11-opponent pool 88.9 %, minimum 48.3 %.

Root causes found in the lost games (replays + `an.py` building-flip timeline):
1. The lost games are decided by the ENG/HALL race (see stack NOTES sec. 4): the opponent (tempo family = v17 + "enemy ENG/HALL worth
   +100 / +1200, own ENG/HALL crit(2) + goal(900), goal-first warrior allocation") holds both ENGs from t~20, spawns 5-6 W per turn at a
   forward hospital with the 2-cost discount and has 1.5-2x our warrior count by t=40; every later fight is a 1:1 trade so that lead
   never comes back.  Wipe-outs (0 points) = the same thing continued to t~100.  The wipe-outs are map dependent
   (seeds 12002, 12006, 12007, 12019, 12020, 12023, 12029 lose both sides) - the same seeds lost against tempo, bot18 and sim.
2. v19's search assumes the opponent moves like a v15/v16/v17 planner (3 model variants).  Against tempo the best of those models predicted
   only 0.6-0.8 of the enemy W/F positions, i.e. the simulator-backed search was fed a wrong enemy plan.
3. v19's own doctrine (pickets on the top-5 holds, 1 W each) is weaker at the ENG/HALL race than tempo's (2-W crit reservations + goal-first).

## Changes (v19 -> robust), both small and switchable (`Cfg::tempo`)

* `policy.hpp`: `Cfg::tempo` (0 off, 1 tempo, 2 tempo without the +1200 attack bonus).  When set the Doctrine additionally does what
  snap2/tempo does: enemy ENG/HALL buildingValue +100; own ENG/HALL crit reservation (need 2, priority 35000+value, totalW >= 10);
  own ENG/HALL defence goal (900, need 2); non-owned ENG/HALL attack goal +1200 (tempo==1 only); goal-first warrior allocation
  (goals by value, nearest source within 5 steps, `stepToward`), before the old utility-based loop.
* `search.hpp`: opponent model set 3 -> 5: model 3 = v17 + tempo(1) (exact model of snap2/tempo), model 4 = v17 + tempo(2)
  (bot18 = snap1/tempo).  Model selection is unchanged (EMA of prediction accuracy, +0.02 hysteresis).  Measured: the tempo model
  predicts snap2/tempo with EMA accuracy 1.00 on every turn of a full game (v17 model 0.65-0.9), so the search now sees the true enemy plan.
* `search.hpp`: `dm.cfg.tempo = prm(110, 1)`: our own base doctrine also runs in tempo style (E1 below).  (P110=0 gives the pure model-only variant "cand2".)
* TUNE-only debug: `BOT_ACC=<file>` writes per-turn model accuracies.

## A/B results (dev seeds; win rate as Y+K, 2 games per seed; n = games)

| build | bot18 | snap2/tempo | blitz | sim |
|---|---|---|---|---|
| v19 (base), seeds 12000-12029 (60 games) | 71.7 | 48.3 | 90.0 | 83.3 |
| cand1 = model 3 only, own doctrine unchanged (12000-12029, 60) | 86.7 | 83.3 | 85.0 | 88.3 |
| cand2 = models 3+4, own doctrine unchanged (12000-12019, 40) | 95.0 | 92.5 | 91.2 | 85.0 |
| E1 = cand2 + own doctrine tempo (12000-12019, 40) | 100 | 97.5 | 95.0 | 97.5 |
| **cand3 = E1 as default, full pool (12000-12029, 60)** | 98.3 | 94.2 | 96.7 | 96.7 |

Full pool of the final candidate (cand3 = submit/, seeds 12000-12029, 60 games each, v19 numbers from the same seeds in brackets):
v19 direct 49W 0D 11L = **81.7 %** (>55 %, 30 seeds); tempo 94.2 [48.3]; bot18 98.3 [71.7]; sim 96.7 [83.3]; blotto 98.3 [90.0];
blitz 96.7 [90.0]; rusher 100 [96.7]; hunter 100 [100]; turtle 100 [100]; bot17 100 [100]; bot15 100 [98.3]; bot_opp 98.3 [100].
Mean over the 11 pool opponents 98.4 % (v19 88.9 %), minimum 94.2 % (v19 48.3 %).

Fresh seeds 12030-12049 (never used for any decision; cand3, 40 games each): v19 direct 38W 2L = **95 %**; snap2/tempo 95 % (1 wipe-out, seed 12031 Y);
snap2/sim 100 %; blitz 95 %; forfeits 0 of ours (1 blotto-style/sim forfeit by the opponent).  v19 direct combined over both seed ranges: 87/100 = 87 %.
The baseline v19 was not re-run on the fresh seeds.

Caveats (honest): the tuning decision (own tempo doctrine on/off) was made on seeds 12000-12019 and the final table re-uses 12000-12029
(the same 20 seeds), so the final numbers are slightly optimistic; no fresh seeds were run for the full pool because of time.
The candidate is now strong exactly against the tempo lineage because of the exact model; unseen styles were only checked with
blotto/sim/blitz/zoo (all improved or equal), not with truly unseen bots.

## Forfeits / timing

* Arena run of the pool had 4 forfeits: blotto's own time-outs (known; it forfeits at heavy load), plus 2 games where OUR bot lost by
  forfeit while leading on points (snap2/tempo seed 12002 Y, bot_opp seed 12028 K).  Both were run at machine load average 10-44
  (other agents).  Re-running both alone: 2/2 wins each, no forfeit.
* Per-turn cost measured with a `-DTIMING` build (12070, vs tempo and blotto, 588 turns): cand3 mean CPU 15.2 ms, max CPU 29 ms, mean wall 16 ms, one
  wall spike of 90 ms (scheduler); v19 same setup: mean CPU 10.8 ms, max 35 ms, max wall 51 ms.  CPU per turn grew ~40 % (5 model plans instead of 3
  and the tempo doctrine in every rollout); the search budgets (<=150 evals, cost cap 340, 50 ms wall, load guard) are unchanged.
* 100 ms stress (`stress.py --timeout 100`), first run with 2 workers under load ~10: 2 forfeits of our bot (bot18 seeds 12060/12061 as Y);
  alone (workers 1) the same games pass for both cand3 and bot19 (4/4, forfeits 0).  
  Serial rerun (workers 1, seeds 12064-12066, bot18 / snap2-tempo / blitz, both sides): 18 games, 0 forfeits, 23 MB RSS.
  Conclusion: load-related, but the margin is thinner than for v19 (mean CPU +40 %), so if the server is slow consider `budgetMs` 50 -> 35.

## Remaining weaknesses

* 12007 K / 12023 K / 12019: map-dependent early ENG/HALL races can still be lost against tempo (3 of 60), and wipe-outs against v19 (seeds 12021, 12023).
* Models exist only for the v15-v17 lineage and its tempo variant; blotto / sim / blitz style opponents are still predicted at ~0.5-0.7 (the doctrine change is what helped there).
* The 'own tempo doctrine' was validated only on ~40-60 games per opponent; CI is about +-6 %.
* v19 direct is 81.7 % but the mirror-like structure means only ~30 independent seeds.

## Reproduce

```
g++ -std=c++20 -O2 -o v18-work/v20/robust/cand3 v18-work/v20/robust/src/main.cpp
v18-work/v20/robust/hard.sh ./v18-work/v20/robust/cand3 12000 30 out.txt ./bot19 ./v18-work/snap2/tempo/bot ...   # per-opponent arena runs
python3 v18-work/v20/robust/an.py <replay.json> Y|K [upto] [every]     # building-flip timeline
BOT_ACC=acc.txt tune3 ...   # per-turn model accuracy (TUNE build)
```

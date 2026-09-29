# v20 econraid - ENG/HALL raid doctrine + tempo/own-doctrine opponent models (on top of v19 "stack")

Directory: `v18-work/v20/econraid/`  (`src/` = working source, `submit/` = package.py output = final submission files,
`bot_final` = non-TUNE build of `submit/`, `bot_c2` = same code with `-DTUNE` (knobs `P<n>` env vars), `res/` = arena json results,
`drv.sh sum.py sum2.py own.py` = helper scripts).

## What changed relative to v19 (all in `src/policy.hpp` and `src/search.hpp`)

1. **tempo doctrine port** (from `snap1/tempo` = v18), as `Cfg` fields of `Doctrine`, default ON for our own doctrine only:
   * `ehBV = 100`  : buildingValue of an ENEMY ENG/HALL +100 (flag target priority for capturing enemy ENG/HALL).
   * `ehN = 2, ehMinW = 10, ehPri = 35000`: own ENG/HALL get a 'crit' reservation of 2 warriors (priority 35000+value) once totalW >= 10.
   * `ehGoal = 900`: warrior goal value 900 (need 2) for own ENG/HALL.
   * `goalFirst = 1, goalFirstD = 5`: goals are served first, by value, from the nearest warrior stack within 5 steps
     (v19 was source-driven: biggest stack first picks its best goal).
   These interact with v19's hold-value garrison / picket logic (pickets stay; the ENG/HALL crit is on top).
2. **Opponent models** (the search predicts the enemy plan with the model whose previous prediction was most accurate):
   v19 had 3 models (v17, v16, v15 planners).  Now 7: + `mdl[3]` = v17 + tempo(v18) features, `mdl[4]` = same + `ehRaid = 1200`
   (goal value of unowned ENG/HALL, this is what snap2/tempo added), `mdl[5]` = the v19 doctrine (our old bot, tempo features off),
   `mdl[6]` = our current doctrine.  Measured with a debug build (per-turn model accuracy, fraction of enemy W/F standing where predicted):
   `mdl[3]` = 1.00 vs bot18 (bit-exact port), `mdl[4]` = 1.00 vs snap2/tempo, `mdl[5]` = 0.94-0.98 vs bot19 (v17 model: 0.8).
   Selection rule unchanged (EMA of accuracy, switch only if better by 0.02), so lineage opponents still get their exact models.
3. Knobs that were added but stay OFF (no gain): `ehRaid` for our own goals (P137), `hospRaid` (P138, unmeasured).

## Measurements (dev seeds 12000-12029 unless noted; each seed is played from both sides; win rate counts draws as 0.5)

Baseline = `bot_base` = the v19 code recompiled (identical to ./bot19).  Candidate = `bot_c2` / `submit/` (final).  All results have
0 forfeits of our bot (some opponents - blotto, and bot17/hunter under load in the baseline runs - forfeit on their own; the arena counts any forfeit).

| opponent (seeds) | v19 baseline | candidate |
|---|---|---|
| bot19 = v19 h2h (12000-12029, 60 games) | 50% by definition | **51/60 = 85.0%** (first 20 seeds: 33/40) |
| snap2/tempo (12000-12029) | 29/60 = 48% | 59/60 (C1 build, see below); 20/20 on 12000-12009 (final build) |
| bot18 = v18 tempo (12000-12029) | 43/60 = 72% | 57/60 (C1); 20/20 on 12000-12009 (final) |
| snap2/blotto (12000-12029) | 54/60 = 90% | 58/60 (C1); 19/20 on 12000-12009 (final) |
| pool, seeds 12000-12014 (30 games each): bot17 | 29/30 | 30/30 |
| bot15 | 30/30 | 30/30 |
| bot_opp | 30/30 | 30/30 |
| zoo rusher / turtle / hunter | 30/30, 30/30, 29/30 | 30/30, 30/30, 30/30 |
| zoo blitz | 26/30 = 87% | 30/30 |
| snap2/sim | 24/30 = 80% | 28/30 = 93% |
| pool total (240 games) | 228/240 = 95.0%, min 80% | **238/240 = 99.2%**, min 93% |

"C1" = the first candidate (6 models: v17, v16, v15, tempo18, tempo-snap2, own-current-doctrine) measured on 30 seeds vs bot19/tempo/bot18/blotto:
47/60, 59/60, 57/60, 58/60.  The final build (C2) differs from C1 only in the model list (mdl[5] = pure v19 doctrine, mdl[6] = current one) and was
measured on the numbers in the table (h2h 51/60 vs 47/60 for C1).

Individual A/B of the doctrine features and of the models (vs bot19, seeds 12000-12019, 40 games; full candidate = 33/40): see the ablation table
at the end of this file (filled in if it finished before the deadline).

Other things tried:
* `ehRaid = 1200` for our own goals (snap2/tempo's extra) with the old models: tempo 30/60 (baseline 29), bot18 40/60 (43), blotto 47/60 (54; 2 load forfeits) -> not kept.
* tempo doctrine (ehBV/ehN/ehGoal/goalFirst) alone with the OLD three models: tempo 49/60, bot18 53/60, blotto 58/60 (baseline 29, 43, 54) - so most of the
  gain against the tempo family comes from the doctrine, the exact models add the rest (59/60, 57/60, 58/60).
* Not done (no time): bank-and-burst spawns, dedicated raid parties (search "macro strike" from stack stays off, measured earlier as inconclusive), hospital raid goals (`hospRaid`, P138, coded but not measured).

## Verification
* `python3 v18-work/hdr_audit.py submit` OK (0 missing std headers); `package.py` compile with -Wall -Wextra clean; TUNE/TIMING code stripped by package.py.
* 100 ms turn-timeout stress: 36 games (bot17, bot19, bot18, blitz, snap2/tempo, snap2/blotto; seeds 50000-50002, both sides): 0 forfeits/timeouts of our bot
  (the 4 flagged games are blotto's own timeouts), max RSS ~24 MB.  CPU per turn is the same order as v19 (mean ~10 ms, max ~25-40 ms measured in a debug build; 5 extra opponent
  models cost ~0.5 ms/turn).

## Known weaknesses
* Wipe-outs (score 0, `instant`) still happen on some seeds: 12013 (both sides) / 12019 K / 12023 Y vs v19, 12023 K vs bot18 and tempo, 12029 vs blotto.  In the ones opened
  (12029 vs blotto, 12023 vs v19) the enemy simply has ~2x our warriors from t~30 (production snowball after the ENG/HALL flip-flop at t=14-33); not fixed.
* The new models are exact only for opponents that are copies of bot18 / snap2-tempo / v19; against unrelated opponents the model selection falls back to the
  old ones (accuracy 0.5-0.7 like v19).  Our opponents in all tests are our own bots, so the size of the gains vs real other-team bots is unknown.
* The h2h gain vs v19 is partly the exact model of v19 (mdl[5]); a v20 built from another agent's doctrine would not be modelled exactly.

## Reproduce
```
g++ -std=c++20 -O2 -DTUNE -o v18-work/v20/econraid/bot_c2 v18-work/v20/econraid/src/main.cpp
python3 v18-work/arena.py --bot ./v18-work/v20/econraid/bot_c2 --opp ./bot19 --seeds 12000 30 --workers 1
P130=0 P131=0 P134=0 P135=0 ...   # turn features off (TUNE build only)
```

## Ablation (individual A/B; candidate config vs bot19 = v19 h2h, 20 seeds x 2 sides = 40 games, TUNE build `bot_c2` with env knobs)

| config | knobs | seeds 12000-12019 vs v19 |
|---|---|---|
| full candidate (final) | ehBV=100 ehN=2 ehGoal=900 goalFirst=1 + 7 models | 33/40 = 82.5% |
| models only (doctrine features off) | P130=0 P131=0 P134=0 P135=0 | 27/40 = 67.5% |
| no goalFirst | P135=0 | 25/40 = 62.5% |
| goalFirst only | P130=0 P131=0 P134=0 | 34/40 = 85.0% |
| no ehBV | P130=0 | 35/40 = 87.5% |
| no ehN/ehGoal (= ehBV + goalFirst, "C3") | P131=0 P134=0 | 37/40 = 92.5% |

Fresh dev seeds 12030-12049 vs v19 (40 games): final (C2) 35/40 = 87.5%, C3 36/40 = 90.0%.  C3 vs the tempo trio (seeds 12000-12009, 20 games each): tempo 19/20, bot18 18/20
(C2 final: 20/20 and 20/20; blotto not finished).  Sample sizes are 40 games (SE ~ 5-6 points), so only "goalFirst is the important piece (+15..20 points)" is clear;
ehBV / ehN / ehGoal are neutral versus v19 and give a small edge versus the tempo family.  The final keeps all four (it is the configuration that carries the full pool
measurement); the simpler C3 (`P131=0 P134=0` defaults) is a candidate for a next round but was not pool-tested.
Overall h2h of the final candidate vs v19: 86/100 games (51/60 on seeds 12000-12029 + 35/40 on 12030-12049).

Extra checks: ASan+UBSan build of `submit/` ran 2 full games (vs bot18, blitz) without a report.

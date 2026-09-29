# turtle (zoo bot) - defensive / economy-first opponent

Files: `main.cpp` (+ `protocol.hpp`, `generated.hpp` copied from submission17, `submission.json`), binary `bot`.
Build: `g++ -std=c++20 -O2 -Wall -Wextra -o bot main.cpp` (no warnings).
Debug: `TURTLE_LOG=/path/log.txt ./bot` appends per-turn decisions and timing (off by default, stderr is silent).
`-DTUNE` build reads `P<i>` env vars (see `prm(i, default)` calls) for parameter sweeps.

This is a from-scratch decision logic (not a fork of v16/v17). Only the BFS/distance idea and the protocol helper are shared.

## Strategy summary

1. **Economy-first expansion.** Up to 8 flags (F) are produced in the first turns, each flag gets a distinct target chosen by a global greedy
   (flag, building) matching on `value/(distance+3)`. Value = 3 x score + type bonus (ENG 150, HALL 110, DEPOT 90, HOSPITAL 70,
   LIB 60 ...), x1.3 for contested buildings (|dist_enemy_base - dist_my_base| <= 3), x0.6 for buildings clearly closer to the enemy.
   Targets are sticky (a per-cell memory keeps last turn's target) so flags do not dither. A flag only walks to a target where our local
   warriors (W) >= enemy W within 3 + 1 (+2 for enemy-owned buildings). Each flag gets a best-effort 1-2 W escort.
2. **Garrisons on every owned building.**
   * baseline: 1 W per owned building (kills lone flags).
   * hard defense: if an enemy flag is within 1 of a building we own, the W that can reach it this turn must be >= enemy W within 1 + 1,
     otherwise the units are NOT fed piecemeal (they stay available for a counter-strike).
   * time-aware anticipation: an enemy *flag* is the only thing that can take a building, so if an enemy flag is 2..5 steps away we pull
     (enemy W that could accompany it) + 1 warriors toward the building, only if enough warriors can arrive in time.
3. **Mass-aware objectives.** Enemy flags (highest value, especially next to our buildings) and enemy-owned buildings are attacked only when
   `1.2..1.25 x (E1 + 0.5 E23) + 2` warriors are within 7 steps; the assigned units advance time-synchronised (units that are nearer
   than the farthest one wait) so they arrive as one stack. Adjacent superior stacks are struck immediately.
4. **Reserves spread by pressure.** Warriors that are not needed elsewhere are distributed over our buildings, level = 2 + 0.5 x enemy W within
   5, weighted by building value x (1 + threat/10); the remainder gathers at the highest-weight (front) building. Idle flags walk to that hub.
5. **Production.** All resources go to W after: flag replacement (floor of 2 flags until turn 145, up to 8 early), capture-cost reserve
   (`captures this turn - income`), and holding W purchases one turn when an ENG capture is imminent. W are spawned at the base or at an owned
   hospital nearest to unmet garrison demand / the hub.
6. Uses PRIORITY (value order) and never plans a move that overdraws a cell. No scouts, no teleport.

## Measurements (dev seeds 10000..10029, both sides => 60 games per opponent, arena.py)

| opponent | W-D-L (60 games) | win rate | avg score margin | forfeits |
| --- | --- | --- | --- | --- |
| bot8  | 51-0-9  | 85.0 % | +23.2 | 0 |
| bot13 | 49-0-11 | 81.7 % | +15.3 | 0 |
| bot17 | 0-0-60  | 0 %    | -29.1 | 0 |

(`python3 v18-work/arena.py --bot ./v18-work/zoo/turtle/bot --opp ./bot8 ./bot13 ./bot17 --seeds 10000 30 --workers 2`, raw output in `res_final.json`.
Bots are deterministic and mirror-symmetric, so the effective sample is 30 seeds, not 60 games.) Wins over bot8/bot13 are mostly
instant wins (opponent at 0 points) around turns 80-130; losses are mostly wipe-outs on maps where the opponent's early flag rush
wins the central 4-point stations.

Per-turn compute: average 0.3 ms, maximum 11.5 ms (measured while the machine was at load average ~74; first turn ~7 ms incl. all-pairs BFS).
AddressSanitizer/UBSan build ran 10 games (both sides) without a report. stderr is silent unless `TURTLE_LOG` is set.

Development history (informal): v1 (distributed W with 6-8 unit garrisons + one "doom stack") lost 0-12 to bot8 with wipe-outs,
because the stack anchored on the spawn cell, chased far-away flags and abandoned the front; replacing the stack by the objective allocator
(mass-aware, time-synchronised) + pressure-weighted reserves (step 3/4 above) took bot8 from 0 % to 77-85 %. Flag-triggered garrisons,
sticky flag targets, the flag floor and idle-flag migration to the hub each added a few percent (all within seed noise, +-8 %).


## Known weaknesses

* Slow reaction: when an enemy stack of ~20+ W and a flag show up next to a building held by ~10 W, the building falls and is only
  retaken 2-3 turns later; strong opponents (bot17) win the local mass battles and take the turtle apart (0-30 wipe-outs).
* Early expansion is at best equal to the F-rush bots; on maps where the enemy is closer to the 4-point central buildings it falls behind
  by 10 points at turn 20 and rarely recovers against bot16/17-class bots.
* Passive: it never raids far behind enemy lines unless the objective allocator finds a thinly held building.
* Flag targets are dropped when local enemy strength is high, so against strong bots flags idle at the hub.
* ~10-15 % of warriors moves are direction reversals (reserve levels change with the threat estimate).

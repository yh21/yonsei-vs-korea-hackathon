# rusher (zoo opponent for v18 testing)

Files: `main.cpp` (+ copied `protocol.hpp`, `generated.hpp`), binary `./v18-work/zoo/rusher/bot`.
Build: `g++ -std=c++20 -O2 -o bot main.cpp` (no warnings). Decision logic is written from scratch
(only the BFS-style helpers/protocol code are of the same kind as in the v5..v17 lineage; nothing of
`decide()` is forked). Nothing is hard-coded per side or per map: bases, map size, building ids and
directions all come from `INIT`; direction tie-breaks are normalised to the own base (Y/K symmetric).

## Strategy (style: "ENG-first mass-W rush")

1. Opening: T1 spawns 2 flags; the first flag goes for the ENG (value 110), the others for the safest
   home-half buildings (value = type bonus + 20*score - 6*distance). While the ENG is <=4 steps away
   the bot banks money (W cost 3 -> 2), spending only what would overflow the 40 cap.
2. Economy: after ENG almost everything goes into W. Flags are kept at ~3 home capturers + 3..5 escorts
   (cheap relative to W; they die rarely because they never step on a cell where enemy W potential >
   own W support).
3. Army: W recruits pile up on the spawn cell (base or a forward owned hospital nearer to the front)
   until 8 W, then that stack launches and marches by itself ("waves"). Every non-recruit stack picks its
   own target each turn: value(type, score, enemy-owned +25, enemy half +30, enemy flags on it +20 each)
   - 5/step, sticky to the previous target. Per step it evaluates 5 options with an enemy-potential map
   (enemy W within 1 cell + spawn potential next to enemy base/hospitals + station tele); an option is
   "safe" if own W within 1 of it > potential; unsafe options are heavily penalised (dodge sideways /
   toward reinforcements), enemy flags on an option cell give +30 each (F kills are the real profit,
   W trades are always 1:1).
4. Capture parties: a stack (>=8 W) with >=2 flags splits off, for every other enemy/neutral building
   within 9 steps, exactly `enemyW(radius 2)+2` W plus ONE flag (up to 10 parties, keeping >= 6 W or the
   main target's need in the core), and off-building enemy flag stacks get small hunting parties. The
   core waits on a building (<=3 turns) while its flag captures it, or up to 8 turns for flags en route.
5. Minimal home defence: (a) my building with an enemy flag within 4 steps gets a garrison of
   `enemyW(radius 2)+1` W from stacks that can arrive in time; (b) enemy flags standing in my half get
   an intercept party (need = potential + 2). Otherwise no defence and no deliberate retreat
   (stacks only dodge locally when the target cell is unsafe).
6. Flags: capture priority = `PRIORITY` by building value; endangered flags flee to the safest
   neighbour cell; leftover flags follow the nearest stack. Enemy-half targets are only taken when a
   stack is within 2 cells.

## Measurements (arena, seeds 10000..10029 = 60 games per opponent, workers 2-3)

| opponent | result (W-D-L) | winrate | notes |
|---|---|---|---|
| bot8  | 58-0-2 | 0.967 | avg margin +29.8; only losses: seed 10013 (both sides, far-flung map) |
| bot13 | 47-0-13 | 0.783 | avg margin +15.5 |
| bot17 | 2-0-58 | 0.033 | avg margin -29 (instant losses mostly): bot17 out-tempos it economically |

Extra (seeds 10000..10009): bot10 100%, bot12 95%, bot15 ~5%, bot16 0%, bot_opp ~35%.

* forfeits: 0 for the rusher in ~600 games. One arena batch (bot8, 60 games) reported `forfeits=1`
  while showing only 2 (non-forfeit) losses -> that forfeit was bot8's (a WIN for the rusher) under
  machine load average ~65; not reproducible in 300+ later games (tools: `ffscan.py`, `findff.py`).
* time: `-DTIMING` build, measured with replay-fed inputs: max 1.9 ms (first turn, all-pairs BFS),
  average 0.12 ms per turn. stderr is empty in the normal build (debug output only with `-DDBG`).
* all emitted commands were applied by the engine (0 rejected lines in 8.7k checked commands).

## Known weaknesses (useful for v18 testing)

* Loses the economic race to the v15..v17 family: lower income (fewer halls/depots held), no
  garrisons, stacks are dispersed so they lose local fights against a concentrated 20+ stack.
* Tempo on maps with far-away home buildings (seed 10013): flags travel alone for 10+ turns.
* Merging all stacks into one core (tested, `P38`) was worse than independent waves + parties.
* Development helpers in this dir: `ev.sh` (TUNE build A/B), `feed.py` (replays a recorded game's
  inputs into the bot with `-DDBG` stack/option dump), `view.py`, `stacks.py`, `own.py`, `stats.py`,
  `fdeaths.py`, `spent.py`.

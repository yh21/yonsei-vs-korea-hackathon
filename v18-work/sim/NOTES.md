# v18 "sim" candidate - simulation based look-ahead bot

(work in progress notes; the final numbers are filled in at the end of the file)

## Files

| file | role |
| --- | --- |
| `main.cpp` | glue: input -> game state, opponent shadow models, candidate generation, rollouts, output |
| `sim.hpp` | exact C++ re-implementation of the engine turn pipeline (`engine/pipeline.py`) |
| `pol17.hpp` | array based re-implementation of the v15/v16/v17 decision procedure (same code, different weights) |
| `eval.hpp`, `valuecoef.hpp` | state features and the coefficients of the linear value model used at the rollout horizon |
| `protocol.hpp`, `generated.hpp`, `submission.json` | unchanged starter files |
| `tools/` | development harness only (not part of the submission): in-process duel runner, validators, data generation |

## Idea

1. `sim.hpp` reproduces the engine exactly (spawn -> move -> combat -> income -> capture -> reveal -> judge).
   Validated on 21 reference replays (bot14..17, bot_opp): resources, occupation, ownership, units and revealed
   sets identical on every turn (`tools/validate`).
2. `pol17.hpp` is a bit exact port of the v15/v16/v17 bots (they are one code base with different weights).
   `tools/difftest` runs the original bots and the port on the same views: 0 mismatching command lists over
   hundreds of games (both sides). The original bots are therefore *simulable*: our opponents of that lineage become
   a deterministic function we can roll forward.
3. Every turn the bot
   * rebuilds the state (own belief about hidden scores, the opponent's revealed set is tracked by simulating the
     reveal rules),
   * runs three shadow models (v17, v16, v15 weights) on the real state. The model whose predictions of the
     opponent's last move (compared through the resulting unit positions) have the smallest recent error is the
     "current model"; its prediction of this turn's orders is exact if the opponent is of that lineage,
   * generates candidate *doctrines*: parameter sets of our own base policy (the v17 procedure) plus injected
     strike goals, produces the exact first-turn orders of each, and rolls the game forward H turns against the
     opponent model (first opponent ply exact, later plies with a faster approximation of the same procedure),
   * scores the end state with a linear model (regression of the final score margin on economy / material /
     local-superiority features, fitted on self play rollouts, `tools/fit_value.py`),
   * plays the first orders of the best candidate (with a little hysteresis so that noisy estimates do not make the
     doctrine flip every turn).

(See the experiment log below.)

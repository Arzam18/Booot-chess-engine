# Booot 7.4 C++ Port — Stage 8: Evaluation and Move Ordering

This stage connects the incremental NNUE state and special-endgame evaluation into one evaluator, and ports the main move-ordering infrastructure from `uSort.pas`.

## Files added

- `booot_eval.hpp/.cpp` — `evaluate_position`, matching `uEval.Evaluate` ordering and score convention.
- `booot_move_order.hpp/.cpp` — history tables, four continuation-history channels, capture history, killers, countermoves, gravity updates, scoring, and staged move picker.
- `booot_eval_move_order_test.cpp` — evaluation dispatch and move-ordering regression cases.
- `run_stage8_tests.sh` — focused GCC/Clang test command for Stage 8 dependencies.
- `run_stage8_sanitizers.sh` — focused ASan/UBSan test command for Stage 8 dependencies.

The complete `run_all_tests.sh` and `run_sanitizer_tests.sh` scripts are also updated to include Stage 8.

## Evaluation behavior

`evaluate_position` first calls `special_cases`. KBNK, KPK, pawnless, and material-draw positions use `evaluate_special_endgame` directly and do not need a valid NNUE state. All other positions call `evaluate_nnue_state`, then apply the Stage-6 bishop+pawn scaling and the final `WinScore` clamp. Scores are from the side-to-move point of view, consistent with `uEval.pas`.

## Move-ordering behavior

`MovePicker` follows the main `uSort.Next` phase sequence:

1. Hash move (`is_pseudo_correct` validation).
2. Good captures and promotions, ordered by `MVV + CapHistory/16` and filtered by `good_see(..., 0)`.
3. First killer.
4. Second killer.
5. Countermove.
6. Quiet moves, ordered by base history + continuation history + the `ThreatValue` adjustment.
7. Deferred losing captures.

When the side to move is in check, it returns the hash move first when valid, then legal evasions, with captures prioritized as in `ScoreEvasions`.

History storage mirrors the Pascal index ranges `[-King..King]`, squares `[a1..h8]`, capture types `[Empty..King]`, and four `HistorySats` channels. `MoveHistoryContext` exposes the channel and the previous 1/2/4-ply piece/destination contexts explicitly. This preserves the scoring/update equations while leaving the original search's dynamic choice of context flags for the later `uSearch` port.

## Commands

Focused new-stage tests:

```sh
./run_stage8_tests.sh
CXX=clang++ ./run_stage8_tests.sh
```

Focused new-stage sanitizers:

```sh
./run_stage8_sanitizers.sh
```

Accumulated regression commands:

```sh
./run_all_tests.sh
QUICK=1 ./run_all_tests.sh
./run_sanitizer_tests.sh
```

`QUICK=1` replaces deep perft depths with depth-1 smoke vectors; the other accumulated regressions still run. The accumulated host suite was attempted here but exceeded the 600-second execution limit during the Stage 8 step. The accumulated sanitizer suite also exceeded the 600-second limit during exhaustive attack-table sanitizer testing. Neither incomplete run is reported as a full-suite pass. Confirmed results for this stage are the standalone Stage 8 tests on GCC and Clang 17 and the standalone Stage 8 ASan/UBSan test.

## Test coverage

- KPK special-case detection; KBNK and material-draw evaluation dispatch without a network pointer. Actual KPK table/evaluation regression remains covered by the Stage-6 endgame test.
- Ordinary-position NNUE evaluation with a synthetic zero-weight test model.
- Stale NNUE-state rejection and final `WinScore` clamp.
- History gravity update, continuation-score weighting, DepthInc bonus, killer and countermove storage.
- Hash → killer1 → killer2 → countermove → quiet ordering.
- `skip_quiets` behavior.
- Good capture priority and deferred losing-capture behavior.
- Check evasions are legal and duplicate-free.

The original `.nn` files are not in the recovered upstream archive, so real-network score parity remains pending. The synthetic network in the evaluator test is only a test fixture and must never replace `booot2.nn`, `booot4.nn`, or `booot58.nn`.

## Remaining work

Stage 8 does not yet implement the alpha-beta search driver. `NextFV`, `NextProbCut`, search pruning/extensions/reductions, search-driven full history updates, correction history, TT storage/replacement, repetition, threads, time management, UCI, and Android packaging remain for subsequent stages. No Android executable or phone test is claimed.

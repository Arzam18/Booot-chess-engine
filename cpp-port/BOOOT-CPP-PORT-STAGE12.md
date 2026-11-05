# Booot 7.4 C++ Port — Stage 12

## Scope: dedicated FV / NextFV search

Stage 12 ports the active Pascal `FV` and `NextFV` path from `original_pascal/uSearch.pas` and `original_pascal/uSort.pas`, and makes it the C++ search core's leaf verifier.

### Search integration

- `alpha_beta()` now enters `fv_search()` when depth reaches zero or below, matching Pascal `Search()` delegating to `FV()`.
- The razoring verification now uses `FV(0)` rather than the older generic quiescence routine.
- ProbCut's first narrow-window verification now uses `FV(0)` before the depth-minus-four alpha-beta verification.
- ProbCut candidate selection is restricted to capture-flag moves, matching `NextProbCut`; a non-capturing promotion is not treated as a ProbCut capture.

### Ported FV behavior

- TT probe, q-depth eligibility (`0` when depth is non-negative or in check, otherwise `-1`), and bound-aware fail-high/fail-low cutoffs.
- Static evaluation / stand-pat handling, hash-score refinement, Booot's `DeltaMargin = 80`, and source-compatible TT lower/upper storage.
- `NextFV` ordering: pseudo-correct hash move first; ordered captures and promotions; quiet checking moves at non-negative depth; all legal evasions while in check.
- SEE filtering at margin `0`, and futility SEE filtering at margin `1`.
- Piece futility values copied from `uBoard.pas`: pawn 100, knight 360, bishop 370, rook 600, queen 1150.
- Shallow quiet-move limit after the second searched candidate, in the same conditions as `FV`.
- Deep recapture-only filtering for generated capture/promotion candidates at depth `-5` and below, while leaving the hash move and check evasions untouched.
- Mate detection when no legal evasion exists, path repetition / rule-50 handling, NNUE make/unmake, PV propagation, and stop/node-limit propagation.

`quiescence()` remains in the source as a separate generic implementation, but the primary alpha-beta leaf, razoring, and ProbCut paths now use `fv_search()`.

## Diagnostics and regression tests

`SearchResult` now reports:
- `fv_calls`
- `fv_beta_cutoffs`
- `fv_quiet_checks`
- `fv_check_nodes`
- `fv_evasions`

The new deterministic tests verify that FV actually searches quiet checks at depth zero, reaches the in-check evasion stage, counts a beta cutoff, preserves the root board, and handles a forced one-evasion position. Existing ProbCut and singular-extension fixtures remain in the search test.

## Fidelity boundaries

This is a port of the active `FV` / `NextFV` search flow, not a claim of bit-for-bit search identity. Remaining differences include potential ordering differences between Pascal `ScoreCaptures` and the C++ capture-history scorer, platform/compiler integer and floating-point behavior elsewhere in evaluation/search, and any source paths not yet ported. Pascal `FVQuiet` is a separate utility used by the original offline `FVEval` routine; it is not part of the normal `Search` leaf path and is not implemented in this stage.

## Verification on the final Stage 12 tree

- `./run_stage12_tests.sh` — GCC: PASS.
- `CXX=clang++ ./run_stage12_tests.sh` — Clang 17: PASS.
- `QUICK=1 CXX=g++ ./run_all_tests.sh` — cumulative Stage 1–12 host suite: PASS. Quick mode skips deep perft vectors.
- `CXX=g++ ./run_stage12_sanitizers.sh` — GCC ASan/UBSan: PASS.
- A combined recheck command once hit its execution time limit during the Stage 4 regression; the separately rerun cumulative quick suite completed successfully. This was a tooling timeout, not a reported assertion failure.

The focused and cumulative search diagnostics use the test evaluator unless the dedicated synthetic NNUE test is running. These results do not establish parity against Booot's original trained networks.

## Still not complete

The project remains a search-core port, not a complete UCI engine or Android executable. Remaining work includes additional IIR/history-pruning details, full root move ordering and time management, multi-thread scheduling, UCI orchestration, original NNUE model parity, Android NDK build, and DroidFish/Termux runtime validation.

The original Pascal tree remains under `original_pascal/` and is byte-identical to Stage 11. The recovered source archive still does not contain `booot2.nn`, `booot4.nn`, `booot58.nn`, or `resource.res`; no replacements were fabricated. The available `resource.rc` and `booot7.res` are retained unchanged.

## Commands

```bash
./run_stage12_tests.sh
CXX=clang++ ./run_stage12_tests.sh
CXX=g++ ./run_stage12_sanitizers.sh
QUICK=1 CXX=g++ ./run_all_tests.sh
```

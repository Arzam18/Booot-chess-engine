# Booot 7.4 C++ Port — Stage 11

## Scope

Stage 11 adds the next two selective-search mechanisms from `original_pascal/uSearch.pas`:

### ProbCut

- Source constants: `ProbCutDepth = 5`, `ProbCutRed = 4`, `ProbCutMargin = 85`.
- Runs only at non-PV, non-check nodes at the configured depth and outside mate-score windows.
- Uses the original bad-ProbCut transposition-table guard.
- Tries the hash move first, followed by captures/promotions from the staged move picker.
- Requires legality and `good_see(move, position, new_beta - static_eval)`.
- Performs a narrow-window quiescence verification; only a fail-high proceeds to the reduced alpha-beta verification at `depth - 4`.
- A verified fail-high stores a TT lower bound at `depth - 3` (the source's `newdepth + 1`).

**Fidelity note:** the original Pascal gate calls its dedicated `FV` / `NextFV` search. This stage currently uses the existing C++ quiescence routine as that first narrow-window gate, followed by the reduced-depth alpha-beta verification. `FV` has additional quiet-check, futility, and move-selection behavior, so an exact `FV` port remains future work. The ProbCut constants, guards, SEE threshold, reduction, and lower-bound storage are represented here; bit-for-bit search equivalence is not claimed.

### Singular extensions

- Source threshold: `SingularDepth = 6`.
- Requires a hash move, a non-mate TT value, a lower-bound-capable TT entry (including exact entries), and `hash_depth >= depth - 3`.
- Verifies alternatives at the same ply with the hash move excluded and a null-window threshold of `hash_value - depth`.
- A fail-low extends the hash move by one ply; sufficiently strong non-PV fail-lows can extend by two while respecting the inherited double-extension counter (`<= 6`).
- Source negative-extension cases are also represented.
- Quiet singular moves reduce the subsequent LMR reduction by one, matching the source's `quietsingular` adjustment.
- The exclusion search bypasses TT cutoffs, null-move pruning, ProbCut, and writes to the excluded node's TT/correction-history entry. Its repetition-path handling enters the same board position only once.

`SearchResult` exposes diagnostic counters for ProbCut attempts/cutoffs and singular tests/extensions. They reset for each `search()` call.

## Files changed in Stage 11

- `booot_search.hpp`
- `booot_search.cpp`
- `booot_search_test.cpp`
- `run_all_tests.sh`
- `run_stage10_tests.sh` (label updated because its search test now includes Stage 11 regressions)
- `run_stage10_sanitizers.sh` (label updated for the expanded regression bundle)
- `run_stage11_tests.sh`
- `run_stage11_sanitizers.sh`
- `BOOOT-CPP-PORT.md`
- `BOOOT-CPP-PORT-STAGE11.md`
- `README-STAGE11.txt`
- Stage 11 inventory, changelog, and checksum manifests

## Verification

- `./run_stage11_tests.sh` — GCC: PASS.
- `CXX=clang++ ./run_stage11_tests.sh` — Clang 17: PASS on the final source tree.
- `QUICK=1 ./run_all_tests.sh` — GCC: PASS across the Stage 1–11 regression set. Quick mode skips the deep perft vectors.
- `./run_stage11_sanitizers.sh` — GCC ASan/UBSan: PASS on the final source tree.
- Clang full quick regression suite: not completed; only the focused Stage 11 suite was completed under Clang. The full quick run exceeded the execution window.

The tests include a tactical fixture that reaches an actual ProbCut cutoff and a checked position with one legal evasion that exercises the excluded-move singular extension. The search integration tests use a diagnostic material evaluator except for the separate synthetic NNUE-state test; they do not prove parity against Booot's original trained networks.

## Still not complete

This remains a search-core port, not yet a complete UCI engine or Android executable. The dedicated Pascal `FV` / `NextFV` routine, remaining IIR/history-pruning details, full root move/time management, multi-thread scheduling, UCI orchestration, original NNUE model parity, NDK build, and DroidFish/Termux runtime validation remain outstanding.

The original Pascal files in `original_pascal/` are carried forward unchanged from Stage 10. The recovered Stage 10 source archive did not contain `booot2.nn`, `booot4.nn`, `booot58.nn`, or `resource.res`; Stage 11 does not manufacture or modify those missing binary assets. `resource.rc` and `booot7.res` remain in the retained Pascal source tree.

## Commands

```bash
./run_stage11_tests.sh
CXX=clang++ ./run_stage11_tests.sh
./run_stage11_sanitizers.sh
QUICK=1 ./run_all_tests.sh
```

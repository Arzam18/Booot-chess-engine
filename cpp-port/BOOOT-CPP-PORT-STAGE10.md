# Booot 7.4 C++ Port — Stage 10

## Scope

Stage 10 ports Booot's three-part correction-history evaluation from `uSearch.pas` / `uSort.pas` into the C++ search core.

- Pawn correction history: 2 sides × 32768 entries.
- White non-pawn-material correction history: 2 × 32768.
- Black non-pawn-material correction history: 2 × 32768.
- Signed 16-bit gravity cells; `CorrDiv=512`, `CorrLimit=128`.
- Evaluation scaling preserves Booot's integer operation order: rule-50 scaling first, then pawn, White non-pawn, and Black non-pawn corrections.
- Updates are limited to quiet best moves outside check and use the node's bound relation to static evaluation.
- `clear_history()` now clears move history and all three correction tables.

The Pascal source constants are taken from `uSort.pas`: `CorrDiv=512`, `CorrLimit=CorrDiv div 4`, `CorrSize=32768`. `ScaleEval` and the update conditions are based on `uSearch.pas`.

## Files changed

- `booot_search.hpp`
- `booot_search.cpp`
- `booot_search_test.cpp`
- `run_stage10_tests.sh`
- `run_stage10_sanitizers.sh`

## Tests

Focused Stage 10 search tests pass under GCC and Clang 17. The test includes multiple iterative searches and verifies `clear_history()` lifecycle. The full Stage 1–10 sanitizer run was attempted but exceeded the available execution window; it is not marked as passed.

Run:

```bash
./run_stage10_tests.sh
CXX=clang++ ./run_stage10_tests.sh
./run_stage10_sanitizers.sh
```

## Not yet ported

ProbCut, singular extensions, correction of the remaining search heuristics, full root/time management, multi-thread scheduling, and UCI orchestration are not included in Stage 10. This is still not a complete UCI engine or Android executable.

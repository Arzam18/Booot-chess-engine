BOOOT 7.4 C++ PORT — STAGE 13

New search features:
  * Internal iterative reduction (IIR), two-ply depth reduction.
  * Optional late-move count pruning (LMP), with Pascal thresholds.
  * Two-previous-ply continuation-history pruning.
  * Explicit cut-node propagation, separate from null-move permission.

Original feature defaults preserved:
  SearchLimits.enable_iir = false
  SearchLimits.enable_lmp = false

Run focused tests:
  ./run_stage13_tests.sh
  CXX=clang++ ./run_stage13_tests.sh
  ./run_stage13_sanitizers.sh

Run all cumulative quick regressions:
  QUICK=1 ./run_all_tests.sh

QUICK=1 skips deep perft; it does not claim the deep perft suite passed.

The complete ZIP contains all stages and original_pascal/. The modified-only
ZIP is an overlay for the Stage 12 complete-source tree. This remains a
host-tested C++ port foundation, not yet a UCI executable or Android build.

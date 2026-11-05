BOOOT 7.4 C++ PORT — STAGE 12

Stage 12 ports the active Pascal FV / NextFV search routine into C++ and
integrates it at alpha-beta leaves, razoring verification and the first
ProbCut verification gate.

Main additions:
- TT-aware FV search with source q-depth rules and stand-pat behavior.
- NextFV ordering: hash move, captures/promotions, quiet checks, evasions.
- Booot DeltaMargin 80, futility piece values, SEE filters and shallow quiet
  limit, plus depth -5 recapture-only filtering.
- ProbCut now verifies through FV(0) and only tries capture-flag candidates.
- FV diagnostic counters and deterministic quiet-check / evasion regressions.

Tests completed on the final source tree:
- GCC Stage 12 focused tests: PASS.
- Clang 17 Stage 12 focused tests: PASS.
- GCC Stage 1–12 cumulative QUICK=1 regression suite: PASS (deep perft skipped).
- GCC ASan/UBSan Stage 12 search tests: PASS.

Fidelity boundary: capture scoring may not be identical to Pascal ScoreCaptures;
Pascal FVQuiet (used by its offline FVEval utility) is not ported. This is not
yet a complete UCI engine or Android executable. Original NNUE binaries and
resource.res remain absent from the recovered source archive and were not
fabricated. Original Pascal files remain unchanged under original_pascal/.

Run:
  ./run_stage12_tests.sh
  CXX=clang++ ./run_stage12_tests.sh
  CXX=g++ ./run_stage12_sanitizers.sh
  QUICK=1 CXX=g++ ./run_all_tests.sh

BOOOT 7.4 C++ PORT — STAGE 11

Stage 11 adds ProbCut and singular extensions to the single-thread search core.

Main changes:
- ProbCut constants 5 / 4 / 85 taken from original uSearch.pas.
- TT safety guard, hash-move-first selection, legal capture/promotion filtering,
  SEE threshold, narrow-window gate, depth-minus-four verification and TT lower
  bound storage.
- Singular-depth threshold 6, same-ply excluded-hash-move verification,
  single/double extensions, negative extensions and quiet-singular LMR relief.
- Search diagnostics count ProbCut and singular-search events.

Fidelity limitation:
The Pascal engine uses a dedicated FV/NextFV verification search. The current
C++ port uses the available quiescence function for the first ProbCut gate; the
Pascal FV quiet-check/futility details are not yet ported. This stage does not
claim bit-identical search behavior.

Run focused tests:
  ./run_stage11_tests.sh
  CXX=clang++ ./run_stage11_tests.sh
  ./run_stage11_sanitizers.sh

Run the cumulative quick regression set:
  QUICK=1 ./run_all_tests.sh

Confirmed on the final source tree: GCC and Clang focused Stage 11 tests, GCC
cumulative quick regressions, and GCC ASan/UBSan. The Clang full quick suite was
not completed. Deep perft is skipped by QUICK=1.

This is still not a complete UCI/Android build. Original NNUE network binaries
and resource.res were not in the recovered Stage 10 source archive and are not
created or altered here. The original Pascal source is retained under
original_pascal/.

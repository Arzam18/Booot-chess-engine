# Booot 7.4 C++ Port — Stage 20: Search-fidelity and strength-recovery corrections

## Goal

Stage 20 targets search-path mismatches found by comparing the Stage 19 C++
search implementation with the preserved original Pascal `uSearch.pas` and
`uSort.pas`. The focus is playing-strength correctness, not higher NPS.

## Corrections

### 1. Raw static evaluation in the transposition table

- Original Pascal stores the raw NNUE `StaticEval` in the TT and applies
  `ScaleEval` when the entry is consumed in the current position.
- Stage 19's C++ path stored the already-scaled score. That score is position
  state-dependent: rule-50 scaling and correction-history terms can change
  while the position hash remains the same.
- `SearchEngine::evaluate()` now returns the raw evaluator score. Search nodes
  scale it at the point of use, while all static-eval TT writes preserve the
  raw value. TT hits are therefore re-scaled with the current rule-50 clock and
  correction histories.
- The scaled, unadjusted static evaluation remains the value used for the
  improving test, stack static-eval history, and correction-history update; a
  more favorable TT bound only changes the local pruning `Eval`, matching the
  Pascal separation between `Tree[ply].StatEval` and `Eval`.

### 2. LMR formula and history input

- Retained the original `LMRReduction` table formula and the non-improving
  one-third increase.
- The C++ LMR helper now returns a signed reduction. For a negative reduction,
  the child search depth becomes `newdepth + 1`, matching Pascal's `D` logic;
  the result is no longer clamped to zero. The reduced depth is still clamped
  to at least one.
- LMR history is now `2 * quiet_history + previous1 + previous2 + previous4`,
  using the continuation-history cells once each. This matches Pascal
  `GetHistoryValue`; the move-ordering score uses different weights and was
  not a valid substitute.
- Restored the cut-node `+2` adjustment, the killer/counter adjustment only
  outside cut nodes, and the separate low-PV condition. Low-PV recognizes
  upper-bound and exact TT types, requires a hash move, and checks that the TT
  depth covers the current depth.
- Re-search depth changes after a successful reduced search now follow the
  Pascal rules (`bestvalue + newdepth`, then `bestvalue + 50`) before deciding
  whether another zero-window search is needed.

### 3. Root LMR

- Root LMR applies the `-2` adjustment only for a PV window, as in
  `RootSearch`.
- A successful reduced root search is re-searched at the original child depth,
  with the original `+80` improvement extension. The adjusted depth is also
  retained for the following PV full-window search.
- Fail-low reduced root moves are not unnecessarily re-searched.

## Validation performed in this workspace

- GCC complete Stage 1–15 host test suite: PASS.
- Clang complete Stage 1–15 host test suite: PASS.
- GCC Stage 19 focused fixed-buffer/compact-undo suite: PASS.
- Clang Stage 19 focused suite and AArch64 baseline-plus-target-attribute SDOT
  disassembly probe: PASS.
- Existing GCC ASan/UBSan Stage 1–8 suite: PASS.
- Additional GCC ASan/UBSan build and execution of the search and UCI test
  suites: PASS.

## What is not yet verified

- No Android NDK executable build or Galaxy A16 runtime test is claimed here.
- The recovered Nov 6 Stage 19 archive does not contain the original production
  `booot2.nn`, `booot4.nn`, and `booot58.nn` networks. Those must be present in
  the repository root for the existing Android workflow to produce the
  self-contained production engine.
- No match-based Elo result, human-game result, or Pascal-vs-C++ strength
  parity is claimed. These are search-fidelity corrections with host regression
  coverage; playing strength must still be measured with the real networks and
  controlled matches on the target build.

## Recommended next validation

Build the Android artifact with the exact original three networks, then test on
the Galaxy A16 with one thread, fixed hash, the same time control and opening
book, and comparable thermal conditions. Record depth, nodes, time, NPS, and
match score separately. Do not interpret depth or NPS alone as a strength
measurement.


## Stage 20.1 Android compile-error hardening

A GitHub Actions ARM64 build reported missing `Undo::moving_piece` and
`Undo::key` members at search call sites. The search code no longer relies on
those fields: it records the moving piece and parent hash from `Position`
before `make_child()` mutates the board. This is semantically equivalent for
history indexing and repetition-path tracking and keeps those search concerns
independent from the board's internal undo representation. `booot_search.hpp`
also includes `booot_board.hpp` directly because its declarations use `Position`
and `Undo`.

The modified-files-only overlay now carries the Stage 19 compact-undo board and
NNUE compatibility files as well as the Stage 20 search changes. This prevents
a Stage 20 search source from being applied to a repository still using an
older board/undo API.

Additional validation after this correction:
- GCC full Stage 1–15 suite, including complete perft vectors: PASS.
- Clang quick Stage 1–15 suite: PASS.
- GCC/Clang Stage 19 compact-undo and SDOT code-generation checks: PASS.
- GCC ASan/UBSan search and UCI test binaries: PASS.
- Android NDK GitHub Actions build: NOT RUN locally (NDK is not installed in
  this environment); the exact CI workflow must be rerun after applying the
  overlay. Production network files are still required by that workflow.

# Booot 7.4 C++ Port — Stage 9: Core Search

## Scope

This stage adds a one-thread alpha-beta search core to the accumulated C++ port. It is a staged translation of the central search path in the recovered `uSearch.pas`, not a claim that all 1,872 lines of the Pascal search unit or the complete engine have been ported.

New files:

- `booot_search.hpp`
- `booot_search.cpp`
- `booot_search_test.cpp`
- `run_stage9_tests.sh`
- `run_stage9_sanitizers.sh`

Updated files:

- `booot_move_order.cpp` — fixes a phase-transition bug found by the search regression: the quiet-move phase now continues returning all eligible quiet moves before advancing to losing captures.
- `run_all_tests.sh` — compiles and runs the Stage 9 search test as part of the accumulated nine-stage regression.

## Implemented search functionality

- Iterative deepening with aspiration windows from depth 5.
- Negamax alpha-beta with principal-variation search and null-window re-search.
- Quiescence search, including full legal evasions when in check.
- Four-entry clustered transposition table, age-aware replacement, lower/upper/exact bounds, move/static-evaluation storage, and mate-distance normalization.
- Repetition detection using supplied prior position keys and the current search path.
- Fifty-move draw handling.
- Null-move pruning with depth-dependent reduction and a verification search at deeper nodes.
- Static/razor pruning, shallow futility and late-move-count pruning, SEE-based capture pruning, and late-move reductions.
- Hash move, captures/promotions, killers, countermove, history-scored quiets, and losing-capture phases through the Stage 8 move picker.
- Node limit and externally supplied atomic stop flag.
- PV collection, legal fallback move if interrupted before a completed iteration, and result/error reporting.
- Real NNUE state integration through the Stage 7 board+NNUE make/unmake wrappers. A diagnostic evaluator hook is provided for deterministic tests only.

## Source-fidelity limits / not included yet

The following parts of the original search/engine are deliberately not represented as complete in this stage:

- ProbCut and its dedicated move-picker path.
- Singular extensions, double extensions, and the original IIR rules.
- The complete correction-history subsystem and all original continuation-history update rules.
- Exact parity for every Pascal search pruning formula / interaction; this is a first integrated core implementation whose behavior still needs playing-strength and source-by-source tuning.
- Original multi-thread scheduling and `uThread.pas` integration.
- Full root-search orchestration, ponder/time management, UCI protocol output, benchmark mode, Syzygy/tablebase support, and opening-book handling.

The implementation is single-threaded. It is not yet a UCI executable and not yet an Android build.

## Build and test

Requirements: C++20, GCC or Clang, Bash.

Focused search test:

```sh
./run_stage9_tests.sh
```

Clang focused search test:

```sh
CXX=clang++ ./run_stage9_tests.sh
```

Stage 9 AddressSanitizer + UndefinedBehaviorSanitizer:

```sh
./run_stage9_sanitizers.sh
```

Accumulated quick regression (deep perft disabled explicitly):

```sh
QUICK=1 ./run_all_tests.sh
```

The normal `./run_all_tests.sh` retains the deep Stage 3 perft checks and may take substantially longer. The quick command passed all nine stage groups in the final verification for this archive.

## Verification recorded for this package

- GCC Stage 9 focused tests: PASS.
- Clang 17 Stage 9 focused tests: PASS.
- Stage 9 ASan/UBSan focused tests: PASS.
- Accumulated Stage 1–9 `QUICK=1` regression: PASS.
- Exhaustive Stage 2 slider subsets were still run in quick mode: 5,248 bishop subsets and 102,400 rook subsets.
- Stage 3 quick mode runs its smoke perft vectors and deliberately skips deep perft counts.
- Synthetic-network search integration test: PASS. This confirms the search is wired to incremental NNUE state safely; it does not establish score parity with the original `.nn` assets.

## Basic integration example

Keep all three original network objects alive while the search engine uses them:

```cpp
booot::Network net2, net4, net58;
std::string error;
if (!booot::load_network("booot2.nn", net2, error) ||
    !booot::load_network("booot4.nn", net4, error) ||
    !booot::load_network("booot58.nn", net58, error)) {
    // Report `error` and stop initialization.
}

booot::NetworkSet networks{{&net2, &net4, &net58}};
booot::Position position;
if (!booot::parse_fen(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        position, error)) {
    // Report `error` and stop initialization.
}

booot::NNUEState nnue{};
booot::SearchEngine search(networks, 64); // 64 MiB transposition table
booot::SearchLimits limits;
limits.depth = 12;
booot::SearchResult result = search.search(position, nnue, limits);
if (!result.error.empty()) {
    // Report `result.error`.
} else if (result.best_move != 0) {
    const std::string bestmove = booot::move_to_uci(result.best_move);
}
```

This is an API usage example, not a complete UCI loop. Use an atomic stop flag in `SearchLimits::stop` for cancellation from a future UCI driver.

## Assets to preserve

Keep `booot2.nn`, `booot4.nn`, `booot58.nn`, and the fork's `resource.res` unchanged. The recovered source archive does not include the three original networks. The synthetic test network is test data only. The Windows resource container is not needed by the planned Android executable when the `.nn` files are loaded directly.

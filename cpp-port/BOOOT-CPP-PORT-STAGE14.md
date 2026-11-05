# Booot 7.4 C++ Port — Stage 14

## Scope

Stage 14 replaces the prior whole-tree-at-root driver with a separate root search and ports the main root iterative-deepening and time-control behavior from `original_pascal/uSearch.pas` (`Iterate`, `RootSearch`) and `original_pascal/uUci.pas` (`SetRemain`). The existing interior alpha-beta, FV/NextFV, TT, history, ProbCut, and singular-search implementation remains in place.

## 1. Explicit root search

`SearchEngine::root_search()` now:

- Iterates the legal root move list separately from interior alpha-beta, matching the Pascal `RootSearch` structure.
- Retains the generated legal root order and promotes the current best move to index zero between iterations, matching Pascal `UpdateList`. It does not run the staged interior `MovePicker` at the root.
- Searches the first root move at full depth and full window; later moves use PVS null-window searches.
- Builds the root PV by prefixing the selected root move to the child PV.
- Pushes the root position into the repetition path while searching each child, so prior-position repetition history remains visible below the root.
- Stores the root result in the transposition table with the appropriate upper/lower/exact bound.
- Preserves a legal fallback if a hard stop interrupts the first iteration. Rule-50 and root repetition draws retain the earlier no-best-move behavior.

## 2. Root late-move reductions and history

The root loop now uses Booot's LMR formula for quiet moves:

- Root depth must be at least 3.
- The fourth and later root moves are candidates (`j > 2` in the Pascal zero-based loop).
- Captures and promotions are excluded from the quiet-move reduction.
- The root is treated as improving; the Pascal root-PV adjustment subtracts two from the base reduction.
- Hash-capture, gives-check, and root quiet-history adjustments are applied.
- A reduced-depth null-window search is followed by a full-depth null-window re-search when it raises alpha, then a full-window PVS re-search when required.

Root best-move and failed-move history are updated after a non-aborted root search. `SearchResult::root_lmr_reductions` and `root_lmr_researches` expose the root LMR activity; `root_move_searches` counts searched root moves.

## 3. Iterative deepening and aspiration windows

The root driver now follows the Pascal aspiration pattern:

- Full window through depth 5.
- At depth 6 and later, a window of `previous_score ± 10`.
- Fail-low moves the upper bound to the old-window midpoint and resets the reduced retry-depth adjustment.
- Fail-high expands beta and increments the aspiration delta by `delta / 4 + 2`.
- For a fail-high with `abs(score) < 1200`, the retry depth is reduced by one (`hbc`), matching Pascal `Iterate`; fail-low resets this adjustment.
- The last completed iteration remains the stable reported result if a later iteration is interrupted.

`SearchResult::aspiration_fail_lows` and `aspiration_fail_highs` record retries.

## 4. Time controls

`SearchLimits` now accepts:

- `move_time_ms` for a fixed move-time budget.
- `time_left_ms`, `increment_ms`, and optional `moves_to_go` for clock-based allocation.
- `move_overhead_ms` for a safety reserve.
- Explicit `soft_time_limit_ms` and `hard_time_limit_ms` overrides.

Automatic allocation is:

- Fixed movetime: available time after overhead; soft budget is 3/4 of available time and hard budget is all available time.
- Clock mode: available time after overhead divided by `moves_to_go` (default 30), plus 3/4 of the increment for the soft budget. The hard budget is capped by available time and is the larger of three times the soft budget or soft budget plus one increment.
- Explicit soft/hard values take precedence over automatically calculated values. If only one is supplied, the other is derived conservatively.

The soft deadline is checked only after a completed iterative-deepening pass. The hard deadline is checked periodically during search (every 256 counted nodes) and forcibly at root-move/iteration boundaries. A hard timeout sets `stopped` and `time_limit_hit`; the engine returns the last completed result or a legal fallback. `SearchResult` reports elapsed time and both selected budgets.

The clock-allocation formula is an engine-level baseline, not a claim of bit-for-bit equivalence with every front-end-specific `game.time`/`game.rezerv` policy in the Pascal UCI shell. The future UCI layer must map incoming `go` parameters to these `SearchLimits` fields.

## Tests and verification

- GCC focused Stage 14 search tests: PASS.
- Clang 17 focused Stage 14 search tests: PASS.
- GCC cumulative Stage 1–14 quick regression: PASS (`QUICK=1`; deep perft vectors are skipped).
- GCC AddressSanitizer + UndefinedBehaviorSanitizer Stage 14 search tests: PASS.
- Root search test verifies the returned best move and every PV move are legal, root position is unchanged, and at least the initial legal root list was searched.
- Depth-10 start-position regression verifies that root LMR reductions are reached.
- Fixed 1 ms movetime regression verifies hard-deadline interruption, `time_limit_hit`, and legal fallback.
- Explicit soft/hard deadline regression verifies soft stop between completed iterations without a hard-timeout flag.
- Clock-plus-increment regression verifies a 1,000 ms clock, 1,000 ms increment, 10 moves-to-go, and 50 ms overhead allocate 845 ms soft / 950 ms hard.
- Existing root-draw and node-limit behavior remains covered.
- Complete-source archive was freshly extracted; its full SHA-256 manifest verified and its focused Stage 14 tests passed.
- Modified-only archive was applied to a fresh Stage 13 tree with overwrite enabled; its overlay SHA-256 manifest verified and its focused Stage 14 tests passed.
- `original_pascal/` compares byte-for-byte with the Stage 13 copy.

## Fidelity boundaries

The root loop is now separate, matching the original Pascal architecture. Accordingly, root-level ProbCut and singular-extension counter expectations from the former whole-tree root search were removed; those mechanisms remain in interior alpha-beta where the Pascal `Search` function implements them. The root search does not claim multi-thread, MultiPV, ponder-hit, or every shell-specific reserve/stability policy. Aspiration retry-depth reduction and root LMR are ported using the source constants and formulas; exact search-tree identity is not claimed because the C++ move generator, NNUE resource availability, and surrounding engine integration are not yet parity-verified.

This is still a host-tested search library foundation, not a finished UCI engine or Android executable. Remaining work includes UCI protocol/orchestration, original NNUE binary parity, multi-thread scheduling, Android NDK compilation, and device validation. `FVQuiet` remains a separate offline Pascal utility and is not part of the active search path.

The original Pascal source under `original_pascal/` is retained unchanged. The recovered source archive did not contain `booot2.nn`, `booot4.nn`, `booot58.nn`, or `resource.res`; none has been fabricated.

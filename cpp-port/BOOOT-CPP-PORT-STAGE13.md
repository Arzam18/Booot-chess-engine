# Booot 7.4 C++ Port — Stage 13

## Scope

Stage 13 ports three search refinements identified in the original `original_pascal/uSearch.pas` and integrates them into the C++ alpha-beta search.

### 1. Internal iterative reduction (IIR)

Source behavior:
- PV node with no TT move: reduce depth by two when depth is at least 3.
- Cut node with no TT move: reduce depth by two when depth is at least 7.
- No IIR while running the singular excluded-move verification.

The C++ search now carries a distinct `cut_node` flag through recursive search calls; it is not inferred from the null-move permission. `SearchLimits::enable_iir` controls the feature and defaults to `false`, matching the original `game.doIIR := False` initialization. `SearchResult::iid_reductions` reports how often an IIR reduction was applied.

### 2. Late-move count pruning (LMP)

The Pascal limits are ported:
- `CountMoveDepth = 16`.
- Improving threshold: `5 + depth * depth`.
- Non-improving threshold: banker's-rounded `(5 + depth * depth) / 2`.
- Pruning only quiet non-checking moves, when the side has non-pawn material and a finite best value.

`searched` now counts each legal candidate before the selective-pruning checks, matching the source's increment order. `SearchLimits::enable_lmp` defaults to `false`, matching `game.doLMP := False`. `SearchResult::lmp_prunes` records skipped moves.

### 3. Continuation-history pruning

The original source prunes a quiet non-checking move when:
- Its predicted depth after the base LMR reduction is below `HistoryDepth = 3`.
- Both the previous-ply and second-previous-ply continuation-history entries for the current move are negative.
- The node has not already failed so low that pruning is disabled, and the side has non-pawn material.

The `MoveHistory::continuation_score()` accessor exposes the individual continuation table entry without mixing it with main history or other continuation distances. The search applies the two-context condition and records `SearchResult::history_prunes`.

## Tests

- GCC focused Stage 13 search tests: PASS.
- Clang 17 focused Stage 13 search tests: PASS.
- IIR test forces deterministic TT misses during a diagnostic search and confirms reductions occur when enabled.
- Default-option test confirms IIR and LMP counters remain zero when their flags are omitted.
- LMP-enabled test confirms move-count pruning occurs in a depth-7 start-position search.
- Continuation-history accessor test verifies signed values and context indexing for the preceding two plies. It validates the two history inputs directly; a deterministic full-search fixture that forces `history_prunes` itself is not yet included.
- GCC cumulative Stage 1–13 quick regression suite: PASS (`QUICK=1`; deep perft skipped).
- GCC AddressSanitizer + UndefinedBehaviorSanitizer Stage 13 search tests: PASS.
- Complete-archive fresh-extraction and modified-only overlay tests: PASS.
- Original Pascal source directory compares byte-for-byte against Stage 12.

## Fidelity boundaries

IIR and LMP are deliberately disabled by default to preserve the original Pascal defaults. The separate cut-node flag is propagated through the current single-thread alpha-beta call graph. The root driver remains the C++ iterative-deepening/aspiration implementation already present; full Pascal RootSearch ordering and time management are not part of this stage. Continuation-history pruning uses the two corresponding continuation contexts; a bit-for-bit equivalence claim for all search node ordering is not made. Its history-table inputs are unit-tested, but the runtime `history_prunes` counter has not yet been forced by a deterministic full-search test.

This is still a host-tested search library foundation, not a finished UCI engine or Android executable. UCI, complete root/time management, multi-thread scheduling, original NNUE binary parity, and Android NDK/device validation remain future stages.

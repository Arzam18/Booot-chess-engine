# Booot 7.4 C++ Port — Stage 19: Search and NNUE hot-path optimization

## Why this stage exists

The Stage 18 executable loaded the three original networks correctly, but a 5-second depth comparison suggested a performance gap. Depth alone is not an engine-speed metric: pruning, evaluation cost, TT contents, hash size, root position, and search semantics all affect the reported depth. This stage therefore makes targeted implementation improvements without changing search pruning thresholds or NNUE arithmetic semantics.

## Changes

### 1. Compact board make/unmake

- Replaced the full `Position` snapshot in each board `Undo` with a compact record containing move squares, moving/captured pieces, castling rook delta, reversible metadata, and saved hash/material keys.
- `unmake_move()` reverses only the affected piece deltas and restores metadata. It no longer assigns an entire board snapshot at every search edge.
- `make_move()` validates the castling rook before mutating the position, so failed castling attempts need no whole-position rollback.

### 2. Incremental NNUE move update

- The search path now feeds the compact board `Undo` directly to `update_nnue_state_for_move()`.
- It no longer copies the before-position and scans all 64 squares to discover move deltas.
- Normal piece moves, captures, en-passant, promotions, castling rook movement, castling-right feature deltas, king-layout refreshes, and network-slot transitions remain handled.
- `NNUEUndo` is still a reversible accumulator snapshot. This stage does not claim to have eliminated that copy.

### 3. Fixed-capacity move buffers

- Added `MoveList`, a 256-entry fixed-capacity move buffer matching the project's Pascal `MaxMoves` approach.
- `MovePicker` now uses one fixed scored-move array instead of four per-node vectors and stable-sort allocations.
- FV/NextFV ordering now uses fixed arrays for its ordered moves, capture scores, and quiet checks.
- Alpha-beta's failed-quiet list is fixed-capacity rather than heap-backed at every node.
- Packed move-score entries keep `sizeof(MovePicker)` at or below 2,304 bytes to limit recursive stack usage; the focused test asserts this.

### 4. Runtime-dispatched ARM Dot Product NNUE first layer

- Stage 18 used the baseline AArch64 NEON widening-multiply implementation for the NNUE 1,024-to-8 first layer.
- This stage adds a function-targeted ARMv8.2-A `SDOT` kernel. It converts each unsigned activation `x` to signed `x-128`, performs signed-byte dot products, and corrects the result by `128 * sum(weights)`. The identity is exact:

  `sum(x * w) = sum((x - 128) * w) + 128 * sum(w)`

- Per-output weight sums are precomputed once when a network is loaded.
- Android/Linux `AT_HWCAP` is checked for `HWCAP_ASIMDDP`. SDOT executes only when that capability is reported; otherwise the original AArch64 NEON path remains active.
- The executable is still compiled for the ARMv8-A + NEON baseline. It does not require Dot Product support to start.
- The UCI handshake reports the active backend (`AArch64 ARMv8.2-A SDOT`, `AArch64 NEON`, or scalar reference).
- The Android ELF verifier checks that the SDOT instruction is present in the final executable. Runtime selection remains HWCAP-guarded.

## Search behavior and fidelity safeguards

- No search pruning thresholds, reductions, extensions, score constants, hash bounds, or time-management settings were intentionally changed in this stage.
- Fixed-buffer move ordering is checked against vector-generated pseudo/legal move sets and the existing Stage 8 move-order tests.
- Stage 1–15 quick tests, Stage 16 build integration, Stage 17 script robustness, and Stage 18 embedding tests are retained.
- The 256-entry buffer is tested against representative high-mobility, castling, en-passant, promotion, and in-check positions.

## Verification performed in this workspace

- GCC Stage 19 focused fixed-buffer/compact-undo tests: PASS.
- Clang Stage 19 focused fixed-buffer/compact-undo tests: PASS.
- AArch64 target-attribute SDOT code-generation probe: PASS; disassembly contains `sdot` while the translation unit baseline is `armv8-a+simd`.
- GCC Stage 1–15 cumulative quick suite: PASS after the final SDOT metadata/diagnostic addition (`QUICK=1`; deep perft is excluded from this suite).
- Clang Stage 1–15 cumulative quick suite: PASS after the final SDOT metadata/diagnostic addition (`QUICK=1`; deep perft is excluded from this suite).
- Stage 16 integration tests: PASS after the final Stage 19 source changes.
- Stage 17 permission-robustness tests: PASS after updating their assertion for the runtime-dispatched SDOT verifier arguments.
- Stage 18 embedded-network fixture tests: PASS. These use temporary synthetic test fixtures only and do not validate the three original production networks.
- Stage 19 focused GCC and Clang tests: PASS, including fixed-buffer move-set equivalence, compact make/unmake restoration, and the unsigned-input SDOT correction identity.
- AArch64 target-attribute code-generation probe: PASS; generated disassembly contains `sdot` with a baseline `armv8-a+simd` translation-unit target.
- Additional optimized host board regressions: PASS, including start-position perft depth 5 (4,865,609 nodes), Kiwipete depth 3 (97,862 nodes), and Position 3 depth 4 (43,238 nodes).
- No Android NDK Stage 19 build or Galaxy A16 benchmark is claimed from this workspace. The recovered original source archive does not contain production network files; the CI workflow correctly requires them and cannot build the self-contained Android artifact without the repository's actual `booot2.nn`, `booot4.nn`, and `booot58.nn`.

## How to compare speed fairly on the phone

Use the same phone, same FEN, same hash size, one thread, similar thermal state, and the same time limit. Record at least `depth`, `nodes`, `time`, and `nps` from the final `info` line. A 20-ply result from one engine is not directly comparable to 15 plies from another because search selectivity and depth accounting differ.

Suggested UCI sequence for each engine:

```text
uci
isready
setoption name Hash value 16
setoption name Threads value 1
position startpos
go movetime 5000
```

Wait for `bestmove`, then retain the final depth/nodes/time/NPS output. Repeat from the same position at least three times after the phone has returned to a comparable temperature.

## Remaining optimization candidates

- Replace the remaining full `NNUEUndo` accumulator snapshot with a compact/reversible representation only if A16 profiling shows it is worthwhile; reverse feature updates can cost more than a small memcpy.
- Further multi-output NEON scheduling for the first layer may help devices without SDOT.
- The FV/NextFV code still has some vectors in auxiliary/root-only paths; they are not the primary per-node MovePicker path addressed here.
- No strength or Pascal-vs-C++ search parity claim is made by a speed-only stage.

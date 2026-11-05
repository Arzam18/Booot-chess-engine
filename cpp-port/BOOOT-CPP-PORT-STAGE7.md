# Booot 7.4 C++ Port — Stage 7: Incremental NNUE Updates

Stage 7 connects Booot's version-35 NNUE accumulators to board transitions. The implementation follows the update policy in the recovered `original_pascal/uNN.pas` `UpdAcc16` procedure and keeps `refresh_accumulators` as the full-rebuild reference implementation.

## New/updated files

- `booot_nnue.hpp` — exposes `NetworkSet`, `NNUEState`, `NNUEUndo`, state refresh/update/evaluate functions, and board+NNUE make/unmake wrappers.
- `booot_nnue.cpp` — incremental piece-feature and castling-feature deltas, frame/mirror detection, network-switch rebuilds, null-move handling, state validation, and evaluation through the currently selected network.
- `booot_nnue_move.cpp` — atomic convenience wrappers around `make_move`/`make_null_move` and the corresponding NNUE update. If the NNUE transition fails, the board is restored before returning.
- `booot_nnue_update_test.cpp` — move-by-move comparison of incremental accumulators against full rebuilds.
- `run_all_tests.sh` — complete Stage 1–7 host regression suite; compiles production modules once.
- `run_sanitizer_tests.sh` — combined AddressSanitizer and UndefinedBehaviorSanitizer coverage.

## Original behavior preserved

1. Network slots follow `booot7.dpr`: slot 0 = `booot2.nn`, slot 1 = `booot4.nn`, slot 2 = `booot58.nn`.
2. `choose_network` applies the original per-color non-pawn/non-king piece-count thresholds.
3. Each perspective retains its own king-relative frame and horizontal mirror.
4. Piece deltas use the original signed-piece mapping. Black's accumulator vertically flips squares and inverts piece colors.
5. Castling features retain the original White/Black feature-ID swap in Black's accumulator.
6. If the selected network changes, both accumulators are rebuilt with the new network.
7. If a perspective king move changes that accumulator's frame or mirror, only that perspective is rebuilt. Otherwise, the king is updated as a normal piece feature.
8. Castling-right loss/gain applies the appropriate feature delta. Castling rook movement is handled automatically by the before/after board diff.
9. Captures, en-passant, promotions, and ordinary moves are handled by comparing the 64 mailbox squares. The transition does not rely on capture flags or infer a capture square.
10. A null move changes no NNUE features, so both accumulators remain unchanged while the position key advances.

Accumulator arithmetic intentionally wraps to 16 bits, matching the original packed `int16` update routines. `NNUEUndo` stores a complete state snapshot for correctness and simple search integration; later profiling can replace it with a compact undo representation.

## Search integration example

Load the original three network files in the same order as `booot7.dpr` and keep the `Network` objects alive for as long as the pointer set is used:

```cpp
booot::Network net2, net4, net58;
std::string error;
if (!booot::load_network("booot2.nn", net2, error)) /* handle error */;
if (!booot::load_network("booot4.nn", net4, error)) /* handle error */;
if (!booot::load_network("booot58.nn", net58, error)) /* handle error */;

booot::NetworkSet networks{{&net2, &net4, &net58}};
booot::Position position;
if (!booot::parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                      position, error)) /* handle error */;
booot::NNUEState nnue;
if (!booot::refresh_nnue_state(position, networks, nnue, error)) /* handle error */;

booot::Undo board_undo;
booot::NNUEUndo nnue_undo;
if (booot::make_move_with_nnue(position, move, networks, nnue,
                               board_undo, nnue_undo, error)) {
    // Search/evaluate child node here.
    booot::unmake_move_with_nnue(position, board_undo, nnue, nnue_undo);
}
```

For null-move pruning, use `make_null_move_with_nnue` and `unmake_null_move_with_nnue`. For existing code that already calls `make_move`, call `update_nnue_state(before, after, ...)` immediately after the board move and pair it with `unmake_nnue_state` when returning to the parent.

`evaluate_nnue_state` verifies that the supplied state belongs to the current position and uses the selected network plus its original Sigma score table.

## Tests

Run from the archive root:

```sh
./run_all_tests.sh
./run_sanitizer_tests.sh
CXX=clang++ ./run_all_tests.sh
```

Stage 7 regression cases include:

- Every legal move from the initial position and targeted castling, en-passant, promotion, and king-movement positions.
- Exact comparison of both incremental 512-element accumulators with `refresh_accumulators` after each transition.
- Evaluation parity between incremental state and a freshly rebuilt state.
- Network-slot transition after a material-changing capture.
- Null move, combined board/NNUE make-unmake wrappers, and rollback when the required network slot is unavailable.
- A deterministic 600-ply legal random walk with per-ply accumulator rebuild comparisons and periodic undo/replay checks.
- Rejection of stale NNUE state used with a different before-position.

The original `.nn` network files are not present in the recovered source archive, so tests use deterministic synthetic weights. Real-network output parity is still pending access to the user's original `booot2.nn`, `booot4.nn`, and `booot58.nn` files.

## Scope and remaining work

Stage 7 completes the incremental NNUE state/update subsystem. It does not yet complete search, move ordering, transposition-table storage/replacement, repetition management, threading, time control, UCI, benchmark, or Android packaging. No Android NDK build or phone test is claimed for this stage.

Keep the original Pascal source, `resource.res`, and the three original `.nn` files unchanged in the fork. The `.nn` files must remain external and must not be regenerated from the synthetic test data.

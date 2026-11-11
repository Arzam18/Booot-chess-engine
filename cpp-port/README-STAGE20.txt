Booot 7.4 C++ Port — Stage 20

Search-fidelity corrections: raw NNUE static evaluation is stored in the TT and
scaled on use; LMR now preserves signed reductions, the original LMR history
formula, cut-node and low-PV adjustments, and Pascal-style re-search depth
changes. Root LMR now respects PV window width and the original +80 re-search
extension.

Validation: GCC and Clang Stage 1–15 host suites PASS; GCC and Clang Stage 19
focused checks PASS; Stage 1–8 ASan/UBSan PASS; additional search/UCI
ASan/UBSan PASS. Android NDK and Galaxy A16 testing not yet performed. The Nov
6 Stage 19 archive lacks the three original production networks.

See BTHOOK? No. See BOOOT-CPP-PORT-STAGE20.md for full details.


Stage 20.1 compile-error hardening: search now snapshots the moving piece and
parent hash directly from the parent Position before making a move. Search
header explicitly includes booot_board.hpp. The modified-only overlay includes
Stage 19 compact-undo compatibility files to avoid mixed-stage APIs.

Additional validation: GCC full perft + Stage 1-15 tests PASS; Clang quick
Stage 1-15 tests PASS; GCC ASan/UBSan search + UCI PASS. Android NDK build is
not run locally because no NDK is installed here.


Stage 20.2 Android link hardening: the NDK build now compiles each source file
to an explicit object, checks the NNUE object with NDK llvm-nm for both
`nnue_first_layer_backend()` and `update_nnue_state_for_move()`, then links the
complete object list. This turns the prior opaque LLD undefined-symbol failure
into a deterministic pre-link check and avoids relying on one-shot temporary
objects. Host separate-object build, symbol preflight, and full link pass; the
Android NDK workflow still needs to be rerun to verify the actual CI toolchain.

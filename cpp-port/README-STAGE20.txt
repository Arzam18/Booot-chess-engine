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

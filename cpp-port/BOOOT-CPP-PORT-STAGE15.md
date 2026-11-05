# Booot 7.4 C++ Port — Stage 15

## Scope

Stage 15 adds a UCI protocol boundary around the Stage 14 root-search and time-control APIs. The core search remains a single-threaded engine; the protocol uses a dedicated worker thread so the command loop remains responsive to `isready`, `stop`, position changes, and shutdown.

## 1. UCI handshake and identity

`booot_uci.cpp` / `booot_uci.hpp` implement:

- `uci`, engine identity, option declarations, and `uciok`.
- `isready` / `readyok`, including while a search worker is active.
- `ucinewgame`, clearing the transposition table and search histories.
- `stop`, `quit`, and safe worker joining.
- `setoption` for Hash, fixed one-thread setting, Move Overhead, and Clear Hash.

Hash resizing constructs a replacement four-way TT and swaps it in only after allocation succeeds. Failed resizing retains the previous table and reports an info string.

## 2. Position and repetition history

The protocol accepts:

- `position startpos [moves ...]`
- `position fen <six FEN fields> [moves ...]`

Every move is parsed against the current legal-move list before application. The parser builds a candidate position and chronological key history; malformed FEN or illegal move input leaves the previous protocol position unchanged. The accumulated pre-root position keys are passed to Stage 14 search so repetition detection sees the supplied game history.

## 3. Go parameters and output

Supported parameters are `depth`, `nodes`, `movetime`, `wtime`, `btime`, `winc`, `binc`, `movestogo`, and `infinite`. Clock values are selected according to the side to move and mapped to the existing Stage 14 budget fields. Explicit zero movetime/current-side clock is treated as an immediate deadline, not as an absent limit.

`SearchLimits::info_callback` reports each completed iterative-deepening pass. UCI output includes depth, score (centipawns or mate distance), nodes, elapsed time, NPS, and the principal variation. The worker always emits `bestmove`; terminal positions use `0000`.

The search callback is exception-contained so an output/front-end callback cannot unwind through the search algorithm. The UCI output stream is protected by a mutex. The main thread joins an active search before mutating the root position, TT, or history.

## 4. Executable and network loading

`booot_uci_main.cpp` builds a command-line executable. `build_booot_uci.sh` compiles the C++20 host executable with pthread support. The executable loads `booot2.nn`, `booot4.nn`, and `booot58.nn` from `--network-dir` (default `.`) and accepts `--hash MB`.

The actual three original NNUE model files were not present in the recovered source archive. The executable deliberately exits with a clear error if any model is absent; it does not manufacture weights. The `.res` Windows resource container is not required by this direct-file loader.

## 5. Tests and verification

- GCC Stage 15 focused search + UCI tests: PASS.
- Clang 17 Stage 15 focused search + UCI tests: PASS, warning-free.
- Cumulative Stage 1–15 quick host regression: PASS (`QUICK=1`; deep perft vectors skipped).
- UCI regression covers handshake/options, Hash resize command, standard startpos with move history, FEN plus legal moves, illegal move rejection without corrupting the last valid position, iterative info/PV output, bestmove, `isready` responsiveness, and stop on an infinite search.
- The Stage 15 sanitizer command was attempted with GCC and Clang but did not complete within the execution limit. The first Clang attempt completed the Stage 14 search regression under ASan/UBSan before timeout; a subsequent changed-module run also timed out. **No Stage 15 sanitizer PASS is claimed.** The script is included for reproducible execution in a less constrained environment.
- The final complete-source archive is freshly extracted and checksum-verified; the modified-only overlay is applied to a fresh Stage 14 base and its focused Stage 15 tests pass.
- Original Pascal source is compared byte-for-byte against the Stage 14 snapshot and remains unchanged.

## Fidelity and support boundaries

This is a UCI-capable host integration layer, not yet a tested Android binary. It supports one search thread, standard chess (not Chess960), and does not implement MultiPV, ponder-hit semantics, or `searchmoves`. Network model parity cannot be asserted until the original network binaries are supplied and tested. The Stage 14 C++ search is connected to the UCI layer, but complete tournament-strength equivalence with the Pascal release is not claimed.

BOOOT 7.4 C++ PORT — STAGE 15

Stage 15 adds a UCI protocol front end and command-line executable.

BUILD (host C++20):
  ./build_booot_uci.sh

Optional build overrides:
  CXX=clang++ OUTPUT=/path/to/booot-uci ./build_booot_uci.sh

RUN:
  ./booot-uci --network-dir /path/containing/networks --hash 16

Required ORIGINAL Booot 7.4 NNUE files in that directory:
  booot2.nn
  booot4.nn
  booot58.nn

The recovered original Pascal source archive did not contain these files.
The executable intentionally refuses to run without all three; no substitute
weights are generated. resource.res is not required by the Linux/Android C++
loader because the NNUE files are loaded directly from disk.

SUPPORTED UCI COMMANDS:
  uci, isready, ucinewgame, setoption, position, go, stop, quit

SUPPORTED OPTIONS:
  Hash (1–1024 MB)
  Threads (fixed at 1)
  Move Overhead (0–5000 ms)
  Clear Hash (button)

SUPPORTED POSITION FORMS:
  position startpos [moves ...]
  position fen <six FEN fields> [moves ...]

SUPPORTED GO PARAMETERS:
  depth, nodes, movetime, wtime, btime, winc, binc, movestogo, infinite

Search runs on a worker thread so isready can be answered during search and
stop/position/ucinewgame/quit can interrupt and join the worker. Iterative
info lines are emitted after each completed depth. The final response is
bestmove <move>, or bestmove 0000 for a terminal position.

LIMITATIONS:
  - One search thread only; no SMP, MultiPV, ponder-hit behavior, or searchmoves.
  - Only the standard (non-Chess960) UCI castling notation is supported.
  - Host C++20 build only; Android NDK/static executable and device validation
    are future stages.
  - Original network parity remains unverified until the actual three model
    files are supplied and validated.

TESTS:
  ./run_stage15_tests.sh
  CXX=clang++ ./run_stage15_tests.sh
  QUICK=1 ./run_all_tests.sh
  ./run_stage15_sanitizers.sh

The sanitizer script instruments the Stage 15 search and UCI translation units
and tests while linking stable dependencies normally. A sanitizer run may need
a larger process stack because the reference SearchEngine test fixtures hold
large engine instances in one test function.

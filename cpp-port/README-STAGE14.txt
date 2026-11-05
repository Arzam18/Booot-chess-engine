BOOOT 7.4 C++ PORT — STAGE 14

Stage 14 adds a separate root search and root/time-management behavior.

CHANGES
- Separate root move loop following Pascal RootSearch architecture.
- Best root move is promoted to the front between iterative-deepening passes.
- Root PVS, root LMR, and root history feedback.
- Aspiration windows from depth 6, including fail-low/high widening and Pascal fail-high retry-depth adjustment.
- Fixed-movetime and clock/increment budgets with overhead, soft deadline, and hard deadline.
- SearchResult diagnostics for root move/LMR counters, aspiration retries, elapsed time, selected budgets, and hard-timeout status.

TESTING
  ./run_stage14_tests.sh
  CXX=clang++ ./run_stage14_tests.sh
  CXX=g++ ./run_stage14_sanitizers.sh
  QUICK=1 CXX=g++ ./run_all_tests.sh

QUICK=1 skips deep perft vectors; it still runs depth-1 perft smoke vectors and all other cumulative tests.

LIMITATIONS
This is not yet a UCI executable or Android binary. The UCI shell must map `go movetime`, `go wtime/btime`, increments, and moves-to-go into SearchLimits. Ponder-hit, multi-thread scheduling, original NNUE network parity, Android NDK build, and actual device validation remain future work. Original Pascal sources are preserved unchanged. Missing original NNUE model binaries and resource.res have not been fabricated.

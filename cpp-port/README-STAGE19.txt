BOOOT 7.4 C++ PORT — STAGE 19

FOCUS:
  Improve search hot-path throughput without changing the search pruning policy.

CHANGES:
  - Compact board Undo; no full Position snapshot on each move.
  - Incremental NNUE move updates use compact board deltas; no before-position
    copy or 64-square change scan in the search move path.
  - Fixed 256-entry MoveList and scored MovePicker arrays.
  - Fixed-array FV/NextFV move-order buffers and alpha-beta failed-quiet storage.
  - Optional ARMv8.2-A SDOT first-layer NNUE kernel, runtime-selected only when
    AT_HWCAP advertises HWCAP_ASIMDDP. The ARMv8-A NEON fallback remains.
  - UCI handshake reports the active NNUE first-layer kernel.
  - Android ELF verifier checks that an SDOT kernel is present in the binary.

VERIFICATION STATUS:
  GCC and Clang Stage 19 focused tests: PASS.
  GCC and Clang Stage 1–15 cumulative QUICK=1 regressions: PASS.
  Stage 16, Stage 17, and Stage 18 integration tests: PASS individually.
  Optimized host perft reference vectors: PASS.
  AArch64 SDOT target-attribute code-generation probe: PASS.
  A real NDK build and Galaxy A16 benchmark are still pending.

TESTS:
  cd cpp-port
  bash ./run_stage19_tests.sh
  CXX=clang++ bash ./run_stage19_tests.sh
  QUICK=1 bash ./run_all_tests.sh
  CXX=clang++ QUICK=1 bash ./run_all_tests.sh
  bash ./run_stage16_tests.sh
  bash ./run_stage17_tests.sh
  bash ./run_stage18_tests.sh

NOT CLAIMED:
  - No actual Android NDK Stage 19 build is verified in this workspace.
  - No Galaxy A16 NPS/depth improvement is claimed until the new binary is run.
  - Production NNUE files are not fabricated or included in source ZIPs.
  - Search-depth numbers across different engines are not directly comparable.

PHONE COMPARISON:
  Use matching FEN, Hash=16, Threads=1, 5000 ms, comparable phone temperature.
  Record final depth, nodes, time, and NPS. The UCI response reports whether
  SDOT or baseline NEON is active.

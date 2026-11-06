BOOOT 7.4 C++ PORT — STAGE 18

STAGE 17 ARTIFACT REVIEW:
  The prior Android artifact was not self-contained. No .nn files were linked;
  runtime loading depended on the DroidFish process working directory. Missing
  networks caused main() to exit before UCIProtocol was constructed.

STAGE 18 FIX:
  Android build now requires root/booot2.nn, root/booot4.nn, root/booot58.nn.
  cpp-port/booot_nnue_embedded.S embeds all three using .incbin, unchanged.
  ELF verification checks all three symbols and exact model byte lengths.
  DroidFish needs only the final executable; no external .nn files are needed.

UCI WITHOUT NETWORKS (host/debug mode):
  `uci` and `isready` still work and the engine identity is reported.
  `go` is explicitly disabled and responds with an info string + bestmove 0000.
  The engine never pretends to search with missing or synthetic weights.

WORKFLOW:
  Actions -> Booot Android ARM64 -> Run workflow
  Uploads executable + .nnue-manifest.txt (SHA-256 of the original inputs).

IMPORTANT:
  The recovered source archive did not contain the three production NNUE files.
  Add the real original model files to repository root before running this build.
  CI fails early rather than publishing an executable without embedded networks.

TESTS:
  cd cpp-port
  bash ./run_stage18_tests.sh
  CXX=clang++ bash ./run_stage18_tests.sh

Device smoke test after Actions succeeds:
  Copy only `booot-uci-android-arm64` into DroidFish/uci/Booot/
  Add it as a UCI engine. It must answer `uciok` and `readyok` without any .nn files.

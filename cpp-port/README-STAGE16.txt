BOOOT 7.4 C++ PORT — STAGE 16 (RESTRUCTURED REPOSITORY)

REPOSITORY LAYOUT:
  /booot7.dpr, /u*.pas, /booot7.dproj, /booot7.lpi, /booot7.res, /resource.rc
      Original Pascal project and sources, preserved at repository root.
  /cpp-port/
      All C++ port source, UCI, tests, build scripts, and port documentation.
  /.github/workflows/android-arm64.yml
      Manual-only C++ Android workflow; builds from cpp-port/ only.
  /original_pascal_android.yml
      Inert copy of the original Pascal workflow, not an active GitHub workflow.

The three original NNUE model files must be placed in the repository root:
  booot2.nn
  booot4.nn
  booot58.nn
They were not included in the recovered source archive. No substitute files
are generated or bundled.

ANDROID BUILD (Linux host, NDK r30 CI pin; local r27c also supported):
  cd cpp-port
  ANDROID_NDK_ROOT=/path/to/android-ndk-r30 bash ./build_booot_android_arm64.sh

The script builds one executable:
  cpp-port/booot-uci-android-arm64

Target configuration:
  Android ABI: arm64-v8a / AArch64
  Minimum API: 24
  ISA: ARMv8-A + Advanced SIMD (NEON baseline)
  NDK CI baseline: r30, version 30.0.16248370
  C++: C++20, optimized Release
  C++ runtime: statically linked libc++ (no libc++_shared.so dependency)
  ELF: PIE, 16 KiB PT_LOAD alignment, RELRO and immediate binding

GITHUB ACTIONS:
  .github/workflows/android-arm64.yml
The workflow is workflow_dispatch-only. It downloads and verifies the NDK,
then sets working-directory to cpp-port for both build and ELF verification.
The C++ compile command uses an explicit list of .cpp files and never compiles
Pascal files or the original Windows resource files.

LOCAL HOST TESTS (from cpp-port/):
  bash ./run_stage16_tests.sh
  CXX=clang++ bash ./run_stage16_tests.sh
  bash ./run_stage15_tests.sh
  CXX=clang++ bash ./run_stage15_tests.sh
  QUICK=1 bash ./run_all_tests.sh

ANDROID ELF VERIFICATION (from cpp-port/):
  READELF=/path/to/llvm-readelf bash ./verify_android_arm64.sh ./booot-uci-android-arm64

RUNTIME:
Run from the repository root so the original network files are in the selected
network directory:
  ./cpp-port/booot-uci-android-arm64 --network-dir . --hash 16

LIMITATIONS / VERIFICATION BOUNDARY:
  - The local environment did not have an Android NDK installed and could not
    reach Google's NDK download host. Android cross-compilation and device
    launch have not been completed locally; run the manual GitHub workflow.
  - No NNUE inference parity claim is possible until the original three models
    are supplied and tested.
  - No APK or Android app integration is included; this is a standalone UCI
    executable for Termux or another compatible Android shell.
  - Baseline ARMv8-A NEON is used; dot-product-only instructions are not required.

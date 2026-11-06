# Booot 7.4 C++ Port — Cumulative Status (Stage 17 — permission-robust CI fix)

The original Booot Pascal project is preserved at repository root (`booot7.dpr`,
`u*.pas`, project/resource files). The C++ port is isolated under `cpp-port/`.
The active `.github/workflows/android-arm64.yml` workflow lives at repository
root, but performs both compilation and ELF verification from `cpp-port/` only.
The original Pascal workflow is retained as `original_pascal_android.yml` and
is not an active GitHub Actions workflow.

The C++ port currently includes board/bitboard, attacks, hashing, tactics/SEE,
KPK/endgames, NNUE parsing/inference and incremental updates, evaluation and
move ordering, alpha-beta/PVS, FV/NextFV, correction history, ProbCut, singular
extensions, IIR/LMP/history pruning, separate root search, aspiration/time
management, and UCI protocol integration.

## Stage 16 Android build status

The Android ARM64 build scripts and manual-only GitHub Actions workflow target
AArch64 ARMv8-A + NEON, API 24, C++20, static libc++, PIE, and 16 KiB ELF load
alignment. The workflow uses NDK r30 (30.0.16248370); local r27c may be selected
with `ANDROID_NDK_ROOT`. It compiles an explicit list of C++ files from
`cpp-port/` and does not compile the Pascal sources at repository root.

Host checks: Stage 16 integration tests passed with GCC and Clang 17; cumulative
Stage 1–15 quick regression passed. Quick mode skips deep perft vectors. The
23 original Pascal project files were restored byte-for-byte from the prior
Stage 16 archive.

The local environment did not have NDK r30 or r27c and could not resolve the
official download host, so no Android cross-compile or Android device launch has
been completed locally. Run the root workflow manually and test the artifact
on an Android ARM64 device before claiming Android runtime validation.

The three required original NNUE models (`booot2.nn`, `booot4.nn`,
`booot58.nn`) must be placed at repository root and passed using
`--network-dir .`. They were absent from the recovered source archive; no
substitutes were fabricated and no original-network parity is claimed.


## Stage 17 — shell-script permission robustness

The first Stage 17 GitHub Actions attempt stopped before compilation because
GitHub's checked-out `build_booot_android_arm64.sh` did not have its executable
bit set (`Permission denied`). This is a file-mode issue, not a C++ compiler
error. Stage 17 makes execution independent of executable-bit preservation:

- The workflow invokes both build and verification scripts with `bash`.
- The Android build script invokes its nested ELF verifier with `bash` too.
- The Stage 16 integration test invokes build scripts with `bash`.
- Setup/build documentation uses `bash script.sh` for shell scripts.

The Android NDK build is still pending. Stage 17 is not successful until the
manual GitHub Actions run completes the compile, ELF verification, and artifact
upload steps.

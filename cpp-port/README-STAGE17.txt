BOOOT 7.4 C++ PORT — STAGE 17 (WORKFLOW PERMISSION FIX)

FIRST STAGE 17 FAILURE:
  ./build_booot_android_arm64.sh: Permission denied

ROOT CAUSES:
  1. The build script was checked out without its executable bit. GitHub web
     uploads or archive/file-copy paths can store shell scripts as 0644 files.
  2. The first Stage 17 modified-files ZIP omitted the required sibling
     cpp-port/verify_android_arm64.sh. The NDK compile ran, but verification
     could not start because the verifier file was absent.

FIX:
  The workflow uses `bash ./build_booot_android_arm64.sh` and
  `bash ./verify_android_arm64.sh ...`. The build script itself invokes the
  verifier with `bash` after confirming the sibling file exists. The updated
  modified-files ZIP includes the verifier dependency.

TEST:
  cd cpp-port
  bash ./run_stage17_tests.sh
  CXX=clang++ bash ./run_stage17_tests.sh

STAGE 17 IS NOT YET COMPLETE:
  The manual Android NDK GitHub Actions run must still build the AArch64 binary,
  pass ELF verification, and upload the artifact. Do not mark Android build or
  device runtime successful until those steps actually pass.

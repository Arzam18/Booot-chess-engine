# Booot 7.4 C++ Port — Stage 17

## Scope

Stage 17 fixes the first GitHub Actions failure after the restructured Stage 16
package was uploaded to the repository. The runner reported:

```text
./build_booot_android_arm64.sh: Permission denied
```

The shell script existed, but the executable permission bit was not present in
the GitHub checkout. This is a packaging/file-mode issue; the Android compiler
had not started, so it is not evidence of a C++ source or NDK build failure.

A second CI attempt then reported that `cpp-port/verify_android_arm64.sh` did
not exist. The Stage 17 modified-files archive had omitted this required sibling
script even though the build helper invoked it. This was an incomplete overlay
package, not a compiler or ELF-verification failure.

## Changes

- Root `.github/workflows/android-arm64.yml` invokes the Android build with
  `bash ./build_booot_android_arm64.sh`.
- The workflow invokes the ELF verifier with `bash` as well.
- `build_booot_android_arm64.sh` invokes its nested verifier using `bash`, so
  the second script does not depend on its executable bit either.
- `run_stage16_tests.sh` invokes the Android help path and host build through
  `bash`, and no longer assumes the scripts are executable.
- The Android build helper checks for its sibling verifier before starting the
  NDK compile and reports an actionable missing-dependency error.
- The Stage 17 modified-files-only archive now includes
  `cpp-port/verify_android_arm64.sh` as a required dependency, even though its
  contents were not changed in this stage.
- The Stage 17 regression test checks both the dependency's presence and the
  early diagnostic when it is deliberately missing.
- README and Android build instructions now show `bash script.sh` commands.
- Added `run_stage17_tests.sh`, including a simulated mode-0644 script check
  and the Stage 16 integration regression.

## Stage 17 validation

- Shell syntax checks: PASS.
- Simulated non-executable build script (`0644`) invoked by Bash: PASS.
- Workflow entry-point checks: PASS.
- Nested verifier invocation check: PASS.
- Verifier dependency included in modified-only overlay: PASS.
- Missing-verifier preflight diagnostic: PASS.
- Stage 16 integration tests from the Stage 17 runner with GCC: PASS.
- Stage 17 permission-regression tests with GCC: PASS.
- Stage 17 permission-regression tests with Clang 17 in the working tree: PASS.
- Fresh extracted-archive GCC regression with build/test scripts forced to mode
  `0644`: PASS.
- A repeated Clang 17 regression from the fresh extraction exceeded the
  available execution window; that repeat is not counted as a pass.
- Android NDK cross-compilation: NOT YET RUN.

## Completion condition

Stage 17 remains **in progress** until the user runs the manual GitHub Actions
workflow and the job successfully completes the Android ARM64 compile, ELF
verification, and artifact upload. Only then should the engine proceed to
phone-side UCI tests.

Run locally from `cpp-port/`:

```bash
bash ./run_stage17_tests.sh
CXX=clang++ bash ./run_stage17_tests.sh
```

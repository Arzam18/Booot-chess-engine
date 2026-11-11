#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$ROOT/.." && pwd)"
WORK="${TMPDIR:-/tmp}/booot-stage17-tests-$$"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

# The Android build helper requires the verifier as a sibling file.
if [[ ! -f "$ROOT/verify_android_arm64.sh" ]]; then
  echo "ERROR: required verifier dependency is absent from cpp-port/." >&2
  exit 1
fi

# Syntax checks do not require executable file modes.
bash -n "$ROOT/build_booot_android_arm64.sh"
bash -n "$ROOT/verify_android_arm64.sh"
bash -n "$ROOT/build_booot_uci.sh"
bash -n "$ROOT/run_stage16_tests.sh"

# Simulate files committed by a web upload with mode 0644.
cp "$ROOT/build_booot_android_arm64.sh" "$WORK/build_booot_android_arm64.sh"
cp "$ROOT/verify_android_arm64.sh" "$WORK/verify_android_arm64.sh"
cp "$ROOT/build_booot_uci.sh" "$WORK/build_booot_uci.sh"
chmod 0644 "$WORK"/*.sh
bash "$WORK/build_booot_android_arm64.sh" --help | grep -q 'Android ARM64 self-contained UCI build'

# Both workflow entry points and the build script's nested verifier must use bash.
WORKFLOW="$REPO/.github/workflows/android-arm64.yml"
grep -q 'workflow_dispatch:' "$WORKFLOW"
grep -q 'run: bash ./build_booot_android_arm64.sh' "$WORKFLOW"
grep -q 'run: bash ./verify_android_arm64.sh ./booot-uci-android-arm64' "$WORKFLOW"
grep -q 'READELF="$READELF" OBJDUMP="$OBJDUMP" bash "$VERIFY_SCRIPT" "$OUTPUT"' "$ROOT/build_booot_android_arm64.sh"
grep -q 'required ELF verifier script is missing' "$ROOT/build_booot_android_arm64.sh"

# A missing verifier must fail early with an actionable message, not a late
# "No such file or directory" after the expensive NDK compile.
mkdir -p "$WORK/missing-verifier"
cp "$ROOT/build_booot_android_arm64.sh" "$WORK/missing-verifier/"
if bash "$WORK/missing-verifier/build_booot_android_arm64.sh" >"$WORK/missing.out" 2>&1; then
  echo 'ERROR: build unexpectedly continued without its verifier dependency' >&2
  exit 1
fi
grep -q 'required ELF verifier script is missing' "$WORK/missing.out"
grep -q 'bash "$ROOT/build_booot_android_arm64.sh" --help' "$ROOT/run_stage16_tests.sh"
grep -q 'CXX="$CXX" bash "$ROOT/build_booot_uci.sh"' "$ROOT/run_stage16_tests.sh"

# Reject direct execution of the Android scripts from workflow run commands.
if grep -Eq 'run: \.\/(build_booot_android_arm64|verify_android_arm64)\.sh' "$WORKFLOW"; then
  echo 'ERROR: workflow still directly executes a shell script' >&2
  exit 1
fi

# Re-run the previous integration gate with mode-independent script invocation.
bash "$ROOT/run_stage16_tests.sh"

echo 'Booot Stage 17 permission-robustness tests: PASS'
echo 'Android NDK compilation is still gated by the manual GitHub Actions run.'

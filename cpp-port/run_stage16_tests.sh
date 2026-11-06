#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
CXX="${CXX:-c++}"
BUILD="${TMPDIR:-/tmp}/booot-stage16-tests-$$"
mkdir -p "$BUILD"
trap 'rm -rf "$BUILD"' EXIT

bash -n "$ROOT/build_booot_android_arm64.sh"
bash -n "$ROOT/verify_android_arm64.sh"
bash "$ROOT/build_booot_android_arm64.sh" --help | grep -q 'Android ARM64 UCI build'
WORKFLOW="$ROOT/../.github/workflows/android-arm64.yml"
grep -q 'workflow_dispatch:' "$WORKFLOW"
grep -q 'NDK_VERSION: 30.0.16248370' "$WORKFLOW"
grep -q 'aarch64-linux-android24-clang++' "$WORKFLOW"
grep -q '5107f898313790e449e87eee2183d9a20602dee9' "$WORKFLOW"
grep -q 'working-directory: cpp-port' "$WORKFLOW"
test -f "$ROOT/../booot7.dpr"
test -f "$ROOT/../uSearch.pas"
if find "$ROOT" -maxdepth 1 -type f -name '*.pas' | grep -q .; then
  echo 'ERROR: Pascal source unexpectedly exists inside cpp-port' >&2; exit 1;
fi

OUTPUT="$BUILD/booot-uci-host" CXX="$CXX" bash "$ROOT/build_booot_uci.sh"
"$BUILD/booot-uci-host" --help | grep -q 'Expected network files'
mkdir -p "$BUILD/empty-networks"
if "$BUILD/booot-uci-host" --network-dir "$BUILD/empty-networks" >"$BUILD/out.txt" 2>"$BUILD/err.txt"; then
  echo "ERROR: executable unexpectedly accepted missing NNUE networks" >&2
  exit 1
fi
grep -q 'Place the original Booot 7.4 network files' "$BUILD/err.txt"

echo "Booot Stage 16 build-integration tests: PASS"
echo "Android NDK cross-compilation itself is performed by the manual GitHub Actions workflow."

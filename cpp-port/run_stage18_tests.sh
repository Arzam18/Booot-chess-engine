#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$ROOT/.." && pwd)"
CXX="${CXX:-c++}"
EMBED_CXX="${EMBED_CXX:-clang++}"
WORK="${TMPDIR:-/tmp}/booot-stage18-tests-$$"
mkdir -p "$WORK"
GENERATED_FIXTURES=0
cleanup() {
  rm -rf "$WORK"
  if (( GENERATED_FIXTURES )); then
    rm -f "$REPO/booot2.nn" "$REPO/booot4.nn" "$REPO/booot58.nn"
  fi
}
trap cleanup EXIT

# Do not overwrite a developer's real model files.
for model in booot2.nn booot4.nn booot58.nn; do
  if [[ -e "$REPO/$model" ]]; then
    echo "ERROR: refusing to overwrite existing production model: $REPO/$model" >&2
    exit 2
  fi
done

bash -n "$ROOT/build_booot_android_arm64.sh"
bash -n "$ROOT/verify_android_arm64.sh"
bash -n "$ROOT/run_stage18_tests.sh"

# Stage 16/17 workflow permission and no-network handshake tests are run
# separately. Here, first ensure the Android build refuses to publish an
# executable before original network files are supplied.
if bash "$ROOT/build_booot_android_arm64.sh" > "$WORK/missing-net.out" 2>&1; then
  echo 'ERROR: Android build unexpectedly accepted missing NNUE assets' >&2
  exit 1
fi
grep -q 'required original NNUE file is missing or empty' "$WORK/missing-net.out"

# Create temporary valid zero-weight version-35 model fixtures. These are only
# parser/linker fixtures and are removed by the EXIT trap; never delivered.
GENERATED_FIXTURES=1
python3 - "$REPO" <<'PY'
import pathlib, struct, sys
root = pathlib.Path(sys.argv[1])
model_size = 772
header = struct.pack('<HhHHHH', 35, 100, 100, 0, 64, 4096)
parts = [
    header,
    bytes(model_size * 512 * 2),
    bytes(1024 * 8),
    bytes(8 * 32 * 4),
    bytes(32 * 4),
    bytes(512 * 2),
    bytes(8 * 4),
    bytes(32 * 4),
    bytes(4),
]
data = b''.join(parts)
for name in ('booot2.nn', 'booot4.nn', 'booot58.nn'):
    (root / name).write_bytes(data)
print(f'temporary synthetic fixture size: {len(data)} bytes each')
PY

# Compile only the NNUE parser plus assembly fixture. This is a faster,
# isolated test of the exact linker embedding and in-memory parser path.
cd "$ROOT"
echo "[embedded] Compile fixture loader with $EMBED_CXX"
"$EMBED_CXX" -std=c++20 -O0 -g0 -Wall -Wextra -Wpedantic \
  -DBOOOT_EMBEDDED_NNUE=1 -I"$ROOT" \
  "$ROOT/booot_embedded_nnue_test.cpp" \
  "$ROOT/booot_nnue.cpp" "$ROOT/booot_nnue_embedded.S" \
  -o "$WORK/booot-embedded-nnue-test"
"$WORK/booot-embedded-nnue-test"
readelf -sW "$WORK/booot-embedded-nnue-test" > "$WORK/symbols.txt"
EXPECTED_SIZE="$(stat -c '%s' "$REPO/booot2.nn")"
for symbol in booot2_nn_start booot4_nn_start booot58_nn_start; do
  actual_raw="$(awk -v wanted="$symbol" '$NF == wanted {print $3; exit}' "$WORK/symbols.txt")"
  actual=$((actual_raw))
  [[ "$actual" == "$EXPECTED_SIZE" ]]
done

echo 'Booot Stage 18 tests: PASS'
echo '  Android build rejects missing production networks: PASS'
echo '  Three .incbin model symbols and exact input sizes: PASS'
echo '  Embedded-memory loader for all three assets: PASS'
echo '  Production network parity and Android/device test: NOT CLAIMED'
if [[ "${RUN_FULL_REGRESSION:-0}" != "1" ]]; then
  echo '  Full Stage 1-15 regression: run separately with QUICK=1 bash ./run_all_tests.sh'
  echo '  Stage 16/17 integration tests: run separately with bash ./run_stage17_tests.sh'
fi

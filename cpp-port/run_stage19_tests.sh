#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
CXX="${CXX:-c++}"
BUILD="${TMPDIR:-/tmp}/booot-stage19-tests-$$"
mkdir -p "$BUILD"
trap 'rm -rf "$BUILD"' EXIT
COMMON=(-std=c++20 -Wall -Wextra -Wpedantic -I"$ROOT")
SOURCES=(booot_bitboard booot_attacks booot_hash booot_board booot_tactics booot_endgame booot_nnue booot_nnue_move booot_eval booot_move_order)
for source in "${SOURCES[@]}"; do
  "$CXX" "${COMMON[@]}" -O2 -c "$ROOT/${source}.cpp" -o "$BUILD/${source}.o"
done
"$CXX" "${COMMON[@]}" -O2 "$ROOT/booot_stage19_performance_test.cpp" \
  "$BUILD"/*.o -o "$BUILD/stage19_test"
"$BUILD/stage19_test"
echo "Booot Stage 19 focused tests: PASS"

if command -v clang >/dev/null 2>&1 && command -v llvm-objdump >/dev/null 2>&1; then
  cat > "$BUILD/dotprod_probe.c" <<'PROBE'
#include <arm_neon.h>
#include <stdint.h>
__attribute__((target("arch=armv8.2-a+dotprod")))
int32_t booot_sdot_probe(const uint8_t *input, const int8_t *weights) {
  int32x4_t accum = vdupq_n_s32(0);
  const uint8x16_t bias = vdupq_n_u8(0x80);
  const int8x16_t centered = vreinterpretq_s8_u8(
      veorq_u8(vld1q_u8(input), bias));
  __asm__ volatile("sdot %0.4s, %1.16b, %2.16b"
                   : "+w"(accum) : "w"(centered), "w"(vld1q_s8(weights)));
  return vaddvq_s32(accum);
}
PROBE
  clang -target aarch64-none-elf -march=armv8-a+simd -ffreestanding -O3 \
    -c "$BUILD/dotprod_probe.c" -o "$BUILD/dotprod_probe.o"
  llvm-objdump -d "$BUILD/dotprod_probe.o" | grep -Eiq '[[:space:]]sdot[[:space:]]'
  echo "Booot Stage 19 AArch64 target-attribute SDOT code-generation test: PASS"
else
  echo "Booot Stage 19 SDOT code-generation test: SKIPPED (clang/llvm-objdump unavailable)"
fi

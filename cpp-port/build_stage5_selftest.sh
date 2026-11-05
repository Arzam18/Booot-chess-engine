#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
CXX="${CXX:-c++}"
BUILD="${TMPDIR:-/tmp}/booot-stage5-test-$$"
mkdir -p "$BUILD"
trap 'rm -rf "$BUILD"' EXIT
COMMON=(-std=c++20 -Wall -Wextra -Wpedantic -I"$ROOT")
for source in booot_bitboard booot_attacks booot_hash booot_board booot_tactics; do
  "$CXX" "${COMMON[@]}" -O2 -c "$ROOT/${source}.cpp" -o "$BUILD/${source}.o"
done
"$CXX" "${COMMON[@]}" -O2 "$ROOT/booot_tactics_test.cpp" "$BUILD"/*.o -o "$BUILD/tactics_test"
"$BUILD/tactics_test"

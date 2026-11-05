#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
CXX="${CXX:-c++}"
COMMON=( -std=c++20 -O3 -Wall -Wextra -Wpedantic -I"$ROOT" )

"$CXX" "${COMMON[@]}" \
  "$ROOT/booot_bitboard.cpp" \
  "$ROOT/booot_attacks.cpp" \
  "$ROOT/booot_hash.cpp" \
  "$ROOT/booot_board.cpp" \
  "$ROOT/booot_board_test.cpp" \
  -o "$ROOT/booot_board_test"
"$ROOT/booot_board_test"

# Independent sanitizer build for the full board/move/perft layer.
"$CXX" -std=c++20 -O1 -g -Wall -Wextra -Wpedantic \
  -DBOOOT_SANITIZER_TEST -fsanitize=address,undefined -fno-omit-frame-pointer -I"$ROOT" \
  "$ROOT/booot_bitboard.cpp" \
  "$ROOT/booot_attacks.cpp" \
  "$ROOT/booot_hash.cpp" \
  "$ROOT/booot_board.cpp" \
  "$ROOT/booot_board_test.cpp" \
  -o "$ROOT/booot_board_test_san"
ASAN_OPTIONS=detect_leaks=1 "$ROOT/booot_board_test_san"

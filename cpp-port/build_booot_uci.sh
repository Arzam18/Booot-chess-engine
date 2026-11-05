#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
CXX="${CXX:-c++}"
OUTPUT="${OUTPUT:-$ROOT/booot-uci}"
SOURCES=(
  booot_nnue.cpp booot_nnue_move.cpp booot_bitboard.cpp booot_attacks.cpp
  booot_hash.cpp booot_board.cpp booot_tactics.cpp booot_endgame.cpp
  booot_eval.cpp booot_move_order.cpp booot_search.cpp booot_uci.cpp
  booot_uci_main.cpp
)
"$CXX" -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -pthread \
  -I"$ROOT" "${SOURCES[@]/#/$ROOT/}" -o "$OUTPUT"
echo "Built: $OUTPUT"
echo "Run from repository root: $OUTPUT --network-dir ."

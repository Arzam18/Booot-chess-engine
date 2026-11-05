#pragma once

#include <cstdint>
#include "booot_nnue.hpp"

namespace booot {

using Bitboard = std::uint64_t;
using Square = int;

inline constexpr Square kA1 = 0;
inline constexpr Square kH8 = 63;
inline constexpr Square kNoSquare = -1;

constexpr Bitboard square_bb(Square square) noexcept {
    return (square >= 0 && square < 64) ? (Bitboard{1} << square) : Bitboard{0};
}

constexpr int file_of(Square square) noexcept { return square & 7; }
constexpr int rank_of(Square square) noexcept { return square >> 3; }
constexpr Square make_square(int file, int rank) noexcept {
    return (file >= 0 && file < 8 && rank >= 0 && rank < 8)
        ? rank * 8 + file : kNoSquare;
}

int popcount(Bitboard value) noexcept;
Square lsb(Bitboard value) noexcept;       // kNoSquare for zero
Square msb(Bitboard value) noexcept;       // kNoSquare for zero
Square pop_lsb(Bitboard& value) noexcept;  // kNoSquare for zero

Bitboard file_bb(int file) noexcept;
Bitboard rank_bb(int rank) noexcept;
Bitboard adjacent_files_bb(Square square) noexcept;
Bitboard forward_ranks_bb(Color color, Square square) noexcept;
Bitboard passer_span_bb(Color color, Square square) noexcept;

} // namespace booot

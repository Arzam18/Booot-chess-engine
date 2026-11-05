#pragma once

#include <array>
#include <cstdint>

#include "booot_board.hpp"

namespace booot {

struct HashValues {
    std::uint64_t key = 0;
    std::uint64_t pawn_key = 0;
    // Booot's NonPawnKey includes every non-pawn piece, including each king.
    std::array<std::uint64_t, 2> non_pawn_key{};
    // Matches TBoard.NonPawnMat: excludes pawns and kings.
    std::array<int, 2> non_pawn_material{};
};

// Exact fixed Zobrist constants copied from the recovered uHash.pas source.
std::uint64_t zobrist_piece(Color color, Piece type, Square square) noexcept;
std::uint64_t zobrist_en_passant(Square square) noexcept;
std::uint64_t zobrist_castling(std::uint8_t rights) noexcept;
std::uint64_t zobrist_side() noexcept;

HashValues calculate_hashes(const Position& position) noexcept;
void recompute_hashes(Position& position) noexcept;
bool hash_values_match(const Position& position) noexcept;

// Equivalent to uHash.pas ValueToTT / ValueFromTT. win_score and infinity
// are supplied by the search module to avoid duplicating uSearch constants.
int value_to_tt(int value, int ply, int win_score, int infinity) noexcept;
int value_from_tt(int value, int ply, int win_score, int infinity) noexcept;

} // namespace booot

#pragma once

#include <array>
#include <cstdint>

#include "booot_board.hpp"

namespace booot {

// Direct port of uBoard.TCheckInfo. The masks use Booot's a1=0 bit numbering.
struct CheckInfo {
    Bitboard discover_check = 0;
    std::array<Bitboard, 7> direct_check{};
    Bitboard pinned = 0;
    Square enemy_king_square = kNoSquare;
};

// Bitboard of squares strictly between two aligned squares, matching
// uBitBoards.InterSect[from][to]. Returns zero for non-aligned/same squares.
Bitboard between_squares(Square from, Square to) noexcept;

// Booot uAttacks.pas tactical helpers.
Bitboard find_pinners(Color piece_color, Color king_color,
                      const Position& position);
void fill_check_info(CheckInfo& info, const Position& position);
bool is_move_check(Move move, const CheckInfo& info,
                   const Position& position);

// `pinned` is the current side's pinned-piece mask. As in the Pascal original,
// this is a fast legality test for a pseudo-legal move; check-evasion filtering
// is performed by is_pseudo_correct / the move generator.
bool is_legal(Move move, Bitboard pinned, const Position& position);

// Checks pseudo-move geometry, capture flags, special-move legality and
// whether a move answers the current check. Does not reject ordinary pinned
// moves; call is_legal for that final fast filter, as Booot does.
bool is_pseudo_correct(Move move, const Position& position);

// Booot's thresholded least-valuable-attacker SEE (uAttacks.GoodSee).
// Returns true when the move's exchange sequence meets `margin`.
bool good_see(Move move, const Position& position, int margin);

} // namespace booot

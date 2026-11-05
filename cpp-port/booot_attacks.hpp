#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "booot_bitboard.hpp"

namespace booot {

struct AttackPosition {
    // Piece-type bitboards contain both colors, as in Booot's TBoard.Pieses[].
    std::array<Bitboard, 7> pieces{};
    std::array<Bitboard, 2> occupancy{};
    std::array<Square, 2> king_square{{kNoSquare, kNoSquare}};
    Bitboard all = 0;
};

struct SliderTable {
    std::array<Bitboard, 64> mask{};
    std::array<std::uint32_t, 64> offset{};
    std::array<std::uint8_t, 64> bits{};
    std::vector<Bitboard> attacks;
};

struct AttackTables {
    std::array<Bitboard, 64> knight{};
    std::array<Bitboard, 64> king{};
    std::array<std::array<Bitboard, 64>, 2> pawn{};
    std::array<Bitboard, 64> bishop_full{};
    std::array<Bitboard, 64> rook_full{};
    std::array<Bitboard, 64> queen_full{};
    SliderTable bishop;
    SliderTable rook;
};

// Lazy, thread-safe construction of the same software-PEXT attack tables used
// by Booot's BOOOT_ARM64 path. The table sizes are expected to be 5,248 bishop
// entries and 102,400 rook entries, matching the Pascal declarations.
const AttackTables& attack_tables();

Bitboard knight_attacks(Square square) noexcept;
Bitboard king_attacks(Square square) noexcept;
Bitboard pawn_attacks(Color color, Square square) noexcept;
Bitboard bishop_attacks(Square square, Bitboard occupied);
Bitboard rook_attacks(Square square, Bitboard occupied);
Bitboard queen_attacks(Square square, Bitboard occupied);
Bitboard piece_attacks(Color color, Piece type, Square square, Bitboard occupied);

// Equivalent to the software pext(BB, mask) in uBitBoards.pas for BOOOT_ARM64.
std::uint32_t pext_index(Bitboard value, Bitboard mask) noexcept;

// Returns the bitboard of all pieces attacking square, for one color or both.
Bitboard attackers_to(Square square, Bitboard occupied,
                      const AttackPosition& position);
Bitboard attackers_to(Square square, Color by_color, Bitboard occupied,
                      const AttackPosition& position);
bool square_attacked(Square square, Color by_color, Bitboard occupied,
                     const AttackPosition& position);

} // namespace booot

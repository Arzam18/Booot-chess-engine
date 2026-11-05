#include "booot_attacks.hpp"

#include <algorithm>
#include <cstdlib>
#include <array>
#include <stdexcept>
#include <string>

namespace booot {
namespace {

struct Direction { int df; int dr; };
constexpr std::array<Direction, 4> kBishopDirs{{{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};
constexpr std::array<Direction, 4> kRookDirs{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

Bitboard ray_attacks(Square square, Bitboard occupied,
                     const Direction* directions, std::size_t count) noexcept {
    if (square < 0 || square >= 64) return 0;
    Bitboard result = 0;
    const int file = file_of(square);
    const int rank = rank_of(square);
    for (std::size_t d = 0; d < count; ++d) {
        int f = file + directions[d].df;
        int r = rank + directions[d].dr;
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            const Bitboard bit = square_bb(make_square(f, r));
            result |= bit;
            if (occupied & bit) break;
            f += directions[d].df;
            r += directions[d].dr;
        }
    }
    return result;
}

Bitboard bishop_ray(Square square, Bitboard occupied) noexcept {
    return ray_attacks(square, occupied, kBishopDirs.data(), kBishopDirs.size());
}

Bitboard rook_ray(Square square, Bitboard occupied) noexcept {
    return ray_attacks(square, occupied, kRookDirs.data(), kRookDirs.size());
}

Bitboard relevant_mask(Square square, bool bishop) noexcept {
    const Bitboard full = bishop ? bishop_ray(square, 0) : rook_ray(square, 0);
    const int file = file_of(square);
    const int rank = rank_of(square);
    Bitboard mask = full;
    // Keep only occupancy squares whose contents can change a ray before its
    // endpoint. The opposite edge square on each ray is deliberately omitted.
    if (file < 7) mask &= ~file_bb(7);
    if (file > 0) mask &= ~file_bb(0);
    if (rank < 7) mask &= ~rank_bb(7);
    if (rank > 0) mask &= ~rank_bb(0);
    return mask;
}

Bitboard occupancy_from_index(std::uint32_t index, Bitboard mask) noexcept {
    Bitboard occupied = 0;
    std::uint32_t bit_index = 0;
    while (mask) {
        const Square square = pop_lsb(mask);
        if (index & (std::uint32_t{1} << bit_index)) occupied |= square_bb(square);
        ++bit_index;
    }
    return occupied;
}

void build_slider_table(SliderTable& table, bool bishop) {
    std::uint32_t total = 0;
    for (Square square = 0; square < 64; ++square) {
        table.mask[square] = relevant_mask(square, bishop);
        table.bits[square] = static_cast<std::uint8_t>(popcount(table.mask[square]));
        table.offset[square] = total;
        total += (std::uint32_t{1} << table.bits[square]);
    }
    table.attacks.assign(total, 0);

    for (Square square = 0; square < 64; ++square) {
        const std::uint32_t count = std::uint32_t{1} << table.bits[square];
        for (std::uint32_t index = 0; index < count; ++index) {
            const Bitboard occupied = occupancy_from_index(index, table.mask[square]);
            table.attacks[table.offset[square] + index] =
                bishop ? bishop_ray(square, occupied) : rook_ray(square, occupied);
        }
    }
}

AttackTables build_tables() {
    AttackTables tables;
    for (Square square = 0; square < 64; ++square) {
        const int file = file_of(square);
        const int rank = rank_of(square);
        for (int df = -2; df <= 2; ++df) {
            for (int dr = -2; dr <= 2; ++dr) {
                if ((df == 0 && dr == 0) || (std::abs(df) == std::abs(dr))) continue;
                if (std::abs(df) + std::abs(dr) == 3 && df != 0 && dr != 0) {
                    const Square target = make_square(file + df, rank + dr);
                    if (target != kNoSquare) tables.knight[square] |= square_bb(target);
                }
            }
        }
        for (int df = -1; df <= 1; ++df) {
            for (int dr = -1; dr <= 1; ++dr) {
                if (df == 0 && dr == 0) continue;
                const Square target = make_square(file + df, rank + dr);
                if (target != kNoSquare) tables.king[square] |= square_bb(target);
            }
        }
        for (int df : {-1, 1}) {
            const Square white_target = make_square(file + df, rank + 1);
            const Square black_target = make_square(file + df, rank - 1);
            if (white_target != kNoSquare) tables.pawn[White][square] |= square_bb(white_target);
            if (black_target != kNoSquare) tables.pawn[Black][square] |= square_bb(black_target);
        }
        tables.bishop_full[square] = bishop_ray(square, 0);
        tables.rook_full[square] = rook_ray(square, 0);
        tables.queen_full[square] = tables.bishop_full[square] | tables.rook_full[square];
    }
    build_slider_table(tables.bishop, true);
    build_slider_table(tables.rook, false);
    if (tables.bishop.attacks.size() != 5248 || tables.rook.attacks.size() != 102400)
        throw std::runtime_error("Booot slider table size mismatch: bishop=" + std::to_string(tables.bishop.attacks.size()) + ", rook=" + std::to_string(tables.rook.attacks.size()));
    return tables;
}

} // namespace

const AttackTables& attack_tables() {
    static const AttackTables tables = build_tables();
    return tables;
}

std::uint32_t pext_index(Bitboard value, Bitboard mask) noexcept {
    std::uint32_t result = 0;
    std::uint32_t output_bit = 0;
    while (mask) {
        const Square square = pop_lsb(mask);
        if (value & square_bb(square)) result |= (std::uint32_t{1} << output_bit);
        ++output_bit;
    }
    return result;
}

Bitboard knight_attacks(Square square) noexcept {
    return square >= 0 && square < 64 ? attack_tables().knight[square] : 0;
}

Bitboard king_attacks(Square square) noexcept {
    return square >= 0 && square < 64 ? attack_tables().king[square] : 0;
}

Bitboard pawn_attacks(Color color, Square square) noexcept {
    if (square < 0 || square >= 64 || (color != White && color != Black)) return 0;
    return attack_tables().pawn[color][square];
}

Bitboard bishop_attacks(Square square, Bitboard occupied) {
    if (square < 0 || square >= 64) return 0;
    const auto& table = attack_tables().bishop;
    const std::uint32_t index = pext_index(occupied, table.mask[square]);
    return table.attacks[table.offset[square] + index];
}

Bitboard rook_attacks(Square square, Bitboard occupied) {
    if (square < 0 || square >= 64) return 0;
    const auto& table = attack_tables().rook;
    const std::uint32_t index = pext_index(occupied, table.mask[square]);
    return table.attacks[table.offset[square] + index];
}

Bitboard queen_attacks(Square square, Bitboard occupied) {
    return bishop_attacks(square, occupied) | rook_attacks(square, occupied);
}

Bitboard piece_attacks(Color color, Piece type, Square square, Bitboard occupied) {
    switch (type) {
        case Pawn:   return pawn_attacks(color, square);
        case Knight: return knight_attacks(square);
        case Bishop: return bishop_attacks(square, occupied);
        case Rook:   return rook_attacks(square, occupied);
        case Queen:  return queen_attacks(square, occupied);
        case King:   return king_attacks(square);
        default:     return 0;
    }
}

Bitboard attackers_to(Square square, Bitboard occupied,
                      const AttackPosition& position) {
    if (square < 0 || square >= 64) return 0;
    const Bitboard sliders_b = bishop_attacks(square, occupied) &
        (position.pieces[Bishop] | position.pieces[Queen]);
    const Bitboard sliders_r = rook_attacks(square, occupied) &
        (position.pieces[Rook] | position.pieces[Queen]);
    const Bitboard knights = knight_attacks(square) & position.pieces[Knight];
    const Bitboard kings = king_attacks(square) & position.pieces[King];
    const Bitboard pawns =
        (pawn_attacks(Black, square) & position.pieces[Pawn] & position.occupancy[White]) |
        (pawn_attacks(White, square) & position.pieces[Pawn] & position.occupancy[Black]);
    return (sliders_b | sliders_r | knights | kings | pawns) & occupied;
}

Bitboard attackers_to(Square square, Color by_color, Bitboard occupied,
                      const AttackPosition& position) {
    if (square < 0 || square >= 64 || (by_color != White && by_color != Black)) return 0;
    const Bitboard sliders_b = bishop_attacks(square, occupied) &
        (position.pieces[Bishop] | position.pieces[Queen]);
    const Bitboard sliders_r = rook_attacks(square, occupied) &
        (position.pieces[Rook] | position.pieces[Queen]);
    const Bitboard knights = knight_attacks(square) & position.pieces[Knight];
    const Bitboard kings = king_attacks(square) & position.pieces[King];
    const Bitboard pawns = pawn_attacks(static_cast<Color>(by_color ^ 1), square) &
        position.pieces[Pawn];
    return (sliders_b | sliders_r | knights | kings | pawns) &
           occupied & position.occupancy[by_color];
}

bool square_attacked(Square square, Color by_color, Bitboard occupied,
                     const AttackPosition& position) {
    return attackers_to(square, by_color, occupied, position) != 0;
}

} // namespace booot

#include "booot_attacks.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>

using namespace booot;

namespace {
struct Dir { int df, dr; };
constexpr Dir BDirs[] = {{1,1},{1,-1},{-1,1},{-1,-1}};
constexpr Dir RDirs[] = {{1,0},{-1,0},{0,1},{0,-1}};

Bitboard slow_ray(Square sq, Bitboard occ, const Dir* dirs, int n) {
    Bitboard out = 0;
    for (int i = 0; i < n; ++i) {
        int f = file_of(sq) + dirs[i].df;
        int r = rank_of(sq) + dirs[i].dr;
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            const Bitboard b = square_bb(make_square(f, r));
            out |= b;
            if (occ & b) break;
            f += dirs[i].df;
            r += dirs[i].dr;
        }
    }
    return out;
}

Bitboard expand_index(std::uint32_t index, Bitboard mask) {
    Bitboard out = 0;
    unsigned bit = 0;
    while (mask) {
        const Square sq = pop_lsb(mask);
        if (index & (1U << bit)) out |= square_bb(sq);
        ++bit;
    }
    return out;
}
}

int main() {
    static_assert(square_bb(0) == 1ULL);
    static_assert(square_bb(63) == (1ULL << 63));
    assert(popcount(0xF0F0ULL) == 8);
    assert(lsb(0) == kNoSquare && msb(0) == kNoSquare);
    assert(lsb(0x8000000000000081ULL) == 0);
    assert(msb(0x8000000000000081ULL) == 63);

    const auto& t = attack_tables();
    assert(t.bishop.attacks.size() == 5248);
    assert(t.rook.attacks.size() == 102400);

    // Exhaustively validate every relevant blocker subset against independent
    // coordinate/ray stepping. This covers every occupancy represented by
    // Booot's ARM64 software-PEXT lookup tables.
    std::uint64_t checked_bishop = 0;
    std::uint64_t checked_rook = 0;
    for (Square sq = 0; sq < 64; ++sq) {
        const std::uint32_t bc = 1U << t.bishop.bits[sq];
        for (std::uint32_t i = 0; i < bc; ++i) {
            const Bitboard occ = expand_index(i, t.bishop.mask[sq]);
            assert(pext_index(occ, t.bishop.mask[sq]) == i);
            assert(bishop_attacks(sq, occ) == slow_ray(sq, occ, BDirs, 4));
            ++checked_bishop;
        }
        const std::uint32_t rc = 1U << t.rook.bits[sq];
        for (std::uint32_t i = 0; i < rc; ++i) {
            const Bitboard occ = expand_index(i, t.rook.mask[sq]);
            assert(pext_index(occ, t.rook.mask[sq]) == i);
            assert(rook_attacks(sq, occ) == slow_ray(sq, occ, RDirs, 4));
            ++checked_rook;
        }
    }
    assert(checked_bishop == 5248 && checked_rook == 102400);

    // Check occupancies outside the relevant masks as well, including edge
    // blockers. A blocker on the final board-edge square still remains an
    // attacked endpoint, exactly as in the original Pascal generator.
    std::mt19937_64 rng(0xB00774ULL);
    for (int i = 0; i < 5000; ++i) {
        const Square sq = static_cast<Square>(rng() & 63U);
        const Bitboard occ = rng();
        assert(bishop_attacks(sq, occ) == slow_ray(sq, occ, BDirs, 4));
        assert(rook_attacks(sq, occ) == slow_ray(sq, occ, RDirs, 4));
    }

    // Leaper geometry and pawn direction checks.
    assert(popcount(knight_attacks(0)) == 2);   // a1: b3, c2
    assert(popcount(king_attacks(0)) == 3);     // a1: b1, a2, b2
    assert(pawn_attacks(White, 8) == square_bb(17));
    assert(pawn_attacks(Black, 48) == square_bb(41));
    assert(pawn_attacks(White, 56) == 0);
    assert(pawn_attacks(Black, 7) == 0);

    // AttackersTo convention: positive white piece codes, negative black in
    // the Pascal board; piece bitboards are color-unioned, color occupancy
    // separates the side just like uAttacks.SquareAttackedBB.
    AttackPosition p;
    p.pieces[King] = square_bb(4) | square_bb(60);
    p.pieces[Queen] = square_bb(3);
    p.pieces[Knight] = square_bb(18);
    p.pieces[Pawn] = square_bb(27) | square_bb(36);
    p.occupancy[White] = square_bb(4) | square_bb(3) | square_bb(18) | square_bb(27);
    p.occupancy[Black] = square_bb(60) | square_bb(36);
    p.all = p.occupancy[White] | p.occupancy[Black];
    assert((attackers_to(27, White, p.all, p) & square_bb(3)) != 0); // queen d1 attacks d4

    std::cout << "Booot bitboard/attack stage-2 self-test: PASS\n"
              << "Exhaustive bishop occupancies: " << checked_bishop << "\n"
              << "Exhaustive rook occupancies: " << checked_rook << "\n";
    return 0;
}

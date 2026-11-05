#include "booot_bitboard.hpp"

#if defined(__GNUC__) || defined(__clang__)
#include <bit>
#endif

namespace booot {

int popcount(Bitboard value) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(value);
#else
    int count = 0;
    while (value) {
        value &= value - 1;
        ++count;
    }
    return count;
#endif
}

Square lsb(Bitboard value) noexcept {
    if (!value) return kNoSquare;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(value);
#else
    Square result = 0;
    while ((value & 1U) == 0U) {
        value >>= 1;
        ++result;
    }
    return result;
#endif
}

Square msb(Bitboard value) noexcept {
    if (!value) return kNoSquare;
#if defined(__GNUC__) || defined(__clang__)
    return 63 - __builtin_clzll(value);
#else
    Square result = 0;
    while (value >>= 1) ++result;
    return result;
#endif
}

Square pop_lsb(Bitboard& value) noexcept {
    const Square result = lsb(value);
    if (result != kNoSquare) value &= value - 1;
    return result;
}

Bitboard file_bb(int file) noexcept {
    if (file < 0 || file > 7) return 0;
    return 0x0101010101010101ULL << file;
}

Bitboard rank_bb(int rank) noexcept {
    if (rank < 0 || rank > 7) return 0;
    return 0xFFULL << (rank * 8);
}

Bitboard adjacent_files_bb(Square square) noexcept {
    if (square < 0 || square >= 64) return 0;
    const int file = file_of(square);
    return file_bb(file - 1) | file_bb(file + 1);
}

Bitboard forward_ranks_bb(Color color, Square square) noexcept {
    if (square < 0 || square >= 64) return 0;
    Bitboard result = 0;
    if (color == White) {
        for (int rank = rank_of(square) + 1; rank < 8; ++rank)
            result |= rank_bb(rank);
    } else {
        for (int rank = rank_of(square) - 1; rank >= 0; --rank)
            result |= rank_bb(rank);
    }
    return result;
}

Bitboard passer_span_bb(Color color, Square square) noexcept {
    if (square < 0 || square >= 64) return 0;
    return (file_bb(file_of(square)) | adjacent_files_bb(square)) &
           forward_ranks_bb(color, square);
}

} // namespace booot

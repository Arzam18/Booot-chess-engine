#include "booot_endgame.hpp"

#include "booot_attacks.hpp"
#include "booot_bitboard.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <mutex>

namespace booot {
namespace {

constexpr std::uint8_t kDraw = 0;
constexpr std::uint8_t kDeadDraw = 253;
constexpr std::uint8_t kStaleMate = 254;
constexpr std::uint8_t kBroken = 255;
constexpr int kPawnlessFunctionPieceThreshold = 3; // TypOfPiese[Bishop] in uEndgame.pas
constexpr int kPawnlessLowMaterialThreshold = 4;  // TypOfPiese[Rook] in uEndgame.pas
constexpr int kKnightValue = 370;
constexpr int kBishopValue = 380;
constexpr int kRookValue = 560;
constexpr int kMinorDifference = kBishopValue - kKnightValue;

constexpr std::array<Square, 24> kPawnSquares{{
    8, 16, 24, 32, 40, 48,
    9, 17, 25, 33, 41, 49,
    10, 18, 26, 34, 42, 50,
    11, 19, 27, 35, 43, 51
}};

constexpr std::array<int, 64> make_pawn_number() {
    std::array<int, 64> map{};
    map.fill(31);
    for (int file = 0; file < 4; ++file)
        for (int rank = 1; rank <= 6; ++rank)
            map[rank * 8 + file] = file * 6 + (rank - 1);
    return map;
}
constexpr auto kPawnNumber = make_pawn_number();

constexpr std::array<int, 64> kWeakKingMate{{
    100,90,80,70,70,80,90,100,
    90,70,60,50,50,60,70,90,
    80,60,40,30,30,40,60,80,
    70,50,30,20,20,30,50,70,
    70,50,30,20,20,30,50,70,
    80,60,40,30,30,40,60,80,
    90,70,60,50,50,60,70,90,
    100,90,80,70,70,80,90,100
}};
constexpr std::array<int, 9> kKingDistanceBonus{{0,0,0,50,25,10,5,2,0}};
constexpr std::array<int, 64> kBnLight{{
    20,30,40,50,60,70,80,100,
    30,20,30,40,50,60,70,80,
    40,30,20,30,40,50,60,70,
    50,40,30,20,30,40,50,60,
    60,50,40,30,20,30,40,50,
    70,60,50,40,30,20,30,40,
    80,70,60,50,40,30,20,30,
    100,80,70,60,50,40,30,20
}};
constexpr std::array<int, 64> kBnDark{{
    100,80,70,60,50,40,30,20,
    80,70,60,50,40,30,20,30,
    70,60,50,40,30,20,30,40,
    60,50,40,30,20,30,40,50,
    50,40,30,20,30,40,50,60,
    40,30,20,30,40,50,60,70,
    30,20,30,40,50,60,70,80,
    20,30,40,50,60,70,80,100
}};

constexpr Bitboard make_dark_squares() {
    Bitboard result = 0;
    for (Square sq = 0; sq < 64; ++sq)
        if (((file_of(sq) + rank_of(sq)) & 1) == 0) result |= square_bb(sq);
    return result;
}
constexpr Bitboard kDarkSquares = make_dark_squares();
constexpr Bitboard kLightSquares = ~kDarkSquares;

struct KpkTables {
    std::array<std::uint8_t, kKpkMaxIndex + 1> white{};
    std::array<std::uint8_t, kKpkMaxIndex + 1> black{};
};
KpkTables g_kpk;
std::once_flag g_kpk_once;

constexpr Square vertical_reflect(Square sq) noexcept { return sq ^ 56; }
constexpr Square horizontal_reflect(Square sq) noexcept {
    return (sq & 56) | (7 - (sq & 7));
}
constexpr int chebyshev_distance(Square a, Square b) noexcept {
    return std::max(std::abs(file_of(a) - file_of(b)),
                    std::abs(rank_of(a) - rank_of(b)));
}
constexpr std::uint32_t kpk_index(Square pawn, Square wk, Square bk) noexcept {
    const int pn = (pawn >= 0 && pawn < 64) ? kPawnNumber[pawn] : 31;
    return static_cast<std::uint32_t>(bk | (wk << 6) | (pn << 12));
}

struct KpkMoveList {
    std::array<std::uint16_t, 10> moves{};
    int count = 0;
};
constexpr std::uint16_t kpk_move(Square from, Square to) noexcept {
    return static_cast<std::uint16_t>(from | (to << 6));
}

void add_kpk_moves(KpkMoveList& list, Square piece, Bitboard targets) {
    while (targets) {
        const Square to = pop_lsb(targets);
        if (list.count < static_cast<int>(list.moves.size()))
            list.moves[list.count++] = kpk_move(piece, to);
    }
}

KpkMoveList kpk_moves(Color side, Square pawn, Square wk, Square bk) {
    KpkMoveList list;
    const Bitboard kings = square_bb(wk) | square_bb(bk);
    if (side == White) {
        const Bitboard king_targets = king_attacks(wk) &
            ~(king_attacks(bk) | square_bb(pawn));
        add_kpk_moves(list, wk, king_targets);

        const Square one = pawn + 8;
        if (one < 64 && !(square_bb(one) & kings)) {
            list.moves[list.count++] = kpk_move(pawn, one);
            if (pawn < 16) {
                const Square two = pawn + 16;
                if (!(square_bb(two) & kings))
                    list.moves[list.count++] = kpk_move(pawn, two);
            }
        }
    } else {
        const Bitboard king_targets = king_attacks(bk) &
            ~(king_attacks(wk) | pawn_attacks(White, pawn));
        add_kpk_moves(list, bk, king_targets);
    }
    return list;
}

void decode_kpk_index(std::uint32_t index, Square& pawn, Square& wk, Square& bk) {
    bk = static_cast<Square>(index & 63U);
    wk = static_cast<Square>((index >> 6) & 63U);
    const int pawn_number = static_cast<int>((index >> 12) & 31U);
    pawn = (pawn_number < 24) ? kPawnSquares[pawn_number] : 0;
}

bool invalid_kpk_position(Square pawn, Square wk, Square bk) {
    return wk == bk || wk == pawn || bk == pawn ||
           ((king_attacks(wk) & square_bb(bk)) != 0);
}

void build_kpk_tables() {
    g_kpk.white.fill(kDraw);
    g_kpk.black.fill(kDraw);

    for (std::uint32_t index = 0; index <= kKpkMaxIndex; ++index) {
        Square pawn = 0, wk = 0, bk = 0;
        decode_kpk_index(index, pawn, wk, bk);

        if (invalid_kpk_position(pawn, wk, bk)) {
            g_kpk.white[index] = kBroken;
            g_kpk.black[index] = kBroken;
            continue;
        }

        if (pawn_attacks(White, pawn) & square_bb(bk))
            g_kpk.white[index] = kBroken;

        const KpkMoveList black_moves = kpk_moves(Black, pawn, wk, bk);
        if (black_moves.count == 0) {
            g_kpk.black[index] = kStaleMate;
        } else if ((king_attacks(bk) & square_bb(pawn)) &&
                   !(king_attacks(wk) & square_bb(pawn))) {
            g_kpk.black[index] = kDeadDraw;
        }

        if (g_kpk.white[index] == kDraw) {
            const KpkMoveList white_moves = kpk_moves(White, pawn, wk, bk);
            if (white_moves.count == 0) {
                g_kpk.white[index] = kStaleMate;
            } else {
                const Square promotion = pawn + 8;
                if (pawn > 47 && promotion < 64 &&
                    !(square_bb(promotion) & (square_bb(wk) | square_bb(bk))) &&
                    (!(king_attacks(bk) & square_bb(promotion)) ||
                     (king_attacks(wk) & square_bb(promotion)))) {
                    g_kpk.white[index] = 1;
                }
            }
        }
    }

    int rank = 1;
    std::uint64_t nodes = 0;
    while (nodes != 0 || rank == 1) {
        // Black to move: a position is a White win only when every legal black
        // reply reaches a White-to-move state already known to be won.
        for (std::uint32_t index = 0; index <= kKpkMaxIndex; ++index) {
            if (g_kpk.black[index] != kDraw) continue;
            Square pawn = 0, wk = 0, bk = 0;
            decode_kpk_index(index, pawn, wk, bk);
            const KpkMoveList moves = kpk_moves(Black, pawn, wk, bk);
            bool has_draw_reply = false;
            for (int i = 0; i < moves.count; ++i) {
                const Square new_bk = static_cast<Square>((moves.moves[i] >> 6) & 63U);
                const std::uint32_t next = kpk_index(pawn, wk, new_bk);
                const std::uint8_t child = g_kpk.white[next];
                if (child == kDraw || child == kStaleMate || child == kDeadDraw) {
                    has_draw_reply = true;
                    break;
                }
            }
            if (!has_draw_reply) g_kpk.black[index] = static_cast<std::uint8_t>(rank);
        }

        nodes = 0;
        ++rank;
        // White to move: one move into a black-to-move state won at the
        // previous rank is sufficient to establish a forced win.
        for (std::uint32_t index = 0; index <= kKpkMaxIndex; ++index) {
            if (g_kpk.white[index] != kDraw) continue;
            Square pawn = 0, wk = 0, bk = 0;
            decode_kpk_index(index, pawn, wk, bk);
            const KpkMoveList moves = kpk_moves(White, pawn, wk, bk);
            bool is_win = false;
            for (int i = 0; i < moves.count; ++i) {
                const Square from = static_cast<Square>(moves.moves[i] & 63U);
                const Square to = static_cast<Square>((moves.moves[i] >> 6) & 63U);
                Square new_wk = wk;
                Square new_pawn = pawn;
                if (from == wk) new_wk = to;
                else new_pawn = to;
                const std::uint32_t next = kpk_index(new_pawn, new_wk, bk);
                if (next <= static_cast<std::uint32_t>(kKpkMaxIndex) &&
                    g_kpk.black[next] == rank - 1) {
                    is_win = true;
                    break;
                }
            }
            if (is_win) {
                g_kpk.white[index] = static_cast<std::uint8_t>(rank);
                ++nodes;
            }
        }
    }
}

int weak_king_mate_score(const Position& p) {
    const int material = p.non_pawn_material[White] - p.non_pawn_material[Black];
    int extra = 0;
    // These two thresholds intentionally mirror the original Pascal
    // TypOfPiese[Bishop]/TypOfPiese[Rook] expressions (3 and 4), not centipawn
    // piece values. They look unusual but changing them would alter Booot 7.4.
    if (material > kPawnlessFunctionPieceThreshold) extra = kNearlyWin;
    else if (material < -kPawnlessFunctionPieceThreshold) extra = -kNearlyWin;

    int bonus = 0;
    const int distance = chebyshev_distance(p.king_square[White], p.king_square[Black]);
    if (material > kMinorDifference) {
        bonus = kWeakKingMate[p.king_square[Black]] + kKingDistanceBonus[distance];
    } else if (material < -kMinorDifference) {
        bonus = -kWeakKingMate[p.king_square[White]] - kKingDistanceBonus[distance];
    } else {
        bonus = kWeakKingMate[p.king_square[Black]] - kWeakKingMate[p.king_square[White]];
    }

    int score = material;
    if (std::abs(score) <= kBishopValue) {
        score = (score + bonus) / 4;
        if (p.non_pawn_material[White] <= kPawnlessLowMaterialThreshold &&
            p.non_pawn_material[Black] <= kPawnlessLowMaterialThreshold)
            score /= 2;
    } else {
        score = score + bonus + extra;
    }
    if (p.side_to_move == Black) score = -score;
    return score;
}

int kbnk_score(const Position& p) {
    const int base = kBishopValue + kKnightValue + kNearlyWin;
    const int wk = p.king_square[White];
    const int bk = p.king_square[Black];
    const int distance_bonus = kKingDistanceBonus[chebyshev_distance(wk, bk)];
    const bool bishop_dark = (p.pieces[Bishop] & kDarkSquares) != 0;
    int score = 0;

    if (p.non_pawn_material[Black] == 0) {
        score = base + 2 * (bishop_dark ? kBnDark[bk] : kBnLight[bk]) + distance_bonus;
        if (p.side_to_move == Black) score = -score;
    } else {
        score = base - 2 * (bishop_dark ? kBnDark[wk] : kBnLight[wk]) - distance_bonus;
        if (p.side_to_move == White) score = -score;
    }
    return score;
}

int kpk_score(const Position& p) {
    Square wk = 0, bk = 0, pawn = 0;
    int color = p.side_to_move;
    if (p.occupancy[White] & p.pieces[Pawn]) {
        wk = p.king_square[White];
        bk = p.king_square[Black];
        pawn = lsb(p.occupancy[White] & p.pieces[Pawn]);
    } else {
        wk = vertical_reflect(p.king_square[Black]);
        bk = vertical_reflect(p.king_square[White]);
        pawn = vertical_reflect(lsb(p.occupancy[Black] & p.pieces[Pawn]));
        color ^= 1;
    }
    if (pawn < 0 || pawn >= 64) return 0;
    if (kPawnNumber[pawn] > 23) {
        pawn = horizontal_reflect(pawn);
        wk = horizontal_reflect(wk);
        bk = horizontal_reflect(bk);
    }
    const int result = kpk_probe(static_cast<Color>(color), pawn, wk, bk);
    if (result == 0) return 0;
    if (result > 0) return kKpkWin - result * 5;
    return -kKpkWin + result * 5;
}

void scale_for_rook_pawn_draw(int& scale, const Position& p, Color pawn_side) noexcept {
    const Color defending_side = static_cast<Color>(pawn_side ^ 1);
    const Bitboard pawns = p.pieces[Pawn] & p.occupancy[pawn_side];
    const Square defending_king = p.king_square[defending_side];
    const Bitboard bishop = p.pieces[Bishop];
    const int promotion_rank = pawn_side == White ? 7 : 0;
    const Square a_corner = make_square(0, promotion_rank);
    const Square h_corner = make_square(7, promotion_rank);

    if ((pawns & file_bb(0)) && !(pawns & ~file_bb(0)) &&
        chebyshev_distance(a_corner, defending_king) <= 1) {
        const bool bishop_wrong_color = pawn_side == White
            ? (bishop & kDarkSquares) != 0
            : (bishop & kLightSquares) != 0;
        if (bishop_wrong_color) scale = kScaleDraw;
    }
    if ((pawns & file_bb(7)) && !(pawns & ~file_bb(7)) &&
        chebyshev_distance(h_corner, defending_king) <= 1) {
        const bool bishop_wrong_color = pawn_side == White
            ? (bishop & kLightSquares) != 0
            : (bishop & kDarkSquares) != 0;
        if (bishop_wrong_color) scale = kScaleDraw;
    }
}

} // namespace

void initialize_kpk() {
    std::call_once(g_kpk_once, build_kpk_tables);
}

int kpk_probe(Color color, Square pawn, Square white_king, Square black_king) {
    if (pawn < 0 || pawn >= 64 || white_king < 0 || white_king >= 64 ||
        black_king < 0 || black_king >= 64)
        return 0;
    const std::uint32_t index = kpk_index(pawn, white_king, black_king);
    if (index > static_cast<std::uint32_t>(kKpkMaxIndex)) return 0;
    initialize_kpk();
    int result = color == White ? g_kpk.white[index] : g_kpk.black[index];
    if (result == kBroken || result == kDeadDraw || result == kStaleMate) return 0;
    if (color == Black) result = -result;
    return result;
}

std::size_t kpk_table_entries() noexcept { return 2U * (kKpkMaxIndex + 1U); }

int special_cases(const Position& p) noexcept {
    const int white_pawns = popcount(p.pieces[Pawn] & p.occupancy[White]);
    const int black_pawns = popcount(p.pieces[Pawn] & p.occupancy[Black]);
    const int npw = p.non_pawn_material[White];
    const int npb = p.non_pawn_material[Black];
    int function = EndgameNone;

    if (white_pawns + black_pawns == 0) {
        function = EndgamePawnless;
        if ((npw == 0 && npb == kKnightValue + kBishopValue) ||
            (npb == 0 && npw == kKnightValue + kBishopValue)) {
            function = EndgameKBNK;
        } else if ((npw == 0 && npb == 2 * kKnightValue) ||
                   (npb == 0 && npw == 2 * kKnightValue)) {
            function = EndgameMaterialDraw;
        } else if (npw < kRookValue && npb < kRookValue) {
            function = EndgameMaterialDraw;
        }
    }

    if (npw == 0 && npb == 0 && white_pawns + black_pawns == 1)
        function = EndgameKPK;
    if (npw == kBishopValue && white_pawns > 0)
        function = EndgameKBPSWhite;
    if (npb == kBishopValue && black_pawns > 0)
        function = EndgameKBPSBlack;
    return function;
}

int evaluate_special_endgame(int function, const Position& p) {
    switch (function) {
        case EndgameKBNK: return kbnk_score(p);
        case EndgameKPK: return kpk_score(p);
        case EndgamePawnless: return weak_king_mate_score(p);
        case EndgameMaterialDraw: return 0;
        default: return 0;
    }
}

void kbps_kw(int& white_scale, const Position& p) noexcept {
    scale_for_rook_pawn_draw(white_scale, p, White);
}

void kbps_kb(int& black_scale, const Position& p) noexcept {
    scale_for_rook_pawn_draw(black_scale, p, Black);
}

int evaluate_with_endgame(const Position& p, int nnue_score, int win_score) {
    const int function = special_cases(p);
    if (function == EndgameKBNK || function == EndgameKPK ||
        function == EndgamePawnless || function == EndgameMaterialDraw)
        return evaluate_special_endgame(function, p);

    int score = nnue_score;
    if (function == EndgameKBPSWhite) {
        int scale = kScaleNormal;
        kbps_kw(scale, p);
        if (scale != kScaleNormal &&
            ((p.side_to_move == White && score > 0) ||
             (p.side_to_move == Black && score < 0)))
            score = (score * scale) / kScaleNormal;
    }
    if (function == EndgameKBPSBlack) {
        int scale = kScaleNormal;
        kbps_kb(scale, p);
        if (scale != kScaleNormal &&
            ((p.side_to_move == Black && score > 0) ||
             (p.side_to_move == White && score < 0)))
            score = (score * scale) / kScaleNormal;
    }
    return std::clamp(score, -win_score, win_score);
}

} // namespace booot

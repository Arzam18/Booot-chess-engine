#include "booot_tactics.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>

namespace booot {
namespace {

constexpr std::array<int, 7> kSeeValues{{0, 100, 300, 300, 500, 900, 10000}};

constexpr Piece piece_type_of(int signed_piece) noexcept {
    return static_cast<Piece>(signed_piece < 0 ? -signed_piece : signed_piece);
}

constexpr Color color_of(int signed_piece) noexcept {
    return signed_piece > 0 ? White : Black;
}

bool is_castling_move(Move move, const Position& p) noexcept {
    const Square from = move_from(move);
    const Square to = move_to(move);
    return p.board[from] != 0 && piece_type_of(p.board[from]) == King &&
           (from - to == 2 || to - from == 2);
}

bool is_en_passant_move(Move move, const Position& p) noexcept {
    const Square to = move_to(move);
    const Square from = move_from(move);
    return move_is_capture(move) && p.board[to] == Empty &&
           p.board[from] != 0 && piece_type_of(p.board[from]) == Pawn;
}

bool is_special_move(Move move, const Position& p) noexcept {
    return move_is_promotion(move) || is_en_passant_move(move, p) ||
           is_castling_move(move, p);
}

bool contains_move(const std::vector<Move>& moves, Move move) {
    return std::find(moves.begin(), moves.end(), move) != moves.end();
}

} // namespace

Bitboard between_squares(Square from, Square to) noexcept {
    if (from < 0 || from >= 64 || to < 0 || to >= 64 || from == to) return 0;
    const int from_file = file_of(from);
    const int from_rank = rank_of(from);
    const int to_file = file_of(to);
    const int to_rank = rank_of(to);
    const int df_raw = to_file - from_file;
    const int dr_raw = to_rank - from_rank;
    if (!(df_raw == 0 || dr_raw == 0 || std::abs(df_raw) == std::abs(dr_raw)))
        return 0;

    const int df = (df_raw > 0) - (df_raw < 0);
    const int dr = (dr_raw > 0) - (dr_raw < 0);
    Bitboard result = 0;
    int file = from_file + df;
    int rank = from_rank + dr;
    while (file != to_file || rank != to_rank) {
        result |= square_bb(make_square(file, rank));
        file += df;
        rank += dr;
    }
    return result;
}

Bitboard find_pinners(Color piece_color, Color king_color,
                      const Position& p) {
    if ((piece_color != White && piece_color != Black) ||
        (king_color != White && king_color != Black)) return 0;
    const Square king = p.king_square[king_color];
    if (king == kNoSquare) return 0;

    const auto& tables = attack_tables();
    const Bitboard candidate_sliders =
        ((tables.bishop_full[king] & (p.pieces[Bishop] | p.pieces[Queen])) |
         (tables.rook_full[king] & (p.pieces[Rook] | p.pieces[Queen]))) &
        p.occupancy[king_color ^ 1];

    Bitboard result = 0;
    Bitboard candidates = candidate_sliders;
    while (candidates) {
        const Square slider = pop_lsb(candidates);
        const Bitboard blockers = between_squares(king, slider) & p.all;
        // Pascal accepts only zero or one intervening blocker, then intersects
        // it with the requested piece color. With zero blockers the result is 0.
        if (blockers && !(blockers & (blockers - 1)))
            result |= blockers & p.occupancy[piece_color];
    }
    return result;
}

void fill_check_info(CheckInfo& info, const Position& p) {
    info = CheckInfo{};
    const Color us = static_cast<Color>(p.side_to_move);
    const Color them = static_cast<Color>(us ^ 1);
    const Square enemy_king = p.king_square[them];
    info.discover_check = find_pinners(us, them, p);
    info.pinned = find_pinners(us, us, p);
    info.enemy_king_square = enemy_king;
    if (enemy_king == kNoSquare) return;

    info.direct_check[Pawn] = pawn_attacks(them, enemy_king);
    info.direct_check[Knight] = knight_attacks(enemy_king);
    info.direct_check[Bishop] = bishop_attacks(enemy_king, p.all);
    info.direct_check[Rook] = rook_attacks(enemy_king, p.all);
    info.direct_check[Queen] = info.direct_check[Bishop] | info.direct_check[Rook];
    info.direct_check[King] = 0;
}

bool is_move_check(Move move, const CheckInfo& info, const Position& p) {
    const Square from = move_from(move);
    const Square to = move_to(move);
    if (from < 0 || from >= 64 || to < 0 || to >= 64 || p.board[from] == Empty ||
        info.enemy_king_square == kNoSquare) return false;

    const Piece moving_type = piece_type_of(p.board[from]);
    if ((info.direct_check[moving_type] & square_bb(to)) != 0) return true;

    if (info.discover_check &&
        (info.discover_check & square_bb(from)) != 0 &&
        (between_squares(from, info.enemy_king_square) & square_bb(to)) == 0 &&
        (between_squares(to, info.enemy_king_square) & square_bb(from)) == 0)
        return true;

    if (move_is_promotion(move)) {
        const Piece promoted = move_promotion(move);
        const Bitboard occupied_without_from = p.all ^ square_bb(from);
        return (piece_attacks(static_cast<Color>(p.side_to_move), promoted, to,
                              occupied_without_from) &
                square_bb(info.enemy_king_square)) != 0;
    }

    // These special cases alter more than the moving piece's destination
    // (en-passant removes a pawn off-target; castling moves a rook too).
    // Simulating them avoids overlooking discovered checks or blockers.
    if (is_en_passant_move(move, p) || is_castling_move(move, p)) {
        Position after = p;
        Undo undo;
        if (!make_move(after, move, undo)) return false;
        const Color mover = static_cast<Color>(p.side_to_move);
        return square_attacked(after, after.king_square[mover ^ 1], mover);
    }
    return false;
}

bool is_legal(Move move, Bitboard pinned, const Position& p) {
    const Square from = move_from(move);
    const Square to = move_to(move);
    if (from < 0 || from >= 64 || to < 0 || to >= 64 ||
        from == to || p.board[from] == Empty) return false;

    const Piece moving_type = piece_type_of(p.board[from]);
    if (is_en_passant_move(move, p) || moving_type == King) {
        Position after = p;
        Undo undo;
        if (!make_move(after, move, undo)) return false;
        return !in_check(after, static_cast<Color>(p.side_to_move));
    }

    if ((pinned & square_bb(from)) == 0) return true;
    const Square king = p.king_square[p.side_to_move];
    if (king == kNoSquare) return false;

    // Matches the two InterSect[] tests in uAttacks.isLegal: the pinned piece
    // must stay on the king/source line, including movement toward or away
    // from the king along that line.
    return (between_squares(king, to) & square_bb(from)) != 0 ||
           (between_squares(king, from) & square_bb(to)) != 0;
}

bool is_pseudo_correct(Move move, const Position& p) {
    const Square from = move_from(move);
    const Square to = move_to(move);
    if (from < 0 || from >= 64 || to < 0 || to >= 64 || from == to ||
        p.board[from] == Empty || color_of(p.board[from]) != p.side_to_move ||
        (p.occupancy[p.side_to_move] & square_bb(to)) != 0)
        return false;

    std::vector<Move> pseudo;
    generate_pseudo_legal_moves(p, pseudo);
    if (!contains_move(pseudo, move)) return false;

    // Booot's original SlowCheck routes promotions, en-passant, and castling
    // through the complete legal move generator.
    if (is_special_move(move, p)) {
        std::vector<Move> legal;
        generate_legal_moves(p, legal);
        return contains_move(legal, move);
    }

    const Color us = static_cast<Color>(p.side_to_move);
    const Color them = static_cast<Color>(us ^ 1);
    const Square king = p.king_square[us];
    if (king == kNoSquare) return false;
    const Bitboard checkers = attackers_to(p, king, them);
    if (!checkers) return true;

    const Piece moving_type = piece_type_of(p.board[from]);
    if (moving_type == King) {
        Position after = p;
        Undo undo;
        return make_move(after, move, undo) && !in_check(after, us);
    }

    // A double check can only be answered by moving the king.
    if (checkers & (checkers - 1)) return false;
    const Square checker = lsb(checkers);
    const Bitboard evasion_targets = between_squares(king, checker) | square_bb(checker);
    return (evasion_targets & square_bb(to)) != 0;
}

bool good_see(Move move, const Position& p, int margin) {
    const Square from = move_from(move);
    const Square to = move_to(move);
    if (from < 0 || from >= 64 || to < 0 || to >= 64 ||
        from == to || p.board[from] == Empty) return false;

    const int moving_code = p.board[from];
    const Piece moving_type = piece_type_of(moving_code);
    const Color moving_color = color_of(moving_code);
    if (is_castling_move(move, p)) return 0 >= margin;

    const bool en_passant = is_en_passant_move(move, p);
    const int victim_value = en_passant ? kSeeValues[Pawn]
        : kSeeValues[piece_type_of(p.board[to])];
    int swap = victim_value - margin;
    if (swap < 0) return false;
    swap -= kSeeValues[moving_type];
    if (swap >= 0) return true;

    Color side = static_cast<Color>(moving_color ^ 1);
    Bitboard occupied = (p.all ^ square_bb(from)) | square_bb(to);
    if (en_passant) {
        const Square captured_square = to + (moving_color == White ? -8 : 8);
        if (captured_square < 0 || captured_square >= 64) return false;
        occupied ^= square_bb(captured_square);
    }

    const AttackPosition attack_view = attack_position(p);
    Bitboard attackers = booot::attackers_to(to, occupied, attack_view);
    const Bitboard diagonal_sliders = p.pieces[Bishop] | p.pieces[Queen];
    const Bitboard line_sliders = p.pieces[Rook] | p.pieces[Queen];

    for (;;) {
        attackers &= occupied;
        const Bitboard side_attackers = attackers & p.occupancy[side];
        if (!side_attackers) break;

        Piece least = Pawn;
        while (least < King && !(p.pieces[least] & side_attackers))
            least = static_cast<Piece>(least + 1);
        if (least == King && (attackers & p.occupancy[side ^ 1])) break;

        side = static_cast<Color>(side ^ 1);
        swap = -swap - 1 - kSeeValues[least];
        if (swap >= 0) break;

        const Square attacker_square = least == King ? p.king_square[side ^ 1]
            : lsb(side_attackers & p.pieces[least]);
        if (attacker_square == kNoSquare) break;
        occupied &= ~square_bb(attacker_square);

        if (least == Pawn || least == Bishop || least == Queen)
            attackers |= bishop_attacks(to, occupied) & diagonal_sliders;
        if (least == Rook || least == Queen)
            attackers |= rook_attacks(to, occupied) & line_sliders;
    }

    return moving_color != side;
}

} // namespace booot

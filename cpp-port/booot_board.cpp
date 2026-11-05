#include "booot_board.hpp"
#include "booot_hash.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>
#include <string_view>

namespace booot {
namespace {

constexpr int piece_color(int signed_piece) noexcept {
    return signed_piece > 0 ? White : Black;
}
constexpr Piece piece_type(int signed_piece) noexcept {
    return static_cast<Piece>(signed_piece < 0 ? -signed_piece : signed_piece);
}
constexpr int signed_piece(Color color, Piece type) noexcept {
    return color == White ? static_cast<int>(type) : -static_cast<int>(type);
}

char piece_to_char(int piece) noexcept {
    constexpr char names[] = " pnbrqk";
    if (piece == 0) return '1';
    const char c = names[piece_type(piece)];
    return piece > 0 ? c : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

int char_to_piece(char c) noexcept {
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    Piece type = Empty;
    switch (lower) {
        case 'p': type = Pawn; break;
        case 'n': type = Knight; break;
        case 'b': type = Bishop; break;
        case 'r': type = Rook; break;
        case 'q': type = Queen; break;
        case 'k': type = King; break;
        default: return 0;
    }
    return std::isupper(static_cast<unsigned char>(c)) ? type : -static_cast<int>(type);
}

void add_piece(Position& p, Square sq, int piece) {
    p.board[sq] = static_cast<std::int8_t>(piece);
    const Color color = static_cast<Color>(piece_color(piece));
    const Piece type = piece_type(piece);
    const Bitboard bit = square_bb(sq);
    p.pieces[type] |= bit;
    p.occupancy[color] |= bit;
    p.all |= bit;
    if (type == King) p.king_square[color] = sq;
}

void remove_piece(Position& p, Square sq) {
    const int piece = p.board[sq];
    if (!piece) return;
    const Color color = static_cast<Color>(piece_color(piece));
    const Piece type = piece_type(piece);
    const Bitboard bit = square_bb(sq);
    p.board[sq] = 0;
    p.pieces[type] &= ~bit;
    p.occupancy[color] &= ~bit;
    p.all &= ~bit;
    if (type == King) p.king_square[color] = kNoSquare;
}

void move_piece(Position& p, Square from, Square to) {
    const int piece = p.board[from];
    remove_piece(p, from);
    if (p.board[to]) remove_piece(p, to);
    add_piece(p, to, piece);
}

void append_promotions(std::vector<Move>& moves, Square from, Square to, bool capture) {
    // Pascal order in AddPawnPseudoPromos: Q, N, R, B.
    moves.push_back(encode_move(from, to, capture, Queen));
    moves.push_back(encode_move(from, to, capture, Knight));
    moves.push_back(encode_move(from, to, capture, Rook));
    moves.push_back(encode_move(from, to, capture, Bishop));
}

bool can_castle(const Position& p, Color side, bool king_side) {
    const Color enemy = static_cast<Color>(side ^ 1);
    const Square king_from = side == White ? 4 : 60;
    const Square king_mid = king_from + (king_side ? 1 : -1);
    const Square king_to = king_from + (king_side ? 2 : -2);
    const Square rook_from = side == White
        ? (king_side ? 7 : 0) : (king_side ? 63 : 56);
    const std::uint8_t right = side == White
        ? (king_side ? kWhiteKingSide : kWhiteQueenSide)
        : (king_side ? kBlackKingSide : kBlackQueenSide);
    if (!(p.castling_rights & right)) return false;
    if (p.board[king_from] != signed_piece(side, King) ||
        p.board[rook_from] != signed_piece(side, Rook)) return false;
    if (king_side) {
        if (p.board[king_from + 1] || p.board[king_from + 2]) return false;
    } else {
        if (p.board[king_from - 1] || p.board[king_from - 2] ||
            p.board[king_from - 3]) return false;
    }
    return !square_attacked(p, king_from, enemy) &&
           !square_attacked(p, king_mid, enemy) &&
           !square_attacked(p, king_to, enemy);
}

void update_castling_rights(Position& p, Square from, Square to) {
    // This is the same corner/king-square invalidation encoded by
    // uBoard.pas CastleRightsSq, expressed as explicit masks.
    if (from == 4 || to == 4) p.castling_rights &= static_cast<std::uint8_t>(~(kWhiteKingSide | kWhiteQueenSide));
    if (from == 7 || to == 7) p.castling_rights &= static_cast<std::uint8_t>(~kWhiteKingSide);
    if (from == 0 || to == 0) p.castling_rights &= static_cast<std::uint8_t>(~kWhiteQueenSide);
    if (from == 60 || to == 60) p.castling_rights &= static_cast<std::uint8_t>(~(kBlackKingSide | kBlackQueenSide));
    if (from == 63 || to == 63) p.castling_rights &= static_cast<std::uint8_t>(~kBlackKingSide);
    if (from == 56 || to == 56) p.castling_rights &= static_cast<std::uint8_t>(~kBlackQueenSide);
}

int non_pawn_material_value(Piece type) noexcept {
    switch (type) {
        case Knight: return 370;
        case Bishop: return 380;
        case Rook: return 560;
        case Queen: return 1150;
        default: return 0;
    }
}

void update_hashes_after_move(Position& p, const Position& old,
                              Color side, Piece moving_type, Piece placed_type,
                              Square from, Square to, int captured,
                              Square capture_square, bool is_promotion,
                              bool is_castle, Square rook_from, Square rook_to) {
    const Color enemy = static_cast<Color>(side ^ 1);
    p.key = old.key ^ zobrist_side() ^
            zobrist_castling(old.castling_rights) ^ zobrist_castling(p.castling_rights);
    if (old.en_passant != kNoSquare) p.key ^= zobrist_en_passant(old.en_passant);
    if (p.en_passant != kNoSquare) p.key ^= zobrist_en_passant(p.en_passant);
    p.key ^= zobrist_piece(side, moving_type, from);
    p.key ^= zobrist_piece(side, placed_type, to);

    p.pawn_key = old.pawn_key;
    p.non_pawn_key = old.non_pawn_key;
    p.non_pawn_material = old.non_pawn_material;

    if (moving_type == Pawn) {
        p.pawn_key ^= zobrist_piece(side, Pawn, from);
        if (!is_promotion) p.pawn_key ^= zobrist_piece(side, Pawn, to);
    } else {
        p.non_pawn_key[side] ^= zobrist_piece(side, moving_type, from) ^
                                zobrist_piece(side, placed_type, to);
    }
    if (is_promotion) {
        // The pawn leaves the pawn hash; the promoted piece enters NonPawnKey.
        p.non_pawn_key[side] ^= zobrist_piece(side, placed_type, to);
        p.non_pawn_material[side] += non_pawn_material_value(placed_type);
    }

    if (captured) {
        const Piece captured_type = piece_type(captured);
        p.key ^= zobrist_piece(enemy, captured_type, capture_square);
        if (captured_type == Pawn)
            p.pawn_key ^= zobrist_piece(enemy, Pawn, capture_square);
        else {
            p.non_pawn_key[enemy] ^= zobrist_piece(enemy, captured_type, capture_square);
            p.non_pawn_material[enemy] -= non_pawn_material_value(captured_type);
        }
    }

    if (is_castle) {
        p.key ^= zobrist_piece(side, Rook, rook_from) ^
                 zobrist_piece(side, Rook, rook_to);
        p.non_pawn_key[side] ^= zobrist_piece(side, Rook, rook_from) ^
                                zobrist_piece(side, Rook, rook_to);
    }
}

bool parse_nonnegative_int(const std::string& token, int& value) {
    if (token.empty()) return false;
    int parsed = 0;
    const char* begin = token.data();
    const char* end = begin + token.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed < 0) return false;
    value = parsed;
    return true;
}

} // namespace

std::string square_to_string(Square square) {
    if (square < 0 || square >= 64) return "-";
    std::string result(2, ' ');
    result[0] = static_cast<char>('a' + file_of(square));
    result[1] = static_cast<char>('1' + rank_of(square));
    return result;
}

Square square_from_string(const std::string& text) noexcept {
    if (text.size() != 2 || text[0] < 'a' || text[0] > 'h' ||
        text[1] < '1' || text[1] > '8') return kNoSquare;
    return make_square(text[0] - 'a', text[1] - '1');
}

std::string move_to_uci(Move move) {
    std::string result = square_to_string(move_from(move)) + square_to_string(move_to(move));
    if (move_is_promotion(move)) {
        switch (move_promotion(move)) {
            case Queen: result += 'q'; break;
            case Rook: result += 'r'; break;
            case Bishop: result += 'b'; break;
            case Knight: result += 'n'; break;
            default: break;
        }
    }
    return result;
}

bool parse_fen(const std::string& fen, Position& out, std::string& error) {
    std::istringstream input(fen);
    std::string placement, active, castling, ep, halfmove, fullmove, extra;
    if (!(input >> placement >> active >> castling >> ep >> halfmove >> fullmove) || (input >> extra)) {
        error = "FEN must contain exactly six fields";
        return false;
    }

    Position candidate{};
    int rank = 7;
    int file = 0;
    for (char c : placement) {
        if (c == '/') {
            if (file != 8 || rank == 0) { error = "invalid FEN rank separator or rank width"; return false; }
            --rank;
            file = 0;
            continue;
        }
        if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) { error = "FEN rank contains more than eight squares"; return false; }
            continue;
        }
        const int piece = char_to_piece(c);
        if (!piece || file >= 8 || rank < 0) { error = "invalid piece placement in FEN"; return false; }
        const Square square = rank * 8 + file;
        if (square < 0 || square >= 64) { error = "piece is outside the FEN board"; return false; }
        add_piece(candidate, square, piece);
        ++file;
    }
    if (rank != 0 || file != 8) { error = "FEN piece placement does not describe eight complete ranks"; return false; }

    if (active == "w") candidate.side_to_move = White;
    else if (active == "b") candidate.side_to_move = Black;
    else { error = "invalid FEN active-color field"; return false; }

    if (castling != "-") {
        for (char c : castling) {
            std::uint8_t bit = 0;
            switch (c) {
                case 'K': bit = kWhiteKingSide; break;
                case 'Q': bit = kWhiteQueenSide; break;
                case 'k': bit = kBlackKingSide; break;
                case 'q': bit = kBlackQueenSide; break;
                default: error = "invalid FEN castling-rights field"; return false;
            }
            if (candidate.castling_rights & bit) { error = "duplicate FEN castling right"; return false; }
            candidate.castling_rights |= bit;
        }
    }

    if (ep == "-") candidate.en_passant = kNoSquare;
    else {
        candidate.en_passant = square_from_string(ep);
        if (candidate.en_passant == kNoSquare) { error = "invalid FEN en-passant square"; return false; }
        const int required_rank = candidate.side_to_move == White ? 5 : 2;
        if (rank_of(candidate.en_passant) != required_rank || candidate.board[candidate.en_passant]) {
            error = "FEN en-passant target has an invalid rank or is occupied";
            return false;
        }
    }

    if (!parse_nonnegative_int(halfmove, candidate.halfmove_clock) ||
        !parse_nonnegative_int(fullmove, candidate.fullmove_number) ||
        candidate.fullmove_number < 1) {
        error = "invalid FEN halfmove/fullmove counters";
        return false;
    }
    candidate.null_count = candidate.halfmove_clock;
    recompute_hashes(candidate);
    if (popcount(candidate.pieces[King] & candidate.occupancy[White]) != 1 ||
        popcount(candidate.pieces[King] & candidate.occupancy[Black]) != 1) {
        error = "FEN must contain exactly one king of each color";
        return false;
    }

    std::string consistency_error;
    if (!position_is_consistent(candidate, &consistency_error)) {
        error = "inconsistent FEN position: " + consistency_error;
        return false;
    }
    out = candidate;
    error.clear();
    return true;
}

std::string to_fen(const Position& p) {
    std::string result;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const int piece = p.board[make_square(file, rank)];
            if (!piece) { ++empty; continue; }
            if (empty) { result += static_cast<char>('0' + empty); empty = 0; }
            char c = piece_to_char(piece);
            if (piece > 0) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            result += c;
        }
        if (empty) result += static_cast<char>('0' + empty);
        if (rank) result += '/';
    }
    result += p.side_to_move == White ? " w " : " b ";
    if (!p.castling_rights) result += '-';
    else {
        if (p.castling_rights & kWhiteKingSide) result += 'K';
        if (p.castling_rights & kWhiteQueenSide) result += 'Q';
        if (p.castling_rights & kBlackKingSide) result += 'k';
        if (p.castling_rights & kBlackQueenSide) result += 'q';
    }
    result += ' ';
    result += p.en_passant == kNoSquare ? "-" : square_to_string(p.en_passant);
    result += ' ' + std::to_string(p.halfmove_clock) + ' ' + std::to_string(p.fullmove_number);
    return result;
}

AttackPosition attack_position(const Position& p) noexcept {
    AttackPosition result;
    result.pieces = p.pieces;
    result.occupancy = p.occupancy;
    result.king_square = p.king_square;
    result.all = p.all;
    return result;
}

Bitboard attackers_to(const Position& p, Square square, Color by_color) {
    const AttackPosition ap = attack_position(p);
    return booot::attackers_to(square, by_color, p.all, ap);
}

bool square_attacked(const Position& p, Square square, Color by_color) {
    return attackers_to(p, square, by_color) != 0;
}

bool in_check(const Position& p, Color color) {
    const Square king = p.king_square[color];
    return king != kNoSquare && square_attacked(p, king, static_cast<Color>(color ^ 1));
}

void generate_pseudo_legal_moves(const Position& p, std::vector<Move>& moves) {
    moves.clear();
    moves.reserve(128);
    const Color side = static_cast<Color>(p.side_to_move);
    const Color enemy = static_cast<Color>(side ^ 1);
    const Bitboard own = p.occupancy[side];
    const Bitboard enemy_occ = p.occupancy[enemy];
    const Bitboard enemy_king = p.pieces[King] & enemy_occ;

    for (Square from = 0; from < 64; ++from) {
        const int piece_code = p.board[from];
        if (!piece_code || piece_color(piece_code) != side) continue;
        const Piece type = piece_type(piece_code);
        if (type == Pawn) {
            const int step = side == White ? 8 : -8;
            const int start_rank = side == White ? 1 : 6;
            const int promotion_rank = side == White ? 7 : 0;
            const Square one = from + step;
            if (one >= 0 && one < 64 && !p.board[one]) {
                if (rank_of(one) == promotion_rank) append_promotions(moves, from, one, false);
                else {
                    moves.push_back(encode_move(from, one));
                    const Square two = from + 2 * step;
                    if (rank_of(from) == start_rank && !p.board[two])
                        moves.push_back(encode_move(from, two));
                }
            }
            Bitboard captures = pawn_attacks(side, from) & enemy_occ & ~enemy_king;
            while (captures) {
                const Square to = pop_lsb(captures);
                if (rank_of(to) == promotion_rank) append_promotions(moves, from, to, true);
                else moves.push_back(encode_move(from, to, true));
            }
            if (p.en_passant != kNoSquare &&
                (pawn_attacks(side, from) & square_bb(p.en_passant))) {
                moves.push_back(encode_move(from, p.en_passant, true));
            }
            continue;
        }

        Bitboard targets = piece_attacks(side, type, from, p.all) & ~own & ~enemy_king;
        while (targets) {
            const Square to = pop_lsb(targets);
            moves.push_back(encode_move(from, to, (enemy_occ & square_bb(to)) != 0));
        }

        if (type == King) {
            if (can_castle(p, side, true))
                moves.push_back(encode_move(from, from + 2));
            if (can_castle(p, side, false))
                moves.push_back(encode_move(from, from - 2));
        }
    }
}

bool make_move(Position& p, Move move, Undo& undo, std::string* error) {
    const Square from = move_from(move);
    const Square to = move_to(move);
    auto fail = [&](const char* message) { if (error) *error = message; return false; };
    if (from < 0 || from >= 64 || to < 0 || to >= 64 || from == to) return fail("move has invalid squares");
    const int moving = p.board[from];
    if (!moving || piece_color(moving) != p.side_to_move) return fail("move origin does not contain a side-to-move piece");
    if (p.board[to] && piece_color(p.board[to]) == p.side_to_move) return fail("move destination contains a friendly piece");
    if (piece_type(p.board[to]) == King) return fail("king capture is not a legal chess move");
    const Piece moving_type = piece_type(moving);
    const bool is_pawn = moving_type == Pawn;
    const bool is_ep = is_pawn && move_is_capture(move) && to == p.en_passant && !p.board[to];
    const Square capture_square = is_ep ? to + (p.side_to_move == White ? -8 : 8) : to;
    const int captured = p.board[capture_square];
    if (move_is_capture(move) != (captured != 0 || is_ep)) return fail("capture flag does not match board occupancy");
    if (is_ep && (capture_square < 0 || capture_square >= 64 ||
                  p.board[capture_square] != signed_piece(static_cast<Color>(p.side_to_move ^ 1), Pawn)))
        return fail("invalid en-passant capture");
    if (move_is_promotion(move)) {
        const Piece promo = move_promotion(move);
        const int last_rank = p.side_to_move == White ? 7 : 0;
        if (!is_pawn || rank_of(to) != last_rank ||
            (promo != Knight && promo != Bishop && promo != Rook && promo != Queen))
            return fail("invalid promotion move");
    } else if (is_pawn && (rank_of(to) == 0 || rank_of(to) == 7)) {
        return fail("pawn reaching last rank must promote");
    }

    undo.previous = p;
    const Color side = static_cast<Color>(p.side_to_move);
    const bool castle = moving_type == King && (to - from == 2 || from - to == 2);
    Square rook_from = kNoSquare;
    Square rook_to = kNoSquare;
    if (castle) {
        if (to > from) { rook_from = from + 3; rook_to = from + 1; }
        else { rook_from = from - 4; rook_to = from - 1; }
    }
    const Piece placed_type = move_is_promotion(move) ? move_promotion(move) : moving_type;
    p.en_passant = kNoSquare;
    update_castling_rights(p, from, to);

    if (captured) remove_piece(p, capture_square);
    remove_piece(p, from);
    add_piece(p, to, signed_piece(side, placed_type));

    if (castle) {
        if (p.board[rook_from] != signed_piece(side, Rook)) {
            p = undo.previous;
            return fail("castling move has no rook on its original square");
        }
        move_piece(p, rook_from, rook_to);
    }

    if (is_pawn && !move_is_promotion(move) && (to - from == 16 || from - to == 16))
        p.en_passant = from + (side == White ? 8 : -8);

    p.halfmove_clock = (is_pawn || captured || is_ep) ? 0 : p.halfmove_clock + 1;
    if (side == Black) ++p.fullmove_number;
    p.last_captured_piece = captured ? captured : (is_ep ? signed_piece(static_cast<Color>(side ^ 1), Pawn) : Empty);
    p.null_count = undo.previous.null_count + 1;
    p.side_to_move ^= 1;
    update_hashes_after_move(p, undo.previous, side, moving_type, placed_type,
                             from, to, captured, capture_square,
                             move_is_promotion(move), castle, rook_from, rook_to);
    if (error) error->clear();
    return true;
}

void unmake_move(Position& p, const Undo& undo) noexcept {
    p = undo.previous;
}

void make_null_move(Position& p, Undo& undo) noexcept {
    undo.previous = p;
    if (p.en_passant != kNoSquare) {
        p.key ^= zobrist_en_passant(p.en_passant);
        p.en_passant = kNoSquare;
    }
    p.key ^= zobrist_side();
    ++p.halfmove_clock;
    p.null_count = 0;
    p.side_to_move ^= 1;
}

void unmake_null_move(Position& p, const Undo& undo) noexcept {
    p = undo.previous;
}

void generate_legal_moves(const Position& p, std::vector<Move>& moves) {
    std::vector<Move> pseudo;
    generate_pseudo_legal_moves(p, pseudo);
    moves.clear();
    moves.reserve(pseudo.size());
    for (Move move : pseudo) {
        Position child = p;
        Undo undo;
        if (!make_move(child, move, undo)) continue;
        const Color mover = static_cast<Color>(p.side_to_move);
        if (!in_check(child, mover)) moves.push_back(move);
    }
}

bool parse_uci_move(const Position& p, const std::string& uci,
                    Move& move, std::string& error) {
    if (uci.size() != 4 && uci.size() != 5) { error = "UCI move must be 4 or 5 characters"; return false; }
    std::vector<Move> legal;
    generate_legal_moves(p, legal);
    for (Move candidate : legal) {
        if (move_to_uci(candidate) == uci) {
            move = candidate;
            error.clear();
            return true;
        }
    }
    error = "move is not legal in the current position: " + uci;
    return false;
}

std::uint64_t perft(Position& p, int depth) {
    if (depth <= 0) return 1;
    std::vector<Move> moves;
    generate_legal_moves(p, moves);
    if (depth == 1) return moves.size();
    std::uint64_t nodes = 0;
    for (Move move : moves) {
        Undo undo;
        if (!make_move(p, move, undo)) continue;
        nodes += perft(p, depth - 1);
        unmake_move(p, undo);
    }
    return nodes;
}

std::vector<std::pair<Move, std::uint64_t>> perft_divide(Position& p, int depth) {
    std::vector<std::pair<Move, std::uint64_t>> result;
    if (depth <= 0) return result;
    std::vector<Move> moves;
    generate_legal_moves(p, moves);
    result.reserve(moves.size());
    for (Move move : moves) {
        Undo undo;
        if (!make_move(p, move, undo)) continue;
        const std::uint64_t nodes = depth == 1 ? 1 : perft(p, depth - 1);
        unmake_move(p, undo);
        result.emplace_back(move, nodes);
    }
    return result;
}

bool position_is_consistent(const Position& p, std::string* error) {
    auto fail = [&](const char* message) { if (error) *error = message; return false; };
    if (p.side_to_move != White && p.side_to_move != Black) return fail("invalid side-to-move value");
    if (p.castling_rights & 0xF0) return fail("unknown castling-rights bits");
    if (p.en_passant != kNoSquare && (p.en_passant < 0 || p.en_passant >= 64)) return fail("invalid en-passant square");
    std::array<Bitboard, 7> pieces{};
    std::array<Bitboard, 2> occupancy{};
    std::array<Square, 2> kings{{kNoSquare, kNoSquare}};
    for (Square sq = 0; sq < 64; ++sq) {
        const int piece = p.board[sq];
        if (!piece) continue;
        const int type_value = piece < 0 ? -piece : piece;
        if (type_value < Pawn || type_value > King) return fail("mailbox contains an invalid piece code");
        const Piece type = static_cast<Piece>(type_value);
        const Color color = static_cast<Color>(piece_color(piece));
        const Bitboard bit = square_bb(sq);
        pieces[type] |= bit;
        occupancy[color] |= bit;
        if (type == King) {
            if (kings[color] != kNoSquare) return fail("more than one king of a color");
            kings[color] = sq;
        }
    }
    if (pieces != p.pieces) return fail("piece-type bitboards do not match mailbox");
    if (occupancy != p.occupancy) return fail("color occupancy does not match mailbox");
    if ((occupancy[White] | occupancy[Black]) != p.all) return fail("all-occupancy bitboard does not match mailbox");
    if (kings != p.king_square) return fail("king-square cache does not match mailbox");
    if (!hash_values_match(p)) return fail("Zobrist/material caches do not match mailbox");
    if (error) error->clear();
    return true;
}

} // namespace booot

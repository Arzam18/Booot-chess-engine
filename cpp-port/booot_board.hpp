#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "booot_attacks.hpp"

namespace booot {

// Booot-compatible move encoding, retained verbatim from uBoard.pas:
// bits 0..5 = from, 6..11 = to, 12..14 = promotion piece, bit 15 = capture.
using Move = std::uint16_t;
inline constexpr Move kCaptureFlag = Move{1} << 15;
inline constexpr Move kPromotionMask = (Move{1} << 12) | (Move{1} << 13) | (Move{1} << 14);
inline constexpr int kNoPiece = 0;
inline constexpr std::uint8_t kWhiteKingSide  = 1;
inline constexpr std::uint8_t kWhiteQueenSide = 2;
inline constexpr std::uint8_t kBlackKingSide  = 4;
inline constexpr std::uint8_t kBlackQueenSide = 8;

constexpr Move encode_move(Square from, Square to, bool capture = false,
                           Piece promotion = Empty) noexcept {
    return static_cast<Move>((from & 63) | ((to & 63) << 6) |
        (capture ? kCaptureFlag : 0) |
        ((promotion != Empty ? static_cast<unsigned>(promotion) : 0U) << 12));
}
constexpr Square move_from(Move move) noexcept { return move & 63; }
constexpr Square move_to(Move move) noexcept { return (move >> 6) & 63; }
constexpr Piece move_promotion(Move move) noexcept {
    return static_cast<Piece>((move >> 12) & 7);
}
constexpr bool move_is_capture(Move move) noexcept { return (move & kCaptureFlag) != 0; }
constexpr bool move_is_promotion(Move move) noexcept { return (move & kPromotionMask) != 0; }

// Fixed-capacity move buffer matching the original engine's MaxMoves=256.
// Search hot paths use this to avoid one heap allocation per generated list.
struct MoveList {
    static constexpr std::size_t kCapacity = 256;
    std::array<Move, kCapacity> data{};
    std::size_t count = 0;

    void clear() noexcept { count = 0; }
    void reserve(std::size_t) noexcept {}
    void push_back(Move move) noexcept {
        if (count < kCapacity) data[count++] = move;
    }
    std::size_t size() const noexcept { return count; }
    bool empty() const noexcept { return count == 0; }
    Move& operator[](std::size_t index) noexcept { return data[index]; }
    const Move& operator[](std::size_t index) const noexcept { return data[index]; }
    Move* begin() noexcept { return data.data(); }
    Move* end() noexcept { return data.data() + count; }
    const Move* begin() const noexcept { return data.data(); }
    const Move* end() const noexcept { return data.data() + count; }
};

struct Position {
    // Signed mailbox matches TBoard.Pos: white pieces positive, black negative.
    std::array<std::int8_t, 64> board{};
    // Piece-type bitboards combine both colors, as in TBoard.Pieses[Pawn..Queen].
    std::array<Bitboard, 7> pieces{};
    std::array<Bitboard, 2> occupancy{};
    std::array<Square, 2> king_square{{kNoSquare, kNoSquare}};
    Bitboard all = 0;
    int side_to_move = White;
    Square en_passant = kNoSquare;
    std::uint8_t castling_rights = 0;
    int halfmove_clock = 0;
    int fullmove_number = 1;
    int last_captured_piece = Empty;
    // These fields mirror TBoard's Zobrist/material caches from uBoard/uHash.
    std::uint64_t key = 0;
    std::uint64_t pawn_key = 0;
    std::array<std::uint64_t, 2> non_pawn_key{};
    std::array<int, 2> non_pawn_material{};
    int null_count = 0;
};

// Compact reversible move state. The original Pascal engine stores only the
// metadata and captured/moving pieces needed to reverse a move; copying the
// entire Position at every search node was a major avoidable cost.
struct Undo {
    Square from_square = kNoSquare;
    Square to_square = kNoSquare;
    int moving_piece = Empty;
    int captured_piece = Empty;
    Square capture_square = kNoSquare;
    Piece placed_type = Empty;
    int side_to_move = White;
    Square en_passant = kNoSquare;
    std::uint8_t castling_rights = 0;
    int halfmove_clock = 0;
    int fullmove_number = 1;
    int last_captured_piece = Empty;
    int null_count = 0;
    std::array<Square, 2> king_square{{kNoSquare, kNoSquare}};
    std::uint64_t key = 0;
    std::uint64_t pawn_key = 0;
    std::array<std::uint64_t, 2> non_pawn_key{};
    std::array<int, 2> non_pawn_material{};
    bool is_castle = false;
    Square rook_from = kNoSquare;
    Square rook_to = kNoSquare;
};

struct PerftResult {
    std::uint64_t nodes = 0;
};

bool parse_fen(const std::string& fen, Position& out, std::string& error);
std::string to_fen(const Position& position);
std::string square_to_string(Square square);
Square square_from_string(const std::string& text) noexcept;
std::string move_to_uci(Move move);

AttackPosition attack_position(const Position& position) noexcept;
Bitboard attackers_to(const Position& position, Square square, Color by_color);
bool square_attacked(const Position& position, Square square, Color by_color);
bool in_check(const Position& position, Color color);

void generate_pseudo_legal_moves(const Position& position, std::vector<Move>& moves);
void generate_pseudo_legal_moves(const Position& position, MoveList& moves);
void generate_legal_moves(const Position& position, std::vector<Move>& moves);
void generate_legal_moves(const Position& position, MoveList& moves);
bool make_move(Position& position, Move move, Undo& undo, std::string* error = nullptr);
void unmake_move(Position& position, const Undo& undo) noexcept;
void make_null_move(Position& position, Undo& undo) noexcept;
void unmake_null_move(Position& position, const Undo& undo) noexcept;
bool parse_uci_move(const Position& position, const std::string& uci,
                    Move& move, std::string& error);
std::uint64_t perft(Position& position, int depth);
std::vector<std::pair<Move, std::uint64_t>> perft_divide(Position& position, int depth);
bool position_is_consistent(const Position& position, std::string* error = nullptr);

} // namespace booot

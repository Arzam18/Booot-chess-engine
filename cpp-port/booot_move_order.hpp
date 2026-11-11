#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "booot_board.hpp"

namespace booot {

inline constexpr int kMaxKillersPly = 127;
inline constexpr int kHistoryDiv = 16384;
inline constexpr int kStatsDiv = 32768;
inline constexpr int kThreatValue = 8192;
inline constexpr int kMaxPlyForHistory = kMaxKillersPly + 2;

struct PreviousMoveContext {
    int piece = Empty; // signed piece code on the previous move's destination
    Square to = kNoSquare;
    bool valid = false;
    std::uint8_t channel = 0; // selects one of Pascal HistorySats' four tables
};

struct MoveHistoryContext {
    PreviousMoveContext previous1{};
    PreviousMoveContext previous2{};
    PreviousMoveContext previous4{};
};

// Booot uSort's history/countermove/killer/capture-history storage. The four
// continuation tables mirror HistorySats[false..true,false..true,...].
class MoveHistory {
public:
    MoveHistory();
    void clear();

    int quiet_score(const Position& position, Move move, int ply,
                    const MoveHistoryContext& context) const;
    int escape_score(const Position& position, Move move, int ply,
                     const MoveHistoryContext& context) const;
    int capture_score(const Position& position, Move move) const;

    void update_quiet(const Position& position, Move move, int bonus, int ply,
                      const MoveHistoryContext& context);
    void update_capture(const Position& position, Move move, int bonus);
    void update_killer_and_counter(const Position& position, Move move,
                                   int ply, Move previous_move);

    Move killer(int ply, int slot) const noexcept;
    Move counter_move(int signed_piece, Square destination) const noexcept;
    void set_quiet_history(int signed_piece, Square destination, int value);
    void set_capture_history(int signed_piece, Square destination,
                             Piece captured, int value);
    void set_continuation(std::uint8_t channel, int previous_piece,
                          Square previous_to, int piece, Square destination,
                          int value);
    int quiet_history(int signed_piece, Square destination) const noexcept;
    int continuation_score(const PreviousMoveContext& previous,
                           int signed_piece, Square destination) const noexcept;
    static int stat_bonus(int depth) noexcept;

private:
    static constexpr int kPieceSlots = 13; // Pascal [-King..King]
    static constexpr int kSquares = 64;
    static constexpr int kPieceTypes = 7;
    static int piece_index(int signed_piece) noexcept;
    static int clamp_ply(int ply) noexcept;
    static std::size_t continuation_index(std::uint8_t channel,
        int previous_piece, Square previous_to, int piece, Square destination) noexcept;
    int continuation_value(const PreviousMoveContext& previous,
                           int piece, Square destination) const noexcept;
    static void gravity(std::int16_t& cell, int value, int range) noexcept;

    std::array<std::array<std::int16_t, kSquares>, kPieceSlots> history_{};
    std::array<std::array<Move, kSquares>, kPieceSlots> counter_moves_{};
    std::array<std::array<Move, 2>, kMaxPlyForHistory + 1> killers_{};
    std::array<std::array<std::array<std::int16_t, kPieceTypes>, kSquares>, kPieceSlots> capture_history_{};
    std::vector<std::int16_t> continuation_;
};

// A staged Booot-style move picker. It follows uSort.Next's phase order:
// hash, good captures/promotions, killers, countermove, quiets, bad captures.
// When the side to move is in check it returns legal evasions, captures first.
class MovePicker {
public:
    MovePicker(const Position& position, const MoveHistory& history,
               int ply, const MoveHistoryContext& context = {},
               Move hash_move = 0, Move killer1 = 0, Move killer2 = 0,
               Move counter_move = 0, bool skip_quiets = false);
    std::optional<Move> next();

private:
    struct ScoredMove {
        int score = 0;
        Move move = 0;
        // Pack the 2-bit phase above a 14-bit stable insertion order. This
        // keeps each entry 8 bytes and limits recursive search stack use.
        std::uint16_t order_phase = 0;
    };
    void add_scored(Move move, int score, std::uint8_t phase) noexcept;
    std::optional<Move> take_phase(std::uint8_t phase) noexcept;
    bool is_duplicate_special(Move move) const noexcept;

    const Position& position_;
    const MoveHistory& history_;
    int ply_;
    MoveHistoryContext context_;
    Move hash_move_;
    Move killer1_;
    Move killer2_;
    Move counter_move_;
    bool skip_quiets_;
    bool in_check_;
    int phase_ = 0;
    bool hash_pending_ = true;
    bool killer1_pending_ = true;
    bool killer2_pending_ = true;
    bool counter_pending_ = true;
    // One fixed 256-entry pool is partitioned by phase and sorted in-place.
    // This replaces four per-node vectors and their heap allocations.
    std::array<ScoredMove, MoveList::kCapacity> scored_{};
    std::size_t scored_count_ = 0;
    std::size_t phase_cursor_ = 0;
    std::uint16_t insertion_order_ = 0;
};

} // namespace booot

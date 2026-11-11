#include "booot_move_order.hpp"

#include "booot_attacks.hpp"
#include "booot_tactics.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace booot {
namespace {
constexpr std::array<int, 7> kMvv{{0, 100, 300, 300, 500, 900, 0}};
constexpr int kCapturePriority = 10'000'000;
constexpr std::size_t kContinuationCells = 4U * 13U * 64U * 13U * 64U;

int captured_type(const Position& p, Move move) noexcept {
    const Square to = move_to(move);
    if (p.board[to] != Empty) return std::abs(static_cast<int>(p.board[to]));
    if (move_is_capture(move) && std::abs(static_cast<int>(p.board[move_from(move)])) == Pawn &&
        to == p.en_passant)
        return Pawn;
    return Empty;
}

struct ThreatMasks {
    Bitboard pawn = 0;
    Bitboard minor = 0;
    Bitboard rook = 0;
};

ThreatMasks build_enemy_threats(const Position& p) {
    ThreatMasks out;
    const Color enemy = static_cast<Color>(p.side_to_move ^ 1);
    Bitboard men = p.pieces[Pawn] & p.occupancy[enemy];
    while (men) out.pawn |= pawn_attacks(enemy, pop_lsb(men));
    out.minor = out.pawn;
    men = p.pieces[Knight] & p.occupancy[enemy];
    while (men) out.minor |= knight_attacks(pop_lsb(men));
    men = p.pieces[Bishop] & p.occupancy[enemy];
    while (men) out.minor |= bishop_attacks(pop_lsb(men), p.all);
    out.rook = out.minor;
    men = p.pieces[Rook] & p.occupancy[enemy];
    while (men) out.rook |= rook_attacks(pop_lsb(men), p.all);
    return out;
}

int quiet_threat_adjustment(const Position& p, Move move,
                            const ThreatMasks& threats) noexcept {
    const int signed_piece = p.board[move_from(move)];
    const Piece type = static_cast<Piece>(std::abs(signed_piece));
    Bitboard relevant = 0;
    if (type == Knight || type == Bishop) relevant = threats.pawn;
    else if (type == Rook) relevant = threats.minor;
    else if (type == Queen) relevant = threats.rook;
    else return 0;
    int score = 0;
    if (relevant & square_bb(move_from(move))) score += kThreatValue;
    if (relevant & square_bb(move_to(move))) score -= kThreatValue;
    return score;
}

bool is_capture_or_promotion(Move move) noexcept {
    return move_is_capture(move) || move_is_promotion(move);
}

} // namespace

MoveHistory::MoveHistory() : continuation_(kContinuationCells, 0) { clear(); }

int MoveHistory::piece_index(int signed_piece) noexcept {
    return std::clamp(signed_piece, -static_cast<int>(King), static_cast<int>(King)) + static_cast<int>(King);
}

int MoveHistory::clamp_ply(int ply) noexcept {
    return std::clamp(ply, 0, kMaxPlyForHistory);
}

std::size_t MoveHistory::continuation_index(std::uint8_t channel,
        int previous_piece, Square previous_to, int piece, Square destination) noexcept {
    const std::size_t c = std::min<std::size_t>(channel, 3);
    const std::size_t pp = static_cast<std::size_t>(piece_index(previous_piece));
    const std::size_t pt = static_cast<std::size_t>(std::clamp(previous_to, 0, 63));
    const std::size_t cp = static_cast<std::size_t>(piece_index(piece));
    const std::size_t ct = static_cast<std::size_t>(std::clamp(destination, 0, 63));
    return ((((c * 13U + pp) * 64U + pt) * 13U + cp) * 64U + ct);
}

void MoveHistory::clear() {
    for (auto& row : history_) row.fill(0);
    for (auto& row : counter_moves_) row.fill(0);
    for (auto& row : killers_) row.fill(0);
    for (auto& piece : capture_history_)
        for (auto& square : piece) square.fill(0);
    std::fill(continuation_.begin(), continuation_.end(), 0);
}

void MoveHistory::gravity(std::int16_t& cell, int value, int range) noexcept {
    // Exact integer update from uSort.CellGravity; division truncates toward zero.
    const int current = cell;
    const int next = current + value - ((current * std::abs(value)) / range);
    cell = static_cast<std::int16_t>(std::clamp(next,
        static_cast<int>(std::numeric_limits<std::int16_t>::min()),
        static_cast<int>(std::numeric_limits<std::int16_t>::max())));
}

int MoveHistory::continuation_value(const PreviousMoveContext& previous,
                                    int piece, Square destination) const noexcept {
    if (!previous.valid || previous.to < 0 || previous.to >= 64 ||
        previous.piece < -King || previous.piece > King)
        return 0;
    return continuation_[continuation_index(previous.channel, previous.piece,
                                               previous.to, piece, destination)];
}

int MoveHistory::quiet_score(const Position& position, Move move, int,
                             const MoveHistoryContext& context) const {
    const int piece = position.board[move_from(move)];
    const Square to = move_to(move);
    return quiet_history(piece, to) +
        2 * continuation_value(context.previous1, piece, to) +
        continuation_value(context.previous2, piece, to) +
        continuation_value(context.previous4, piece, to);
}

int MoveHistory::escape_score(const Position& position, Move move, int,
                              const MoveHistoryContext& context) const {
    const int piece = position.board[move_from(move)];
    const Square to = move_to(move);
    return quiet_history(piece, to) +
        2 * continuation_value(context.previous1, piece, to);
}

int MoveHistory::capture_score(const Position& position, Move move) const {
    const int piece = position.board[move_from(move)];
    const int captured = captured_type(position, move);
    if (captured < Empty || captured > King) return 0;
    const int base = kMvv[static_cast<std::size_t>(captured)];
    return base + capture_history_[piece_index(piece)][move_to(move)][captured] / 16;
}

void MoveHistory::update_quiet(const Position& position, Move move, int bonus,
                               int, const MoveHistoryContext& context) {
    const int piece = position.board[move_from(move)];
    const Square to = move_to(move);
    gravity(history_[piece_index(piece)][to], bonus, kHistoryDiv);
    auto update_continuation = [&](const PreviousMoveContext& previous) {
        if (!previous.valid || previous.to < 0 || previous.to >= 64) return;
        auto& cell = continuation_[continuation_index(previous.channel,
            previous.piece, previous.to, piece, to)];
        gravity(cell, bonus, kStatsDiv);
    };
    update_continuation(context.previous1);
    update_continuation(context.previous2);
    update_continuation(context.previous4);
}

void MoveHistory::update_capture(const Position& position, Move move, int bonus) {
    const int piece = position.board[move_from(move)];
    const int captured = captured_type(position, move);
    if (captured < Empty || captured > King) return;
    gravity(capture_history_[piece_index(piece)][move_to(move)][captured],
            bonus, kHistoryDiv);
}

void MoveHistory::update_killer_and_counter(const Position& position, Move move,
                                            int ply, Move previous_move) {
    const int index = clamp_ply(ply);
    if (killers_[index][0] != move) {
        killers_[index][1] = killers_[index][0];
        killers_[index][0] = move;
    }
    if (previous_move != 0) {
        const Square previous_to = move_to(previous_move);
        const int piece = position.board[previous_to];
        if (piece >= -King && piece <= King && previous_to >= 0 && previous_to < 64)
            counter_moves_[piece_index(piece)][previous_to] = move;
    }
}

Move MoveHistory::killer(int ply, int slot) const noexcept {
    if (slot < 0 || slot > 1) return 0;
    return killers_[clamp_ply(ply)][slot];
}

Move MoveHistory::counter_move(int signed_piece, Square destination) const noexcept {
    if (destination < 0 || destination >= 64 || signed_piece < -King || signed_piece > King)
        return 0;
    return counter_moves_[piece_index(signed_piece)][destination];
}

void MoveHistory::set_quiet_history(int signed_piece, Square destination, int value) {
    if (destination >= 0 && destination < 64 && signed_piece >= -King && signed_piece <= King)
        history_[piece_index(signed_piece)][destination] = static_cast<std::int16_t>(value);
}

void MoveHistory::set_capture_history(int signed_piece, Square destination,
                                      Piece captured, int value) {
    if (destination >= 0 && destination < 64 && signed_piece >= -King &&
        signed_piece <= King && captured >= Empty && captured <= King)
        capture_history_[piece_index(signed_piece)][destination][captured] =
            static_cast<std::int16_t>(value);
}

void MoveHistory::set_continuation(std::uint8_t channel, int previous_piece,
                                   Square previous_to, int piece, Square destination,
                                   int value) {
    if (channel > 3 || previous_piece < -King || previous_piece > King ||
        piece < -King || piece > King || previous_to < 0 || previous_to >= 64 ||
        destination < 0 || destination >= 64) return;
    continuation_[continuation_index(channel, previous_piece, previous_to,
                                      piece, destination)] = static_cast<std::int16_t>(value);
}

int MoveHistory::quiet_history(int signed_piece, Square destination) const noexcept {
    if (destination < 0 || destination >= 64 || signed_piece < -King || signed_piece > King)
        return 0;
    return history_[piece_index(signed_piece)][destination];
}

int MoveHistory::continuation_score(const PreviousMoveContext& previous,
                                    int signed_piece, Square destination) const noexcept {
    if (signed_piece < -King || signed_piece > King ||
        destination < 0 || destination >= 64) return 0;
    return continuation_value(previous, signed_piece, destination);
}

int MoveHistory::stat_bonus(int depth) noexcept {
    const int d = std::clamp(depth, 0, 128);
    return std::clamp((9 * d + 270) * d - 310, 0, 2150);
}

void MovePicker::add_scored(Move move, int score, std::uint8_t phase) noexcept {
    if (scored_count_ >= scored_.size()) return;
    const std::uint16_t packed = static_cast<std::uint16_t>(
        ((phase & 0x3U) << 14U) | (insertion_order_++ & 0x3FFFU));
    scored_[scored_count_++] = {score, move, packed};
}

std::optional<Move> MovePicker::take_phase(std::uint8_t phase) noexcept {
    while (phase_cursor_ < scored_count_ &&
           (scored_[phase_cursor_].order_phase >> 14U) < phase)
        ++phase_cursor_;
    if (phase_cursor_ >= scored_count_ ||
        (scored_[phase_cursor_].order_phase >> 14U) != phase)
        return std::nullopt;
    return scored_[phase_cursor_++].move;
}

MovePicker::MovePicker(const Position& position, const MoveHistory& history,
                       int ply, const MoveHistoryContext& context,
                       Move hash_move, Move killer1, Move killer2,
                       Move counter_move, bool skip_quiets)
    : position_(position), history_(history), ply_(ply), context_(context),
      hash_move_(hash_move), killer1_(killer1), killer2_(killer2),
      counter_move_(counter_move), skip_quiets_(skip_quiets),
      in_check_(in_check(position, static_cast<Color>(position.side_to_move))) {
    if (in_check_) {
        MoveList legal;
        generate_legal_moves(position_, legal);
        for (Move move : legal) {
            const int score = move_is_capture(move)
                ? kCapturePriority + kMvv[static_cast<std::size_t>(captured_type(position_, move))]
                : history_.escape_score(position_, move, ply_, context_);
            add_scored(move, score, 0);
        }
    } else {
        MoveList pseudo;
        generate_pseudo_legal_moves(position_, pseudo);
        const ThreatMasks threats = build_enemy_threats(position_);
        for (Move move : pseudo) {
            if (is_capture_or_promotion(move)) {
                const int score = history_.capture_score(position_, move);
                add_scored(move, score, good_see(move, position_, 0) ? 0 : 2);
            } else {
                const int score = history_.quiet_score(position_, move, ply_, context_) +
                                  quiet_threat_adjustment(position_, move, threats);
                add_scored(move, score, 1);
            }
        }
    }

    // Pascal uSort keeps stable order within the winning-capture and quiet
    // groups, but reverses the losing-capture tail while draining it. Encode
    // those tie rules in the comparator so no temporary vectors are needed.
    std::sort(scored_.begin(), scored_.begin() + static_cast<std::ptrdiff_t>(scored_count_),
        [](const ScoredMove& a, const ScoredMove& b) {
            const unsigned a_phase = a.order_phase >> 14U;
            const unsigned b_phase = b.order_phase >> 14U;
            if (a_phase != b_phase) return a_phase < b_phase;
            if (a.score != b.score) {
                if (a_phase == 2) return a.score < b.score;
                return a.score > b.score;
            }
            const unsigned a_order = a.order_phase & 0x3FFFU;
            const unsigned b_order = b.order_phase & 0x3FFFU;
            if (a_phase == 2) return a_order > b_order;
            return a_order < b_order;
        });
}

bool MovePicker::is_duplicate_special(Move move) const noexcept {
    return move == hash_move_ || move == killer1_ || move == killer2_ || move == counter_move_;
}

std::optional<Move> MovePicker::next() {
    if (phase_ == 0) {
        phase_ = in_check_ ? 7 : 1;
        if (hash_pending_) {
            hash_pending_ = false;
            if (hash_move_ != 0 && is_pseudo_correct(hash_move_, position_))
                return hash_move_;
            hash_move_ = 0;
        }
    }

    if (in_check_) {
        while (auto move = take_phase(0)) {
            if (*move != hash_move_) return move;
        }
        return std::nullopt;
    }

    for (;;) {
        if (phase_ == 1) {
            while (auto move = take_phase(0))
                if (*move != hash_move_) return move;
            phase_ = 2;
        } else if (phase_ == 2) {
            phase_ = 3;
            if (killer1_pending_) {
                killer1_pending_ = false;
                if (killer1_ != 0 && killer1_ != hash_move_ &&
                    position_.board[move_to(killer1_)] == Empty &&
                    is_pseudo_correct(killer1_, position_)) return killer1_;
                killer1_ = 0;
            }
        } else if (phase_ == 3) {
            phase_ = 4;
            if (killer2_pending_) {
                killer2_pending_ = false;
                if (killer2_ != 0 && killer2_ != hash_move_ && killer2_ != killer1_ &&
                    position_.board[move_to(killer2_)] == Empty &&
                    is_pseudo_correct(killer2_, position_)) return killer2_;
                killer2_ = 0;
            }
        } else if (phase_ == 4) {
            phase_ = 5;
            if (counter_pending_) {
                counter_pending_ = false;
                if (counter_move_ != 0 && counter_move_ != hash_move_ &&
                    counter_move_ != killer1_ && counter_move_ != killer2_ &&
                    position_.board[move_to(counter_move_)] == Empty &&
                    is_pseudo_correct(counter_move_, position_)) return counter_move_;
                counter_move_ = 0;
            }
        } else if (phase_ == 5) {
            if (!skip_quiets_) {
                while (auto move = take_phase(1))
                    if (!is_duplicate_special(*move)) return move;
            }
            phase_ = 6;
        } else if (phase_ == 6) {
            phase_ = 7;
            while (auto move = take_phase(2))
                if (*move != hash_move_) return move;
        } else {
            return std::nullopt;
        }
    }
}

} // namespace booot

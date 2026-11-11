#include "booot_search.hpp"

#include "booot_tactics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace booot {
namespace {
constexpr int kRazorMargin = 225;
constexpr int kStatixMargin = 80;
constexpr int kFutilityMargin = 120;
constexpr int kProbCutDepth = 5;
constexpr int kProbCutReduction = 4;
constexpr int kProbCutMargin = 85;
constexpr int kSingularDepth = 6;
constexpr int kHistoryDepth = 3;
constexpr int kCountMoveDepth = 16;
constexpr int kIirDepthPv = 3;
constexpr int kIirDepthCut = 7;
constexpr int kIirReduction = 2;
constexpr int kHistoryLmrDivisor = 16000;
constexpr int kQMaxPly = kSearchMaxPly - 2;

} // namespace

TranspositionTable::TranspositionTable(std::size_t megabytes) {
    const std::size_t bytes = std::max<std::size_t>(1, megabytes) * 1024U * 1024U;
    const std::size_t wanted_entries = std::max<std::size_t>(4, bytes / sizeof(TTEntry));
    bucket_count_ = std::max<std::size_t>(1, wanted_entries / 4);
    entries_.resize(bucket_count_ * 4);
}

bool TranspositionTable::resize(std::size_t megabytes) noexcept {
    try {
        const std::size_t mb = std::max<std::size_t>(1, megabytes);
        if (mb > std::numeric_limits<std::size_t>::max() / (1024U * 1024U)) return false;
        const std::size_t bytes = mb * 1024U * 1024U;
        const std::size_t wanted_entries = std::max<std::size_t>(4, bytes / sizeof(TTEntry));
        const std::size_t buckets = std::max<std::size_t>(1, wanted_entries / 4);
        std::vector<TTEntry> replacement(buckets * 4);
        entries_.swap(replacement);
        bucket_count_ = buckets;
        age_ = 0;
        return true;
    } catch (...) {
        return false;
    }
}

void TranspositionTable::clear() noexcept {
    for (TTEntry& entry : entries_) entry = TTEntry{};
    age_ = 0;
}

void TranspositionTable::new_search() noexcept {
    age_ = static_cast<std::uint8_t>(age_ + 8U);
}

const TTEntry* TranspositionTable::probe(std::uint64_t key) const noexcept {
    const std::size_t base = static_cast<std::size_t>(key % bucket_count_) * 4U;
    for (std::size_t i = 0; i < 4; ++i) {
        const TTEntry& entry = entries_[base + i];
        if (entry.valid && entry.key == key) return &entry;
    }
    return nullptr;
}

void TranspositionTable::store(std::uint64_t key, int value, int depth,
                               TTBound bound, Move move, int static_eval,
                               bool pv, int ply) noexcept {
    const std::size_t base = static_cast<std::size_t>(key % bucket_count_) * 4U;
    std::size_t replacement = base;
    int worst_quality = std::numeric_limits<int>::max();
    for (std::size_t i = 0; i < 4; ++i) {
        TTEntry& entry = entries_[base + i];
        if (entry.valid && entry.key == key) { replacement = base + i; break; }
        if (!entry.valid) { replacement = base + i; break; }
        const int age_distance = static_cast<std::uint8_t>(age_ - entry.age);
        const int quality = static_cast<int>(entry.depth) - age_distance;
        if (quality < worst_quality) { worst_quality = quality; replacement = base + i; }
    }
    TTEntry& entry = entries_[replacement];
    if (entry.valid && entry.key == key) {
        if (move == 0) move = entry.move;
        const int age_distance = static_cast<std::uint8_t>(age_ - entry.age);
        const bool preserve = bound != TTBound::Exact && age_distance == 0 &&
            depth + 4 + (pv ? 2 : 0) <= entry.depth;
        if (preserve) {
            if (move != 0) entry.move = move;
            return;
        }
    } else if (entry.valid) {
        const int age_distance = static_cast<std::uint8_t>(age_ - entry.age);
        const bool preserve = bound != TTBound::Exact && age_distance == 0 &&
            depth + 4 + (pv ? 2 : 0) <= entry.depth;
        if (preserve) return;
    }
    entry.key = key;
    entry.move = move;
    entry.value = value_to_tt(value, ply, kSearchWinScore, kSearchInfinity);
    entry.static_eval = static_eval;
    entry.depth = static_cast<std::int16_t>(std::clamp(depth, -127, 127));
    entry.bound = bound;
    entry.age = age_;
    entry.pv = pv;
    entry.valid = true;
}

SearchEngine::SearchEngine(NetworkSet networks, std::size_t hash_megabytes)
    : networks_(networks), use_nnue_(true), tt_(hash_megabytes) {}

SearchEngine::SearchEngine(SearchEvalCallback diagnostic_evaluator,
                           std::size_t hash_megabytes)
    : diagnostic_evaluator_(std::move(diagnostic_evaluator)),
      use_nnue_(false), tt_(hash_megabytes) {}

bool SearchEngine::resize_hash(std::size_t megabytes) noexcept {
    return tt_.resize(megabytes);
}

bool SearchEngine::evaluate(const Position& position, const NNUEState& nnue,
                            int& score) {
    if (!use_nnue_) {
        if (!diagnostic_evaluator_) { error_ = "No diagnostic evaluator is configured"; stopped_ = true; return false; }
        score = diagnostic_evaluator_(position);
        return true;
    }
    EvaluationResult result;
    std::string error;
    if (!evaluate_position(position, networks_, nnue, kSearchWinScore, result, error)) {
        error_ = "Booot evaluation failed: " + error;
        stopped_ = true;
        return false;
    }
    score = result.score;
    return true;
}

bool SearchEngine::make_child(Position& position, NNUEState& nnue, Move move,
                              Undo& board_undo, NNUEUndo& nnue_undo) {
    if (use_nnue_) {
        std::string error;
        if (!make_move_with_nnue(position, move, networks_, nnue,
                                 board_undo, nnue_undo, error)) {
            error_ = "Booot NNUE move update failed: " + error;
            stopped_ = true;
            return false;
        }
        return true;
    }
    return make_move(position, move, board_undo);
}

void SearchEngine::unmake_child(Position& position, NNUEState& nnue,
                                const Undo& board_undo,
                                const NNUEUndo& nnue_undo) noexcept {
    if (use_nnue_) unmake_move_with_nnue(position, board_undo, nnue, nnue_undo);
    else unmake_move(position, board_undo);
}

bool SearchEngine::make_null(Position& position, NNUEState& nnue,
                             Undo& board_undo, NNUEUndo& nnue_undo) {
    if (use_nnue_) {
        std::string error;
        if (!make_null_move_with_nnue(position, networks_, nnue,
                                      board_undo, nnue_undo, error)) {
            error_ = "Booot NNUE null-move update failed: " + error;
            stopped_ = true;
            return false;
        }
        return true;
    }
    make_null_move(position, board_undo);
    return true;
}

void SearchEngine::unmake_null(Position& position, NNUEState& nnue,
                               const Undo& board_undo,
                               const NNUEUndo& nnue_undo) noexcept {
    if (use_nnue_) unmake_null_move_with_nnue(position, board_undo, nnue, nnue_undo);
    else unmake_null_move(position, board_undo);
}

bool SearchEngine::should_stop(bool force_clock_check) noexcept {
    if (stopped_) return true;
    if (limits_.stop && limits_.stop->load(std::memory_order_relaxed)) stopped_ = true;
    if (limits_.node_limit != 0 && nodes_ >= limits_.node_limit) stopped_ = true;
    if (!stopped_ && has_hard_deadline_ &&
        (force_clock_check || (nodes_ & 255U) == 0U) &&
        std::chrono::steady_clock::now() >= hard_deadline_) {
        time_limit_hit_ = true;
        stopped_ = true;
    }
    return stopped_;
}

bool SearchEngine::soft_time_expired() const noexcept {
    return has_soft_deadline_ &&
        std::chrono::steady_clock::now() >= soft_deadline_;
}

bool SearchEngine::is_draw(const Position& position, int ply) {
    if (position.halfmove_clock >= 100) {
        if (!in_check(position, static_cast<Color>(position.side_to_move))) return true;
        std::vector<Move> legal;
        generate_legal_moves(position, legal);
        if (!legal.empty()) return true;
    }
    if (path_keys_.size() < 3) return false;
    const std::size_t current = path_keys_.size() - 1;
    const std::size_t max_back = std::min<std::size_t>(
        static_cast<std::size_t>(std::max(0, position.halfmove_clock)), current);
    const std::size_t earliest = current - max_back;
    // Same side to move occurs every two plies. As in uSearch.isDraw, one
    // repetition inside the current search line is sufficient for a draw.
    for (std::size_t i = current >= 2 ? current - 2 : 0;;) {
        if (i < earliest) break;
        if (path_keys_[i] == position.key) return true;
        if (i < earliest + 2) break;
        i -= 2;
    }
    (void)ply;
    return false;
}

MoveHistoryContext SearchEngine::history_context(int ply) const noexcept {
    MoveHistoryContext context{};
    auto assign = [&](int source_ply, PreviousMoveContext& out) {
        if (source_ply < 0 || source_ply > kSearchMaxPly) return;
        const StackFrame& frame = stack_[static_cast<std::size_t>(source_ply)];
        if (!frame.valid) return;
        out.piece = frame.history_piece;
        out.to = move_to(frame.move);
        out.valid = true;
        out.channel = frame.history_channel;
    };
    assign(ply - 1, context.previous1);
    assign(ply - 2, context.previous2);
    assign(ply - 4, context.previous4);
    return context;
}

Move SearchEngine::previous_counter_move(int ply,
                                         const Position&) const noexcept {
    if (ply <= 0 || ply - 1 > kSearchMaxPly || !stack_[ply - 1].valid) return 0;
    const StackFrame& previous = stack_[ply - 1];
    return history_.counter_move(previous.counter_piece, move_to(previous.move));
}

int SearchEngine::scaled_eval(int score, const Position& position) const noexcept {
    // Booot uSearch.ScaleEval: rule-50 scaling followed by three independent
    // correction histories (pawn structure, White non-pawn material, Black
    // non-pawn material). Keep the original integer division order.
    const int factor = std::clamp(200 - position.halfmove_clock, 0, 200);
    score = (factor * score) / 200;
    const std::size_t side = static_cast<std::size_t>(position.side_to_move);
    const std::size_t mask = kCorrectionSize - 1;
    score += (32 * pawn_correction_[side * kCorrectionSize + (position.pawn_key & mask)]) / 512;
    score += (40 * white_nonpawn_correction_[side * kCorrectionSize + (position.non_pawn_key[White] & mask)]) / 512;
    score += (40 * black_nonpawn_correction_[side * kCorrectionSize + (position.non_pawn_key[Black] & mask)]) / 512;
    return score;
}

void SearchEngine::update_correction_history(const Position& position,
                                             int best_value, int static_eval,
                                             int depth, TTBound bound,
                                             Move best_move) noexcept {
    // Booot updates correction only for a quiet best move, outside check, and
    // when the node bound can improve the static evaluation estimate.
    if (best_move == 0 || move_is_capture(best_move) || move_is_promotion(best_move) ||
        in_check(position, static_cast<Color>(position.side_to_move))) return;
    const bool usable = (bound == TTBound::Lower && best_value >= static_eval) ||
                        (bound == TTBound::Upper && best_value < static_eval) ||
                        (bound == TTBound::Exact);
    if (!usable) return;
    int bonus = ((best_value - static_eval) * std::max(1, depth)) / 10;
    bonus = std::clamp(bonus, -128, 128); // CorrLimit = CorrDiv / 4
    const std::size_t side = static_cast<std::size_t>(position.side_to_move);
    const std::size_t mask = kCorrectionSize - 1;
    auto gravity = [bonus](std::int16_t& cell) {
        constexpr int range = 512; // CorrDiv
        const int current = cell;
        const int next = current + bonus - (current * std::abs(bonus)) / range;
        cell = static_cast<std::int16_t>(std::clamp(next, -range, range));
    };
    gravity(pawn_correction_[side * kCorrectionSize + (position.pawn_key & mask)]);
    gravity(white_nonpawn_correction_[side * kCorrectionSize + (position.non_pawn_key[White] & mask)]);
    gravity(black_nonpawn_correction_[side * kCorrectionSize + (position.non_pawn_key[Black] & mask)]);
}

int SearchEngine::lmr_reduction(int depth, int move_number, bool improving,
                                bool tt_pv, bool low_pv, bool cut_node, bool hash_capture,
                                bool killer_or_counter, bool gives_check,
                                bool quiet_singular, int history_score) const noexcept {
    // Match uSearch.LMRReduction and the adjustments in uSearch.Search.
    // Keep the result signed: a negative R intentionally searches one ply
    // deeper (D := newdepth + 1) in the original Pascal implementation.
    depth = std::clamp(depth, 1, 64);
    move_number = std::clamp(move_number, 1, 64);
    double r = (21.0 * std::log(static_cast<double>(depth))) *
               (21.0 * std::log(static_cast<double>(move_number))) + 250.0;
    if (!improving) r += r / 3.0;
    int reduction = static_cast<int>(r / 1000.0);
    if (tt_pv) {
        if (!low_pv) reduction -= 2;
        --reduction;
    }
    if (hash_capture) ++reduction;
    if (quiet_singular) --reduction;
    if (cut_node) reduction += 2;
    else if (killer_or_counter) --reduction;
    if (gives_check) --reduction;
    reduction -= history_score / kHistoryLmrDivisor;
    return reduction;
}

void SearchEngine::update_pv(int ply, Move move) {
    if (ply < 0 || ply >= kSearchMaxPly) return;
    pv_table_[ply][0] = move;
    const int child_length = std::clamp(pv_length_[ply + 1], 0,
                                        kSearchMaxPly - ply - 1);
    for (int i = 0; i < child_length; ++i)
        pv_table_[ply][i + 1] = pv_table_[ply + 1][i];
    pv_length_[ply] = child_length + 1;
}


int SearchEngine::fv_search(Position& position, NNUEState& nnue,
                            int alpha, int beta, int ply, int depth) {
    ++fv_calls_;
    if (should_stop()) return 0;
    ++nodes_;
    if (should_stop()) return 0;
    if (ply >= kSearchMaxPly - 1) {
        int raw_score = 0;
        return evaluate(position, nnue, raw_score)
            ? scaled_eval(raw_score, position) : 0;
    }

    path_keys_.push_back(position.key);
    if (is_draw(position, ply)) {
        path_keys_.pop_back();
        return (nodes_ & 1U) ? 1 : -1;
    }

    pv_length_[ply] = 0;
    const bool pv_node = (beta - alpha) > 1;
    const int original_alpha = alpha;
    const bool checked = in_check(position, static_cast<Color>(position.side_to_move));
    if (checked) ++fv_check_nodes_;
    const int q_depth = (depth >= 0 || checked) ? 0 : -1;

    const TTEntry* tt_hit = tt_.probe(position.key);
    const Move hash_move = tt_hit ? tt_hit->move : 0;
    const int hash_depth = tt_hit ? tt_hit->depth : -128;
    const int hash_value = tt_hit
        ? value_from_tt(tt_hit->value, ply, kSearchWinScore, kSearchInfinity)
        : -kSearchInfinity;
    const TTBound hash_bound = tt_hit ? tt_hit->bound : TTBound::None;
    int static_eval = tt_hit ? tt_hit->static_eval : kSearchNoEval;
    if (checked) static_eval = -kSearchInfinity;

    const auto can_use_better_score = [](TTBound bound, int score, int level) {
        return ((bound == TTBound::Lower || bound == TTBound::Exact) && score >= level) ||
               ((bound == TTBound::Upper || bound == TTBound::Exact) && score < level);
    };
    if (!pv_node && tt_hit && hash_depth >= q_depth &&
        hash_value != -kSearchInfinity && can_use_better_score(hash_bound, hash_value, beta)) {
        path_keys_.pop_back();
        return hash_value;
    }

    int best_value = -kSearchInfinity;
    int futility = -kSearchInfinity;
    if (!checked) {
        if (static_eval == kSearchNoEval || static_eval == -kSearchInfinity) {
            if (!evaluate(position, nnue, static_eval)) {
                path_keys_.pop_back();
                return 0;
            }
        }
        const int scaled_static_eval = scaled_eval(static_eval, position);
        best_value = scaled_static_eval;
        stack_[ply].static_eval = best_value;
        if (tt_hit && hash_value > -kSearchWinScore &&
            hash_value < kSearchWinScore &&
            can_use_better_score(hash_bound, hash_value, best_value))
            best_value = hash_value;

        if (best_value > alpha) {
            if (best_value >= beta) {
                if (!tt_hit) {
                    tt_.store(position.key, best_value, -5, TTBound::Lower,
                              0, static_eval, pv_node, ply);
                }
                path_keys_.pop_back();
                ++fv_beta_cutoffs_;
                return best_value;
            }
            alpha = best_value;
        }
        futility = best_value + 80; // uSearch.DeltaMargin
    } else {
        stack_[ply].static_eval = -kSearchInfinity;
    }

    const Move previous_move = (ply > 0 && stack_[ply - 1].valid)
        ? stack_[ply - 1].move : 0;
    const Square previous_to = previous_move != 0 ? move_to(previous_move) : -1;
    CheckInfo check_info;
    fill_check_info(check_info, position);

    // Port of uSort.NextFV: hash move, ordered captures/promotions, quiet
    // checks at non-negative depth, or all legal evasions while in check.
    // Pascal uses fixed MaxMoves buffers; avoid three heap-backed vectors and
    // stable_sort allocations at every FV/qsearch node.
    struct FVScoredMove { int score = 0; Move move = 0; std::uint16_t order = 0; };
    std::array<FVScoredMove, MoveList::kCapacity> scored_moves{};
    std::size_t scored_count = 0;
    MoveList ordered_moves;
    MoveList quiet_checks;
    if (hash_move != 0 && is_pseudo_correct(hash_move, position))
        ordered_moves.push_back(hash_move);

    if (checked) {
        MoveList evasions;
        generate_legal_moves(position, evasions);
        std::uint16_t order = 0;
        for (Move move : evasions) {
            if (move == hash_move || scored_count >= scored_moves.size()) continue;
            const int score = move_is_capture(move)
                ? history_.capture_score(position, move)
                : history_.escape_score(position, move, ply, history_context(ply));
            scored_moves[scored_count++] = {score, move, order++};
        }
        std::sort(scored_moves.begin(), scored_moves.begin() +
                  static_cast<std::ptrdiff_t>(scored_count),
            [](const FVScoredMove& a, const FVScoredMove& b) {
                if (a.score != b.score) return a.score > b.score;
                return a.order < b.order;
            });
        for (std::size_t i = 0; i < scored_count; ++i)
            ordered_moves.push_back(scored_moves[i].move);
    } else {
        MoveList pseudo_moves;
        generate_pseudo_legal_moves(position, pseudo_moves);
        std::uint16_t order = 0;
        for (Move move : pseudo_moves) {
            if (move == hash_move ||
                (!move_is_capture(move) && !move_is_promotion(move))) continue;
            if (scored_count < scored_moves.size())
                scored_moves[scored_count++] = {history_.capture_score(position, move), move, order++};
        }
        std::sort(scored_moves.begin(), scored_moves.begin() +
                  static_cast<std::ptrdiff_t>(scored_count),
            [](const FVScoredMove& a, const FVScoredMove& b) {
                if (a.score != b.score) return a.score > b.score;
                return a.order < b.order;
            });
        for (std::size_t i = 0; i < scored_count; ++i)
            ordered_moves.push_back(scored_moves[i].move);

        if (depth >= 0) {
            for (Move move : pseudo_moves) {
                if (move == hash_move || move_is_capture(move) || move_is_promotion(move))
                    continue;
                if (is_move_check(move, check_info, position)) quiet_checks.push_back(move);
            }
            for (Move move : quiet_checks) ordered_moves.push_back(move);
        }
    }

    int searched = 0;
    Move best_move = 0;
    for (Move move : ordered_moves) {
        // NextFV applies its deep recapture-only filter to generated captures,
        // but not to the hash move or to check evasions.
        if (!checked && depth <= -5 && move != hash_move &&
            (move_is_capture(move) || move_is_promotion(move)) &&
            move_to(move) != previous_to) continue;
        if (!is_legal(move, check_info.pinned, position)) continue;
        ++searched;
        if (checked) ++fv_evasions_;
        const bool gives_check = is_move_check(move, check_info, position);
        const bool capture = move_is_capture(move);
        const bool promotion = move_is_promotion(move);
        const Square destination = move_to(move);
        if (!checked && best_value > -kSearchWinScore &&
            position.non_pawn_material[position.side_to_move] > 0) {
            if (!gives_check && futility > -kSearchWinScore && !promotion &&
                destination != previous_to) {
                if (searched > 2) continue; // NextFV's shallow quiet-move limit.
                int captured_value = 0;
                const int captured_piece = position.board[destination];
                const int captured_type = std::abs(captured_piece);
                constexpr std::array<int, 7> piece_futility{{0, 100, 360, 370, 600, 1150, 0}};
                if (captured_type >= Pawn && captured_type <= King)
                    captured_value = piece_futility[static_cast<std::size_t>(captured_type)];
                if (captured_piece == Empty && capture) captured_value += piece_futility[Pawn];
                const int futility_value = futility + captured_value;
                if (futility_value <= alpha) {
                    if (futility_value > best_value) best_value = futility_value;
                    continue;
                }
                if (futility <= alpha && !good_see(move, position, 1)) {
                    if (futility > best_value) best_value = futility;
                    continue;
                }
            }
            if (!good_see(move, position, 0)) continue;
        }

        if (!quiet_checks.empty() &&
            std::find(quiet_checks.begin(), quiet_checks.end(), move) != quiet_checks.end())
            ++fv_quiet_checks_;

        Undo board_undo;
        NNUEUndo nnue_undo;
        if (!make_child(position, nnue, move, board_undo, nnue_undo)) {
            if (stopped_) { path_keys_.pop_back(); return 0; }
            continue;
        }
        stack_[ply].move = move;
        stack_[ply].history_piece = board_undo.moving_piece;
        stack_[ply].counter_piece = position.board[destination];
        stack_[ply].history_channel = static_cast<std::uint8_t>(
            (checked ? 2 : 0) | ((capture || promotion) ? 1 : 0));
        stack_[ply].valid = true;

        const int value = -fv_search(position, nnue, -beta, -alpha,
                                     ply + 1, depth - 1);
        unmake_child(position, nnue, board_undo, nnue_undo);
        stack_[ply].valid = false;
        if (stopped_) { path_keys_.pop_back(); return 0; }

        if (value > best_value) {
            best_value = value;
            if (value > alpha) {
                alpha = value;
                best_move = move;
                update_pv(ply, move);
                if (alpha >= beta) {
                    tt_.store(position.key, best_value, q_depth, TTBound::Lower,
                              best_move, static_eval, pv_node, ply);
                    path_keys_.pop_back();
                    ++fv_beta_cutoffs_;
                    return best_value;
                }
            }
        }
        // In-check search can stop after a quiet evasion once a legal capture
        // has already established a non-losing bound, as in uSearch.FV.
        if (checked && best_value > -kSearchWinScore && !capture) break;
    }

    if (checked && searched == 0) best_value = -kSearchMate + ply;
    if (best_value == -kSearchInfinity && !checked)
        best_value = scaled_eval(static_eval, position);
    tt_.store(position.key, best_value, q_depth, TTBound::Upper,
              best_move, static_eval, pv_node, ply);
    path_keys_.pop_back();
    (void)original_alpha;
    return best_value;
}

int SearchEngine::quiescence(Position& position, NNUEState& nnue,
                             int alpha, int beta, int ply, int qdepth) {
    if (should_stop()) return 0;
    ++nodes_;
    if (should_stop()) return 0;
    if (ply >= kQMaxPly) {
        int raw_eval = 0;
        return evaluate(position, nnue, raw_eval)
            ? scaled_eval(raw_eval, position) : 0;
    }
    path_keys_.push_back(position.key);
    const bool draw = (ply > 0 && is_draw(position, ply));
    if (draw) { path_keys_.pop_back(); return (nodes_ & 1U) ? 1 : -1; }

    pv_length_[ply] = 0;
    const bool checked = in_check(position, static_cast<Color>(position.side_to_move));
    int best = -kSearchInfinity;
    int static_eval = -kSearchInfinity;
    if (!checked) {
        if (!evaluate(position, nnue, static_eval)) {
            path_keys_.pop_back();
            return 0;
        }
        static_eval = scaled_eval(static_eval, position);
        best = static_eval;
        if (best >= beta) { path_keys_.pop_back(); return best; }
        if (best > alpha) alpha = best;
        if (qdepth < -12) { path_keys_.pop_back(); return best; }
    }

    const TTEntry* hit = tt_.probe(position.key);
    const Move hash_move = hit ? hit->move : 0;
    const Move killer1 = history_.killer(ply, 0);
    const Move killer2 = history_.killer(ply, 1);
    MovePicker picker(position, history_, ply, history_context(ply),
                      hash_move, killer1, killer2,
                      previous_counter_move(ply, position), true);
    CheckInfo check_info;
    fill_check_info(check_info, position);
    int legal_searched = 0;
    while (auto candidate = picker.next()) {
        const Move move = *candidate;
        if (!is_legal(move, check_info.pinned, position)) continue;
        ++legal_searched;
        Undo board_undo;
        NNUEUndo nnue_undo;
        if (!make_child(position, nnue, move, board_undo, nnue_undo)) continue;
        const int value = -quiescence(position, nnue, -beta, -alpha,
                                      ply + 1, qdepth - 1);
        unmake_child(position, nnue, board_undo, nnue_undo);
        if (stopped_) { path_keys_.pop_back(); return 0; }
        if (value > best) best = value;
        if (value > alpha) {
            alpha = value;
            update_pv(ply, move);
            if (alpha >= beta) break;
        }
    }
    if (checked && legal_searched == 0) best = -kSearchMate + ply;
    if (!checked && legal_searched == 0 && best == -kSearchInfinity) best = static_eval;
    path_keys_.pop_back();
    return best;
}

int SearchEngine::alpha_beta(Position& position, NNUEState& nnue, int depth,
                             int alpha, int beta, int ply, bool pv_node,
                             bool allow_null, Move excluded_move, bool cut_node) {
    if (depth <= 0) return fv_search(position, nnue, alpha, beta, ply, depth);
    if (should_stop()) return 0;
    ++nodes_;
    if (should_stop()) return 0;
    if (ply >= kSearchMaxPly - 1) {
        int raw_eval = 0;
        return evaluate(position, nnue, raw_eval)
            ? scaled_eval(raw_eval, position) : 0;
    }

    path_keys_.push_back(position.key);
    if (is_draw(position, ply)) {
        path_keys_.pop_back();
        return (nodes_ & 1U) ? 1 : -1;
    }

    pv_length_[ply] = 0;
    const int original_alpha = alpha;
    if (alpha < -kSearchMate + ply) alpha = -kSearchMate + ply;
    if (beta > kSearchMate - ply) beta = kSearchMate - ply;
    if (alpha >= beta) { path_keys_.pop_back(); return alpha; }

    const TTEntry* tt_hit = tt_.probe(position.key);
    const Move hash_move = tt_hit ? tt_hit->move : 0;
    const bool tt_pv = pv_node || (tt_hit && tt_hit->pv);
    const int hash_depth = tt_hit ? tt_hit->depth : -128;
    const int hash_value = tt_hit
        ? value_from_tt(tt_hit->value, ply, kSearchWinScore, kSearchInfinity)
        : -kSearchInfinity;
    const TTBound hash_bound = tt_hit ? tt_hit->bound : TTBound::None;
    const bool low_pv = pv_node && hash_move != 0 && tt_hit &&
                        (hash_bound == TTBound::Upper || hash_bound == TTBound::Exact) &&
                        hash_depth >= depth;
    int static_eval = -kSearchInfinity;

    if (tt_hit) {
        static_eval = tt_hit->static_eval;
        if (excluded_move == 0 && !pv_node && hash_depth >= depth &&
            hash_value != -kSearchInfinity && position.halfmove_clock < 90) {
            if (hash_bound == TTBound::Exact ||
                (hash_bound == TTBound::Lower && hash_value >= beta) ||
                (hash_bound == TTBound::Upper && hash_value <= alpha)) {
                path_keys_.pop_back();
                return hash_value;
            }
        }
    }

    const bool checked = in_check(position, static_cast<Color>(position.side_to_move));
    int eval = -kSearchInfinity;
    int scaled_static_eval = -kSearchInfinity;
    if (!checked) {
        if (static_eval == kSearchNoEval || static_eval == -kSearchInfinity) {
            if (!evaluate(position, nnue, static_eval)) {
                path_keys_.pop_back();
                return 0;
            }
        }
        scaled_static_eval = scaled_eval(static_eval, position);
        eval = scaled_static_eval;
        if (tt_hit && hash_value != -kSearchInfinity &&
            (hash_bound == TTBound::Exact ||
             (hash_bound == TTBound::Lower && hash_value > eval) ||
             (hash_bound == TTBound::Upper && hash_value < eval)))
            eval = hash_value;
    }

    const bool improving = ply >= 2 && stack_[ply - 2].static_eval > -kSearchInfinity &&
        scaled_static_eval > stack_[ply - 2].static_eval;
    // Pascal's Tree[ply].StatEval remains ScaleEval(raw NNUE), even when a
    // compatible TT bound improves the local pruning value in Eval.
    stack_[ply].static_eval = checked ? -kSearchInfinity : scaled_static_eval;

    // Booot razoring: a shallow, non-PV node far below alpha is reduced to qsearch.
    if (!pv_node && !checked && depth < 4 && eval <= alpha - kRazorMargin * depth) {
        const int razor = fv_search(position, nnue, alpha, beta, ply, 0);
        if (stopped_) { path_keys_.pop_back(); return 0; }
        if (razor <= alpha) { path_keys_.pop_back(); return razor; }
    }

    // Booot static pruning at shallow non-PV nodes. Hash captures prevent it.
    const bool hash_capture = hash_move != 0 && move_is_capture(hash_move);
    if (!pv_node && !checked && depth < 7 &&
        eval - kStatixMargin * (improving ? std::max(0, depth - 1) : depth) >= beta &&
        (hash_move == 0 || hash_capture)) {
        path_keys_.pop_back();
        return eval;
    }

    // Null-move pruning, with Booot's material guard and adaptive reduction.
    if (excluded_move == 0 && allow_null && !pv_node && !checked && depth >= 3 && ply > 0 &&
        stack_[ply - 1].valid &&
        position.non_pawn_material[position.side_to_move] > 0 &&
        eval >= beta && eval >= beta - 14 * depth + 90 &&
        beta > -kSearchWinScore) {
        int reduction = 4 + depth / 3 + std::min(4, std::max(0, (eval - beta) / 100));
        Undo board_undo;
        NNUEUndo nnue_undo;
        if (make_null(position, nnue, board_undo, nnue_undo)) {
            stack_[ply].valid = false;
            const int null_value = -alpha_beta(position, nnue, depth - reduction,
                                               -beta, -beta + 1, ply + 1,
                                               false, false, 0, !cut_node);
            unmake_null(position, nnue, board_undo, nnue_undo);
            if (stopped_) { path_keys_.pop_back(); return 0; }
            if (null_value >= beta) {
                const int result = null_value >= kSearchWinScore ? beta : null_value;
                if (depth < 12) { path_keys_.pop_back(); return result; }
                // Deep fail-highs are verified without allowing another null move.
                const int verify = alpha_beta(position, nnue, depth - reduction,
                                              beta - 1, beta, ply, false, false, 0, false);
                if (stopped_) { path_keys_.pop_back(); return 0; }
                if (verify >= beta) { path_keys_.pop_back(); return result; }
            }
        }
        if (stopped_) { path_keys_.pop_back(); return 0; }
    }

    // Booot uSearch.ProbCut: first test the TT move, then captures/promotions
    // with SEE >= (beta + 85 - static evaluation). A shallow qsearch gates
    // the reduced-depth verification search.
    if (excluded_move == 0 && !pv_node && !checked && depth >= kProbCutDepth &&
        std::abs(beta) < kSearchWinScore) {
        const int probcut_beta = std::min(beta + kProbCutMargin, kSearchMate);
        const bool bad_probcut = tt_hit && hash_depth >= depth - 3 &&
            hash_value != -kSearchInfinity && hash_value < probcut_beta;
        if (!bad_probcut) {
            ++probcut_attempts_;
            const int probcut_depth = depth - kProbCutReduction;
            const int probcut_see_margin = probcut_beta - static_eval;
            MovePicker probcut_picker(position, history_, ply, history_context(ply),
                                      hash_move, 0, 0, 0, true);
            CheckInfo probcut_check_info;
            fill_check_info(probcut_check_info, position);
            while (auto candidate = probcut_picker.next()) {
                const Move move = *candidate;
                if (!move_is_capture(move) || move == excluded_move ||
                    !is_legal(move, probcut_check_info.pinned, position) ||
                    !good_see(move, position, probcut_see_margin)) continue;

                const bool capture = move_is_capture(move);
                const bool promotion = move_is_promotion(move);
                const int destination = move_to(move);
                Undo board_undo;
                NNUEUndo nnue_undo;
                if (!make_child(position, nnue, move, board_undo, nnue_undo)) {
                    if (stopped_) { path_keys_.pop_back(); return 0; }
                    continue;
                }
                stack_[ply].move = move;
                stack_[ply].history_piece = board_undo.moving_piece;
                stack_[ply].counter_piece = position.board[destination];
                stack_[ply].history_channel = static_cast<std::uint8_t>(
                    (checked ? 2 : 0) | ((capture || promotion) ? 1 : 0));
                stack_[ply].double_extensions = ply > 0
                    ? stack_[ply - 1].double_extensions : 0;
                stack_[ply].valid = true;

                int value = -fv_search(position, nnue, -probcut_beta,
                                       -probcut_beta + 1, ply + 1, 0);
                if (!stopped_ && value >= probcut_beta) {
                    value = -alpha_beta(position, nnue, probcut_depth,
                                        -probcut_beta, -probcut_beta + 1,
                                        ply + 1, false, allow_null, 0, !cut_node);
                }

                unmake_child(position, nnue, board_undo, nnue_undo);
                stack_[ply].valid = false;
                if (stopped_) { path_keys_.pop_back(); return 0; }
                if (value >= probcut_beta) {
                    tt_.store(position.key, value, probcut_depth + 1,
                              TTBound::Lower, move, static_eval, tt_pv, ply);
                    ++probcut_cutoffs_;
                    path_keys_.pop_back();
                    return value;
                }
            }
        }
    }

    // Original Booot IIR: with no TT move, reduce a PV node from depth 3,
    // or a cut node from depth 7, by two plies. It is opt-in just like
    // uSearch.game.doIIR (whose original default is false).
    if (limits_.enable_iir && excluded_move == 0 && hash_move == 0 &&
        ((pv_node && depth >= kIirDepthPv) ||
         (cut_node && depth >= kIirDepthCut))) {
        depth = std::max(1, depth - kIirReduction);
        ++iid_reductions_;
    }

    const Move killer1 = history_.killer(ply, 0);
    const Move killer2 = history_.killer(ply, 1);
    const Move counter = previous_counter_move(ply, position);
    const MoveHistoryContext context = history_context(ply);
    const bool singular_node = excluded_move == 0 && depth >= kSingularDepth &&
        hash_move != 0 && std::abs(hash_value) < kSearchWinScore &&
        (hash_bound == TTBound::Lower || hash_bound == TTBound::Exact) &&
        hash_depth >= depth - 3;
    MovePicker picker(position, history_, ply, context, hash_move,
                      killer1, killer2, counter, false);
    CheckInfo check_info;
    fill_check_info(check_info, position);

    int best_value = -kSearchInfinity;
    Move best_move = 0;
    int searched = 0;
    std::array<Move, MoveList::kCapacity> failed_quiets{};
    std::size_t failed_quiet_count = 0;

    while (auto candidate = picker.next()) {
        const Move move = *candidate;
        if (move == excluded_move || !is_legal(move, check_info.pinned, position)) continue;
        const bool capture = move_is_capture(move);
        const bool promotion = move_is_promotion(move);
        const bool quiet = !capture && !promotion;
        const int destination = move_to(move);
        const bool gives_check = is_move_check(move, check_info, position);
        int new_depth = depth - 1;
        ++searched; // Pascal increments before move-count/history pruning.

        // Booot's optional late-move count pruning (doLMP defaults off).
        if (limits_.enable_lmp && quiet && !checked && !gives_check &&
            depth < kCountMoveDepth &&
            searched >= (improving ? 5 + depth * depth :
                static_cast<int>(std::nearbyint((5.0 + depth * depth) / 2.0))) &&
            best_value > -kSearchWinScore &&
            position.non_pawn_material[position.side_to_move] > 0) {
            ++lmp_prunes_;
            continue;
        }

        // Original history pruning: if both the previous and the second-
        // previous continuation-history entries dislike this quiet move,
        // discard it when its predicted depth falls below HistoryDepth=3.
        if (quiet && !checked && !gives_check && best_value > -kSearchWinScore &&
            position.non_pawn_material[position.side_to_move] > 0 && ply >= 2) {
            const int moving_piece = position.board[move_from(move)];
            const int base_reduction = lmr_reduction(depth, searched, improving,
                false, false, false, false, false, false, false, 0);
            const int predicted_depth = std::max(1, new_depth - base_reduction);
            const int previous1 = history_.continuation_score(
                context.previous1, moving_piece, static_cast<Square>(destination));
            const int previous2 = history_.continuation_score(
                context.previous2, moving_piece, static_cast<Square>(destination));
            if (predicted_depth < kHistoryDepth && previous1 < 0 && previous2 < 0) {
                ++history_prunes_;
                continue;
            }
        }

        if (quiet && !checked && depth <= 7 && best_value > -kSearchWinScore &&
            position.non_pawn_material[position.side_to_move] > 0 && !gives_check) {
            const int margin = kStatixMargin * std::max(0, new_depth) + kFutilityMargin;
            if (eval + margin <= alpha) continue;
            if (!good_see(move, position, -10 * new_depth * new_depth)) continue;
        } else if (!checked && (capture || promotion) && !good_see(move, position, -100 * depth)) {
            // Avoid pruning checks and all evasions; this branch is only applied
            // after the legality test and only outside check.
            if (!gives_check) continue;
        }

        int extension = 0;
        bool quiet_singular = false;
        if (singular_node && move == hash_move) {
            ++singular_tests_;
            const int singular_beta = hash_value - depth;
            const int singular_depth = (depth - 1) / 2;
            // Re-enter this same ply without duplicating the current position
            // in the repetition path; alpha_beta will push it exactly once.
            if (!path_keys_.empty()) path_keys_.pop_back();
            const int singular_value = alpha_beta(position, nnue, singular_depth,
                singular_beta - 1, singular_beta, ply, false, false, move, cut_node);
            path_keys_.push_back(position.key);
            pv_length_[ply] = 0; // The exclusion search is not part of the main PV.
            if (stopped_) { path_keys_.pop_back(); return 0; }

            if (singular_value < singular_beta) {
                quiet_singular = !capture;
                const int previous_double_extensions = ply > 0
                    ? stack_[ply - 1].double_extensions : 0;
                if (!pv_node && singular_value < singular_beta - 15 &&
                    previous_double_extensions <= 6) extension = 2;
                else extension = 1;
            } else if (singular_beta >= beta) {
                path_keys_.pop_back();
                return singular_beta;
            } else if (hash_value >= beta) {
                extension = pv_node ? -1 : -2;
            } else if (hash_value <= alpha) {
                extension = -1;
            }
            if (extension > 0) ++singular_extensions_;
        }
        new_depth = std::max(0, depth + extension - 1);
        const bool parent_in_check = checked;
        const int moving_piece = position.board[move_from(move)];
        const int history_score = quiet
            ? 2 * history_.quiet_history(moving_piece, static_cast<Square>(destination)) +
              history_.continuation_score(context.previous1, moving_piece, static_cast<Square>(destination)) +
              history_.continuation_score(context.previous2, moving_piece, static_cast<Square>(destination)) +
              history_.continuation_score(context.previous4, moving_piece, static_cast<Square>(destination))
            : 0;
        Undo board_undo;
        NNUEUndo nnue_undo;
        if (!make_child(position, nnue, move, board_undo, nnue_undo)) {
            --searched;
            if (stopped_) { path_keys_.pop_back(); return 0; }
            continue;
        }
        stack_[ply].move = move;
        stack_[ply].history_piece = board_undo.moving_piece;
        stack_[ply].counter_piece = position.board[destination];
        stack_[ply].history_channel = static_cast<std::uint8_t>(
            (parent_in_check ? 2 : 0) | ((capture || promotion) ? 1 : 0));
        stack_[ply].double_extensions = (ply > 0
            ? stack_[ply - 1].double_extensions : 0) + (extension > 1 ? 1 : 0);
        stack_[ply].valid = true;

        int value = -kSearchInfinity;
        if (searched == 1) {
            value = -alpha_beta(position, nnue, new_depth, -beta, -alpha,
                                ply + 1, pv_node, true, 0, false);
        } else {
            int lmr_depth = -kSearchMaxPly; // Pascal's D sentinel when LMR is inactive.
            bool do_research = true;
            if (quiet && depth >= 3 && searched > 1) {
                const int reduction = lmr_reduction(depth, searched, improving,
                    tt_pv, low_pv, cut_node, hash_capture,
                    move == killer1 || move == killer2 || move == counter,
                    gives_check, quiet_singular, history_score);
                lmr_depth = reduction >= 0 ? new_depth - reduction : new_depth + 1;
                lmr_depth = std::max(1, lmr_depth);
                value = -alpha_beta(position, nnue, lmr_depth, -alpha - 1,
                                    -alpha, ply + 1, false, true, 0, true);
                do_research = !stopped_ && value > alpha && lmr_depth < new_depth;
            }
            if (!stopped_ && do_research) {
                if (!pv_node && lmr_depth > 0 &&
                    value < best_value + new_depth) --new_depth;
                if (value > best_value + 50) ++new_depth;
                if (new_depth > lmr_depth)
                    value = -alpha_beta(position, nnue, new_depth, -alpha - 1,
                                        -alpha, ply + 1, false, true, 0, !cut_node);
            }
            if (!stopped_ && pv_node && value > alpha && value < beta)
                value = -alpha_beta(position, nnue, new_depth, -beta, -alpha,
                                    ply + 1, true, true);
        }

        unmake_child(position, nnue, board_undo, nnue_undo);
        if (stopped_) { stack_[ply].valid = false; path_keys_.pop_back(); return 0; }

        if (value > best_value) {
            best_value = value;
            if (value > alpha) {
                alpha = value;
                best_move = move;
                update_pv(ply, move);
                if (alpha >= beta) {
                    if (quiet) {
                        const int bonus = MoveHistory::stat_bonus(depth);
                        history_.update_quiet(position, move, bonus, ply, context);
                        history_.update_killer_and_counter(position, move, ply, stack_[ply - 1].valid ? stack_[ply - 1].move : 0);
                        for (std::size_t i = 0; i < failed_quiet_count; ++i)
                            history_.update_quiet(position, failed_quiets[i], -bonus / 2, ply, context);
                    } else {
                        history_.update_capture(position, move,
                                                MoveHistory::stat_bonus(depth));
                    }
                    break;
                }
            }
        }
        if (quiet && move != best_move && failed_quiet_count < failed_quiets.size())
            failed_quiets[failed_quiet_count++] = move;
        stack_[ply].valid = false;
    }

    if (searched == 0 && excluded_move == 0) {
        std::vector<Move> legal;
        generate_legal_moves(position, legal);
        if (legal.empty()) best_value = checked ? -kSearchMate + ply : 0;
        else if (best_value == -kSearchInfinity) best_value = checked ? -kSearchMate + ply : eval;
    }

    if (!stopped_ && excluded_move == 0) {
        TTBound bound = TTBound::Upper;
        if (best_value >= beta) bound = TTBound::Lower;
        else if (pv_node && best_move != 0 && best_value > original_alpha)
            bound = TTBound::Exact;
        tt_.store(position.key, best_value, depth, bound, best_move,
                  static_eval, tt_pv, ply);
        update_correction_history(position, best_value, scaled_static_eval, depth,
                                  bound, best_move);
    }
    stack_[ply].valid = false;
    path_keys_.pop_back();
    return best_value;
}

int SearchEngine::root_search(Position& position, NNUEState& nnue,
                              std::vector<Move>& root_moves, int depth,
                              int alpha, int beta, Move& best_move) {
    const int original_alpha = alpha;
    const bool pv_node = (beta - alpha) > 1;
    best_move = 0;
    pv_length_[0] = 0;
    if (root_moves.empty()) {
        std::vector<Move> legal;
        generate_legal_moves(position, legal);
        if (legal.empty())
            return in_check(position, static_cast<Color>(position.side_to_move))
                ? -kSearchMate : 0;
    }

    const CheckInfo check_info = [&]() {
        CheckInfo info;
        fill_check_info(info, position);
        return info;
    }();
    // Pascal RootSearch retains its legal root list and promotes only the
    // current best move to index zero between iterations (UpdateList). It does
    // not run the interior staged MovePicker at the root.
    const Move first_move = root_moves.front();
    const bool hash_capture = first_move != 0 && move_is_capture(first_move);

    int best_value = -kSearchInfinity;
    int searched = 0;
    std::vector<Move> failed_quiets;
    std::vector<Move> failed_captures;
    failed_quiets.reserve(root_moves.size());
    failed_captures.reserve(root_moves.size());
    for (std::size_t index = 0; index < root_moves.size(); ++index) {
        if (should_stop(true)) break;
        const Move move = root_moves[index];
        if (!is_legal(move, check_info.pinned, position)) continue;
        const bool capture = move_is_capture(move);
        const bool promotion = move_is_promotion(move);
        const bool quiet = !capture && !promotion;
        const bool gives_check = is_move_check(move, check_info, position);
        const bool root_parent_in_check = in_check(
            position, static_cast<Color>(position.side_to_move));
        Undo board_undo;
        NNUEUndo nnue_undo;
        if (!make_child(position, nnue, move, board_undo, nnue_undo)) {
            if (stopped_) break;
            continue;
        }

        stack_[0].move = move;
        stack_[0].history_piece = board_undo.moving_piece;
        stack_[0].counter_piece = position.board[move_to(move)];
        stack_[0].history_channel = static_cast<std::uint8_t>(
            (root_parent_in_check ? 2 : 0) |
            ((move_is_capture(move) || move_is_promotion(move)) ? 1 : 0));
        stack_[0].valid = true;

        path_keys_.push_back(board_undo.key);
        pv_length_[1] = 0;
        int value = -kSearchInfinity;
        const int history_score = quiet
            ? 2 * history_.quiet_history(board_undo.moving_piece,
                                         move_to(move)) : 0;
        int root_reduction = -1;
        if (quiet && depth >= 3 && searched >= 3) {
            // RootSearch applies R -= 2 only for a PV window, without the
            // non-root cut/killer adjustments.
            const int root_base = lmr_reduction(depth, searched + 1, true,
                false, false, false, hash_capture, false, gives_check, false,
                history_score);
            root_reduction = root_base - (pv_node ? 2 : 0);
        }
        if (searched == 0) {
            value = -alpha_beta(position, nnue, depth - 1, -beta, -alpha,
                                1, true, true, 0, false);
        } else {
            bool do_research = true;
            int lmr_depth = -1;
            int root_child_depth = depth - 1;
            if (root_reduction > 0) {
                lmr_depth = std::max(1, root_child_depth - root_reduction);
                ++root_lmr_reductions_;
                value = -alpha_beta(position, nnue, lmr_depth, -alpha - 1, -alpha,
                                    1, false, true, 0, true);
                do_research = !stopped_ && value > alpha;
            }
            if (!stopped_ && do_research) {
                if (root_reduction > 0 && value > best_value + 80) ++root_child_depth;
                if (root_reduction > 0) {
                    ++root_lmr_researches_;
                    pv_length_[1] = 0;
                }
                value = -alpha_beta(position, nnue, root_child_depth, -alpha - 1, -alpha,
                                    1, false, true, 0, true);
            }
            if (!stopped_ && value > alpha && value < beta) {
                pv_length_[1] = 0;
                value = -alpha_beta(position, nnue, root_child_depth, -beta, -alpha,
                                    1, true, true, 0, false);
            }
        }
        path_keys_.pop_back();
        unmake_child(position, nnue, board_undo, nnue_undo);
        stack_[0].valid = false;
        ++root_move_searches_;
        ++searched;
        if (stopped_) break;

        if (value > best_value) {
            best_value = value;
            best_move = move;
            update_pv(0, move);
        }
        if (value > alpha) alpha = value;
        if (best_move != move) {
            if (capture) failed_captures.push_back(move);
            else failed_quiets.push_back(move);
        }
        if (alpha >= beta) break;
        if (should_stop(true)) break;
    }

    if (best_value == -kSearchInfinity) best_value = 0;
    if (!stopped_ && best_move != 0) {
        const int margin = best_value - beta;
        const int bonus = MoveHistory::stat_bonus(depth + (margin >= 6 ? 1 : 0));
        if (move_is_capture(best_move)) {
            history_.update_capture(position, best_move, bonus);
        } else {
            history_.update_quiet(position, best_move, bonus, 0, MoveHistoryContext{});
            history_.update_killer_and_counter(position, best_move, 0, 0);
            for (Move failed : failed_quiets)
                history_.update_quiet(position, failed, -bonus, 0, MoveHistoryContext{});
        }
        for (Move failed : failed_captures)
            history_.update_capture(position, failed, -bonus);
    }
    if (best_move != 0) {
        const auto it = std::find(root_moves.begin(), root_moves.end(), best_move);
        if (it != root_moves.end()) std::rotate(root_moves.begin(), it, it + 1);
    }
    if (!stopped_) {
        TTBound bound = TTBound::Upper;
        if (best_value >= beta) bound = TTBound::Lower;
        else if (best_value > original_alpha) bound = TTBound::Exact;
        tt_.store(position.key, best_value, depth, bound, best_move,
                  kSearchNoEval, true, 0);
    }
    return best_value;
}

SearchResult SearchEngine::search(Position& root, NNUEState& root_nnue,
                                  const SearchLimits& limits) {
    SearchResult result;
    limits_ = limits;
    nodes_ = 0;
    stopped_ = false;
    error_.clear();
    time_limit_hit_ = false;
    has_soft_deadline_ = false;
    has_hard_deadline_ = false;
    root_move_searches_ = 0;
    root_lmr_reductions_ = 0;
    root_lmr_researches_ = 0;
    aspiration_fail_lows_ = 0;
    aspiration_fail_highs_ = 0;
    probcut_attempts_ = 0;
    probcut_cutoffs_ = 0;
    singular_tests_ = 0;
    singular_extensions_ = 0;
    fv_calls_ = 0;
    fv_beta_cutoffs_ = 0;
    fv_quiet_checks_ = 0;
    fv_check_nodes_ = 0;
    fv_evasions_ = 0;
    iid_reductions_ = 0;
    lmp_prunes_ = 0;
    history_prunes_ = 0;
    search_start_ = std::chrono::steady_clock::now();

    // Time allocation: a fixed movetime reserves the configured overhead;
    // otherwise allocate roughly 1/moves_to_go (default 30) plus 3/4 increment.
    // Explicit limits override the automatic budget and are useful to UCI hosts.
    std::uint64_t soft_ms = limits.soft_time_limit_ms;
    std::uint64_t hard_ms = limits.hard_time_limit_ms;
    const std::uint64_t overhead = limits.move_overhead_ms;
    const auto saturating_add = [](std::uint64_t a, std::uint64_t b) {
        return b > std::numeric_limits<std::uint64_t>::max() - a
            ? std::numeric_limits<std::uint64_t>::max() : a + b;
    };
    const auto saturating_times_three = [](std::uint64_t value) {
        return value > std::numeric_limits<std::uint64_t>::max() / 3
            ? std::numeric_limits<std::uint64_t>::max() : value * 3;
    };
    if (limits.move_time_ms != 0) {
        const std::uint64_t usable = limits.move_time_ms > overhead
            ? limits.move_time_ms - overhead : 1;
        if (soft_ms == 0) soft_ms = std::max<std::uint64_t>(1, usable * 3 / 4);
        if (hard_ms == 0) hard_ms = std::max<std::uint64_t>(1, usable);
    } else if (limits.time_left_ms != 0) {
        const std::uint64_t usable = limits.time_left_ms > overhead
            ? limits.time_left_ms - overhead : 1;
        const std::uint64_t horizon = limits.moves_to_go != 0
            ? std::clamp<std::uint64_t>(limits.moves_to_go, 1, 80) : 30;
        const std::uint64_t increment_share = limits.increment_ms - limits.increment_ms / 4;
        const std::uint64_t base = usable / horizon;
        const std::uint64_t auto_soft = std::min(usable,
            saturating_add(base, increment_share));
        const std::uint64_t auto_hard = std::min(usable,
            std::max(saturating_times_three(auto_soft),
                     saturating_add(auto_soft, limits.increment_ms)));
        if (soft_ms == 0) soft_ms = std::max<std::uint64_t>(1, auto_soft);
        if (hard_ms == 0) hard_ms = std::max<std::uint64_t>(1, auto_hard);
    }
    if (hard_ms == 0 && soft_ms != 0) hard_ms = soft_ms;
    if (soft_ms == 0 && hard_ms != 0) soft_ms = std::max<std::uint64_t>(1, hard_ms * 3 / 4);
    if (hard_ms != 0 && soft_ms > hard_ms) soft_ms = hard_ms;
    if (soft_ms != 0) {
        has_soft_deadline_ = true;
        soft_deadline_ = search_start_ + std::chrono::milliseconds(soft_ms);
    }
    if (hard_ms != 0) {
        has_hard_deadline_ = true;
        hard_deadline_ = search_start_ + std::chrono::milliseconds(hard_ms);
    }
    result.soft_time_budget_ms = soft_ms;
    result.hard_time_budget_ms = hard_ms;

    for (auto& row : pv_table_) row.fill(0);
    pv_length_.fill(0);
    stack_.fill(StackFrame{});
    path_keys_ = limits.previous_position_keys;

    if (use_nnue_) {
        std::string error;
        if (!root_nnue.valid || root_nnue.position_key != root.key) {
            if (!refresh_nnue_state(root, networks_, root_nnue, error)) {
                result.error = "Unable to initialize Booot NNUE state: " + error;
                result.elapsed_ms = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - search_start_).count());
                return result;
            }
        }
    }

    std::vector<Move> root_moves;
    generate_legal_moves(root, root_moves);
    bool root_is_draw = false;
    path_keys_.push_back(root.key);
    root_is_draw = is_draw(root, 0);
    path_keys_.pop_back();
    if (root_moves.empty()) {
        result.score = in_check(root, static_cast<Color>(root.side_to_move))
            ? -kSearchMate : 0;
    } else if (root_is_draw) {
        // Match the original root Search behavior: a rule-50/repetition draw
        // returns no best move and completes the first root iteration.
        result.completed_depth = 1;
        result.score = (nodes_ & 1U) ? 1 : -1;
    } else {
        tt_.new_search();
        Move last_best = 0;
        int last_score = 0;
        const int max_depth = std::clamp(limits.depth, 1, kSearchMaxPly - 1);
        for (int depth = 1; depth <= max_depth; ++depth) {
            if (should_stop(true)) break;
            int hbc = 0; // Pascal Iterate reduces the retry depth after a modest fail-high.
            const int initial_delta = depth > 5 ? 10 : kSearchInfinity;
            int delta = initial_delta;
            int alpha = depth > 5
                ? std::max(-kSearchInfinity, last_score - delta) : -kSearchInfinity;
            int beta = depth > 5
                ? std::min(kSearchInfinity, last_score + delta) : kSearchInfinity;
            int score = 0;
            for (;;) {
                const int retry_depth = std::max(1, depth - hbc);
                score = root_search(root, root_nnue, root_moves, retry_depth,
                                    alpha, beta, last_best);
                if (stopped_) break;
                if (score <= alpha) {
                    hbc = 0;
                    ++aspiration_fail_lows_;
                    beta = (alpha + beta) / 2;
                    delta += delta / 4 + 2;
                    alpha = std::max(-kSearchInfinity, score - delta);
                } else if (score >= beta) {
                    ++aspiration_fail_highs_;
                    if (std::abs(score) < 1200 && hbc < depth - 1) ++hbc;
                    delta += delta / 4 + 2;
                    beta = std::min(kSearchInfinity, score + delta);
                } else break;
            }
            if (stopped_) break;
            last_score = score;
            if (last_best == 0 && pv_length_[0] > 0) last_best = pv_table_[0][0];
            result.best_move = last_best;
            result.score = last_score;
            result.completed_depth = depth;
            result.nodes = nodes_;
            result.principal_variation.clear();
            for (int i = 0; i < pv_length_[0]; ++i)
                result.principal_variation.push_back(pv_table_[0][i]);
            if (limits_.info_callback) {
                SearchInfo info;
                info.depth = depth;
                info.score = last_score;
                info.nodes = nodes_;
                info.elapsed_ms = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - search_start_).count());
                info.principal_variation = result.principal_variation;
                try { limits_.info_callback(info); } catch (...) { /* UI callbacks must not abort search. */ }
            }
            if (result.best_move == 0) break;
            // Soft time is checked only between completed iterations. Hard time
            // remains active inside the tree, preserving the last stable result.
            if (soft_time_expired()) break;
        }
    }

    result.nodes = nodes_;
    result.stopped = stopped_;
    result.probcut_attempts = probcut_attempts_;
    result.probcut_cutoffs = probcut_cutoffs_;
    result.singular_tests = singular_tests_;
    result.singular_extensions = singular_extensions_;
    result.fv_calls = fv_calls_;
    result.fv_beta_cutoffs = fv_beta_cutoffs_;
    result.fv_quiet_checks = fv_quiet_checks_;
    result.fv_check_nodes = fv_check_nodes_;
    result.fv_evasions = fv_evasions_;
    result.iid_reductions = iid_reductions_;
    result.lmp_prunes = lmp_prunes_;
    result.history_prunes = history_prunes_;
    result.root_move_searches = root_move_searches_;
    result.root_lmr_reductions = root_lmr_reductions_;
    result.root_lmr_researches = root_lmr_researches_;
    result.aspiration_fail_lows = aspiration_fail_lows_;
    result.aspiration_fail_highs = aspiration_fail_highs_;
    result.time_limit_hit = time_limit_hit_;
    result.elapsed_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - search_start_).count());
    result.error = error_;
    if (result.best_move == 0 && result.completed_depth == 0 && result.error.empty()) {
        if (!root_moves.empty()) result.best_move = root_moves.front();
        if (root_moves.empty()) result.score = in_check(root, static_cast<Color>(root.side_to_move))
            ? -kSearchMate : 0;
    }
    return result;
}

} // namespace booot

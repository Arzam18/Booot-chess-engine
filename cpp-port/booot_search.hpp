#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "booot_eval.hpp"
#include "booot_hash.hpp"
#include "booot_move_order.hpp"

namespace booot {

inline constexpr int kSearchMate = 32700;
inline constexpr int kSearchInfinity = kSearchMate + 1;
inline constexpr int kSearchMaxPly = 127;
inline constexpr int kSearchWinScore = kSearchMate - kSearchMaxPly;
inline constexpr int kSearchNoEval = kSearchInfinity + 1;

enum class TTBound : std::uint8_t { None = 0, Lower = 1, Upper = 2, Exact = 3 };

struct TTEntry {
    std::uint64_t key = 0;
    Move move = 0;
    std::int32_t value = 0;
    std::int32_t static_eval = kSearchNoEval;
    std::int16_t depth = -128;
    TTBound bound = TTBound::None;
    std::uint8_t age = 0;
    bool pv = false;
    bool valid = false;
};

// Four-way clustered transposition table, following uHash's EntrySize=4
// bucket layout, age-aware replacement and mate-score normalization.
class TranspositionTable {
public:
    explicit TranspositionTable(std::size_t megabytes = 16);
    void clear() noexcept;
    void new_search() noexcept;
    const TTEntry* probe(std::uint64_t key) const noexcept;
    void store(std::uint64_t key, int value, int depth, TTBound bound,
               Move move, int static_eval, bool pv, int ply) noexcept;
    std::size_t bucket_count() const noexcept { return bucket_count_; }
    std::uint8_t age() const noexcept { return age_; }
    bool resize(std::size_t megabytes) noexcept;

private:
    std::vector<TTEntry> entries_;
    std::size_t bucket_count_ = 0;
    std::uint8_t age_ = 0;
};

struct SearchInfo {
    int depth = 0;
    int score = 0;
    std::uint64_t nodes = 0;
    std::uint64_t elapsed_ms = 0;
    std::vector<Move> principal_variation;
};

struct SearchLimits {
    int depth = 8;
    std::uint64_t node_limit = 0; // 0 = unlimited
    // Booot's original defaults are doIIR=false and doLMP=false.
    bool enable_iir = false;
    bool enable_lmp = false;
    // UCI/front-end time controls. Explicit soft/hard limits take precedence;
    // otherwise move_time_ms or time_left_ms/increment_ms are budgeted here.
    std::uint64_t move_time_ms = 0;
    std::uint64_t time_left_ms = 0;
    std::uint64_t increment_ms = 0;
    std::uint32_t moves_to_go = 0;
    std::uint64_t move_overhead_ms = 20;
    std::uint64_t soft_time_limit_ms = 0;
    std::uint64_t hard_time_limit_ms = 0;
    std::vector<std::uint64_t> previous_position_keys; // chronological, before root
    std::atomic<bool>* stop = nullptr;
    std::function<void(const SearchInfo&)> info_callback;
};

struct SearchResult {
    Move best_move = 0;
    int score = 0; // side-to-move centipawns, or Booot mate score
    int completed_depth = 0;
    std::uint64_t nodes = 0;
    bool stopped = false;
    std::vector<Move> principal_variation;
    std::uint64_t probcut_attempts = 0;
    std::uint64_t probcut_cutoffs = 0;
    std::uint64_t singular_tests = 0;
    std::uint64_t singular_extensions = 0;
    std::uint64_t fv_calls = 0;
    std::uint64_t fv_beta_cutoffs = 0;
    std::uint64_t fv_quiet_checks = 0;
    std::uint64_t fv_check_nodes = 0;
    std::uint64_t fv_evasions = 0;
    std::uint64_t iid_reductions = 0;
    std::uint64_t lmp_prunes = 0;
    std::uint64_t history_prunes = 0;
    std::uint64_t root_move_searches = 0;
    std::uint64_t root_lmr_reductions = 0;
    std::uint64_t root_lmr_researches = 0;
    std::uint64_t aspiration_fail_lows = 0;
    std::uint64_t aspiration_fail_highs = 0;
    std::uint64_t elapsed_ms = 0;
    std::uint64_t soft_time_budget_ms = 0;
    std::uint64_t hard_time_budget_ms = 0;
    bool time_limit_hit = false;
    std::string error;
};

// Optional evaluator hook is intended for deterministic tests and diagnostics.
// It must return a score from the current side-to-move perspective. Production
// construction should omit it and pass the original Booot NNUE NetworkSet.
using SearchEvalCallback = std::function<int(const Position&)>;

class SearchEngine {
public:
    explicit SearchEngine(NetworkSet networks, std::size_t hash_megabytes = 16);
    explicit SearchEngine(SearchEvalCallback diagnostic_evaluator,
                          std::size_t hash_megabytes = 16);

    SearchResult search(Position& root, NNUEState& root_nnue,
                        const SearchLimits& limits);
    void clear_hash() noexcept { tt_.clear(); }
    bool resize_hash(std::size_t megabytes) noexcept;
    void clear_history() {
        history_.clear();
        pawn_correction_.fill(0);
        white_nonpawn_correction_.fill(0);
        black_nonpawn_correction_.fill(0);
    }
    const TranspositionTable& transposition_table() const noexcept { return tt_; }

private:
    struct StackFrame {
        Move move = 0;
        int history_piece = Empty; // signed moving piece before move (uSort history index)
        int counter_piece = Empty; // signed piece on destination after move (countermove index)
        std::uint8_t history_channel = 0; // (was-in-check << 1) | capture
        int static_eval = -kSearchInfinity;
        int double_extensions = 0;
        bool valid = false;
    };

    int root_search(Position& position, NNUEState& nnue,
                    std::vector<Move>& root_moves, int depth,
                    int alpha, int beta, Move& best_move);
    int alpha_beta(Position& position, NNUEState& nnue, int depth,
                   int alpha, int beta, int ply, bool pv_node,
                   bool allow_null, Move excluded_move = 0,
                   bool cut_node = false);
    int fv_search(Position& position, NNUEState& nnue, int alpha,
                  int beta, int ply, int depth);
    int quiescence(Position& position, NNUEState& nnue, int alpha,
                   int beta, int ply, int qdepth);
    bool evaluate(const Position& position, const NNUEState& nnue,
                  int& score);
    bool make_child(Position& position, NNUEState& nnue, Move move,
                    Undo& board_undo, NNUEUndo& nnue_undo);
    void unmake_child(Position& position, NNUEState& nnue,
                      const Undo& board_undo, const NNUEUndo& nnue_undo) noexcept;
    bool make_null(Position& position, NNUEState& nnue,
                   Undo& board_undo, NNUEUndo& nnue_undo);
    void unmake_null(Position& position, NNUEState& nnue,
                     const Undo& board_undo, const NNUEUndo& nnue_undo) noexcept;
    bool should_stop(bool force_clock_check = false) noexcept;
    bool soft_time_expired() const noexcept;
    bool is_draw(const Position& position, int ply);
    MoveHistoryContext history_context(int ply) const noexcept;
    Move previous_counter_move(int ply, const Position& position) const noexcept;
    int scaled_eval(int score, const Position& position) const noexcept;
    void update_correction_history(const Position& position, int best_value,
                                   int static_eval, int depth, TTBound bound,
                                   Move best_move) noexcept;
    int lmr_reduction(int depth, int move_number, bool improving,
                      bool pv_node, bool tt_pv, bool hash_capture,
                      bool killer_or_counter, bool gives_check,
                      bool quiet_singular, int history_score) const noexcept;
    void update_pv(int ply, Move move);

    NetworkSet networks_{};
    SearchEvalCallback diagnostic_evaluator_{};
    bool use_nnue_ = true;
    TranspositionTable tt_;
    MoveHistory history_{};
    // uSort.PawnCorrHist/WNonPawnCorrHist/BNonPawnCorrHist:
    // two sides x 32768 hashed entries, signed 16-bit gravity cells.
    static constexpr std::size_t kCorrectionSize = 32768;
    std::array<std::int16_t, 2 * kCorrectionSize> pawn_correction_{};
    std::array<std::int16_t, 2 * kCorrectionSize> white_nonpawn_correction_{};
    std::array<std::int16_t, 2 * kCorrectionSize> black_nonpawn_correction_{};
    SearchLimits limits_{};
    std::uint64_t nodes_ = 0;
    bool stopped_ = false;
    std::string error_{};
    std::array<StackFrame, kSearchMaxPly + 8> stack_{};
    std::array<std::array<Move, kSearchMaxPly + 1>, kSearchMaxPly + 1> pv_table_{};
    std::array<int, kSearchMaxPly + 1> pv_length_{};
    std::vector<std::uint64_t> path_keys_;
    std::uint64_t probcut_attempts_ = 0;
    std::uint64_t probcut_cutoffs_ = 0;
    std::uint64_t singular_tests_ = 0;
    std::uint64_t singular_extensions_ = 0;
    std::uint64_t fv_calls_ = 0;
    std::uint64_t fv_beta_cutoffs_ = 0;
    std::uint64_t fv_quiet_checks_ = 0;
    std::uint64_t fv_check_nodes_ = 0;
    std::uint64_t fv_evasions_ = 0;
    std::uint64_t iid_reductions_ = 0;
    std::uint64_t lmp_prunes_ = 0;
    std::uint64_t history_prunes_ = 0;
    std::uint64_t root_move_searches_ = 0;
    std::uint64_t root_lmr_reductions_ = 0;
    std::uint64_t root_lmr_researches_ = 0;
    std::uint64_t aspiration_fail_lows_ = 0;
    std::uint64_t aspiration_fail_highs_ = 0;
    std::chrono::steady_clock::time_point search_start_{};
    std::chrono::steady_clock::time_point soft_deadline_{};
    std::chrono::steady_clock::time_point hard_deadline_{};
    bool has_soft_deadline_ = false;
    bool has_hard_deadline_ = false;
    bool time_limit_hit_ = false;
};

} // namespace booot

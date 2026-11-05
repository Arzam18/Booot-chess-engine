#include "booot_search.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace booot;

static Position fen(const char* text) {
    Position p;
    std::string error;
    if (!parse_fen(text, p, error)) {
        std::cerr << "FEN error: " << error << "\n";
        std::abort();
    }
    return p;
}

static int material_eval(const Position& p) {
    constexpr int values[7] = {0, 100, 300, 300, 500, 900, 0};
    int total = 0;
    for (int sq = 0; sq < 64; ++sq) {
        const int piece = p.board[sq];
        if (!piece) continue;
        const int type = piece > 0 ? piece : -piece;
        total += piece > 0 ? values[type] : -values[type];
    }
    return p.side_to_move == White ? total : -total;
}

static void test_tt_cluster_and_mate_normalization() {
    TranspositionTable tt(1);
    const std::uint64_t key = 0xABCDEF0123456789ULL;
    tt.store(key, kSearchMate - 7, 12, TTBound::Exact, 0x1234,
             42, true, 7);
    const TTEntry* hit = tt.probe(key);
    assert(hit && hit->valid && hit->move == 0x1234);
    assert(hit->bound == TTBound::Exact && hit->depth == 12 && hit->pv);
    const int decoded = value_from_tt(hit->value, 3,
                                      kSearchWinScore, kSearchInfinity);
    assert(decoded == kSearchMate - 3);
    tt.store(key + 1, 25, 4, TTBound::Lower, 0, -10, false, 0);
    assert(tt.probe(key + 1) != nullptr);
    // Same-age, much shallower non-exact writes preserve the deeper score.
    tt.store(key, 3, 2, TTBound::Upper, 0, 12, false, 0);
    hit = tt.probe(key);
    assert(hit && hit->depth == 12 && hit->move == 0x1234);
    tt.new_search();
    assert(tt.age() == 8);
    tt.clear();
    assert(tt.probe(key) == nullptr);
}

static void test_start_position_search_and_tt_reuse() {
    Position p = fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    NNUEState unused{};
    SearchEngine engine(material_eval, 2);
    SearchLimits limits;
    limits.depth = 4;
    const std::string root_fen = to_fen(p);
    SearchResult first = engine.search(p, unused, limits);
    assert(to_fen(p) == root_fen);
    assert(first.error.empty());
    assert(first.completed_depth == 4);
    assert(first.best_move != 0);
    assert(first.nodes > 0);
    assert(!first.principal_variation.empty());
    assert(first.principal_variation.front() == first.best_move);
    Position pv_position = p;
    for (Move pv_move : first.principal_variation) {
        std::vector<Move> pv_legal;
        generate_legal_moves(pv_position, pv_legal);
        bool pv_move_is_legal = false;
        for (Move legal_move : pv_legal) if (legal_move == pv_move) pv_move_is_legal = true;
        assert(pv_move_is_legal);
        Undo pv_undo;
        assert(make_move(pv_position, pv_move, pv_undo));
    }
    std::vector<Move> legal;
    generate_legal_moves(p, legal);
    bool found = false;
    for (Move m : legal) if (m == first.best_move) found = true;
    assert(found);

    MoveHistory all_quiet_history;
    MovePicker all_quiets(p, all_quiet_history, 0);
    std::set<Move> picked;
    while (auto move = all_quiets.next()) picked.insert(*move);
    std::vector<Move> all_legal;
    generate_legal_moves(p, all_legal);
    assert(picked.size() == all_legal.size());
    for (Move move : all_legal) assert(picked.count(move) == 1);

    SearchResult second = engine.search(p, unused, limits);
    assert(second.completed_depth == 4);
    assert(second.best_move != 0);
    assert(second.score == first.score);
}

static void test_mate_and_stalemate() {
    // White has a mate in one in this position.
    Position mate_in_one = fen("7k/5Q2/6K1/8/8/8/8/8 w - - 0 1");
    NNUEState unused{};
    SearchEngine engine(material_eval, 1);
    SearchLimits limits;
    limits.depth = 3;
    SearchResult mate = engine.search(mate_in_one, unused, limits);
    assert(mate.best_move != 0);
    assert(mate.score >= kSearchMate - 3);

    Position checkmated = fen("7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");
    SearchEngine mate_engine(material_eval, 1);
    SearchResult lost = mate_engine.search(checkmated, unused, limits);
    assert(lost.best_move == 0);
    assert(lost.score == -kSearchMate);

    Position stalemate = fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    assert(!in_check(stalemate, Black));
    SearchEngine stale_engine(material_eval, 1);
    SearchResult drawn = stale_engine.search(stalemate, unused, limits);
    assert(drawn.best_move == 0);
    assert(drawn.score == 0);
}


static void test_real_nnue_state_in_search() {
    Position p = fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    Network net{};
    net.model = 0;
    net.model_size = 772;
    net.scale_act = 1.0;
    net.scale_out = 100;
    net.w1 = 64;
    net.w2 = 4096;
    net.feature_weights.assign(static_cast<std::size_t>(net.model_size) * kHalf, 0);
    net.sigma = {37};
    net.max_sigma = 0;
    NetworkSet networks{};
    networks.by_slot.fill(&net);

    SearchEngine engine(networks, 1);
    NNUEState state{};
    SearchLimits limits;
    limits.depth = 2;
    SearchResult result = engine.search(p, state, limits);
    assert(result.error.empty());
    assert(result.completed_depth == 2);
    assert(result.best_move != 0);
    assert(state.valid && state.position_key == p.key);
    assert(position_is_consistent(p));
}

static void test_rule50_repetition_and_node_stop() {
    NNUEState unused{};
    Position fifty = fen("4k3/8/8/8/8/8/8/4K3 w - - 100 1");
    SearchEngine fifty_engine([](const Position&) { return 700; }, 1);
    SearchLimits limits;
    limits.depth = 2;
    SearchResult fifty_result = fifty_engine.search(fifty, unused, limits);
    assert(fifty_result.completed_depth == 1);
    assert(fifty_result.best_move == 0);
    assert(fifty_result.score == 0 || fifty_result.score == 1 || fifty_result.score == -1);

    Position repeated = fen("4k3/8/8/8/8/8/8/4K3 w - - 8 1");
    SearchEngine repetition_engine([](const Position&) { return 500; }, 1);
    SearchLimits repetition_limits;
    repetition_limits.depth = 2;
    repetition_limits.previous_position_keys = {repeated.key, 0x1234ULL};
    // The root position was seen two plies ago by the same side to move.
    SearchResult repeated_result = repetition_engine.search(repeated, unused, repetition_limits);
    assert(repeated_result.best_move == 0);
    assert(repeated_result.score == 0 || repeated_result.score == 1 || repeated_result.score == -1);

    Position start = fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    SearchEngine limited_engine(material_eval, 1);
    SearchLimits node_limits;
    node_limits.depth = 10;
    node_limits.node_limit = 50;
    SearchResult stopped = limited_engine.search(start, unused, node_limits);
    assert(stopped.stopped);
    assert(stopped.nodes >= 50);
    assert(stopped.best_move != 0); // retains last completed iteration or legal fallback
}


static void test_correction_history_lifecycle() {
    Position start = fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    NNUEState unused{};
    SearchEngine engine([](const Position& p) {
        // Deterministic side-to-move material score with a small pawn-structure
        // term, so repeated search stores and consumes correction entries.
        int score = 0;
        for (int sq = 0; sq < 64; ++sq) {
            const int piece = p.board[sq];
            if (piece == Empty) continue;
            const int value = std::abs(piece) == Pawn ? 100 :
                              std::abs(piece) == Knight ? 320 :
                              std::abs(piece) == Bishop ? 330 :
                              std::abs(piece) == Rook ? 500 :
                              std::abs(piece) == Queen ? 900 : 0;
            score += piece > 0 ? value : -value;
        }
        return p.side_to_move == White ? score : -score;
    }, 2);
    SearchLimits limits;
    limits.depth = 4;
    SearchResult first = engine.search(start, unused, limits);
    assert(first.error.empty() && first.completed_depth == 4 && first.best_move != 0);
    limits.depth = 5;
    SearchResult second = engine.search(start, unused, limits);
    assert(second.error.empty() && second.completed_depth == 5 && second.best_move != 0);
    engine.clear_history(); // Must clear move and all three correction histories.
    limits.depth = 4;
    SearchResult reset = engine.search(start, unused, limits);
    assert(reset.error.empty() && reset.completed_depth == 4 && reset.best_move != 0);
}

static void test_probcut_and_singular_search_paths() {
    Position start = fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    NNUEState unused{};
    SearchEngine engine(material_eval, 2);
    SearchLimits limits;
    limits.depth = 6;
    SearchResult first = engine.search(start, unused, limits);
    assert(first.error.empty());
    assert(first.completed_depth == 6);
    assert(first.best_move != 0);
    // The explicit root loop mirrors Pascal RootSearch, which has no root ProbCut.
    assert(position_is_consistent(start));

    // Balanced rooks with a defended hanging rook create a concrete tactical
    // ProbCut fail-high at depth nine. This checks the cutoff/TT-store branch,
    // not just that the ProbCut eligibility condition was reached.
    Position tactical = fen("r3r2k/8/8/8/4R3/8/8/R5K1 w - - 0 1");
    SearchEngine probcut_engine(material_eval, 1);
    limits.depth = 9;
    SearchResult probcut = probcut_engine.search(tactical, unused, limits);
    assert(probcut.error.empty());
    assert(probcut.completed_depth == 9);
    assert(probcut.best_move != 0);
    assert(probcut.probcut_attempts > 0);
    assert(probcut.probcut_cutoffs > 0);
    assert(position_is_consistent(tactical));

    // This checked position has exactly one legal evasion (Ka8-b8). The first
    // search stores it as the root TT move. On the second search, singular
    // verification excludes that move, finds no alternative, and extends it.
    Position forced = fen("k7/8/3N4/8/8/8/8/R3K3 b - - 0 1");
    std::vector<Move> evasions;
    generate_legal_moves(forced, evasions);
    assert(in_check(forced, Black));
    assert(evasions.size() == 1);
    SearchEngine singular_engine(material_eval, 1);
    limits.depth = 6;
    SearchResult forced_first = singular_engine.search(forced, unused, limits);
    assert(forced_first.error.empty());
    assert(forced_first.completed_depth == 6);
    assert(forced_first.best_move == evasions.front());
    SearchResult forced_second = singular_engine.search(forced, unused, limits);
    assert(forced_second.error.empty());
    assert(forced_second.completed_depth == 6);
    assert(forced_second.best_move == evasions.front());
    // Root singular extensions are intentionally absent: Pascal RootSearch is separate.
    assert(position_is_consistent(forced));
}

static void test_fv_nextfv_port() {
    NNUEState unused{};
    SearchLimits limits;
    limits.depth = 1;

    // Black has a king against a white queen. After Black's legal king move,
    // White's depth-zero FV must include quiet checking moves, not captures only.
    Position quiet_check_position = fen("4k3/8/8/8/8/8/3Q4/4K3 b - - 0 1");
    SearchEngine quiet_check_engine(material_eval, 1);
    SearchResult quiet_checks = quiet_check_engine.search(
        quiet_check_position, unused, limits);
    assert(quiet_checks.error.empty());
    assert(quiet_checks.completed_depth == 1);
    assert(quiet_checks.best_move != 0);
    assert(quiet_checks.fv_calls > 0);
    assert(quiet_checks.fv_quiet_checks > 0);
    assert(quiet_checks.fv_check_nodes > 0);
    assert(quiet_checks.fv_evasions > 0);
    assert(quiet_checks.fv_beta_cutoffs > 0);
    assert(position_is_consistent(quiet_check_position));

    // A forced check-evasion line ensures the FV move generator can enter its
    // in-check stage and enumerate legal evasions after a checking move.
    Position evasion_position = fen("k7/8/3N4/8/8/8/8/R3K3 b - - 0 1");
    std::vector<Move> legal_evasions;
    generate_legal_moves(evasion_position, legal_evasions);
    assert(in_check(evasion_position, Black));
    assert(legal_evasions.size() == 1);
    SearchEngine evasion_engine(material_eval, 1);
    SearchResult evasions = evasion_engine.search(evasion_position, unused, limits);
    assert(evasions.error.empty());
    assert(evasions.completed_depth == 1);
    assert(evasions.best_move == legal_evasions.front());
    assert(evasions.fv_check_nodes > 0);
    assert(evasions.fv_evasions > 0);
    assert(position_is_consistent(evasion_position));
}


static void test_stage13_iir_lmp_and_history_controls() {
    Position start = fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    NNUEState unused{};

    SearchEngine default_engine(material_eval, 2);
    SearchLimits defaults;
    defaults.depth = 5;
    SearchResult ordinary = default_engine.search(start, unused, defaults);
    assert(ordinary.error.empty());
    assert(ordinary.completed_depth == 5);
    // uSearch defaults: doIIR=false and doLMP=false.
    assert(ordinary.iid_reductions == 0);
    assert(ordinary.lmp_prunes == 0);

    SearchEngine* iid_engine_ptr = nullptr;
    SearchEngine iid_engine([&](const Position& position) {
        // Empty the TT during the diagnostic run so the depth>=3 nodes
        // deterministically exercise IIR's missing-hash-move path.
        if (iid_engine_ptr) iid_engine_ptr->clear_hash();
        return material_eval(position);
    }, 2);
    iid_engine_ptr = &iid_engine;
    SearchLimits iid_limits;
    iid_limits.depth = 5;
    iid_limits.enable_iir = true;
    SearchResult iid = iid_engine.search(start, unused, iid_limits);
    assert(iid.error.empty());
    assert(iid.completed_depth == 5);
    assert(iid.best_move != 0);
    assert(iid.iid_reductions > 0);

    SearchEngine lmp_engine(material_eval, 2);
    SearchLimits lmp_limits;
    lmp_limits.depth = 7;
    lmp_limits.enable_lmp = true;
    SearchResult lmp = lmp_engine.search(start, unused, lmp_limits);
    assert(lmp.error.empty());
    assert(lmp.completed_depth == 7);
    assert(lmp.best_move != 0);
    assert(lmp.lmp_prunes > 0);

    // Validate the two previous-ply continuation channels used by history
    // pruning, including the same signed-piece/destination indexing as search.
    MoveHistory history;
    const int piece = Pawn;
    const Square from = square_from_string("e2");
    const Square to = square_from_string("e4");
    const Move move = encode_move(from, to);
    PreviousMoveContext previous1{Knight, square_from_string("f3"), true, 0};
    PreviousMoveContext previous2{Bishop, square_from_string("c4"), true, 2};
    history.set_continuation(previous1.channel, previous1.piece, previous1.to,
                             piece, to, -120);
    history.set_continuation(previous2.channel, previous2.piece, previous2.to,
                             piece, to, -80);
    assert(history.continuation_score(previous1, piece, to) == -120);
    assert(history.continuation_score(previous2, piece, to) == -80);
    assert(move_from(move) == from && move_to(move) == to);
}


static void test_stage14_root_search_and_time_controls() {
    Position start = fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    const std::string root_fen = to_fen(start);
    NNUEState unused{};
    SearchEngine engine(material_eval, 2);
    SearchLimits limits;
    limits.depth = 5;
    SearchResult completed = engine.search(start, unused, limits);
    assert(completed.error.empty());
    assert(completed.completed_depth == 5);
    assert(completed.best_move != 0);
    assert(completed.root_move_searches >= 20);
    assert(!completed.principal_variation.empty());
    assert(completed.principal_variation.front() == completed.best_move);
    assert(completed.elapsed_ms <= 60000);
    assert(!completed.time_limit_hit);
    assert(to_fen(start) == root_fen);
    assert(position_is_consistent(start));

    std::vector<Move> legal;
    generate_legal_moves(start, legal);
    assert(std::find(legal.begin(), legal.end(), completed.best_move) != legal.end());

    // A deeper root iteration reaches the late quiet-move reduction zone from
    // Pascal RootSearch (depth >= 3, fourth and later quiet root candidates).
    SearchEngine root_lmr_engine(material_eval, 2);
    SearchLimits root_lmr_limits;
    root_lmr_limits.depth = 10;
    root_lmr_limits.node_limit = 200000;
    SearchResult root_lmr = root_lmr_engine.search(start, unused, root_lmr_limits);
    assert(root_lmr.error.empty());
    assert(root_lmr.completed_depth == 10);
    assert(root_lmr.root_lmr_reductions > 0);

    // Automatic movetime budgeting must enforce a hard deadline while retaining
    // a legal fallback if no complete iteration can finish in the budget.
    SearchEngine timed_engine(material_eval, 2);
    SearchLimits timed;
    timed.depth = 40;
    timed.move_time_ms = 1;
    timed.move_overhead_ms = 0;
    SearchResult timed_result = timed_engine.search(start, unused, timed);
    assert(timed_result.time_limit_hit);
    assert(timed_result.stopped);
    assert(timed_result.best_move != 0);
    assert(std::find(legal.begin(), legal.end(), timed_result.best_move) != legal.end());
    assert(timed_result.elapsed_ms >= 1);
    assert(to_fen(start) == root_fen);

    // An explicit hard limit wins over clock-derived defaults and an explicit
    // soft limit is observed only after a completed iteration.
    SearchEngine explicit_engine(material_eval, 2);
    SearchLimits explicit_limits;
    explicit_limits.depth = 40;
    explicit_limits.soft_time_limit_ms = 1;
    explicit_limits.hard_time_limit_ms = 100;
    SearchResult explicit_result = explicit_engine.search(start, unused, explicit_limits);
    assert(explicit_result.best_move != 0);
    assert(explicit_result.completed_depth >= 1);
    assert(!explicit_result.time_limit_hit);

    SearchEngine clock_engine(material_eval, 2);
    SearchLimits clock_limits;
    clock_limits.depth = 2;
    clock_limits.time_left_ms = 1000;
    clock_limits.increment_ms = 1000;
    clock_limits.moves_to_go = 10;
    clock_limits.move_overhead_ms = 50;
    SearchResult clock_result = clock_engine.search(start, unused, clock_limits);
    assert(clock_result.completed_depth == 2);
    assert(clock_result.soft_time_budget_ms == 845);
    assert(clock_result.hard_time_budget_ms == 950);
}

int main() {
    test_tt_cluster_and_mate_normalization();
    test_start_position_search_and_tt_reuse();
    test_mate_and_stalemate();
    test_real_nnue_state_in_search();
    test_rule50_repetition_and_node_stop();
    test_correction_history_lifecycle();
    test_probcut_and_singular_search_paths();
    test_fv_nextfv_port();
    test_stage13_iir_lmp_and_history_controls();
    test_stage14_root_search_and_time_controls();
    std::cout << "Booot Stage-14 search tests: PASS\n";
    return 0;
}

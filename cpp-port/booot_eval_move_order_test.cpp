#include "booot_eval.hpp"
#include "booot_move_order.hpp"
#include "booot_tactics.hpp"

#include <cassert>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace booot;

static Position fen(const char* text) {
    Position p;
    std::string error;
    assert(parse_fen(text, p, error) && error.empty());
    return p;
}

static Move uci(const Position& p, const char* text) {
    Move move = 0;
    std::string error;
    if (!parse_uci_move(p, text, move, error)) { std::cerr << "bad UCI " << text << " : " << error << " position=" << to_fen(p) << "\n"; std::abort(); }
    return move;
}

static void test_full_evaluation_dispatch() {
    EvaluationResult result;
    std::string error;
    NetworkSet no_networks{};
    NNUEState invalid_state{};

    // Special endgames must be evaluated before NNUE and therefore need no model.
    Position kpk = fen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
    assert(special_cases(kpk) == EndgameKPK);

    Position kings = fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    assert(special_cases(kings) == EndgameMaterialDraw);
    assert(evaluate_position(kings, no_networks, invalid_state, 29873, result, error));
    assert(!result.used_nnue && result.score == 0);

    Position kbnk = fen("7k/8/8/8/8/5N2/6B1/4K3 w - - 0 1");
    assert(special_cases(kbnk) == EndgameKBNK);
    assert(evaluate_position(kbnk, no_networks, invalid_state, 29873, result, error));
    assert(!result.used_nnue);

    // Ordinary start-position evaluation must use the current incremental NNUE state.
    Position start = fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
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
    NNUEState state;
    assert(refresh_nnue_state(start, networks, state, error));
    assert(evaluate_position(start, networks, state, 29873, result, error));
    assert(result.used_nnue && result.special_function == EndgameNone);
    assert(result.score == 37);
    assert(evaluate_position(start, networks, state, 25, result, error));
    assert(result.score == 25); // uEval's final WinScore clamp

    // Wrong-bishop rook-pawn scaling is applied after NNUE, sign-sensitively.
    Position kbps = fen("k7/P7/8/8/8/8/8/2B1K3 w - - 0 1");
    assert(special_cases(kbps) == EndgameKBPSWhite);
    NetworkSet kbps_networks{};
    kbps_networks.by_slot[0] = &net;
    NNUEState kbps_state;
    assert(refresh_nnue_state(kbps, kbps_networks, kbps_state, error));
    assert(evaluate_position(kbps, kbps_networks, kbps_state, 29873, result, error));
    assert(result.used_nnue && result.score == 0);

    // The evaluator rejects a stale incremental state for a non-special position.
    Position changed = start;
    Undo undo;
    assert(make_move(changed, uci(changed, "e2e4"), undo));
    assert(!evaluate_position(changed, networks, state, 29873, result, error));
    assert(!error.empty());
}

static void test_history_and_gravity() {
    MoveHistory history;
    Position start = fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    const Move e4 = uci(start, "e2e4");
    const int pawn = start.board[move_from(e4)];
    const Square to = move_to(e4);
    assert(history.quiet_history(pawn, to) == 0);
    history.update_quiet(start, e4, 1200, 5, {});
    assert(history.quiet_history(pawn, to) > 0);
    const int after_positive = history.quiet_history(pawn, to);
    history.update_quiet(start, e4, -1200, 5, {});
    assert(history.quiet_history(pawn, to) < after_positive);
    assert(MoveHistory::stat_bonus(0) == 0);
    assert(MoveHistory::stat_bonus(2) == 266);

    PreviousMoveContext previous{Pawn, square_from_string("e4"), true, 2};
    history.set_continuation(2, Pawn, square_from_string("e4"), pawn, to, 400);
    MoveHistoryContext context{};
    context.previous1 = previous;
    assert(history.quiet_score(start, e4, 6, context) >= history.quiet_history(pawn, to) + 800);

    history.update_killer_and_counter(start, e4, 4, 0);
    assert(history.killer(4, 0) == e4);

    Position after = start;
    Undo white_undo, black_undo;
    assert(make_move(after, e4, white_undo));
    const Move e5 = uci(after, "e7e5");
    assert(make_move(after, e5, black_undo));
    const Move response = uci(after, "g1f3");
    history.update_killer_and_counter(after, response, 5, e5);
    assert(history.killer(5, 0) == response);
    assert(history.counter_move(-Pawn, square_from_string("e5")) == response);
}

static void test_staged_move_picker() {
    Position start = fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    MoveHistory history;
    const Move hash = uci(start, "e2e4");
    const Move killer1 = uci(start, "g1f3");
    const Move killer2 = uci(start, "b1c3");
    const Move counter = uci(start, "d2d4");
    const Move best_quiet = uci(start, "g2g4");
    history.set_quiet_history(start.board[move_from(best_quiet)], move_to(best_quiet), 12000);

    MovePicker picker(start, history, 6, {}, hash, killer1, killer2, counter);
    assert(picker.next() == hash);
    assert(picker.next() == killer1);
    assert(picker.next() == killer2);
    assert(picker.next() == counter);
    assert(picker.next() == best_quiet);

    MovePicker skipped(start, history, 6, {}, hash, killer1, killer2, counter, true);
    assert(skipped.next() == hash);
    assert(skipped.next() == killer1);
    assert(skipped.next() == killer2);
    assert(skipped.next() == counter);
    assert(!skipped.next());

    // Captures/promotions precede killers and quiets.
    Position capture = fen("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1");
    MoveHistory empty_history;
    const Move e4d5 = uci(capture, "e4d5");
    empty_history.set_capture_history(Pawn, move_to(e4d5), Pawn, 320);
    assert(empty_history.capture_score(capture, e4d5) == 120); // MVV 100 + 320/16
    MovePicker capture_picker(capture, empty_history, 1);
    assert(capture_picker.next() == e4d5);

    // A queen capture defended by a pawn is deferred as a losing capture.
    Position losing_capture = fen("4k3/8/2p5/3p4/3Q4/8/8/4K3 w - - 0 1");
    const Move qxd5 = uci(losing_capture, "d4d5");
    assert(!good_see(qxd5, losing_capture, 0));
    MovePicker losing_picker(losing_capture, empty_history, 1);
    bool quiet_seen_before_bad = false;
    bool bad_capture_seen = false;
    while (auto next = losing_picker.next()) {
        if (*next == qxd5) {
            bad_capture_seen = true;
            assert(quiet_seen_before_bad);
            break;
        }
        if (!move_is_capture(*next) && !move_is_promotion(*next))
            quiet_seen_before_bad = true;
    }
    assert(bad_capture_seen);

    // In check, only legal evasions are returned; the hash move remains first if valid.
    Position checked = fen("4k3/8/8/8/8/8/4r3/4K3 w - - 0 1");
    assert(in_check(checked, White));
    std::vector<Move> expected;
    generate_legal_moves(checked, expected);
    MovePicker evasion_picker(checked, empty_history, 1);
    std::set<Move> actual_set;
    while (auto next = evasion_picker.next()) actual_set.insert(*next);
    assert(actual_set.size() == expected.size());
    for (Move move : expected) assert(actual_set.count(move) == 1);
}

int main() {
    test_full_evaluation_dispatch();
    test_history_and_gravity();
    test_staged_move_picker();
    std::cout << "Booot Stage-8 evaluation and move-ordering tests: PASS\n";
    return 0;
}

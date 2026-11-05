#include "booot_board.hpp"
#include "booot_nnue.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool same_accumulator(const booot::Accumulator& a, const booot::Accumulator& b) {
    for (int side = 0; side < 2; ++side)
        if (a.side[side] != b.side[side]) return false;
    return true;
}

bool same_state(const booot::NNUEState& a, const booot::NNUEState& b) {
    return a.valid == b.valid && a.network_slot == b.network_slot &&
           a.position_key == b.position_key &&
           same_accumulator(a.accumulator, b.accumulator);
}

booot::Network make_test_network(int model, int seed) {
    static constexpr int block_count[9] = {1, 3, 4, 6, 8, 10, 12, 14, 16};
    booot::Network net;
    net.model = model;
    net.model_size = block_count[model] * booot::kModelBlockSize;
    net.scale_act = 1.0;
    net.scale_out = 100;
    net.w1 = 64;
    net.w2 = 4096;
    net.feature_weights.resize(static_cast<std::size_t>(net.model_size) * booot::kHalf);
    for (std::size_t i = 0; i < net.feature_weights.size(); ++i) {
        const int value = static_cast<int>((i * 13 + static_cast<std::size_t>(seed * 17)) % 23) - 11;
        net.feature_weights[i] = static_cast<int16_t>(value);
    }
    for (int i = 0; i < booot::kHalf; ++i)
        net.feature_bias[i] = static_cast<int16_t>((i % 9) - 4);
    // Non-zero inference weights make the evaluation assertion meaningful.
    for (std::size_t i = 0; i < net.first_layer.size(); ++i)
        net.first_layer[i] = static_cast<int8_t>((static_cast<int>(i + seed) % 7) - 3);
    for (std::size_t i = 0; i < net.second_layer.size(); ++i)
        net.second_layer[i] = static_cast<int32_t>((i % 5) - 2) * 128;
    for (std::size_t i = 0; i < net.output_layer.size(); ++i)
        net.output_layer[i] = static_cast<int32_t>((i % 7) - 3) * 64;
    net.output_bias = seed * 13;
    net.max_sigma = 100;
    net.sigma.resize(101);
    for (int i = 0; i <= 100; ++i) net.sigma[i] = i * 3 - 150;
    return net;
}

void parse(const char* fen, booot::Position& position) {
    std::string error;
    assert(booot::parse_fen(fen, position, error) && error.empty());
}

void assert_increment_matches_rebuild(const booot::Position& after,
                                      const booot::NetworkSet& networks,
                                      const booot::NNUEState& incremental) {
    const int slot = booot::choose_network(booot::BoardView{
        .piece = after.board,
        .king_square = after.king_square,
        .castle_rights = after.castling_rights
    });
    assert(incremental.valid);
    assert(incremental.network_slot == slot);
    assert(incremental.position_key == after.key);
    booot::Accumulator rebuilt{};
    std::string error;
    assert(booot::refresh_accumulators(
        booot::BoardView{.piece = after.board,
                         .king_square = after.king_square,
                         .castle_rights = after.castling_rights},
        *networks.by_slot[slot], rebuilt, error));
    assert(same_accumulator(incremental.accumulator, rebuilt));
}

void test_every_legal_move(const char* fen, const booot::NetworkSet& networks,
                           bool expect_castle = false,
                           bool expect_ep = false,
                           bool expect_promotion = false) {
    booot::Position before;
    parse(fen, before);
    booot::NNUEState base;
    std::string error;
    assert(booot::refresh_nnue_state(before, networks, base, error));
    std::vector<booot::Move> moves;
    booot::generate_legal_moves(before, moves);
    assert(!moves.empty());

    bool saw_castle = false;
    bool saw_ep = false;
    bool saw_promotion = false;
    for (booot::Move move : moves) {
        booot::Position after = before;
        booot::Undo board_undo;
        assert(booot::make_move(after, move, board_undo, &error));
        const bool is_castle = std::abs(static_cast<int>(booot::move_to(move)) -
                                        static_cast<int>(booot::move_from(move))) == 2 &&
                               std::abs(static_cast<int>(before.board[booot::move_from(move)])) == booot::King;
        const bool is_ep = std::abs(static_cast<int>(before.board[booot::move_from(move)])) == booot::Pawn &&
                           booot::move_is_capture(move) &&
                           before.en_passant == booot::move_to(move) &&
                           before.board[booot::move_to(move)] == 0;
        saw_castle |= is_castle;
        saw_ep |= is_ep;
        saw_promotion |= booot::move_is_promotion(move);

        booot::NNUEState state = base;
        booot::NNUEUndo nnue_undo;
        assert(booot::update_nnue_state(before, after, networks, state, nnue_undo, error));
        assert_increment_matches_rebuild(after, networks, state);

        int32_t incremental_score = 0;
        assert(booot::evaluate_nnue_state(after, networks, state, incremental_score, error));
        booot::NNUEState full_state;
        assert(booot::refresh_nnue_state(after, networks, full_state, error));
        int32_t full_score = 0;
        assert(booot::evaluate_nnue_state(after, networks, full_state, full_score, error));
        assert(incremental_score == full_score);

        booot::unmake_nnue_state(state, nnue_undo);
        assert(same_state(state, base));
        booot::unmake_move(after, board_undo);
        assert(booot::to_fen(after) == booot::to_fen(before));
    }
    if (expect_castle) assert(saw_castle);
    if (expect_ep) assert(saw_ep);
    if (expect_promotion) assert(saw_promotion);
}

void test_null_move(const booot::NetworkSet& networks) {
    booot::Position before;
    parse("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", before);
    booot::NNUEState state;
    std::string error;
    assert(booot::refresh_nnue_state(before, networks, state, error));
    const booot::NNUEState original = state;
    booot::Position after = before;
    booot::Undo board_undo;
    booot::make_null_move(after, board_undo);
    booot::NNUEUndo nnue_undo;
    assert(booot::update_nnue_state(before, after, networks, state, nnue_undo, error));
    assert(same_accumulator(state.accumulator, original.accumulator));
    assert_increment_matches_rebuild(after, networks, state);
    booot::unmake_nnue_state(state, nnue_undo);
    booot::unmake_null_move(after, board_undo);
    assert(same_state(state, original));
    assert(booot::to_fen(after) == booot::to_fen(before));
}

void test_network_switch(const booot::NetworkSet& networks) {
    // Both sides begin with five non-pawn pieces. Qxd8 removes Black's fifth
    // non-pawn piece, changing Booot's selected network from slot 2 to slot 1.
    booot::Position before;
    parse("rnbqk2r/8/8/8/8/8/8/R1BQK2R w - - 0 1", before);
    assert(booot::choose_network(booot::BoardView{
        .piece = before.board, .king_square = before.king_square,
        .castle_rights = before.castling_rights}) == 2);
    booot::Move move{};
    std::string error;
    assert(booot::parse_uci_move(before, "d1d8", move, error));
    booot::Position after = before;
    booot::Undo board_undo;
    assert(booot::make_move(after, move, board_undo, &error));
    assert(booot::choose_network(booot::BoardView{
        .piece = after.board, .king_square = after.king_square,
        .castle_rights = after.castling_rights}) == 1);

    booot::NNUEState state;
    assert(booot::refresh_nnue_state(before, networks, state, error));
    assert(state.network_slot == 2);
    booot::NNUEUndo nnue_undo;
    assert(booot::update_nnue_state(before, after, networks, state, nnue_undo, error));
    assert(state.network_slot == 1);
    assert_increment_matches_rebuild(after, networks, state);
    booot::unmake_nnue_state(state, nnue_undo);
    booot::unmake_move(after, board_undo);
    assert(state.network_slot == 2);
}

void test_random_playout(const booot::NetworkSet& networks) {
    booot::Position position;
    parse("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", position);
    booot::NNUEState state;
    std::string error;
    assert(booot::refresh_nnue_state(position, networks, state, error));
    std::uint64_t rng = 0x9e3779b97f4a7c15ULL;
    int checked = 0;
    for (int ply = 0; ply < 600; ++ply) {
        std::vector<booot::Move> moves;
        booot::generate_legal_moves(position, moves);
        if (moves.empty()) {
            parse("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", position);
            assert(booot::refresh_nnue_state(position, networks, state, error));
            continue;
        }
        rng ^= rng << 7;
        rng ^= rng >> 9;
        rng ^= rng << 8;
        const booot::Move move = moves[static_cast<std::size_t>(rng % moves.size())];
        const booot::Position before = position;
        booot::Undo board_undo;
        assert(booot::make_move(position, move, board_undo, &error));
        booot::NNUEUndo nnue_undo;
        assert(booot::update_nnue_state(before, position, networks, state,
                                        nnue_undo, error));
        assert_increment_matches_rebuild(position, networks, state);
        if ((ply % 17) == 0) {
            const booot::NNUEState after_state = state;
            booot::unmake_nnue_state(state, nnue_undo);
            assert(state.position_key == before.key);
            booot::unmake_move(position, board_undo);
            assert(booot::to_fen(position) == booot::to_fen(before));
            // Re-apply the exact same move after proving undo symmetry.
            assert(booot::make_move(position, move, board_undo, &error));
            booot::NNUEUndo redo_undo;
            assert(booot::update_nnue_state(before, position, networks, state,
                                            redo_undo, error));
            assert(same_state(state, after_state));
        }
        ++checked;
    }
    assert(checked >= 500);
}


void test_combined_move_wrappers(const booot::NetworkSet& networks) {
    booot::Position position;
    parse("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", position);
    booot::NNUEState state;
    std::string error;
    assert(booot::refresh_nnue_state(position, networks, state, error));
    const booot::Position original_position = position;
    const booot::NNUEState original_state = state;

    std::vector<booot::Move> moves;
    booot::generate_legal_moves(position, moves);
    assert(!moves.empty());
    booot::Undo board_undo;
    booot::NNUEUndo nnue_undo;
    assert(booot::make_move_with_nnue(position, moves.front(), networks, state,
                                      board_undo, nnue_undo, error));
    assert_increment_matches_rebuild(position, networks, state);
    booot::unmake_move_with_nnue(position, board_undo, state, nnue_undo);
    assert(booot::to_fen(position) == booot::to_fen(original_position));
    assert(same_state(state, original_state));

    booot::Undo null_board_undo;
    booot::NNUEUndo null_nnue_undo;
    assert(booot::make_null_move_with_nnue(position, networks, state,
                                           null_board_undo, null_nnue_undo, error));
    assert_increment_matches_rebuild(position, networks, state);
    booot::unmake_null_move_with_nnue(position, null_board_undo, state,
                                      null_nnue_undo);
    assert(booot::to_fen(position) == booot::to_fen(original_position));
    assert(same_state(state, original_state));

    // A missing selected network must roll the board back and leave the
    // previously initialized NNUE state untouched.
    booot::NetworkSet incomplete{};
    booot::Undo failed_board_undo;
    booot::NNUEUndo failed_nnue_undo;
    assert(!booot::make_move_with_nnue(position, moves.front(), incomplete, state,
                                       failed_board_undo, failed_nnue_undo, error));
    assert(booot::to_fen(position) == booot::to_fen(original_position));
    assert(same_state(state, original_state));
}

void test_state_mismatch_rejected(const booot::NetworkSet& networks) {
    booot::Position position;
    parse("4k3/8/8/8/8/8/8/4K3 w - - 0 1", position);
    booot::NNUEState state;
    std::string error;
    assert(booot::refresh_nnue_state(position, networks, state, error));
    booot::Position other = position;
    booot::Undo undo;
    booot::make_null_move(other, undo);
    booot::NNUEUndo nnue_undo;
    assert(!booot::update_nnue_state(other, position, networks, state, nnue_undo, error));
    assert(!error.empty());
}

} // namespace

int main() {
    auto low_material = make_test_network(8, 1);
    auto medium_material = make_test_network(5, 2);
    auto high_material = make_test_network(3, 3);
    booot::NetworkSet networks{{&low_material, &medium_material, &high_material}};

    test_every_legal_move("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", networks);
    test_every_legal_move("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", networks, true);
    test_every_legal_move("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", networks, false, true);
    test_every_legal_move("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1", networks, false, false, true);
    test_every_legal_move("4k3/8/8/8/8/8/8/4K3 w - - 0 1", networks);
    test_null_move(networks);
    test_network_switch(networks);
    test_combined_move_wrappers(networks);
    test_random_playout(networks);
    test_state_mismatch_rejected(networks);

    std::cout << "Booot Stage 7 incremental NNUE tests: PASS\n";
    return 0;
}

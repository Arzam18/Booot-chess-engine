#include "booot_board.hpp"
#include "booot_move_order.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace booot;

static Position parse(const char* fen) {
    Position p;
    std::string error;
    assert(parse_fen(fen, p, error) && error.empty());
    return p;
}


static void test_unsigned_signed_dotprod_identity() {
    // SDOT accepts signed x signed bytes. Re-centering uint8 activations by
    // 128 and adding 128*sum(weights) must exactly reproduce uint8 x int8.
    std::array<std::uint8_t, 1024> input{};
    std::array<std::int8_t, 1024> weights{};
    std::uint32_t rng = 0x91E10DA5U;
    for (int test = 0; test < 32; ++test) {
        std::int64_t reference = 0;
        std::int64_t centered = 0;
        std::int64_t weight_sum = 0;
        for (std::size_t i = 0; i < input.size(); ++i) {
            rng = rng * 1664525U + 1013904223U;
            input[i] = static_cast<std::uint8_t>(rng >> 24);
            rng = rng * 1664525U + 1013904223U;
            weights[i] = static_cast<std::int8_t>(rng >> 24);
            reference += static_cast<std::int64_t>(input[i]) * weights[i];
            centered += (static_cast<int>(input[i]) - 128) *
                        static_cast<int>(weights[i]);
            weight_sum += weights[i];
        }
        assert(reference == centered + 128 * weight_sum);
    }
}

static void compare_move_buffers(const Position& p) {
    std::vector<Move> vector_pseudo;
    MoveList fixed_pseudo;
    generate_pseudo_legal_moves(p, vector_pseudo);
    generate_pseudo_legal_moves(p, fixed_pseudo);
    assert(vector_pseudo.size() == fixed_pseudo.size());
    assert(std::equal(vector_pseudo.begin(), vector_pseudo.end(), fixed_pseudo.begin()));

    std::vector<Move> vector_legal;
    MoveList fixed_legal;
    generate_legal_moves(p, vector_legal);
    generate_legal_moves(p, fixed_legal);
    assert(vector_legal.size() == fixed_legal.size());
    assert(std::equal(vector_legal.begin(), vector_legal.end(), fixed_legal.begin()));

    // Compact make/unmake must restore the entire position, hashes and all
    // cached material after every generated move, including special moves.
    for (Move move : vector_pseudo) {
        Position child = p;
        Undo undo;
        if (!make_move(child, move, undo)) continue;
        unmake_move(child, undo);
        std::string consistency;
        assert(position_is_consistent(child, &consistency));
        assert(to_fen(child) == to_fen(p));
        assert(child.key == p.key && child.pawn_key == p.pawn_key);
        assert(child.non_pawn_key == p.non_pawn_key);
        assert(child.non_pawn_material == p.non_pawn_material);
    }
}

static void compare_picker(const Position& p, bool checked) {
    MoveHistory history;
    MovePicker picker(p, history, 0);
    std::vector<Move> actual;
    while (auto move = picker.next()) actual.push_back(*move);

    std::vector<Move> expected;
    if (checked) generate_legal_moves(p, expected);
    else generate_pseudo_legal_moves(p, expected);
    std::sort(actual.begin(), actual.end());
    std::sort(expected.begin(), expected.end());
    assert(actual == expected);
    assert(std::adjacent_find(actual.begin(), actual.end()) == actual.end());
}

int main() {
    static_assert(sizeof(Undo) <= 128, "board undo must remain compact");
    static_assert(sizeof(MovePicker) <= 2304,
                  "MovePicker must not grow recursive search stack usage excessively");

    const Position positions[] = {
        parse("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
        parse("r3k2r/p1ppqpb1/bn2pnp1/2pP4/1p2P3/2N2N2/PPQBBPPP/R3K2R w KQkq - 0 1"),
        parse("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"),
        parse("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"),
        parse("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"),
        parse("4k3/P7/8/8/8/8/7p/4K3 w - - 0 1")
    };

    test_unsigned_signed_dotprod_identity();
    for (const Position& p : positions) compare_move_buffers(p);
    compare_picker(positions[0], false);
    compare_picker(positions[1], false);
    compare_picker(positions[3], false);

    const Position checked = parse("4k3/8/8/8/8/8/4r3/4K3 w - - 0 1");
    assert(in_check(checked, White));
    compare_picker(checked, true);

    std::cout << "Booot Stage 19 fixed-buffer / compact-undo tests: PASS\n";
}

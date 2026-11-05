#include "booot_hash.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

using namespace booot;

static Position load(const char* fen) {
    Position p;
    std::string error;
    assert(parse_fen(fen, p, error));
    return p;
}

static void check_hash(const Position& p) {
    const HashValues h = calculate_hashes(p);
    assert(p.key == h.key);
    assert(p.pawn_key == h.pawn_key);
    assert(p.non_pawn_key == h.non_pawn_key);
    assert(p.non_pawn_material == h.non_pawn_material);
    assert(hash_values_match(p));
}

int main() {
    constexpr const char* start_fen =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    Position start = load(start_fen);
    // Golden values independently calculated from every literal in uHash.pas.
    assert(start.key == 0x56484344680E1999ULL);
    assert(start.pawn_key == 0x2EA1D85E9EC1675CULL);
    assert(start.non_pawn_key[White] == 0x0BEC80507CDAA86BULL);
    assert(start.non_pawn_key[Black] == 0x6549DD81703A4BCEULL);
    assert(start.non_pawn_material[White] == 3770);
    assert(start.non_pawn_material[Black] == 3770);
    check_hash(start);

    Position kiwi = load("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    assert(kiwi.key == 0xE81ADA5760C047CBULL);
    assert(kiwi.pawn_key == 0x2162299C882783A4ULL);
    check_hash(kiwi);

    // Incremental key updates are checked after every move in a capture-rich line.
    Position p = load(start_fen);
    const char* line[] = {"e2e4", "d7d5", "e4d5", "d8d5", "b1c3", "d5e5", "d1e2"};
    Undo undos[7];
    Move moves[7]{};
    std::string error;
    for (int i = 0; i < 7; ++i) {
        assert(parse_uci_move(p, line[i], moves[i], error));
        assert(make_move(p, moves[i], undos[i], &error));
        check_hash(p);
        std::string consistency;
        assert(position_is_consistent(p, &consistency));
    }
    for (int i = 6; i >= 0; --i) {
        unmake_move(p, undos[i]);
        check_hash(p);
    }
    assert(to_fen(p) == start_fen);
    assert(p.key == start.key && p.pawn_key == start.pawn_key);

    // Special-move hash deltas: en-passant and castling.
    Position ep = load("rnbqkbnr/1pp1pppp/p7/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3");
    Move ep_move{};
    assert(parse_uci_move(ep, "e5d6", ep_move, error));
    Undo u;
    assert(make_move(ep, ep_move, u, &error));
    check_hash(ep);
    unmake_move(ep, u);
    check_hash(ep);

    // Capturing a non-pawn must update the opponent NonPawnKey and material.
    Position non_pawn_capture = load("4k3/8/8/4n3/4R3/8/8/4K3 w - - 0 1");
    const int black_material_before = non_pawn_capture.non_pawn_material[Black];
    Move rook_takes_knight{};
    assert(parse_uci_move(non_pawn_capture, "e4e5", rook_takes_knight, error));
    assert(make_move(non_pawn_capture, rook_takes_knight, u, &error));
    assert(non_pawn_capture.non_pawn_material[Black] == black_material_before - 370);
    check_hash(non_pawn_capture);
    unmake_move(non_pawn_capture, u);
    check_hash(non_pawn_capture);

    // Promotion removes the pawn feature and inserts the promoted piece key/material.
    Position promotion = load("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    Move promote_queen{};
    assert(parse_uci_move(promotion, "a7a8q", promote_queen, error));
    assert(make_move(promotion, promote_queen, u, &error));
    assert(promotion.non_pawn_material[White] == 1150);
    check_hash(promotion);
    unmake_move(promotion, u);
    assert(promotion.non_pawn_material[White] == 0);
    check_hash(promotion);

    Position castle = load("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    Move castle_move{};
    assert(parse_uci_move(castle, "e1g1", castle_move, error));
    assert(make_move(castle, castle_move, u, &error));
    check_hash(castle);
    unmake_move(castle, u);
    check_hash(castle);

    // Null move removes the en-passant component, toggles side, and restores exactly.
    Position null_position = load("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    const Position before_null = null_position;
    make_null_move(null_position, u);
    assert(null_position.en_passant == kNoSquare);
    assert(null_position.side_to_move == White);
    assert(null_position.halfmove_clock == before_null.halfmove_clock + 1);
    check_hash(null_position);
    unmake_null_move(null_position, u);
    assert(null_position.key == before_null.key);
    assert(to_fen(null_position) == to_fen(before_null));
    check_hash(null_position);

    // Mate-distance scores are normalized exactly like ValueToTT/ValueFromTT.
    constexpr int win_score = 29000;
    constexpr int infinity = 32000;
    assert(value_to_tt(30000, 6, win_score, infinity) == 30006);
    assert(value_from_tt(30006, 6, win_score, infinity) == 30000);
    assert(value_to_tt(-30000, 6, win_score, infinity) == -30006);
    assert(value_from_tt(-30006, 6, win_score, infinity) == -30000);
    assert(value_to_tt(-infinity, 6, win_score, infinity) == -infinity);
    assert(value_from_tt(-infinity, 6, win_score, infinity) == -infinity);

    std::cout << "Booot Zobrist/material/null-move stage-4 self-test: PASS\n";
    std::cout << "Exact Pascal constants + incremental move hashes: PASS\n";
    return 0;
}

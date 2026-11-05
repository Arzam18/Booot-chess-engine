#include "booot_board.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace booot;

static Position load(const char* fen) {
    Position p;
    std::string error;
    if (!parse_fen(fen, p, error)) {
        std::cerr << "FEN parse failed: " << error << "\n";
        std::abort();
    }
    return p;
}

static void expect_perft(const char* fen, int depth, std::uint64_t expected) {
    Position p = load(fen);
    const std::string before = to_fen(p);
    const auto got = perft(p, depth);
    if (got != expected) {
        std::cerr << "perft depth " << depth << ": expected " << expected
                  << ", got " << got << "\nFEN: " << fen << "\n";
        for (const auto& [move, nodes] : perft_divide(p, depth))
            std::cerr << move_to_uci(move) << ": " << nodes << "\n";
        std::abort();
    }
    assert(to_fen(p) == before);
    std::string consistency;
    assert(position_is_consistent(p, &consistency));
}

int main() {
    // Start position: canonical Chessprogramming perft sequence.
    constexpr const char* start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    expect_perft(start, 1, 20);
#if !defined(BOOOT_SANITIZER_TEST) && !defined(BOOOT_QUICK_REGRESSION)
    expect_perft(start, 2, 400);
    expect_perft(start, 3, 8902);
    expect_perft(start, 4, 197281);
    expect_perft(start, 5, 4865609);
#endif

    // Kiwipete: castling, pins, checks, and tactical move legality.
    constexpr const char* kiwipete = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    expect_perft(kiwipete, 1, 48);
#if !defined(BOOOT_SANITIZER_TEST) && !defined(BOOOT_QUICK_REGRESSION)
    expect_perft(kiwipete, 2, 2039);
    expect_perft(kiwipete, 3, 97862);
#endif

    // Position 3: en-passant, rook endgame checks, and promotions.
    constexpr const char* pos3 = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
    expect_perft(pos3, 1, 14);
#if !defined(BOOOT_SANITIZER_TEST) && !defined(BOOOT_QUICK_REGRESSION)
    expect_perft(pos3, 2, 191);
    expect_perft(pos3, 3, 2812);
    expect_perft(pos3, 4, 43238);
#endif

    // Additional canonical perft positions cover promotion-heavy and tactical nodes.
    constexpr const char* pos4 = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
    expect_perft(pos4, 1, 6);
#if !defined(BOOOT_SANITIZER_TEST) && !defined(BOOOT_QUICK_REGRESSION)
    expect_perft(pos4, 2, 264);
    expect_perft(pos4, 3, 9467);
#endif
    constexpr const char* pos5 = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";
    expect_perft(pos5, 1, 44);
#if !defined(BOOOT_SANITIZER_TEST) && !defined(BOOOT_QUICK_REGRESSION)
    expect_perft(pos5, 2, 1486);
    expect_perft(pos5, 3, 62379);
#endif
    constexpr const char* pos6 = "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10";
    expect_perft(pos6, 1, 46);
#if !defined(BOOOT_SANITIZER_TEST) && !defined(BOOOT_QUICK_REGRESSION)
    expect_perft(pos6, 2, 2079);
    expect_perft(pos6, 3, 89890);
#endif

    Position p = load(start);
    assert(to_fen(p) == start);
    Move e2e4{};
    std::string error;
    assert(parse_uci_move(p, "e2e4", e2e4, error));
    Undo undo;
    assert(make_move(p, e2e4, undo, &error));
    assert(to_fen(p) == "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    assert(position_is_consistent(p, &error));
    unmake_move(p, undo);
    assert(to_fen(p) == start);

    // Castling must move both king and rook and clear the side's rights.
    Position castle = load("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    Move e1g1{};
    assert(parse_uci_move(castle, "e1g1", e1g1, error));
    assert(make_move(castle, e1g1, undo, &error));
    assert(castle.board[6] == King && castle.board[5] == Rook);
    assert(!(castle.castling_rights & (kWhiteKingSide | kWhiteQueenSide)));
    assert(position_is_consistent(castle, &error));
    unmake_move(castle, undo);
    assert(castle.board[4] == King && castle.board[7] == Rook);

    // En-passant is represented by Booot's normal capture flag and FEN target square.
    Position ep = load("rnbqkbnr/1pp1pppp/p7/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3");
    Move e5d6{};
    assert(parse_uci_move(ep, "e5d6", e5d6, error));
    assert(move_is_capture(e5d6));
    assert(make_move(ep, e5d6, undo, &error));
    assert(ep.board[square_from_string("d5")] == Empty);
    assert(ep.board[square_from_string("d6")] == Pawn);
    assert(position_is_consistent(ep, &error));
    unmake_move(ep, undo);
    assert(ep.board[square_from_string("e5")] == Pawn);
    assert(ep.board[square_from_string("d5")] == -Pawn);

    // Promotion move list must encode four distinct piece choices in Booot order.
    Position promo = load("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    std::vector<Move> legal;
    generate_legal_moves(promo, legal);
    int promotions = 0;
    for (Move m : legal) if (move_is_promotion(m)) ++promotions;
    assert(promotions == 4);
    Move a7a8n{};
    assert(parse_uci_move(promo, "a7a8n", a7a8n, error));
    assert(make_move(promo, a7a8n, undo, &error));
    assert(promo.board[square_from_string("a8")] == Knight);
    unmake_move(promo, undo);
    assert(promo.board[square_from_string("a7")] == Pawn);

    Position invalid;
    assert(!parse_fen("8/8/8/8/8/8/8/4K3 w - - 0 1", invalid, error));

    std::cout << "Booot board/FEN/move/perft stage-3 self-test: PASS\n";
#if defined(BOOOT_SANITIZER_TEST)
    std::cout << "Sanitizer smoke-perft vectors: PASS\n";
#elif defined(BOOOT_QUICK_REGRESSION)
    std::cout << "Quick perft regression vectors: PASS\n";
#else
    std::cout << "Start position perft depth 5: 4,865,609\n";
    std::cout << "Kiwipete perft depth 3: 97,862\n";
    std::cout << "Position 3 perft depth 4: 43,238\n";
    std::cout << "Positions 4/5/6 reference perft: PASS\n";
#endif
    return 0;
}

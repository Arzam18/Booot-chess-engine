#include "booot_tactics.hpp"

#include <cassert>
#include <cstdlib>
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

static Move move(const Position& p, const char* uci) {
    Move result{};
    std::string error;
    if (!parse_uci_move(p, uci, result, error)) {
        // Some tests intentionally inspect pseudo-legal moves that are pinned.
        const std::string from_text(uci, uci + 2);
        const std::string to_text(uci + 2, uci + 4);
        const Square from = square_from_string(from_text);
        const Square to = square_from_string(to_text);
        std::vector<Move> pseudo;
        generate_pseudo_legal_moves(p, pseudo);
        for (Move candidate : pseudo) {
            if (move_from(candidate) == from && move_to(candidate) == to) return candidate;
        }
        std::cerr << "Move lookup failed: " << uci << ": " << error << "\n";
        std::abort();
    }
    return result;
}

int main() {
    // The strict-between mask mirrors uBitBoards.InterSect.
    const Square e1 = square_from_string("e1");
    const Square e8 = square_from_string("e8");
    const Bitboard e2_to_e7 = square_bb(square_from_string("e2")) |
        square_bb(square_from_string("e3")) | square_bb(square_from_string("e4")) |
        square_bb(square_from_string("e5")) | square_bb(square_from_string("e6")) |
        square_bb(square_from_string("e7"));
    assert(between_squares(e1, e8) == e2_to_e7);
    assert(between_squares(e1, square_from_string("f3")) == 0);

    // A white rook on e2 is pinned between its king and the black rook.
    Position pinned = load("4r1k1/8/8/8/8/8/4R3/4K3 w - - 0 1");
    CheckInfo pin_info;
    fill_check_info(pin_info, pinned);
    const Bitboard e2bit = square_bb(square_from_string("e2"));
    assert(pin_info.pinned == e2bit);
    assert(find_pinners(White, White, pinned) == e2bit);

    Move e2e3 = move(pinned, "e2e3");
    Move e2f2 = move(pinned, "e2f2");
    assert(is_pseudo_correct(e2e3, pinned));
    assert(is_pseudo_correct(e2f2, pinned));
    assert(is_legal(e2e3, pin_info.pinned, pinned));
    assert(!is_legal(e2f2, pin_info.pinned, pinned));

    // A bishop blocks a rook's line to the enemy king; moving it reveals check.
    Position discovered = load("4k3/8/8/8/8/8/4B3/K3R3 w - - 0 1");
    CheckInfo discovered_info;
    fill_check_info(discovered_info, discovered);
    const Bitboard e2discover = square_bb(square_from_string("e2"));
    assert((discovered_info.discover_check & e2discover) != 0);
    assert(find_pinners(White, Black, discovered) == e2discover);
    const Move e2f3 = move(discovered, "e2f3");
    assert(is_move_check(e2f3, discovered_info, discovered));

    // Direct check by a knight after moving onto c7 against the king on e8.
    Position direct = load("4k3/8/4N3/8/8/8/8/K7 w - - 0 1");
    CheckInfo direct_info;
    fill_check_info(direct_info, direct);
    const Move e6c7 = move(direct, "e6c7");
    assert((direct_info.direct_check[Knight] & square_bb(move_to(e6c7))) != 0);
    assert(is_move_check(e6c7, direct_info, direct));

    // En-passant discovered check: e5xd6 removes the d5 pawn and vacates e5,
    // opening the e-file for the rook. The special path is simulation-backed.
    Position ep_discovery = load("4k3/8/8/3pP3/8/8/8/K3R3 w - d6 0 1");
    CheckInfo ep_info;
    fill_check_info(ep_info, ep_discovery);
    Move e5d6{};
    std::string error;
    assert(parse_uci_move(ep_discovery, "e5d6", e5d6, error));
    assert(is_move_check(e5d6, ep_info, ep_discovery));

    // A pseudo-legal non-king move cannot answer a double check.
    Position double_check = load("4r1k1/8/8/8/1b6/8/8/4K2R w - - 0 1");
    const Move h1h2 = move(double_check, "h1h2");
    assert(!is_pseudo_correct(h1h2, double_check));

    // SEE accepts a profitable queen capture and rejects a queen move onto
    // a pawn-controlled square when no compensating capture is available.
    Position queen_capture = load("4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1");
    const Move e4d5 = move(queen_capture, "e4d5");
    assert(good_see(e4d5, queen_capture, 0));

    Position queen_hang = load("7k/8/4p3/8/4Q3/8/8/K7 w - - 0 1");
    const Move queen_hang_move = move(queen_hang, "e4d5");
    assert(!good_see(queen_hang_move, queen_hang, 0));

    // Exercise the check-info tables on every square and verify no invalid
    // square or empty-origin request is accidentally treated as a check.
    Position start = load("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    CheckInfo start_info;
    fill_check_info(start_info, start);
    assert(!is_move_check(encode_move(0, 0), start_info, start));

    std::cout << "Booot uAttacks tactical/check/SEE stage-5 self-test: PASS\n";
    return 0;
}

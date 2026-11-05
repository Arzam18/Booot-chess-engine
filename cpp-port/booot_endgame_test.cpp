#include "booot_endgame.hpp"
#include "booot_board.hpp"
#include "booot_bitboard.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace booot;

namespace {
int failures = 0;
void check(bool condition, const char* label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << '\n';
        ++failures;
    }
}
Position fen(const char* text) {
    Position p;
    std::string error;
    if (!parse_fen(text, p, error)) {
        std::cerr << "FEN parse failed: " << error << " :: " << text << '\n';
        std::exit(2);
    }
    return p;
}
}

int main() {
    std::cout << "Booot endgame/KPK tests\n";

    check(kpk_table_entries() == 2U * 24U * 64U * 64U,
          "KPK table has 2 x 24 x 64 x 64 entries");
    initialize_kpk();

    // The side to move is encoded separately in the probe. A position where
    // the black king can take an unprotected pawn is a draw for either turn.
    check(kpk_probe(Black, 8, 7, 16) == 0,
          "black king can capture an unprotected pawn");

    // Invalid king overlap and adjacent kings are normalized to a draw.
    check(kpk_probe(White, 8, 8, 16) == 0,
          "overlapping pawn/king state is a draw result");
    check(kpk_probe(White, 8, 0, 1) == 0,
          "adjacent kings invalid state is a draw result");

    // Horizontal and vertical normalization must preserve the theoretical
    // result, including side-to-move inversion for a black pawn.
    const int white_win = kpk_probe(White, 50, 42, 56);
    check(white_win > 0,
          "advanced protected pawn with active king is a theoretical win");
    check(kpk_probe(Black, 50, 42, 56) == -white_win,
          "KPK side-to-move sign convention");

    // Single-pawn positions select KPK and evaluate independently of NNUE.
    Position kpk_white = fen("8/4P3/8/4K3/8/8/8/4k3 w - - 0 1");
    check(special_cases(kpk_white) == EndgameKPK,
          "white KPK recognized by special-case dispatcher");
    Position kpk_black = fen("4K3/8/8/8/8/3k4/4p3/8 b - - 0 1");
    check(special_cases(kpk_black) == EndgameKPK,
          "black KPK recognized by special-case dispatcher");

    // Bare king versus two knights is a theoretical draw in Booot's dispatch.
    Position knnk = fen("8/8/8/8/8/8/NN6/4K1k1 w - - 0 1");
    check(special_cases(knnk) == EndgameMaterialDraw,
          "KNNK material draw recognized");
    check(evaluate_special_endgame(EndgameMaterialDraw, knnk) == 0,
          "material draw evaluates to zero");

    // K+B+N versus bare king selects the corner-driving special function.
    Position kbnk = fen("8/8/8/8/8/8/BN6/4K1k1 w - - 0 1");
    check(special_cases(kbnk) == EndgameKBNK,
          "KBNK recognized");
    check(evaluate_special_endgame(EndgameKBNK, kbnk) > 2500,
          "KBNK returns a near-mate winning score for stronger side");

    // Rook-pawn + bishop wrong-color corner fortress scale cases.
    Position kbpsw = fen("k7/8/8/8/8/8/P7/2B1K3 w - - 0 1");
    check(special_cases(kbpsw) == EndgameKBPSWhite,
          "white bishop-and-pawns endgame recognized");
    int white_scale = kScaleNormal;
    kbps_kw(white_scale, kbpsw);
    check(white_scale == kScaleDraw,
          "white a-pawn wrong-bishop corner draw scaling");

    Position kbpsb = fen("2b1k3/p7/8/8/8/8/8/K7 b - - 0 1");
    check(special_cases(kbpsb) == EndgameKBPSBlack,
          "black bishop-and-pawns endgame recognized");
    int black_scale = kScaleNormal;
    kbps_kb(black_scale, kbpsb);
    check(black_scale == kScaleDraw,
          "black a-pawn wrong-bishop corner draw scaling");

    // Scaling applies only when the NNUE score favors the pawn side.
    Position scaled = kbpsw;
    scaled.side_to_move = White;
    check(evaluate_with_endgame(scaled, 600, 10000) == 0,
          "white bishop-pawn draw scaling clamps a winning advantage to zero");
    check(evaluate_with_endgame(scaled, -600, 10000) == -600,
          "white bishop-pawn scale does not reduce a losing score");
    Position ordinary = fen("k7/8/8/8/8/8/P7/1NB1K3 w - - 0 1");
    check(evaluate_with_endgame(ordinary, 50000, 12000) == 12000,
          "ordinary NNUE score is clamped to WinScore");

    if (failures) {
        std::cerr << failures << " endgame test(s) failed\n";
        return 1;
    }
    std::cout << "All Booot endgame/KPK tests passed\n";
    return 0;
}

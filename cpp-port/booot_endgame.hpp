#pragma once

#include <cstddef>
#include <cstdint>

#include "booot_board.hpp"

namespace booot {

inline constexpr int kKpkMaxIndex = 24 * 64 * 64 - 1;
inline constexpr int kKpkWin = 800;
inline constexpr int kNearlyWin = 2500;
inline constexpr int kScaleNormal = 64;
inline constexpr int kScaleDraw = 0;

// Mirrors the endgame-function identifiers in uEndgame.pas.
enum EndgameFunction : int {
    EndgameNone = 0,
    EndgameMaterialDraw = 1,
    EndgameKBNK = 2,
    EndgameKBPSWhite = 3,
    EndgameKBPSBlack = 4,
    EndgameKPK = 5,
    EndgamePawnless = 6
};

// Initializes Booot's 24 x 64 x 64 KPK win/draw tables. Safe to call repeatedly
// and concurrently; the first call performs the retrograde analysis.
void initialize_kpk();

// Returns Booot's KPKProbe score convention: positive for a forced win by
// White in normalized orientation, negative for a forced win by Black, zero
// for draws/invalid states. `color` is the side to move.
int kpk_probe(Color color, Square pawn, Square white_king, Square black_king);

// Number of valid/allocated table entries, provided for diagnostics/tests.
std::size_t kpk_table_entries() noexcept;

int special_cases(const Position& position) noexcept;
int evaluate_special_endgame(int function, const Position& position);
void kbps_kw(int& white_scale, const Position& position) noexcept;
void kbps_kb(int& black_scale, const Position& position) noexcept;

// Apply uEval.pas's endgame dispatch, bishop-pawn scaling and WinScore clamp.
// `nnue_score` must already be from the side-to-move point of view.
int evaluate_with_endgame(const Position& position, int nnue_score,
                          int win_score);

} // namespace booot

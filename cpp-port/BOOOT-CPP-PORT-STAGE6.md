# Booot 7.4 C++ Port — Stage 6: KPK and endgames

This stage translates `uKPK.pas` and `uEndgame.pas` into C++20. It is integrated with the existing `Position`, bitboard, attack, and material-cache interfaces from Stages 1–5.

## New files

- `booot_endgame.hpp` — KPK and endgame public interface.
- `booot_endgame.cpp` — KPK table generation/probing, special-endgame selection, KBNK, pawnless evaluation, bishop+pawn rook-pawn scaling, and `uEval.pas`-style NNUE score integration.
- `booot_endgame_test.cpp` — KPK, dispatch, KBNK, material-draw, bishop+pawn scaling, score-clamp and side-to-move regression tests.

## KPK port details

- Uses the original 24 normalized pawn squares: a2–d7.
- Preserves the original `bk | (wk << 6) | (pawn_number << 12)` index.
- Preserves Booot's 8-bit win-rank and terminal markers: Draw=0, DeadDraw=253, StaleMate=254, Broken=255.
- Recreates the original KPK move generation and alternating black/white retrograde passes.
- Initializes lazily and thread-safely using `std::call_once`.
- `kpk_probe` is the low-level equivalent of Pascal `KPKProbe`; callers should provide normalized pawn files a–d. The higher-level `kpk_score` applies vertical and horizontal reflection, exactly as `uEndgame.pas` does.

## Endgame port details

- Preserves `SpecialCases` dispatch priority and the original identifiers.
- Ports KPK score conversion (`KPKWin - rank*5`), KBNK corner-driving tables, pawnless material/king-distance evaluation, and bishop+pawn rook-pawn wrong-bishop scaling.
- `evaluate_with_endgame(position, nnue_score, win_score)` reproduces `uEval.pas` dispatch and scaling. `nnue_score` must already be from the side-to-move perspective.
- The original Pascal expressions `TypOfPiese[Bishop]` and `TypOfPiese[Rook]` in `PawnLess` evaluate to 3 and 4. Although these thresholds look unusual, this port deliberately preserves them rather than silently changing Booot 7.4 behavior.

## Build and test

Requires C++20 and GCC or Clang:

```sh
./run_all_tests.sh
./run_sanitizer_tests.sh
```

The full Stage 1–6 host suite compiles from the archive root. Sanitizer coverage includes board/perft, hashing, tactics, and KPK/endgame tests.

## Current limitations

This completes the KPK/endgame evaluation subsystem, not the full chess engine. Search, move ordering, NNUE incremental updates, transposition-table management, repetition handling, threading, time management, UCI protocol, and benchmark are still outstanding. No Android executable is included or claimed by this stage.

Keep the original Pascal source, `booot2.nn`, `booot4.nn`, `booot58.nn`, and `resource.res` intact. The recovered archive contains the original Pascal source but not the separate NNUE model files from the user's fork.

# Booot 7.4 C++ Port — Stage 5: Tactical Attack Helpers

This stage ports the tactical/check helper layer from the original `uAttacks.pas` into `booot_tactics.cpp/.hpp`.

## New and updated files

- `booot_tactics.hpp` — CheckInfo structure and public helper interfaces.
- `booot_tactics.cpp` — pinner discovery, check-info construction, move-check detection, fast legality filtering, pseudo-move validation, and thresholded SEE.
- `booot_tactics_test.cpp` — regression tests for pins, discovered/direct checks, en-passant discovered check, double-check evasions, and SEE decisions.
- `run_all_tests.sh` — updated to run Stages 1–5.
- `run_sanitizer_tests.sh` — updated to include the Stage 5 module/tests.
- `build_stage5_selftest.sh` — focused Stage 5 test runner.

## Pascal-to-C++ mapping

| Original `uAttacks.pas` helper | C++ implementation |
|---|---|
| `KnightAttacksBB`, `KingAttacksBB` | `booot_attacks.cpp` |
| `BishopAttacksBB`, `RookAttacksBB`, `QueenAttacksBB` | `booot_attacks.cpp` |
| `PieseAttacksBB`, `SquareAttackedBB` | `piece_attacks`, `attackers_to` in `booot_attacks.cpp` |
| `ColorOf` | internal `color_of` helper |
| `isLegal` | `is_legal` |
| `FindPinners` | `find_pinners` |
| `isMoveCheck` | `is_move_check` |
| `SlowCheck` + `MoveisSpec` | folded into `is_pseudo_correct` special-move path |
| `isPseudoCorrect` | `is_pseudo_correct` |
| `FillCheckInfo` | `fill_check_info` |
| `GoodSee` | `good_see` |

## Behavior and implementation notes

- Bit numbering remains `a1 = 0` through `h8 = 63`.
- The `CheckInfo` members mirror the original `TCheckInfo`: discovered-check candidates, per-piece direct-check destination masks, pinned pieces, and enemy king square.
- `between_squares()` matches `uBitBoards.InterSect[][]`: it includes only squares strictly between two aligned endpoints.
- `find_pinners()` uses the original ray-candidate plus one-blocker test.
- `is_legal()` is the fast pinned-piece legality filter. As in the Pascal routine, it assumes the move has already passed pseudo-legal/evasion generation. King and en-passant moves use a make/check simulation for reliable occupancy handling.
- `is_pseudo_correct()` reproduces the original split: promotions, en-passant and castling are checked through the complete legal generator; ordinary check evasions are filtered by the checker mask and block/capture ray. Ordinary pinned moves are left for `is_legal()`.
- `is_move_check()` uses the original direct/discovered-check mask logic. Promotion, en-passant, and castling are handled with their special occupancy semantics.
- `good_see()` preserves Booot's SEE piece values (Pawn 100, Knight 300, Bishop 300, Rook 500, Queen 900, King 10000), its least-valuable-attacker sequence, x-ray revelation, and margin decision.
- `good_see()` intentionally preserves the source algorithm rather than replacing it with a different engine's SEE convention.

## Tests

Run from the repository root:

```sh
./run_all_tests.sh
./run_sanitizer_tests.sh
```

The complete host suite was run with both GCC and Clang 17. The ASan/UBSan suite passed with GCC.

Stage 5 tactical cases include:

- A rook pinned on the king file: moving on the line is accepted, moving off the line is rejected by `is_legal()`.
- A bishop screening a rook from the enemy king: moving the bishop reveals check.
- A knight move that directly checks the enemy king.
- En-passant that vacates a rook line and reveals check.
- A non-king move rejected while in double check.
- SEE accepting an undefended queen capture and rejecting a queen move onto a pawn attack.

## Status / limitations

Stage 5 ports the helper routines from `uAttacks.pas`; it does not yet port the search's specialized check/capture move generators in `uBoard.pas`. It also does not yet wire these helpers into a playable UCI search. The engine remains incomplete and no Android ARM64 executable is claimed.

Keep the original Pascal files, your three original `.nn` files, and your repository's `resource.res` unchanged. The recovered Pascal source backup contains `booot7.res` and `resource.rc`, but does not contain your external model files or the separate `resource.res` mentioned in the repository.

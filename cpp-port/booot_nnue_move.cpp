#include "booot_nnue.hpp"
#include "booot_board.hpp"

namespace booot {

bool make_move_with_nnue(Position& position, Move move,
                         const NetworkSet& networks, NNUEState& state,
                         Undo& board_undo, NNUEUndo& nnue_undo,
                         std::string& error) {
    const Position before = position;
    if (!make_move(position, move, board_undo, &error)) return false;
    if (!update_nnue_state(before, position, networks, state, nnue_undo, error)) {
        unmake_move(position, board_undo);
        return false;
    }
    error.clear();
    return true;
}

void unmake_move_with_nnue(Position& position, const Undo& board_undo,
                           NNUEState& state, const NNUEUndo& nnue_undo) noexcept {
    unmake_nnue_state(state, nnue_undo);
    unmake_move(position, board_undo);
}

bool make_null_move_with_nnue(Position& position, const NetworkSet& networks,
                              NNUEState& state, Undo& board_undo,
                              NNUEUndo& nnue_undo, std::string& error) {
    const Position before = position;
    make_null_move(position, board_undo);
    if (!update_nnue_state(before, position, networks, state, nnue_undo, error)) {
        unmake_null_move(position, board_undo);
        return false;
    }
    error.clear();
    return true;
}

void unmake_null_move_with_nnue(Position& position, const Undo& board_undo,
                                NNUEState& state, const NNUEUndo& nnue_undo) noexcept {
    unmake_nnue_state(state, nnue_undo);
    unmake_null_move(position, board_undo);
}

} // namespace booot

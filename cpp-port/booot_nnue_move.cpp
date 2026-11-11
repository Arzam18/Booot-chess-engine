#include "booot_nnue.hpp"
#include "booot_board.hpp"

namespace booot {

bool make_move_with_nnue(Position& position, Move move,
                         const NetworkSet& networks, NNUEState& state,
                         Undo& board_undo, NNUEUndo& nnue_undo,
                         std::string& error) {
    if (!make_move(position, move, board_undo, &error)) return false;
    if (!update_nnue_state_for_move(board_undo, position, networks, state,
                                    nnue_undo, error)) {
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
    (void)networks; // A null move changes side-to-move, not material or features.
    if (!state.valid || state.position_key != position.key) {
        error = "NNUE state does not match the null-move before-position";
        return false;
    }
    nnue_undo.previous = state;
    make_null_move(position, board_undo);
    // The forward pass selects accumulator order from side_to_move. Both
    // accumulators are unchanged by a null move; only their position key moves.
    state.position_key = position.key;
    state.valid = true;
    error.clear();
    return true;
}

void unmake_null_move_with_nnue(Position& position, const Undo& board_undo,
                                NNUEState& state, const NNUEUndo& nnue_undo) noexcept {
    unmake_nnue_state(state, nnue_undo);
    unmake_null_move(position, board_undo);
}

} // namespace booot

#pragma once

#include <cstdint>
#include <string>

#include "booot_nnue.hpp"
#include "booot_endgame.hpp"

namespace booot {

struct EvaluationResult {
    int score = 0;                 // Side-to-move point of view, like uEval.Evaluate.
    int special_function = EndgameNone;
    bool used_nnue = false;
};

// Full uEval.pas evaluation dispatch. Special endgames are handled before NNUE,
// matching Booot 7.4. For ordinary positions the caller supplies the incremental
// NNUE state associated with the current position.
bool evaluate_position(const Position& position, const NetworkSet& networks,
                       const NNUEState& nnue_state, int win_score,
                       EvaluationResult& result, std::string& error);

} // namespace booot

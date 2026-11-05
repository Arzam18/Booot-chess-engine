#include "booot_eval.hpp"

namespace booot {

bool evaluate_position(const Position& position, const NetworkSet& networks,
                       const NNUEState& nnue_state, int win_score,
                       EvaluationResult& result, std::string& error) {
    EvaluationResult next{};
    next.special_function = special_cases(position);

    // uEval.pas returns directly for these functions, without invoking NNUE.
    if (next.special_function == EndgameKBNK ||
        next.special_function == EndgameKPK ||
        next.special_function == EndgamePawnless ||
        next.special_function == EndgameMaterialDraw) {
        next.score = evaluate_special_endgame(next.special_function, position);
        next.used_nnue = false;
        result = next;
        error.clear();
        return true;
    }

    int32_t nnue_score = 0;
    if (!evaluate_nnue_state(position, networks, nnue_state, nnue_score, error))
        return false;

    // Includes the exact KBPSKw/KBPSKb sign-sensitive scaling and WinScore clamp
    // from uEval.pas. evaluate_with_endgame repeats SpecialCases intentionally so
    // its standalone Stage-6 API remains independently usable.
    next.score = evaluate_with_endgame(position, nnue_score, win_score);
    next.used_nnue = true;
    result = next;
    error.clear();
    return true;
}

} // namespace booot

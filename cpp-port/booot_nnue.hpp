#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>


namespace booot {

struct Position;
struct Undo;

inline constexpr int kMaxNetworks = 3;
inline constexpr int kHalf = 512;
inline constexpr int kHidden1 = 1024;
inline constexpr int kHidden2 = 8;
inline constexpr int kHidden3 = 32;
inline constexpr int kModelBlockSize = 64 * 12 + 2 + 2;
inline constexpr int kCurrentNetworkVersion = 35;

// Booot's original square numbering is a1=0, b1=1, ..., h8=63.
// A positive piece is white; a negative piece is black.
enum Piece : int8_t {
    Empty = 0, Pawn = 1, Knight = 2, Bishop = 3,
    Rook = 4, Queen = 5, King = 6
};

enum Color : int { White = 0, Black = 1 };

struct BoardView {
    std::array<int8_t, 64> piece{};
    std::array<int, 2> king_square{{-1, -1}};
    uint8_t castle_rights = 0; // bit 0 K, bit 1 Q, bit 2 k, bit 3 q
};

struct Network {
    int model = -1;
    double scale_act = 0.0;
    int scale_out = 0;
    int w1 = 0;
    int w2 = 0;
    int model_size = 0;
    std::vector<int16_t> feature_weights;
    std::array<int8_t, kHidden1 * kHidden2> first_layer{};
    // Per-output row sums let the AArch64 SDOT path transform unsigned
    // activations into signed bytes without recomputing the correction term.
    std::array<int32_t, kHidden2> first_layer_weight_sums{};
    std::array<int32_t, kHidden2 * kHidden3> second_layer{};
    std::array<int32_t, kHidden3> output_layer{};
    std::array<int16_t, kHalf> feature_bias{};
    std::array<int32_t, kHidden2> first_bias{};
    std::array<int32_t, kHidden3> second_bias{};
    int32_t output_bias = 0;
    std::vector<int32_t> sigma;
    int max_sigma = 0;
};

struct Accumulator {
    std::array<int16_t, kHalf> side[2]{};
};

// The three NNUE files selected by Booot's material-count policy. Slots 0, 1,
// and 2 correspond to booot2.nn, booot4.nn, and booot58.nn respectively; each
// Network carries its own feature-layout model index in Network::model.
struct NetworkSet {
    std::array<const Network*, kMaxNetworks> by_slot{};
};

// Search-node NNUE state. position_key ties the accumulators to the exact
// board node and catches accidental use with a different Position.
struct NNUEState {
    Accumulator accumulator{};
    int network_slot = -1;
    std::uint64_t position_key = 0;
    bool valid = false;
};

// Reference-safe undo snapshot. At 2 KiB this is deliberately simple; later
// search-stack optimization can replace it with compact feature deltas.
struct NNUEUndo {
    NNUEState previous{};
};

// Loads the exact version-35 Booot network binary format. No transformation is
// applied to network bytes; all integers are decoded as little-endian values.
bool load_network(const std::filesystem::path& path, Network& out,
                  std::string& error);
// Loads an unmodified version-35 network directly from bytes embedded in the
// executable. `data` must remain valid for the duration of this call only.
bool load_network_from_memory(const void* data, std::size_t size,
                              Network& out, std::string& error);

// Selects network 0/1/2 using Booot's original non-pawn, non-king material
// thresholds. Returns -1 only if the board representation is invalid.
int choose_network(const BoardView& board);

// Rebuilds both accumulators from the position using Booot's model/frame
// feature mapping. This is the safe reference path for initial integration.
bool refresh_accumulators(const BoardView& board, const Network& net,
                          Accumulator& acc, std::string& error);

// Exact scalar reference inference. The AArch64 implementation uses baseline
// NEON (no dot-product instruction requirement); other targets use scalar code.
int32_t forward_pass(int side_to_move, const Accumulator& acc,
                     const Network& net);
// Describes the active first-layer inference backend for UCI diagnostics.
const char* nnue_first_layer_backend() noexcept;

// Converts the raw network output into Booot's centipawn-like score table.
int32_t network_score(const Network& net, int32_t raw_output);

// Stage 7: initialize and incrementally maintain both perspective accumulators.
// The update accepts any board transition produced by make_move or
// make_null_move. Piece, king, rook (castling), capture, en-passant, promotion,
// and castling-right changes are derived from the before/after positions.
bool refresh_nnue_state(const Position& position, const NetworkSet& networks,
                        NNUEState& state, std::string& error);
bool update_nnue_state(const Position& before, const Position& after,
                       const NetworkSet& networks, NNUEState& state,
                       NNUEUndo& undo, std::string& error);
// Search hot path: applies the move deltas already captured by compact board
// Undo. It avoids copying the pre-move Position and scanning all 64 squares.
bool update_nnue_state_for_move(const Undo& board_undo, const Position& after,
                                const NetworkSet& networks, NNUEState& state,
                                NNUEUndo& undo, std::string& error);
void unmake_nnue_state(NNUEState& state, const NNUEUndo& undo) noexcept;

// Runs the selected network for the side to move and converts its raw output
// using the network's original Sigma lookup table.
bool evaluate_nnue_state(const Position& position, const NetworkSet& networks,
                         const NNUEState& state, int32_t& score,
                         std::string& error);

// Convenience wrappers that keep Position and NNUE state in lockstep. If the
// accumulator update fails, the board move is rolled back before returning.
bool make_move_with_nnue(Position& position, std::uint16_t move,
                         const NetworkSet& networks, NNUEState& state,
                         Undo& board_undo, NNUEUndo& nnue_undo,
                         std::string& error);
void unmake_move_with_nnue(Position& position, const Undo& board_undo,
                           NNUEState& state, const NNUEUndo& nnue_undo) noexcept;
bool make_null_move_with_nnue(Position& position, const NetworkSet& networks,
                              NNUEState& state, Undo& board_undo,
                              NNUEUndo& nnue_undo, std::string& error);
void unmake_null_move_with_nnue(Position& position, const Undo& board_undo,
                                NNUEState& state, const NNUEUndo& nnue_undo) noexcept;

} // namespace booot

#include "booot_nnue.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

static void put16(std::ofstream& f, uint16_t x) {
    const char b[2] = {static_cast<char>(x & 255), static_cast<char>(x >> 8)};
    f.write(b, 2);
}

static void write_zero_network(const std::filesystem::path& path) {
    constexpr int model_size = 772;
    std::ofstream f(path, std::ios::binary);
    assert(f);
    put16(f, 35);  // version
    put16(f, 100); // scale_act=1.00
    put16(f, 100); // scale_out
    put16(f, 0);   // model zero
    put16(f, 64);  // w1
    put16(f, 4096);// w2
    std::vector<int16_t> feature(static_cast<std::size_t>(model_size) * 512, 0);
    std::vector<int8_t> first(1024 * 8, 0);
    std::vector<int32_t> second(8 * 32, 0), output(32, 0), first_bias(8, 0), second_bias(32, 0);
    std::vector<int16_t> feature_bias(512, 0);
    f.write(reinterpret_cast<const char*>(feature.data()), feature.size() * sizeof(int16_t));
    f.write(reinterpret_cast<const char*>(first.data()), first.size());
    f.write(reinterpret_cast<const char*>(second.data()), second.size() * sizeof(int32_t));
    f.write(reinterpret_cast<const char*>(output.data()), output.size() * sizeof(int32_t));
    f.write(reinterpret_cast<const char*>(feature_bias.data()), feature_bias.size() * sizeof(int16_t));
    f.write(reinterpret_cast<const char*>(first_bias.data()), first_bias.size() * sizeof(int32_t));
    f.write(reinterpret_cast<const char*>(second_bias.data()), second_bias.size() * sizeof(int32_t));
    int32_t out_bias = 0;
    f.write(reinterpret_cast<const char*>(&out_bias), sizeof(out_bias));
    assert(f);
}

int main() {
    const auto path = std::filesystem::temp_directory_path() / "booot_nnue_zero_test.nn";
    write_zero_network(path);

    booot::Network net;
    std::string error;
    assert(booot::load_network(path, net, error) && error.empty());
    assert(net.model == 0 && net.model_size == 772);

    std::ifstream embedded_input(path, std::ios::binary);
    const std::vector<char> embedded_bytes(
        (std::istreambuf_iterator<char>(embedded_input)),
        std::istreambuf_iterator<char>());
    booot::Network embedded_net;
    assert(booot::load_network_from_memory(embedded_bytes.data(),
                                          embedded_bytes.size(),
                                          embedded_net, error));
    assert(embedded_net.model == net.model);
    assert(embedded_net.feature_weights == net.feature_weights);
    assert(net.feature_weights.size() == static_cast<std::size_t>(772) * 512);

    booot::BoardView board;
    board.piece[4] = booot::King;      // e1
    board.piece[60] = -booot::King;    // e8
    board.king_square = {4, 60};
    board.piece[0] = booot::Rook;
    board.piece[12] = booot::Pawn;
    board.piece[52] = -booot::Pawn;
    board.castle_rights = 15;

    assert(booot::choose_network(board) == 0);
    // The a1 white-rook feature is block 192 + square 0 for White,
    // and block 576 + (a1 xor 56) for Black's perspective.
    net.feature_weights[static_cast<std::size_t>(192) * 512] = 77;
    net.feature_weights[static_cast<std::size_t>(632) * 512] = -19;
    booot::Accumulator acc;
    assert(booot::refresh_accumulators(board, net, acc, error));
    assert(acc.side[booot::White][0] == 77);
    assert(acc.side[booot::Black][0] == -19);

    // Check the 1024 -> 8 -> 32 -> 1 path with a hand-computable vector.
    booot::Accumulator input{};
    input.side[booot::White][0] = 10;
    input.side[booot::Black][0] = 20;
    net.first_layer[0] = 64;
    net.first_layer[512] = 64;
    net.first_layer_weight_sums[0] = 128;
    net.second_layer[0] = 4096;
    net.output_layer[0] = 3;
    net.output_bias = 5;
    assert(booot::forward_pass(booot::White, input, net) == 95);
    assert(booot::network_score(net, 0) == -5664);
    assert(booot::network_score(net, 95) > 0);

    std::filesystem::remove(path);
    std::cout << "Booot NNUE stage-1 self-test: PASS\n";
    return 0;
}

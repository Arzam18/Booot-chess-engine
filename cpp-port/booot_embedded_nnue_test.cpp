#include "booot_nnue.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

#if !defined(BOOOT_EMBEDDED_NNUE)
#error "This test must be compiled with BOOOT_EMBEDDED_NNUE=1"
#endif

extern "C" {
extern const unsigned char booot2_nn_start[];
extern const unsigned char booot2_nn_end[];
extern const unsigned char booot4_nn_start[];
extern const unsigned char booot4_nn_end[];
extern const unsigned char booot58_nn_start[];
extern const unsigned char booot58_nn_end[];
}

int main() {
    struct Asset {
        const char* name;
        const unsigned char* begin;
        const unsigned char* end;
    };
    const std::array<Asset, 3> assets{{
        {"booot2.nn", booot2_nn_start, booot2_nn_end},
        {"booot4.nn", booot4_nn_start, booot4_nn_end},
        {"booot58.nn", booot58_nn_start, booot58_nn_end}
    }};

    for (const auto& asset : assets) {
        const auto begin = reinterpret_cast<std::uintptr_t>(asset.begin);
        const auto end = reinterpret_cast<std::uintptr_t>(asset.end);
        assert(end > begin);
        booot::Network network;
        std::string error;
        assert(booot::load_network_from_memory(
            asset.begin, static_cast<std::size_t>(end - begin), network, error));
        assert(network.model_size == 772);
        assert(network.feature_weights.size() == 772U * 512U);
        std::cout << asset.name << ": embedded and parsed (" << (end - begin)
                  << " bytes)\n";
    }
    std::cout << "Booot embedded NNUE memory test: PASS\n";
    return 0;
}

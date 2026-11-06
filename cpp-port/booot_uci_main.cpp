#include "booot_uci.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#if defined(BOOOT_EMBEDDED_NNUE)
extern "C" {
extern const unsigned char booot2_nn_start[];
extern const unsigned char booot2_nn_end[];
extern const unsigned char booot4_nn_start[];
extern const unsigned char booot4_nn_end[];
extern const unsigned char booot58_nn_start[];
extern const unsigned char booot58_nn_end[];
}
#endif

int main(int argc, char** argv) {
    std::filesystem::path network_dir = ".";
    std::size_t hash_mb = 16;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--network-dir" && i + 1 < argc) network_dir = argv[++i];
        else if (arg == "--hash" && i + 1 < argc) {
            try { hash_mb = static_cast<std::size_t>(std::stoul(argv[++i])); }
            catch (...) { std::cerr << "Invalid --hash value\n"; return 2; }
            if (hash_mb < 1 || hash_mb > 1024) {
                std::cerr << "--hash must be between 1 and 1024 MB\n";
                return 2;
            }
        } else if (arg == "--help" || arg == "-h") {
#if defined(BOOOT_EMBEDDED_NNUE)
            std::cout << "Booot 7.4 C++ UCI engine (three NNUE networks embedded)\n"
                         "Usage: booot-uci [--hash MB]\n";
#else
            std::cout << "Booot 7.4 C++ UCI engine\n"
                         "Usage: booot-uci [--network-dir DIR] [--hash MB]\n"
                         "Expected network files: booot2.nn, booot4.nn, booot58.nn\n";
#endif
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            return 2;
        }
    }

    constexpr std::array<const char*, 3> names{{"booot2.nn", "booot4.nn", "booot58.nn"}};
    std::array<booot::Network, 3> networks{};
    booot::NetworkSet network_set{};
    bool networks_ready = true;
    std::string network_error;

#if defined(BOOOT_EMBEDDED_NNUE)
    struct EmbeddedAsset {
        const unsigned char* begin;
        const unsigned char* end;
    };
    const std::array<EmbeddedAsset, 3> embedded{{
        {booot2_nn_start, booot2_nn_end},
        {booot4_nn_start, booot4_nn_end},
        {booot58_nn_start, booot58_nn_end}
    }};
    for (std::size_t i = 0; i < networks.size(); ++i) {
        const auto begin = reinterpret_cast<std::uintptr_t>(embedded[i].begin);
        const auto end = reinterpret_cast<std::uintptr_t>(embedded[i].end);
        if (end <= begin ||
            !booot::load_network_from_memory(embedded[i].begin,
                                             static_cast<std::size_t>(end - begin),
                                             networks[i], network_error)) {
            networks_ready = false;
            if (network_error.empty()) network_error = "invalid embedded byte range";
            std::cerr << "Booot embedded NNUE load failed for " << names[i]
                      << ": " << network_error << '\n';
            break;
        }
        network_set.by_slot[i] = &networks[i];
    }
#else
    for (std::size_t i = 0; i < networks.size(); ++i) {
        const auto path = network_dir / names[i];
        if (!booot::load_network(path, networks[i], network_error)) {
            networks_ready = false;
            std::cerr << "Booot NNUE load failed for " << path.string()
                      << ": " << network_error << '\n';
            break;
        }
        network_set.by_slot[i] = &networks[i];
    }
#endif

    if (!networks_ready) {
        std::cerr << "Booot UCI will remain available, but search is disabled until "
                     "all three original NNUE networks load successfully.\n";
        network_set = booot::NetworkSet{};
    }

    std::unique_ptr<booot::SearchEngine> engine;
    if (networks_ready) {
        engine = std::make_unique<booot::SearchEngine>(network_set, hash_mb);
    } else {
        engine = std::make_unique<booot::SearchEngine>(
            booot::SearchEvalCallback{}, hash_mb);
    }
    booot::UCIProtocol protocol(*engine, std::cin, std::cout, networks_ready);
    return protocol.run();
}

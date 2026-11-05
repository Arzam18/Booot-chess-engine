#include "booot_uci.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <string>

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
            std::cout << "Booot 7.4 C++ UCI engine\n"
                         "Usage: booot-uci [--network-dir DIR] [--hash MB]\n"
                         "Expected network files: booot2.nn, booot4.nn, booot58.nn\n";
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            return 2;
        }
    }

    constexpr std::array<const char*, 3> names{{"booot2.nn", "booot4.nn", "booot58.nn"}};
    std::array<booot::Network, 3> networks{};
    booot::NetworkSet network_set{};
    for (std::size_t i = 0; i < networks.size(); ++i) {
        const auto path = network_dir / names[i];
        std::string error;
        if (!booot::load_network(path, networks[i], error)) {
            std::cerr << "Booot NNUE load failed for " << path.string() << ": " << error << '\n';
            std::cerr << "Place the original Booot 7.4 network files in --network-dir. "
                         "The port does not generate substitute networks.\n";
            return 1;
        }
        network_set.by_slot[i] = &networks[i];
    }

    booot::SearchEngine engine(network_set, hash_mb);
    booot::UCIProtocol protocol(engine, std::cin, std::cout);
    return protocol.run();
}

#pragma once

#include <cstddef>
#include <iosfwd>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

#include "booot_search.hpp"

namespace booot {

// Synchronous command processor with an asynchronous search worker, as
// required by UCI's stop/isready/quit command semantics. The SearchEngine and
// any Network objects referenced by it must outlive this protocol instance.
class UCIProtocol {
public:
    UCIProtocol(SearchEngine& engine, std::istream& input, std::ostream& output);
    ~UCIProtocol();
    UCIProtocol(const UCIProtocol&) = delete;
    UCIProtocol& operator=(const UCIProtocol&) = delete;

    int run();
    const Position& position() const noexcept { return position_; }

private:
    void handle_line(const std::string& line, bool& quit);
    void handle_position(const std::string& line);
    void handle_go(const std::string& line);
    void handle_setoption(const std::string& line);
    void stop_and_join();
    void send_line(const std::string& line);
    void send_info_string(const std::string& message);

    SearchEngine& engine_;
    std::istream& input_;
    std::ostream& output_;
    mutable std::mutex output_mutex_;
    std::thread worker_;
    std::atomic<bool> stop_requested_{false};
    Position position_{};
    std::vector<std::uint64_t> previous_position_keys_;
    std::size_t hash_megabytes_ = 16;
    std::uint64_t move_overhead_ms_ = 20;
};

} // namespace booot

#include "booot_uci.hpp"
#include "booot_nnue.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string_view>
#include <utility>

namespace booot {
namespace {
constexpr const char* kStartFen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

std::vector<std::string> split_words(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> words;
    std::string word;
    while (in >> word) words.push_back(std::move(word));
    return words;
}

bool parse_u64(std::string_view text, std::uint64_t& value) {
    if (text.empty()) return false;
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
    value = parsed;
    return true;
}

std::string score_to_uci(int score) {
    if (score >= kSearchWinScore || score <= -kSearchWinScore) {
        const int plies = std::max(0, kSearchMate - std::abs(score));
        const int moves = (plies + 1) / 2;
        return "mate " + std::to_string(score < 0 ? -moves : moves);
    }
    return "cp " + std::to_string(score);
}

std::string pv_to_uci(const std::vector<Move>& pv) {
    std::string result;
    for (Move move : pv) {
        if (!result.empty()) result.push_back(' ');
        result += move_to_uci(move);
    }
    return result;
}
} // namespace

UCIProtocol::UCIProtocol(SearchEngine& engine, std::istream& input,
                         std::ostream& output, bool search_enabled)
    : engine_(engine), search_enabled_(search_enabled),
      input_(input), output_(output) {
    std::string error;
    if (!parse_fen(kStartFen, position_, error)) {
        // This constant is compiled into the engine; reaching this branch means
        // a programming error, but leave a valid default Position on failure.
        position_ = Position{};
    }
}

UCIProtocol::~UCIProtocol() { stop_and_join(); }

void UCIProtocol::send_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    output_ << line << '\n' << std::flush;
}

void UCIProtocol::send_info_string(const std::string& message) {
    send_line("info string " + message);
}

void UCIProtocol::stop_and_join() {
    stop_requested_.store(true, std::memory_order_relaxed);
    if (worker_.joinable()) worker_.join();
}

int UCIProtocol::run() {
    std::string line;
    while (std::getline(input_, line)) {
        bool quit = false;
        handle_line(line, quit);
        if (quit) break;
    }
    stop_and_join();
    return 0;
}

void UCIProtocol::handle_line(const std::string& line, bool& quit) {
    const auto words = split_words(line);
    if (words.empty()) return;
    const std::string& command = words[0];
    if (command == "uci") {
        send_line("id name Booot 7.4 C++ Port");
        send_line("id author Booot contributors / Mobile C++ port");
        send_line("option name Hash type spin default 16 min 1 max 1024");
        send_line("option name Threads type spin default 1 min 1 max 1");
        send_line("option name Move Overhead type spin default 20 min 0 max 5000");
        send_line("option name Clear Hash type button");
        send_line(std::string("info string NNUE first-layer kernel: ") +
                  nnue_first_layer_backend());
        send_line("uciok");
    } else if (command == "isready") {
        send_line("readyok");
    } else if (command == "ucinewgame") {
        stop_and_join();
        engine_.clear_hash();
        engine_.clear_history();
        previous_position_keys_.clear();
    } else if (command == "position") {
        handle_position(line);
    } else if (command == "go") {
        handle_go(line);
    } else if (command == "stop") {
        stop_and_join();
    } else if (command == "setoption") {
        handle_setoption(line);
    } else if (command == "quit") {
        stop_and_join();
        quit = true;
    } else if (command == "debug" || command == "register") {
        // These commands have no stateful effect in this single-thread engine.
    } else if (command == "ponderhit") {
        send_info_string("ponder-hit behavior is not supported in this build");
    } else {
        send_info_string("unknown command: " + command);
    }
}

void UCIProtocol::handle_position(const std::string& line) {
    stop_and_join();
    const auto words = split_words(line);
    if (words.size() < 2) { send_info_string("position requires startpos or fen"); return; }

    Position candidate{};
    std::vector<std::uint64_t> candidate_history;
    std::size_t index = 0;
    std::string error;
    if (words[1] == "startpos") {
        if (!parse_fen(kStartFen, candidate, error)) {
            send_info_string("internal start position error: " + error);
            return;
        }
        index = 2;
    } else if (words[1] == "fen") {
        if (words.size() < 8) { send_info_string("position fen requires six FEN fields"); return; }
        std::string fen_text;
        for (std::size_t i = 2; i < 8; ++i) {
            if (!fen_text.empty()) fen_text.push_back(' ');
            fen_text += words[i];
        }
        if (!parse_fen(fen_text, candidate, error)) {
            send_info_string("invalid FEN: " + error);
            return;
        }
        index = 8;
    } else {
        send_info_string("position expects startpos or fen");
        return;
    }

    if (index < words.size()) {
        if (words[index] != "moves") {
            send_info_string("unexpected token after position: " + words[index]);
            return;
        }
        ++index;
        for (; index < words.size(); ++index) {
            Move move = 0;
            if (!parse_uci_move(candidate, words[index], move, error)) {
                send_info_string("invalid position move " + words[index] + ": " + error);
                return;
            }
            candidate_history.push_back(candidate.key);
            Undo undo;
            if (!make_move(candidate, move, undo, &error)) {
                send_info_string("cannot apply position move " + words[index] + ": " + error);
                return;
            }
        }
    }
    position_ = candidate;
    previous_position_keys_ = std::move(candidate_history);
}

void UCIProtocol::handle_go(const std::string& line) {
    stop_and_join();
    if (!search_enabled_) {
        send_info_string("search disabled: original Booot 7.4 NNUE networks are not loaded");
        send_line("bestmove 0000");
        return;
    }
    const auto words = split_words(line);
    SearchLimits limits;
    limits.depth = kSearchMaxPly - 1;
    limits.move_overhead_ms = move_overhead_ms_;
    limits.previous_position_keys = previous_position_keys_;
    limits.stop = &stop_requested_;

    bool explicit_depth = false;
    bool infinite = false;
    bool reject_go = false;
    bool have_movetime = false, have_wtime = false, have_btime = false;
    std::uint64_t wtime = 0, btime = 0, winc = 0, binc = 0;
    for (std::size_t i = 1; i < words.size(); ++i) {
        const std::string& key = words[i];
        if (key == "infinite") { infinite = true; continue; }
        if (key == "ponder" || key == "searchmoves") {
            send_info_string(key + " is not supported in this build");
            reject_go = true;
            break;
        }
        if (i + 1 >= words.size()) {
            send_info_string("missing value for go parameter " + key);
            reject_go = true;
            break;
        }
        std::uint64_t value = 0;
        if (!parse_u64(words[i + 1], value)) {
            send_info_string("invalid numeric value for go parameter " + key);
            reject_go = true;
            break;
        }
        ++i;
        if (key == "depth") { limits.depth = static_cast<int>(std::clamp<std::uint64_t>(value, 1, kSearchMaxPly - 1)); explicit_depth = true; }
        else if (key == "nodes") limits.node_limit = value;
        else if (key == "movetime") { limits.move_time_ms = value; have_movetime = true; }
        else if (key == "wtime") { wtime = value; have_wtime = true; }
        else if (key == "btime") { btime = value; have_btime = true; }
        else if (key == "winc") winc = value;
        else if (key == "binc") binc = value;
        else if (key == "movestogo") limits.moves_to_go = static_cast<std::uint32_t>(std::min<std::uint64_t>(value, UINT32_MAX));
        else { send_info_string("ignoring unsupported go parameter: " + key); }
    }
    if (reject_go) {
        send_line("bestmove 0000");
        return;
    }
    if (!infinite && limits.move_time_ms == 0 && limits.node_limit == 0 &&
        wtime == 0 && btime == 0 && !explicit_depth) {
        // UCI's bare "go" means continue until stop; use the engine's maximum depth.
        limits.depth = kSearchMaxPly - 1;
    }
    const std::uint64_t side_time = position_.side_to_move == White ? wtime : btime;
    const std::uint64_t side_inc = position_.side_to_move == White ? winc : binc;
    const bool have_side_time = position_.side_to_move == White ? have_wtime : have_btime;
    limits.time_left_ms = side_time;
    limits.increment_ms = side_inc;
    // The search API uses zero as "no clock limit". UCI permits an explicit
    // zero clock/movetime, which must instead terminate essentially at once.
    if ((have_movetime && limits.move_time_ms == 0) ||
        (have_side_time && side_time == 0)) {
        limits.soft_time_limit_ms = 1;
        limits.hard_time_limit_ms = 1;
        limits.move_time_ms = 0;
        limits.time_left_ms = 0;
    }

    Position root = position_;
    std::vector<std::uint64_t> history = previous_position_keys_;
    stop_requested_.store(false, std::memory_order_relaxed);
    worker_ = std::thread([this, root = std::move(root), history = std::move(history), limits]() mutable {
        NNUEState nnue{};
        SearchLimits worker_limits = limits;
        worker_limits.previous_position_keys = std::move(history);
        worker_limits.info_callback = [this](const SearchInfo& info) {
            const std::uint64_t nps = info.nodes * 1000U / std::max<std::uint64_t>(1, info.elapsed_ms);
            std::string line = "info depth " + std::to_string(info.depth) +
                " score " + score_to_uci(info.score) +
                " nodes " + std::to_string(info.nodes) +
                " time " + std::to_string(info.elapsed_ms) +
                " nps " + std::to_string(nps);
            const std::string pv = pv_to_uci(info.principal_variation);
            if (!pv.empty()) line += " pv " + pv;
            send_line(line);
        };
        SearchResult result = engine_.search(root, nnue, worker_limits);
        if (!result.error.empty()) send_info_string(result.error);
        std::string best = "bestmove " + (result.best_move ? move_to_uci(result.best_move) : "0000");
        send_line(best);
    });
}

void UCIProtocol::handle_setoption(const std::string& line) {
    stop_and_join();
    const auto words = split_words(line);
    if (words.size() < 3 || words[1] != "name") {
        send_info_string("setoption syntax: setoption name <option> value <value>");
        return;
    }
    std::size_t value_pos = words.size();
    for (std::size_t i = 2; i < words.size(); ++i) {
        if (words[i] == "value") { value_pos = i; break; }
    }
    std::string name;
    for (std::size_t i = 2; i < value_pos; ++i) {
        if (!name.empty()) name.push_back(' ');
        name += words[i];
    }
    std::string value;
    if (value_pos < words.size()) {
        for (std::size_t i = value_pos + 1; i < words.size(); ++i) {
            if (!value.empty()) value.push_back(' ');
            value += words[i];
        }
    }
    if (name == "Hash") {
        std::uint64_t mb = 0;
        if (!parse_u64(value, mb) || mb < 1 || mb > 1024) {
            send_info_string("Hash must be an integer from 1 to 1024 MB");
            return;
        }
        if (!engine_.resize_hash(static_cast<std::size_t>(mb))) {
            send_info_string("unable to allocate requested Hash size");
            return;
        }
        hash_megabytes_ = static_cast<std::size_t>(mb);
    } else if (name == "Threads") {
        std::uint64_t threads = 0;
        if (!parse_u64(value, threads) || threads != 1)
            send_info_string("this Booot port currently supports exactly one search thread");
    } else if (name == "Move Overhead") {
        std::uint64_t ms = 0;
        if (!parse_u64(value, ms) || ms > 5000) {
            send_info_string("Move Overhead must be an integer from 0 to 5000 ms");
            return;
        }
        move_overhead_ms_ = ms;
    } else if (name == "Clear Hash") {
        engine_.clear_hash();
    } else {
        send_info_string("unknown option: " + name);
    }
}

} // namespace booot

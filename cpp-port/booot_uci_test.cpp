#include "booot_uci.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

using namespace booot;

class DelayedEofBuffer final : public std::stringbuf {
public:
    explicit DelayedEofBuffer(const std::string& text)
        : std::stringbuf(text), delayed_(false) {}
protected:
    int_type underflow() override {
        if (gptr() == egptr() && !delayed_) {
            delayed_ = true;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        return std::stringbuf::underflow();
    }
private:
    bool delayed_;
};

static int simple_eval(const Position& p) {
    constexpr int values[7] = {0, 100, 300, 320, 500, 900, 0};
    int score = 0;
    for (int sq = 0; sq < 64; ++sq) {
        const int piece = p.board[sq];
        if (!piece) continue;
        const int type = piece > 0 ? piece : -piece;
        score += piece > 0 ? values[type] : -values[type];
    }
    return p.side_to_move == White ? score : -score;
}

static void test_uci_handshake_position_and_search() {
    SearchEngine engine(simple_eval, 2);
    std::istringstream input(
        "uci\n"
        "isready\n"
        "setoption name Hash value 1\n"
        "setoption name Move Overhead value 0\n"
        "setoption name Threads value 1\n"
        "setoption name Clear Hash\n"
        "position startpos moves e2e4 e7e5 g1f3\n"
        "go depth 3\n");
    DelayedEofBuffer input_buffer(input.str());
    std::istream delayed_input(&input_buffer);
    std::ostringstream output;
    UCIProtocol protocol(engine, delayed_input, output);
    assert(protocol.run() == 0);
    const std::string text = output.str();
    assert(text.find("id name Booot 7.4 C++ Port") != std::string::npos);
    assert(text.find("option name Hash type spin") != std::string::npos);
    assert(text.find("uciok") != std::string::npos);
    assert(text.find("readyok") != std::string::npos);
    assert(text.find("info depth 1 score ") != std::string::npos);
    assert(text.find("info depth 3 score ") != std::string::npos);
    assert(text.find(" pv ") != std::string::npos);
    assert(text.find("bestmove ") != std::string::npos);
    assert(to_fen(protocol.position()) ==
        "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2");
}

static void test_fen_position_and_invalid_move_rejection() {
    SearchEngine engine(simple_eval, 1);
    std::istringstream input(
        "position fen 4k3/8/8/8/8/8/8/4K3 w - - 0 1 moves e1e2\n"
        "position startpos moves e2e5\n"
        "isready\n"
        "quit\n");
    std::ostringstream output;
    UCIProtocol protocol(engine, input, output);
    protocol.run();
    assert(to_fen(protocol.position()) == "4k3/8/8/8/8/8/4K3/8 b - - 1 1");
    assert(output.str().find("invalid position move e2e5:") != std::string::npos);
    assert(output.str().find("readyok") != std::string::npos);
}

static void test_unsupported_go_modes_are_rejected() {
    SearchEngine engine(simple_eval, 1);
    std::istringstream input(
        "go searchmoves e2e4\n"
        "go ponder\n"
        "go depth invalid\n"
        "quit\n");
    std::ostringstream output;
    UCIProtocol protocol(engine, input, output);
    protocol.run();
    const std::string text = output.str();
    assert(text.find("searchmoves is not supported") != std::string::npos);
    assert(text.find("ponder is not supported") != std::string::npos);
    assert(text.find("invalid numeric value for go parameter depth") != std::string::npos);
    assert(text.find("bestmove 0000") != std::string::npos);
}

static void test_stop_and_go_clock_commands() {
    SearchEngine engine(simple_eval, 1);
    std::istringstream input(
        "position startpos\n"
        "go infinite\n"
        "isready\n"
        "stop\n"
        "quit\n");
    std::ostringstream output;
    UCIProtocol protocol(engine, input, output);
    protocol.run();
    const std::string text = output.str();
    assert(text.find("readyok") != std::string::npos);
    assert(text.find("bestmove ") != std::string::npos);
}

int main() {
    std::cerr << "[UCI test 1] handshake and completed search\n";
    test_uci_handshake_position_and_search();
    std::cerr << "[UCI test 2] FEN and rejected move\n";
    test_fen_position_and_invalid_move_rejection();
    std::cerr << "[UCI test 3] unsupported modes\n";
    test_unsupported_go_modes_are_rejected();
    std::cerr << "[UCI test 4] asynchronous stop\n";
    test_stop_and_go_clock_commands();
    std::cout << "Booot Stage-15 UCI tests: PASS\n";
    return 0;
}

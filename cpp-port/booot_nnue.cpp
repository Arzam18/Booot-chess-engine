#include "booot_nnue.hpp"
#include "booot_board.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <streambuf>
#include <type_traits>

static_assert(std::endian::native == std::endian::little, "Booot NNUE files are little-endian");

#if defined(__aarch64__)
#include <arm_neon.h>
#if defined(__ANDROID__) || defined(__linux__)
#include <sys/auxv.h>
#ifndef HWCAP_ASIMDDP
#define HWCAP_ASIMDDP (1UL << 20)
#endif
#endif
#endif

namespace booot {
namespace {

constexpr std::array<bool, 9> kIsHorizontalMirror = {
    false, false, true, false, false, true, true, true, true
};
constexpr std::array<int, 13> kPieceBlock = {
    704, 640, 576, 512, 448, 384, 0, 0, 64, 128, 192, 256, 320
};
constexpr int kWhiteShortCastle = 768;
constexpr int kWhiteLongCastle  = 769;
constexpr int kBlackShortCastle = 770;
constexpr int kBlackLongCastle  = 771;

constexpr std::array<int, 64> kEdgeBlock = {
    0,1,2,3,3,2,1,0, 0,1,2,3,3,2,1,0,
    0,1,2,3,3,2,1,0, 0,1,2,3,3,2,1,0,
    0,1,2,3,3,2,1,0, 0,1,2,3,3,2,1,0,
    0,1,2,3,3,2,1,0, 0,1,2,3,3,2,1,0
};
constexpr std::array<int, 64> kK10Block = {
    0,1,2,3,3,2,1,0, 0,1,2,3,3,2,1,0,
    4,4,5,5,5,5,4,4, 4,4,5,5,5,5,4,4,
    6,6,7,7,7,7,6,6, 6,6,7,7,7,7,6,6,
    8,8,9,9,9,9,8,8, 8,8,9,9,9,9,8,8
};
constexpr std::array<int, 64> kK12Block = {
    0,1,2,3,3,2,1,0, 4,4,5,5,5,5,4,4,
    6,6,7,7,7,7,6,6, 6,6,7,7,7,7,6,6,
    8,8,9,9,9,9,8,8, 8,8,9,9,9,9,8,8,
    10,10,11,11,11,11,10,10, 10,10,11,11,11,11,10,10
};
constexpr std::array<int, 64> kK14Block = {
    0,1,2,3,3,2,1,0, 4,5,6,7,7,6,5,4,
    8,8,9,9,9,9,8,8, 8,8,9,9,9,9,8,8,
    10,10,11,11,11,11,10,10, 10,10,11,11,11,11,10,10,
    12,12,13,13,13,13,12,12, 12,12,13,13,13,13,12,12
};
constexpr std::array<int, 64> kK16Block = {
    0,1,2,3,3,2,1,0, 4,5,6,7,7,6,5,4,
    8,8,9,9,9,9,8,8, 10,10,11,11,11,11,10,10,
    12,12,13,13,13,13,12,12, 12,12,13,13,13,13,12,12,
    14,14,15,15,15,15,14,14, 14,14,15,15,15,15,14,14
};
constexpr std::array<int, 64> kKingMirror = {
    0,0,0,0,1,1,1,1, 0,0,0,0,1,1,1,1,
    0,0,0,0,1,1,1,1, 0,0,0,0,1,1,1,1,
    0,0,0,0,1,1,1,1, 0,0,0,0,1,1,1,1,
    0,0,0,0,1,1,1,1, 0,0,0,0,1,1,1,1
};
constexpr std::array<int, 64> kHorizontalMirror = {
    7,6,5,4,3,2,1,0, 15,14,13,12,11,10,9,8,
    23,22,21,20,19,18,17,16, 31,30,29,28,27,26,25,24,
    39,38,37,36,35,34,33,32, 47,46,45,44,43,42,41,40,
    55,54,53,52,51,50,49,48, 63,62,61,60,59,58,57,56
};

int model_frame_size(int model) {
    static constexpr int blocks[] = {1, 3, 4, 6, 8, 10, 12, 14, 16};
    if (model < 0 || model >= 9) return 0;
    return blocks[model] * kModelBlockSize;
}

int select_flank(int king_sq, bool extended) {
    const int file = king_sq & 7; // x=0..7, Pascal posx=1..8
    int result = (file < 3) ? 0 : ((file > 4) ? 2 : 1);
    if (extended && (king_sq >> 3) > 2) result += 3;
    return result;
}

int white_frame_index(int model, int king_sq) {
    switch (model) {
        case 0: return 0;
        case 1: return select_flank(king_sq, false) * kModelBlockSize;
        case 2: return kEdgeBlock[king_sq] * kModelBlockSize;
        case 3: return select_flank(king_sq, true) * kModelBlockSize;
        case 4: return (king_sq & 7) * kModelBlockSize;
        case 5: return kK10Block[king_sq] * kModelBlockSize;
        case 6: return kK12Block[king_sq] * kModelBlockSize;
        case 7: return kK14Block[king_sq] * kModelBlockSize;
        case 8: return kK16Block[king_sq] * kModelBlockSize;
        default: return 0;
    }
}

int black_frame_index(int model, int king_sq) {
    return white_frame_index(model, king_sq ^ 56);
}

int get_block_index(int piece, int square, int mirror) {
    if (piece < -King || piece > King) return -1;
    const int file_mirrored_square = (mirror == 1) ? square : kHorizontalMirror[square];
    return kPieceBlock[piece + King] + file_mirrored_square;
}

int white_mirror(int model, int king_sq) {
    return kIsHorizontalMirror[model] ? kKingMirror[king_sq] : 1;
}

int black_mirror(int model, int king_sq) {
    return kIsHorizontalMirror[model] ? kKingMirror[king_sq ^ 56] : 1;
}

uint16_t read_u16(std::istream& in, bool& ok) {
    uint8_t b[2]{};
    in.read(reinterpret_cast<char*>(b), 2);
    if (!in) { ok = false; return 0; }
    return static_cast<uint16_t>(b[0] | (static_cast<uint16_t>(b[1]) << 8));
}

bool read_exact(std::istream& in, void* dst, std::size_t bytes) {
    in.read(static_cast<char*>(dst), static_cast<std::streamsize>(bytes));
    return static_cast<std::size_t>(in.gcount()) == bytes;
}

int32_t wrap_i32(int64_t value) {
    const uint32_t low = static_cast<uint32_t>(static_cast<uint64_t>(value));
    return std::bit_cast<int32_t>(low);
}

int32_t add_wrap_i32(int32_t a, int32_t b) {
    return std::bit_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

int32_t arithmetic_shift_right(int32_t value, unsigned shift) {
    if (shift == 0) return value;
    if (value >= 0) return value >> shift;
    const int64_t magnitude = -static_cast<int64_t>(value);
    return static_cast<int32_t>(-((magnitude + ((int64_t{1} << shift) - 1)) >> shift));
}

int32_t clamp_u8(int32_t value) {
    return std::clamp(value, 0, 255);
}

#if defined(__aarch64__)
int32_t dot_u8_s8(const uint8_t* input, const int8_t* weights) {
    int32x4_t accum = vdupq_n_s32(0);
    for (int i = 0; i < kHidden1; i += 16) {
        const uint8x16_t in8 = vld1q_u8(input + i);
        const int8x16_t wt8 = vld1q_s8(weights + i);

        const int16x8_t in_lo = vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(in8)));
        const int16x8_t in_hi = vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(in8)));
        const int16x8_t wt_lo = vmovl_s8(vget_low_s8(wt8));
        const int16x8_t wt_hi = vmovl_s8(vget_high_s8(wt8));

        accum = vaddq_s32(accum, vpaddlq_s16(vmulq_s16(in_lo, wt_lo)));
        accum = vaddq_s32(accum, vpaddlq_s16(vmulq_s16(in_hi, wt_hi)));
    }
    return vaddvq_s32(accum);
}

// Optional ARMv8.2-A Dot Product kernel. The NNUE input activation is
// unsigned [0,255], whereas SDOT consumes signed bytes. XOR with 0x80 maps
// each activation to input-128; adding 128*sum(weights) restores the exact
// unsigned-by-signed dot product. The target attribute keeps SDOT isolated
// from the ARMv8-A baseline code, and HWCAP dispatch prevents illegal use on
// older AArch64 CPUs.
__attribute__((target("arch=armv8.2-a+dotprod")))
int32_t dot_u8_s8_dotprod(const uint8_t* input, const int8_t* weights,
                          int32_t weight_sum) {
    int32x4_t accum = vdupq_n_s32(0);
    const uint8x16_t sign_bias = vdupq_n_u8(0x80);
    for (int i = 0; i < kHidden1; i += 16) {
        const uint8x16_t input_u8 = vld1q_u8(input + i);
        const int8x16_t input_s8 = vreinterpretq_s8_u8(veorq_u8(input_u8, sign_bias));
        const int8x16_t weights_s8 = vld1q_s8(weights + i);
        __asm__ volatile("sdot %0.4s, %1.16b, %2.16b"
                         : "+w"(accum) : "w"(input_s8), "w"(weights_s8));
    }
    const int64_t corrected = static_cast<int64_t>(vaddvq_s32(accum)) +
                              128LL * static_cast<int64_t>(weight_sum);
    return wrap_i32(corrected);
}

bool cpu_has_dotprod() noexcept {
#if defined(__ANDROID__) || defined(__linux__)
    static const bool supported =
        (getauxval(AT_HWCAP) & static_cast<unsigned long>(HWCAP_ASIMDDP)) != 0;
    return supported;
#else
    return false;
#endif
}

int32_t dot_i32_i32_8(const int32_t* input, const int32_t* weights) {
    int32x4_t a = vdupq_n_s32(0);
    a = vaddq_s32(a, vmulq_s32(vld1q_s32(input), vld1q_s32(weights)));
    a = vaddq_s32(a, vmulq_s32(vld1q_s32(input + 4), vld1q_s32(weights + 4)));
    return vaddvq_s32(a);
}

int32_t dot_i32_i32_32(const int32_t* input, const int32_t* weights) {
    int32x4_t a = vdupq_n_s32(0);
    for (int i = 0; i < kHidden3; i += 4)
        a = vaddq_s32(a, vmulq_s32(vld1q_s32(input + i), vld1q_s32(weights + i)));
    return vaddvq_s32(a);
}
#else
int32_t dot_u8_s8(const uint8_t* input, const int8_t* weights) {
    int64_t accum = 0;
    for (int i = 0; i < kHidden1; ++i)
        accum += static_cast<int32_t>(input[i]) * static_cast<int32_t>(weights[i]);
    return wrap_i32(accum);
}

int32_t dot_i32_i32_8(const int32_t* input, const int32_t* weights) {
    int64_t accum = 0;
    for (int i = 0; i < kHidden2; ++i)
        accum += static_cast<int64_t>(input[i]) * weights[i];
    return wrap_i32(accum);
}

int32_t dot_i32_i32_32(const int32_t* input, const int32_t* weights) {
    int64_t accum = 0;
    for (int i = 0; i < kHidden3; ++i)
        accum += static_cast<int64_t>(input[i]) * weights[i];
    return wrap_i32(accum);
}
#endif

int32_t re_sigma(double y) {
    if (y < 0.000001) y = 0.000001;
    if (y > 0.999999) y = 0.999999;
    return static_cast<int32_t>(std::nearbyint(-410.0 * std::log((1.0 / y) - 1.0)));
}

bool add_feature(const Network& net, int frame, int piece, int square,
                 int mirror, std::array<int32_t, kHalf>& sum) {
    const int block = get_block_index(piece, square, mirror);
    if (block < 0) return false;
    const std::size_t start = static_cast<std::size_t>(frame + block) * kHalf;
    if (start + kHalf > net.feature_weights.size()) return false;
    const int16_t* w = net.feature_weights.data() + start;
    for (int i = 0; i < kHalf; ++i) sum[i] += w[i];
    return true;
}

bool build_one_accumulator(const BoardView& board, const Network& net,
                           int color, std::array<int16_t, kHalf>& out,
                           std::string& error) {
    const int white_king = board.king_square[White];
    const int black_king = board.king_square[Black];
    if (white_king < 0 || white_king >= 64 || black_king < 0 || black_king >= 64) {
        error = "BoardView must provide both king squares";
        return false;
    }

    const int frame = (color == White)
        ? white_frame_index(net.model, white_king)
        : black_frame_index(net.model, black_king);
    const int mirror = (color == White)
        ? white_mirror(net.model, white_king)
        : black_mirror(net.model, black_king);

    std::array<int32_t, kHalf> sum{};
    for (int i = 0; i < kHalf; ++i) sum[i] = net.feature_bias[i];

    auto add_piece = [&](int signed_piece, int square) -> bool {
        int p = signed_piece;
        int sq = square;
        if (color == Black) {
            p = -p;
            sq ^= 56;
        }
        return add_feature(net, frame, p, sq, mirror, sum);
    };

    // The two king features are always supplied in white-oriented form here;
    // add_piece applies Booot's sign/square inversion for Black's accumulator.
    if (!add_piece(+King, white_king) || !add_piece(-King, black_king)) {
        error = "King feature index exceeds this network's frame";
        return false;
    }

    for (int sq = 0; sq < 64; ++sq) {
        const int p = board.piece[sq];
        if (p == 0 || std::abs(p) == King) continue;
        if (!add_piece(p, sq)) {
            error = "Piece feature index exceeds this network's frame";
            return false;
        }
    }

    auto add_castle = [&](int feature) -> bool {
        const int block = frame + feature;
        const std::size_t start = static_cast<std::size_t>(block) * kHalf;
        if (start + kHalf > net.feature_weights.size()) return false;
        const int16_t* w = net.feature_weights.data() + start;
        for (int i = 0; i < kHalf; ++i) sum[i] += w[i];
        return true;
    };

    if (color == White) {
        if ((board.castle_rights & 1) && !add_castle(kWhiteShortCastle)) goto bad_castle;
        if ((board.castle_rights & 2) && !add_castle(kWhiteLongCastle)) goto bad_castle;
        if ((board.castle_rights & 4) && !add_castle(kBlackShortCastle)) goto bad_castle;
        if ((board.castle_rights & 8) && !add_castle(kBlackLongCastle)) goto bad_castle;
    } else {
        if ((board.castle_rights & 1) && !add_castle(kBlackShortCastle)) goto bad_castle;
        if ((board.castle_rights & 2) && !add_castle(kBlackLongCastle)) goto bad_castle;
        if ((board.castle_rights & 4) && !add_castle(kWhiteShortCastle)) goto bad_castle;
        if ((board.castle_rights & 8) && !add_castle(kWhiteLongCastle)) goto bad_castle;
    }

    for (int i = 0; i < kHalf; ++i) out[i] = static_cast<int16_t>(sum[i]);
    return true;

bad_castle:
    error = "Castling feature index exceeds this network's frame";
    return false;
}

} // namespace

namespace {

class EmbeddedMemoryBuffer final : public std::streambuf {
public:
    EmbeddedMemoryBuffer(const void* data, std::size_t size) {
        char* begin = const_cast<char*>(static_cast<const char*>(data));
        setg(begin, begin, begin + size);
    }
};

bool load_network_stream(std::istream& in, Network& out, std::string& error) {
    bool ok = true;
    Network net{};
    const int16_t version = static_cast<int16_t>(read_u16(in, ok));
    if (!ok) { error = "Cannot read network version"; return false; }
    if (version != kCurrentNetworkVersion) {
        error = "Wrong network version: file=" + std::to_string(version) +
                ", expected=" + std::to_string(kCurrentNetworkVersion);
        return false;
    }

    const int16_t scale_act_x100 = static_cast<int16_t>(read_u16(in, ok));
    const uint16_t scale_out = read_u16(in, ok);
    const uint16_t model = read_u16(in, ok);
    const uint16_t w1 = read_u16(in, ok);
    const uint16_t w2 = read_u16(in, ok);
    if (!ok) { error = "Truncated network header"; return false; }

    net.scale_act = static_cast<double>(scale_act_x100) / 100.0;
    net.scale_out = scale_out;
    net.model = model;
    net.w1 = w1;
    net.w2 = w2;
    net.model_size = model_frame_size(net.model);

    if (net.model_size == 0) { error = "Unknown NNUE model index"; return false; }
    if (net.scale_act <= 0.0 || net.scale_out <= 0) {
        error = "Invalid NNUE output scaling values";
        return false;
    }
    if (net.w1 != 64 && net.w1 != 128 && net.w1 != 256 && net.w1 != 512) {
        error = "Unsupported first-layer quantization shift (w1)";
        return false;
    }
    if (net.w2 != 4096 && net.w2 != 8192) {
        error = "Unsupported second-layer quantization shift (w2)";
        return false;
    }

    const std::size_t feature_count = static_cast<std::size_t>(net.model_size) * kHalf;
    net.feature_weights.resize(feature_count);
    if (!read_exact(in, net.feature_weights.data(), feature_count * sizeof(int16_t))) {
        error = "Truncated feature-weight section";
        return false;
    }
    if (!read_exact(in, net.first_layer.data(), net.first_layer.size())) {
        error = "Truncated first-layer weights";
        return false;
    }
    for (int row = 0; row < kHidden2; ++row) {
        int32_t sum = 0;
        const std::size_t row_start = static_cast<std::size_t>(row) * kHidden1;
        for (int column = 0; column < kHidden1; ++column)
            sum += net.first_layer[row_start + static_cast<std::size_t>(column)];
        net.first_layer_weight_sums[static_cast<std::size_t>(row)] = sum;
    }
    if (!read_exact(in, net.second_layer.data(), net.second_layer.size() * sizeof(int32_t))) {
        error = "Truncated second-layer weights";
        return false;
    }
    if (!read_exact(in, net.output_layer.data(), net.output_layer.size() * sizeof(int32_t))) {
        error = "Truncated output-layer weights";
        return false;
    }
    if (!read_exact(in, net.feature_bias.data(), net.feature_bias.size() * sizeof(int16_t))) {
        error = "Truncated feature biases";
        return false;
    }
    if (!read_exact(in, net.first_bias.data(), net.first_bias.size() * sizeof(int32_t))) {
        error = "Truncated first-layer biases";
        return false;
    }
    if (!read_exact(in, net.second_bias.data(), net.second_bias.size() * sizeof(int32_t))) {
        error = "Truncated second-layer biases";
        return false;
    }
    if (!read_exact(in, &net.output_bias, sizeof(net.output_bias))) {
        error = "Truncated output-layer bias";
        return false;
    }

    const int sigma_max = static_cast<int>(std::nearbyint(net.scale_act * net.scale_out));
    net.max_sigma = sigma_max;
    net.sigma.resize(static_cast<std::size_t>(sigma_max) + 1);
    for (int i = 0; i <= sigma_max; ++i) {
        const double y = (static_cast<double>(i) / net.scale_out) / net.scale_act;
        net.sigma[static_cast<std::size_t>(i)] = re_sigma(y);
    }

    out = std::move(net);
    error.clear();
    return true;
}

} // namespace

bool load_network(const std::filesystem::path& path, Network& out,
                  std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Cannot open neural network: " + path.string();
        return false;
    }
    return load_network_stream(in, out, error);
}

bool load_network_from_memory(const void* data, std::size_t size,
                              Network& out, std::string& error) {
    if (data == nullptr || size == 0) {
        error = "Embedded neural network is empty";
        return false;
    }
    EmbeddedMemoryBuffer buffer(data, size);
    std::istream in(&buffer);
    return load_network_stream(in, out, error);
}

int choose_network(const BoardView& board) {
    int non_pawn_king[2] = {0, 0};
    for (int p : board.piece) {
        const int kind = std::abs(p);
        if (kind >= Knight && kind <= Queen) {
            ++non_pawn_king[p > 0 ? White : Black];
        }
    }
    if (non_pawn_king[White] <= 2 && non_pawn_king[Black] <= 2) return 0;
    if (non_pawn_king[White] <= 4 && non_pawn_king[Black] <= 4) return 1;
    return 2;
}

bool refresh_accumulators(const BoardView& board, const Network& net,
                          Accumulator& acc, std::string& error) {
    if (!build_one_accumulator(board, net, White, acc.side[White], error)) return false;
    if (!build_one_accumulator(board, net, Black, acc.side[Black], error)) return false;
    error.clear();
    return true;
}

int32_t forward_pass(int side_to_move, const Accumulator& acc,
                     const Network& net) {
    std::array<uint8_t, kHidden1> inputs{};
    for (int i = 0; i < kHalf; ++i) {
        inputs[i] = static_cast<uint8_t>(clamp_u8(acc.side[side_to_move][i]));
        inputs[i + kHalf] = static_cast<uint8_t>(clamp_u8(acc.side[side_to_move ^ 1][i]));
    }

    std::array<int32_t, kHidden2> first_relu{};
    const unsigned shift1 = static_cast<unsigned>(std::countr_zero(static_cast<unsigned>(net.w1)));
#if defined(__aarch64__)
    const bool use_dotprod = cpu_has_dotprod();
#endif
    for (int j = 0; j < kHidden2; ++j) {
#if defined(__aarch64__)
        const std::size_t row_start = static_cast<std::size_t>(j) * kHidden1;
        const int32_t sum = use_dotprod
            ? dot_u8_s8_dotprod(inputs.data(), net.first_layer.data() + row_start,
                                net.first_layer_weight_sums[static_cast<std::size_t>(j)])
            : dot_u8_s8(inputs.data(), net.first_layer.data() + row_start);
#else
        const int32_t sum = dot_u8_s8(inputs.data(), net.first_layer.data() + j * kHidden1);
#endif
        const int32_t biased = add_wrap_i32(sum, net.first_bias[j]);
        first_relu[j] = clamp_u8(arithmetic_shift_right(biased, shift1));
    }

    std::array<int32_t, kHidden3> second_relu{};
    const unsigned shift2 = (net.w2 == 4096) ? 12u : 13u;
    for (int j = 0; j < kHidden3; ++j) {
        const int32_t sum = dot_i32_i32_8(first_relu.data(),
                                          net.second_layer.data() + j * kHidden2);
        const int32_t biased = add_wrap_i32(sum, net.second_bias[j]);
        second_relu[j] = clamp_u8(arithmetic_shift_right(biased, shift2));
    }

    return add_wrap_i32(dot_i32_i32_32(second_relu.data(), net.output_layer.data()),
                        net.output_bias);
}

const char* nnue_first_layer_backend() noexcept {
#if defined(__aarch64__)
    return cpu_has_dotprod() ? "AArch64 ARMv8.2-A SDOT" : "AArch64 NEON";
#else
    return "scalar reference";
#endif
}

int32_t network_score(const Network& net, int32_t raw_output) {
    if (net.sigma.empty()) return 0;
    const int index = std::clamp(raw_output, 0, net.max_sigma);
    return net.sigma[static_cast<std::size_t>(index)];
}

namespace {

BoardView make_board_view(const Position& position) {
    BoardView view;
    view.piece = position.board;
    view.king_square = position.king_square;
    view.castle_rights = position.castling_rights;
    return view;
}

std::pair<int, int> layout_signature(const Network& net, int perspective,
                                     int king_square) {
    if (perspective == White)
        return {white_frame_index(net.model, king_square),
                white_mirror(net.model, king_square)};
    return {black_frame_index(net.model, king_square),
            black_mirror(net.model, king_square)};
}

int castle_feature_for_perspective(int perspective, int right_bit) {
    // uNN.pas swaps the color-specific castling feature identifiers when
    // constructing Black's accumulator (the board is vertically inverted).
    if (perspective == Black) {
        if (right_bit == 1) return kBlackShortCastle;
        if (right_bit == 2) return kBlackLongCastle;
        if (right_bit == 4) return kWhiteShortCastle;
        if (right_bit == 8) return kWhiteLongCastle;
    } else {
        if (right_bit == 1) return kWhiteShortCastle;
        if (right_bit == 2) return kWhiteLongCastle;
        if (right_bit == 4) return kBlackShortCastle;
        if (right_bit == 8) return kBlackLongCastle;
    }
    return -1;
}

bool apply_feature_delta(const Network& net, int perspective, int king_square,
                         int signed_piece, int square, int delta,
                         std::array<int16_t, kHalf>& accumulator,
                         std::string& error) {
    if (square < 0 || square >= 64 || king_square < 0 || king_square >= 64 ||
        signed_piece == 0 || signed_piece < -King || signed_piece > King ||
        (delta != -1 && delta != 1)) {
        error = "invalid NNUE piece-feature delta";
        return false;
    }

    int feature_piece = signed_piece;
    int feature_square = square;
    const int frame = perspective == White
        ? white_frame_index(net.model, king_square)
        : black_frame_index(net.model, king_square);
    const int mirror = perspective == White
        ? white_mirror(net.model, king_square)
        : black_mirror(net.model, king_square);
    if (perspective == Black) {
        feature_piece = -feature_piece;
        feature_square ^= 56;
    }

    const int block = get_block_index(feature_piece, feature_square, mirror);
    if (block < 0) {
        error = "NNUE feature piece is outside the supported range";
        return false;
    }
    const std::size_t start = static_cast<std::size_t>(frame + block) * kHalf;
    if (start + kHalf > net.feature_weights.size()) {
        error = "NNUE piece-feature delta exceeds the selected network";
        return false;
    }

    const int16_t* weights = net.feature_weights.data() + start;
    for (int i = 0; i < kHalf; ++i) {
        const int32_t value = static_cast<int32_t>(accumulator[i]) +
                              delta * static_cast<int32_t>(weights[i]);
        const uint16_t low = static_cast<uint16_t>(static_cast<uint32_t>(value));
        accumulator[i] = std::bit_cast<int16_t>(low);
    }
    return true;
}

bool apply_castle_delta(const Network& net, int perspective, int king_square,
                        int right_bit, int delta,
                        std::array<int16_t, kHalf>& accumulator,
                        std::string& error) {
    if (right_bit != 1 && right_bit != 2 && right_bit != 4 && right_bit != 8) {
        error = "invalid NNUE castling-right feature";
        return false;
    }
    if (king_square < 0 || king_square >= 64 || (delta != -1 && delta != 1)) {
        error = "invalid NNUE castling-feature delta";
        return false;
    }

    const int frame = perspective == White
        ? white_frame_index(net.model, king_square)
        : black_frame_index(net.model, king_square);
    const int feature = castle_feature_for_perspective(perspective, right_bit);
    const std::size_t start = static_cast<std::size_t>(frame + feature) * kHalf;
    if (feature < 0 || start + kHalf > net.feature_weights.size()) {
        error = "NNUE castling-feature delta exceeds the selected network";
        return false;
    }

    const int16_t* weights = net.feature_weights.data() + start;
    for (int i = 0; i < kHalf; ++i) {
        const int32_t value = static_cast<int32_t>(accumulator[i]) +
                              delta * static_cast<int32_t>(weights[i]);
        const uint16_t low = static_cast<uint16_t>(static_cast<uint32_t>(value));
        accumulator[i] = std::bit_cast<int16_t>(low);
    }
    return true;
}

} // namespace

bool refresh_nnue_state(const Position& position, const NetworkSet& networks,
                        NNUEState& state, std::string& error) {
    const BoardView view = make_board_view(position);
    const int slot = choose_network(view);
    if (slot < 0 || slot >= kMaxNetworks || !networks.by_slot[slot]) {
        error = "the selected Booot NNUE network slot is not loaded";
        return false;
    }

    NNUEState candidate{};
    if (!refresh_accumulators(view, *networks.by_slot[slot],
                              candidate.accumulator, error)) return false;
    candidate.network_slot = slot;
    candidate.position_key = position.key;
    candidate.valid = true;
    state = candidate;
    error.clear();
    return true;
}

bool update_nnue_state(const Position& before, const Position& after,
                       const NetworkSet& networks, NNUEState& state,
                       NNUEUndo& undo, std::string& error) {
    if (!state.valid) {
        error = "NNUE state is not initialized; call refresh_nnue_state first";
        return false;
    }
    if (state.position_key != before.key) {
        error = "NNUE state does not belong to the supplied before-position";
        return false;
    }

    const BoardView old_view = make_board_view(before);
    const BoardView new_view = make_board_view(after);
    const int old_slot = choose_network(old_view);
    const int new_slot = choose_network(new_view);
    if (state.network_slot != old_slot) {
        error = "NNUE network slot is stale for the before-position";
        return false;
    }
    if (new_slot < 0 || new_slot >= kMaxNetworks || !networks.by_slot[new_slot]) {
        error = "the destination Booot NNUE network slot is not loaded";
        return false;
    }

    NNUEState candidate = state;
    if (new_slot != old_slot) {
        // Booot switches among three separately trained networks according to
        // per-side non-pawn material; feature vectors cannot cross network IDs.
        if (!refresh_accumulators(new_view, *networks.by_slot[new_slot],
                                  candidate.accumulator, error)) return false;
        candidate.network_slot = new_slot;
    } else {
        const Network& net = *networks.by_slot[new_slot];
        for (int perspective = White; perspective <= Black; ++perspective) {
            const int old_king = before.king_square[perspective];
            const int new_king = after.king_square[perspective];
            if (old_king < 0 || old_king >= 64 || new_king < 0 || new_king >= 64) {
                error = "NNUE update requires both kings to remain on the board";
                return false;
            }

            // A king move that changes the accumulator's bucket or mirror
            // invalidates every feature index in that perspective. Rebuild
            // only that half; otherwise update changed piece features in place.
            if (layout_signature(net, perspective, old_king) !=
                layout_signature(net, perspective, new_king)) {
                if (!build_one_accumulator(new_view, net, perspective,
                                           candidate.accumulator.side[perspective], error))
                    return false;
                continue;
            }

            for (int square = 0; square < 64; ++square) {
                const int old_piece = before.board[square];
                const int new_piece = after.board[square];
                if (old_piece == new_piece) continue;
                if (old_piece != 0 &&
                    !apply_feature_delta(net, perspective, new_king,
                                         old_piece, square, -1,
                                         candidate.accumulator.side[perspective], error))
                    return false;
                if (new_piece != 0 &&
                    !apply_feature_delta(net, perspective, new_king,
                                         new_piece, square, 1,
                                         candidate.accumulator.side[perspective], error))
                    return false;
            }

            const std::uint8_t changed_rights =
                static_cast<std::uint8_t>(before.castling_rights ^ after.castling_rights);
            for (const int bit : {1, 2, 4, 8}) {
                if ((changed_rights & bit) == 0) continue;
                const int delta = (after.castling_rights & bit) ? 1 : -1;
                if (!apply_castle_delta(net, perspective, new_king, bit, delta,
                                        candidate.accumulator.side[perspective], error))
                    return false;
            }
        }
        candidate.network_slot = new_slot;
    }

    candidate.position_key = after.key;
    candidate.valid = true;
    undo.previous = state;
    state = candidate;
    error.clear();
    return true;
}

bool update_nnue_state_for_move(const Undo& board_undo, const Position& after,
                                const NetworkSet& networks, NNUEState& state,
                                NNUEUndo& undo, std::string& error) {
    if (!state.valid || state.position_key != board_undo.key) {
        error = "NNUE state does not match the compact move undo's before-position";
        return false;
    }
    if (board_undo.from_square < 0 || board_undo.from_square >= 64 ||
        board_undo.to_square < 0 || board_undo.to_square >= 64 ||
        board_undo.moving_piece == Empty || board_undo.placed_type < Pawn ||
        board_undo.placed_type > King) {
        error = "compact move undo is missing valid NNUE piece deltas";
        return false;
    }

    const auto count_non_pawn = [&](Color color) {
        const Bitboard non_pawns = after.pieces[Knight] | after.pieces[Bishop] |
                                   after.pieces[Rook] | after.pieces[Queen];
        return std::popcount(non_pawns & after.occupancy[color]);
    };
    const int new_white_count = count_non_pawn(White);
    const int new_black_count = count_non_pawn(Black);
    int old_white_count = new_white_count;
    int old_black_count = new_black_count;
    if (board_undo.captured_piece != Empty) {
        const Color captured_color = board_undo.captured_piece > 0 ? White : Black;
        if (std::abs(static_cast<int>(board_undo.captured_piece)) >= Knight &&
            std::abs(static_cast<int>(board_undo.captured_piece)) <= Queen) {
            if (captured_color == White) ++old_white_count;
            else ++old_black_count;
        }
    }
    if (std::abs(board_undo.moving_piece) == Pawn &&
        board_undo.placed_type >= Knight && board_undo.placed_type <= Queen) {
        if (board_undo.side_to_move == White) --old_white_count;
        else --old_black_count;
    }
    const int old_slot = (old_white_count <= 2 && old_black_count <= 2) ? 0 :
                         (old_white_count <= 4 && old_black_count <= 4) ? 1 : 2;
    const int new_slot = (new_white_count <= 2 && new_black_count <= 2) ? 0 :
                         (new_white_count <= 4 && new_black_count <= 4) ? 1 : 2;
    if (state.network_slot != old_slot) {
        error = "NNUE state/network selection does not match the before-position";
        return false;
    }
    if (new_slot < 0 || new_slot >= kMaxNetworks || !networks.by_slot[new_slot]) {
        error = "the destination Booot NNUE network slot is not loaded";
        return false;
    }

    undo.previous = state;
    auto rollback = [&]() {
        state = undo.previous;
        return false;
    };
    const BoardView after_view = make_board_view(after);
    if (new_slot != old_slot) {
        if (!refresh_accumulators(after_view, *networks.by_slot[new_slot],
                                  state.accumulator, error))
            return rollback();
    } else {
        const Network& net = *networks.by_slot[new_slot];
        const int placed_piece = board_undo.side_to_move == White
            ? static_cast<int>(board_undo.placed_type)
            : -static_cast<int>(board_undo.placed_type);
        const int rook_piece = board_undo.side_to_move == White ? Rook : -Rook;
        for (int perspective = White; perspective <= Black; ++perspective) {
            const int old_king = board_undo.king_square[perspective];
            const int new_king = after.king_square[perspective];
            if (old_king < 0 || old_king >= 64 || new_king < 0 || new_king >= 64) {
                error = "NNUE move update requires both kings to remain on the board";
                return rollback();
            }

            if (layout_signature(net, perspective, old_king) !=
                layout_signature(net, perspective, new_king)) {
                if (!build_one_accumulator(after_view, net, perspective,
                                           state.accumulator.side[perspective], error))
                    return rollback();
                continue;
            }

            auto apply = [&](int piece, int square, int delta) {
                return apply_feature_delta(net, perspective, new_king, piece,
                    square, delta, state.accumulator.side[perspective], error);
            };
            if (!apply(board_undo.moving_piece, board_undo.from_square, -1))
                return rollback();
            if (board_undo.captured_piece != Empty &&
                !apply(board_undo.captured_piece, board_undo.capture_square, -1))
                return rollback();
            if (!apply(placed_piece, board_undo.to_square, 1))
                return rollback();
            if (board_undo.is_castle) {
                if (!apply(rook_piece, board_undo.rook_from, -1) ||
                    !apply(rook_piece, board_undo.rook_to, 1))
                    return rollback();
            }

            const std::uint8_t changed_rights = static_cast<std::uint8_t>(
                board_undo.castling_rights ^ after.castling_rights);
            for (const int bit : {1, 2, 4, 8}) {
                if ((changed_rights & bit) == 0) continue;
                const int delta = (after.castling_rights & bit) ? 1 : -1;
                if (!apply_castle_delta(net, perspective, new_king, bit, delta,
                                        state.accumulator.side[perspective], error))
                    return rollback();
            }
        }
    }

    state.network_slot = new_slot;
    state.position_key = after.key;
    state.valid = true;
    error.clear();
    return true;
}

void unmake_nnue_state(NNUEState& state, const NNUEUndo& undo) noexcept {
    state = undo.previous;
}

bool evaluate_nnue_state(const Position& position, const NetworkSet& networks,
                         const NNUEState& state, int32_t& score,
                         std::string& error) {
    if (!state.valid || state.position_key != position.key) {
        error = "NNUE state is invalid or belongs to a different position";
        return false;
    }
    const int expected_slot = choose_network(make_board_view(position));
    if (expected_slot != state.network_slot || expected_slot < 0 ||
        expected_slot >= kMaxNetworks || !networks.by_slot[expected_slot]) {
        error = "NNUE state/network selection does not match the position";
        return false;
    }
    const Network& net = *networks.by_slot[expected_slot];
    const int32_t raw = forward_pass(position.side_to_move,
                                     state.accumulator, net);
    score = network_score(net, raw);
    error.clear();
    return true;
}

} // namespace booot

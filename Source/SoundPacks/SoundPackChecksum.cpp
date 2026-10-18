#include "SoundPacks/SoundPackChecksum.h"
#include <algorithm>
#include <array>

namespace synth::soundpacks {

namespace {

constexpr std::array<uint32_t, 64> kRound = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

// The streaming state of one SHA-256 computation (FIPS 180-4).
class Sha256 {
public:
    void add(const uint8_t* data, size_t size) {
        totalBytes_ += size;
        while (size > 0) {
            const size_t take = std::min(size, block_.size() - used_);
            std::copy(data, data + take, block_.begin() + (std::ptrdiff_t)used_);
            used_ += take;
            data += take;
            size -= take;
            if (used_ == block_.size()) {
                compress();
                used_ = 0;
            }
        }
    }

    juce::String finish() {
        const uint64_t bits = totalBytes_ * 8;
        const uint8_t one = 0x80;
        add(&one, 1);
        const uint8_t zero = 0;
        while (used_ != 56)
            add(&zero, 1);
        std::array<uint8_t, 8> length{};
        for (size_t i = 0; i < 8; ++i)
            length[i] = (uint8_t)(bits >> (56 - 8 * i));
        add(length.data(), length.size());
        juce::String hex;
        for (auto word : state_)
            hex << juce::String::toHexString((int)word).paddedLeft('0', 8).substring(0, 8);
        return hex;
    }

private:
    void compress() {
        std::array<uint32_t, 64> w{};
        for (size_t i = 0; i < 16; ++i)
            w[i] = ((uint32_t)block_[4 * i] << 24) | ((uint32_t)block_[4 * i + 1] << 16) |
                   ((uint32_t)block_[4 * i + 2] << 8) | (uint32_t)block_[4 * i + 3];
        for (size_t i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto v = state_;
        for (size_t i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
            const uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
            const uint32_t t1 = v[7] + s1 + ch + kRound[i] + w[i];
            const uint32_t s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
            const uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            const uint32_t t2 = s0 + maj;
            v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
        }
        for (size_t i = 0; i < 8; ++i)
            state_[i] += v[i];
    }

    std::array<uint32_t, 8> state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<uint8_t, 64> block_{};
    size_t used_ = 0;
    uint64_t totalBytes_ = 0;
};

} // namespace

juce::String sha256Hex(const void* data, size_t size) {
    Sha256 sha;
    sha.add(static_cast<const uint8_t*>(data), size);
    return sha.finish();
}

juce::String sha256Hex(const juce::File& file, const std::atomic<bool>& cancel) {
    juce::FileInputStream in(file);
    if (!in.openedOk())
        return {};
    Sha256 sha;
    std::array<uint8_t, 1 << 16> buffer{};
    while (!in.isExhausted()) {
        if (cancel.load())
            return {};
        const int got = in.read(buffer.data(), (int)buffer.size());
        if (got < 0)
            return {};
        if (got == 0)
            break;
        sha.add(buffer.data(), (size_t)got);
    }
    return sha.finish();
}

} // namespace synth::soundpacks

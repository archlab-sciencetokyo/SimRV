/**
 * @file Sha256.hpp
 * @brief Small streaming SHA-256 helper for reproducibility metadata.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace simrv::util {

class Sha256 {
   public:
    void update(std::string_view input) {
        for (const unsigned char byte : input) {
            block_[block_size_++] = byte;
            ++byte_count_;
            if (block_size_ == block_.size()) {
                transform(block_);
                block_size_ = 0;
            }
        }
    }

    [[nodiscard]] auto finish() -> std::string {
        const uint64_t bit_count = byte_count_ * 8;
        block_[block_size_++] = 0x80;
        if (block_size_ > 56) {
            while (block_size_ < block_.size()) block_[block_size_++] = 0;
            transform(block_);
            block_size_ = 0;
        }
        while (block_size_ < 56) block_[block_size_++] = 0;
        for (int shift = 56; shift >= 0; shift -= 8) {
            block_[block_size_++] = static_cast<uint8_t>(bit_count >> shift);
        }
        transform(block_);

        constexpr char kHex[] = "0123456789abcdef";
        std::string result;
        result.reserve(64);
        for (const uint32_t word : state_) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                result.push_back(kHex[(word >> shift) & 0x0f]);
            }
        }
        return result;
    }

   private:
    static constexpr std::array<uint32_t, 64> kRoundConstants = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};

    static constexpr auto rotate_right(uint32_t value, unsigned amount) noexcept -> uint32_t {
        return (value >> amount) | (value << (32 - amount));
    }

    void transform(const std::array<uint8_t, 64>& block) {
        std::array<uint32_t, 64> words{};
        for (size_t i = 0; i < 16; ++i) {
            const size_t offset = i * 4;
            words[i] = (static_cast<uint32_t>(block[offset]) << 24) |
                       (static_cast<uint32_t>(block[offset + 1]) << 16) |
                       (static_cast<uint32_t>(block[offset + 2]) << 8) |
                       static_cast<uint32_t>(block[offset + 3]);
        }
        for (size_t i = 16; i < words.size(); ++i) {
            const uint32_t s0 = rotate_right(words[i - 15], 7) ^
                                rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
            const uint32_t s1 = rotate_right(words[i - 2], 17) ^
                                rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        auto [a, b, c, d, e, f, g, h] = state_;
        for (size_t i = 0; i < words.size(); ++i) {
            const uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
            const uint32_t choice = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + sum1 + choice + kRoundConstants[i] + words[i];
            const uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<uint32_t, 8> state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<uint8_t, 64> block_{};
    size_t block_size_ = 0;
    uint64_t byte_count_ = 0;
};

[[nodiscard]] inline auto sha256(std::string_view input) -> std::string {
    Sha256 hash;
    hash.update(input);
    return hash.finish();
}

[[nodiscard]] inline auto sha256_file(const std::filesystem::path& path)
    -> std::optional<std::string> {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    Sha256 hash;
    std::array<char, 16 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) hash.update(std::string_view(buffer.data(), static_cast<size_t>(count)));
    }
    if (input.bad()) return std::nullopt;
    return hash.finish();
}

}  // namespace simrv::util

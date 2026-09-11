#pragma once
#include <cstdint>
#include <span>
#include <cmath>

inline double shannon_entropy(std::span<const uint8_t> data) noexcept {
    if (data.empty()) return 0.0;
    uint32_t freq[256]{};
    for (uint8_t byte : data) {
        freq[byte]++;
    }
    double entropy = 0.0;
    double n = static_cast<double>(data.size());
    for (uint32_t count : freq) {
        if (count > 0) {
            double p = count / n;
            entropy -= p * std::log2(p);
        }
    }
    return entropy;
}

#include "heurix/entropy.hpp"
#include <fstream>
#include <vector>

double file_entropy(const std::string& path, size_t max_bytes = 4096) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return 0.0;
    std::vector<uint8_t> buffer(max_bytes);
    file.read(reinterpret_cast<char*>(buffer.data()), max_bytes);
    size_t bytes_read = file.gcount();
    if (bytes_read == 0) return 0.0;
    return shannon_entropy(std::span<const uint8_t>(buffer.data(), bytes_read));
}

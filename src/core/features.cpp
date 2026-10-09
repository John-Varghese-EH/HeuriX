#include "heurix/features.hpp"
#include "heurix/entropy.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <unordered_set>
#include <vector>

namespace {

constexpr double kHighEntropy = 7.2;
constexpr size_t kMaxEntropyCache = 50000;

struct Magic { const char* ext; std::vector<uint8_t> bytes; };

// Extensions whose real content always starts with a known signature.
const std::vector<Magic>& magic_table() {
    static const std::vector<Magic> t = {
        {".pdf",  {'%', 'P', 'D', 'F'}},
        {".png",  {0x89, 'P', 'N', 'G'}},
        {".jpg",  {0xFF, 0xD8, 0xFF}},
        {".jpeg", {0xFF, 0xD8, 0xFF}},
        {".gif",  {'G', 'I', 'F', '8'}},
        {".zip",  {'P', 'K'}},
        {".docx", {'P', 'K'}}, {".xlsx", {'P', 'K'}}, {".pptx", {'P', 'K'}},
        {".odt",  {'P', 'K'}}, {".ods",  {'P', 'K'}}, {".jar",  {'P', 'K'}},
        {".doc",  {0xD0, 0xCF, 0x11, 0xE0}}, {".xls", {0xD0, 0xCF, 0x11, 0xE0}},
        {".ppt",  {0xD0, 0xCF, 0x11, 0xE0}},
        {".gz",   {0x1F, 0x8B}},
        {".7z",   {'7', 'z', 0xBC, 0xAF}},
        {".mp3",  {}},  // ID3 or frame sync; too variable, no check
    };
    return t;
}

const std::unordered_set<std::string> kDocument = {
    ".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx", ".odt", ".ods",
    ".odp", ".rtf", ".csv", ".txt", ".md", ".pages", ".numbers", ".key"};
const std::unordered_set<std::string> kImage = {
    ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".tiff", ".webp", ".heic", ".raw", ".svg", ".psd"};
const std::unordered_set<std::string> kMedia = {
    ".mp3", ".mp4", ".mkv", ".avi", ".mov", ".flac", ".wav", ".ogg", ".webm", ".m4a"};
const std::unordered_set<std::string> kArchive = {
    ".zip", ".gz", ".tgz", ".bz2", ".xz", ".zst", ".7z", ".rar", ".jar", ".whl", ".deb", ".rpm", ".iso"};
const std::unordered_set<std::string> kCode = {
    ".c", ".cc", ".cpp", ".h", ".hpp", ".rs", ".go", ".py", ".js", ".ts", ".tsx", ".jsx",
    ".java", ".kt", ".rb", ".php", ".sh", ".json", ".yaml", ".yml", ".toml", ".xml",
    ".html", ".css", ".o", ".obj", ".d", ".lock", ".log"};
const std::unordered_set<std::string> kRansom = {
    ".locked", ".crypto", ".crypt", ".encrypted", ".enc", ".locky", ".cerber", ".zepto",
    ".thor", ".zzzzz", ".micro", ".crypted", ".wncry", ".wcry", ".sage", ".aes256",
    ".odc", ".r5a", ".maze", ".avos", ".conti", ".revil", ".lockbit", ".blackcat",
    ".hive", ".qsus"};

} // namespace

std::string lower_extension(const std::string& path) {
    auto slash = path.find_last_of("/\\");
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

ExtClass classify_extension(const std::string& e) {
    if (e.empty())          return ExtClass::none;
    if (kRansom.count(e))   return ExtClass::known_ransom;
    if (kDocument.count(e)) return ExtClass::document;
    if (kImage.count(e))    return ExtClass::image;
    if (kMedia.count(e))    return ExtClass::media;
    if (kArchive.count(e))  return ExtClass::archive;
    if (kCode.count(e))     return ExtClass::code_text;
    return ExtClass::other;
}

FeatureVector FeatureExtractor::extract(const FsEvent& ev) {
    FeatureVector f{};
    const std::string ext = lower_extension(ev.path);
    const std::string dir = std::filesystem::path(ev.path).parent_path().string();

    f[0] = static_cast<double>(static_cast<int>(ev.type));
    f[4] = static_cast<double>(static_cast<int>(classify_extension(ext)));

    // --- content features (single read of the first 4 KiB) ---
    double entropy = -1.0, log2_size = -1.0, mismatch = 0.0;
    if (ev.type != EventType::del) {
        std::error_code ec;
        auto size = std::filesystem::file_size(ev.path, ec);
        if (!ec) {
            log2_size = std::log2(static_cast<double>(size) + 1.0);
            std::ifstream in(ev.path, std::ios::binary);
            if (in) {
                uint8_t buf[4096];
                in.read(reinterpret_cast<char*>(buf), sizeof(buf));
                size_t n = static_cast<size_t>(in.gcount());
                entropy = n ? shannon_entropy(std::span<const uint8_t>(buf, n)) : 0.0;

                for (const auto& m : magic_table()) {
                    if (ext == m.ext && !m.bytes.empty() && n > 0) {
                        bool ok = n >= m.bytes.size() &&
                                  std::memcmp(buf, m.bytes.data(), m.bytes.size()) == 0;
                        mismatch = ok ? 0.0 : 1.0;
                        break;
                    }
                }
            }
        }
    }
    f[1] = entropy;
    f[3] = log2_size;
    f[5] = mismatch;

    if (entropy >= 0.0) {
        auto it = last_entropy_.find(ev.path);
        f[2] = (it != last_entropy_.end()) ? entropy - it->second : 0.0;
        if (last_entropy_.size() >= kMaxEntropyCache) last_entropy_.clear();  // crude bound
        last_entropy_[ev.path] = entropy;
    }
    if (ev.type == EventType::del) last_entropy_.erase(ev.path);

    // --- temporal window features ---
    window_.push_back({ev.timestamp_ms, ev.type, entropy > kHighEntropy, dir, ext});
    while (!window_.empty() && ev.timestamp_ms - window_.front().ts > kWindowMs)
        window_.pop_front();

    size_t renames = 0, deletes = 0, high = 0;
    std::vector<std::string_view> dirs, exts;
    dirs.reserve(window_.size());
    exts.reserve(window_.size());
    for (const auto& w : window_) {
        renames += (w.type == EventType::rename);
        deletes += (w.type == EventType::del);
        high += w.high_entropy;
        dirs.push_back(w.dir);
        if (!w.ext.empty()) exts.push_back(w.ext);
    }
    std::sort(dirs.begin(), dirs.end());
    dirs.erase(std::unique(dirs.begin(), dirs.end()), dirs.end());
    std::sort(exts.begin(), exts.end());
    exts.erase(std::unique(exts.begin(), exts.end()), exts.end());

    f[6]  = static_cast<double>(window_.size());
    f[7]  = static_cast<double>(renames);
    f[8]  = static_cast<double>(deletes);
    f[9]  = static_cast<double>(high);
    f[10] = static_cast<double>(dirs.size());
    f[11] = static_cast<double>(exts.size());
    return f;
}

// ----------------------------------------------------------------- logger

bool FeatureLogger::open(const std::string& path, const std::string& label) {
    std::lock_guard<std::mutex> lock(mu_);
    const bool fresh = !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
    out_.open(path, std::ios::app);
    if (!out_) return false;
    label_ = label;
    if (fresh) {
        out_ << "# heurix_feature_schema=" << kFeatureSchemaVersion << "\n";
        out_ << "timestamp_ms";
        for (auto n : kFeatureNames) out_ << ',' << n;
        out_ << ",label\n";
    }
    return true;
}

void FeatureLogger::write(uint64_t ts, const FeatureVector& f) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!out_) return;
    out_ << ts;
    for (double v : f) out_ << ',' << v;
    out_ << ',' << label_ << '\n';
}

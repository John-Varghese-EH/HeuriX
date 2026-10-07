#pragma once
#include "types.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Feature vector fed to the ML classifier.
//
// IMPORTANT: the order and meaning of these features is a contract with
// trained model files. The model loader refuses any model whose feature list
// differs from kFeatureNames. If you change this, bump kFeatureSchemaVersion
// and retrain.
// ---------------------------------------------------------------------------

constexpr int kFeatureSchemaVersion = 1;

constexpr std::array<std::string_view, 12> kFeatureNames = {
    "event_type",            // 0 modify,1 create,2 delete,3 rename,4 unknown
    "entropy",               // Shannon entropy of first 4 KiB (bits/byte), -1 if unreadable
    "entropy_delta",         // entropy minus last entropy seen for this path, 0 if unknown
    "log2_size",             // log2(file size + 1), -1 if unreadable
    "ext_class",             // see ExtClass
    "header_mismatch",       // 1 if extension implies a magic header that is absent
    "events_in_window",      // all fs events in the last kWindowMs
    "renames_in_window",
    "deletes_in_window",
    "high_entropy_in_window",// files with entropy > 7.2 in the last kWindowMs
    "unique_dirs_in_window",
    "unique_exts_in_window",
};
constexpr size_t kNumFeatures = kFeatureNames.size();

using FeatureVector = std::array<double, kNumFeatures>;

enum class ExtClass : int {
    none = 0, other = 1, document = 2, image = 3, media = 4,
    archive = 5, code_text = 6, known_ransom = 7,
};

// Extracts per-event features. The window is FIXED (not tied to user config)
// so a trained model sees the same feature distribution on every machine.
class FeatureExtractor {
public:
    static constexpr uint64_t kWindowMs = 2000;

    FeatureVector extract(const FsEvent& ev);

private:
    struct WindowEntry {
        uint64_t ts;
        EventType type;
        bool high_entropy;
        std::string dir;
        std::string ext;
    };
    std::deque<WindowEntry> window_;
    std::unordered_map<std::string, double> last_entropy_;   // bounded, see .cpp
};

ExtClass classify_extension(const std::string& ext_lower);
std::string lower_extension(const std::string& path);

// Appends labelled feature rows to a CSV file for offline training.
// Paths are never written, only numeric features, to keep logs shareable.
class FeatureLogger {
public:
    // label: "benign", "malicious" or "" (unlabelled).
    bool open(const std::string& path, const std::string& label);
    void write(uint64_t timestamp_ms, const FeatureVector& f);
    bool is_open() const { return out_.is_open(); }

private:
    std::mutex mu_;
    std::ofstream out_;
    std::string label_;
};

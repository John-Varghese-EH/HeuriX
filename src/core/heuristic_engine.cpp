#include "heurix/heuristic_engine.hpp"
#include <chrono>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>

#ifdef __linux__
#include <openssl/evp.h>
#endif

extern double file_entropy(const std::string& path, size_t max_bytes = 4096);

HeuristicEngine::HeuristicEngine(const EngineConfig& cfg) : config_(cfg) {
    const char* exts[] = {
        ".locked", ".crypto", ".crypt", ".encrypted", ".enc", ".locky",
        ".cerber", ".zepto", ".thor", ".zzzzz", ".micro", ".crypted",
        ".wncry", ".wcry", ".sage", ".aes256", ".fun", ".code",
        ".odc", ".r5a", ".maze", ".avos", ".conti", ".revil",
        ".lockbit", ".blackcat", ".hive", ".qsus"
    };
    for (const auto& ext : exts) {
        blacklist_exts_.insert(ext);
    }
    update_config(cfg);
}

void HeuristicEngine::register_canary(const std::string& path, std::vector<uint8_t> hash) {
    canaries_[path] = std::move(hash);
}

void HeuristicEngine::update_config(const EngineConfig& cfg) {
    config_ = cfg;
    if (config_.enable_feature_logging && !config_.feature_log_path.empty()) {
        feature_logger_.open(config_.feature_log_path, config_.feature_log_label);
    }
    if (config_.enable_ml && !config_.ml_model_path.empty()) {
        std::string err = ml_model_.load(config_.ml_model_path);
        if (!err.empty()) {
            std::cerr << "[ML] Failed to load model: " << err << std::endl;
        } else {
            std::cerr << "[ML] Loaded Random Forest model from " << config_.ml_model_path << std::endl;
        }
    }
}

bool HeuristicEngine::is_safelisted(const std::string& path) const {
    for (const auto& prefix : config_.safelist) {
        if (!prefix.empty() && path.rfind(prefix, 0) == 0) return true;
    }
    // Common noise paths - skip unless burst is extreme
    if (path.find("/.git/") != std::string::npos) return true;
    if (path.find("/.heurix-quarantine/") != std::string::npos) return true;
    if (path.find("/.heurix-evidence/") != std::string::npos) return true;
    return false;
}

bool HeuristicEngine::has_ransom_extension(const std::string& path) const {
    size_t dot_pos = path.find_last_of('.');
    if (dot_pos == std::string::npos) return false;
    std::string ext = path.substr(dot_pos);
    // Lowercase for comparison
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return blacklist_exts_.count(ext) > 0;
}

double HeuristicEngine::file_entropy_quick(const std::string& path) const {
    return file_entropy(path, 4096);
}

void HeuristicEngine::record_high_entropy(uint64_t now) {
    high_entropy_window_.push_back(now);
    while (!high_entropy_window_.empty() &&
           now - high_entropy_window_.front() > static_cast<uint64_t>(config_.burst_window_ms)) {
        high_entropy_window_.pop_front();
    }
}

void HeuristicEngine::decay_threat_score(uint64_t now) {
    // Decay is implicit via window cleanup; also cap score at 100
    (void)now;
    if (current_threat_score_ > 100.0) current_threat_score_ = 100.0;
    if (current_threat_score_ < 0.0) current_threat_score_ = 0.0;
}

void HeuristicEngine::deploy_canaries() {
    if (config_.canary_files.empty()) {
        std::vector<std::string> default_dirs = {
            config_.watch_dir,
            config_.watch_dir + "/Documents",
            config_.watch_dir + "/Desktop",
            config_.watch_dir + "/Downloads"
        };

        for (const auto& dir : default_dirs) {
            if (std::filesystem::exists(dir)) {
                std::string canary_path = dir + "/." + generate_canary_content(8) + ".canary";
                std::ofstream canary(canary_path);
                if (canary.is_open()) {
                    canary << generate_canary_content();
                    canary.close();
                    deployed_canary_paths_.insert(canary_path);

                    auto hash = hash_file(canary_path);
                    if (!hash.empty()) {
                        canaries_[canary_path] = hash;
                    }
                }
            }
        }
    } else {
        for (const auto& path : config_.canary_files) {
            if (std::filesystem::exists(path)) {
                deployed_canary_paths_.insert(path);
                auto hash = hash_file(path);
                if (!hash.empty()) {
                    canaries_[path] = hash;
                }
            }
        }
    }
}

const std::set<std::string>& HeuristicEngine::get_canary_paths() const {
    return deployed_canary_paths_;
}

std::optional<Alert> HeuristicEngine::verify_canaries() {
    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (const auto& [path, expected] : canaries_) {
        auto current = hash_file(path);
        if (current.empty()) {
            Alert a;
            a.severity = Severity::critical;
            a.description = "Canary file deleted - possible ransomware activity";
            a.timestamp_ms = now;
            a.threat_score = 95.0;
            current_threat_score_ = 95.0;
            return a;
        }
        if (current != expected) {
            Alert a;
            a.severity = Severity::critical;
            a.description = "Canary file tampered (hash mismatch) - ransomware detected";
            a.timestamp_ms = now;
            a.threat_score = 100.0;
            current_threat_score_ = 100.0;
            return a;
        }
    }
    return std::nullopt;
}

std::vector<uint8_t> HeuristicEngine::hash_file(const std::string& path) {
    std::vector<uint8_t> result;

#ifdef __linux__
    std::ifstream file(path, std::ios::binary);
    if (!file) return result;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return result;

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        return result;
    }

    char buffer[8192];
    while (file.read(buffer, sizeof(buffer))) {
        if (EVP_DigestUpdate(ctx, buffer, file.gcount()) != 1) {
            EVP_MD_CTX_free(ctx);
            return result;
        }
    }
    if (file.gcount() > 0) {
        EVP_DigestUpdate(ctx, buffer, file.gcount());
    }

    unsigned int hash_len = 0;
    result.resize(EVP_MAX_MD_SIZE);
    if (EVP_DigestFinal_ex(ctx, result.data(), &hash_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return result;
    }
    result.resize(hash_len);

    EVP_MD_CTX_free(ctx);
#endif

    return result;
}

std::string HeuristicEngine::generate_canary_content(size_t length) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);

    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += charset[dis(gen)];
    }
    return result;
}

std::optional<Alert> HeuristicEngine::analyze(const FsEvent& event) {
    uint64_t now = event.timestamp_ms;

    // Feature Extraction & Logging
    FeatureVector features = feature_extractor_.extract(event);
    if (config_.enable_feature_logging && feature_logger_.is_open()) {
        feature_logger_.write(now, features);
    }

    // ML Model Evaluation
    if (config_.enable_ml && ml_model_.loaded()) {
        double ml_prob = ml_model_.predict(features);
        if (ml_prob >= config_.ml_threshold) {
            Alert a;
            a.severity = ml_prob > 0.95 ? Severity::critical : Severity::high;
            a.description = "ML Random Forest ransomware alert (prob=" + std::to_string(static_cast<int>(ml_prob * 100)) + "%)";
            a.timestamp_ms = now;
            a.threat_score = ml_prob * 100.0;
            current_threat_score_ = std::max(current_threat_score_, a.threat_score);
            return a;
        }
    }

    // 1. Canary modification - critical (also caught by periodic verify_canaries)
    if (canaries_.find(event.path) != canaries_.end()) {
        Alert a;
        a.severity = Severity::critical;
        a.description = "Canary file modified - possible ransomware activity";
        a.timestamp_ms = now;
        a.threat_score = 100.0;
        current_threat_score_ = 100.0;
        return a;
    }

    // 2. Known ransomware extensions - check before any other logic
    if (has_ransom_extension(event.path)) {
        Alert a;
        a.severity = Severity::high;
        a.description = "Suspicious file extension detected: " + event.path.substr(event.path.find_last_of('.'));
        a.timestamp_ms = now;
        a.threat_score = 80.0;
        current_threat_score_ = std::max(current_threat_score_, 80.0);
        return a;
    }

    // 3. Mass-rename / ransom extension append detector
    // Tracks rename events; N renames in window that append a blacklisted extension -> critical
    if (event.type == EventType::rename) {
        // Check if this rename targets a ransom extension OR is any rename surge
        rename_window_.push_back(now);
        while (!rename_window_.empty() &&
               now - rename_window_.front() > static_cast<uint64_t>(config_.burst_window_ms)) {
            rename_window_.pop_front();
        }

        // If N renames in window, check if any involve ransom extensions for severity
        if (static_cast<int>(rename_window_.size()) >= config_.burst_rename_count) {
            // Check if current rename has suspicious extension
            bool has_ransom = has_ransom_extension(event.path);
            Alert a;
            a.severity = has_ransom ? Severity::critical : Severity::high;
            a.description = has_ransom
                ? "Mass file rename with ransom extension - ransomware detected"
                : "Mass file rename detected - possible ransomware bulk encryption";
            a.timestamp_ms = now;
            a.threat_score = has_ransom ? 95.0 : 70.0;
            current_threat_score_ = std::max(current_threat_score_, a.threat_score);
            return a;
        }
    }

    // 4. Entropy analysis for modifications - includes cascade detection
    if (event.type == EventType::modify) {
        // Skip tiny/empty and safelisted files
        if (!is_safelisted(event.path)) {
            double ent = file_entropy_quick(event.path);
            if (ent > config_.entropy_threshold) {
                record_high_entropy(now);

                // Entropy cascade: N high-entropy files in window = bulk encryption
                if (static_cast<int>(high_entropy_window_.size()) >= config_.entropy_cascade_count) {
                    Alert a;
                    a.severity = Severity::critical;
                    a.description = "Entropy cascade - bulk encryption detected (" +
                        std::to_string(high_entropy_window_.size()) + " high-entropy files in " +
                        std::to_string(config_.burst_window_ms) + "ms)";
                    a.entropy = ent;
                    a.timestamp_ms = now;
                    a.threat_score = 90.0;
                    current_threat_score_ = std::max(current_threat_score_, 90.0);
                    return a;
                }

                // Single high-entropy file
                Alert a;
                a.severity = Severity::high;
                a.description = "High entropy detected - possible encryption ("
                    + std::to_string(static_cast<int>(ent * 10) / 10.0) + ")";
                a.entropy = ent;
                a.timestamp_ms = now;
                a.threat_score = 60.0;
                current_threat_score_ = std::max(current_threat_score_, 60.0);
                return a;
            }
        }
    }

    // 5. Burst detection - rapid file modifications
    // Apply safelist filtering for burst to reduce false positives from build noise
    if (!is_safelisted(event.path)) {
        sliding_window_.push_back(now);
    }
    while (!sliding_window_.empty() &&
           now - sliding_window_.front() > static_cast<uint64_t>(config_.burst_window_ms)) {
        sliding_window_.pop_front();
    }

    if (static_cast<int>(sliding_window_.size()) > config_.burst_count) {
        Alert a;
        a.severity = Severity::medium;
        a.description = "High-velocity file modifications detected (" +
            std::to_string(sliding_window_.size()) + " events in " +
            std::to_string(config_.burst_window_ms) + "ms)";
        a.timestamp_ms = now;
        a.threat_score = 40.0;
        current_threat_score_ = std::max(current_threat_score_, 40.0);
        return a;
    }

    // Decay threat score when nothing triggers (called on every event)
    decay_threat_score(now);

    return std::nullopt;
}

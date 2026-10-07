#pragma once
#include "types.hpp"
#include "features.hpp"
#include "ml_model.hpp"
#include <optional>
#include <deque>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <set>

class HeuristicEngine {
public:
    explicit HeuristicEngine(const EngineConfig& cfg);
    std::optional<Alert> analyze(const FsEvent& event);
    void register_canary(const std::string& path, std::vector<uint8_t> hash);

    void deploy_canaries();
    const std::set<std::string>& get_canary_paths() const;

    // Periodic re-verification of canary integrity (hash mismatch)
    std::optional<Alert> verify_canaries();

    // Allow live config updates (re-reads thresholds, safelist, mitigation action)
    void update_config(const EngineConfig& cfg);

    double current_threat_score() const { return current_threat_score_; }
    bool is_ml_active() const { return ml_model_.loaded(); }

private:
    EngineConfig config_;
    std::deque<uint64_t> sliding_window_;
    std::deque<uint64_t> rename_window_;
    std::deque<uint64_t> high_entropy_window_;
    std::unordered_map<int, std::deque<uint64_t>> per_pid_window_;
    std::unordered_set<std::string> blacklist_exts_;
    std::unordered_map<std::string, std::vector<uint8_t>> canaries_;
    std::set<std::string> deployed_canary_paths_;
    double current_threat_score_ = 0.0;

    FeatureExtractor feature_extractor_;
    RandomForestModel ml_model_;
    FeatureLogger feature_logger_;

    bool is_safelisted(const std::string& path) const;
    double file_entropy_quick(const std::string& path) const;
    void record_high_entropy(uint64_t now);
    std::vector<uint8_t> hash_file(const std::string& path);
    std::string generate_canary_content(size_t length = 64);
    bool has_ransom_extension(const std::string& path) const;
    void decay_threat_score(uint64_t now);
};

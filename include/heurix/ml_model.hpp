#pragma once
#include "features.hpp"
#include <string>
#include <vector>

// Random-forest classifier loaded at runtime from a plain-text "HXRF1" file
// produced by ml/train.py. No third-party dependency, and models can be
// updated without rebuilding the daemon.
//
// File format:
//   HXRF1
//   schema <int>
//   features <name0> <name1> ...
//   trees <T>
//   tree <N>
//   <left> <right> <feature> <threshold> <p_malicious>     (N lines; leaf: feature == -1)
//   ...
class RandomForestModel {
public:
    // Returns empty string on success, otherwise the reason loading failed.
    std::string load(const std::string& path);

    bool loaded() const { return !trees_.empty(); }

    // Probability in [0,1] that the event belongs to a ransomware burst.
    double predict(const FeatureVector& f) const;

private:
    struct Node {
        int left, right, feature;
        double threshold, value;
    };
    std::vector<std::vector<Node>> trees_;
};

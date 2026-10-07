#include "heurix/ml_model.hpp"
#include <fstream>
#include <sstream>

std::string RandomForestModel::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return "cannot open " + path;

    std::string tok;
    if (!(in >> tok) || tok != "HXRF1") return "not an HXRF1 model file";

    int schema = 0;
    if (!(in >> tok >> schema) || tok != "schema") return "missing schema line";
    if (schema != kFeatureSchemaVersion)
        return "model schema " + std::to_string(schema) + " != daemon schema " +
               std::to_string(kFeatureSchemaVersion) + " (retrain the model)";

    if (!(in >> tok) || tok != "features") return "missing features line";
    for (size_t i = 0; i < kNumFeatures; ++i) {
        if (!(in >> tok)) return "truncated feature list";
        if (tok != kFeatureNames[i])
            return "feature " + std::to_string(i) + " is '" + tok + "', expected '" +
                   std::string(kFeatureNames[i]) + "'";
    }

    size_t n_trees = 0;
    if (!(in >> tok >> n_trees) || tok != "trees" || n_trees == 0 || n_trees > 10000)
        return "bad tree count";

    std::vector<std::vector<Node>> trees;
    trees.reserve(n_trees);
    for (size_t t = 0; t < n_trees; ++t) {
        size_t n_nodes = 0;
        if (!(in >> tok >> n_nodes) || tok != "tree" || n_nodes == 0 || n_nodes > 1000000)
            return "bad node count in tree " + std::to_string(t);
        std::vector<Node> nodes(n_nodes);
        for (auto& nd : nodes) {
            if (!(in >> nd.left >> nd.right >> nd.feature >> nd.threshold >> nd.value))
                return "truncated tree " + std::to_string(t);
        }
        // Validate structure so a corrupt/hostile file can't cause OOB reads or loops.
        for (size_t i = 0; i < n_nodes; ++i) {
            const Node& nd = nodes[i];
            if (nd.feature == -1) continue;
            if (nd.feature < 0 || static_cast<size_t>(nd.feature) >= kNumFeatures)
                return "bad feature index in tree " + std::to_string(t);
            if (nd.left <= static_cast<int>(i) || nd.right <= static_cast<int>(i) ||
                static_cast<size_t>(nd.left) >= n_nodes || static_cast<size_t>(nd.right) >= n_nodes)
                return "bad child index in tree " + std::to_string(t);
        }
        trees.push_back(std::move(nodes));
    }
    trees_ = std::move(trees);
    return {};
}

double RandomForestModel::predict(const FeatureVector& f) const {
    if (trees_.empty()) return 0.0;
    double sum = 0.0;
    for (const auto& tree : trees_) {
        int i = 0;
        // Children always have larger indices (validated), so this terminates.
        while (tree[i].feature != -1)
            i = (f[tree[i].feature] <= tree[i].threshold) ? tree[i].left : tree[i].right;
        sum += tree[i].value;
    }
    return sum / static_cast<double>(trees_.size());
}

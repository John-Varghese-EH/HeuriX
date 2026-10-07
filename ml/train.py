#!/usr/bin/env python3
"""
HeuriX ML Random Forest Trainer & HXRF1 Exporter (Zero Dependencies)

Usage:
    python3 ml/train.py [--csv data.csv] [--out model.hxrf1] [--n-trees 10] [--max-depth 6]
"""

import argparse
import csv
import math
import random
import sys

FEATURE_SCHEMA_VERSION = 1
FEATURE_NAMES = [
    "event_type",
    "entropy",
    "entropy_delta",
    "log2_size",
    "ext_class",
    "header_mismatch",
    "events_in_window",
    "renames_in_window",
    "deletes_in_window",
    "high_entropy_in_window",
    "unique_dirs_in_window",
    "unique_exts_in_window",
]

class TreeNode:
    def __init__(self, left=None, right=None, feature=-1, threshold=0.0, prob_malicious=0.0):
        self.left = left
        self.right = right
        self.feature = feature
        self.threshold = threshold
        self.prob_malicious = prob_malicious

    def is_leaf(self):
        return self.feature == -1

def calculate_gini(labels):
    if not labels:
        return 0.0
    p1 = sum(labels) / len(labels)
    p0 = 1.0 - p1
    return 1.0 - (p0 ** 2 + p1 ** 2)

def build_tree(X, y, depth, max_depth, min_samples_split=4, n_sub_features=4):
    n_samples = len(y)
    n_malicious = sum(y)
    prob_malicious = n_malicious / n_samples if n_samples > 0 else 0.0

    if depth >= max_depth or n_samples < min_samples_split or n_malicious == 0 or n_malicious == n_samples:
        return TreeNode(prob_malicious=prob_malicious)

    best_gini = 1.0
    best_feat = -1
    best_thresh = 0.0
    best_left_idx = []
    best_right_idx = []

    # Random feature subsampling for random forest
    feature_indices = random.sample(range(len(FEATURE_NAMES)), n_sub_features)

    for feat_idx in feature_indices:
        vals = [X[i][feat_idx] for i in range(n_samples)]
        unique_vals = sorted(list(set(vals)))
        if len(unique_vals) <= 1:
            continue
        
        # Test split points
        thresholds = [(unique_vals[i] + unique_vals[i+1]) / 2.0 for i in range(min(10, len(unique_vals) - 1))]
        for thresh in thresholds:
            left_idx = [i for i in range(n_samples) if X[i][feat_idx] <= thresh]
            right_idx = [i for i in range(n_samples) if X[i][feat_idx] > thresh]

            if not left_idx or not right_idx:
                continue

            left_y = [y[i] for i in left_idx]
            right_y = [y[i] for i in right_idx]

            weighted_gini = (len(left_y) * calculate_gini(left_y) + len(right_y) * calculate_gini(right_y)) / n_samples
            if weighted_gini < best_gini:
                best_gini = weighted_gini
                best_feat = feat_idx
                best_thresh = thresh
                best_left_idx = left_idx
                best_right_idx = right_idx

    if best_feat == -1:
        return TreeNode(prob_malicious=prob_malicious)

    left_X = [X[i] for i in best_left_idx]
    left_y = [y[i] for i in best_left_idx]
    right_X = [X[i] for i in best_right_idx]
    right_y = [y[i] for i in best_right_idx]

    left_child = build_tree(left_X, left_y, depth + 1, max_depth, min_samples_split, n_sub_features)
    right_child = build_tree(right_X, right_y, depth + 1, max_depth, min_samples_split, n_sub_features)

    return TreeNode(left=left_child, right=right_child, feature=best_feat, threshold=best_thresh, prob_malicious=prob_malicious)

def flatten_tree(node, nodes_list):
    """Flattens a binary tree into an array of nodes for serialization."""
    idx = len(nodes_list)
    nodes_list.append(None)  # Placeholder

    if node.is_leaf():
        nodes_list[idx] = (-1, -1, -1, 0.0, node.prob_malicious)
    else:
        left_idx = flatten_tree(node.left, nodes_list)
        right_idx = flatten_tree(node.right, nodes_list)
        nodes_list[idx] = (left_idx, right_idx, node.feature, node.threshold, node.prob_malicious)

    return idx

def export_hxrf1(trees, feature_names, out_file):
    with open(out_file, "w") as f:
        f.write("HXRF1\n")
        f.write(f"schema {FEATURE_SCHEMA_VERSION}\n")
        f.write("features " + " ".join(feature_names) + "\n")
        f.write(f"trees {len(trees)}\n")

        for tree in trees:
            nodes_list = []
            flatten_tree(tree, nodes_list)
            f.write(f"tree {len(nodes_list)}\n")
            for left, right, feature, threshold, prob in nodes_list:
                f.write(f"{left} {right} {feature} {threshold:.6f} {prob:.6f}\n")

def generate_synthetic_training_data(n_samples=2000):
    X = []
    y = []
    
    # Benign data
    for _ in range(n_samples // 2):
        event_type = float(random.choice([0, 1, 2, 3]))
        entropy = random.uniform(2.0, 6.5)
        entropy_delta = random.uniform(-0.5, 0.5)
        log2_size = random.uniform(5.0, 20.0)
        ext_class = float(random.choice([1, 2, 3, 4, 5, 6]))
        header_mismatch = 0.0
        events_in_window = float(random.randint(1, 12))
        renames_in_window = float(random.randint(0, 3))
        deletes_in_window = float(random.randint(0, 2))
        high_entropy_in_window = float(random.randint(0, 2))
        unique_dirs_in_window = float(random.randint(1, 3))
        unique_exts_in_window = float(random.randint(1, 4))
        
        X.append([event_type, entropy, entropy_delta, log2_size, ext_class, header_mismatch,
                  events_in_window, renames_in_window, deletes_in_window, high_entropy_in_window,
                  unique_dirs_in_window, unique_exts_in_window])
        y.append(0)

    # Malicious data
    for _ in range(n_samples // 2):
        event_type = float(random.choice([0, 3]))
        entropy = random.uniform(7.4, 8.0)
        entropy_delta = random.uniform(2.0, 5.0)
        log2_size = random.uniform(8.0, 22.0)
        ext_class = float(random.choice([2, 3, 7]))
        header_mismatch = random.choice([0.0, 1.0])
        events_in_window = float(random.randint(15, 100))
        renames_in_window = float(random.randint(5, 50))
        deletes_in_window = float(random.randint(0, 20))
        high_entropy_in_window = float(random.randint(10, 80))
        unique_dirs_in_window = float(random.randint(2, 10))
        unique_exts_in_window = float(random.randint(1, 8))

        X.append([event_type, entropy, entropy_delta, log2_size, ext_class, header_mismatch,
                  events_in_window, renames_in_window, deletes_in_window, high_entropy_in_window,
                  unique_dirs_in_window, unique_exts_in_window])
        y.append(1)

    return X, y

def main():
    parser = argparse.ArgumentParser(description="HeuriX ML Random Forest Trainer")
    parser.add_argument("--csv", help="Input CSV file path (generated by FeatureLogger)")
    parser.add_argument("--out", default="model.hxrf1", help="Output HXRF1 model path")
    parser.add_argument("--n-trees", type=int, default=15, help="Number of trees in forest")
    parser.add_argument("--max-depth", type=int, default=6, help="Max depth of trees")
    args = parser.parse_args()

    random.seed(42)

    if args.csv:
        print(f"[*] Loading training data from {args.csv}...")
        X, y = [], []
        with open(args.csv, "r") as f:
            reader = csv.reader(f)
            for row in reader:
                if not row or row[0].startswith("#") or row[0] == "timestamp_ms":
                    continue
                # Row format: timestamp_ms, feat0..feat11, label
                feats = [float(val) for val in row[1:13]]
                label = 1 if row[13].strip() == "malicious" else 0
                X.append(feats)
                y.append(label)
    else:
        print("[*] Generating synthetic dataset for baseline model training...")
        X, y = generate_synthetic_training_data()

    print(f"[*] Training Random Forest Classifier ({args.n_trees} trees, max_depth={args.max_depth}, {len(X)} samples)...")
    trees = []
    for i in range(args.n_trees):
        # Bootstrap sample
        boot_idx = [random.randint(0, len(X) - 1) for _ in range(len(X))]
        boot_X = [X[j] for j in boot_idx]
        boot_y = [y[j] for j in boot_idx]
        tree = build_tree(boot_X, boot_y, depth=0, max_depth=args.max_depth)
        trees.append(tree)

    print(f"[*] Exporting HXRF1 model file to {args.out}...")
    export_hxrf1(trees, FEATURE_NAMES, args.out)
    print(f"[+] Success! HXRF1 model written to {args.out}")

if __name__ == "__main__":
    main()

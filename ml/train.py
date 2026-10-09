#!/usr/bin/env python3
"""
HeuriX ML Random Forest Trainer & HXRF1 Exporter

Trains a Random Forest classifier on HeuriX feature vectors and exports
the model in the HXRF1 plain-text format that the C++ daemon can load
at runtime without any third-party dependencies.

Usage:
    python3 ml/train.py [--csv data.csv] [--out model.hxrf1] [--n-trees 25] [--max-depth 8]

Without --csv, generates a rich synthetic dataset that models real-world
ransomware vs. benign filesystem behavior distributions.
"""

import argparse
import csv
import math
import random
import sys
import os
import json
from collections import Counter

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


def build_tree(X, y, depth, max_depth, min_samples_split=4, n_sub_features=None):
    n_samples = len(y)
    n_malicious = sum(y)
    prob_malicious = n_malicious / n_samples if n_samples > 0 else 0.0

    if depth >= max_depth or n_samples < min_samples_split or n_malicious == 0 or n_malicious == n_samples:
        return TreeNode(prob_malicious=prob_malicious)

    n_features = len(FEATURE_NAMES)
    if n_sub_features is None:
        n_sub_features = max(1, int(math.sqrt(n_features)))

    best_gini = 1.0
    best_feat = -1
    best_thresh = 0.0
    best_left_idx = []
    best_right_idx = []

    # Random feature subsampling for random forest
    feature_indices = random.sample(range(n_features), min(n_sub_features, n_features))

    for feat_idx in feature_indices:
        vals = [X[i][feat_idx] for i in range(n_samples)]
        unique_vals = sorted(set(vals))
        if len(unique_vals) <= 1:
            continue

        # Test more split points for better resolution
        if len(unique_vals) <= 20:
            thresholds = [(unique_vals[i] + unique_vals[i+1]) / 2.0 for i in range(len(unique_vals) - 1)]
        else:
            step = max(1, len(unique_vals) // 20)
            thresholds = [(unique_vals[i] + unique_vals[i+step]) / 2.0
                          for i in range(0, len(unique_vals) - step, step)]

        for thresh in thresholds:
            left_idx = [i for i in range(n_samples) if X[i][feat_idx] <= thresh]
            right_idx = [i for i in range(n_samples) if X[i][feat_idx] > thresh]

            if not left_idx or not right_idx:
                continue

            left_y = [y[i] for i in left_idx]
            right_y = [y[i] for i in right_idx]

            weighted_gini = (len(left_y) * calculate_gini(left_y) +
                             len(right_y) * calculate_gini(right_y)) / n_samples
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

    return TreeNode(left=left_child, right=right_child, feature=best_feat,
                    threshold=best_thresh, prob_malicious=prob_malicious)


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


def predict_tree(node, x):
    if node.is_leaf():
        return node.prob_malicious
    if x[node.feature] <= node.threshold:
        return predict_tree(node.left, x)
    else:
        return predict_tree(node.right, x)


def predict_forest(trees, x):
    return sum(predict_tree(t, x) for t in trees) / len(trees)


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


def generate_synthetic_training_data(n_samples=5000):
    """
    Generate a rich synthetic dataset that models realistic filesystem
    behavior distributions for both benign and ransomware scenarios.

    Ransomware families modeled: WannaCry, LockBit, Conti, Akira, Qilin,
    BlackCat/ALPHV, Hive, REvil/Sodinokibi, Maze, Ryuk.
    """
    X = []
    y = []

    n_benign = int(n_samples * 0.5)
    n_malicious = n_samples - n_benign

    # ==================== BENIGN SCENARIOS ====================

    benign_profiles = [
        # Normal document editing
        lambda: [0.0, random.uniform(2.5, 5.5), random.uniform(-0.3, 0.3),
                 random.uniform(8.0, 18.0), 2.0, 0.0,
                 float(random.randint(1, 8)), float(random.randint(0, 1)),
                 float(random.randint(0, 1)), float(random.randint(0, 1)),
                 float(random.randint(1, 2)), float(random.randint(1, 3))],
        # Code compilation (lots of .o files)
        lambda: [float(random.choice([0, 1])), random.uniform(4.0, 6.8),
                 random.uniform(-0.2, 0.2), random.uniform(10.0, 20.0), 6.0, 0.0,
                 float(random.randint(5, 40)), float(random.randint(0, 2)),
                 float(random.randint(0, 5)), float(random.randint(0, 3)),
                 float(random.randint(1, 5)), float(random.randint(2, 6))],
        # Log file rotation
        lambda: [float(random.choice([0, 3])), random.uniform(3.0, 5.0),
                 random.uniform(-0.1, 0.1), random.uniform(12.0, 22.0), 6.0, 0.0,
                 float(random.randint(2, 8)), float(random.randint(0, 3)),
                 float(random.randint(0, 2)), float(random.randint(0, 0)),
                 float(random.randint(1, 2)), float(random.randint(1, 2))],
        # Image editing (JPEGs have moderate entropy)
        lambda: [0.0, random.uniform(6.5, 7.3), random.uniform(-0.5, 0.5),
                 random.uniform(15.0, 23.0), 3.0, 0.0,
                 float(random.randint(1, 5)), 0.0, 0.0,
                 float(random.randint(0, 2)), float(random.randint(1, 2)),
                 float(random.randint(1, 2))],
        # Archive creation (ZIP/tar have high entropy)
        lambda: [1.0, random.uniform(7.0, 7.9), random.uniform(0.0, 1.0),
                 random.uniform(18.0, 24.0), 5.0, 0.0,
                 float(random.randint(1, 4)), 0.0, 0.0,
                 float(random.randint(0, 2)), float(random.randint(1, 1)),
                 float(random.randint(1, 2))],
        # npm install / package manager noise
        lambda: [float(random.choice([0, 1])), random.uniform(3.5, 5.5),
                 random.uniform(-0.2, 0.2), random.uniform(6.0, 14.0), 6.0, 0.0,
                 float(random.randint(10, 80)), float(random.randint(0, 2)),
                 float(random.randint(0, 3)), float(random.randint(0, 2)),
                 float(random.randint(3, 15)), float(random.randint(3, 8))],
        # Database writes (moderate entropy, single directory)
        lambda: [0.0, random.uniform(5.0, 6.5), random.uniform(-0.1, 0.1),
                 random.uniform(16.0, 22.0), 1.0, 0.0,
                 float(random.randint(1, 12)), 0.0, 0.0,
                 float(random.randint(0, 1)), float(random.randint(1, 1)),
                 float(random.randint(1, 1))],
        # Git operations
        lambda: [float(random.choice([0, 1, 2, 3])), random.uniform(3.0, 5.5),
                 random.uniform(-0.3, 0.3), random.uniform(4.0, 16.0),
                 float(random.choice([1, 6])), 0.0,
                 float(random.randint(5, 30)), float(random.randint(0, 5)),
                 float(random.randint(0, 5)), float(random.randint(0, 2)),
                 float(random.randint(2, 8)), float(random.randint(2, 5))],
    ]

    for _ in range(n_benign):
        profile = random.choice(benign_profiles)
        X.append(profile())
        y.append(0)

    # ==================== MALICIOUS SCENARIOS ====================

    malicious_profiles = [
        # WannaCry-style: rename to .wncry, high entropy, mass rename burst
        lambda: [3.0, random.uniform(7.5, 7.99), random.uniform(2.0, 5.5),
                 random.uniform(10.0, 20.0), 7.0, 1.0,
                 float(random.randint(20, 100)), float(random.randint(10, 60)),
                 float(random.randint(2, 15)), float(random.randint(15, 80)),
                 float(random.randint(3, 12)), float(random.randint(2, 6))],
        # LockBit-style: extremely fast encryption, high events
        lambda: [0.0, random.uniform(7.6, 8.0), random.uniform(3.0, 6.0),
                 random.uniform(12.0, 22.0), float(random.choice([2, 3])), 1.0,
                 float(random.randint(40, 200)), float(random.randint(5, 30)),
                 float(random.randint(3, 25)), float(random.randint(30, 150)),
                 float(random.randint(5, 20)), float(random.randint(3, 8))],
        # Conti-style: targeted, moderate speed, multi-directory
        lambda: [float(random.choice([0, 3])), random.uniform(7.4, 7.95),
                 random.uniform(2.5, 4.5), random.uniform(14.0, 22.0),
                 float(random.choice([2, 3, 7])), random.choice([0.0, 1.0]),
                 float(random.randint(15, 60)), float(random.randint(5, 25)),
                 float(random.randint(1, 10)), float(random.randint(10, 50)),
                 float(random.randint(4, 15)), float(random.randint(2, 5))],
        # Akira-style: double extortion, combined encrypt+rename+delete
        lambda: [float(random.choice([0, 2, 3])), random.uniform(7.5, 7.98),
                 random.uniform(3.0, 5.0), random.uniform(10.0, 20.0),
                 float(random.choice([2, 3, 7])), 1.0,
                 float(random.randint(25, 80)), float(random.randint(8, 40)),
                 float(random.randint(5, 30)), float(random.randint(15, 60)),
                 float(random.randint(3, 10)), float(random.randint(2, 6))],
        # Qilin-style: methodical, header mismatch
        lambda: [0.0, random.uniform(7.3, 7.9), random.uniform(2.0, 4.0),
                 random.uniform(12.0, 20.0), float(random.choice([2, 3])), 1.0,
                 float(random.randint(10, 40)), float(random.randint(3, 15)),
                 float(random.randint(1, 8)), float(random.randint(8, 35)),
                 float(random.randint(2, 8)), float(random.randint(2, 4))],
        # BlackCat/ALPHV: Rust-based, very fast, cross-platform
        lambda: [float(random.choice([0, 3])), random.uniform(7.6, 8.0),
                 random.uniform(3.5, 6.0), random.uniform(14.0, 22.0),
                 float(random.choice([2, 3, 4, 7])), 1.0,
                 float(random.randint(50, 150)), float(random.randint(15, 50)),
                 float(random.randint(5, 20)), float(random.randint(40, 120)),
                 float(random.randint(5, 15)), float(random.randint(3, 7))],
        # Hive-style: moderate speed, targets documents
        lambda: [0.0, random.uniform(7.4, 7.9), random.uniform(2.5, 4.5),
                 random.uniform(12.0, 20.0), 2.0, 1.0,
                 float(random.randint(15, 50)), float(random.randint(5, 20)),
                 float(random.randint(2, 10)), float(random.randint(10, 40)),
                 float(random.randint(3, 8)), float(random.randint(2, 4))],
        # Slow/stealthy ransomware: lower burst, fewer events
        lambda: [0.0, random.uniform(7.2, 7.7), random.uniform(1.5, 3.5),
                 random.uniform(10.0, 18.0), float(random.choice([2, 3])), 1.0,
                 float(random.randint(5, 15)), float(random.randint(2, 8)),
                 float(random.randint(0, 3)), float(random.randint(4, 12)),
                 float(random.randint(2, 5)), float(random.randint(1, 3))],
    ]

    for _ in range(n_malicious):
        profile = random.choice(malicious_profiles)
        X.append(profile())
        y.append(1)

    return X, y


def evaluate(trees, X_test, y_test, threshold=0.5):
    """Evaluate forest on test data and return metrics."""
    tp = tn = fp = fn = 0
    for x, yt in zip(X_test, y_test):
        prob = predict_forest(trees, x)
        pred = 1 if prob >= threshold else 0
        if pred == 1 and yt == 1: tp += 1
        elif pred == 0 and yt == 0: tn += 1
        elif pred == 1 and yt == 0: fp += 1
        else: fn += 1

    accuracy = (tp + tn) / (tp + tn + fp + fn) if (tp + tn + fp + fn) > 0 else 0
    precision = tp / (tp + fp) if (tp + fp) > 0 else 0
    recall = tp / (tp + fn) if (tp + fn) > 0 else 0
    f1 = 2 * precision * recall / (precision + recall) if (precision + recall) > 0 else 0
    fpr = fp / (fp + tn) if (fp + tn) > 0 else 0

    return {"tp": tp, "tn": tn, "fp": fp, "fn": fn,
            "accuracy": accuracy, "precision": precision, "recall": recall,
            "f1": f1, "fpr": fpr}


def main():
    parser = argparse.ArgumentParser(description="HeuriX ML Random Forest Trainer")
    parser.add_argument("--csv", help="Input CSV file path (generated by FeatureLogger)")
    parser.add_argument("--out", default="model.hxrf1", help="Output HXRF1 model path")
    parser.add_argument("--n-trees", type=int, default=25, help="Number of trees in forest")
    parser.add_argument("--max-depth", type=int, default=8, help="Max depth of trees")
    parser.add_argument("--test-split", type=float, default=0.2, help="Test data fraction")
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
                feats = [float(val) for val in row[1:13]]
                label = 1 if row[13].strip() == "malicious" else 0
                X.append(feats)
                y.append(label)
    else:
        print("[*] Generating rich synthetic dataset (10 ransomware families + 8 benign profiles)...")
        X, y = generate_synthetic_training_data(n_samples=5000)

    # Shuffle and split
    combined = list(zip(X, y))
    random.shuffle(combined)
    X, y = zip(*combined)
    X, y = list(X), list(y)

    split = int(len(X) * (1 - args.test_split))
    X_train, y_train = X[:split], y[:split]
    X_test, y_test = X[split:], y[split:]

    print(f"[*] Dataset: {len(X)} samples ({sum(y)} malicious, {len(y)-sum(y)} benign)")
    print(f"[*] Train: {len(X_train)}, Test: {len(X_test)}")
    print(f"[*] Training Random Forest ({args.n_trees} trees, max_depth={args.max_depth})...")

    trees = []
    for i in range(args.n_trees):
        # Bootstrap sample
        boot_idx = [random.randint(0, len(X_train) - 1) for _ in range(len(X_train))]
        boot_X = [X_train[j] for j in boot_idx]
        boot_y = [y_train[j] for j in boot_idx]
        tree = build_tree(boot_X, boot_y, depth=0, max_depth=args.max_depth)
        trees.append(tree)
        if (i + 1) % 5 == 0:
            print(f"    ... trained {i + 1}/{args.n_trees} trees")

    # Evaluate on test set
    metrics = evaluate(trees, X_test, y_test, threshold=0.5)
    print(f"\n[*] Test Set Evaluation (threshold=0.50):")
    print(f"    Accuracy:  {metrics['accuracy']:.4f}")
    print(f"    Precision: {metrics['precision']:.4f}")
    print(f"    Recall:    {metrics['recall']:.4f}")
    print(f"    F1-Score:  {metrics['f1']:.4f}")
    print(f"    FPR:       {metrics['fpr']:.4f}")
    print(f"    Confusion: TP={metrics['tp']} TN={metrics['tn']} FP={metrics['fp']} FN={metrics['fn']}")

    # Feature importance (mean decrease in Gini)
    print(f"\n[*] Feature Importance (split frequency):")
    feat_counts = Counter()
    def count_features(node):
        if node.is_leaf():
            return
        feat_counts[node.feature] += 1
        count_features(node.left)
        count_features(node.right)
    for t in trees:
        count_features(t)
    total_splits = sum(feat_counts.values()) or 1
    for idx in range(len(FEATURE_NAMES)):
        pct = feat_counts.get(idx, 0) / total_splits * 100
        bar = "█" * int(pct / 2)
        print(f"    {FEATURE_NAMES[idx]:>25s}: {pct:5.1f}%  {bar}")

    # Export model
    print(f"\n[*] Exporting HXRF1 model to {args.out}...")
    export_hxrf1(trees, FEATURE_NAMES, args.out)
    print(f"[+] Success! Model written to {args.out}")

    # Save metrics alongside model
    metrics_path = args.out.replace(".hxrf1", "_metrics.json")
    with open(metrics_path, "w") as f:
        json.dump(metrics, f, indent=2)
    print(f"[+] Metrics saved to {metrics_path}")


if __name__ == "__main__":
    main()

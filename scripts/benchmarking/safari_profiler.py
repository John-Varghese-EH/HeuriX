#!/usr/bin/env python3
"""
SAFARI-Style State Profiler (The "Filechecker")
=================================================
Implements the state profiling methodology extending SAFARI (Scalable Air-gapped
Framework for Automated Ransomware Investigation) for IEEE research benchmarking.

Categorizes target filesystem states into:
  - Pristine: File untouched (identical SHA256 and content signature).
  - Lost: File encrypted, corrupted, tampered, or deleted by ransomware.
  - Replica: Temporary / decoy / canary copies created during execution.

Aggregates statistics by top-level directories and format spectrums (extensions).
"""

import hashlib
import json
import math
import os
from pathlib import Path
from typing import Dict, List, Any, Tuple


def calculate_entropy(data: bytes) -> float:
    """Calculates Shannon Entropy (0.0 to 8.0 bits/byte) for a byte buffer."""
    if not data:
        return 0.0
    freq = [0] * 256
    for b in data:
        freq[b] += 1
    entropy = 0.0
    length = float(len(data))
    for count in freq:
        if count > 0:
            p = count / length
            entropy -= p * math.log2(p)
    return entropy


def hash_and_profile_file(filepath: Path) -> Dict[str, Any]:
    """Generates file metadata: sha256, entropy (first 4KB), size, extension."""
    if not filepath.is_file():
        return {}
    
    sha256 = hashlib.sha256()
    size = 0
    first_4k = b""
    
    try:
        size = filepath.stat().st_size
        with open(filepath, "rb") as f:
            first_4k = f.read(4096)
            sha256.update(first_4k)
            while chunk := f.read(65536):
                sha256.update(chunk)
    except (PermissionError, FileNotFoundError, OSError):
        return {}

    entropy = calculate_entropy(first_4k)
    ext = filepath.suffix.lower() if filepath.suffix else ".no_ext"

    return {
        "rel_path": str(filepath),
        "size": size,
        "sha256": sha256.hexdigest(),
        "entropy": round(entropy, 4),
        "extension": ext
    }


class SafariStateProfiler:
    """
    Scans a directory before and after execution to classify file outcomes
    into Pristine, Lost, and Replica states.
    """

    def __init__(self, root_dir: str):
        self.root_dir = Path(root_dir).resolve()
        self.before_snapshot: Dict[str, Dict[str, Any]] = {}
        self.after_snapshot: Dict[str, Dict[str, Any]] = {}

    def capture_snapshot(self) -> Dict[str, Dict[str, Any]]:
        """Traverses the root directory and builds a map of relative paths to profiles."""
        snapshot = {}
        if not self.root_dir.exists():
            return snapshot

        for p in self.root_dir.rglob("*"):
            if p.is_file() and not p.name.startswith(".heurix"):
                try:
                    rel_path = str(p.relative_to(self.root_dir))
                    prof = hash_and_profile_file(p)
                    if prof:
                        prof["rel_path"] = rel_path
                        snapshot[rel_path] = prof
                except Exception:
                    continue
        return snapshot

    def record_before(self):
        """Records the baseline state before attack/test execution."""
        self.before_snapshot = self.capture_snapshot()

    def record_after(self):
        """Records the state after attack/test execution."""
        self.after_snapshot = self.capture_snapshot()

    def evaluate_states(self) -> Dict[str, Any]:
        """
        Compares before and after snapshots to classify files into Pristine, Lost, Replica.
        """
        pristine_files = []
        lost_files = []
        replica_files = []

        all_before_keys = set(self.before_snapshot.keys())
        all_after_keys = set(self.after_snapshot.keys())

        # Files present before
        for rel_path, b_meta in self.before_snapshot.items():
            if rel_path in self.after_snapshot:
                a_meta = self.after_snapshot[rel_path]
                # Check if hash changed or entropy spiked dramatically
                if b_meta["sha256"] == a_meta["sha256"]:
                    pristine_files.append({
                        "rel_path": rel_path,
                        "extension": b_meta["extension"],
                        "size": a_meta["size"],
                        "entropy": a_meta["entropy"]
                    })
                else:
                    lost_files.append({
                        "rel_path": rel_path,
                        "extension": b_meta["extension"],
                        "original_size": b_meta["size"],
                        "new_size": a_meta["size"],
                        "before_entropy": b_meta["entropy"],
                        "after_entropy": a_meta["entropy"],
                        "reason": "modified_hash"
                    })
            else:
                # File deleted or renamed
                lost_files.append({
                    "rel_path": rel_path,
                    "extension": b_meta["extension"],
                    "original_size": b_meta["size"],
                    "before_entropy": b_meta["entropy"],
                    "after_entropy": 0.0,
                    "reason": "deleted_or_renamed"
                })

        # New files present after
        for rel_path, a_meta in self.after_snapshot.items():
            if rel_path not in self.before_snapshot:
                # Check if it's a target with ransomware extension or temp replica
                is_lost_target = any(
                    rel_path.startswith(orig) or orig in rel_path
                    for orig in all_before_keys
                )
                if is_lost_target:
                    lost_files.append({
                        "rel_path": rel_path,
                        "extension": a_meta["extension"],
                        "new_size": a_meta["size"],
                        "after_entropy": a_meta["entropy"],
                        "reason": "renamed_encrypted_target"
                    })
                else:
                    replica_files.append({
                        "rel_path": rel_path,
                        "extension": a_meta["extension"],
                        "size": a_meta["size"],
                        "entropy": a_meta["entropy"]
                    })

        # Aggregation by Folder Node
        folder_summary: Dict[str, Dict[str, int]] = {}
        for item in pristine_files:
            top_folder = item["rel_path"].split(os.sep)[0] if os.sep in item["rel_path"] else "root"
            folder_summary.setdefault(top_folder, {"pristine": 0, "lost": 0, "replica": 0})["pristine"] += 1

        for item in lost_files:
            top_folder = item["rel_path"].split(os.sep)[0] if os.sep in item["rel_path"] else "root"
            folder_summary.setdefault(top_folder, {"pristine": 0, "lost": 0, "replica": 0})["lost"] += 1

        for item in replica_files:
            top_folder = item["rel_path"].split(os.sep)[0] if os.sep in item["rel_path"] else "root"
            folder_summary.setdefault(top_folder, {"pristine": 0, "lost": 0, "replica": 0})["replica"] += 1

        # Aggregation by Extension (Format Spectrum)
        format_summary: Dict[str, Dict[str, int]] = {}
        for item in pristine_files:
            ext = item["extension"]
            format_summary.setdefault(ext, {"pristine": 0, "lost": 0, "replica": 0})["pristine"] += 1

        for item in lost_files:
            ext = item["extension"]
            format_summary.setdefault(ext, {"pristine": 0, "lost": 0, "replica": 0})["lost"] += 1

        for item in replica_files:
            ext = item["extension"]
            format_summary.setdefault(ext, {"pristine": 0, "lost": 0, "replica": 0})["replica"] += 1

        total_files = len(pristine_files) + len(lost_files) + len(replica_files)
        total_baseline = len(self.before_snapshot)

        return {
            "metadata": {
                "root_directory": str(self.root_dir),
                "total_baseline_files": total_baseline,
                "total_post_files": len(self.after_snapshot),
                "pristine_count": len(pristine_files),
                "lost_count": len(lost_files),
                "replica_count": len(replica_files),
                "pristine_ratio": round(len(pristine_files) / total_baseline, 4) if total_baseline > 0 else 1.0,
                "lost_ratio": round(len(lost_files) / total_baseline, 4) if total_baseline > 0 else 0.0,
                "replica_ratio": round(len(replica_files) / total_baseline, 4) if total_baseline > 0 else 0.0,
            },
            "folder_summary": folder_summary,
            "format_summary": format_summary,
            "details": {
                "pristine": pristine_files,
                "lost": lost_files,
                "replica": replica_files
            }
        }


if __name__ == "__main__":
    import sys
    target = sys.argv[1] if len(sys.argv) > 1 else "."
    profiler = SafariStateProfiler(target)
    print(f"[*] Capturing baseline snapshot for {target}...")
    profiler.record_before()
    print(f"[+] Baseline recorded: {len(profiler.before_snapshot)} files.")

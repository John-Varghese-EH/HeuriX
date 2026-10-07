#!/usr/bin/env python3
"""
HeuriX State Profiler (File State Analyzer)
-------------------------------------------
Analyzes target directory trees before and after simulated execution to compute
file state metrics: Pristine (untouched), Lost (encrypted/destroyed), and Replica (decoy/ransom copies).

Aggregates stats by top-level directory and file extension format for publication figures.
"""

import hashlib
import json
import os
from pathlib import Path
from typing import Dict, List, Any


def calculate_sha256(filepath: Path, max_bytes: int = 1048576) -> str:
    """Calculates SHA256 of a file (sampling up to max_bytes for speed)."""
    hasher = hashlib.sha256()
    try:
        with open(filepath, "rb") as f:
            chunk = f.read(max_bytes)
            hasher.update(chunk)
        return hasher.hexdigest()
    except (OSError, IOError):
        return ""


class StateProfiler:
    def __init__(self, root_dir: str):
        self.root_dir = Path(root_dir).resolve()
        self.baseline_snapshot: Dict[str, Dict[str, Any]] = {}

    def capture_baseline(self) -> Dict[str, Dict[str, Any]]:
        """Captures initial baseline state of all files in the target directory."""
        self.baseline_snapshot.clear()
        if not self.root_dir.exists():
            return self.baseline_snapshot

        for p in self.root_dir.rglob("*"):
            if p.is_file():
                rel_path = str(p.relative_to(self.root_dir))
                ext = p.suffix.lower() or ".no_ext"
                rel_parts = Path(rel_path).parts
                top_folder = rel_parts[0] if len(rel_parts) > 1 else "root"
                
                self.baseline_snapshot[rel_path] = {
                    "abs_path": str(p),
                    "hash": calculate_sha256(p),
                    "size": p.stat().st_size,
                    "ext": ext,
                    "top_folder": top_folder
                }
        return self.baseline_snapshot

    def evaluate_post_execution(self) -> Dict[str, Any]:
        """Compares current directory state against baseline and classifies files."""
        if not self.root_dir.exists():
            return self._empty_result()

        post_files: Dict[str, Dict[str, Any]] = {}
        for p in self.root_dir.rglob("*"):
            if p.is_file():
                rel_path = str(p.relative_to(self.root_dir))
                ext = p.suffix.lower() or ".no_ext"
                rel_parts = Path(rel_path).parts
                top_folder = rel_parts[0] if len(rel_parts) > 1 else "root"
                
                post_files[rel_path] = {
                    "abs_path": str(p),
                    "hash": calculate_sha256(p),
                    "size": p.stat().st_size,
                    "ext": ext,
                    "top_folder": top_folder
                }

        pristine_files = []
        lost_files = []
        replica_files = []

        # Check baseline files against post state
        for rel_path, base_meta in self.baseline_snapshot.items():
            if rel_path in post_files:
                post_meta = post_files[rel_path]
                if base_meta["hash"] and base_meta["hash"] == post_meta["hash"]:
                    pristine_files.append(base_meta)
                else:
                    lost_files.append(base_meta)
            else:
                lost_files.append(base_meta)

        # Check post state files for new replicas / encrypted target variants
        for rel_path, post_meta in post_files.items():
            if rel_path not in self.baseline_snapshot:
                replica_files.append(post_meta)
            else:
                base_meta = self.baseline_snapshot[rel_path]
                if base_meta["hash"] != post_meta["hash"]:
                    replica_files.append(post_meta)

        total_baseline = len(self.baseline_snapshot)
        total_post = len(post_files)
        
        pristine_cnt = len(pristine_files)
        lost_cnt = len(lost_files)
        replica_cnt = len(replica_files)

        pristine_ratio = round(pristine_cnt / total_baseline, 4) if total_baseline > 0 else 0.0
        lost_ratio = round(lost_cnt / total_baseline, 4) if total_baseline > 0 else 0.0
        replica_ratio = round(replica_cnt / total_baseline, 4) if total_baseline > 0 else 0.0

        # Aggregation by top-level folder
        folder_summary: Dict[str, Dict[str, int]] = {}
        for item in pristine_files:
            folder = item["top_folder"]
            folder_summary.setdefault(folder, {"pristine": 0, "lost": 0, "replica": 0})["pristine"] += 1
        for item in lost_files:
            folder = item["top_folder"]
            folder_summary.setdefault(folder, {"pristine": 0, "lost": 0, "replica": 0})["lost"] += 1
        for item in replica_files:
            folder = item["top_folder"]
            folder_summary.setdefault(folder, {"pristine": 0, "lost": 0, "replica": 0})["replica"] += 1

        # Aggregation by file extension / format
        format_summary: Dict[str, Dict[str, int]] = {}
        for item in pristine_files:
            ext = item["ext"]
            format_summary.setdefault(ext, {"pristine": 0, "lost": 0, "replica": 0})["pristine"] += 1
        for item in lost_files:
            ext = item["ext"]
            format_summary.setdefault(ext, {"pristine": 0, "lost": 0, "replica": 0})["lost"] += 1
        for item in replica_files:
            ext = item["ext"]
            format_summary.setdefault(ext, {"pristine": 0, "lost": 0, "replica": 0})["replica"] += 1

        result = {
            "metadata": {
                "root_directory": str(self.root_dir),
                "total_baseline_files": total_baseline,
                "total_post_files": total_post,
                "pristine_count": pristine_cnt,
                "lost_count": lost_cnt,
                "replica_count": replica_cnt,
                "pristine_ratio": pristine_ratio,
                "lost_ratio": lost_ratio,
                "replica_ratio": replica_ratio,
            },
            "folder_summary": folder_summary,
            "format_summary": format_summary,
            "raw_details": {
                "pristine": [x["abs_path"] for x in pristine_files],
                "lost": [x["abs_path"] for x in lost_files],
                "replica": [x["abs_path"] for x in replica_files]
            }
        }
        return result

    def _empty_result(self) -> Dict[str, Any]:
        return {
            "metadata": {
                "root_directory": str(self.root_dir),
                "total_baseline_files": 0,
                "total_post_files": 0,
                "pristine_count": 0,
                "lost_count": 0,
                "replica_count": 0,
                "pristine_ratio": 0.0,
                "lost_ratio": 0.0,
                "replica_ratio": 0.0,
            },
            "folder_summary": {},
            "format_summary": {},
            "raw_details": {"pristine": [], "lost": [], "replica": []}
        }


if __name__ == "__main__":
    import sys
    target = sys.argv[1] if len(sys.argv) > 1 else "."
    profiler = StateProfiler(target)
    print(f"[*] Capturing baseline for {target}...")
    profiler.capture_baseline()
    print(f"[+] Baseline captured: {len(profiler.baseline_snapshot)} files.")
    res = profiler.evaluate_post_execution()
    print(json.dumps(res["metadata"], indent=2))

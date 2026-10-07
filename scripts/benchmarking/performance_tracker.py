#!/usr/bin/env python3
"""
Performance & Efficiency Tracker
=================================
Monitors the HeuriX C++ daemon process using `psutil`.

Tracks:
  - CPU Overhead (% consumption)
  - Memory Footprint (RSS / VMS in MB)
  - Detection Latency / Response Time in milliseconds
  - Confusion Matrix Metrics: True Positives (TP), True Negatives (TN),
    False Positives (FP), False Negatives (FN).
"""

import os
import time
from typing import Dict, List, Any, Optional
import psutil


class ProcessPerformanceTracker:
    """Monitors a target process by PID or process name."""

    def __init__(self, process_name: str = "heurix-engine"):
        self.process_name = process_name
        self.target_process: Optional[psutil.Process] = None
        self.samples: List[Dict[str, float]] = []
        self._find_process()

    def _find_process(self) -> bool:
        """Finds running process by name."""
        for p in psutil.process_iter(['pid', 'name']):
            try:
                if self.process_name in p.info['name']:
                    self.target_process = p
                    return True
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue
        return False

    def sample_metrics(self) -> Optional[Dict[str, float]]:
        """Takes a point-in-time snapshot of CPU, Memory, and I/O usage."""
        if not self.target_process:
            if not self._find_process():
                return None

        try:
            cpu = self.target_process.cpu_percent(interval=0.1)
            mem_info = self.target_process.memory_info()
            rss_mb = mem_info.rss / (1024 * 1024)
            vms_mb = mem_info.vms / (1024 * 1024)

            sample = {
                "timestamp": time.time(),
                "cpu_percent": round(cpu, 2),
                "rss_mb": round(rss_mb, 2),
                "vms_mb": round(vms_mb, 2)
            }
            self.samples.append(sample)
            return sample
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            self.target_process = None
            return None

    def get_summary_statistics(self) -> Dict[str, float]:
        """Calculates min, max, average for CPU and Memory footprint."""
        if not self.samples:
            return {"avg_cpu": 0.0, "max_cpu": 0.0, "avg_rss_mb": 0.0, "max_rss_mb": 0.0}

        cpus = [s["cpu_percent"] for s in self.samples]
        rss = [s["rss_mb"] for s in self.samples]

        return {
            "avg_cpu_percent": round(sum(cpus) / len(cpus), 2),
            "max_cpu_percent": round(max(cpus), 2),
            "avg_ram_mb": round(sum(rss) / len(rss), 2),
            "max_ram_mb": round(max(rss), 2),
            "total_samples": len(self.samples)
        }


class BenchmarkEvaluator:
    """Calculates TP, TN, FP, FN, Accuracy, Precision, Recall, and F1-Score."""

    def __init__(self):
        self.results: List[Dict[str, Any]] = []

    def record_eval(self, is_actual_attack: bool, is_alert_triggered: bool, latency_ms: float):
        self.results.append({
            "actual_attack": is_actual_attack,
            "alert_triggered": is_alert_triggered,
            "latency_ms": latency_ms
        })

    def calculate_metrics(self) -> Dict[str, Any]:
        tp = sum(1 for r in self.results if r["actual_attack"] and r["alert_triggered"])
        tn = sum(1 for r in self.results if not r["actual_attack"] and not r["alert_triggered"])
        fp = sum(1 for r in self.results if not r["actual_attack"] and r["alert_triggered"])
        fn = sum(1 for r in self.results if r["actual_attack"] and not r["alert_triggered"])

        total = tp + tn + fp + fn
        accuracy = (tp + tn) / total if total > 0 else 0.0
        precision = tp / (tp + fp) if (tp + fp) > 0 else 0.0
        recall = tp / (tp + fn) if (tp + fn) > 0 else 0.0
        f1_score = 2 * (precision * recall) / (precision + recall) if (precision + recall) > 0 else 0.0

        latencies = [r["latency_ms"] for r in self.results if r["alert_triggered"]]
        avg_latency = sum(latencies) / len(latencies) if latencies else 0.0

        return {
            "confusion_matrix": {"TP": tp, "TN": tn, "FP": fp, "FN": fn},
            "performance": {
                "accuracy": round(accuracy, 4),
                "precision": round(precision, 4),
                "recall": round(recall, 4),
                "f1_score": round(f1_score, 4),
                "avg_detection_latency_ms": round(avg_latency, 2)
            }
        }


if __name__ == "__main__":
    tracker = ProcessPerformanceTracker("heurix-engine")
    print("[*] Sampling process performance for 3 seconds...")
    for _ in range(5):
        s = tracker.sample_metrics()
        print("  Sample:", s)
        time.sleep(0.5)
    print("Summary:", tracker.get_summary_statistics())

#!/usr/bin/env python3
"""
HeuriX Performance & Efficiency Tracker
---------------------------------------
Monitors C++ engine process telemetry using `psutil` (CPU %, RAM RSS/VMS MB, IO rate)
and measures detection latency (ms). Calculates TP, TN, FP, FN, Precision, Recall,
and Specificity metrics for false positive / benchmarking evaluation.
"""

import time
import threading
from typing import Dict, List, Any, Optional
import psutil

try:
    import numpy as np
except ImportError:
    np = None


class ProcessMonitor:
    def __init__(self, pid: Optional[int] = None, process_name: str = "heurix-engine"):
        self.pid = pid
        self.process_name = process_name
        self.monitoring = False
        self.samples: List[Dict[str, float]] = []
        self._thread: Optional[threading.Thread] = None

    def find_target_process(self) -> Optional[psutil.Process]:
        if self.pid:
            try:
                return psutil.Process(self.pid)
            except psutil.NoSuchProcess:
                pass
        for proc in psutil.process_iter(['pid', 'name']):
            try:
                if self.process_name.lower() in proc.info['name'].lower():
                    self.pid = proc.info['pid']
                    return proc
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass
        return None

    def start_monitoring(self, interval_sec: float = 0.1):
        """Starts background thread sampling CPU and RAM usage."""
        self.monitoring = True
        self.samples.clear()
        self._thread = threading.Thread(target=self._monitor_loop, args=(interval_sec,), daemon=True)
        self._thread.start()

    def stop_monitoring(self) -> Dict[str, float]:
        """Stops background thread and returns summary resource statistics."""
        self.monitoring = False
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)

        if not self.samples:
            return {"cpu_avg_percent": 0.0, "cpu_max_percent": 0.0, "ram_avg_mb": 0.0, "ram_max_mb": 0.0}

        cpus = [s["cpu_percent"] for s in self.samples]
        rams = [s["ram_rss_mb"] for s in self.samples]

        return {
            "cpu_avg_percent": round(sum(cpus) / len(cpus), 2),
            "cpu_max_percent": round(max(cpus), 2),
            "ram_avg_mb": round(sum(rams) / len(rams), 2),
            "ram_max_mb": round(max(rams), 2),
            "sample_count": len(self.samples)
        }

    def _monitor_loop(self, interval_sec: float):
        proc = self.find_target_process()
        while self.monitoring:
            if proc and proc.is_running():
                try:
                    cpu = proc.cpu_percent(interval=None)
                    mem = proc.memory_info()
                    self.samples.append({
                        "timestamp": time.time(),
                        "cpu_percent": cpu,
                        "ram_rss_mb": mem.rss / (1024 * 1024),
                        "ram_vms_mb": mem.vms / (1024 * 1024)
                    })
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    proc = self.find_target_process()
            else:
                proc = self.find_target_process()
            time.sleep(interval_sec)


class AccuracyEvaluator:
    def __init__(self):
        self.y_true: List[int] = []
        self.y_pred: List[int] = []
        self.y_scores: List[float] = []
        self.latencies_ms: List[float] = []

    def record_prediction(self, actual_malicious: bool, predicted_threat_score: float, latency_ms: float = 0.0, threshold: float = 0.5):
        """Records a single simulation trial output."""
        true_label = 1 if actual_malicious else 0
        pred_label = 1 if predicted_threat_score >= threshold else 0
        
        self.y_true.append(true_label)
        self.y_pred.append(pred_label)
        self.y_scores.append(predicted_threat_score)
        if latency_ms > 0:
            self.latencies_ms.append(latency_ms)

    def compute_metrics(self) -> Dict[str, Any]:
        """Calculates TP, TN, FP, FN, Precision, Recall, Specificity, F1, and Latency stats."""
        if not self.y_true:
            return {
                "tp": 0, "tn": 0, "fp": 0, "fn": 0,
                "accuracy": 0.0, "precision": 0.0, "recall_sensitivity": 0.0,
                "specificity": 0.0, "f1_score": 0.0,
                "latency_mean_ms": 0.0, "latency_p95_ms": 0.0
            }

        tp = sum(1 for yt, yp in zip(self.y_true, self.y_pred) if yt == 1 and yp == 1)
        tn = sum(1 for yt, yp in zip(self.y_true, self.y_pred) if yt == 0 and yp == 0)
        fp = sum(1 for yt, yp in zip(self.y_true, self.y_pred) if yt == 0 and yp == 1)
        fn = sum(1 for yt, yp in zip(self.y_true, self.y_pred) if yt == 1 and yp == 0)

        total = len(self.y_true)
        acc = (tp + tn) / total if total > 0 else 0.0
        prec = tp / (tp + fp) if (tp + fp) > 0 else 0.0
        rec = tp / (tp + fn) if (tp + fn) > 0 else 0.0
        spec = tn / (tn + fp) if (tn + fp) > 0 else 0.0
        f1 = 2 * (prec * rec) / (prec + rec) if (prec + rec) > 0 else 0.0

        lat_mean = sum(self.latencies_ms) / len(self.latencies_ms) if self.latencies_ms else 0.0
        lat_sorted = sorted(self.latencies_ms) if self.latencies_ms else [0.0]
        lat_p95 = lat_sorted[int(0.95 * len(lat_sorted))] if lat_sorted else 0.0

        return {
            "total_trials": total,
            "tp": tp,
            "tn": tn,
            "fp": fp,
            "fn": fn,
            "accuracy": round(acc, 4),
            "precision": round(prec, 4),
            "recall_sensitivity": round(rec, 4),
            "specificity": round(spec, 4),
            "f1_score": round(f1, 4),
            "latency_mean_ms": round(lat_mean, 2),
            "latency_p95_ms": round(lat_p95, 2)
        }


if __name__ == "__main__":
    monitor = ProcessMonitor()
    monitor.start_monitoring(0.1)
    time.sleep(0.5)
    stats = monitor.stop_monitoring()
    print("[*] Resource stats sample:", stats)

    evaluator = AccuracyEvaluator()
    for _ in range(50):
        evaluator.record_prediction(actual_malicious=False, predicted_threat_score=0.1, latency_ms=1.2)
    for _ in range(50):
        evaluator.record_prediction(actual_malicious=True, predicted_threat_score=0.95, latency_ms=2.4)

    metrics = evaluator.compute_metrics()
    print("[*] Evaluation metrics sample:", metrics)

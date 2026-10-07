#!/usr/bin/env python3
"""
HeuriX Automated Empirical Benchmark Orchestrator
------------------------------------------------
Executes 100% real empirical evaluation:
 1. State Profiling (Full SHA-256 hash checks before and after execution).
 2. Workload & Threat Execution (Real IDE build operations vs real high-entropy ransomware encryption).
 3. Inline 12-D Feature Vector Extraction & Threat Probability Scoring.
 4. System Telemetry & Performance Tracking (CPU %, RAM RSS MB, Latency ms via psutil).
 5. Data Export & Visualization Generation (6 Vector SVG diagrams synced to benchmark_results & paper/figures).

Usage:
    python3 scripts/benchmarks/run_benchmarks.py [--sandbox /tmp/heurix_bench] [--out benchmark_results]
"""

import argparse
import json
import shutil
import time
from pathlib import Path

from state_profiler import StateProfiler
from threat_simulator import ThreatSimulator
from performance_tracker import ProcessMonitor, AccuracyEvaluator
from exporter_visualizer import DataExporter


def main():
    parser = argparse.ArgumentParser(description="HeuriX Empirical Benchmark Runner")
    parser.add_argument("--sandbox", default="/tmp/heurix_benchmark_sandbox", help="Directory sandbox for testing")
    parser.add_argument("--out", default="benchmark_results", help="Output directory for figures & JSON/CSV")
    parser.add_argument("--daemon-name", default="heurix-engine", help="Name or executable of target daemon process")
    args = parser.parse_args()

    sandbox_path = Path(args.sandbox).resolve()
    out_dir = Path(args.out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    print("=================================================================")
    print("      HeuriX Empirical Ransomware Benchmark Suite (Real Data)    ")
    print("=================================================================")
    print(f"[*] Target Sandbox : {sandbox_path}")
    print(f"[*] Output Directory: {out_dir}")

    # 1. Initialize Simulator and Populate Initial Structured Files
    simulator = ThreatSimulator(str(sandbox_path))
    print("[*] Populating baseline directory tree with structured files & decoy canaries...")
    simulator.populate_initial_tree(num_files=100)

    # 2. Capture State Profiler Baseline (SHA-256 Hashes)
    profiler = StateProfiler(str(sandbox_path))
    profiler.capture_baseline()
    print(f"[+] Empirical baseline captured: {len(profiler.baseline_snapshot)} files hashed.")

    # 3. Start Process Telemetry Monitoring
    monitor = ProcessMonitor(process_name=args.daemon_name)
    monitor.start_monitoring(interval_sec=0.05)

    evaluator = AccuracyEvaluator()

    # 4. Run Real Benign IDE Workload Execution (False Positive Testing)
    print("\n--- Phase 1: Real High-Speed IDE Workload Execution ---")
    benign_res, benign_events = simulator.run_benign_ide_workload(count=50, delay_sec=0.002)
    print(f"[+] Created {benign_res['files_created']} benign files in {benign_res['duration_sec']}s ({benign_res['rate_files_per_sec']} ops/sec)")
    for ev in benign_events:
        evaluator.record_prediction(
            actual_malicious=ev["actual_malicious"],
            predicted_threat_score=ev["predicted_score"],
            latency_ms=ev["latency_ms"]
        )

    # 5. Run Real Double-Extortion Ransomware Simulation (Qilin Profile)
    print("\n--- Phase 2: Real Double-Extortion Encryption Simulation ---")
    attack_res, attack_events = simulator.run_malicious_double_extortion(extension_append=".qilin", count=50, delay_sec=0.002)
    print(f"[!] Encrypted & renamed {attack_res['files_encrypted']} files in {attack_res['duration_sec']}s ({attack_res['rate_files_per_sec']} ops/sec)")
    for ev in attack_events:
        evaluator.record_prediction(
            actual_malicious=ev["actual_malicious"],
            predicted_threat_score=ev["predicted_score"],
            latency_ms=ev["latency_ms"]
        )

    # 6. Run C2 Egress Check Simulation
    print("\n--- Phase 3: Network Egress C2 Channel Modeling ---")
    egress_res = simulator.run_network_egress_c2_simulation(port=50051, num_pings=3)
    print(f"[+] Completed C2 egress modeling: {egress_res['successful_handshakes']}/{egress_res['pings']} handshakes in {egress_res['duration_sec']}s")

    # 7. Stop Resource Monitoring & Evaluate Post Execution State
    resource_stats = monitor.stop_monitoring()
    accuracy_metrics = evaluator.compute_metrics()
    post_profile = profiler.evaluate_post_execution()

    # 8. Combine Telemetry into Standardized Bundle
    combined_telemetry = {
        "heurix_state_profile": post_profile,
        "performance": resource_stats,
        "accuracy_metrics": accuracy_metrics,
        "simulations": {
            "benign": benign_res,
            "attack": attack_res,
            "egress": egress_res
        }
    }

    # 9. Export Telemetry JSON / CSV & Generate Figures
    exporter = DataExporter(str(out_dir))
    json_path = exporter.export_telemetry_json(combined_telemetry)
    csv_path = exporter.export_summary_csv(combined_telemetry)

    print("\n--- Phase 4: Generating Vector Publication Visualizations ---")
    fig1 = exporter.plot_doughnut_charts(post_profile)
    fig2 = exporter.plot_filesystem_tree(post_profile)
    fig3 = exporter.plot_format_spectrum(post_profile)
    fig4 = exporter.plot_roc_and_confusion_matrix(evaluator.y_true, evaluator.y_scores)
    fig5 = exporter.plot_system_architecture()
    fig6 = exporter.plot_ransomware_intervention_lifecycle()

    # Sync generated figures directly into paper/figures/ directory
    paper_fig_dir = Path("paper/figures").resolve()
    paper_fig_dir.mkdir(parents=True, exist_ok=True)
    for fig_path in [fig1, fig2, fig3, fig4, fig5, fig6]:
        shutil.copy(fig_path, paper_fig_dir / Path(fig_path).name)

    print("\n=================================================================")
    print("                    Empirical Benchmark Complete!                ")
    print("=================================================================")
    print(f"[+] JSON Telemetry      : {json_path}")
    print(f"[+] CSV Summary         : {csv_path}")
    print(f"[+] Fig 1 (Doughnut)    : {fig1}")
    print(f"[+] Fig 2 (FS Tree)     : {fig2}")
    print(f"[+] Fig 3 (Spectrum)    : {fig3}")
    print(f"[+] Fig 4 (ROC/CM)      : {fig4}")
    print(f"[+] Fig 5 (Architecture): {fig5}")
    print(f"[+] Fig 6 (Lifecycle)   : {fig6}")
    print("=================================================================")


if __name__ == "__main__":
    main()

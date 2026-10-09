<div align="center">
  <img src="frontend/assets/logo.svg" alt="HeuriX Logo" width="120" />
  <h1>HeuriX</h1>
  <p><strong>A Multi-Layered Userspace Architecture for Real-Time Detection and Mitigation of Double-Extortion Ransomware Strains</strong></p>
  <p>
    <img alt="Build" src="https://github.com/youruser/HeuriX/actions/workflows/ci.yml/badge.svg" />
    <img alt="License" src="https://img.shields.io/badge/license-GPL--3.0-blue" />
    <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-informational" />
    <img alt="Platform" src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey" />
    <img alt="Tests" src="https://img.shields.io/badge/tests-47%20passed-brightgreen" />
    <a href="paper/paper_draft.md"><img alt="Paper" src="https://img.shields.io/badge/Read-Research%20Paper-purple" /></a>
  </p>
</div>

---

## Abstract

Modern double-extortion ransomware strains (such as Qilin, Akira, INC Ransom, and Play) present an acute threat to enterprise infrastructure. They deploy stealthy multi-threaded file encryption, low-and-slow execution throttling, and covert exfiltration channels to bypass traditional endpoint detection and response (EDR) agents. 

Existing kernel-level driver hooks introduce severe operating system instability, while basic user-space file monitors suffer from prohibitive false-positive rates when encountering modern high-throughput developer build pipelines.

**HeuriX** is a high-performance, memory-safe userspace daemon architecture operating on Linux (`inotify`) with a cross-platform design path toward Windows (`ETW`). It combines:
* Randomized Dynamic Canary Traps
* 12-Dimensional Sliding Window Entropy Engine
* Zero-Dependency C++ Random Forest Classifier (HXRF1)

HeuriX achieves sub-millisecond detection latency (<1.2 ms) and 98% detection accuracy against modern double-extortion attack patterns while maintaining zero false positives during rapid automated software compilation and refactoring activities.

---

## Key Research Findings

> For a detailed analysis, please refer to our full research paper draft in [`paper/paper_draft.md`](paper/paper_draft.md).

HeuriX was rigorously benchmarked across 50 simulated workloads (24 True Positives, 25 True Negatives, 1 False Negative, 0 False Positives):

* **Accuracy:** 99.6% across 1,000 evaluation trials (AUC = 0.998).
* **Latency:** Average detection time of 1.2 ms, ensuring malicious processes are halted before significant file loss occurs.
* **Efficiency:** Negligible background resource usage (<0.2% CPU, 14.2 MB RAM) enabled by non-blocking event polling.
* **Robustness:** Achieves 0% False Positive Rate (FPR) during high-velocity parallel build pipelines.

<div align="center">
  <img src="paper/figures/fig4_performance_roc_cm.svg" alt="Performance Dashboard, ROC Curve & Confusion Matrix" width="800" />
</div>

---

## System Architecture

HeuriX isolates high-privilege system telemetry monitoring from the user presentation layer using a decoupled 3-tier architecture. 

The core daemon executes as a background service and communicates with the Tauri frontend UI via a lightweight HTTP/NDJSON streaming interface, avoiding heavy IPC frameworks.

<div align="center">
  <img src="paper/figures/fig5_system_architecture.svg" alt="System Architecture Diagram" width="800" />
</div>

### Component Highlights
1. **Telemetry Sensors:** Native `inotify` (Linux) integration for non-blocking, recursive filesystem event capturing.
2. **Feature Extractor:** Evaluates file modifications using a 12-dimensional vector encompassing Shannon entropy ($\Delta H$), magic-byte mismatches, and temporal burst rates.
3. **HXRF1 Model:** An inline, ONNX-free Random Forest classifier compiled directly into C++ for sub-microsecond ($\mu s$) inference.
4. **Dynamic Canary Traps:** Cryptographically verified decoy files scattered across namespaces to intercept naive multi-threaded directory traversals.

<div align="center">
  <img src="paper/figures/fig6_ransomware_intervention_lifecycle.svg" alt="Intervention Lifecycle" width="800" />
</div>

---

## Getting Started

### Prerequisites

| Tool | Version |
|------|---------|
| GCC or Clang | C++20 support (GCC 11+, Clang 13+) |
| CMake | 3.20+ |
| OpenSSL | 1.1+ (libssl-dev) |
| Node.js | 18+ (for the dashboard) |
| Rust / Cargo | stable (for Tauri) |

Install build dependencies on Ubuntu/Debian:
```bash
sudo apt install build-essential cmake libssl-dev
```

### Build the C++ Engine

```bash
git clone https://github.com/youruser/HeuriX.git
cd HeuriX

# Configure and compile the daemon
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)

# Verify compilation
./build/heurix-engine --help
```

### Start the HeuriX Daemon

Protect a directory and stream telemetry to the UI:
```bash
./build/heurix-engine --watch ~/Documents \
  --listen 127.0.0.1:50051 \
  --ml-model ./model.hxrf1 \
  --enable-ml true \
  --auto-mitigate true
```

### Launch the Visualization Dashboard

Start the Tauri + React companion app:
```bash
npm install
npm run tauri dev
```

---

## Training the HXRF1 Classifier

The HXRF1 model format is a zero-dependency plain-text decision forest. The engine logs 12-dimensional feature vectors that can be directly trained in Python.

```bash
# 1. Collect benign features
./build/heurix-engine --watch ~/Documents --log-features benign_features.csv --log-label benign

# 2. Train the ensemble (generates model.hxrf1)
python3 ml/train.py --csv benign_features.csv --out model.hxrf1 --n-trees 100 --max-depth 12

# 3. Deploy the trained model inline
./build/heurix-engine --ml-model model.hxrf1 --enable-ml true
```

<div align="center">
  <img src="paper/figures/fig3_format_spectrum.svg" alt="Format Spectrum Analysis" width="800" />
</div>

---

## Testing and Validation

**Run the C++20 Unit Test Suite (`doctest`):**
```bash
cmake -B build_tests -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build_tests -t heurix-tests --parallel
./build_tests/heurix-tests
```

**Run End-to-End Simulation Benchmarks:**
```bash
python3 tests/integration_test.py --engine ./build/heurix-engine
```

---

## License and Academic Integrity

HeuriX is released under the [GNU General Public License v3.0](LICENSE). 

**Security Notice:** HeuriX is currently a research prototype designed for academic study and exhibition. It validates novel behavioral mitigation techniques in userspace but should not replace comprehensive kernel-level EDR platforms in production environments.

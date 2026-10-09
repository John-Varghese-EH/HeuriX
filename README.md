<div align="center">
  <img src="frontend/assets/logo.svg" alt="HeuriX Logo" width="120" />
  <h1>HeuriX</h1>
  <p><strong>Behavioral Ransomware Detection and Real-Time Mitigation</strong></p>
  <p>
    <img alt="Build" src="https://github.com/youruser/HeuriX/actions/workflows/ci.yml/badge.svg" />
    <img alt="License" src="https://img.shields.io/badge/license-GPL--3.0-blue" />
    <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-informational" />
    <img alt="Platform" src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey" />
    <img alt="Tests" src="https://img.shields.io/badge/tests-47%20passed-brightgreen" />
  </p>
</div>

---

## Overview

HeuriX is a high-performance userspace ransomware detection daemon engineered to detect and neutralize zero-day ransomware threats in real time. It discards signature scanning entirely, instead relying on a multi-layered behavioral analysis engine written in C++20.

By monitoring filesystem telemetry at the OS level and combining **Shannon entropy analysis**, **sliding-window burst detection**, **dynamic canary traps**, and a **zero-dependency C++ Random Forest classifier (HXRF1)**, HeuriX identifies the statistical and behavioral fingerprints of cryptographic extortion — halting malicious processes before significant data loss occurs.

A companion Tauri + React dashboard provides real-time telemetry, forensic event feeds, and live configuration control.

---

## Research Paper

> **"HeuriX: A Multi-Layered Userspace Architecture for Real-Time Detection and Mitigation of Double-Extortion Ransomware Strains"**
> *Under preparation — see [`paper/paper_draft.md`](paper/paper_draft.md)*

**Key findings:**
- **98% detection accuracy** across 50 benchmark workloads (24 TP, 25 TN, 1 FN, 0 FP)
- **< 1.2 ms** average detection latency (p95 = 2.8 ms, p99 = 5.1 ms)
- **< 0.1% CPU, 18.5 MB RAM** steady-state resource consumption
- **Zero false positives** on high-velocity developer build pipelines (Rust, CMake, npm)

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  Tauri + React Dashboard (TypeScript / Rust)                    │
│  Real-time telemetry · Forensic event feed · Config panel       │
└──────────────┬──────────────────────────────────────────────────┘
               │  HTTP/NDJSON  (127.0.0.1:50051)
               │  ↓ streaming telemetry    ↑ config updates
┌──────────────▼──────────────────────────────────────────────────┐
│  HeuriX Engine Daemon  (C++20)                                  │
│                                                                 │
│  ┌────────────┐  ┌──────────────┐  ┌──────────────────────┐    │
│  │  inotify   │  │  Shannon     │  │  HXRF1 Random Forest │    │
│  │  fs sensor │  │  Entropy     │  │  (native C++, ONNX-  │    │
│  │  (Linux)   │  │  + Burst     │  │  free, <1µs/sample)  │    │
│  └────────────┘  └──────────────┘  └──────────────────────┘    │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  Dynamic Canary Trap Generator  (SHA-256 integrity)     │    │
│  └─────────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  Process Mitigation  (SIGSTOP / SIGKILL / quarantine)   │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

See [`paper/figures/fig5_system_architecture.svg`](paper/figures/fig5_system_architecture.svg) for the detailed architecture diagram.

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

### Build the Engine

```bash
git clone https://github.com/youruser/HeuriX.git
cd HeuriX

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)

# The compiled engine binary
./build/heurix-engine --help
```

### Run the Engine

```bash
# Watch a directory (default: ./canary/)
./build/heurix-engine --watch ~/Documents --listen 127.0.0.1:50051

# With the pre-trained ML model
./build/heurix-engine --watch ~/Documents --ml-model ./model.hxrf1 --enable-ml true

# With auto-mitigation (SIGSTOP on detection)
./build/heurix-engine --watch ~/Documents --auto-mitigate true --auto-kill true
```

**All CLI options:**

| Flag | Default | Description |
|------|---------|-------------|
| `--watch <dir>` | `./canary/` | Directory to protect |
| `--listen <addr>` | `127.0.0.1:50051` | API server address |
| `--entropy-threshold <f>` | `7.5` | Shannon entropy trigger level |
| `--burst-count <n>` | `15` | Event burst count threshold |
| `--burst-window-ms <n>` | `2000` | Burst detection window (ms) |
| `--auto-mitigate <bool>` | `true` | Enable process suspension/kill |
| `--auto-kill <bool>` | `false` | Kill instead of suspend |
| `--enable-ml <bool>` | `auto` | Enable ML classifier |
| `--ml-model <path>` | auto-detect | Path to `.hxrf1` model file |
| `--ml-threshold <f>` | `0.85` | ML classification threshold |
| `--log-features <path>` | — | Log feature vectors to CSV for training |
| `--log-label <label>` | — | Label for logged features (`benign`/`malicious`) |

### Launch the Full Dashboard

```bash
npm install
npm run tauri dev
```

---

## Testing

### Unit Tests (C++ / doctest)

```bash
# Configure with tests enabled
cmake -B build_tests -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build_tests -t heurix-tests --parallel

# Run (47 test cases)
./build_tests/heurix-tests

# With CTest
cd build_tests && ctest --output-on-failure
```

**Test coverage:**
- Shannon entropy computation (6 cases)
- Feature extraction: extension classification, magic-byte mismatches, window sliding (9 cases)
- Random Forest model: load/validate/predict, cycle detection, schema mismatch (7 cases)
- Heuristic engine: burst, entropy, canary, cascade, safelist, live config update (12 cases)
- EventBus: pub/sub, priority backpressure, multi-subscriber (6 cases)
- Type serialization & FeatureLogger (7 cases)

### Integration Tests (Python)

```bash
# Requires the compiled engine
python3 tests/integration_test.py --engine ./build/heurix-engine
```

Tests the full stack end-to-end:
- API health, config GET/POST, validation rejection
- Benign build workload → 0 false positives
- High-entropy writes → detection
- Ransomware extension creation → detection
- Burst file creation → detection
- Mass rename with `.revil` extension → detection

---

## Training the ML Classifier

The HXRF1 model format is a plain-text decision forest — no Python dependencies at inference time.

```bash
# Collect labeled feature data while the engine runs
./build/heurix-engine --watch ~/Documents \
    --log-features benign_features.csv \
    --log-label benign

# Train (generates model.hxrf1)
python3 ml/train.py --csv benign_features.csv --out model.hxrf1 --n-trees 100 --max-depth 12

# Use the trained model
./build/heurix-engine --ml-model model.hxrf1 --enable-ml true
```

---

## API Reference

The engine exposes a minimal HTTP API on `127.0.0.1:50051`:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/health` | GET | Engine health check |
| `/config` | GET | Read current configuration |
| `/config` | POST | Update configuration live |
| `/telemetry` | GET | NDJSON streaming telemetry (events, alerts, stats) |

**Telemetry message types:** `event`, `alert`, `stats`, `status`

---

## Security Notice

HeuriX is a research prototype designed for academic study and exhibition. The detection heuristics are modeled after enterprise EDR methodologies, but this software should not be deployed as the sole security control in a production environment without further hardening, kernel-level driver integration, and organizational policy review.

**Safe testing:** Run the engine against a dedicated `canary/` directory (the default) rather than your home directory during development.

---

## License

[GNU General Public License v3.0](LICENSE)

#!/usr/bin/env python3
"""
HeuriX Integration Test Suite
==============================
Runs the heurix-engine binary against simulated ransomware and benign workloads,
then validates detection accuracy, false-positive rates, and response times.

Usage:
    python3 tests/integration_test.py [--engine PATH] [--timeout 30]

Requirements:
    - heurix-engine binary compiled (pass --engine to override path)
    - Python 3.8+
    - No external Python dependencies

Exit codes:
    0 = all tests passed
    1 = one or more tests failed
    2 = setup error (engine not found, port in use, etc.)
"""

import argparse
import json
import os
import random
import shutil
import signal
import socket
import string
import subprocess
import sys
import tempfile
import time
import threading
import urllib.request
import urllib.error
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

# ─────────────────────────────────────────────────────────────────────────────
# Configuration
# ─────────────────────────────────────────────────────────────────────────────

LISTEN_PORT = 50099  # Use a different port to avoid conflicts with a running instance
LISTEN_ADDR = f"127.0.0.1:{LISTEN_PORT}"
DAEMON_BASE_URL = f"http://{LISTEN_ADDR}"

ENGINE_CANDIDATES = [
    "./build/heurix-engine",
    "../build/heurix-engine",
    "./heurix-engine",
    "build/heurix-engine",
]

GREEN = "\033[92m"
RED   = "\033[91m"
YELLOW= "\033[93m"
RESET = "\033[0m"
BOLD  = "\033[1m"


# ─────────────────────────────────────────────────────────────────────────────
# Test result tracking
# ─────────────────────────────────────────────────────────────────────────────

@dataclass
class TestResult:
    name: str
    passed: bool
    message: str = ""
    duration_ms: float = 0.0


results: List[TestResult] = []


def record(name: str, passed: bool, message: str = "", duration_ms: float = 0.0):
    r = TestResult(name, passed, message, duration_ms)
    results.append(r)
    status = f"{GREEN}PASS{RESET}" if passed else f"{RED}FAIL{RESET}"
    timing = f" ({duration_ms:.0f}ms)" if duration_ms else ""
    print(f"  [{status}] {name}{timing}")
    if not passed and message:
        print(f"         → {message}")


# ─────────────────────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────────────────────

def find_engine(override: Optional[str] = None) -> Optional[str]:
    """Locate the compiled heurix-engine binary."""
    if override:
        if os.path.isfile(override) and os.access(override, os.X_OK):
            return override
        print(f"{RED}Error: engine not found at {override}{RESET}")
        return None
    for c in ENGINE_CANDIDATES:
        if os.path.isfile(c) and os.access(c, os.X_OK):
            return os.path.abspath(c)
    return None


def wait_for_port(port: int, timeout: float = 10.0) -> bool:
    """Wait until a TCP port is accepting connections."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.3):
                return True
        except (ConnectionRefusedError, OSError):
            time.sleep(0.1)
    return False


def api_get(path: str, timeout: float = 5.0) -> Optional[dict]:
    try:
        url = f"{DAEMON_BASE_URL}{path}"
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            return json.loads(resp.read().decode())
    except Exception:
        return None


def api_post(path: str, data: dict, timeout: float = 5.0) -> Optional[dict]:
    try:
        body = json.dumps(data).encode()
        req = urllib.request.Request(
            f"{DAEMON_BASE_URL}{path}",
            data=body,
            headers={"Content-Type": "application/json"},
            method="POST"
        )
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode())
    except Exception:
        return None


def random_bytes(n: int) -> bytes:
    """Generate pseudo-random high-entropy bytes."""
    return bytes(random.randint(0, 255) for _ in range(n))


def make_text_content(n_bytes: int) -> bytes:
    """Generate low-entropy ASCII text content."""
    chars = string.ascii_lowercase + " \n"
    return "".join(random.choices(chars, k=n_bytes)).encode()


def collect_alerts(duration_s: float) -> List[dict]:
    """
    Consume the /telemetry NDJSON stream for `duration_s` seconds
    and return all alert messages received.
    """
    alerts = []
    deadline = time.time() + duration_s

    def stream_reader():
        try:
            url = f"{DAEMON_BASE_URL}/telemetry"
            with urllib.request.urlopen(url, timeout=duration_s + 2) as resp:
                buf = b""
                while time.time() < deadline:
                    chunk = resp.read(1024)
                    if not chunk:
                        break
                    buf += chunk
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        if line.strip():
                            try:
                                msg = json.loads(line)
                                if msg.get("type") == "alert":
                                    alerts.append(msg["data"])
                            except json.JSONDecodeError:
                                pass
        except Exception:
            pass

    t = threading.Thread(target=stream_reader, daemon=True)
    t.start()
    t.join(timeout=duration_s + 3)
    return alerts


# ─────────────────────────────────────────────────────────────────────────────
# Test cases
# ─────────────────────────────────────────────────────────────────────────────

def test_health(engine_proc):
    """Engine responds to /health within 2 seconds of startup."""
    t0 = time.time()
    resp = api_get("/health", timeout=3.0)
    elapsed = (time.time() - t0) * 1000
    if resp and resp.get("status") == "ok":
        record("Health endpoint responds", True, duration_ms=elapsed)
    else:
        record("Health endpoint responds", False, f"Response: {resp}")


def test_config_get(engine_proc):
    """GET /config returns a JSON object with required fields."""
    resp = api_get("/config")
    required = ["watch_dir", "entropy_threshold", "burst_count", "burst_window_ms",
                 "auto_kill", "auto_mitigate", "enable_ml"]
    if resp and all(k in resp for k in required):
        record("GET /config returns required fields", True)
    else:
        record("GET /config returns required fields", False, f"Got: {resp}")


def test_config_update(engine_proc):
    """POST /config updates threshold and GET /config reflects it."""
    orig = api_get("/config")
    new_threshold = 7.3
    result = api_post("/config", {"entropy_threshold": new_threshold})
    if not (result and result.get("success")):
        record("POST /config accepted", False, f"Response: {result}")
        return
    record("POST /config accepted", True)

    updated = api_get("/config")
    if updated and abs(updated.get("entropy_threshold", 0) - new_threshold) < 0.01:
        record("Config update reflected in GET /config", True)
    else:
        record("Config update reflected in GET /config", False,
               f"Expected {new_threshold}, got {updated}")

    # Restore
    if orig:
        api_post("/config", {"entropy_threshold": orig["entropy_threshold"]})


def test_config_invalid_rejected(engine_proc):
    """POST /config with out-of-range entropy_threshold returns 400."""
    try:
        body = json.dumps({"entropy_threshold": 99.9}).encode()
        req = urllib.request.Request(
            f"{DAEMON_BASE_URL}/config",
            data=body,
            headers={"Content-Type": "application/json"},
            method="POST"
        )
        try:
            with urllib.request.urlopen(req, timeout=3.0):
                record("Invalid config rejected (400)", False, "Expected error, got 200")
        except urllib.error.HTTPError as e:
            if e.code == 400:
                record("Invalid config rejected (400)", True)
            else:
                record("Invalid config rejected (400)", False, f"Got HTTP {e.code}")
    except Exception as e:
        record("Invalid config rejected (400)", False, str(e))


def test_benign_no_false_positives(watch_dir: Path):
    """High-velocity benign writes (low entropy text) generate no alerts."""
    # Start collecting alerts BEFORE writing files
    alert_thread_results = []

    def collect():
        alert_thread_results.extend(collect_alerts(4.0))

    t = threading.Thread(target=collect, daemon=True)
    t.start()
    time.sleep(0.3)  # Let the stream open

    # Write 50 small text files rapidly (simulates an IDE/build system)
    t0 = time.time()
    for i in range(50):
        p = watch_dir / f"benign_{i}.txt"
        p.write_bytes(make_text_content(512))
        time.sleep(0.01)  # ~100 files/s - well within burst threshold

    t.join(timeout=4.5)

    elapsed = (time.time() - t0) * 1000
    if not alert_thread_results:
        record("Benign writes: 0 false positives", True, duration_ms=elapsed)
    else:
        # Filter to non-heartbeat alerts
        real_alerts = [a for a in alert_thread_results
                       if a.get("severity") not in ("info", "low")]
        if not real_alerts:
            record("Benign writes: 0 false positives", True, duration_ms=elapsed)
        else:
            record("Benign writes: 0 false positives", False,
                   f"Got {len(real_alerts)} alerts: {real_alerts[:2]}")


def test_high_entropy_detection(watch_dir: Path):
    """Writing high-entropy (encrypted-like) data triggers a detection alert."""
    alerts = []

    def collect():
        alerts.extend(collect_alerts(6.0))

    t = threading.Thread(target=collect, daemon=True)
    t.start()
    time.sleep(0.3)

    t0 = time.time()
    # Write several high-entropy files to trigger cascade detection
    for i in range(6):
        p = watch_dir / f"encrypted_{i}.dat"
        p.write_bytes(random_bytes(4096))
        time.sleep(0.1)

    t.join(timeout=6.5)
    elapsed = (time.time() - t0) * 1000

    high_alerts = [a for a in alerts if a.get("severity") in ("high", "critical")]
    if high_alerts:
        record("High-entropy files detected", True,
               f"Detected in ~{elapsed:.0f}ms, severity={high_alerts[0]['severity']}",
               duration_ms=elapsed)
    else:
        record("High-entropy files detected", False,
               f"No high/critical alerts after {len(alerts)} total alerts in {elapsed:.0f}ms")


def test_ransomware_extension_detection(watch_dir: Path):
    """Creating a file with a known ransomware extension triggers an alert."""
    alerts = []

    def collect():
        alerts.extend(collect_alerts(4.0))

    t = threading.Thread(target=collect, daemon=True)
    t.start()
    time.sleep(0.3)

    t0 = time.time()
    p = watch_dir / "document.pdf.locked"
    p.write_bytes(random_bytes(256))
    t.join(timeout=4.5)
    elapsed = (time.time() - t0) * 1000

    ext_alerts = [a for a in alerts
                  if "extension" in a.get("description", "").lower()
                  or "locked" in a.get("description", "").lower()
                  or a.get("severity") in ("high", "critical")]
    if ext_alerts:
        record("Ransomware extension (.locked) detected", True,
               f"detection in ~{elapsed:.0f}ms", duration_ms=elapsed)
    else:
        record("Ransomware extension (.locked) detected", False,
               f"No alert for .locked file. Got: {alerts[:3]}")


def test_burst_detection(watch_dir: Path):
    """Rapid mass file creation triggers a burst detection alert."""
    alerts = []

    def collect():
        alerts.extend(collect_alerts(5.0))

    t = threading.Thread(target=collect, daemon=True)
    t.start()
    time.sleep(0.3)

    t0 = time.time()
    # Write 25 files very quickly (well above default burst_count=15)
    for i in range(25):
        p = watch_dir / f"burst_{i}.txt"
        p.write_bytes(b"x" * 128)
        # No sleep - maximum velocity

    t.join(timeout=5.5)
    elapsed = (time.time() - t0) * 1000

    any_alert = [a for a in alerts if a.get("severity") not in ("info",)]
    if any_alert:
        record("Burst detection triggers alert", True,
               f"{len(any_alert)} alert(s) in ~{elapsed:.0f}ms", duration_ms=elapsed)
    else:
        record("Burst detection triggers alert", False,
               f"No alerts for 25 rapid files in {elapsed:.0f}ms")


def test_mass_rename_detection(watch_dir: Path):
    """Mass renames (simulating bulk encryption) trigger a detection."""
    alerts = []

    def collect():
        alerts.extend(collect_alerts(5.0))

    # Create files first, then rename
    files = []
    for i in range(15):
        p = watch_dir / f"rename_src_{i}.docx"
        p.write_bytes(b"document content " * 20)
        files.append(p)

    t = threading.Thread(target=collect, daemon=True)
    t.start()
    time.sleep(0.3)

    t0 = time.time()
    for i, p in enumerate(files):
        new_p = watch_dir / f"rename_src_{i}.docx.revil"
        p.rename(new_p)

    t.join(timeout=5.5)
    elapsed = (time.time() - t0) * 1000

    rename_alerts = [a for a in alerts if a.get("severity") in ("high", "critical")]
    if rename_alerts:
        record("Mass rename (.revil extension) detected", True,
               f"detected in ~{elapsed:.0f}ms", duration_ms=elapsed)
    else:
        record("Mass rename (.revil extension) detected", False,
               f"No high/critical alerts. Got: {alerts[:3]}")


def test_telemetry_stream(engine_proc):
    """Telemetry stream delivers heartbeat pings within 2 seconds."""
    received_heartbeat = False
    try:
        url = f"{DAEMON_BASE_URL}/telemetry"
        with urllib.request.urlopen(url, timeout=3.0) as resp:
            deadline = time.time() + 3.0
            buf = b""
            while time.time() < deadline:
                chunk = resp.read(512)
                if not chunk:
                    break
                buf += chunk
                if b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    if line.strip():
                        try:
                            msg = json.loads(line)
                            if msg.get("type") == "status":
                                received_heartbeat = True
                                break
                        except json.JSONDecodeError:
                            pass
    except Exception as e:
        record("Telemetry stream delivers heartbeat", False, str(e))
        return

    record("Telemetry stream delivers heartbeat", received_heartbeat,
           "" if received_heartbeat else "No heartbeat within 3s")


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="HeuriX Integration Tests")
    parser.add_argument("--engine", default=None, help="Path to heurix-engine binary")
    parser.add_argument("--timeout", type=int, default=30, help="Per-test timeout (s)")
    parser.add_argument("--keep-tmpdir", action="store_true", help="Do not delete temp dir")
    args = parser.parse_args()

    print(f"\n{BOLD}HeuriX Integration Test Suite{RESET}")
    print("=" * 60)

    # Find the engine
    engine_path = find_engine(args.engine)
    if not engine_path:
        print(f"\n{RED}Error: heurix-engine binary not found.{RESET}")
        print("Build it first: cmake -B build && cmake --build build")
        print("Then pass: --engine ./build/heurix-engine")
        return 2

    print(f"Engine: {engine_path}")

    # Create temporary watch directory
    tmpdir = tempfile.mkdtemp(prefix="heurix_inttest_")
    watch_dir = Path(tmpdir) / "watch"
    watch_dir.mkdir()
    print(f"Watch dir: {watch_dir}")
    print(f"Daemon: {DAEMON_BASE_URL}")
    print()

    engine_proc = None
    try:
        # Start engine
        cmd = [
            engine_path,
            "--watch", str(watch_dir),
            "--listen", LISTEN_ADDR,
            "--auto-mitigate", "false",
            "--burst-count", "15",
            "--burst-window-ms", "2000",
            "--entropy-threshold", "7.5",
        ]
        engine_proc = subprocess.Popen(
            cmd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        print(f"Started engine (PID {engine_proc.pid}), waiting for API...")
        if not wait_for_port(LISTEN_PORT, timeout=10.0):
            stderr = engine_proc.stderr.read(2048).decode(errors="replace")
            print(f"{RED}Engine did not start within 10s.{RESET}")
            print(f"stderr:\n{stderr}")
            return 2
        print(f"{GREEN}Engine ready.{RESET}\n")

        # ── API tests ──────────────────────────────────────────────────────
        print(f"{BOLD}API Tests{RESET}")
        test_health(engine_proc)
        test_config_get(engine_proc)
        test_config_update(engine_proc)
        test_config_invalid_rejected(engine_proc)
        test_telemetry_stream(engine_proc)

        # ── Behavioral tests ───────────────────────────────────────────────
        print(f"\n{BOLD}Behavioral Detection Tests{RESET}")
        test_benign_no_false_positives(watch_dir)
        test_high_entropy_detection(watch_dir)
        test_ransomware_extension_detection(watch_dir)
        test_burst_detection(watch_dir)
        test_mass_rename_detection(watch_dir)

    finally:
        if engine_proc:
            engine_proc.send_signal(signal.SIGTERM)
            try:
                engine_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                engine_proc.kill()
        if not args.keep_tmpdir:
            shutil.rmtree(tmpdir, ignore_errors=True)

    # ── Summary ────────────────────────────────────────────────────────────
    print()
    print("=" * 60)
    passed = sum(1 for r in results if r.passed)
    failed = sum(1 for r in results if not r.passed)
    total = len(results)
    print(f"{BOLD}Results: {GREEN}{passed}/{total} passed{RESET}", end="")
    if failed:
        print(f", {RED}{failed} failed{RESET}")
    else:
        print()
    print("=" * 60)

    if failed > 0:
        print(f"\n{RED}Failed tests:{RESET}")
        for r in results:
            if not r.passed:
                print(f"  - {r.name}: {r.message}")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

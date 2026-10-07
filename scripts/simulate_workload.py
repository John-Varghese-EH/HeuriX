#!/usr/bin/env python3
"""
HeuriX Workload & Attack Simulator

Usage:
    python3 scripts/simulate_workload.py --mode benign --target-dir ./test_dir --count 50
    python3 scripts/simulate_workload.py --mode attack --target-dir ./test_dir --count 50
"""

import argparse
import os
import random
import string
import time

def generate_random_bytes(length):
    return bytes(random.getrandbits(8) for _ in range(length))

def generate_text_content(lines=20):
    words = ["heurix", "security", "enterprise", "userspace", "daemon", "protection", "telemetry", "random_forest", "entropy", "event"]
    content = []
    for _ in range(lines):
        line = " ".join(random.choices(words, k=10))
        content.append(line)
    return "\n".join(content)

def run_benign_simulation(target_dir, count):
    print(f"[*] Starting BENIGN workload simulation in {target_dir} ({count} operations)...")
    os.makedirs(target_dir, exist_ok=True)
    
    extensions = [".txt", ".md", ".json", ".log", ".cpp", ".py"]
    created_files = []

    for i in range(count):
        op = random.choice(["create", "modify", "read"])
        
        if op == "create" or not created_files:
            fname = f"doc_{i}_{random.randint(100, 999)}{random.choice(extensions)}"
            fpath = os.path.join(target_dir, fname)
            with open(fpath, "w") as f:
                f.write(generate_text_content(random.randint(5, 30)))
            created_files.append(fpath)
            print(f"  [+] Created text file: {fname}")
            
        elif op == "modify":
            fpath = random.choice(created_files)
            if os.path.exists(fpath):
                with open(fpath, "a") as f:
                    f.write("\n// Added new log line\n" + generate_text_content(2))
                print(f"  [~] Modified file: {os.path.basename(fpath)}")

        time.sleep(random.uniform(0.05, 0.2)) # Normal user/build delay

def run_attack_simulation(target_dir, count):
    print(f"[!] Starting RANSOMWARE ATTACK simulation in {target_dir} ({count} encryptions)...")
    os.makedirs(target_dir, exist_ok=True)
    
    # 1. Create target victim files first
    victim_files = []
    for i in range(count):
        fpath = os.path.join(target_dir, f"important_user_doc_{i}.pdf")
        with open(fpath, "wb") as f:
            f.write(b"%PDF-1.5 header\n" + generate_random_bytes(500))
        victim_files.append(fpath)

    time.sleep(0.5)
    print(f"[!] Simulating rapid high-entropy bulk encryption & ransom extension appends...")

    # 2. Simulate rapid ransomware burst: replace content with high-entropy pseudo-random bytes & rename
    for fpath in victim_files:
        if os.path.exists(fpath):
            encrypted_bytes = generate_random_bytes(4096)
            with open(fpath, "wb") as f:
                f.write(encrypted_bytes)
            
            # Append ransomware extension
            new_fpath = fpath + ".locked"
            os.rename(fpath, new_fpath)
            print(f"  [!] Encrypted & Renamed: {os.path.basename(fpath)} -> {os.path.basename(new_fpath)}")
        
        time.sleep(0.01) # Rapid burst velocity!

def main():
    parser = argparse.ArgumentParser(description="HeuriX Workload Simulator")
    parser.add_argument("--mode", choices=["benign", "attack"], required=True, help="Simulation mode")
    parser.add_argument("--target-dir", default="./canary", help="Target directory for operations")
    parser.add_argument("--count", type=int, default=30, help="Number of file operations")
    args = parser.parse_args()

    if args.mode == "benign":
        run_benign_simulation(args.target_dir, args.count)
    else:
        run_attack_simulation(args.target_dir, args.count)

if __name__ == "__main__":
    main()

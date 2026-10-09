#!/usr/bin/env bash
# HeuriX Live Demonstration Script
# Run this during your presentation to trigger a simulated ransomware attack!

echo -e "\033[1;36m[HeuriX Demo]\033[0m Preparing live demonstration environment..."

cd "$(dirname "$0")/.."

# 1. Ask the live daemon what directory it is currently watching
WATCH_DIR=$(curl -s http://127.0.0.1:50051/config | grep -o '"watch_dir":"[^"]*' | grep -o '[^"]*$')

if [ -z "$WATCH_DIR" ]; then
    echo -e "\033[1;31m[!] Error:\033[0m Could not connect to the HeuriX daemon. Is it running?"
    exit 1
fi

echo -e "\033[1;36m[HeuriX Demo]\033[0m Daemon is currently protecting: $WATCH_DIR"

# 2. Create a specific test folder inside the currently watched directory
TARGET_DIR="$WATCH_DIR/heurix_live_demo"
mkdir -p "$TARGET_DIR"

echo -e "\033[1;33m[HeuriX Demo]\033[0m Initiating simulated Ransomware burst on $TARGET_DIR..."
echo "Watch your HeuriX dashboard for live ML detection alerts and high-entropy spikes!"
sleep 2

# 3. Execute the workload simulator in attack mode directly on the watched path
python3 scripts/simulate_workload.py --mode attack --target-dir "$TARGET_DIR" --count 100

echo -e "\033[1;32m[HeuriX Demo]\033[0m Attack simulation complete. Check the UI!"

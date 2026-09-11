#!/usr/bin/env bash
# Sync C++ engine binary to Tauri sidecar binaries
# Run after any engine rebuild: cmake --build build -j && ./scripts/sync-sidecar.sh

set -euo pipefail

REPO_ROOT="/home/j0x/Documents/GitHub/HeuriX"
BUILD_DIR="$REPO_ROOT/build"
ENGINE_BIN="$BUILD_DIR/heurix-engine"
SIDECAR_DIR="$REPO_ROOT/src-tauri/binaries"
SIDECAR_BIN="$SIDECAR_DIR/heurix-engine-x86_64-unknown-linux-gnu"

if [[ ! -f "$ENGINE_BIN" ]]; then
    echo "ERROR: Engine binary not found at $ENGINE_BIN"
    echo "Run: cmake -B build -S . && cmake --build build -j"
    exit 1
fi

mkdir -p "$SIDECAR_DIR"
cp "$ENGINE_BIN" "$SIDECAR_BIN"
chmod +x "$SIDECAR_BIN"

echo "Synced: $ENGINE_BIN -> $SIDECAR_BIN"
ls -lh "$SIDECAR_BIN"

#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "============================================"
echo " Jarvis-Cpp Build & Deploy"
echo " Target: Proxmox LXC / Ubuntu 24.04"
echo "============================================"
echo ""

echo "[1/5] Checking system dependencies..."
NEED_INSTALL=false

for cmd in cmake g++ git curl python3; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        NEED_INSTALL=true
        break
    fi
done

if [ ! -f /usr/include/sqlite3.h ] || [ ! -f /usr/include/curl/curl.h ]; then
    NEED_INSTALL=true
fi

if [ "$NEED_INSTALL" = true ]; then
    echo "  Installing build dependencies..."
    apt-get update -qq
    apt-get install -y -qq \
        cmake \
        g++ \
        git \
        python3 \
        libcurl4-openssl-dev \
        libsqlite3-dev \
        libssl-dev \
        zlib1g-dev
fi

if ! command -v node >/dev/null 2>&1; then
    echo "  Installing Node.js..."
    curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
    apt-get install -y -qq nodejs
fi

echo "  Dependencies OK"
echo ""

echo "[2/5] Building React frontend..."
cd "$SCRIPT_DIR/jarvis-cpp-academic-hub"
if [ ! -d node_modules ]; then
    echo "  Running npm install..."
    npm install --silent
fi
npm run build --silent
echo "  Frontend built: dist/"
echo ""

echo "[3/5] Copying frontend build to server directory..."
cd "$SCRIPT_DIR"
rm -rf build
cp -r jarvis-cpp-academic-hub/dist build
echo "  Copied frontend bundle to ./build/"
echo ""

echo "[4/5] Building C++ server..."
cmake -B build_cpp -S . -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -5
cmake --build build_cpp --parallel "$(nproc)" 2>&1 | tail -10
echo "  Server binary: build_cpp/jarvis-server"
echo ""

echo "[5/5] Verifying config and starting server..."
CONFIG_FILE=""
if [ -f config.local.json ]; then
    CONFIG_FILE="config.local.json"
else
    if [ -t 0 ]; then
        python3 scripts/setup_local_config.py --config config.local.json
    else
        python3 scripts/setup_local_config.py --config config.local.json --write-defaults
    fi
    CONFIG_FILE="config.local.json"
    echo "  Created ${CONFIG_FILE} with the setup wizard"
fi

echo "  Config file: ${CONFIG_FILE}"
echo "  Proxmox host: $(grep -o '\"host\": \"[^\"]*\"' "${CONFIG_FILE}" | head -1)"
echo "  SQLite DB: jarvis.db"
echo "  Listening on: http://0.0.0.0:8080"
echo ""

exec ./build_cpp/jarvis-server

#!/bin/bash
# setup_pi.sh — Build RCCService on Raspberry Pi OS (64-bit, ARM64)
# Run this on the Pi. It installs deps, clones the repo, sets up vcpkg, and builds.
#
# Usage (on Pi):
#   curl -fsSL https://raw.githubusercontent.com/<your-user>/projectpizzacomplete/main/setup_pi.sh | bash
# OR clone first then:
#   chmod +x setup_pi.sh && ./setup_pi.sh

set -e

REPO_URL="https://github.com/lmaobamar/projectpizzacomplete"
REPO_DIR="$HOME/projectpizzacomplete"
VCPKG_DIR="$HOME/vcpkg"

echo ""
echo "======================================================"
echo "  ProjectPizza — RCCService build for Raspberry Pi"
echo "======================================================"
echo ""

# ── 1. System dependencies ─────────────────────────────────────────────────
echo "[1/5] Installing system packages..."
sudo apt-get update -qq
sudo apt-get install -y \
    git cmake ninja-build \
    clang \
    python3 python3-pip \
    curl zip unzip tar \
    pkg-config \
    libssl-dev \
    libx11-dev libxrandr-dev libxi-dev \
    nasm \
    gperf \
    autoconf automake libtool

echo "      Done."

# ── 2. vcpkg ───────────────────────────────────────────────────────────────
echo "[2/5] Setting up vcpkg..."
if [ ! -d "$VCPKG_DIR" ]; then
    git clone https://github.com/microsoft/vcpkg.git "$VCPKG_DIR"
fi
cd "$VCPKG_DIR"
git pull --quiet
./bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT="$VCPKG_DIR"
echo "      Done. VCPKG_ROOT=$VCPKG_ROOT"

# ── 3. Clone / update the project ─────────────────────────────────────────
echo "[3/5] Getting projectpizzacomplete..."
if [ ! -d "$REPO_DIR" ]; then
    git clone "$REPO_URL" "$REPO_DIR"
else
    echo "      Repo already exists, pulling latest..."
    cd "$REPO_DIR" && git pull
fi
cd "$REPO_DIR"
echo "      Done."

# ── 4. CMake configure ────────────────────────────────────────────────────
echo "[4/5] Configuring (this downloads + compiles vcpkg deps — ~30-60 min first time)..."
cmake --preset pi-rcc-release
echo "      Configure done."

# ── 5. Build ──────────────────────────────────────────────────────────────
echo "[5/5] Building RCCService..."
# Use all available cores
CORES=$(nproc)
cmake --build build/pi-rcc-release --parallel "$CORES"

echo ""
echo "======================================================"
echo "  Build complete!"
echo ""
echo "  Binary: $REPO_DIR/build/pi-rcc-release/RCCService/RCCService"
echo ""
echo "  To run:"
echo "    cd $REPO_DIR/build/pi-rcc-release"
echo "    ./RCCService/RCCService"
echo ""
echo "  RCCService listens on port 64989 (SOAP) and starts"
echo "  a RakNet UDP server when a game job is opened."
echo ""
echo "  Then on your Windows machine run:"
echo "    python ws_bridge.py --server <pi-ip>:53640 --port 9000"
echo "======================================================"

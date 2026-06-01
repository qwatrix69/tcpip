#!/usr/bin/env bash
#
# build.sh — сборка TCP/IP бенчмарка.
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build"

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j

echo "Binaries:"
echo "  $BUILD_DIR/tcp_bench_server"
echo "  $BUILD_DIR/tcp_bench_client"

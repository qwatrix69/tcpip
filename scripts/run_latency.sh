#!/usr/bin/env bash
#
# run_latency.sh — серия Б: задержка RTT (p50/p99/p999) методом ping-pong.
#
# Запуск (на двух узлах):
#   Сервер:  ./run_latency.sh server
#   Клиент:  ./run_latency.sh client <SERVER_IP>
#
set -euo pipefail

ROLE="${1:?usage: run_latency.sh <server|client> [server_ip]}"
SERVER_IP="${2:-127.0.0.1}"
PORT=5557

BIN_DIR="$(cd "$(dirname "$0")/../build" && pwd)"
RESULTS_DIR="$(cd "$(dirname "$0")/.." && pwd)/results"
mkdir -p "$RESULTS_DIR"

ITERS=10000

if [[ "$ROLE" == "server" ]]; then
    echo "=== [SERVER] ping-pong, ${ITERS} iters ==="
    "$BIN_DIR/tcp_bench_server" \
        --port "$PORT" --ping-pong --iters "$ITERS"
elif [[ "$ROLE" == "client" ]]; then
    echo "mode,side,buf_kb,num_buf,T_gbps,S_calls_s,M_gbps,p50_us,p99_us,cpu_pct" \
        > "$RESULTS_DIR/latency_client.csv"
    echo "=== [CLIENT] ping-pong, ${ITERS} iters ==="
    "$BIN_DIR/tcp_bench_client" \
        --addr "$SERVER_IP" --port "$PORT" --ping-pong --iters "$ITERS" --csv \
        >> "$RESULTS_DIR/latency_client.csv"
    echo "Latency results -> $RESULTS_DIR/latency_client.csv"
else
    echo "Unknown role: $ROLE"; exit 1
fi
